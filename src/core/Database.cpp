// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Database.h"
#include "core/Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>

namespace {
QString g_path;
bool g_hasFts = false;

const int SchemaVersion = 5;

const char *SchemaV1 = R"SQL(
CREATE TABLE IF NOT EXISTS sources (
  id        INTEGER PRIMARY KEY,
  kind      TEXT NOT NULL,           -- 'local' | 'jellyfin'
  name      TEXT NOT NULL,
  url       TEXT NOT NULL DEFAULT '',
  user_id   TEXT NOT NULL DEFAULT '',
  token     TEXT NOT NULL DEFAULT '',
  last_sync INTEGER NOT NULL DEFAULT 0
);
INSERT OR IGNORE INTO sources(id, kind, name) VALUES (1, 'local', 'Lokale Musik');

CREATE TABLE IF NOT EXISTS folders (
  id        INTEGER PRIMARY KEY,
  source_id INTEGER NOT NULL DEFAULT 1,
  parent_id INTEGER NOT NULL DEFAULT 0,
  path      TEXT NOT NULL UNIQUE,
  name      TEXT NOT NULL,
  mtime     INTEGER NOT NULL DEFAULT 0,
  seen      INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_folders_parent ON folders(parent_id, name);

CREATE TABLE IF NOT EXISTS tracks (
  id           INTEGER PRIMARY KEY,
  source_id    INTEGER NOT NULL DEFAULT 1,
  folder_id    INTEGER NOT NULL DEFAULT 0,
  filename     TEXT NOT NULL DEFAULT '',
  remote_id    TEXT,
  size         INTEGER NOT NULL DEFAULT 0,
  mtime        INTEGER NOT NULL DEFAULT 0,
  added_at     INTEGER NOT NULL DEFAULT 0,
  tags_read    INTEGER NOT NULL DEFAULT 0,  -- 0 pending, 1 ok, 2 unreadable
  title        TEXT NOT NULL DEFAULT '',
  artist       TEXT NOT NULL DEFAULT '',
  album        TEXT NOT NULL DEFAULT '',
  album_artist TEXT NOT NULL DEFAULT '',
  aa           TEXT NOT NULL DEFAULT '',    -- effective album artist
  genre        TEXT NOT NULL DEFAULT '',
  composer     TEXT NOT NULL DEFAULT '',
  year         INTEGER NOT NULL DEFAULT 0,
  track_no     INTEGER NOT NULL DEFAULT 0,
  disc_no      INTEGER NOT NULL DEFAULT 0,
  duration_ms  INTEGER NOT NULL DEFAULT 0,
  bitrate      INTEGER NOT NULL DEFAULT 0,
  sample_rate  INTEGER NOT NULL DEFAULT 0,
  channels     INTEGER NOT NULL DEFAULT 0,
  bits         INTEGER NOT NULL DEFAULT 0,
  codec        TEXT NOT NULL DEFAULT '',
  play_count   INTEGER NOT NULL DEFAULT 0,
  skip_count   INTEGER NOT NULL DEFAULT 0,
  last_played  INTEGER NOT NULL DEFAULT 0,
  rating       INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_tracks_folder  ON tracks(folder_id, filename);
CREATE INDEX IF NOT EXISTS idx_tracks_pending ON tracks(folder_id, id) WHERE tags_read = 0;

CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);

CREATE TABLE IF NOT EXISTS plays (
  id          INTEGER PRIMARY KEY,
  track_id    INTEGER NOT NULL,
  played_at   INTEGER NOT NULL,
  listened_ms INTEGER NOT NULL,
  completed   INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_plays_track ON plays(track_id);
CREATE INDEX IF NOT EXISTS idx_plays_time  ON plays(played_at);
)SQL";

// Secondary indexes for sorting and grouping. Dropped during a bulk import (every
// insert would otherwise update all of them) and rebuilt in one pass afterwards.
const char *SortIndexesV1 = R"SQL(
CREATE INDEX IF NOT EXISTS idx_tracks_album   ON tracks(aa COLLATE NOCASE, album COLLATE NOCASE, disc_no, track_no, year);
CREATE INDEX IF NOT EXISTS idx_tracks_title   ON tracks(title COLLATE NOCASE);
CREATE INDEX IF NOT EXISTS idx_tracks_artist  ON tracks(artist COLLATE NOCASE);
CREATE INDEX IF NOT EXISTS idx_tracks_genre   ON tracks(genre COLLATE NOCASE);
CREATE INDEX IF NOT EXISTS idx_tracks_year    ON tracks(year);
CREATE INDEX IF NOT EXISTS idx_tracks_added   ON tracks(added_at);
CREATE INDEX IF NOT EXISTS idx_tracks_mtime   ON tracks(mtime);
CREATE INDEX IF NOT EXISTS idx_tracks_played  ON tracks(last_played);
CREATE INDEX IF NOT EXISTS idx_tracks_remote  ON tracks(source_id, remote_id);
)SQL";
const char *SortIndexNames[] = {"idx_tracks_album", "idx_tracks_title", "idx_tracks_artist", "idx_tracks_genre", "idx_tracks_year", "idx_tracks_added", "idx_tracks_mtime", "idx_tracks_played", "idx_tracks_remote"};

const char *FtsTableV1 = R"SQL(
CREATE VIRTUAL TABLE IF NOT EXISTS tracks_fts USING fts5(
  title, artist, album, aa, genre, composer,
  content='tracks', content_rowid='id',
  tokenize='unicode61 remove_diacritics 2', prefix='2 3');
)SQL";

