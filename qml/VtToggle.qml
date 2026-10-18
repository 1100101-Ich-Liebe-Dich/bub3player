import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Chrome bat-handle toggle switch with its engraving (label above, ON / OFF).
AbstractButton {
    id: tg
    property string label
    property bool on: false
    property color engrave: "#2a241d"
    property bool lamp: false             // small jewel lamp next to the switch
    property color lampColor: "#ff4a2a"
    implicitWidth: 54
    implicitHeight: 64
    focusPolicy: Qt.TabFocus
    Accessible.name: label
    background: null
    contentItem: Item {
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 0
            text: tg.label
            font.family: "Barlow Condensed"
            font.pixelSize: 10
            font.weight: Font.Bold
            font.letterSpacing: 1.2
            color: tg.engrave
        }
        // mounting nut
        Rectangle {
            id: nut
            anchors.horizontalCenter: parent.horizontalCenter
            y: 30
            width: 20; height: 20; radius: 10
            gradient: Gradient {
                GradientStop { position: 0; color: "#f2f3f5" }
                GradientStop { position: 1; color: "#6c6f74" }
            }
            border.color: tg.visualFocus ? Theme.accent : Qt.rgba(0, 0, 0, 0.5)
            border.width: tg.visualFocus ? 2 : 1
            Rectangle { anchors.centerIn: parent; width: 9; height: 9; radius: 4.5; color: "#2c2d30" }
        }
        // the bat handle, drawn in perspective: long when up, short when down
        Rectangle {
            anchors.horizontalCenter: nut.horizontalCenter
            width: 8
            height: tg.on ? 20 : 13
            y: tg.on ? nut.y + nut.height / 2 - height : nut.y + nut.height / 2
            radius: 4
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#7b7e84" }
                GradientStop { position: 0.4; color: "#ffffff" }
                GradientStop { position: 1; color: "#8b8e93" }
            }
            border.color: Qt.rgba(0, 0, 0, 0.4)
            Behavior on height { NumberAnimation { duration: 70 } }
            Rectangle { // tip
                anchors.horizontalCenter: parent.horizontalCenter
                y: tg.on ? -2 : parent.height - 8
                width: 11; height: 10; radius: 5
                gradient: Gradient {
                    GradientStop { position: 0; color: "#ffffff" }
                    GradientStop { position: 1; color: "#9a9da2" }
                }
                border.color: Qt.rgba(0, 0, 0, 0.35)
            }
        }
        Text {
            anchors.right: nut.left; anchors.rightMargin: 4
            y: nut.y - 4
            text: "ON"
            font.family: "Barlow Condensed"; font.pixelSize: 8; font.weight: Font.Bold
            color: tg.engrave; opacity: 0.8
        }
        Text {
            anchors.right: nut.left; anchors.rightMargin: 4
            y: nut.y + nut.height - 6
            text: "OFF"
            font.family: "Barlow Condensed"; font.pixelSize: 8; font.weight: Font.Bold
            color: tg.engrave; opacity: 0.8
        }
        Rectangle {
            visible: tg.lamp
            anchors.left: nut.right; anchors.leftMargin: 6
            anchors.verticalCenter: nut.verticalCenter
            width: 9; height: 9; radius: 4.5
            color: tg.on ? tg.lampColor : Qt.darker(tg.lampColor, 3.2)
            border.color: Qt.rgba(0, 0, 0, 0.5)
            Rectangle { x: 2; y: 1.5; width: 3; height: 3; radius: 1.5; color: Qt.rgba(1, 1, 1, tg.on ? 0.8 : 0.25) }
            SoftGlow { anchors.centerIn: parent; width: 30; height: 30; visible: tg.on; color: Qt.rgba(tg.lampColor.r, tg.lampColor.g, tg.lampColor.b, 0.5) }
        }
    }
}
