// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// A big number of the year in review that counts up from zero. On the Hi-Fi tower
// in seven segments with the unlit "8"s behind, glowing in Aurora and Synthwave.
Item {
    id: n
    property real value: 0
    property int size: 140
    property color color: Theme.accent
    property int delay: 200
    property string suffix: ""
    property bool grouped: true      // "48.213"; false for a year
    property bool counting: true     // false: there at once
    property real shown: 0
    function fmt(v) { return (grouped ? Library.formatCount(Math.round(v)) : String(Math.round(v))) + suffix }
    readonly property string digits: fmt(shown)
    implicitWidth: label.implicitWidth
    implicitHeight: label.implicitHeight

    Text {
        // the unlit segments of a display
        visible: window.look.skin === "tower"
        anchors.right: label.right
        anchors.baseline: label.baseline
        text: n.fmt(n.value).replace(/[0-9]/g, "8")
        font: label.font
        color: Theme.alpha(n.color, 0.1)
    }
    SoftGlow {
        visible: window.look.glow
        anchors.centerIn: label
        width: label.width * 1.25
        height: label.height * 1.1
        color: Theme.alpha(n.color, 0.3)
    }
    Text {
        id: label
        text: n.digits
        font.family: window.look.numberFont
        font.weight: window.look.numberWeight
        font.pixelSize: window.look.skin === "tower" ? n.size * 0.72 : n.size
        font.letterSpacing: window.look.skin === "tower" ? 4 : -2
        color: n.color
    }

    SequentialAnimation {
        id: count
        PauseAnimation { duration: window.animationsActive ? n.delay : 0 }
        NumberAnimation {
            target: n; property: "shown"; from: n.counting ? 0 : n.value; to: n.value
            duration: window.animationsActive && n.counting ? 1400 : 0
            easing.type: Easing.OutCubic
        }
    }
    Component.onCompleted: count.start()
    onValueChanged: count.restart()
}
