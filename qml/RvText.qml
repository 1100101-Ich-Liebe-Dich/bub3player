// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// Text of the year in review. In the terminal it is typed out after `delay` ms,
// with a block cursor while typing; elsewhere it fades in after `delay`.
Text {
    id: t
    property string full
    property int delay: 0
    readonly property bool typed: window.look.enter === "type"
    property real shown: typed ? 0 : full.length
    text: typed ? full.substring(0, Math.floor(shown)) + (shown < full.length ? "█" : "") : full
    opacity: typed ? 1 : 0
    wrapMode: Text.Wrap
    color: Theme.text

    SequentialAnimation {
        id: show
        PauseAnimation { duration: window.animationsActive ? t.delay : 0 }
        ParallelAnimation {
            NumberAnimation {
                target: t; property: "shown"; from: t.typed ? 0 : t.full.length; to: t.full.length
                duration: window.animationsActive && t.typed ? Math.min(1400, t.full.length * 22) : 0
            }
            NumberAnimation {
                target: t; property: "opacity"; to: 1
                duration: window.animationsActive && !t.typed ? 450 : 0
            }
        }
    }
    property bool ready: false
    Component.onCompleted: { ready = true; show.start() }
    onFullChanged: if (ready) show.restart() // new text (the numbers came in): again
}
