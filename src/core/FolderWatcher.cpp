// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/FolderWatcher.h"

#include "core/Database.h"
#include "core/TagReader.h"

#include <QDebug>
#include <QFile>
#include <QHash>
#include <QSet>

#include <limits>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/vfs.h>
#include <unistd.h>

namespace {

// What a folder scan has to look at: entries appearing, disappearing or
// finished writing. IN_CREATE for files is skipped (the file is still being
// written; IN_CLOSE_WRITE follows), IN_ATTRIB too (a tag editor writes the file).
constexpr uint32_t WatchMask = IN_CREATE | IN_DELETE | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF
                               | IN_MOVE_SELF | IN_ONLYDIR | IN_EXCL_UNLINK;

bool under(const QString &path, const QString &root)
{
    return path == root || path.startsWith(root + QLatin1Char('/'));
}

} // namespace

FolderWatcher::FolderWatcher(QObject *parent) : QObject(parent) {}

FolderWatcher::~FolderWatcher()
{
    stop();
}

bool FolderWatcher::isNetworkFs(const QString &path)
{
    struct statfs fs;
    if (statfs(QFile::encodeName(path).constData(), &fs) != 0)
        return false;
    switch (static_cast<unsigned long>(fs.f_type)) {
    case 0x6969:     // NFS
    case 0x517B:     // SMB
    case 0xFF534D42: // CIFS
    case 0xFE534D42: // SMB2
    case 0x65735546: // FUSE (sshfs, rclone, gvfs ...)
    case 0x01021997: // 9P
    case 0x00C36400: // Ceph
    case 0x5346414F: // AFS
    case 0x47504653: // GPFS
        return true;
    default:
        return false;
    }
}

qint64 FolderWatcher::kernelLimit()
{
    QFile f(QStringLiteral("/proc/sys/fs/inotify/max_user_watches"));
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    return f.readAll().trimmed().toLongLong();
}

QStringList FolderWatcher::networkRoots() const
{
    return m_network;
}

void FolderWatcher::start(const QStringList &roots)
{
    stop();
    m_roots = roots;
    m_network.clear();
    QStringList local;
    for (const QString &r : roots)
        (isNetworkFs(r) ? m_network : local) << r;
    m_watched = 0;
    m_total = 0;
    m_limited = false;
    m_active = true;
    if (!local.isEmpty()) {
        m_wake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        m_quit = false;
        m_thread = std::thread([this, local] { loop(local); });
    }
    emit statusChanged();
}

void FolderWatcher::stop()
{
    if (m_thread.joinable()) {
        m_quit = true;
        const uint64_t one = 1;
        (void)!::write(m_wake, &one, sizeof one);
        m_thread.join();
    }
    if (m_wake >= 0)
        ::close(m_wake);
    m_wake = -1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cmds.clear();
    }
    m_folderOfWd.clear();
    m_folderOfWd.shrink_to_fit();
    m_placeholder.clear();
    const bool was = m_active;
    m_active = false;
    m_watched = 0;
    m_total = 0;
    m_limited = false;
    if (was)
        emit statusChanged();
}

void FolderWatcher::addTrees(const QStringList &paths)
{
    if (!m_thread.joinable() || paths.isEmpty())
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cmds.push_back({0, paths});
    }
    const uint64_t one = 1;
    (void)!::write(m_wake, &one, sizeof one);
}

void FolderWatcher::resync()
{
    if (!m_thread.joinable())
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cmds.push_back({1, {}});
    }
    const uint64_t one = 1;
    (void)!::write(m_wake, &one, sizeof one);
}

// Worker thread ----------------------------------------------------------------

