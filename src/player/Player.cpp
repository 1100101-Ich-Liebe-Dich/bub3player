#include "player/Player.h"

#include "analysis/AnalysisService.h"
#include "core/CoverProvider.h"
#include "core/Database.h"
#include "jellyfin/JellyfinAccess.h"
#include "library/TrackCache.h"
#include "player/PlayQueue.h"

#include <QDateTime>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QPointer>
#include <QSettings>
#include <QUrl>
#include <QtConcurrent>
#include <cmath>
#include <gst/controller/gstdirectcontrolbinding.h>
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gst/gst.h>

// QT_LOGGING_RULES="halcyon.mix.debug=true": transitions step by step, and the
// level of both decks every 100 ms (to see the fades).
Q_LOGGING_CATEGORY(lcMix, "halcyon.mix", QtWarningMsg)

namespace {

constexpr int SpectrumBands = 256;
constexpr int Bars = 40;
constexpr double FloorDb = -72.0;

GstBusSyncReply busTrampoline(GstBus *bus, GstMessage *msg, gpointer deck)
{
    return GstBusSyncReply(Player::onBusSync(bus, msg, deck));
}

void aboutToFinishTrampoline(GstElement *playbin, gpointer deck)
{
    Player::onAboutToFinish(playbin, deck);
}

GstElement *make(const char *factory, const char *name)
{
    GstElement *e = gst_element_factory_make(factory, name);
    if (!e)
        qWarning() << "GStreamer-Element fehlt:" << factory << "(Funktion deaktiviert)";
    return e;
}

// Record a finished play off the UI thread; the scanner may hold the write lock.
// Returns whether it counted as a play or a skip (else: just loaded, ignored).
int recordPlay(qint64 id, qint64 listenedMs, qint64 durationMs, bool skipped)
{
    if (id <= 0 || listenedMs < 1500)
        return 0; // loaded but never really played
    const bool counted = listenedMs >= std::min<qint64>(240000, std::max<qint64>(30000, durationMs / 2));
    const bool skip = skipped && !counted && listenedMs < std::max<qint64>(30000, durationMs * 3 / 10);
    if (!counted && !skip)
        return 0;
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
    return counted ? 1 : 2;
}

// Applies the fallback gain of the stream that is starting (gapless: the next track's).
GstPadProbeReturn onStreamEvent(GstPad *, GstPadProbeInfo *info, gpointer data)
{
    auto *d = static_cast<Player::Deck *>(data);
    GstEvent *ev = GST_PAD_PROBE_INFO_EVENT(info);
    if (ev && GST_EVENT_TYPE(ev) == GST_EVENT_STREAM_START && d->rg) {
        const double g = d->expectAdvance ? d->gainNext.load() : d->gainNow.load();
        g_object_set(d->rg, "fallback-gain", g, nullptr);
    }
    return GST_PAD_PROBE_OK;
}

QVector<QPair<qint64, double>> fadeOutCurve(qint64 from, qint64 len, bool hold)
{
    QVector<QPair<qint64, double>> p;
    if (hold) {
        // The track fades by itself: keep it, only close the last moment.
        const qint64 drop = std::min<qint64>(1500, len / 3);
        p << qMakePair(from, 1.0) << qMakePair(from + len - drop, 1.0) << qMakePair(from + len, 0.0);
        return p;
    }
    const int steps = 16;
    for (int i = 0; i <= steps; ++i) {
        const double t = double(i) / steps;
        p << qMakePair(from + qint64(len * t), std::cos(t * M_PI / 2)); // equal power
    }
    return p;
}

QVector<QPair<qint64, double>> fadeInCurve(qint64 from, qint64 len)
{
    QVector<QPair<qint64, double>> p;
    const int steps = 16;
    for (int i = 0; i <= steps; ++i) {
        const double t = double(i) / steps;
        p << qMakePair(from + qint64(len * t), std::sin(t * M_PI / 2));
    }
    return p;
}

} // namespace

Player::Player(PlayQueue *queue, QObject *parent) : QObject(parent), m_queue(queue)
{
    QSettings s;
    m_volume = std::clamp(s.value(QStringLiteral("player/volume"), 0.8).toDouble(), 0.0, 1.0);
    m_rgMode = std::clamp(s.value(QStringLiteral("player/replayGain"), 2).toInt(), 0, 2);
    m_preamp = std::clamp(s.value(QStringLiteral("player/preamp"), 0.0).toDouble(), -15.0, 15.0);
    m_transition = std::clamp(s.value(QStringLiteral("player/transition"), 0).toInt(), 0, 2);
    m_fadeSec = std::clamp(s.value(QStringLiteral("player/fadeSeconds"), 6).toInt(), 1, 15);
    m_albumGapless = s.value(QStringLiteral("player/albumGapless"), true).toBool();
    m_skipSilence = s.value(QStringLiteral("player/skipSilence"), true).toBool();
    m_loudnessMatch = s.value(QStringLiteral("player/loudnessMatch"), true).toBool();

    if (!initDeck(m_deck[0], 0)) {
        m_error = tr("GStreamer 'playbin' fehlt. Bitte gst-plugins-base installieren.");
        qCritical() << m_error;
        return;
    }
    initDeck(m_deck[1], 1);

    m_posTimer.setInterval(250);
    connect(&m_posTimer, &QTimer::timeout, this, &Player::pollPosition);
    connect(m_queue, &PlayQueue::upcomingChanged, this, &Player::armNext);

    if (AnalysisService *a = AnalysisService::instance()) {
        connect(a, &AnalysisService::waveformReady, this, [this](qint64 id, const QVariantList &values) {
            if (id != m_playingId || !m_waveEnabled)
                return;
            m_waveform = values;
            emit waveformChanged();
        });
        connect(a, &AnalysisService::analysed, this, [this](qint64 id) {
            if (id == m_playingId)
                updateTrackInfo();
            if (id == m_queue->nextId())
                armNext(); // its loudness and transition are known now
            else if (id == m_playingId)
                plan();
        });
    }
    applyVolume();
    applyReplayGain();
    m_bars.fill(0.f, Bars);
}

