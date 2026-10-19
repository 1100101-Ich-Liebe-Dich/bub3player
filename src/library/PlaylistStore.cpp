#include "library/PlaylistStore.h"

#include "core/Database.h"
#include "library/GroupModel.h"
#include "library/Library.h"
#include "library/QueryBuilder.h"
#include "library/TrackCache.h"
#include "library/TrackListModel.h"
#include "player/PlayQueue.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QtConcurrent>

#include <algorithm>

PlaylistStore *PlaylistStore::s_instance = nullptr;

namespace {

const QChar Sep(0x1f); // hint fields: path, title, artist, album, length (ms)

struct Hint
{
    QString path, title, artist, album;
    qint64 durationMs = 0;
};

Hint parseHint(const QString &s)
{
    const QStringList f = s.split(Sep);
    Hint h;
    h.path = f.value(0);
    h.title = f.value(1);
    h.artist = f.value(2);
    h.album = f.value(3);
    h.durationMs = f.value(4).toLongLong();
    return h;
}

QString makeHint(const Hint &h)
{
    auto clean = [](QString v) { return v.remove(Sep); };
    return QStringList{clean(h.path), clean(h.title), clean(h.artist), clean(h.album), QString::number(h.durationMs)}.join(Sep);
}

QString de(qint64 n)
{
    return QLocale(QLocale::German).toString(n);
}

QString titles(qint64 n)
{
    return n == 1 ? QStringLiteral("1 Titel") : QStringLiteral("%1 Titel").arg(de(n));
}

// Only what describes a smart playlist; everything else (search, seed ...) is per view.
QVariantMap cleanRules(const QVariantMap &in)
{
    QVariantMap r;
    r.insert(QStringLiteral("match"), in.value(QStringLiteral("match")).toString() == QLatin1String("any")
                                          ? QStringLiteral("any") : QStringLiteral("all"));
    r.insert(QStringLiteral("filters"), in.value(QStringLiteral("filters")).toList());
    r.insert(QStringLiteral("sort"), in.value(QStringLiteral("sort"), QStringLiteral("album")).toString());
    r.insert(QStringLiteral("desc"), in.value(QStringLiteral("desc")).toBool());
    const int limit = in.value(QStringLiteral("limit")).toInt();
    if (limit > 0)
        r.insert(QStringLiteral("limit"), limit);
    return r;
}

// Local track by full path (folder path + file name, both indexed).
qint64 trackByPath(Stmt &q, const QString &path)
{
    const int slash = int(path.lastIndexOf(QLatin1Char('/')));
    if (slash <= 0)
        return 0;
    q.bind(1, path.left(slash)).bind(2, path.mid(slash + 1));
    const qint64 id = q.step() ? q.i64(0) : 0;
    q.reset();
    return id;
}

// Same song by title and artist: local first, then the closest length.
qint64 trackByName(Stmt &q, const Hint &h)
{
    if (h.title.isEmpty())
        return 0;
    q.bind(1, h.title).bind(2, h.artist).bind(3, h.album).bind(4, h.durationMs);
    qint64 id = 0;
    while (q.step()) {
        const qint64 len = q.i64(1);
        if (h.durationMs > 0 && len > 0 && std::abs(len - h.durationMs) > 10000)
            continue; // a different version (live, remix ...)
        id = q.i64(0);
        break;
    }
    q.reset();
    return id;
}

const char *ByPathSql = "SELECT t.id FROM tracks t JOIN folders f ON f.id = t.folder_id "
                        "WHERE f.path = ? AND t.filename = ? AND t.source_id = 1";
const char *ByNameSql = "SELECT t.id, t.duration_ms FROM tracks t WHERE t.title = ?1 COLLATE NOCASE AND t.artist = ?2 COLLATE NOCASE "
                        "ORDER BY (t.source_id = 1) DESC, (t.album = ?3 COLLATE NOCASE) DESC, abs(t.duration_ms - ?4) LIMIT 8";
const char *ByRemoteSql = "SELECT id FROM tracks WHERE source_id = ? AND remote_id = ?";

} // namespace

PlaylistStore::PlaylistStore(PlayQueue *queue, QObject *parent) : QObject(parent), m_queue(queue)
{
    s_instance = this;
    m_writer.setMaxThreadCount(1);
    reload();
}

PlaylistStore::~PlaylistStore()
{
    m_writer.waitForDone(); // finish what was started (a few seconds at most)
    s_instance = nullptr;
}

PlaylistStore *PlaylistStore::instance()
{
    return s_instance;
}

