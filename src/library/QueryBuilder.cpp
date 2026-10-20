// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/QueryBuilder.h"

#include <atomic>

#include "core/Database.h"
#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

namespace {

struct Field
{
    const char *expr;
    bool text;
    bool analysis = false; // needs the analysis table (alias a)
};

// Joined only when a filter, sort or grouping uses it. Stale analyses (file
// changed since) do not count.
const char *AnalysisJoin = " LEFT JOIN analysis a ON a.track_id = t.id AND a.mtime = t.mtime";

const QHash<QString, Field> &fieldMap()
{
    static const QHash<QString, Field> m = {
        {QStringLiteral("title"), {"t.title", true}},
        {QStringLiteral("artist"), {"t.artist", true}},
        {QStringLiteral("album"), {"t.album", true}},
        {QStringLiteral("albumArtist"), {"t.aa", true}},
        {QStringLiteral("genre"), {"t.genre", true}},
        {QStringLiteral("composer"), {"t.composer", true}},
        {QStringLiteral("codec"), {"t.codec", true}},
        {QStringLiteral("filename"), {"t.filename", true}},
        {QStringLiteral("year"), {"t.year", false}},
        {QStringLiteral("decade"), {"(t.year / 10 * 10)", false}},
        {QStringLiteral("bits"), {"t.bits", false}},
        {QStringLiteral("sampleRate"), {"t.sample_rate", false}},
        {QStringLiteral("bitrate"), {"t.bitrate", false}},
        {QStringLiteral("added"), {"t.added_at", false}},
        {QStringLiteral("mtime"), {"t.mtime", false}},
        {QStringLiteral("playCount"), {"t.play_count", false}},
        {QStringLiteral("skipCount"), {"t.skip_count", false}},
        {QStringLiteral("lastPlayed"), {"t.last_played", false}},
        {QStringLiteral("duration"), {"t.duration_ms", false}},
        {QStringLiteral("rating"), {"t.rating", false}},
        {QStringLiteral("folder"), {"t.folder_id", false}},
        {QStringLiteral("source"), {"t.source_id", false}},
        {QStringLiteral("addedMonth"), {"strftime('%Y-%m', t.added_at, 'unixepoch')", true}},
        {QStringLiteral("fileYear"), {"CAST(strftime('%Y', t.mtime, 'unixepoch') AS INTEGER)", false}},
        // from the audio analysis
        {QStringLiteral("bpm"), {"(CASE WHEN a.bpm_conf >= 0.3 THEN a.bpm END)", false, true}},
        {QStringLiteral("tempo"), {"(CASE WHEN a.bpm_conf >= 0.3 THEN CAST(a.bpm / 10 AS INTEGER) * 10 END)", false, true}},
        {QStringLiteral("key"), {"(CASE WHEN a.key >= 0 AND a.key_conf >= 0.25 THEN a.key * 2 + a.minor END)", false, true}},
        {QStringLiteral("energy"), {"a.energy", false, true}},
        {QStringLiteral("loudness"), {"a.loudness", false, true}},
        {QStringLiteral("quality"),
         {"(CASE WHEN t.bits > 0 AND t.sample_rate > 0 "
          "THEN printf('%s %d bit, %g kHz', t.codec, t.bits, t.sample_rate / 1000.0) "
          "WHEN t.bitrate > 0 THEN printf('%s %d kbps', t.codec, ((t.bitrate + 16) / 32) * 32) "
          "ELSE t.codec END)",
          true}},
    };
    return m;
}

struct SortDef
{
    QStringList keys;  // first keys get the direction, the rest stay ascending
    int directional;   // how many leading keys follow 'desc'
    bool needsFolder;
    bool needsAnalysis = false;
};

const QHash<QString, SortDef> &sortMap()
{
    static const QString albumTail = QStringLiteral("t.disc_no|t.track_no|t.filename");
    auto split = [](const QString &s) { return s.split(QLatin1Char('|')); };
    static const QHash<QString, SortDef> m = {
        // Album order stops at track_no: the index then serves the whole ORDER BY
        // (ties fall back to rowid = file name order), 4x faster on 1M rows.
        {QStringLiteral("album"), {split(QStringLiteral("t.aa COLLATE NOCASE|t.album COLLATE NOCASE|t.disc_no|t.track_no")), 2, false}},
        {QStringLiteral("title"), {split(QStringLiteral("t.title COLLATE NOCASE|t.id")), 1, false}},
        {QStringLiteral("artist"), {split(QStringLiteral("t.artist COLLATE NOCASE|t.album COLLATE NOCASE|") + albumTail), 1, false}},
        {QStringLiteral("year"), {split(QStringLiteral("t.year|t.aa COLLATE NOCASE|t.album COLLATE NOCASE|") + albumTail), 1, false}},
        {QStringLiteral("added"), {split(QStringLiteral("t.added_at|t.id")), 1, false}},
        {QStringLiteral("mtime"), {split(QStringLiteral("t.mtime|t.id")), 1, false}},
        {QStringLiteral("duration"), {split(QStringLiteral("t.duration_ms|t.id")), 1, false}},
        {QStringLiteral("playCount"), {split(QStringLiteral("t.play_count|t.last_played")), 2, false}},
        {QStringLiteral("lastPlayed"), {split(QStringLiteral("t.last_played|t.id")), 1, false}},
        {QStringLiteral("codec"), {split(QStringLiteral("t.codec|t.bits|t.sample_rate|t.bitrate|t.id")), 4, false}},
        {QStringLiteral("genre"), {split(QStringLiteral("t.genre COLLATE NOCASE|t.aa COLLATE NOCASE|t.album COLLATE NOCASE|") + albumTail), 1, false}},
        {QStringLiteral("path"), {split(QStringLiteral("f.path|t.filename")), 2, true}},
        {QStringLiteral("random"), {split(QStringLiteral("RANDOM()")), 0, false}},
        // playlist order (only with spec.playlist)
        {QStringLiteral("position"), {split(QStringLiteral("p.pos")), 1, false}},
        // unanalysed tracks last, either direction
        {QStringLiteral("bpm"), {split(QStringLiteral("(a.bpm_conf IS NULL OR a.bpm_conf < 0.3)|a.bpm|t.id")), 0, false, true}},
        {QStringLiteral("energy"), {split(QStringLiteral("(a.energy IS NULL)|a.energy|t.id")), 0, false, true}},
    };
    return m;
}

QString ftsQuery(const QString &text)
{
    // Each word becomes a quoted prefix term; quotes inside are stripped.
    QStringList terms;
    const QStringList words = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (QString w : words) {
        w.remove(QLatin1Char('"'));
        if (!w.isEmpty())
            terms << QStringLiteral("\"%1\"*").arg(w);
    }
    return terms.join(QLatin1Char(' '));
}

QString likeEscape(QString s)
{
    s.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    s.replace(QLatin1Char('%'), QLatin1String("\\%"));
    s.replace(QLatin1Char('_'), QLatin1String("\\_"));
    return s;
}

std::atomic<bool> g_hideDuplicates{false};

// "limit" for groups: only the first N (search rows).
QString groupLimit(const QVariantMap &spec)
{
    const int limit = spec.value(QStringLiteral("limit")).toInt();
    return limit > 0 ? QStringLiteral(" LIMIT %1").arg(limit) : QString();
}

bool buildWhere(const QVariantMap &spec, QStringList &where, QVariantList &binds, QString &error, bool *analysis)
{
    const qint64 playlist = spec.value(QStringLiteral("playlist")).toLongLong();
    if (playlist > 0) {
        // Tracks of one playlist (alias p, joined by the caller). Everything the
        // user put there is shown, local copies or not.
        where << QStringLiteral("p.playlist_id = ?");
        binds << QVariant::fromValue<qint64>(playlist);
    } else if (g_hideDuplicates.load(std::memory_order_relaxed)) {
        // Jellyfin copies of tracks that also exist locally (see JellyfinWriter::markDuplicates).
        where << QStringLiteral("t.dup = 0");
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    auto addFilters = [&](const QVariantList &filters, QStringList &out) -> bool {
    for (const QVariant &fv : filters) {
        const QVariantMap f = fv.toMap();
        const QString name = f.value(QStringLiteral("field")).toString();
        const QString op = f.value(QStringLiteral("op"), QStringLiteral("eq")).toString();
        const QVariant value = f.value(QStringLiteral("value"));

        if (op == QLatin1String("under")) {
            // Whole folder subtree; the range keeps the path index usable.
            QString p = value.toString();
            while (p.size() > 1 && p.endsWith(QLatin1Char('/')))
                p.chop(1);
            out << QStringLiteral("t.folder_id IN (SELECT id FROM folders WHERE path = ? OR (path >= ? AND path < ?))");
            binds << p << (p + QLatin1Char('/')) << (p + QLatin1Char('0'));
            continue;
        }
        if (name == QLatin1String("playlist")) {
            // In (eq) or not in (ne) a playlist. NOT IN must skip missing items
            // (NULL), or it would match nothing.
            if (op != QLatin1String("eq") && op != QLatin1String("ne")) {
                error = QStringLiteral("Unbekannter Operator: %1").arg(op);
                return false;
            }
            out << (op == QLatin1String("eq")
                        ? QStringLiteral("t.id IN (SELECT track_id FROM playlist_items WHERE playlist_id = ?)")
                        : QStringLiteral("t.id NOT IN (SELECT track_id FROM playlist_items WHERE playlist_id = ? AND track_id IS NOT NULL)"));
            binds << QVariant::fromValue<qint64>(value.toLongLong());
            continue;
        }

        auto it = fieldMap().constFind(name);
        if (it == fieldMap().constEnd()) {
            error = QStringLiteral("Unbekanntes Feld: %1").arg(name);
            return false;
        }
        const QString e = QString::fromLatin1(it->expr);
        const QString coll = it->text ? QStringLiteral(" COLLATE NOCASE") : QString();
        if (it->analysis && analysis)
            *analysis = true;

        if (op == QLatin1String("eq")) {
            out << e + QStringLiteral(" = ?") + coll;
            binds << value;
        } else if (op == QLatin1String("ne")) {
            out << e + QStringLiteral(" <> ?") + coll;
            binds << value;
        } else if (op == QLatin1String("lt") || op == QLatin1String("le") || op == QLatin1String("gt") || op == QLatin1String("ge")) {
            static const QHash<QString, QString> sym = {{QStringLiteral("lt"), QStringLiteral("<")}, {QStringLiteral("le"), QStringLiteral("<=")},
                                                        {QStringLiteral("gt"), QStringLiteral(">")}, {QStringLiteral("ge"), QStringLiteral(">=")}};
            out << QStringLiteral("%1 %2 ?").arg(e, sym.value(op));
            binds << value;
        } else if (op == QLatin1String("contains")) {
            out << e + QStringLiteral(" LIKE ? ESCAPE '\\'");
            binds << QStringLiteral("%%1%").arg(likeEscape(value.toString()));
        } else if (op == QLatin1String("startsWith")) {
            out << e + QStringLiteral(" LIKE ? ESCAPE '\\'");
            binds << QStringLiteral("%1%").arg(likeEscape(value.toString()));
        } else if (op == QLatin1String("empty")) {
            // analysis values: "not analysed (or not sure)" is NULL
            out << (it->analysis ? e + QStringLiteral(" IS NULL")
                    : it->text   ? e + QStringLiteral(" = ''") : e + QStringLiteral(" = 0"));
        } else if (op == QLatin1String("notEmpty")) {
            out << (it->analysis ? e + QStringLiteral(" IS NOT NULL")
                    : it->text   ? e + QStringLiteral(" <> ''") : e + QStringLiteral(" > 0"));
        } else if (op == QLatin1String("withinDays")) {
            out << e + QStringLiteral(" >= ?");
            binds << QVariant::fromValue<qint64>(now - qint64(value.toDouble() * 86400));
        } else if (op == QLatin1String("olderThanDays")) {
            out << e + QStringLiteral(" < ?");
            binds << QVariant::fromValue<qint64>(now - qint64(value.toDouble() * 86400));
        } else {
            error = QStringLiteral("Unbekannter Operator: %1").arg(op);
            return false;
        }
    }

    return true;
    };

    // Filters: all must match, or with "match": "any" at least one.
    // "require" always has to match as well (e.g. the group opened from a view).
    const bool any = spec.value(QStringLiteral("match")).toString() == QLatin1String("any");
    QStringList conds;
    if (!addFilters(spec.value(QStringLiteral("filters")).toList(), any ? conds : where))
        return false;
    if (any && !conds.isEmpty())
        where << QStringLiteral("(") + conds.join(QStringLiteral(" OR ")) + QStringLiteral(")");
    if (!addFilters(spec.value(QStringLiteral("require")).toList(), where))
        return false;

    const QString search = spec.value(QStringLiteral("search")).toString().trimmed();
    if (!search.isEmpty()) {
        // searchIn: only the album artist ("artist") or album and album artist ("album"),
        // for the artist and album rows above the search results.
        const QString in = spec.value(QStringLiteral("searchIn")).toString();
        const QStringList cols = in == QLatin1String("artist") ? QStringList{QStringLiteral("aa")}
                                 : in == QLatin1String("album") ? QStringList{QStringLiteral("album"), QStringLiteral("aa")}
                                                                : QStringList{QStringLiteral("title"), QStringLiteral("artist"),
                                                                              QStringLiteral("album"), QStringLiteral("aa")};
        if (Database::hasFts()) {
            QString q = ftsQuery(search);
            if (!q.isEmpty()) {
                if (!in.isEmpty())
                    q = QStringLiteral("{%1} : (%2)").arg(cols.join(QLatin1Char(' ')), q);
                where << QStringLiteral("t.id IN (SELECT rowid FROM tracks_fts WHERE tracks_fts MATCH ?)");
                binds << q;
            }
        } else {
            for (const QString &w : search.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
                QStringList any;
                const QString p = QStringLiteral("%%1%").arg(likeEscape(w));
                for (const QString &c : cols) {
                    any << QStringLiteral("t.%1 LIKE ? ESCAPE '\\'").arg(c);
                    binds << p;
                }
                where << QStringLiteral("(") + any.join(QStringLiteral(" OR ")) + QStringLiteral(")");
            }
        }
    }
    return true;
}

} // namespace

namespace QueryBuilder {

void setHideDuplicates(bool on)
{
    g_hideDuplicates.store(on);
}

bool hideDuplicates()
{
    return g_hideDuplicates.load();
}

QStringList fields()
{
    return fieldMap().keys();
}

Built trackIds(const QVariantMap &spec)
{
    Built b;
    QStringList where;
    bool analysis = false;
    if (!buildWhere(spec, where, b.binds, b.error, &analysis)) {
        b.ok = false;
        return b;
    }
    const bool playlist = spec.value(QStringLiteral("playlist")).toLongLong() > 0;
    QString sortKey = spec.value(QStringLiteral("sort"), playlist ? QStringLiteral("position") : QStringLiteral("album")).toString();
    if (sortKey == QLatin1String("position") && !playlist)
        sortKey = QStringLiteral("album");
    const SortDef sd = sortMap().value(sortKey, sortMap().value(QStringLiteral("album")));
    const bool desc = spec.value(QStringLiteral("desc")).toBool();
    QStringList order;
    const qint64 seed = spec.value(QStringLiteral("seed")).toLongLong();
    if (sortKey == QLatin1String("random") && seed > 0) {
        // Shuffled, but the same order on every reload until the seed changes
        // (a smart playlist must not reshuffle while a scan updates the library).
        // x = a·id + s1, then x² + s2 (mod p): a different seed gives a different
        // order, not just a rotation of the same one. Stays below 2^62.
        const QString x = QStringLiteral("((t.id * 1103515245 + %1) % 2147483647)").arg(seed % 2147483647LL);
        order << QStringLiteral("((%1 * %1 + %2) % 2147483647)").arg(x).arg((seed * 7919) % 2147483647LL) << QStringLiteral("t.id");
    } else {
        for (int i = 0; i < sd.keys.size(); ++i) {
            // bpm/energy: the value itself follows the direction, "not analysed" stays last
            const bool dir = sd.needsAnalysis ? i == 1 : i < sd.directional;
            order << sd.keys[i] + ((desc && dir) ? QStringLiteral(" DESC") : QString());
        }
    }

    QString from = playlist ? QStringLiteral(" FROM playlist_items p JOIN tracks t ON t.id = p.track_id")
                            : QStringLiteral(" FROM tracks t");
    if (sd.needsFolder)
        from += QStringLiteral(" JOIN folders f ON f.id = t.folder_id");
    if (analysis || sd.needsAnalysis)
        from += QLatin1String(AnalysisJoin);
    if (!where.isEmpty())
        from += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    // Playlists: the position comes along, it identifies the entry (a track may appear twice).
    b.withKeys = playlist;
    b.sql = (playlist ? QStringLiteral("SELECT t.id, p.pos") : QStringLiteral("SELECT t.id")) + from
            + QStringLiteral(" ORDER BY ") + order.join(QStringLiteral(", "));
    b.sumSql = QStringLiteral("SELECT COALESCE(SUM(t.duration_ms), 0)") + from;
    const int limit = spec.value(QStringLiteral("limit")).toInt();
    if (limit > 0) {
        b.sql += QStringLiteral(" LIMIT %1").arg(limit);
        // only the tracks that make the cut count
        b.sumSql = QStringLiteral("SELECT COALESCE(SUM(d), 0) FROM (SELECT t.duration_ms AS d") + from + QStringLiteral(" ORDER BY ")
                   + order.join(QStringLiteral(", ")) + QStringLiteral(" LIMIT %1)").arg(limit);
        b.countSql = QStringLiteral("SELECT COUNT(*), COALESCE(SUM(d), 0) FROM (SELECT t.duration_ms AS d") + from
                     + QStringLiteral(" ORDER BY ") + order.join(QStringLiteral(", ")) + QStringLiteral(" LIMIT %1)").arg(limit);
    } else {
        b.countSql = QStringLiteral("SELECT COUNT(*), COALESCE(SUM(t.duration_ms), 0)") + from;
    }
    return b;
}

Built groups(const QVariantMap &spec)
{
    Built b;
    QStringList where;
    bool analysis = false;
    if (!buildWhere(spec, where, b.binds, b.error, &analysis)) {
        b.ok = false;
        return b;
    }
    const QString by = spec.value(QStringLiteral("groupBy"), QStringLiteral("album")).toString();
    const bool byAnalysis = by == QLatin1String("tempo") || by == QLatin1String("key");
    if (byAnalysis) // only tracks that have a value
        where << QStringLiteral("%1 IS NOT NULL").arg(QLatin1String(fieldMap().value(by).expr));
    QString join = (analysis || byAnalysis) ? QString::fromLatin1(AnalysisJoin) : QString();
    if (spec.value(QStringLiteral("playlist")).toLongLong() > 0)
        join = QStringLiteral(" JOIN playlist_items p ON p.track_id = t.id") + join;
    const QString gsort = spec.value(QStringLiteral("groupSort"), QStringLiteral("name")).toString();
    const bool desc = spec.value(QStringLiteral("desc")).toBool();
    const QString dir = desc ? QStringLiteral(" DESC") : QString();
    const QString whereSql = where.isEmpty() ? QString() : QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));

