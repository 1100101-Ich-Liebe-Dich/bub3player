// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

ScrollBar {
    id: sb
    policy: ScrollBar.AsNeeded
    minimumSize: 0.04
    padding: 3
    contentItem: Rectangle {
        implicitWidth: sb.hovered || sb.pressed ? 8 : 5
        radius: width / 2
        color: sb.pressed ? Theme.alpha(Theme.text, 0.45) : Theme.alpha(Theme.text, sb.hovered ? 0.32 : 0.2)
        opacity: sb.active || sb.hovered ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 300 } }
        Behavior on implicitWidth { NumberAnimation { duration: 120 } }
    }
    background: null
}
