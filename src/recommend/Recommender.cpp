#include "recommend/Recommender.h"

#include "analysis/AudioAnalyzer.h"
#include "core/Database.h"
#include "jellyfin/JellyfinAccess.h"
#include "library/GroupModel.h"
#include "library/Library.h"
#include "library/QueryBuilder.h"
#include "library/TrackCache.h"
#include "library/TrackListModel.h"
#include "player/PlayQueue.h"
#include "player/Player.h"

#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QtConcurrent>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace {

constexpr qint64 SessionGap = 45 * 60;          // plays further apart: different session
const char *DaypartColumn[4] = {"night", "morning", "afternoon", "evening"};

int daypartOf(const QDateTime &t)
{
    const int h = t.time().hour();
    return h < 6 ? 0 : h < 12 ? 1 : h < 18 ? 2 : 3;
}

// "Artist feat. Someone" -> "Artist" (as written)
QString artistBase(const QString &artist)
{
    static const QRegularExpression feat(QStringLiteral("\\s+(feat\\.?|ft\\.|featuring|with)\\s.*$|\\s+\\((feat|ft)\\.?\\s.*$"),
                                         QRegularExpression::CaseInsensitiveOption);
    QString s = artist;
    s.remove(feat);
    return s.trimmed();
}

QString upperBound(const QString &s)
{
    return s + QString::fromUcs4(U"\U0010FFFF");
}

// Tracks of exactly this artist, also "Artist feat. ..." (each range uses the
// artist index; a plain prefix would also match "Airbourne" for "Air").
const char *ArtistWhere = "(artist = ?1 COLLATE NOCASE "
                          "OR (artist COLLATE NOCASE >= ?2 AND artist COLLATE NOCASE < ?3) "
                          "OR (artist COLLATE NOCASE >= ?4 AND artist COLLATE NOCASE < ?5) "
                          "OR (artist COLLATE NOCASE >= ?6 AND artist COLLATE NOCASE < ?7))";

void bindArtist(Stmt &s, const QString &name)
{
    s.bind(1, name);
    int i = 2;
    for (const char *suffix : {" feat", " ft", " (feat"}) {
        const QString p = name + QLatin1String(suffix);
        s.bind(i, p).bind(i + 1, upperBound(p));
        i += 2;
    }
}

QStringList genreTokens(const QString &genre)
{
    QStringList out;
    for (const QString &t : genre.split(QRegularExpression(QStringLiteral("[;,/|]")), Qt::SkipEmptyParts)) {
        const QString v = t.trimmed().toLower();
        if (!v.isEmpty())
            out << v;
    }
    return out;
}

double genreSim(const QStringList &a, const QStringList &b)
{
    if (a.isEmpty() || b.isEmpty())
        return 0.3; // unknown: neutral
    if (a.first() == b.first())
        return 1.0;
    for (const QString &x : a)
        if (b.contains(x))
            return 0.8;
    // "Progressive Rock" vs "Rock", "Deep House" vs "House"
    for (const QString &x : a)
        for (const QString &w : x.split(QRegularExpression(QStringLiteral("[\\s\\-&]+")), Qt::SkipEmptyParts))
            if (w.size() > 2)
                for (const QString &y : b)
                    if (y.split(QRegularExpression(QStringLiteral("[\\s\\-&]+")), Qt::SkipEmptyParts).contains(w))
                        return 0.45;
    return 0.0;
}

int camelotNumber(int key, bool minor)
{
    static const int maj[12] = {8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1};
    static const int min[12] = {5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10};
    return key < 0 ? 0 : (minor ? min[key] : maj[key]);
}

// Harmonic mixing: same key, neighbours on the Camelot wheel, relative major/minor.
double keySim(int k1, bool m1, int k2, bool m2)
{
    const int a = camelotNumber(k1, m1), b = camelotNumber(k2, m2);
    if (!a || !b)
        return 0.5;
    const int d = std::min((a - b + 12) % 12, (b - a + 12) % 12);
    if (d == 0)
        return m1 == m2 ? 1.0 : 0.8;
    if (d == 1 && m1 == m2)
        return 0.85;
    if (d == 2 && m1 == m2)
        return 0.4;
    return 0.15;
}

double tempoSim(double a, double b)
{
    if (a <= 0 || b <= 0)
        return 0.5;
    double r = b / a;
    while (r >= 1.5)
        r /= 2;
    while (r < 0.75)
        r *= 2;
    const double d = std::fabs(std::log2(r)) / 0.07;
    return std::exp(-d * d);
}

struct Row
{
    qint64 id = 0, source = 1, lastPlayed = 0, durationMs = 0;
    QString artist, key, album, aa, genre;
    QStringList genres;
    int year = 0, playCount = 0, skipCount = 0, rating = 0;
    bool dup = false, ok = false, analysed = false;
    double bpm = 0, bpmConf = 0, keyConf = 0, energy = 0;
    int musicalKey = -1;
    bool minor = false;
    bool hasTempo() const { return analysed && bpm > 0 && bpmConf >= 0.3; }
    bool hasKey() const { return analysed && musicalKey >= 0 && keyConf >= 0.25; }
};

