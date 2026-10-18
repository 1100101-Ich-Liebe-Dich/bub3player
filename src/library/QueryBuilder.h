#pragma once

#include <QString>
#include <QVariant>

// Turns a view spec (plain data from QML or views.json) into SQL.
// Only whitelisted fields, operators and sort keys are accepted and every value
// is bound as a parameter, so user-defined views can never inject SQL.
//
// Spec keys:
//   filters: [{field, op, value}]   op: eq ne lt le gt ge contains startsWith empty notEmpty
//                                       withinDays olderThanDays under(folder path)
//   sort:    album | title | artist | year | added | mtime | duration | playCount
//            | lastPlayed | codec | genre | path | random
//   desc:    bool
//   search:  free text (FTS5 prefix match)
//   limit:   optional row cap
//   groupBy: album | albumArtist | artist | genre | year | decade | quality
//            | addedMonth | fileYear | composer          (group queries only)
//   groupSort: name | count | year | added
namespace QueryBuilder {

struct Built
{
    QString sql;
    QString sumSql; // total duration of the same selection (trackIds only)
    QVariantList binds;
    bool ok = true;
    QString error;
};

Built trackIds(const QVariantMap &spec);
Built groups(const QVariantMap &spec);

// Filters that select the tracks of one group row (used when opening a group).
QVariantList groupFilters(const QString &groupBy, const QVariant &key1, const QVariant &key2);

// Field list for building the view editor UI.
QStringList fields();

} // namespace QueryBuilder
