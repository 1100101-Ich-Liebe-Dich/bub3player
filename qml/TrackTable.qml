// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Virtualized track list. Rows are fixed height and only visible rows exist,
// so a million entries scroll like a hundred.
Item {
    id: table
    property var model
    property string sortKey: "album"
    property bool sortDesc: false
    property bool albumMode: false      // track numbers instead of covers, no album column
    property string extraColumn: ""     // added | mtime | playCount | lastPlayed
    property alias list: list
    property alias header: list.header
    property bool reorderable: false    // rows can be dragged into a new order (playlists)
    signal sortRequested(string key)
    signal activated(int row)
    signal contextRequested(int row, real x, real y)
    signal moveRequested(int row, int to)

    readonly property bool canSelect: !!table.model && typeof table.model.select === "function"
    // Click with Ctrl toggles, Shift extends from the last clicked row, a plain
    // click clears the selection.
    function rowClick(row, modifiers) {
        list.currentIndex = row
        list.forceActiveFocus()
        if (!canSelect) return
        if (modifiers & Qt.ControlModifier) table.model.select(row, 1)
        else if (modifiers & Qt.ShiftModifier) table.model.select(row, 2)
        else table.model.select(row, 0)
    }

    Connections {
        target: table.model
        ignoreUnknownSignals: true
        function onSpecChanged() { list.pinnedTop = true; list.currentIndex = -1; list.positionViewAtBeginning() }
    }

    readonly property int side: 36
    readonly property int inner: width - side * 2
    readonly property bool showAlbum: !albumMode && inner > 620
    readonly property bool showYear: inner > 760
    readonly property bool showFormat: inner > 900
    readonly property bool showExtra: extraColumn !== "" && inner > 700
    readonly property int wLead: 52
    readonly property int wYear: showYear ? 64 : 0
    readonly property int wFormat: showFormat ? 116 : 0
    readonly property int wExtra: showExtra ? 108 : 0
    readonly property int wDur: 60
    readonly property int wFav: 34
    readonly property int flex: inner - wLead - wYear - wFormat - wExtra - wFav - wDur - 16
    readonly property int wTitle: showAlbum ? Math.round(flex * 0.56) : flex
    readonly property int wAlbum: showAlbum ? flex - wTitle : 0

    function extraLabel() {
        switch (extraColumn) {
        case "added": return qsTr("Hinzugefügt")
        case "mtime": return qsTr("Datei-Datum")
        case "playCount": return qsTr("Gespielt")
        case "lastPlayed": return qsTr("Zuletzt")
        }
        return ""
    }

    ListView {
        id: list
        anchors.fill: parent
        model: table.model
        clip: true
        reuseItems: true
        cacheBuffer: 540
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true
        highlightMoveDuration: 0
        currentIndex: -1
        focus: true
        Keys.onReturnPressed: if (currentIndex >= 0) table.activated(currentIndex)
        Keys.onPressed: (e) => {
            if (table.canSelect && e.key === Qt.Key_A && (e.modifiers & Qt.ControlModifier)) {
                table.model.selectAll()
                e.accepted = true
                return
            }
            if (table.canSelect && e.key === Qt.Key_Escape && table.model.selectionCount > 0) {
                table.model.clearSelection()
                e.accepted = true
                return
            }
            const page = Math.max(1, Math.floor(height / 54) - 1)
            if (e.key === Qt.Key_Home) { currentIndex = 0; positionViewAtBeginning() }
            else if (e.key === Qt.Key_End) { currentIndex = count - 1; positionViewAtEnd() }
            else if (e.key === Qt.Key_PageDown) currentIndex = Math.min(count - 1, Math.max(0, currentIndex) + page)
            else if (e.key === Qt.Key_PageUp) currentIndex = Math.max(0, currentIndex - page)
            else return
            e.accepted = true
        }
        ScrollBar.vertical: HScrollBar {}
        // The header grows once data arrives; stay pinned to the top until the user scrolls.
        property bool pinnedTop: true
        onMovementEnded: pinnedTop = atYBeginning
        Connections {
            target: list.headerItem
            ignoreUnknownSignals: true
            function onHeightChanged() { if (list.pinnedTop) list.positionViewAtBeginning() }
        }

        delegate: Item {
            id: row
            width: list.width
            height: 54
            readonly property bool isCurrent: model.trackId === Player.currentId
            readonly property bool selected: ListView.isCurrentItem || model.selected === true

            Surface {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: Theme.style.controls === "neu" ? 4 : 0
                anchors.bottomMargin: Theme.style.controls === "neu" ? 4 : 0
                radius: Theme.radius - 2
                quiet: true
                hovered: mouse.containsMouse && Theme.style.controls !== "neu"
                active: row.selected || row.isCurrent
                flatColor: model.selected === true ? Theme.alpha(Theme.accent, 0.2)
                         : row.selected ? Theme.alpha(Theme.text, 0.09)
                         : row.isCurrent ? Theme.alpha(Theme.accent, 0.09)
                         : mouse.containsMouse ? Theme.alpha(Theme.text, 0.045) : "transparent"
            }

            Item {
                id: lead
                x: table.side
                width: table.wLead
                height: parent.height
                CoverArt {
                    visible: !table.albumMode
                    anchors.verticalCenter: parent.verticalCenter
                    width: 38
                    height: 38
                    radius: 5
                    requestSize: 64
                    coverId: table.albumMode ? 0 : model.trackId
                    label: model.album || model.title || ""
                    showInitial: false
                }
                Text {
                    visible: table.albumMode && !row.isCurrent
                    anchors.verticalCenter: parent.verticalCenter
                    x: 6
                    text: model.trackNo
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: Theme.textFaint
                }
                Rectangle {
                    visible: row.isCurrent
                    anchors.verticalCenter: parent.verticalCenter
                    x: table.albumMode ? 0 : 5
                    width: 28; height: 28; radius: 14
                    color: table.albumMode ? "transparent" : Theme.alpha("#000000", 0.55)
                    Icon {
                        anchors.centerIn: parent
                        name: Player.playing ? "eq" : "pause"
                        size: 16
                        color: Theme.accent
                    }
                }
            }

            Column {
                x: lead.x + table.wLead
                width: table.wTitle - 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    width: parent.width
                    text: model.title || ""
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: row.isCurrent ? Theme.accent : Theme.text
                }
                Text {
                    width: parent.width
                    // On album pages only guests differ from the album artist.
                    visible: !table.albumMode || (model.artist !== model.albumArtist)
                    text: model.artist || ""
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: Theme.textDim
                }
            }
            Text {
                visible: table.showAlbum
                x: lead.x + table.wLead + table.wTitle
                width: table.wAlbum - 16
                anchors.verticalCenter: parent.verticalCenter
                text: model.album || ""
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: 13
                color: Theme.textDim
            }
            Text {
                visible: table.showYear
                x: lead.x + table.wLead + table.wTitle + table.wAlbum
                width: table.wYear
                anchors.verticalCenter: parent.verticalCenter
                text: model.year || ""
                font.family: Theme.uiFont
                font.pixelSize: 13
                color: Theme.textDim
            }
            Text {
                visible: table.showFormat
                x: lead.x + table.wLead + table.wTitle + table.wAlbum + table.wYear
                width: table.wFormat - 8
                anchors.verticalCenter: parent.verticalCenter
                text: model.format || ""
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: 12
                font.weight: Font.Medium
                color: Theme.textFaint
            }
            Text {
                visible: table.showExtra
                x: lead.x + table.wLead + table.wTitle + table.wAlbum + table.wYear + table.wFormat
                width: table.wExtra
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    switch (table.extraColumn) {
                    case "added": return model.added || ""
                    case "mtime": return model.mtime || ""
                    case "playCount": return model.playCount > 0 ? model.playCount + "×" : ""
                    case "lastPlayed": return model.lastPlayed || ""
                    }
                    return ""
                }
                font.family: Theme.uiFont
                font.pixelSize: 13
                color: Theme.textDim
            }
            Text {
                x: table.width - table.side - table.wDur
                width: table.wDur
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignRight
                text: model.duration || ""
                font.family: Theme.uiFont
                font.pixelSize: 13
                color: Theme.textDim
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: (m) => {
                    if (m.button === Qt.RightButton) {
                        // acting on a row outside the selection: that row alone
                        if (table.canSelect && model.selected !== true)
                            table.rowClick(index, 0)
                        list.currentIndex = index
                        list.forceActiveFocus()
                        table.contextRequested(index, m.x, row.y - list.contentY + m.y)
                    } else {
                        table.rowClick(index, m.modifiers)
                    }
                }
                onDoubleClicked: (m) => { if (m.button === Qt.LeftButton) table.activated(index) }
            }
            // the heart: always there for favourites, otherwise when the row is pointed at
            HeartButton {
                x: table.width - table.side - table.wDur - table.wFav
                anchors.verticalCenter: parent.verticalCenter
                size: 30
                on: model.favorite === true
                visible: on || mouse.containsMouse || hovered || activeFocus
                onClicked: Library.setFavorite(model.trackId, !on)
            }
        }
    }

    RowDragArea {
        anchors.fill: list
        list: list
        enabled: table.reorderable
        visible: enabled
        scrollBarWidth: table.side + table.wFav + table.wDur // the heart stays clickable
        onRowClicked: (r, mods) => table.rowClick(r, mods)
        onRowDoubleClicked: (r) => table.activated(r)
        onMoveRequested: (from, to) => { list.currentIndex = -1; table.moveRequested(from, to) }
    }

    // Column header, used as the list header so it scrolls away with the page header.
    property Component columnHeader: Item {
        width: list.width
        height: 34
        Rectangle {
            anchors.bottom: parent.bottom
            x: table.side - 8
            width: parent.width - 2 * table.side + 16
            height: 1
            color: Theme.alpha(Theme.border, 0.8)
        }
        HeaderCell { x: table.side + table.wLead; width: table.wTitle; label: qsTr("Titel"); key: "title" }
        HeaderCell { visible: table.showAlbum; x: table.side + table.wLead + table.wTitle; width: table.wAlbum; label: qsTr("Album"); key: "album" }
        HeaderCell { visible: table.showYear; x: table.side + table.wLead + table.wTitle + table.wAlbum; width: table.wYear; label: qsTr("Jahr"); key: "year" }
        HeaderCell { visible: table.showFormat; x: table.side + table.wLead + table.wTitle + table.wAlbum + table.wYear; width: table.wFormat; label: qsTr("Format"); key: "codec" }
        HeaderCell { visible: table.showExtra; x: table.side + table.wLead + table.wTitle + table.wAlbum + table.wYear + table.wFormat; width: table.wExtra; label: table.extraLabel(); key: table.extraColumn }
        HeaderCell { x: table.width - table.side - table.wDur; width: table.wDur; label: qsTr("Dauer"); key: "duration"; alignRight: true }
    }

    component HeaderCell: MouseArea {
        id: cell
        property string label
        property string key
        property bool alignRight: false
        readonly property bool active: table.sortKey === key
        height: 34
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        enabled: !table.albumMode
        onClicked: table.sortRequested(key)
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: cell.alignRight ? parent.right : undefined
            spacing: 4
            Text {
                text: cell.label
                font.family: Theme.uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: cell.active ? Theme.text : (cell.containsMouse ? Theme.textDim : Theme.textFaint)
            }
            Icon {
                visible: cell.active && !table.albumMode
                anchors.verticalCenter: parent.verticalCenter
                name: table.sortDesc ? "down" : "up"
                size: 14
                color: Theme.textDim
            }
        }
    }
}