void FolderWatcher::addWatches(const QStringList &paths, bool all, const QStringList &roots)
{
    try {
        DbConnection db = Database::open(1024);
        db.exec("PRAGMA mmap_size=0"); // a short look at the folder list: no need to map the file
        auto inRoots = [&](const QString &p) {
            for (const QString &r : roots)
                if (under(p, r))
                    return true;
            return false;
        };
        qint64 seen = 0, known = 0;
        // Leave a tenth of the system's watches to other programs (file
        // managers, editors): they would fail in odd ways without any.
        const qint64 limit = kernelLimit();
        const qint64 cap = limit > 0 ? limit * 9 / 10 : std::numeric_limits<qint64>::max();
        auto watch = [&](qint64 id, const QString &path) {
            ++seen;
            if (m_limited.load())
                return;
            if (m_watched.load() >= cap) {
                m_limited = true;
                qInfo() << "Ordnerüberwachung: Grenze erreicht nach" << m_watched.load() << "Ordnern";
                return;
            }
            const int wd = inotify_add_watch(m_fd, QFile::encodeName(path).constData(), WatchMask);
            if (wd < 0) {
                if (errno == ENOSPC) {
                    m_limited = true; // the kernel's limit: the rest is checked from time to time
                    qInfo() << "Ordnerüberwachung: Grenze des Systems erreicht nach" << m_watched.load() << "Ordnern";
                }
                return; // gone in the meantime, no access ...
            }
            if (size_t(wd) >= m_folderOfWd.size())
                m_folderOfWd.resize(size_t(wd) + 1024, 0);
            if (m_folderOfWd[size_t(wd)] == 0)
                ++m_watched;
            else
                ++known;
            m_folderOfWd[size_t(wd)] = id;
            m_placeholder.erase(wd);
        };
        if (all) {
            m_limited = false; // try again: the limit may have been raised, folders freed
            qint64 total = 0;
            // Shallow folders first: if the limit is reached, a new album or artist
            // anywhere still shows up at once; only changes deep inside wait.
            Stmt q(db, "SELECT id, path FROM folders WHERE source_id = 1 "
                       "ORDER BY length(path) - length(replace(path, '/', '')), id");
            while (q.step() && !m_quit.load()) {
                const QString path = q.str(1);
                if (!inRoots(path))
                    continue;
                ++total;
                watch(q.i64(0), path);
            }
            m_total = total;
        } else {
            Stmt q(db, "SELECT id, path FROM folders WHERE source_id = 1 AND (path = ? OR (path >= ? AND path < ?)) ORDER BY id");
            for (const QString &p : paths) {
                if (!inRoots(p))
                    continue;
                q.bind(1, p).bind(2, p + QLatin1Char('/')).bind(3, p + QLatin1Char('0'));
                while (q.step())
                    watch(q.i64(0), q.str(1));
                q.reset();
            }
            m_total += seen - known; // folders new to the watcher (watched or over the limit)
        }
    } catch (const std::exception &e) {
        qWarning() << "Ordnerüberwachung:" << e.what();
    }
    QMetaObject::invokeMethod(this, &FolderWatcher::statusChanged, Qt::QueuedConnection);
}

// A folder that just appeared is watched right away, before the scan has
// even seen it: files copied into it meanwhile must not slip through. Its
// events count for the nearest known folder (whose scan picks up new
// subfolders as a whole); the scan's addTrees() gives it its own id later.
void FolderWatcher::watchNewFolder(int parentWd, qint64 knownFolder, const char *name)
{
    QByteArray parent;
    auto it = m_placeholder.find(parentWd);
    if (it != m_placeholder.end()) {
        parent = it->second;
    } else {
        try {
            DbConnection db = Database::open(256);
            db.exec("PRAGMA mmap_size=0");
            Stmt q(db, "SELECT path FROM folders WHERE id = ?");
            q.bind(1, knownFolder);
            if (q.step())
                parent = QFile::encodeName(q.str(0));
        } catch (const std::exception &) {
        }
    }
    if (parent.isEmpty() || m_limited.load())
        return;
    const QByteArray path = parent + '/' + name;
    const int wd = inotify_add_watch(m_fd, path.constData(), WatchMask);
    if (wd < 0)
        return;
    if (size_t(wd) >= m_folderOfWd.size())
        m_folderOfWd.resize(size_t(wd) + 1024, 0);
    if (m_folderOfWd[size_t(wd)] == 0) {
        ++m_watched;
        m_folderOfWd[size_t(wd)] = knownFolder;
        m_placeholder[wd] = path;
    }
}

