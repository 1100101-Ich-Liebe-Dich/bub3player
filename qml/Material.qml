import QtQuick
import QtQuick.Window

// Cabinet material (woods, metals, plastics), see material.frag.
ShaderEffect {
    id: m
    property string material: "walnut"
    property real radius: 6
    property real darken: 0
    property bool vertical: false
    property real seed: 0
    property real bevel: 1
    readonly property var kinds: ({ walnut: 0, aluminium: 1, carbon: 2, lacquer: 3, teak: 4, rosewood: 5,
                                    champagne: 6, oak: 7, blackmetal: 8, tolex: 9, grille: 10, plastic: 11, enamel: 12 })
    readonly property real kind: kinds[material] !== undefined ? kinds[material] : 0
    property size itemSize: Qt.size(width, height)
    readonly property bool lightSurface: material === "aluminium" || material === "champagne" || material === "enamel" || material === "oak"
    fragmentShader: "qrc:/shaders/material.frag.qsb"
    // Static surface: render once into a texture instead of on every frame.
    layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
    // Software renderer: plain colour.
    Rectangle {
        anchors.fill: parent
        visible: GraphicsInfo.api === GraphicsInfo.Software
        radius: m.radius
        color: ({ walnut: "#4a2c18", aluminium: "#9ea1a5", carbon: "#16171a", lacquer: "#08080a", teak: "#7a4a24",
                  rosewood: "#3a160c", champagne: "#c4b38f", oak: "#a07c50", blackmetal: "#18181a", tolex: "#151311",
                  grille: "#1d1813", plastic: "#131315", enamel: "#e2dac6" })[m.material] || "#4a2c18"
    }
}