const char *FtsTriggersV1 = R"SQL(
CREATE TRIGGER IF NOT EXISTS tracks_ai AFTER INSERT ON tracks BEGIN
  INSERT INTO tracks_fts(rowid, title, artist, album, aa, genre, composer)
  VALUES (new.id, new.title, new.artist, new.album, new.aa, new.genre, new.composer);
END;
CREATE TRIGGER IF NOT EXISTS tracks_ad AFTER DELETE ON tracks BEGIN
  INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, aa, genre, composer)
  VALUES ('delete', old.id, old.title, old.artist, old.album, old.aa, old.genre, old.composer);
END;
CREATE TRIGGER IF NOT EXISTS tracks_au AFTER UPDATE OF title, artist, album, aa, genre, composer ON tracks BEGIN
  INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, aa, genre, composer)
  VALUES ('delete', old.id, old.title, old.artist, old.album, old.aa, old.genre, old.composer);
  INSERT INTO tracks_fts(rowid, title, artist, album, aa, genre, composer)
  VALUES (new.id, new.title, new.artist, new.album, new.aa, new.genre, new.composer);
END;
)SQL";
} // namespace

namespace Database {

QString path()
{
    if (g_path.isEmpty()) {
        g_path = Paths::dataDir() + QStringLiteral("/library.db");
    }
    return g_path;
}

void setPath(const QString &p)
{
    g_path = QFileInfo(p).absoluteFilePath();
    QDir().mkpath(QFileInfo(g_path).absolutePath());
}

DbConnection open(int cacheKiB)
{
    sqlite3 *raw = nullptr;
    const QByteArray p = path().toUtf8();
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
    if (sqlite3_open_v2(p.constData(), &raw, flags, nullptr) != SQLITE_OK) {
        const std::string msg = raw ? sqlite3_errmsg(raw) : "out of memory";
        if (raw)
            sqlite3_close_v2(raw);
        throw SqlError("Datenbank kann nicht geöffnet werden: " + msg);
    }
    DbConnection db(raw);
    sqlite3_busy_timeout(raw, 10000);
    db.exec("PRAGMA journal_mode=WAL");
    db.exec("PRAGMA synchronous=NORMAL");
    db.exec("PRAGMA foreign_keys=OFF");
    db.exec("PRAGMA temp_store=MEMORY");
    db.exec(QStringLiteral("PRAGMA cache_size=-%1").arg(cacheKiB).toUtf8().constData());
    // Memory-mapped reads help a lot on big libraries; kept modest for 32-bit/old boxes.
    db.exec(sizeof(void *) >= 8 ? "PRAGMA mmap_size=268435456" : "PRAGMA mmap_size=33554432");
    return db;
}

// V2: Jellyfin. Server settings, server path and cover reference per track,
// a flag for remote copies of local tracks, and a unique key for upserts.
void migrateV2(DbConnection &db)
{
    auto hasColumn = [&](const char *table, const char *col) {
        Stmt s(db, QStringLiteral("PRAGMA table_info(%1)").arg(QLatin1String(table)));
        while (s.step())
            if (s.str(1) == QLatin1String(col))
                return true;
        return false;
    };
    if (!hasColumn("sources", "config"))
        db.exec("ALTER TABLE sources ADD COLUMN config TEXT NOT NULL DEFAULT '{}'");
    if (!hasColumn("tracks", "remote_path"))
        db.exec("ALTER TABLE tracks ADD COLUMN remote_path TEXT NOT NULL DEFAULT ''");
    if (!hasColumn("tracks", "image_ref"))
        db.exec("ALTER TABLE tracks ADD COLUMN image_ref TEXT NOT NULL DEFAULT ''");
    if (!hasColumn("tracks", "remote_lib"))
        db.exec("ALTER TABLE tracks ADD COLUMN remote_lib TEXT NOT NULL DEFAULT ''");
    if (!hasColumn("tracks", "dup"))
        db.exec("ALTER TABLE tracks ADD COLUMN dup INTEGER NOT NULL DEFAULT 0");
    db.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_tracks_remote_u ON tracks(source_id, remote_id) WHERE remote_id IS NOT NULL");
}

// V3: audio analysis (tempo, key, loudness, transitions) and what the
// recommendations learn from the listening history.
const char *SchemaV3 = R"SQL(
CREATE TABLE IF NOT EXISTS analysis (
  track_id    INTEGER PRIMARY KEY,
  version     INTEGER NOT NULL,
  mtime       INTEGER NOT NULL DEFAULT 0,   -- tracks.mtime at analysis time (stale if different)
  status      INTEGER NOT NULL DEFAULT 0,   -- 0 ok, 1 unreadable, 2 only partly analysed (very long)
  analysed_at INTEGER NOT NULL DEFAULT 0,
  length_ms   INTEGER NOT NULL DEFAULT 0,   -- decoded length
  start_ms    INTEGER NOT NULL DEFAULT 0,   -- first sound (leading silence ends)
  end_ms      INTEGER NOT NULL DEFAULT 0,   -- last sound (trailing silence starts)
  intro_ms    INTEGER NOT NULL DEFAULT 0,   -- quiet intro / fade-in reaches full level
  outro_ms    INTEGER NOT NULL DEFAULT 0,   -- fade-out / quiet ending starts
  bpm         REAL NOT NULL DEFAULT 0,
  bpm_conf    REAL NOT NULL DEFAULT 0,
  beat_ms     INTEGER NOT NULL DEFAULT 0,   -- position of the first beat
  key         INTEGER NOT NULL DEFAULT -1,  -- 0 = C ... 11 = B, -1 unknown
  minor       INTEGER NOT NULL DEFAULT 0,
  key_conf    REAL NOT NULL DEFAULT 0,
  loudness    REAL NOT NULL DEFAULT 0,      -- integrated loudness, LUFS
  peak        REAL NOT NULL DEFAULT 0,      -- sample peak, dBFS
  lra         REAL NOT NULL DEFAULT 0,      -- loudness range, LU
  energy      REAL NOT NULL DEFAULT 0,      -- 0..1
  brightness  REAL NOT NULL DEFAULT 0,      -- spectral centroid, Hz
  onset_rate  REAL NOT NULL DEFAULT 0       -- note onsets per second
);
CREATE INDEX IF NOT EXISTS idx_analysis_bpm ON analysis(bpm);
CREATE TRIGGER IF NOT EXISTS tracks_ad_analysis AFTER DELETE ON tracks BEGIN
  DELETE FROM analysis WHERE track_id = old.id;
END;

-- Artists heard close together (both directions stored), learned from plays.
CREATE TABLE IF NOT EXISTS artist_links (
  a       TEXT NOT NULL,
  b       TEXT NOT NULL,
  w       REAL NOT NULL,
  updated INTEGER NOT NULL,
  PRIMARY KEY (a, b)
) WITHOUT ROWID;

-- Per artist: how often played / skipped, and when during the day.
CREATE TABLE IF NOT EXISTS artist_stats (
  artist    TEXT PRIMARY KEY,               -- key (lower case, without "feat. ...")
  name      TEXT NOT NULL DEFAULT '',       -- as written in the tags
  plays     INTEGER NOT NULL DEFAULT 0,
  skips     INTEGER NOT NULL DEFAULT 0,
  last      INTEGER NOT NULL DEFAULT 0,
  night     INTEGER NOT NULL DEFAULT 0,     -- 0-6 h
  morning   INTEGER NOT NULL DEFAULT 0,     -- 6-12 h
  afternoon INTEGER NOT NULL DEFAULT 0,     -- 12-18 h
  evening   INTEGER NOT NULL DEFAULT 0      -- 18-24 h
) WITHOUT ROWID;
)SQL";

