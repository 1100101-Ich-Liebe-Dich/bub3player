// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Row of exclusive options. options: [{value, label}] or plain strings with `values`.
Flow {
    id: seg
    property var options: []
    property var values: []
    property var current
    signal picked(var value)
    spacing: 4
    function valueAt(i) {
        var o = options[i]
        return (o !== null && typeof o === "object") ? o.value : (values.length > i ? values[i] : o)
    }
    function labelAt(i) {
        var o = options[i]
        return (o !== null && typeof o === "object") ? o.label : o
    }
    Repeater {
        model: seg.options.length
        AbstractButton {
            id: sb
            readonly property bool on: String(seg.valueAt(index)) === String(seg.current)
            height: 34
            width: Math.max(54, segText.implicitWidth + 26)
            hoverEnabled: true
            focusPolicy: Qt.TabFocus
            onClicked: seg.picked(seg.valueAt(index))
            background: Surface {
                radius: Math.min(10, Theme.radius)
                hovered: sb.hovered
                pressed: sb.pressed
                active: sb.on
                primary: sb.on && Theme.style.controls !== "neu"
                focused: sb.visualFocus
                flatColor: sb.on ? Theme.accent : (sb.hovered ? Theme.alpha(Theme.text, 0.12) : Theme.alpha(Theme.text, 0.06))
            }
            contentItem: Text {
                id: segText
                text: seg.labelAt(index)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: sb.on ? (Theme.style.controls === "neu" ? Theme.accent : Theme.onAccent) : Theme.text
            }
        }
    }
}
