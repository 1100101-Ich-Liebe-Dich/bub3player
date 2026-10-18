#include "library/TrackCache.h"

#include "core/Database.h"
#include <QStringList>

namespace {
const char *Columns = "t.id, t.folder_id, t.added_at, t.mtime, t.last_played, t.title, t.artist, t.album, t.aa, "
                      "t.genre, t.codec, t.filename, t.year, t.track_no, t.disc_no, t.duration_ms, t.bitrate, "
                      "t.sample_rate, t.bits, t.play_count, t.source_id, COALESCE(t.remote_id, '')";

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
        parts << QString::number(id);
    const QString sql = QStringLiteral("SELECT %1 FROM tracks t WHERE t.id IN (%2)")
                            .arg(QLatin1String(Columns), parts.join(QLatin1Char(',')));
    QHash<qint64, TrackRow> found;
    found.reserve(ids.size());
    try {
        Stmt s(db(), sql);
        while (s.step()) {
            TrackRow r = readRow(s);
            found.insert(r.id, r);
        }
    } catch (const SqlError &) {
        return out;
    }
    for (int i = 0; i < ids.size(); ++i)
        out[i] = found.value(ids[i]);
    return out;
}

TrackRow TrackCache::get(qint64 id)
{
    auto it = m_rows.constFind(id);
    if (it != m_rows.constEnd())
        return *it;
    if (m_rows.size() > 20000)
        m_rows.clear();
    TrackRow r;
    try {
        Stmt s(db(), QStringLiteral("SELECT %1 FROM tracks t WHERE t.id = ?").arg(QLatin1String(Columns)));
        s.bind(1, id);
        if (s.step())
            r = readRow(s);
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

void TrackCache::invalidate()
{
    m_rows.clear();
}
