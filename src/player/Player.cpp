#include "player/Player.h"

#include "core/CoverProvider.h"
#include "core/Database.h"
#include "library/TrackCache.h"
#include "player/PlayQueue.h"

#include <QDateTime>
#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <QUrl>
#include <QtConcurrent>
#include <cmath>
#include <gst/gst.h>

namespace {

constexpr int SpectrumBands = 256;
constexpr int Bars = 40;
constexpr double FloorDb = -72.0;

GstBusSyncReply busTrampoline(GstBus *bus, GstMessage *msg, gpointer self)
{
    return GstBusSyncReply(Player::onBusSync(bus, msg, self));
}

void aboutToFinishTrampoline(GstElement *playbin, gpointer self)
{
    Player::onAboutToFinish(playbin, self);
}

GstElement *make(const char *factory, const char *name)
{
    GstElement *e = gst_element_factory_make(factory, name);
    if (!e)
        qWarning() << "GStreamer-Element fehlt:" << factory << "(Funktion deaktiviert)";
    return e;
}

// Record a finished play off the UI thread; the scanner may hold the write lock.
void recordPlay(qint64 id, qint64 listenedMs, qint64 durationMs, bool skipped)
{
    if (id <= 0 || listenedMs < 1500)
        return; // loaded but never really played
    const bool counted = listenedMs >= std::min<qint64>(240000, std::max<qint64>(30000, durationMs / 2));
    const bool skip = skipped && !counted && listenedMs < std::max<qint64>(30000, durationMs * 3 / 10);
    if (!counted && !skip)
        return;
    (void)QtConcurrent::run([id, listenedMs, counted, skip] {
        try {
            DbConnection db = Database::open(1024);
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            if (counted) {
                Stmt u(db, "UPDATE tracks SET play_count = play_count + 1, last_played = ? WHERE id = ?");
                u.bind(1, now).bind(2, id);
                u.exec();
            } else if (skip) {
                Stmt u(db, "UPDATE tracks SET skip_count = skip_count + 1 WHERE id = ?");
                u.bind(1, id);
                u.exec();
            }
            Stmt p(db, "INSERT INTO plays(track_id, played_at, listened_ms, completed) VALUES (?, ?, ?, ?)");
            p.bind(1, id).bind(2, now).bind(3, listenedMs).bind(4, counted ? 1 : 0);
            p.exec();
        } catch (const std::exception &e) {
            qWarning() << "Wiedergabestatistik nicht gespeichert:" << e.what();
        }
    });
}

} // namespace

Player::Player(PlayQueue *queue, QObject *parent) : QObject(parent), m_queue(queue)
{
    connect(&m_wave, &Waveform::ready, this, [this](qint64 id, const QVariantList &values) {
        if (id != m_playingId)
            return;
        m_waveform = values;
        emit waveformChanged();
        // Prepare the next track while nothing else is going on.
        const qint64 next = m_queue->nextId();
        if (next > 0 && !values.isEmpty())
            m_wave.request(next, TrackCache::instance().pathOf(next), 0, TrackCache::instance().get(next).mtime);
    });
    m_playbin = gst_element_factory_make("playbin", "halcyon");
    if (!m_playbin) {
        m_error = tr("GStreamer 'playbin' fehlt. Bitte gst-plugins-base installieren.");
        qCritical() << m_error;
        return;
    }

    // Audio only: no video decoding, subtitles or visualisations.
    gint flags = 0;
    g_object_get(m_playbin, "flags", &flags, nullptr);
    flags &= ~((1 << 0) | (1 << 2) | (1 << 3)); // VIDEO, TEXT, VIS
    flags |= (1 << 1);                         // AUDIO
    g_object_set(m_playbin, "flags", flags, nullptr);

    // Two prepared filter chains; ReplayGain "off" swaps in the chain without rgvolume.
    m_binRg = buildFilterBin(true, &m_rgEl, &m_spectrumRg, &m_levelRg);
    m_binPlain = buildFilterBin(false, nullptr, &m_spectrumPlain, &m_levelPlain);

    g_signal_connect(m_playbin, "about-to-finish", G_CALLBACK(aboutToFinishTrampoline), this);
    GstBus *bus = gst_element_get_bus(m_playbin);
    gst_bus_set_sync_handler(bus, busTrampoline, this, nullptr);
    gst_object_unref(bus);

    m_posTimer.setInterval(250);
    connect(&m_posTimer, &QTimer::timeout, this, &Player::pollPosition);
    connect(m_queue, &PlayQueue::upcomingChanged, this, &Player::armNext);

    QSettings s;
    m_volume = std::clamp(s.value(QStringLiteral("player/volume"), 0.8).toDouble(), 0.0, 1.0);
    m_rgMode = std::clamp(s.value(QStringLiteral("player/replayGain"), 2).toInt(), 0, 2);
    m_preamp = std::clamp(s.value(QStringLiteral("player/preamp"), 0.0).toDouble(), -15.0, 15.0);
    applyVolume();
    if (m_rgEl)
        applyReplayGain();
    selectFilterBin();
    m_bars.fill(0.f, Bars);
}

