import QtQuick
import QtQuick.Controls.Basic
import Halcyon

AbstractButton {
    id: tg
    property string label
    checkable: true
    implicitWidth: track.width + 12 + labelText.implicitWidth
    implicitHeight: 32
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: label
    background: null
    contentItem: Item {
        Rectangle {
            id: track
            width: 42; height: 24; radius: Math.min(12, Theme.radius + 6)
            anchors.verticalCenter: parent.verticalCenter
            color: tg.checked ? Theme.accent : Theme.alpha(Theme.text, tg.hovered ? 0.24 : 0.17)
            border.width: tg.visualFocus ? 2 : 0
            border.color: Theme.text
            Behavior on color { ColorAnimation { duration: 140 } }
            Rectangle {
                width: 18; height: 18; radius: Math.min(9, Theme.radius + 3)
                y: 3
                x: tg.checked ? parent.width - width - 3 : 3
                color: tg.checked ? Theme.onAccent : Theme.text
                Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            }
        }
        Text {
            id: labelText
            anchors.left: track.right
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: tg.label
            elide: Text.ElideRight
            font.family: Theme.uiFont
            font.pixelSize: 14
            color: tg.enabled ? Theme.text : Theme.textFaint
        }
    }
}
