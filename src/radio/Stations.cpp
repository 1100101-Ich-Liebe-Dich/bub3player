// SPDX-License-Identifier: GPL-3.0-or-later
#include "radio/Stations.h"

#include "core/Database.h"
#include "core/Http.h"
#include "core/Paths.h"
#include "library/External.h"
#include "library/TrackCache.h"
#include "player/PlayQueue.h"
#include "player/Player.h"
#include "radio/StreamList.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QUrlQuery>

namespace {

const QByteArray UserAgent = QByteArrayLiteral("BuB3Player/" HALCYON_VERSION " (https://github.com/1100101-Ich-Liebe-Dich/bub3player)");

QString codecOf(const QByteArray &contentType)
{
    const QByteArray ct = contentType.toLower();
    if (ct.contains("mpeg") || ct.contains("mp3"))
        return QStringLiteral("MP3");
    if (ct.contains("aac"))
        return QStringLiteral("AAC");
    if (ct.contains("ogg") || ct.contains("vorbis"))
        return QStringLiteral("OGG");
    if (ct.contains("opus"))
        return QStringLiteral("OPUS");
    if (ct.contains("flac"))
        return QStringLiteral("FLAC");
    return {};
}

QString hostName(const QUrl &u)
{
    QString h = u.host();
    if (h.startsWith(QLatin1String("www.")))
        h = h.mid(4);
    return h.isEmpty() ? u.toString() : h;
}

bool isWeb(const QUrl &u)
{
    return u.isValid() && (u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https")) && !u.host().isEmpty();
}

} // namespace

Stations::Stations(Player *player, PlayQueue *queue, QObject *parent) : QObject(parent), m_player(player), m_queue(queue)
{
    m_pool.setMaxThreadCount(1);
    connect(player, &Player::trackChanged, this, &Stations::onTrackChanged);
    reload();
}

Stations::~Stations()
{
    ++m_searchGen;
    m_pool.clear();
    m_pool.waitForDone();
}

bool Stations::online() const
{
    return QSettings().value(QStringLiteral("radio/online"), false).toBool();
}

void Stations::setOnline(bool on)
{
    if (on == online())
        return;
    QSettings().setValue(QStringLiteral("radio/online"), on);
    if (!on)
        clearResults();
    emit settingsChanged();
}

QString Stations::directory() const
{
    const QByteArray env = qgetenv("HALCYON_RADIOBROWSER_URL"); // tests: a local stand-in
    // all.api.radio-browser.info: one of the directory's servers (they share a certificate)
    return env.isEmpty() ? QStringLiteral("https://all.api.radio-browser.info") : QString::fromUtf8(env);
}

void Stations::run(std::function<void()> work)
{
    ++m_busy;
    if (m_busy == 1)
        emit busyChanged();
    QPointer<Stations> self(this);
    m_pool.start([self, work] {
        work();
        QMetaObject::invokeMethod(qApp, [self] {
            if (self && --self->m_busy == 0)
                emit self->busyChanged();
        }, Qt::QueuedConnection);
    });
}

void Stations::reload()
{
    QVariantList list;
    try {
        Stmt s(TrackCache::instance().db(),
               "SELECT id, name, url, stream, homepage, tags, country, codec, bitrate, uuid, last_played FROM stations "
               "ORDER BY name COLLATE NOCASE, id");
        while (s.step()) {
            const qint64 id = s.i64(0);
            const QFileInfo logo(External::stationLogoFile(id));
            list << QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("item"), External::stationItem(id)},
                {QStringLiteral("name"), s.str(1)},
                {QStringLiteral("url"), s.str(2)},
                {QStringLiteral("stream"), s.str(3)},
                {QStringLiteral("homepage"), s.str(4)},
                {QStringLiteral("tags"), s.str(5)},
                {QStringLiteral("country"), s.str(6)},
                {QStringLiteral("format"), s.i32(8) > 0 ? QStringLiteral("%1 %2").arg(s.str(7)).arg(s.i32(8)) : s.str(7)},
                {QStringLiteral("uuid"), s.str(9)},
                {QStringLiteral("lastPlayed"), s.i64(10)},
                // the cover provider shows the logo; the version makes the picture reload when it changes
                {QStringLiteral("logo"), logo.isFile() ? logo.lastModified().toSecsSinceEpoch() : 0},
            };
        }
    } catch (const std::exception &e) {
        qWarning() << "Sender:" << e.what();
    }
    m_list = list;
    TrackCache::instance().invalidate(); // names in the queue and the player
    emit listChanged();
}

