import QtQuick
import QtQuick.Window
import Halcyon

// Shows its content through a chromatic split; trigger() adds a short burst
// of horizontal slice glitches (used on track changes).
Item {
    id: gl
    default property alias content: holder.data
    property real split: 0.004
    property color tintA: Theme.accent2
    property color tintB: Theme.accent
    property bool enabledFx: true
    property real amount: 0
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software
    implicitWidth: holder.childrenRect.width
    implicitHeight: holder.childrenRect.height

    function trigger() {
        if (enabledFx && canShade)
            burst.restart()
    }

    Item {
        id: holder
        anchors.fill: parent
    }
    ShaderEffect {
        visible: gl.canShade && gl.enabledFx
        x: -width * 0.05
        width: parent.width * 1.1
        height: parent.height
        property variant source: ShaderEffectSource {
            sourceItem: holder
            sourceRect: Qt.rect(-holder.width * 0.05, 0, holder.width * 1.1, holder.height)
            hideSource: gl.canShade && gl.enabledFx
            live: true
        }
        property real amount: gl.amount
        property real split: gl.split
        property real seed: 0
        property color tintA: gl.tintA
        property color tintB: gl.tintB
        fragmentShader: "qrc:/shaders/glitch.frag.qsb"
        NumberAnimation on seed { from: 0; to: 100; duration: 4000; loops: Animation.Infinite; running: gl.amount > 0.01 }
    }
    SequentialAnimation {
        id: burst
        NumberAnimation { target: gl; property: "amount"; from: 0; to: 1; duration: 60 }
        NumberAnimation { target: gl; property: "amount"; to: 0.35; duration: 160 }
        NumberAnimation { target: gl; property: "amount"; to: 0.9; duration: 80 }
        NumberAnimation { target: gl; property: "amount"; to: 0; duration: 450; easing.type: Easing.OutQuad }
    }
}
