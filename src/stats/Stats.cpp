// SPDX-License-Identifier: GPL-3.0-or-later
#include "stats/Stats.h"

#include "analysis/AudioAnalyzer.h"
#include "core/Database.h"
#include "player/PlayQueue.h"
#include "player/Player.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QHash>
#include <QLocale>
#include <QPointer>
#include <QSet>
#include <QtConcurrent>

#include <algorithm>
#include <limits>
#include <numeric>

namespace {

// A short connection of its own: no memory map (it would count twice), big
// sorts on disk rather than in memory.
DbConnection openDb()
{
    DbConnection db = Database::open(2048);
    db.exec("PRAGMA mmap_size=0");
    db.exec("PRAGMA temp_store=FILE");
    return db;
}

const QLocale &german()
{
    static const QLocale l(QLocale::German, QLocale::Germany);
    return l;
}

qint64 localSecs(const QDate &d)
{
    return QDateTime(d, QTime(0, 0)).toSecsSinceEpoch();
}

// "played_at in the range" for plays and station_plays (?1, ?2)
const char *InRange = "played_at >= ?1 AND played_at < ?2";
const char *Day = "strftime('%Y-%m-%d', played_at, 'unixepoch', 'localtime')";

Stmt ranged(DbConnection &db, const QString &sql, const Stats::Range &r)
{
    Stmt s(db, sql);
    s.bind(1, r.from).bind(2, r.to);
    return s;
}

// A title that is still in the library for a play of the past (cover, playing):
// the same id, or the same names.
qint64 resolve(DbConnection &db, qint64 id, const QString &artist, const QString &title, const QString &album = {})
{
    if (id > 0) {
        Stmt s(db, "SELECT 1 FROM tracks WHERE id = ?");
        s.bind(1, id);
        if (s.step())
            return id;
    }
    Stmt s;
    if (!title.isEmpty()) {
        s.prepare(db, "SELECT id FROM tracks WHERE artist = ?1 COLLATE NOCASE AND title = ?2 COLLATE NOCASE AND dup = 0 LIMIT 1");
        s.bind(1, artist).bind(2, title);
    } else if (!album.isEmpty()) {
        s.prepare(db, "SELECT id FROM tracks WHERE aa = ?1 COLLATE NOCASE AND album = ?2 COLLATE NOCASE AND dup = 0 LIMIT 1");
        s.bind(1, artist).bind(2, album);
    } else if (!artist.isEmpty()) {
        s.prepare(db, "SELECT id FROM tracks WHERE artist = ?1 COLLATE NOCASE AND dup = 0 LIMIT 1");
        s.bind(1, artist);
    } else {
        return 0;
    }
    return s.step() ? s.i64(0) : 0;
}

enum class Top { Artists, Tracks, Albums, Genres };

// The most played of a range: [{name, artist, plays, ms, trackId, share}]
QVariantList top(DbConnection &db, const Stats::Range &r, Top kind, int limit, qint64 allPlays)
{
    QString sql;
    switch (kind) {
    case Top::Artists:
        sql = QStringLiteral("SELECT artist, '', COUNT(*) c, SUM(listened_ms) ms, MAX(track_id) FROM plays "
                             "WHERE completed = 1 AND %1 AND artist <> '' GROUP BY artist COLLATE NOCASE");
        break;
    case Top::Tracks:
        sql = QStringLiteral("SELECT title, artist, COUNT(*) c, SUM(listened_ms) ms, MAX(track_id) FROM plays "
                             "WHERE completed = 1 AND %1 AND title <> '' GROUP BY artist COLLATE NOCASE, title COLLATE NOCASE");
        break;
    case Top::Albums:
        sql = QStringLiteral("SELECT album, CASE WHEN aa <> '' THEN aa ELSE artist END, COUNT(*) c, SUM(listened_ms) ms, MAX(track_id) "
                             "FROM plays WHERE completed = 1 AND %1 AND album <> '' GROUP BY aa COLLATE NOCASE, album COLLATE NOCASE");
        break;
    case Top::Genres:
        sql = QStringLiteral("SELECT genre, '', COUNT(*) c, SUM(listened_ms) ms, 0 FROM plays "
                             "WHERE completed = 1 AND %1 AND genre <> '' GROUP BY genre COLLATE NOCASE");
        break;
    }
    sql = sql.arg(QLatin1String(InRange)) + QStringLiteral(" ORDER BY c DESC, ms DESC LIMIT %1").arg(limit);
    struct Row
    {
        QString name, artist;
        qint64 plays, ms, id;
    };
    QVector<Row> rows;
    {
        Stmt s = ranged(db, sql, r);
        while (s.step())
            rows.append({s.str(0), s.str(1), s.i64(2), s.i64(3), s.i64(4)});
    }
    QVariantList out;
    for (const Row &row : std::as_const(rows)) {
        qint64 id = 0;
        if (kind == Top::Artists)
            id = resolve(db, row.id, row.name, {});
        else if (kind == Top::Tracks)
            id = resolve(db, row.id, row.artist, row.name);
        else if (kind == Top::Albums)
            id = resolve(db, row.id, row.artist, {}, row.name);
        out << QVariantMap{
            {QStringLiteral("name"), row.name},
            {QStringLiteral("artist"), row.artist},
            {QStringLiteral("plays"), row.plays},
            {QStringLiteral("ms"), row.ms},
            {QStringLiteral("trackId"), id},
            {QStringLiteral("share"), allPlays > 0 ? double(row.plays) / double(allPlays) : 0.0},
        };
    }
    return out;
}

struct Totals
{
    qint64 plays = 0, skips = 0, musicMs = 0, radioMs = 0, tracks = 0, artists = 0, albums = 0;
};

Totals totals(DbConnection &db, const Stats::Range &r)
{
    Totals t;
    Stmt s = ranged(db, QStringLiteral(
        "SELECT COUNT(*) FILTER (WHERE completed = 1), COUNT(*) FILTER (WHERE completed = 0), COALESCE(SUM(listened_ms), 0), "
        "COUNT(DISTINCT CASE WHEN completed = 1 THEN lower(artist) || char(31) || lower(title) END), "
        "COUNT(DISTINCT CASE WHEN completed = 1 AND artist <> '' THEN lower(artist) END), "
        "COUNT(DISTINCT CASE WHEN completed = 1 AND album <> '' THEN lower(aa) || char(31) || lower(album) END) "
        "FROM plays WHERE %1").arg(QLatin1String(InRange)), r);
    if (s.step()) {
        t.plays = s.i64(0);
        t.skips = s.i64(1);
        t.musicMs = s.i64(2);
        t.tracks = s.i64(3);
        t.artists = s.i64(4);
        t.albums = s.i64(5);
    }
    Stmt radio = ranged(db, QStringLiteral("SELECT COALESCE(SUM(listened_ms), 0) FROM station_plays WHERE %1").arg(QLatin1String(InRange)), r);
    if (radio.step())
        t.radioMs = radio.i64(0);
    return t;
}

void putTotals(QVariantMap &m, const Totals &t)
{
    m.insert(QStringLiteral("plays"), t.plays);
    m.insert(QStringLiteral("skips"), t.skips);
    m.insert(QStringLiteral("musicMs"), t.musicMs);
    m.insert(QStringLiteral("radioMs"), t.radioMs);
    m.insert(QStringLiteral("listenedMs"), t.musicMs + t.radioMs);
    m.insert(QStringLiteral("tracks"), t.tracks);
    m.insert(QStringLiteral("artists"), t.artists);
    m.insert(QStringLiteral("albums"), t.albums);
}

// Listening time by weekday (Monday first) and hour, music and radio.
QVector<qint64> heat(DbConnection &db, const Stats::Range &r)
{
    QVector<qint64> h(7 * 24, 0);
    for (const char *table : {"plays", "station_plays"}) {
        Stmt s = ranged(db, QStringLiteral("SELECT CAST(strftime('%w', played_at, 'unixepoch', 'localtime') AS INTEGER), "
                                           "CAST(strftime('%H', played_at, 'unixepoch', 'localtime') AS INTEGER), SUM(listened_ms) "
                                           "FROM %1 WHERE %2 GROUP BY 1, 2").arg(QLatin1String(table), QLatin1String(InRange)), r);
        while (s.step()) {
            const int day = (s.i32(0) + 6) % 7, hour = std::clamp(s.i32(1), 0, 23);
            h[day * 24 + hour] += s.i64(2);
        }
    }
    return h;
}

// Listening time per bucket: `format` is a strftime format, `keys` the buckets in order.
QVariantList series(DbConnection &db, const Stats::Range &r, const char *format, const QStringList &keys, const QStringList &labels)
{
    QHash<QString, qint64> ms;
    for (const char *table : {"plays", "station_plays"}) {
        Stmt s = ranged(db, QStringLiteral("SELECT strftime('%1', played_at, 'unixepoch', 'localtime') k, SUM(listened_ms) FROM %2 "
                                           "WHERE %3 GROUP BY k").arg(QLatin1String(format), QLatin1String(table), QLatin1String(InRange)), r);
        while (s.step())
            ms[s.str(0)] += s.i64(1);
    }
    QVariantList out;
    for (int i = 0; i < keys.size(); ++i)
        out << QVariantMap{{QStringLiteral("label"), labels.value(i)}, {QStringLiteral("ms"), ms.value(keys[i])}};
    return out;
}

QVariantList toList(const QVector<qint64> &v)
{
    QVariantList out;
    for (qint64 x : v)
        out << x;
    return out;
}

int argMax(const QVector<qint64> &v)
{
    return v.isEmpty() ? -1 : int(std::max_element(v.begin(), v.end()) - v.begin());
}

QVariantMap trackMap(DbConnection &db, const QString &title, const QString &artist, qint64 id)
{
    return {{QStringLiteral("name"), title}, {QStringLiteral("artist"), artist}, {QStringLiteral("trackId"), resolve(db, id, artist, title)}};
}

QVariantMap computeOverview(const QString &period, int offset)
{
    const Stats::Range r = Stats::rangeOf(period, offset);
    DbConnection db = openDb();
    QVariantMap m;
    const Totals t = totals(db, r);
    putTotals(m, t);
    const QDate today = QDate::currentDate();
    const QDate first = QDateTime::fromSecsSinceEpoch(r.from).date();
    QStringList keys, labels;
    QString label;
    if (period == QLatin1String("week")) {
        for (int i = 0; i < 7; ++i) {
            const QDate d = first.addDays(i);
            keys << d.toString(Qt::ISODate);
            labels << german().dayName(d.dayOfWeek(), QLocale::ShortFormat);
        }
        const QDate last = first.addDays(6);
        label = offset == 0 ? QObject::tr("Diese Woche") : offset == -1 ? QObject::tr("Letzte Woche")
                : QObject::tr("Woche vom %1").arg(german().toString(first, QStringLiteral("d. MMMM yyyy")));
        m.insert(QStringLiteral("detail"), QStringLiteral("%1 – %2").arg(german().toString(first, QStringLiteral("d. MMMM")),
                                                                         german().toString(last, QStringLiteral("d. MMMM yyyy"))));
        m.insert(QStringLiteral("chart"), series(db, r, "%Y-%m-%d", keys, labels));
    } else if (period == QLatin1String("month")) {
        for (int i = 0; i < first.daysInMonth(); ++i) {
            keys << first.addDays(i).toString(Qt::ISODate);
            labels << QString::number(i + 1);
        }
        label = german().standaloneMonthName(first.month()) + QLatin1Char(' ') + QString::number(first.year());
        m.insert(QStringLiteral("detail"), offset == 0 ? QObject::tr("Dieser Monat") : QString());
        m.insert(QStringLiteral("chart"), series(db, r, "%Y-%m-%d", keys, labels));
    } else if (period == QLatin1String("year")) {
        for (int i = 1; i <= 12; ++i) {
            keys << QStringLiteral("%1-%2").arg(first.year()).arg(i, 2, 10, QLatin1Char('0'));
            labels << german().standaloneMonthName(i, QLocale::ShortFormat);
        }
        label = QString::number(first.year());
        m.insert(QStringLiteral("detail"), offset == 0 ? QObject::tr("Dieses Jahr") : QString());
        m.insert(QStringLiteral("chart"), series(db, r, "%Y-%m", keys, labels));
    } else {
        int from = today.year();
        Stmt s(db, "SELECT MIN(played_at) FROM (SELECT played_at FROM plays UNION ALL SELECT played_at FROM station_plays)");
        if (s.step() && !s.isNull(0))
            from = std::min(from, QDateTime::fromSecsSinceEpoch(s.i64(0)).date().year());
        for (int y = from; y <= today.year(); ++y) {
            keys << QString::number(y);
            labels << QString::number(y);
        }
        label = QObject::tr("Insgesamt");
        m.insert(QStringLiteral("detail"), from < today.year() ? QStringLiteral("%1 – %2").arg(from).arg(today.year()) : QString());
        m.insert(QStringLiteral("chart"), series(db, r, "%Y", keys, labels));
    }
    m.insert(QStringLiteral("label"), label);
    const QVector<qint64> h = heat(db, r);
    m.insert(QStringLiteral("heat"), toList(h));
    m.insert(QStringLiteral("topArtists"), top(db, r, Top::Artists, 10, t.plays));
    m.insert(QStringLiteral("topTracks"), top(db, r, Top::Tracks, 10, t.plays));
    m.insert(QStringLiteral("topAlbums"), top(db, r, Top::Albums, 10, t.plays));
    m.insert(QStringLiteral("topGenres"), top(db, r, Top::Genres, 8, t.plays));
    QVariantList stations;
    {
        Stmt s = ranged(db, QStringLiteral("SELECT name, SUM(listened_ms) ms FROM station_plays WHERE %1 GROUP BY station_id "
                                           "ORDER BY ms DESC LIMIT 5").arg(QLatin1String(InRange)), r);
        while (s.step())
            stations << QVariantMap{{QStringLiteral("name"), s.str(0)}, {QStringLiteral("ms"), s.i64(1)}};
    }
    m.insert(QStringLiteral("topStations"), stations);
    if (period != QLatin1String("all")) { // compared with the one before
        const Totals before = totals(db, Stats::rangeOf(period, offset - 1));
        m.insert(QStringLiteral("previousMs"), before.musicMs + before.radioMs);
    }
    m.insert(QStringLiteral("current"), r.to > QDateTime::currentSecsSinceEpoch());
    return m;
}

QVariantMap computeLibrary()
{
    DbConnection db = openDb();
    QVariantMap m;
    Stmt s(db, "SELECT COUNT(*), COALESCE(SUM(duration_ms), 0), COALESCE(SUM(size), 0), COUNT(*) FILTER (WHERE rating >= 5), "
               "COUNT(*) FILTER (WHERE play_count = 0), COALESCE(SUM(play_count), 0), COUNT(*) FILTER (WHERE bits >= 24), "
               "COUNT(*) FILTER (WHERE source_id > 1), COUNT(DISTINCT aa COLLATE NOCASE), COUNT(DISTINCT genre COLLATE NOCASE), "
               "MIN(NULLIF(year, 0)), MAX(year) FROM tracks WHERE dup = 0");
    if (s.step()) {
        m.insert(QStringLiteral("tracks"), s.i64(0));
        m.insert(QStringLiteral("durationMs"), s.i64(1));
        m.insert(QStringLiteral("bytes"), s.i64(2));
        m.insert(QStringLiteral("favorites"), s.i64(3));
        m.insert(QStringLiteral("neverPlayed"), s.i64(4));
        m.insert(QStringLiteral("playCount"), s.i64(5));
        m.insert(QStringLiteral("hires"), s.i64(6));
        m.insert(QStringLiteral("remote"), s.i64(7));
        m.insert(QStringLiteral("artists"), s.i64(8));
        m.insert(QStringLiteral("genres"), s.i64(9));
        m.insert(QStringLiteral("oldestYear"), s.isNull(10) ? 0 : s.i64(10));
        m.insert(QStringLiteral("newestYear"), s.i64(11));
    }
    Stmt a(db, "SELECT COUNT(*) FROM (SELECT 1 FROM tracks WHERE dup = 0 AND album <> '' GROUP BY aa COLLATE NOCASE, album COLLATE NOCASE)");
    if (a.step())
        m.insert(QStringLiteral("albums"), a.i64(0));
    QVariantList formats;
    Stmt f(db, "SELECT CASE WHEN codec = '' THEN '?' ELSE upper(codec) END c, COUNT(*) n FROM tracks WHERE dup = 0 GROUP BY c ORDER BY n DESC");
    qint64 other = 0;
    while (f.step()) {
        if (formats.size() < 6)
            formats << QVariantMap{{QStringLiteral("name"), f.str(0)}, {QStringLiteral("count"), f.i64(1)}};
        else
            other += f.i64(1);
    }
    if (other > 0)
        formats << QVariantMap{{QStringLiteral("name"), QObject::tr("Andere")}, {QStringLiteral("count"), other}};
    m.insert(QStringLiteral("formats"), formats);
    QVariantList decades;
    Stmt d(db, "SELECT (year / 10) * 10 dc, COUNT(*) FROM tracks WHERE dup = 0 AND year >= 1900 GROUP BY dc ORDER BY dc");
    while (d.step())
        decades << QVariantMap{{QStringLiteral("decade"), d.i64(0)}, {QStringLiteral("count"), d.i64(1)}};
    m.insert(QStringLiteral("decades"), decades);
    Stmt an(db, "SELECT COUNT(*) FROM analysis WHERE status = 0");
    if (an.step())
        m.insert(QStringLiteral("analysed"), an.i64(0));
    return m;
}

// The longest run of days in a row with listening: {days, from, to}
QVariantMap streak(DbConnection &db, const Stats::Range &r)
{
    QVector<QDate> days;
    for (const char *table : {"plays", "station_plays"}) {
        Stmt s = ranged(db, QStringLiteral("SELECT DISTINCT %1 FROM %2 WHERE %3").arg(QLatin1String(Day), QLatin1String(table), QLatin1String(InRange)), r);
        while (s.step())
            days << QDate::fromString(s.str(0), Qt::ISODate);
    }
    std::sort(days.begin(), days.end());
    days.erase(std::unique(days.begin(), days.end()), days.end());
    int best = 0, run = 0;
    QDate bestEnd, prev;
    for (const QDate &d : std::as_const(days)) {
        run = prev.isValid() && prev.daysTo(d) == 1 ? run + 1 : 1;
        if (run > best) {
            best = run;
            bestEnd = d;
        }
        prev = d;
    }
    return {{QStringLiteral("days"), best},
            {QStringLiteral("from"), best > 0 ? german().toString(bestEnd.addDays(1 - best), QStringLiteral("d. MMMM")) : QString()},
            {QStringLiteral("to"), best > 0 ? german().toString(bestEnd, QStringLiteral("d. MMMM")) : QString()},
            {QStringLiteral("activeDays"), int(days.size())}};
}

QVariantMap computeReview(int year)
{
    const Stats::Range r{localSecs(QDate(year, 1, 1)), localSecs(QDate(year + 1, 1, 1))};
    DbConnection db = openDb();
    QVariantMap m;
    m.insert(QStringLiteral("year"), year);
    const Totals t = totals(db, r);
    putTotals(m, t);
    m.insert(QStringLiteral("topArtists"), top(db, r, Top::Artists, 5, t.plays));
    m.insert(QStringLiteral("topTracks"), top(db, r, Top::Tracks, 5, t.plays));
    m.insert(QStringLiteral("topAlbums"), top(db, r, Top::Albums, 5, t.plays));
    m.insert(QStringLiteral("topGenres"), top(db, r, Top::Genres, 5, t.plays));

    // the year's favourites, for a playlist
    QVariantList ids;
    for (const QVariant &v : top(db, r, Top::Tracks, 60, t.plays)) {
        const qint64 id = v.toMap().value(QStringLiteral("trackId")).toLongLong();
        if (id > 0 && ids.size() < 50)
            ids << id;
    }
    m.insert(QStringLiteral("playlistIds"), ids);

    // when: months, weekdays, hours
    QStringList keys, labels;
    for (int i = 1; i <= 12; ++i) {
        keys << QStringLiteral("%1-%2").arg(year).arg(i, 2, 10, QLatin1Char('0'));
        labels << german().standaloneMonthName(i, QLocale::ShortFormat);
    }
    const QVariantList months = series(db, r, "%Y-%m", keys, labels);
    m.insert(QStringLiteral("months"), months);
    int topMonth = 0;
    for (int i = 1; i < months.size(); ++i)
        if (months[i].toMap().value(QStringLiteral("ms")).toLongLong() > months[topMonth].toMap().value(QStringLiteral("ms")).toLongLong())
            topMonth = i;
    m.insert(QStringLiteral("topMonth"), topMonth);
    m.insert(QStringLiteral("topMonthName"), german().standaloneMonthName(topMonth + 1));
    const QVector<qint64> h = heat(db, r);
    QVector<qint64> hours(24, 0), weekdays(7, 0);
    for (int i = 0; i < h.size(); ++i) {
        hours[i % 24] += h[i];
        weekdays[i / 24] += h[i];
    }
    m.insert(QStringLiteral("hours"), toList(hours));
    m.insert(QStringLiteral("weekdays"), toList(weekdays));
    m.insert(QStringLiteral("topHour"), argMax(hours));
    const int topDay = argMax(weekdays);
    m.insert(QStringLiteral("topWeekday"), topDay);
    m.insert(QStringLiteral("topWeekdayName"), german().dayName(topDay + 1));
    // the time of day: 0 morning (5–11), 1 midday (11–14), 2 afternoon (14–18), 3 evening (18–22), 4 night (22–5)
    QVector<qint64> parts(5, 0);
    for (int hr = 0; hr < 24; ++hr)
        parts[hr >= 5 && hr < 11 ? 0 : hr >= 11 && hr < 14 ? 1 : hr >= 14 && hr < 18 ? 2 : hr >= 18 && hr < 22 ? 3 : 4] += hours[hr];
    const qint64 allMs = std::accumulate(parts.begin(), parts.end(), qint64(0));
    const int part = argMax(parts);
    m.insert(QStringLiteral("daypart"), part);
    m.insert(QStringLiteral("daypartShare"), allMs > 0 ? double(parts[part]) / double(allMs) : 0.0);

    m.insert(QStringLiteral("streak"), streak(db, r));
    {
        Stmt s = ranged(db, QStringLiteral("SELECT %1 d, SUM(listened_ms) ms, COUNT(*) FILTER (WHERE completed = 1) FROM plays WHERE %2 "
                                           "GROUP BY d ORDER BY ms DESC LIMIT 1").arg(QLatin1String(Day), QLatin1String(InRange)), r);
        if (s.step())
            m.insert(QStringLiteral("busiestDay"), QVariantMap{
                {QStringLiteral("date"), german().toString(QDate::fromString(s.str(0), Qt::ISODate), QStringLiteral("dddd, d. MMMM"))},
                {QStringLiteral("ms"), s.i64(1)}, {QStringLiteral("plays"), s.i64(2)}});
    }
    {   // on repeat: one title most often on one day
        Stmt s = ranged(db, QStringLiteral("SELECT title, artist, MAX(track_id), %1 d, COUNT(*) c FROM plays WHERE completed = 1 AND %2 "
                                           "AND title <> '' GROUP BY d, artist COLLATE NOCASE, title COLLATE NOCASE ORDER BY c DESC LIMIT 1")
                                .arg(QLatin1String(Day), QLatin1String(InRange)), r);
        if (s.step() && s.i64(4) >= 3) {
            QVariantMap rep = trackMap(db, s.str(0), s.str(1), s.i64(2));
            rep.insert(QStringLiteral("count"), s.i64(4));
            rep.insert(QStringLiteral("date"), german().toString(QDate::fromString(s.str(3), Qt::ISODate), QStringLiteral("d. MMMM")));
            m.insert(QStringLiteral("repeat"), rep);
        }
    }
    {   // the first and the latest
        Stmt s = ranged(db, QStringLiteral("SELECT title, artist, track_id, played_at FROM plays WHERE completed = 1 AND %1 "
                                           "ORDER BY played_at, id LIMIT 1").arg(QLatin1String(InRange)), r);
        if (s.step()) {
            QVariantMap first = trackMap(db, s.str(0), s.str(1), s.i64(2));
            first.insert(QStringLiteral("date"), german().toString(QDateTime::fromSecsSinceEpoch(s.i64(3)).date(), QStringLiteral("d. MMMM")));
            m.insert(QStringLiteral("firstTrack"), first);
        }
    }
    {   // new to you: artists first heard this year
        Stmt before(db, "SELECT EXISTS (SELECT 1 FROM plays WHERE completed = 1 AND played_at < ?)");
        before.bind(1, r.from);
        const bool hasBefore = before.step() && before.i64(0) != 0;
        m.insert(QStringLiteral("hasBefore"), hasBefore);
        if (hasBefore) {
            QSet<QString> known;
            Stmt k(db, "SELECT DISTINCT lower(artist) FROM plays WHERE completed = 1 AND played_at < ?1 AND artist <> ''");
            k.bind(1, r.from);
            while (k.step())
                known.insert(k.str(0));
            QVariantList fresh;
            int count = 0;
            Stmt s = ranged(db, QStringLiteral("SELECT artist, COUNT(*) c, MAX(track_id), lower(artist) FROM plays WHERE completed = 1 "
                                               "AND %1 AND artist <> '' GROUP BY artist COLLATE NOCASE ORDER BY c DESC").arg(QLatin1String(InRange)), r);
            QVector<std::tuple<QString, qint64, qint64>> found;
            while (s.step()) {
                if (known.contains(s.str(3)))
                    continue;
                ++count;
                if (found.size() < 5)
                    found.append({s.str(0), s.i64(1), s.i64(2)});
            }
            for (const auto &[name, plays, id] : std::as_const(found))
                fresh << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("plays"), plays},
                                     {QStringLiteral("trackId"), resolve(db, id, name, {})}};
            m.insert(QStringLiteral("newArtists"), count);
            m.insert(QStringLiteral("newTop"), fresh);
        }
        const Totals prev = totals(db, Stats::Range{localSecs(QDate(year - 1, 1, 1)), r.from});
        m.insert(QStringLiteral("previousMs"), prev.musicMs + prev.radioMs);
    }
    {   // tempo, key and energy from the analysis
        Stmt s = ranged(db, QStringLiteral("SELECT AVG(a.bpm), AVG(a.energy), COUNT(*) FROM plays p JOIN analysis a ON a.track_id = p.track_id "
                                           "WHERE p.completed = 1 AND p.%1 AND a.bpm > 0").arg(QLatin1String(InRange)), r);
        if (s.step() && s.i64(2) >= 10) {
            m.insert(QStringLiteral("bpm"), qRound(s.dbl(0)));
            m.insert(QStringLiteral("energy"), s.dbl(1));
            Stmt f = ranged(db, QStringLiteral("SELECT p.title, p.artist, p.track_id, a.bpm FROM plays p JOIN analysis a ON a.track_id = p.track_id "
                                               "WHERE p.completed = 1 AND p.%1 AND a.bpm > 0 AND a.bpm_conf >= 0.3 ORDER BY a.bpm DESC LIMIT 1")
                                    .arg(QLatin1String(InRange)), r);
            if (f.step()) {
                QVariantMap fast = trackMap(db, f.str(0), f.str(1), f.i64(2));
                fast.insert(QStringLiteral("bpm"), qRound(f.dbl(3)));
                m.insert(QStringLiteral("fastest"), fast);
            }
        }
        Stmt k = ranged(db, QStringLiteral("SELECT a.key, a.minor, COUNT(*) c FROM plays p JOIN analysis a ON a.track_id = p.track_id "
                                           "WHERE p.completed = 1 AND p.%1 AND a.key >= 0 GROUP BY a.key, a.minor ORDER BY c DESC LIMIT 1")
                                .arg(QLatin1String(InRange)), r);
        if (k.step() && k.i64(2) >= 10) {
            m.insert(QStringLiteral("keyName"), AudioAnalyzer::keyName(k.i32(0), k.i32(1) != 0));
            m.insert(QStringLiteral("keyCode"), AudioAnalyzer::camelot(k.i32(0), k.i32(1) != 0));
            m.insert(QStringLiteral("keyMinor"), k.i32(1) != 0);
        }
    }
    {   // the decades the music comes from
        QVariantList decades;
        Stmt s = ranged(db, QStringLiteral("SELECT (year / 10) * 10 dc, COUNT(*) c FROM plays WHERE completed = 1 AND %1 AND year >= 1900 "
                                           "GROUP BY dc ORDER BY dc").arg(QLatin1String(InRange)), r);
        while (s.step())
            decades << QVariantMap{{QStringLiteral("decade"), s.i64(0)}, {QStringLiteral("plays"), s.i64(1)}};
        m.insert(QStringLiteral("decades"), decades);
        Stmt o = ranged(db, QStringLiteral("SELECT title, artist, MAX(track_id), year FROM plays WHERE completed = 1 AND %1 AND year >= 1900 "
                                           "AND title <> '' GROUP BY artist COLLATE NOCASE, title COLLATE NOCASE ORDER BY year LIMIT 1")
                                .arg(QLatin1String(InRange)), r);
        if (o.step()) {
            QVariantMap oldest = trackMap(db, o.str(0), o.str(1), o.i64(2));
            oldest.insert(QStringLiteral("year"), o.i64(3));
            m.insert(QStringLiteral("oldest"), oldest);
        }
    }
    {   // radio
        QVariantList stations;
        Stmt s = ranged(db, QStringLiteral("SELECT name, SUM(listened_ms) ms FROM station_plays WHERE %1 GROUP BY station_id "
                                           "ORDER BY ms DESC LIMIT 3").arg(QLatin1String(InRange)), r);
        while (s.step())
            stations << QVariantMap{{QStringLiteral("name"), s.str(0)}, {QStringLiteral("ms"), s.i64(1)}};
        m.insert(QStringLiteral("topStations"), stations);
    }
    return m;
}