// ---------------------------------------------------------------- adding

void Stations::insert(QVector<Station> stations, bool announce, bool playFirst)
{
    QPointer<Stations> self(this);
    const bool fetchLogos = online();
    run([self, stations, announce, playFirst, fetchLogos] {
        int added = 0, known = 0;
        qint64 firstId = 0; // the first station, new or known

        QVector<QPair<qint64, QString>> logos;
        try {
            DbConnection db = Database::open(1024);
            Transaction tx(db);
            Stmt has(db, "SELECT id FROM stations WHERE stream = ? OR (uuid <> '' AND uuid = ?)");
            Stmt ins(db, "INSERT INTO stations(name, url, stream, homepage, logo, tags, country, codec, bitrate, uuid, added) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            for (const Station &st : stations) {
                has.bind(1, st.stream).bind(2, st.uuid);
                const bool exists = has.step();
                const qint64 knownId = exists ? has.i64(0) : 0;
                has.reset();
                if (exists) {
                    ++known;
                    if (firstId == 0)
                        firstId = knownId;
                    continue;
                }
                ins.bind(1, st.name).bind(2, st.url).bind(3, st.stream).bind(4, st.homepage).bind(5, st.logo).bind(6, st.tags)
                    .bind(7, st.country).bind(8, st.codec).bind(9, st.bitrate).bind(10, st.uuid).bind(11, now);
                ins.exec();
                ++added;
                if (firstId == 0)
                    firstId = sqlite3_last_insert_rowid(db);
                if (fetchLogos && !st.logo.isEmpty())
                    logos.append({sqlite3_last_insert_rowid(db), st.logo});
            }
            tx.commit();
        } catch (const std::exception &e) {
            qWarning() << "Sender nicht gespeichert:" << e.what();
        }
        QMetaObject::invokeMethod(qApp, [self, added, known, logos, announce, playFirst, firstId, stations] {
            if (!self)
                return;
            self->reload();
            for (const auto &[id, address] : logos)
                self->fetchLogo(id, address);
            if (playFirst && firstId > 0)
                self->play(firstId);
            if (!announce || (playFirst && added == 0)) // a known station that plays needs no word
                return;
            if (added == 1 && stations.size() == 1)
                emit self->message(tr("Sender »%1« hinzugefügt").arg(stations.first().name));
            else if (added > 0)
                emit self->message(added == 1 ? tr("1 Sender hinzugefügt") : tr("%1 Sender hinzugefügt").arg(added));
            else if (known > 0)
                emit self->message(stations.size() == 1 ? tr("Dieser Sender ist schon in der Liste") : tr("Diese Sender sind schon in der Liste"));
            else
                emit self->message(tr("Keine Sender gefunden"));
        }, Qt::QueuedConnection);
    });
}

