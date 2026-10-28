// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// A share as a thin horizontal bar (genres, formats, a top list): "[#####·····]"
// in the terminal, segments on the Hi-Fi tower, a slot on Soft, a bar elsewhere.
Item {
    id: m
    property real value: 0      // 0 … 1
    property bool strong: true
    property color color: "transparent"   // set: in this colour (on a colour field), else the theme's
    implicitHeight: window.look.bars === "blocks" ? meterText.implicitHeight : 6

    Text {
        id: meterText
        visible: window.look.bars === "blocks"
        readonly property int n: Math.max(4, Math.floor(m.width / 9) - 2)
        readonly property int k: Math.round(Math.max(0, Math.min(1, m.value)) * n)
        text: "[" + "#".repeat(k) + "·".repeat(n - k) + "]"
        font.family: Theme.monoFont
        font.pixelSize: 13
        color: m.strong ? Theme.accent : Theme.textDim
    }
    Row {
        visible: window.look.bars === "led"
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        readonly property int count: Math.max(1, Math.floor((m.width + 2) / 6))
        Repeater {
            model: parent.count
            Rectangle {
                width: 4
                height: 6
                radius: 1
                color: index < Math.round(m.value * parent.count) ? (index >= parent.count * 0.8 ? Theme.accent2 : Theme.accent)
                                                                     : Theme.alpha(Theme.accent, 0.08)
            }
        }
    }
    Rectangle {
        visible: window.look.bars !== "blocks" && window.look.bars !== "led"
        anchors.fill: parent
        radius: window.look.square ? 0 : height / 2
        color: window.look.bars === "lamp" ? Theme.alpha("#000000", 0.35) : m.color.a > 0 ? Theme.alpha(m.color, 0.18) : Theme.alpha(Theme.text, 0.08)
        Rectangle {
            width: parent.width * Math.max(0, Math.min(1, m.value))
            height: parent.height
            radius: parent.radius
            opacity: m.strong ? 1 : window.look.dimBars
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: m.color.a > 0 ? m.color : window.look.barBottom }
                GradientStop { position: 1; color: m.color.a > 0 ? m.color : window.look.barTop }
            }
        }
    }
}
