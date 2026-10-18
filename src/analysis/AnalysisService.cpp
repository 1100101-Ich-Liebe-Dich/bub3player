#include "analysis/AnalysisService.h"

#include "analysis/AudioAnalyzer.h"
#include "core/Database.h"
#include "core/Paths.h"
#include "core/Scanner.h"
#include "jellyfin/JellyfinAccess.h"
#include "library/TrackCache.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSettings>
#include <QUrl>
#include <QtConcurrent>
#include <QDebug>
#include <chrono>
#include <cmath>

AnalysisService *AnalysisService::s_instance = nullptr;

namespace {

constexpr quint32 WaveMagic = 0x57415631; // "WAV1"
constexpr int WaveBuckets = AudioAnalyzer::WaveBuckets;
constexpr int BatchSize = 64;

// Same file names and format as the waveform cache of 0.4/0.5.
QString waveFile(const QString &key, qint64 mtime)
{
    const QByteArray k = key.toUtf8() + '|' + QByteArray::number(0) + '|' + QByteArray::number(mtime);
    const QString dir = Paths::cacheDir() + QStringLiteral("/waves");
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + QString::fromLatin1(QCryptographicHash::hash(k, QCryptographicHash::Sha1).toHex()) + QStringLiteral(".wav1");
}

QVariantList loadWave(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    if (data.size() != int(sizeof(quint32) + WaveBuckets))
        return {};
    quint32 magic;
    memcpy(&magic, data.constData(), sizeof magic);
    if (magic != WaveMagic)
        return {};
    QVariantList v;
    v.reserve(WaveBuckets);
    for (int i = 0; i < WaveBuckets; ++i)
        v.push_back(quint8(data[int(sizeof magic) + i]) / 255.0);
    return v;
}

void saveWave(const QString &file, const QVector<float> &v)
{
    QByteArray data;
    data.resize(int(sizeof(quint32)) + WaveBuckets);
    memcpy(data.data(), &WaveMagic, sizeof WaveMagic);
    for (int i = 0; i < WaveBuckets; ++i)
        data[int(sizeof(quint32)) + i] = char(quint8(std::lround(std::clamp(v.value(i), 0.f, 1.f) * 255.f)));
    QFile f(file + QStringLiteral(".tmp"));
    if (f.open(QIODevice::WriteOnly) && f.write(data) == data.size()) {
        f.close();
        QFile::remove(file);
        f.rename(file);
    }
}

QVariantList toList(const QVector<float> &v)
{
    QVariantList l;
    l.reserve(v.size());
    for (float x : v)
        l.push_back(double(x));
    return l;
}

const char *Columns = "a.version, a.mtime, t.mtime, a.status, a.length_ms, a.start_ms, a.end_ms, a.intro_ms, a.outro_ms, "
                      "a.bpm, a.bpm_conf, a.beat_ms, a.key, a.minor, a.key_conf, a.loudness, a.peak, a.lra, a.energy, "
                      "a.brightness, a.onset_rate";

void write(DbConnection &db, qint64 id, qint64 mtime, const AudioAnalyzer::Result &r)
{
    Stmt s(db, "INSERT OR REPLACE INTO analysis(track_id, version, mtime, status, analysed_at, length_ms, start_ms, end_ms, "
               "intro_ms, outro_ms, bpm, bpm_conf, beat_ms, key, minor, key_conf, loudness, peak, lra, energy, brightness, onset_rate) "
               "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    s.bind(1, id).bind(2, AudioAnalyzer::Version).bind(3, mtime).bind(4, r.ok ? r.status : 1)
        .bind(5, QDateTime::currentSecsSinceEpoch()).bind(6, r.lengthMs).bind(7, r.startMs).bind(8, r.endMs)
        .bind(9, r.introMs).bind(10, r.outroMs).bind(11, r.bpm).bind(12, r.bpmConf).bind(13, r.beatMs)
        .bind(14, r.key).bind(15, r.minor ? 1 : 0).bind(16, r.keyConf).bind(17, r.loudness).bind(18, r.peak)
        .bind(19, r.lra).bind(20, r.energy).bind(21, r.brightness).bind(22, r.onsetRate);
    s.exec();
}

} // namespace

AnalysisService::AnalysisService(QObject *parent) : QObject(parent)
{
    s_instance = this;
    QSettings s;
    m_mode = std::clamp(s.value(QStringLiteral("analysis/mode"), 1).toInt(), 0, 2);
    m_remote = s.value(QStringLiteral("analysis/remote"), false).toBool();
    m_prio = std::thread([this] { priorityLoop(); });

    m_progressTimer.setInterval(2000);
    connect(&m_progressTimer, &QTimer::timeout, this, &AnalysisService::updateStatus);
    m_progressTimer.start();
    // Let the start-up (scan, first page of covers) go first.
    QTimer::singleShot(15000, this, [this] {
        refreshCounts();
        startBackground();
    });
    updateStatus();
}

AnalysisService::~AnalysisService()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_quit = true;
    }
    stopBackground();
    m_cv.notify_all();
    if (m_prio.joinable())
        m_prio.join();
    s_instance = nullptr;
}

