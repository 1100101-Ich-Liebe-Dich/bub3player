import QtQuick
import QtQuick.Window
import Halcyon

// Chamfered neon panel (see hud.frag). Children go inside.
Item {
    id: hp
    property real chamfer: Theme.style.chamfer !== undefined ? Theme.style.chamfer : 14
    property color fill: Theme.alpha(Theme.panel, 0.74)
    property color border: Theme.alpha(Theme.accent, 0.85)
    property color bracket: Theme.accent2
    property real borderWidth: 1.5
    property real glow: 9
    property real scan: 0.6
    property int margin: 24
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software

    ShaderEffect {
        visible: hp.canShade
        x: -hp.margin
        y: -hp.margin
        width: hp.width + 2 * hp.margin
        height: hp.height + 2 * hp.margin
        property size itemSize: Qt.size(width, height)
        property real margin: hp.margin
        property real chamfer: hp.chamfer
        property real bw: hp.borderWidth
        property real glow: hp.glow
        property real scan: hp.scan
        property color fill: hp.fill
        property color border: hp.border
        property color bracket: hp.bracket
        fragmentShader: "qrc:/shaders/hud.frag.qsb"
        layer.enabled: true // cached until size or colours change
    }
    Rectangle {
        visible: !hp.canShade
        anchors.fill: parent
        color: hp.fill
        border.color: hp.border
        border.width: hp.borderWidth
    }
}
