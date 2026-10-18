#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

// Thread-safe access data for Jellyfin servers, used by the player (stream
// URLs), the cover loader (image URLs, in worker threads) and the sync.
namespace JellyfinAccess {

struct Server
{
    QString url;      // https://host:8096 (no trailing slash)
    QString token;
    QString userId;
    QString quality;  // "original" | "320" | "192" | "128"
};

void set(qint64 sourceId, const Server &s);
void remove(qint64 sourceId);
bool get(qint64 sourceId, Server &out);

// Reachability as last seen by the app (unknown counts as online). Lets
// recommendations leave out tracks of a server that is switched off.
void setOnline(qint64 sourceId, bool online);
bool isOnline(qint64 sourceId);

QString deviceId();                       // stable per installation
QString deviceName();
QString clientVersion();
// Value for the Authorization header ("MediaBrowser Client=…, Token=…").
QByteArray authHeader(const QString &token = {});

QByteArray streamUrl(qint64 sourceId, const QString &itemId);
QUrl imageUrl(qint64 sourceId, const QString &itemId, const QString &tag, int size);

// Blocking GET for worker threads (covers). status: HTTP status, 0 = no connection.
QByteArray httpGet(const QUrl &url, const QString &token, int timeoutMs, int *status);

// Server time format used by the API (ISO 8601, UTC).
QString isoTime(qint64 secsSinceEpoch);
qint64 parseTime(const QString &iso);

} // namespace JellyfinAccess
