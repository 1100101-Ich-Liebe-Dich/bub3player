import QtQuick
import QtQuick.Window

// Cabinet material (walnut, aluminium, carbon, lacquer), see material.frag.
ShaderEffect {
    id: m
    property string material: "walnut"
    property real radius: 6
    property real darken: 0
    readonly property real kind: material === "aluminium" ? 1 : material === "carbon" ? 2 : material === "lacquer" ? 3 : 0
    property size itemSize: Qt.size(width, height)
    readonly property bool lightSurface: material === "aluminium"
    fragmentShader: "qrc:/shaders/material.frag.qsb"
    // Static surface: render once into a texture instead of on every frame.
    layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
    // Software renderer: plain colour.
    Rectangle {
        anchors.fill: parent
        visible: GraphicsInfo.api === GraphicsInfo.Software
        radius: m.radius
        color: m.material === "aluminium" ? "#9ea1a5" : m.material === "carbon" ? "#16171a" : m.material === "lacquer" ? "#08080a" : "#4a2c18"
    }
}
