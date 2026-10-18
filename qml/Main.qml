import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Halcyon

// Window, navigation and global state. The look itself comes from the active
// skin (qml/skins/*.qml), loaded below. Every skin hosts the same pages, so all
// content and functions are available in every theme.
ApplicationWindow {
    id: window
    width: 1380
    height: 880
    minimumWidth: 960
    minimumHeight: 620
    visible: true
    title: Player.currentId > 0 ? Player.title + " – " + Player.artist + " – BuB3Player" : "BuB3Player"
    color: Theme.bg
    font.family: Theme.uiFont

    // ---------- state shared by all skins ----------
    property string currentViewId: ""
    property bool queueOpen: Library.setting("queueOpen", false) === true || Library.setting("queueOpen", false) === "true"
    property bool nowPlayingOpen: false
    property bool visualizer: Library.setting("visualizer", true) !== false && Library.setting("visualizer", true) !== "false"
    property bool pauseInactive: Library.setting("pauseInactive", true) !== false && Library.setting("pauseInactive", true) !== "false"
    property int settingsTab: 0
    property bool lowPower: Library.setting("lowPower", false) === true || Library.setting("lowPower", false) === "true"
    onLowPowerChanged: Library.setSetting("lowPower", lowPower)
    property var stack: skinLoader.item ? skinLoader.item.stack : null
    readonly property var skin: skinLoader.item

    onQueueOpenChanged: Library.setSetting("queueOpen", queueOpen)
    onVisualizerChanged: Library.setSetting("visualizer", visualizer)
    onPauseInactiveChanged: Library.setSetting("pauseInactive", pauseInactive)

    readonly property bool onScreen: visibility !== Window.Minimized && visibility !== Window.Hidden
    // Effects run only while someone can see them (and optionally only while focused).
    readonly property bool animationsActive: onScreen && (!pauseInactive || active)

    // ---------- effect clock ----------
    // One shared clock for all shader effects. "Sparsam" ticks at 30 fps, "Aus" stops it.
    readonly property int fxFps: {
        var f = Theme.opt.fps === undefined ? 60 : parseInt(Theme.opt.fps)
        return lowPower ? Math.min(f, 30) : f
    }
    property real fxTime: 0
    Timer {
        interval: window.fxFps >= 60 ? 16 : 33
        repeat: true
        running: window.fxFps > 0 && window.animationsActive && !!window.skin && window.skin.animated === true
        onTriggered: window.fxTime += interval / 1000
    }

    Binding {
        target: Player
        property: "spectrumEnabled"
        value: window.visualizer && window.onScreen
    }
    Binding {
        target: Player
        property: "analysisHz"
        value: window.lowPower ? 15 : 30
    }
    Binding {
        target: Player
        property: "levelsEnabled"
        value: window.onScreen && !!window.skin && window.skin.wantsLevels === true
    }

    function fmtTime(ms) {
        if (!(ms > 0)) return "0:00"
        var s = Math.floor(ms / 1000)
        var m = Math.floor(s / 60)
        var h = Math.floor(m / 60)
        var ss = (s % 60 < 10 ? "0" : "") + (s % 60)
        if (h > 0) return h + ":" + ((m % 60) < 10 ? "0" : "") + (m % 60) + ":" + ss
        return m + ":" + ss
    }

    // ---------- navigation ----------
    function openView(v) {
        if (!v || !v.id || !stack) return
        currentViewId = v.id
        nowPlayingOpen = false
        if (v.id !== "settings")
            Library.setSetting("lastView", v.id)
        if (skin && skin.clearSearch) skin.clearSearch()
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
    function openSettings(tab) {
        if (tab !== undefined) settingsTab = tab
        openView({ id: "settings", kind: "settings" })
    }
    function openTracks(title, subtitle, spec, coverId, albumMode) {
        stack.push(tracksPage, { title: title, subtitle: subtitle || "", spec: spec, coverId: coverId || 0, albumMode: albumMode === true })
    }
    function openAlbum(albumArtist, album, coverId) {
        openTracks(album || qsTr("Unbekanntes Album"), albumArtist,
                   { filters: [{ field: "albumArtist", op: "eq", value: albumArtist }, { field: "album", op: "eq", value: album }], sort: "album" },
                   coverId, true)
    }
    function openCurrentAlbum() {
        if (Player.currentId > 0) {
            nowPlayingOpen = false
            openAlbum(Player.albumArtist, Player.album, Player.currentId)
        }
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
        if (!stack) return
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
    function restoreView() {
        var start = currentViewId !== "" ? currentViewId
                  : (typeof startView !== "undefined" && startView !== "" ? startView : Library.setting("lastView", "albums"))
        if (start === "settings" || (Library.empty && Library.roots.length === 0)) {
            openSettings()
        } else {
            var v = Library.viewById(start)
            openView(v && v.id ? v : Library.viewById("albums"))
        }
    }

    Component { id: tracksPage; TracksPage {} }
    Component { id: groupsPage; GroupsPage {} }
    Component { id: foldersPage; FoldersPage {} }
    Component { id: settingsPage; SettingsPage {} }

    // ---------- the skin ----------
    Loader {
        id: skinLoader
        anchors.fill: parent
        source: Theme.skinQml
        focus: true
        onLoaded: window.restoreView()
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: (window.skin && window.skin.toastMargin !== undefined ? window.skin.toastMargin : 110)
        z: 100
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
    Shortcut { sequence: "Ctrl+F"; onActivated: { window.nowPlayingOpen = false; if (window.skin && window.skin.focusSearch) window.skin.focusSearch() } }
    Shortcut { sequence: "Ctrl+J"; onActivated: window.queueOpen = !window.queueOpen }
    Shortcut { sequence: "Ctrl+L"; onActivated: window.nowPlayingOpen = !window.nowPlayingOpen }
    Shortcut { sequence: "Ctrl+T"; onActivated: window.openSettings(1) }
    Shortcut { sequence: "Escape"; enabled: window.nowPlayingOpen; onActivated: window.nowPlayingOpen = false }
    Shortcut { sequence: StandardKey.Quit; onActivated: Qt.quit() }
    Shortcut { sequence: "Alt+Left"; enabled: !!window.stack && window.stack.depth > 1; onActivated: window.stack.pop() }

    Component.onCompleted: {
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
            if (a.indexOf("theme-tab") >= 0) window.openSettings(1)
            if (a.indexOf("search") >= 0) {
                if (window.skin && window.skin.setSearchText) window.skin.setSearchText("nord")
                window.search("nord")
            }
        }
    }
}
