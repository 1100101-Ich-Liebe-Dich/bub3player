#pragma once

#include <atomic>

#include <QByteArray>
#include <QColor>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVector>

typedef struct _GstElement GstElement;
typedef struct _GstMessage GstMessage;
typedef struct _GstBus GstBus;
typedef struct _GstControlSource GstControlSource;
typedef struct _GError GError;

class AudioOutput;
class PlayQueue;

// GStreamer playback on two decks (two playbins). One deck plays; the other
// prepares the next track for a transition and afterwards lets the previous
// track fade out. Without transitions everything runs on one deck, gapless
// via about-to-finish, exactly as before.
//
// Transitions (setting "Übergänge"):
//   0 gapless   - no fades, no gaps
//   1 crossfade - fixed equal-power crossfade of `fadeSeconds`
//   2 automix   - uses the audio analysis: skips silence, starts the blend
//                 where the outgoing track fades or ends, blends longer when
//                 the tempos match and on a beat, keeps a natural fade-in
// Consecutive tracks of the same album always stay gapless (if enabled).
//
// ReplayGain via rgvolume + rglimiter (never clips). Tracks without
// ReplayGain tags get their gain from the analysed loudness (else -6 dB).
// Every optional element is probed first: a missing plugin disables a
// feature, it never breaks playback.
//
// Output and equalizer come from AudioOutput. Each deck has one filter chain,
// built from the settings; ReplayGain on/off, equalizer on/off, another
// device or bit-perfect rebuild it (short stop, then on at the same spot).
// Without a chosen device and without equalizer the pipeline is exactly the
// one of 1.0: no sink set, no extra element.
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
    // Loudness envelope of the current track for the seek bar (480 values 0..1, empty until ready).
    Q_PROPERTY(QVariantList waveform READ waveform NOTIFY waveformChanged)
    Q_PROPERTY(bool waveformEnabled READ waveformEnabled WRITE setWaveformEnabled NOTIFY waveformChanged)
    // From the audio analysis of the current track (0 / "" until known).
    Q_PROPERTY(double bpm READ bpm NOTIFY trackInfoChanged)
    Q_PROPERTY(QString musicalKey READ musicalKey NOTIFY trackInfoChanged)  // "8A · a-Moll"
    Q_PROPERTY(QString formatInfo READ formatInfo NOTIFY trackInfoChanged)  // "FLAC 24/96 · 124 BPM · 8A" for big views
    // Transitions
    Q_PROPERTY(int transition READ transition WRITE setTransition NOTIFY settingsChanged)  // 0 gapless, 1 crossfade, 2 automix
    Q_PROPERTY(int fadeSeconds READ fadeSeconds WRITE setFadeSeconds NOTIFY settingsChanged)
    Q_PROPERTY(bool albumGapless READ albumGapless WRITE setAlbumGapless NOTIFY settingsChanged)
    Q_PROPERTY(bool skipSilence READ skipSilence WRITE setSkipSilence NOTIFY settingsChanged)
    Q_PROPERTY(bool loudnessMatch READ loudnessMatch WRITE setLoudnessMatch NOTIFY settingsChanged)
    Q_PROPERTY(bool blending READ blending NOTIFY blendingChanged)           // a transition is audible right now
    Q_PROPERTY(QString nextTransition READ nextTransition NOTIFY planChanged) // "Überblenden, 6 s" / "Lückenlos (Album)"
    // The chain runs bit-perfect right now: no volume, ReplayGain, equalizer or blends.
    Q_PROPERTY(bool bitPerfect READ bitPerfect NOTIFY settingsChanged)

