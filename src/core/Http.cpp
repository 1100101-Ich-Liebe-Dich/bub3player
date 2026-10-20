// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Http.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace Http {

QByteArray get(const QUrl &url, const Headers &headers, int timeoutMs, int *status)
{
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    for (const auto &h : headers)
        req.setRawHeader(h.first, h.second);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status)
        *status = code;
    QByteArray data = code >= 200 && code < 300 ? reply->readAll() : QByteArray();
    reply->deleteLater();
    return data;
}

} // namespace Http