void FolderWatcher::loop(QStringList roots)
{
    m_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_fd < 0) {
        qWarning() << "Ordnerüberwachung nicht möglich:" << std::strerror(errno);
        return;
    }
    addWatches({}, true, roots);

    std::vector<char> buf(64 * 1024);
    QSet<qint64> dirty;
    bool overflow = false;
    auto lastEvent = std::chrono::steady_clock::now();
    auto firstEvent = lastEvent;
    pollfd fds[2] = {{m_fd, POLLIN, 0}, {m_wake, POLLIN, 0}};

    while (!m_quit.load()) {
        const int timeout = (dirty.isEmpty() && !overflow) ? -1 : 250;
        const int r = ::poll(fds, 2, timeout);
        if (r < 0 && errno != EINTR)
            break;
        if (m_quit.load())
            break;

        if (fds[1].revents & POLLIN) {
            uint64_t v;
            (void)!::read(m_wake, &v, sizeof v);
            std::deque<Cmd> cmds;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                cmds.swap(m_cmds);
            }
            for (const Cmd &c : cmds)
                addWatches(c.paths, c.kind == 1, roots);
        }

        if (fds[0].revents & POLLIN) {
            for (;;) {
                const ssize_t n = ::read(m_fd, buf.data(), buf.size());
                if (n <= 0)
                    break;
                for (char *p = buf.data(); p < buf.data() + n;) {
                    const auto *ev = reinterpret_cast<const inotify_event *>(p);
                    p += sizeof(inotify_event) + ev->len;
                    if (ev->mask & IN_Q_OVERFLOW) {
                        overflow = true; // the kernel dropped events: only a rescan knows what happened
                        continue;
                    }
                    if (ev->wd < 0 || size_t(ev->wd) >= m_folderOfWd.size())
                        continue;
                    const qint64 folder = m_folderOfWd[size_t(ev->wd)];
                    if (ev->mask & IN_IGNORED) {
                        // folder deleted, moved away or its drive unmounted: the watch is gone
                        if (folder != 0)
                            --m_watched;
                        m_folderOfWd[size_t(ev->wd)] = 0;
                        m_placeholder.erase(ev->wd);
                        continue;
                    }
                    if (ev->mask & IN_MOVE_SELF) {
                        // moved somewhere else (maybe out of the library, e.g. to the
                        // trash): drop the watch, the new place gets one after its scan
                        inotify_rm_watch(m_fd, ev->wd);
                        continue;
                    }
                    if (folder == 0 || (ev->mask & (IN_DELETE_SELF | IN_UNMOUNT)))
                        continue; // the parent folder reports it (or the drive is away: keep everything)
                    if (ev->len == 0 || ev->name[0] == '.' || ev->name[0] == 0)
                        continue; // hidden files: partial downloads, sync tools
                    const bool isDir = ev->mask & IN_ISDIR;
                    if (!isDir && (ev->mask & IN_CREATE))
                        continue;
                    if (isDir && (ev->mask & (IN_CREATE | IN_MOVED_TO)))
                        watchNewFolder(ev->wd, folder, ev->name);
                    if (isDir || TagReader::isAudioFile(ev->name, std::strlen(ev->name))) {
                        if (dirty.isEmpty())
                            firstEvent = std::chrono::steady_clock::now();
                        dirty.insert(folder);
                        lastEvent = std::chrono::steady_clock::now();
                    }
                }
            }
        }

        // Hand over once things calm down for a moment (a copied album arrives
        // file by file); the library waits a little longer before scanning.
        const auto quiet = std::chrono::steady_clock::now() - lastEvent;
        if (overflow) {
            overflow = false;
            dirty.clear();
            QMetaObject::invokeMethod(this, &FolderWatcher::everythingChanged, Qt::QueuedConnection);
        } else if (!dirty.isEmpty() && (quiet > std::chrono::milliseconds(400)
                                        || std::chrono::steady_clock::now() - firstEvent > std::chrono::seconds(3))) {
            QStringList paths;
            try {
                DbConnection db = Database::open(256);
                db.exec("PRAGMA mmap_size=0");
                Stmt q(db, "SELECT path FROM folders WHERE id = ?");
                for (qint64 id : std::as_const(dirty)) {
                    q.bind(1, id);
                    if (q.step())
                        paths << q.str(0);
                    q.reset();
                }
            } catch (const std::exception &e) {
                qWarning() << "Ordnerüberwachung:" << e.what();
            }
            dirty.clear();
            if (!paths.isEmpty())
                QMetaObject::invokeMethod(this, [this, paths] { emit foldersChanged(paths); }, Qt::QueuedConnection);
        }
    }
    ::close(m_fd);
    m_fd = -1;
}
