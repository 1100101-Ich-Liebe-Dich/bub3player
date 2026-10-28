// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/External.h"

#include "core/Paths.h"
#include "library/TrackCache.h"

#include <QCoreApplication>
#include <QUrl>
#include <QMutex>

namespace External {

namespace {
QMutex g_mutex;
CdDisc g_disc;
} // namespace

void setDisc(const CdDisc &disc)
{
    QMutexLocker lock(&g_mutex);
    g_disc = disc;
}

CdDisc disc()
{
    QMutexLocker lock(&g_mutex);
    return g_disc;
}

QString stationLogoFile(qint64 stationId)
{
    return Paths::cacheDir() + QStringLiteral("/stations/%1").arg(stationId);
}

TrackRow row(qint64 id)
{
    TrackRow r;
    if (isStation(id)) {
        try {
            Stmt s(TrackCache::instance().db(), "SELECT name, stream, codec, bitrate, country, tags, last_played FROM stations WHERE id = ?");
            s.bind(1, stationOf(id));
            if (!s.step())
                return r;
            r.id = id;
            r.title = s.str(0);
            r.album = r.title;
            r.artist = QCoreApplication::translate("External", "Internetradio");
            r.url = s.str(1);
            r.codec = s.str(2);
            r.bitrate = s.i32(3);
            r.genre = s.str(5).section(QLatin1Char(','), 0, 0).trimmed();
            r.lastPlayed = s.i64(6);
            r.sourceId = 0;
            r.valid = true;
        } catch (const SqlError &) {
        }
        return r;
    }
    if (isCd(id)) {
        const CdDisc d = disc();
        const int n = cdTrackOf(id);
        for (const CdTrack &t : d.tracks) {
            if (t.number != n)
                continue;
            r.id = id;
            r.title = t.title.isEmpty() ? QCoreApplication::translate("External", "Titel %1").arg(n) : t.title;
            r.artist = t.artist.isEmpty() ? d.albumArtist : t.artist;
            r.album = d.album.isEmpty() ? QCoreApplication::translate("External", "Audio-CD") : d.album;
            r.aa = d.albumArtist;
            r.year = d.year;
            r.trackNo = n;
            r.discNo = 1;
            r.durationMs = int(t.durationMs);
            r.codec = QStringLiteral("CD");
            r.bits = 16;
            r.sampleRate = 44100;
            // the cdda source takes the device from the address: cdda:///dev/sr0#3
            r.url = QStringLiteral("cdda://") + QString::fromLatin1(QUrl::toPercentEncoding(d.device, "/")) + QLatin1Char('#') + QString::number(n);
            r.sourceId = 0;
            r.valid = true;
            break;
        }
    }
    return r;
}

} // namespace External