void PlaylistStore::reload()
{
    QVariantList list;
    try {
        Stmt s(TrackCache::instance().db(),
               "SELECT p.id, p.name, p.smart, p.spec, COALESCE(c.n, 0), COALESCE(c.m, 0) FROM playlists p "
               "LEFT JOIN (SELECT playlist_id, COUNT(track_id) AS n, COUNT(*) - COUNT(track_id) AS m "
               "           FROM playlist_items GROUP BY playlist_id) c ON c.playlist_id = p.id "
               "ORDER BY p.name COLLATE NOCASE, p.id");
        while (s.step()) {
            QVariantMap m;
            m.insert(QStringLiteral("id"), s.i64(0));
            m.insert(QStringLiteral("name"), s.str(1));
            m.insert(QStringLiteral("smart"), s.i32(2) != 0);
            m.insert(QStringLiteral("rules"), s.i32(2) ? QJsonDocument::fromJson(s.str(3).toUtf8()).object().toVariantMap() : QVariantMap());
            m.insert(QStringLiteral("count"), s.i64(4));
            m.insert(QStringLiteral("missing"), s.i64(5));
            list << m;
        }
    } catch (const std::exception &e) {
        qWarning() << "Playlists:" << e.what();
    }
    m_list = list;
    emit playlistsChanged();
}

QVariantList PlaylistStore::normalPlaylists() const
{
    QVariantList out;
    for (const QVariant &v : m_list)
        if (!v.toMap().value(QStringLiteral("smart")).toBool())
            out << v;
    return out;
}

QVariantList PlaylistStore::views() const
{
    const QString section = tr("Playlists");
    QVariantList out;
    for (const QVariant &v : m_list) {
        const QVariantMap p = v.toMap();
        const qint64 id = p.value(QStringLiteral("id")).toLongLong();
        const bool smart = p.value(QStringLiteral("smart")).toBool();
        QVariantMap spec = smart ? p.value(QStringLiteral("rules")).toMap()
                                 : QVariantMap{{QStringLiteral("playlist"), id}, {QStringLiteral("sort"), QStringLiteral("position")}};
        out << QVariantMap{{QStringLiteral("id"), QStringLiteral("playlist-%1").arg(id)},
                           {QStringLiteral("name"), p.value(QStringLiteral("name"))},
                           {QStringLiteral("icon"), smart ? QStringLiteral("filter") : QStringLiteral("playlist")},
                           {QStringLiteral("section"), section},
                           {QStringLiteral("kind"), QStringLiteral("playlist")},
                           {QStringLiteral("playlistId"), id},
                           {QStringLiteral("smart"), smart},
                           {QStringLiteral("spec"), spec}};
    }
    out << QVariantMap{{QStringLiteral("id"), QStringLiteral("playlist-new")},
                       {QStringLiteral("name"), tr("Neue Playlist")},
                       {QStringLiteral("icon"), QStringLiteral("add")},
                       {QStringLiteral("section"), section},
                       {QStringLiteral("kind"), QStringLiteral("newPlaylist")}};
    return out;
}

QVariantMap PlaylistStore::get(qint64 id) const
{
    for (const QVariant &v : m_list)
        if (v.toMap().value(QStringLiteral("id")).toLongLong() == id)
            return v.toMap();
    return {};
}

QString PlaylistStore::uniqueName(const QString &base) const
{
    QString b = base.trimmed();
    if (b.isEmpty())
        b = tr("Neue Playlist");
    auto taken = [&](const QString &n) {
        for (const QVariant &v : m_list)
            if (v.toMap().value(QStringLiteral("name")).toString().compare(n, Qt::CaseInsensitive) == 0)
                return true;
        return false;
    };
    if (!taken(b))
        return b;
    for (int i = 2;; ++i) {
        const QString n = QStringLiteral("%1 (%2)").arg(b).arg(i);
        if (!taken(n))
            return n;
    }
}

qint64 PlaylistStore::create(const QString &name, const QVariantMap &rules)
{
    const bool smart = !rules.isEmpty();
    QString json;
    if (smart) {
        const QVariantMap r = cleanRules(rules);
        const QueryBuilder::Built q = QueryBuilder::trackIds(r);
        if (!q.ok) {
            emit message(q.error);
            return 0;
        }
        json = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(r)).toJson(QJsonDocument::Compact));
    }
    qint64 id = 0;
    try {
        DbConnection &db = TrackCache::instance().db();
        Stmt s(db, "INSERT INTO playlists(name, smart, spec, created, updated) VALUES (?, ?, ?, ?, ?)");
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        s.bind(1, uniqueName(name)).bind(2, smart ? 1 : 0).bind(3, json).bind(4, now).bind(5, now);
        s.exec();
        id = sqlite3_last_insert_rowid(db);
    } catch (const std::exception &e) {
        emit message(tr("Playlist konnte nicht angelegt werden: %1").arg(QString::fromUtf8(e.what())));
        return 0;
    }
    reload();
    return id;
}