// V4: playlists. Items keep a hint (path, title, artist, album, length) so a
// track that disappears (moved file, drive away, server removed) stays in the
// list as "missing" and is linked again when it shows up.
const char *SchemaV4 = R"SQL(
CREATE TABLE IF NOT EXISTS playlists (
  id      INTEGER PRIMARY KEY,
  name    TEXT NOT NULL,
  smart   INTEGER NOT NULL DEFAULT 0,
  spec    TEXT NOT NULL DEFAULT '',      -- smart playlists: rules as JSON
  created INTEGER NOT NULL DEFAULT 0,
  updated INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS playlist_items (
  playlist_id INTEGER NOT NULL,
  pos         INTEGER NOT NULL,
  track_id    INTEGER,                   -- NULL: track missing right now
  hint        TEXT NOT NULL DEFAULT '',
  PRIMARY KEY (playlist_id, pos)
) WITHOUT ROWID;
CREATE INDEX IF NOT EXISTS idx_playlist_items_track ON playlist_items(track_id) WHERE track_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_playlist_items_missing ON playlist_items(playlist_id) WHERE track_id IS NULL;
CREATE TRIGGER IF NOT EXISTS tracks_ad_playlist AFTER DELETE ON tracks BEGIN
  UPDATE playlist_items SET track_id = NULL WHERE track_id = old.id;
END;
)SQL";

