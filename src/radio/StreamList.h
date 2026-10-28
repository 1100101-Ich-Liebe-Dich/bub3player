// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVector>

// The addresses in a station list: .pls, .m3u/.m3u8, .xspf or .asx – what radio
// websites hand out instead of the stream itself. Only http(s) entries count.
namespace StreamList {

struct Entry
{
    QUrl url;
    QString title;
};

// `base` resolves relative entries. Recognised by the content, not the name.
QVector<Entry> parse(const QByteArray &data, const QUrl &base = {});

// Is the answer (or file) a list of streams rather than a stream? By the address,
// the content type, or the first bytes.
bool looksLikeList(const QUrl &url, const QByteArray &contentType, const QByteArray &head);

// HLS (#EXT-X-…) is a stream of its own, not a list of stations.
bool isHls(const QByteArray &data);

// "MP3", "AAC", "OPUS", "FLAC", "OGG" from a content type ("audio/aacp") or a
// GStreamer codec description ("MPEG-4 AAC"); empty if unknown.
QString codecName(const QString &description);

} // namespace StreamList
