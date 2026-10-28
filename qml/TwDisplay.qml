// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import Halcyon

// Vacuum-fluorescent display of a 90s mini system: indicator words, a
// 14-segment marquee that steps character by character, 7-segment track and
// time, and a segment spectrum analyser with peak hold. Unlit segments shine
// through faintly, like on the real thing.
Rectangle {
    id: vfd
    property color ink: "#45E3FF"
    property color ink2: "#FF7A3A"
    property int mode: 0              // 0 normal, 1 big spectrum, 2 clock
    property bool dimmed: false
    property bool demo: false
    property bool active: true
    property real beat: 0
    readonly property color ghost: Qt.rgba(ink.r, ink.g, ink.b, 0.075)
    readonly property real lum: dimmed ? 0.5 : 1.0
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software
    color: "#040706"
    radius: 6
    border.color: "#000000"
    border.width: 2
    clip: true

    // blink phase for paused time and demo indicators
    property bool blink: true
    Timer { interval: 480; repeat: true; running: vfd.active && vfd.visible; onTriggered: vfd.blink = !vfd.blink }

    // ---------- marquee text ----------
    function seg14(s) {
        s = s.toUpperCase().replace(/Ä/g, "AE").replace(/Ö/g, "OE").replace(/Ü/g, "UE").replace(/ß/g, "SS")
        return s.replace(/[^A-Z0-9 \-\.\'\/\*\+\(\)=<>_,]/g, "-")
    }
    readonly property string message: {
        if (!Player.hasTrack) return "NO DISC  *  BUB3PLAYER  *  "
        var t = Player.title + "  -  " + Player.artist
        if (vfd.demo) t += "  *  BUB3PLAYER  *  PROJEKT PRAESENTIERT VON WWW.BAUENUNDBOOTEN.DE"
        return vfd.seg14(t) + "    "
    }
    property int offset: 0
    onMessageChanged: offset = 0
    readonly property int cols: Math.max(4, Math.floor(marqueeBox.width / Math.max(1, fm14.advanceWidth("~"))))
    readonly property bool needsScroll: message.trim().length > cols
    Timer {
        interval: 260
        repeat: true
        running: vfd.active && vfd.visible && vfd.needsScroll
        onTriggered: vfd.offset = (vfd.offset + 1) % vfd.message.length
    }
    readonly property string window14: {
        if (!needsScroll) {
            var m = message.trim()
            while (m.length < cols) m += " "
            return m.substring(0, cols)
        }
        var s = message + message
        return s.substring(offset, offset + cols)
    }
    FontMetrics { id: fm14; font.family: "DSEG14 Classic"; font.pixelSize: Math.round(vfd.height * 0.2) }

    // ---------- spectrum with peak hold ----------
    readonly property int bands: 20
    readonly property int segs: vfd.mode === 1 ? 14 : 8
    readonly property bool live: vfd.visible && vfd.active && Player.spectrumEnabled && Player.playing && Player.spectrum.length > 0
    property var levels: []
    property var peaks: []
    Connections {
        target: Player
        enabled: vfd.live
        function onSpectrumChanged() { vfd.updateBands() }
    }
    function updateBands() {
        var s = Player.spectrum
        var lv = [], pk = vfd.peaks.length === vfd.bands ? vfd.peaks.slice() : []
        for (var i = 0; i < vfd.bands; ++i) {
            // log-ish spacing: more bands for the lows
            var a = Math.floor(Math.pow(i / vfd.bands, 1.6) * s.length)
            var b = Math.max(a + 1, Math.floor(Math.pow((i + 1) / vfd.bands, 1.6) * s.length))
            var m = 0
            for (var j = a; j < b && j < s.length; ++j) m = Math.max(m, s[j])
            var l = Math.round(m * vfd.segs)
            lv.push(l)
            var p = pk.length > i ? pk[i] : 0
            pk[i] = l >= p ? l + 0.999 : Math.max(0, p - 0.12)
        }
        vfd.levels = lv
        vfd.peaks = pk
    }
    onLiveChanged: if (!live) { levels = []; peaks = [] }

    // ---------- layout ----------
    Image { // phosphor dot mesh
        anchors.fill: parent
        source: "image://icon/mesh/4"
        fillMode: Image.Tile
        opacity: 0.5
        smooth: false
    }

    // glow of the phosphor (drawn under the sharp segments)
    Glow {
        target: content
        visible: vfd.canShade
        downscale: 4
        spread: 1.0
        opacity: 0.7 * vfd.lum
    }
    Item {
        id: content
        anchors.fill: parent
        anchors.margins: Math.round(vfd.height * 0.06)
        opacity: vfd.lum
        readonly property real u: vfd.height / 150

        // indicator row
        Row {
            id: ind
            spacing: 7 * content.u
            Repeater {
                model: [
                    ["CD", Player.hasTrack], ["TAPE", false], ["TUNER", false],
                    ["▶", Player.state === 1], ["❚❚", Player.state === 2 && vfd.blink],
                    ["RPT", Queue.repeat > 0], ["1", Queue.repeat === 2], ["SHUF", Queue.shuffle], ["PGM", window.queueOpen],
                    ["DSP", vfd.demo && (vfd.beat > 0.4 || vfd.blink)], ["X-BASS", vfd.demo ? vfd.beat > 0.25 : Player.playing], ["NR", false]
                ]
                Text {
                    text: modelData[0]
                    font.family: "Barlow Condensed"
                    font.pixelSize: Math.round(11 * content.u)
                    font.weight: Font.Bold
                    color: modelData[1] ? (modelData[0] === "X-BASS" || modelData[0] === "DSP" ? vfd.ink2 : vfd.ink) : vfd.ghost
                }
            }
        }

        // 14-segment marquee
        Item {
            id: marqueeBox
            anchors.top: ind.bottom
            anchors.topMargin: 4 * content.u
            width: parent.width
            height: fm14.height
            visible: vfd.mode !== 2
            Text {
                text: "~".repeat(vfd.cols)
                font: fm14.font
                color: vfd.ghost
            }
            Text {
                text: vfd.window14.replace(/ /g, "!")
                font: fm14.font
                color: vfd.ink
            }
        }

        // track, disc and time
        Item {
            id: midRow
            anchors.top: marqueeBox.bottom
            anchors.topMargin: 4 * content.u
            width: parent.width
            height: 30 * content.u
            visible: vfd.mode === 0
            Column {
                y: 2 * content.u
                Text { text: "TRACK"; font.family: "Barlow Condensed"; font.pixelSize: Math.round(9 * content.u); font.weight: Font.Bold; color: vfd.ink }
                Text { text: "DISC"; font.family: "Barlow Condensed"; font.pixelSize: Math.round(9 * content.u); font.weight: Font.Bold; color: vfd.ink }
            }
            Seg7 {
                x: 30 * content.u
                text: ((Queue.currentRow + 1) % 100 < 10 ? "0" : "") + ((Queue.currentRow + 1) % 100)
                ghostText: "88"
                size: Math.round(24 * content.u)
            }
            Row {
                x: 82 * content.u
                y: 4 * content.u
                spacing: 4 * content.u
                Repeater {
                    model: 3
                    Rectangle {
                        width: 14 * content.u; height: 14 * content.u; radius: 7 * content.u
                        color: "transparent"
                        border.width: 1.5
                        readonly property bool on: Player.hasTrack && (Math.max(0, Queue.currentRow) % 3) === index
                        border.color: on ? vfd.ink : vfd.ghost
                        Text {
                            anchors.centerIn: parent
                            text: index + 1
                            font.family: "Barlow Condensed"; font.pixelSize: Math.round(9 * content.u); font.weight: Font.Bold
                            color: parent.on ? vfd.ink : vfd.ghost
                        }
                    }
                }
            }
            Seg7 {
                anchors.right: parent.right
                text: {
                    var ms = Player.position
                    var s = Math.floor(ms / 1000), m = Math.floor(s / 60)
                    var str = (m % 100 < 10 ? "!" : "") + (m % 100) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
                    return Player.state === 2 && !vfd.blink ? "!!:!!" : str
                }
                ghostText: "88:88"
                size: Math.round(28 * content.u)
            }
        }

        // clock mode
        Seg7 {
            visible: vfd.mode === 2
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 4 * content.u
            property date now: new Date()
            Timer { interval: 1000; repeat: true; running: vfd.mode === 2 && vfd.visible; onTriggered: parent.now = new Date() }
            text: Qt.formatTime(now, "hh") + (vfd.blink ? ":" : "!") + Qt.formatTime(now, "mm")
            ghostText: "88:88"
            size: Math.round(52 * content.u)
        }

        // spectrum
        Item {
            id: specBox
            visible: vfd.mode !== 2
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10 * content.u
            width: parent.width
            height: vfd.mode === 1 ? parent.height - marqueeBox.y - marqueeBox.height - 18 * content.u : 34 * content.u
            Row {
                anchors.fill: parent
                spacing: 3 * content.u
                Repeater {
                    model: vfd.bands
                    Item {
                        id: band
                        width: (specBox.width - (vfd.bands - 1) * 3 * content.u) / vfd.bands
                        height: specBox.height
                        readonly property int lit: vfd.levels.length > index ? vfd.levels[index] : 0
                        readonly property int peak: vfd.peaks.length > index ? Math.floor(vfd.peaks[index]) : 0
                        Repeater {
                            model: vfd.segs
                            Rectangle {
                                readonly property int s: index + 1        // 1 = bottom
                                width: band.width
                                height: (band.height - (vfd.segs - 1) * 1.5) / vfd.segs
                                y: band.height - s * (height + 1.5) + 1.5
                                color: s <= band.lit || (s === band.peak && band.peak > 0)
                                       ? (s > vfd.segs - 2 ? vfd.ink2 : vfd.ink) : vfd.ghost
                            }
                        }
                    }
                }
            }
        }
        Row {
            visible: vfd.mode !== 2
            anchors.bottom: parent.bottom
            anchors.bottomMargin: -2 * content.u
            width: parent.width
            Repeater {
                model: ["63", "", "250", "", "", "1k", "", "", "4k", "", "", "", "16k"]
                Text {
                    width: content.width / 13
                    horizontalAlignment: Text.AlignHCenter
                    text: modelData
                    font.family: "Barlow Condensed"
                    font.pixelSize: Math.round(8 * content.u)
                    font.weight: Font.Bold
                    color: vfd.ink
                    opacity: 0.7
                }
            }
        }
    }
    // smoked glass reflection
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.07) }
            GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }

    component Seg7: Item {
        id: s7
        property string text
        property string ghostText: "88"
        property int size: 24
        width: g7.implicitWidth
        height: g7.implicitHeight
        Text { id: g7; text: s7.ghostText; font.family: "DSEG7 Classic"; font.pixelSize: s7.size; color: vfd.ghost }
        Text { text: s7.text; font.family: "DSEG7 Classic"; font.pixelSize: s7.size; color: vfd.ink }
    }
}
