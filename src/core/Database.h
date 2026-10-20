// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Sql.h"
#include <QString>

// Schema, migrations and connection setup.
// SQLite runs in WAL mode so the UI can read while the scanner writes.
namespace Database {

constexpr int LocalSource = 1;

QString path();                    // active database file
void setPath(const QString &path); // override (command line / tests)

// Opens a new connection with all pragmas applied. Caller owns it.
DbConnection open(int cacheKiB = 16 * 1024);

// Creates or upgrades the schema. Call once at startup, before other threads.
void migrate();

bool hasFts(); // FTS5 available? Falls back to LIKE search if not.

// Bulk import: keeping the search index in sync row by row is the slowest part
// of a first scan, so triggers are dropped and the index is rebuilt once at the end.
// A 'fts_dirty' flag survives crashes, so an interrupted import still gets rebuilt.
void dropFtsTriggers(DbConnection &db);
void restoreFtsTriggers(DbConnection &db); // recreates triggers, rebuilds if dirty
bool ftsDirty(DbConnection &db);

void dropSortIndexes(DbConnection &db);
void createSortIndexes(DbConnection &db);

QString meta(DbConnection &db, const char *key);
void setMeta(DbConnection &db, const char *key, const QString &value);

// Effective album artist: album artist tag, else track artist.
inline QString effectiveAlbumArtist(const QString &albumArtist, const QString &artist)
{
    return albumArtist.isEmpty() ? artist : albumArtist;
}

} // namespace Database
