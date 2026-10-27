// SPDX-License-Identifier: GPL-3.0-or-later
#include "jellyfin/Jellyfin.h"

#include "core/Database.h"
#include "core/Sql.h"
#include "jellyfin/JellyfinAccess.h"
#include "jellyfin/JellyfinSync.h"
#include "library/Library.h"
#include "library/QueryBuilder.h"
#include "library/TrackCache.h"
#include "player/Player.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QHash>
#include <QJsonArray>
#include <QHostAddress>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QVersionNumber>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrlQuery>
#include <QUuid>
#include <QDebug>

using namespace JellyfinAccess;

namespace {
constexpr int FavoritesInFlight = 4;          // requests on the way at once
constexpr int FavoritesRetryMs = 2 * 60 * 1000; // after a failure: try again

// Hearts not taken by the server yet, next to the database they belong to.
QString pendingFavoritesFile()
{
    const QFileInfo db(Database::path());
    return db.absolutePath() + QLatin1Char('/') + db.completeBaseName() + QStringLiteral("-jellyfin-favorites.txt");
}

QString favoriteKey(qint64 serverId, const QString &itemId)
{
    return QString::number(serverId) + QLatin1Char('/') + itemId;
}

qint64 serverOfKey(const QString &key)
{
    return QStringView(key).left(key.indexOf(QLatin1Char('/'))).toLongLong();
}

constexpr int PageSize = 1000;
constexpr int IdPageSize = 5000;
const char *ItemFields = "Path,MediaSources,DateCreated,Genres";

QString cleanUrl(QString u)
{
    u = u.trimmed();
    while (u.endsWith(QLatin1Char('/')))
        u.chop(1);
    for (const char *suffix : {"/web/index.html", "/web/#", "/web"}) {
        const QString s = QLatin1String(suffix);
        if (u.endsWith(s, Qt::CaseInsensitive))
            u.chop(s.size());
    }
    while (u.endsWith(QLatin1Char('/')))
        u.chop(1);
    return u;
}

QString errorText(int status)
{
    if (status == 0)
        return QObject::tr("Server nicht erreichbar");
    if (status == 401 || status == 403)
        return QObject::tr("Anmeldung abgelehnt");
    return QObject::tr("Fehler vom Server (HTTP %1)").arg(status);
}
} // namespace

struct Jellyfin::Job
{
    bool full = false;
    bool cancelled = false;
    QStringList libs;
    int li = -1;
    QString lib, libName, prefix;
    bool libFull = false;
    int start = 0;
    int total = -1;
    QString since;
    int changed = 0, removed = 0;
    qint64 startedAt = 0;
    std::shared_ptr<JellyfinWriter> writer;
    QPointer<QNetworkReply> reply;
};

Jellyfin::Jellyfin(Player *player, Library *library, QObject *parent)
    : QObject(parent), m_player(player), m_library(library)
{
    m_pool.setMaxThreadCount(1);
    m_pool.setThreadPriority(QThread::LowPriority);
    m_preferLocal = QSettings().value(QStringLiteral("jellyfin/preferLocal"), true).toBool();
    QueryBuilder::setHideDuplicates(m_preferLocal);

    m_quickPoll.setInterval(2000);
    connect(&m_quickPoll, &QTimer::timeout, this, [this] {
        if (m_quickAge.elapsed() > 5 * 60 * 1000) {
            cancelQuickConnect();
            emit loginResult(false, tr("Quick Connect abgelaufen"));
            return;
        }
        auto *r = get(m_quickUrl, QStringLiteral("/QuickConnect/Connect"), {}, {{QStringLiteral("Secret"), m_quickSecret}});
        onReply(r, [this](int status, const QByteArray &data) {
            if (status != 200 || m_quickSecret.isEmpty())
                return;
            if (!QJsonDocument::fromJson(data).object().value(QLatin1String("Authenticated")).toBool())
                return;
            m_quickPoll.stop();
            const QString url = m_quickUrl;
            auto *a = post(url, QStringLiteral("/Users/AuthenticateWithQuickConnect"), {},
                           QJsonObject{{QStringLiteral("Secret"), m_quickSecret}});
            onReply(a, [this, url](int st, const QByteArray &auth) {
                m_quickSecret.clear();
                m_quickCode.clear();
                emit quickChanged();
                if (st != 200) {
                    emit loginResult(false, errorText(st));
                    return;
                }
                finishLogin(url, auth);
            });
        });
    });

    m_progressTimer.setInterval(10000);
    connect(&m_progressTimer, &QTimer::timeout, this, &Jellyfin::reportProgress);
    connect(m_player, &Player::trackChanged, this, &Jellyfin::onTrackChanged);
    connect(m_library, &Library::remoteFavoritesChanged, this, &Jellyfin::setFavorites);
    connect(m_player, &Player::stateChanged, this, &Jellyfin::onStateChanged);
    connect(m_player, &Player::positionChanged, this, [this] { m_lastPos = m_player->position(); });
    connect(m_player, &Player::seeked, this, [this] {
        m_lastPos = m_player->position();
        reportProgress();
    });

    // Keep the reachability for other parts (recommendations) up to date.
    connect(this, &Jellyfin::serversChanged, this, [this] {
        for (const Server &srv : std::as_const(m_servers))
            JellyfinAccess::setOnline(srv.id, srv.status != QLatin1String("offline") && srv.status != QLatin1String("denied"));
    });
    load();
    loadPendingFavorites();
    m_saveFavorites.setSingleShot(true);
    m_saveFavorites.setInterval(1000);
    connect(&m_saveFavorites, &QTimer::timeout, this, &Jellyfin::savePendingFavorites);
    m_retryFavorites.setSingleShot(true);
    m_retryFavorites.setInterval(FavoritesRetryMs);
    connect(&m_retryFavorites, &QTimer::timeout, this, [this] {
        // the server may have been updated meanwhile (the address depends on its version)
        const QSet<qint64> paused = std::exchange(m_favoritesPaused, {});
        for (qint64 id : paused)
            refreshVersion(id);
    });
    m_autoSync.setInterval(30 * 60 * 1000);
    connect(&m_autoSync, &QTimer::timeout, this, &Jellyfin::scheduleAutoSync);
    m_autoSync.start();
    QTimer::singleShot(8000, this, [this] {
        checkOnline();
        scheduleAutoSync();
    });
}

Jellyfin::~Jellyfin()
{
    for (Server &s : m_servers)
        if (s.job)
            s.job->cancelled = true;
    reportStopped();
    m_pool.waitForDone(5000);
}

