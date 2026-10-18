import QtQuick
import Halcyon

Rectangle {
    id: toast
    property alias text: label.text
    function show(t) {
        if (!t) return
        label.text = t
        anim.restart()
    }
    width: Math.min(label.implicitWidth + 36, parent ? parent.width - 48 : 600)
    height: label.implicitHeight + 22
    radius: height / 2
    color: Theme.surface
    border.color: Theme.border
    opacity: 0
    visible: opacity > 0
    Text {
        id: label
        anchors.centerIn: parent
        width: parent.width - 36
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        font.family: Theme.uiFont
        font.pixelSize: 13
        color: Theme.text
    }
    SequentialAnimation {
        id: anim
        NumberAnimation { target: toast; property: "opacity"; to: 1; duration: 160 }
        PauseAnimation { duration: 3600 }
        NumberAnimation { target: toast; property: "opacity"; to: 0; duration: 400 }
    }
}
