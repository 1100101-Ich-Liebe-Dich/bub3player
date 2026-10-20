// SPDX-License-Identifier: GPL-3.0-or-later
#include "player/AudioOutput.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QtConcurrent>

#include <cmath>
#include <gst/audio/audio.h>
#include <gst/gst.h>

namespace {

// Centre frequencies of equalizer-10bands, as shown.
const char *FreqLabels[AudioOutput::Bands] = {"31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};

struct Preset
{
    const char *name;
    double gains[AudioOutput::Bands];
};

// Gentle curves: a few dB already change a lot; the preamp keeps them from clipping.
const Preset Builtin[] = {
    {"Neutral", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"Mehr Bass", {6, 5, 4, 2, 0, 0, 0, 0, 0, 0}},
    {"Mehr Höhen", {0, 0, 0, 0, 0, 1, 2, 4, 5, 6}},
    {"Leise hören", {6, 5, 3, 0, -1, 0, 0, 2, 4, 5}},
    {"Rock", {4, 3, 2, 0, -1, -1, 1, 3, 4, 4}},
    {"Pop", {-1, 1, 3, 4, 3, 0, -1, -1, 0, 1}},
    {"Jazz", {3, 2, 1, 2, -1, -1, 0, 1, 2, 3}},
    {"Klassik", {2, 1, 0, 0, 0, 0, 0, 1, 2, 2}},
    {"Elektronik", {5, 4, 1, 0, -2, 1, 0, 1, 4, 5}},
    {"Hip-Hop", {5, 4, 2, 3, -1, -1, 1, 0, 2, 3}},
    {"Akustik", {2, 2, 1, 0, 1, 1, 2, 3, 2, 1}},
    {"Stimme und Hörbuch", {-3, -2, -1, 1, 3, 4, 4, 2, 0, -2}},
    {"Kleine Lautsprecher", {-4, -2, 0, 2, 3, 3, 2, 2, 1, 0}},
    {"Nachts (weniger Bass)", {-6, -4, -2, 0, 0, 0, 0, 0, -1, -2}},
};

// Sample format of caps: bits of the samples (not the container), float or not.
bool sampleFormat(const GstCaps *caps, int &bits, bool &isFloat, int &rate, int &channels)
{
    if (!caps || gst_caps_get_size(caps) == 0)
        return false;
    const GstStructure *s = gst_caps_get_structure(caps, 0);
    const char *format = gst_structure_get_string(s, "format");
    rate = channels = 0;
    gst_structure_get_int(s, "rate", &rate);
    gst_structure_get_int(s, "channels", &channels);
    const GstAudioFormatInfo *info = format ? gst_audio_format_get_info(gst_audio_format_from_string(format)) : nullptr;
    if (!info || GST_AUDIO_FORMAT_INFO_FORMAT(info) == GST_AUDIO_FORMAT_UNKNOWN)
        return false;
    bits = GST_AUDIO_FORMAT_INFO_DEPTH(info);
    isFloat = GST_AUDIO_FORMAT_INFO_IS_FLOAT(info);
    return true;
}

// "factory|property|value" -> the sink element.
GstElement *sinkFromKey(const QString &key)
{
    const QStringList p = key.split(QLatin1Char('|'));
    if (p.size() < 3)
        return nullptr;
    GstElement *e = gst_element_factory_make(p[0].toUtf8().constData(), nullptr);
    if (!e)
        return nullptr;
    if (!g_object_class_find_property(G_OBJECT_GET_CLASS(e), p[1].toUtf8().constData())) {
        gst_object_unref(e);
        return nullptr;
    }
    g_object_set(e, p[1].toUtf8().constData(), p.mid(2).join(QLatin1Char('|')).toUtf8().constData(), nullptr);
    return e;
}

} // namespace

AudioOutput::AudioOutput(QObject *parent) : QObject(parent)
{
    m_eqAvailable = hasElement("equalizer-10bands");
    m_alsaAvailable = hasElement("alsasink");

    QSettings s;
    m_device = s.value(QStringLiteral("audio/device")).toString();
    m_deviceName = s.value(QStringLiteral("audio/deviceName")).toString();
    m_bitPerfect = s.value(QStringLiteral("audio/bitPerfect"), false).toBool() && m_alsaAvailable;
    m_hwDevice = s.value(QStringLiteral("audio/hwDevice")).toString();
    m_hwName = s.value(QStringLiteral("audio/hwDeviceName")).toString();
    m_eqEnabled = s.value(QStringLiteral("audio/eqEnabled"), false).toBool();
    const QVariantList bands = s.value(QStringLiteral("audio/eqBands")).toList();
    for (int i = 0; i < Bands; ++i)
        m_bands[i] = std::clamp(bands.value(i).toDouble(), -12.0, 12.0);
    m_preamp = std::clamp(s.value(QStringLiteral("audio/eqPreamp"), 0.0).toDouble(), -12.0, 12.0);
    m_autoPreamp = s.value(QStringLiteral("audio/eqAutoPreamp"), true).toBool();
    m_userPresets = QJsonDocument::fromJson(s.value(QStringLiteral("audio/eqPresets")).toByteArray()).array().toVariantList();
    m_presetName = s.value(QStringLiteral("audio/eqPresetName")).toString();
    loadHwDevices();
}

bool AudioOutput::hasElement(const char *factory)
{
    GstElementFactory *f = gst_element_factory_find(factory);
    if (!f)
        return false;
    gst_object_unref(f);
    return true;
}

void AudioOutput::volumeIgnored()
{
    emit message(tr("Bit-Perfect: Die Lautstärke wird am Verstärker oder DAC geregelt"));
}

// ---------- output ----------

void AudioOutput::loadHwDevices()
{
    // ALSA devices that can play, by card id (stays the same when cards are
    // plugged in a different order). /proc/asound/cards: " 0 [PCH ]: HDA-Intel - HDA Intel PCH",
    // /proc/asound/pcm: "00-00: name : name : playback 1".
    m_hwDevices.clear();
    QHash<QString, QString> cardId, cardName;
    // Tests: HALCYON_TEST_ASOUND=DIR with "cards" and "pcm" like /proc/asound.
    const QString base = qEnvironmentVariable("HALCYON_TEST_ASOUND", QStringLiteral("/proc/asound"));
    QFile cards(base + QStringLiteral("/cards"));
    if (cards.open(QIODevice::ReadOnly)) {
        static const QRegularExpression re(QStringLiteral("^\\s*(\\d+)\\s+\\[(\\S+)\\s*\\]:\\s*.*?\\s-\\s(.*)$"));
        for (const QByteArray &line : cards.readAll().split('\n')) {
            const auto m = re.match(QString::fromUtf8(line));
            if (m.hasMatch()) {
                cardId.insert(m.captured(1), m.captured(2));
                cardName.insert(m.captured(1), m.captured(3).trimmed());
            }
        }
    }
    QFile pcm(base + QStringLiteral("/pcm"));
    if (pcm.open(QIODevice::ReadOnly)) {
        for (const QByteArray &raw : pcm.readAll().split('\n')) {
            const QString line = QString::fromUtf8(raw);
            if (!line.contains(QLatin1String("playback")))
                continue;
            const QString ids = line.section(QLatin1Char(':'), 0, 0).trimmed(); // "00-03"
            const QString card = QString::number(ids.section(QLatin1Char('-'), 0, 0).toInt());
            const QString dev = QString::number(ids.section(QLatin1Char('-'), 1, 1).toInt());
            const QString name = line.section(QLatin1Char(':'), 1, 1).trimmed();
            const QString id = cardId.value(card, card);
            m_hwDevices << QVariantMap{{QStringLiteral("key"), QStringLiteral("hw:CARD=%1,DEV=%2").arg(id, dev)},
                                       {QStringLiteral("name"), cardName.value(card, id) + QStringLiteral(" – ") + name},
                                       {QStringLiteral("detail"), QStringLiteral("hw:%1,%2").arg(card, dev)}};
        }
    }
}

void AudioOutput::refreshDevices()
{
    loadHwDevices();
    if (m_scanning) {
        emit devicesChanged(); // the ALSA list may have changed
        return;
    }
    m_scanning = true;
    emit devicesChanged();
    QPointer<AudioOutput> self(this);
    // The sound server can take a moment to answer: off the UI thread.
    (void)QtConcurrent::run([self] {
        QVariantList out;
        GstDeviceMonitor *mon = gst_device_monitor_new();
        gst_device_monitor_add_filter(mon, "Audio/Sink", nullptr);
        if (gst_device_monitor_start(mon)) {
            GList *list = gst_device_monitor_get_devices(mon);
            // One sound server is enough: its devices, not the raw ALSA ones it
            // already uses (those belong to bit-perfect).
            QHash<QString, QVariantList> byFactory;
            for (GList *l = list; l; l = l->next) {
                GstDevice *dev = GST_DEVICE(l->data);
                GstElement *e = gst_device_create_element(dev, nullptr);
                if (e) {
                    const QString factory = QString::fromUtf8(GST_OBJECT_NAME(gst_element_get_factory(e)));
                    // The property that names the device (PulseAudio/ALSA: device, PipeWire: target-object).
                    for (const char *prop : {"device", "target-object"}) {
                        GParamSpec *ps = g_object_class_find_property(G_OBJECT_GET_CLASS(e), prop);
                        if (!ps || ps->value_type != G_TYPE_STRING)
                            continue;
                        gchar *value = nullptr;
                        g_object_get(e, prop, &value, nullptr);
                        const bool found = value && *value;
                        if (found) {
                            gchar *name = gst_device_get_display_name(dev);
                            byFactory[factory] << QVariantMap{
                                {QStringLiteral("key"), QStringLiteral("%1|%2|%3").arg(factory, QLatin1String(prop), QString::fromUtf8(value))},
                                {QStringLiteral("name"), QString::fromUtf8(name)},
                                {QStringLiteral("detail"), QString::fromUtf8(value)}};
                            g_free(name);
                        }
                        g_free(value);
                        if (found)
                            break;
                    }
                    gst_object_unref(e);
                }
            }
            g_list_free_full(list, gst_object_unref);
            gst_device_monitor_stop(mon);
            // PulseAudio first: the system default (autoaudiosink) goes the same way,
            // also on PipeWire (pipewire-pulse).
            for (const char *f : {"pulsesink", "pipewiresink", "alsasink"})
                if (byFactory.contains(QLatin1String(f))) {
                    out = byFactory.value(QLatin1String(f));
                    break;
                }
        }
        gst_object_unref(mon);
        QMetaObject::invokeMethod(qApp, [self, out] {
            if (!self)
                return;
            self->m_devices = out;
            self->m_scanning = false;
            emit self->devicesChanged();
        }, Qt::QueuedConnection);
    });
}

void AudioOutput::setDevice(const QString &key, const QString &name)
{
    if (key == m_device && !m_useSystem && !m_bitPerfect)
        return;
    m_device = key;
    m_deviceName = key.isEmpty() ? QString() : name;
    m_bitPerfect = false;
    m_useSystem = false;
    m_fallback.clear();
    m_told = false;
    QSettings s;
    s.setValue(QStringLiteral("audio/device"), m_device);
    s.setValue(QStringLiteral("audio/deviceName"), m_deviceName);
    s.setValue(QStringLiteral("audio/bitPerfect"), false);
    emit outputChanged();
    emit chainChanged();
}

void AudioOutput::setHwDevice(const QString &key, const QString &name)
{
    if (key == m_hwDevice && !(m_bitPerfect && m_useSystem))
        return;
    m_hwDevice = key;
    m_hwName = name;
    QSettings s;
    s.setValue(QStringLiteral("audio/hwDevice"), m_hwDevice);
    s.setValue(QStringLiteral("audio/hwDeviceName"), m_hwName);
    if (m_bitPerfect) { // a new try with the new device
        m_useSystem = false;
        m_fallback.clear();
        m_told = false;
    }
    emit outputChanged();
    if (m_bitPerfect)
        emit chainChanged();
}

void AudioOutput::setBitPerfect(bool on)
{
    on = on && m_alsaAvailable;
    if (on && m_hwDevice.isEmpty() && !m_hwDevices.isEmpty()) {
        const QVariantMap first = m_hwDevices.first().toMap();
        m_hwDevice = first.value(QStringLiteral("key")).toString();
        m_hwName = first.value(QStringLiteral("name")).toString();
        QSettings s;
        s.setValue(QStringLiteral("audio/hwDevice"), m_hwDevice);
        s.setValue(QStringLiteral("audio/hwDeviceName"), m_hwName);
    }
    if (on && m_hwDevice.isEmpty()) {
        emit outputChanged(); // no card to talk to: the switch goes back
        return;
    }
    if (on == m_bitPerfect && !m_useSystem)
        return;
    m_bitPerfect = on;
    m_useSystem = false;
    m_fallback.clear();
    m_told = false;
    QSettings().setValue(QStringLiteral("audio/bitPerfect"), on);
    emit outputChanged();
    emit chainChanged();
}

bool AudioOutput::customSink() const
{
    if (m_useSystem)
        return false;
    return qEnvironmentVariableIsSet("HALCYON_TEST_SINK") || bitPerfectActive() || !m_device.isEmpty();
}

GstElement *AudioOutput::createSink() const
{
    if (m_useSystem)
        return nullptr;
    // Tests: any sink instead of the device, e.g.
    // HALCYON_TEST_SINK="capsfilter caps=audio/x-raw,format=S24LE ! filesink location=/tmp/out.raw"
    const QByteArray test = qgetenv("HALCYON_TEST_SINK");
    if (!test.isEmpty()) {
        GError *err = nullptr;
        GstElement *e = gst_parse_bin_from_description(test.constData(), TRUE, &err);
        if (err) {
            qWarning() << "HALCYON_TEST_SINK:" << err->message;
            g_clear_error(&err);
        }
        return e;
    }
    if (bitPerfectActive()) {
        GstElement *e = gst_element_factory_make("alsasink", nullptr);
        if (!e)
            return nullptr;
        g_object_set(e, "device", m_hwDevice.toUtf8().constData(), nullptr);
        return e;
    }
    if (!m_device.isEmpty())
        return sinkFromKey(m_device);
    return nullptr;
}

bool AudioOutput::outputFailed(bool busy)
{
    if (!customSink())
        return false;
    const bool bp = bitPerfectActive();
    m_useSystem = true;
    const QString why = busy ? tr("belegt, ein anderes Programm nutzt es") : tr("nicht angeschlossen oder nicht erreichbar");
    m_fallback = (bp ? tr("Das Gerät für Bit-Perfect ist %1.") : tr("Das Ausgabegerät ist %1.")).arg(why)
                 + QLatin1Char(' ')
                 + tr("Der Ton läuft so lange über den Systemstandard; beim nächsten Titel, den du startest, versucht es der Player wieder.");
    if (!m_told) { // once per choice, not on every new try
        m_told = true;
        emit message(tr("Ausgabegerät nicht verfügbar – der Ton läuft über den Systemstandard"));
    }
    emit outputChanged();
    return true;
}

bool AudioOutput::retryChosen()
{
    if (!m_useSystem)
        return false;
    m_useSystem = false;
    m_fallback.clear();
    emit outputChanged();
    return true;
}

QString AudioOutput::describeCaps(const GstCaps *caps)
{
    int bits = 0, rate = 0, channels = 0;
    bool isFloat = false;
    if (!sampleFormat(caps, bits, isFloat, rate, channels))
        return {};
    QStringList parts;
    parts << (isFloat ? tr("%1 Bit Gleitkomma").arg(bits) : tr("%1 Bit").arg(bits));
    if (rate > 0)
        parts << QStringLiteral("%1 kHz").arg(QLocale(QLocale::German).toString(rate / 1000.0, 'g', 4));
    if (channels == 1)
        parts << tr("Mono");
    else if (channels == 2)
        parts << tr("Stereo");
    else if (channels > 2)
        parts << tr("%1 Kanäle").arg(channels);
    return parts.join(QStringLiteral(" · "));
}

bool AudioOutput::sameSignal(const GstCaps *in, const GstCaps *out)
{
    int bitsIn = 0, rateIn = 0, chIn = 0, bitsOut = 0, rateOut = 0, chOut = 0;
    bool floatIn = false, floatOut = false;
    if (!sampleFormat(in, bitsIn, floatIn, rateIn, chIn) || !sampleFormat(out, bitsOut, floatOut, rateOut, chOut))
        return false;
    return rateIn == rateOut && chIn == chOut && floatIn == floatOut && bitsOut >= bitsIn;
}

bool AudioOutput::floatSamples(const GstCaps *caps)
{
    int bits = 0, rate = 0, channels = 0;
    bool isFloat = false;
    return sampleFormat(caps, bits, isFloat, rate, channels) && isFloat;
}

// ---------- equalizer ----------

QVariantList AudioOutput::eqBands() const
{
    QVariantList out;
    for (double b : m_bands)
        out << b;
    return out;
}

QVariantList AudioOutput::eqFrequencies() const
{
    QVariantList out;
    for (const char *f : FreqLabels)
        out << QString::fromLatin1(f);
    return out;
}

void AudioOutput::setEqEnabled(bool on)
{
    if (on == m_eqEnabled)
        return;
    m_eqEnabled = on;
    QSettings().setValue(QStringLiteral("audio/eqEnabled"), on);
    emit eqChanged();
    if (!bitPerfectActive() && m_eqAvailable)
        emit chainChanged();
}

double AudioOutput::eqEffectivePreamp() const
{
    if (!m_autoPreamp)
        return m_preamp;
    // Room for the loudest boost; neighbouring bands overlap a little, hence the extra.
    double peak = 0;
    for (double b : m_bands)
        peak = std::max(peak, b);
    return peak > 0 ? -(peak + 0.5) : 0.0;
}

void AudioOutput::setEqPreamp(double db)
{
    db = std::clamp(db, -12.0, 12.0);
    if (std::abs(db - m_preamp) < 0.01)
        return;
    m_preamp = db;
    eqTouched();
}

void AudioOutput::setEqAutoPreamp(bool on)
{
    if (on == m_autoPreamp)
        return;
    m_autoPreamp = on;
    eqTouched();
}

void AudioOutput::setBand(int band, double db)
{
    if (band < 0 || band >= Bands)
        return;
    db = std::clamp(std::round(db * 2) / 2, -12.0, 12.0); // half-dB steps
    if (std::abs(db - m_bands[band]) < 0.01)
        return;
    m_bands[band] = db;
    m_presetName.clear();
    eqTouched();
}

void AudioOutput::eqTouched()
{
    saveEq();
    emit eqChanged();
    emit eqGainsChanged();
}

void AudioOutput::saveEq()
{
    QSettings s;
    s.setValue(QStringLiteral("audio/eqBands"), eqBands());
    s.setValue(QStringLiteral("audio/eqPreamp"), m_preamp);
    s.setValue(QStringLiteral("audio/eqAutoPreamp"), m_autoPreamp);
    s.setValue(QStringLiteral("audio/eqPresetName"), m_presetName);
}

QVariantList AudioOutput::eqPresets() const
{
    QVariantList out;
    for (const Preset &p : Builtin) {
        QVariantList g;
        for (double v : p.gains)
            g << v;
        out << QVariantMap{{QStringLiteral("name"), QString::fromUtf8(p.name)}, {QStringLiteral("gains"), g},
                           {QStringLiteral("builtin"), true}};
    }
    for (const QVariant &v : m_userPresets) {
        QVariantMap m = v.toMap();
        m.insert(QStringLiteral("builtin"), false);
        out << m;
    }
    return out;
}

QString AudioOutput::eqPreset() const
{
    // The one chosen last, else the first with this curve.
    QString first;
    for (const QVariant &v : eqPresets()) {
        const QVariantMap m = v.toMap();
        const QVariantList g = m.value(QStringLiteral("gains")).toList();
        bool same = g.size() == Bands;
        for (int i = 0; same && i < Bands; ++i)
            same = std::abs(g[i].toDouble() - m_bands[i]) < 0.01;
        if (!same)
            continue;
        const QString name = m.value(QStringLiteral("name")).toString();
        if (name == m_presetName)
            return name;
        if (first.isEmpty())
            first = name;
    }
    return first;
}

void AudioOutput::applyPreset(const QString &name)
{
    for (const QVariant &v : eqPresets()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("name")).toString() != name)
            continue;
        const QVariantList g = m.value(QStringLiteral("gains")).toList();
        for (int i = 0; i < Bands; ++i)
            m_bands[i] = std::clamp(g.value(i).toDouble(), -12.0, 12.0);
        m_presetName = name;
        eqTouched();
        return;
    }
}

