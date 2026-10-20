// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// Seek bar showing the real loudness envelope of the track (Player.waveform).
// Drawn once per track and size; the played part is just a clipped copy in the
// accent colours, so moving the play position costs nothing.
Item {
    id: wf
    property var values: Player.waveform
    readonly property bool ready: values && values.length > 0
    property string style: "mirror"           // mirror | bars | line
    property real barWidth: 2
    property real barGap: 1
    property color dimColor: Theme.alpha(Theme.text, 0.2)
    property color color1: Theme.accent
    property color color2: Theme.accent
    property bool interactive: true
    property bool showHead: true
    property real dragFrac: -1
    readonly property real frac: Player.duration > 0
                                 ? (dragFrac >= 0 ? dragFrac : Math.min(1, Player.position / Player.duration)) : 0
    implicitHeight: 36

    // grow in when a new envelope arrives
    property real grow: 0
    onValuesChanged: { grow = 0; if (ready) growAnim.restart(); base.requestPaint(); played.requestPaint() }
    NumberAnimation { id: growAnim; target: wf; property: "grow"; from: 0; to: 1; duration: 520; easing.type: Easing.OutCubic }
    Component.onCompleted: if (ready) grow = 1

    function paint(ctx, w, h, fill) {
        ctx.reset()
        if (!wf.ready || w <= 0 || h <= 0) return
        ctx.fillStyle = fill
        var v = wf.values
        var n = v.length
        if (wf.style === "line") {
            var mid = h / 2
            ctx.beginPath()
            ctx.moveTo(0, mid)
            var steps = Math.max(2, Math.floor(w / 2))
            for (var i = 0; i <= steps; ++i) {
                var a = v[Math.min(n - 1, Math.floor(i / steps * n))]
                ctx.lineTo(i / steps * w, mid - Math.max(0.5, a * mid * 0.95))
            }
            for (var j = steps; j >= 0; --j) {
                var b = v[Math.min(n - 1, Math.floor(j / steps * n))]
                ctx.lineTo(j / steps * w, mid + Math.max(0.5, b * mid * 0.95))
            }
            ctx.closePath()
            ctx.fill()
            return
        }
        var step = wf.barWidth + wf.barGap
        var count = Math.max(1, Math.floor((w + wf.barGap) / step))
        for (var k = 0; k < count; ++k) {
            // max over the source values covered by this bar
            var s0 = Math.floor(k / count * n), s1 = Math.max(s0 + 1, Math.floor((k + 1) / count * n))
            var m = 0
            for (var q = s0; q < s1 && q < n; ++q) m = Math.max(m, v[q])
            var bh = Math.max(wf.style === "mirror" ? 2 : 2, m * h)
            var x = k * step
            if (wf.style === "mirror")
                ctx.fillRect(x, (h - bh) / 2, wf.barWidth, bh)
            else
                ctx.fillRect(x, h - bh, wf.barWidth, bh)
        }
    }

    Item {
        id: shape
        width: parent.width
        height: parent.height
        transform: Scale { origin.y: wf.style === "bars" ? shape.height : shape.height / 2; yScale: wf.grow }
        Canvas {
            id: base
            anchors.fill: parent
            onPaint: wf.paint(getContext("2d"), width, height, wf.dimColor)
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }
        Item {
            width: parent.width * wf.frac
            height: parent.height
            clip: true
            Canvas {
                id: played
                width: shape.width
                height: shape.height
                onPaint: {
                    var ctx = getContext("2d")
                    var g = ctx.createLinearGradient(0, 0, width, 0)
                    g.addColorStop(0, wf.color1)
                    g.addColorStop(1, wf.color2)
                    wf.paint(ctx, width, height, g)
                }
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
            }
        }
    }
    Connections {
        target: Theme
        function onChanged() { base.requestPaint(); played.requestPaint() }
        function onAccentChanged() { played.requestPaint() }
    }
    onDimColorChanged: base.requestPaint()
    onColor1Changed: played.requestPaint()
    onColor2Changed: played.requestPaint()
    onStyleChanged: { base.requestPaint(); played.requestPaint() }

    // play head
    Rectangle {
        visible: wf.showHead && wf.ready && Player.duration > 0
        x: wf.width * wf.frac - 1
        width: 2
        height: wf.height
        radius: 1
        color: wf.color2
    }
    // hover / drag marker
    Rectangle {
        visible: wf.interactive && (hover.containsMouse || hover.pressed) && Player.duration > 0
        x: Math.max(0, Math.min(wf.width, hover.mouseX)) - 1
        width: 2
        height: wf.height + 6
        y: -3
        radius: 1
        color: Theme.text
        opacity: 0.7
    }
    MouseArea {
        id: hover
        anchors.fill: parent
        anchors.topMargin: -6
        anchors.bottomMargin: -6
        enabled: wf.interactive
        hoverEnabled: true
        cursorShape: Player.duration > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
        function fracAt(x) { return Math.max(0, Math.min(1, x / wf.width)) }
        onPressed: (m) => wf.dragFrac = fracAt(m.x)
        onPositionChanged: (m) => { if (pressed) wf.dragFrac = fracAt(m.x) }
        onReleased: {
            if (Player.duration > 0)
                Player.seek(Math.round(wf.dragFrac * Player.duration))
            wf.dragFrac = -1
        }
        onCanceled: wf.dragFrac = -1
    }
}