QVariantList computeYears()
{
    DbConnection db = openDb();
    QVariantList out;
    Stmt s(db, "SELECT CAST(strftime('%Y', played_at, 'unixepoch', 'localtime') AS INTEGER) y, COUNT(*) FROM plays "
               "WHERE completed = 1 GROUP BY y ORDER BY y DESC");
    while (s.step())
        out << QVariantMap{{QStringLiteral("year"), s.i64(0)}, {QStringLiteral("plays"), s.i64(1)}};
    return out;
}

} // namespace

QVariantMap Stats::overviewOf(const QString &period, int offset)
{
    return computeOverview(period, offset);
}

QVariantMap Stats::reviewOf(int year)
{
    return computeReview(year);
}

QVariantMap Stats::libraryNumbers()
{
    return computeLibrary();
}

QVariantList Stats::yearsWithPlays()
{
    return computeYears();
}

Stats::Stats(Player *player, PlayQueue *queue, QObject *parent) : QObject(parent), m_player(player), m_queue(queue)
{
}

Stats::~Stats()
{
    // the workers only hand back results through a QPointer; they finish on their own
}

Stats::Range Stats::rangeOf(const QString &period, int offset)
{
    const QDate today = QDate::currentDate();
    if (period == QLatin1String("week")) {
        const QDate monday = today.addDays(1 - today.dayOfWeek()).addDays(7 * offset);
        return {localSecs(monday), localSecs(monday.addDays(7))};
    }
    if (period == QLatin1String("month")) {
        const QDate first = QDate(today.year(), today.month(), 1).addMonths(offset);
        return {localSecs(first), localSecs(first.addMonths(1))};
    }
    if (period == QLatin1String("year")) {
        const QDate first(today.year() + offset, 1, 1);
        return {localSecs(first), localSecs(first.addYears(1))};
    }
    return {0, std::numeric_limits<qint64>::max()};
}

