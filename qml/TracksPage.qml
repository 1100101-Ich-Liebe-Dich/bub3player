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

    readonly property var effectiveSpec: {
        var s = Object.assign({}, page.spec || {})
        s.sort = page.sortKey
        s.desc = page.sortDesc
        return s
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
        onSortRequested: (key) => {
            if (key === page.sortKey) {
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
                    if (tracks.count === 0)
                        return ""
                    var n = tracks.count === 1 ? qsTr("1 Titel") : qsTr("%1 Titel").arg(Library.formatCount(tracks.count))
                    return n + ", " + Library.formatDuration(tracks.totalDurationMs, true)
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
                            model: [
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
                            ]
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
        iconName: page.isSearch ? "search" : "note"
        title: page.isSearch ? qsTr("Keine Treffer") : (Library.empty ? qsTr("Noch keine Musik hier") : qsTr("Diese Ansicht ist leer"))
        text: page.isSearch ? qsTr("Versuch es mit einem Teil des Namens oder einem anderen Wort.")
                            : (Library.empty ? qsTr("Füge in den Einstellungen deinen Musikordner hinzu. Die ersten Titel erscheinen schon während des Scans.")
                                             : (tracks.error !== "" ? tracks.error : qsTr("Sobald Titel zu dieser Ansicht passen, tauchen sie hier auf.")))
        actionText: Library.empty && !page.isSearch ? qsTr("Musikordner hinzufügen") : ""
        onAction: window.openSettings()
    }

    HMenu {
        id: menu
        property int row: -1
        HMenuItem { text: qsTr("Abspielen"); onTriggered: Library.playList(tracks, menu.row, false) }
        HMenuItem { text: qsTr("Als Nächstes spielen"); onTriggered: Library.enqueueList(tracks, menu.row, true) }
        HMenuItem { text: qsTr("Zur Warteschlange hinzufügen"); onTriggered: Library.enqueueList(tracks, menu.row, false) }
        HMenuItem { text: qsTr("Mehr wie dieser Titel (Radio)"); onTriggered: Recommend.startRadioFromList(tracks, menu.row) }
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