bool PlaylistStore::rename(qint64 id, const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || get(id).isEmpty())
        return false;
    if (get(id).value(QStringLiteral("name")).toString() == n)
        return true;
    try {
        Stmt s(TrackCache::instance().db(), "UPDATE playlists SET name = ?, updated = ? WHERE id = ?");
        s.bind(1, uniqueName(n)).bind(2, QDateTime::currentSecsSinceEpoch()).bind(3, id);
        s.exec();
    } catch (const std::exception &e) {
        qWarning() << e.what();
        return false;
    }
    reload();
    return true;
}

bool PlaylistStore::setRules(qint64 id, const QVariantMap &rules)
{
    const QVariantMap r = cleanRules(rules);
    const QueryBuilder::Built q = QueryBuilder::trackIds(r);
    if (!q.ok) {
        emit message(q.error);
        return false;
    }
    try {
        Stmt s(TrackCache::instance().db(), "UPDATE playlists SET spec = ?, updated = ? WHERE id = ? AND smart = 1");
        s.bind(1, QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(r)).toJson(QJsonDocument::Compact)))
            .bind(2, QDateTime::currentSecsSinceEpoch()).bind(3, id);
        s.exec();
    } catch (const std::exception &e) {
        qWarning() << e.what();
        return false;
    }
    reload();
    emit contentChanged(id);
    return true;
}

void PlaylistStore::remove(qint64 id)
{
    const QString name = get(id).value(QStringLiteral("name")).toString();
    try {
        DbConnection &db = TrackCache::instance().db();
        Transaction tx(db);
        Stmt a(db, "DELETE FROM playlist_items WHERE playlist_id = ?");
        a.bind(1, id);
        a.exec();
        Stmt b(db, "DELETE FROM playlists WHERE id = ?");
        b.bind(1, id);
        b.exec();
        tx.commit();
    } catch (const std::exception &e) {
        qWarning() << e.what();
        return;
    }
    reload();
    if (!name.isEmpty())
        emit message(tr("Playlist »%1« gelöscht").arg(name));
}

qint64 PlaylistStore::freeze(qint64 id, TrackListModel *model)
{
    if (!model)
        return 0;
    const QString name = get(id).value(QStringLiteral("name")).toString();
    const qint64 nid = create(tr("%1 (%2)").arg(name, QLocale(QLocale::German).toString(QDate::currentDate(), QStringLiteral("dd.MM.yyyy"))));
    if (nid > 0)
        append(nid, model->ids());
    return nid;
}

void PlaylistStore::touch(qint64 id)
{
    Stmt s(TrackCache::instance().db(), "UPDATE playlists SET updated = ? WHERE id = ?");
    s.bind(1, QDateTime::currentSecsSinceEpoch()).bind(2, id);
    s.exec();
}

QString PlaylistStore::hintFor(DbConnection &db, qint64 trackId)
{
    Stmt s(db, "SELECT COALESCE(f.path, ''), t.filename, t.title, t.artist, t.album, t.duration_ms, t.source_id, "
               "COALESCE(t.remote_id, '') FROM tracks t LEFT JOIN folders f ON f.id = t.folder_id WHERE t.id = ?");
    s.bind(1, trackId);
    if (!s.step())
        return {};
    Hint h;
    h.path = s.i64(6) == Database::LocalSource ? s.str(0) + QLatin1Char('/') + s.str(1)
                                               : QStringLiteral("jf:%1:%2").arg(s.i64(6)).arg(s.str(7));
    h.title = s.str(2);
    h.artist = s.str(3);
    h.album = s.str(4);
    h.durationMs = s.i64(5);
    return makeHint(h);
}