bool Player::initDeck(Deck &d, int index)
{
    d.owner = this;
    d.index = index;
    d.playbin = gst_element_factory_make("playbin", index == 0 ? "halcyon" : "halcyon-b");
    if (!d.playbin)
        return false;
    // Audio only: no video decoding, subtitles or visualisations.
    gint flags = 0;
    g_object_get(d.playbin, "flags", &flags, nullptr);
    flags &= ~((1 << 0) | (1 << 2) | (1 << 3)); // VIDEO, TEXT, VIS
    flags |= (1 << 1);                         // AUDIO
    g_object_set(d.playbin, "flags", flags, nullptr);

    // Two prepared filter chains; ReplayGain "off" swaps in the chain without rgvolume.
    d.binRg = buildFilterBin(d, true);
    d.binPlain = buildFilterBin(d, false);

    g_signal_connect(d.playbin, "about-to-finish", G_CALLBACK(aboutToFinishTrampoline), &d);
    GstBus *bus = gst_element_get_bus(d.playbin);
    gst_bus_set_sync_handler(bus, busTrampoline, &d, nullptr);
    gst_object_unref(bus);
    selectFilterBin(d);
    return true;
}

GstElement *Player::buildFilterBin(Deck &d, bool withRg)
{
    // audioconvert ! [rgvolume ! rglimiter] ! audioconvert ! [volume (fader)] ! [spectrum] ! [level]
    // Meters come after the fader: they show what is heard, blends included.
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
    if (withRg && !rg) {
        gst_object_unref(conv1);
        gst_object_unref(conv2);
        return nullptr;
    }
    GstElement *spectrum = make("spectrum", nullptr);
    GstElement *level = make("level", nullptr);
    GstElement *fader = make("volume", nullptr);

    GstElement *bin = gst_bin_new(nullptr);
    QVector<GstElement *> chain;
    for (GstElement *e : {conv1, rg, limiter, conv2, fader, spectrum, level})
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
    if (rg) // fallback gain per stream (see onStreamEvent)
        gst_pad_add_probe(sink, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, onStreamEvent, &d, nullptr);
    gst_object_unref(sink);
    gst_object_unref(src);
    const guint64 interval = guint64(1000 / m_analysisHz) * GST_MSECOND;
    if (spectrum)
        g_object_set(spectrum, "bands", guint(SpectrumBands), "threshold", gint(FloorDb), "interval", interval,
                     "post-messages", FALSE, "message-magnitude", TRUE, nullptr);
    if (level)
        g_object_set(level, "interval", interval, "post-messages", FALSE, "peak-ttl", guint64(300 * GST_MSECOND), nullptr);
    GstControlSource *ctl = nullptr;
    if (fader) {
        ctl = gst_interpolation_control_source_new();
        g_object_set(ctl, "mode", GST_INTERPOLATION_MODE_LINEAR, nullptr);
    }
    if (withRg) {
        d.rgRg = rg;
        d.spectrumRg = spectrum;
        d.levelRg = level;
        d.faderRg = fader;
        d.ctlRg = ctl;
    } else {
        d.spectrumPlain = spectrum;
        d.levelPlain = level;
        d.faderPlain = fader;
        d.ctlPlain = ctl;
    }
    gst_object_ref_sink(bin); // we own it; playbin takes its own reference
    return bin;
}

void Player::selectFilterBin(Deck &d)
{
    if (!d.playbin)
        return;
    const bool useRg = m_rgMode != 0 && d.binRg;
    GstElement *bin = useRg ? d.binRg : d.binPlain;
    if (d.controlled)
        setFader(d, 1.0); // drop the curve of the chain we leave
    d.spectrum = useRg ? d.spectrumRg : d.spectrumPlain;
    d.level = useRg ? d.levelRg : d.levelPlain;
    d.fader = useRg ? d.faderRg : d.faderPlain;
    d.ctl = useRg ? d.ctlRg : d.ctlPlain;
    d.rg = useRg ? d.rgRg : nullptr;
    const bool isActive = &d == &m_deck[m_active];
    if (d.spectrum)
        g_object_set(d.spectrum, "post-messages", m_spectrumEnabled && isActive ? TRUE : FALSE, nullptr);
    if (d.level)
        g_object_set(d.level, "post-messages", (m_levelsEnabled && isActive) || lcMix().isDebugEnabled() ? TRUE : FALSE, nullptr);
    if (bin)
        g_object_set(d.playbin, "audio-filter", bin, nullptr);
}