public:
    Player(PlayQueue *queue, AudioOutput *output, QObject *parent = nullptr);
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
    bool spectrumAvailable() const { return m_spectrumAvailable; }
    bool replayGainAvailable() const { return m_rgAvailable; }
    QString error() const { return m_error; }
    QVariantList coverPalette() const { return m_coverPalette; }
    bool levelsEnabled() const { return m_levelsEnabled; }
    void setLevelsEnabled(bool on);
    bool levelsAvailable() const { return m_levelsAvailable; }
    double levelL() const { return m_levelL; }
    double levelR() const { return m_levelR; }
    double peakL() const { return m_peakL; }
    double peakR() const { return m_peakR; }
    double energy() const { return m_energy; }
    double beat() const { return m_beat; }
    int analysisHz() const { return m_analysisHz; }
    void setAnalysisHz(int hz);
    QVariantList waveform() const { return m_waveform; }
    bool waveformEnabled() const { return m_waveEnabled; }
    void setWaveformEnabled(bool on);
    double bpm() const { return m_bpm; }
    QString musicalKey() const { return m_key; }
    QString formatInfo() const;
    int transition() const { return m_transition; }
    void setTransition(int mode);
    int fadeSeconds() const { return m_fadeSec; }
    void setFadeSeconds(int s);
    bool albumGapless() const { return m_albumGapless; }
    void setAlbumGapless(bool on);
    bool skipSilence() const { return m_skipSilence; }
    void setSkipSilence(bool on);
    bool loudnessMatch() const { return m_loudnessMatch; }
    void setLoudnessMatch(bool on);
    bool blending() const { return m_tail >= 0; }
    QString nextTransition() const;
    bool bitPerfect() const { return m_bitPerfect; }

    // What reaches the chain and what leaves for the device, for the settings:
    // {input, output, exact} – "16 bit · 44,1 kHz · Stereo"; exact: unchanged samples.
    Q_INVOKABLE QVariantMap signalPath() const;

    // interruptIsSkip: the track playing now counts as skipped (not when a radio starts from it)
    Q_INVOKABLE void playCurrent(bool interruptIsSkip = true);
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
    void trackInfoChanged();
    void blendingChanged();
    void planChanged();
    // A track ended and counted: as a play, or as a skip (skipped early by the listener).
    // Recommendations learn from it.
    void trackFinished(qint64 id, qint64 listenedMs, qint64 durationMs, bool skipped);

public:
    struct Deck;
    // GStreamer callbacks (called from streaming threads).
    static void onAboutToFinish(GstElement *playbin, void *deck);
    static int onBusSync(GstBus *bus, GstMessage *msg, void *deck);

    struct Deck
    {
        Player *owner = nullptr;
        int index = 0;
        GstElement *playbin = nullptr;
        int baseFlags = 0;                                   // playbin flags of the normal chain
        bool customSink = false;                             // "audio-sink" was set by us
        // the filter chain (rebuilt when the settings change it)
        GstElement *bin = nullptr;
        GstElement *spectrum = nullptr, *level = nullptr, *fader = nullptr, *rg = nullptr;
        GstElement *eq = nullptr, *eqPre = nullptr;          // equalizer and its preamp (only while on)
        GstControlSource *ctl = nullptr;
        bool controlled = false;                             // fader follows a curve right now
        std::atomic<int> gen{0};                             // bumped on errors: later errors of the same stream are stale
        qint64 trackId = 0;
        // ReplayGain fallback for tracks without tags (dB), applied when a stream starts
        std::atomic<double> gainNow{-6.0}, gainNext{-6.0};
        std::atomic<bool> expectAdvance{false};             // gapless: next stream belongs to the next track
    };

