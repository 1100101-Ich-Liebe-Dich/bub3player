// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Sql.h"
#include <QHash>
#include <QString>
#include <QVector>

struct TrackRow
{
    qint64 id = 0, folderId = 0, added = 0, mtime = 0, lastPlayed = 0, sourceId = 1;
    QString remoteId;              // Jellyfin item id (source_id > 1)
    QString url;                   // not in the library (id < 0, see External): what to play
    QString title, artist, album, aa, genre, codec, filename;
    int year = 0, trackNo = 0, discNo = 0, durationMs = 0, bitrate = 0, sampleRate = 0, bits = 0, playCount = 0;
    int rating = 0;                // 0..5; the heart (favourite) is 5
    bool valid = false;
    bool isRemote() const { return sourceId > 1 && !remoteId.isEmpty(); }
    bool isExternal() const { return id < 0; }
    bool favorite() const { return rating >= FavoriteRating; }
    static constexpr int FavoriteRating = 5;

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
    // Hearts set but not in the database yet: rows read meanwhile (a list reloads,
    // the cache was emptied) get them too. `sortedIds` sorted ascending; returns a
    // ticket for donePending once the database has them (or failed to take them).
    quint64 setPendingRating(QVector<qint64> sortedIds, int rating);
    void donePending(quint64 ticket);

    DbConnection &db(); // main-thread connection (reads + small writes)

private:
    TrackCache() = default;
    void applyPending(TrackRow &row) const;

    struct PendingRating
    {
        quint64 ticket;
        QVector<qint64> ids; // sorted
        int rating;
    };
    DbConnection m_db;
    QHash<qint64, TrackRow> m_rows;
    QVector<PendingRating> m_pending; // oldest first: the last match wins
    quint64 m_nextTicket = 1;
};

QString formatDuration(qint64 ms);