// ---------------------------------------------------------------- HTTP

QNetworkReply *Jellyfin::get(const QString &base, const QString &path, const QString &token,
                             const QList<QPair<QString, QString>> &query)
{
    QUrl u(base + path);
    QUrlQuery q;
    for (const auto &kv : query)
        q.addQueryItem(kv.first, kv.second);
    u.setQuery(q);
    QNetworkRequest req(u);
    req.setRawHeader("Authorization", authHeader(token));
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(30000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *r = m_nam.get(req);
    connect(r, &QNetworkReply::finished, r, &QObject::deleteLater); // also fire-and-forget calls
    return r;
}

QNetworkReply *Jellyfin::post(const QString &base, const QString &path, const QString &token, const QJsonObject &body)
{
    QNetworkRequest req(QUrl(base + path));
    req.setRawHeader("Authorization", authHeader(token));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(30000);
    QNetworkReply *r = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, r, &QObject::deleteLater); // reports are fire-and-forget
    return r;
}

void Jellyfin::onReply(QNetworkReply *r, std::function<void(int, const QByteArray &)> fn)
{
    connect(r, &QNetworkReply::finished, this, [r, fn] {
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = r->readAll();
        if (r->error() == QNetworkReply::OperationCanceledError && status == 0)
            return; // aborted on purpose
        fn(status, data);
    });
}

// ---------------------------------------------------------------- servers

void Jellyfin::load()
{
    m_servers.clear();
    try {
        Stmt s(TrackCache::instance().db(),
               "SELECT id, name, url, user_id, token, last_sync, config FROM sources WHERE kind = 'jellyfin' ORDER BY id");
        while (s.step()) {
            Server srv;
            srv.id = s.i64(0);
            srv.name = s.str(1);
            srv.url = s.str(2);
            srv.userId = s.str(3);
            srv.token = s.str(4);
            srv.lastSync = s.i64(5);
            srv.config = QJsonDocument::fromJson(s.str(6).toUtf8()).object();
            srv.userName = srv.config.value(QLatin1String("userName")).toString();
            srv.serverId = srv.config.value(QLatin1String("serverId")).toString();
            srv.version = srv.config.value(QLatin1String("version")).toString();
            for (const QJsonValue &l : srv.config.value(QLatin1String("knownLibraries")).toArray()) {
                const QJsonObject o = l.toObject();
                srv.libraries << QVariantMap{{QStringLiteral("id"), o.value(QLatin1String("id")).toString()},
                                             {QStringLiteral("name"), o.value(QLatin1String("name")).toString()}};
            }
            countTracks(srv);
            publish(srv);
            m_servers << srv;
        }
    } catch (const std::exception &e) {
        qWarning() << "Jellyfin:" << e.what();
    }
}

void Jellyfin::save(const Server &s)
{
    try {
        Stmt u(TrackCache::instance().db(), "UPDATE sources SET name = ?, url = ?, user_id = ?, token = ?, last_sync = ?, config = ? WHERE id = ?");
        u.bind(1, s.name).bind(2, s.url).bind(3, s.userId).bind(4, s.token).bind(5, s.lastSync)
            .bind(6, QString::fromUtf8(QJsonDocument(s.config).toJson(QJsonDocument::Compact))).bind(7, s.id);
        u.exec();
    } catch (const std::exception &e) {
        qWarning() << "Jellyfin:" << e.what();
    }
}

Jellyfin::Server *Jellyfin::find(qint64 id)
{
    for (Server &s : m_servers)
        if (s.id == id)
            return &s;
    return nullptr;
}

void Jellyfin::publish(const Server &s)
{
    JellyfinAccess::set(s.id, {s.url, s.token, s.userId, s.config.value(QLatin1String("quality")).toString(QStringLiteral("original"))});
}

void Jellyfin::countTracks(Server &s)
{
    try {
        Stmt c(TrackCache::instance().db(), "SELECT COUNT(*) FROM tracks WHERE source_id = ?");
        c.bind(1, s.id);
        s.trackCount = c.step() ? c.i32(0) : 0;
    } catch (const std::exception &) {
    }
}

QStringList Jellyfin::enabledLibraries(const Server &s) const
{
    QStringList out;
    if (!s.config.contains(QLatin1String("libraries"))) {
        for (const QVariant &l : s.libraries)
            out << l.toMap().value(QStringLiteral("id")).toString();
        return out;
    }
    for (const QJsonValue &v : s.config.value(QLatin1String("libraries")).toArray())
        out << v.toString();
    return out;
}

QVariantList Jellyfin::servers() const
{
    QVariantList out;
    for (const Server &s : m_servers) {
        const QStringList enabled = enabledLibraries(s);
        QVariantList libs;
        for (const QVariant &l : s.libraries) {
            QVariantMap m = l.toMap();
            m.insert(QStringLiteral("enabled"), enabled.contains(m.value(QStringLiteral("id")).toString()));
            libs << m;
        }
        out << QVariantMap{
            {QStringLiteral("id"), s.id},
            {QStringLiteral("name"), s.name},
            {QStringLiteral("url"), s.url},
            {QStringLiteral("userName"), s.userName},
            {QStringLiteral("version"), s.version},
            {QStringLiteral("status"), s.status},
            {QStringLiteral("syncing"), s.syncing},
            {QStringLiteral("progress"), s.progress},
            {QStringLiteral("syncText"), s.syncText},
            {QStringLiteral("lastSync"), s.lastSync},
            {QStringLiteral("trackCount"), s.trackCount},
            {QStringLiteral("libraries"), libs},
            {QStringLiteral("quality"), s.config.value(QLatin1String("quality")).toString(QStringLiteral("original"))},
            {QStringLiteral("report"), s.config.value(QLatin1String("report")).toBool(true)},
            {QStringLiteral("autoSync"), s.config.value(QLatin1String("autoSync")).toString(QStringLiteral("start"))},
        };
    }
    return out;
}

void Jellyfin::setPreferLocal(bool on)
{
    if (on == m_preferLocal)
        return;
    m_preferLocal = on;
    QSettings().setValue(QStringLiteral("jellyfin/preferLocal"), on);
    QueryBuilder::setHideDuplicates(on);
    emit preferLocalChanged();
    m_library->notifyExternalChange();
}

// ---------------------------------------------------------------- sign-in