private:
    // Planned transition from the current to the next track.
    struct Plan
    {
        bool valid = false;
        qint64 nextId = 0;
        bool gapless = true;
        QString reason;          // shown in the queue
        qint64 startMs = 0;      // position in the current track where the blend starts
        qint64 outMs = 0;        // fade-out length of the current track
        bool holdOut = false;    // the track fades by itself: hold, then drop at the end
        qint64 nextStartMs = 0;  // where the next track starts (leading silence skipped)
        qint64 inMs = 0;         // fade-in length of the next track (0 = starts at full level)
    };

    void handleMessage(int deck, GstMessage *msg);
    void handleSpectrum(const QVector<float> &mags);
    void handleLevels(const QVector<double> &rms, const QVector<double> &peak);
    void resetLevels();

    bool initDeck(Deck &d, int index);
    void freeDeck(Deck &d);
    void freeChain(Deck &d);
    GstElement *buildFilterBin(Deck &d);
    void configureDeck(Deck &d);   // chain, sink and flags from the settings (deck stopped)
    void updateMeters(Deck &d);    // only the playing deck posts spectrum/level
    enum class Resume { Keep, Play, None };
    // Stop, configure both decks, then (Keep) continue where we were, (Play)
    // the same but playing, (None) stay stopped – a load follows.
    void rebuildChains(Resume resume = Resume::Keep);
    bool retryOutput(Resume how);  // the listener starts something: try the chosen output again
    bool handleOutputError(GstMessage *msg, const GError *err);
    void applyEq();
    int transitionMode() const { return m_bitPerfect ? 0 : m_transition; }
    double unity() const { return m_bitPerfect && m_muted ? 0.0 : 1.0; } // fader at rest (bit-perfect mutes here)
    Deck &active() { return m_deck[m_active]; }
    Deck &other() { return m_deck[1 - m_active]; }

    // sameTrack: the playing track again after a rebuild (no new track for listeners)
    bool load(qint64 id, bool startPlaying, qint64 seekMs = 0, bool sameTrack = false);
    void setState(int s);
    void updateMeta(qint64 id);
    void updateTrackInfo();
    void armNext();
    void applyVolume();
    void applyReplayGain();
    void updatePosition();         // position + listening time
    void pollPosition();           // ... plus transitions (timer)
    void finishTrack(bool skipped);
    QByteArray uriFor(qint64 id) const;
    double fallbackGain(qint64 id);

    // transitions
    void plan();
    void prepareNext();
    void cancelPrepared();
    void startBlend();
    void stopTail();
    void setFader(Deck &d, double value);
    void setCurve(Deck &d, const QVector<QPair<qint64, double>> &points);
    bool canSeek(qint64 id) const;

    PlayQueue *m_queue;
    AudioOutput *m_out;
    Deck m_deck[2];
    bool m_rgAvailable = false, m_spectrumAvailable = false, m_levelsAvailable = false;
    bool m_bitPerfect = false;     // the chains were built bit-perfect
    bool m_volumeHinted = false;
    bool m_volumeAfterRebuild = false; // set the volume again once the new chain runs
    qint64 m_suspectId = 0;            // fell back for "no decoder": maybe the track, not the device
    std::atomic<int> m_active{0};
    int m_tail = -1;               // deck fading out, -1 none
    qint64 m_tailEndMs = 0;        // ... until this position
    int m_prepared = 0;            // other deck: 0 idle, 1 loading, 2 seeking, 3 ready
    qint64 m_preparedId = 0;
    Plan m_plan;
    bool m_curveOnActive = false;  // fade-out curve already installed on the playing deck
    qint64 m_fadeInEndMs = 0;      // the playing deck's fade-in curve ends here
    qint64 m_prepareFailedId = 0;  // could not be prepared: no blend into it

    bool m_levelsEnabled = false;
    double m_levelL = 0, m_levelR = 0, m_peakL = 0, m_peakR = 0, m_energy = 0, m_beat = 0, m_avgEnergy = 0;
    int m_beatCooldown = 0;
    int m_analysisHz = 30;
    QVariantList m_coverPalette;

    QMutex m_mutex; // guards m_nextUri / m_nextId (streaming thread)
    QByteArray m_nextUri;
    qint64 m_nextId = 0;
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
    bool m_wantPlay = false;       // the last load was meant to play (not a paused restore)
    bool m_handingOver = false;    // startBlend: queue already advanced, track info not yet

    int m_transition = 0;
    int m_fadeSec = 6;
    bool m_albumGapless = true;
    bool m_skipSilence = true;
    bool m_loudnessMatch = true;

    qint64 m_playingId = 0;
    QString m_title, m_artist, m_album, m_albumArtist, m_format, m_error, m_key;
    int m_year = 0;
    double m_bpm = 0;
    QColor m_coverColor;

    bool m_waveEnabled = true;
    QVariantList m_waveform;

    QVector<float> m_bars;
    QVariantList m_spectrumOut;
};
