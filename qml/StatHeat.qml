// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// When you listen: 7 weekdays × 24 hours, the more, the brighter. Shades of the
// accent; block characters in the terminal, round lamps on the Hi-Fi tower.
// Drawn in one go (a canvas, or one text in the terminal), not as 168 items.
Item {
    id: hm
    property var values: []               // 168 numbers, Monday 0:00 first
    property var tipText: function (day, hour) { return "" }
    readonly property var days: [qsTr("Mo"), qsTr("Di"), qsTr("Mi"), qsTr("Do"), qsTr("Fr"), qsTr("Sa"), qsTr("So")]
    readonly property bool blocks: window.look.bars === "blocks"
    readonly property real maxValue: {
        var m = 0
        for (var i = 0; i < values.length; ++i) m = Math.max(m, values[i])
        return m
    }
    readonly property int labelWidth: 30
    readonly property real cell: Math.max(6, Math.min((width - labelWidth) / 24, 30))
    property int hovered: -1
    implicitHeight: 7 * cell + 22

    function level(i) { return hm.maxValue > 0 && hm.values.length > i ? hm.values[i] / hm.maxValue : 0 }

    Repeater {
        model: 7
        Text {
            y: index * hm.cell + (hm.cell - height) / 2
            width: hm.labelWidth - 6
            text: hm.days[index]
            horizontalAlignment: Text.AlignRight
            font.family: window.look.monoLabels ? Theme.monoFont : Theme.uiFont
            font.pixelSize: 11
            font.capitalization: window.look.upper ? Font.AllUppercase : Font.MixedCase
            color: Theme.textFaint
        }
    }
    Repeater {
        model: 5 // 0, 6, 12, 18, 24 o'clock
        Text {
            x: hm.labelWidth + index * 6 * hm.cell - (index === 0 ? 0 : width / 2)
            y: 7 * hm.cell + 6
            text: String(index * 6)
            font.family: window.look.monoLabels ? Theme.monoFont : Theme.uiFont
            font.pixelSize: 11
            color: Theme.textFaint
        }
    }

    // the terminal: seven lines of shade characters, two per hour
    Text {
        visible: hm.blocks
        x: hm.labelWidth
        width: 24 * hm.cell
        text: {
            var lines = []
            for (var d = 0; d < 7; ++d) {
                var line = ""
                for (var h = 0; h < 24; ++h) {
                    var v = hm.level(d * 24 + h)
                    line += v <= 0 ? " ·" : " ░▒▓█".charAt(Math.min(4, 1 + Math.floor(v * 3.999))).repeat(2)
                }
                lines.push(line)
            }
            return lines.join("\n")
        }
        lineHeightMode: Text.FixedHeight
        lineHeight: hm.cell
        font.family: Theme.monoFont
        // two characters across one cell
        font.pixelSize: Math.max(6, Math.round(hm.cell / 1.2))
        color: Theme.text
    }

    Canvas {
        id: canvas
        visible: !hm.blocks
        x: hm.labelWidth
        width: 24 * hm.cell
        height: 7 * hm.cell
        readonly property bool led: window.look.bars === "led"
        readonly property color on: Theme.accent
        readonly property color off: Theme.alpha(Theme.text, 0.05)
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var c = hm.cell
            var m = led ? c * 0.18 : 1.5
            var r = led ? (c - 2 * m) / 2 : window.look.square ? 0 : Math.min(4, c / 4)
            for (var i = 0; i < 168; ++i) {
                var v = hm.level(i)
                var x = (i % 24) * c + m, y = Math.floor(i / 24) * c + m, s = c - 2 * m
                ctx.fillStyle = v <= 0 ? off : Qt.rgba(on.r, on.g, on.b, 0.15 + 0.85 * v)
                ctx.beginPath()
                ctx.roundedRect(x, y, s, s, r, r)
                ctx.fill()
            }
        }
        Connections {
            target: hm
            function onValuesChanged() { canvas.requestPaint() }
            function onCellChanged() { canvas.requestPaint() }
        }
        onOnChanged: requestPaint()
        onOffChanged: requestPaint()
    }

    Rectangle { // the cell under the mouse
        visible: hm.hovered >= 0
        x: hm.labelWidth + (hm.hovered % 24) * hm.cell
        y: Math.floor(hm.hovered / 24) * hm.cell
        width: hm.cell
        height: hm.cell
        color: "transparent"
        border.width: 1.5
        border.color: Theme.text
    }
    MouseArea {
        x: hm.labelWidth
        width: 24 * hm.cell
        height: 7 * hm.cell
        hoverEnabled: true
        onPositionChanged: (m) => hm.hovered = Math.min(6, Math.floor(m.y / hm.cell)) * 24 + Math.min(23, Math.floor(m.x / hm.cell))
        onExited: hm.hovered = -1
    }
    Rectangle {
        visible: hm.hovered >= 0 && heatTip.text !== ""
        readonly property real cx: hm.labelWidth + (hm.hovered % 24 + 0.5) * hm.cell
        x: Math.max(0, Math.min(hm.width - width, cx - width / 2))
        y: Math.floor(hm.hovered / 24) * hm.cell - height - 4
        width: heatTip.implicitWidth + 14
        height: heatTip.implicitHeight + 8
        radius: window.look.square ? 0 : 6
        color: Theme.surface
        border.color: Theme.border
        z: 2
        Text {
            id: heatTip
            anchors.centerIn: parent
            text: hm.hovered >= 0 ? hm.tipText(Math.floor(hm.hovered / 24), hm.hovered % 24) : ""
            font.family: Theme.uiFont
            font.pixelSize: 12
            color: Theme.text
        }
    }
}
