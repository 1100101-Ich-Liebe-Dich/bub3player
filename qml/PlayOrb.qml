// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Big round play/pause button with a gradient and a glow that breathes with the beat.
AbstractButton {
    id: orb
    property color c1: Theme.accent
    property color c2: Theme.accent2
    property real pulse: Player.playing ? Player.beat : 0
    property int iconSize: Math.round(width * 0.42)
    implicitWidth: 56
    implicitHeight: 56
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: Player.playing ? qsTr("Pause") : qsTr("Abspielen")
    onClicked: Player.toggle()

    SoftGlow {
        anchors.centerIn: parent
        width: orb.width * (1.9 + orb.pulse * 0.5)
        height: width
        color: Theme.alpha(orb.c1, orb.hovered ? 0.55 : 0.38)
        softness: 1.0
        z: -1
    }
    background: Rectangle {
        radius: width / 2
        scale: orb.pressed ? 0.93 : (orb.hovered ? 1.04 : 1)
        Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.lighter(orb.c1, 1.12) }
            GradientStop { position: 1; color: orb.c2 }
        }
        border.width: orb.visualFocus ? 2 : 0
        border.color: Theme.text
        // glossy top highlight
        Rectangle {
            x: parent.width * 0.18
            y: parent.height * 0.08
            width: parent.width * 0.64
            height: parent.height * 0.36
            radius: height / 2
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.38) }
                GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0) }
            }
        }
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: Player.playing ? 0 : 2
            name: Player.playing ? "pause" : "play"
            size: orb.iconSize
            color: Theme.onAccent
        }
    }
}
