import QtQuick
import QtQuick.Window
import Halcyon

// Circular progress with a two-colour sweep and glow.
ShaderEffect {
    property real progress: 0
    property real thickness: 0.08
    property real glow: 0.04
    property color color1: Theme.accent
    property color color2: Theme.accent2
    property color track: Theme.alpha(Theme.text, 0.15)
    visible: GraphicsInfo.api !== GraphicsInfo.Software
    fragmentShader: "qrc:/shaders/ring.frag.qsb"
}
