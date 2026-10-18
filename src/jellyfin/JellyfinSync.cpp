#include "jellyfin/JellyfinSync.h"

#include "core/Database.h"
#include "jellyfin/JellyfinAccess.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace {

QString codecName(const QString &codec, const QString &container)
{
    const QString c = codec.toLower();
    if (c == QLatin1String("flac")) return QStringLiteral("FLAC");
    if (c == QLatin1String("mp3")) return QStringLiteral("MP3");
    if (c == QLatin1String("vorbis")) return QStringLiteral("OGG");
    if (c == QLatin1String("opus")) return QStringLiteral("OPUS");
    if (c == QLatin1String("aac")) return QStringLiteral("AAC");
    if (c == QLatin1String("alac")) return QStringLiteral("ALAC");
    if (c == QLatin1String("ape")) return QStringLiteral("APE");
    if (c == QLatin1String("wavpack")) return QStringLiteral("WV");
    if (c.startsWith(QLatin1String("pcm"))) return container.compare(QLatin1String("aiff"), Qt::CaseInsensitive) == 0 ? QStringLiteral("AIFF") : QStringLiteral("WAV");
    if (c.startsWith(QLatin1String("dsd"))) return QStringLiteral("DSF");
    if (!c.isEmpty()) return c.toUpper();
    return container.toUpper();
}

bool lossless(const QString &codec)
{
    static const QStringList l = {QStringLiteral("FLAC"), QStringLiteral("ALAC"), QStringLiteral("WAV"), QStringLiteral("AIFF"),
                                  QStringLiteral("APE"), QStringLiteral("WV"), QStringLiteral("DSF")};
    return l.contains(codec);
}

QString dirOf(const QString &path)
{
    const int i = path.lastIndexOf(QLatin1Char('/'));
    return i > 0 ? path.left(i) : QString();
}

QString baseName(const QString &path)
{
    const int i = path.lastIndexOf(QLatin1Char('/'));
    return i >= 0 ? path.mid(i + 1) : path;
}

// Server paths may use backslashes (Windows servers).
QString normPath(QString p)
{
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return p;
}

QString safeComponent(QString s)
{
    s.replace(QLatin1Char('/'), QLatin1Char('-'));
    s = s.trimmed();
    return s.isEmpty() ? QStringLiteral("Unbekannt") : s;
}

const char *UpsertSql =
    "INSERT INTO tracks(source_id, remote_id, remote_lib, remote_path, folder_id, filename, size, mtime, added_at, tags_read, "
    "title, artist, album, album_artist, aa, genre, year, track_no, disc_no, duration_ms, bitrate, sample_rate, channels, bits, "
    "codec, play_count, last_played, rating, image_ref) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, 1, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28) "
    "ON CONFLICT(source_id, remote_id) WHERE remote_id IS NOT NULL DO UPDATE SET "
    "remote_lib = excluded.remote_lib, remote_path = excluded.remote_path, folder_id = excluded.folder_id, filename = excluded.filename, "
    "size = excluded.size, mtime = excluded.mtime, title = excluded.title, artist = excluded.artist, album = excluded.album, "
    "album_artist = excluded.album_artist, aa = excluded.aa, genre = excluded.genre, year = excluded.year, track_no = excluded.track_no, "
    "disc_no = excluded.disc_no, duration_ms = excluded.duration_ms, bitrate = excluded.bitrate, sample_rate = excluded.sample_rate, "
    "channels = excluded.channels, bits = excluded.bits, codec = excluded.codec, "
    "play_count = max(tracks.play_count, excluded.play_count), last_played = max(tracks.last_played, excluded.last_played), "
    "image_ref = excluded.image_ref "
    // only write when something really changed (keeps the search index and WAL quiet)
    "WHERE tracks.title IS NOT excluded.title OR tracks.artist IS NOT excluded.artist OR tracks.album IS NOT excluded.album "
    "OR tracks.aa IS NOT excluded.aa OR tracks.genre IS NOT excluded.genre OR tracks.year IS NOT excluded.year "
    "OR tracks.track_no IS NOT excluded.track_no OR tracks.disc_no IS NOT excluded.disc_no "
    "OR tracks.duration_ms IS NOT excluded.duration_ms OR tracks.folder_id IS NOT excluded.folder_id "
    "OR tracks.image_ref IS NOT excluded.image_ref OR tracks.codec IS NOT excluded.codec "
    "OR tracks.remote_lib IS NOT excluded.remote_lib OR tracks.remote_path IS NOT excluded.remote_path "
    "OR tracks.play_count < excluded.play_count OR tracks.last_played < excluded.last_played";

} // namespace

