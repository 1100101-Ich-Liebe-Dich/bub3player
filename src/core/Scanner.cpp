// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Scanner.h"

#include "core/Database.h"
#include "core/Paths.h"
#include "core/TagReader.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QThreadPool>
#include <QtConcurrent>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct DevIno
{
    quint64 dev, ino;
    bool operator==(const DevIno &o) const { return dev == o.dev && ino == o.ino; }
};
struct DevInoHash
{
    size_t operator()(const DevIno &d) const { return std::hash<quint64>()(d.dev * 0x9E3779B97F4A7C15ULL ^ d.ino); }
};

struct FileStat
{
    qint64 size = 0, mtime = 0, added = 0;
    bool isDir = false, isReg = false;
    DevIno id{0, 0};
};

bool statAt(int dirfd, const char *name, FileStat &out)
{
    struct statx sx;
    const unsigned mask = STATX_TYPE | STATX_SIZE | STATX_MTIME | STATX_CTIME | STATX_BTIME | STATX_INO;
    if (statx(dirfd, name, 0, mask, &sx) == 0) {
        out.isDir = S_ISDIR(sx.stx_mode);
        out.isReg = S_ISREG(sx.stx_mode);
        out.size = qint64(sx.stx_size);
        out.mtime = sx.stx_mtime.tv_sec;
        // "Added" = when the file arrived on this disk: birth time if the
        // filesystem has it, else inode change time. Only used on first sight.
        out.added = (sx.stx_mask & STATX_BTIME) ? sx.stx_btime.tv_sec : sx.stx_ctime.tv_sec;
        out.id = {quint64(makedev(sx.stx_dev_major, sx.stx_dev_minor)), quint64(sx.stx_ino)};
        return true;
    }
    struct stat st;
    if (fstatat(dirfd, name, &st, 0) != 0)
        return false;
    out.isDir = S_ISDIR(st.st_mode);
    out.isReg = S_ISREG(st.st_mode);
    out.size = st.st_size;
    out.mtime = st.st_mtime;
    out.added = st.st_ctime;
    out.id = {quint64(st.st_dev), quint64(st.st_ino)};
    return true;
}

QString baseTitle(const std::string &name)
{
    const size_t dot = name.rfind('.');
    return QFile::decodeName(QByteArray(name.data(), int(dot == std::string::npos ? name.size() : dot)));
}

QString extUpper(const std::string &name)
{
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos)
        return {};
    return QString::fromLatin1(name.c_str() + dot + 1).toUpper();
}

bool isRotational(const QString &path)
{
    struct stat st;
    if (stat(QFile::encodeName(path).constData(), &st) != 0)
        return false;
    const QString sys = QStringLiteral("/sys/dev/block/%1:%2").arg(major(st.st_dev)).arg(minor(st.st_dev));
    const QString real = QFileInfo(sys).canonicalFilePath();
    if (real.isEmpty())
        return false;
    for (const QString &p : {real + QStringLiteral("/queue/rotational"), real + QStringLiteral("/../queue/rotational")}) {
        QFile f(p);
        if (f.open(QIODevice::ReadOnly))
            return f.readAll().trimmed() == "1";
    }
    return false;
}

// Groups writes into transactions: one fsync per few thousand rows instead of per row.
class Batcher
{
public:
    explicit Batcher(DbConnection &db) : m_db(db) {}
    ~Batcher() { finish(); }
    void op(int n = 1)
    {
        if (!m_open) {
            m_db.exec("BEGIN IMMEDIATE");
            m_open = true;
            m_timer.start();
        }
        m_ops += n;
        if (m_ops >= 5000 || m_timer.elapsed() > 750)
            flush();
    }
    void flush()
    {
        if (m_open) {
            m_db.exec("COMMIT");
            m_open = false;
            m_ops = 0;
            m_flushed = true;
        }
    }
    void finish() { try { flush(); } catch (...) { } }
    bool takeFlushed() { bool f = m_flushed; m_flushed = false; return f; }

private:
    DbConnection &m_db;
    bool m_open = false;
    bool m_flushed = false;
    int m_ops = 0;
    QElapsedTimer m_timer;
};