// V5: internet radio stations (not tracks: they never reach views, statistics or
// analysis; the player knows them by negative ids, see External).
const char *SchemaV5 = R"SQL(
CREATE TABLE IF NOT EXISTS stations (
  id          INTEGER PRIMARY KEY AUTOINCREMENT, -- never reused: the queue keeps -id over a restart
  name        TEXT NOT NULL,
  url         TEXT NOT NULL,              -- as given (may be a .pls/.m3u address)
  stream      TEXT NOT NULL,              -- what plays
  homepage    TEXT NOT NULL DEFAULT '',
  logo        TEXT NOT NULL DEFAULT '',   -- address of the logo (radio-browser)
  tags        TEXT NOT NULL DEFAULT '',
  country     TEXT NOT NULL DEFAULT '',
  codec       TEXT NOT NULL DEFAULT '',
  bitrate     INTEGER NOT NULL DEFAULT 0,
  uuid        TEXT NOT NULL DEFAULT '',   -- radio-browser station uuid
  added       INTEGER NOT NULL DEFAULT 0,
  last_played INTEGER NOT NULL DEFAULT 0
);
)SQL";

void migrate()
{
    DbConnection db = open();
    const qint64 version = db.scalar("PRAGMA user_version");
    if (version > SchemaVersion)
        qWarning() << "Datenbank stammt von einer neueren Version" << version;

    db.exec("BEGIN");
    db.exec(SchemaV1);
    migrateV2(db);
    db.exec(SchemaV3);
    db.exec(SchemaV4);
    db.exec(SchemaV5);
    {   // V4: device of each folder, to tell an unmounted drive from deleted folders
        bool hasDev = false;
        Stmt s(db, "PRAGMA table_info(folders)");
        while (s.step())
            hasDev = hasDev || s.str(1) == QLatin1String("dev");
        if (!hasDev)
            db.exec("ALTER TABLE folders ADD COLUMN dev INTEGER NOT NULL DEFAULT 0");
    }
    {   // test builds of 0.6 created artist_stats without a name
        bool hasName = false;
        Stmt s(db, "PRAGMA table_info(artist_stats)");
        while (s.step())
            hasName = hasName || s.str(1) == QLatin1String("name");
        if (!hasName)
            db.exec("ALTER TABLE artist_stats ADD COLUMN name TEXT NOT NULL DEFAULT ''");
    }
    if (version < 3)
        setMeta(db, "learn_rebuild", "1"); // learn from the existing play history once
    db.exec(SortIndexesV1); // also restores them after an interrupted bulk import
    try {
        db.exec(FtsTableV1);
        // An interrupted bulk import leaves the index stale; triggers stay off
        // until the scanner rebuilds it (deleting from a stale index can corrupt it).
        if (meta(db, "fts_dirty") != QLatin1String("1"))
            db.exec(FtsTriggersV1);
        g_hasFts = true;
    } catch (const SqlError &e) {
        qWarning() << "FTS5 nicht verfügbar, Suche nutzt LIKE:" << e.what();
        g_hasFts = false;
    }
    db.exec(QStringLiteral("PRAGMA user_version=%1").arg(SchemaVersion).toUtf8().constData());
    db.exec("COMMIT");
    db.exec("PRAGMA optimize");
}

