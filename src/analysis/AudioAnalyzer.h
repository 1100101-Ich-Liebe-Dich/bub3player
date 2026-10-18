#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <atomic>

// One pass over a track, decoded at 22.05 kHz in a separate low-priority
// GStreamer pipeline (independent of playback). Everything the player needs
// for transitions and recommendations comes out of this single decode:
//
//  - waveform for the seek bar (480 values 0..1)
//  - loudness (EBU R128 / BS.1770: integrated, range, sample peak)
//  - first/last sound, quiet intro and fade-out (where a transition may start)
//  - tempo (BPM) with confidence and the first beat
//  - key (Krumhansl-Kessler profiles on a peak-picked chromagram)
//  - brightness, note onsets per second and an overall energy value
//
// Memory stays small: the audio is processed while it streams through, only
// a few short envelopes (100 per second at most) are kept.
namespace AudioAnalyzer {

constexpr int Version = 1;          // bump when results change: tracks are analysed again
constexpr int WaveBuckets = 480;

struct Result
{
    bool ok = false;
    bool timedOut = false;          // not finished in time (busy machine, slow network): try again later
    int status = 1;                 // 0 ok, 1 unreadable, 2 only partly analysed (very long file)
    qint64 lengthMs = 0;
    qint64 startMs = 0, endMs = 0;  // first / last sound
    qint64 introMs = 0, outroMs = 0;
    double bpm = 0, bpmConf = 0;
    qint64 beatMs = 0;
    int key = -1;                   // 0 = C ... 11 = B
    bool minor = false;
    double keyConf = 0;
    double loudness = 0;            // LUFS
    double peak = -100;             // dBFS
    double lra = 0;                 // LU
    double energy = 0;              // 0..1
    double brightness = 0;          // Hz
    double onsetRate = 0;           // per second
    QVector<float> waveform;        // WaveBuckets values, empty if not available
};

// Synchronous (worker threads only). `uri`: file:// or http(s):// URI.
// `maxSeconds`: audio beyond this is not analysed (status 2), keeps DJ mixes
// and audio books from blocking the queue. `duty`: share of one CPU core to
// use (the decoder thread rests in between), 1 = full speed.
Result analyse(const QByteArray &uri, const std::atomic<bool> &cancel, int maxSeconds = 1800, double duty = 1.0);

// Key as shown to people: Camelot code ("8A") and German name ("a-Moll").
QString camelot(int key, bool minor);
QString keyName(int key, bool minor);

} // namespace AudioAnalyzer
