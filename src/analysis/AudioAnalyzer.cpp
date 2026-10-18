#include "analysis/AudioAnalyzer.h"

#include "analysis/Fft.h"

#include <QElapsedTimer>
#include <QtGlobal>
#include <QString>
#include <algorithm>
#include <array>
#include <chrono>
#include <thread>
#include <memory>
#include <cmath>
#include <gst/gst.h>
#include <numeric>
#include <vector>

namespace AudioAnalyzer {

namespace {

// The decoder delivers the file's own rate; we average it down by 2, 4 or 8
// to about 22-24 kHz (cheaper than a resampler, and folding the top octave
// back keeps its energy for the loudness measurement).
constexpr int OnsetFft = 1024;
constexpr int OnsetHop = 512;                // ~43 frames per second
constexpr int ChromaFft = 8192;
constexpr int ChromaHop = 8192;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    inline double run(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// BS.1770 K-weighting for any sample rate (same derivation as libebur128).
void kWeighting(Biquad &shelf, Biquad &hp, double rate)
{
    double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    double K = std::tan(M_PI * f0 / rate);
    const double Vh = std::pow(10.0, G / 20.0);
    const double Vb = std::pow(Vh, 0.4996667741545416);
    double a0 = 1.0 + K / Q + K * K;
    shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
    shelf.b1 = 2.0 * (K * K - Vh) / a0;
    shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
    shelf.a1 = 2.0 * (K * K - 1.0) / a0;
    shelf.a2 = (1.0 - K / Q + K * K) / a0;

    f0 = 38.13547087602444;
    Q = 0.5003270373238773;
    K = std::tan(M_PI * f0 / rate);
    a0 = 1.0 + K / Q + K * K;
    hp.b0 = 1.0;
    hp.b1 = -2.0;
    hp.b2 = 1.0;
    hp.a1 = 2.0 * (K * K - 1.0) / a0;
    hp.a2 = (1.0 - K / Q + K * K) / a0;
}

double toLufs(double meanSquareSum)
{
    return meanSquareSum > 0 ? -0.691 + 10.0 * std::log10(meanSquareSum) : -200.0;
}

// Everything that happens while the audio streams through (streaming thread).
struct State
{
    State(int nativeRate, qint64 maxSeconds) : onsetFft(OnsetFft), chromaFft(ChromaFft)
    {
        decim = 1;
        while (nativeRate / (decim * 2) >= 22000)
            decim *= 2;
        rate = nativeRate / decim;
        blockSamples = rate / 10;
        fps = double(rate) / OnsetHop;
        maxFrames = maxSeconds * rate;
        for (int c = 0; c < 2; ++c)
            kWeighting(shelf[c], hp[c], rate);
        mono.reserve(ChromaFft * 4);
        blocks.reserve(6000);
        flux.reserve(30000);
        peaks10ms.reserve(30000);
        prevMag.assign(OnsetFft / 2 + 1, 0.f);
        prevLow.assign(OnsetFft / 2 + 1, 0.f);
        chroma.fill(0.0);
    }

    int rate = 22050, decim = 1, blockSamples = 2205;
    double fps = 86.0;
    // decimation
    double accL = 0, accR = 0;
    int accN = 0;

    // loudness
    Biquad shelf[2], hp[2];
    double blockAcc = 0;   // sum over channels of squared K-weighted samples
    int blockN = 0;
    std::vector<float> blocks; // mean square (channel sum) per 100 ms
    double peak = 0;

    // waveform (peak per 10 ms of the mono signal)
    std::vector<float> peaks10ms;
    float curPeak = 0;
    double peakPos = 0;
    qint64 frames = 0;

    // spectra
    std::vector<float> mono;
    size_t onsetPos = 0, chromaPos = 0;   // next frame start inside `mono`
    RealFft onsetFft, chromaFft;
    std::vector<float> mag, prevMag;
    std::vector<float> flux;              // spectral flux per onset frame
    std::vector<float> fluxLow, prevLow;  // the same for the bass range
    double centroidAcc = 0, centroidW = 0;
    std::array<double, 12> chroma;
    int chromaFrames = 0;

    qint64 maxFrames = 0;
    std::atomic<bool> full{false};        // reached maxFrames

    void push(const float *inter, int n)
    {
        qint64 produced = 0;
        for (int i = 0; i < n; ++i) {
            float l = inter[2 * i], r = inter[2 * i + 1];
            if (decim > 1) {
                accL += l;
                accR += r;
                if (++accN < decim)
                    continue;
                l = float(accL / decim);
                r = float(accR / decim);
                accL = accR = 0;
                accN = 0;
            }
            ++produced;
            const double kl = hp[0].run(shelf[0].run(l));
            const double kr = hp[1].run(shelf[1].run(r));
            blockAcc += kl * kl + kr * kr;
            if (++blockN == blockSamples) {
                blocks.push_back(float(blockAcc / blockSamples));
                blockAcc = 0;
                blockN = 0;
            }
            const float m = 0.5f * (l + r);
            const float a = std::max(std::fabs(l), std::fabs(r));
            if (a > peak)
                peak = a;
            const float am = std::fabs(m);
            if (am > curPeak)
                curPeak = am;
            peakPos += 1.0;
            if (peakPos >= rate / 100.0) {
                peakPos -= rate / 100.0;
                peaks10ms.push_back(curPeak);
                curPeak = 0;
            }
            mono.push_back(m);
        }
        frames += produced;
        spectra();
        if (maxFrames > 0 && frames >= maxFrames)
            full = true;
    }

    void spectra()
    {
        while (onsetPos + OnsetFft <= mono.size()) {
            onsetFft.magnitudes(mono.data() + onsetPos, mag);
            // Spectral flux on compressed magnitudes, 30 Hz .. 8 kHz.
            const int lo = 2, hi = std::min(int(mag.size()) - 1, int(8000.0 * OnsetFft / rate));
            double f = 0, cw = 0, cs = 0;
            for (int k = lo; k <= hi; ++k) {
                const float v = std::log1p(100.f * mag[size_t(k)] / (OnsetFft / 4));
                const float d = v - prevMag[size_t(k)];
                if (d > 0)
                    f += d;
                prevMag[size_t(k)] = v;
                cw += mag[size_t(k)];
                cs += mag[size_t(k)] * (double(k) * rate / OnsetFft);
            }
            flux.push_back(float(f));
            // Bass only (up to ~150 Hz): kick drums mark the beat itself, hi-hats the off-beat.
            double fl = 0;
            const int lowHi = std::max(lo + 1, int(150.0 * OnsetFft / rate));
            for (int k = lo; k <= lowHi; ++k) {
                const float v = std::log1p(100.f * mag[size_t(k)] / (OnsetFft / 4));
                const float d = v - prevLow[size_t(k)];
                if (d > 0)
                    fl += d;
                prevLow[size_t(k)] = v;
            }
            fluxLow.push_back(float(fl));
            if (cw > 1e-3) {
                centroidAcc += cs;
                centroidW += cw;
            }
            onsetPos += OnsetHop;
        }
        while (chromaPos + ChromaFft <= mono.size()) {
            chromaFrame(mono.data() + chromaPos);
            chromaPos += ChromaHop;
        }
        // Drop what both analyses are done with.
        const size_t used = std::min(onsetPos, chromaPos);
        if (used > size_t(ChromaFft) * 2) {
            mono.erase(mono.begin(), mono.begin() + std::ptrdiff_t(used));
            onsetPos -= used;
            chromaPos -= used;
        }
    }

    void chromaFrame(const float *x)
    {
        double e = 0;
        for (int i = 0; i < ChromaFft; i += 4)
            e += double(x[i]) * x[i];
        if (e / (ChromaFft / 4) < 1e-6) // below about -60 dBFS: no harmony to read
            return;
        chromaFft.magnitudes(x, mag);
        const int lo = std::max(1, int(55.0 * ChromaFft / rate)), hi = std::min(ChromaFft / 2 - 1, int(2100.0 * ChromaFft / rate));
        double mean = 0;
        for (int k = lo; k <= hi; ++k)
            mean += mag[size_t(k)];
        mean /= (hi - lo + 1);
        std::array<double, 12> frame{};
        for (int k = lo + 1; k < hi; ++k) {
            const float c = mag[size_t(k)];
            // Spectral peaks only: tones, not drums and noise.
            if (c <= mag[size_t(k - 1)] || c < mag[size_t(k + 1)] || c < mean * 3.0)
                continue;
            // Parabolic interpolation of the peak frequency.
            const double a = mag[size_t(k - 1)], b = c, g = mag[size_t(k + 1)];
            const double den = a - 2 * b + g;
            const double off = std::fabs(den) > 1e-12 ? 0.5 * (a - g) / den : 0.0;
            const double freq = (k + off) * rate / ChromaFft;
            const double midi = 69.0 + 12.0 * std::log2(freq / 440.0);
            const int pc = ((int(std::lround(midi)) % 12) + 12) % 12;
            frame[size_t(pc)] += std::sqrt(c);
        }
        const double mx = *std::max_element(frame.begin(), frame.end());
        if (mx <= 0)
            return;
        for (int i = 0; i < 12; ++i)
            chroma[size_t(i)] += frame[size_t(i)] / mx;
        ++chromaFrames;
    }
};

struct Holder
{
    std::unique_ptr<State> st;   // created and filled by the streaming thread; read after the pipeline stopped
    std::atomic<bool> full{false};
    qint64 maxSeconds = 1800;
    // Throttling: share of one core this analysis may use (1 = as fast as possible).
    double duty = 1.0;
    std::chrono::steady_clock::time_point last{};
    std::chrono::nanoseconds active{0};
    const std::atomic<bool> *cancel = nullptr;

    void throttle()
    {
        if (duty >= 0.999)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (last.time_since_epoch().count() != 0)
            active += now - last;
        last = now;
        if (active < std::chrono::milliseconds(25))
            return;
        // Rest in short slices so cancelling stays quick.
        auto rest = std::chrono::duration_cast<std::chrono::nanoseconds>(active * (1.0 / duty - 1.0));
        active = std::chrono::nanoseconds(0);
        while (rest.count() > 0 && !(cancel && *cancel)) {
            const auto slice = std::min<std::chrono::nanoseconds>(rest, std::chrono::milliseconds(50));
            std::this_thread::sleep_for(slice);
            rest -= slice;
        }
        last = std::chrono::steady_clock::now();
    }
};

GstPadProbeReturn onBuffer(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
    auto *h = static_cast<Holder *>(data);
    if (!h->st) {
        int nativeRate = 0;
        if (GstCaps *caps = gst_pad_get_current_caps(pad)) {
            gst_structure_get_int(gst_caps_get_structure(caps, 0), "rate", &nativeRate);
            gst_caps_unref(caps);
        }
        if (nativeRate < 4000)
            return GST_PAD_PROBE_DROP;
        h->st = std::make_unique<State>(nativeRate, h->maxSeconds);
    }
    State *st = h->st.get();
    if (st->full) {
        h->full = true;
        return GST_PAD_PROBE_DROP;
    }
    h->throttle();
    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    GstMapInfo map;
    if (buf && gst_buffer_map(buf, &map, GST_MAP_READ)) {
        st->push(reinterpret_cast<const float *>(map.data), int(map.size / (2 * sizeof(float))));
        gst_buffer_unmap(buf, &map);
    }
    return GST_PAD_PROBE_OK;
}

void onPadAdded(GstElement *, GstPad *pad, gpointer sinkElement)
{
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps)
        caps = gst_pad_query_caps(pad, nullptr);
    const GstStructure *s = caps ? gst_caps_get_structure(caps, 0) : nullptr;
    const bool audio = s && g_str_has_prefix(gst_structure_get_name(s), "audio/");
    if (caps)
        gst_caps_unref(caps);
    if (!audio)
        return;
    GstPad *sinkPad = gst_element_get_static_pad(GST_ELEMENT(sinkElement), "sink");
    if (!gst_pad_is_linked(sinkPad)) {
        gst_pad_link(pad, sinkPad);
    } else if (GstElement *bin = GST_ELEMENT(gst_element_get_parent(GST_ELEMENT(sinkElement)))) {
        // A second audio stream (rare): the first one is analysed, this one is
        // just drained, an unlinked pad would stop the whole pipeline.
        GstElement *drain = gst_element_factory_make("fakesink", nullptr);
        if (drain) {
            g_object_set(drain, "sync", FALSE, "async", FALSE, nullptr);
            gst_bin_add(GST_BIN(bin), drain);
            gst_element_sync_state_with_parent(drain);
            GstPad *dp = gst_element_get_static_pad(drain, "sink");
            gst_pad_link(pad, dp);
            gst_object_unref(dp);
        }
        gst_object_unref(bin);
    }
    gst_object_unref(sinkPad);
}

// ---------------------------------------------------------------- post-processing

void loudness(const State &st, Result &r)
{
    const std::vector<float> &b = st.blocks;
    const size_t n = b.size();
    if (n < 4) {
        r.loudness = -70;
        return;
    }
    // 400 ms gating blocks, 75 % overlap.
    std::vector<double> z;
    z.reserve(n);
    for (size_t i = 0; i + 4 <= n; ++i)
        z.push_back((double(b[i]) + b[i + 1] + b[i + 2] + b[i + 3]) / 4.0);
    double sum = 0;
    int cnt = 0;
    for (double v : z)
        if (toLufs(v) > -70.0) {
            sum += v;
            ++cnt;
        }
    if (cnt == 0) {
        r.loudness = -70;
        return;
    }
    const double rel = toLufs(sum / cnt) - 10.0;
    sum = 0;
    cnt = 0;
    for (double v : z)
        if (toLufs(v) > -70.0 && toLufs(v) > rel) {
            sum += v;
            ++cnt;
        }
    r.loudness = cnt ? toLufs(sum / cnt) : -70.0;

    // Loudness range: 3 s short-term windows, gates at -70 LUFS and -20 LU.
    std::vector<double> st3;
    double s3 = 0, s3n = 0;
    for (size_t i = 0; i + 30 <= n; i += 1) {
        double m = 0;
        for (size_t k = i; k < i + 30; ++k)
            m += b[k];
        m /= 30.0;
        if (toLufs(m) > -70.0) {
            st3.push_back(m);
            s3 += m;
            ++s3n;
        }
        if (n > 3000) // long tracks: one window per second is plenty
            i += 9;
    }
    if (s3n > 0) {
        const double gate = toLufs(s3 / s3n) - 20.0;
        std::vector<double> l;
        for (double m : st3)
            if (toLufs(m) > gate)
                l.push_back(toLufs(m));
        if (l.size() >= 2) {
            std::sort(l.begin(), l.end());
            r.lra = l[size_t(0.95 * (l.size() - 1))] - l[size_t(0.10 * (l.size() - 1))];
        }
    }
}

void edges(const State &st, Result &r)
{
    const std::vector<float> &b = st.blocks;
    const int n = int(b.size());
    if (n == 0)
        return;
    const double thr = std::max(-60.0, r.loudness - 35.0);
    int first = -1, last = -1;
    for (int i = 0; i < n; ++i)
        if (toLufs(b[size_t(i)]) > thr) {
            first = i;
            break;
        }
    for (int i = n - 1; i >= 0; --i)
        if (toLufs(b[size_t(i)]) > thr) {
            last = i;
            break;
        }
    if (first < 0) { // silent file
        r.startMs = 0;
        r.endMs = r.lengthMs;
        r.introMs = 0;
        r.outroMs = r.lengthMs;
        return;
    }
    r.startMs = qint64(first) * 100;
    r.endMs = std::min(r.lengthMs, qint64(last + 1) * 100);

    // 1 s windows against the track's own loudness: where does the body start and end?
    const double body = r.loudness - 6.0;
    auto win = [&](int i) {
        double m = 0;
        int k = 0;
        for (int j = i; j < std::min(n, i + 10); ++j, ++k)
            m += b[size_t(j)];
        return k ? toLufs(m / k) : -200.0;
    };
    int in = first, out = last;
    for (int i = first; i <= last; ++i)
        if (win(i) >= body) {
            in = i;
            break;
        }
    for (int i = std::max(first, last - 9); i >= first; --i)
        if (win(i) >= body) {
            out = std::min(last, i + 9);
            break;
        }
    r.introMs = std::clamp(qint64(in) * 100, r.startMs, r.endMs);
    r.outroMs = std::clamp(qint64(out + 1) * 100, r.introMs, r.endMs);
}

// Tempo from the autocorrelation of the onset envelope, a perceptual prior
// around 120 BPM, then a comb search for the exact period and the beat phase.
void tempo(const State &st, Result &r)
{
    const std::vector<float> &f = st.flux;
    const int n = int(f.size());
    const double OnsetFps = st.fps;
    if (n < int(OnsetFps * 8)) // under 8 s: no reliable tempo
        return;
    // Remove the slowly moving part, keep the attacks.
    auto detrend = [&](const std::vector<float> &in) {
        std::vector<float> out(size_t(n), 0.f);
        const int w = int(OnsetFps / 2);
        std::vector<double> pre(size_t(n + 1), 0.0);
        for (int i = 0; i < n; ++i)
            pre[size_t(i + 1)] = pre[size_t(i)] + in[size_t(i)];
        for (int i = 0; i < n; ++i) {
            const int a = std::max(0, i - w), b = std::min(n, i + w + 1);
            out[size_t(i)] = std::max(0.f, float(in[size_t(i)] - (pre[size_t(b)] - pre[size_t(a)]) / (b - a)));
        }
        return out;
    };
    const std::vector<float> o = detrend(f);
    const std::vector<float> oLow = int(st.fluxLow.size()) == n ? detrend(st.fluxLow) : std::vector<float>();
    // Onsets per second (peaks clearly above the average).
    {
        double mean = 0, var = 0;
        for (float v : o)
            mean += v;
        mean /= n;
        for (float v : o)
            var += (v - mean) * (v - mean);
        const double thr = mean + std::sqrt(var / n);
        int count = 0, lastPeak = -100;
        for (int i = 1; i + 1 < n; ++i)
            if (o[size_t(i)] > thr && o[size_t(i)] >= o[size_t(i - 1)] && o[size_t(i)] > o[size_t(i + 1)] && i - lastPeak >= 4) {
                ++count;
                lastPeak = i;
            }
        r.onsetRate = count / (n / OnsetFps);
    }

    const int minLag = int(std::floor(60.0 * OnsetFps / 220.0));
    const int maxLag = int(std::ceil(60.0 * OnsetFps / 50.0));
    std::vector<double> acf(size_t(maxLag * 2 + 2), 0.0);
    double acf0 = 0;
    for (float v : o)
        acf0 += double(v) * v;
    acf0 /= n;
    for (int lag = minLag - 1; lag <= std::min(n - 1, maxLag * 2 + 1); ++lag) {
        double s = 0;
        for (int i = 0; i + lag < n; ++i)
            s += double(o[size_t(i)]) * o[size_t(i + lag)];
        acf[size_t(lag)] = s / (n - lag);
    }
    auto prior = [](double bpm) {
        const double x = std::log2(bpm / 120.0) / 0.9;
        return std::exp(-0.5 * x * x);
    };
    int best = -1;
    double bestScore = 0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const double bpm = 60.0 * OnsetFps / lag;
        // Half and double period agree with a real beat: count them a little.
        double s = acf[size_t(lag)] + 0.5 * acf[size_t(lag * 2)];
        const double sc = s * prior(bpm);
        if (sc > bestScore) {
            bestScore = sc;
            best = lag;
        }
    }
    if (best < 0 || bestScore <= 0)
        return;
    // Confidence: how periodic the onsets are (correlation at the beat period
    // relative to the signal's own energy). Noise ~0.13, steady beats 0.4+.
    const double periodicity = acf0 > 0 ? acf[size_t(best)] / acf0 : 0.0;
    r.bpmConf = std::clamp((periodicity - 0.15) / 0.3, 0.0, 1.0);

