import QtQuick
import QtQuick.Controls.Basic
import Halcyon

Row {
    id: vol
    spacing: 4
    IconButton {
        anchors.verticalCenter: parent.verticalCenter
        iconName: Player.muted || Player.volume === 0 ? "volume-off" : (Player.volume < 0.5 ? "volume-low" : "volume")
        tip: Player.muted ? qsTr("Ton an") : qsTr("Stumm")
        onClicked: Player.muted = !Player.muted
    }
    Slider {
        id: slider
        anchors.verticalCenter: parent.verticalCenter
        width: 104
        from: 0
        to: 1
        value: Player.volume
        onMoved: Player.volume = value
        wheelEnabled: true
        stepSize: 0.02
        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4
            radius: 2
            color: Theme.alpha(Theme.text, 0.16)
            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: Player.muted ? Theme.textFaint : (slider.hovered ? Theme.accent : Theme.text)
            }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 12
            height: 12
            radius: 6
            color: Theme.text
            visible: slider.hovered || slider.pressed
        }
    }
}