bool hasFts()
{
    return g_hasFts;
}

void dropSortIndexes(DbConnection &db)
{
    for (const char *name : SortIndexNames)
        db.exec(QStringLiteral("DROP INDEX IF EXISTS %1").arg(QLatin1String(name)).toUtf8().constData());
}

void createSortIndexes(DbConnection &db)
{
    db.exec(SortIndexesV1);
}

void dropFtsTriggers(DbConnection &db)
{
    if (!g_hasFts)
        return;
    setMeta(db, "fts_dirty", "1");
    db.exec("DROP TRIGGER IF EXISTS tracks_ai; DROP TRIGGER IF EXISTS tracks_ad; DROP TRIGGER IF EXISTS tracks_au;");
}

void restoreFtsTriggers(DbConnection &db)
{
    if (!g_hasFts)
        return;
    db.exec(FtsTriggersV1);
    if (meta(db, "fts_dirty") == QLatin1String("1")) {
        db.exec("INSERT INTO tracks_fts(tracks_fts) VALUES('rebuild')");
        setMeta(db, "fts_dirty", "0");
    }
}

bool ftsDirty(DbConnection &db)
{
    return g_hasFts && meta(db, "fts_dirty") == QLatin1String("1");
}

QString meta(DbConnection &db, const char *key)
{
    Stmt s(db, "SELECT value FROM meta WHERE key=?");
    s.bind(1, QString::fromLatin1(key));
    return s.step() ? s.str(0) : QString();
}

void setMeta(DbConnection &db, const char *key, const QString &value)
{
    Stmt s(db, "INSERT INTO meta(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    s.bind(1, QString::fromLatin1(key)).bind(2, value);
    s.exec();
}

} // namespace Database