AnalysisService *AnalysisService::instance()
{
    return s_instance;
}

void AnalysisService::setScanner(Scanner *scanner)
{
    connect(scanner, &Scanner::runningChanged, this, [this, scanner] {
        m_scanning = scanner->running();
        m_bgPaused = m_scanning;
        m_bgCv.notify_all();
        updateStatus();
    });
    connect(scanner, &Scanner::finished, this, &AnalysisService::libraryChanged);
}

void AnalysisService::setMode(int mode)
{
    mode = std::clamp(mode, 0, 2);
    if (mode == m_mode)
        return;
    m_mode = mode;
    QSettings().setValue(QStringLiteral("analysis/mode"), mode);
    stopBackground();
    startBackground();
    emit settingsChanged();
    updateStatus();
}

void AnalysisService::setRemoteEnabled(bool on)
{
    if (on == m_remote)
        return;
    m_remote = on;
    QSettings().setValue(QStringLiteral("analysis/remote"), on);
    emit settingsChanged();
}

// ---------------------------------------------------------------- features

AnalysisService::Features AnalysisService::features(qint64 trackId)
{
    if (trackId <= 0)
        return {};
    auto it = m_cache.constFind(trackId);
    if (it != m_cache.constEnd())
        return *it;
    Features f;
    try {
        Stmt s(TrackCache::instance().db(),
               QStringLiteral("SELECT %1 FROM analysis a JOIN tracks t ON t.id = a.track_id WHERE a.track_id = ?").arg(QLatin1String(Columns)));
        s.bind(1, trackId);
        if (s.step() && s.i32(0) == AudioAnalyzer::Version && s.i64(1) == s.i64(2) && s.i32(3) != 1) {
            f.valid = true;
            f.status = s.i32(3);
            f.lengthMs = s.i64(4);
            f.startMs = s.i64(5);
            f.endMs = s.i64(6);
            f.introMs = s.i64(7);
            f.outroMs = s.i64(8);
            f.bpm = s.dbl(9);
            f.bpmConf = s.dbl(10);
            f.beatMs = s.i64(11);
            f.key = s.i32(12);
            f.minor = s.i32(13) != 0;
            f.keyConf = s.dbl(14);
            f.loudness = s.dbl(15);
            f.peak = s.dbl(16);
            f.lra = s.dbl(17);
            f.energy = s.dbl(18);
            f.brightness = s.dbl(19);
            f.onsetRate = s.dbl(20);
        }
    } catch (const std::exception &e) {
        qWarning() << "Analyse:" << e.what();
    }
    if (m_cache.size() > 4000)
        m_cache.clear();
    m_cache.insert(trackId, f);
    return f;
}

