// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <atomic>
#include <thread>

// Two-phase, incremental library scanner.
//
// Phase 1 (walk):  walks the folders with raw readdir/statx. New files are inserted
//                  right away with their file name as title, so the folder view is
//                  usable within seconds. Unchanged files (same size + mtime) are skipped.
// Phase 2 (tags):  reads tags for everything still marked pending, in directory order
//                  (kind to spinning disks), with a small thread pool. Resumable: if
//                  the app quits mid-scan, the next run continues where it stopped.
// Safety:          a library folder that is missing (unmounted disk) is never pruned.
//
// Besides the full scan there are two smaller ones, both "in the background"
// (no progress bar): a subset of the roots (network shares checked from time
// to time, a drive that came back) and single folders reported by the folder
// watcher (their files, plus new subfolders as a whole).
class Scanner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int phase READ phase NOTIFY progressChanged)
    Q_PROPERTY(qint64 foldersSeen READ foldersSeen NOTIFY progressChanged)
    Q_PROPERTY(qint64 filesSeen READ filesSeen NOTIFY progressChanged)
    Q_PROPERTY(qint64 tagsDone READ tagsDone NOTIFY progressChanged)
    Q_PROPERTY(qint64 tagsTotal READ tagsTotal NOTIFY progressChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY runningChanged)
    Q_PROPERTY(bool background READ background NOTIFY runningChanged) // small automatic scan: no progress bar

public:
    enum Phase { Idle = 0, Walking = 1, Tagging = 2, Indexing = 3 };
    enum Mode { Full = 0, Subset = 1, Folders = 2 };

    struct Stats
    {
        qint64 folders = 0, files = 0, added = 0, changed = 0, removed = 0;
        qint64 tagged = 0, failed = 0;
        double walkSec = 0, tagSec = 0, indexSec = 0, totalSec = 0;
        bool cancelled = false;
        int mode = Full;
        QStringList missingRoots;
        QStringList scanned;    // Subset: the roots, Folders: the folders
        QStringList newFolders; // folders that appeared (top of each new subtree)
    };

    explicit Scanner(QObject *parent = nullptr);
    ~Scanner() override;

    // threads = 0: auto (fewer on spinning disks)
    Q_INVOKABLE void start(const QStringList &roots, int threads = 0);
    // Only these roots (all of them must be library roots); prunes only inside them.
    void startSubset(const QStringList &roots, int threads = 0);
    // Only these folders (inside `roots`): their files, gone and new subfolders.
    void startFolders(const QStringList &roots, const QStringList &folders);
    Q_INVOKABLE void cancel();

    // Synchronous run for the command line benchmark.
    Stats runBlocking(const QStringList &roots, int threads, bool verbose);

    bool running() const { return m_running; }
    int phase() const { return m_phase.load(); }
    qint64 foldersSeen() const { return m_folders.load(); }
    qint64 filesSeen() const { return m_files.load(); }
    qint64 tagsDone() const { return m_tagsDone.load(); }
    qint64 tagsTotal() const { return m_tagsTotal.load(); }
    QString summary() const { return m_summary; }
    bool background() const { return m_running && m_mode != Full; }
    const Stats &lastStats() const { return m_stats; }

    static int autoThreads(const QStringList &roots);

signals:
    void runningChanged();
    void progressChanged();
    void libraryChanged(); // throttled while scanning
    void finished();

private:
    void run(QStringList roots, int threads, bool verbose, int mode = Full, QStringList folders = {});
    void launch(const QStringList &roots, int threads, int mode, const QStringList &folders);
    void poll();

    std::thread m_thread;
    QTimer m_poll;
    QElapsedTimer m_sinceNotify;
    bool m_running = false;
    int m_mode = Full;
    QString m_summary;
    Stats m_stats;

    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_done{false};
    std::atomic<bool> m_dirty{false};
    std::atomic<int> m_phase{Idle};
    std::atomic<qint64> m_folders{0}, m_files{0}, m_tagsDone{0}, m_tagsTotal{0};
};
