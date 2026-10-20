// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Above the search results: matching artists, then matching albums, each a row
// that scrolls sideways (arrows, wheel, drag). The tracks follow below.
Column {
    id: sh
    property string text: ""
    property int inset: 36
    spacing: 18
    bottomPadding: shelvesShown ? 18 : 0
    readonly property bool shelvesShown: artists.count > 0 || albums.count > 0
    readonly property int shown: 60 // per row; one more is asked for to know there are more

    GroupModel {
        id: artists
        spec: sh.text.trim() === "" ? ({}) : { search: sh.text, searchIn: "artist", groupBy: "albumArtist", groupSort: "count", limit: sh.shown + 1 }
    }
    GroupModel {
        id: albums
        spec: sh.text.trim() === "" ? ({}) : { search: sh.text, searchIn: "album", groupBy: "album", groupSort: "count", limit: sh.shown + 1 }
    }

    Shelf {
        title: qsTr("Künstler")
        model: artists
        round: true
        groupBy: "albumArtist"
    }
    Shelf {
        title: qsTr("Alben")
        model: albums
        round: false
        groupBy: "album"
    }
    Text {
        visible: sh.shelvesShown
        x: sh.inset
        text: qsTr("Titel")
        font.family: Theme.displayFont
        font.pixelSize: 20
        font.weight: Font.Bold
        color: Theme.text
    }

    component Shelf: Column {
        id: shelf
        property string title
        property var model
        property bool round: false
        property string groupBy: "album"
        readonly property int cell: 150
        visible: model.count > 0
        width: sh.width
        spacing: 10

        Item {
            x: sh.inset
            width: sh.width - sh.inset * 2
            height: 34
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: shelf.title
                    font.family: Theme.displayFont
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    color: Theme.text
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: shelf.model.count > sh.shown ? qsTr("mehr als %1").arg(sh.shown) : String(shelf.model.count)
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: Theme.textFaint
                }
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                visible: row.contentWidth > row.width
                IconButton {
                    iconName: "prev"
                    iconSize: 18
                    tip: qsTr("Zurück")
                    enabled: !row.atXBeginning
                    onClicked: row.page(-1)
                }
                IconButton {
                    iconName: "next"
                    iconSize: 18
                    tip: qsTr("Weiter")
                    enabled: !row.atXEnd
                    onClicked: row.page(1)
                }
            }
        }

        ListView {
            id: row
            width: sh.width
            height: shelf.cell + 54
            orientation: ListView.Horizontal
            model: shelf.model
            spacing: 14
            leftMargin: sh.inset
            rightMargin: sh.inset
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            cacheBuffer: 300
            // one page: the visible width, ending on a whole cover
            function page(dir) {
                var step = Math.max(shelf.cell + spacing, Math.floor((width - leftMargin) / (shelf.cell + spacing)) * (shelf.cell + spacing))
                var to = Math.max(originX - leftMargin, Math.min(contentX + dir * step, originX + contentWidth + rightMargin - width))
                scrollAnim.to = to
                scrollAnim.restart()
            }
            NumberAnimation { id: scrollAnim; target: row; property: "contentX"; duration: 240; easing.type: Easing.OutCubic }
            // Shift + wheel scrolls the row sideways (a sideways wheel does anyway)
            WheelHandler {
                acceptedModifiers: Qt.ShiftModifier
                target: null
                onWheel: (ev) => {
                    var lo = row.originX - row.leftMargin
                    var hi = Math.max(lo, row.originX + row.contentWidth + row.rightMargin - row.width)
                    row.contentX = Math.max(lo, Math.min(hi, row.contentX - ev.angleDelta.y))
                }
            }

            delegate: Item {
                id: cellItem
                // the one past the limit only tells that there are more
                visible: index < sh.shown
                width: visible ? shelf.cell : 0
                height: row.height
                CoverArt {
                    id: art
                    width: shelf.cell
                    height: shelf.cell
                    radius: shelf.round ? width / 2 : Theme.radius
                    requestSize: 256
                    coverId: model.coverId
                    artist: shelf.round ? String(model.key1 || "") : ""
                    label: model.title || ""
                    scale: hover.containsMouse ? 1.03 : 1
                    Behavior on scale { NumberAnimation { duration: 140 } }
                }
                Text {
                    anchors.top: art.bottom
                    anchors.topMargin: 8
                    width: parent.width
                    horizontalAlignment: shelf.round ? Text.AlignHCenter : Text.AlignLeft
                    text: model.title || ""
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                Text {
                    anchors.top: art.bottom
                    anchors.topMargin: 27
                    width: parent.width
                    horizontalAlignment: shelf.round ? Text.AlignHCenter : Text.AlignLeft
                    text: model.subtitle || "" // artists: "1.234 Titel", albums: the artist
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    color: Theme.textDim
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: (m) => {
                        if (m.button === Qt.RightButton) {
                            ctx.openFor(shelf.model, index)
                        } else {
                            window.openGroup(shelf.model, index, shelf.groupBy)
                        }
                    }
                }
            }
        }
    }

    GroupMenu { id: ctx }
}