QVector<Row> loadRows(DbConnection &db, const QVector<qint64> &ids)
{
    QVector<Row> out;
    for (int i = 0; i < ids.size(); i += 400) {
        QStringList ph;
        const int n = std::min<int>(400, int(ids.size()) - i);
        for (int k = 0; k < n; ++k)
            ph << QStringLiteral("?");
        Stmt s(db, QStringLiteral("SELECT t.id, t.source_id, t.artist, t.aa, t.album, t.genre, t.year, t.duration_ms, t.play_count, "
                                  "t.skip_count, t.last_played, t.rating, t.dup, t.tags_read, a.version, a.bpm, a.bpm_conf, a.key, "
                                  "a.minor, a.key_conf, a.energy, a.status "
                                  "FROM tracks t LEFT JOIN analysis a ON a.track_id = t.id AND a.mtime = t.mtime WHERE t.id IN (%1)")
                       .arg(ph.join(QLatin1Char(','))));
        for (int k = 0; k < n; ++k)
            s.bind(k + 1, ids[i + k]);
        while (s.step()) {
            Row r;
            r.id = s.i64(0);
            r.source = s.i64(1);
            r.artist = s.str(2);
            r.key = Recommender::artistKey(r.artist);
            r.aa = s.str(3);
            r.album = s.str(4);
            r.genre = s.str(5);
            r.genres = genreTokens(r.genre);
            r.year = s.i32(6);
            r.durationMs = s.i64(7);
            r.playCount = s.i32(8);
            r.skipCount = s.i32(9);
            r.lastPlayed = s.i64(10);
            r.rating = s.i32(11);
            r.dup = s.i32(12) != 0;
            r.ok = s.i32(13) == 1;
            r.analysed = !s.isNull(14) && s.i32(14) == AudioAnalyzer::Version && s.i32(21) != 1;
            if (r.analysed) {
                r.bpm = s.dbl(15);
                r.bpmConf = s.dbl(16);
                r.musicalKey = s.i32(17);
                r.minor = s.i32(18) != 0;
                r.keyConf = s.dbl(19);
                r.energy = s.dbl(20);
            }
            out << r;
        }
    }
    return out;
}

struct ArtistStat
{
    int plays = 0, skips = 0;
    int part[4] = {0, 0, 0, 0};
};

void learnOne(DbConnection &db, qint64 id, bool counted, qint64 when)
{
    QString name;
    {
        // finished before the write transaction: an open read would block it
        Stmt row(db, "SELECT artist FROM tracks WHERE id = ?");
        row.bind(1, id);
        if (!row.step())
            return;
        name = artistBase(row.str(0));
    }
    const QString key = Recommender::artistKey(name);
    if (key.isEmpty())
        return;
    const int part = daypartOf(QDateTime::fromSecsSinceEpoch(when));
    Transaction tx(db);
    Stmt st(db, QStringLiteral("INSERT INTO artist_stats(artist, name, plays, skips, last, %1) VALUES (?, ?, ?, ?, ?, ?) "
                               "ON CONFLICT(artist) DO UPDATE SET name = excluded.name, plays = plays + excluded.plays, "
                               "skips = skips + excluded.skips, last = max(last, excluded.last), %1 = %1 + excluded.%1")
                    .arg(QLatin1String(DaypartColumn[part])));
    st.bind(1, key).bind(2, name).bind(3, counted ? 1 : 0).bind(4, counted ? 0 : 1).bind(5, when).bind(6, counted ? 1 : 0);
    st.exec();

    // Artists heard just before, in the same session (most recent first).
    Stmt prev(db, "SELECT t.artist FROM plays p JOIN tracks t ON t.id = p.track_id "
                  "WHERE p.played_at >= ? AND p.played_at <= ? AND p.track_id <> ? AND p.completed = 1 "
                  "ORDER BY p.played_at DESC, p.id DESC LIMIT 8");
    prev.bind(1, when - SessionGap).bind(2, when).bind(3, id);
    QStringList before;
    while (prev.step()) {
        const QString k = Recommender::artistKey(prev.str(0));
        if (!k.isEmpty() && k != key && !before.contains(k))
            before << k;
        if (before.size() >= 5)
            break;
    }
    Stmt link(db, "INSERT INTO artist_links(a, b, w, updated) VALUES (?, ?, ?, ?) "
                  "ON CONFLICT(a, b) DO UPDATE SET w = max(-3.0, min(50.0, w + excluded.w)), updated = excluded.updated");
    auto add = [&](const QString &a, const QString &b, double w) {
        for (int dir = 0; dir < 2; ++dir) {
            link.bind(1, dir ? b : a).bind(2, dir ? a : b).bind(3, w).bind(4, when);
            link.exec();
        }
    };
    if (counted) {
        for (int i = 0; i < before.size(); ++i)
            add(before[i], key, 1.0 / (i + 1));
    } else if (!before.isEmpty()) {
        add(before.first(), key, -0.5); // skipped right after that artist: not a good pair
    }
    tx.commit();
}

