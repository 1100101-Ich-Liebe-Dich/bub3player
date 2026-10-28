// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// A bar chart in the look of the theme: LED columns on the Hi-Fi tower, block
// characters in the terminal, amber lamps for Vintage, neon for Synthwave, soft
// pressed slots for Soft, frosted glass for Glas, glowing gradients for Aurora,
// plain bars for Klassik. Hovering a bar shows its value (tipText).
Item {
    id: sb
    property var values: []
    property var labels: []
    property int highlight: -1            // the bar that stands out (best month, today …)
    property var tipText: function (i) { return "" }
    property int labelEvery: 1            // a label under every n-th bar
    property color color: "transparent"   // set: bars in this colour (on a colour field), else the theme's
    property color labelColor: Theme.textFaint
    property color slot: Theme.alpha("#000000", 0.35) // the dark slot behind lamp bars
    property real gap: values.length > 20 ? 2 : 6
    readonly property string kind: window.look.bars
    readonly property real maxValue: {
        var m = 0
        for (var i = 0; i < values.length; ++i) m = Math.max(m, values[i])
        return m
    }
    readonly property real labelHeight: labels.length > 0 ? 20 : 0
    readonly property real barWidth: values.length > 0 ? Math.max(1, (width - gap * (values.length - 1)) / values.length) : 0
    property int hovered: -1

    // grows in when the numbers change
    property real grow: 0
    onValuesChanged: { grow = 0; growAnim.restart() }
    NumberAnimation { id: growAnim; target: sb; property: "grow"; from: 0; to: 1; duration: window.animationsActive ? 700 : 0; easing.type: Easing.OutCubic }

    function frac(i) { return sb.maxValue > 0 ? sb.values[i] / sb.maxValue * sb.grow : 0 }

    Repeater {
        model: sb.values.length
        delegate: Item {
            id: col
            readonly property real f: sb.frac(index)
            readonly property bool strong: index === sb.highlight || index === sb.hovered
            x: index * (sb.barWidth + sb.gap)
            width: sb.barWidth
            height: sb.height - sb.labelHeight
            Loader {
                anchors.fill: parent
                sourceComponent: sb.kind === "blocks" ? blocks : sb.kind === "led" ? led : sb.kind === "lamp" ? lamp
                               : sb.kind === "neu" ? neu : plain
                property real f: col.f
                property bool strong: col.strong
            }
            Text {
                visible: sb.labelHeight > 0 && index % sb.labelEvery === 0
                anchors.top: parent.bottom
                anchors.topMargin: 4
                anchors.horizontalCenter: parent.horizontalCenter
                text: sb.labels[index] !== undefined ? sb.labels[index] : ""
                font.family: window.look.monoLabels ? Theme.monoFont : Theme.uiFont
                font.pixelSize: 11
                font.capitalization: window.look.upper ? Font.AllUppercase : Font.MixedCase
                color: col.strong ? (sb.color.a > 0 ? sb.color : Theme.text) : sb.labelColor
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onContainsMouseChanged: sb.hovered = containsMouse ? index : (sb.hovered === index ? -1 : sb.hovered)
            }
        }
    }

    // the value of the bar under the mouse
    Rectangle {
        visible: sb.hovered >= 0 && tipLabel.text !== ""
        readonly property real cx: sb.hovered * (sb.barWidth + sb.gap) + sb.barWidth / 2
        x: Math.max(0, Math.min(sb.width - width, cx - width / 2))
        y: -height - 4
        width: tipLabel.implicitWidth + 14
        height: tipLabel.implicitHeight + 8
        radius: window.look.square ? 0 : 6
        color: Theme.surface
        border.color: Theme.border
        Text {
            id: tipLabel
            anchors.centerIn: parent
            text: sb.hovered >= 0 ? (sb.labels[sb.hovered] !== undefined ? sb.labels[sb.hovered] + ": " : "") + sb.tipText(sb.hovered) : ""
            font.family: Theme.uiFont
            font.pixelSize: 12
            color: Theme.text
        }
    }

    // ---- the looks ----
    Component {
        id: plain // Klassik, Glas, Aurora, Synthwave: a rounded bar, the colours from StatLook
        Item {
            Rectangle {
                id: glowBar
                visible: window.look.glow && strong && f > 0
                anchors.horizontalCenter: bar.horizontalCenter
                anchors.bottom: parent.bottom
                width: bar.width + 8
                height: bar.height + 6
                radius: bar.radius + 4
                color: Theme.alpha(Theme.accent, 0.25)
            }
            Rectangle {
                id: bar
                anchors.bottom: parent.bottom
                width: parent.width
                height: Math.max(f > 0 ? 2 : 0, parent.height * f)
                radius: window.look.square ? 0 : Math.min(width / 2, 6)
                opacity: strong ? 1 : window.look.dimBars
                gradient: Gradient {
                    GradientStop { position: 0; color: sb.color.a > 0 ? sb.color : window.look.barTop }
                    GradientStop { position: 1; color: sb.color.a > 0 ? sb.color : window.look.barBottom }
                }
                border.width: window.look.barBorder.a > 0 ? 1 : 0
                border.color: window.look.barBorder
            }
        }
    }
    Component {
        id: blocks // Terminal: a column of block characters, the top one partly filled
        Text {
            readonly property int rows: Math.max(1, Math.floor(height / 15))
            readonly property int across: Math.max(1, Math.floor(width / 9)) // characters side by side
            readonly property real cells: f * rows
            text: {
                var full = Math.floor(cells)
                var part = Math.round((cells - full) * 8)
                var lines = []
                for (var r = rows - 1; r >= 0; --r)
                    lines.push((r < full ? "█" : (r === full && part > 0 ? " ▁▂▃▄▅▆▇".charAt(part) : " ")).repeat(across))
                return lines.join("\n")
            }
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignBottom
            lineHeightMode: Text.FixedHeight
            lineHeight: 15
            font.family: Theme.monoFont
            font.pixelSize: 15
            color: strong ? Theme.accent : Theme.text
        }
    }
    Component {
        id: led // Hi-Fi tower: segments that light up, the top ones in the second colour
        Column {
            readonly property int count: Math.max(1, Math.floor((height + 2) / 6))
            readonly property int lit: Math.round(f * count)
            spacing: 2
            Repeater {
                model: parent.count
                Rectangle {
                    readonly property int level: parent.count - 1 - index // from the bottom
                    width: parent.width
                    height: 4
                    radius: 1
                    color: level < parent.lit ? (level >= parent.count * 0.8 ? Theme.accent2 : Theme.accent) : Theme.alpha(Theme.accent, 0.08)
                    opacity: level < parent.lit && !strong ? 0.85 : 1
                }
            }
        }
    }
    Component {
        id: lamp // Vintage: amber light in a dark slot, like the scale of an old receiver
        Rectangle {
            radius: 3
            color: sb.slot
            border.color: Theme.alpha(Theme.text, 0.08)
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 4
                anchors.bottomMargin: 2
                height: Math.max(0, (parent.height - 4) * f)
                radius: 2
                opacity: strong ? 1 : 0.8
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.lighter(Theme.accent2, 1.25) }
                    GradientStop { position: 1; color: Theme.accent }
                }
            }
        }
    }
    Component {
        id: neu // Soft: a pressed-in slot, the bar raised inside it
        Item {
            NeuBox { anchors.fill: parent; mode: "inset"; radius: Math.min(width / 2, 10); depth: 3 }
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 3
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(2, parent.width - 6)
                height: Math.max(0, (parent.height - 6) * f)
                radius: Math.min(width / 2, 8)
                color: strong ? Theme.accent : Theme.alpha(Theme.accent, 0.6)
            }
        }
    }
}