GstElement *Player::buildFilterBin(bool withRg, GstElement **rgOut, GstElement **spectrumOut, GstElement **levelOut)
{
    // audioconvert ! [rgvolume ! rglimiter] ! audioconvert ! [spectrum] ! [level]
    GstElement *conv1 = make("audioconvert", nullptr);
    GstElement *conv2 = make("audioconvert", nullptr);
    if (!conv1 || !conv2) {
        if (conv1) gst_object_unref(conv1);
        if (conv2) gst_object_unref(conv2);
        return nullptr;
    }
    GstElement *rg = withRg ? make("rgvolume", nullptr) : nullptr;
    GstElement *limiter = rg ? make("rglimiter", nullptr) : nullptr;
    if (rg && !limiter) {
        gst_object_unref(rg);
        rg = nullptr; // never apply gain without the clipping guard
    }
    GstElement *spectrum = make("spectrum", nullptr);
    GstElement *level = make("level", nullptr);

    GstElement *bin = gst_bin_new(nullptr);
    QVector<GstElement *> chain;
    for (GstElement *e : {conv1, rg, limiter, conv2, spectrum, level})
        if (e) {
            chain << e;
            gst_bin_add(GST_BIN(bin), e);
        }
    bool linked = true;
    for (int i = 0; i + 1 < chain.size(); ++i)
        linked = linked && gst_element_link(chain[i], chain[i + 1]);
    if (!linked) {
        qWarning() << "Filterkette konnte nicht verbunden werden";
        gst_object_unref(bin);
        return nullptr;
    }
    GstPad *sink = gst_element_get_static_pad(chain.first(), "sink");
    GstPad *src = gst_element_get_static_pad(chain.last(), "src");
    gst_element_add_pad(bin, gst_ghost_pad_new("sink", sink));
    gst_element_add_pad(bin, gst_ghost_pad_new("src", src));
    gst_object_unref(sink);
    gst_object_unref(src);
    if (spectrum)
        g_object_set(spectrum, "bands", guint(SpectrumBands), "threshold", gint(FloorDb), "interval",
                     guint64(33 * GST_MSECOND), "post-messages", FALSE, "message-magnitude", TRUE, nullptr);
    if (level)
        g_object_set(level, "interval", guint64(33 * GST_MSECOND), "post-messages", FALSE, "peak-ttl",
                     guint64(300 * GST_MSECOND), nullptr);
    if (levelOut)
        *levelOut = level;
    if (rgOut)
        *rgOut = rg;
    if (spectrumOut)
        *spectrumOut = spectrum;
    gst_object_ref_sink(bin); // we own it; playbin takes its own reference
    return bin;
}

void Player::selectFilterBin()
{
    if (!m_playbin)
        return;
    const bool useRg = m_rgMode != 0 && m_binRg;
    GstElement *bin = useRg ? m_binRg : m_binPlain;
    m_spectrumEl = useRg ? m_spectrumRg : m_spectrumPlain;
    m_levelEl = useRg ? m_levelRg : m_levelPlain;
    if (m_spectrumEl)
        g_object_set(m_spectrumEl, "post-messages", m_spectrumEnabled ? TRUE : FALSE, nullptr);
    if (m_levelEl)
        g_object_set(m_levelEl, "post-messages", m_levelsEnabled ? TRUE : FALSE, nullptr);
    if (bin)
        g_object_set(m_playbin, "audio-filter", bin, nullptr);
}

