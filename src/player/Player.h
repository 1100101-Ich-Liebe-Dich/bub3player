#pragma once

#include <QByteArray>
#include <QColor>
#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVector>

#include "player/Waveform.h"

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
    Q_PROPERTY(QVariantList coverPalette READ coverPalette NOTIFY coverColorChanged)
    // Audio analysis for meters and audio-reactive effects (0..1, ~30 updates/s).
    Q_PROPERTY(bool levelsEnabled READ levelsEnabled WRITE setLevelsEnabled NOTIFY levelsEnabledChanged)
    Q_PROPERTY(bool levelsAvailable READ levelsAvailable CONSTANT)
    Q_PROPERTY(double levelL READ levelL NOTIFY levelsChanged)
    Q_PROPERTY(double levelR READ levelR NOTIFY levelsChanged)
    Q_PROPERTY(double peakL READ peakL NOTIFY levelsChanged)
    Q_PROPERTY(double peakR READ peakR NOTIFY levelsChanged)
    Q_PROPERTY(double energy READ energy NOTIFY levelsChanged)
    Q_PROPERTY(double beat READ beat NOTIFY levelsChanged)
    Q_PROPERTY(int analysisHz READ analysisHz WRITE setAnalysisHz NOTIFY analysisHzChanged) // spectrum/level update rate
    // Loudness envelope of the current track for the seek bar (Waveform::Buckets values 0..1, empty until ready).
    Q_PROPERTY(QVariantList waveform READ waveform NOTIFY waveformChanged)
    Q_PROPERTY(bool waveformEnabled READ waveformEnabled WRITE setWaveformEnabled NOTIFY waveformChanged)

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
    QVariantList coverPalette() const { return m_coverPalette; }
    bool levelsEnabled() const { return m_levelsEnabled; }
    void setLevelsEnabled(bool on);
    bool levelsAvailable() const { return m_levelRg || m_levelPlain; }
    double levelL() const { return m_levelL; }
    double levelR() const { return m_levelR; }
    double peakL() const { return m_peakL; }
    double peakR() const { return m_peakR; }
    double energy() const { return m_energy; }
    double beat() const { return m_beat; }
    int analysisHz() const { return m_analysisHz; }
    void setAnalysisHz(int hz);
    QVariantList waveform() const { return m_waveform; }
    bool waveformEnabled() const { return m_wave.enabled(); }
    void setWaveformEnabled(bool on);

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
    void levelsEnabledChanged();
    void levelsChanged();
    void beatHit();
    void analysisHzChanged();
    void waveformChanged();

public:
    // GStreamer callbacks (called from streaming threads).
    static void onAboutToFinish(GstElement *playbin, void *self);
    static int onBusSync(GstBus *bus, GstMessage *msg, void *self);

private:
    void handleMessage(GstMessage *msg);
    void handleSpectrum(const QVector<float> &mags);
    void handleLevels(const QVector<double> &rms, const QVector<double> &peak);
    void resetLevels();

    bool load(qint64 id, bool startPlaying, qint64 seekMs = 0);
    void setState(int s);
    void updateMeta(qint64 id);
    void armNext();
    void applyVolume();
    void applyReplayGain();
    GstElement *buildFilterBin(bool withRg, GstElement **rgOut, GstElement **spectrumOut, GstElement **levelOut);
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
    GstElement *m_levelEl = nullptr, *m_levelRg = nullptr, *m_levelPlain = nullptr;
    bool m_levelsEnabled = false;
    double m_levelL = 0, m_levelR = 0, m_peakL = 0, m_peakR = 0, m_energy = 0, m_beat = 0, m_avgEnergy = 0;
    int m_beatCooldown = 0;
    int m_analysisHz = 30;
    QVariantList m_coverPalette;

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

    Waveform m_wave;
    QVariantList m_waveform;
    void requestWaveform();

    QVector<float> m_bars;
    QVariantList m_spectrumOut;
};