    // Sub-frame period from the neighbours.
    double period = best;
    {
        const double a = acf[size_t(best - 1)], b = acf[size_t(best)], c = acf[size_t(best + 1)];
        const double den = a - 2 * b + c;
        if (std::fabs(den) > 1e-12)
            period += std::clamp(0.5 * (a - c) / den, -0.5, 0.5);
    }

    // Comb search: exact period (±1.5 %) and phase that line up best with the onsets.
    auto at = [&](double x) {
        const int i = int(x);
        if (i < 0 || i + 1 >= n)
            return 0.0;
        const double t = x - i;
        return double(o[size_t(i)]) * (1 - t) + double(o[size_t(i + 1)]) * t;
    };
    double bestP = period, bestPhase = 0, bestS = -1;
    for (int k = -30; k <= 30; ++k) {
        const double p = period * (1.0 + k * 0.0005);
        for (double ph = 0; ph < p; ph += 1.0) {
            double s = 0;
            int beats = 0;
            for (double x = ph; x < n - 1; x += p, ++beats)
                s += at(x);
            s /= std::max(1, beats);
            if (s > bestS) {
                bestS = s;
                bestP = p;
                bestPhase = ph;
            }
        }
    }
    // The beat itself sits on the kick: take the phase from the bass envelope
    // when it has a clear pulse at this period.
    if (!oLow.empty()) {
        auto atLow = [&](double x) {
            const int i = int(x);
            if (i < 0 || i + 1 >= n)
                return 0.0;
            const double t = x - i;
            return double(oLow[size_t(i)]) * (1 - t) + double(oLow[size_t(i + 1)]) * t;
        };
        double bestL = -1, phL = 0, sumL = 0;
        int cntL = 0;
        for (double ph = 0; ph < bestP; ph += 1.0) {
            double sl = 0;
            int beats = 0;
            for (double x = ph; x < n - 1; x += bestP, ++beats)
                sl += atLow(x);
            sl /= std::max(1, beats);
            sumL += sl;
            ++cntL;
            if (sl > bestL) {
                bestL = sl;
                phL = ph;
            }
        }
        if (cntL > 0 && bestL > 2.0 * (sumL / cntL))
            bestPhase = phL;
    }
    if (qEnvironmentVariableIsSet("HALCYON_ANALYSIS_DEBUG"))
        qInfo("tempo: best lag %d, periodicity %.3f, period %.3f", best, periodicity, bestP);
    double bpm = 60.0 * OnsetFps / bestP;
    // Same feel, one octave apart: keep it in the range DJs read.
    while (bpm < 75.0)
        bpm *= 2.0;
    while (bpm >= 185.0)
        bpm /= 2.0;
    r.bpm = std::round(bpm * 10.0) / 10.0;
    // The onset envelope lags the attack by about half an analysis frame.
    r.beatMs = qint64(std::lround((bestPhase * OnsetHop + OnsetFft / 2.0) * 1000.0 / st.rate));
}

void key(const State &st, Result &r)
{
    if (st.chromaFrames < 8)
        return;
    static const double major[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static const double minor[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    auto corr = [&](const double *prof, int tonic) {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) {
            mx += st.chroma[size_t((i + tonic) % 12)];
            my += prof[i];
        }
        mx /= 12;
        my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i) {
            const double x = st.chroma[size_t((i + tonic) % 12)] - mx, y = prof[i] - my;
            sxy += x * y;
            sxx += x * x;
            syy += y * y;
        }
        return sxx > 0 && syy > 0 ? sxy / std::sqrt(sxx * syy) : 0.0;
    };
    double best = -2, second = -2;
    int bestKey = -1;
    bool bestMinor = false;
    for (int t = 0; t < 12; ++t)
        for (int m = 0; m < 2; ++m) {
            const double c = corr(m ? minor : major, t);
            if (c > best) {
                second = best;
                best = c;
                bestKey = t;
                bestMinor = m;
            } else if (c > second) {
                second = c;
            }
        }
    if (bestKey < 0 || best < 0.3)
        return;
    r.key = bestKey;
    r.minor = bestMinor;
    // Clear winner and a strong fit = confident.
    r.keyConf = std::clamp((best - second) * 5.0 + (best - 0.5), 0.0, 1.0);
}

QVector<float> waveform(const State &st)
{
    const std::vector<float> &raw = st.peaks10ms;
    const int n = int(raw.size());
    if (n < 10)
        return {};
    QVector<float> out(WaveBuckets, 0.f);
    for (int b = 0; b < WaveBuckets; ++b) {
        const int a = int(qint64(b) * n / WaveBuckets);
        const int e = std::max(a + 1, int(qint64(b + 1) * n / WaveBuckets));
        float sum = 0.f, mx = 0.f;
        for (int i = a; i < e && i < n; ++i) {
            sum += raw[size_t(i)];
            mx = std::max(mx, raw[size_t(i)]);
        }
        out[b] = 0.65f * (sum / float(e - a)) + 0.35f * mx;
    }
    const float top = *std::max_element(out.begin(), out.end());
    if (top <= 0.0001f)
        return QVector<float>(WaveBuckets, 0.02f);
    for (float &v : out)
        v = std::pow(v / top, 0.8f);
    return out;
}

} // namespace