Player::~Player()
{
    if (m_playbin) {
        gst_element_set_state(m_playbin, GST_STATE_NULL);
        GstBus *bus = gst_element_get_bus(m_playbin);
        gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
        gst_object_unref(bus);
        gst_object_unref(m_playbin);
    }
    if (m_binRg)
        gst_object_unref(m_binRg);
    if (m_binPlain)
        gst_object_unref(m_binPlain);
}

QByteArray Player::uriFor(qint64 id) const
{
    const QString path = TrackCache::instance().pathOf(id);
    return path.isEmpty() ? QByteArray() : QUrl::fromLocalFile(path).toEncoded();
}

void Player::onAboutToFinish(GstElement *playbin, void *data)
{
    auto *self = static_cast<Player *>(data);
    QMutexLocker lock(&self->m_mutex);
    if (self->m_nextUri.isEmpty())
        return;
    g_object_set(playbin, "uri", self->m_nextUri.constData(), nullptr);
    self->m_expectAdvance = true;
}

int Player::onBusSync(GstBus *, GstMessage *msg, void *data)
{
    auto *self = static_cast<Player *>(data);
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ELEMENT: {
        const GstStructure *st = gst_message_get_structure(msg);
        if (st && gst_structure_has_name(st, "spectrum")) {
            const GValue *list = gst_structure_get_value(st, "magnitude");
            if (list && GST_VALUE_HOLDS_LIST(list)) {
                const guint n = gst_value_list_get_size(list);
                QVector<float> mags(qsizetype(n), 0.f);
                for (guint i = 0; i < n; ++i)
                    mags[int(i)] = g_value_get_float(gst_value_list_get_value(list, i));
                QMetaObject::invokeMethod(self, [self, mags] { self->handleSpectrum(mags); }, Qt::QueuedConnection);
            }
        } else if (st && gst_structure_has_name(st, "level")) {
            // rms / peak: one double per channel, in dB.
            auto values = [st](const char *field) {
                QVector<double> out;
                const GValue *v = gst_structure_get_value(st, field);
                if (!v)
                    return out;
                if (GST_VALUE_HOLDS_LIST(v)) {
                    for (guint i = 0; i < gst_value_list_get_size(v); ++i)
                        out << g_value_get_double(gst_value_list_get_value(v, i));
                } else if (G_VALUE_HOLDS_BOXED(v)) {
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                    auto *arr = static_cast<GValueArray *>(g_value_get_boxed(v));
                    for (guint i = 0; arr && i < arr->n_values; ++i)
                        out << g_value_get_double(g_value_array_get_nth(arr, i));
G_GNUC_END_IGNORE_DEPRECATIONS
                }
                return out;
            };
            const QVector<double> rms = values("rms"), peak = values("peak");
            QMetaObject::invokeMethod(self, [self, rms, peak] { self->handleLevels(rms, peak); }, Qt::QueuedConnection);
        }
        break;
    }
    case GST_MESSAGE_EOS:
    case GST_MESSAGE_ERROR:
    case GST_MESSAGE_WARNING:
    case GST_MESSAGE_STREAM_START:
    case GST_MESSAGE_ASYNC_DONE:
    case GST_MESSAGE_DURATION_CHANGED:
    case GST_MESSAGE_STATE_CHANGED: {
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_STATE_CHANGED && GST_MESSAGE_SRC(msg) != GST_OBJECT(self->m_playbin))
            break;
        gst_message_ref(msg);
        QMetaObject::invokeMethod(self, [self, msg] {
            self->handleMessage(msg);
            gst_message_unref(msg);
        }, Qt::QueuedConnection);
        break;
    }
    default:
        break;
    }
    // Everything we need was copied or ref'd above. A sync handler that returns
    // GST_BUS_DROP owns the message and must unref it, otherwise every message
    // (30+ spectrum/level messages per second) stays in memory forever.
    gst_message_unref(msg);
    return GST_BUS_DROP;
}

