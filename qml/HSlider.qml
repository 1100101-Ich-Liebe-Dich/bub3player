import QtQuick
import QtQuick.Controls.Basic
import Halcyon

Slider {
    id: sl
    implicitWidth: 260
    implicitHeight: 28
    readonly property bool neu: Theme.style.controls === "neu"
    background: Rectangle {
        x: sl.leftPadding
        y: sl.topPadding + sl.availableHeight / 2 - height / 2
        width: sl.availableWidth
        height: sl.neu ? 10 : 5
        radius: height / 2
        color: sl.neu ? "transparent" : Theme.alpha(Theme.text, 0.16)
        NeuBox {
            visible: sl.neu
            anchors.fill: parent
            radius: parent.radius
            mode: "inset"
            depth: 3
        }
        Rectangle {
            x: sl.neu ? 3 : 0
            y: sl.neu ? 3 : 0
            width: sl.visualPosition * (parent.width - (sl.neu ? 6 : 0))
            height: parent.height - (sl.neu ? 6 : 0)
            radius: height / 2
            color: Theme.accent
        }
    }
    handle: Rectangle {
        x: sl.leftPadding + sl.visualPosition * (sl.availableWidth - width)
        y: sl.topPadding + sl.availableHeight / 2 - height / 2
        width: sl.neu ? 22 : 16; height: width; radius: width / 2
        color: sl.neu ? Theme.bg : Theme.text
        NeuBox {
            visible: sl.neu
            anchors.fill: parent
            radius: parent.radius
            mode: "convex"
            depth: 3
        }
        border.width: sl.visualFocus ? 2 : 0
        border.color: Theme.accent
        scale: sl.pressed ? 1.15 : 1
        Behavior on scale { NumberAnimation { duration: 100 } }
    }
}
