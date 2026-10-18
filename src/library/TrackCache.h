#pragma once

#include "core/Sql.h"
#include <QHash>
#include <QString>
#include <QVector>

struct TrackRow
{
    qint64 id = 0, folderId = 0, added = 0, mtime = 0, lastPlayed = 0;
    QString title, artist, album, aa, genre, codec, filename;
    int year = 0, trackNo = 0, discNo = 0, durationMs = 0, bitrate = 0, sampleRate = 0, bits = 0, playCount = 0;
    bool valid = false;

    QString formatLabel() const;   // "FLAC 24/96", "MP3 320"
    QString durationText() const;  // "3:45"
};

// Main-thread row access. Lists hold only ids (8 bytes per track) and pull the
// rows they actually display from here, so a 1M-track view costs ~8 MB, not ~500 MB.
class TrackCache
{
public:
    static TrackCache &instance();

    // Fetches rows for ids in the given order (missing ids yield invalid rows).
    QVector<TrackRow> fetch(const QVector<qint64> &ids);
    TrackRow get(qint64 id);
    QString pathOf(qint64 id);
    void invalidate();

    DbConnection &db(); // main-thread connection (reads + small writes)

private:
    TrackCache() = default;
    DbConnection m_db;
    QHash<qint64, TrackRow> m_rows;
};

QString formatDuration(qint64 ms);
