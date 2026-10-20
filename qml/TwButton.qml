// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// 90s hi-fi button: glossy dark plastic capsule with a chrome ring, the label
// printed above it (or an icon on the cap) and an optional little LED.
AbstractButton {
    id: b
    property string label
    property string iconName
    property bool led: false
    property bool showLed: false
    property color ledColor: "#ff3b2e"
    property bool labelOnCap: false
    property bool round: false
    property color cap: "#26282d"
    implicitWidth: round ? 30 : 44
    implicitHeight: round ? 30 : (labelOnCap ? 20 : 34)
    focusPolicy: Qt.TabFocus
    hoverEnabled: true
    Accessible.name: label !== "" ? label : iconName
    background: Item {
        Text {
            visible: !b.labelOnCap && !b.round && b.label !== ""
            anchors.horizontalCenter: parent.horizontalCenter
            y: 0
            text: b.label
            font.family: "Barlow Condensed"
            font.pixelSize: 9
            font.weight: Font.Bold
            font.letterSpacing: 0.8
            color: "#b9c0c8"
        }
        Rectangle {
            id: capItem
            width: parent.width
            height: b.round ? parent.height : (b.labelOnCap ? parent.height : 16)
            anchors.bottom: parent.bottom
            radius: height / 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#c7ccd2" }
                GradientStop { position: 1; color: "#5b6067" }
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1.5
                radius: height / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: b.pressed ? Qt.darker(b.cap, 1.3) : Qt.lighter(b.cap, b.hovered ? 1.55 : 1.35) }
                    GradientStop { position: 0.5; color: b.pressed ? Qt.darker(b.cap, 1.1) : b.cap }
                    GradientStop { position: 1; color: Qt.darker(b.cap, 1.5) }
                }
                border.color: b.visualFocus ? Theme.accent : "transparent"
                border.width: 2
                Rectangle { // gloss
                    x: parent.width * 0.15; y: 1.5
                    width: parent.width * 0.7; height: parent.height * 0.35
                    radius: height / 2
                    color: Qt.rgba(1, 1, 1, b.pressed ? 0.04 : 0.12)
                }
                Icon {
                    visible: b.iconName !== ""
                    anchors.centerIn: parent
                    name: b.iconName
                    size: Math.round(Math.min(parent.height * 0.7, 16))
                    color: "#e6eaee"
                }
                Text {
                    visible: (b.labelOnCap || b.round) && b.iconName === ""
                    anchors.centerIn: parent
                    text: b.label
                    font.family: "Barlow Condensed"
                    font.pixelSize: b.round ? 10 : 9
                    font.weight: Font.Bold
                    font.letterSpacing: 0.6
                    color: "#e6eaee"
                }
            }
            Rectangle {
                visible: b.showLed
                anchors.right: parent.right
                anchors.rightMargin: b.round ? -2 : 5
                anchors.top: parent.top
                anchors.topMargin: b.round ? -2 : (parent.height - height) / 2
                width: 5; height: 5; radius: 2.5
                color: b.led ? b.ledColor : Qt.darker(b.ledColor, 4)
                SoftGlow { anchors.centerIn: parent; width: 18; height: 18; visible: b.led; color: Qt.rgba(b.ledColor.r, b.ledColor.g, b.ledColor.b, 0.6) }
            }
        }
    }
    contentItem: null
}