void Player::handleMessage(GstMessage *msg)
{
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_STATE_CHANGED: {
        GstState oldS, newS, pending;
        gst_message_parse_state_changed(msg, &oldS, &newS, &pending);
        if (newS == GST_STATE_PLAYING) {
            m_switching = false;
            setState(1);
        } else if (newS == GST_STATE_PAUSED && pending == GST_STATE_VOID_PENDING && !m_switching) {
            setState(2);
        } else if (newS == GST_STATE_PAUSED && pending == GST_STATE_VOID_PENDING && m_switching) {
            // Loaded paused (session restore): done switching.
            QMutexLocker lock(&m_mutex);
            if (!m_expectAdvance) {
                m_switching = false;
                lock.unlock();
                setState(2);
            }
        } else if (newS <= GST_STATE_READY && !m_switching) {
            setState(0);
        }
        break;
    }
    case GST_MESSAGE_ASYNC_DONE:
    case GST_MESSAGE_DURATION_CHANGED: {
        gint64 dur = 0;
        if (gst_element_query_duration(m_playbin, GST_FORMAT_TIME, &dur) && dur > 0) {
            m_duration = dur / GST_MSECOND;
            emit durationChanged();
        }
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ASYNC_DONE && m_pendingSeek >= 0) {
            const qint64 ms = m_pendingSeek;
            m_pendingSeek = -1;
            gst_element_seek_simple(m_playbin, GST_FORMAT_TIME, GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                                    ms * GST_MSECOND);
            m_position = m_lastPollPos = ms;
            emit positionChanged();
        }
        break;
    }
    case GST_MESSAGE_STREAM_START: {
        bool advanced = false;
        {
            QMutexLocker lock(&m_mutex);
            advanced = m_expectAdvance;
            m_expectAdvance = false;
        }
        if (advanced) {
            // Gapless hand-over: the next track is already playing.
            finishTrack(false);
            m_queue->advance();
            updateMeta(m_queue->currentId());
            m_position = m_lastPollPos = 0;
            emit positionChanged();
        }
        m_consecutiveErrors = 0;
        break;
    }
    case GST_MESSAGE_EOS: {
        finishTrack(false);
        gst_element_set_state(m_playbin, GST_STATE_READY);
        m_position = 0;
        emit positionChanged();
        setState(0);
        break;
    }
    case GST_MESSAGE_ERROR: {
        GError *err = nullptr;
        gchar *dbg = nullptr;
        gst_message_parse_error(msg, &err, &dbg);
        m_error = tr("Wiedergabefehler: %1").arg(QString::fromUtf8(err ? err->message : "unbekannt"));
        qWarning() << m_error << (dbg ? dbg : "");
        g_clear_error(&err);
        g_free(dbg);
        emit errorChanged();
        gst_element_set_state(m_playbin, GST_STATE_READY);
        // Skip unreadable files, but never spin through a whole broken queue.
        if (++m_consecutiveErrors < 5 && m_queue->advance()) {
            load(m_queue->currentId(), true);
        } else {
            m_switching = false;
            setState(0);
        }
        break;
    }
    case GST_MESSAGE_WARNING: {
        GError *err = nullptr;
        gst_message_parse_warning(msg, &err, nullptr);
        qWarning() << "GStreamer:" << (err ? err->message : "");
        g_clear_error(&err);
        break;
    }
    default:
        break;
    }
}

