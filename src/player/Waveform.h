#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QThreadPool>
#include <QVariantList>
#include <QVector>
#include <atomic>

// Waveform (loudness envelope) of a track for the seek bar.
//
// The file is decoded in a separate, low-priority GStreamer pipeline that is
// completely independent of playback (decode to 8 kHz mono, peak per bucket).
// Results are cached on disk (~/.cache/halcyon/waves), so every track is
// analysed once. A failure only means: no waveform, the seek bar falls back.
class Waveform : public QObject
{
    Q_OBJECT
public:
    static constexpr int Buckets = 480;

    explicit Waveform(QObject *parent = nullptr);
    ~Waveform() override;

    // Asynchronous; emits ready(id, values) when done (values empty on failure).
    void request(qint64 id, const QString &path, qint64 size, qint64 mtime);
    void setEnabled(bool on) { m_enabled = on; }
    bool enabled() const { return m_enabled; }

    // Synchronous worker (any thread). Returns Buckets values 0..1, or empty.
    static QVector<float> analyse(const QString &path, const std::atomic<bool> &cancel);

signals:
    void ready(qint64 id, const QVariantList &values);

private:
    static QString cacheFile(const QString &path, qint64 size, qint64 mtime);
    static QVector<float> loadCache(const QString &file);
    static void saveCache(const QString &file, const QVector<float> &v);

    QThreadPool m_pool;
    QSet<qint64> m_pending;
    std::atomic<bool> m_cancel{false};
    bool m_enabled = true;
};
