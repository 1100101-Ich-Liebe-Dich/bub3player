// SPDX-License-Identifier: GPL-3.0-or-later
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
    title: Player.hasTrack ? Player.title + " – " + Player.artist + " – BuB3Player" : "BuB3Player"
    color: Theme.bg
    font.family: Theme.uiFont

    // ---------- state shared by all skins ----------
    property string currentViewId: ""
    property bool queueOpen: Library.setting("queueOpen", false) === true || Library.setting("queueOpen", false) === "true"
    property bool nowPlayingOpen: false
    property bool visualizer: Library.setting("visualizer", true) !== false && Library.setting("visualizer", true) !== "false"
    property bool waveform: Library.setting("waveform", true) !== false && Library.setting("waveform", true) !== "false"
    onWaveformChanged: Library.setSetting("waveform", waveform)
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

    // Lyrics are looked for while a big view is open (and the window not minimized);
    // the skins show them in place of the cover (Hi-Fi Turm: above the tower).
    Binding { target: Lyrics; property: "active"; value: window.nowPlayingOpen && Player.currentId > 0 && window.onScreen }
    readonly property bool lyricsOn: Lyrics.shown && Lyrics.available
    // the lyrics buttons go pale (none found, or a station or CD: lyrics are for the library only)
    readonly property bool lyricsMissing: Lyrics.status === 3 || Player.currentId < 0
    readonly property string lyricsTip: (Player.currentId < 0 ? qsTr("Liedtexte gibt es nur für Titel der Bibliothek")
                                         : Lyrics.status === 1 ? qsTr("Liedtext wird gesucht …")
                                         : Lyrics.shown ? (Lyrics.available ? qsTr("Cover statt Liedtext zeigen") : qsTr("Kein Liedtext gefunden"))
                                         : qsTr("Liedtext zeigen")) + qsTr("  (Strg + Umschalt + L)")
    function toggleLyrics() {
        if (!nowPlayingOpen && Player.currentId > 0) {
            nowPlayingOpen = true
            Lyrics.shown = true
        } else {
            Lyrics.shown = !Lyrics.shown
        }
        if (Lyrics.shown && Lyrics.status === 3)
            toast.show(qsTr("Für diesen Titel gibt es keinen Liedtext."))
    }

    // F11: full screen and back to how the window was (maximized or not) – also when
    // the window manager put it into full screen.
    property int visibilityBeforeFullScreen: Window.Windowed
    // Only settled states count: on the way into full screen the window passes "windowed".
    onVisibilityChanged: settleTimer.restart()
    Timer {
        id: settleTimer
        interval: 400
        onTriggered: if (window.visibility === Window.Windowed || window.visibility === Window.Maximized)
                         window.visibilityBeforeFullScreen = window.visibility
    }
    function toggleFullScreen() {
        if (visibility === Window.FullScreen) {
            if (visibilityBeforeFullScreen === Window.Maximized)
                showMaximized()
            else
                showNormal()
        } else {
            showFullScreen()
            toast.show(qsTr("Vollbild – zurück mit F11"))
        }
    }
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
        property: "waveformEnabled"
        value: window.waveform
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

    // the length of what plays; a radio station has none
    readonly property StatLook look: StatLook {} // statistics and year in review in the theme's look
    readonly property string durationText: Player.live ? qsTr("Live") : fmtTime(Player.duration)
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
        if (v.kind === "newPlaylist") { playlistUi.newPlaylist(); return }
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
        case "foryou":
            stack.replace(null, forYouPage, { pageId: v.id })
            break
        case "radio":
            stack.replace(null, radioPage, {})
            break
        case "cd":
            stack.replace(null, cdPage, {})
            break
        case "stats":
            stack.replace(null, statsPage, {})
            break
        case "playlist":
            stack.replace(null, tracksPage, { pageId: v.id, title: v.name, spec: v.spec || {}, playlistId: v.playlistId,
                                              smart: v.smart === true, subtitle: v.smart ? qsTr("Smart-Playlist") : qsTr("Playlist") })
            break
        }
    }
    // ---------- the year in review ----------
    property bool reviewOpen: false
    property int reviewYear: 0
    function openReview(year) {
        nowPlayingOpen = false
        reviewYear = year
        reviewOpen = true
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
    // The albums of an album artist, with their picture (as from the artist grid).
    function openArtist(albumArtist, coverId) {
        nowPlayingOpen = false
        stack.push(groupsPage, {
            view: { id: "", name: albumArtist || qsTr("Unbekannter Künstler"), layout: "grid", heroCover: coverId || 0, heroArtist: albumArtist,
                    spec: { filters: [{ field: "albumArtist", op: "eq", value: albumArtist }], groupBy: "album", groupSort: "year" } },
            subtitle: qsTr("Künstler")
        })
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
            // Drill into the albums of that artist / genre / era (an artist with their picture).
            var isArtist = groupBy === "albumArtist" || groupBy === "artist"
            stack.push(groupsPage, {
                view: { id: "", name: g.title, layout: "grid",
                        heroCover: isArtist ? g.coverId : 0, heroArtist: isArtist ? String(g.key1 || "") : "",
                        spec: Object.assign({}, spec, { groupBy: "album", groupSort: groupBy === "albumArtist" || groupBy === "artist" ? "year" : "name" }) },
                subtitle: g.subtitle
            })
        } else {
            openTracks(g.title, g.subtitle, Object.assign({}, spec, {
                                              sort: groupBy === "addedMonth" ? "added" : groupBy === "tempo" ? "bpm" : "album",
                                              desc: groupBy === "addedMonth" }), 0, false)
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
    Component { id: forYouPage; ForYouPage {} }
    Component { id: radioPage; RadioPage {} }
    Component { id: cdPage; CdPage {} }
    Component { id: statsPage; StatsPage {} }

    // ---------- the skin ----------
    Loader {
        id: skinLoader
        anchors.fill: parent
        source: Theme.skinQml
        focus: true
        onLoaded: {
            window.restoreView()
            // the previous theme is gone now: let QML collect it and give the memory back
            Memory.trimSoon(1500)
            secondTrim.restart()
        }
    }

    // made only while it is open (and while it fades out)
    Loader {
        id: reviewLoader
        anchors.fill: parent
        z: 60
        active: window.reviewLive
        sourceComponent: Component { YearReview {} }
    }
    property bool reviewLive: false
    onReviewOpenChanged: if (reviewOpen) reviewLive = true; else reviewGone.restart()
    Timer { id: reviewGone; interval: 600; onTriggered: if (!window.reviewOpen) window.reviewLive = false } // after it faded out

    readonly property alias playlistUi: playlistUi
    PlaylistUi { id: playlistUi }

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
        function onOutsideLibrary(folders, paths) {
            outsideDialog.folders = folders
            outsideDialog.paths = paths
            outsideDialog.open()
        }
    }

    // ---------- files from the file manager (dropped, "Öffnen mit") ----------
    // Playlists (.m3u) are imported, music and folders play.
    function openPaths(paths) {
        var music = [], lists = 0
        for (var i = 0; i < paths.length; ++i) {
            var p = String(paths[i])
            if (/^https?:\/\//i.test(p)) {
                Stations.add(p, "", true) // a stream or a station list on the web: play it
                ++lists
            } else if (/\.(pls|xspf|asx|m3u8?)$/i.test(p)) {
                Stations.openList(p) // stations (and play), or a playlist: Stations.onNotStations
                ++lists
            } else {
                music.push(p)
            }
        }
        if (music.length > 0)
            Library.openPaths(music, lists > 0)
    }
    DropArea {
        id: fileDrop
        // Some drag sources (seen on X11) deliver the list only while dragging, not on the drop.
        property var entered: []
        function strings(urls) {
            var list = []
            for (var i = 0; i < urls.length; ++i) list.push(String(urls[i]))
            return list
        }
        anchors.fill: parent
        z: 90
        keys: ["text/uri-list"]
        onEntered: (drag) => {
            drag.accepted = drag.hasUrls
            entered = drag.hasUrls ? strings(drag.urls) : []
        }
        onExited: entered = []
        onDropped: (drop) => {
            var list = drop.urls.length > 0 ? strings(drop.urls) : entered
            entered = []
            if (list.length === 0) return
            drop.acceptProposedAction()
            window.openPaths(list)
        }
        Rectangle {
            anchors.fill: parent
            visible: fileDrop.containsDrag
            color: Qt.rgba(0, 0, 0, 0.5)
            Rectangle {
                anchors.fill: parent
                anchors.margins: 18
                radius: Theme.radius + 6
                color: "transparent"
                border.width: 2
                border.color: Theme.accent
            }
            Column {
                anchors.centerIn: parent
                spacing: 12
                Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "play"; size: 56; color: Theme.accent }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Loslassen zum Abspielen")
                    font.family: Theme.displayFont
                    font.pixelSize: 26
                    color: "white"
                }
            }
        }
    }
    HDialog {
        id: outsideDialog
        property var folders: []
        property var paths: []
        title: qsTr("Nicht in der Bibliothek")
        text: folders.length === 1
              ? qsTr("Der Ordner gehört nicht zur Bibliothek. Gespielt wird Musik aus der Bibliothek – soll er dazukommen? Der Player liest dort nur, an den Dateien ändert sich nichts.")
              : qsTr("Diese Ordner gehören nicht zur Bibliothek. Gespielt wird Musik aus der Bibliothek – sollen sie dazukommen? Der Player liest dort nur, an den Dateien ändert sich nichts.")
        acceptText: qsTr("Hinzufügen und abspielen")
        acceptIcon: "add"
        Text {
            width: parent.width
            text: outsideDialog.folders.join("\n")
            wrapMode: Text.WrapAnywhere
            font.family: Theme.uiFont
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: Theme.text
        }
        onAccepted: Library.addRootsAndOpen(folders, paths)
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
    Shortcut { sequence: "Ctrl+L"; enabled: !window.reviewOpen; onActivated: window.nowPlayingOpen = !window.nowPlayingOpen }
    Shortcut { sequence: "Ctrl+Shift+L"; onActivated: window.toggleLyrics() }
    Shortcut {
        sequence: "Ctrl+H"
        enabled: Player.currentId > 0
        onActivated: {
            Library.toggleCurrentFavorite()
            toast.show(Library.currentFavorite ? qsTr("Zu den Favoriten hinzugefügt") : qsTr("Aus den Favoriten entfernt"))
        }
    }
    Shortcut { sequence: "Ctrl+T"; onActivated: window.openSettings(1) }
    Shortcut { sequence: "Ctrl+E"; onActivated: window.openSettings(3) }
    Shortcut { sequence: "Escape"; enabled: window.nowPlayingOpen && !window.reviewOpen; onActivated: window.nowPlayingOpen = false }
    Shortcut { sequence: "Escape"; enabled: window.reviewOpen; onActivated: window.reviewOpen = false }
    Shortcut { sequences: ["Right", "Return", "Enter"]; enabled: window.reviewOpen && reviewLoader.item !== null; onActivated: reviewLoader.item.next() }
    Shortcut { sequence: "Left"; enabled: window.reviewOpen && reviewLoader.item !== null; onActivated: reviewLoader.item.previous() }
    Shortcut { sequence: "F11"; onActivated: window.toggleFullScreen() }
    Shortcut { sequence: StandardKey.Quit; onActivated: Qt.quit() }
    Shortcut { sequence: "Alt+Left"; enabled: !!window.stack && window.stack.depth > 1; onActivated: window.stack.pop() }

    Component.onCompleted: {
        if (typeof uiTest !== "undefined" && uiTest !== "")
            uiTestTimer.start()
    }

    // a second pass: objects released by the first collection go in the second
    Timer { id: secondTrim; interval: 6000; onTriggered: Memory.trim() }

    // --ui-test swap-<id>: switch to another theme after a while (memory tests)
    Timer {
        id: swapTimer
        property string target
        interval: 6000
        onTriggered: Theme.setSkin(target)
    }

    // --ui-test blendloop: jump near the end every 9 s, so a transition follows (automix tests)
    Timer { id: blendTimer; interval: 9000; repeat: true; onTriggered: if (Player.duration > 20000) Player.seek(Player.duration - 9000) }

    // --ui-test skip: next track every 1.5 s (track change / waveform / cover tests)
    Timer { id: skipTimer; interval: 1500; repeat: true; onTriggered: Player.next() }

    // --ui-test scroll: keep paging through the current grid
    Timer {
        id: scrollTimer
        interval: 250
        repeat: true
        onTriggered: if (window.stack && window.stack.currentItem && window.stack.currentItem.scrollStep) window.stack.currentItem.scrollStep()
    }

    Timer {
        id: reviewTimer
        property int slideNo: 0
        interval: 1200
        onTriggered: { window.openReview(Stats.reviewYear()); reviewSlideTimer.start() }
    }
    Timer {
        id: reviewSlideTimer
        interval: 1500 // after the numbers are there
        onTriggered: {
            var rv = reviewLoader.item
            if (!rv) return
            if (reviewTimer.slideNo > 0) { rv.index = Math.min(reviewTimer.slideNo, rv.slides.length - 1); rv.enter() }
            var img = uiTest.match(/bildnach=(\S+)/) // the picture of the year to a file (with rueckblick=<last>)
            if (img) Qt.callLater(function () { rv.writeImage(img[1]) })
            if (uiTest.indexOf("jahrliste") >= 0) rv.savePlaylist()
        }
    }
    Timer {
        id: scrollYTimer
        property int y: 0
        interval: 2500 // after the page has its data
        onTriggered: if (window.stack && window.stack.currentItem && window.stack.currentItem.scrollTo) window.stack.currentItem.scrollTo(y)
    }

    // --ui-test cycle: switch through all skins (tests loading and teardown).
    Timer {
        id: cycleTimer
        property int i: 0
        interval: 1600
        repeat: true
        onTriggered: {
            var list = uiTest.indexOf("pair") >= 0 ? [{ id: "klassik" }, { id: "aurora" }] : Theme.skins
            if (i >= list.length * 2 && uiTest.indexOf("cycleloop") < 0) { stop(); return }
            Theme.setSkin(list[i % list.length].id)
            if (i % 3 === 1) window.nowPlayingOpen = true
            ++i
        }
    }

    // Scripted states for automated screenshots (--ui-test).
    // "pauseat=MS playat=MS nextat=MS": player actions at fixed times (transition tests),
    // "eqonat=MS eqoffat=MS bassat=MS": equalizer on, off, preset "Mehr Bass" (chain rebuild tests);
    // "closeat=MS": close the window as the window manager would (tray tests)
    Component {
        id: actTimer
        Timer {
            property string act
            running: true
            onTriggered: {
                if (act === "pause") Player.pause()
                else if (act === "play") Player.play()
                else if (act === "next") Player.next()
                else if (act === "eqon") Audio.eqEnabled = true
                else if (act === "eqoff") Audio.eqEnabled = false
                else if (act === "bass") Audio.applyPreset("Mehr Bass")
                else if (act === "close") window.close()
                destroy()
            }
        }
    }
    Timer { id: seekTimer; property real to: 0; interval: 4000; onTriggered: Player.seek(to) }
    Timer {
        id: radioPlayTimer
        property int n: 0
        interval: 2500 // after funkadd
        onTriggered: if (n < Stations.list.length) Stations.play(Stations.list[n].id)
    }
    Connections {
        // CD (with --view cd): scheibe=N plays track N once the disc is read, scheibelesen reads it into files
        target: AudioCd
        enabled: typeof uiTest !== "undefined" && uiTest.indexOf("scheibe") >= 0
        function onDiscChanged() {
            if (AudioCd.status !== 3) return
            var n = uiTest.match(/scheibe=(\d+)/)
            if (n) AudioCd.play(Number(n[1]))
            if (uiTest.indexOf("scheibelesen") >= 0 && !AudioCd.ripping && (!AudioCd.online || AudioCd.album !== "")) AudioCd.rip()
        }
    }
    Connections {
        target: Stations
        function onNotStations(path) { Playlists.importFile(path) }
    }
    Connections {
        target: Jellyfin
        enabled: typeof uiTest !== "undefined" && uiTest.indexOf("jf-login") >= 0
        function onProbeResult(ok, url) {
            if (ok && uiTest.indexOf("jf-quick") >= 0) Jellyfin.startQuickConnect(url)
            else if (ok) Jellyfin.login(url, "demo", "demo")
        }
    }
    QtObject {
        id: testPl
        property var model: null
        property var pid: 0
        property Connections c: Connections {
            target: testPl.model
            function onReloaded() {
                // a few rows of every second album, then the queue
                for (var r = 0; r < testPl.model.count; r += 5) testPl.model.select(r, 1)
                Playlists.addRows(testPl.pid, testPl.model, 0)
            }
        }
    }
    Timer {
        id: openLater
        property int idx: 0
        interval: 900
        onTriggered: {
            var p = Playlists.playlists[idx]
            if (p) playlistUi.openPlaylist(p.id)
            if (uiTest.indexOf("plsel") >= 0) selLater.start()
        }
    }
    Timer {
        id: selLater
        interval: 700
        onTriggered: {
            var t = stack.currentItem && stack.currentItem.resources
            // the TrackListModel of the page
            for (var i = 0; t && i < t.length; ++i)
                if (t[i].selectAll !== undefined) { t[i].select(1, 0); t[i].select(2, 1); t[i].select(4, 1) }
        }
    }
    Timer {
        id: editLater
        interval: 1200
        onTriggered: {
            for (var i = 0; i < Playlists.playlists.length; ++i)
                if (Playlists.playlists[i].smart) { playlistUi.editRules(Playlists.playlists[i].id); return }
        }
    }
    Timer {
        id: uiTestTimer
        interval: 900
        onTriggered: {
            var a = uiTest
            if (a.indexOf("playall") >= 0)
                Library.playSpec({ sort: "album" }, false)
            else if (a.indexOf("play") >= 0)
                Library.playSpec({ sort: "album", filters: [{ field: "bits", op: "ge", value: 24 }] }, false)
            var acts = ["pause", "play", "next", "eqon", "eqoff", "bass", "close"]
            for (var ai = 0; ai < acts.length; ++ai) {
                var re = new RegExp(acts[ai] + "at=(\\d+)", "g"), mm
                while ((mm = re.exec(a)) !== null)
                    actTimer.createObject(window, { interval: Number(mm[1]), act: acts[ai] })
            }
            var rd = a.match(/radio=(\d+)/)
            if (rd) Recommend.startRadio(Number(rd[1]))
            var rw = a.match(/row=(\d+)/)
            if (rw) Player.playRow(Number(rw[1]))
            if (a.indexOf("queue") >= 0) window.queueOpen = true
            if (a.indexOf("now-playing") >= 0) window.nowPlayingOpen = true
            if (a.indexOf("album") >= 0 && stack.currentItem && stack.currentItem.openFirst)
                stack.currentItem.openFirst()
            if (a.indexOf("theme-tab") >= 0) window.openSettings(1)
            if (a.indexOf("mix-tab") >= 0) window.openSettings(2)
            if (a.indexOf("klang-tab") >= 0) window.openSettings(3)
            if (a.indexOf("fullscreen") >= 0) window.toggleFullScreen()
            if (a.indexOf("lyrics-off") >= 0) Lyrics.shown = false
            else if (a.indexOf("lyrics") >= 0) Lyrics.shown = true
            if (a.indexOf("jellyfin-tab") >= 0) window.openSettings(4)
            if (a.indexOf("about-tab") >= 0) window.openSettings(5)
            var sk = a.match(/seek=(\d+)/)
            if (sk) seekTimer.to = Number(sk[1]), seekTimer.start()
            var jl = a.match(/jf-login=([^ ]+)/)
            if (jl) Jellyfin.probe(jl[1])
            if (a.indexOf("cycle") >= 0) cycleTimer.start()
            // statistics: statzeit=week|month|year|all, rueckblick[=N] (the year in review, at slide N)
            var sz = a.match(/statzeit=(\w+)/)
            if (sz) Stats.setPeriod(sz[1], 0)
            var rb = a.match(/rueckblick(?:=(\d+))?/)
            if (rb) reviewTimer.slideNo = rb[1] ? Number(rb[1]) : 0, reviewTimer.start()
            var sy = a.match(/scrolly=(\d+)/) // a page scrolled to y (when it has scrollTo)
            if (sy) scrollYTimer.y = Number(sy[1]), scrollYTimer.start()
            else if (a.indexOf("scroll") >= 0) scrollTimer.start()
            if (a.indexOf("skip") >= 0) skipTimer.start()
            if (a.indexOf("blendloop") >= 0) blendTimer.start()
            var sw = a.match(/swap-(\w+)/)
            if (sw) swapTimer.target = sw[1], swapTimer.start()
            // playlists: mkpl (create examples), plopen=N, pledit, plnew, plsel
            if (a.indexOf("mkpl") >= 0 && Playlists.playlists.length === 0) {
                var pid = Playlists.create("Roadtrip")
                var m = Qt.createQmlObject('import Halcyon; TrackListModel { spec: ({ sort: "album" }) }', window)
                testPl.model = m
                testPl.pid = pid
                Playlists.create("Schnell und laut", { match: "all", filters: [{ field: "bpm", op: "ge", value: 120 }, { field: "energy", op: "ge", value: 0.3 }], sort: "bpm", desc: true })
            }
            var po = a.match(/plopen=(\d+)/)
            if (po) openLater.start(), openLater.idx = Number(po[1])
            if (a.indexOf("pledit") >= 0) editLater.start()
            if (a.indexOf("plnew") >= 0) playlistUi.newPlaylist()
            if (a.indexOf("plimport") >= 0) playlistUi.importFile()
            if (a.indexOf("plexport") >= 0 && Playlists.playlists.length > 0) playlistUi.exportFile(Playlists.playlists[0].id)
            if (a.indexOf("autoplay") >= 0) { Queue.autoplay = true; Queue.jumpTo(Queue.count - 1); window.queueOpen = true }
            // radio: funkadd=URL (add a station), funkon=N (play the n-th), funksuche=WORT (directory)
            var ra = a.match(/funkadd=(\S+)/)
            if (ra) Stations.add(ra[1])
            var rp = a.match(/funkon=(\d+)/)
            if (rp) radioPlayTimer.start(), radioPlayTimer.n = Number(rp[1])
            var rs = a.match(/funksuche=(\S+)/)
            if (rs) { Stations.online = true; Stations.search(rs[1]) }
            if (a.indexOf("search") >= 0) { // "search" or "search=wort"
                var sq = a.match(/search=(\S+)/)
                var term = sq ? sq[1] : "nord"
                if (window.skin && window.skin.setSearchText) window.skin.setSearchText(term)
                window.search(term)
            }
        }
    }
}
