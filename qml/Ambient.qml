// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import Halcyon

// Soft glow in the colours of the current cover, behind the whole window.
// The blur is computed into a 160x100 texture and stretched, so it is cheap
// even while the visualizer animates.
Item {
    id: amb
    property var coverId: 0
    property real strength: 0.5
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software
    visible: strength > 0.01 && coverId !== 0

    Image {
        id: src
        source: amb.coverId !== 0 ? "image://cover/" + amb.coverId : ""
        sourceSize: Qt.size(64, 64)
        asynchronous: true
        visible: false
        onStatusChanged: if (status === Image.Ready) fadeIn.restart()
    }

    ShaderEffect {
        id: small
        width: 160
        height: 100
        property variant source: src
        property real strength: amb.strength
        property real aspect: amb.width / Math.max(1, amb.height)
        fragmentShader: "qrc:/shaders/ambient.frag.qsb"
    }

    ShaderEffectSource {
        id: stretched
        anchors.fill: parent
        visible: amb.canShade
        sourceItem: small
        hideSource: true
        smooth: true
        textureSize: Qt.size(160, 100)
        opacity: 0
    }

    // Software renderer: tiny image scaled up is already a blur.
    Image {
        anchors.fill: parent
        visible: !amb.canShade
        source: src.source
        sourceSize: Qt.size(12, 12)
        fillMode: Image.PreserveAspectCrop
        smooth: true
        opacity: stretched.opacity * amb.strength * 0.6
    }

    NumberAnimation {
        id: fadeIn
        target: stretched
        property: "opacity"
        from: 0.35
        to: 1
        duration: 700
        easing.type: Easing.OutCubic
    }
}
