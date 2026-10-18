import QtQuick
import QtQuick.Window

// Round soft light (glow behind orbs and covers).
ShaderEffect {
    id: g
    property color color: "white"
    property real softness: 1.0
    visible: GraphicsInfo.api !== GraphicsInfo.Software && color.a > 0.001
    fragmentShader: "qrc:/shaders/softglow.frag.qsb"
}