void Stations::add(const QString &address, const QString &name, bool playNow)
{
    const QUrl url = QUrl::fromUserInput(address.trimmed());
    if (!isWeb(url)) {
        emit message(tr("Bitte eine Internetadresse angeben (http:// oder https://)"));
        return;
    }
    QPointer<Stations> self(this);
    const QString given = name.trimmed();
    run([self, url, given, playNow] {
        // What answers there: the stream, or a list of streams?
        const Http::Headers headers{{"User-Agent", UserAgent}, {"Icy-MetaData", "1"}};
        Http::Probe p = Http::probe(url, headers, 10000, 64 * 1024);
        Station st;
        st.url = url.toString();
        st.stream = url.toString();
        if (p.status >= 400) {
            QMetaObject::invokeMethod(qApp, [self, status = p.status] {
                if (self)
                    emit self->message(tr("Der Sender antwortet mit einem Fehler (HTTP %1)").arg(status));
            }, Qt::QueuedConnection);
            return;
        }
        QString listTitle;
        if (p.status > 0 && StreamList::looksLikeList(url, p.header("content-type"), p.head) && !StreamList::isHls(p.head)) {
            const QVector<StreamList::Entry> entries = StreamList::parse(p.head, p.finalUrl.isValid() ? p.finalUrl : url);
            if (entries.isEmpty()) {
                QMetaObject::invokeMethod(qApp, [self] {
                    if (self)
                        emit self->message(tr("In dieser Liste steht kein Sender"));
                }, Qt::QueuedConnection);
                return;
            }
            st.stream = entries.first().url.toString();
            listTitle = entries.first().title;
            p = Http::probe(entries.first().url, headers, 10000, 4096); // the stream's own name and format
        }
        // ICY headers: "icy-name", "icy-br", "icy-genre", "icy-url"
        const QString icyName = QString::fromUtf8(p.header("icy-name")).trimmed();
        st.name = !given.isEmpty() ? given : !icyName.isEmpty() ? icyName : !listTitle.isEmpty() ? listTitle : hostName(QUrl(st.stream));
        st.codec = codecOf(p.header("content-type"));
        st.bitrate = p.header("icy-br").split(',').first().trimmed().toInt();
        st.tags = QString::fromUtf8(p.header("icy-genre")).trimmed();
        const QUrl home(QString::fromUtf8(p.header("icy-url")).trimmed());
        if (isWeb(home))
            st.homepage = home.toString();
        QMetaObject::invokeMethod(qApp, [self, st, playNow] {
            if (self)
                self->insert({st}, true, playNow);
        }, Qt::QueuedConnection);
    });
}

void Stations::importFile(const QVariant &pathOrUrl, bool playNow)
{
    QFile f(Paths::localPath(pathOrUrl));
    if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024) {
        emit message(tr("Datei kann nicht gelesen werden: %1").arg(f.fileName()));
        return;
    }
    QVector<Station> stations;
    for (const StreamList::Entry &e : StreamList::parse(f.readAll())) {
        Station st;
        st.url = st.stream = e.url.toString();
        st.name = e.title.isEmpty() ? hostName(e.url) : e.title;
        stations << st;
    }
    insert(stations, true, playNow);
}

bool Stations::isStationList(const QVariant &pathOrUrl)
{
    QFile f(Paths::localPath(pathOrUrl));
    if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024)
        return false;
    const QByteArray data = f.readAll();
    if (data.trimmed().toLower().startsWith("[playlist]"))
        return true;
    // only internet addresses, no files
    int web = 0, other = 0;
    for (const QByteArray &raw : data.split('\n')) {
        const QByteArray l = raw.trimmed();
        if (l.isEmpty() || l.startsWith('#'))
            continue;
        const QByteArray low = l.toLower();
        (low.startsWith("http://") || low.startsWith("https://") ? web : other) += 1;
    }
    return web > 0 && other == 0 && !StreamList::isHls(data);
}

// ---------------------------------------------------------------- editing

void Stations::rename(qint64 id, const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty())
        return;
    QPointer<Stations> self(this);
    run([self, id, n] {
        try {
            DbConnection db = Database::open(1024);
            Stmt u(db, "UPDATE stations SET name = ? WHERE id = ?");
            u.bind(1, n).bind(2, id);
            u.exec();
        } catch (const std::exception &e) {
            qWarning() << "Sender nicht umbenannt:" << e.what();
        }
        QMetaObject::invokeMethod(qApp, [self] {
            if (self)
                self->reload();
        }, Qt::QueuedConnection);
    });
}