bool AudioOutput::savePreset(const QString &rawName)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty())
        return false;
    for (const Preset &p : Builtin)
        if (name.compare(QString::fromUtf8(p.name), Qt::CaseInsensitive) == 0) {
            emit message(tr("»%1« gibt es schon als Voreinstellung").arg(name));
            return false;
        }
    const QVariantList gains = eqBands();
    QString stored; // an own preset of that name (any case) is overwritten
    for (QVariant &v : m_userPresets) {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("name")).toString().compare(name, Qt::CaseInsensitive) == 0) {
            m.insert(QStringLiteral("gains"), gains);
            v = m;
            stored = m.value(QStringLiteral("name")).toString();
        }
    }
    if (stored.isEmpty()) {
        m_userPresets << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("gains"), gains}};
        stored = name;
    }
    m_presetName = stored;
    saveEq();
    QSettings().setValue(QStringLiteral("audio/eqPresets"),
                         QJsonDocument(QJsonArray::fromVariantList(m_userPresets)).toJson(QJsonDocument::Compact));
    emit eqChanged();
    emit message(tr("Klang »%1« gespeichert").arg(name));
    return true;
}

void AudioOutput::deletePreset(const QString &name)
{
    for (int i = 0; i < m_userPresets.size(); ++i)
        if (m_userPresets[i].toMap().value(QStringLiteral("name")).toString() == name) {
            m_userPresets.removeAt(i);
            if (m_presetName == name) {
                m_presetName.clear();
                saveEq();
            }
            QSettings().setValue(QStringLiteral("audio/eqPresets"),
                                 QJsonDocument(QJsonArray::fromVariantList(m_userPresets)).toJson(QJsonDocument::Compact));
            emit eqChanged();
            return;
        }
}

void AudioOutput::resetEq()
{
    applyPreset(QStringLiteral("Neutral"));
}
