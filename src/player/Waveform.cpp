#include "player/Waveform.h"

#include "core/Paths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QThread>
#include <QUrl>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <gst/gst.h>

namespace {

constexpr int Rate = 8000;          // decode rate: plenty for an envelope, cheap
constexpr int ChunkSamples = 80;    // 10 ms per raw value
constexpr quint32 CacheMagic = 0x57415631; // "WAV1"

struct Collect
{
    QVector<float> raw;   // peak per 10 ms
    float cur = 0.f;
    int n = 0;
};

GstPadProbeReturn onBuffer(GstPad *, GstPadProbeInfo *info, gpointer data)
{
    auto *c = static_cast<Collect *>(data);
    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    GstMapInfo map;
    if (buf && gst_buffer_map(buf, &map, GST_MAP_READ)) {
        const auto *s = reinterpret_cast<const qint16 *>(map.data);
        const gsize count = map.size / 2;
        for (gsize i = 0; i < count; ++i) {
            const float v = std::abs(float(s[i])) / 32768.f;
            if (v > c->cur)
                c->cur = v;
            if (++c->n >= ChunkSamples) {
                c->raw.push_back(c->cur);
                c->cur = 0.f;
                c->n = 0;
            }
        }
        gst_buffer_unmap(buf, &map);
    }
    return GST_PAD_PROBE_OK;
}

void onPadAdded(GstElement *, GstPad *pad, gpointer sinkElement)
{
    GstPad *sinkPad = gst_element_get_static_pad(GST_ELEMENT(sinkElement), "sink");
    if (!gst_pad_is_linked(sinkPad)) {
        GstCaps *caps = gst_pad_get_current_caps(pad);
        if (!caps)
            caps = gst_pad_query_caps(pad, nullptr);
        const GstStructure *st = caps ? gst_caps_get_structure(caps, 0) : nullptr;
        if (st && g_str_has_prefix(gst_structure_get_name(st), "audio/"))
            gst_pad_link(pad, sinkPad);
        if (caps)
            gst_caps_unref(caps);
    }
    gst_object_unref(sinkPad);
}

} // namespace

Waveform::Waveform(QObject *parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_pool.setThreadPriority(QThread::LowestPriority);
}

Waveform::~Waveform()
{
    m_cancel = true;
    m_pool.clear();
    m_pool.waitForDone(3000);
}

QString Waveform::cacheFile(const QString &path, qint64 size, qint64 mtime)
{
    const QByteArray key = path.toUtf8() + '|' + QByteArray::number(size) + '|' + QByteArray::number(mtime);
    const QString dir = Paths::cacheDir() + QStringLiteral("/waves");
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex()) + QStringLiteral(".wav1");
}

QVector<float> Waveform::loadCache(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    if (data.size() != int(sizeof(quint32) + Buckets))
        return {};
    quint32 magic;
    memcpy(&magic, data.constData(), sizeof magic);
    if (magic != CacheMagic)
        return {};
    QVector<float> v(Buckets);
    for (int i = 0; i < Buckets; ++i)
        v[i] = quint8(data[int(sizeof magic) + i]) / 255.f;
    return v;
}

void Waveform::saveCache(const QString &file, const QVector<float> &v)
{
    QByteArray data;
    data.resize(int(sizeof(quint32)) + Buckets);
    memcpy(data.data(), &CacheMagic, sizeof CacheMagic);
    for (int i = 0; i < Buckets; ++i)
        data[int(sizeof(quint32)) + i] = char(quint8(std::lround(std::clamp(v.value(i), 0.f, 1.f) * 255.f)));
    QFile f(file + QStringLiteral(".tmp"));
    if (f.open(QIODevice::WriteOnly) && f.write(data) == data.size()) {
        f.close();
        QFile::remove(file);
        f.rename(file);
    }
}