QString AnalysisService::keyText(qint64 trackId)
{
    const Features f = features(trackId);
    if (!f.hasKey())
        return {};
    return AudioAnalyzer::camelot(f.key, f.minor) + QStringLiteral(" · ") + AudioAnalyzer::keyName(f.key, f.minor);
}

bool AnalysisService::jobFor(qint64 trackId, Job &job)
{
    const TrackRow r = TrackCache::instance().get(trackId);
    if (!r.valid)
        return false;
    job.id = trackId;
    job.mtime = r.mtime;
    if (r.isRemote()) {
        if (!m_remote)
            return false;
        job.uri = JellyfinAccess::streamUrl(r.sourceId, r.remoteId);
        job.waveKey = QStringLiteral("jellyfin:%1/%2").arg(r.sourceId).arg(r.remoteId);
    } else {
        const QString path = TrackCache::instance().pathOf(trackId);
        if (path.isEmpty())
            return false;
        job.uri = QUrl::fromLocalFile(path).toEncoded();
        job.waveKey = path;
    }
    return !job.uri.isEmpty();
}

void AnalysisService::request(qint64 trackId, bool wantWave)
{
    Job job;
    if (!jobFor(trackId, job))
        return;
    bool haveWave = false;
    if (wantWave) {
        const QVariantList wave = loadWave(waveFile(job.waveKey, job.mtime));
        if (!wave.isEmpty()) {
            haveWave = true;
            QPointer<AnalysisService> self(this);
            QMetaObject::invokeMethod(this, [self, trackId, wave] {
                if (self)
                    emit self->waveformReady(trackId, wave);
            }, Qt::QueuedConnection);
        }
    }
    if (features(trackId).valid && (haveWave || !wantWave))
        return;
    job.wantWave = wantWave && !haveWave;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queued.contains(trackId))
            return;
        m_queued.insert(trackId);
        m_jobs.push_front(job); // the newest request (usually the playing track) first
        while (m_jobs.size() > 6) {
            m_queued.remove(m_jobs.back().id);
            m_jobs.pop_back();
        }
    }
    m_cv.notify_one();
}

// ---------------------------------------------------------------- workers

bool AnalysisService::runJob(const Job &job, double duty, bool background)
{
    const std::atomic<bool> &cancel = background ? m_bgStop : m_quit;
    const auto t0 = std::chrono::steady_clock::now();
    const AudioAnalyzer::Result r = AudioAnalyzer::analyse(job.uri, cancel, 1800, duty);
    if (cancel)
        return false;
    const bool remote = job.waveKey.startsWith(QLatin1String("jellyfin:"));
    if (!r.ok && (remote || r.timedOut))
        return false; // offline or too slow right now: try again another time
    try {
        thread_local DbConnection db;
        if (!db.isOpen())
            db = Database::open(1024);
        write(db, job.id, job.mtime, r);
    } catch (const std::exception &e) {
        qWarning() << "Analyse nicht gespeichert:" << e.what();
        return false;
    }
    if (!r.waveform.isEmpty())
        saveWave(waveFile(job.waveKey, job.mtime), r.waveform);
    if (background) {
        m_bgDone += 1;
        m_bgNanos += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    }
    const qint64 id = job.id;
    const QVariantList wave = toList(r.waveform);
    const bool ok = r.ok;
    QPointer<AnalysisService> self(this);
    QMetaObject::invokeMethod(qApp, [self, id, wave, ok, background] {
        if (!self)
            return;
        self->m_cache.remove(id);
        if (background) {
            ++self->m_analysed;
            if (!ok)
                ++self->m_failed;
        }
        emit self->analysed(id);
        if (!wave.isEmpty())
            emit self->waveformReady(id, wave);
    }, Qt::QueuedConnection);
    return true;
}

void AnalysisService::priorityLoop()
{
    while (!m_quit) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_quit || !m_jobs.empty(); });
            if (m_quit)
                return;
            job = m_jobs.front();
            m_jobs.pop_front();
        }
        runJob(job, 1.0, false);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queued.remove(job.id);
    }
}

