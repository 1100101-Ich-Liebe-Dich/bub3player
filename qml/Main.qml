import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Halcyon

ApplicationWindow {
    id: window
    width: 1380
    height: 880
    minimumWidth: 940
    minimumHeight: 600
    visible: true
    title: Player.currentId > 0 ? Player.title + " – " + Player.artist : "Halcyon"
    color: Theme.bg
    font.family: Theme.uiFont

    property string currentViewId: ""
    property bool queueOpen: Library.setting("queueOpen", false) === true || Library.setting("queueOpen", false) === "true"
    property bool nowPlayingOpen: false
    property bool visualizer: Library.setting("visualizer", true) !== false && Library.setting("visualizer", true) !== "false"

    onQueueOpenChanged: Library.setSetting("queueOpen", queueOpen)
    onVisualizerChanged: Library.setSetting("visualizer", visualizer)

    // The spectrum only runs when someone can see it.
    Binding {
        target: Player
        property: "spectrumEnabled"
        value: window.visualizer && window.visibility !== Window.Minimized && window.visibility !== Window.Hidden
    }

    // ---------- navigation ----------
    function openView(v) {
        if (!v || !v.id) return
        currentViewId = v.id
        nowPlayingOpen = false
        if (v.id !== "settings")
            Library.setSetting("lastView", v.id)
        if (sidebar.searchText !== "" ) sidebar.searchText = ""
        switch (v.kind) {
        case "tracks":
            stack.replace(null, tracksPage, { pageId: v.id, title: v.name, spec: v.spec || {} })
            break
        case "groups":
            stack.replace(null, groupsPage, { pageId: v.id, view: v })
            break
        case "folders":
            stack.replace(null, foldersPage, {})
            break
        case "settings":
            stack.replace(null, settingsPage, {})
            break
        }
    }
    function openSettings() { openView({ id: "settings", kind: "settings" }) }
    function openTracks(title, subtitle, spec, coverId, albumMode) {
        stack.push(tracksPage, { title: title, subtitle: subtitle || "", spec: spec, coverId: coverId || 0, albumMode: albumMode === true })
    }
    function openAlbum(albumArtist, album, coverId) {
        openTracks(album || qsTr("Unbekanntes Album"), albumArtist,
                   { filters: [{ field: "albumArtist", op: "eq", value: albumArtist }, { field: "album", op: "eq", value: album }], sort: "album" },
                   coverId, true)
    }
    function openGroup(model, row, groupBy) {
        var g = model.get(row)
        var spec = model.trackSpec(row)
        if (groupBy === "album") {
            openTracks(g.title, g.subtitle + (g.year > 0 ? ", " + g.year : ""), spec, g.coverId, true)
        } else if (groupBy === "albumArtist" || groupBy === "artist" || groupBy === "genre" || groupBy === "decade" || groupBy === "year") {
            // Drill into the albums of that artist / genre / era.
            stack.push(groupsPage, {
                view: { id: "", name: g.title, layout: "grid",
                        spec: { groupBy: "album", groupSort: groupBy === "albumArtist" || groupBy === "artist" ? "year" : "name", filters: spec.filters } },
                subtitle: g.subtitle
            })
        } else {
            openTracks(g.title, g.subtitle, { filters: spec.filters, sort: groupBy === "addedMonth" ? "added" : "album", desc: groupBy === "addedMonth" }, 0, false)
        }
    }
    function search(text) {
        if (text.trim() === "") {
            if (stack.currentItem && stack.currentItem.isSearch)
                openView(Library.viewById(Library.setting("lastView", "albums")))
            return
        }
        currentViewId = ""
        nowPlayingOpen = false
        var spec = { search: text, sort: "album" }
        if (stack.currentItem && stack.currentItem.isSearch && stack.depth === 1)
            stack.currentItem.spec = spec
        else
            stack.replace(null, tracksPage, { isSearch: true, title: qsTr("Suche"), subtitle: "", spec: spec })
        stack.currentItem.subtitle = qsTr("Ergebnisse für »%1«").arg(text)
    }

    Component { id: tracksPage; TracksPage {} }
    Component { id: groupsPage; GroupsPage {} }
    Component { id: foldersPage; FoldersPage {} }
    Component { id: settingsPage; SettingsPage {} }

    // ---------- layout ----------
    Ambient {
        anchors.fill: parent
        coverId: Player.currentId
        strength: Theme.ambient
        visible: !nowPlaying.open || nowPlaying.opacity < 1
    }

    RowLayout {
        id: mainLayout
        anchors.fill: parent
        anchors.bottomMargin: playerBar.height
        spacing: 0
        // Hidden under the full-screen view: nothing to draw, nothing to pay for.
        visible: !(nowPlaying.open && nowPlaying.opacity === 1)

        Sidebar {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: 252
            currentId: window.currentViewId
            onOpen: (v) => window.openView(v)
            onSearchRequested: (t) => window.search(t)
        }

        StackView {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            replaceEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 } }
            replaceExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }
            pushEnter: Transition {
                ParallelAnimation {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 }
                    NumberAnimation { property: "x"; from: 40; to: 0; duration: 220; easing.type: Easing.OutCubic }
                }
            }
            pushExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120 } }
            popEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 } }
            popExit: Transition {
                ParallelAnimation {
                    NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 140 }
                    NumberAnimation { property: "x"; from: 0; to: 40; duration: 160 }
                }
            }
        }

        QueuePanel {
            Layout.fillHeight: true
            Layout.preferredWidth: window.queueOpen ? 340 : 0
            visible: Layout.preferredWidth > 1
            Behavior on Layout.preferredWidth { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        }
    }

    PlayerBar {
        id: playerBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        queueOpen: window.queueOpen
        onToggleQueue: window.queueOpen = !window.queueOpen
        onOpenNowPlaying: window.nowPlayingOpen = true
        z: 5
    }

    NowPlaying {
        id: nowPlaying
        anchors.fill: parent
        anchors.bottomMargin: 0
        open: window.nowPlayingOpen
        onClose: window.nowPlayingOpen = false
        z: 10
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: playerBar.height + 18
        z: 20
    }
    Connections {
        target: Library
        function onMessage(text) { toast.show(text) }
    }

    // ---------- keyboard ----------
    Shortcut { sequence: "Space"; onActivated: Player.toggle() }
    Shortcut { sequence: "Media Play"; onActivated: Player.toggle() }
    Shortcut { sequence: "Ctrl+Right"; onActivated: Player.next() }
    Shortcut { sequence: "Ctrl+Left"; onActivated: Player.previous() }
    Shortcut { sequence: "Ctrl+Up"; onActivated: Player.volume = Math.min(1, Player.volume + 0.05) }
    Shortcut { sequence: "Ctrl+Down"; onActivated: Player.volume = Math.max(0, Player.volume - 0.05) }
    Shortcut { sequence: "Ctrl+F"; onActivated: { window.nowPlayingOpen = false; sidebar.focusSearch() } }
    Shortcut { sequence: "Ctrl+J"; onActivated: window.queueOpen = !window.queueOpen }
    Shortcut { sequence: "Ctrl+L"; onActivated: window.nowPlayingOpen = !window.nowPlayingOpen }
    Shortcut { sequence: "Escape"; enabled: window.nowPlayingOpen; onActivated: window.nowPlayingOpen = false }
    Shortcut { sequence: StandardKey.Quit; onActivated: Qt.quit() }
    Shortcut { sequence: "Alt+Left"; enabled: stack.depth > 1; onActivated: stack.pop() }

    Component.onCompleted: {
        var start = typeof startView !== "undefined" && startView !== "" ? startView : Library.setting("lastView", "albums")
        if (start === "settings") {
            openSettings()
        } else {
            var v = Library.viewById(start)
            openView(v && v.id ? v : Library.viewById("albums"))
        }
        if (Library.empty && Library.roots.length === 0)
            openSettings()
        if (typeof uiTest !== "undefined" && uiTest !== "")
            uiTestTimer.start()
    }

    // Scripted states for automated screenshots (--ui-test).
    Timer {
        id: uiTestTimer
        interval: 900
        onTriggered: {
            var a = uiTest
            if (a.indexOf("play") >= 0)
                Library.playSpec({ sort: "album", filters: [{ field: "bits", op: "ge", value: 24 }] }, false)
            if (a.indexOf("queue") >= 0) window.queueOpen = true
            if (a.indexOf("now-playing") >= 0) window.nowPlayingOpen = true
            if (a.indexOf("album") >= 0 && stack.currentItem && stack.currentItem.openFirst)
                stack.currentItem.openFirst()
            if (a.indexOf("search") >= 0) { sidebar.searchText = "nord"; window.search("nord") }
        }
    }
}
