// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <memory>

class Library;
class Player;
class QNetworkReply;
class JellyfinWriter;

// Jellyfin servers: sign-in (password or Quick Connect), library selection,
// incremental sync into the same database as the local music, streaming
// (see JellyfinAccess) and playback reporting back to the server.
//
// Everything network-related runs asynchronously on the main thread; parsing
// and database writes run in one worker thread, page by page.
class Jellyfin : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged)
    Q_PROPERTY(bool preferLocal READ preferLocal WRITE setPreferLocal NOTIFY preferLocalChanged)
    Q_PROPERTY(QString quickCode READ quickCode NOTIFY quickChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    Jellyfin(Player *player, Library *library, QObject *parent = nullptr);
    ~Jellyfin() override;

    QVariantList servers() const;
    bool preferLocal() const { return m_preferLocal; }
    void setPreferLocal(bool on);
    QString quickCode() const { return m_quickCode; }
    bool busy() const { return m_busy; }

    // Sign-in flow
    Q_INVOKABLE void probe(const QString &url);
    Q_INVOKABLE void login(const QString &url, const QString &user, const QString &password);
    Q_INVOKABLE void startQuickConnect(const QString &url);
    Q_INVOKABLE void cancelQuickConnect();

    // Per server
    Q_INVOKABLE void sync(qint64 serverId, bool full = false);
    Q_INVOKABLE void cancelSync(qint64 serverId);
    Q_INVOKABLE void remove(qint64 serverId);
    Q_INVOKABLE void refreshLibraries(qint64 serverId);
    Q_INVOKABLE void setLibraryEnabled(qint64 serverId, const QString &libraryId, bool on);
    Q_INVOKABLE void setServerOption(qint64 serverId, const QString &key, const QVariant &value);
    Q_INVOKABLE void checkOnline();

    // Call after the local scanner changed the library (duplicates may change).
    void localLibraryChanged();
    // On quit: tells the server playback stopped (waits at most ~1.5 s).
    void shutdown();

signals:
    void serversChanged();
    void preferLocalChanged();
    void quickChanged();
    void busyChanged();
    void probeResult(bool ok, const QString &url, const QString &serverName, const QString &version,
                     bool quickConnect, const QString &error);
    void loginResult(bool ok, const QString &error);
    void message(const QString &text);

private:
    struct Job;
    struct Server
    {
        qint64 id = 0;
        QString name, url, userId, userName, token, serverId, version;
        qint64 lastSync = 0;
        QJsonObject config;           // libraries, prefixes, quality, report, autoSync
        QVariantList libraries;       // [{id, name, enabled}] music libraries on the server
        QString status;               // "" unknown, "online", "offline", "denied"
        bool syncing = false;
        double progress = 0;
        QString syncText;
        int trackCount = 0;
        std::shared_ptr<Job> job;
    };

    // HTTP
    QNetworkReply *get(const QString &base, const QString &path, const QString &token, const QList<QPair<QString, QString>> &query = {});
    QNetworkReply *post(const QString &base, const QString &path, const QString &token, const QJsonObject &body);
    void onReply(QNetworkReply *r, std::function<void(int status, const QByteArray &data)> fn);

    void load();
    void save(const Server &s);
    Server *find(qint64 id);
    void publish(const Server &s);               // to JellyfinAccess (thread-safe copy)
    void countTracks(Server &s);
    void finishLogin(const QString &url, const QByteArray &authJson);
    QStringList enabledLibraries(const Server &s) const;

    // Sync state machine
    void syncNextLibrary(qint64 serverId);
    void requestPage(qint64 serverId);
    void requestCount(qint64 serverId);
    void requestIds(qint64 serverId);
    void finishSync(qint64 serverId, const QString &error = {});
    void runInWorker(std::function<void()> work, std::function<void()> done);

    // Favourites (the heart) set in the player: told to the server; kept until it
    // answered, so a sync cannot take them back meanwhile.
    void setFavorites(const QVariantList &items, bool on);
    void sendFavorite(qint64 serverId, const QString &itemId, bool on);
    void flushFavorites(qint64 serverId);
    QHash<QString, bool> pendingFavorites(qint64 serverId) const; // item id -> heart

    // Playback reporting
    void onTrackChanged();
    void onStateChanged();
    void reportStart();
    void reportProgress();
    QNetworkReply *reportStopped();
    void sendCapabilities(Server &s);
    void scheduleAutoSync();

    Player *m_player;
    Library *m_library;
    QNetworkAccessManager m_nam;
    QThreadPool m_pool;
    QVector<Server> m_servers;
    bool m_preferLocal = true;
    bool m_busy = false;
    QHash<QString, bool> m_sendingFavorites; // "server/item" -> heart, on the way

    QString m_quickUrl, m_quickSecret, m_quickCode;
    QTimer m_quickPoll;
    QElapsedTimer m_quickAge;

    // currently playing remote item
    qint64 m_playSource = 0;
    QString m_playItem;
    bool m_playStarted = false;
    QString m_playSession;
    qint64 m_lastPos = 0;
    QTimer m_progressTimer;
    QTimer m_autoSync;
};