// Once after the upgrade: learn from the play history that already exists.
void rebuild(DbConnection &db)
{
    QHash<QPair<QString, QString>, double> links;
    QHash<QString, ArtistStat> stats;
    QHash<QString, QString> names;
    QHash<QString, qint64> last;
    struct Recent { QString key; qint64 at; };
    QVector<Recent> window;
    Stmt s(db, "SELECT p.played_at, p.completed, t.artist FROM plays p JOIN tracks t ON t.id = p.track_id ORDER BY p.played_at, p.id");
    while (s.step()) {
        const qint64 at = s.i64(0);
        const bool counted = s.i32(1) != 0;
        const QString name = artistBase(s.str(2));
        const QString key = Recommender::artistKey(name);
        if (key.isEmpty())
            continue;
        names.insert(key, name);
        ArtistStat &st = stats[key];
        (counted ? st.plays : st.skips) += 1;
        if (counted)
            st.part[daypartOf(QDateTime::fromSecsSinceEpoch(at))] += 1;
        last[key] = std::max(last.value(key), at);
        window.erase(std::remove_if(window.begin(), window.end(), [at](const Recent &r) { return at - r.at > SessionGap; }), window.end());
        QStringList before;
        for (int i = int(window.size()) - 1; i >= 0 && before.size() < 5; --i)
            if (window[i].key != key && !before.contains(window[i].key))
                before << window[i].key;
        auto add = [&](const QString &a, const QString &b, double w) {
            double &x = links[qMakePair(a, b)];
            x = std::clamp(x + w, -3.0, 50.0);
            double &y = links[qMakePair(b, a)];
            y = std::clamp(y + w, -3.0, 50.0);
        };
        if (counted) {
            for (int i = 0; i < before.size(); ++i)
                add(before[i], key, 1.0 / (i + 1));
            window.push_back({key, at});
            if (window.size() > 12)
                window.removeFirst();
        } else if (!before.isEmpty()) {
            add(before.first(), key, -0.5);
        }
    }
    Transaction tx(db);
    db.exec("DELETE FROM artist_links");
    db.exec("DELETE FROM artist_stats");
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    Stmt li(db, "INSERT INTO artist_links(a, b, w, updated) VALUES (?, ?, ?, ?)");
    for (auto it = links.cbegin(); it != links.cend(); ++it) {
        li.bind(1, it.key().first).bind(2, it.key().second).bind(3, it.value()).bind(4, now);
        li.exec();
    }
    Stmt si(db, "INSERT INTO artist_stats(artist, name, plays, skips, last, night, morning, afternoon, evening) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (auto it = stats.cbegin(); it != stats.cend(); ++it) {
        si.bind(1, it.key()).bind(2, names.value(it.key())).bind(3, it->plays).bind(4, it->skips).bind(5, last.value(it.key()))
            .bind(6, it->part[0]).bind(7, it->part[1]).bind(8, it->part[2]).bind(9, it->part[3]);
        si.exec();
    }
    Database::setMeta(db, "learn_rebuild", QStringLiteral("0"));
    tx.commit();
}

} // namespace

// ================================================================ picking

void Recommender::relearn(DbConnection &db)
{
    rebuild(db);
}

QString Recommender::artistKey(const QString &artist)
{
    return artistBase(artist).toLower();
}

