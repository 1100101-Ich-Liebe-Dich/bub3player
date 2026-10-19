#pragma once

#include <QObject>
#include <QStringList>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

// Watches the library folders with inotify and reports folders whose content
// changed (new, changed, moved or deleted audio files, new or removed
// subfolders). The library then rescans just those folders.
//
// One watch per folder, added from the database after each scan. The kernel
// limits the number of watches (fs.inotify.max_user_watches); when it is
// reached, the remaining folders are not watched and `limited` is set, so the
// library checks everything from time to time instead. Network shares (NFS,
// SMB, sshfs ...) are never watched: inotify does not see changes made on the
// server.
class FolderWatcher : public QObject
{
    Q_OBJECT

public:
    explicit FolderWatcher(QObject *parent = nullptr);
    ~FolderWatcher() override;

    // Start (or restart) watching all known local folders below `roots`.
    void start(const QStringList &roots);
    void stop();
    // After a scan: watch folders below these paths that are new in the database.
    void addTrees(const QStringList &paths);
    // After a full scan: (re)watch every folder, retry after hitting the limit.
    void resync();

    bool active() const { return m_active; }
    qint64 watched() const { return m_watched.load(); }
    qint64 total() const { return m_total.load(); }
    bool limited() const { return m_limited.load(); }
    QStringList networkRoots() const;
    static bool isNetworkFs(const QString &path);
    static qint64 kernelLimit();

signals:
    void foldersChanged(const QStringList &paths); // main thread, a few at a time
    void everythingChanged();                      // events were lost: rescan all
    void statusChanged();

private:
    struct Cmd { int kind; QStringList paths; }; // 0 add trees, 1 add all
    void loop(QStringList roots);
    void addWatches(const QStringList &paths, bool all, const QStringList &roots);

    std::thread m_thread;
    std::mutex m_mutex;
    std::deque<Cmd> m_cmds;
    int m_wake = -1;               // eventfd: commands or stop
    int m_fd = -1;                 // inotify (worker thread)
    std::atomic<bool> m_quit{false};
    std::atomic<qint64> m_watched{0}, m_total{0};
    std::atomic<bool> m_limited{false};
    bool m_active = false;
    QStringList m_roots, m_network;
    std::vector<qint64> m_folderOfWd; // watch descriptor -> folder id (worker thread only)
};
