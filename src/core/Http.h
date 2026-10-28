// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QUrl>

#include <functional>

namespace Http {

using Headers = QList<QPair<QByteArray, QByteArray>>;

// How the player names itself to web services: "BuB3Player/<version> (<repository>)".
QByteArray userAgent();

// An internet address (http or https, with a host): the only kind a station,
// a station's web page or a directory entry may have.
bool isWeb(const QUrl &url);

// Blocking GET for worker threads (covers, lyrics). Follows only redirects that
// do not lower security. status: HTTP status, 0 = no connection, timeout or
// cancelled. Returns the body of a 2xx answer, otherwise nothing. `cancel` is
// asked ten times a second; true ends the request at once.
QByteArray get(const QUrl &url, const Headers &headers, int timeoutMs, int *status,
               const std::function<bool()> &cancel = {});

// The start of an answer that may never end (a radio stream): status, headers
// (lower-case names) and at most `maxBytes` of the body, then the request stops.
// `cancel` as with get().
struct Probe
{
    int status = 0;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray head;
    QUrl finalUrl;
    QByteArray header(const QByteArray &name) const;
};
Probe probe(const QUrl &url, const Headers &headers, int timeoutMs, qint64 maxBytes, const std::function<bool()> &cancel = {});

} // namespace Http