void Player::freeDeck(Deck &d)
{
    if (d.playbin) {
        gst_element_set_state(d.playbin, GST_STATE_NULL);
        GstBus *bus = gst_element_get_bus(d.playbin);
        gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
        gst_object_unref(bus);
        gst_object_unref(d.playbin);
        d.playbin = nullptr;
    }
    for (GstControlSource *c : {d.ctlRg, d.ctlPlain})
        if (c)
            gst_object_unref(c);
    for (GstElement *b : {d.binRg, d.binPlain})
        if (b)
            gst_object_unref(b);
    d.binRg = d.binPlain = nullptr;
}

Player::~Player()
{
    freeDeck(m_deck[0]);
    freeDeck(m_deck[1]);
}

QByteArray Player::uriFor(qint64 id) const
{
    const TrackRow r = TrackCache::instance().get(id);
    if (r.isRemote())
        return JellyfinAccess::streamUrl(r.sourceId, r.remoteId);
    const QString path = TrackCache::instance().pathOf(id);
    return path.isEmpty() ? QByteArray() : QUrl::fromLocalFile(path).toEncoded();
}

bool Player::canSeek(qint64 id) const
{
    const TrackRow r = TrackCache::instance().get(id);
    if (!r.isRemote())
        return true;
    JellyfinAccess::Server s;
    return JellyfinAccess::get(r.sourceId, s) && (s.quality.isEmpty() || s.quality == QLatin1String("original"));
}

// Gain for a track without ReplayGain tags: from the analysed loudness to the
// ReplayGain 2 reference (-18 LUFS), else the old fixed -6 dB.
double Player::fallbackGain(qint64 id)
{
    if (m_loudnessMatch && id > 0)
        if (AnalysisService *a = AnalysisService::instance()) {
            const AnalysisService::Features f = a->features(id);
            if (f.valid && f.loudness > -60.0)
                return std::clamp(-18.0 - f.loudness, -14.0, 8.0);
        }
    return -6.0;
}

void Player::onAboutToFinish(GstElement *playbin, void *data)
{
    auto *d = static_cast<Deck *>(data);
    Player *self = d->owner;
    if (d->index != self->m_active)
        return;
    QMutexLocker lock(&self->m_mutex);
    if (self->m_nextUri.isEmpty())
        return;
    g_object_set(playbin, "uri", self->m_nextUri.constData(), nullptr);
    d->expectAdvance = true;
}

