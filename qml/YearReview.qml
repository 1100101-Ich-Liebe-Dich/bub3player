// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs
import Halcyon

// "Dein Jahr in Musik": the year of listening as a row of slides – time, artist,
// title and album of the year, the top fives, genres, the time of day you listen
// (with a name for it), the months, records, the sound (tempo, key), decades,
// discoveries, radio – and at the end everything on one picture to save, and the
// favourites as a playlist. Over the theme's own background (the skins hide
// their furniture while it is open); cards, fonts and transitions per theme.
Item {
    id: rv
    readonly property bool open: window.reviewOpen
    readonly property int year: window.reviewYear
    readonly property var info: Stats.review
    property var slides: []
    property int index: 0
    readonly property var slide: slides[index] || ({})
    readonly property bool klassik: window.look.card === "none"
    readonly property real accentHue: Theme.accent.hslHue < 0 ? 0.6 : Theme.accent.hslHue
    visible: opacity > 0
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: window.animationsActive ? 450 : 0; easing.type: Easing.InOutCubic } }

    onOpenChanged: {
        if (!open)
            return
        index = 0
        slides = []
        if (Stats.review.year === year)
            slides = build(Stats.review)
        Stats.loadReview(year)
    }
    Connections {
        target: Stats
        function onReviewChanged() {
            if (rv.open && Stats.review.year === rv.year) {
                rv.slides = rv.build(Stats.review)
                rv.index = Math.min(rv.index, rv.slides.length - 1)
            }
        }
    }

    function next() { if (index < slides.length - 1) { ++index; enter() } }
    function previous() { if (index > 0) { --index; enter() } }
    function close() { window.reviewOpen = false }

    // ---- what is on the slides ----
    readonly property var personas: [qsTr("Frühaufsteher"), qsTr("Mittagshörer"), qsTr("Nachmittagsmensch"), qsTr("Feierabend-Hörer"), qsTr("Nachteule")]
    readonly property var personaTitles: [qsTr("Du bist ein Frühaufsteher"), qsTr("Du bist ein Mittagshörer"), qsTr("Du bist ein Nachmittagsmensch"),
                                          qsTr("Du bist ein Feierabend-Hörer"), qsTr("Du bist eine Nachteule")]
    readonly property var dayparts: [qsTr("morgens zwischen 5 und 11 Uhr"), qsTr("mittags zwischen 11 und 14 Uhr"), qsTr("nachmittags zwischen 14 und 18 Uhr"),
                                     qsTr("abends zwischen 18 und 22 Uhr"), qsTr("nachts zwischen 22 und 5 Uhr")]
    function fc(n) { return Library.formatCount(n || 0) }
    function pct(f) { return Math.round((f || 0) * 100) }
    function dur(ms) { return Library.formatDuration(ms || 0, true) }
    function one(d, a, b) { return d === 1 ? a : b.arg(fc(d)) }

    function build(d) {
        if (!d || !(d.plays > 0))
            return [{ kind: "intro", kicker: qsTr("Dein Jahr in Musik"), text: qsTr("In diesem Jahr lief noch nichts. Hör ein bisschen – dann gibt es hier etwas zu sehen.") }]
        var s = []
        var minutes = Math.round(d.listenedMs / 60000)
        var sofar = d.year === new Date().getFullYear() // the year is not over yet
        s.push({ kind: "intro", kicker: qsTr("Dein Jahr in Musik"),
                 text: (sofar ? qsTr("Bis heute %1 Wiedergaben.") : qsTr("%1 Wiedergaben.")).arg(fc(d.plays)) + " " + qsTr("Schauen wir, was dahintersteckt.") })

        var lines = []
        var days = d.listenedMs / 86400000
        lines.push(days >= 1 ? qsTr("Das sind %1 Tage Musik am Stück.").arg(Number(days).toLocaleString(Qt.locale("de_DE"), "f", 1))
                             : qsTr("Das sind %1 Musik am Stück.").arg(dur(d.listenedMs)))
        if (d.previousMs > 0) {
            var change = Math.round((d.listenedMs - d.previousMs) / d.previousMs * 100)
            lines.push(change >= 0 ? qsTr("%1 % mehr als %2.").arg(change).arg(d.year - 1) : qsTr("%1 % weniger als %2.").arg(-change).arg(d.year - 1))
        }
        if (d.streak && d.streak.activeDays > 0)
            lines.push(one(d.streak.activeDays, qsTr("An einem Tag lief Musik."), qsTr("An %1 Tagen lief Musik.")))
        if (d.radioMs > 0)
            lines.push(qsTr("Davon %1 Internetradio.").arg(dur(d.radioMs)))
        s.push({ kind: "number", kicker: qsTr("Deine Hörzeit"), value: minutes, unit: minutes === 1 ? qsTr("Minute") : qsTr("Minuten"), lines: lines })

        var a = d.topArtists || []
        if (a.length > 0)
            s.push({ kind: "hero", kicker: qsTr("Dein Künstler des Jahres"), name: a[0].name, trackId: a[0].trackId, round: true,
                     lines: [qsTr("%1 Mal gehört – %2 % deiner Musik.").arg(fc(a[0].plays)).arg(pct(a[0].share))] })
        if (a.length > 1)
            s.push({ kind: "list", kicker: qsTr("Deine Top-Künstler"), title: qsTr("Sie haben dich durchs Jahr begleitet"), items: a, round: true })

        var t = d.topTracks || []
        if (t.length > 0)
            s.push({ kind: "hero", kicker: qsTr("Dein Lied des Jahres"), name: t[0].name, artist: t[0].artist, trackId: t[0].trackId,
                     playable: true, ids: d.playlistIds || [],
                     lines: [one(t[0].plays, qsTr("Einmal ganz gehört."), qsTr("%1 Mal gespielt."))] })
        if (t.length > 1)
            s.push({ kind: "list", kicker: qsTr("Deine Top-Titel"), title: qsTr("Die liefen am meisten"), items: t })

        var al = d.topAlbums || []
        if (al.length > 0)
            s.push({ kind: "hero", kicker: qsTr("Dein Album des Jahres"), name: al[0].name, artist: al[0].artist, trackId: al[0].trackId,
                     lines: [one(al[0].plays, qsTr("Einmal lief ein Titel daraus."), qsTr("%1 Mal lief ein Titel daraus."))] })

        var g = d.topGenres || []
        if (g.length > 0)
            s.push({ kind: "shares", kicker: qsTr("Deine Genres"), title: qsTr("Vor allem %1").arg(g[0].name), items: g })

        var hourLabels = []
        for (var h = 0; h < 24; ++h) hourLabels.push(String(h))
        s.push({ kind: "bars", kicker: qsTr("Wann du hörst"), title: personaTitles[d.daypart] || "",
                 lines: [qsTr("%1 % deiner Musik lief %2.").arg(pct(d.daypartShare)).arg(dayparts[d.daypart] || ""),
                         qsTr("Am liebsten am %1 – und um %2 Uhr mehr als zu jeder anderen Zeit.").arg(d.topWeekdayName).arg(d.topHour)],
                 bars: d.hours || [], labels: hourLabels, highlight: d.topHour, tip: dur })
        var months = d.months || []
        s.push({ kind: "bars", kicker: qsTr("Dein Jahr im Verlauf"), title: qsTr("Dein Monat: %1").arg(d.topMonthName),
                 lines: [qsTr("Im %1 lief am meisten: %2.").arg(d.topMonthName).arg(dur((months[d.topMonth] || {}).ms))],
                 bars: months.map(function (m) { return m.ms }), labels: months.map(function (m) { return m.label }),
                 highlight: d.topMonth, tip: dur })

        var f = []
        if (d.streak && d.streak.days > 1)
            f.push({ value: d.streak.days, unit: qsTr("Tage am Stück"), text: qsTr("Vom %1 bis zum %2 jeden Tag Musik.").arg(d.streak.from).arg(d.streak.to) })
        if (d.repeat)
            f.push({ value: d.repeat.count, suffix: "×", unit: qsTr("an einem Tag"), text: qsTr("„%1“ von %2, am %3.").arg(d.repeat.name).arg(d.repeat.artist).arg(d.repeat.date) })
        if (d.busiestDay)
            f.push({ value: Math.round(d.busiestDay.ms / 60000), unit: qsTr("Minuten an einem Tag"), text: qsTr("Dein längster Tag: %1.").arg(d.busiestDay.date) })
        if (f.length > 0)
            s.push({ kind: "facts", kicker: qsTr("Rekorde"), title: qsTr("Du bist drangeblieben"), facts: f })

        if (d.bpm) {
            var sound = [{ value: d.bpm, unit: qsTr("BPM im Schnitt"),
                           text: d.bpm < 90 ? qsTr("Gemächlich – Musik zum Zurücklehnen.") : d.bpm < 115 ? qsTr("Entspannt, aber nicht schläfrig.")
                               : d.bpm < 135 ? qsTr("Tanzbar – genau im Takt der Clubs.") : qsTr("Schnell! Dein Puls hat mitgehalten.") }]
            if (d.keyName)
                sound.push({ big: d.keyCode, unit: qsTr("Lieblingstonart: %1").arg(d.keyName),
                             text: d.keyMinor ? qsTr("Moll – eher nachdenklich.") : qsTr("Dur – eher hell und froh.") })
            if (d.fastest)
                sound.push({ value: d.fastest.bpm, unit: qsTr("BPM: dein schnellster Titel"), text: qsTr("„%1“ von %2.").arg(d.fastest.name).arg(d.fastest.artist) })
            s.push({ kind: "facts", kicker: qsTr("Dein Sound"), title: d.energy > 0.55 ? qsTr("Voller Energie") : d.energy > 0.3 ? qsTr("Schön ausgewogen") : qsTr("Eher ruhig"),
                     facts: sound })
        }

        var dec = d.decades || []
        if (dec.length > 1) {
            var best = 0
            for (var i = 1; i < dec.length; ++i) if (dec[i].plays > dec[best].plays) best = i
            var name = function (x) { return x >= 2000 ? String(x) : "'" + String(x).slice(2) }
            s.push({ kind: "bars", kicker: qsTr("Aus welcher Zeit"), title: qsTr("Am meisten aus den %1ern").arg(name(dec[best].decade)),
                     lines: d.oldest ? [qsTr("Der älteste Titel: „%1“ von %2, aus dem Jahr %3.").arg(d.oldest.name).arg(d.oldest.artist).arg(d.oldest.year)] : [],
                     bars: dec.map(function (x) { return x.plays }), labels: dec.map(function (x) { return name(x.decade) + "er" }),
                     highlight: best, tip: function (v) { return one(v, qsTr("1 Wiedergabe"), qsTr("%1 Wiedergaben")) } })
        }

        if (d.hasBefore && d.newArtists > 0)
            s.push({ kind: "list", kicker: qsTr("Neu entdeckt"), round: true, items: d.newTop || [],
                     title: one(d.newArtists, qsTr("Ein Künstler zum ersten Mal"), qsTr("%1 Künstler zum ersten Mal")) })

        var st = d.topStations || []
        if (d.radioMs >= 30 * 60000 && st.length > 0)
            s.push({ kind: "number", kicker: qsTr("Internetradio"), value: Math.round(d.radioMs / 3600000), unit: qsTr("Stunden"),
                     lines: [qsTr("Am liebsten: %1, %2 lang.").arg(st[0].name).arg(dur(st[0].ms))] })

        s.push({ kind: "summary" })
        return s
    }

    // Klassik: every slide on its own colour field, close to the accent
    function fieldColor(i) {
        return Qt.hsla((accentHue + (i % 6) * 0.085) % 1, 0.5, 0.3 + 0.05 * (i % 3), 1)
    }

    // ---- the background ----
    Rectangle { // dims the theme's world; Klassik paints a colour field, Soft and Terminal their own ground
        anchors.fill: parent
        color: rv.klassik ? rv.fieldColor(rv.index) : (window.look.skin === "soft" || window.look.skin === "terminal") ? Theme.bg : "#000000"
        opacity: rv.klassik || window.look.skin === "soft" || window.look.skin === "terminal" ? 1
               : ({ glas: 0.2, aurora: 0.25, synthwave: 0.35, vintage: 0.5, tower: 0.55 })[window.look.skin] || 0.4
        Behavior on color { ColorAnimation { duration: window.animationsActive ? 600 : 0 } }
    }
    Repeater { // Klassik: big soft shapes drifting over the field
        model: rv.klassik ? 3 : 0
        Rectangle {
            readonly property real s: [0.7, 0.45, 0.3][index]
            width: rv.height * s
            height: width
            radius: index === 1 ? width * 0.18 : width / 2
            color: Qt.rgba(1, 1, 1, [0.07, 0.06, 0.09][index])
            x: [rv.width * 0.62, -width * 0.25, rv.width * 0.2][index] + (rv.index % 3) * 30
            y: [-height * 0.3, rv.height * 0.55, rv.height * 0.72][index] - (rv.index % 2) * 40
            rotation: rv.index * [12, -20, 30][index]
            Behavior on x { NumberAnimation { duration: 900; easing.type: Easing.InOutCubic } }
            Behavior on y { NumberAnimation { duration: 900; easing.type: Easing.InOutCubic } }
            Behavior on rotation { NumberAnimation { duration: 900; easing.type: Easing.InOutCubic } }
        }
    }
    Repeater { // Terminal: scan lines over everything
        model: window.look.skin === "terminal" ? Math.ceil(rv.height / 3) : 0
        Rectangle { y: index * 3; width: rv.width; height: 1; color: Qt.rgba(0, 0, 0, 0.18); z: 5 }
    }
    MouseArea { // a click goes on (left quarter: back)
        anchors.fill: parent
        onClicked: (m) => m.x < width / 4 ? rv.previous() : rv.next()
    }

    // ---- the slide ----
    GlitchLayer {
        id: glitch
        anchors.fill: parent
        anchors.topMargin: 92
        anchors.bottomMargin: 96
        anchors.leftMargin: 70
        anchors.rightMargin: 70
        enabledFx: window.look.enter === "glitch"
        split: 0 // only the short burst when a slide arrives
        Item {
            id: stage
            width: glitch.width
            height: glitch.height
            Loader {
                id: slideLoader
                anchors.fill: parent
                sourceComponent: rv.slide.kind === "summary" ? summary : rv.slide.kind ? slideComp : null
            }
        }
    }
    Component {
        id: slideComp
        RvSlide {
            slide: rv.slide
            review: rv.info
            onPlay: (ids, startId) => Stats.playIds(ids, startId)
        }
    }

    // how a slide arrives, by theme
    function enter() {
        // a new slide: build it anew (text types and numbers count again)
        slideLoader.active = false
        slideLoader.active = true
        if (!window.animationsActive)
            return
        stage.opacity = 1; stage.x = 0; stage.y = 0; stage.scale = 1; stage.rotation = 0
        switch (window.look.enter) {
        case "zoom": zoomIn.restart(); break
        case "slide": slideIn.restart(); break
        case "flicker": flickerIn.restart(); break
        case "glitch": glitch.trigger(); riseIn.restart(); break
        case "type": break
        default: riseIn.restart()
        }
    }
    ParallelAnimation {
        id: riseIn
        NumberAnimation { target: stage; property: "opacity"; from: 0; to: 1; duration: 450 }
        NumberAnimation { target: stage; property: "y"; from: 40; to: 0; duration: 600; easing.type: Easing.OutCubic }
    }
    ParallelAnimation {
        id: zoomIn
        NumberAnimation { target: stage; property: "opacity"; from: 0; to: 1; duration: 350 }
        NumberAnimation { target: stage; property: "scale"; from: 0.86; to: 1; duration: 650; easing.type: Easing.OutBack }
    }
    ParallelAnimation {
        id: slideIn
        NumberAnimation { target: stage; property: "x"; from: rv.width * 0.5; to: 0; duration: 650; easing.type: Easing.OutCubic }
        NumberAnimation { target: stage; property: "rotation"; from: 5; to: 0; duration: 700; easing.type: Easing.OutCubic }
        NumberAnimation { target: stage; property: "opacity"; from: 0; to: 1; duration: 300 }
    }
    SequentialAnimation {
        id: flickerIn // a display that switches on
        PropertyAction { target: stage; property: "opacity"; value: 0 }
        PauseAnimation { duration: 90 }
        PropertyAction { target: stage; property: "opacity"; value: 0.8 }
        PauseAnimation { duration: 60 }
        PropertyAction { target: stage; property: "opacity"; value: 0.1 }
        PauseAnimation { duration: 80 }
        PropertyAction { target: stage; property: "opacity"; value: 1 }
        PauseAnimation { duration: 50 }
        PropertyAction { target: stage; property: "opacity"; value: 0.5 }
        PauseAnimation { duration: 40 }
        PropertyAction { target: stage; property: "opacity"; value: 1 }
    }

    // ---- the end: everything on one picture ----
    Component {
        id: summary
        Item {
            id: end
            readonly property real cardScale: Math.min(1.2, (height - 10) / share.height)
            Row {
                anchors.centerIn: parent
                spacing: 48
                Item {
                    width: share.width * end.cardScale
                    height: share.height * end.cardScale
                    ShareCard {
                        id: share
                        Component.onCompleted: rv.pendingCard = share // what "Als Bild speichern" saves
                        review: rv.info
                        persona: rv.personas[rv.info.daypart] || ""
                        scale: end.cardScale
                        transformOrigin: Item.TopLeft
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 14
                    RvText {
                        width: 360
                        full: qsTr("Das war dein %1.").arg(rv.year)
                        font.family: window.look.skin === "tower" ? "Barlow Condensed" : Theme.displayFont
                        font.pixelSize: 40
                        font.weight: Theme.style.titleWeight || Font.Bold
                        font.capitalization: window.look.upper ? Font.AllUppercase : Font.MixedCase
                        color: rv.klassik ? "#FFFFFF" : Theme.text
                    }
                    RvText {
                        width: 360
                        full: qsTr("Alles auf einem Bild – und deine Lieblingstitel als Playlist.")
                        delay: 300
                        font.family: Theme.uiFont
                        font.pixelSize: 17
                        color: rv.klassik ? Qt.rgba(1, 1, 1, 0.75) : Theme.textDim
                    }
                    Item { width: 1; height: 8 }
                    PillButton {
                        text: qsTr("Als Bild speichern")
                        iconName: "image"
                        primary: true
                        onClicked: rv.saveImage()
                    }
                    PillButton {
                        text: qsTr("Als Playlist speichern")
                        iconName: "playlist-add"
                        enabled: (rv.info.playlistIds || []).length > 0
                        onClicked: rv.savePlaylist()
                    }
                    PillButton {
                        text: qsTr("Noch einmal von vorn")
                        iconName: "refresh"
                        onClicked: { rv.index = 0; rv.enter() }
                    }
                }
            }
        }
    }

    function savePlaylist() {
        var id = Playlists.create(Playlists.uniqueName(qsTr("Mein Jahr %1").arg(year)))
        if (id > 0)
            Playlists.addIds(id, info.playlistIds || [])
    }

    property Item pendingCard: null
    function saveImage() {
        var name = qsTr("Mein Jahr in Musik %1.png").arg(year)
        SystemDialogs.saveFile(qsTr("Bild speichern"), Stats.picturesFolder(), name,
                               [{ name: qsTr("PNG-Bild"), patterns: ["*.png"] }],
                               function (path, confirmed) {
                                   if (path === null) {
                                       imageDlg.selectedFile = imageDlg.currentFolder + "/" + encodeURIComponent(name)
                                       imageDlg.open()
                                   } else if (path !== "") {
                                       if (confirmed === false) {
                                           replaceDlg.path = path
                                           replaceDlg.open()
                                       } else {
                                           rv.writeImage(path)
                                       }
                                   }
                               })
    }
    function writeImage(path) {
        if (!pendingCard)
            return
        pendingCard.grabToImage(function (result) {
            if (result.saveToFile(path))
                Library.message(qsTr("Bild gespeichert: %1").arg(path))
            else
                Library.message(qsTr("Das Bild ließ sich nicht speichern: %1").arg(path))
        }, Qt.size(1080, 1350))
    }
    FileDialog {
        id: imageDlg
        title: qsTr("Bild speichern")
        fileMode: FileDialog.SaveFile
        currentFolder: Stats.picturesFolder()
        nameFilters: [qsTr("PNG-Bild (*.png)")]
        onAccepted: rv.writeImage(decodeURIComponent(String(selectedFile).replace(/^file:\/\//, "")))
    }
    HDialog {
        id: replaceDlg
        property string path
        title: qsTr("Datei ersetzen?")
        text: qsTr("%1 gibt es schon.").arg(path)
        acceptText: qsTr("Ersetzen")
        destructive: true
        onAccepted: rv.writeImage(path)
    }

    // ---- around the slide ----
    Row { // how far: one segment per slide
        id: progress
        anchors.horizontalCenter: parent.horizontalCenter
        y: 34
        spacing: 6
        visible: rv.slides.length > 1 && window.look.skin !== "terminal" && window.look.skin !== "tower"
        readonly property real seg: Math.max(8, (Math.min(rv.width - 480, 1000) - spacing * (rv.slides.length - 1)) / Math.max(1, rv.slides.length))
        Repeater {
            model: rv.slides.length
            Rectangle {
                width: progress.seg
                height: 5
                radius: window.look.square ? 0 : 2.5
                color: rv.klassik ? Qt.rgba(1, 1, 1, index <= rv.index ? 0.95 : 0.3)
                     : index <= rv.index ? Theme.accent : Theme.alpha(Theme.text, 0.18)
                Behavior on color { ColorAnimation { duration: 300 } }
            }
        }
    }
    Text { // the terminal and the tower count instead
        visible: !progress.visible && rv.slides.length > 1
        anchors.horizontalCenter: parent.horizontalCenter
        y: 28
        text: window.look.skin === "terminal"
              ? "[" + "■".repeat(rv.index + 1) + "·".repeat(Math.max(0, rv.slides.length - rv.index - 1)) + "] " + (rv.index + 1) + "/" + rv.slides.length
              : "TRACK " + String(rv.index + 1).padStart(2, "0") + " / " + String(rv.slides.length).padStart(2, "0")
        font.family: window.look.skin === "terminal" ? Theme.monoFont : "DSEG14 Classic"
        font.pixelSize: window.look.skin === "terminal" ? 15 : 18
        color: Theme.accent
    }
    PillButton { // music to it: the year's favourites
        x: 28
        y: 22
        compact: true
        visible: (rv.info.playlistIds || []).length > 0 && rv.info.year === rv.year
        text: qsTr("Mit deinen Liedern")
        iconName: "play"
        onClicked: Stats.playIds(rv.info.playlistIds, 0)
    }
    IconButton {
        anchors.right: parent.right
        anchors.rightMargin: 24
        y: 20
        iconName: "close"
        iconColor: rv.klassik ? "#FFFFFF" : Theme.text
        tip: qsTr("Schließen (Esc)")
        onClicked: rv.close()
    }
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 30
        spacing: 18
        IconButton {
            iconName: "chevron"
            rotation: 180
            enabled: rv.index > 0
            opacity: enabled ? 1 : 0.3
            iconColor: rv.klassik ? "#FFFFFF" : Theme.text
            tip: qsTr("Zurück (←)")
            onClicked: rv.previous()
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: Stats.reviewBusy && rv.slides.length === 0 ? qsTr("Wird gezählt …") : (rv.index + 1) + " / " + rv.slides.length
            font.family: Theme.uiFont
            font.pixelSize: 14
            color: rv.klassik ? Qt.rgba(1, 1, 1, 0.8) : Theme.textDim
        }
        IconButton {
            iconName: "chevron"
            enabled: rv.index < rv.slides.length - 1
            opacity: enabled ? 1 : 0.3
            iconColor: rv.klassik ? "#FFFFFF" : Theme.text
            tip: qsTr("Weiter (→)")
            onClicked: rv.next()
        }
    }
}