QUrl Stats::picturesFolder() const
{
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return QUrl::fromLocalFile(pictures.isEmpty() ? QDir::homePath() : pictures);
}

int Stats::reviewYear() const
{
    const QDate today = QDate::currentDate();
    return today.month() == 1 ? today.year() - 1 : today.year();
}

void Stats::started()
{
    if (++m_busy == 1)
        emit busyChanged();
}

void Stats::finished()
{
    if (--m_busy == 0)
        emit busyChanged();
}

void Stats::setPeriod(const QString &period, int offset)
{
    static const QStringList known{QStringLiteral("week"), QStringLiteral("month"), QStringLiteral("year"), QStringLiteral("all")};
    const QString p = known.contains(period) ? period : QStringLiteral("month");
    const int o = p == QLatin1String("all") ? 0 : std::min(0, offset); // nothing in the future
    if (p == m_period && o == m_offset)
        return;
    m_period = p;
    m_offset = o;
    emit periodChanged();
    loadOverview();
}

void Stats::refresh()
{
    loadOverview();
    loadYears();
    // the library changes slowly, and counting a million titles takes a moment
    if (m_library.isEmpty() || QDateTime::currentMSecsSinceEpoch() - m_libraryAt > 10 * 60 * 1000)
        loadLibrary();
}

