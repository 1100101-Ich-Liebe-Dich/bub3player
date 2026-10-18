import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Piano key of a 70s deck: chrome-capped, latches down while `down` is true.
AbstractButton {
    id: key
    property string label
    property string iconName
    property bool latched: false           // latched (e.g. PLAY while playing)
    property color capColor: "#E9E2D2"   // cream plastic
    property color ink: "#2a241d"
    implicitWidth: 64
    implicitHeight: 54
    focusPolicy: Qt.TabFocus
    hoverEnabled: true
    Accessible.name: label
    readonly property bool sunk: pressed || latched
    background: Item {
        // slot
        Rectangle {
            anchors.fill: parent
            radius: 3
            color: "#0e0c0a"
        }
        // key body: the front face shows as a darker band below the top
        Rectangle {
            x: 2
            width: parent.width - 4
            y: key.sunk ? 7 : 2
            height: parent.height - y - 2
            radius: 2
            color: Qt.darker(key.capColor, 1.55)
            Behavior on y { NumberAnimation { duration: 60 } }
        }
        Rectangle {
            id: cap
            x: 2
            width: parent.width - 4
            y: key.sunk ? 7 : 2
            height: parent.height - (key.sunk ? 14 : 12)
            radius: 2
            Behavior on y { NumberAnimation { duration: 60 } }
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.lighter(key.capColor, key.hovered ? 1.08 : 1.03) }
                GradientStop { position: 1; color: Qt.darker(key.capColor, 1.12) }
            }
            border.color: key.visualFocus ? Theme.accent : Qt.rgba(0, 0, 0, 0.35)
            border.width: key.visualFocus ? 2 : 1
            // chrome trim at the front edge
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width; height: 4
                radius: 1
                gradient: Gradient {
                    GradientStop { position: 0; color: "#ffffff" }
                    GradientStop { position: 1; color: "#7d8085" }
                }
            }
            Column {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -2
                spacing: 1
                Icon {
                    visible: key.iconName !== ""
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: key.iconName
                    size: 16
                    color: key.ink
                }
                Text {
                    visible: key.label !== ""
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: key.label
                    font.family: "Barlow Condensed"
                    font.pixelSize: key.iconName !== "" ? 9 : 12
                    font.weight: Font.Bold
                    font.letterSpacing: 1.2
                    color: key.ink
                }
            }
        }
    }
    contentItem: null
}