void PlaylistStore::append(qint64 id, QVector<qint64> trackIds, bool announce)
{
    const QVariantMap p = get(id);
    if (p.isEmpty() || p.value(QStringLiteral("smart")).toBool() || trackIds.isEmpty())
        return;
    // Lists from a view hold every track once; the queue or a mix may repeat one.
    if (trackIds.size() < 50000) {
        QSet<qint64> seen;
        seen.reserve(trackIds.size());
        trackIds.erase(std::remove_if(trackIds.begin(), trackIds.end(),
                                      [&](qint64 t) {
                                          if (t <= 0 || seen.contains(t))
                                              return true;
                                          seen.insert(t);
                                          return false;
                                      }),
                       trackIds.end());
    }
    const QString name = p.value(QStringLiteral("name")).toString();
    ++m_pendingWrites;
    QPointer<PlaylistStore> self(this);
    // Off the UI thread: a million tracks take a few seconds.
    (void)QtConcurrent::run(&m_writer, [self, id, ids = std::move(trackIds), announce, name] {
        qint64 added = 0;
        QString error;
        try {
            DbConnection db = Database::open(4 * 1024);
            db.exec("PRAGMA temp_store=FILE"); // big lists: keep the temporary tables out of RAM
            Transaction tx(db);
            db.exec("CREATE TEMP TABLE IF NOT EXISTS pl_add(seq INTEGER PRIMARY KEY, track_id INTEGER NOT NULL)");
            {
                Stmt ins(db, "INSERT INTO temp.pl_add(seq, track_id) VALUES (?, ?)");
                qint64 seq = 0;
                for (qint64 t : ids) {
                    ins.bind(1, seq++).bind(2, t);
                    ins.exec();
                }
            }
            // Each track once: adding an album twice by mistake should not double it.
            // The hint keeps path, title, artist, album and length (see Hint).
            Stmt add(db, "INSERT INTO playlist_items(playlist_id, pos, track_id, hint) "
                         "SELECT ?1, (SELECT COALESCE(MAX(pos), -1) + 1 FROM playlist_items WHERE playlist_id = ?1) + a.seq, t.id, "
                         "  (CASE WHEN t.source_id = 1 THEN COALESCE(f.path, '') || '/' || t.filename "
                         "        ELSE 'jf:' || t.source_id || ':' || COALESCE(t.remote_id, '') END) "
                         "  || char(31) || replace(t.title, char(31), '') || char(31) || replace(t.artist, char(31), '') "
                         "  || char(31) || replace(t.album, char(31), '') || char(31) || t.duration_ms "
                         "FROM temp.pl_add a JOIN tracks t ON t.id = a.track_id LEFT JOIN folders f ON f.id = t.folder_id "
                         "WHERE t.id NOT IN (SELECT track_id FROM playlist_items WHERE playlist_id = ?1 AND track_id IS NOT NULL) "
                         "AND EXISTS (SELECT 1 FROM playlists WHERE id = ?1) " // deleted in the meantime
                         "ORDER BY a.seq");
            add.bind(1, id);
            add.exec();
            added = sqlite3_changes(db);
            Stmt touch(db, "UPDATE playlists SET updated = ? WHERE id = ?");
            touch.bind(1, QDateTime::currentSecsSinceEpoch()).bind(2, id);
            touch.exec();
            tx.commit();
            db.exec("DROP TABLE IF EXISTS temp.pl_add");
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        const qint64 offered = ids.size();
        QMetaObject::invokeMethod(qApp, [self, id, added, offered, error, announce, name] {
            if (!self)
                return;
            --self->m_pendingWrites;
            if (!error.isEmpty()) {
                emit self->message(tr("Hinzufügen fehlgeschlagen: %1").arg(error));
                return;
            }
            self->reload();
            emit self->contentChanged(id);
            if (!announce)
                return;
            const qint64 already = offered - added; // already in it (or listed twice)
            if (added == 0 && already > 0)
                emit self->message(already == 1 ? tr("Ist schon in »%1«").arg(name) : tr("Sind schon alle in »%1«").arg(name));
            else if (already > 0)
                emit self->message(tr("%1 zu »%2« hinzugefügt, %3 waren schon drin").arg(titles(added), name, de(already)));
            else
                emit self->message(tr("%1 zu »%2« hinzugefügt").arg(titles(added), name));
        }, Qt::QueuedConnection);
    });
}

void PlaylistStore::addRows(qint64 id, TrackListModel *model, int row)
{
    if (model)
        append(id, model->actionIds(row));
}

void PlaylistStore::addAll(qint64 id, TrackListModel *model)
{
    if (model)
        append(id, model->ids());
}

static QVector<qint64> idsFor(const QVariantMap &spec)
{
    QVector<qint64> ids;
    const QueryBuilder::Built q = QueryBuilder::trackIds(spec);
    if (!q.ok)
        return ids;
    try {
        Stmt s(TrackCache::instance().db(), q.sql);
        for (int i = 0; i < q.binds.size(); ++i)
            s.bind(i + 1, q.binds[i]);
        while (s.step())
            ids << s.i64(0);
    } catch (const std::exception &e) {
        qWarning() << e.what();
    }
    return ids;
}

void PlaylistStore::addGroup(qint64 id, GroupModel *model, int row)
{
    if (model)
        append(id, idsFor(model->trackSpec(row)));
}

void PlaylistStore::addFolder(qint64 id, const QString &path)
{
    const QVariantMap spec{{QStringLiteral("filters"), QVariantList{QVariantMap{{QStringLiteral("field"), QStringLiteral("folder")},
                                                                                 {QStringLiteral("op"), QStringLiteral("under")},
                                                                                 {QStringLiteral("value"), path}}}},
                           {QStringLiteral("sort"), QStringLiteral("path")}};
    append(id, idsFor(spec));
}

void PlaylistStore::addQueue(qint64 id)
{
    if (!m_queue)
        return;
    QVector<qint64> ids;
    for (int r = 0; r < m_queue->count(); ++r)
        ids << m_queue->idAt(r);
    append(id, ids);
}

void PlaylistStore::addTrack(qint64 id, qint64 trackId)
{
    append(id, {trackId});
}

void PlaylistStore::writeAsync(qint64 id, std::function<void(DbConnection &)> work, std::function<void()> after)
{
    ++m_pendingWrites;
    QPointer<PlaylistStore> self(this);
    // One writer thread: edits run in the order they were made, and a big
    // playlist never blocks the window.
    (void)QtConcurrent::run(&m_writer, [self, id, work = std::move(work), after = std::move(after)] {
        QString error;
        try {
            DbConnection db = Database::open(4 * 1024);
            db.exec("PRAGMA temp_store=FILE"); // big lists: keep temporary tables out of RAM
            Transaction tx(db);
            work(db);
            Stmt touch(db, "UPDATE playlists SET updated = ? WHERE id = ?");
            touch.bind(1, QDateTime::currentSecsSinceEpoch()).bind(2, id);
            touch.exec();
            tx.commit();
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        QMetaObject::invokeMethod(qApp, [self, id, error, after] {
            if (!self)
                return;
            --self->m_pendingWrites;
            if (!error.isEmpty()) {
                qWarning() << "Playlist:" << error;
                emit self->message(tr("Playlist konnte nicht geändert werden: %1").arg(error));
                return;
            }
            self->reload();
            emit self->contentChanged(id);
            if (after)
                after();
        }, Qt::QueuedConnection);
    });
}

static void fillKeys(DbConnection &db, const QVector<qint64> &keys)
{
    db.exec("CREATE TEMP TABLE IF NOT EXISTS pl_keys(pos INTEGER PRIMARY KEY)");
    db.exec("DELETE FROM temp.pl_keys");
    Stmt ins(db, "INSERT OR IGNORE INTO temp.pl_keys(pos) VALUES (?)");
    for (qint64 k : keys) {
        ins.bind(1, k);
        ins.exec();
    }
}

void PlaylistStore::removeRows(qint64 id, TrackListModel *model, int row)
{
    if (!model || !model->hasKeys())
        return;
    QVector<qint64> keys;
    for (int r : model->actionRows(row))
        keys << model->keyAt(r);
    if (keys.isEmpty())
        return;
    model->clearSelection();
    const QString msg = tr("%1 aus »%2« entfernt").arg(titles(keys.size()), get(id).value(QStringLiteral("name")).toString());
    QPointer<PlaylistStore> self(this);
    writeAsync(id, [id, keys](DbConnection &db) {
        fillKeys(db, keys);
        Stmt del(db, "DELETE FROM playlist_items WHERE playlist_id = ? AND pos IN (SELECT pos FROM temp.pl_keys)");
        del.bind(1, id);
        del.exec();
        db.exec("DELETE FROM temp.pl_keys");
    }, [self, msg] { if (self) emit self->message(msg); });
}

void PlaylistStore::moveRows(qint64 id, TrackListModel *model, int row, int to)
{
    if (!model || !model->hasKeys())
        return;
    QVector<qint64> keys;
    for (int r : model->actionRows(row))
        keys << model->keyAt(r);
    if (keys.isEmpty())
        return;
    model->clearSelection();
    // Entries are keyed by position. Only the stretch between the moved rows
    // and the target changes; missing entries (not in the model) keep their
    // place relative to their neighbours.
    const qint64 target = to >= 0 && to < model->count() ? model->keyAt(to) : std::numeric_limits<qint64>::max();
    writeAsync(id, [id, keys, target](DbConnection &db) {
        fillKeys(db, keys);
        qint64 lo = 0, hi = 0;
        {
            Stmt s(db, "SELECT MIN(pos), MAX(pos) FROM temp.pl_keys");
            s.step();
            lo = std::min(s.i64(0), target);
            hi = s.i64(1);
            Stmt last(db, "SELECT COALESCE(MAX(pos), 0) FROM playlist_items WHERE playlist_id = ?");
            last.bind(1, id);
            last.step();
            hi = std::max(hi, std::min(target, last.i64(0)));
        }
        db.exec("DROP TABLE IF EXISTS temp.pl_rank");
        db.exec("CREATE TEMP TABLE pl_rank(slot INTEGER PRIMARY KEY, pos INTEGER, rnk INTEGER)");
        Stmt rank(db, "INSERT INTO temp.pl_rank(slot, pos, rnk) "
                      "SELECT ROW_NUMBER() OVER (ORDER BY pos), pos, ROW_NUMBER() OVER (ORDER BY grp, pos) FROM "
                      "(SELECT pos, CASE WHEN pos IN (SELECT pos FROM temp.pl_keys) THEN 1 WHEN pos < ?2 THEN 0 ELSE 2 END AS grp "
                      " FROM playlist_items WHERE playlist_id = ?1 AND pos BETWEEN ?3 AND ?4)");
        rank.bind(1, id).bind(2, target).bind(3, lo).bind(4, hi);
        rank.exec();
        // the entry ranked k takes the position of slot k
        db.exec("DROP TABLE IF EXISTS temp.pl_map");
        db.exec("CREATE TEMP TABLE pl_map(old INTEGER PRIMARY KEY, new INTEGER)");
        db.exec("INSERT INTO temp.pl_map(old, new) SELECT a.pos, b.pos FROM temp.pl_rank a JOIN temp.pl_rank b ON b.slot = a.rnk");
        Stmt park(db, "UPDATE playlist_items SET pos = -1 - pos WHERE playlist_id = ? AND pos BETWEEN ? AND ?");
        park.bind(1, id).bind(2, lo).bind(3, hi);
        park.exec();
        Stmt place(db, "UPDATE playlist_items SET pos = (SELECT new FROM temp.pl_map WHERE old = -1 - playlist_items.pos) "
                       "WHERE playlist_id = ? AND pos < 0");
        place.bind(1, id);
        place.exec();
        db.exec("DROP TABLE temp.pl_rank; DROP TABLE temp.pl_map; DELETE FROM temp.pl_keys;");
    });
}

int PlaylistStore::removeMissing(qint64 id)
{
    int n = 0;
    try {
        DbConnection &db = TrackCache::instance().db();
        Stmt s(db, "DELETE FROM playlist_items WHERE playlist_id = ? AND track_id IS NULL");
        s.bind(1, id);
        s.exec();
        n = sqlite3_changes(db);
    } catch (const std::exception &e) {
        qWarning() << e.what();
    }
    if (n > 0) {
        reload();
        emit contentChanged(id);
        emit message(tr("%1 entfernt, die nicht mehr da waren").arg(titles(n)));
    }
    return n;
}

void PlaylistStore::relink()
{
    QSet<qint64> touched;
    try {
        DbConnection &db = TrackCache::instance().db();
        if (db.scalar("SELECT EXISTS(SELECT 1 FROM playlist_items WHERE track_id IS NULL)") == 0)
            return;
        struct Missing { qint64 playlist, pos; QString hint; };
        QVector<Missing> missing;
        {
            Stmt s(db, "SELECT playlist_id, pos, hint FROM playlist_items WHERE track_id IS NULL");
            while (s.step())
                missing << Missing{s.i64(0), s.i64(1), s.str(2)};
        }
        Stmt byPath(db, ByPathSql), byName(db, ByNameSql), byRemote(db, ByRemoteSql);
        Stmt upd(db, "UPDATE playlist_items SET track_id = ? WHERE playlist_id = ? AND pos = ?");
        Transaction tx(db);
        for (const Missing &m : missing) {
            const Hint h = parseHint(m.hint);
            qint64 t = 0;
            if (h.path.startsWith(QLatin1String("jf:"))) {
                const QStringList f = h.path.split(QLatin1Char(':'));
                byRemote.bind(1, f.value(1).toLongLong()).bind(2, f.mid(2).join(QLatin1Char(':')));
                t = byRemote.step() ? byRemote.i64(0) : 0;
                byRemote.reset();
            } else if (!h.path.isEmpty()) {
                t = trackByPath(byPath, h.path);
            }
            if (t == 0)
                t = trackByName(byName, h);
            if (t == 0)
                continue;
            upd.bind(1, t).bind(2, m.playlist).bind(3, m.pos);
            upd.exec();
            touched.insert(m.playlist);
        }
        tx.commit();
    } catch (const std::exception &e) {
        qWarning() << "Playlists wieder verknüpfen:" << e.what();
    }
    if (touched.isEmpty())
        return;
    reload();
    for (qint64 id : std::as_const(touched))
        emit contentChanged(id);
}

// ---------- M3U ----------

qint64 PlaylistStore::importFile(const QUrl &url)
{
    const QString file = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        emit message(tr("Datei kann nicht gelesen werden: %1").arg(file));
        return 0;
    }
    const QByteArray raw = f.readAll();
    // .m3u8 is UTF-8; old .m3u files are often Latin-1 / Windows-1252.
    QString text;
    {
        QStringDecoder utf8(QStringDecoder::Utf8);
        text = utf8.decode(raw);
        if (utf8.hasError())
            text = QString::fromLatin1(raw);
    }
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);

    const QDir base = QFileInfo(file).absoluteDir();
    QVector<qint64> found;
    QVector<Hint> lost;
    QVector<int> order; // index into found (>= 0) or -(index into lost) - 1, in file order
    int total = 0;
    try {
        DbConnection &db = TrackCache::instance().db();
        Stmt byPath(db, ByPathSql), byName(db, ByNameSql);
        Hint extinf;
        bool haveExtinf = false;
        static const QRegularExpression lineBreak(QStringLiteral("[\r\n]+"));
        for (QString line : text.split(lineBreak, Qt::SkipEmptyParts)) {
            line = line.trimmed();
            if (line.isEmpty())
                continue;
            if (line.startsWith(QLatin1String("#EXTINF:"), Qt::CaseInsensitive)) {
                // #EXTINF:253,Artist - Title
                const int comma = int(line.indexOf(QLatin1Char(',')));
                extinf = Hint{};
                extinf.durationMs = qint64(line.mid(8, comma > 8 ? comma - 8 : -1).section(QLatin1Char(' '), 0, 0).toDouble() * 1000);
                const QString name = comma > 0 ? line.mid(comma + 1).trimmed() : QString();
                const int dash = int(name.indexOf(QLatin1String(" - ")));
                if (dash > 0) {
                    extinf.artist = name.left(dash).trimmed();
                    extinf.title = name.mid(dash + 3).trimmed();
                } else {
                    extinf.title = name;
                }
                haveExtinf = true;
                continue;
            }
            if (line.startsWith(QLatin1Char('#')))
                continue;
            ++total;
            Hint h = haveExtinf ? extinf : Hint{};
            haveExtinf = false;
            QString path;
            if (line.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
                path = QUrl(line).toLocalFile();
            } else if (!line.contains(QLatin1String("://"))) {
                path = line;
                path.replace(QLatin1Char('\\'), QLatin1Char('/')); // written on Windows
                if (QFileInfo(path).isRelative())
                    path = base.absoluteFilePath(path);
                path = QDir::cleanPath(path);
            }
            qint64 t = 0;
            if (!path.isEmpty()) {
                t = trackByPath(byPath, path);
                if (t == 0) {
                    const QString canon = QFileInfo(path).canonicalFilePath();
                    if (!canon.isEmpty() && canon != path)
                        t = trackByPath(byPath, canon);
                }
                h.path = path;
                if (h.title.isEmpty())
                    h.title = QFileInfo(path).completeBaseName();
            }
            if (t == 0)
                t = trackByName(byName, h);
            if (t > 0) {
                order << int(found.size());
                found << t;
            } else {
                order << -int(lost.size()) - 1;
                lost << h;
            }
        }
    } catch (const std::exception &e) {
        emit message(tr("Import fehlgeschlagen: %1").arg(QString::fromUtf8(e.what())));
        return 0;
    }
    if (total == 0) {
        emit message(tr("In der Datei stehen keine Titel"));
        return 0;
    }

    const qint64 id = create(QFileInfo(file).completeBaseName());
    if (id <= 0)
        return 0;
    // Entries that are not in the library stay as "missing": they link up when
    // the files get scanned (e.g. a folder not added yet).
    try {
        DbConnection &db = TrackCache::instance().db();
        Transaction tx(db);
        Stmt ins(db, "INSERT INTO playlist_items(playlist_id, pos, track_id, hint) VALUES (?, ?, ?, ?)");
        qint64 pos = 0;
        for (int o : order) {
            ins.bind(1, id).bind(2, pos++);
            if (o >= 0)
                ins.bind(3, found[o]).bind(4, hintFor(db, found[o]));
            else
                ins.bindNull(3).bind(4, makeHint(lost[-o - 1]));
            ins.exec();
        }
        tx.commit();
    } catch (const std::exception &e) {
        emit message(tr("Import fehlgeschlagen: %1").arg(QString::fromUtf8(e.what())));
    }
    reload();
    emit contentChanged(id);
    const QString name = get(id).value(QStringLiteral("name")).toString();
    if (lost.isEmpty())
        emit message(tr("»%1« importiert: %2").arg(name, titles(found.size())));
    else
        emit message(tr("»%1« importiert: %2 von %3 gefunden, der Rest wird verknüpft, sobald er auftaucht")
                         .arg(name, de(found.size()), titles(total)));
    return id;
}

