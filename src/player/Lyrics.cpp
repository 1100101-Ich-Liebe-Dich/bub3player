// SPDX-License-Identifier: GPL-3.0-or-later
#include "player/Lyrics.h"

#include "core/Http.h"
#include "core/Paths.h"
#include "core/TagReader.h"
#include "jellyfin/JellyfinAccess.h"
#include "library/TrackCache.h"
#include "player/Player.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStringDecoder>
#include <QUrlQuery>
#include <QtConcurrent>

#include <algorithm>
#include <atomic>
#include <functional>

namespace {

constexpr int MaxLines = 1000;
constexpr qint64 MaxFileBytes = 1 << 20;
constexpr qint64 LeadMs = 200;          // the position is polled every 250 ms: show a line a touch early
constexpr int NoneDays = 30;            // LRCLIB "nothing found" is asked again after this
constexpr qint64 RetryAfterMs = 300000; // LRCLIB unreachable: wait 5 minutes

// Removes empty lines at both ends and runs of empty lines (stanza breaks stay single).
void tidy(QStringList &lines, QVector<qint64> *times)
{
    QStringList outLines;
    QVector<qint64> outTimes;
    for (int i = 0; i < lines.size(); ++i) {
        const bool empty = lines[i].isEmpty();
        if (empty && (outLines.isEmpty() || outLines.last().isEmpty()))
            continue;
        outLines << lines[i];
        if (times)
            outTimes << times->at(i);
    }
    while (!outLines.isEmpty() && outLines.last().isEmpty()) {
        outLines.removeLast();
        if (times)
            outTimes.removeLast();
    }
    if (outLines.size() > MaxLines) {
        outLines = outLines.mid(0, MaxLines);
        if (times)
            outTimes.resize(MaxLines);
    }
    lines = outLines;
    if (times)
        *times = outTimes;
}

QString decode(const QByteArray &bytes)
{
    if (const auto bom = QStringConverter::encodingForData(bytes)) { // UTF-16/32 with a byte order mark
        QStringDecoder dec(*bom);
        return dec.decode(bytes);
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString s = utf8.decode(bytes);
    if (utf8.hasError())
        s = QString::fromLatin1(bytes); // old .lrc files in Latin-1
    return s;
}

// ---------------------------------------------------------------- sources

struct TrackInfo
{
    QString path;
    qint64 sourceId = 1;
    QString remoteId;
    QString title, artist, album;
    int durationMs = 0;
};

LyricsText fromLrcFile(const QString &audioPath)
{
    const QFileInfo fi(audioPath);
    const QString base = fi.path() + QLatin1Char('/') + fi.completeBaseName();
    for (const QString &ext : {QStringLiteral(".lrc"), QStringLiteral(".LRC"), QStringLiteral(".Lrc")}) {
        QFile f(base + ext);
        if (!f.exists() || f.size() > MaxFileBytes || !f.open(QIODevice::ReadOnly))
            continue;
        LyricsText t = LyricsText::parse(decode(f.readAll()));
        t.source = QStringLiteral("lrc");
        return t;
    }
    return {};
}

LyricsText fromTags(const QString &audioPath)
{
    LyricsText plain;
    for (const QString &text : TagReader::embeddedLyrics(audioPath)) {
        LyricsText t = LyricsText::parse(text);
        t.source = QStringLiteral("tags");
        if (t.synced())
            return t;
        if (plain.isEmpty())
            plain = t;
    }
    return plain;
}

using Stale = std::function<bool()>; // the search is no longer wanted (another track by now)

LyricsText fromJellyfin(qint64 sourceId, const QString &itemId, const Stale &stale)
{
    JellyfinAccess::Server server;
    if (!JellyfinAccess::isOnline(sourceId) || !JellyfinAccess::get(sourceId, server))
        return {};
    int status = 0;
    const QByteArray bytes = Http::get(JellyfinAccess::lyricsUrl(sourceId, itemId), {{"Authorization", JellyfinAccess::authHeader(server.token)}},
                                       8000, &status, stale);
    // {"Metadata": {…}, "Lyrics": [{"Text": "…", "Start": ticks}, …]} (Jellyfin 10.9+)
    const QJsonArray rows = QJsonDocument::fromJson(bytes).object().value(QStringLiteral("Lyrics")).toArray();
    LyricsText t;
    bool timed = !rows.isEmpty();
    for (const QJsonValue &v : rows) {
        const QJsonObject o = v.toObject();
        t.lines << o.value(QStringLiteral("Text")).toString().trimmed();
        if (o.value(QStringLiteral("Start")).isDouble())
            t.times << qint64(o.value(QStringLiteral("Start")).toDouble() / 10000.0); // 100 ns ticks
        else
            timed = false;
    }
    if (!timed) {
        t.times.clear();
        tidy(t.lines, nullptr);
    } else {
        tidy(t.lines, &t.times);
    }
    t.source = QStringLiteral("jellyfin");
    return t;
}

QString lrclibBase()
{
    const QByteArray env = qgetenv("HALCYON_LRCLIB_URL"); // tests: a local stand-in
    return env.isEmpty() ? QStringLiteral("https://lrclib.net") : QString::fromUtf8(env);
}

QString enc(const QString &s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s));
}

