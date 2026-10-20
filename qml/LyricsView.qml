// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// The lyrics of the playing track, for the big views. Synchronised lyrics follow
// the music: the line being sung sits a little above the middle, lines fade out
// towards the edges, a click on a line jumps there. Scrolling by hand stops the
// following for a few seconds. Plain lyrics just scroll. The skins only choose
// fonts, colours and alignment.
Flickable {
    id: lv
    property string fontFamily: Theme.uiFont
    property int fontSize: 28
    property int fontWeight: Font.Bold
    property color color: Theme.text                       // the line being sung; plain lyrics
    property color dimColor: Theme.alpha(Theme.text, 0.35) // the other lines
    property color sourceColor: dimColor
    property int align: Text.AlignLeft
    property bool upper: false
    property string marker: ""     // in front of the line being sung (Terminal: "> ")
    property real focusAt: 0.4     // where the line being sung sits, as a fraction of the height
    property real fadeEdge: 0.24   // fraction of the height that fades out at the top and the bottom
    property real lineGap: Math.round(fontSize * 0.5)

    readonly property bool synced: Lyrics.synced
    readonly property int current: Lyrics.current
    property bool following: true

    clip: true
    contentWidth: width
    contentHeight: column.height
    // plain lyrics: the first line below the faded edge
    topMargin: synced ? height * focusAt : height * fadeEdge * 0.8
    bottomMargin: synced ? height * (1 - focusAt) : fontSize
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick

    function follow(animated) {
        if (!synced || !following || height <= 0)
            return
        var item = rep.itemAt(Math.max(0, current))
        if (!item)
            return
        var to = item.y + item.height / 2 - height * focusAt
        to = Math.max(-topMargin, Math.min(to, contentHeight - height + bottomMargin))
        if (animated) {
            scrollAnim.to = to
            scrollAnim.restart()
        } else {
            scrollAnim.stop()
            contentY = to
        }
    }
    function restart() {
        resume.stop()
        following = true
        if (synced) {
            follow(false)
        } else {
            scrollAnim.stop()
            contentY = -topMargin
        }
    }

    NumberAnimation {
        id: scrollAnim
        target: lv
        property: "contentY"
        duration: 480
        easing.type: Easing.OutCubic
    }
    Timer {
        id: resume
        interval: 4000
        onTriggered: { lv.following = true; lv.follow(true) }
    }

    onCurrentChanged: follow(true)
    onHeightChanged: Qt.callLater(follow, false)
    onWidthChanged: Qt.callLater(follow, false)
    // dragging, flicking and the wheel (only the listener's scrolling starts a "movement")
    onMovementStarted: { scrollAnim.stop(); following = false; resume.restart() }
    onMovementEnded: resume.restart()
    Connections {
        target: Lyrics
        function onChanged() { Qt.callLater(lv.restart) }
    }
    Component.onCompleted: Qt.callLater(restart)

    Column {
        id: column
        width: lv.width
        spacing: lv.lineGap

        Repeater {
            id: rep
            model: Lyrics.lines
            delegate: Text {
                id: line
                required property int index
                required property string modelData
                readonly property bool isCurrent: lv.synced && index === lv.current
                // fade out towards the edges of the view
                readonly property real mid: y + height / 2 - lv.contentY
                readonly property real edge: Math.max(1, lv.height * lv.fadeEdge)
                width: column.width
                text: (lv.marker !== "" ? (isCurrent ? lv.marker : " ".repeat(lv.marker.length)) : "")
                      + (modelData !== "" ? modelData : (lv.synced ? "♪" : ""))
                wrapMode: Text.Wrap
                horizontalAlignment: lv.align
                font.family: lv.fontFamily
                font.pixelSize: lv.fontSize
                font.weight: lv.fontWeight
                font.capitalization: lv.upper ? Font.AllUppercase : Font.MixedCase
                lineHeight: 1.08
                color: !lv.synced || isCurrent ? lv.color : hit.containsMouse ? Theme.mix(lv.dimColor, lv.color, 0.5) : lv.dimColor
                Behavior on color { ColorAnimation { duration: 260 } }
                opacity: Math.max(0, Math.min(1, mid / edge, (lv.height - mid) / edge))
                MouseArea {
                    id: hit
                    anchors.fill: parent
                    enabled: lv.synced
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { lv.following = true; Lyrics.seekTo(line.index) }
                }
            }
        }
        // where they come from; LRCLIB asks to be named
        Text {
            visible: Lyrics.available
            width: column.width
            topPadding: lv.lineGap
            horizontalAlignment: lv.align
            readonly property var names: ({ lrc: qsTr("Liedtext aus der .lrc-Datei"), tags: qsTr("Liedtext aus den Tags"),
                                              jellyfin: qsTr("Liedtext vom Jellyfin-Server"), lrclib: qsTr("Liedtext von lrclib.net") })
            text: (names[Lyrics.source] || "") + (lv.synced ? "" : qsTr(" · nicht synchron"))
            font.family: lv.fontFamily
            font.pixelSize: Math.max(11, Math.round(lv.fontSize * 0.45))
            font.capitalization: lv.upper ? Font.AllUppercase : Font.MixedCase
            color: lv.sourceColor
            opacity: 0.8
        }
    }
}