JellyfinWriter::JellyfinWriter(qint64 sourceId, const QString &serverName) : m_source(sourceId), m_serverName(serverName) {}

void JellyfinWriter::ensureOpen()
{
    if (m_db.isOpen())
        return;
    m_db = Database::open(8 * 1024);
    Stmt s(m_db, "SELECT id, path FROM folders WHERE source_id = ?");
    s.bind(1, m_source);
    while (s.step())
        m_folders.insert(s.str(1), s.i64(0));
}

qint64 JellyfinWriter::folderFor(const QString &libId, const QString &libName, const QString &relDir)
{
    // Root per library: "jellyfin:<source>:<library>", shown as "<Server> · <Library>".
    const QString root = QStringLiteral("jellyfin:%1:%2").arg(m_source).arg(libId);
    auto make = [&](const QString &path, const QString &name, qint64 parent) -> qint64 {
        auto it = m_folders.constFind(path);
        if (it != m_folders.constEnd())
            return *it;
        Stmt ins(m_db, "INSERT INTO folders(source_id, parent_id, path, name, mtime, seen) VALUES (?, ?, ?, ?, 0, 0) "
                       "ON CONFLICT(path) DO UPDATE SET name = excluded.name RETURNING id");
        ins.bind(1, m_source).bind(2, parent).bind(3, path).bind(4, name);
        qint64 id = 0;
        if (ins.step())
            id = ins.i64(0);
        m_folders.insert(path, id);
        return id;
    };
    qint64 parent = make(root, QStringLiteral("%1 · %2").arg(m_serverName, libName), 0);
    QString cur = root;
    const QStringList parts = relDir.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        cur += QLatin1Char('/') + p;
        parent = make(cur, p, parent);
    }
    return parent;
}

