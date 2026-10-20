// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

struct TrackTags
{
    QString title, artist, album, albumArtist, genre, composer, codec;
    int year = 0, trackNo = 0, discNo = 0;
    int durationMs = 0, bitrate = 0, sampleRate = 0, channels = 0, bits = 0;
    bool ok = false;
};

namespace TagReader {

// File extensions we treat as music (lower case, without dot).
const QStringList &audioExtensions();
bool isAudioFile(const char *name, size_t len);

// Reads tags and audio properties. Never throws; ok=false if unreadable.
TrackTags read(const QString &path);

// Raw bytes of the embedded front cover (JPEG/PNG), empty if none.
QByteArray embeddedCover(const QString &path);

// Lyrics stored in the file, best first: synchronised ones (ID3v2 SYLT, as LRC
// text) before the plain fields (USLT, LYRICS, UNSYNCEDLYRICS, ©lyr …), which
// may hold LRC text as well. Empty if there are none.
QStringList embeddedLyrics(const QString &path);

} // namespace TagReader
