// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Any list of tracks: a view, a search, an album, a genre ...
Item {
    id: page
    property string pageId: ""
    property string title
    property string subtitle
    property var spec: ({})
    property var coverId: 0
    property bool isSearch: false
    property bool albumMode: false
    property bool showBack: StackView.view ? StackView.view.depth > 1 : false
    property string sortKey: spec && spec.sort ? spec.sort : "album"
    property bool sortDesc: spec && spec.desc ? true : false
    // Playlists: a normal one can be edited and reordered, a smart one has rules.
    property var playlistId: 0
    property bool smart: false
    // the "Favoriten" view (or any list of hearts only): its own hint when empty
    readonly property bool favorites: !!spec && !!spec.filters && spec.filters.length === 1 && spec.filters[0].field === "rating"
    // re-read whenever the playlists change (counts, missing entries)
    readonly property var playlist: (Playlists.playlists, playlistId > 0 ? Playlists.get(playlistId) : ({}))
    readonly property bool editable: playlistId > 0 && !smart
    // A shuffled smart playlist keeps its order while the page is open.
    readonly property int seed: Math.floor(Math.random() * 1000000000) + 1
    property real keepY: -1

    readonly property var effectiveSpec: {
        var s = Object.assign({}, page.spec || {})
        s.sort = page.sortKey
        s.desc = page.sortDesc
        if (page.smart && s.sort === "random") s.seed = page.seed
        return s
    }

    // Reload in place (after an edit) without jumping back to the top.
    function reloadKeepingPlace() {
        page.keepY = table.list.contentY
        tracks.reload()
    }
    function addTo(id, fn) { window.playlistUi.addTo(id, fn) }

    Connections {
        target: Playlists
        function onContentChanged(id) {
            if (page.playlistId === id || (page.smart && page.playlistId > 0)) page.reloadKeepingPlace()
        }
        function onPlaylistsChanged() {
            if (page.playlistId <= 0) return
            var p = Playlists.get(page.playlistId)
            if (!p.id) return
            page.title = p.name
            if (page.smart && JSON.stringify(p.rules) !== JSON.stringify(page.spec)) {
                page.spec = p.rules
                page.sortKey = p.rules.sort || "random"
                page.sortDesc = p.rules.desc === true
            }
        }
    }
    Connections {
        target: tracks
        function onReloaded() {
            if (page.keepY < 0) return
            var y = page.keepY
            page.keepY = -1
            Qt.callLater(function () {
                var l = table.list
                l.contentY = Math.max(l.originY, Math.min(y, l.originY + l.contentHeight - l.height))
            })
        }
    }

    function extraFor(key) {
        if (key === "added" || key === "mtime" || key === "playCount" || key === "lastPlayed")
            return key
        return ""
    }

    TrackListModel {
        id: tracks
        spec: page.effectiveSpec
    }

    TrackTable {
        id: table
        anchors.fill: parent
        model: tracks
        albumMode: page.albumMode
        sortKey: page.sortKey
        sortDesc: page.sortDesc
        extraColumn: page.extraFor(page.sortKey)
        reorderable: page.editable && page.sortKey === "position" && !page.sortDesc && !Playlists.busy
        onMoveRequested: (row, to) => Playlists.moveRows(page.playlistId, tracks, row, to)
        onSortRequested: (key) => {
            if (key === page.sortKey) {
                // third click in a playlist: back to its own order
                if (page.editable && page.sortDesc) { page.sortKey = "position"; page.sortDesc = false; return }
                page.sortDesc = !page.sortDesc
            } else {
                page.sortKey = key
                page.sortDesc = (key === "added" || key === "mtime" || key === "playCount" || key === "lastPlayed")
            }
        }
        onActivated: (row) => Library.playList(tracks, row, false)
        onContextRequested: (row, x, y) => {
            menu.row = row
            menu.popup(table, x, y)
        }

        header: Column {
            width: table.width
            PageHeader {
                width: parent.width
                title: page.title
                subtitle: page.subtitle
                coverId: page.coverId
                showBack: page.showBack
                loading: tracks.loading
                onBack: page.StackView.view.pop()
                detail: {
                    if (tracks.loading && tracks.count === 0)
                        return qsTr("Wird geladen …")
                    var missing = page.editable && page.playlist.missing > 0
                        ? (page.playlist.missing === 1 ? qsTr("1 Titel fehlt gerade") : qsTr("%1 Titel fehlen gerade").arg(Library.formatCount(page.playlist.missing)))
                        : ""
                    if (tracks.count === 0)
                        return missing
                    var n = tracks.count === 1 ? qsTr("1 Titel") : qsTr("%1 Titel").arg(Library.formatCount(tracks.count))
                    var parts = [n + ", " + Library.formatDuration(tracks.totalDurationMs, true)]
                    if (missing !== "") parts.push(missing)
                    if (tracks.selectionCount > 1) parts.push(qsTr("%1 ausgewählt").arg(Library.formatCount(tracks.selectionCount)))
                    return parts.join(" · ")
                }
                PillButton {
                    primary: true
                    text: qsTr("Abspielen")
                    iconName: "play"
                    enabled: tracks.count > 0
                    onClicked: Library.playList(tracks, 0, false)
                }
                PillButton {
                    text: qsTr("Zufällig")
                    iconName: "shuffle"
                    enabled: tracks.count > 0
                    onClicked: Library.playList(tracks, -1, true)
                }
                PillButton {
                    visible: !page.albumMode
                    text: qsTr("Sortieren")
                    iconName: "list"
                    onClicked: sortMenu.popup()
                    HMenu {
                        id: sortMenu
                        Repeater {
                            model: (page.editable ? [{ key: "position", label: qsTr("Reihenfolge der Playlist") }] : []).concat([
                                { key: "album", label: qsTr("Albumkünstler und Album") },
                                { key: "artist", label: qsTr("Künstler") },
                                { key: "title", label: qsTr("Titel") },
                                { key: "year", label: qsTr("Erscheinungsjahr") },
                                { key: "added", label: qsTr("Hinzugefügt") },
                                { key: "mtime", label: qsTr("Datei-Datum") },
                                { key: "genre", label: qsTr("Genre") },
                                { key: "codec", label: qsTr("Format und Qualität") },
                                { key: "duration", label: qsTr("Dauer") },
                                { key: "playCount", label: qsTr("Wiedergaben") },
                                { key: "lastPlayed", label: qsTr("Zuletzt gespielt") },
                                { key: "path", label: qsTr("Dateipfad") }
                            ]).concat(page.smart ? [{ key: "random", label: qsTr("Zufällig") }, { key: "bpm", label: qsTr("Tempo") },
                                                    { key: "energy", label: qsTr("Energie") }] : [])
                            HMenuItem {
                                text: modelData.label
                                checkable: true
                                checked: page.sortKey === modelData.key
                                onTriggered: {
                                    page.sortDesc = (modelData.key === "added" || modelData.key === "mtime"
                                                     || modelData.key === "playCount" || modelData.key === "lastPlayed")
                                    page.sortKey = modelData.key
                                }
                            }
                        }
                        MenuSeparator {
                            contentItem: Rectangle { implicitHeight: 1; color: Theme.border }
                        }
                        HMenuItem {
                            text: qsTr("Absteigend")
                            checkable: true
                            checked: page.sortDesc
                            onTriggered: page.sortDesc = !page.sortDesc
                        }
                    }
                }
                PillButton {
                    visible: page.smart
                    text: qsTr("Regeln")
                    iconName: "filter"
                    onClicked: window.playlistUi.editRules(page.playlistId)
                }
                PillButton {
                    visible: page.playlistId <= 0
                    text: qsTr("Zur Playlist")
                    iconName: "playlist-add"
                    enabled: tracks.count > 0
                    onClicked: allMenu.popup()
                    PlaylistMenu {
                        id: allMenu
                        onChosen: (id) => page.addTo(id, function (pid) { Playlists.addAll(pid, tracks) })
                    }
                }
                IconButton {
                    visible: page.playlistId > 0
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    iconName: "more"
                    tip: qsTr("Mehr")
                    onClicked: plMenu.popup()
                    HMenu {
                        id: plMenu
                        HMenuItem { text: qsTr("Umbenennen …"); onTriggered: window.playlistUi.rename(page.playlistId) }
                        HMenuItem { visible: page.smart; height: visible ? implicitHeight : 0; text: qsTr("Regeln bearbeiten …"); onTriggered: window.playlistUi.editRules(page.playlistId) }
                        HMenuItem {
                            visible: page.smart; height: visible ? implicitHeight : 0
                            text: qsTr("Als feste Playlist speichern")
                            enabled: tracks.count > 0
                            onTriggered: { var id = Playlists.freeze(page.playlistId, tracks); if (id > 0) window.playlistUi.openPlaylist(id) }
                        }
                        HMenuItem { visible: page.editable; height: visible ? implicitHeight : 0; text: qsTr("Als M3U exportieren …"); onTriggered: window.playlistUi.exportFile(page.playlistId) }
                        HMenuItem {
                            visible: page.editable && page.playlist.missing > 0; height: visible ? implicitHeight : 0
                            text: qsTr("Fehlende Titel entfernen")
                            onTriggered: Playlists.removeMissing(page.playlistId)
                        }
                        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
                        HMenuItem { text: qsTr("Löschen …"); onTriggered: window.playlistUi.confirmDelete(page.playlistId) }
                    }
                }
            }
            Loader {
                // search: matching artists and albums first, then the tracks
                active: page.isSearch
                visible: active
                width: parent.width
                sourceComponent: SearchShelves { width: table.width; text: page.spec && page.spec.search ? page.spec.search : "" }
            }
            Loader {
                width: parent.width
                sourceComponent: tracks.count > 0 ? table.columnHeader : null
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 60
        visible: !tracks.loading && tracks.count === 0
        iconName: page.isSearch ? "search" : page.smart ? "filter" : page.playlistId > 0 ? "playlist" : page.favorites ? "heart" : "note"
        title: page.isSearch ? qsTr("Keine Treffer")
             : page.smart ? qsTr("Gerade passt kein Titel")
             : page.playlistId > 0 ? qsTr("Diese Playlist ist leer")
             : page.favorites ? qsTr("Noch keine Favoriten")
             : (Library.empty ? qsTr("Noch keine Musik hier") : qsTr("Diese Ansicht ist leer"))
        text: page.isSearch ? qsTr("Versuch es mit einem Teil des Namens oder einem anderen Wort.")
            : tracks.error !== "" ? tracks.error
            : page.smart ? qsTr("Die Regeln lassen sich jederzeit ändern. Sobald Titel passen, tauchen sie hier auf.")
            : page.playlistId > 0 ? qsTr("Rechtsklick auf Titel, Alben, Künstler oder Ordner → „Zur Playlist hinzufügen“. Mehrere Titel wählst du mit Strg oder Umschalt aus.")
            : page.favorites ? qsTr("Ein Klick auf das Herz neben einem Titel, unten beim laufenden Titel oder in der Großansicht – oder Strg + H für den Titel, der gerade läuft.")
            : (Library.empty ? qsTr("Füge in den Einstellungen deinen Musikordner hinzu. Die ersten Titel erscheinen schon während des Scans.")
                             : qsTr("Sobald Titel zu dieser Ansicht passen, tauchen sie hier auf."))
        actionText: page.smart ? qsTr("Regeln bearbeiten") : (Library.empty && !page.isSearch && page.playlistId <= 0 ? qsTr("Musikordner hinzufügen") : "")
        onAction: page.smart ? window.playlistUi.editRules(page.playlistId) : window.openSettings()
    }

    HMenu {
        id: menu
        property int row: -1
        property int n: 1 // rows the actions apply to (selection)
        property bool fav: false // the clicked row has the heart: the item takes it away (from all of them)
        onAboutToShow: { n = tracks.actionCount(row); fav = tracks.get(row).favorite === true }
        HMenuItem {
            text: menu.n > 1 ? qsTr("%1 Titel abspielen").arg(Library.formatCount(menu.n)) : qsTr("Abspielen")
            onTriggered: menu.n > 1 ? Library.playSelection(tracks, menu.row) : Library.playList(tracks, menu.row, false)
        }
        HMenuItem { text: qsTr("Als Nächstes spielen"); onTriggered: Library.enqueueList(tracks, menu.row, true) }
        HMenuItem { text: qsTr("Zur Warteschlange hinzufügen"); onTriggered: Library.enqueueList(tracks, menu.row, false) }
        HMenuItem {
            text: menu.fav ? qsTr("Aus den Favoriten entfernen") : qsTr("Zu den Favoriten")
            onTriggered: Library.setFavoriteRows(tracks, menu.row, !menu.fav)
        }
        PlaylistMenu {
            onChosen: (id) => {
                var r = menu.row
                page.addTo(id, function (pid) { Playlists.addRows(pid, tracks, r) })
            }
        }
        HMenuItem {
            visible: page.editable
            enabled: !Playlists.busy
            height: visible ? implicitHeight : 0
            text: qsTr("Aus der Playlist entfernen")
            onTriggered: Playlists.removeRows(page.playlistId, tracks, menu.row)
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
        HMenuItem { visible: menu.n === 1; height: visible ? implicitHeight : 0; text: qsTr("Mehr wie dieser Titel (Radio)"); onTriggered: Recommend.startRadioFromList(tracks, menu.row) }
        HMenuItem {
            text: qsTr("Album öffnen")
            enabled: !page.albumMode
            onTriggered: {
                var t = tracks.get(menu.row)
                window.openAlbum(t.albumArtist, t.album, t.trackId)
            }
        }
        HMenuItem {
            text: qsTr("Alle Titel des Künstlers")
            onTriggered: {
                var t = tracks.get(menu.row)
                window.openTracks(t.albumArtist || t.artist, qsTr("Künstler"),
                                  { filters: [{ field: "albumArtist", op: "eq", value: t.albumArtist }], sort: "year" }, 0)
            }
        }
    }
}
