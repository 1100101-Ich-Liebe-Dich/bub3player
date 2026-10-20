// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QUrl>

namespace Http {

using Headers = QList<QPair<QByteArray, QByteArray>>;

// Blocking GET for worker threads (covers, lyrics). Follows only redirects that
// do not lower security. status: HTTP status, 0 = no connection or timeout.
// Returns the body of a 2xx answer, otherwise nothing.
QByteArray get(const QUrl &url, const Headers &headers, int timeoutMs, int *status);

} // namespace Http
