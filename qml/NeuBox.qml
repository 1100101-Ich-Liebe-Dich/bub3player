import QtQuick
import QtQuick.Window
import Halcyon

// Soft-UI surface (neumorphism), see neu.frag.
// mode: "raised" | "inset" | "convex" | "flat"
Item {
    id: nb
    property string mode: "raised"
    property real radius: Theme.radius
    property real depth: Theme.opt.depth !== undefined ? Theme.opt.depth : (Theme.style.neuDepth !== undefined ? Theme.style.neuDepth : 6)
    property real strength: 1.0
    property color fill: Theme.bg
    property color lightColor: Theme.paletteData.light || Qt.lighter(Theme.bg, 1.18)
    property color darkColor: Theme.paletteData.shadow || Qt.darker(Theme.bg, 1.32)
    readonly property int margin: Math.ceil(depth * 3 + 2)
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software

    ShaderEffect {
        visible: nb.canShade
        x: -nb.margin
        y: -nb.margin
        width: nb.width + 2 * nb.margin
        height: nb.height + 2 * nb.margin
        property size itemSize: Qt.size(width, height)
        property real margin: nb.margin
        property real radius: nb.radius
        property real dist: nb.mode === "inset" ? nb.depth * 0.6 : nb.depth
        property real blur: nb.depth * 2.0
        property real mode: nb.mode === "inset" ? 1 : nb.mode === "convex" ? 2 : nb.mode === "flat" ? 3 : 0
        property real strength: nb.strength
        property color base: Theme.bg
        property color light: nb.lightColor
        property color dark: nb.darkColor
        property color fill: nb.fill
        fragmentShader: "qrc:/shaders/neu.frag.qsb"
    }
    Rectangle {
        visible: !nb.canShade
        anchors.fill: parent
        radius: nb.radius
        color: nb.mode === "inset" ? Qt.darker(nb.fill, 1.06) : nb.fill
        border.color: nb.darkColor
    }
}
