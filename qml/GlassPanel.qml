// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import Halcyon

// Frosted glass over an animated background texture (see glass.frag).
// `backdrop` is the ShaderEffectSource with the background, `ref` the item that
// backdrop covers (usually the skin root), `light` the mouse in ref coordinates.
Item {
    id: gp
    property var backdrop
    property Item ref
    property real radius: 24
    property real blur: 0.7
    property color tint: Theme.panel
    property real tintAmount: 0.45
    property bool reflex: true
    property point light: Qt.point(-9999, -9999)
    property real shadowAlpha: 0.35
    property real rim: 0.55
    property color rim1: "white"
    property color rim2: "white"
    property int margin: 28
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software

    // Region of this panel inside the backdrop, refreshed on geometry changes.
    property rect region: Qt.rect(0, 0, 1, 1)
    function refresh() {
        if (!ref || ref.width <= 0 || ref.height <= 0) return
        var p = gp.mapToItem(ref, -margin, -margin)
        region = Qt.rect(p.x / ref.width, p.y / ref.height, (width + 2 * margin) / ref.width, (height + 2 * margin) / ref.height)
    }
    onXChanged: refresh()
    onYChanged: refresh()
    onWidthChanged: refresh()
    onHeightChanged: refresh()
    Component.onCompleted: refresh()
    Connections {
        target: gp.ref
        function onWidthChanged() { gp.refresh() }
        function onHeightChanged() { gp.refresh() }
    }
    Timer { interval: 250; running: true; onTriggered: gp.refresh() } // after layouts settle

    ShaderEffect {
        visible: gp.canShade && !!gp.backdrop
        x: -gp.margin
        y: -gp.margin
        width: gp.width + 2 * gp.margin
        height: gp.height + 2 * gp.margin
        property variant source: gp.backdrop
        property vector4d rect: Qt.vector4d(gp.region.x, gp.region.y, gp.region.width, gp.region.height)
        property size itemSize: Qt.size(width, height)
        property real radius: gp.radius
        property real margin: gp.margin
        property real blur: gp.blur
        property real tintAmount: gp.tintAmount
        property real reflex: gp.reflex ? 1.0 : 0.0
        property real shadowAlpha: gp.shadowAlpha
        property real rim: gp.rim
        property color tint: gp.tint
        property color rim1: gp.rim1
        property color rim2: gp.rim2
        property point light: {
            var p = gp.ref ? gp.mapFromItem(gp.ref, gp.light.x, gp.light.y) : Qt.point(-9999, -9999)
            return Qt.point(p.x + gp.margin, p.y + gp.margin)
        }
        fragmentShader: "qrc:/shaders/glass.frag.qsb"
    }
    // Software renderer: plain translucent panel.
    Rectangle {
        visible: !gp.canShade
        anchors.fill: parent
        radius: gp.radius
        color: Theme.alpha(gp.tint, 0.85)
        border.color: Theme.alpha("#ffffff", 0.15)
    }
}
