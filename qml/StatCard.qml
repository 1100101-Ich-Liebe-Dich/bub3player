// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// The background of a box on the statistics page, in the theme's material:
// raised soft UI, a line box in the terminal, a dark display window on the Hi-Fi
// tower, a faint surface elsewhere. Children go on top.
Item {
    id: c
    property int padding: 18
    default property alias content: inner.data
    implicitHeight: inner.childrenRect.height + 2 * padding

    NeuBox {
        visible: window.look.card === "neu"
        anchors.fill: parent
        mode: "raised"
        radius: Theme.radius + 4
    }
    Rectangle {
        visible: window.look.card !== "neu"
        anchors.fill: parent
        radius: window.look.square ? 0 : Theme.radius + 4
        color: window.look.card === "box" ? "transparent"
             : window.look.card === "vfd" ? Theme.alpha("#000000", 0.45)
             : window.look.card === "hud" ? Theme.alpha(Theme.panel, 0.6)
             : Theme.alpha(Theme.text, 0.045)
        border.width: window.look.card === "box" || window.look.card === "vfd" || window.look.card === "hud" ? 1 : 0
        border.color: window.look.card === "box" ? Theme.border : Theme.alpha(Theme.accent, window.look.card === "hud" ? 0.7 : 0.25)
    }
    Item {
        id: inner
        x: c.padding
        y: c.padding
        width: c.width - 2 * c.padding
    }
}
