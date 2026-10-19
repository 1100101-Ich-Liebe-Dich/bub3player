import QtQuick
import QtQuick.Controls.Basic
import Halcyon

MenuItem {
    id: it
    implicitHeight: 34
    leftPadding: 12
    rightPadding: 12
    contentItem: Item {
        implicitWidth: row.implicitWidth + (it.subMenu ? 26 : 0)
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.verticalCenter: parent.verticalCenter
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
        Icon {
            visible: !!it.subMenu
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron"
            size: 16
            color: Theme.textDim
        }
    }
    arrow: null
    indicator: null
    background: Rectangle {
        radius: Theme.radius - 4
        color: it.highlighted ? Theme.surfaceHover : "transparent"
    }
}