void Stats::loadOverview()
{
    const int gen = ++m_overviewGen;
    started();
    QPointer<Stats> self(this);
    (void)QtConcurrent::run([self, gen, period = m_period, offset = m_offset] {
        QVariantMap m;
        try {
            m = computeOverview(period, offset);
        } catch (const std::exception &e) {
            qWarning() << "Statistik:" << e.what();
        }
        QMetaObject::invokeMethod(qApp, [self, gen, m] {
            if (!self)
                return;
            self->finished();
            if (gen != self->m_overviewGen.load())
                return;
            self->m_overview = m;
            emit self->overviewChanged();
        }, Qt::QueuedConnection);
    });
}

void Stats::loadLibrary()
{
    m_libraryAt = QDateTime::currentMSecsSinceEpoch();
    started();
    QPointer<Stats> self(this);
    (void)QtConcurrent::run([self] {
        QVariantMap m;
        try {
            m = computeLibrary();
        } catch (const std::exception &e) {
            qWarning() << "Statistik:" << e.what();
        }
        QMetaObject::invokeMethod(qApp, [self, m] {
            if (!self)
                return;
            self->finished();
            self->m_library = m;
            emit self->libraryChanged();
        }, Qt::QueuedConnection);
    });
}

void Stats::loadYears()
{
    QPointer<Stats> self(this);
    (void)QtConcurrent::run([self] {
        QVariantList years;
        try {
            years = computeYears();
        } catch (const std::exception &e) {
            qWarning() << "Statistik:" << e.what();
        }
        QMetaObject::invokeMethod(qApp, [self, years] {
            if (!self || years == self->m_years)
                return;
            self->m_years = years;
            emit self->yearsChanged();
        }, Qt::QueuedConnection);
    });
}

void Stats::loadReview(int year)
{
    const int gen = ++m_reviewGen;
    m_reviewBusy = true;
    emit busyChanged();
    QPointer<Stats> self(this);
    (void)QtConcurrent::run([self, gen, year] {
        QVariantMap m;
        try {
            m = computeReview(year);
        } catch (const std::exception &e) {
            qWarning() << "Jahresrückblick:" << e.what();
        }
        QMetaObject::invokeMethod(qApp, [self, gen, m] {
            if (!self || gen != self->m_reviewGen.load())
                return;
            self->m_reviewBusy = false;
            self->m_review = m;
            emit self->busyChanged();
            emit self->reviewChanged();
        }, Qt::QueuedConnection);
    });
}

void Stats::playIds(const QVariantList &ids, qint64 startId)
{
    QVector<qint64> list;
    for (const QVariant &v : ids)
        if (v.toLongLong() > 0)
            list << v.toLongLong();
    if (list.isEmpty())
        return;
    m_queue->setTracks(list, std::max(0, int(list.indexOf(startId))), false);
    m_player->playCurrent();
}
