// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

Column {
    id: es
    property string iconName: "note"
    property string title
    property string text
    property string actionText
    signal action()
    width: Math.min(420, parent ? parent.width - 80 : 420)
    spacing: 10

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: 64; height: 64; radius: 32
        color: Theme.alpha(Theme.accent, 0.12)
        Icon { anchors.centerIn: parent; name: es.iconName; size: 28; color: Theme.accent }
    }
    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: es.title
        font.family: Theme.displayFont
        font.pixelSize: 22
        font.weight: Font.DemiBold
        color: Theme.text
        wrapMode: Text.Wrap
    }
    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: es.text
        font.family: Theme.uiFont
        font.pixelSize: 14
        lineHeight: 1.25
        color: Theme.textDim
        wrapMode: Text.Wrap
    }
    PillButton {
        visible: es.actionText !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        primary: true
        text: es.actionText
        iconName: "add"
        onClicked: es.action()
    }
}
