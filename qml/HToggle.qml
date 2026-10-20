// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

AbstractButton {
    id: tg
    property string label
    checkable: true
    implicitWidth: track.width + 12 + labelText.implicitWidth
    // Long labels wrap (when a width is set) instead of being cut off.
    implicitHeight: Math.max(32, labelText.height + 12)
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: label
    background: null
    contentItem: Item {
        Rectangle {
            id: track
            readonly property string kind: Theme.style.controls || "flat"
            width: 42; height: 24; radius: Math.min(12, Theme.radius + 6)
            y: 4
            color: kind === "neu" ? "transparent"
                 : kind === "glass" ? (tg.checked ? Theme.alpha(Theme.accent, 0.85) : Qt.rgba(1, 1, 1, tg.hovered ? 0.24 : 0.16))
                 : (tg.checked ? Theme.accent : Theme.alpha(Theme.text, tg.hovered ? 0.24 : 0.17))
            NeuBox {
                visible: track.kind === "neu"
                anchors.fill: parent
                radius: parent.radius
                mode: "inset"
                depth: 3
                fill: tg.checked ? Theme.mix(Theme.bg, Theme.accent, 0.35) : Theme.bg
            }
            border.width: tg.visualFocus ? 2 : 0
            border.color: Theme.text
            Behavior on color { ColorAnimation { duration: 140 } }
            Rectangle {
                width: 18; height: 18; radius: Math.min(9, Theme.radius + 3)
                y: 3
                x: tg.checked ? parent.width - width - 3 : 3
                color: track.kind === "neu" ? (tg.checked ? Theme.accent : Theme.bg) : (tg.checked ? Theme.onAccent : Theme.text)
                NeuBox {
                    visible: track.kind === "neu"
                    anchors.fill: parent
                    radius: parent.radius
                    mode: "convex"
                    depth: 2
                    fill: tg.checked ? Theme.accent : Theme.bg
                }
                Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            }
        }
        Text {
            id: labelText
            anchors.left: track.right
            anchors.leftMargin: 12
            anchors.right: parent.right
            y: 6
            text: tg.label
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            font.family: Theme.uiFont
            font.pixelSize: 14
            color: tg.enabled ? Theme.text : Theme.textFaint
        }
    }
}
