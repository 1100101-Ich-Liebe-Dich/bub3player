import QtQuick
import Halcyon

// Rotary volume knob: drag up/down or use the wheel. LED ring shows the value.
Item {
    id: knob
    property real value: Player.volume
    property color ledColor: "#F0A030"
    property bool lightKnob: true
    signal moved(real v)
    implicitWidth: 120
    implicitHeight: 120
    readonly property real minAngle: -135
    readonly property real maxAngle: 135

    // LED ring
    Repeater {
        model: 21
        Rectangle {
            readonly property real t: index / 20
            readonly property real a: (knob.minAngle + t * (knob.maxAngle - knob.minAngle)) * Math.PI / 180
            width: 5; height: 5; radius: 2.5
            x: knob.width / 2 + Math.sin(a) * knob.width * 0.47 - 2.5
            y: knob.height / 2 - Math.cos(a) * knob.height * 0.47 - 2.5
            color: t <= knob.value + 0.001 ? knob.ledColor : Qt.rgba(0, 0, 0, 0.45)
            opacity: t <= knob.value + 0.001 ? 1 : 0.8
        }
    }
    // knob body (static face; only the pointer turns)
    Canvas {
        id: face
        anchors.centerIn: parent
        width: knob.width * 0.76
        height: width
        onPaint: {
            var c = getContext("2d")
            var r = width / 2
            c.reset()
            // shadow
            c.beginPath(); c.arc(r, r + 3, r - 1, 0, Math.PI * 2)
            c.fillStyle = "rgba(0,0,0,0.45)"; c.fill()
            // body: conical brushed metal
            var g = c.createConicalGradient(r, r, Math.PI / 4)
            if (knob.lightKnob) {
                g.addColorStop(0.0, "#f2f3f5"); g.addColorStop(0.12, "#8e9196"); g.addColorStop(0.25, "#e6e8eb")
                g.addColorStop(0.38, "#9a9da2"); g.addColorStop(0.5, "#f0f1f3"); g.addColorStop(0.62, "#8b8e93")
                g.addColorStop(0.75, "#e2e4e7"); g.addColorStop(0.88, "#979a9f"); g.addColorStop(1.0, "#f2f3f5")
            } else {
                g.addColorStop(0.0, "#4a4a4e"); g.addColorStop(0.12, "#18181a"); g.addColorStop(0.25, "#3c3c40")
                g.addColorStop(0.38, "#151517"); g.addColorStop(0.5, "#46464a"); g.addColorStop(0.62, "#141416")
                g.addColorStop(0.75, "#3a3a3e"); g.addColorStop(0.88, "#161618"); g.addColorStop(1.0, "#4a4a4e")
            }
            c.beginPath(); c.arc(r, r, r - 2, 0, Math.PI * 2)
            c.fillStyle = g; c.fill()
            c.lineWidth = 1.5; c.strokeStyle = "rgba(0,0,0,0.5)"; c.stroke()
            // inner cap
            c.beginPath(); c.arc(r, r, r * 0.62, 0, Math.PI * 2)
            var g2 = c.createLinearGradient(0, 0, 0, width)
            g2.addColorStop(0, knob.lightKnob ? "#dcdee1" : "#2c2c30")
            g2.addColorStop(1, knob.lightKnob ? "#a5a8ad" : "#111113")
            c.fillStyle = g2; c.fill()
        }
        Component.onCompleted: requestPaint()
        onWidthChanged: requestPaint()
    }
    Connections { target: knob; function onLightKnobChanged() { face.requestPaint() } }
    // pointer
    Item {
        anchors.centerIn: parent
        width: face.width
        height: face.height
        rotation: knob.minAngle + knob.value * (knob.maxAngle - knob.minAngle)
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height * 0.08
            width: 4; height: parent.height * 0.22; radius: 2
            color: knob.ledColor
        }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.SizeVerCursor
        property real startY
        property real startV
        onPressed: (m) => { startY = m.y; startV = knob.value }
        onPositionChanged: (m) => { if (pressed) knob.moved(Math.max(0, Math.min(1, startV + (startY - m.y) / 200))) }
        onWheel: (w) => knob.moved(Math.max(0, Math.min(1, knob.value + (w.angleDelta.y > 0 ? 0.04 : -0.04))))
    }
}
