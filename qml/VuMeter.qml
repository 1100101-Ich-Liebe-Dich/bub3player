// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// Analog VU meter with a sprung needle (or an LED chain).
// level/peak: 0..1 = -40..0 dBFS. 0 VU is calibrated to -18 dBFS.
Item {
    id: vu
    property real level: 0
    property real peak: 0
    property string style: "classic"       // classic | amber | led
    property string channel: "L"
    property real inertia: 0.45            // 0.1 fast .. 1.0 slow
    property bool active: true

    readonly property real half: 42             // scale spans -half..+half degrees
    readonly property real restAngle: -half
    property real angle: restAngle
    property real vel: 0

    function vuToT(vuv) {
        var lo = Math.pow(10, -20 / 20), hi = Math.pow(10, 3 / 20)
        return (Math.pow(10, vuv / 20) - lo) / (hi - lo)
    }
    function targetAngle() {
        if (vu.level <= 0.001) return restAngle - 2
        var dbfs = vu.level * 40 - 40
        var vuv = Math.max(-24, Math.min(5, dbfs + 18))
        return restAngle + Math.max(-0.04, Math.min(1.08, vuToT(vuv))) * half * 2
    }

    // Needle physics: a slightly under-damped spring, like the real thing.
    Timer {
        interval: 16
        repeat: true
        running: vu.active && vu.style !== "led"
                 && (Player.playing || Math.abs(vu.angle - vu.targetAngle()) > 0.15 || Math.abs(vu.vel) > 0.2)
        onTriggered: {
            var k = 520 - vu.inertia * 440
            var c = 2 * Math.sqrt(k) * 0.62
            var acc = k * (vu.targetAngle() - vu.angle) - c * vu.vel
            vu.vel += acc * 0.016
            vu.angle += vu.vel * 0.016
            vu.angle = Math.max(vu.restAngle - 4, Math.min(vu.restAngle + vu.half * 2 + 6, vu.angle))
        }
    }

    // ---------- analog face ----------
    Rectangle {
        id: bezel
        anchors.fill: parent
        visible: vu.style !== "led"
        radius: 6
        color: "#0c0b0a"
        border.color: Qt.rgba(0, 0, 0, 0.6)

        Item {
            id: faceArea
            anchors.fill: parent
            anchors.margins: 5
            clip: true

            Canvas {
                id: face
                anchors.fill: parent
                property string style: vu.style
                onStyleChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    var c = getContext("2d")
                    var w = width, h = height
                    c.reset()
                    var amber = vu.style === "amber"
                    // face
                    var g = amber ? c.createRadialGradient(w / 2, h * 1.05, h * 0.1, w / 2, h * 0.9, h * 1.1)
                                  : c.createLinearGradient(0, 0, 0, h)
                    if (amber) {
                        g.addColorStop(0, "#ffe2a0"); g.addColorStop(0.55, "#ffb347"); g.addColorStop(1, "#8a4a10")
                    } else {
                        g.addColorStop(0, "#f6ead0"); g.addColorStop(1, "#e2cfa3")
                    }
                    c.fillStyle = g
                    c.fillRect(0, 0, w, h)
                    // scale
                    var px = w / 2, py = h * 1.3, R = h * 1.12
                    var ink = amber ? "#2a1606" : "#1e1a14"
                    function ang(t) { return (-vu.half + t * vu.half * 2) * Math.PI / 180 }
                    function pt(t, r) { var a = ang(t); return [px + Math.sin(a) * r, py - Math.cos(a) * r] }
                    // arc
                    c.lineWidth = 1.6
                    c.strokeStyle = ink
                    c.beginPath(); c.arc(px, py, R * 0.78, -Math.PI / 2 + ang(0), -Math.PI / 2 + ang(vu.vuToT(0))); c.stroke()
                    c.strokeStyle = "#c0281b"
                    c.lineWidth = 4
                    c.beginPath(); c.arc(px, py, R * 0.775, -Math.PI / 2 + ang(vu.vuToT(0)), -Math.PI / 2 + ang(1)); c.stroke()
                    // ticks and numbers
                    var marks = [[-20, "20"], [-10, "10"], [-7, "7"], [-5, "5"], [-3, "3"], [-2, "2"], [-1, "1"], [0, "0"], [1, "1"], [2, "2"], [3, "3"]]
                    c.font = "bold " + Math.round(h * 0.095) + "px 'Barlow Condensed'"
                    c.textAlign = "center"
                    c.textBaseline = "middle"
                    for (var i = 0; i < marks.length; ++i) {
                        var t = vu.vuToT(marks[i][0])
                        var red = marks[i][0] > 0
                        var a = pt(t, R * 0.80), b = pt(t, R * (marks[i][0] % 5 === 0 || marks[i][0] >= -3 ? 0.88 : 0.85))
                        c.strokeStyle = red ? "#c0281b" : ink
                        c.lineWidth = 1.6
                        c.beginPath(); c.moveTo(a[0], a[1]); c.lineTo(b[0], b[1]); c.stroke()
                        var n = pt(t, R * 0.94)
                        c.fillStyle = red ? "#c0281b" : ink
                        c.fillText(marks[i][1], n[0], n[1])
                    }
                    // minor ticks
                    for (var v = -20; v <= 3; v += 1) {
                        var tt = vu.vuToT(v)
                        var a2 = pt(tt, R * 0.8), b2 = pt(tt, R * 0.83)
                        c.strokeStyle = v > 0 ? "#c0281b" : ink
                        c.lineWidth = 1
                        c.beginPath(); c.moveTo(a2[0], a2[1]); c.lineTo(b2[0], b2[1]); c.stroke()
                    }
                    // legends
                    c.fillStyle = ink
                    c.font = "bold " + Math.round(h * 0.17) + "px 'Barlow Condensed'"
                    c.fillText("VU", w / 2, h * 0.64)
                    c.font = "600 " + Math.round(h * 0.09) + "px 'Barlow Condensed'"
                    c.textAlign = "left"
                    c.fillText("−", w * 0.07, h * 0.86)
                    c.textAlign = "right"
                    c.fillStyle = "#c0281b"
                    c.fillText("+", w * 0.93, h * 0.86)
                    c.fillStyle = ink
                    c.textAlign = "left"
                    c.fillText(vu.channel, w * 0.06, h * 0.13)
                }
            }

            // needle
            Item {
                x: faceArea.width / 2
                y: faceArea.height * 1.3
                rotation: vu.angle
                Rectangle {
                    x: -1
                    y: -faceArea.height * 1.08
                    width: 2
                    height: faceArea.height * 1.08
                    antialiasing: true
                    gradient: Gradient {
                        GradientStop { position: 0; color: vu.style === "amber" ? "#1a0d02" : "#111" }
                        GradientStop { position: 0.72; color: vu.style === "amber" ? "#1a0d02" : "#111" }
                        GradientStop { position: 0.73; color: "transparent" }
                    }
                }
            }
            // pivot cover
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: faceArea.height * 0.88
                width: faceArea.width * 0.42
                height: faceArea.height * 0.5
                radius: height / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: "#3a332c" }
                    GradientStop { position: 1; color: "#14110e" }
                }
            }
            // glass reflection
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.0) }
                    GradientStop { position: 0.3; color: Qt.rgba(1, 1, 1, 0.10) }
                    GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0.0) }
                }
            }
        }
        // peak LED
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            width: 8; height: 8; radius: 4
            readonly property bool on: vu.peak > 0.975
            color: on ? "#ff3b2e" : "#4a1410"
            SoftGlow { anchors.centerIn: parent; width: 30; height: 30; visible: parent.on; color: Qt.rgba(1, 0.25, 0.15, 0.6) }
        }
    }

    // ---------- LED chain ----------
    Rectangle {
        anchors.fill: parent
        visible: vu.style === "led"
        radius: 6
        color: "#070707"
        border.color: Qt.rgba(1, 1, 1, 0.06)
        Text {
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            text: vu.channel
            font.family: "Barlow Condensed"
            font.pixelSize: 16
            font.weight: Font.Bold
            color: "#8a8178"
        }
        Row {
            x: 30
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            readonly property int n: 24
            readonly property real segW: (vu.width - 44 - (n - 1) * 3) / n
            readonly property int lit: Math.round(vu.level * n)
            readonly property int peakSeg: Math.min(n - 1, Math.round(vu.peak * n))
            Repeater {
                model: parent.n
                Rectangle {
                    width: parent.segW
                    height: Math.min(vu.height * 0.42, 26)
                    radius: 2
                    readonly property color on: index >= 21 ? "#ff3b2e" : index >= 17 ? "#ffc23d" : "#42e66a"
                    color: index < parent.lit || index === parent.peakSeg && vu.peak > 0.05 ? on : Qt.rgba(on.r, on.g, on.b, 0.1)
                }
            }
        }
    }
}
