// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Http.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace Http {

QByteArray get(const QUrl &url, const Headers &headers, int timeoutMs, int *status, const std::function<bool()> &cancel)
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
    QTimer poll;
    if (cancel) {
        QObject::connect(&poll, &QTimer::timeout, reply, [&] {
            if (cancel())
                reply->abort();
        });
        poll.start(100);
    }
    loop.exec();
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status)
        *status = code;
    QByteArray data = code >= 200 && code < 300 ? reply->readAll() : QByteArray();
    reply->deleteLater();
    return data;
}

QByteArray Probe::header(const QByteArray &name) const
{
    for (const auto &h : headers)
        if (h.first == name)
            return h.second;
    return {};
}

Probe probe(const QUrl &url, const Headers &headers, int timeoutMs, qint64 maxBytes)
{
    Probe p;
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    for (const auto &h : headers)
        req.setRawHeader(h.first, h.second);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        p.head += reply->read(maxBytes - p.head.size());
        if (p.head.size() >= maxBytes)
            loop.quit(); // enough: a stream would go on forever
    });
    timer.start(timeoutMs);
    loop.exec();
    p.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    p.finalUrl = reply->url();
    for (const auto &h : reply->rawHeaderPairs())
        p.headers.append({h.first.toLower(), h.second});
    if (p.head.size() < maxBytes)
        p.head += reply->read(maxBytes - p.head.size());
    reply->abort();
    reply->deleteLater();
    return p;
}

} // namespace Http
