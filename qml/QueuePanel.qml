// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

Rectangle {
    id: panel
    property bool transparent: false
    color: transparent ? "transparent" : Theme.alpha(Theme.panel, Theme.ambient > 0 ? 0.86 : 1)
    clip: true

    Rectangle { visible: !panel.transparent; width: 1; height: parent.height; color: Theme.alpha(Theme.border, 0.8) }

    Item {
        id: head
        width: parent.width
        height: 88
        Text {
            x: 24
            y: 22
            // shrinks a little in narrow panels instead of running under the buttons
            width: saveBtn.x - x - 6
            height: 28
            verticalAlignment: Text.AlignVCenter
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 15
            text: qsTr("Warteschlange")
            font.family: Theme.displayFont
            font.pixelSize: 21
            font.weight: Font.DemiBold
            color: Theme.text
        }
        Text {
            x: 24
            y: 50
            width: head.width - 150
            elide: Text.ElideRight
            text: (Queue.radio !== "" ? Queue.radio + " · " : "")
                  + (Queue.count > 0 ? qsTr("Titel %1 von %2").arg(Library.formatCount(Queue.currentRow + 1)).arg(Library.formatCount(Queue.count)) : qsTr("Leer"))
            font.family: Theme.uiFont
            font.pixelSize: 13
            color: Theme.textDim
        }
        IconButton {
            id: saveBtn
            anchors.right: autoBtn.left
            anchors.rightMargin: 2
            y: 22
            iconName: "playlist-add"
            iconSize: 18
            tip: qsTr("Als Playlist speichern")
            enabled: Queue.count > 0
            onClicked: saveMenu.popup(saveBtn, 0, saveBtn.height)
            PlaylistMenu {
                id: saveMenu
                onChosen: (id) => window.playlistUi.addTo(id, function (pid) { Playlists.addQueue(pid) })
            }
        }
        IconButton {
            id: autoBtn
            anchors.right: clearBtn.left
            anchors.rightMargin: 2
            y: 22
            iconName: "sparkle"
            iconSize: 18
            active: Queue.autoplay
            tip: Queue.autoplay ? qsTr("Autoplay an: am Ende geht es mit passender Musik weiter")
                                : qsTr("Autoplay: am Ende passende Titel anhängen")
            onClicked: Queue.autoplay = !Queue.autoplay
        }
        IconButton {
            id: clearBtn
            anchors.right: parent.right
            anchors.rightMargin: 14
            y: 22
            iconName: "delete"
            iconSize: 18
            tip: qsTr("Warteschlange leeren")
            enabled: Queue.count > 0
            onClicked: Queue.clear()
        }
    }

    ListView {
        id: list
        anchors.top: head.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        model: Queue
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HScrollBar {}
        function showCurrent(mode) {
            if (Queue.currentRow >= 0)
                Qt.callLater(function () { list.positionViewAtIndex(Queue.currentRow, mode) })
        }
        Component.onCompleted: showCurrent(ListView.Beginning)
        footer: Column {
            width: list.width
            topPadding: visible ? 12 : 0
            bottomPadding: visible ? 14 : 0
            spacing: 8
            visible: Queue.count > 0 && (autoLine.visible || mixLine.visible)
            Row {
                id: autoLine
                x: 24
                spacing: 8
                visible: Queue.autoplay || Queue.radio !== ""
                Icon { anchors.verticalCenter: parent.verticalCenter; name: Queue.radio !== "" ? "radio" : "sparkle"; size: 16; color: Theme.accent }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: list.width - 72
                    wrapMode: Text.Wrap
                    text: Queue.repeat !== 0 ? qsTr("Autoplay pausiert, solange Wiederholen an ist")
                        : Queue.radio !== "" ? qsTr("Radio: danach geht es passend weiter")
                                             : qsTr("Autoplay: danach geht es passend weiter")
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    color: Theme.textDim
                }
            }
            Row {
                id: mixLine
                x: 24
                spacing: 8
                visible: Player.transition !== 0 && Player.nextTransition !== ""
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "shuffle"; size: 16; color: Theme.textDim }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: list.width - 72
                    wrapMode: Text.Wrap
                    text: qsTr("Nächster Übergang: %1").arg(Player.nextTransition)
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    color: Theme.textDim
                }
            }
        }
        onHeightChanged: if (height > 100) showCurrent(ListView.Beginning)
        Connections {
            target: Queue
            function onCurrentChanged() { if (panel.visible) list.showCurrent(ListView.Contain) }
        }

        delegate: Item {
            id: row
            width: list.width
            height: 52
            opacity: model.isPast ? 0.5 : 1
            Surface {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.topMargin: Theme.style.controls === "neu" ? 4 : 0
                anchors.bottomMargin: Theme.style.controls === "neu" ? 4 : 0
                radius: Theme.radius - 2
                quiet: true
                hovered: mouse.containsMouse && Theme.style.controls !== "neu"
                active: model.isCurrent
                flatColor: model.isCurrent ? Theme.alpha(Theme.accent, 0.13) : mouse.containsMouse ? Theme.alpha(Theme.text, 0.05) : "transparent"
            }
            CoverArt {
                id: art
                x: 20
                anchors.verticalCenter: parent.verticalCenter
                width: 36; height: 36
                radius: 5
                requestSize: 64
                coverId: model.trackId
                label: model.album || ""
                showInitial: false
            }
            Column {
                anchors.left: art.right
                anchors.leftMargin: 12
                anchors.right: tail.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    width: parent.width
                    text: model.title || ""
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: model.isCurrent ? Theme.accent : Theme.text
                }
                Text {
                    width: parent.width
                    text: model.artist || ""
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    color: Theme.textDim
                }
            }
            Item {
                id: tail
                anchors.right: parent.right
                anchors.rightMargin: 18
                width: model.isAuto ? 62 : 44
                height: parent.height
                Icon {
                    // picked by autoplay / radio
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    visible: model.isAuto && !mouse.containsMouse
                    name: "sparkle"
                    size: 13
                    color: Theme.alpha(Theme.accent, 0.8)
                }
                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !mouse.containsMouse
                    text: model.duration || ""
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    color: Theme.textFaint
                }
                IconButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30; height: 30
                    visible: mouse.containsMouse && !model.isCurrent
                    iconName: "close"
                    iconSize: 16
                    tip: qsTr("Entfernen")
                    onClicked: Queue.removeAt(index)
                }
            }
            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                z: -1
                onDoubleClicked: Player.playRow(index)
                onClicked: (m) => {
                    if (m.button !== Qt.RightButton)
                        return
                    rowMenu.row = index
                    rowMenu.trackId = model.trackId
                    rowMenu.album = model.album || ""
                    rowMenu.albumArtist = model.albumArtist || ""
                    rowMenu.fav = Library.isFavorite(model.trackId)
                    rowMenu.popup()
                }
            }
        }
    }

    // Drag to reorder (the remove button on the right stays clickable).
    RowDragArea {
        anchors.fill: list
        list: list
        scrollBarWidth: 70
        onRowClicked: (r, mods) => {}
        onRowDoubleClicked: (r) => Player.playRow(r)
        onMoveRequested: (from, to) => Queue.move(from, to > from ? to - 1 : to)
    }

    // Right click on a row. The row may move while the menu is open (next track,
    // autoplay): actions check that it is still the same track.
    HMenu {
        id: rowMenu
        property int row: -1
        property var trackId: 0
        property string album: ""
        property string albumArtist: ""
        property bool fav: false
        // the same track, and not the one playing (that can change while the menu is open)
        function usable() { return Queue.idAt(row) === trackId && row !== Queue.currentRow }
        HMenuItem {
            text: qsTr("Abspielen")
            enabled: rowMenu.row !== Queue.currentRow
            onTriggered: if (rowMenu.usable()) Player.playRow(rowMenu.row)
        }
        HMenuItem {
            text: rowMenu.fav ? qsTr("Aus den Favoriten entfernen") : qsTr("Zu den Favoriten")
            enabled: rowMenu.trackId > 0 // not for a station or a CD track
            onTriggered: Library.setFavorite(rowMenu.trackId, !rowMenu.fav)
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
        HMenuItem {
            text: qsTr("Zum Album")
            enabled: rowMenu.trackId > 0
            onTriggered: window.openAlbum(rowMenu.albumArtist, rowMenu.album, rowMenu.trackId)
        }
        HMenuItem {
            text: qsTr("Zum Künstler")
            enabled: rowMenu.trackId > 0 && rowMenu.albumArtist !== ""
            onTriggered: window.openArtist(rowMenu.albumArtist, rowMenu.trackId)
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
        HMenuItem {
            text: qsTr("Entfernen")
            enabled: rowMenu.row !== Queue.currentRow
            onTriggered: if (rowMenu.usable()) Queue.removeAt(rowMenu.row)
        }
    }

    EmptyState {
        anchors.centerIn: list
        visible: Queue.count === 0
        width: parent.width - 48
        iconName: "queue"
        title: qsTr("Nichts geplant")
        text: qsTr("Doppelklick auf einen Titel spielt ihn ab, Rechtsklick reiht ihn ein.")
    }
}
