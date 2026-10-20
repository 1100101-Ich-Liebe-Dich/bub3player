// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Settings tab "Klang": where the sound goes (device, bit-perfect), the
// equalizer and ReplayGain. Off means off: without a device and without the
// equalizer the signal path is exactly the one of before.
Column {
    id: snd
    spacing: 34
    // Format of the signal, polled while the tab is open.
    property var path: ({})
    // Bit-perfect asked for, not confirmed yet (volume down at the amplifier first).
    property bool bpArmed: false

    Component.onCompleted: Audio.refreshDevices()

    Timer {
        interval: 1000
        repeat: true
        triggeredOnStart: true
        running: snd.visible && Player.state !== 0
        onTriggered: snd.path = Player.signalPath()
    }
    Connections {
        target: Player
        function onStateChanged() { if (Player.state === 0) snd.path = ({}) }
    }

    // ---------------- output ----------------
    SettingsSection {
        width: snd.width
        title: qsTr("Ausgabe")
        text: qsTr("Systemstandard: Der Ton geht wie bisher über den Soundserver des Systems (PipeWire oder PulseAudio). Ein bestimmtes Gerät wählst du hier, ohne die Einstellungen des Systems zu ändern.")

        SettingsOption {
            width: snd.width
            label: qsTr("Ausgabegerät")
            enabled: !Audio.bitPerfect
            Row {
                spacing: 10
                HCombo {
                    id: deviceCombo
                    width: Math.min(460, snd.width - 160)
                    options: {
                        var list = [{ value: "", label: qsTr("Systemstandard") }]
                        var found = Audio.device === ""
                        for (var i = 0; i < Audio.devices.length; ++i) {
                            list.push({ value: Audio.devices[i].key, label: Audio.devices[i].name })
                            if (Audio.devices[i].key === Audio.device) found = true
                        }
                        if (!found) // chosen earlier, not there right now
                            list.push({ value: Audio.device, label: qsTr("%1 (nicht verbunden)").arg(Audio.deviceName || Audio.device) })
                        return list
                    }
                    current: Audio.device
                    onPicked: (v) => Audio.setDevice(v, snd.nameOf(Audio.devices, v, Audio.device, Audio.deviceName))
                }
                PillButton {
                    compact: true
                    anchors.verticalCenter: parent.verticalCenter
                    text: Audio.scanning ? qsTr("Suche …") : qsTr("Neu suchen")
                    iconName: "refresh"
                    enabled: !Audio.scanning
                    onClicked: Audio.refreshDevices()
                }
            }
        }

        HToggle {
            width: snd.width
            label: qsTr("Bit-Perfect: unverändert direkt an den DAC (ALSA, am Soundserver vorbei)")
            checked: Audio.bitPerfect || snd.bpArmed
            enabled: Audio.alsaAvailable && (Audio.bitPerfect || Audio.hwDevices.length > 0)
            onToggled: {
                // On: only asks for the confirmation below. Off: right away.
                snd.bpArmed = checked && !Audio.bitPerfect
                if (!checked)
                    Audio.bitPerfect = false
                checked = Qt.binding(() => Audio.bitPerfect || snd.bpArmed)
            }
        }
        // Full level at the device from the first sample: confirm first.
        Rectangle {
            visible: snd.bpArmed && !Audio.bitPerfect
            width: snd.width
            height: confirmCol.implicitHeight + 28
            radius: Math.min(12, Theme.radius + 2)
            color: Theme.alpha(Theme.accent, 0.12)
            border.width: 1
            border.color: Theme.alpha(Theme.accent, 0.6)
            Column {
                id: confirmCol
                x: 14; y: 14
                width: parent.width - 28
                spacing: 10
                Row {
                    spacing: 10
                    width: parent.width
                    Icon { name: "volume"; size: 22; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        width: parent.width - 32
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.Wrap
                        text: qsTr("Erst leiser drehen!")
                        font.family: Theme.uiFont
                        font.pixelSize: 15
                        font.weight: Font.Bold
                        color: Theme.text
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("Bei Bit-Perfect regelt der Player die Lautstärke nicht mehr: Der Ton kommt mit voller Stärke beim Gerät an. Dreh deshalb vorher den Verstärker, den DAC oder den Kopfhörer ganz leise und danach langsam lauter. Wähle unten auch das richtige Gerät.")
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: Theme.textDim
                }
                HToggle {
                    width: parent.width
                    label: qsTr("Ich habe Verstärker, DAC bzw. Kopfhörer leise gestellt – Bit-Perfect einschalten")
                    checked: false
                    onToggled: {
                        if (checked) {
                            Audio.bitPerfect = true
                            snd.bpArmed = false
                        }
                        checked = false
                    }
                }
            }
        }
        Text {
            width: snd.width
            wrapMode: Text.Wrap
            text: !Audio.alsaAvailable
                  ? qsTr("Für Bit-Perfect fehlt das GStreamer-Plugin „alsasink“ (Paket gstreamer1.0-alsa bzw. gst-plugins-base).")
                  : Audio.hwDevices.length === 0 && !Audio.bitPerfect
                    ? qsTr("Keine Soundkarte gefunden, die sich direkt ansprechen lässt.")
                    : qsTr("Jeder Titel kommt in seiner eigenen Abtastrate und Bittiefe beim Gerät an, Sample für Sample wie in der Datei. Dafür sind Lautstärkeregler, ReplayGain, Equalizer und Übergänge aus: die Lautstärke regelst du am Verstärker oder DAC. Solange der Player spielt, gehört das Gerät ihm allein.")
            font.family: Theme.uiFont
            font.pixelSize: 13
            color: Theme.textFaint
        }
        SettingsOption {
            width: snd.width
            visible: Audio.bitPerfect || snd.bpArmed
            label: qsTr("Gerät für Bit-Perfect")
            hint: Audio.bitPerfect ? qsTr("Vor einem Wechsel des Geräts auch dort erst leiser drehen.") : ""
            HCombo {
                width: Math.min(460, snd.width)
                options: {
                    var list = []
                    var found = false
                    for (var i = 0; i < Audio.hwDevices.length; ++i) {
                        var d = Audio.hwDevices[i]
                        list.push({ value: d.key, label: d.name + "  (" + d.detail + ")" })
                        if (d.key === Audio.hwDevice) found = true
                    }
                    if (!found && Audio.hwDevice !== "")
                        list.push({ value: Audio.hwDevice, label: qsTr("%1 (nicht verbunden)").arg(Audio.hwDeviceName || Audio.hwDevice) })
                    return list
                }
                current: Audio.hwDevice !== "" ? Audio.hwDevice : (Audio.hwDevices.length > 0 ? Audio.hwDevices[0].key : "") // the first one is taken by default
                onPicked: (v) => Audio.setHwDevice(v, snd.nameOf(Audio.hwDevices, v, Audio.hwDevice, Audio.hwDeviceName))
            }
        }

        // what happens right now
        Rectangle {
            visible: Audio.fallback !== ""
            width: snd.width
            height: fallbackText.implicitHeight + 20
            radius: Math.min(10, Theme.radius)
            color: Theme.alpha(Theme.accent, 0.12)
            border.color: Theme.alpha(Theme.accent, 0.5)
            Text {
                id: fallbackText
                x: 12; y: 10
                width: parent.width - 24
                wrapMode: Text.Wrap
                text: Audio.fallback
                font.family: Theme.uiFont
                font.pixelSize: 13
                color: Theme.text
            }
        }
        Column {
            width: snd.width
            spacing: 4
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: snd.path.input
                      ? qsTr("Signal: %1  →  %2: %3").arg(snd.path.input)
                            .arg(Player.bitPerfect || (Audio.device !== "" && Audio.fallback === "") ? qsTr("Gerät") : qsTr("Soundserver"))
                            .arg(snd.path.output || "–")
                      : qsTr("Signal: (spielt gerade nichts)")
                font.family: Theme.uiFont
                font.pixelSize: 13
                color: Theme.textDim
            }
            Text {
                visible: Player.bitPerfect && !!snd.path.input
                width: parent.width
                wrapMode: Text.Wrap
                text: snd.path.exact ? "✓ " + qsTr("Bit-Perfect: Die Samples kommen unverändert beim Gerät an.")
                    : snd.path.lossy ? qsTr("Verlustbehaftete Formate (MP3, AAC, Ogg, Opus) kommen aus dem Decoder als Gleitkommazahlen und werden für das Gerät in ganze Zahlen umgesetzt.")
                    : qsTr("Nicht bit-perfect: Das Gerät kann diese Abtastrate oder Kanalzahl nicht, sie wird umgerechnet.")
                font.family: Theme.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: snd.path.exact ? Theme.accent : Theme.textDim
            }
        }
    }

    // ---------------- equalizer ----------------
    SettingsSection {
        width: snd.width
        title: qsTr("Equalizer")
        text: !Audio.eqAvailable
              ? qsTr("Der Equalizer ist nicht verfügbar: Das GStreamer-Plugin „equalizer“ fehlt (gst-plugins-good).")
              : Player.bitPerfect
                ? qsTr("Bei Bit-Perfect ist der Equalizer aus, das Signal bleibt unverändert.")
                : qsTr("Zehn Bänder von 31 Hz bis 16 kHz. Der Equalizer ist nur in der Kette, solange er eingeschaltet ist; ausgeschaltet bleibt das Signal unberührt. Doppelklick auf einen Regler setzt ihn auf 0 dB.")

        HToggle {
            width: snd.width
            label: qsTr("Equalizer einschalten")
            enabled: Audio.eqAvailable && !Player.bitPerfect
            checked: Audio.eqEnabled
            onToggled: Audio.eqEnabled = checked
        }

        Row {
            spacing: 10
            enabled: Audio.eqAvailable
            opacity: enabled ? 1 : 0.5
            HCombo {
                id: presetCombo
                width: 250
                options: {
                    var list = []
                    for (var i = 0; i < Audio.eqPresets.length; ++i)
                        list.push({ value: Audio.eqPresets[i].name, label: Audio.eqPresets[i].name })
                    if (Audio.eqPreset === "")
                        list.push({ value: "", label: qsTr("Eigene Einstellung") })
                    return list
                }
                current: Audio.eqPreset
                onPicked: (v) => { if (v !== "") Audio.applyPreset(v) }
            }
            PillButton {
                compact: true
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Speichern …")
                iconName: "add"
                onClicked: { saveField.text = Audio.eqPreset !== "" && !snd.builtin(Audio.eqPreset) ? Audio.eqPreset : ""; saveDlg.open() }
            }
            PillButton {
                compact: true
                anchors.verticalCenter: parent.verticalCenter
                visible: Audio.eqPreset !== "" && !snd.builtin(Audio.eqPreset)
                text: qsTr("Löschen")
                iconName: "delete"
                onClicked: Audio.deletePreset(Audio.eqPreset)
            }
            PillButton {
                compact: true
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Zurücksetzen")
                iconName: "refresh"
                onClicked: Audio.resetEq()
            }
        }

        EqCurve {
            width: Math.min(snd.width, 760)
            height: 250
            enabled: Audio.eqAvailable
            opacity: !enabled ? 0.4 : Audio.eqEnabled && !Player.bitPerfect ? 1 : 0.6
        }

        SettingsOption {
            width: snd.width
            enabled: Audio.eqAvailable
            label: qsTr("Vorverstärkung")
            value: snd.db(Audio.eqEffectivePreamp, 1) + " dB"
            hint: Audio.eqAutoPreamp ? qsTr("Automatisch so viel leiser, wie das lauteste Band anhebt: Nichts übersteuert.") : ""
            Column {
                spacing: 6
                HToggle {
                    label: qsTr("Automatisch")
                    checked: Audio.eqAutoPreamp
                    onToggled: Audio.eqAutoPreamp = checked
                }
                HSlider {
                    visible: !Audio.eqAutoPreamp
                    width: Math.min(420, snd.width)
                    from: -12; to: 12; stepSize: 0.5
                    value: Audio.eqPreamp
                    onMoved: Audio.eqPreamp = value
                }
            }
        }
    }

    // ---------------- ReplayGain ----------------
    SettingsSection {
        width: snd.width
        title: qsTr("Lautstärke angleichen")
        text: !Player.replayGainAvailable
              ? qsTr("ReplayGain ist nicht verfügbar: Das GStreamer-Plugin rgvolume fehlt (gst-plugins-good).")
              : Player.bitPerfect
                ? qsTr("Bei Bit-Perfect ist ReplayGain aus, das Signal bleibt unverändert.")
                : qsTr("ReplayGain gleicht die Lautstärke zwischen Titeln an. Ein Limiter verhindert Übersteuerung.")
        SettingsOption {
            width: snd.width
            label: qsTr("ReplayGain")
            enabled: Player.replayGainAvailable && !Player.bitPerfect
            HSegmented {
                width: snd.width
                options: [qsTr("Aus"), qsTr("Titel"), qsTr("Album")]
                values: [0, 1, 2]
                current: Player.replayGain
                onPicked: (v) => Player.replayGain = v
            }
        }
        SettingsOption {
            width: snd.width
            label: qsTr("Vorverstärkung")
            value: snd.db(Player.preamp, 1) + " dB"
            enabled: Player.replayGainAvailable && Player.replayGain > 0 && !Player.bitPerfect
            HSlider {
                width: Math.min(420, snd.width)
                from: -10; to: 10; stepSize: 0.5
                value: Player.preamp
                onMoved: Player.preamp = value
            }
        }
    }

    // "+2,5" – German decimal comma, sign for gains
    function db(v, decimals) {
        return (v > 0 ? "+" : "") + Number(v).toLocaleString(Qt.locale("de_DE"), "f", decimals)
    }
    // Name of a device by key (a chosen device that is gone keeps its saved name).
    function nameOf(list, key, chosen, chosenName) {
        for (var i = 0; i < list.length; ++i)
            if (list[i].key === key) return list[i].name
        return key === chosen ? chosenName : ""
    }

    function builtin(name) {
        for (var i = 0; i < Audio.eqPresets.length; ++i)
            if (Audio.eqPresets[i].name === name) return Audio.eqPresets[i].builtin
        return false
    }

    HDialog {
        id: saveDlg
        title: qsTr("Klang speichern")
        text: qsTr("Unter diesem Namen erscheint die Einstellung in der Liste. Ein vorhandener eigener Name wird überschrieben.")
        acceptText: qsTr("Speichern")
        acceptEnabled: saveField.text.trim() !== ""
        HTextField {
            id: saveField
            width: parent.width
            placeholder: qsTr("Name, z. B. Kopfhörer")
            onAccepted: saveDlg.accept()
        }
        onOpened: saveField.forceActiveFocus()
        onAccepted: Audio.savePreset(saveField.text)
    }

    // Ten vertical sliders over a drawn curve.
    component EqCurve: Item {
        id: eqc
        readonly property real padTop: 26     // room for the dB values
        readonly property real padBottom: 26  // room for the frequencies
        readonly property real plotH: height - padTop - padBottom
        readonly property real colW: width / 10
        function yOf(db) { return padTop + (1 - (db + 12) / 24) * plotH }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            color: Theme.alpha(Theme.text, 0.04)
            border.color: Theme.alpha(Theme.text, 0.08)
        }
        // grid: +12, +6, 0, −6, −12 dB
        Repeater {
            model: [12, 6, 0, -6, -12]
            Rectangle {
                x: 8
                width: eqc.width - 16
                y: eqc.yOf(modelData)
                height: 1
                color: Theme.alpha(Theme.text, modelData === 0 ? 0.22 : 0.08)
            }
        }
        Canvas {
            id: curve
            anchors.fill: parent
            renderStrategy: Canvas.Cooperative
            property var bands: Audio.eqBands
            property color accent: Theme.accent
            onBandsChanged: requestPaint()
            onAccentChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                if (!bands || bands.length < 10) return
                var pts = []
                for (var i = 0; i < 10; ++i)
                    pts.push({ x: eqc.colW * (i + 0.5), y: eqc.yOf(bands[i]) })
                function trace() {
                    ctx.moveTo(pts[0].x, pts[0].y)
                    for (var k = 0; k < pts.length - 1; ++k) {
                        var mx = (pts[k].x + pts[k + 1].x) / 2, my = (pts[k].y + pts[k + 1].y) / 2
                        ctx.quadraticCurveTo(pts[k].x, pts[k].y, mx, my)
                    }
                    ctx.lineTo(pts[pts.length - 1].x, pts[pts.length - 1].y)
                }
                var zero = eqc.yOf(0)
                ctx.beginPath()
                trace()
                ctx.lineTo(pts[9].x, zero)
                ctx.lineTo(pts[0].x, zero)
                ctx.closePath()
                ctx.fillStyle = Theme.alpha(Theme.accent, 0.16)
                ctx.fill()
                ctx.beginPath()
                trace()
                ctx.lineWidth = 2.5
                ctx.strokeStyle = Theme.accent
                ctx.stroke()
            }
        }
        Repeater {
            model: 10
            Item {
                id: band
                x: eqc.colW * index
                width: eqc.colW
                height: eqc.height
                readonly property real gain: Audio.eqBands.length > index ? Audio.eqBands[index] : 0
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 5
                    text: snd.db(band.gain, band.gain % 1 === 0 ? 0 : 1)
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: band.gain === 0 ? Theme.textFaint : Theme.accent
                }
                Slider {
                    id: sl
                    x: (parent.width - width) / 2
                    y: eqc.padTop - 9
                    width: 30
                    height: eqc.plotH + 18
                    orientation: Qt.Vertical
                    from: -12; to: 12; stepSize: 0.5
                    padding: 9
                    value: band.gain
                    focusPolicy: Qt.TabFocus
                    Accessible.name: { var f = String(Audio.eqFrequencies[index]); return f.indexOf("k") >= 0 ? f.replace("k", " kHz") : f + " Hz" }
                    onMoved: Audio.setBand(index, value)
                    background: Item {}
                    handle: Rectangle {
                        x: sl.leftPadding + (sl.availableWidth - width) / 2
                        y: sl.topPadding + sl.visualPosition * (sl.availableHeight - height)
                        width: sl.pressed ? 18 : 15
                        height: width
                        radius: width / 2
                        color: Theme.text
                        border.width: sl.visualFocus ? 3 : 2
                        border.color: Theme.accent
                        Behavior on width { NumberAnimation { duration: 80 } }
                    }
                    TapHandler {
                        // double click: back to 0 dB (dragging stays with the slider)
                        onDoubleTapped: Audio.setBand(index, 0)
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: eqc.height - 21
                    text: Audio.eqFrequencies[index]
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    color: Theme.textDim
                }
            }
        }
    }
}