int Player::onBusSync(GstBus *, GstMessage *msg, void *data)
{
    auto *d = static_cast<Deck *>(data);
    Player *self = d->owner;
    const bool isActive = d->index == self->m_active;
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ELEMENT: {
        const GstStructure *st = gst_message_get_structure(msg);
        if (lcMix().isDebugEnabled() && st && gst_structure_has_name(st, "level")) {
            static thread_local qint64 last[2] = {0, 0};
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - last[d->index] >= 100) {
                last[d->index] = now;
                const GValue *v = gst_structure_get_value(st, "rms");
                double db = -100;
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                if (v && GST_VALUE_HOLDS_LIST(v) && gst_value_list_get_size(v) > 0) {
                    db = g_value_get_double(gst_value_list_get_value(v, 0));
                } else if (v && G_VALUE_HOLDS_BOXED(v)) {
                    auto *arr = static_cast<GValueArray *>(g_value_get_boxed(v));
                    if (arr && arr->n_values > 0)
                        db = g_value_get_double(g_value_array_get_nth(arr, 0));
                }
G_GNUC_END_IGNORE_DEPRECATIONS
                gint64 pos = -1;
                gst_element_query_position(d->playbin, GST_FORMAT_TIME, &pos);
                qCDebug(lcMix, "level deck %d%s pos %lld ms rms %.1f dB", d->index, isActive ? "*" : " ",
                        (long long)(pos / GST_MSECOND), db);
            }
        }
        if (!isActive)
            break;
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
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_STATE_CHANGED && GST_MESSAGE_SRC(msg) != GST_OBJECT(d->playbin))
            break;
        gst_message_ref(msg);
        // A failing stream usually posts several errors in a row; only the first
        // one of the current load counts.
        const int gen = d->gen.load();
        const int index = d->index;
        QMetaObject::invokeMethod(self, [self, msg, gen, index] {
            // Messages of a stream the deck has already left (cancelled, failed, stopped) are stale.
            if (gen == self->m_deck[index].gen.load())
                self->handleMessage(index, msg);
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

void Player::handleMessage(int deckIndex, GstMessage *msg)
{
    Deck &d = m_deck[deckIndex];
    if (deckIndex != m_active) {
        // The second deck: either preparing the next track or fading out.
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ASYNC_DONE:
            if (m_prepared == 1 && m_plan.nextStartMs > 0
                && gst_element_seek_simple(d.playbin, GST_FORMAT_TIME, GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                                           m_plan.nextStartMs * GST_MSECOND)) {
                m_prepared = 2; // ready after the seek's ASYNC_DONE
            } else if (m_prepared == 1 || m_prepared == 2) {
                if (m_prepared == 1)
                    m_plan.nextStartMs = 0; // no seek (none needed, or not possible): starts at the beginning
                m_prepared = 3;
                qCDebug(lcMix) << "deck" << deckIndex << "ready";
                // Ready: the fade-out of the playing track can be set up in advance (sample-exact start).
                if (m_plan.valid && !m_plan.gapless && m_state != 0) {
                    setCurve(active(), fadeOutCurve(m_plan.startMs, m_plan.outMs, m_plan.holdOut));
                    m_curveOnActive = true;
                }
                pollPosition();
            }
            break;
        case GST_MESSAGE_EOS:
            if (deckIndex == m_tail)
                stopTail();
            break;
        case GST_MESSAGE_ERROR: {
            GError *err = nullptr;
            gst_message_parse_error(msg, &err, nullptr);
            qWarning() << "Wiedergabefehler (zweites Deck):" << (err ? err->message : "");
            g_clear_error(&err);
            ++d.gen;
            if (deckIndex == m_tail) {
                stopTail();
            } else {
                // Not again for this track; the playing one keeps its level and simply ends.
                m_prepareFailedId = m_preparedId;
                cancelPrepared();
            }
            break;
        }
        default:
            break;
        }
        return;
    }

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
            if (!d.expectAdvance) {
                m_switching = false;
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
        if (gst_element_query_duration(d.playbin, GST_FORMAT_TIME, &dur) && dur > 0) {
            const qint64 old = m_duration;
            m_duration = dur / GST_MSECOND;
            emit durationChanged();
            if (std::llabs(old - m_duration) > 1000 && m_transition != 0)
                plan(); // the tags were wrong about the length
        }
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ASYNC_DONE && m_pendingSeek >= 0) {
            const qint64 ms = m_pendingSeek;
            m_pendingSeek = -1;
            gst_element_seek_simple(d.playbin, GST_FORMAT_TIME, GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                                    ms * GST_MSECOND);
            m_position = m_lastPollPos = ms;
            emit positionChanged();
        }
        break;
    }
    case GST_MESSAGE_STREAM_START: {
        const bool advanced = d.expectAdvance.exchange(false);
        if (advanced) {
            // Gapless hand-over: the next track is already playing.
            qCDebug(lcMix) << "gapless hand-over on deck" << deckIndex << (d.controlled ? "(fader still on a curve!)" : "");
            finishTrack(false);
            m_handingOver = true;
            m_queue->advance();
            m_handingOver = false;
            d.gainNow = d.gainNext.load();
            updateMeta(m_queue->currentId());
            d.trackId = m_playingId;
            m_position = m_lastPollPos = 0;
            emit positionChanged();
        }
        break;
    }
    case GST_MESSAGE_EOS: {
        finishTrack(false);
        cancelPrepared();
        // No gapless hand-over armed (transitions on, or the blend could not
        // start in time): continue with the next track the plain way.
        if (m_queue->nextId() > 0 && m_queue->advance()) {
            load(m_queue->currentId(), true);
            break;
        }
        gst_element_set_state(d.playbin, GST_STATE_READY);
        m_position = 0;
        emit positionChanged();
        setState(0);
        break;
    }
    case GST_MESSAGE_ERROR: {
        GError *err = nullptr;
        gchar *dbg = nullptr;
        gst_message_parse_error(msg, &err, &dbg);
        if (TrackCache::instance().get(m_playingId).isRemote())
            m_error = tr("Titel vom Jellyfin-Server konnte nicht geladen werden. Ist der Server erreichbar?");
        else
            m_error = tr("Wiedergabefehler: %1").arg(QString::fromUtf8(err ? err->message : "unbekannt"));
        qWarning() << "Wiedergabefehler:" << (err ? err->message : "") << (dbg ? dbg : "");
        ++d.gen; // ignore the rest of this stream's errors
        g_clear_error(&err);
        g_free(dbg);
        emit errorChanged();
        gst_element_set_state(d.playbin, GST_STATE_READY);
        // Skip unreadable files, but never spin through a whole broken queue.
        if (++m_consecutiveErrors < 5 && m_queue->advance()) {
            load(m_queue->currentId(), m_wantPlay);
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
    if (active().level)
        g_object_set(active().level, "post-messages", on ? TRUE : FALSE, nullptr);
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
    for (Deck &d : m_deck)
        for (GstElement *e : {d.spectrumRg, d.spectrumPlain, d.levelRg, d.levelPlain})
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
    m_prepareFailedId = 0;
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

    if (!m_waveform.isEmpty()) {
        m_waveform.clear();
        emit waveformChanged();
    }
    if (AnalysisService *a = AnalysisService::instance()) {
        // The next one first: requests are served newest first, so the playing track wins.
        const qint64 next = m_queue->nextId();
        if (next > 0 && next != id)
            a->request(next, false);
        a->request(id, m_waveEnabled);
    }
    updateTrackInfo();

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

void Player::updateTrackInfo()
{
    double bpm = 0;
    QString key;
    if (AnalysisService *a = AnalysisService::instance()) {
        const AnalysisService::Features f = a->features(m_playingId);
        if (f.hasTempo())
            bpm = f.bpm;
        key = a->keyText(m_playingId);
    }
    m_bpm = bpm;
    m_key = key;
    emit trackInfoChanged(); // also the format part of formatInfo
}

QString Player::formatInfo() const
{
    QStringList parts;
    if (!m_format.isEmpty())
        parts << m_format;
    if (m_bpm > 0)
        parts << tr("%1 BPM").arg(qRound(m_bpm));
    if (!m_key.isEmpty())
        parts << m_key.section(QStringLiteral(" · "), 0, 0);
    return parts.join(QStringLiteral(" · "));
}

void Player::setWaveformEnabled(bool on)
{
    if (on == m_waveEnabled)
        return;
    m_waveEnabled = on;
    if (on && m_playingId > 0) {
        if (AnalysisService *a = AnalysisService::instance())
            a->request(m_playingId, true);
    } else if (!on) {
        m_waveform.clear();
    }
    emit waveformChanged();
}

// ---------------------------------------------------------------- transitions

void Player::armNext()
{
    if (!active().playbin || m_handingOver)
        return; // during a hand-over the queue moves before the track does: updateMeta re-arms
    plan();
    const qint64 next = m_queue->nextId();
    // Gapless hand-over on the playing deck (also used for album runs when blending is on).
    const bool gapless = next > 0 && (m_transition == 0 || (m_plan.valid && m_plan.gapless));
    const QByteArray uri = gapless ? uriFor(next) : QByteArray();
    active().gainNext = fallbackGain(next);
    QMutexLocker lock(&m_mutex);
    m_nextId = next;
    m_nextUri = uri;
}

void Player::plan()
{
    Plan p;
    const qint64 cur = m_playingId, next = m_queue->nextId();
    if (cur <= 0 || next <= 0) {
        cancelPrepared();
        m_plan = p;
        emit planChanged();
        return;
    }
    p.valid = true;
    p.nextId = next;
    const TrackRow a = TrackCache::instance().get(cur);
    const TrackRow b = TrackCache::instance().get(next);
    const bool sameAlbum = !a.album.isEmpty() && a.album.compare(b.album, Qt::CaseInsensitive) == 0
                           && a.aa.compare(b.aa, Qt::CaseInsensitive) == 0
                           && ((b.discNo == a.discNo && b.trackNo == a.trackNo + 1) || (b.discNo == a.discNo + 1 && b.trackNo == 1));
    if (m_transition == 0 || next == cur) {
        p.reason = m_transition == 0 ? QString() : tr("Lückenlos");
    } else if (m_albumGapless && sameAlbum) {
        p.reason = tr("Lückenlos (Album)");
    } else {
        p.gapless = false;
        AnalysisService *svc = AnalysisService::instance();
        const AnalysisService::Features fa = svc ? svc->features(cur) : AnalysisService::Features();
        const AnalysisService::Features fb = svc ? svc->features(next) : AnalysisService::Features();
        const qint64 durA = m_duration > 0 ? m_duration : a.durationMs;
        const qint64 base = qint64(m_fadeSec) * 1000;
        const qint64 endA = (m_skipSilence && fa.valid && fa.endMs > 0) ? std::min(fa.endMs, durA) : durA;
        const qint64 startB = (m_skipSilence && fb.valid && canSeek(next)) ? fb.startMs : 0;
        qint64 len = base;
        if (m_transition == 2) {
            const bool naturalOut = fa.valid && endA - fa.outroMs >= 2500;
            const bool naturalIn = fb.valid && fb.introMs - fb.startMs >= 2000;
            bool tempoMatch = false;
            if (fa.hasTempo() && fb.hasTempo()) {
                double ratio = fb.bpm / fa.bpm;
                while (ratio > 1.5)
                    ratio /= 2;
                while (ratio < 0.75)
                    ratio *= 2;
                tempoMatch = std::fabs(ratio - 1.0) <= 0.06;
                len = tempoMatch ? std::min<qint64>(base * 3 / 2, 12000) : std::min<qint64>(base, 4000);
            }
            if (naturalOut) {
                // Let it fade by itself: the next track comes in during the fade.
                p.startMs = std::max(fa.outroMs, endA - 12000);
                p.outMs = endA - p.startMs;
                p.holdOut = true;
            } else {
                p.outMs = std::min(len, std::max<qint64>(1000, durA / 4));
                p.startMs = endA - p.outMs;
                if (fa.hasTempo() && p.outMs >= 2000) {
                    // Start the blend on a beat of the outgoing track.
                    const double beat = 60000.0 / fa.bpm;
                    const double k = std::round((p.startMs - fa.beatMs) / beat);
                    const qint64 snapped = qint64(fa.beatMs + k * beat);
                    if (std::llabs(snapped - p.startMs) < beat && snapped + 1000 < endA) {
                        p.startMs = snapped;
                        p.outMs = endA - snapped;
                    }
                }
            }
            p.nextStartMs = startB;
            p.inMs = naturalIn ? 0 : (p.holdOut ? std::min<qint64>(2000, p.outMs) : p.outMs);
            p.reason = naturalOut ? tr("Automix: in das Ausblenden hinein")
                     : tempoMatch ? tr("Automix: passendes Tempo, %1 s").arg((p.outMs + 500) / 1000)
                     : tr("Automix: %1 s").arg((p.outMs + 500) / 1000);
            if (!fa.valid || !fb.valid)
                p.reason = tr("Überblenden, %1 s (Analyse folgt)").arg((p.outMs + 500) / 1000);
        } else {
            p.outMs = std::min(base, std::max<qint64>(1000, durA / 4));
            p.startMs = endA - p.outMs;
            p.nextStartMs = startB;
            p.inMs = p.outMs;
            p.reason = tr("Überblenden, %1 s").arg((p.outMs + 500) / 1000);
        }
        p.startMs = std::max<qint64>(0, p.startMs);
        qCDebug(lcMix) << "plan input: A" << (fa.valid ? "analysed" : "-") << fa.bpm << fa.bpmConf << "end" << endA << "outro" << fa.outroMs
                       << "| B" << (fb.valid ? "analysed" : "-") << fb.bpm << fb.bpmConf << "start" << fb.startMs << "intro" << fb.introMs;
    }
    // The playing deck already took the next track gapless (about-to-finish): stays so.
    if (active().expectAdvance && !p.gapless) {
        p.gapless = true;
        p.reason = tr("Lückenlos");
    }
    // A prepared deck that no longer fits the plan is dropped (it prepares again).
    if (m_prepared != 0 && (p.gapless || m_preparedId != p.nextId || p.nextStartMs != m_plan.nextStartMs))
        cancelPrepared();
    else if (m_curveOnActive && (p.startMs != m_plan.startMs || p.outMs != m_plan.outMs || p.holdOut != m_plan.holdOut)) {
        setCurve(active(), fadeOutCurve(p.startMs, p.outMs, p.holdOut));
    }
    m_plan = p;
    qCDebug(lcMix) << "plan" << cur << "->" << next << (p.gapless ? "gapless" : "blend") << "start" << p.startMs << "out" << p.outMs
                   << (p.holdOut ? "hold" : "") << "next from" << p.nextStartMs << "in" << p.inMs << p.reason;
    emit planChanged();
}

QString Player::nextTransition() const
{
    return m_plan.valid ? m_plan.reason : QString();
}

void Player::prepareNext()
{
    if (m_prepared != 0 || m_tail >= 0 || !m_plan.valid || m_plan.gapless || m_plan.nextId == m_prepareFailedId
        || active().expectAdvance)
        return;
    Deck &d = other();
    if (!d.playbin)
        return;
    const QByteArray uri = uriFor(m_plan.nextId);
    if (uri.isEmpty())
        return;
    d.expectAdvance = false;
    ++d.gen;
    gst_element_set_state(d.playbin, GST_STATE_READY);
    selectFilterBin(d);
    d.gainNow = fallbackGain(m_plan.nextId);
    if (d.rg)
        g_object_set(d.rg, "fallback-gain", d.gainNow.load(), nullptr);
    setFader(d, m_plan.inMs > 0 ? 0.0 : 1.0);
    g_object_set(d.playbin, "uri", uri.constData(), nullptr);
    d.trackId = m_plan.nextId;
    m_preparedId = m_plan.nextId;
    m_prepared = 1;
    qCDebug(lcMix) << "prepare deck" << d.index << "track" << m_plan.nextId << "at" << m_plan.nextStartMs << "ms";
    gst_element_set_state(d.playbin, GST_STATE_PAUSED);
}

void Player::cancelPrepared()
{
    if (m_curveOnActive) {
        setFader(active(), 1.0);
        m_curveOnActive = false;
    }
    if (m_prepared == 0)
        return;
    Deck &d = other();
    ++d.gen;
    gst_element_set_state(d.playbin, GST_STATE_NULL);
    m_prepared = 0;
    m_preparedId = 0;
}

void Player::startBlend()
{
    Deck &from = active();
    Deck &to = other();
    m_prepared = 0;
    m_preparedId = 0;
    const qint64 posA = m_position;
    // Fade-out of the playing track: installed already when the next deck got
    // ready; if we are late (seeked into the blend), start it from here.
    if (!m_curveOnActive || posA > m_plan.startMs + 200) {
        const qint64 start = std::max(posA, m_plan.startMs);
        const qint64 len = std::max<qint64>(300, m_plan.startMs + m_plan.outMs - start);
        setCurve(from, fadeOutCurve(start, len, m_plan.holdOut && start == m_plan.startMs));
    }
    if (m_plan.inMs > 0) {
        setCurve(to, fadeInCurve(m_plan.nextStartMs, m_plan.inMs));
        m_fadeInEndMs = m_plan.nextStartMs + m_plan.inMs + 300;
    } else {
        setFader(to, 1.0);
    }
    g_object_set(to.playbin, "volume", m_muted ? 0.0 : m_volume * m_volume * m_volume, nullptr);
    gst_element_set_state(to.playbin, GST_STATE_PLAYING);
    qCDebug(lcMix) << "blend: deck" << from.index << "at" << posA << "ms fades out, deck" << to.index << "starts";

    // Hand over: the new track is "the" track from now on.
    finishTrack(false);
    m_tail = from.index;
    m_tailEndMs = std::max(posA, m_plan.startMs) + m_plan.outMs + 300;
    m_curveOnActive = false;
    m_active = to.index;
    for (Deck *d : {&from, &to}) {
        if (d->spectrum)
            g_object_set(d->spectrum, "post-messages", (m_spectrumEnabled && d == &to) ? TRUE : FALSE, nullptr);
        if (d->level)
            g_object_set(d->level, "post-messages", (m_levelsEnabled && d == &to) || lcMix().isDebugEnabled() ? TRUE : FALSE, nullptr);
    }
    m_handingOver = true;
    m_queue->advance();
    m_handingOver = false;
    m_wantPlay = true;
    m_position = m_lastPollPos = m_plan.nextStartMs;
    updateMeta(m_queue->currentId());
    to.trackId = m_playingId;
    // The deck prerolled while it was not the playing one: take its real length now.
    gint64 dur = 0;
    if (gst_element_query_duration(to.playbin, GST_FORMAT_TIME, &dur) && dur > 0
        && std::llabs(dur / GST_MSECOND - m_duration) > 1000) {
        m_duration = dur / GST_MSECOND;
        emit durationChanged();
        plan();
    }
    emit positionChanged();
    emit blendingChanged();
    setState(1);
}

void Player::stopTail()
{
    if (m_tail < 0)
        return;
    Deck &d = m_deck[m_tail];
    qCDebug(lcMix) << "tail deck" << d.index << "stopped";
    m_tail = -1;
    ++d.gen;
    gst_element_set_state(d.playbin, GST_STATE_NULL); // frees its decoders until the next blend
    setFader(d, 1.0);
    emit blendingChanged(); // the next transition can be prepared from now on (see pollPosition)
}

void Player::setFader(Deck &d, double value)
{
    if (!d.fader)
        return;
    if (d.controlled) {
        if (GstControlBinding *b = gst_object_get_control_binding(GST_OBJECT(d.fader), "volume")) {
            gst_object_remove_control_binding(GST_OBJECT(d.fader), b);
            gst_object_unref(b);
        }
        if (d.ctl)
            gst_timed_value_control_source_unset_all(GST_TIMED_VALUE_CONTROL_SOURCE(d.ctl));
        d.controlled = false;
    }
    g_object_set(d.fader, "volume", value, nullptr);
}

// The fader follows `points` (stream time in ms -> gain) sample-exactly.
void Player::setCurve(Deck &d, const QVector<QPair<qint64, double>> &points)
{
    if (!d.fader || !d.ctl || points.isEmpty())
        return;
    auto *tv = GST_TIMED_VALUE_CONTROL_SOURCE(d.ctl);
    gst_timed_value_control_source_unset_all(tv);
    for (const auto &p : points)
        gst_timed_value_control_source_set(tv, GstClockTime(std::max<qint64>(0, p.first)) * GST_MSECOND, p.second);
    if (!d.controlled) {
        gst_object_add_control_binding(GST_OBJECT(d.fader),
                                       gst_direct_control_binding_new_absolute(GST_OBJECT(d.fader), "volume", d.ctl));
        d.controlled = true;
    }
    // Before the first point the property value applies.
    g_object_set(d.fader, "volume", points.first().second, nullptr);
}

// ---------------------------------------------------------------- loading

bool Player::load(qint64 id, bool startPlaying, qint64 seekMs)
{
    Deck &d = active();
    if (!d.playbin || id <= 0)
        return false;
    const QByteArray uri = uriFor(id);
    if (uri.isEmpty())
        return false;
    stopTail();
    cancelPrepared();
    d.expectAdvance = false;
    m_switching = true;
    ++d.gen; // whatever the previous stream still has queued is stale now
    gst_element_set_state(d.playbin, GST_STATE_READY);
    setFader(d, 1.0);
    d.gainNow = fallbackGain(id);
    if (d.rg)
        g_object_set(d.rg, "fallback-gain", d.gainNow.load(), nullptr);
    g_object_set(d.playbin, "uri", uri.constData(), nullptr);
    d.trackId = id;
    m_wantPlay = startPlaying;
    m_pendingSeek = seekMs > 0 ? seekMs : -1;
    updateMeta(id);
    m_position = std::max<qint64>(0, seekMs);
    emit positionChanged();
    gst_element_set_state(d.playbin, startPlaying ? GST_STATE_PLAYING : GST_STATE_PAUSED);
    m_error.clear();
    emit errorChanged();
    return true;
}

void Player::finishTrack(bool skipped)
{
    updatePosition();
    const qint64 id = m_playingId, listened = m_listenedMs, dur = m_duration;
    if (const int kind = recordPlay(id, listened, dur, skipped))
        emit trackFinished(id, listened, dur, kind == 2);
    m_listenedMs = 0;
}

void Player::playCurrent(bool interruptIsSkip)
{
    if (m_state != 0)
        finishTrack(interruptIsSkip);
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
    if (!active().playbin)
        return;
    if (m_state == 2) {
        gst_element_set_state(active().playbin, GST_STATE_PLAYING);
        if (m_tail >= 0)
            gst_element_set_state(m_deck[m_tail].playbin, GST_STATE_PLAYING);
    } else if (m_state == 0) {
        if (m_queue->currentId() > 0)
            load(m_queue->currentId(), true);
    }
}

void Player::pause()
{
    if (active().playbin && m_state == 1) {
        gst_element_set_state(active().playbin, GST_STATE_PAUSED);
        if (m_tail >= 0) // the fading track pauses with it and continues the fade later
            gst_element_set_state(m_deck[m_tail].playbin, GST_STATE_PAUSED);
    }
}

void Player::stop()
{
    if (!active().playbin)
        return;
    finishTrack(true);
    stopTail();
    cancelPrepared();
    m_switching = false;
    gst_element_set_state(active().playbin, GST_STATE_READY);
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
    if (!active().playbin)
        return;
    if (m_state == 0) {
        if (m_queue->currentId() > 0)
            load(m_queue->currentId(), true, ms);
        return;
    }
    if (active().controlled && !m_curveOnActive)
        setFader(active(), 1.0); // leaving a fade-in: full level from here
    gst_element_seek_simple(active().playbin, GST_FORMAT_TIME, GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                            ms * GST_MSECOND);
    m_position = m_lastPollPos = ms;
    emit positionChanged();
    emit seeked(ms);
    pollPosition();
}

void Player::updatePosition()
{
    if (!active().playbin || m_state == 0)
        return;
    gint64 pos = 0;
    if (gst_element_query_position(active().playbin, GST_FORMAT_TIME, &pos) && pos >= 0) {
        const qint64 ms = pos / GST_MSECOND;
        const qint64 delta = ms - m_lastPollPos;
        if (delta > 0 && delta < 2000)
            m_listenedMs += delta; // only real listening time, seeks don't count
        if (m_listenedMs > 1000)
            m_consecutiveErrors = 0; // it really plays (a stream may start and fail right after)
        m_lastPollPos = ms;
        if (ms != m_position) {
            m_position = ms;
            emit positionChanged();
        }
    }
}

void Player::pollPosition()
{
    if (!active().playbin || m_state == 0)
        return;
    updatePosition();
    // The fade-in of the playing deck is over: back to a plain fader (a later
    // gapless track or a seek back must not fade in again).
    if (active().controlled && !m_curveOnActive && m_position >= m_fadeInEndMs)
        setFader(active(), 1.0);
    if (lcMix().isDebugEnabled() && m_tail >= 0) {
        double va = -1, vb = -1;
        if (active().fader)
            g_object_get(active().fader, "volume", &va, nullptr);
        if (m_deck[m_tail].fader)
            g_object_get(m_deck[m_tail].fader, "volume", &vb, nullptr);
        qCDebug(lcMix, "faders: active %.3f (ctl %d), tail %.3f (ctl %d)", va, active().controlled, vb, m_deck[m_tail].controlled);
    }
    // The fading deck: done when its fade is over.
    if (m_tail >= 0) {
        gint64 tp = 0;
        if (gst_element_query_position(m_deck[m_tail].playbin, GST_FORMAT_TIME, &tp) && tp / GST_MSECOND >= m_tailEndMs)
            stopTail();
    }
    // Blend ahead: prepare the next track in time, start the blend on the dot.
    int interval = 250;
    if (m_state == 1 && m_plan.valid && !m_plan.gapless && m_tail < 0) {
        const bool remote = TrackCache::instance().get(m_plan.nextId).isRemote();
        const qint64 lead = remote ? 20000 : 6000;
        const qint64 toStart = m_plan.startMs - m_position;
        if (m_prepared == 0 && toStart <= lead)
            prepareNext();
        if (m_prepared == 3 && toStart <= 0)
            startBlend();
        else if (toStart < 3000)
            interval = 40;
    }
    if (m_posTimer.interval() != interval)
        m_posTimer.setInterval(interval);
}

// ---------------------------------------------------------------- volume & settings

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
    // Cubic curve: the slider feels linear to the ear.
    const double linear = m_muted ? 0.0 : m_volume * m_volume * m_volume;
    for (Deck &d : m_deck)
        if (d.playbin)
            g_object_set(d.playbin, "volume", linear, nullptr);
}

void Player::setReplayGain(int mode)
{
    mode = std::clamp(mode, 0, 2);
    if (mode == m_rgMode)
        return;
    const bool chainChanges = (mode == 0) != (m_rgMode == 0);
    m_rgMode = mode;
    applyReplayGain();
    if (chainChanges && active().playbin) {
        // Swap the filter chain: needs a stopped pipeline, then resume where we were.
        const int wasState = m_state;
        const qint64 pos = m_position;
        const qint64 id = m_playingId;
        stopTail();
        cancelPrepared();
        m_switching = true;
        for (Deck &d : m_deck)
            if (d.playbin) {
                gst_element_set_state(d.playbin, GST_STATE_NULL);
                selectFilterBin(d);
            }
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
    for (Deck &d : m_deck)
        if (d.rgRg)
            g_object_set(d.rgRg, "album-mode", m_rgMode == 2 ? TRUE : FALSE, "pre-amp", m_preamp, "headroom", 0.0, nullptr);
}

void Player::setSpectrumEnabled(bool on)
{
    if (on == m_spectrumEnabled)
        return;
    m_spectrumEnabled = on;
    if (active().spectrum)
        g_object_set(active().spectrum, "post-messages", on ? TRUE : FALSE, nullptr);
    if (!on) {
        m_bars.fill(0.f, Bars);
        m_spectrumOut.clear();
        emit spectrumChanged();
    }
    emit spectrumEnabledChanged();
}

void Player::setTransition(int mode)
{
    mode = std::clamp(mode, 0, 2);
    if (mode == m_transition)
        return;
    m_transition = mode;
    QSettings().setValue(QStringLiteral("player/transition"), mode);
    armNext();
    emit settingsChanged();
}

void Player::setFadeSeconds(int s)
{
    s = std::clamp(s, 1, 15);
    if (s == m_fadeSec)
        return;
    m_fadeSec = s;
    QSettings().setValue(QStringLiteral("player/fadeSeconds"), s);
    armNext();
    emit settingsChanged();
}

void Player::setAlbumGapless(bool on)
{
    if (on == m_albumGapless)
        return;
    m_albumGapless = on;
    QSettings().setValue(QStringLiteral("player/albumGapless"), on);
    armNext();
    emit settingsChanged();
}

void Player::setSkipSilence(bool on)
{
    if (on == m_skipSilence)
        return;
    m_skipSilence = on;
    QSettings().setValue(QStringLiteral("player/skipSilence"), on);
    armNext();
    emit settingsChanged();
}

void Player::setLoudnessMatch(bool on)
{
    if (on == m_loudnessMatch)
        return;
    m_loudnessMatch = on;
    QSettings().setValue(QStringLiteral("player/loudnessMatch"), on);
    armNext();
    emit settingsChanged();
}

// ---------------------------------------------------------------- session

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
