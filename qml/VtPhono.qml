// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import Halcyon

// Vintage turntable, seen from above: wooden plinth, metal top plate, platter
// with strobe dots and lamp, the cover as record label, a tonearm that lowers
// onto the record and travels inwards with the track, speed keys and a
// start/stop key.
Item {
    id: ph
    property string wood: "walnut"
    property string plate: "aluminium"        // aluminium | champagne | blackmetal
    property real rpm: 33.333
    property bool followTrack: true
    property bool showControls: true
    signal speedRequested(string rpm)
    readonly property bool spinning: Player.playing && window.animationsActive && visible
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software
    readonly property bool hasRecord: Player.hasTrack
    readonly property bool darkPlate: plate === "blackmetal"
    readonly property color engrave: darkPlate ? "#d8d2c4" : "#2a241d"

    readonly property real m: Math.round(Math.min(width, height) * 0.045)
    readonly property real pp: Math.min(height - 2 * m - 24, (width - 2 * m) * 0.74)

    // plinth
    Rectangle { anchors.fill: parent; anchors.topMargin: 6; radius: 10; color: Qt.rgba(0, 0, 0, 0.45) }
    Material { anchors.fill: parent; material: ph.wood; radius: 9; seed: 3 }
    // top plate
    Material {
        id: plateItem
        x: ph.m; y: ph.m
        width: ph.width - 2 * ph.m
        height: ph.height - 2 * ph.m
        material: ph.plate
        radius: 4
        seed: 5
    }
    Rectangle { // recess line around the plate
        anchors.fill: plateItem
        anchors.margins: -1
        radius: 5
        color: "transparent"
        border.color: Qt.rgba(0, 0, 0, 0.45)
    }

    // ---------- platter ----------
    Item {
        id: platter
        width: ph.pp
        height: width
        x: ph.m + (plateItem.height - ph.pp) / 2
        anchors.verticalCenter: plateItem.verticalCenter

        Rectangle { // well
            anchors.fill: parent
            anchors.margins: -5
            radius: width / 2
            color: Qt.rgba(0, 0, 0, 0.55)
        }
        Rectangle { // platter edge, machined aluminium
            anchors.fill: parent
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#e9ebee" }
                GradientStop { position: 0.45; color: "#8d9096" }
                GradientStop { position: 0.55; color: "#b9bcc1" }
                GradientStop { position: 1; color: "#5d6066" }
            }
        }
        // everything that turns
        Item {
            id: spinner
            anchors.fill: parent
            Canvas {
                id: dots
                anchors.fill: parent
                renderStrategy: Canvas.Cooperative
                onPaint: {
                    var c = getContext("2d")
                    var r = width / 2
                    c.reset()
                    var rows = [[0.985, 180], [0.962, 174], [0.94, 168]]
                    for (var k = 0; k < rows.length; ++k) {
                        var rr = r * rows[k][0], n = rows[k][1]
                        c.fillStyle = k % 2 === 0 ? "rgba(30,30,34,0.85)" : "rgba(30,30,34,0.6)"
                        for (var i = 0; i < n; ++i) {
                            var a = i / n * Math.PI * 2
                            c.fillRect(r + Math.cos(a) * rr - 0.9, r + Math.sin(a) * rr - 0.9, 1.8, 1.8)
                        }
                    }
                }
                onWidthChanged: requestPaint()
            }
            CoverArt {
                id: label
                visible: ph.hasRecord
                anchors.centerIn: parent
                width: record.width * 0.36
                height: width
                radius: width / 2
                requestSize: 256
                coverId: Player.currentId
                label: Player.album || "BuB3"
            }
            // a mark on the label so the turning is visible on plain covers
            Rectangle {
                visible: ph.hasRecord
                x: parent.width / 2 - 1
                y: parent.height / 2 - label.height * 0.47
                width: 2; height: label.height * 0.1
                color: Qt.rgba(1, 1, 1, 0.5)
            }
            RotationAnimator on rotation {
                from: 0; to: 360
                duration: 60000 / Math.max(1, ph.rpm)
                loops: Animation.Infinite
                running: ph.spinning
            }
        }
        // rubber mat (when no record lies on the platter)
        Canvas {
            id: mat
            visible: !ph.hasRecord
            anchors.centerIn: parent
            width: parent.width * 0.92
            height: width
            onPaint: {
                var c = getContext("2d")
                var r = width / 2
                c.reset()
                c.beginPath(); c.arc(r, r, r, 0, Math.PI * 2)
                c.fillStyle = "#151515"; c.fill()
                c.lineWidth = 1.2
                for (var rr = r * 0.2; rr < r * 0.97; rr += r * 0.045) {
                    c.beginPath(); c.arc(r, r, rr, 0, Math.PI * 2)
                    c.strokeStyle = "rgba(255,255,255,0.06)"; c.stroke()
                }
            }
            onWidthChanged: requestPaint()
        }
        Item {
            id: record
            visible: ph.hasRecord
            anchors.centerIn: parent
            width: parent.width * 0.9
            height: width
            ShaderEffect {
                anchors.fill: parent
                visible: ph.canShade
                property real labelRadius: 0.36
                property color sheenColor: "#fff3e0"
                fragmentShader: "qrc:/shaders/vinyl.frag.qsb"
                layer.enabled: true
            }
            Rectangle {
                visible: !ph.canShade
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: width * 0.32
                border.color: "#111"
            }
        }
        // spindle
        Rectangle {
            anchors.centerIn: parent
            width: Math.max(6, parent.width * 0.024); height: width; radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#f4f4f4" }
                GradientStop { position: 1; color: "#77797c" }
            }
        }
        // strobe lamp at the front left edge
        Item {
            readonly property real a: 135 * Math.PI / 180
            x: platter.width / 2 + Math.cos(a) * platter.width * 0.53 - width / 2
            y: platter.height / 2 + Math.sin(a) * platter.width * 0.53 - height / 2
            width: 22; height: 14
            rotation: 45
            Rectangle { anchors.fill: parent; radius: 3; color: "#202022"; border.color: "#000" }
            Rectangle {
                anchors.centerIn: parent
                width: 12; height: 5; radius: 2
                color: ph.spinning ? "#ff8a2a" : "#4a2a14"
            }
            SoftGlow {
                anchors.centerIn: parent
                width: 70; height: 70
                visible: ph.spinning
                color: Qt.rgba(1, 0.5, 0.15, 0.5)
            }
        }
    }

    // ---------- tonearm ----------
    readonly property point pivot: Qt.point(platter.x + ph.pp * 1.08, platter.y + ph.pp * 0.14)
    readonly property real armLength: ph.pp * 0.83
    readonly property point center: Qt.point(platter.x + ph.pp / 2, platter.y + ph.pp / 2)
    function angleFor(r) {
        var best = 0, bestErr = 1e9
        for (var a = -25; a <= 70; a += 0.25) {
            var rad = a * Math.PI / 180
            var tx = pivot.x - Math.sin(rad) * armLength * 0.97
            var ty = pivot.y + Math.cos(rad) * armLength * 0.97
            var err = Math.abs(Math.hypot(tx - center.x, ty - center.y) - r)
            if (err < bestErr) { bestErr = err; best = a }
        }
        return best
    }
    readonly property real startAngle: angleFor(record.width * 0.47)
    readonly property real endAngle: angleFor(record.width * 0.2)
    readonly property real restAngle: -4
    readonly property real progress: Player.duration > 0 ? Math.min(1, Player.position / Player.duration) : 0
    readonly property bool onRecord: ph.hasRecord && Player.state !== 0
    property real armAngle: !onRecord ? restAngle : (followTrack ? startAngle + (endAngle - startAngle) * progress : startAngle + 2)
    Behavior on armAngle { SpringAnimation { spring: 1.4; damping: 0.4; epsilon: 0.05 } }

    // arm rest post
    Rectangle {
        x: ph.pivot.x - Math.sin(ph.restAngle * Math.PI / 180) * ph.armLength * 0.72 - 9
        y: ph.pivot.y + Math.cos(ph.restAngle * Math.PI / 180) * ph.armLength * 0.72 - 9
        width: 18; height: 18; radius: 9
        gradient: Gradient {
            GradientStop { position: 0; color: "#3a3a3d" }
            GradientStop { position: 1; color: "#121214" }
        }
        border.color: "#000"
        Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: "#5a5a5e" }
    }
    // cue lever
    Item {
        x: ph.pivot.x - ph.pp * 0.2
        y: ph.pivot.y + ph.pp * 0.13
        width: ph.pp * 0.13; height: 12
        Rectangle { anchors.fill: parent; radius: 3; color: Qt.rgba(0, 0, 0, 0.35) }
        Rectangle {
            width: parent.width * 0.55; height: 8; radius: 3
            anchors.verticalCenter: parent.verticalCenter
            x: ph.onRecord ? parent.width - width - 2 : 2
            Behavior on x { NumberAnimation { duration: 300 } }
            gradient: Gradient {
                GradientStop { position: 0; color: "#f0f0f2" }
                GradientStop { position: 1; color: "#8a8c90" }
            }
        }
        Text {
            anchors.top: parent.bottom
            anchors.topMargin: 2
            text: "CUE"
            font.family: "Barlow Condensed"
            font.pixelSize: 9
            font.weight: Font.Bold
            font.letterSpacing: 1
            color: ph.engrave
        }
    }

    Item {
        id: arm
        x: ph.pivot.x
        y: ph.pivot.y
        rotation: ph.armAngle
        // shadow of the arm on the plate
        Rectangle {
            x: 3; y: -ph.armLength * 0.08 + 6
            width: 7; height: ph.armLength * 0.94
            radius: 3.5
            color: Qt.rgba(0, 0, 0, 0.28)
        }
        // counterweight
        Rectangle {
            x: -13; y: -ph.armLength * 0.25
            width: 26; height: ph.armLength * 0.17
            radius: 5
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#3a3c40" }
                GradientStop { position: 0.45; color: "#c9ccd1" }
                GradientStop { position: 1; color: "#3f4146" }
            }
            Repeater {
                model: 5
                Rectangle { y: 4 + index * (parent.height - 8) / 5; width: parent.width; height: 1; color: Qt.rgba(0, 0, 0, 0.35) }
            }
        }
        // tube
        Rectangle {
            x: -3.5; y: -ph.armLength * 0.08
            width: 7; height: ph.armLength * 0.92
            radius: 3.5
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#75787e" }
                GradientStop { position: 0.4; color: "#f5f6f8" }
                GradientStop { position: 1; color: "#82858a" }
            }
        }
        // headshell, finger lift, cartridge
        Item {
            y: ph.armLength * 0.83
            rotation: 22
            Rectangle {
                x: -10; y: 0
                width: 20; height: ph.armLength * 0.16
                radius: 3
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#aeb1b6" }
                    GradientStop { position: 0.5; color: "#eceef0" }
                    GradientStop { position: 1; color: "#909398" }
                }
            }
            Rectangle { // finger lift
                x: 9; y: ph.armLength * 0.03
                width: 16; height: 3; radius: 1.5
                color: "#d7d9dc"
                rotation: -20
            }
            Rectangle { // cartridge
                x: -7; y: ph.armLength * 0.075
                width: 14; height: ph.armLength * 0.075
                radius: 2
                color: "#16161a"
                Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 2; width: 8; height: 3; radius: 1; color: ph.onRecord ? "#d0402a" : "#7a2a1a" }
            }
        }
    }
    // pivot base with anti-skate dial
    Item {
        x: ph.pivot.x - 26; y: ph.pivot.y - 26
        width: 52; height: 52
        Rectangle {
            anchors.fill: parent
            radius: 26
            gradient: Gradient {
                GradientStop { position: 0; color: ph.darkPlate ? "#38383c" : "#eceef1" }
                GradientStop { position: 1; color: ph.darkPlate ? "#111113" : "#7f8288" }
            }
            border.color: Qt.rgba(0, 0, 0, 0.5)
        }
        Repeater {
            model: 9
            Rectangle {
                readonly property real a: (-120 + index * 30) * Math.PI / 180
                x: 26 + Math.sin(a) * 21 - 0.75
                y: 26 - Math.cos(a) * 21 - 2
                width: 1.5; height: 4
                rotation: -120 + index * 30
                color: ph.darkPlate ? "#bbb" : "#333"
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: 26; height: 26; radius: 13
            gradient: Gradient {
                GradientStop { position: 0; color: "#2c2c30" }
                GradientStop { position: 1; color: "#0d0d0f" }
            }
        }
    }

    // ---------- controls on the plate ----------
    Item {
        visible: ph.showControls && plateItem.width - (platter.x + ph.pp) > 70
        x: platter.x + ph.pp + ph.m * 0.6
        width: plateItem.x + plateItem.width - x - 10
        anchors.bottom: plateItem.bottom
        anchors.bottomMargin: 10
        height: 96

        Text {
            anchors.right: parent.right
            y: 0
            text: "BuB3 PHONO"
            font.family: "Righteous"
            font.pixelSize: 13
            color: ph.engrave
            style: Text.Raised
            styleColor: ph.darkPlate ? Qt.rgba(0, 0, 0, 0.6) : Qt.rgba(1, 1, 1, 0.6)
        }
        Row {
            anchors.right: parent.right
            y: 24
            spacing: 6
            Repeater {
                model: ["33", "45"]
                AbstractButton {
                    width: 30; height: 26
                    readonly property bool on: (ph.rpm > 40) === (modelData === "45")
                    onClicked: ph.speedRequested(modelData)
                    background: Rectangle {
                        radius: 3
                        gradient: Gradient {
                            GradientStop { position: 0; color: parent.parent.pressed ? "#9a9da2" : "#f2f3f5" }
                            GradientStop { position: 1; color: "#a3a6ab" }
                        }
                        border.color: Qt.rgba(0, 0, 0, 0.5)
                    }
                    contentItem: Text {
                        text: modelData
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: "Barlow Condensed"
                        font.pixelSize: 13
                        font.weight: Font.Bold
                        color: "#2a241d"
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.top
                        anchors.bottomMargin: 3
                        width: 5; height: 5; radius: 2.5
                        color: parent.on ? "#ff6a2a" : "#4a2418"
                    }
                }
            }
        }
        AbstractButton {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: Math.min(parent.width, 92); height: 34
            onClicked: Player.toggle()
            background: Rectangle {
                radius: 4
                gradient: Gradient {
                    GradientStop { position: 0; color: parent.parent.pressed ? "#2a2a2c" : "#3d3d41" }
                    GradientStop { position: 1; color: "#141416" }
                }
                border.color: "#000"
                Rectangle { x: 2; y: 1; width: parent.width - 4; height: 1; color: Qt.rgba(1, 1, 1, 0.18) }
            }
            contentItem: Text {
                text: "START·STOP"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: "Barlow Condensed"
                font.pixelSize: 12
                font.weight: Font.Bold
                font.letterSpacing: 1
                color: "#e8dcc6"
            }
        }
    }
}