Result analyse(const QByteArray &uri, const std::atomic<bool> &cancel, int maxSeconds, double duty)
{
    Result r;
    GstElement *pipe = gst_pipeline_new("analysis");
    GstElement *src = gst_element_factory_make("uridecodebin", nullptr);
    GstElement *conv = gst_element_factory_make("audioconvert", nullptr);
    GstElement *filter = gst_element_factory_make("capsfilter", nullptr);
    GstElement *sink = gst_element_factory_make("fakesink", nullptr);
    if (!pipe || !src || !conv || !filter || !sink) {
        for (GstElement *e : {pipe, src, conv, filter, sink})
            if (e)
                gst_object_unref(e);
        return r;
    }
    GstCaps *caps = gst_caps_new_simple("audio/x-raw", "format", G_TYPE_STRING, "F32LE", "layout", G_TYPE_STRING, "interleaved",
                                        "channels", G_TYPE_INT, 2, nullptr);
    g_object_set(filter, "caps", caps, nullptr);
    gst_caps_unref(caps);
    g_object_set(sink, "sync", FALSE, nullptr);
    g_object_set(src, "uri", uri.constData(), nullptr);
    // Never open video decoders for files with embedded pictures.
    GstCaps *audioOnly = gst_caps_from_string("audio/x-raw");
    g_object_set(src, "caps", audioOnly, nullptr);
    gst_caps_unref(audioOnly);

    gst_bin_add_many(GST_BIN(pipe), src, conv, filter, sink, nullptr);
    gst_element_link_many(conv, filter, sink, nullptr);
    g_signal_connect(src, "pad-added", G_CALLBACK(onPadAdded), conv);

    Holder holder;
    holder.maxSeconds = maxSeconds;
    holder.duty = std::clamp(duty, 0.05, 1.0);
    holder.cancel = &cancel;
    GstPad *sinkPad = gst_element_get_static_pad(sink, "sink");
    gst_pad_add_probe(sinkPad, GST_PAD_PROBE_TYPE_BUFFER, onBuffer, &holder, nullptr);
    gst_object_unref(sinkPad);

    bool ok = false, partial = false, answered = false;
    gint64 duration = 0;
    QElapsedTimer clock;
    clock.start();
    if (gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE) {
        GstBus *bus = gst_element_get_bus(pipe);
        // Streams over a network (or a throttled analysis) may take a while.
        const qint64 limitMs = qint64(300000 / std::clamp(duty, 0.05, 1.0));
        while (!cancel && clock.elapsed() < limitMs) {
            GstMessage *msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
                                                         GstMessageType(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
            if (holder.full) {
                partial = true;
                ok = true;
                if (msg)
                    gst_message_unref(msg);
                break;
            }
            if (!msg)
                continue;
            ok = GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS;
            answered = true;
            gst_message_unref(msg);
            break;
        }
        r.timedOut = !answered && !partial && !cancel;
        if (partial)
            gst_element_query_duration(pipe, GST_FORMAT_TIME, &duration);
        gst_object_unref(bus);
    }
    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
    State *st = holder.st.get();
    if (!ok || cancel || !st || st->frames < st->rate) // under a second: nothing to learn
        return r;

    // Flush the last partial loudness block.
    if (st->blockN > st->rate / 50)
        st->blocks.push_back(float(st->blockAcc / st->blockN));

    r.ok = true;
    r.status = partial ? 2 : 0;
    r.lengthMs = partial && duration > 0 ? duration / GST_MSECOND : st->frames * 1000 / st->rate;
    r.peak = st->peak > 0 ? 20.0 * std::log10(st->peak) : -100.0;
    loudness(*st, r);
    edges(*st, r);
    if (partial) { // the end was never seen
        r.endMs = r.outroMs = r.lengthMs;
    }
    tempo(*st, r);
    key(*st, r);
    r.brightness = st->centroidW > 0 ? st->centroidAcc / st->centroidW : 0;
    const double e1 = std::clamp((r.loudness + 30.0) / 24.0, 0.0, 1.0);
    const double e2 = std::clamp(r.onsetRate / 5.0, 0.0, 1.0);
    const double e3 = std::clamp((r.brightness - 800.0) / 2700.0, 0.0, 1.0);
    r.energy = std::round((0.45 * e1 + 0.35 * e2 + 0.2 * e3) * 1000.0) / 1000.0;
    if (!partial) // a waveform of only the first part would put the seek bar off
        r.waveform = waveform(*st);
    return r;
}

QString camelot(int key, bool minor)
{
    if (key < 0 || key > 11)
        return {};
    static const int maj[12] = {8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1};
    static const int min[12] = {5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10};
    return QString::number(minor ? min[key] : maj[key]) + (minor ? QLatin1Char('A') : QLatin1Char('B'));
}

QString keyName(int key, bool minor)
{
    if (key < 0 || key > 11)
        return {};
    static const char *maj[12] = {"C", "Des", "D", "Es", "E", "F", "Fis", "G", "As", "A", "B", "H"};
    static const char *min[12] = {"c", "cis", "d", "es", "e", "f", "fis", "g", "gis", "a", "b", "h"};
    return minor ? QString::fromLatin1(min[key]) + QStringLiteral("-Moll") : QString::fromLatin1(maj[key]) + QStringLiteral("-Dur");
}

} // namespace AudioAnalyzer