namespace {
// Loopback, private IPv4 ranges, IPv6 link-local/unique-local, names without a
// dot or ending in .local/.lan/.home: plain http there stays in the home network.
bool inHomeNetwork(const QString &host)
{
    const QHostAddress a(host);
    if (!a.isNull()) {
        if (a.isLoopback() || a.isLinkLocal())
            return true;
        if (a.protocol() == QAbstractSocket::IPv4Protocol) {
            const quint32 v = a.toIPv4Address();
            return (v >> 24) == 10 || (v >> 20) == (172u << 4 | 1) || (v >> 16) == (192u << 8 | 168) || (v >> 22) == (100u << 2 | 1);
        }
        return (a.toIPv6Address()[0] & 0xfe) == 0xfc; // fc00::/7
    }
    const QString h = host.toLower();
    return !h.contains(QLatin1Char('.')) || h.endsWith(QLatin1String(".local")) || h.endsWith(QLatin1String(".lan"))
           || h.endsWith(QLatin1String(".home")) || h.endsWith(QLatin1String(".internal"));
}
} // namespace

void Jellyfin::probe(const QString &rawUrl)
{
    const QString cleaned = cleanUrl(rawUrl);
    QStringList candidates;
    if (cleaned.contains(QLatin1String("://"))) {
        candidates << cleaned;
    } else if (!cleaned.isEmpty()) {
        // encrypted first: the password must not go over plain http when https works
        candidates << QStringLiteral("https://") + cleaned << QStringLiteral("http://") + cleaned;
        if (!cleaned.contains(QLatin1Char(':')))
            candidates << QStringLiteral("http://%1:8096").arg(cleaned);
    }
    if (candidates.isEmpty()) {
        emit probeResult(false, {}, {}, {}, false, tr("Bitte eine Adresse eingeben"));
        return;
    }
    m_busy = true;
    emit busyChanged();
    auto tryNext = std::make_shared<std::function<void(int)>>();
    *tryNext = [this, candidates, tryNext](int i) {
        if (i >= candidates.size()) {
            m_busy = false;
            emit busyChanged();
            emit probeResult(false, {}, {}, {}, false, tr("Kein Jellyfin-Server unter dieser Adresse gefunden"));
            return;
        }
        const QString url = candidates[i];
        onReply(get(url, QStringLiteral("/System/Info/Public"), {}), [this, url, i, tryNext](int status, const QByteArray &data) {
            const QJsonObject info = QJsonDocument::fromJson(data).object();
            if (status != 200 || !info.contains(QLatin1String("Id"))) {
                (*tryNext)(i + 1);
                return;
            }
            const QString name = info.value(QLatin1String("ServerName")).toString();
            const QString version = info.value(QLatin1String("Version")).toString();
            onReply(get(url, QStringLiteral("/QuickConnect/Enabled"), {}), [this, url, name, version](int st, const QByteArray &d) {
                m_busy = false;
                emit busyChanged();
                const bool quick = st == 200 && d.trimmed() == "true";
                if (url.startsWith(QLatin1String("http://")) && !inHomeNetwork(QUrl(url).host()))
                    emit message(tr("Achtung: Dieser Server antwortet ohne Verschlüsselung (http). Passwort und Zugangsschlüssel "
                                    "gehen dann lesbar durchs Netz – über das Internet besser https verwenden."));
                emit probeResult(true, url, name, version, quick, {});
            });
        });
    };
    (*tryNext)(0);
}

void Jellyfin::login(const QString &url, const QString &user, const QString &password)
{
    m_busy = true;
    emit busyChanged();
    auto *r = post(cleanUrl(url), QStringLiteral("/Users/AuthenticateByName"), {},
                   QJsonObject{{QStringLiteral("Username"), user}, {QStringLiteral("Pw"), password}});
    const QString u = cleanUrl(url);
    onReply(r, [this, u](int status, const QByteArray &data) {
        m_busy = false;
        emit busyChanged();
        if (status != 200) {
            emit loginResult(false, status == 401 ? tr("Benutzername oder Passwort falsch") : errorText(status));
            return;
        }
        finishLogin(u, data);
    });
}

void Jellyfin::startQuickConnect(const QString &url)
{
    cancelQuickConnect();
    m_quickUrl = cleanUrl(url);
    auto handle = [this](int status, const QByteArray &data) {
        const QJsonObject o = QJsonDocument::fromJson(data).object();
        if (status != 200 || o.value(QLatin1String("Secret")).toString().isEmpty()) {
            emit loginResult(false, status == 401 || status == 403 ? tr("Quick Connect ist auf dem Server ausgeschaltet") : errorText(status));
            return;
        }
        m_quickSecret = o.value(QLatin1String("Secret")).toString();
        m_quickCode = o.value(QLatin1String("Code")).toString();
        m_quickAge.start();
        m_quickPoll.start();
        emit quickChanged();
    };
    // 10.9+: POST; older servers: GET.
    onReply(post(m_quickUrl, QStringLiteral("/QuickConnect/Initiate"), {}, {}), [this, handle](int status, const QByteArray &data) {
        if (status == 404 || status == 405)
            onReply(get(m_quickUrl, QStringLiteral("/QuickConnect/Initiate"), {}), handle);
        else
            handle(status, data);
    });
}

void Jellyfin::cancelQuickConnect()
{
    m_quickPoll.stop();
    m_quickSecret.clear();
    if (!m_quickCode.isEmpty()) {
        m_quickCode.clear();
        emit quickChanged();
    }
}

