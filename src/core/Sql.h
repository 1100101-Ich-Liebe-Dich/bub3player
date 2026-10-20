// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Minimal RAII wrapper around the SQLite C API.
// Each thread owns its own connection; statements never cross threads.

#include <sqlite3.h>
#include <QByteArray>
#include <QString>
#include <QVariant>
#include <stdexcept>
#include <string>

class SqlError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class Stmt
{
public:
    Stmt() = default;
    Stmt(sqlite3 *db, const char *sql) { prepare(db, sql); }
    Stmt(sqlite3 *db, const QString &sql) { prepare(db, sql); }
    ~Stmt() { finalize(); }
    Stmt(const Stmt &) = delete;
    Stmt &operator=(const Stmt &) = delete;
    Stmt(Stmt &&o) noexcept : m_s(o.m_s) { o.m_s = nullptr; }
    Stmt &operator=(Stmt &&o) noexcept
    {
        if (this != &o) {
            finalize();
            m_s = o.m_s;
            o.m_s = nullptr;
        }
        return *this;
    }

    void prepare(sqlite3 *db, const char *sql)
    {
        finalize();
        if (sqlite3_prepare_v2(db, sql, -1, &m_s, nullptr) != SQLITE_OK)
            throw SqlError(std::string(sqlite3_errmsg(db)) + " in: " + sql);
    }
    void prepare(sqlite3 *db, const QString &sql)
    {
        const QByteArray b = sql.toUtf8();
        prepare(db, b.constData());
    }

    Stmt &bind(int i, int v) { sqlite3_bind_int(m_s, i, v); return *this; }
    Stmt &bind(int i, qint64 v) { sqlite3_bind_int64(m_s, i, v); return *this; }
    Stmt &bind(int i, double v) { sqlite3_bind_double(m_s, i, v); return *this; }
    // A null QString binds as '' (columns are NOT NULL); use bindNull() for SQL NULL.
    Stmt &bind(int i, const QString &v)
    {
        const QByteArray b = v.toUtf8();
        sqlite3_bind_text(m_s, i, b.constData(), b.size(), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt &bind(int i, const QVariant &v)
    {
        switch (v.typeId()) {
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::Bool:
            return bind(i, v.toInt());
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::Long:
            return bind(i, v.toLongLong());
        case QMetaType::Double:
        case QMetaType::Float:
            return bind(i, v.toDouble());
        default:
            if (!v.isValid() || v.isNull()) {
                sqlite3_bind_null(m_s, i);
                return *this;
            }
            return bind(i, v.toString());
        }
    }
    Stmt &bindNull(int i) { sqlite3_bind_null(m_s, i); return *this; }

    // Returns true while a row is available.
    bool step()
    {
        const int rc = sqlite3_step(m_s);
        if (rc == SQLITE_ROW)
            return true;
        if (rc == SQLITE_DONE)
            return false;
        throw SqlError(sqlite3_errmsg(sqlite3_db_handle(m_s)));
    }
    // Run a statement that returns no rows, then make it reusable.
    void exec()
    {
        step();
        reset();
    }
    void reset()
    {
        sqlite3_reset(m_s);
        sqlite3_clear_bindings(m_s);
    }

    qint64 i64(int c) const { return sqlite3_column_int64(m_s, c); }
    int i32(int c) const { return sqlite3_column_int(m_s, c); }
    double dbl(int c) const { return sqlite3_column_double(m_s, c); }
    bool isNull(int c) const { return sqlite3_column_type(m_s, c) == SQLITE_NULL; }
    QString str(int c) const
    {
        const auto *t = reinterpret_cast<const char *>(sqlite3_column_text(m_s, c));
        const int n = sqlite3_column_bytes(m_s, c);
        return t ? QString::fromUtf8(t, n) : QString();
    }
    QVariant var(int c) const
    {
        switch (sqlite3_column_type(m_s, c)) {
        case SQLITE_INTEGER: return QVariant::fromValue<qint64>(i64(c));
        case SQLITE_FLOAT: return dbl(c);
        case SQLITE_NULL: return {};
        default: return str(c);
        }
    }

    sqlite3_stmt *handle() const { return m_s; }
    explicit operator bool() const { return m_s != nullptr; }

private:
    void finalize()
    {
        if (m_s)
            sqlite3_finalize(m_s);
        m_s = nullptr;
    }
    sqlite3_stmt *m_s = nullptr;
};

// Owns one connection. Not copyable; one per thread.
class DbConnection
{
public:
    DbConnection() = default;
    explicit DbConnection(sqlite3 *db) : m_db(db) {}
    ~DbConnection() { close(); }
    DbConnection(const DbConnection &) = delete;
    DbConnection &operator=(const DbConnection &) = delete;
    DbConnection(DbConnection &&o) noexcept : m_db(o.m_db) { o.m_db = nullptr; }
    DbConnection &operator=(DbConnection &&o) noexcept
    {
        if (this != &o) {
            close();
            m_db = o.m_db;
            o.m_db = nullptr;
        }
        return *this;
    }

    sqlite3 *get() const { return m_db; }
    operator sqlite3 *() const { return m_db; }
    bool isOpen() const { return m_db != nullptr; }

    void exec(const char *sql)
    {
        char *err = nullptr;
        if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err ? err : "unknown error";
            sqlite3_free(err);
            throw SqlError(msg + " in: " + sql);
        }
    }
    qint64 scalar(const char *sql)
    {
        Stmt s(m_db, sql);
        return s.step() ? s.i64(0) : 0;
    }
    void close()
    {
        if (m_db)
            sqlite3_close_v2(m_db);
        m_db = nullptr;
    }

private:
    sqlite3 *m_db = nullptr;
};

// Commits on destruction unless rolled back; batches many writes into one fsync.
class Transaction
{
public:
    explicit Transaction(DbConnection &db) : m_db(db) { m_db.exec("BEGIN IMMEDIATE"); }
    ~Transaction()
    {
        if (!m_done) {
            try { m_db.exec("COMMIT"); } catch (...) { }
        }
    }
    void commit()
    {
        m_db.exec("COMMIT");
        m_done = true;
    }
    void rollback()
    {
        m_db.exec("ROLLBACK");
        m_done = true;
    }

private:
    DbConnection &m_db;
    bool m_done = false;
};