    // Columns: key1, key2, count, coverId, year, added
    if (by == QLatin1String("album")) {
        QString order;
        if (gsort == QLatin1String("year"))
            order = QStringLiteral("5%1, 1 COLLATE NOCASE, 2 COLLATE NOCASE").arg(dir);
        else if (gsort == QLatin1String("added"))
            order = QStringLiteral("6%1").arg(desc ? QString() : QStringLiteral(" DESC"));
        else if (gsort == QLatin1String("count"))
            order = QStringLiteral("3%1").arg(desc ? QString() : QStringLiteral(" DESC"));
        else if (gsort == QLatin1String("title"))
            order = QStringLiteral("2 COLLATE NOCASE%1, 1 COLLATE NOCASE").arg(dir);
        else // unknown artist / album go last, not first
            order = QStringLiteral("(t.aa = '') , 1 COLLATE NOCASE%1, (t.album = ''), 2 COLLATE NOCASE%1").arg(dir);
        const bool needAdded = gsort == QLatin1String("added");
        b.sql = QStringLiteral("SELECT t.aa, t.album, COUNT(*), MIN(t.id), MAX(t.year), %1 FROM tracks t%2%3 "
                               "GROUP BY t.aa COLLATE NOCASE, t.album COLLATE NOCASE ORDER BY %4")
                    .arg(needAdded ? QStringLiteral("MAX(t.added_at)") : QStringLiteral("0"), join, whereSql, order)
                + groupLimit(spec);
        return b;
    }

