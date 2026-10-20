// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// Thin indeterminate bar for queries that take longer than a moment.
Item {
    id: lb
    property bool running: false
    height: 2
    clip: true
    visible: opacity > 0
    opacity: running ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 250 } }
    Rectangle {
        id: bar
        width: lb.width * 0.3
        height: parent.height
        radius: 1
        color: Theme.accent
        NumberAnimation on x {
            running: lb.visible
            loops: Animation.Infinite
            from: -bar.width
            to: lb.width
            duration: 1100
            easing.type: Easing.InOutQuad
        }
    }
}