// Picks the text out of an LRCLIB record: synchronised before plain, nothing for instrumentals.
QString lrclibText(const QJsonObject &o)
{
    if (o.value(QStringLiteral("instrumental")).toBool())
        return {};
    const QString synced = o.value(QStringLiteral("syncedLyrics")).toString();
    return synced.trimmed().isEmpty() ? o.value(QStringLiteral("plainLyrics")).toString() : synced;
}

std::atomic<qint64> g_lrclibRetryAt{0};

LyricsText fromLrclib(const TrackInfo &info, const Stale &stale)
{
    if (info.title.isEmpty() || info.artist.isEmpty())
        return {};
    const int secs = int((info.durationMs + 500) / 1000);
    const QByteArray key = QCryptographicHash::hash(
                               QStringList{info.artist.toLower(), info.title.toLower(), info.album.toLower(), QString::number(secs)}
                                   .join(QLatin1Char('\n'))
                                   .toUtf8(),
                               QCryptographicHash::Sha1)
                               .toHex();
    const QString dir = Paths::ensure(Paths::cacheDir() + QStringLiteral("/lyrics"));
    const QString hit = dir + QLatin1Char('/') + QString::fromLatin1(key) + QStringLiteral(".lrc");
    const QString none = dir + QLatin1Char('/') + QString::fromLatin1(key) + QStringLiteral(".none");
    auto parsed = [](const QString &text) {
        LyricsText t = LyricsText::parse(text);
        t.source = QStringLiteral("lrclib");
        return t;
    };
    {
        QFile f(hit);
        if (f.size() <= MaxFileBytes && f.open(QIODevice::ReadOnly))
            return parsed(QString::fromUtf8(f.readAll()));
    }
    const QFileInfo noneInfo(none);
    if (noneInfo.exists() && noneInfo.lastModified().daysTo(QDateTime::currentDateTime()) < NoneDays)
        return {};
    if (QDateTime::currentMSecsSinceEpoch() < g_lrclibRetryAt.load())
        return {};

    const Http::Headers headers = {
        {"User-Agent", QByteArrayLiteral("BuB3Player/" HALCYON_VERSION " (https://github.com/1100101-Ich-Liebe-Dich/bub3player)")}};
    QString text;
    int status = 0;
    // exact match by name, album and length first
    const bool exact = !info.album.isEmpty() && secs > 0;
    if (exact) {
        QUrl u(lrclibBase() + QStringLiteral("/api/get"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("track_name"), enc(info.title));
        q.addQueryItem(QStringLiteral("artist_name"), enc(info.artist));
        q.addQueryItem(QStringLiteral("album_name"), enc(info.album));
        q.addQueryItem(QStringLiteral("duration"), QString::number(secs));
        u.setQuery(q);
        const QByteArray bytes = Http::get(u, headers, 10000, &status, stale);
        if (stale())
            return {}; // cancelled: no answer to keep, no reason to wait
        if (status == 200)
            text = lrclibText(QJsonDocument::fromJson(bytes).object());
    }
    // else a search (a found record without text is an instrumental: nothing to search for);
    // only a record of about the same length counts (live and radio versions differ)
    if (!exact || status == 404) {
        QUrl u(lrclibBase() + QStringLiteral("/api/search"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("track_name"), enc(info.title));
        q.addQueryItem(QStringLiteral("artist_name"), enc(info.artist));
        u.setQuery(q);
        const QByteArray bytes = Http::get(u, headers, 10000, &status, stale);
        if (stale())
            return {}; // cancelled: no answer to keep, no reason to wait
        if (status == 200) {
            QString plain;
            for (const QJsonValue &v : QJsonDocument::fromJson(bytes).array()) {
                const QJsonObject o = v.toObject();
                if (secs > 0 && std::abs(o.value(QStringLiteral("duration")).toDouble() - secs) > 2.5)
                    continue;
                const QString t = lrclibText(o);
                if (t.trimmed().isEmpty())
                    continue;
                if (!o.value(QStringLiteral("syncedLyrics")).toString().trimmed().isEmpty()) {
                    text = t;
                    break;
                }
                if (plain.isEmpty())
                    plain = t;
            }
            if (text.isEmpty())
                text = plain;
        }
    }
    if (status != 200 && status != 404) { // no connection, server trouble: no answer to keep
        g_lrclibRetryAt = QDateTime::currentMSecsSinceEpoch() + RetryAfterMs;
        return {};
    }
    QSaveFile out(text.trimmed().isEmpty() ? none : hit);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(text.toUtf8());
        out.commit();
    }
    if (text.trimmed().isEmpty())
        return {};
    QFile::remove(none);
    return parsed(text);
}

// Everything up to the level asked for; the best find wins (synchronised before plain).
// A search that is no longer wanted (another track by now) ends before the network.
LyricsText find(const TrackInfo &info, bool network, const Stale &stale = [] { return false; })
{
    LyricsText best;
    if (stale())
        return best;
    if (info.sourceId != 1 && !info.remoteId.isEmpty()) {
        best = fromJellyfin(info.sourceId, info.remoteId, stale);
    } else if (!info.path.isEmpty()) {
        best = fromLrcFile(info.path);
        if (!best.synced()) {
            const LyricsText tags = fromTags(info.path);
            if (tags.synced() || best.isEmpty())
                best = tags;
        }
    }
    if (network && !best.synced() && !stale()) {
        LyricsText net = fromLrclib(info, stale);
        if (net.synced() || best.isEmpty())
            best = std::move(net);
    }
    return best;
}

} // namespace