QVector<float> Waveform::analyse(const QString &path, const std::atomic<bool> &cancel)
{
    GstElement *pipe = gst_pipeline_new("waveform");
    GstElement *src = gst_element_factory_make("uridecodebin", nullptr);
    GstElement *conv = gst_element_factory_make("audioconvert", nullptr);
    GstElement *res = gst_element_factory_make("audioresample", nullptr);
    GstElement *filter = gst_element_factory_make("capsfilter", nullptr);
    GstElement *sink = gst_element_factory_make("fakesink", nullptr);
    if (!pipe || !src || !conv || !res || !filter || !sink) {
        for (GstElement *e : {pipe, src, conv, res, filter, sink})
            if (e)
                gst_object_unref(e);
        return {};
    }
    GstCaps *caps = gst_caps_new_simple("audio/x-raw", "format", G_TYPE_STRING, "S16LE",
                                        "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, Rate, nullptr);
    g_object_set(filter, "caps", caps, nullptr);
    gst_caps_unref(caps);
    g_object_set(sink, "sync", FALSE, nullptr);
    const QByteArray uri = QUrl::fromLocalFile(path).toEncoded();
    g_object_set(src, "uri", uri.constData(), nullptr);
    // Never open video decoders for files with embedded pictures.
    GstCaps *audioOnly = gst_caps_from_string("audio/x-raw");
    g_object_set(src, "caps", audioOnly, nullptr);
    gst_caps_unref(audioOnly);

    gst_bin_add_many(GST_BIN(pipe), src, conv, res, filter, sink, nullptr);
    gst_element_link_many(conv, res, filter, sink, nullptr);
    g_signal_connect(src, "pad-added", G_CALLBACK(onPadAdded), conv);

    Collect collect;
    collect.raw.reserve(60 * 100 * 6);
    GstPad *sinkPad = gst_element_get_static_pad(sink, "sink");
    gst_pad_add_probe(sinkPad, GST_PAD_PROBE_TYPE_BUFFER, onBuffer, &collect, nullptr);
    gst_object_unref(sinkPad);

    bool ok = false;
    if (gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE) {
        GstBus *bus = gst_element_get_bus(pipe);
        for (int waited = 0; waited < 600 && !cancel; ++waited) { // at most ~2 min
            GstMessage *msg = gst_bus_timed_pop_filtered(bus, 200 * GST_MSECOND,
                                                         GstMessageType(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
            if (!msg)
                continue;
            ok = GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS;
            gst_message_unref(msg);
            break;
        }
        gst_object_unref(bus);
    }
    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
    if (!ok || collect.raw.size() < 10)
        return {};

    // Downsample to Buckets: average of the 10 ms peaks (smooth, readable),
    // mixed with the bucket maximum so transients still show.
    QVector<float> out(Buckets, 0.f);
    const int n = collect.raw.size();
    for (int b = 0; b < Buckets; ++b) {
        const int a = int(qint64(b) * n / Buckets);
        const int e = std::max(a + 1, int(qint64(b + 1) * n / Buckets));
        float sum = 0.f, mx = 0.f;
        for (int i = a; i < e && i < n; ++i) {
            sum += collect.raw[i];
            mx = std::max(mx, collect.raw[i]);
        }
        out[b] = 0.65f * (sum / float(e - a)) + 0.35f * mx;
    }
    // Normalise to the loudest part and compress a little, so quiet passages
    // stay visible without flattening loud masters.
    const float top = *std::max_element(out.begin(), out.end());
    if (top <= 0.0001f)
        return QVector<float>(Buckets, 0.02f);
    for (float &v : out)
        v = std::pow(v / top, 0.8f);
    return out;
}

void Waveform::request(qint64 id, const QString &path, qint64 size, qint64 mtime)
{
    if (!m_enabled || id <= 0 || path.isEmpty())
        return;
    const QString file = cacheFile(path, size, mtime);
    const QVector<float> cached = loadCache(file);
    if (!cached.isEmpty()) {
        QVariantList list;
        list.reserve(cached.size());
        for (float v : cached)
            list.push_back(v);
        emit ready(id, list);
        return;
    }
    if (m_pending.contains(id))
        return;
    m_pending.insert(id);
    QPointer<Waveform> self(this);
    m_pool.start([self, id, path, file, cancel = &m_cancel] {
        const QVector<float> v = analyse(path, *cancel);
        if (!v.isEmpty())
            saveCache(file, v);
        QVariantList list;
        list.reserve(v.size());
        for (float x : v)
            list.push_back(x);
        QMetaObject::invokeMethod(qApp, [self, id, list] {
            if (!self)
                return;
            self->m_pending.remove(id);
            emit self->ready(id, list);
        }, Qt::QueuedConnection);
    });
}