void Jellyfin::finishLogin(const QString &url, const QByteArray &authJson)
{
    const QJsonObject auth = QJsonDocument::fromJson(authJson).object();
    const QString token = auth.value(QLatin1String("AccessToken")).toString();
    const QJsonObject user = auth.value(QLatin1String("User")).toObject();
    const QString userId = user.value(QLatin1String("Id")).toString();
    const QString serverId = auth.value(QLatin1String("ServerId")).toString();
    if (token.isEmpty() || userId.isEmpty()) {
        emit loginResult(false, tr("Unerwartete Antwort vom Server"));
        return;
    }
    // Same server and user again: just renew the token.
    Server *existing = nullptr;
    for (Server &s : m_servers)
        if ((s.serverId == serverId && s.userId == userId) || (s.url == url && s.userId == userId))
            existing = &s;
    if (!existing) {
        Server s;
        s.url = url;
        s.name = QUrl(url).host();
        s.config.insert(QStringLiteral("quality"), QStringLiteral("original"));
        s.config.insert(QStringLiteral("report"), true);
        s.config.insert(QStringLiteral("autoSync"), QStringLiteral("start"));
        try {
            Stmt ins(TrackCache::instance().db(), "INSERT INTO sources(kind, name, url) VALUES ('jellyfin', ?, ?)");
            ins.bind(1, s.name).bind(2, url);
            ins.exec();
            s.id = sqlite3_last_insert_rowid(TrackCache::instance().db());
        } catch (const std::exception &e) {
            emit loginResult(false, QString::fromUtf8(e.what()));
            return;
        }
        m_servers << s;
        existing = &m_servers.last();
    }
    existing->url = url;
    existing->token = token;
    existing->userId = userId;
    existing->userName = user.value(QLatin1String("Name")).toString();
    existing->serverId = serverId;
    existing->status = QStringLiteral("online");
    existing->config.insert(QStringLiteral("userName"), existing->userName);
    existing->config.insert(QStringLiteral("serverId"), serverId);
    save(*existing);
    publish(*existing);
    emit serversChanged();
    emit loginResult(true, {});

    // Server name and version, libraries, then the first sync.
    const qint64 id = existing->id;
    // The server name comes first: it names the library roots in the folder view.
    onReply(get(url, QStringLiteral("/System/Info/Public"), {}), [this, id](int status, const QByteArray &data) {
        Server *s = find(id);
        if (!s)
            return;
        if (status == 200) {
            const QJsonObject info = QJsonDocument::fromJson(data).object();
            s->name = info.value(QLatin1String("ServerName")).toString(s->name);
            s->version = info.value(QLatin1String("Version")).toString();
            s->config.insert(QStringLiteral("version"), s->version);
            save(*s);
            emit serversChanged();
        }
        refreshLibraries(id);
    });
    sendCapabilities(*existing);
}

void Jellyfin::refreshLibraries(qint64 serverId)
{
    Server *s = find(serverId);
    if (!s)
        return;
    const bool firstTime = s->lastSync == 0 && !s->syncing;
    onReply(get(s->url, QStringLiteral("/UserViews"), s->token, {{QStringLiteral("userId"), s->userId}}),
            [this, serverId, firstTime](int status, const QByteArray &data) {
        Server *s = find(serverId);
        if (!s)
            return;
        if (status != 200) {
            s->status = status == 0 ? QStringLiteral("offline") : (status == 401 ? QStringLiteral("denied") : s->status);
            emit serversChanged();
            return;
        }
        QVariantList libs;
        QJsonArray known;
        for (const QJsonValue &v : QJsonDocument::fromJson(data).object().value(QLatin1String("Items")).toArray()) {
            const QJsonObject o = v.toObject();
            if (o.value(QLatin1String("CollectionType")).toString() != QLatin1String("music"))
                continue;
            const QString id = o.value(QLatin1String("Id")).toString();
            const QString name = o.value(QLatin1String("Name")).toString();
            libs << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}};
            known << QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}};
        }
        s->libraries = libs;
        s->config.insert(QStringLiteral("knownLibraries"), known);
        save(*s);
        emit serversChanged();
        if (firstTime)
            sync(serverId, true);
    });
}

void Jellyfin::setLibraryEnabled(qint64 serverId, const QString &libraryId, bool on)
{
    Server *s = find(serverId);
    if (!s)
        return;
    QStringList libs = enabledLibraries(*s);
    if (on && !libs.contains(libraryId))
        libs << libraryId;
    if (!on)
        libs.removeAll(libraryId);
    s->config.insert(QStringLiteral("libraries"), QJsonArray::fromStringList(libs));
    save(*s);
    emit serversChanged();
    // Newly added libraries get a full sync; removed ones are dropped by the sync.
    sync(serverId, false);
}

void Jellyfin::setServerOption(qint64 serverId, const QString &key, const QVariant &value)
{
    Server *s = find(serverId);
    if (!s)
        return;
    s->config.insert(key, QJsonValue::fromVariant(value));
    save(*s);
    publish(*s);
    emit serversChanged();
}

void Jellyfin::remove(qint64 serverId)
{
    Server *s = find(serverId);
    if (!s)
        return;
    if (s->job)
        s->job->cancelled = true;
    // Sign out on the server (best effort).
    post(s->url, QStringLiteral("/Sessions/Logout"), s->token, {});
    JellyfinAccess::remove(serverId);
    // its hearts that were still to be sent
    const QHash<QString, bool> pending = pendingFavorites(serverId);
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        m_pendingFavorites.remove(favoriteKey(serverId, it.key()));
    m_favoritesPaused.remove(serverId);
    m_confirmedDuringSync.remove(serverId);
    m_saveFavorites.start();
    for (int i = 0; i < m_servers.size(); ++i)
        if (m_servers[i].id == serverId)
            m_servers.removeAt(i);
    emit serversChanged();
    runInWorker([serverId] { JellyfinWriter::removeSource(serverId); },
                [this] {
                    m_library->notifyExternalChange();
                    emit message(tr("Jellyfin-Server entfernt"));
                });
}

void Jellyfin::checkOnline()
{
    for (const Server &srv : m_servers) {
        const qint64 id = srv.id;
        onReply(get(srv.url, QStringLiteral("/Users/Me"), srv.token), [this, id](int status, const QByteArray &) {
            Server *s = find(id);
            if (!s)
                return;
            s->status = status == 200 ? QStringLiteral("online")
                      : status == 0 ? QStringLiteral("offline")
                      : status == 401 ? QStringLiteral("denied")
                      : QStringLiteral("online"); // older servers: no /Users/Me
            emit serversChanged();
            if (s->status == QLatin1String("online"))
                refreshVersion(id); // then sends what is pending
            else if (!pendingFavorites(id).isEmpty())
                pauseFavorites(id); // and try again later
        });
    }
}

// The server may have been updated since the login (the address for hearts depends on it).
void Jellyfin::refreshVersion(qint64 serverId)
{
    const Server *srv = find(serverId);
    if (!srv)
        return;
    onReply(get(srv->url, QStringLiteral("/System/Info/Public"), {}), [this, serverId](int status, const QByteArray &data) {
        Server *s = find(serverId);
        if (!s)
            return;
        const QString version = QJsonDocument::fromJson(data).object().value(QLatin1String("Version")).toString();
        if (status == 200 && !version.isEmpty() && version != s->version) {
            s->version = version;
            s->config.insert(QStringLiteral("version"), version);
            save(*s);
        }
        flushFavorites(serverId);
    });
}

// ---------------------------------------------------------------- favourites

