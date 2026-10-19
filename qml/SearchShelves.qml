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

    GroupModel {
        id: artists
        spec: sh.text.trim() === "" ? ({}) : { search: sh.text, searchIn: "artist", groupBy: "albumArtist", groupSort: "count", limit: 60 }
    }
    GroupModel {
        id: albums
        spec: sh.text.trim() === "" ? ({}) : { search: sh.text, searchIn: "album", groupBy: "album", groupSort: "count", limit: 60 }
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
                    text: shelf.model.count >= 60 ? qsTr("die ersten 60") : String(shelf.model.count)
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
            // the mouse wheel scrolls the row sideways only when it is meant for it (Shift or a sideways wheel)
            WheelHandler {
                acceptedModifiers: Qt.ShiftModifier
                orientation: Qt.Vertical
                property: "contentX"
                target: row
                rotationScale: -1
            }

            delegate: Item {
                id: cellItem
                width: shelf.cell
                height: row.height
                CoverArt {
                    id: art
                    width: shelf.cell
                    height: shelf.cell
                    radius: shelf.round ? width / 2 : Theme.radius
                    requestSize: 256
                    coverId: model.coverId
                    artist: shelf.round ? (model.title || "") : ""
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
                    text: shelf.round ? (model.count === 1 ? qsTr("1 Titel") : qsTr("%1 Titel").arg(model.count)) : (model.subtitle || "")
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
                            ctx.row = index
                            ctx.model = shelf.model
                            ctx.popup()
                        } else {
                            window.openGroup(shelf.model, index, shelf.groupBy)
                        }
                    }
                }
            }
        }
    }

    HMenu {
        id: ctx
        property int row: -1
        property var model
        HMenuItem { text: qsTr("Abspielen"); onTriggered: Library.playGroup(ctx.model, ctx.row, false) }
        HMenuItem { text: qsTr("Zufällig abspielen"); onTriggered: Library.playGroup(ctx.model, ctx.row, true) }
        HMenuItem { text: qsTr("Als Nächstes spielen"); onTriggered: Library.enqueueGroup(ctx.model, ctx.row, true) }
        HMenuItem { text: qsTr("Zur Warteschlange hinzufügen"); onTriggered: Library.enqueueGroup(ctx.model, ctx.row, false) }
        HMenuItem { text: qsTr("Radio dazu starten"); onTriggered: Recommend.startRadioFromGroup(ctx.model, ctx.row) }
        PlaylistMenu {
            onChosen: (id) => {
                var m = ctx.model, r = ctx.row
                window.playlistUi.addTo(id, function (pid) { Playlists.addGroup(pid, m, r) })
            }
        }
    }
}