    static const QHash<QString, QString> single = {
        {QStringLiteral("albumArtist"), QStringLiteral("t.aa")},
        {QStringLiteral("artist"), QStringLiteral("t.artist")},
        {QStringLiteral("genre"), QStringLiteral("t.genre")},
        {QStringLiteral("composer"), QStringLiteral("t.composer")},
        {QStringLiteral("year"), QStringLiteral("t.year")},
        {QStringLiteral("decade"), QStringLiteral("decade")},
        {QStringLiteral("quality"), QStringLiteral("quality")},
        {QStringLiteral("addedMonth"), QStringLiteral("addedMonth")},
        {QStringLiteral("fileYear"), QStringLiteral("fileYear")},
        {QStringLiteral("tempo"), QStringLiteral("tempo")},
        {QStringLiteral("key"), QStringLiteral("key")},
    };
    auto it = single.constFind(by);
    if (it == single.constEnd()) {
        b.ok = false;
        b.error = QStringLiteral("Unbekannte Gruppierung: %1").arg(by);
        return b;
    }
    QString expr = *it;
    bool text = true;
    if (!expr.startsWith(QLatin1String("t."))) {
        const Field f = fieldMap().value(expr);
        expr = QString::fromLatin1(f.expr);
        text = f.text;
    } else {
        text = expr != QLatin1String("t.year");
    }
    const QString coll = text ? QStringLiteral(" COLLATE NOCASE") : QString();
    // Time-like groups read best newest first.
    const bool timeLike = by == QLatin1String("addedMonth") || by == QLatin1String("fileYear")
                          || by == QLatin1String("year") || by == QLatin1String("decade");
    QString order;
    if (gsort == QLatin1String("count"))
        order = QStringLiteral("3%1").arg(desc ? QString() : QStringLiteral(" DESC"));
    else if (byAnalysis)
        order = QStringLiteral("1%1").arg(desc ? QStringLiteral(" DESC") : QString()); // slow to fast (key: GroupModel sorts by Camelot)
    else if (timeLike)
        order = QStringLiteral("1%1").arg(desc ? QString() : QStringLiteral(" DESC"));
    else
        order = QStringLiteral("(%1 = ''), 1%2%3").arg(expr, coll, dir); // "Unbekannt" last
    if (timeLike && gsort != QLatin1String("count"))
        order = QStringLiteral("(%1 = 0), ").arg(expr) + order;       // "Ohne Jahr" last
    b.sql = QStringLiteral("SELECT %1, NULL, COUNT(*), MIN(t.id), 0, 0 FROM tracks t%2%3 GROUP BY 1%4 ORDER BY %5")
                .arg(expr, join, whereSql, coll, order)
            + groupLimit(spec);
    return b;
}

QVariantList groupFilters(const QString &groupBy, const QVariant &key1, const QVariant &key2)
{
    auto f = [](const QString &field, const QVariant &v) {
        return QVariantMap{{QStringLiteral("field"), field}, {QStringLiteral("op"), QStringLiteral("eq")}, {QStringLiteral("value"), v}};
    };
    if (groupBy == QLatin1String("album"))
        return {f(QStringLiteral("albumArtist"), key1), f(QStringLiteral("album"), key2)};
    return {f(groupBy, key1)};
}

} // namespace QueryBuilder
