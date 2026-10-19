#pragma once

#include <QObject>
#include <QVariantList>
#include <QVector>

typedef struct _GstElement GstElement;
typedef struct _GstCaps GstCaps;

// Where the sound goes and how it is shaped (QML singleton "Audio").
//
// Output:
//  - "Systemstandard" (default): playbin picks the sink itself, exactly as
//    before this class existed. Nothing in the pipeline changes.
//  - a device from the system's sound server (PipeWire / PulseAudio), found
//    with GstDeviceMonitor and remembered as "factory|property|value".
//  - Bit-Perfect: ALSA directly ("hw:CARD=…,DEV=…"). The decoded samples reach
//    the DAC unchanged, the sample rate follows the track. No ReplayGain, no
//    equalizer, no software volume, no blends (the device is opened by one
//    stream only). A container the device lacks gets the samples losslessly
//    (16 in 32 bit); a sample rate it lacks is converted (the status says so).
// A device that fails (unplugged, busy) falls back to the system default until
// the listener starts the next track; the choice itself is kept.
//
// Equalizer: 10 bands (equalizer-10bands) plus a preamp, only in the chain
// while switched on – off means the exact chain of before.
class AudioOutput : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)        // [{key, name, detail}]
    Q_PROPERTY(QVariantList hwDevices READ hwDevices NOTIFY devicesChanged)    // [{key, name, detail}]
    Q_PROPERTY(bool scanning READ scanning NOTIFY devicesChanged)
    Q_PROPERTY(QString device READ device NOTIFY outputChanged)                // key, "" = system default
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY outputChanged)
    Q_PROPERTY(bool bitPerfect READ bitPerfect WRITE setBitPerfect NOTIFY outputChanged)
    Q_PROPERTY(QString hwDevice READ hwDevice NOTIFY outputChanged)
    Q_PROPERTY(QString hwDeviceName READ hwDeviceName NOTIFY outputChanged)
    Q_PROPERTY(bool alsaAvailable READ alsaAvailable CONSTANT)
    Q_PROPERTY(QString fallback READ fallback NOTIFY outputChanged)            // why the chosen output is not used right now
    Q_PROPERTY(bool eqAvailable READ eqAvailable CONSTANT)
    Q_PROPERTY(bool eqEnabled READ eqEnabled WRITE setEqEnabled NOTIFY eqChanged)
    Q_PROPERTY(QVariantList eqBands READ eqBands NOTIFY eqChanged)             // 10 values, dB
    Q_PROPERTY(QVariantList eqFrequencies READ eqFrequencies CONSTANT)         // labels: "31", "62" … "16k"
    Q_PROPERTY(double eqPreamp READ eqPreamp WRITE setEqPreamp NOTIFY eqChanged)
    Q_PROPERTY(bool eqAutoPreamp READ eqAutoPreamp WRITE setEqAutoPreamp NOTIFY eqChanged)
    Q_PROPERTY(double eqEffectivePreamp READ eqEffectivePreamp NOTIFY eqChanged)
    Q_PROPERTY(QString eqPreset READ eqPreset NOTIFY eqChanged)                // name of the matching preset, "" = own curve
    Q_PROPERTY(QVariantList eqPresets READ eqPresets NOTIFY eqChanged)         // [{name, gains, builtin}]

public:
    static constexpr int Bands = 10;

    explicit AudioOutput(QObject *parent = nullptr);

    QVariantList devices() const { return m_devices; }
    QVariantList hwDevices() const { return m_hwDevices; }
    bool scanning() const { return m_scanning; }
    QString device() const { return m_device; }
    QString deviceName() const { return m_deviceName; }
    bool bitPerfect() const { return m_bitPerfect; }
    void setBitPerfect(bool on);
    QString hwDevice() const { return m_hwDevice; }
    QString hwDeviceName() const { return m_hwName; }
    bool alsaAvailable() const { return m_alsaAvailable; }
    QString fallback() const { return m_fallback; }

    bool eqAvailable() const { return m_eqAvailable; }
    bool eqEnabled() const { return m_eqEnabled; }
    void setEqEnabled(bool on);
    QVariantList eqBands() const;
    QVariantList eqFrequencies() const;
    double eqPreamp() const { return m_preamp; }
    void setEqPreamp(double db);
    bool eqAutoPreamp() const { return m_autoPreamp; }
    void setEqAutoPreamp(bool on);
    double eqEffectivePreamp() const;
    QString eqPreset() const;
    QVariantList eqPresets() const;

    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void setDevice(const QString &key, const QString &name);
    Q_INVOKABLE void setHwDevice(const QString &key, const QString &name);
    Q_INVOKABLE void setBand(int band, double db);
    Q_INVOKABLE void applyPreset(const QString &name);
    Q_INVOKABLE bool savePreset(const QString &name);
    Q_INVOKABLE void deletePreset(const QString &name);
    Q_INVOKABLE void resetEq();

    // --- for the Player ---
    // The equalizer belongs in the filter chain (on, available, not bit-perfect).
    bool chainHasEq() const { return m_eqEnabled && m_eqAvailable && !bitPerfectActive(); }
    // Bit-perfect is chosen and not given up for this session.
    bool bitPerfectActive() const { return m_bitPerfect && !m_hwDevice.isEmpty() && !m_useSystem; }
    // A new sink for one deck, or nullptr for playbin's own choice (system default).
    GstElement *createSink() const;
    bool customSink() const;
    // The chosen output failed (gone, busy): system default until retryChosen().
    // Returns false when the system default is in use already.
    bool outputFailed(bool busy);
    // Back to the chosen output after a failure (the listener starts a track).
    // Returns whether anything changed.
    bool retryChosen();
    double bandGain(int band) const { return band >= 0 && band < Bands ? m_bands[band] : 0.0; }
    // The volume was changed while bit-perfect ignores it: tell once.
    void volumeIgnored();

    static bool hasElement(const char *factory);
    static QString describeCaps(const GstCaps *caps); // "24 Bit · 96 kHz · Stereo"
    // Same rate and channels, no bits lost (a wider integer container is fine).
    static bool sameSignal(const GstCaps *in, const GstCaps *out);
    static bool floatSamples(const GstCaps *caps);

signals:
    void devicesChanged();
    void outputChanged();
    void eqChanged();
    // The pipeline has to be rebuilt (equalizer on/off, output, bit-perfect).
    void chainChanged();
    // Only gains or preamp changed: applied live.
    void eqGainsChanged();
    void message(const QString &text);

private:
    void loadHwDevices();
    void saveEq();
    void eqTouched(); // gains changed: save, signal

    QVariantList m_devices, m_hwDevices;
    bool m_scanning = false;
    QString m_device, m_deviceName;
    bool m_bitPerfect = false;
    QString m_hwDevice, m_hwName;
    bool m_alsaAvailable = false;
    bool m_useSystem = false;  // the chosen output failed: system default for now
    bool m_told = false;       // the listener heard about the failure of this choice
    QString m_fallback;

    bool m_eqAvailable = false;
    bool m_eqEnabled = false;
    double m_bands[Bands] = {};
    double m_preamp = 0.0;
    bool m_autoPreamp = true;
    QVariantList m_userPresets; // [{name, gains}]
    QString m_presetName;       // last chosen or saved preset (several may share a curve)
};
