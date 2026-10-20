// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// LED-segment spectrum: each bar is a column of segments that light up.
Item {
    id: led
    property int bars: 28
    property int segments: 10
    property real gap: 3
    property color low: Theme.accent2
    property color high: Theme.accent
    property color off: Theme.alpha(Theme.text, 0.07)
    readonly property var spec: Player.spectrum
    readonly property bool live: Player.spectrumEnabled && Player.playing && spec.length > 0

    Row {
        anchors.fill: parent
        spacing: led.gap
        Repeater {
            model: led.bars
            Column {
                id: bar
                readonly property real level: led.live ? led.spec[Math.min(led.spec.length - 1, Math.floor(index * led.spec.length / led.bars))] : 0
                readonly property int lit: Math.round(level * led.segments)
                width: (led.width - (led.bars - 1) * led.gap) / led.bars
                height: led.height
                spacing: 2
                Repeater {
                    model: led.segments
                    Rectangle {
                        readonly property int seg: led.segments - 1 - index   // 0 = bottom
                        width: bar.width
                        height: (led.height - (led.segments - 1) * 2) / led.segments
                        radius: 1
                        color: seg < bar.lit ? Theme.mix(led.low, led.high, seg / Math.max(1, led.segments - 1)) : led.off
                    }
                }
            }
        }
    }
}