struct TagJob
{
    qint64 id = 0;
    QString path;
    TrackTags tags;
};

} // namespace

Scanner::Scanner(QObject *parent) : QObject(parent)
{
    m_poll.setInterval(200);
    connect(&m_poll, &QTimer::timeout, this, &Scanner::poll);
}

Scanner::~Scanner()
{
    m_cancel = true;
    if (m_thread.joinable())
        m_thread.join();
}

int Scanner::autoThreads(const QStringList &roots)
{
    for (const QString &r : roots)
        if (isRotational(r))
            return 2; // a spinning disk can't serve more; extra threads just cause seeking
    return std::clamp(QThread::idealThreadCount(), 2, 8);
}

void Scanner::start(const QStringList &roots, int threads)
{
    launch(roots, threads, Full, {});
}

void Scanner::startSubset(const QStringList &roots, int threads)
{
    launch(roots, threads, Subset, {});
}

void Scanner::startFolders(const QStringList &roots, const QStringList &folders)
{
    launch(roots, 2, Folders, folders);
}

void Scanner::launch(const QStringList &roots, int threads, int mode, const QStringList &folders)
{
    if (m_running)
        return;
    if (m_thread.joinable())
        m_thread.join();
    m_cancel = false;
    m_done = false;
    m_running = true;
    m_mode = mode;
    m_sinceNotify.start();
    emit runningChanged();
    m_thread = std::thread([this, roots, threads, mode, folders] { run(roots, threads, false, mode, folders); });
    m_poll.start();
}

void Scanner::cancel()
{
    m_cancel = true;
}

Scanner::Stats Scanner::runBlocking(const QStringList &roots, int threads, bool verbose)
{
    m_cancel = false;
    run(roots, threads, verbose);
    return m_stats;
}

void Scanner::poll()
{
    emit progressChanged();
    // Small automatic scans report once at the end, and only if something changed.
    if (m_mode == Full && m_dirty.load() && m_sinceNotify.elapsed() > 2500) {
        m_dirty = false;
        m_sinceNotify.restart();
        emit libraryChanged();
    }
    if (m_done.load()) {
        m_poll.stop();
        if (m_thread.joinable())
            m_thread.join();
        m_running = false;
        const QLocale de(QLocale::German);
        if (m_stats.mode != Full) {
            // small automatic scans keep the summary of the last full scan
        } else if (m_stats.cancelled) {
            m_summary = tr("Scan abgebrochen");
        } else {
            m_summary = (m_stats.files == 1 ? tr("%1 Datei geprüft: %2 neu, %3 geändert, %4 entfernt (%5 s)")
                                            : tr("%1 Dateien geprüft: %2 neu, %3 geändert, %4 entfernt (%5 s)"))
                            .arg(de.toString(m_stats.files), de.toString(m_stats.added),
                                 de.toString(m_stats.changed), de.toString(m_stats.removed),
                                 de.toString(m_stats.totalSec, 'f', 1));
            if (!m_stats.missingRoots.isEmpty())
                m_summary += tr(". Nicht erreichbar (übersprungen): %1").arg(m_stats.missingRoots.join(QStringLiteral(", ")));
        }
        m_phase = Idle;
        emit progressChanged();
        emit runningChanged();
        const Stats &st = m_stats;
        if (m_mode == Full || st.added + st.changed + st.removed + st.tagged + st.failed > 0)
            emit libraryChanged();
        emit finished();
    }
}

