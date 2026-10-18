import QtQuick
import QtQuick.Controls.Basic
import Halcyon

Slider {
    id: sl
    implicitWidth: 260
    implicitHeight: 28
    background: Rectangle {
        x: sl.leftPadding
        y: sl.topPadding + sl.availableHeight / 2 - height / 2
        width: sl.availableWidth
        height: 5
        radius: 2.5
        color: Theme.alpha(Theme.text, 0.16)
        Rectangle {
            width: sl.visualPosition * parent.width
            height: parent.height
            radius: 2.5
            color: Theme.accent
        }
    }
    handle: Rectangle {
        x: sl.leftPadding + sl.visualPosition * (sl.availableWidth - width)
        y: sl.topPadding + sl.availableHeight / 2 - height / 2
        width: 16; height: 16; radius: 8
        color: Theme.text
        border.width: sl.visualFocus ? 2 : 0
        border.color: Theme.accent
        scale: sl.pressed ? 1.15 : 1
        Behavior on scale { NumberAnimation { duration: 100 } }
    }
}