// ---------------------------------------------------------------- LRC

LyricsText LyricsText::parse(const QString &raw)
{
    static const QRegularExpression stamp(QStringLiteral(R"(\[(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?\])"));
    // only the LRC header tags: "[Chorus: Anna]" in plain lyrics is text
    static const QRegularExpression meta(QStringLiteral(R"(^\[(ar|al|ti|au|by|offset|re|ve|length|la|id|tool|#)\s*:(.*)\]$)"),
                                         QRegularExpression::CaseInsensitiveOption);
    // word stamps of enhanced LRC ("<01:02.50>"), and stamps inside a line ("Hi [00:01.50]there")
    static const QRegularExpression wordStamp(QStringLiteral(R"(<\d{1,3}:\d{1,2}(?:[.:]\d{1,3})?>|\[\d{1,3}:\d{1,2}(?:[.:]\d{1,3})?\])"));

    QString text = raw;
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    struct Timed
    {
        qint64 t;
        QString s;
    };
    QVector<Timed> timed;
    QStringList plain;
    qint64 offset = 0;
    int stampedRows = 0;
    for (const QString &row : text.split(QLatin1Char('\n'))) {
        const QString r = row.trimmed();
        QVector<qint64> stamps;
        int pos = 0;
        for (;;) {
            const auto m = stamp.match(r, pos, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
            if (!m.hasMatch())
                break;
            const QString frac = m.captured(3);
            int ms = frac.toInt();
            if (frac.size() == 1)
                ms *= 100;
            else if (frac.size() == 2)
                ms *= 10;
            stamps << (m.captured(1).toLongLong() * 60 + m.captured(2).toLongLong()) * 1000 + ms;
            pos = m.capturedEnd();
        }
        if (stamps.isEmpty()) {
            const auto mm = meta.match(r);
            if (mm.hasMatch()) {
                if (mm.captured(1).compare(QLatin1String("offset"), Qt::CaseInsensitive) == 0)
                    offset = mm.captured(2).trimmed().toLongLong();
                continue;
            }
            plain << QString(r).remove(wordStamp).simplified();
            continue;
        }
        ++stampedRows;
        const QString line = r.mid(pos).remove(wordStamp).simplified();
        plain << line;
        for (qint64 t : stamps)
            timed.push_back({t, line});
    }

    LyricsText out;
    std::stable_sort(timed.begin(), timed.end(), [](const Timed &a, const Timed &b) { return a.t < b.t; });
    // only time stamps that move on make a song synchronised (not "[00:00.00]" on every line)
    if (stampedRows >= 2 && timed.first().t != timed.last().t) {
        for (const Timed &t : timed) {
            // two lines at the same time (original and translation) are sung as one
            if (!out.times.isEmpty() && out.times.last() == std::max<qint64>(0, t.t - offset)) {
                if (t.s.isEmpty())
                    continue;
                if (out.lines.last().isEmpty())
                    out.lines.last() = t.s;
                else
                    out.lines.last() += QLatin1Char('\n') + t.s;
                continue;
            }
            out.lines << t.s;
            out.times << std::max<qint64>(0, t.t - offset); // "+" means earlier (LRC convention)
        }
        tidy(out.lines, &out.times);
    } else {
        out.lines = plain;
        tidy(out.lines, nullptr);
    }
    return out;
}

// ---------------------------------------------------------------- service

Lyrics::Lyrics(Player *player, QObject *parent)
    : QObject(parent)
    , m_player(player)
{
    QSettings s;
    m_shown = s.value(QStringLiteral("lyrics/shown"), true).toBool();
    m_online = s.value(QStringLiteral("lyrics/online"), false).toBool();
    m_pool.setMaxThreadCount(2); // network waits must not hold up the lists in the global pool
    m_clearTimer.setSingleShot(true);
    m_clearTimer.setInterval(400);
    connect(&m_clearTimer, &QTimer::timeout, this, [this] { clear(m_running != Nothing ? Searching : None); });
    connect(player, &Player::trackChanged, this, &Lyrics::onTrackChanged);
    connect(player, &Player::positionChanged, this, [this] {
        if (m_active)
            updateCurrent();
    });
}

Lyrics::~Lyrics()
{
    m_liveGen = ~quint64(0); // running searches end, also in the middle of a request
    m_pool.clear();
}

void Lyrics::setActive(bool on)
{
    if (m_active == on)
        return;
    m_active = on;
    emit activeChanged();
    if (on) {
        search();
        updateCurrent();
    }
}

void Lyrics::setShown(bool on)
{
    if (m_shown == on)
        return;
    m_shown = on;
    QSettings().setValue(QStringLiteral("lyrics/shown"), on);
    emit settingsChanged();
    search();
}

void Lyrics::setOnline(bool on)
{
    if (m_online == on)
        return;
    m_online = on;
    QSettings().setValue(QStringLiteral("lyrics/online"), on);
    emit settingsChanged();
    search();
}

Lyrics::Level Lyrics::wantedLevel() const
{
    return m_shown && m_online ? Network : Local;
}

void Lyrics::onTrackChanged()
{
    if (m_active) {
        search();
        return;
    }
    // looked for again when a big view opens
    m_liveGen = ++m_gen;
    m_trackId = 0;
    m_level = m_running = Nothing;
    m_clearTimer.stop();
    clear(Idle);
}

void Lyrics::search()
{
    if (!m_active)
        return;
    const qint64 id = m_player->currentId();
    if (id != m_trackId) {
        m_liveGen = ++m_gen;
        m_trackId = id;
        m_level = m_running = Nothing;
        if (id <= 0) {
            m_clearTimer.stop();
            clear(Idle);
            return;
        }
        if (!m_text.isEmpty())
            m_clearTimer.start(); // the old lines until the new ones are there, but not for long
    }
    const Level want = wantedLevel();
    if (id <= 0 || m_level >= want || m_running >= want)
        return;
    m_running = want;
    if (m_text.isEmpty() && m_status != Searching) {
        m_status = Searching;
        emit changed();
    }

    const TrackRow r = TrackCache::instance().get(id);
    TrackInfo info;
    info.sourceId = r.sourceId;
    info.remoteId = r.remoteId;
    info.title = r.title;
    info.artist = r.artist;
    info.album = r.album;
    info.durationMs = r.durationMs;
    if (!r.isRemote())
        info.path = TrackCache::instance().pathOf(id);
    const quint64 gen = m_gen;
    QPointer<Lyrics> self(this);
    std::atomic<quint64> *live = &m_liveGen;
    (void)QtConcurrent::run(&m_pool, [self, id, gen, want, info, live] {
        const LyricsText text = find(info, want == Network, [live, gen] { return live->load() != gen; });
        QMetaObject::invokeMethod(qApp, [self, id, gen, want, text] {
            if (self)
                self->apply(id, gen, want, text);
        });
    });
}

void Lyrics::apply(qint64 id, quint64 gen, int level, const LyricsText &text)
{
    if (gen != m_gen || id != m_trackId || level < m_level)
        return; // an old track, or a wider search has answered already
    // a narrower search while a wider one runs: its find is shown meanwhile, "none" is not final
    const bool last = level >= m_running;
    if (!last && text.isEmpty())
        return;
    if (last)
        m_running = Nothing;
    m_clearTimer.stop();
    // synchronised lyrics of our own: nothing better to fetch
    m_level = text.synced() ? Network : std::max(m_level, level);
    const int status = text.isEmpty() ? None : Found;
    const bool same = text.lines == m_text.lines && text.times == m_text.times && text.source == m_text.source
                      && m_textId == id;
    if (!same || status != m_status) {
        m_text = text;
        m_textId = id;
        m_status = status;
        emit changed();
        updateCurrent(true);
    }
    search(); // the wish may have grown meanwhile (lyrics or LRCLIB switched on)
}

void Lyrics::clear(int status)
{
    const bool had = !m_text.isEmpty() || m_current != -1;
    m_text = LyricsText();
    m_textId = 0;
    m_current = -1;
    if (had || m_status != status) {
        m_status = status;
        emit changed();
        emit currentChanged();
    }
}

void Lyrics::updateCurrent(bool force)
{
    if (!force && m_textId != m_player->currentId())
        return; // the old lines stay as they are until the new ones come
    int idx = -1;
    if (m_text.synced() && m_player->currentId() == m_textId) {
        const qint64 pos = m_player->position() + LeadMs;
        idx = int(std::upper_bound(m_text.times.begin(), m_text.times.end(), pos) - m_text.times.begin()) - 1;
    }
    if (idx != m_current || force) {
        m_current = idx;
        emit currentChanged();
    }
}

LyricsText Lyrics::forFile(const QString &path)
{
    TrackInfo info;
    info.path = path;
    return find(info, false);
}

void Lyrics::seekTo(int line)
{
    if (!m_text.synced() || line < 0 || line >= m_text.times.size())
        return;
    m_player->seek(m_text.times[line]);
    updateCurrent();
}