void Scanner::run(QStringList roots, int threads, bool verbose, int mode, QStringList folders)
{
    Stats st;
    st.mode = mode;
    for (QString &r : roots)
        while (r.size() > 1 && r.endsWith(QLatin1Char('/')))
            r.chop(1);
    QElapsedTimer total, part;
    total.start();
    m_folders = 0;
    m_files = 0;
    m_tagsDone = 0;
    m_tagsTotal = 0;

    try {
        DbConnection db = Database::open(48 * 1024);
        // Folder scans keep the current generation: they see too little to prune by it.
        const qint64 gen = db.scalar(mode == Folders ? "SELECT COALESCE(MAX(seen), 1) FROM folders"
                                                     : "SELECT COALESCE(MAX(seen), 0) + 1 FROM folders");
        const qint64 existing = db.scalar("SELECT COUNT(*) FROM tracks WHERE source_id = 1");
        // First import of a big library: drop the sort indexes and search triggers,
        // rebuild both once at the end. Several times faster than row-by-row upkeep.
        bool bulk = mode == Full && (existing < 5000 || Database::ftsDirty(db));
        bool indexesDropped = false;
        if (bulk) {
            Database::dropFtsTriggers(db);
            if (existing < 5000) {
                Database::dropSortIndexes(db);
                indexesDropped = true;
            }
        }

        // ---------------- Phase 1: walk ----------------
        m_phase = Walking;
        part.start();
        {
            Stmt selFolder(db, "SELECT id, dev FROM folders WHERE path = ?");
            Stmt insFolder(db, "INSERT INTO folders(source_id, parent_id, path, name, mtime, seen, dev) VALUES (1, ?, ?, ?, ?, ?, ?)");
            Stmt updFolder(db, "UPDATE folders SET parent_id = ?, mtime = ?, seen = ?, dev = ? WHERE id = ?");
            Stmt selTracks(db, "SELECT id, filename, size, mtime FROM tracks WHERE folder_id = ?");
            Stmt selChildren(db, "SELECT id, path, dev FROM folders WHERE parent_id = ? AND source_id = 1");
            Stmt knownBelow(db, "SELECT EXISTS(SELECT 1 FROM folders WHERE source_id = 1 AND path >= ? AND path < ?)");
            Stmt insTrack(db, "INSERT INTO tracks(source_id, folder_id, filename, size, mtime, added_at, title, codec) "
                              "VALUES (1, ?, ?, ?, ?, ?, ?, ?)");
            Stmt updTrack(db, "UPDATE tracks SET size = ?, mtime = ?, tags_read = 0 WHERE id = ?");
            Stmt delTrack(db, "DELETE FROM tracks WHERE id = ?");
            Stmt keepRoot(db, "UPDATE folders SET seen = ? WHERE path = ? OR (path >= ? AND path < ?)");
            Stmt delTreeTracks(db, "DELETE FROM tracks WHERE folder_id IN "
                                   "(SELECT id FROM folders WHERE source_id = 1 AND (path = ? OR (path >= ? AND path < ?)))");
            Stmt delTreeFolders(db, "DELETE FROM folders WHERE source_id = 1 AND (path = ? OR (path >= ? AND path < ?))");

            Batcher batch(db);
            std::unordered_set<DevIno, DevInoHash> visited;
            std::unordered_set<qint64> insertedIds;

            struct Pending { std::string path; qint64 parent; };
            struct Existing { qint64 id, size, mtime; bool seen; };

            auto bindTree = [](Stmt &q, const QString &path) {
                q.bind(1, path).bind(2, path + QLatin1Char('/')).bind(3, path + QLatin1Char('0'));
            };
            // A folder that is gone while its parent is still there (deleted or
            // moved away): remove it with everything below.
            auto deleteTree = [&](const QString &path) {
                bindTree(delTreeTracks, path);
                delTreeTracks.exec();
                st.removed += sqlite3_changes(db);
                bindTree(delTreeFolders, path);
                delTreeFolders.exec();
                batch.op();
            };
            // A root is usable if it is there, readable, and not an empty mount
            // point (a drive in /etc/fstab that is not mounted right now leaves
            // its empty folder behind) while we know music below it.
            auto rootUsable = [&](const QString &root) {
                const QByteArray b = QFile::encodeName(root);
                struct stat rs;
                if (stat(b.constData(), &rs) != 0 || !S_ISDIR(rs.st_mode) || access(b.constData(), R_OK | X_OK) != 0)
                    return false;
                bool empty = true;
                if (DIR *d = opendir(b.constData())) {
                    while (dirent *e = readdir(d))
                        if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0) {
                            empty = false;
                            break;
                        }
                    closedir(d);
                }
                if (!empty)
                    return true;
                knownBelow.bind(1, root + QLatin1Char('/')).bind(2, root + QLatin1Char('0'));
                const bool known = knownBelow.step() && knownBelow.i64(0) != 0;
                knownBelow.reset();
                return !known;
            };
            auto keepTree = [&](const QString &path) {
                keepRoot.bind(1, gen).bind(2, path).bind(3, path + QLatin1Char('/')).bind(4, path + QLatin1Char('0'));
                keepRoot.exec();
                batch.op();
            };

            // Reads folders from the stack. `onlyNew`: descend only into subfolders
            // the database does not know yet (folder scans: known ones are watched
            // on their own) and drop known subfolders that are gone.
            auto walk = [&](std::vector<Pending> &stack, bool onlyNew) {
                while (!stack.empty() && !m_cancel.load()) {
                    Pending cur = std::move(stack.back());
                    stack.pop_back();

                    const int fd = ::open(cur.path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
                    if (fd < 0)
                        continue;
                    struct stat dst;
                    if (fstat(fd, &dst) != 0 || !visited.insert({quint64(dst.st_dev), quint64(dst.st_ino)}).second) {
                        ::close(fd); // symlink loop or already scanned via another root
                        continue;
                    }
                    DIR *dir = fdopendir(fd);
                    if (!dir) {
                        ::close(fd);
                        continue;
                    }

                    std::vector<std::string> subdirs;
                    std::vector<std::pair<std::string, FileStat>> files;
                    bool anyEntry = false;
                    while (dirent *e = readdir(dir)) {
                        const char *n = e->d_name;
                        if (n[0] == '.' && (n[1] == 0 || (n[1] == '.' && n[2] == 0)))
                            continue;
                        anyEntry = true;
                        if (n[0] == '.')
                            continue; // hidden folders and files
                        const size_t len = std::strlen(n);
                        if (e->d_type == DT_DIR) {
                            subdirs.emplace_back(n, len);
                            continue;
                        }
                        const bool audio = TagReader::isAudioFile(n, len);
                        if (e->d_type == DT_REG && !audio)
                            continue; // skip images, cue, logs without touching the inode
                        FileStat fs;
                        if (!statAt(dirfd(dir), n, fs))
                            continue;
                        if (fs.isDir)
                            subdirs.emplace_back(n, len); // symlinked or DT_UNKNOWN folder
                        else if (fs.isReg && audio)
                            files.emplace_back(std::string(n, len), fs);
                    }
                    closedir(dir);
                    // Name order: new rows get ids in track order, which the
                    // "recently added" and album sorts use as their tiebreak.
                    std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

                    // Folder row
                    const QString qpath = QFile::decodeName(QByteArray(cur.path.data(), int(cur.path.size())));
                    qint64 folderId = 0, knownDev = 0;
                    selFolder.bind(1, qpath);
                    if (selFolder.step()) {
                        folderId = selFolder.i64(0);
                        knownDev = selFolder.i64(1);
                    }
                    selFolder.reset();
                    const qint64 dev = qint64(dst.st_dev);
                    if (folderId != 0 && knownDev != 0 && knownDev != dev && !anyEntry) {
                        // Was on another drive and is empty now: an unmounted mount
                        // point, not deleted music. Keep everything below.
                        keepTree(qpath);
                        continue;
                    }
                    if (folderId == 0) {
                        const size_t slash = cur.path.rfind('/');
                        const std::string name = slash == std::string::npos ? cur.path : cur.path.substr(slash + 1);
                        insFolder.bind(1, cur.parent).bind(2, qpath)
                            .bind(3, QFile::decodeName(QByteArray(name.data(), int(name.size()))))
                            .bind(4, qint64(dst.st_mtime)).bind(5, gen).bind(6, dev);
                        insFolder.exec();
                        folderId = sqlite3_last_insert_rowid(db);
                        if (!insertedIds.count(cur.parent))
                            st.newFolders << qpath;
                        insertedIds.insert(folderId);
                    } else {
                        updFolder.bind(1, cur.parent).bind(2, qint64(dst.st_mtime)).bind(3, gen).bind(4, dev).bind(5, folderId);
                        updFolder.exec();
                    }
                    batch.op();
                    ++st.folders;
                    ++m_folders;

                    // Diff files against what the database knows about this folder.
                    std::unordered_map<std::string, Existing> known;
                    selTracks.bind(1, folderId);
                    while (selTracks.step()) {
                        const QByteArray fn = QFile::encodeName(selTracks.str(1));
                        known.emplace(std::string(fn.constData(), size_t(fn.size())),
                                      Existing{selTracks.i64(0), selTracks.i64(2), selTracks.i64(3), false});
                    }
                    selTracks.reset();

                    for (const auto &[name, fs] : files) {
                        ++st.files;
                        ++m_files;
                        auto it = known.find(name);
                        if (it == known.end()) {
                            insTrack.bind(1, folderId)
                                .bind(2, QFile::decodeName(QByteArray(name.data(), int(name.size()))))
                                .bind(3, fs.size).bind(4, fs.mtime).bind(5, fs.added)
                                .bind(6, baseTitle(name)).bind(7, extUpper(name));
                            insTrack.exec();
                            ++st.added;
                            batch.op();
                        } else {
                            it->second.seen = true;
                            if (it->second.size != fs.size || it->second.mtime != fs.mtime) {
                                updTrack.bind(1, fs.size).bind(2, fs.mtime).bind(3, it->second.id);
                                updTrack.exec();
                                ++st.changed;
                                batch.op();
                            }
                        }
                    }
                    for (const auto &[name, ex] : known) {
                        if (!ex.seen) {
                            delTrack.bind(1, ex.id);
                            delTrack.exec();
                            ++st.removed;
                            batch.op();
                        }
                    }

                    // Depth-first in name order keeps folder ids in disk order,
                    // which phase 2 relies on for sequential reads.
                    std::sort(subdirs.begin(), subdirs.end(), std::greater<>());
                    if (onlyNew) {
                        std::unordered_set<std::string> onDisk(subdirs.begin(), subdirs.end());
                        std::unordered_set<std::string> knownNames;
                        QStringList gone;
                        selChildren.bind(1, folderId);
                        while (selChildren.step()) {
                            const QString child = selChildren.str(1);
                            const QByteArray nb = QFile::encodeName(child.mid(child.lastIndexOf(QLatin1Char('/')) + 1));
                            const std::string n(nb.constData(), size_t(nb.size()));
                            knownNames.insert(n);
                            const qint64 childDev = selChildren.i64(2);
                            // gone from a folder on another drive: that drive was unmounted
                            if (!onDisk.count(n) && (childDev == 0 || childDev == dev))
                                gone << child;
                        }
                        selChildren.reset();
                        for (const QString &g : std::as_const(gone))
                            deleteTree(g);
                        for (auto &sd : subdirs) {
                            if (knownNames.count(sd))
                                continue;
                            const std::string sub = cur.path + '/' + sd;
                            // A new link to a folder that is already in the library
                            // (the full scan reaches it there): don't add it twice.
                            struct stat ls;
                            if (lstat(sub.c_str(), &ls) == 0 && S_ISLNK(ls.st_mode)) {
                                const QString real = QFileInfo(QFile::decodeName(QByteArray(sub.data(), int(sub.size())))).canonicalFilePath();
                                bool inside = false;
                                for (const QString &r : std::as_const(roots))
                                    inside = inside || Paths::isUnder(real, r);
                                if (inside)
                                    continue;
                            }
                            stack.push_back({sub, folderId});
                        }
                    } else {
                        for (auto &sd : subdirs)
                            stack.push_back({cur.path + '/' + sd, folderId});
                    }

                    if (batch.takeFlushed())
                        m_dirty = true;
                }
            };

            if (mode == Folders) {
                // The folder watcher saw something change in these folders. A folder
                // that is gone is handled by its parent (which saw it go).
                auto rootOf = [&](const QString &p) -> QString {
                    for (const QString &r : std::as_const(roots))
                        if (Paths::isUnder(p, r))
                            return r;
                    return {};
                };
                auto knownId = [&](const QString &p) -> qint64 {
                    selFolder.bind(1, p);
                    const qint64 id = selFolder.step() ? selFolder.i64(0) : 0;
                    selFolder.reset();
                    return id;
                };
                QSet<QString> todo;
                for (QString f : std::as_const(folders)) {
                    while (f.size() > 1 && f.endsWith(QLatin1Char('/')))
                        f.chop(1);
                    const QString root = rootOf(f);
                    if (root.isEmpty() || !rootUsable(root))
                        continue; // not ours, or the whole drive is away: keep everything
                    // nearest folder that exists and that the database knows
                    while (f != root && (!QFileInfo(f).isDir() || knownId(f) == 0)) {
                        const qsizetype cut = f.lastIndexOf(QLatin1Char('/'));
                        f = cut > 0 ? f.left(cut) : root; // the root may be "/"
                    }
                    todo.insert(f);
                    st.scanned << f;
                }
                std::vector<Pending> stack;
                for (const QString &f : std::as_const(todo)) {
                    qint64 parent = 0;
                    if (!roots.contains(f)) {
                        selFolder.bind(1, f.left(f.lastIndexOf(QLatin1Char('/'))));
                        parent = selFolder.step() ? selFolder.i64(0) : 0;
                        selFolder.reset();
                    }
                    const QByteArray b = QFile::encodeName(f);
                    stack.push_back({std::string(b.constData(), size_t(b.size())), parent});
                }
                walk(stack, true);
            } else {
                for (QString root : roots) {
                    while (root.size() > 1 && root.endsWith(QLatin1Char('/')))
                        root.chop(1);
                    if (!rootUsable(root)) {
                        // Unplugged disk or unmounted share: keep everything we know about it.
                        st.missingRoots << root;
                        keepTree(root);
                        continue;
                    }
                    if (mode == Subset)
                        st.scanned << root;
                    const QByteArray rootBytes = QFile::encodeName(root);
                    std::vector<Pending> stack;
                    stack.push_back({std::string(rootBytes.constData(), size_t(rootBytes.size())), 0});
                    walk(stack, false);
                }
            }
            batch.flush();

            if (!m_cancel.load() && mode != Folders) {
                // A folder that lived on another drive than its parent and is gone
                // now was a mount point (udisks removes /media/<user>/DISK when the
                // drive is unplugged): keep it, like a missing root.
                {
                    Stmt mounts(db, "SELECT c.path FROM folders c JOIN folders p ON p.id = c.parent_id "
                                    "WHERE c.source_id = 1 AND c.seen <> ? AND c.dev <> 0 AND p.dev <> 0 AND c.dev <> p.dev");
                    mounts.bind(1, gen);
                    QStringList keep;
                    while (mounts.step())
                        keep << mounts.str(0);
                    mounts.reset();
                    for (const QString &k : std::as_const(keep))
                        keepTree(k);
                    batch.flush();
                }
                // Prune folders (and their tracks) that disappeared. Missing roots were
                // marked as seen above, so an unplugged disk is never wiped.
                // A full scan also drops folders of roots no longer in the library;
                // a subset scan only looks inside its roots.
                Transaction tx(db);
                if (mode == Full) {
                    Stmt delGone(db, "DELETE FROM tracks WHERE folder_id IN (SELECT id FROM folders WHERE source_id = 1 AND seen <> ?)");
                    delGone.bind(1, gen);
                    delGone.exec();
                    st.removed += sqlite3_changes(db);
                    Stmt delFolders(db, "DELETE FROM folders WHERE source_id = 1 AND seen <> ?");
                    delFolders.bind(1, gen);
                    delFolders.exec();
                } else {
                    Stmt delGone(db, "DELETE FROM tracks WHERE folder_id IN (SELECT id FROM folders WHERE source_id = 1 AND seen <> ?1 "
                                     "AND (path = ?2 OR (path >= ?3 AND path < ?4)))");
                    Stmt delFolders(db, "DELETE FROM folders WHERE source_id = 1 AND seen <> ?1 AND (path = ?2 OR (path >= ?3 AND path < ?4))");
                    for (const QString &r : std::as_const(st.scanned)) {
                        delGone.bind(1, gen).bind(2, r).bind(3, r + QLatin1Char('/')).bind(4, r + QLatin1Char('0'));
                        delGone.exec();
                        st.removed += sqlite3_changes(db);
                        delFolders.bind(1, gen).bind(2, r).bind(3, r + QLatin1Char('/')).bind(4, r + QLatin1Char('0'));
                        delFolders.exec();
                    }
                }
                tx.commit();
            }
        }
        st.walkSec = part.elapsed() / 1000.0;
        m_dirty = true;
        if (verbose)
            std::printf("Phase 1 (Ordner): %lld Ordner, %lld Dateien, %lld neu, %lld geändert, %lld entfernt in %.2f s\n",
                        st.folders, st.files, st.added, st.changed, st.removed, st.walkSec);

        // ---------------- Phase 2: tags ----------------
        m_phase = Tagging;
        part.restart();
        // Pending rows are always local (remote sources insert finished rows), and the
        // partial index keeps both queries proportional to the work left, not the library.
        const qint64 pending = db.scalar("SELECT COUNT(*) FROM tracks INDEXED BY idx_tracks_pending WHERE tags_read = 0");
        m_tagsTotal = pending;
        if (!bulk && pending > 50000 && mode == Full) {
            bulk = true;
            Database::dropFtsTriggers(db);
            Database::dropSortIndexes(db);
            indexesDropped = true;
        }
        if (threads <= 0)
            threads = autoThreads(roots);
        if (verbose)
            std::printf("Phase 2 (Tags): %lld Dateien, %d Threads\n", pending, threads);

        if (pending > 0 && !m_cancel.load()) {
            QThreadPool pool;
            pool.setMaxThreadCount(threads);
            Stmt chunkQ(db, "SELECT t.id, f.path, t.filename, t.folder_id FROM tracks t INDEXED BY idx_tracks_pending "
                            "JOIN folders f ON f.id = t.folder_id "
                            "WHERE t.tags_read = 0 AND (t.folder_id, t.id) > (?, ?) "
                            "ORDER BY t.folder_id, t.id LIMIT 512");
            Stmt upd(db, "UPDATE tracks SET tags_read = ?, title = ?, artist = ?, album = ?, album_artist = ?, aa = ?, "
                         "genre = ?, composer = ?, year = ?, track_no = ?, disc_no = ?, duration_ms = ?, bitrate = ?, "
                         "sample_rate = ?, channels = ?, bits = ?, codec = ? WHERE id = ?");

            qint64 lastFolder = -1, lastId = -1;
            auto fetch = [&](std::vector<TagJob> &out) {
                out.clear();
                chunkQ.bind(1, lastFolder).bind(2, lastId);
                while (chunkQ.step()) {
                    TagJob j;
                    j.id = chunkQ.i64(0);
                    j.path = chunkQ.str(1) + QLatin1Char('/') + chunkQ.str(2);
                    lastFolder = chunkQ.i64(3);
                    lastId = j.id;
                    out.push_back(std::move(j));
                }
                chunkQ.reset();
            };
            auto readTags = [](TagJob &j) { j.tags = TagReader::read(j.path); };

            // Two buffers: tags for chunk N+1 are read while chunk N is written.
            std::vector<TagJob> buf[2];
            int curBuf = 0;
            fetch(buf[0]);
            QFuture<void> fut = QtConcurrent::map(&pool, buf[0], readTags);
            while (!buf[curBuf].empty()) {
                fut.waitForFinished();
                const int nextBuf = 1 - curBuf;
                fetch(buf[nextBuf]);
                QFuture<void> nextFut;
                if (!buf[nextBuf].empty() && !m_cancel.load())
                    nextFut = QtConcurrent::map(&pool, buf[nextBuf], readTags);

                {
                    Transaction tx(db);
                    for (const TagJob &j : buf[curBuf]) {
                        const TrackTags &t = j.tags;
                        upd.bind(1, t.ok ? 1 : 2).bind(2, t.title).bind(3, t.artist).bind(4, t.album)
                            .bind(5, t.albumArtist).bind(6, Database::effectiveAlbumArtist(t.albumArtist, t.artist))
                            .bind(7, t.genre).bind(8, t.composer).bind(9, t.year).bind(10, t.trackNo)
                            .bind(11, t.discNo).bind(12, t.durationMs).bind(13, t.bitrate).bind(14, t.sampleRate)
                            .bind(15, t.channels).bind(16, t.bits).bind(17, t.codec).bind(18, j.id);
                        upd.exec();
                        t.ok ? ++st.tagged : ++st.failed;
                    }
                    tx.commit();
                }
                m_tagsDone += qint64(buf[curBuf].size());
                m_dirty = true;
                buf[curBuf].clear();

                if (m_cancel.load()) {
                    nextFut.waitForFinished();
                    break;
                }
                fut = nextFut;
                curBuf = nextBuf;
                if (verbose && (m_tagsDone.load() % 20480) < 512)
                    std::printf("  %lld / %lld\n", m_tagsDone.load(), pending);
            }
        }
        st.tagSec = part.elapsed() / 1000.0;
        if (verbose)
            std::printf("Phase 2 (Tags): %lld gelesen, %lld unlesbar in %.2f s\n", st.tagged, st.failed, st.tagSec);

        // ---------------- Phase 3: search index ----------------
        part.restart();
        if (indexesDropped) {
            // Even after a cancel: sorting without indexes would make the UI slow.
            m_phase = Indexing;
            Database::createSortIndexes(db);
        }
        if (bulk && !m_cancel.load()) {
            m_phase = Indexing;
            Database::restoreFtsTriggers(db);
        }
        if (mode == Full) {
            db.exec("PRAGMA optimize");
            db.exec("PRAGMA wal_checkpoint(TRUNCATE)");
        } else {
            db.exec("PRAGMA wal_checkpoint(PASSIVE)"); // small scans: never wait for readers
        }
        st.indexSec = part.elapsed() / 1000.0;
        if (verbose && bulk)
            std::printf("Phase 3 (Sortier- und Suchindex): %.2f s\n", st.indexSec);
    } catch (const std::exception &e) {
        qWarning() << "Scan fehlgeschlagen:" << e.what();
    }

    st.cancelled = m_cancel.load();
    st.totalSec = total.elapsed() / 1000.0;
    m_stats = st;
    m_done = true;
}