QVector<qint64> Recommender::pick(const Context &ctx)
{
    QVector<qint64> out;
    if (ctx.count <= 0)
        return out;
    DbConnection db;
    try {
        db = Database::open(4096);
    } catch (const std::exception &e) {
        qWarning() << "Empfehlungen:" << e.what();
        return out;
    }
    auto *rng = QRandomGenerator::global();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    try {
        const QVector<Row> seeds = loadRows(db, ctx.seeds);
        QHash<qint64, Row> seedById;
        for (const Row &r : seeds)
            seedById.insert(r.id, r);
        // seeds in the given order, the last one is the most important
        QVector<Row> seq;
        for (qint64 id : ctx.seeds)
            if (seedById.contains(id))
                seq << seedById.value(id);

        QSet<qint64> pool;
        const qint64 maxId = db.scalar("SELECT MAX(id) FROM tracks");
        // Random windows in id order through an index: cheap even on 1M tracks.
        auto sample = [&](const QString &where, const QVariantList &binds, int perProbe, int probes) {
            if (maxId <= 0)
                return;
            Stmt s(db, QStringLiteral("SELECT t.id FROM tracks t WHERE %1 AND t.id >= ? ORDER BY t.id LIMIT %2").arg(where).arg(perProbe));
            for (int p = 0; p < probes; ++p) {
                const qint64 start = qint64(rng->bounded(quint64(maxId) + 1));
                for (int i = 0; i < binds.size(); ++i)
                    s.bind(i + 1, binds[i]);
                s.bind(int(binds.size()) + 1, start);
                int got = 0;
                while (s.step()) {
                    pool.insert(s.i64(0));
                    ++got;
                }
                s.reset();
                if (got < perProbe && start > 0) { // wrap around
                    for (int i = 0; i < binds.size(); ++i)
                        s.bind(i + 1, binds[i]);
                    s.bind(int(binds.size()) + 1, qint64(0));
                    while (s.step() && got < perProbe) {
                        pool.insert(s.i64(0));
                        ++got;
                    }
                    s.reset();
                }
            }
        };

        // --- what the seeds lead to
        QHash<QString, double> linked; // artist key -> strength
        QStringList seedArtists;       // most recent first
        for (int i = int(seq.size()) - 1; i >= 0; --i)
            if (!seq[i].key.isEmpty() && !seedArtists.contains(seq[i].key))
                seedArtists << seq[i].key;
        if (ctx.learning) {
            Stmt l(db, "SELECT b, w FROM artist_links WHERE a = ? AND w > 0.3 ORDER BY w DESC LIMIT 30");
            for (int i = 0; i < std::min<int>(4, int(seedArtists.size())); ++i) {
                const double f = 1.0 / (1 + i * 0.5);
                l.bind(1, seedArtists[i]);
                while (l.step())
                    linked[l.str(0)] += l.dbl(1) * f;
                l.reset();
            }
        }
        // Same artists (a few), linked artists (many).
        auto addArtist = [&](const QString &name, int n) {
            if (name.isEmpty())
                return;
            Stmt s(db, QStringLiteral("SELECT id FROM tracks WHERE %1 ORDER BY random() LIMIT %2").arg(QLatin1String(ArtistWhere)).arg(n));
            bindArtist(s, name);
            while (s.step())
                pool.insert(s.i64(0));
        };
        if (ctx.mode != 2)
            for (int i = 0; i < std::min<int>(3, int(seq.size())); ++i)
                addArtist(artistBase(seq[seq.size() - 1 - i].artist), 20);
        {
            QVector<QPair<double, QString>> top;
            for (auto it = linked.cbegin(); it != linked.cend(); ++it)
                if (!seedArtists.contains(it.key()))
                    top << qMakePair(it.value(), it.key());
            std::sort(top.begin(), top.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
            Stmt nm(db, "SELECT name FROM artist_stats WHERE artist = ?");
            for (int i = 0; i < std::min<int>(14, int(top.size())); ++i) {
                nm.bind(1, top[i].second);
                if (nm.step())
                    addArtist(nm.str(0), 12);
                nm.reset();
            }
        }
        // Genre, era, tempo of the most recent seeds.
        QSet<QString> genresDone;
        for (int i = int(seq.size()) - 1; i >= 0 && genresDone.size() < 2; --i) {
            const QString g = seq[i].genre;
            if (g.isEmpty() || genresDone.contains(g.toLower()))
                continue;
            genresDone.insert(g.toLower());
            sample(QStringLiteral("t.genre = ? COLLATE NOCASE"), {g}, 25, 4);
            const QStringList toks = seq[i].genres;
            if (toks.size() > 1 || (toks.size() == 1 && toks.first() != g.toLower()))
                sample(QStringLiteral("t.genre = ? COLLATE NOCASE"), {toks.first()}, 20, 2);
        }
        if (!seq.isEmpty() && seq.last().year > 0)
            for (int k = 0; k < 3; ++k)
                sample(QStringLiteral("t.year = ?"), {seq.last().year + int(rng->bounded(7)) - 3}, 15, 1);
        if (!seq.isEmpty() && seq.last().hasTempo()) {
            Stmt s(db, "SELECT track_id FROM analysis WHERE bpm BETWEEN ? AND ? AND bpm_conf >= 0.3 ORDER BY random() LIMIT 80");
            s.bind(1, seq.last().bpm * 0.95).bind(2, seq.last().bpm * 1.05);
            while (s.step())
                pool.insert(s.i64(0));
        }
        // Liked, but not heard for a while.
        {
            Stmt s(db, "SELECT id FROM tracks WHERE last_played BETWEEN 1 AND ? AND play_count >= 2 ORDER BY random() LIMIT 40");
            s.bind(1, now - 14 * 86400);
            while (s.step())
                pool.insert(s.i64(0));
        }
        // A little chance for anything; mostly unknown tracks when discovering.
        sample(QStringLiteral("1"), {}, 6, ctx.mode == 2 ? 6 : 4);
        if (ctx.mode == 2 || ctx.adventure > 0.6)
            sample(QStringLiteral("t.play_count = 0"), {}, 8, 6);

        // --- load and filter
        const QVector<Row> cands = loadRows(db, QVector<qint64>(pool.cbegin(), pool.cend()));
        qint64 maxDur = 15 * 60000;
        for (const Row &r : seq)
            maxDur = std::max(maxDur, r.durationMs * 3 / 2);
        QHash<QString, ArtistStat> stats;
        {
            QSet<QString> keys;
            for (const Row &r : cands)
                keys.insert(r.key);
            Stmt s(db, "SELECT plays, skips, night, morning, afternoon, evening FROM artist_stats WHERE artist = ?");
            for (const QString &k : keys) {
                s.bind(1, k);
                if (s.step()) {
                    ArtistStat st;
                    st.plays = s.i32(0);
                    st.skips = s.i32(1);
                    for (int p = 0; p < 4; ++p)
                        st.part[p] = s.i32(2 + p);
                    stats.insert(k, st);
                }
                s.reset();
            }
        }
        const double linkMax = [&] {
            double m = 0;
            for (double v : linked)
                m = std::max(m, v);
            return m;
        }();

        struct Scored { const Row *r; double base; };
        QVector<Scored> scored;
        for (const Row &r : cands) {
            if (!r.ok || ctx.exclude.contains(r.id) || seedById.contains(r.id))
                continue;
            if (ctx.hideDuplicates && r.dup)
                continue;
            if (r.source != 1 && !ctx.online.contains(r.source))
                continue;
            if (r.durationMs > 0 && (r.durationMs < 45000 || r.durationMs > maxDur))
                continue;
            if (ctx.mode == 2 && r.playCount > 0)
                continue;
            double s = 0;
            // taste: artists that go together with the seeds
            const bool sameArtist = seedArtists.contains(r.key);
            const double link = linked.value(r.key);
            const double aff = linkMax > 0 ? 1.0 - std::exp(-link / std::max(1.0, linkMax / 3)) : 0.0;
            // similarity to the seeds (recent ones count more)
            double g = 0, era = 0, wsum = 0;
            for (int i = 0; i < seq.size(); ++i) {
                const double w = 1.0 + i; // later = more recent
                g += w * genreSim(seq[i].genres, r.genres);
                era += w * ((seq[i].year > 0 && r.year > 0) ? std::exp(-std::abs(seq[i].year - r.year) / 8.0) : 0.4);
                wsum += w;
            }
            if (wsum > 0) {
                g /= wsum;
                era /= wsum;
            } else {
                g = 0.3;
                era = 0.4;
            }
            // how much it is liked
            const ArtistStat st = stats.value(r.key);
            double love = std::min(1.0, std::log1p(r.playCount) / std::log1p(30.0));
            const double skipRatio = double(r.skipCount) / (r.playCount + r.skipCount + 1);
            love *= 1.0 - skipRatio;
            if (r.rating >= 4)
                love = std::min(1.0, love + 0.3);
            if (st.plays + st.skips >= 3)
                love += 0.4 * double(st.plays - st.skips) / (st.plays + st.skips);
            if (skipRatio > 0.5 && r.skipCount >= 2)
                love -= 0.3;
            // time of day: does this artist belong to now?
            double timeFit = 0.5;
            const int total = st.part[0] + st.part[1] + st.part[2] + st.part[3];
            if (total >= 5)
                timeFit = std::clamp(double(st.part[ctx.daypart]) / total / 0.25, 0.0, 2.0) / 2.0;

            if (ctx.mode == 1) { // close to the seeds
                s = 0.25 * aff + 0.08 * (sameArtist ? 1 : 0) + 0.25 * g + 0.12 * era + 0.06 * love + 0.04 * timeFit;
            } else {
                s = 0.28 * aff + 0.10 * (sameArtist ? 1 : 0) + 0.16 * g + 0.08 * era + 0.12 * love + 0.06 * timeFit;
            }
            // heard recently: not again so soon
            if (r.lastPlayed > now - 2 * 86400)
                s -= 0.6;
            else if (r.lastPlayed > now - 7 * 86400)
                s -= 0.2;
            if (r.playCount == 0)
                s += 0.12 * ctx.adventure;
            if (ctx.avoidArtists.contains(r.key))
                s -= 0.7;
            scored << Scored{&r, s};
        }
        if (scored.isEmpty())
            return out;

        // --- draw: weighted chance, with flow (tempo, key, energy follow the
        // previous pick) and variety (artists and albums do not pile up).
        const Row *prev = seq.isEmpty() ? nullptr : &seq.last();
        QHash<QString, int> artistCount, albumCount;
        const double temp = 0.045 + 0.07 * ctx.adventure;
        const double flowWeight = ctx.automix ? 0.26 : (ctx.mode == 1 ? 0.2 : 0.12);
        QSet<qint64> taken;
        while (out.size() < ctx.count) {
            QVector<QPair<double, int>> eff;
            eff.reserve(scored.size());
            for (int i = 0; i < scored.size(); ++i) {
                const Row &r = *scored[i].r;
                if (taken.contains(r.id))
                    continue;
                double s = scored[i].base;
                if (prev && prev->analysed && r.analysed) {
                    const double t = (prev->hasTempo() && r.hasTempo()) ? tempoSim(prev->bpm, r.bpm) : 0.5;
                    const double k = (prev->hasKey() && r.hasKey()) ? keySim(prev->musicalKey, prev->minor, r.musicalKey, r.minor) : 0.5;
                    const double e = 1.0 - std::min(1.0, std::fabs(prev->energy - r.energy) * 1.5);
                    s += flowWeight * (0.4 * t + 0.25 * k + 0.35 * e);
                } else {
                    s += flowWeight * 0.45;
                }
                s -= 0.25 * artistCount.value(r.key);
                s -= 0.15 * albumCount.value(r.aa + QLatin1Char('|') + r.album);
                if (prev && r.key == prev->key)
                    s -= 0.3; // not the same artist twice in a row
                eff << qMakePair(s, i);
            }
            if (eff.isEmpty())
                break;
            std::sort(eff.begin(), eff.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
            const int k = std::min<int>(30, int(eff.size()));
            double total = 0;
            QVector<double> w(k);
            for (int i = 0; i < k; ++i) {
                w[i] = std::exp((eff[i].first - eff[0].first) / temp);
                total += w[i];
            }
            double x = rng->generateDouble() * total;
            int chosen = 0;
            for (int i = 0; i < k; ++i) {
                x -= w[i];
                if (x <= 0) {
                    chosen = i;
                    break;
                }
            }
            const Row &r = *scored[eff[chosen].second].r;
            out << r.id;
            taken.insert(r.id);
            artistCount[r.key] += 1;
            albumCount[r.aa + QLatin1Char('|') + r.album] += 1;
            prev = &r;
        }
    } catch (const std::exception &e) {
        qWarning() << "Empfehlungen:" << e.what();
    }
    return out;
}

// ================================================================ object

Recommender::Recommender(Player *player, PlayQueue *queue, Library *library, QObject *parent)
    : QObject(parent), m_player(player), m_queue(queue), m_library(library)
{
    QSettings s;
    m_adventure = std::clamp(s.value(QStringLiteral("recommend/adventure"), 1).toInt(), 0, 2);
    m_learning = s.value(QStringLiteral("recommend/learning"), true).toBool();

    // Autoplay / radio: check whenever the queue or playback state changes
    // (queued, so the queue has settled first).
    connect(m_queue, &PlayQueue::upcomingChanged, this, &Recommender::maybeAutoplay, Qt::QueuedConnection);
    connect(m_queue, &PlayQueue::modeChanged, this, &Recommender::maybeAutoplay, Qt::QueuedConnection);
    connect(m_player, &Player::stateChanged, this, &Recommender::maybeAutoplay, Qt::QueuedConnection);
    connect(m_player, &Player::trackFinished, this, &Recommender::onTrackFinished);

    // Learn from the history that is already there (once, after the upgrade).
    QPointer<Recommender> self(this);
    (void)QtConcurrent::run([self] {
        try {
            DbConnection db = Database::open(4096);
            if (Database::meta(db, "learn_rebuild") == QLatin1String("1"))
                rebuild(db);
        } catch (const std::exception &e) {
            qWarning() << "Empfehlungen:" << e.what();
        }
        QMetaObject::invokeMethod(qApp, [self] {
            if (self)
                self->refreshLearned();
        }, Qt::QueuedConnection);
    });
}

Recommender::~Recommender() = default;

void Recommender::setAdventure(int a)
{
    a = std::clamp(a, 0, 2);
    if (a == m_adventure)
        return;
    m_adventure = a;
    QSettings().setValue(QStringLiteral("recommend/adventure"), a);
    m_mixesAt = 0;
    emit settingsChanged();
}

void Recommender::setLearning(bool on)
{
    if (on == m_learning)
        return;
    m_learning = on;
    QSettings().setValue(QStringLiteral("recommend/learning"), on);
    emit settingsChanged();
}

void Recommender::setBusy(bool b)
{
    if (b != m_busy) {
        m_busy = b;
        emit busyChanged();
    }
}

Recommender::Context Recommender::baseContext() const
{
    Context c;
    c.adventure = m_adventure * 0.5;
    c.automix = m_player->transition() == 2;
    c.daypart = daypartOf(QDateTime::currentDateTime());
    c.hideDuplicates = QueryBuilder::hideDuplicates();
    c.avoidArtists = m_avoid;
    c.learning = m_learning;
    for (qint64 id : m_recent)
        c.exclude.insert(id);
    try {
        Stmt s(TrackCache::instance().db(), "SELECT id FROM sources WHERE kind = 'jellyfin'");
        while (s.step())
            if (JellyfinAccess::isOnline(s.i64(0)))
                c.online << s.i64(0);
    } catch (const std::exception &) {
    }
    return c;
}

void Recommender::maybeAutoplay()
{
    const bool radio = !m_queue->radio().isEmpty();
    if ((!m_queue->autoplay() && !radio) || m_queue->repeat() != 0 || m_player->state() != 1 || m_autoplayRunning)
        return;
    const int count = m_queue->count();
    if (count == 0 || m_queue->currentRow() < count - 2)
        return;
    const qint64 seed = m_queue->lastId();
    if (seed <= 0 || seed == m_autoplaySeed)
        return;
    m_autoplaySeed = seed;
    Context c = baseContext();
    for (int row = std::max(0, count - 5); row < count; ++row)
        c.seeds << m_queue->idAt(row);
    for (qint64 id : m_queue->ids())
        c.exclude.insert(id);
    c.count = 8;
    m_autoplayRunning = true;
    QPointer<Recommender> self(this);
    (void)QtConcurrent::run([self, c, seed, radio] {
        const QVector<qint64> picks = pick(c);
        QMetaObject::invokeMethod(qApp, [self, picks, seed, radio] {
            if (!self)
                return;
            self->m_autoplayRunning = false;
            if (picks.isEmpty() || self->m_queue->lastId() != seed)
                return;
            self->m_queue->append(picks, true);
            if (!radio)
                emit self->m_library->message(tr("Autoplay: %n passende Titel angehängt", nullptr, int(picks.size())));
        }, Qt::QueuedConnection);
    });
}

void Recommender::onTrackFinished(qint64 id, qint64 listenedMs, qint64 durationMs, bool skipped)
{
    Q_UNUSED(listenedMs)
    Q_UNUSED(durationMs)
    m_recent << id;
    if (m_recent.size() > 200)
        m_recent.removeFirst();
    if (skipped) {
        // Skipped early: less of this artist for the rest of the session, and
        // the radio drops what it had already planned from them.
        const QString key = artistKey(TrackCache::instance().get(id).artist);
        if (!key.isEmpty()) {
            m_avoid.insert(key);
            const int removed = m_queue->removeUpcomingAuto([key](qint64 other) {
                return artistKey(TrackCache::instance().get(other).artist) == key;
            });
            if (removed > 0) {
                m_autoplaySeed = 0;
                QMetaObject::invokeMethod(this, &Recommender::maybeAutoplay, Qt::QueuedConnection);
            }
        }
    }
    if (!m_learning)
        return;
    const qint64 when = QDateTime::currentSecsSinceEpoch();
    QPointer<Recommender> self(this);
    (void)QtConcurrent::run([self, id, skipped, when] {
        try {
            thread_local DbConnection db;
            if (!db.isOpen())
                db = Database::open(1024);
            learnOne(db, id, !skipped, when);
        } catch (const std::exception &e) {
            qWarning() << "Empfehlungen:" << e.what();
        }
    });
}

// ---------------------------------------------------------------- radio

void Recommender::startRadio(qint64 trackId)
{
    if (trackId <= 0)
        return;
    const TrackRow r = TrackCache::instance().get(trackId);
    startRadioFromSeeds({trackId}, trackId, tr("Mehr wie „%1“").arg(r.title));
}

void Recommender::startRadioFromList(TrackListModel *model, int row)
{
    if (model)
        startRadio(model->idAt(row));
}

void Recommender::startRadioFromGroup(GroupModel *model, int row)
{
    if (!model)
        return;
    const QVariantMap spec = model->trackSpec(row);
    const QString title = model->get(row).value(QStringLiteral("title")).toString();
    const QueryBuilder::Built q = QueryBuilder::trackIds({{QStringLiteral("filters"), spec.value(QStringLiteral("filters"))},
                                                          {QStringLiteral("sort"), QStringLiteral("playCount")},
                                                          {QStringLiteral("desc"), true},
                                                          {QStringLiteral("limit"), 200}});
    if (!q.ok)
        return;
    setBusy(true);
    QPointer<Recommender> self(this);
    (void)QtConcurrent::run([self, q, title] {
        QVector<qint64> ids;
        try {
            DbConnection db = Database::open(2048);
            Stmt s(db, q.sql);
            for (int i = 0; i < q.binds.size(); ++i)
                s.bind(i + 1, q.binds[i]);
            while (s.step())
                ids << s.i64(0);
        } catch (const std::exception &e) {
            qWarning() << "Empfehlungen:" << e.what();
        }
        QMetaObject::invokeMethod(qApp, [self, ids, title] {
            if (!self)
                return;
            self->setBusy(false);
            if (ids.isEmpty())
                return;
            // Start with one of its favourites, the others guide the picks.
            QVector<qint64> seeds;
            auto *rng = QRandomGenerator::global();
            const qint64 first = ids[int(rng->bounded(std::min<int>(5, int(ids.size()))))];
            QVector<qint64> rest = ids;
            std::shuffle(rest.begin(), rest.end(), *rng);
            for (qint64 id : rest)
                if (id != first && seeds.size() < 4)
                    seeds << id;
            seeds << first;
            self->startRadioFromSeeds(seeds, first, tr("Radio: %1").arg(title));
        }, Qt::QueuedConnection);
    });
}

void Recommender::startRadioFromSeeds(const QVector<qint64> &seeds, qint64 first, const QString &label)
{
    Context c = baseContext();
    c.seeds = seeds;
    c.exclude.insert(first);
    c.count = 24;
    c.mode = 1;
    setBusy(true);
    QPointer<Recommender> self(this);
    (void)QtConcurrent::run([self, c, first, label] {
        const QVector<qint64> picks = pick(c);
        QMetaObject::invokeMethod(qApp, [self, picks, first, label] {
            if (!self)
                return;
            self->setBusy(false);
            QVector<qint64> ids;
            ids << first << picks;
            self->m_autoplaySeed = 0;
            self->m_queue->setRadio(ids, label);
            self->m_player->playCurrent(false);
            emit self->m_library->message(picks.isEmpty() ? tr("Für das Radio fehlen noch passende Titel")
                                                          : tr("%1: %n Titel", nullptr, int(picks.size())).arg(label));
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------- mixes ("Für dich")

void Recommender::loadMixes(bool force)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_mixesLoading || (!force && m_mixesAt > 0 && now - m_mixesAt < 600 && !m_mixes.isEmpty()))
        return;
    m_mixesLoading = true;
    emit mixesChanged();
    const Context base = baseContext();
    QPointer<Recommender> self(this);
    (void)QtConcurrent::run([self, base, now] {
        QVariantList mixes;
        auto add = [&](const QString &kind, const QString &title, const QString &subtitle, const QVector<qint64> &ids) {
            if (ids.size() < 5)
                return;
            QVariantList l;
            for (qint64 id : ids)
                l << id;
            mixes << QVariantMap{{QStringLiteral("kind"), kind}, {QStringLiteral("title"), title},
                                 {QStringLiteral("subtitle"), subtitle}, {QStringLiteral("ids"), l}};
        };
        try {
            DbConnection db = Database::open(4096);
            QSet<qint64> used;
            auto ctxWith = [&](const QVector<qint64> &seeds, int mode) {
                Context c = base;
                c.seeds = seeds;
                c.mode = mode;
                c.count = 30;
                for (qint64 id : used)
                    c.exclude.insert(id);
                return c;
            };
            auto remember = [&](const QVector<qint64> &ids) {
                for (qint64 id : ids)
                    used.insert(id);
            };

            // 1. Like what was played last
            QVector<qint64> recent;
            {
                Stmt s(db, "SELECT track_id FROM plays WHERE completed = 1 ORDER BY played_at DESC, id DESC LIMIT 40");
                while (s.step() && recent.size() < 5)
                    if (!recent.contains(s.i64(0)))
                        recent.prepend(s.i64(0));
            }
            if (!recent.isEmpty()) {
                const QVector<qint64> ids = pick(ctxWith(recent, 0));
                add(QStringLiteral("recent"), tr("Weiter wie zuletzt"), tr("Passend zu dem, was du zuletzt gehört hast"), ids);
                remember(ids);
            }
            // 2. Time of day
            {
                static const char *titles[4] = {QT_TR_NOOP("Dein Nacht-Mix"), QT_TR_NOOP("Dein Morgen-Mix"),
                                                QT_TR_NOOP("Dein Nachmittags-Mix"), QT_TR_NOOP("Dein Abend-Mix")};
                QVector<qint64> seeds;
                Stmt a(db, QStringLiteral("SELECT name FROM artist_stats WHERE %1 > 0 ORDER BY %1 DESC, plays DESC LIMIT 6")
                               .arg(QLatin1String(DaypartColumn[base.daypart])));
                Stmt t(db, QStringLiteral("SELECT id FROM tracks WHERE %1 ORDER BY play_count DESC, random() LIMIT 1")
                               .arg(QLatin1String(ArtistWhere)));
                while (a.step()) {
                    bindArtist(t, a.str(0));
                    if (t.step())
                        seeds << t.i64(0);
                    t.reset();
                }
                if (seeds.size() >= 2) {
                    std::reverse(seeds.begin(), seeds.end());
                    const QVector<qint64> ids = pick(ctxWith(seeds, 0));
                    add(QStringLiteral("daypart"), tr(titles[base.daypart]), tr("Was du um diese Tageszeit gern hörst"), ids);
                    remember(ids);
                }
            }
            // 3. Rediscover
            {
                QVector<qint64> ids;
                Stmt s(db, "SELECT id, dup, source_id FROM tracks WHERE last_played BETWEEN 1 AND ? AND play_count >= 2 "
                           "ORDER BY random() LIMIT 60");
                s.bind(1, now - 60 * 86400);
                while (s.step() && ids.size() < 30) {
                    if (used.contains(s.i64(0)) || (base.hideDuplicates && s.i32(1)) || (s.i64(2) != 1 && !base.online.contains(s.i64(2))))
                        continue;
                    ids << s.i64(0);
                }
                add(QStringLiteral("rediscover"), tr("Wiederentdecken"), tr("Lange nicht gehört, früher gern gespielt"), ids);
                remember(ids);
            }
            // 4. New to you: never played, chosen around your favourites
            {
                QVector<qint64> seeds;
                Stmt s(db, "SELECT id FROM tracks WHERE play_count > 0 ORDER BY play_count DESC, last_played DESC LIMIT 6");
                while (s.step())
                    seeds.prepend(s.i64(0));
                const QVector<qint64> ids = pick(ctxWith(seeds, 2));
                add(QStringLiteral("new"), tr("Neu für dich"),
                    seeds.isEmpty() ? tr("Titel, die du noch nie gehört hast") : tr("Noch nie gehört, passend zu deinen Lieblingen"), ids);
                remember(ids);
            }
            // Nothing learnt yet: a mix across the library
            if (recent.isEmpty()) {
                Context c = base;
                c.count = 30;
                c.adventure = 1.0;
                for (qint64 id : used)
                    c.exclude.insert(id);
                add(QStringLiteral("random"), tr("Querbeet"), tr("Ein Mix quer durch deine Bibliothek"), pick(c));
            }
        } catch (const std::exception &e) {
            qWarning() << "Empfehlungen:" << e.what();
        }
        QMetaObject::invokeMethod(qApp, [self, mixes, now] {
            if (!self)
                return;
            self->m_mixes = mixes;
            self->m_mixesAt = now;
            self->m_mixesLoading = false;
            emit self->mixesChanged();
        }, Qt::QueuedConnection);
    });
}

void Recommender::playMix(int index, int start, bool shuffle)
{
    if (index < 0 || index >= m_mixes.size())
        return;
    const QVariantMap m = m_mixes[index].toMap();
    QVector<qint64> ids;
    for (const QVariant &v : m.value(QStringLiteral("ids")).toList())
        ids << v.toLongLong();
    if (ids.isEmpty())
        return;
    if (shuffle) {
        std::shuffle(ids.begin(), ids.end(), *QRandomGenerator::global());
        start = 0;
    }
    m_autoplaySeed = 0;
    m_queue->setRadio(ids, m.value(QStringLiteral("title")).toString(), std::clamp(start, 0, int(ids.size()) - 1), true);
    m_player->playCurrent(false);
}

// ---------------------------------------------------------------- learning

void Recommender::resetLearning()
{
    QPointer<Recommender> self(this);
    (void)QtConcurrent::run([self] {
        try {
            DbConnection db = Database::open(1024);
            db.exec("DELETE FROM artist_links");
            db.exec("DELETE FROM artist_stats");
        } catch (const std::exception &e) {
            qWarning() << "Empfehlungen:" << e.what();
        }
        QMetaObject::invokeMethod(qApp, [self] {
            if (!self)
                return;
            self->m_avoid.clear();
            self->m_mixesAt = 0;
            self->refreshLearned();
            emit self->m_library->message(tr("Gelerntes gelöscht. Die Wiedergabestatistik der Titel bleibt."));
        }, Qt::QueuedConnection);
    });
}

void Recommender::refreshLearned()
{
    try {
        DbConnection &db = TrackCache::instance().db();
        const qint64 plays = db.scalar("SELECT COUNT(*) FROM plays");
        const qint64 artists = db.scalar("SELECT COUNT(*) FROM artist_stats");
        const qint64 links = db.scalar("SELECT COUNT(*) FROM artist_links WHERE w > 0.3") / 2;
        if (plays == 0)
            m_learnedText = tr("Noch nichts gelernt: Empfehlungen richten sich vorerst nach Genre, Zeit und Klang.");
        else
            m_learnedText = tr("Gelernt aus %1 Wiedergaben: %2 Künstler, %3 Verbindungen zwischen Künstlern.")
                                .arg(m_library->formatCount(plays), m_library->formatCount(artists), m_library->formatCount(links));
    } catch (const std::exception &) {
    }
    emit learnedChanged();
}
