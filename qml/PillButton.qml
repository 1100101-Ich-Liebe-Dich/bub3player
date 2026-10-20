// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Primary / secondary text buttons.
AbstractButton {
    id: b
    property string iconName
    property bool primary: false
    property bool compact: false

    implicitHeight: compact ? 32 : 40
    implicitWidth: row.implicitWidth + (compact ? 24 : 32)
    hoverEnabled: true
    focusPolicy: Qt.TabFocus

    background: Surface {
        radius: Math.min(height / 2, Theme.style.buttonRadius !== undefined ? Theme.style.buttonRadius : height / 2)
        hovered: b.hovered
        pressed: b.pressed
        primary: b.primary
        focused: b.visualFocus
        flatColor: b.primary ? (b.pressed ? Qt.darker(Theme.accent, 1.15) : b.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent)
                             : (b.pressed ? Theme.alpha(Theme.text, 0.16) : b.hovered ? Theme.alpha(Theme.text, 0.1) : Theme.alpha(Theme.text, 0.05))
        flatBorderWidth: b.primary ? 0 : 1
        flatBorder: Theme.alpha(Theme.text, 0.12)
    }
    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Icon {
                visible: b.iconName !== ""
                anchors.verticalCenter: parent.verticalCenter
                name: b.iconName
                size: b.compact ? 16 : 18
                color: b.primary ? Theme.onAccent : Theme.text
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: b.text
                font.family: Theme.uiFont
                font.pixelSize: b.compact ? 13 : 14
                font.weight: Font.DemiBold
                color: b.primary ? Theme.onAccent : Theme.text
            }
        }
    }
}
