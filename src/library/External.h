// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QVector>

struct TrackRow;

// Things that play but are not in the library: internet radio stations and the
// tracks of an audio CD. They travel through the queue and the player like
// tracks, with negative ids, so everything that is only for library tracks
// (hearts, play counts, analysis, lyrics, playlists) simply skips them (id > 0).
//   -1 … -(CdBase - 1)   station n is -n (stable: the queue keeps them over a restart)
//   -(CdBase + n)        track n of the CD in the drive (gone with the disc)
namespace External {

constexpr qint64 CdBase = 1'000'000'000'000LL;

inline bool isStation(qint64 id) { return id < 0 && id > -CdBase; }
inline bool isCd(qint64 id) { return id <= -CdBase; }
inline qint64 stationItem(qint64 stationId) { return -stationId; }
inline qint64 stationOf(qint64 id) { return -id; }
inline qint64 cdItem(int track) { return -(CdBase + track); }
inline int cdTrackOf(qint64 id) { return int(-id - CdBase); }

// The disc in the drive (set by AudioCd, read from any thread).
struct CdTrack
{
    int number = 0;
    QString title, artist;
    qint64 durationMs = 0;
};
struct CdDisc
{
    QString device;      // what the cdda source opens
    QString discId;      // MusicBrainz disc id
    QString album, albumArtist;
    int year = 0;
    QString coverFile;   // front cover in the cache, if known
    QVector<CdTrack> tracks;
};
void setDisc(const CdDisc &disc); // empty: no disc
CdDisc disc();
// The drive is being read into files: its tracks do not play meanwhile.
void setCdBusy(bool busy);
bool cdBusy();
QString cdBusyText(); // what to tell when the CD is asked to play meanwhile

// Station logo in the cache (may not exist).
QString stationLogoFile(qint64 stationId);

// The row the lists and the player show (main thread: stations come from the
// database through `db` of TrackCache). Invalid if the station or CD is gone.
TrackRow row(qint64 id);

} // namespace External
