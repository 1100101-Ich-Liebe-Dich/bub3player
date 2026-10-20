// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import Halcyon

// 3-disc CD changer: the disc spins behind a smoked window (the cover is
// printed on it), transport keys, disc LEDs and a busy "READ" lamp.
Item {
    id: cd
    property color ink: "#45E3FF"
    property bool active: true
    property real beat: 0
    readonly property bool spinning: Player.playing && active && visible
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software

    Unit { anchors.fill: parent }

    Text {
        x: 16; y: 10
        text: "3 DISC CHANGER"
        font.family: "Barlow Condensed"; font.pixelSize: 12; font.weight: Font.Bold; font.italic: true; font.letterSpacing: 1.5
        color: "#d6dce2"
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 16; y: 11
        text: "COMPACT DISC PLAYER  ·  1-BIT DAC"
        font.family: "Barlow Condensed"; font.pixelSize: 10; font.weight: Font.Bold; font.letterSpacing: 1
        color: "#8c949d"
    }

    // window with the spinning disc
    Rectangle {
        id: win
        x: 16
        y: 30
        height: cd.height - 44
        width: Math.min(cd.width * 0.5, height * 1.35)
        radius: 10
        color: "#07080a"
        border.color: "#3b4047"
        border.width: 2
        clip: true
        // tray
        Rectangle {
            anchors.centerIn: disc
            width: disc.width * 1.14; height: width; radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#2a2d33" }
                GradientStop { position: 1; color: "#121417" }
            }
        }
        Item {
            id: disc
            width: win.height - 14
            height: width
            anchors.centerIn: parent
            Item {
                anchors.fill: parent
                CoverArt {
                    anchors.centerIn: parent
                    width: parent.width * 0.68; height: width
                    radius: width / 2
                    requestSize: 256
                    coverId: Player.currentId
                    label: Player.album || "BuB3"
                    visible: Player.currentId > 0
                }
                Rectangle { // a mark so the spin is visible on any cover
                    visible: Player.currentId > 0
                    x: parent.width / 2 - 1.5; y: parent.height * 0.18
                    width: 3; height: parent.height * 0.08; radius: 1.5
                    color: Qt.rgba(1, 1, 1, 0.6)
                }
                RotationAnimator on rotation {
                    from: 0; to: 360
                    duration: 900
                    loops: Animation.Infinite
                    running: cd.spinning
                }
            }
            ShaderEffect {
                anchors.fill: parent
                visible: cd.canShade && Player.currentId > 0
                property real labelRadius: 0.34
                property real lightAngle: -0.8
                property real sparkle: 0
                fragmentShader: "qrc:/shaders/cd.frag.qsb"
                layer.enabled: true
            }
            Rectangle { // centre hole
                visible: Player.currentId > 0
                anchors.centerIn: parent
                width: parent.width * 0.1; height: width; radius: width / 2
                color: "#0a0b0d"
                border.color: Qt.rgba(1, 1, 1, 0.25)
            }
            Text {
                visible: Player.currentId <= 0
                anchors.centerIn: parent
                text: "NO DISC"
                font.family: "Barlow Condensed"; font.pixelSize: 13; font.weight: Font.Bold; font.letterSpacing: 2
                color: "#565c64"
            }
        }
        // smoked glass
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.10) }
                GradientStop { position: 0.4; color: Qt.rgba(1, 1, 1, 0.02) }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.3) }
            }
        }
    }

    // right side: disc LEDs, lamps, keys
    Item {
        anchors.left: win.right
        anchors.leftMargin: 14
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.top: win.top
        anchors.bottom: win.bottom

        Row {
            spacing: 10
            Repeater {
                model: 3
                Column {
                    spacing: 3
                    readonly property bool on: Player.currentId > 0 && (Math.max(0, Queue.currentRow) % 3) === index
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 7; height: 7; radius: 3.5
                        color: parent.on ? "#4dff6a" : "#123a18"
                        SoftGlow { anchors.centerIn: parent; width: 22; height: 22; visible: parent.parent.on; color: Qt.rgba(0.3, 1, 0.4, 0.5) }
                    }
                    Text { text: "DISC " + (index + 1); font.family: "Barlow Condensed"; font.pixelSize: 9; font.weight: Font.Bold; color: "#9aa2ab" }
                }
            }
        }
        Row {
            anchors.right: parent.right
            spacing: 6
            Rectangle {
                id: readLed
                anchors.verticalCenter: parent.verticalCenter
                width: 6; height: 6; radius: 3
                property bool on: false
                color: on ? "#ffb02e" : "#3a2a0e"
                Timer {
                    interval: 70; repeat: true
                    running: cd.spinning
                    onTriggered: readLed.on = Math.random() < 0.55
                    onRunningChanged: if (!running) readLed.on = false
                }
            }
            Text { anchors.verticalCenter: parent.verticalCenter; text: "READ"; font.family: "Barlow Condensed"; font.pixelSize: 9; font.weight: Font.Bold; color: "#9aa2ab" }
        }

        Row {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 26
            spacing: 6
            TwButton { width: 40; label: "SKIP"; iconName: "prev"; onClicked: Player.previous() }
            TwButton { width: 46; label: "PLAY/PAUSE"; iconName: Player.playing ? "pause" : "play"; showLed: true; led: Player.playing; ledColor: "#4dff6a"; onClicked: Player.toggle() }
            TwButton { width: 40; label: "STOP"; iconName: ""; onClicked: Player.stop()
                Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 23; width: 8; height: 8; color: "#e6eaee" } }
            TwButton { width: 40; label: "SKIP"; iconName: "next"; onClicked: Player.next() }
        }
        TwButton {
            anchors.bottom: parent.bottom
            width: 104
            labelOnCap: true
            label: "OPEN / CLOSE"
            enabled: Player.currentId > 0
            onClicked: window.openCurrentAlbum()
        }
        Text {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 2
            text: "DIGITAL"
            font.family: "Barlow Condensed"; font.pixelSize: 11; font.weight: Font.Black; font.italic: true; font.letterSpacing: 2
            color: "#6d757e"
        }
    }

    component Unit: Item {
        Rectangle { anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: -4; radius: 10; color: Qt.rgba(0, 0, 0, 0.6) }
        Material { anchors.fill: parent; material: "plastic"; radius: 10 }
        Rectangle { // chrome trim
            x: 10; width: parent.width - 20; y: 0; height: 2
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#00ffffff" }
                GradientStop { position: 0.5; color: "#c0c8d0" }
                GradientStop { position: 1; color: "#00ffffff" }
            }
        }
    }
}
