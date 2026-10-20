// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// Seek bar: the real waveform of the track once it has been analysed
// (see Waveform.qml); until then a row of level bars with the live spectrum.
// Played part is in the accent colour.
Item {
    id: seek
    property int bars: 56
    property real barGap: 2
    property bool interactive: true
    property real dragFrac: -1
    readonly property var spec: Player.spectrum
    readonly property bool hasWave: Player.waveform.length > 0
    readonly property bool live: !hasWave && Player.spectrumEnabled && Player.playing && spec.length > 0
    property color waveColor1: Theme.accent
    property color waveColor2: Theme.accent
    readonly property real frac: Player.duration > 0
                                 ? (dragFrac >= 0 ? dragFrac : Math.min(1, Player.position / Player.duration)) : 0
    implicitHeight: 32

    function seed(i) {
        var x = Math.sin((Player.currentId + 7) * 12.9898 + i * 78.233) * 43758.5453
        x = x - Math.floor(x)
        // Gentle envelope: quieter at both ends, like most tracks.
        var t = i / Math.max(1, bars - 1)
        var env = 0.55 + 0.45 * Math.sin(Math.PI * Math.min(1, t * 1.1))
        return (0.25 + 0.6 * x) * env
    }

    Waveform {
        anchors.fill: parent
        visible: seek.hasWave
        interactive: false
        showHead: false
        dragFrac: seek.dragFrac
        style: "mirror"
        barWidth: 2
        barGap: 1.5
        color1: seek.waveColor1
        color2: seek.waveColor2
        dimColor: Theme.alpha(Theme.text, hover.containsMouse ? 0.3 : 0.2)
    }

    Row {
        id: row
        visible: !seek.hasWave
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height
        spacing: seek.barGap
        Repeater {
            model: seek.bars
            Rectangle {
                readonly property real rest: seek.seed(index)
                readonly property real level: seek.live ? seek.spec[Math.min(seek.spec.length - 1, Math.floor(index * seek.spec.length / seek.bars))] : 0
                width: (seek.width - (seek.bars - 1) * seek.barGap) / seek.bars
                height: Math.max(3, seek.height * (seek.live ? Math.max(0.1, level) : rest * 0.7))
                anchors.bottom: parent.bottom
                radius: Math.min(width / 2, 2)
                color: (index + 0.5) / seek.bars <= seek.frac ? Theme.accent : Theme.alpha(Theme.text, hover.containsMouse ? 0.28 : 0.17)
            }
        }
    }

    // Hover / drag marker
    Rectangle {
        visible: seek.interactive && (hover.containsMouse || hover.pressed) && Player.duration > 0
        x: Math.max(0, Math.min(seek.width, hover.mouseX)) - 1
        width: 2
        height: seek.height + 6
        y: -3
        radius: 1
        color: Theme.text
    }

    MouseArea {
        id: hover
        anchors.fill: parent
        anchors.topMargin: -6
        anchors.bottomMargin: -6
        enabled: seek.interactive
        hoverEnabled: true
        cursorShape: Player.duration > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
        function fracAt(x) { return Math.max(0, Math.min(1, x / seek.width)) }
        onPressed: (m) => seek.dragFrac = fracAt(m.x)
        onPositionChanged: (m) => { if (pressed) seek.dragFrac = fracAt(m.x) }
        onReleased: {
            if (Player.duration > 0)
                Player.seek(Math.round(seek.dragFrac * Player.duration))
            seek.dragFrac = -1
        }
        onCanceled: seek.dragFrac = -1
    }
}
