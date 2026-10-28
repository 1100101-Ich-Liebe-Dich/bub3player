// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/TrackCache.h"

#include "core/Database.h"
#include "library/External.h"
#include <QStringList>

#include <algorithm>

namespace {
const char *Columns = "t.id, t.folder_id, t.added_at, t.mtime, t.last_played, t.title, t.artist, t.album, t.aa, "
                      "t.genre, t.codec, t.filename, t.year, t.track_no, t.disc_no, t.duration_ms, t.bitrate, "
                      "t.sample_rate, t.bits, t.play_count, t.source_id, COALESCE(t.remote_id, ''), t.rating";

TrackRow readRow(const Stmt &s)
{
    TrackRow r;
    r.id = s.i64(0);
    r.folderId = s.i64(1);
    r.added = s.i64(2);
    r.mtime = s.i64(3);
    r.lastPlayed = s.i64(4);
    r.title = s.str(5);
    r.artist = s.str(6);
    r.album = s.str(7);
    r.aa = s.str(8);
    r.genre = s.str(9);
    r.codec = s.str(10);
    r.filename = s.str(11);
    r.year = s.i32(12);
    r.trackNo = s.i32(13);
    r.discNo = s.i32(14);
    r.durationMs = s.i32(15);
    r.bitrate = s.i32(16);
    r.sampleRate = s.i32(17);
    r.bits = s.i32(18);
    r.playCount = s.i32(19);
    r.sourceId = s.i64(20);
    r.remoteId = s.str(21);
    r.rating = s.i32(22);
    r.valid = true;
    return r;
}
} // namespace

QString formatDuration(qint64 ms)
{
    if (ms <= 0)
        return QStringLiteral("–");
    const qint64 s = ms / 1000;
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString TrackRow::formatLabel() const
{
    if (bits > 0 && sampleRate > 0) {
        const double khz = sampleRate / 1000.0;
        return QStringLiteral("%1 %2/%3").arg(codec).arg(bits).arg(khz, 0, 'g', 3);
    }
    if (bitrate > 0)
        return QStringLiteral("%1 %2").arg(codec).arg(bitrate);
    return codec;
}

QString TrackRow::durationText() const
{
    return formatDuration(durationMs);
}

TrackCache &TrackCache::instance()
{
    static TrackCache c;
    return c;
}

DbConnection &TrackCache::db()
{
    if (!m_db.isOpen())
        m_db = Database::open(16 * 1024);
    return m_db;
}

QVector<TrackRow> TrackCache::fetch(const QVector<qint64> &ids)
{
    QVector<TrackRow> out(ids.size());
    if (ids.isEmpty())
        return out;
    // ids are integers from our own database, so inlining them is safe.
    QStringList parts;
    parts.reserve(ids.size());
    for (qint64 id : ids)
        if (id > 0)
            parts << QString::number(id);
    const QString sql = QStringLiteral("SELECT %1 FROM tracks t WHERE t.id IN (%2)")
                            .arg(QLatin1String(Columns), parts.join(QLatin1Char(',')));
    QHash<qint64, TrackRow> found;
    found.reserve(ids.size());
    try {
        Stmt s(db(), sql);
        while (s.step()) {
            TrackRow r = readRow(s);
            applyPending(r);
            found.insert(r.id, r);
        }
    } catch (const SqlError &) {
        return out;
    }
    for (int i = 0; i < ids.size(); ++i)
        out[i] = ids[i] < 0 ? External::row(ids[i]) : found.value(ids[i]);
    return out;
}

TrackRow TrackCache::get(qint64 id)
{
    auto it = m_rows.constFind(id);
    if (it != m_rows.constEnd())
        return *it;
    if (m_rows.size() > 20000)
        m_rows.clear();
    if (id < 0) {
        const TrackRow r = External::row(id);
        m_rows.insert(id, r);
        return r;
    }
    TrackRow r;
    try {
        Stmt s(db(), QStringLiteral("SELECT %1 FROM tracks t WHERE t.id = ?").arg(QLatin1String(Columns)));
        s.bind(1, id);
        if (s.step()) {
            r = readRow(s);
            applyPending(r);
        }
    } catch (const SqlError &) {
    }
    m_rows.insert(id, r);
    return r;
}

QString TrackCache::pathOf(qint64 id)
{
    try {
        Stmt s(db(), "SELECT f.path, t.filename FROM tracks t JOIN folders f ON f.id = t.folder_id WHERE t.id = ?");
        s.bind(1, id);
        if (s.step())
            return s.str(0) + QLatin1Char('/') + s.str(1);
    } catch (const SqlError &) {
    }
    return {};
}

quint64 TrackCache::setPendingRating(QVector<qint64> sortedIds, int rating)
{
    for (qint64 id : std::as_const(sortedIds)) {
        auto it = m_rows.find(id);
        if (it != m_rows.end())
            it->rating = rating;
    }
    const quint64 ticket = m_nextTicket++;
    m_pending.append({ticket, std::move(sortedIds), rating});
    return ticket;
}

void TrackCache::donePending(quint64 ticket)
{
    m_pending.removeIf([ticket](const PendingRating &p) { return p.ticket == ticket; });
}

void TrackCache::applyPending(TrackRow &row) const
{
    for (const PendingRating &p : m_pending)
        if (std::binary_search(p.ids.cbegin(), p.ids.cend(), row.id))
            row.rating = p.rating;
}

void TrackCache::invalidate()
{
    m_rows.clear();
}