void Jellyfin::loadPendingFavorites()
{
    // one line per heart: "server/item<TAB>1" (linear to read and write, also for many)
    QFile f(pendingFavoritesFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    QHash<qint64, QHash<QString, bool>> perServer;
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray &line : lines) {
        const qsizetype tab = line.indexOf('\t');
        if (tab <= 0)
            continue;
        const QString key = QString::fromUtf8(line.left(tab));
        const bool on = line.mid(tab + 1).trimmed() == "1";
        m_pendingFavorites.insert(key, on);
        perServer[serverOfKey(key)].insert(key.mid(key.indexOf(QLatin1Char('/')) + 1), on);
    }
    // a sync that ran when the player was closed may have written over them
    for (auto it = perServer.cbegin(); it != perServer.cend(); ++it)
        m_library->restoreRemoteHearts(it.key(), it.value());
}

void Jellyfin::savePendingFavorites()
{
    const QString file = pendingFavoritesFile();
    if (m_pendingFavorites.isEmpty()) {
        QFile::remove(file);
        return;
    }
    QByteArray out;
    out.reserve(m_pendingFavorites.size() * 48);
    for (auto it = m_pendingFavorites.cbegin(); it != m_pendingFavorites.cend(); ++it)
        out += it.key().toUtf8() + (it.value() ? "\t1\n" : "\t0\n");
    QSaveFile f(file);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(out);
        f.commit();
    }
}

void Jellyfin::setFavorites(const RemoteItems &items, bool on)
{
    for (const auto &[sid, item] : items) {
        if (!find(sid) || item.isEmpty())
            continue;
        const QString key = favoriteKey(sid, item);
        m_pendingFavorites.insert(key, on);
        m_favoriteQueue.insert(key);
    }
    m_saveFavorites.start();
    if (!m_favoritesPaused.isEmpty() && !m_retryFavorites.isActive())
        m_retryFavorites.start();
    sendFavorites();
}

void Jellyfin::pauseFavorites(qint64 serverId)
{
    m_favoritesPaused.insert(serverId);
    if (!m_retryFavorites.isActive())
        m_retryFavorites.start();
}

// A few at a time, at most one per title: the last click is what the server keeps.
void Jellyfin::sendFavorites()
{
    while (!m_favoritesStopped && m_favoritesInFlight.size() < FavoritesInFlight && !m_favoriteQueue.isEmpty()) {
        const QString key = *m_favoriteQueue.cbegin();
        m_favoriteQueue.erase(m_favoriteQueue.cbegin());
        auto want = m_pendingFavorites.constFind(key);
        if (want == m_pendingFavorites.cend() || m_favoritesInFlight.contains(key))
            continue; // done meanwhile, or on the way (checked again when it is back)
        const qint64 sid = serverOfKey(key);
        if (m_favoritesPaused.contains(sid))
            continue; // the retry queues it again
        Server *s = find(sid);
        if (!s) {
            m_pendingFavorites.remove(key); // server removed
            m_saveFavorites.start();
            continue;
        }
        if (s->token.isEmpty() || s->userId.isEmpty())
            continue;
        const bool on = want.value();
        const QString item = key.mid(key.indexOf(QLatin1Char('/')) + 1);
        // 10.9 and later: /UserFavoriteItems; older ones only know the user in the path
        QUrl url(s->url + QStringLiteral("/UserFavoriteItems/") + item);
        if (QVersionNumber::fromString(s->version) >= QVersionNumber(10, 9))
            url.setQuery(QStringLiteral("userId=") + s->userId);
        else
            url = QUrl(s->url + QStringLiteral("/Users/%1/FavoriteItems/%2").arg(s->userId, item));
        QNetworkRequest req(url);
        req.setRawHeader("Authorization", authHeader(s->token));
        req.setTransferTimeout(30000);
        QNetworkReply *r = on ? m_nam.post(req, QByteArray()) : m_nam.deleteResource(req);
        m_favoritesInFlight.insert(key);
        connect(r, &QNetworkReply::finished, this, [this, r, key, sid, item, on] {
            r->deleteLater();
            m_favoritesInFlight.remove(key);
            const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            auto want = m_pendingFavorites.constFind(key);
            if (want != m_pendingFavorites.cend() && want.value() != on) {
                m_favoriteQueue.insert(key); // clicked again meanwhile
            } else if ((status >= 200 && status < 300) || status == 400 || status == 404 || status == 410) {
                // done, or the server will never take it (the title is gone there)
                m_pendingFavorites.remove(key);
                m_saveFavorites.start();
                // a running sync may still write the state it fetched before
                const Server *s = find(sid);
                if (s && s->syncing && status < 300)
                    m_confirmedDuringSync[sid].insert(item, on);
            } else if (want != m_pendingFavorites.cend()) {
                pauseFavorites(sid); // server away (or refusing): all of it again later
            }
            sendFavorites();
        });
    }
}

void Jellyfin::flushFavorites(qint64 serverId)
{
    m_favoritesPaused.remove(serverId);
    const QHash<QString, bool> pending = pendingFavorites(serverId);
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        m_favoriteQueue.insert(favoriteKey(serverId, it.key()));
    sendFavorites();
}

QHash<QString, bool> Jellyfin::pendingFavorites(qint64 serverId) const
{
    QHash<QString, bool> out;
    const QString prefix = QString::number(serverId) + QLatin1Char('/');
    for (auto it = m_pendingFavorites.cbegin(); it != m_pendingFavorites.cend(); ++it)
        if (it.key().startsWith(prefix))
            out.insert(it.key().mid(prefix.size()), it.value());
    return out;
}

// ---------------------------------------------------------------- sync

void Jellyfin::runInWorker(std::function<void()> work, std::function<void()> done)
{
    QPointer<Jellyfin> self(this);
    m_pool.start([self, work, done] {
        work();
        QMetaObject::invokeMethod(qApp, [self, done] {
            if (self && done)
                done();
        }, Qt::QueuedConnection);
    });
}