void Player::handleSpectrum(const QVector<float> &mags)
{
    if (!m_spectrumEnabled || mags.size() < 16)
        return;
    const int n = int(mags.size());
    QVariantList out;
    out.reserve(Bars);
    // Log-spaced bars over linear FFT bands: bass gets as much room as treble.
    double lo = 1.0;
    const double ratio = std::pow(double(n) / lo, 1.0 / Bars);
    for (int b = 0; b < Bars; ++b) {
        const double hi = lo * ratio;
        int i0 = int(lo), i1 = std::max(i0 + 1, int(hi));
        i1 = std::min(i1, n);
        float peak = float(FloorDb);
        for (int i = i0; i < i1; ++i)
            peak = std::max(peak, mags[i]);
        float v = float((peak - FloorDb) / -FloorDb);
        v = std::clamp(v, 0.f, 1.f);
        // Fast attack, slow release.
        m_bars[b] = v > m_bars[b] ? v : m_bars[b] * 0.86f + v * 0.14f;
        out << double(m_bars[b]);
        lo = hi;
    }
    m_spectrumOut = out;
    emit spectrumChanged();
}

void Player::handleLevels(const QVector<double> &rms, const QVector<double> &peak)
{
    if (!m_levelsEnabled || rms.isEmpty())
        return;
    // dB -> 0..1 on a VU-like scale: -40 dB is the floor, 0 dBFS is full scale.
    auto norm = [](double db) { return std::clamp((db + 40.0) / 40.0, 0.0, 1.0); };
    const double l = norm(rms.value(0, -100));
    const double r = norm(rms.value(rms.size() > 1 ? 1 : 0, -100));
    m_levelL = l;
    m_levelR = r;
    m_peakL = norm(peak.value(0, -100));
    m_peakR = norm(peak.value(peak.size() > 1 ? 1 : 0, -100));
    const double e = (l + r) / 2;
    // Simple onset detector: energy clearly above its recent average.
    m_avgEnergy = m_avgEnergy * 0.92 + e * 0.08;
    if (e > m_avgEnergy * 1.18 + 0.03 && m_beatCooldown <= 0) {
        m_beat = 1.0;
        m_beatCooldown = 7; // ~230 ms
        emit beatHit();
    } else {
        m_beat *= 0.86;
    }
    if (m_beatCooldown > 0)
        --m_beatCooldown;
    m_energy = m_energy * 0.6 + e * 0.4;
    emit levelsChanged();
}

void Player::setLevelsEnabled(bool on)
{
    if (on == m_levelsEnabled)
        return;
    m_levelsEnabled = on;
    if (m_levelEl)
        g_object_set(m_levelEl, "post-messages", on ? TRUE : FALSE, nullptr);
    if (!on)
        resetLevels();
    emit levelsEnabledChanged();
}

void Player::setAnalysisHz(int hz)
{
    hz = std::clamp(hz, 5, 60);
    if (hz == m_analysisHz)
        return;
    m_analysisHz = hz;
    const guint64 interval = guint64(1000 / hz) * GST_MSECOND;
    for (GstElement *e : {m_spectrumRg, m_spectrumPlain, m_levelRg, m_levelPlain})
        if (e)
            g_object_set(e, "interval", interval, nullptr);
    emit analysisHzChanged();
}

void Player::resetLevels()
{
    m_levelL = m_levelR = m_peakL = m_peakR = m_energy = m_beat = 0;
    emit levelsChanged();
}

void Player::setState(int s)
{
    if (s == m_state)
        return;
    m_state = s;
    if (s == 1)
        m_posTimer.start();
    else
        m_posTimer.stop();
    if (s != 1 && m_levelsEnabled)
        resetLevels();
    if (s != 1 && m_spectrumEnabled) {
        m_bars.fill(0.f, Bars);
        m_spectrumOut.clear();
        emit spectrumChanged();
    }
    emit stateChanged();
}

void Player::updateMeta(qint64 id)
{
    m_playingId = id;
    m_listenedMs = 0;
    m_lastPollPos = 0;
    const TrackRow r = TrackCache::instance().get(id);
    m_title = r.title;
    m_artist = r.artist;
    m_album = r.album;
    m_albumArtist = r.aa;
    m_year = r.year;
    m_format = r.formatLabel();
    m_duration = r.durationMs;
    emit trackChanged();
    emit durationChanged();
    requestWaveform();

    QPointer<Player> self(this);
    (void)QtConcurrent::run([self, id] {
        const QImage img = CoverProvider::load(id, 64);
        const QColor c = CoverProvider::dominantColor(img);
        const QVariantList pal = CoverProvider::palette(img);
        QMetaObject::invokeMethod(qApp, [self, id, c, pal] {
            if (self && self->m_playingId == id) {
                self->m_coverColor = c;
                self->m_coverPalette = pal;
                emit self->coverColorChanged();
            }
        }, Qt::QueuedConnection);
    });
    armNext();
}

