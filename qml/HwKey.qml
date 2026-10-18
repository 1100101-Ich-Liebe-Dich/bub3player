import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Hardware key with bevel, travel when pressed and an LED above it.
AbstractButton {
    id: key
    property string iconName
    property string label
    property bool lit: false
    property color ledColor: "#7CFF6B"
    property bool lightKey: true      // brushed metal key vs. black plastic key
    implicitWidth: 56
    implicitHeight: 46
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: label

    // LED
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 0
        width: 7; height: 7; radius: 3.5
        color: key.lit ? key.ledColor : "#2a2420"
        border.color: Qt.rgba(0, 0, 0, 0.5)
        SoftGlow {
            anchors.centerIn: parent
            width: 26; height: 26
            visible: key.lit
            color: Qt.rgba(key.ledColor.r, key.ledColor.g, key.ledColor.b, 0.55)
        }
    }
    background: Item {
        y: 12
        width: key.width
        height: key.height - 12
        Rectangle { // shadow / slot
            anchors.fill: parent
            anchors.topMargin: 2
            radius: 4
            color: Qt.rgba(0, 0, 0, 0.55)
        }
        Rectangle {
            id: cap
            width: parent.width
            height: parent.height - (key.pressed ? 1 : 3)
            y: key.pressed ? 2 : 0
            radius: 4
            gradient: Gradient {
                GradientStop { position: 0; color: key.lightKey ? (key.pressed ? "#9da0a4" : "#d9dbde") : (key.pressed ? "#141414" : "#2d2d30") }
                GradientStop { position: 1; color: key.lightKey ? (key.pressed ? "#c8cacd" : "#a7aaae") : (key.pressed ? "#242426" : "#151517") }
            }
            border.color: key.visualFocus ? Theme.accent : Qt.rgba(0, 0, 0, 0.45)
            border.width: key.visualFocus ? 2 : 1
            Rectangle { // top highlight
                x: 2; y: 1
                width: parent.width - 4; height: 1
                color: Qt.rgba(1, 1, 1, key.lightKey ? 0.8 : 0.18)
            }
            Icon {
                anchors.centerIn: parent
                visible: key.iconName !== ""
                name: key.iconName
                size: 18
                color: key.lightKey ? "#2a2522" : "#e8e2d6"
            }
            Text {
                anchors.centerIn: parent
                visible: key.iconName === ""
                text: key.label
                font.family: "Barlow Condensed"
                font.pixelSize: 13
                font.weight: Font.Bold
                font.letterSpacing: 1
                color: key.lightKey ? "#2a2522" : "#e8e2d6"
            }
        }
    }
}
