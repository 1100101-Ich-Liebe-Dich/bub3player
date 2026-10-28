// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Statistics: what you listened to in a week, a month, a year or ever – time,
// plays, the course over time, when you listen, the top artists, titles, albums,
// genres and stations – and what the library holds. Charts in the theme's look.
// From here the year in review opens.
Item {
    id: page
    property string pageId: "stats"
    readonly property int inset: 36
    readonly property var ov: Stats.overview
    readonly property var lib: Stats.library
    readonly property bool hasData: (ov.listenedMs || 0) > 0
    // the year shown (paging back through years), else the one just past or running
    readonly property int reviewYear: Stats.period === "year" ? new Date().getFullYear() + Stats.offset : Stats.reviewYear()
    readonly property bool canReview: Stats.years.some(function (y) { return y.year === page.reviewYear && y.plays >= 20 })
    readonly property real contentWidth: Math.min(col.width - 2 * inset, 1180)

    Component.onCompleted: Stats.refresh()
    // counting a big library leaves memory behind in the worker threads: give it back
    Connections {
        target: Stats
        function onLibraryChanged() { Memory.trimSoon(500) }
    }
    function scrollTo(y) { flick.contentY = Math.max(0, Math.min(y, flick.contentHeight - flick.height)) }

    function dur(ms) { return Library.formatDuration(ms || 0, true) }
    // under an hour in minutes, else hours: [value, unit]
    function time(ms, longUnit) {
        return (ms || 0) < 3600000 ? [Library.formatCount(Math.round((ms || 0) / 60000)), qsTr("Min.")]
                                   : [hours(ms), longUnit ? qsTr("Stunden") : qsTr("Std.")]
    }
    function hours(ms) {
        var h = (ms || 0) / 3600000
        return h >= 10 ? Library.formatCount(Math.round(h)) : Number(h).toLocaleString(Qt.locale("de_DE"), "f", 1)
    }
    function bytes(b) {
        var units = ["B", "KB", "MB", "GB", "TB"]
        var i = 0, v = b || 0
        while (v >= 1000 && i < units.length - 1) { v /= 1000; ++i }
        return (i >= 3 ? v.toFixed(1).replace(".", ",") : Math.round(v)) + " " + units[i]
    }
    function count(n, one, many) { return n === 1 ? one : many.arg(Library.formatCount(n || 0)) }
    function delta() {
        if (ov.previousMs === undefined || !(ov.previousMs > 0)) return ""
        var d = Math.round((ov.listenedMs - ov.previousMs) / ov.previousMs * 100)
        // a period still running is compared with the same span of the one before
        var before = (ov.current ? { week: qsTr("zum selben Zeitpunkt der Woche davor"), month: qsTr("zum selben Tag des Monats davor"),
                                     year: qsTr("zum selben Tag des Jahres davor") }
                                 : { week: qsTr("zur Woche davor"), month: qsTr("zum Monat davor"), year: qsTr("zum Jahr davor") })[Stats.period] || ""
        return (d > 0 ? "▲ " : d < 0 ? "▼ " : "= ") + Math.abs(d) + " % " + before
    }
    // the bar of today (this day, this month, this year)
    function todayIndex() {
        if (!ov.current) return -1
        var now = new Date()
        return ({ week: (now.getDay() + 6) % 7, month: now.getDate() - 1, year: now.getMonth(),
                  all: (ov.chart || []).length - 1 })[Stats.period]
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: col.height + 60
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HScrollBar {}

        Column {
            id: col
            width: flick.width
            spacing: 22

            PageHeader {
                width: parent.width
                title: qsTr("Statistik")
                subtitle: [page.ov.label || "", page.ov.detail || ""].filter(function (s) { return s !== "" }).join(" · ")
                loading: Stats.busy
                PillButton {
                    visible: page.canReview
                    primary: true
                    iconName: "star"
                    text: qsTr("Dein Jahr %1").arg(page.reviewYear)
                    onClicked: window.openReview(page.reviewYear)
                }
            }

            // ---- which time ----
            Row {
                x: page.inset
                spacing: 10
                HSegmented {
                    width: 600 // laid out in one row, then just as wide as that (a Flow needs a width)
                    Component.onCompleted: width = childrenRect.width
                    options: [qsTr("Woche"), qsTr("Monat"), qsTr("Jahr"), qsTr("Gesamt")]
                    values: ["week", "month", "year", "all"]
                    current: Stats.period
                    onPicked: (v) => Stats.setPeriod(v, 0)
                }
                IconButton {
                    visible: Stats.period !== "all"
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "chevron"
                    rotation: 180
                    tip: qsTr("Davor")
                    onClicked: Stats.setPeriod(Stats.period, Stats.offset - 1)
                }
                IconButton {
                    visible: Stats.period !== "all"
                    enabled: Stats.offset < 0
                    opacity: enabled ? 1 : 0.35
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "chevron"
                    tip: qsTr("Danach")
                    onClicked: Stats.setPeriod(Stats.period, Stats.offset + 1)
                }
            }

            EmptyState {
                visible: !page.hasData && !Stats.busy
                anchors.horizontalCenter: parent.horizontalCenter
                iconName: "chart"
                title: qsTr("Noch nichts gehört")
                text: Stats.period === "all" ? qsTr("Sobald du Musik hörst, steht hier, was, wann und wie viel. Alles bleibt auf deinem Rechner.")
                                             : qsTr("In diesem Zeitraum lief noch nichts. Probier einen anderen Zeitraum.")
            }

            // ---- the numbers ----
            Flow {
                visible: page.hasData
                x: page.inset
                width: page.contentWidth
                spacing: 14
                StatTile {
                    width: 260
                    big: true
                    value: page.time(page.ov.listenedMs, true)[0]
                    unit: page.time(page.ov.listenedMs, true)[1]
                    label: qsTr("Hörzeit")
                    sub: page.delta()
                }
                StatTile { value: Library.formatCount(page.ov.plays || 0); label: page.ov.plays === 1 ? qsTr("Wiedergabe") : qsTr("Wiedergaben") }
                StatTile { value: Library.formatCount(page.ov.tracks || 0); label: qsTr("verschiedene Titel") }
                StatTile { value: Library.formatCount(page.ov.artists || 0); label: qsTr("Künstler") }
                StatTile { value: Library.formatCount(page.ov.albums || 0); label: page.ov.albums === 1 ? qsTr("Album") : qsTr("Alben") }
                StatTile {
                    visible: (page.ov.radioMs || 0) > 0
                    value: page.time(page.ov.radioMs)[0]
                    unit: page.time(page.ov.radioMs)[1]
                    label: qsTr("Internetradio")
                }
            }

            // ---- the course over time ----
            StatCard {
                visible: page.hasData
                x: page.inset
                width: page.contentWidth
                Column {
                    width: parent.width
                    spacing: 16
                    SectionTitle { text: qsTr("Verlauf") }
                    StatBars {
                        id: chartBars
                        width: parent.width
                        height: 190
                        values: (page.ov.chart || []).map(function (c) { return c.ms })
                        labels: (page.ov.chart || []).map(function (c) { return c.label })
                        labelEvery: values.length > 20 ? 5 : 1
                        highlight: page.todayIndex()
                        tipText: function (i) { return page.dur(chartBars.values[i]) }
                    }
                }
            }

            StatCard {
                visible: page.hasData
                x: page.inset
                width: page.contentWidth
                Column {
                    width: parent.width
                    spacing: 16
                    SectionTitle { text: qsTr("Wann du hörst") }
                    StatHeat {
                        id: heat
                        width: parent.width
                        height: implicitHeight
                        values: page.ov.heat || []
                        tipText: function (day, hour) {
                            return qsTr("%1, %2–%3 Uhr: %4").arg(heat.days[day]).arg(hour).arg(hour + 1).arg(page.dur(heat.values[day * 24 + hour]))
                        }
                    }
                }
            }

            // ---- the tops ----
            Flow {
                visible: page.hasData
                x: page.inset
                width: page.contentWidth
                spacing: 18
                readonly property real half: width >= 760 ? (width - spacing) / 2 : width
                TopList {
                    width: parent.half
                    title: qsTr("Künstler")
                    items: page.ov.topArtists || []
                    round: true
                    // counted by the artist of each title, so their titles (an album artist would miss guests)
                    onActivated: (item) => window.openTracks(item.name, qsTr("Künstler"), { filters: [{ field: "artist", op: "eq", value: item.name }], sort: "album" }, item.trackId)
                }
                TopList {
                    width: parent.half
                    title: qsTr("Titel")
                    items: page.ov.topTracks || []
                    playable: true
                    onActivated: (item) => Stats.playIds((page.ov.topTracks || []).map(function (t) { return t.trackId }), item.trackId)
                }
                TopList {
                    width: parent.half
                    title: qsTr("Alben")
                    items: page.ov.topAlbums || []
                    onActivated: (item) => window.openAlbum(item.artist, item.name, item.trackId)
                }
                TopList {
                    width: parent.half
                    title: qsTr("Genres")
                    items: page.ov.topGenres || []
                    noCover: true
                }
                TopList {
                    visible: (page.ov.topStations || []).length > 0
                    width: parent.half
                    title: qsTr("Sender")
                    items: (page.ov.topStations || []).map(function (s) { return { name: s.name, ms: s.ms, trackId: 0 } })
                    noCover: true
                    byTime: true
                }
            }

            // ---- the library ----
            Text {
                x: page.inset
                visible: page.lib.tracks !== undefined
                text: qsTr("Deine Bibliothek")
                font.family: Theme.displayFont
                font.pixelSize: 24
                font.weight: Theme.style.titleWeight || Font.Bold
                font.capitalization: Theme.style.titleUpper ? Font.AllUppercase : Font.MixedCase
                color: Theme.text
            }
            Flow {
                visible: page.lib.tracks !== undefined
                x: page.inset
                width: page.contentWidth
                spacing: 14
                StatTile { value: Library.formatCount(page.lib.tracks || 0); label: qsTr("Titel") }
                StatTile {
                    readonly property bool days: (page.lib.durationMs || 0) >= 2 * 86400000
                    value: days ? Library.formatCount(Math.round(page.lib.durationMs / 86400000)) : page.hours(page.lib.durationMs)
                    unit: days ? qsTr("Tage") : qsTr("Std.")
                    label: qsTr("Musik am Stück")
                }
                StatTile { value: page.bytes(page.lib.bytes); label: qsTr("auf der Platte") }
                StatTile { value: Library.formatCount(page.lib.artists || 0); label: qsTr("Albumkünstler") }
                StatTile { value: Library.formatCount(page.lib.albums || 0); label: qsTr("Alben") }
                StatTile { value: Library.formatCount(page.lib.genres || 0); label: qsTr("Genres") }
                StatTile { value: Library.formatCount(page.lib.favorites || 0); label: qsTr("Favoriten") }
                StatTile {
                    value: (page.lib.tracks || 0) > 0 ? Math.round((page.lib.tracks - page.lib.neverPlayed) / page.lib.tracks * 100) + " %" : "–"
                    label: qsTr("schon gehört")
                }
                StatTile {
                    value: (page.lib.tracks || 0) > 0 ? Math.round(Math.min(1, (page.lib.analysed || 0) / page.lib.tracks) * 100) + " %" : "–"
                    label: qsTr("analysiert")
                }
                StatTile {
                    visible: (page.lib.hires || 0) > 0
                    value: Library.formatCount(page.lib.hires || 0)
                    label: qsTr("in Hi-Res")
                }
                StatTile {
                    visible: (page.lib.oldestYear || 0) > 0 && page.lib.newestYear > page.lib.oldestYear
                    value: page.lib.oldestYear + "–" + page.lib.newestYear
                    label: qsTr("aus den Jahren")
                }
                StatTile {
                    visible: (page.lib.remote || 0) > 0
                    value: Library.formatCount(page.lib.remote || 0)
                    label: qsTr("vom Jellyfin-Server")
                }
            }
            Flow {
                visible: page.lib.tracks !== undefined && (page.lib.tracks || 0) > 0
                x: page.inset
                width: page.contentWidth
                spacing: 18
                readonly property real half: width >= 760 ? (width - spacing) / 2 : width
                StatCard {
                    width: parent.half
                    Column {
                        width: parent.width
                        spacing: 12
                        SectionTitle { text: qsTr("Formate") }
                        Repeater {
                            model: page.lib.formats || []
                            Item {
                                width: parent.width
                                height: 22
                                Text {
                                    id: fmtName
                                    width: 80
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.name
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: 13
                                    color: Theme.text
                                }
                                StatMeter {
                                    anchors.left: fmtName.right
                                    anchors.right: fmtCount.left
                                    anchors.rightMargin: 12
                                    anchors.verticalCenter: parent.verticalCenter
                                    value: modelData.count / Math.max(1, page.lib.tracks)
                                    strong: index === 0
                                }
                                Text {
                                    id: fmtCount
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 90
                                    horizontalAlignment: Text.AlignRight
                                    text: Library.formatCount(modelData.count)
                                    font.family: Theme.uiFont
                                    font.pixelSize: 13
                                    color: Theme.textDim
                                }
                            }
                        }
                    }
                }
                StatCard {
                    width: parent.half
                    visible: (page.lib.decades || []).length > 0
                    Column {
                        width: parent.width
                        spacing: 16
                        SectionTitle { text: qsTr("Aus welchem Jahrzehnt") }
                        StatBars {
                            id: decadeBars
                            width: parent.width
                            height: 150
                            values: (page.lib.decades || []).map(function (d) { return d.count })
                            labels: (page.lib.decades || []).map(function (d) { return (d.decade >= 2000 || d.decade < 1920 ? String(d.decade) : String(d.decade).slice(2)) + "er" })
                            tipText: function (i) { return page.count(decadeBars.values[i], qsTr("1 Titel"), qsTr("%1 Titel")) }
                        }
                    }
                }
            }
        }
    }

    // ---- parts ----
    component SectionTitle: Text {
        font.family: Theme.displayFont
        font.pixelSize: 18
        font.weight: Theme.style.titleWeight || Font.Bold
        font.capitalization: Theme.style.titleUpper ? Font.AllUppercase : Font.MixedCase
        color: Theme.text
    }

    // A number with its label.
    component StatTile: StatCard {
        id: tile
        property string value
        property string unit
        property string label
        property string sub
        property bool big: false
        width: Math.max(170, valueRow.implicitWidth + 2 * padding) // a long value (years) widens it
        padding: 16
        Column {
            width: parent.width
            spacing: 2
            Row {
                id: valueRow
                spacing: 6
                Text {
                    text: tile.value
                    font.family: window.look.numberFont
                    font.weight: window.look.numberWeight
                    font.pixelSize: tile.big ? 44 : 30
                    color: tile.big ? Theme.accent : Theme.text
                }
                Text {
                    visible: tile.unit !== ""
                    anchors.baseline: parent.children[0].baseline
                    text: tile.unit
                    font.family: Theme.uiFont
                    font.pixelSize: tile.big ? 16 : 13
                    color: Theme.textDim
                }
            }
            Text {
                width: parent.width
                text: tile.label
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: 13
                color: Theme.textDim
            }
            Text {
                visible: tile.sub !== ""
                width: parent.width
                text: tile.sub
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: 12
                color: Theme.textFaint
            }
        }
    }

    // The top ten of something: rank, picture, name, how often, a share bar.
    component TopList: StatCard {
        id: tl
        property string title
        property var items: []
        property bool round: false
        property bool playable: false
        property bool noCover: false
        property bool byTime: false
        signal activated(var item)
        Column {
            width: parent.width
            spacing: 6
            SectionTitle { text: tl.title }
            Repeater {
                model: tl.items
                delegate: Item {
                    id: row
                    width: parent.width
                    height: tl.noCover ? 34 : 46
                    readonly property real best: tl.items.length > 0 ? (tl.byTime ? tl.items[0].ms : tl.items[0].plays) : 1
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4
                        radius: window.look.square ? 0 : Theme.radius
                        color: rowMouse.containsMouse && rowMouse.enabled ? Theme.alpha(Theme.text, 0.06) : "transparent"
                    }
                    Text {
                        id: rank
                        width: 26
                        anchors.verticalCenter: parent.verticalCenter
                        text: index + 1
                        horizontalAlignment: Text.AlignRight
                        font.family: window.look.numberFont
                        font.pixelSize: 15
                        color: index === 0 ? Theme.accent : Theme.textFaint
                    }
                    CoverArt {
                        id: pic
                        visible: !tl.noCover
                        anchors.left: rank.right
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: tl.noCover ? 0 : 38
                        height: width
                        radius: tl.round ? width / 2 : Math.min(6, Theme.radius)
                        coverId: modelData.trackId || 0
                        artist: tl.round ? modelData.name : ""
                        label: modelData.name
                        requestSize: 64
                    }
                    Column {
                        anchors.left: tl.noCover ? rank.right : pic.right
                        anchors.leftMargin: 12
                        anchors.right: amount.left
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Text {
                            width: parent.width
                            text: modelData.name + (modelData.artist && !tl.round && !tl.noCover ? "  ·  " + modelData.artist : "")
                            elide: Text.ElideRight
                            font.family: Theme.uiFont
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }
                        StatMeter {
                            width: parent.width
                            value: (tl.byTime ? modelData.ms : modelData.plays) / Math.max(1, row.best)
                            strong: index === 0
                        }
                    }
                    Text {
                        id: amount
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: tl.byTime ? page.dur(modelData.ms) : Library.formatCount(modelData.plays) + "×"
                        font.family: Theme.uiFont
                        font.pixelSize: 12
                        color: Theme.textDim
                    }
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        enabled: !tl.noCover && (modelData.trackId > 0 || !tl.playable)
                        hoverEnabled: true
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: tl.activated(modelData)
                    }
                }
            }
        }
    }
}