JellyfinWriter::Page JellyfinWriter::writePage(const QByteArray &json, const QString &libId, const QString &libName,
                                               const QString &prefix, bool trackSeen)
{
    Page page;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (perr.error != QJsonParseError::NoError) {
        page.error = QStringLiteral("Antwort des Servers unlesbar: %1").arg(perr.errorString());
        return page;
    }
    const QJsonObject root = doc.object();
    const QJsonArray items = root.value(QLatin1String("Items")).toArray();
    page.items = int(items.size());
    page.total = root.value(QLatin1String("TotalRecordCount")).toInt();
    try {
        ensureOpen();
        if (trackSeen && !m_seenReady)
            resetSeen();
        Transaction tx(m_db);
        Stmt up(m_db, UpsertSql);
        Stmt seen;
        if (trackSeen)
            seen.prepare(m_db, "INSERT OR IGNORE INTO temp.jf_seen(remote_id) VALUES (?)");
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const QString pre = normPath(prefix);
        for (const QJsonValue &v : items) {
            const QJsonObject it = v.toObject();
            const QString id = it.value(QLatin1String("Id")).toString();
            if (id.isEmpty() || it.value(QLatin1String("Type")).toString() != QLatin1String("Audio"))
                continue;
            if (trackSeen) {
                seen.bind(1, id);
                seen.exec();
            }
            const QString path = normPath(it.value(QLatin1String("Path")).toString());
            const QString title = it.value(QLatin1String("Name")).toString();
            const QString album = it.value(QLatin1String("Album")).toString();
            QStringList artists;
            for (const QJsonValue &a : it.value(QLatin1String("Artists")).toArray())
                artists << a.toString();
            QString albumArtist = it.value(QLatin1String("AlbumArtist")).toString();
            const QString artist = artists.isEmpty() ? albumArtist : artists.join(QStringLiteral(" / "));
            const QString aa = albumArtist.isEmpty() ? (artists.isEmpty() ? QString() : artists.first()) : albumArtist;
            const QJsonArray genres = it.value(QLatin1String("Genres")).toArray();
            const QString genre = genres.isEmpty() ? QString() : genres.first().toString();

            // Format from the first media source / audio stream.
            const QJsonObject ms = it.value(QLatin1String("MediaSources")).toArray().first().toObject();
            QJsonObject stream;
            for (const QJsonValue &sv : ms.value(QLatin1String("MediaStreams")).toArray()) {
                if (sv.toObject().value(QLatin1String("Type")).toString() == QLatin1String("Audio")) {
                    stream = sv.toObject();
                    break;
                }
            }
            const QString container = ms.value(QLatin1String("Container")).toString(it.value(QLatin1String("Container")).toString());
            const QString codec = codecName(stream.value(QLatin1String("Codec")).toString(), container);
            const int bitrate = int((stream.value(QLatin1String("BitRate")).toDouble(ms.value(QLatin1String("Bitrate")).toDouble())) / 1000);
            const int bits = lossless(codec) ? stream.value(QLatin1String("BitDepth")).toInt() : 0;

            // Folder view: the server's directory below the library root,
            // or Artist/Album when the server does not tell us the path.
            QString rel;
            if (!path.isEmpty()) {
                const QString dir = dirOf(path);
                rel = (!pre.isEmpty() && (dir == pre || dir.startsWith(pre + QLatin1Char('/')))) ? dir.mid(pre.size()) : dir;
            } else {
                rel = QLatin1Char('/') + safeComponent(aa) + QLatin1Char('/') + safeComponent(album);
            }
            const qint64 folder = folderFor(libId, libName, rel);

            QString imageRef;
            const QString albumTag = it.value(QLatin1String("AlbumPrimaryImageTag")).toString();
            const QString ownTag = it.value(QLatin1String("ImageTags")).toObject().value(QLatin1String("Primary")).toString();
            if (!ownTag.isEmpty())
                imageRef = id + QLatin1Char(':') + ownTag;
            else if (!albumTag.isEmpty())
                imageRef = it.value(QLatin1String("AlbumId")).toString() + QLatin1Char(':') + albumTag;

            const QJsonObject ud = it.value(QLatin1String("UserData")).toObject();
            up.bind(1, m_source).bind(2, id).bind(3, libId).bind(4, path).bind(5, folder)
                .bind(6, path.isEmpty() ? title : baseName(path))
                .bind(7, qint64(ms.value(QLatin1String("Size")).toDouble()))
                .bind(8, now)
                .bind(9, JellyfinAccess::parseTime(it.value(QLatin1String("DateCreated")).toString()))
                .bind(10, title).bind(11, artist).bind(12, album).bind(13, albumArtist).bind(14, aa).bind(15, genre)
                .bind(16, it.value(QLatin1String("ProductionYear")).toInt())
                .bind(17, it.value(QLatin1String("IndexNumber")).toInt())
                .bind(18, it.value(QLatin1String("ParentIndexNumber")).toInt())
                .bind(19, qint64(it.value(QLatin1String("RunTimeTicks")).toDouble() / 10000))
                .bind(20, bitrate)
                .bind(21, stream.value(QLatin1String("SampleRate")).toInt())
                .bind(22, stream.value(QLatin1String("Channels")).toInt())
                .bind(23, bits)
                .bind(24, codec)
                .bind(25, ud.value(QLatin1String("PlayCount")).toInt())
                .bind(26, JellyfinAccess::parseTime(ud.value(QLatin1String("LastPlayedDate")).toString()))
                .bind(27, ud.value(QLatin1String("IsFavorite")).toBool() ? 5 : 0)
                .bind(28, imageRef);
            up.exec();
            page.changed += sqlite3_changes(m_db);
        }
        tx.commit();
    } catch (const std::exception &e) {
        page.error = QString::fromUtf8(e.what());
    }
    return page;
}

void JellyfinWriter::resetSeen()
{
    ensureOpen();
    m_db.exec("CREATE TEMP TABLE IF NOT EXISTS jf_seen(remote_id TEXT PRIMARY KEY)");
    m_db.exec("DELETE FROM temp.jf_seen");
    m_seenReady = true;
}

JellyfinWriter::Page JellyfinWriter::seenPage(const QByteArray &json)
{
    Page page;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonArray items = root.value(QLatin1String("Items")).toArray();
    page.items = int(items.size());
    page.total = root.value(QLatin1String("TotalRecordCount")).toInt();
    try {
        if (!m_seenReady)
            resetSeen();
        Transaction tx(m_db);
        Stmt seen(m_db, "INSERT OR IGNORE INTO temp.jf_seen(remote_id) VALUES (?)");
        for (const QJsonValue &v : items) {
            seen.bind(1, v.toObject().value(QLatin1String("Id")).toString());
            seen.exec();
        }
        tx.commit();
    } catch (const std::exception &e) {
        page.error = QString::fromUtf8(e.what());
    }
    return page;
}

int JellyfinWriter::removeUnseen(const QString &libId)
{
    try {
        ensureOpen();
        if (!m_seenReady)
            return 0;
        Stmt del(m_db, "DELETE FROM tracks WHERE source_id = ? AND remote_lib = ? "
                       "AND remote_id NOT IN (SELECT remote_id FROM temp.jf_seen)");
        del.bind(1, m_source).bind(2, libId);
        del.step();
        return sqlite3_changes(m_db);
    } catch (const std::exception &e) {
        qWarning() << "Jellyfin:" << e.what();
        return 0;
    }
}

