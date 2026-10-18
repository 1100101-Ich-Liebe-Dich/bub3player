import QtQuick
import QtQuick.Controls.Basic

// The page stack every skin embeds (views, albums, search, settings ...).
StackView {
    id: stack
    clip: true
    focus: true
    property int slide: 40
    replaceEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 170 } }
    replaceExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }
    pushEnter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 }
            NumberAnimation { property: "x"; from: stack.slide; to: 0; duration: 240; easing.type: Easing.OutCubic }
        }
    }
    pushExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120 } }
    popEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 } }
    popExit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 140 }
            NumberAnimation { property: "x"; from: 0; to: stack.slide; duration: 160 }
        }
    }
}