void Stations::remove(qint64 id)
{
    // out of the queue first: the item would play nothing any more
    const qint64 item = External::stationItem(id);
    for (int row = m_queue->count() - 1; row >= 0; --row)
        if (m_queue->idAt(row) == item && row != m_queue->currentRow())
            m_queue->removeAt(row);
    QPointer<Stations> self(this);
    run([self, id] {
        try {
            DbConnection db = Database::open(1024);
            Stmt d(db, "DELETE FROM stations WHERE id = ?");
            d.bind(1, id);
            d.exec();
        } catch (const std::exception &e) {
            qWarning() << "Sender nicht entfernt:" << e.what();
        }
        QFile::remove(External::stationLogoFile(id));
        QMetaObject::invokeMethod(qApp, [self] {
            if (self) {
                self->reload();
                emit self->message(tr("Sender entfernt"));
            }
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------- playing

void Stations::play(qint64 id)
{
    QVector<qint64> items;
    int start = 0;
    for (const QVariant &v : std::as_const(m_list)) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toLongLong() == id)
            start = int(items.size());
        items << m.value(QStringLiteral("item")).toLongLong();
    }
    if (items.isEmpty())
        return;
    m_queue->setTracks(items, start, false);
    m_player->playCurrent();
}

void Stations::onTrackChanged()
{
    const qint64 item = m_player->currentId();
    if (!External::isStation(item) || item == m_lastPlayed)
        return;
    m_lastPlayed = item;
    const qint64 id = External::stationOf(item);
    QString uuid;
    for (const QVariant &v : std::as_const(m_list))
        if (v.toMap().value(QStringLiteral("id")).toLongLong() == id)
            uuid = v.toMap().value(QStringLiteral("uuid")).toString();
    // radio-browser counts what is listened to (its way to rank stations)
    const QString click = online() && !uuid.isEmpty() ? directory() + QStringLiteral("/json/url/") + uuid : QString();
    run([id, click] {
        try {
            DbConnection db = Database::open(1024);
            Stmt u(db, "UPDATE stations SET last_played = ? WHERE id = ?");
            u.bind(1, QDateTime::currentSecsSinceEpoch()).bind(2, id);
            u.exec();
        } catch (const std::exception &e) {
            qWarning() << "Sender:" << e.what();
        }
        if (!click.isEmpty()) {
            int status = 0;
            Http::get(QUrl(click), {{"User-Agent", UserAgent}}, 10000, &status);
        }
    });
}

// ---------------------------------------------------------------- directory

void Stations::search(const QString &text)
{
    const QString q = text.trimmed();
    if (q.isEmpty() || !online()) {
        clearResults();
        return;
    }
    const int gen = ++m_searchGen;
    m_resultsText = tr("Suche …");
    emit resultsChanged();
    QPointer<Stations> self(this);
    const QString base = directory();
    run([self, q, gen, base] {
        if (!self || self->m_searchGen.load() != gen)
            return; // a newer search is waiting
        QVariantList found;
        QSet<QString> seen;
        int lastStatus = 0;
        // by name, then by genre ("jazz", "80s" …)
        for (const char *field : {"name", "tag"}) {
            QUrlQuery query;
            query.addQueryItem(QString::fromLatin1(field), q);
            query.addQueryItem(QStringLiteral("limit"), QStringLiteral("60"));
            query.addQueryItem(QStringLiteral("hidebroken"), QStringLiteral("true"));
            query.addQueryItem(QStringLiteral("order"), QStringLiteral("clickcount"));
            query.addQueryItem(QStringLiteral("reverse"), QStringLiteral("true"));
            QUrl url(base + QStringLiteral("/json/stations/search"));
            url.setQuery(query);
            const QByteArray data = Http::get(url, {{"User-Agent", UserAgent}}, 15000, &lastStatus, [self, gen] {
                return !self || self->m_searchGen.load() != gen;
            });
            for (const QJsonValue &v : QJsonDocument::fromJson(data).array()) {
                const QJsonObject o = v.toObject();
                const QString uuid = o.value(QLatin1String("stationuuid")).toString();
                QString stream = o.value(QLatin1String("url_resolved")).toString();
                if (stream.isEmpty())
                    stream = o.value(QLatin1String("url")).toString();
                if (stream.isEmpty() || seen.contains(uuid))
                    continue;
                seen.insert(uuid);
                const int bitrate = o.value(QLatin1String("bitrate")).toInt();
                const QString codec = o.value(QLatin1String("codec")).toString().toUpper();
                found << QVariantMap{
                    {QStringLiteral("name"), o.value(QLatin1String("name")).toString().trimmed()},
                    {QStringLiteral("url"), o.value(QLatin1String("url")).toString()},
                    {QStringLiteral("stream"), stream},
                    {QStringLiteral("homepage"), o.value(QLatin1String("homepage")).toString()},
                    {QStringLiteral("logo"), o.value(QLatin1String("favicon")).toString()},
                    {QStringLiteral("tags"), o.value(QLatin1String("tags")).toString()},
                    {QStringLiteral("country"), o.value(QLatin1String("country")).toString()},
                    {QStringLiteral("codec"), codec},
                    {QStringLiteral("bitrate"), bitrate},
                    {QStringLiteral("format"), bitrate > 0 ? QStringLiteral("%1 %2").arg(codec).arg(bitrate) : codec},
                    {QStringLiteral("uuid"), uuid},
                };
            }
            if (found.size() >= 30)
                break;
        }
        QMetaObject::invokeMethod(qApp, [self, found, gen, lastStatus] {
            if (!self || self->m_searchGen.load() != gen)
                return;
            self->m_results = found;
            self->m_resultsText = !found.isEmpty() ? (found.size() == 1 ? tr("1 Sender") : tr("%1 Sender").arg(found.size()))
                                : lastStatus == 0 ? tr("radio-browser.info ist gerade nicht erreichbar")
                                                  : tr("Nichts gefunden");
            emit self->resultsChanged();
        }, Qt::QueuedConnection);
    });
}

void Stations::addResult(int index)
{
    if (index < 0 || index >= m_results.size())
        return;
    const QVariantMap m = m_results.at(index).toMap();
    Station st;
    st.name = m.value(QStringLiteral("name")).toString();
    st.url = m.value(QStringLiteral("url")).toString();
    st.stream = m.value(QStringLiteral("stream")).toString();
    st.homepage = m.value(QStringLiteral("homepage")).toString();
    st.logo = m.value(QStringLiteral("logo")).toString();
    st.tags = m.value(QStringLiteral("tags")).toString();
    st.country = m.value(QStringLiteral("country")).toString();
    st.codec = m.value(QStringLiteral("codec")).toString();
    st.bitrate = m.value(QStringLiteral("bitrate")).toInt();
    st.uuid = m.value(QStringLiteral("uuid")).toString();
    if (st.name.isEmpty())
        st.name = hostName(QUrl(st.stream));
    insert({st}, true);
}

void Stations::clearResults()
{
    ++m_searchGen;
    m_results.clear();
    m_resultsText.clear();
    emit resultsChanged();
}

void Stations::fetchLogo(qint64 id, const QString &address)
{
    const QUrl url(address);
    if (!isWeb(url))
        return;
    QPointer<Stations> self(this);
    run([self, id, url] {
        int status = 0;
        const QByteArray data = Http::get(url, {{"User-Agent", UserAgent}}, 15000, &status);
        if (data.isEmpty() || data.size() > 2 * 1024 * 1024)
            return;
        const QString file = External::stationLogoFile(id);
        QDir().mkpath(QFileInfo(file).absolutePath());
        QSaveFile out(file);
        if (!out.open(QIODevice::WriteOnly))
            return;
        out.write(data);
        if (!out.commit())
            return;
        QMetaObject::invokeMethod(qApp, [self] {
            if (self)
                self->reload();
        }, Qt::QueuedConnection);
    });
}
