// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// Backlit tuning dial of a 70s receiver. The red pointer is the playback
// position (drag it to seek); the time scale replaces the frequency scale
// at the bottom; the "station" is the current track. The lamps warm up
// slowly when the skin starts, like light bulbs behind the glass.
Item {
    id: dial
    property color light: "#5FD4E8"           // backlight
    property color ink: "#F4EBD9"           // printed scale
    property color pointer: "#FF4A2A"
    property real previewFrac: -1             // set by the tuning knob while turning
    readonly property real frac: previewFrac >= 0 ? previewFrac
                                : dragFrac >= 0 ? dragFrac
                                : (Player.duration > 0 ? Math.min(1, Player.position / Player.duration) : 0)
    property real dragFrac: -1
    property bool warmup: true
    property real warm: 0
    Component.onCompleted: if (!warmup) warm = 1
    SequentialAnimation on warm {
        running: dial.warmup
        PauseAnimation { duration: 250 }
        NumberAnimation { to: 0.35; duration: 250 }
        NumberAnimation { to: 0.2; duration: 120 }
        NumberAnimation { to: 0.7; duration: 600; easing.type: Easing.InQuad }
        NumberAnimation { to: 0.6; duration: 90 }
        NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.OutQuad }
    }
    readonly property real sx: 18            // scale inset left/right

    // chrome bezel
    Rectangle {
        anchors.fill: parent
        radius: 5
        gradient: Gradient {
            GradientStop { position: 0; color: "#f4f5f7" }
            GradientStop { position: 0.5; color: "#8e9197" }
            GradientStop { position: 1; color: "#dcdee2" }
        }
    }
    Rectangle {
        id: glass
        anchors.fill: parent
        anchors.margins: 3
        radius: 3
        clip: true
        color: "#07090a"

        // backlight
        Rectangle {
            anchors.fill: parent
            opacity: dial.warm
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(dial.light.r, dial.light.g, dial.light.b, 0.10) }
                GradientStop { position: 0.25; color: Qt.rgba(dial.light.r, dial.light.g, dial.light.b, 0.42) }
                GradientStop { position: 0.5; color: Qt.rgba(dial.light.r, dial.light.g, dial.light.b, 0.30) }
                GradientStop { position: 0.75; color: Qt.rgba(dial.light.r, dial.light.g, dial.light.b, 0.42) }
                GradientStop { position: 1.0; color: Qt.rgba(dial.light.r, dial.light.g, dial.light.b, 0.10) }
            }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.55) }
                GradientStop { position: 0.35; color: Qt.rgba(0, 0, 0, 0.0) }
                GradientStop { position: 0.7; color: Qt.rgba(0, 0, 0, 0.0) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.6) }
            }
        }

        // printed scales
        Canvas {
            id: scales
            anchors.fill: parent
            opacity: 0.35 + dial.warm * 0.65
            property real dur: Player.duration
            onDurChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var c = getContext("2d")
                var w = width, h = height, x0 = dial.sx, x1 = w - dial.sx
                c.reset()
                c.fillStyle = dial.ink
                c.strokeStyle = dial.ink
                c.textAlign = "center"
                c.textBaseline = "middle"
                var fs = Math.max(9, Math.round(h * 0.11))
                // FM
                c.font = "600 " + fs + "px 'Barlow Condensed'"
                var fm = [88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 108]
                var yTop = h * 0.13
                for (var i = 0; i < fm.length; ++i) {
                    var x = x0 + 26 + (x1 - x0 - 52) * i / (fm.length - 1)
                    c.fillText(fm[i], x, yTop)
                }
                c.textAlign = "left"; c.fillText("FM", x0 - 6, yTop)
                c.textAlign = "right"; c.fillText("MHz", x1 + 8, yTop)
                // fine ticks under FM
                c.lineWidth = 1
                for (var t = 0; t <= 100; ++t) {
                    var xx = x0 + 26 + (x1 - x0 - 52) * t / 100
                    var len = t % 10 === 0 ? h * 0.07 : (t % 5 === 0 ? h * 0.05 : h * 0.03)
                    c.beginPath(); c.moveTo(xx, h * 0.22); c.lineTo(xx, h * 0.22 + len); c.stroke()
                }
                // AM (smaller, dimmer)
                c.globalAlpha = 0.6
                c.font = "500 " + Math.max(8, fs - 2) + "px 'Barlow Condensed'"
                c.textAlign = "center"
                var am = [530, 600, 700, 800, 1000, 1200, 1400, 1600]
                for (var j = 0; j < am.length; ++j) {
                    var xa = x0 + 40 + (x1 - x0 - 80) * j / (am.length - 1)
                    c.fillText(am[j], xa, h * 0.37)
                }
                c.globalAlpha = 1
                // time scale at the bottom: a tick every 10 s, numbered minutes
                var y2 = h * 0.8
                c.lineWidth = 1.2
                c.beginPath(); c.moveTo(x0, y2); c.lineTo(x1, y2); c.stroke()
                var d = Math.max(1, dur / 1000)
                var step = d > 900 ? 60 : (d > 300 ? 30 : 10)
                c.font = "600 " + fs + "px 'Barlow Condensed'"
                c.textAlign = "center"
                for (var s = 0; s <= d + 0.01; s += step) {
                    var xs = x0 + (x1 - x0) * s / d
                    var major = s % 60 === 0
                    c.beginPath(); c.moveTo(xs, y2); c.lineTo(xs, y2 - (major ? h * 0.09 : h * 0.045)); c.stroke()
                    if (major && (x1 - x0) / (d / 60) > 22)
                        c.fillText(Math.round(s / 60), xs, y2 + h * 0.1)
                }
                c.textAlign = "left"
                c.fillText("MIN", x1 + 2 > w - 20 ? w - 20 : x1 + 2, y2 + h * 0.1)
            }
        }

        // the station: current track
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height * 0.47
            width: parent.width - 220
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: Player.hasTrack ? (Player.title + "  ·  " + Player.artist).toUpperCase() : "BUB3PLAYER  ·  STEREO"
            font.family: "Barlow Condensed"
            font.pixelSize: Math.max(12, Math.round(dial.height * 0.17))
            font.weight: Font.Bold
            font.letterSpacing: 2.5
            color: Qt.lighter(dial.light, 1.5)
            opacity: 0.25 + dial.warm * 0.75
            style: Text.Outline
            styleColor: Qt.rgba(dial.light.r, dial.light.g, dial.light.b, 0.25 * dial.warm)
        }
        // lamps
        Row {
            x: dial.sx - 4
            y: parent.height * 0.47
            spacing: 8
            Lamp { text: "STEREO"; on: Player.playing; col: "#FF3A24" }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: dial.sx - 6
            y: parent.height * 0.47
            spacing: 10
            // the heart: lit for a favourite, a click sets it (see the MouseArea below)
            Lamp {
                id: heartLamp
                visible: Player.currentId > 0
                text: "♥"
                on: Library.currentFavorite
                col: "#FF4F6E"
                font.pixelSize: Math.max(12, Math.round(dial.height * 0.16))
                anchors.verticalCenter: parent.verticalCenter
            }
            Lamp { text: Player.format !== "" ? Player.format.toUpperCase() : "PHONO"; on: Player.hasTrack; col: "#7CFF7A" }
        }

        // pointer
        Item {
            x: dial.sx + (glass.width - 2 * dial.sx) * dial.frac - width / 2
            width: 24
            height: parent.height
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 10; height: parent.height
                opacity: 0.35 * dial.warm
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 0.5; color: dial.pointer }
                    GradientStop { position: 1; color: "transparent" }
                }
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 2.5; height: parent.height
                color: Qt.lighter(dial.pointer, 1.0 + 0.3 * dial.warm)
            }
        }
        // glass reflection
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.0) }
                GradientStop { position: 0.18; color: Qt.rgba(1, 1, 1, 0.07) }
                GradientStop { position: 0.3; color: Qt.rgba(1, 1, 1, 0.0) }
                GradientStop { position: 0.62; color: Qt.rgba(1, 1, 1, 0.0) }
                GradientStop { position: 0.7; color: Qt.rgba(1, 1, 1, 0.04) }
                GradientStop { position: 0.76; color: Qt.rgba(1, 1, 1, 0.0) }
            }
        }
        Rectangle {
            width: parent.width; height: 1
            color: Qt.rgba(1, 1, 1, 0.12)
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        readonly property bool overHeart: heartLamp.visible && containsMouse
                                          && heartLamp.contains(heartLamp.mapFromItem(this, mouseX, mouseY))
        cursorShape: overHeart ? Qt.PointingHandCursor : Player.duration > 0 ? Qt.SizeHorCursor : Qt.ArrowCursor
        enabled: Player.hasTrack
        function f(x) { return Math.max(0, Math.min(1, (x - dial.sx - 3) / (width - 2 * dial.sx - 6))) }
        onPressed: (m) => {
            if (heartLamp.visible && heartLamp.contains(heartLamp.mapFromItem(this, m.x, m.y)))
                Library.toggleCurrentFavorite()
            else if (Player.duration > 0)
                dial.dragFrac = f(m.x)
        }
        onPositionChanged: (m) => { if (pressed && dial.dragFrac >= 0) dial.dragFrac = f(m.x) }
        onReleased: {
            if (dial.dragFrac >= 0)
                Player.seek(Math.round(dial.dragFrac * Player.duration))
            dial.dragFrac = -1
        }
        onCanceled: dial.dragFrac = -1
    }

    component Lamp: Text {
        property bool on: false
        property color col: "red"
        font.family: "Barlow Condensed"
        font.pixelSize: Math.max(10, Math.round(dial.height * 0.12))
        font.weight: Font.Bold
        font.letterSpacing: 1.5
        color: on ? col : Qt.rgba(col.r * 0.35, col.g * 0.35, col.b * 0.35, 1)
        opacity: on ? (0.4 + 0.6 * dial.warm) : 0.8
        style: on ? Text.Outline : Text.Normal
        styleColor: Qt.rgba(col.r, col.g, col.b, 0.35)
    }
}