bool AnalysisService::loadTooHigh() const
{
    QFile f(QStringLiteral("/proc/loadavg"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const double load = QString::fromLatin1(f.readLine()).section(QLatin1Char(' '), 0, 0).toDouble();
    const double cores = std::max(1u, std::thread::hardware_concurrency());
    // "Sanft" steps back as soon as others need the machine; "Zügig" only when it is overloaded.
    return load > cores * (m_mode == 2 ? 1.5 : 0.85);
}

void AnalysisService::startBackground()
{
    if (m_mode == 0 || !m_bg.empty())
        return;
    m_bgStop = false;
    m_bgPaused = m_scanning;
    const int threads = m_mode == 2 ? std::max(1, int(std::thread::hardware_concurrency()) / 2) : 1;
    for (int i = 0; i < threads; ++i)
        m_bg.emplace_back([this, i] { backgroundLoop(i); });
}

void AnalysisService::stopBackground()
{
    {
        // under the lock: a worker between its check and its wait would miss the wake-up
        std::lock_guard<std::mutex> lock(m_bgMutex);
        m_bgStop = true;
    }
    m_bgCv.notify_all();
    for (std::thread &t : m_bg)
        if (t.joinable())
            t.join();
    m_bg.clear();
    std::lock_guard<std::mutex> lock(m_bgMutex);
    m_batch.clear();
    m_bgState = 0;
}

// Next tracks in id order without a (current) analysis. Database work happens
// outside m_bgMutex (the UI thread takes that one); m_fetchMutex keeps two
// workers from fetching the same batch.
bool AnalysisService::fetchBatch(DbConnection &db, bool &restarted)
{
    std::lock_guard<std::mutex> fetch(m_fetchMutex);
    {
        std::lock_guard<std::mutex> lock(m_bgMutex);
        if (!m_batch.empty())
            return true;
        if (m_passDone)
            return false;
    }
    if (!m_cursorLoaded) {
        m_cursor = Database::meta(db, "analysis_cursor").toLongLong();
        m_cursorLoaded = true;
    }
    std::deque<Job> jobs;
    for (int round = 0; round < 2 && jobs.empty(); ++round) {
        try {
            Stmt q(db, "SELECT t.id, f.path, t.filename, t.mtime FROM tracks t JOIN folders f ON f.id = t.folder_id "
                       "LEFT JOIN analysis a ON a.track_id = t.id "
                       "WHERE t.id > ? AND t.source_id = 1 AND t.tags_read = 1 "
                       "AND (a.track_id IS NULL OR a.version <> ? OR a.mtime <> t.mtime) ORDER BY t.id LIMIT ?");
            q.bind(1, m_cursor).bind(2, AudioAnalyzer::Version).bind(3, BatchSize);
            while (q.step()) {
                Job j;
                j.id = q.i64(0);
                j.waveKey = q.str(1) + QLatin1Char('/') + q.str(2);
                j.uri = QUrl::fromLocalFile(j.waveKey).toEncoded();
                j.mtime = q.i64(3);
                jobs.push_back(j);
                m_cursor = j.id;
            }
            Database::setMeta(db, "analysis_cursor", QString::number(m_cursor));
        } catch (const std::exception &e) {
            qWarning() << "Analyse:" << e.what();
            break;
        }
        if (jobs.empty() && m_cursor > 0 && !restarted) {
            // Once more from the start: tracks whose tags were not read yet last time.
            m_cursor = 0;
            restarted = true;
        } else {
            break;
        }
    }
    std::lock_guard<std::mutex> lock(m_bgMutex);
    if (jobs.empty()) {
        m_passDone = true;
        return false;
    }
    for (Job &j : jobs)
        m_batch.push_back(std::move(j));
    return true;
}

void AnalysisService::backgroundLoop(int)
{
    DbConnection db;
    try {
        db = Database::open(2048);
    } catch (const std::exception &e) {
        qWarning() << "Analyse:" << e.what();
        return;
    }
    const double duty = m_mode == 2 ? 1.0 : 0.35;
    auto waitFor = [this](int seconds) {
        std::unique_lock<std::mutex> lock(m_bgMutex);
        m_bgCv.wait_for(lock, std::chrono::seconds(seconds), [this] { return m_bgStop.load(); });
    };
    bool restarted = false;
    while (!m_bgStop) {
        if (m_bgPaused) {
            m_bgState = 3;
            waitFor(2);
            continue;
        }
        if (loadTooHigh()) {
            m_bgState = 2;
            waitFor(15);
            continue;
        }
        if (!fetchBatch(db, restarted)) {
            // All done: wait until the library changes (or we stop).
            std::unique_lock<std::mutex> lock(m_bgMutex);
            m_bgState = 4;
            m_bgCv.wait_for(lock, std::chrono::seconds(60), [this] { return m_bgStop || !m_passDone; });
            restarted = false;
            continue;
        }
        Job job;
        {
            std::lock_guard<std::mutex> lock(m_bgMutex);
            if (m_batch.empty())
                continue;
            job = m_batch.front();
            m_batch.pop_front();
        }
        // The playing track always goes first.
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_queued.contains(job.id))
                continue;
        }
        m_bgState = 1;
        runJob(job, duty, true);
    }
}

