// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/TrackListModel.h"

#include "core/Database.h"
#include "library/Library.h"
#include "library/QueryBuilder.h"

#include <QDateTime>
#include <bit>
#include <QLocale>
#include <QPointer>
#include <QtConcurrent>

TrackListModel::TrackListModel(QObject *parent) : QAbstractListModel(parent)
{
    if (Library::instance())
        connect(Library::instance(), &Library::changed, this, &TrackListModel::reload);
}

int TrackListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_ids.size());
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return {
        {IdRole, "trackId"}, {TitleRole, "title"}, {ArtistRole, "artist"}, {AlbumRole, "album"},
        {AlbumArtistRole, "albumArtist"}, {YearRole, "year"}, {DurationRole, "duration"},
        {DurationMsRole, "durationMs"}, {FormatRole, "format"}, {TrackNoRole, "trackNo"}, {DiscNoRole, "discNo"},
        {GenreRole, "genre"}, {AddedRole, "added"}, {MtimeRole, "mtime"}, {PlayCountRole, "playCount"},
        {LastPlayedRole, "lastPlayed"}, {FileNameRole, "fileName"}, {ValidRole, "valid"}, {SelectedRole, "selected"},
    };
}

const TrackRow &TrackListModel::rowAt(int row) const
{
    const int page = row / PageSize;
    auto it = m_pages.find(page);
    if (it == m_pages.end()) {
        const int start = page * PageSize;
        const int end = std::min<int>(start + PageSize, int(m_ids.size()));
        it = m_pages.insert(page, TrackCache::instance().fetch(m_ids.mid(start, end - start)));
        m_lru.append(page);
        while (m_lru.size() > MaxPages)
            m_pages.remove(m_lru.takeFirst());
    }
    const QVector<TrackRow> &rows = *it;
    const int i = row - page * PageSize;
    return i < rows.size() ? rows[i] : m_empty;
}

static QString shortDate(qint64 secs)
{
    if (secs <= 0)
        return QStringLiteral("–");
    return QLocale(QLocale::German).toString(QDateTime::fromSecsSinceEpoch(secs).date(), QStringLiteral("dd.MM.yyyy"));
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_ids.size())
        return {};
    if (role == IdRole)
        return m_ids[index.row()];
    if (role == SelectedRole)
        return isSelected(index.row());
    const TrackRow &r = rowAt(index.row());
    switch (role) {
    case TitleRole: return r.title;
    case ArtistRole: return r.artist;
    case AlbumRole: return r.album;
    case AlbumArtistRole: return r.aa;
    case YearRole: return r.year > 0 ? QString::number(r.year) : QString();
    case DurationRole: return r.durationText();
    case DurationMsRole: return r.durationMs;
    case FormatRole: return r.formatLabel();
    case TrackNoRole: return r.trackNo > 0 ? QString::number(r.trackNo) : QString();
    case DiscNoRole: return r.discNo;
    case GenreRole: return r.genre;
    case AddedRole: return shortDate(r.added);
    case MtimeRole: return shortDate(r.mtime);
    case PlayCountRole: return r.playCount;
    case LastPlayedRole: return r.lastPlayed > 0 ? shortDate(r.lastPlayed) : QString();
    case FileNameRole: return r.filename;
    case ValidRole: return r.valid;
    }
    return {};
}

QVariantMap TrackListModel::get(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= m_ids.size())
        return m;
    const auto roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it)
        m.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return m;
}

void TrackListModel::setSpec(const QVariantMap &spec)
{
    if (m_specSet && spec == m_spec)
        return;
    m_spec = spec;
    m_specSet = true;
    emit specChanged();
    reload();
}

void TrackListModel::reload()
{
    if (!m_specSet)
        return;
    const int gen = ++m_generation;
    if (m_spec.contains(QStringLiteral("ids"))) {
        // A fixed list (a mix): shown in exactly this order.
        QVector<qint64> ids;
        for (const QVariant &v : m_spec.value(QStringLiteral("ids")).toList())
            ids << v.toLongLong();
        qint64 total = 0;
        for (const TrackRow &r : TrackCache::instance().fetch(ids))
            total += r.durationMs;
        apply(gen, std::move(ids), {}, total, {});
        return;
    }
    if (!m_loading) {
        m_loading = true;
        emit loadingChanged();
    }
    const QueryBuilder::Built q = QueryBuilder::trackIds(m_spec);
    QPointer<TrackListModel> self(this);
    (void)QtConcurrent::run([self, gen, q] {
        QVector<qint64> ids, keys;
        QString error = q.error;
        DbConnection db;
        if (q.ok) {
            try {
                db = Database::open(8 * 1024);
                Stmt s(db, q.sql);
                for (int i = 0; i < q.binds.size(); ++i)
                    s.bind(i + 1, q.binds[i]);
                while (s.step()) {
                    ids.push_back(s.i64(0));
                    if (q.withKeys)
                        keys.push_back(s.i64(1));
                }
            } catch (const std::exception &e) {
                error = QString::fromUtf8(e.what());
            }
        }
        const qsizetype n = ids.size();
        // Ids first: the list is usable now; the total duration follows.
        QMetaObject::invokeMethod(qApp, [self, gen, ids = std::move(ids), keys = std::move(keys), error]() mutable {
            if (self)
                self->apply(gen, std::move(ids), std::move(keys), -1, error);
        }, Qt::QueuedConnection);
        if (!q.ok || n == 0 || !db.isOpen())
            return;
        qint64 total = 0;
        try {
            Stmt s(db, q.sumSql);
            for (int i = 0; i < q.binds.size(); ++i)
                s.bind(i + 1, q.binds[i]);
            if (s.step())
                total = s.i64(0);
        } catch (const std::exception &) {
            return;
        }
        QMetaObject::invokeMethod(qApp, [self, gen, total] {
            if (self && gen == self->m_generation) {
                self->m_totalMs = total;
                emit self->countChanged();
            }
        }, Qt::QueuedConnection);
    });
}

