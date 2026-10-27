// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// The heart: marks a track as a favourite. Filled when on, an outline otherwise.
AbstractButton {
    id: h
    property bool on: false
    property real size: 32
    property color onColor: Theme.accent
    property color offColor: Theme.textDim
    property string tip: on ? qsTr("Aus den Favoriten entfernen") : qsTr("Zu den Favoriten")

    implicitWidth: size
    implicitHeight: size
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: tip
    Accessible.checkable: true
    Accessible.checked: on

    background: Rectangle {
        radius: width / 2
        color: h.pressed ? Theme.alpha(Theme.text, 0.14) : h.hovered ? Theme.alpha(Theme.text, 0.07) : "transparent"
        border.width: h.visualFocus ? 2 : 0
        border.color: Theme.accent
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: h.on ? "heart" : "heart-outline"
            size: Math.round(h.size * 0.56)
            color: h.on ? h.onColor : (h.hovered ? Theme.text : h.offColor)
            scale: h.pressed ? 0.85 : 1
            Behavior on scale { NumberAnimation { duration: 120 } }
        }
    }
    Loader {
        active: h.tip !== "" && h.hovered
        sourceComponent: Tip { visible: true; text: h.tip }
    }
}
