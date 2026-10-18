#pragma once

#include <QByteArray>
#include <QColor>
#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVector>

typedef struct _GstElement GstElement;
typedef struct _GstMessage GstMessage;
typedef struct _GstBus GstBus;

class PlayQueue;

// GStreamer playback (playbin): gapless via about-to-finish, ReplayGain via
// rgvolume + rglimiter (never clips), optional spectrum for the visualizer.
// Every optional element is probed first: a missing plugin disables a feature,
// it never breaks playback.
class Player : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state NOTIFY stateChanged)          // 0 stopped, 1 playing, 2 paused
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY volumeChanged)
    Q_PROPERTY(qint64 currentId READ currentId NOTIFY trackChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QString album READ album NOTIFY trackChanged)
    Q_PROPERTY(QString albumArtist READ albumArtist NOTIFY trackChanged)
    Q_PROPERTY(int year READ year NOTIFY trackChanged)
    Q_PROPERTY(QString format READ format NOTIFY trackChanged)
    Q_PROPERTY(QColor coverColor READ coverColor NOTIFY coverColorChanged)
    Q_PROPERTY(int replayGain READ replayGain WRITE setReplayGain NOTIFY settingsChanged) // 0 off, 1 track, 2 album
    Q_PROPERTY(double preamp READ preamp WRITE setPreamp NOTIFY settingsChanged)        // dB
    Q_PROPERTY(bool spectrumEnabled READ spectrumEnabled WRITE setSpectrumEnabled NOTIFY spectrumEnabledChanged)
    Q_PROPERTY(QVariantList spectrum READ spectrum NOTIFY spectrumChanged)
    Q_PROPERTY(bool spectrumAvailable READ spectrumAvailable CONSTANT)
    Q_PROPERTY(bool replayGainAvailable READ replayGainAvailable CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit Player(PlayQueue *queue, QObject *parent = nullptr);
    ~Player() override;

    int state() const { return m_state; }
    bool playing() const { return m_state == 1; }
    qint64 position() const { return m_position; }
    qint64 duration() const { return m_duration; }
    double volume() const { return m_volume; }
    void setVolume(double v);
    bool muted() const { return m_muted; }
    void setMuted(bool m);
    qint64 currentId() const { return m_playingId; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    QString albumArtist() const { return m_albumArtist; }
    int year() const { return m_year; }
    QString format() const { return m_format; }
    QColor coverColor() const { return m_coverColor; }
    int replayGain() const { return m_rgMode; }
    void setReplayGain(int mode);
    double preamp() const { return m_preamp; }
    void setPreamp(double db);
    bool spectrumEnabled() const { return m_spectrumEnabled; }
    void setSpectrumEnabled(bool on);
    QVariantList spectrum() const { return m_spectrumOut; }
    bool spectrumAvailable() const { return m_spectrumEl != nullptr; }
    bool replayGainAvailable() const { return m_rgEl != nullptr; }
    QString error() const { return m_error; }

    Q_INVOKABLE void playCurrent();
    Q_INVOKABLE void playRow(int queueRow);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void seekRelative(qint64 deltaMs) { seek(std::max<qint64>(0, m_position + deltaMs)); }

    void restoreSession();
    void saveSession();

signals:
    void stateChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void trackChanged();
    void coverColorChanged();
    void settingsChanged();
    void spectrumEnabledChanged();
    void spectrumChanged();
    void errorChanged();
    void seeked(qint64 ms);

public:
    // GStreamer callbacks (called from streaming threads).
    static void onAboutToFinish(GstElement *playbin, void *self);
    static int onBusSync(GstBus *bus, GstMessage *msg, void *self);

private:
    void handleMessage(GstMessage *msg);
    void handleSpectrum(const QVector<float> &mags);

    bool load(qint64 id, bool startPlaying, qint64 seekMs = 0);
    void setState(int s);
    void updateMeta(qint64 id);
    void armNext();
    void applyVolume();
    void applyReplayGain();
    GstElement *buildFilterBin(bool withRg, GstElement **rgOut, GstElement **spectrumOut);
    void selectFilterBin();
    void pollPosition();
    void finishTrack(bool skipped);
    QByteArray uriFor(qint64 id) const;

    PlayQueue *m_queue;
    GstElement *m_playbin = nullptr;
    GstElement *m_rgEl = nullptr;
    GstElement *m_spectrumEl = nullptr; // spectrum of the active chain
    GstElement *m_binRg = nullptr, *m_binPlain = nullptr;
    GstElement *m_spectrumRg = nullptr, *m_spectrumPlain = nullptr;

    QMutex m_mutex; // guards m_nextUri/m_nextId/m_expectAdvance (streaming thread)
    QByteArray m_nextUri;
    qint64 m_nextId = 0;
    bool m_expectAdvance = false;
    bool m_switching = false;

    QTimer m_posTimer;
    int m_state = 0;
    qint64 m_position = 0, m_duration = 0, m_pendingSeek = -1;
    qint64 m_listenedMs = 0, m_lastPollPos = 0;
    double m_volume = 0.8;
    bool m_muted = false;
    int m_rgMode = 2;
    double m_preamp = 0.0;
    bool m_spectrumEnabled = false;
    int m_consecutiveErrors = 0;

    qint64 m_playingId = 0;
    QString m_title, m_artist, m_album, m_albumArtist, m_format, m_error;
    int m_year = 0;
    QColor m_coverColor;

    QVector<float> m_bars;
    QVariantList m_spectrumOut;
};
