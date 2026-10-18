#include "jellyfin/JellyfinAccess.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QReadWriteLock>
#include <QSettings>
#include <QSysInfo>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>

namespace JellyfinAccess {

namespace {
QReadWriteLock g_lock;
QHash<qint64, Server> g_servers;
QHash<qint64, bool> g_online;
}

void setOnline(qint64 sourceId, bool online)
{
    QWriteLocker l(&g_lock);
    g_online.insert(sourceId, online);
}

bool isOnline(qint64 sourceId)
{
    QReadLocker l(&g_lock);
    return g_servers.contains(sourceId) && g_online.value(sourceId, true);
}

void set(qint64 sourceId, const Server &s)
{
    QWriteLocker l(&g_lock);
    g_servers.insert(sourceId, s);
}

void remove(qint64 sourceId)
{
    QWriteLocker l(&g_lock);
    g_servers.remove(sourceId);
}

bool get(qint64 sourceId, Server &out)
{
    QReadLocker l(&g_lock);
    auto it = g_servers.constFind(sourceId);
    if (it == g_servers.constEnd())
        return false;
    out = *it;
    return true;
}

QString deviceId()
{
    static const QString id = [] {
        QSettings s;
        QString v = s.value(QStringLiteral("jellyfin/deviceId")).toString();
        if (v.isEmpty()) {
            v = QUuid::createUuid().toString(QUuid::Id128);
            s.setValue(QStringLiteral("jellyfin/deviceId"), v);
        }
        return v;
    }();
    return id;
}

QString deviceName()
{
    const QString h = QSysInfo::machineHostName();
    return h.isEmpty() ? QStringLiteral("Linux") : h;
}

QString clientVersion()
{
    const QString v = QCoreApplication::applicationVersion();
    return v.isEmpty() ? QStringLiteral("0.5") : v;
}

QByteArray authHeader(const QString &token)
{
    auto esc = [](QString s) { return s.replace(QLatin1Char('"'), QLatin1Char('\'')); };
    QString h = QStringLiteral("MediaBrowser Client=\"BuB3Player\", Device=\"%1\", DeviceId=\"%2\", Version=\"%3\"")
                    .arg(esc(deviceName()), deviceId(), clientVersion());
    if (!token.isEmpty())
        h += QStringLiteral(", Token=\"%1\"").arg(token);
    return h.toUtf8();
}

QByteArray streamUrl(qint64 sourceId, const QString &itemId)
{
    Server s;
    if (!get(sourceId, s) || itemId.isEmpty())
        return {};
    QUrl u(s.url);
    QUrlQuery q;
    if (s.quality.isEmpty() || s.quality == QLatin1String("original")) {
        // The original file, untouched: gapless and ReplayGain tags stay intact.
        u.setPath(u.path() + QStringLiteral("/Audio/%1/stream").arg(itemId));
        q.addQueryItem(QStringLiteral("static"), QStringLiteral("true"));
    } else {
        // Direct when the original is small enough, otherwise transcoded to MP3.
        u.setPath(u.path() + QStringLiteral("/Audio/%1/universal").arg(itemId));
        q.addQueryItem(QStringLiteral("UserId"), s.userId);
        q.addQueryItem(QStringLiteral("MaxStreamingBitrate"), QString::number(s.quality.toInt() * 1000));
        q.addQueryItem(QStringLiteral("Container"), QStringLiteral("mp3,flac,ogg,opus,m4a,aac,wav,webm"));
        q.addQueryItem(QStringLiteral("TranscodingContainer"), QStringLiteral("mp3"));
        q.addQueryItem(QStringLiteral("TranscodingProtocol"), QStringLiteral("http"));
        q.addQueryItem(QStringLiteral("AudioCodec"), QStringLiteral("mp3"));
    }
    q.addQueryItem(QStringLiteral("DeviceId"), deviceId());
    q.addQueryItem(QStringLiteral("api_key"), s.token);
    u.setQuery(q);
    return u.toEncoded();
}

QUrl imageUrl(qint64 sourceId, const QString &itemId, const QString &tag, int size)
{
    Server s;
    if (!get(sourceId, s) || itemId.isEmpty())
        return {};
    QUrl u(s.url);
    u.setPath(u.path() + QStringLiteral("/Items/%1/Images/Primary").arg(itemId));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("fillWidth"), QString::number(size));
    q.addQueryItem(QStringLiteral("fillHeight"), QString::number(size));
    q.addQueryItem(QStringLiteral("quality"), QStringLiteral("90"));
    if (!tag.isEmpty())
        q.addQueryItem(QStringLiteral("tag"), tag);
    u.setQuery(q);
    return u;
}

QByteArray httpGet(const QUrl &url, const QString &token, int timeoutMs, int *status)
{
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", authHeader(token));
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

QString isoTime(qint64 secs)
{
    return QDateTime::fromSecsSinceEpoch(secs, Qt::UTC).toString(Qt::ISODate);
}

qint64 parseTime(const QString &iso)
{
    if (iso.isEmpty())
        return 0;
    // Jellyfin sends 7 fractional digits ("2024-05-01T10:20:30.1234567Z"); Qt reads up to 3.
    QString s = iso;
    const int dot = s.indexOf(QLatin1Char('.'));
    if (dot > 0) {
        int end = dot + 1;
        while (end < s.size() && s.at(end).isDigit())
            ++end;
        s.remove(dot, end - dot);
    }
    QDateTime t = QDateTime::fromString(s, Qt::ISODate);
    return t.isValid() ? t.toSecsSinceEpoch() : 0;
}

} // namespace JellyfinAccess
