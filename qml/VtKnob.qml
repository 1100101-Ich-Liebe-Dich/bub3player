import QtQuick
import Halcyon

// Knurled aluminium knob of a 70s receiver.
// Normal mode: value 0..1 over -140°..140° with a printed 0–10 scale.
// Endless mode (tuning): turns freely, reports the angle it was turned by.
Item {
    id: knob
    property real value: 0
    property bool endless: false
    property string label: ""
    property color engrave: "#2a241d"
    property color pointerColor: "#2a241d"
    property bool champagne: false
    signal moved(real v)
    signal turned(real degrees)
    signal released()
    property real freeAngle: 0
    readonly property real minAngle: -140
    readonly property real maxAngle: 140
    readonly property real knobSize: Math.min(width, height) * (endless ? 0.9 : 0.62)

    // printed scale
    Repeater {
        model: knob.endless ? 0 : 11
        Item {
            readonly property real a: (knob.minAngle + index / 10 * (knob.maxAngle - knob.minAngle)) * Math.PI / 180
            readonly property real rr: knob.knobSize / 2 + Math.max(7, knob.width * 0.07)
            x: knob.width / 2 + Math.sin(a) * rr
            y: knob.height / 2 - Math.cos(a) * rr
            Rectangle {
                width: index % 5 === 0 ? 2 : 1.2
                height: index % 5 === 0 ? 6 : 4
                x: -width / 2; y: -height / 2
                rotation: parent.a * 180 / Math.PI
                color: knob.engrave
            }
            Text {
                visible: index % 2 === 0
                x: -width / 2 + Math.sin(parent.a) * 9
                y: -height / 2 - Math.cos(parent.a) * 9
                text: index
                font.family: "Barlow Condensed"
                font.pixelSize: 10
                font.weight: Font.Bold
                color: knob.engrave
            }
        }
    }
    Text {
        visible: knob.label !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: -4
        text: knob.label
        font.family: "Barlow Condensed"
        font.pixelSize: 11
        font.weight: Font.Bold
        font.letterSpacing: 2
        color: knob.engrave
    }

    // body (static face, drawn once)
    Canvas {
        id: face
        anchors.centerIn: parent
        width: knob.knobSize
        height: width
        onPaint: {
            var c = getContext("2d")
            var r = width / 2
            c.reset()
            // drop shadow
            c.beginPath(); c.arc(r + 1, r + 4, r - 2, 0, Math.PI * 2)
            c.fillStyle = "rgba(0,0,0,0.45)"; c.fill()
            // knurled skirt
            var n = Math.round(r * 2.2)
            for (var i = 0; i < n; ++i) {
                var a0 = i / n * Math.PI * 2, a1 = (i + 0.5) / n * Math.PI * 2
                c.beginPath()
                c.moveTo(r, r)
                c.arc(r, r, r - 1, a0, a1)
                c.closePath()
                var lit = 0.5 + 0.5 * Math.cos(a0 + 2.3)
                var v = Math.round(90 + lit * 140)
                c.fillStyle = knob.champagne ? "rgb(" + v + "," + Math.round(v * 0.9) + "," + Math.round(v * 0.72) + ")" : "rgb(" + v + "," + v + "," + Math.round(v * 1.02) + ")"
                c.fill()
            }
            c.beginPath(); c.arc(r, r, r - 1, 0, Math.PI * 2)
            c.lineWidth = 1; c.strokeStyle = "rgba(0,0,0,0.55)"; c.stroke()
            // machined top with concentric (spun) finish
            var top = r * 0.8
            var g = c.createConicalGradient(r, r, Math.PI / 4)
            var s = knob.champagne ? [["#f3e7cc", "#9c8a66"], ["#e6d6b2", "#a8946e"]] : [["#f4f5f7", "#8d9095"], ["#e4e6e9", "#9a9da2"]]
            g.addColorStop(0.0, s[0][0]); g.addColorStop(0.12, s[0][1]); g.addColorStop(0.25, s[1][0])
            g.addColorStop(0.38, s[1][1]); g.addColorStop(0.5, s[0][0]); g.addColorStop(0.62, s[0][1])
            g.addColorStop(0.75, s[1][0]); g.addColorStop(0.88, s[1][1]); g.addColorStop(1.0, s[0][0])
            c.beginPath(); c.arc(r, r, top, 0, Math.PI * 2)
            c.fillStyle = g; c.fill()
            c.lineWidth = 1; c.strokeStyle = "rgba(0,0,0,0.35)"; c.stroke()
            for (var k = 1; k < 7; ++k) {
                c.beginPath(); c.arc(r, r, top * k / 7, 0, Math.PI * 2)
                c.strokeStyle = "rgba(255,255,255," + (0.05 + 0.02 * (k % 2)) + ")"; c.stroke()
            }
        }
        Component.onCompleted: requestPaint()
        onWidthChanged: requestPaint()
    }
    Connections { target: knob; function onChampagneChanged() { face.requestPaint() } }

    // turning parts: pointer line or finger dimple
    Item {
        anchors.centerIn: parent
        width: face.width
        height: face.height
        rotation: knob.endless ? knob.freeAngle : knob.minAngle + knob.value * (knob.maxAngle - knob.minAngle)
        Rectangle {
            visible: !knob.endless
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height * 0.12
            width: 3; height: parent.height * 0.26; radius: 1.5
            color: knob.pointerColor
        }
        Rectangle { // finger dimple of the tuning flywheel
            visible: knob.endless
            x: parent.width * 0.5 - width / 2
            y: parent.height * 0.2
            width: parent.width * 0.2; height: width; radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.35) }
                GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0.35) }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: knob.endless ? Qt.OpenHandCursor : Qt.SizeVerCursor
        property real startY
        property real startV
        property real lastA
        function angleAt(mx, my) { return Math.atan2(mx - width / 2, -(my - height / 2)) * 180 / Math.PI }
        onPressed: (m) => { startY = m.y; startV = knob.value; lastA = angleAt(m.x, m.y) }
        onPositionChanged: (m) => {
            if (!pressed) return
            if (knob.endless) {
                var a = angleAt(m.x, m.y)
                var d = a - lastA
                if (d > 180) d -= 360
                if (d < -180) d += 360
                lastA = a
                knob.freeAngle += d
                knob.turned(d)
            } else {
                knob.moved(Math.max(0, Math.min(1, startV + (startY - m.y) / 180)))
            }
        }
        onReleased: knob.released()
        onWheel: (w) => {
            if (knob.endless) { var d = w.angleDelta.y > 0 ? 12 : -12; knob.freeAngle += d; knob.turned(d); knob.released() }
            else knob.moved(Math.max(0, Math.min(1, knob.value + (w.angleDelta.y > 0 ? 0.04 : -0.04))))
        }
    }
}