int JellyfinWriter::removeLibrary(const QString &libId)
{
    try {
        ensureOpen();
        Stmt del(m_db, "DELETE FROM tracks WHERE source_id = ? AND remote_lib = ?");
        del.bind(1, m_source).bind(2, libId);
        del.step();
        const int n = sqlite3_changes(m_db);
        pruneFolders();
        return n;
    } catch (const std::exception &e) {
        qWarning() << "Jellyfin:" << e.what();
        return 0;
    }
}

int JellyfinWriter::localCount(const QString &libId)
{
    ensureOpen();
    Stmt s(m_db, "SELECT COUNT(*) FROM tracks WHERE source_id = ? AND remote_lib = ?");
    s.bind(1, m_source).bind(2, libId);
    return s.step() ? s.i32(0) : 0;
}

void JellyfinWriter::pruneFolders()
{
    // Folders without tracks and without subfolders, a few levels deep.
    try {
        ensureOpen();
        for (int pass = 0; pass < 12; ++pass) {
            Stmt del(m_db, "DELETE FROM folders WHERE source_id = ?1 "
                           "AND NOT EXISTS (SELECT 1 FROM tracks t WHERE t.folder_id = folders.id) "
                           "AND NOT EXISTS (SELECT 1 FROM folders c WHERE c.parent_id = folders.id)");
            del.bind(1, m_source);
            del.step();
            if (sqlite3_changes(m_db) == 0)
                break;
        }
        m_folders.clear();
        Stmt s(m_db, "SELECT id, path FROM folders WHERE source_id = ?");
        s.bind(1, m_source);
        while (s.step())
            m_folders.insert(s.str(1), s.i64(0));
    } catch (const std::exception &e) {
        qWarning() << "Jellyfin:" << e.what();
    }
}

// Flips only rows whose state changes, so sqlite3_changes() tells whether the
// views need a refresh. The lookup uses idx_tracks_album (aa, album NOCASE).
static const char *DupSql =
    "UPDATE tracks SET dup = 1 - dup WHERE source_id <> 1 AND dup <> EXISTS (SELECT 1 FROM tracks l "
    "WHERE l.source_id = 1 AND l.aa = tracks.aa COLLATE NOCASE AND l.album = tracks.album COLLATE NOCASE "
    "AND l.title = tracks.title COLLATE NOCASE AND abs(l.duration_ms - tracks.duration_ms) <= 3000)";

int JellyfinWriter::markDuplicates()
{
    try {
        ensureOpen();
        Stmt s(m_db, QStringLiteral("%1 AND source_id = ?").arg(QLatin1String(DupSql)));
        s.bind(1, m_source);
        s.step();
        return sqlite3_changes(m_db);
    } catch (const std::exception &e) {
        qWarning() << "Jellyfin:" << e.what();
        return 0;
    }
}

int JellyfinWriter::markAllDuplicates()
{
    try {
        DbConnection db = Database::open(4 * 1024);
        if (db.scalar("SELECT COUNT(*) FROM tracks WHERE source_id <> 1") == 0)
            return 0;
        db.exec(DupSql);
        return sqlite3_changes(db);
    } catch (const std::exception &e) {
        qWarning() << "Jellyfin:" << e.what();
        return 0;
    }
}

int JellyfinWriter::removeSource(qint64 sourceId)
{
    try {
        DbConnection db = Database::open(4 * 1024);
        Transaction tx(db);
        Stmt t(db, "DELETE FROM tracks WHERE source_id = ?");
        t.bind(1, sourceId);
        t.step();
        const int n = sqlite3_changes(db);
        Stmt f(db, "DELETE FROM folders WHERE source_id = ?");
        f.bind(1, sourceId);
        f.step();
        Stmt s(db, "DELETE FROM sources WHERE id = ?");
        s.bind(1, sourceId);
        s.step();
        tx.commit();
        return n;
    } catch (const std::exception &e) {
        qWarning() << "Jellyfin:" << e.what();
        return 0;
    }
}

QString JellyfinWriter::commonPrefix(const QByteArray &json)
{
    const QJsonArray items = QJsonDocument::fromJson(json).object().value(QLatin1String("Items")).toArray();
    QString prefix;
    bool first = true;
    for (const QJsonValue &v : items) {
        const QString dir = dirOf(normPath(v.toObject().value(QLatin1String("Path")).toString()));
        if (dir.isEmpty())
            continue;
        if (first) {
            prefix = dir;
            first = false;
            continue;
        }
        while (!prefix.isEmpty() && dir != prefix && !dir.startsWith(prefix + QLatin1Char('/')))
            prefix = dirOf(prefix);
    }
    return prefix;
}
