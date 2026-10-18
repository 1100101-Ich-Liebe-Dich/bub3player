#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

class DbConnection;
class Scanner;

// Audio analysis for the whole library (see AudioAnalyzer).
//
// Two kinds of work:
//  - requests: the playing and the next track, analysed right away in their
//    own thread (needed for the waveform and for the next transition)
//  - background: the rest of the local library, one track after another,
//    throttled. "Sanft" uses about a third of one core and pauses while the
//    scanner runs or the computer is busy; "Zügig" uses half the cores.
//
// Results go to the `analysis` table, waveforms to ~/.cache/halcyon/waves
// (same files as before, so existing waveforms stay valid).
class AnalysisService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY settingsChanged)                   // 0 off, 1 gentle, 2 fast
    Q_PROPERTY(bool remoteEnabled READ remoteEnabled WRITE setRemoteEnabled NOTIFY settingsChanged)
    Q_PROPERTY(qint64 analysedCount READ analysedCount NOTIFY progressChanged)
    Q_PROPERTY(qint64 totalCount READ totalCount NOTIFY progressChanged)
    Q_PROPERTY(qint64 failedCount READ failedCount NOTIFY progressChanged)   // of analysedCount: could not be read
    Q_PROPERTY(QString status READ status NOTIFY progressChanged)
    Q_PROPERTY(double secondsPerTrack READ secondsPerTrack NOTIFY progressChanged)

public:
    struct Features
    {
        bool valid = false;
        int status = 1;
        qint64 lengthMs = 0, startMs = 0, endMs = 0, introMs = 0, outroMs = 0, beatMs = 0;
        double bpm = 0, bpmConf = 0, keyConf = 0, loudness = 0, peak = 0, lra = 0, energy = 0, brightness = 0, onsetRate = 0;
        int key = -1;
        bool minor = false;
        bool hasTempo() const { return valid && bpm > 0 && bpmConf >= 0.3; }
        bool hasKey() const { return valid && key >= 0 && keyConf >= 0.25; }
    };

    explicit AnalysisService(QObject *parent = nullptr);
    ~AnalysisService() override;
    static AnalysisService *instance();

    void setScanner(Scanner *scanner);

    int mode() const { return m_mode; }
    void setMode(int mode);
    bool remoteEnabled() const { return m_remote; }
    void setRemoteEnabled(bool on);
    qint64 analysedCount() const { return m_analysed; }
    qint64 totalCount() const { return m_total; }
    qint64 failedCount() const { return m_failed; }
    QString status() const { return m_status; }
    double secondsPerTrack() const { return m_secPerTrack; }

    // Main thread. Features from the database (small cache), invalid if the
    // track was not analysed yet or changed since.
    Features features(qint64 trackId);
    // Playing / next track: analyse now if needed. `wantWave`: emit
    // waveformReady (from the cache file if there is one).
    void request(qint64 trackId, bool wantWave);
    // Library changed (scan, Jellyfin): recount and continue the background pass.
    Q_INVOKABLE void libraryChanged();

    Q_INVOKABLE QString keyText(qint64 trackId); // "8A · a-Moll", "" if unknown

signals:
    void settingsChanged();
    void progressChanged();
    void analysed(qint64 trackId);
    void waveformReady(qint64 trackId, const QVariantList &values);

private:
    struct Job
    {
        qint64 id = 0;
        QByteArray uri;
        QString waveKey;
        qint64 mtime = 0;
        bool wantWave = false;
    };
    bool jobFor(qint64 trackId, Job &job);
    void priorityLoop();
    void backgroundLoop(int index);
    bool fetchBatch(DbConnection &db, bool &restarted);
    bool runJob(const Job &job, double duty, bool background);
    void startBackground();
    void stopBackground();
    void refreshCounts();
    void updateStatus();
    bool loadTooHigh() const;

    static AnalysisService *s_instance;
    int m_mode = 1;
    bool m_remote = false;
    qint64 m_analysed = 0, m_total = 0, m_failed = 0;
    double m_secPerTrack = 0;
    QString m_status;
    bool m_scanning = false;

    // main-thread feature cache
    QHash<qint64, Features> m_cache;
    QSet<qint64> m_wantWave;

    // priority work
    std::thread m_prio;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    QSet<qint64> m_queued;          // guarded by m_mutex
    std::atomic<bool> m_quit{false};

    // background work
    std::vector<std::thread> m_bg;
    std::atomic<bool> m_bgStop{false};
    std::atomic<bool> m_bgPaused{false};  // scanner running
    std::atomic<int> m_bgState{0};        // 0 idle, 1 working, 2 waiting (busy computer), 3 paused (scan), 4 pass done
    std::atomic<qint64> m_bgDone{0};      // analysed in this session (background)
    std::atomic<qint64> m_bgNanos{0};
    std::mutex m_bgMutex;                 // cursor / batch hand-out
    std::condition_variable m_bgCv;
    std::deque<Job> m_batch;              // next local tracks to analyse
    std::mutex m_fetchMutex;              // one worker queries the next batch at a time
    qint64 m_cursor = 0;                  // guarded by m_fetchMutex
    bool m_cursorLoaded = false;
    bool m_passDone = false;
    QTimer m_progressTimer;
};
