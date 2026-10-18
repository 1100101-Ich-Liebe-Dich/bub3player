import QtQuick
import QtQuick.Controls.Basic
import Halcyon

AbstractButton {
    id: b
    property string iconName
    property int iconSize: 20
    property color iconColor: Theme.textDim
    property color activeColor: Theme.accent
    property bool active: false
    property string tip: ""

    implicitWidth: 36
    implicitHeight: 36
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: tip

    background: Rectangle {
        radius: height / 2
        color: b.pressed ? Theme.alpha(Theme.text, 0.14) : b.hovered ? Theme.alpha(Theme.text, 0.07) : "transparent"
        border.width: b.visualFocus ? 2 : 0
        border.color: Theme.accent
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: b.iconName
            size: b.iconSize
            color: !b.enabled ? Theme.textFaint : b.active ? b.activeColor : (b.hovered ? Theme.text : b.iconColor)
        }
        // A small dot marks toggles that are on, so state never relies on colour alone.
        Rectangle {
            visible: b.active
            width: 4; height: 4; radius: 2
            color: b.activeColor
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 2
        }
    }
    Loader {
        active: b.tip !== "" && b.hovered
        sourceComponent: Tip { visible: true; text: b.tip }
    }
}