void Player::requestWaveform()
{
    if (!m_waveform.isEmpty()) {
        m_waveform.clear();
        emit waveformChanged();
    }
    if (m_playingId > 0 && m_wave.enabled()) {
        const TrackRow r = TrackCache::instance().get(m_playingId);
        m_wave.request(m_playingId, TrackCache::instance().pathOf(m_playingId), 0, r.mtime);
    }
}

void Player::setWaveformEnabled(bool on)
{
    if (on == m_wave.enabled())
        return;
    m_wave.setEnabled(on);
    if (on)
        requestWaveform();
    else if (!m_waveform.isEmpty())
        m_waveform.clear();
    emit waveformChanged();
}

void Player::armNext()
{
    if (!m_playbin)
        return;
    const qint64 next = m_queue->nextId();
    const QByteArray uri = next > 0 ? uriFor(next) : QByteArray();
    QMutexLocker lock(&m_mutex);
    m_nextId = next;
    m_nextUri = uri;
}

bool Player::load(qint64 id, bool startPlaying, qint64 seekMs)
{
    if (!m_playbin || id <= 0)
        return false;
    const QByteArray uri = uriFor(id);
    if (uri.isEmpty())
        return false;
    {
        QMutexLocker lock(&m_mutex);
        m_expectAdvance = false;
    }
    m_switching = true;
    gst_element_set_state(m_playbin, GST_STATE_READY);
    g_object_set(m_playbin, "uri", uri.constData(), nullptr);
    m_pendingSeek = seekMs > 0 ? seekMs : -1;
    updateMeta(id);
    m_position = std::max<qint64>(0, seekMs);
    emit positionChanged();
    gst_element_set_state(m_playbin, startPlaying ? GST_STATE_PLAYING : GST_STATE_PAUSED);
    m_error.clear();
    emit errorChanged();
    return true;
}

void Player::finishTrack(bool skipped)
{
    pollPosition();
    recordPlay(m_playingId, m_listenedMs, m_duration, skipped);
    m_listenedMs = 0;
}

void Player::playCurrent()
{
    if (m_state != 0)
        finishTrack(true);
    m_consecutiveErrors = 0;
    load(m_queue->currentId(), true);
}

void Player::playRow(int queueRow)
{
    m_queue->jumpTo(queueRow);
    playCurrent();
}

void Player::toggle()
{
    if (m_state == 1)
        pause();
    else
        play();
}

void Player::play()
{
    if (!m_playbin)
        return;
    if (m_state == 2) {
        gst_element_set_state(m_playbin, GST_STATE_PLAYING);
    } else if (m_state == 0) {
        if (m_queue->currentId() > 0)
            load(m_queue->currentId(), true);
    }
}

void Player::pause()
{
    if (m_playbin && m_state == 1)
        gst_element_set_state(m_playbin, GST_STATE_PAUSED);
}

void Player::stop()
{
    if (!m_playbin)
        return;
    finishTrack(true);
    m_switching = false;
    gst_element_set_state(m_playbin, GST_STATE_READY);
    m_position = 0;
    emit positionChanged();
    setState(0);
}

void Player::next()
{
    if (m_state != 0)
        finishTrack(true);
    if (m_queue->advance())
        load(m_queue->currentId(), m_state != 2);
}

void Player::previous()
{
    // Restart the track first, like every hardware player.
    if (m_position > 3000) {
        seek(0);
        return;
    }
    if (m_state != 0)
        finishTrack(true);
    if (m_queue->back())
        load(m_queue->currentId(), m_state != 2);
}

void Player::seek(qint64 ms)
{
    if (!m_playbin)
        return;
    if (m_state == 0) {
        if (m_queue->currentId() > 0)
            load(m_queue->currentId(), true, ms);
        return;
    }
    gst_element_seek_simple(m_playbin, GST_FORMAT_TIME, GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                            ms * GST_MSECOND);
    m_position = m_lastPollPos = ms;
    emit positionChanged();
    emit seeked(ms);
}