void Jellyfin::sync(qint64 serverId, bool full)
{
    Server *s = find(serverId);
    if (!s || s->syncing || s->token.isEmpty())
        return;
    auto job = std::make_shared<Job>();
    job->full = full || s->lastSync == 0;
    job->libs = enabledLibraries(*s);
    job->startedAt = QDateTime::currentSecsSinceEpoch();
    job->writer = std::make_shared<JellyfinWriter>(s->id, s->name);
    s->job = job;
    s->syncing = true;
    s->progress = 0;
    m_confirmedDuringSync.remove(serverId); // left from a sync that ended before they came
    s->syncText = tr("Abgleich startet …");
    emit serversChanged();
    flushFavorites(serverId); // hearts set while the server was away

    // Drop libraries that are no longer selected; keep the folder view's root
    // names ("Server · Library") in step with renames on the server.
    const QStringList keep = job->libs;
    auto writer = job->writer;
    const qint64 sid = s->id;
    QHash<QString, QString> rootNames;
    for (const QVariant &l : s->libraries) {
        const QVariantMap m = l.toMap();
        rootNames.insert(QStringLiteral("jellyfin:%1:%2").arg(sid).arg(m.value(QStringLiteral("id")).toString()),
                         s->name + QStringLiteral(" · ") + m.value(QStringLiteral("name")).toString());
    }
    auto removed = std::make_shared<int>(0);
    runInWorker([writer, keep, sid, removed, rootNames] {
        try {
            DbConnection db = Database::open(2048);
            Stmt rn(db, "UPDATE folders SET name = ? WHERE path = ? AND name <> ?");
            for (auto it = rootNames.cbegin(); it != rootNames.cend(); ++it) {
                rn.bind(1, it.value()).bind(2, it.key()).bind(3, it.value());
                rn.exec();
            }
            Stmt q(db, "SELECT DISTINCT remote_lib FROM tracks WHERE source_id = ?");
            q.bind(1, sid);
            QStringList present;
            while (q.step())
                present << q.str(0);
            for (const QString &lib : present)
                if (!keep.contains(lib))
                    *removed += writer->removeLibrary(lib);
        } catch (const std::exception &e) {
            qWarning() << "Jellyfin:" << e.what();
        }
    }, [this, serverId, job, removed] {
        job->removed += *removed;
        syncNextLibrary(serverId);
    });
}

void Jellyfin::cancelSync(qint64 serverId)
{
    Server *s = find(serverId);
    if (!s || !s->job)
        return;
    s->job->cancelled = true;
    if (s->job->reply)
        s->job->reply->abort();
    finishSync(serverId, tr("Abgebrochen"));
}

void Jellyfin::syncNextLibrary(qint64 serverId)
{
    Server *s = find(serverId);
    if (!s || !s->job || s->job->cancelled)
        return;
    Job &j = *s->job;
    if (++j.li >= j.libs.size()) {
        finishSync(serverId);
        return;
    }
    j.lib = j.libs[j.li];
    j.libName = j.lib;
    for (const QVariant &l : s->libraries)
        if (l.toMap().value(QStringLiteral("id")).toString() == j.lib)
            j.libName = l.toMap().value(QStringLiteral("name")).toString();
    // A library we have never synced gets a full pass.
    const QJsonObject prefixes = s->config.value(QLatin1String("prefixes")).toObject();
    j.libFull = j.full || !prefixes.contains(j.lib);
    j.since = j.libFull ? QString() : isoTime(s->lastSync - 600);
    j.start = 0;
    j.total = -1;
    j.prefix = prefixes.value(j.lib).toString();

    auto startPages = [this, serverId] {
        Server *s = find(serverId);
        if (!s || !s->job)
            return;
        auto writer = s->job->writer;
        if (s->job->libFull)
            runInWorker([writer] { writer->resetSeen(); }, [this, serverId] { requestPage(serverId); });
        else
            requestPage(serverId);
    };
    if (prefixes.contains(j.lib)) {
        startPages();
        return;
    }
    // First time: find the library's root directory from a sample of paths.
    auto *r = get(s->url, QStringLiteral("/Items"), s->token,
                  {{QStringLiteral("userId"), s->userId}, {QStringLiteral("ParentId"), j.lib},
                   {QStringLiteral("IncludeItemTypes"), QStringLiteral("Audio")}, {QStringLiteral("Recursive"), QStringLiteral("true")},
                   {QStringLiteral("Fields"), QStringLiteral("Path")}, {QStringLiteral("SortBy"), QStringLiteral("SortName")},
                   {QStringLiteral("Limit"), QStringLiteral("300")}, {QStringLiteral("EnableImages"), QStringLiteral("false")},
                   {QStringLiteral("EnableUserData"), QStringLiteral("false")}});
    j.reply = r;
    onReply(r, [this, serverId, startPages](int status, const QByteArray &data) {
        Server *s = find(serverId);
        if (!s || !s->job || s->job->cancelled)
            return;
        if (status != 200) {
            finishSync(serverId, errorText(status));
            return;
        }
        QJsonObject prefixes = s->config.value(QLatin1String("prefixes")).toObject();
        s->job->prefix = JellyfinWriter::commonPrefix(data);
        prefixes.insert(s->job->lib, s->job->prefix);
        s->config.insert(QStringLiteral("prefixes"), prefixes);
        save(*s);
        startPages();
    });
}

