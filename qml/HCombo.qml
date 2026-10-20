// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Drop-down choice: options [{value, label}].
AbstractButton {
    id: c
    property var options: []
    property var current
    signal picked(var value)
    readonly property string currentLabel: {
        for (var i = 0; i < options.length; ++i)
            if (String(options[i].value) === String(current)) return options[i].label
        return ""
    }
    implicitHeight: 38
    implicitWidth: Math.max(110, label.implicitWidth + 50)
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    onClicked: menu.popup(c, 0, c.height + 4)

    background: Surface {
        radius: Math.min(19, Theme.radius + 9)
        hovered: c.hovered
        pressed: c.pressed
        focused: c.visualFocus
        flatColor: c.pressed ? Theme.alpha(Theme.text, 0.14) : c.hovered ? Theme.alpha(Theme.text, 0.1) : Theme.alpha(Theme.text, 0.06)
        flatBorderWidth: 1
        flatBorder: menu.visible ? Theme.accent : Theme.alpha(Theme.text, 0.1)
    }
    contentItem: Item {
        Text {
            id: label
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.right: arrow.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: c.currentLabel
            elide: Text.ElideRight
            font.family: Theme.uiFont
            font.pixelSize: 14
            color: Theme.text
        }
        Icon {
            id: arrow
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            name: "down"
            size: 18
            color: Theme.textDim
        }
    }
    HMenu {
        id: menu
        width: Math.max(c.width, 200)
        Repeater {
            model: c.options
            HMenuItem {
                text: modelData.label
                checkable: true
                checked: String(modelData.value) === String(c.current)
                onTriggered: c.picked(modelData.value)
            }
        }
    }
}
