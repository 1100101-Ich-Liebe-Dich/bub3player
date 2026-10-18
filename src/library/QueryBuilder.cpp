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
};

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

bool buildWhere(const QVariantMap &spec, QStringList &where, QVariantList &binds, QString &error)
{
    // Jellyfin copies of tracks that also exist locally (see JellyfinWriter::markDuplicates).
    if (g_hideDuplicates.load(std::memory_order_relaxed))
        where << QStringLiteral("t.dup = 0");
    const QVariantList filters = spec.value(QStringLiteral("filters")).toList();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
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
            where << QStringLiteral("t.folder_id IN (SELECT id FROM folders WHERE path = ? OR (path >= ? AND path < ?))");
            binds << p << (p + QLatin1Char('/')) << (p + QLatin1Char('0'));
            continue;
        }

        auto it = fieldMap().constFind(name);
        if (it == fieldMap().constEnd()) {
            error = QStringLiteral("Unbekanntes Feld: %1").arg(name);
            return false;
        }
        const QString e = QString::fromLatin1(it->expr);
        const QString coll = it->text ? QStringLiteral(" COLLATE NOCASE") : QString();

        if (op == QLatin1String("eq")) {
            where << e + QStringLiteral(" = ?") + coll;
            binds << value;
        } else if (op == QLatin1String("ne")) {
            where << e + QStringLiteral(" <> ?") + coll;
            binds << value;
        } else if (op == QLatin1String("lt") || op == QLatin1String("le") || op == QLatin1String("gt") || op == QLatin1String("ge")) {
            static const QHash<QString, QString> sym = {{QStringLiteral("lt"), QStringLiteral("<")}, {QStringLiteral("le"), QStringLiteral("<=")},
                                                        {QStringLiteral("gt"), QStringLiteral(">")}, {QStringLiteral("ge"), QStringLiteral(">=")}};
            where << QStringLiteral("%1 %2 ?").arg(e, sym.value(op));
            binds << value;
        } else if (op == QLatin1String("contains")) {
            where << e + QStringLiteral(" LIKE ? ESCAPE '\\'");
            binds << QStringLiteral("%%1%").arg(likeEscape(value.toString()));
        } else if (op == QLatin1String("startsWith")) {
            where << e + QStringLiteral(" LIKE ? ESCAPE '\\'");
            binds << QStringLiteral("%1%").arg(likeEscape(value.toString()));
        } else if (op == QLatin1String("empty")) {
            where << (it->text ? e + QStringLiteral(" = ''") : e + QStringLiteral(" = 0"));
        } else if (op == QLatin1String("notEmpty")) {
            where << (it->text ? e + QStringLiteral(" <> ''") : e + QStringLiteral(" > 0"));
        } else if (op == QLatin1String("withinDays")) {
            where << e + QStringLiteral(" >= ?");
            binds << QVariant::fromValue<qint64>(now - qint64(value.toDouble() * 86400));
        } else if (op == QLatin1String("olderThanDays")) {
            where << e + QStringLiteral(" < ?");
            binds << QVariant::fromValue<qint64>(now - qint64(value.toDouble() * 86400));
        } else {
            error = QStringLiteral("Unbekannter Operator: %1").arg(op);
            return false;
        }
    }

    const QString search = spec.value(QStringLiteral("search")).toString().trimmed();
    if (!search.isEmpty()) {
        if (Database::hasFts()) {
            const QString q = ftsQuery(search);
            if (!q.isEmpty()) {
                where << QStringLiteral("t.id IN (SELECT rowid FROM tracks_fts WHERE tracks_fts MATCH ?)");
                binds << q;
            }
        } else {
            for (const QString &w : search.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
                where << QStringLiteral("(t.title LIKE ? ESCAPE '\\' OR t.artist LIKE ? ESCAPE '\\' OR t.album LIKE ? ESCAPE '\\' OR t.aa LIKE ? ESCAPE '\\')");
                const QString p = QStringLiteral("%%1%").arg(likeEscape(w));
                binds << p << p << p << p;
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
    if (!buildWhere(spec, where, b.binds, b.error)) {
        b.ok = false;
        return b;
    }
    const QString sortKey = spec.value(QStringLiteral("sort"), QStringLiteral("album")).toString();
    const SortDef sd = sortMap().value(sortKey, sortMap().value(QStringLiteral("album")));
    const bool desc = spec.value(QStringLiteral("desc")).toBool();
    QStringList order;
    for (int i = 0; i < sd.keys.size(); ++i)
        order << sd.keys[i] + ((desc && i < sd.directional) ? QStringLiteral(" DESC") : QString());

    QString from = QStringLiteral(" FROM tracks t");
    if (sd.needsFolder)
        from += QStringLiteral(" JOIN folders f ON f.id = t.folder_id");
    if (!where.isEmpty())
        from += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    b.sql = QStringLiteral("SELECT t.id") + from + QStringLiteral(" ORDER BY ") + order.join(QStringLiteral(", "));
    b.sumSql = QStringLiteral("SELECT COALESCE(SUM(t.duration_ms), 0)") + from;
    const int limit = spec.value(QStringLiteral("limit")).toInt();
    if (limit > 0)
        b.sql += QStringLiteral(" LIMIT %1").arg(limit);
    return b;
}

Built groups(const QVariantMap &spec)
{
    Built b;
    QStringList where;
    if (!buildWhere(spec, where, b.binds, b.error)) {
        b.ok = false;
        return b;
    }
    const QString by = spec.value(QStringLiteral("groupBy"), QStringLiteral("album")).toString();
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
        b.sql = QStringLiteral("SELECT t.aa, t.album, COUNT(*), MIN(t.id), MAX(t.year), %1 FROM tracks t%2 "
                               "GROUP BY t.aa COLLATE NOCASE, t.album COLLATE NOCASE ORDER BY %3")
                    .arg(needAdded ? QStringLiteral("MAX(t.added_at)") : QStringLiteral("0"), whereSql, order);
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
    else if (timeLike)
        order = QStringLiteral("1%1").arg(desc ? QString() : QStringLiteral(" DESC"));
    else
        order = QStringLiteral("(%1 = ''), 1%2%3").arg(expr, coll, dir); // "Unbekannt" last
    if (timeLike && gsort != QLatin1String("count"))
        order = QStringLiteral("(%1 = 0), ").arg(expr) + order;       // "Ohne Jahr" last
    b.sql = QStringLiteral("SELECT %1, NULL, COUNT(*), MIN(t.id), 0, 0 FROM tracks t%2 GROUP BY 1%3 ORDER BY %4")
                .arg(expr, whereSql, coll, order);
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