void Player::pollPosition()
{
    if (!m_playbin || m_state == 0)
        return;
    gint64 pos = 0;
    if (gst_element_query_position(m_playbin, GST_FORMAT_TIME, &pos) && pos >= 0) {
        const qint64 ms = pos / GST_MSECOND;
        const qint64 delta = ms - m_lastPollPos;
        if (delta > 0 && delta < 2000)
            m_listenedMs += delta; // only real listening time, seeks don't count
        m_lastPollPos = ms;
        if (ms != m_position) {
            m_position = ms;
            emit positionChanged();
        }
    }
}

void Player::setVolume(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    if (qFuzzyCompare(v, m_volume))
        return;
    m_volume = v;
    applyVolume();
    emit volumeChanged();
}

void Player::setMuted(bool m)
{
    if (m == m_muted)
        return;
    m_muted = m;
    applyVolume();
    emit volumeChanged();
}

void Player::applyVolume()
{
    if (!m_playbin)
        return;
    // Cubic curve: the slider feels linear to the ear.
    const double linear = m_muted ? 0.0 : m_volume * m_volume * m_volume;
    g_object_set(m_playbin, "volume", linear, nullptr);
}

void Player::setReplayGain(int mode)
{
    mode = std::clamp(mode, 0, 2);
    if (mode == m_rgMode)
        return;
    const bool chainChanges = (mode == 0) != (m_rgMode == 0);
    m_rgMode = mode;
    applyReplayGain();
    if (chainChanges && m_playbin) {
        // Swap the filter chain: needs a stopped pipeline, then resume where we were.
        const int wasState = m_state;
        const qint64 pos = m_position;
        const qint64 id = m_playingId;
        m_switching = true;
        gst_element_set_state(m_playbin, GST_STATE_NULL);
        selectFilterBin();
        if (wasState != 0 && id > 0)
            load(id, wasState == 1, pos);
        else
            m_switching = false;
    }
    emit settingsChanged();
}

void Player::setPreamp(double db)
{
    db = std::clamp(db, -15.0, 15.0);
    if (qFuzzyCompare(db + 100, m_preamp + 100))
        return;
    m_preamp = db;
    applyReplayGain();
    emit settingsChanged();
}

void Player::applyReplayGain()
{
    if (!m_rgEl)
        return;
    // Files without ReplayGain tags get -6 dB so they don't jump out next to tagged ones.
    g_object_set(m_rgEl, "album-mode", m_rgMode == 2 ? TRUE : FALSE, "pre-amp", m_preamp, "fallback-gain", -6.0,
                 "headroom", 0.0, nullptr);
}

void Player::setSpectrumEnabled(bool on)
{
    if (on == m_spectrumEnabled)
        return;
    m_spectrumEnabled = on;
    if (m_spectrumEl)
        g_object_set(m_spectrumEl, "post-messages", on ? TRUE : FALSE, nullptr);
    if (!on) {
        m_bars.fill(0.f, Bars);
        m_spectrumOut.clear();
        emit spectrumChanged();
    }
    emit spectrumEnabledChanged();
}

void Player::restoreSession()
{
    QSettings s;
    const qint64 pos = s.value(QStringLiteral("player/position"), 0).toLongLong();
    const qint64 id = m_queue->currentId();
    if (id > 0 && s.value(QStringLiteral("player/trackId")).toLongLong() == id)
        load(id, false, pos);
    else if (id > 0)
        updateMeta(id);
}

void Player::saveSession()
{
    QSettings s;
    s.setValue(QStringLiteral("player/volume"), m_volume);
    s.setValue(QStringLiteral("player/replayGain"), m_rgMode);
    s.setValue(QStringLiteral("player/preamp"), m_preamp);
    s.setValue(QStringLiteral("player/trackId"), m_playingId);
    s.setValue(QStringLiteral("player/position"), m_position);
    if (m_state != 0)
        finishTrack(false); // quitting is not skipping
}