// ---------------------------------------------------------------- progress

void AnalysisService::libraryChanged()
{
    {
        std::lock_guard<std::mutex> lock(m_bgMutex);
        m_passDone = false;
    }
    m_bgCv.notify_all();
    refreshCounts();
}

void AnalysisService::refreshCounts()
{
    QPointer<AnalysisService> self(this);
    (void)QtConcurrent::run([self] {
        qint64 done = 0, total = 0, failed = 0;
        try {
            DbConnection db = Database::open(4096);
            total = db.scalar("SELECT COUNT(*) FROM tracks WHERE source_id = 1 AND tags_read = 1");
            Stmt s(db, "SELECT COUNT(*), COALESCE(SUM(a.status = 1), 0) FROM analysis a JOIN tracks t ON t.id = a.track_id "
                       "WHERE t.source_id = 1 AND t.tags_read = 1 AND a.version = ? AND a.mtime = t.mtime");
            s.bind(1, AudioAnalyzer::Version);
            if (s.step()) {
                done = s.i64(0);
                failed = s.i64(1);
            }
        } catch (const std::exception &e) {
            qWarning() << "Analyse:" << e.what();
            return;
        }
        QMetaObject::invokeMethod(qApp, [self, done, total, failed] {
            if (!self)
                return;
            self->m_analysed = done;
            self->m_total = total;
            self->m_failed = failed;
            self->updateStatus();
        }, Qt::QueuedConnection);
    });
}

void AnalysisService::updateStatus()
{
    QString s;
    const qint64 left = std::max<qint64>(0, m_total - m_analysed);
    if (m_mode == 0)
        s = tr("Nur gespielte Titel werden analysiert.");
    else if (m_scanning)
        s = tr("Pausiert, solange die Bibliothek gescannt wird.");
    else if (m_bgState == 2)
        s = tr("Wartet: der Rechner hat gerade viel zu tun.");
    else if ((m_total > 0 && left == 0) || m_bgState == 4)
        s = tr("Fertig: alle Titel sind analysiert.");
    else if (m_bgState == 1)
        s = tr("Läuft im Hintergrund.");
    else
        s = tr("Wartet auf den Start.");
    const qint64 done = m_bgDone.load();
    const double spt = done > 0 ? m_bgNanos.load() / 1e9 / done : 0.0;
    if (s != m_status || std::fabs(spt - m_secPerTrack) > 0.01) {
        m_status = s;
        m_secPerTrack = spt;
    }
    emit progressChanged();
}
