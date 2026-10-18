import QtQuick
import QtQuick.Controls.Basic
import Halcyon

MenuItem {
    id: it
    implicitHeight: 34
    leftPadding: 12
    rightPadding: 12
    contentItem: Row {
        spacing: 10
        Text {
            width: 14
            anchors.verticalCenter: parent.verticalCenter
            text: it.checked ? "✓" : ""
            color: Theme.accent
            font.pixelSize: 13
            visible: it.checkable
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: it.text
            font.family: Theme.uiFont
            font.pixelSize: 14
            color: it.enabled ? Theme.text : Theme.textFaint
        }
    }
    background: Rectangle {
        radius: Theme.radius - 4
        color: it.highlighted ? Theme.surfaceHover : "transparent"
    }
}