void Jellyfin::requestPage(qint64 serverId)
{
    Server *s = find(serverId);
    if (!s || !s->job || s->job->cancelled)
        return;
    Job &j = *s->job;
    QList<QPair<QString, QString>> q = {
        {QStringLiteral("userId"), s->userId}, {QStringLiteral("ParentId"), j.lib},
        {QStringLiteral("IncludeItemTypes"), QStringLiteral("Audio")}, {QStringLiteral("Recursive"), QStringLiteral("true")},
        {QStringLiteral("Fields"), QLatin1String(ItemFields)}, {QStringLiteral("EnableImageTypes"), QStringLiteral("Primary")},
        {QStringLiteral("ImageTypeLimit"), QStringLiteral("1")}, {QStringLiteral("EnableUserData"), QStringLiteral("true")},
        {QStringLiteral("SortBy"), QStringLiteral("SortName")}, {QStringLiteral("SortOrder"), QStringLiteral("Ascending")},
        {QStringLiteral("StartIndex"), QString::number(j.start)}, {QStringLiteral("Limit"), QString::number(PageSize)}};
    if (!j.since.isEmpty())
        q << qMakePair(QStringLiteral("MinDateLastSaved"), j.since);
    auto *r = get(s->url, QStringLiteral("/Items"), s->token, q);
    j.reply = r;
    onReply(r, [this, serverId](int status, const QByteArray &data) {
        Server *s = find(serverId);
        if (!s || !s->job || s->job->cancelled)
            return;
        if (status != 200) {
            if (status == 401)
                s->status = QStringLiteral("denied");
            finishSync(serverId, status == 401 ? tr("Anmeldung abgelaufen, bitte neu anmelden") : errorText(status));
            return;
        }
        auto job = s->job;
        auto result = std::make_shared<JellyfinWriter::Page>();
        const QString lib = job->lib, libName = job->libName, prefix = job->prefix;
        const bool seen = job->libFull;
        auto writer = job->writer;
        runInWorker([writer, result, data, lib, libName, prefix, seen] {
            *result = writer->writePage(data, lib, libName, prefix, seen);
        }, [this, serverId, job, result] {
            Server *s = find(serverId);
            if (!s || s->job != job || job->cancelled)
                return;
            if (!result->error.isEmpty()) {
                finishSync(serverId, result->error);
                return;
            }
            job->changed += result->changed;
            job->total = result->total;
            job->start += result->items;
            const double libPart = job->total > 0 ? std::min(1.0, double(job->start) / job->total) : 1.0;
            s->progress = (job->li + libPart) / std::max<qsizetype>(1, job->libs.size());
            s->syncText = job->libFull
                ? tr("%1: %2 von %3 Titeln").arg(job->libName, m_library->formatCount(job->start), m_library->formatCount(job->total))
                : tr("%1: Änderungen prüfen …").arg(job->libName);
            emit serversChanged();
            if (result->items > 0 && job->start < job->total) {
                requestPage(serverId);
                return;
            }
            // Library done.
            if (job->libFull) {
                auto writer = job->writer;
                const QString lib = job->lib;
                auto removed = std::make_shared<int>(0);
                runInWorker([writer, lib, removed] { *removed = writer->removeUnseen(lib); },
                            [this, serverId, job, removed] {
                                job->removed += *removed;
                                syncNextLibrary(serverId);
                            });
            } else {
                requestCount(serverId);
            }
        });
    });
}

// After an incremental pass: does the number of tracks still match? Fewer on
// the server means something was deleted: fetch all ids (cheap) and remove the
// rest. More means we missed something: run a full pass for this library.
void Jellyfin::requestCount(qint64 serverId)
{
    Server *s = find(serverId);
    if (!s || !s->job || s->job->cancelled)
        return;
    Job &j = *s->job;
    auto *r = get(s->url, QStringLiteral("/Items"), s->token,
                  {{QStringLiteral("userId"), s->userId}, {QStringLiteral("ParentId"), j.lib},
                   {QStringLiteral("IncludeItemTypes"), QStringLiteral("Audio")}, {QStringLiteral("Recursive"), QStringLiteral("true")},
                   {QStringLiteral("Limit"), QStringLiteral("0")}, {QStringLiteral("EnableTotalRecordCount"), QStringLiteral("true")}});
    j.reply = r;
    onReply(r, [this, serverId](int status, const QByteArray &data) {
        Server *s = find(serverId);
        if (!s || !s->job || s->job->cancelled)
            return;
        if (status != 200) {
            finishSync(serverId, errorText(status));
            return;
        }
        const int serverCount = QJsonDocument::fromJson(data).object().value(QLatin1String("TotalRecordCount")).toInt(-1);
        auto job = s->job;
        auto writer = job->writer;
        const QString lib = job->lib;
        auto local = std::make_shared<int>(0);
        runInWorker([writer, lib, local] { *local = writer->localCount(lib); }, [this, serverId, job, serverCount, local] {
            Server *s = find(serverId);
            if (!s || s->job != job || job->cancelled)
                return;
            if (serverCount < 0 || serverCount == *local) {
                syncNextLibrary(serverId);
            } else if (serverCount < *local) {
                job->start = 0;
                auto writer = job->writer;
                runInWorker([writer] { writer->resetSeen(); }, [this, serverId] { requestIds(serverId); });
            } else {
                job->libFull = true;
                job->since.clear();
                job->start = 0;
                auto writer = job->writer;
                runInWorker([writer] { writer->resetSeen(); }, [this, serverId] { requestPage(serverId); });
            }
        });
    });
}

void Jellyfin::requestIds(qint64 serverId)
{
    Server *s = find(serverId);
    if (!s || !s->job || s->job->cancelled)
        return;
    Job &j = *s->job;
    auto *r = get(s->url, QStringLiteral("/Items"), s->token,
                  {{QStringLiteral("userId"), s->userId}, {QStringLiteral("ParentId"), j.lib},
                   {QStringLiteral("IncludeItemTypes"), QStringLiteral("Audio")}, {QStringLiteral("Recursive"), QStringLiteral("true")},
                   {QStringLiteral("EnableImages"), QStringLiteral("false")}, {QStringLiteral("EnableUserData"), QStringLiteral("false")},
                   {QStringLiteral("SortBy"), QStringLiteral("SortName")},
                   {QStringLiteral("StartIndex"), QString::number(j.start)}, {QStringLiteral("Limit"), QString::number(IdPageSize)}});
    j.reply = r;
    onReply(r, [this, serverId](int status, const QByteArray &data) {
        Server *s = find(serverId);
        if (!s || !s->job || s->job->cancelled)
            return;
        if (status != 200) {
            finishSync(serverId, errorText(status));
            return;
        }
        auto job = s->job;
        auto writer = job->writer;
        auto result = std::make_shared<JellyfinWriter::Page>();
        runInWorker([writer, data, result] { *result = writer->seenPage(data); }, [this, serverId, job, result] {
            Server *s = find(serverId);
            if (!s || s->job != job || job->cancelled)
                return;
            job->start += result->items;
            if (result->items > 0 && job->start < result->total) {
                requestIds(serverId);
                return;
            }
            auto writer = job->writer;
            const QString lib = job->lib;
            auto removed = std::make_shared<int>(0);
            runInWorker([writer, lib, removed] { *removed = writer->removeUnseen(lib); }, [this, serverId, job, removed] {
                job->removed += *removed;
                syncNextLibrary(serverId);
            });
        });
    });
}

