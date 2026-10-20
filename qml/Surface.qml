// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// Background for buttons, tabs, nav items and rows. Follows the skin's control
// style (Theme.style.controls): "flat" (default), "neu" (soft UI) or "glass".
Item {
    id: s
    property real radius: 8
    property bool hovered: false
    property bool pressed: false
    property bool active: false     // toggled / current / selected
    property bool primary: false    // main action (accent)
    property bool focused: false
    property bool quiet: false      // invisible at rest (icon buttons, list rows)
    property color flatColor: "transparent"
    property color flatBorder: "transparent"
    property real flatBorderWidth: 0
    readonly property string kind: Theme.style.controls || "flat"
    readonly property bool showing: !quiet || hovered || pressed || active || primary

    Rectangle {
        visible: s.kind === "flat"
        anchors.fill: parent
        radius: s.radius
        color: s.flatColor
        border.width: s.flatBorderWidth
        border.color: s.flatBorder
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    NeuBox {
        visible: s.kind === "neu" && s.showing
        anchors.fill: parent
        radius: s.radius
        depth: s.quiet ? 3 : Math.max(3, (Theme.opt.depth !== undefined ? Theme.opt.depth : 6) - 1)
        mode: s.pressed || (s.active && !s.primary) ? "inset" : (s.primary ? "convex" : (s.quiet && s.hovered ? "raised" : "raised"))
        fill: s.primary ? Theme.accent : Theme.bg
        strength: s.quiet && s.hovered && !s.active && !s.pressed ? 0.6 : 1
    }

    Rectangle {
        visible: s.kind === "glass" && s.showing
        anchors.fill: parent
        radius: s.radius
        color: s.primary ? Theme.alpha(Theme.accent, s.pressed ? 0.95 : (s.hovered ? 0.88 : 0.78))
                         : Qt.rgba(1, 1, 1, s.pressed ? 0.28 : (s.hovered ? 0.2 : (s.active ? 0.2 : 0.1)))
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, s.hovered || s.active ? 0.5 : 0.3)
        Rectangle { // top sheen
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(0, parent.radius - 1)
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.18) }
                GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.0) }
            }
        }
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    Rectangle { // keyboard focus ring, same in every style
        visible: s.focused
        anchors.fill: parent
        anchors.margins: -2
        radius: s.radius + 2
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
    }
}
