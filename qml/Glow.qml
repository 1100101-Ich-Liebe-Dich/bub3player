import QtQuick

// Cheap glow: renders `target` into a tiny texture and stretches it behind
// the original, which blurs it. Updates only when the target changes.
ShaderEffectSource {
    id: g
    property Item target
    property real spread: 1.0       // >1 makes the glow larger than the item
    property int downscale: 5
    sourceItem: target
    hideSource: false
    live: true
    smooth: true
    textureSize: target ? Qt.size(Math.max(4, Math.round(target.width / downscale)), Math.max(4, Math.round(target.height / downscale))) : Qt.size(4, 4)
    width: target ? target.width * spread : 0
    height: target ? target.height * spread : 0
    x: target ? target.x - (width - target.width) / 2 : 0
    y: target ? target.y - (height - target.height) / 2 : 0
}