void Jellyfin::finishSync(qint64 serverId, const QString &error)
{
    Server *s = find(serverId);
    if (!s || !s->job)
        return;
    auto job = s->job;
    const bool ok = error.isEmpty() && !job->cancelled;
    auto writer = job->writer;
    auto dupChanged = std::make_shared<int>(0);
    runInWorker([writer, ok, dupChanged] {
        if (ok)
            writer->pruneFolders();
        *dupChanged = writer->markDuplicates();
    }, [this, serverId, job, ok, error, dupChanged] {
        Server *s = find(serverId);
        if (!s)
            return;
        s->syncing = false;
        s->job.reset();
        // Hearts set here stay as set here: the sync wrote the server's state from
        // before (those still to be sent, and those the server took meanwhile). Through
        // the library's writer, in order with new clicks.
        QHash<QString, bool> mine = m_confirmedDuringSync.take(serverId);
        mine.insert(pendingFavorites(serverId));
        if (!mine.isEmpty())
            m_library->restoreRemoteHearts(serverId, mine);
        s->progress = ok ? 1 : 0;
        if (ok) {
            s->lastSync = job->startedAt;
            s->status = QStringLiteral("online");
            s->syncText = (job->changed || job->removed)
                ? tr("%1 Titel neu oder geändert, %2 entfernt").arg(m_library->formatCount(job->changed), m_library->formatCount(job->removed))
                : tr("Alles aktuell");
            save(*s);
        } else {
            s->syncText = error;
            if (error == tr("Server nicht erreichbar"))
                s->status = QStringLiteral("offline");
        }
        countTracks(*s);
        emit serversChanged();
        if (job->changed || job->removed || *dupChanged)
            m_library->notifyExternalChange();
        const QString who = s->name.contains(QLatin1String("jellyfin"), Qt::CaseInsensitive)
            ? s->name : tr("Jellyfin „%1“").arg(s->name);
        if (ok && (job->changed || job->removed))
            emit message(QStringLiteral("%1: %2").arg(who, s->syncText));
        else if (!ok && !job->cancelled)
            emit message(QStringLiteral("%1: %2").arg(who, error));
    });
}

void Jellyfin::scheduleAutoSync()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    static bool startDone = false;
    for (const Server &s : m_servers) {
        const QString mode = s.config.value(QLatin1String("autoSync")).toString(QStringLiteral("start"));
        const qint64 age = now - s.lastSync;
        bool due = false;
        if (mode == QLatin1String("start"))
            due = !startDone;
        else if (mode == QLatin1String("6h"))
            due = age > 6 * 3600;
        else if (mode == QLatin1String("24h"))
            due = age > 24 * 3600;
        if (due)
            sync(s.id, false);
    }
    startDone = true;
}

void Jellyfin::localLibraryChanged()
{
    if (m_servers.isEmpty())
        return;
    auto changed = std::make_shared<int>(0);
    runInWorker([changed] { *changed = JellyfinWriter::markAllDuplicates(); }, [this, changed] {
        if (m_preferLocal && *changed)
            m_library->notifyExternalChange();
    });
}

// ---------------------------------------------------------------- reporting

void Jellyfin::sendCapabilities(Server &s)
{
    post(s.url, QStringLiteral("/Sessions/Capabilities/Full"), s.token,
         QJsonObject{{QStringLiteral("PlayableMediaTypes"), QJsonArray{QStringLiteral("Audio")}},
                     {QStringLiteral("SupportedCommands"), QJsonArray{}},
                     {QStringLiteral("SupportsMediaControl"), false}});
}

void Jellyfin::onTrackChanged()
{
    reportStopped();
    m_playSource = 0;
    m_playItem.clear();
    m_playStarted = false;
    const TrackRow r = TrackCache::instance().get(m_player->currentId());
    if (!r.isRemote())
        return;
    Server *s = find(r.sourceId);
    if (!s || !s->config.value(QLatin1String("report")).toBool(true))
        return;
    m_playSource = r.sourceId;
    m_playItem = r.remoteId;
    m_playSession = QUuid::createUuid().toString(QUuid::Id128);
    m_lastPos = 0;
    if (m_player->playing())
        reportStart();
}

void Jellyfin::onStateChanged()
{
    if (m_playItem.isEmpty())
        return;
    if (m_player->state() == 1) {
        if (!m_playStarted)
            reportStart();
        else
            reportProgress();
        m_progressTimer.start();
    } else if (m_player->state() == 2) {
        reportProgress();
        m_progressTimer.stop();
    } else {
        reportStopped();
    }
}

static QJsonObject playBody(const QString &item, const QString &session, qint64 posMs, bool paused, bool original)
{
    return QJsonObject{{QStringLiteral("ItemId"), item},
                       {QStringLiteral("MediaSourceId"), item},
                       {QStringLiteral("PlaySessionId"), session},
                       {QStringLiteral("PositionTicks"), double(posMs) * 10000.0},
                       {QStringLiteral("IsPaused"), paused},
                       {QStringLiteral("CanSeek"), true},
                       {QStringLiteral("PlayMethod"), original ? QStringLiteral("DirectPlay") : QStringLiteral("Transcode")}};
}

void Jellyfin::reportStart()
{
    Server *s = find(m_playSource);
    if (!s || m_playItem.isEmpty())
        return;
    const bool original = s->config.value(QLatin1String("quality")).toString(QStringLiteral("original")) == QLatin1String("original");
    post(s->url, QStringLiteral("/Sessions/Playing"), s->token, playBody(m_playItem, m_playSession, m_lastPos, false, original));
    m_playStarted = true;
    m_progressTimer.start();
}

void Jellyfin::reportProgress()
{
    Server *s = find(m_playSource);
    if (!s || m_playItem.isEmpty() || !m_playStarted)
        return;
    const bool original = s->config.value(QLatin1String("quality")).toString(QStringLiteral("original")) == QLatin1String("original");
    post(s->url, QStringLiteral("/Sessions/Playing/Progress"), s->token,
         playBody(m_playItem, m_playSession, m_lastPos, m_player->state() == 2, original));
}

QNetworkReply *Jellyfin::reportStopped()
{
    m_progressTimer.stop();
    Server *s = find(m_playSource);
    if (!s || m_playItem.isEmpty() || !m_playStarted)
        return nullptr;
    const bool original = s->config.value(QLatin1String("quality")).toString(QStringLiteral("original")) == QLatin1String("original");
    m_playStarted = false;
    return post(s->url, QStringLiteral("/Sessions/Playing/Stopped"), s->token, playBody(m_playItem, m_playSession, m_lastPos, false, original));
}

void Jellyfin::shutdown()
{
    m_favoritesStopped = true; // nothing new goes out; what is open stays in the file
    m_favoriteQueue.clear();
    for (Server &s : m_servers)
        if (s.job)
            s.job->cancelled = true;
    if (QNetworkReply *r = reportStopped()) {
        QEventLoop loop;
        QTimer::singleShot(1500, &loop, &QEventLoop::quit);
        connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }
    m_saveFavorites.stop();
    savePendingFavorites();
}