void TrackListModel::apply(int generation, QVector<qint64> ids, QVector<qint64> keys, qint64 totalMs, QString error)
{
    if (generation != m_generation)
        return; // a newer query superseded this one
    TrackCache::instance().invalidate();
    if (ids == m_ids && keys == m_keys) {
        // Same rows (e.g. tags updated during a scan): refresh visible data without
        // resetting, so the scroll position stays put.
        m_pages.clear();
        m_lru.clear();
        if (!m_ids.isEmpty())
            emit dataChanged(index(0), index(int(m_ids.size()) - 1));
    } else {
        beginResetModel();
        m_ids = std::move(ids);
        m_keys = std::move(keys);
        const bool hadSelection = m_selCount > 0;
        m_sel.clear();
        m_selCount = 0;
        m_anchor = -1;
        if (hadSelection)
            emit selectionChanged();
        m_pages.clear();
        m_lru.clear();
        endResetModel();
    }
    if (totalMs >= 0 || m_ids.isEmpty())
        m_totalMs = std::max<qint64>(0, totalMs);
    m_error = error;
    m_loading = false;
    emit countChanged();
    emit loadingChanged();
    emit reloaded();
}

// ---------- selection ----------

bool TrackListModel::isSelected(int row) const
{
    if (row < 0 || m_sel.empty())
        return false;
    const size_t w = size_t(row) >> 6;
    return w < m_sel.size() && (m_sel[w] >> (row & 63)) & 1;
}

void TrackListModel::setBit(int row, bool on)
{
    if (row < 0 || row >= m_ids.size())
        return;
    if (m_sel.empty()) {
        if (!on)
            return;
        m_sel.assign((size_t(m_ids.size()) + 63) / 64, 0);
    }
    quint64 &w = m_sel[size_t(row) >> 6];
    const quint64 bit = quint64(1) << (row & 63);
    if (bool(w & bit) == on)
        return;
    w ^= bit;
    m_selCount += on ? 1 : -1;
}

void TrackListModel::selectionUpdated()
{
    if (m_selCount == 0)
        m_sel.clear();
    if (!m_ids.isEmpty())
        emit dataChanged(index(0), index(int(m_ids.size()) - 1), {SelectedRole});
    emit selectionChanged();
}

void TrackListModel::select(int row, int mode)
{
    if (row < 0 || row >= m_ids.size())
        return;
    if (mode == Toggle) {
        setBit(row, !isSelected(row));
        m_anchor = row;
    } else if (mode == Range && m_anchor >= 0 && m_anchor < m_ids.size()) {
        // Shift: from the anchor to here, replacing the rest (like file managers)
        std::fill(m_sel.begin(), m_sel.end(), 0);
        m_selCount = 0;
        for (int r = std::min(m_anchor, row), e = std::max(m_anchor, row); r <= e; ++r)
            setBit(r, true);
    } else {
        // A plain click selects just this row (and is the anchor for Shift).
        std::fill(m_sel.begin(), m_sel.end(), 0);
        m_selCount = 0;
        setBit(row, true);
        m_anchor = row;
    }
    selectionUpdated();
}

void TrackListModel::selectAll()
{
    if (m_ids.isEmpty())
        return;
    m_sel.assign((size_t(m_ids.size()) + 63) / 64, ~quint64(0));
    if (const int rest = int(m_ids.size() & 63))
        m_sel.back() = (quint64(1) << rest) - 1;
    m_selCount = int(m_ids.size());
    selectionUpdated();
}

void TrackListModel::clearSelection()
{
    if (m_selCount == 0)
        return;
    m_sel.clear();
    m_selCount = 0;
    selectionUpdated();
}

QVector<int> TrackListModel::actionRows(int row) const
{
    QVector<int> rows;
    if (m_selCount > 0 && (row < 0 || isSelected(row))) {
        rows.reserve(m_selCount);
        for (size_t w = 0; w < m_sel.size(); ++w) {
            quint64 bits = m_sel[w];
            while (bits) {
                const int b = std::countr_zero(bits);
                rows << int(w * 64 + size_t(b));
                bits &= bits - 1;
            }
        }
    } else if (row >= 0 && row < m_ids.size()) {
        rows << row;
    }
    return rows;
}

QVector<qint64> TrackListModel::actionIds(int row) const
{
    QVector<qint64> ids;
    for (int r : actionRows(row))
        ids << m_ids[r];
    return ids;
}