QString PlaylistStore::suggestedExportName(qint64 id) const
{
    QString n = get(id).value(QStringLiteral("name")).toString();
    static const QRegularExpression bad(QStringLiteral("[/\\\\:*?\"<>|]"));
    n.replace(bad, QStringLiteral("_"));
    return (n.isEmpty() ? QStringLiteral("Playlist") : n) + QStringLiteral(".m3u8");
}

QUrl PlaylistStore::defaultFolder() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        dir = QDir::homePath();
    return QUrl::fromLocalFile(dir);
}

bool PlaylistStore::exportFile(qint64 id, const QUrl &url)
{
    QString file = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (file.isEmpty())
        return false;
    const QString suffix = QFileInfo(file).suffix().toLower();
    if (suffix != QLatin1String("m3u") && suffix != QLatin1String("m3u8"))
        file += QStringLiteral(".m3u8");
    const QString dir = QFileInfo(file).absolutePath();

    QByteArray out("#EXTM3U\n");
    int written = 0, skipped = 0;
    try {
        DbConnection &db = TrackCache::instance().db();
        Stmt s(db, "SELECT p.track_id, p.hint, COALESCE(f.path, ''), t.filename, t.title, t.artist, t.duration_ms, t.source_id "
                   "FROM playlist_items p LEFT JOIN tracks t ON t.id = p.track_id LEFT JOIN folders f ON f.id = t.folder_id "
                   "WHERE p.playlist_id = ? ORDER BY p.pos");
        s.bind(1, id);
        while (s.step()) {
            Hint h = parseHint(s.str(1));
            QString path;
            if (!s.isNull(0)) {
                h.title = s.str(4);
                h.artist = s.str(5);
                h.durationMs = s.i64(6);
                if (s.i64(7) == Database::LocalSource)
                    path = s.str(2) + QLatin1Char('/') + s.str(3);
            } else if (!h.path.startsWith(QLatin1String("jf:"))) {
                path = h.path; // missing right now: keep where it was
            }
            const QString name = h.artist.isEmpty() ? h.title : h.artist + QStringLiteral(" - ") + h.title;
            if (path.isEmpty()) {
                // Server tracks have no file; a stream address would contain the access key.
                out += "# Jellyfin: " + name.toUtf8() + '\n';
                ++skipped;
                continue;
            }
            // Inside the playlist's folder: relative, so the folder can move as a whole (USB stick).
            if (path.startsWith(dir + QLatin1Char('/')))
                path = path.mid(dir.size() + 1);
            out += "#EXTINF:" + QByteArray::number(qMax<qint64>(-1, (h.durationMs + 500) / 1000)) + ',' + name.toUtf8() + '\n';
            out += path.toUtf8() + '\n';
            ++written;
        }
    } catch (const std::exception &e) {
        emit message(tr("Export fehlgeschlagen: %1").arg(QString::fromUtf8(e.what())));
        return false;
    }
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(out) != out.size() || !f.commit()) {
        emit message(tr("Datei kann nicht geschrieben werden: %1").arg(file));
        return false;
    }
    if (skipped > 0)
        emit message(tr("%1 gespeichert. %2 vom Jellyfin-Server stehen nur als Kommentar drin")
                         .arg(titles(written), de(skipped)));
    else
        emit message(tr("%1 gespeichert: %2").arg(titles(written), QFileInfo(file).fileName()));
    return true;
}

int PlaylistStore::preview(const QVariantMap &rules)
{
    const int serial = ++m_previewSerial;
    const QueryBuilder::Built q = QueryBuilder::trackIds(cleanRules(rules));
    QPointer<PlaylistStore> self(this);
    if (!q.ok) {
        QMetaObject::invokeMethod(this, [self, serial, err = q.error] {
            if (self)
                emit self->previewReady(serial, 0, 0, err);
        }, Qt::QueuedConnection);
        return serial;
    }
    (void)QtConcurrent::run([self, serial, q] {
        qint64 n = 0, ms = 0;
        QString error;
        try {
            DbConnection db = Database::open(4 * 1024);
            Stmt s(db, q.countSql);
            for (int i = 0; i < q.binds.size(); ++i)
                s.bind(i + 1, q.binds[i]);
            if (s.step()) {
                n = s.i64(0);
                ms = s.i64(1);
            }
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        QMetaObject::invokeMethod(qApp, [self, serial, n, ms, error] {
            if (self && serial == self->m_previewSerial)
                emit self->previewReady(serial, n, ms, error);
        }, Qt::QueuedConnection);
    });
    return serial;
}
