// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import Halcyon

// Rules of a smart playlist: which tracks, in which order, how many.
// Every change is counted right away ("passt gerade auf 1 234 Titel").
HDialog {
    id: ed
    property var playlistId: 0
    property string match: "all"
    property string sortKey: "random"
    property bool sortDesc: false
    property bool limitOn: false
    property string limitText: "50"
    property int previewSerial: 0
    property string previewText: ""
    property bool previewError: false
    signal saved(var id, bool created)

    dialogWidth: 820
    title: playlistId > 0 ? qsTr("Smart-Playlist bearbeiten") : qsTr("Neue Smart-Playlist")
    acceptText: playlistId > 0 ? qsTr("Speichern") : qsTr("Anlegen")
    acceptEnabled: nameField.text.trim() !== "" && rulesValid && !previewError

    readonly property var fields: [
        { key: "artist", label: qsTr("Künstler"), type: "text" },
        { key: "albumArtist", label: qsTr("Albumkünstler"), type: "text" },
        { key: "title", label: qsTr("Titel"), type: "text" },
        { key: "album", label: qsTr("Album"), type: "text" },
        { key: "genre", label: qsTr("Genre"), type: "text" },
        { key: "composer", label: qsTr("Komponist"), type: "text" },
        { key: "year", label: qsTr("Erscheinungsjahr"), type: "number" },
        { key: "added", label: qsTr("Hinzugefügt"), type: "date" },
        { key: "lastPlayed", label: qsTr("Zuletzt gespielt"), type: "played" },
        { key: "playCount", label: qsTr("Wiedergaben"), type: "number" },
        { key: "skipCount", label: qsTr("Übersprungen"), type: "number" },
        { key: "duration", label: qsTr("Dauer (Minuten)"), type: "minutes" },
        { key: "bpm", label: qsTr("Tempo (BPM)"), type: "measured" },
        { key: "key", label: qsTr("Tonart"), type: "key" },
        { key: "energy", label: qsTr("Energie (%)"), type: "percent" },
        { key: "loudness", label: qsTr("Lautheit (LUFS)"), type: "measured" },
        { key: "codec", label: qsTr("Format"), type: "text" },
        { key: "bits", label: qsTr("Bittiefe (bit)"), type: "number" },
        { key: "sampleRate", label: qsTr("Abtastrate (kHz)"), type: "khz" },
        { key: "bitrate", label: qsTr("Bitrate (kbit/s)"), type: "number" },
        { key: "folder", label: qsTr("Ordner"), type: "folder" },
        { key: "source", label: qsTr("Quelle"), type: "source" },
        { key: "playlist", label: qsTr("Playlist"), type: "playlist" },
        { key: "mtime", label: qsTr("Datei geändert"), type: "date" },
        { key: "filename", label: qsTr("Dateiname"), type: "text" }
    ]
    readonly property var fieldOptions: fields.map(function (f) { return { value: f.key, label: f.label } })
    readonly property var opsByType: ({
        text: [{ value: "contains", label: qsTr("enthält") }, { value: "eq", label: qsTr("ist") }, { value: "ne", label: qsTr("ist nicht") },
               { value: "startsWith", label: qsTr("beginnt mit") }, { value: "empty", label: qsTr("ist leer") }, { value: "notEmpty", label: qsTr("ist nicht leer") }],
        number: [{ value: "ge", label: qsTr("mindestens") }, { value: "le", label: qsTr("höchstens") }, { value: "eq", label: qsTr("genau") }, { value: "ne", label: qsTr("nicht") }],
        minutes: [{ value: "ge", label: qsTr("mindestens") }, { value: "le", label: qsTr("höchstens") }],
        khz: [{ value: "ge", label: qsTr("mindestens") }, { value: "le", label: qsTr("höchstens") }, { value: "eq", label: qsTr("genau") }],
        percent: [{ value: "ge", label: qsTr("mindestens") }, { value: "le", label: qsTr("höchstens") }],
        measured: [{ value: "ge", label: qsTr("mindestens") }, { value: "le", label: qsTr("höchstens") }, { value: "empty", label: qsTr("noch unbekannt") }],
        date: [{ value: "withinDays", label: qsTr("in den letzten … Tagen") }, { value: "olderThanDays", label: qsTr("vor mehr als … Tagen") }],
        played: [{ value: "withinDays", label: qsTr("in den letzten … Tagen") }, { value: "olderThanDays", label: qsTr("vor mehr als … Tagen") },
                 { value: "never", label: qsTr("noch nie") }, { value: "ever", label: qsTr("schon mal") }],
        key: [{ value: "eq", label: qsTr("ist") }, { value: "ne", label: qsTr("ist nicht") }],
        source: [{ value: "local", label: qsTr("ist lokal") }, { value: "remote", label: qsTr("ist der Jellyfin-Server") }],
        playlist: [{ value: "eq", label: qsTr("ist in") }, { value: "ne", label: qsTr("ist nicht in") }],
        folder: [{ value: "under", label: qsTr("liegt in") }]
    })
    readonly property var noValueOps: ["empty", "notEmpty", "never", "ever", "local", "remote"]
    readonly property var sortOptions: [
        { value: "random", label: qsTr("Zufällig") }, { value: "added", label: qsTr("Hinzugefügt") },
        { value: "lastPlayed", label: qsTr("Zuletzt gespielt") }, { value: "playCount", label: qsTr("Wiedergaben") },
        { value: "album", label: qsTr("Album") }, { value: "artist", label: qsTr("Künstler") },
        { value: "title", label: qsTr("Titel") }, { value: "year", label: qsTr("Erscheinungsjahr") },
        { value: "bpm", label: qsTr("Tempo") }, { value: "energy", label: qsTr("Energie") },
        { value: "duration", label: qsTr("Dauer") }
    ]
    // Camelot wheel, German key names (as in "Nach Tonart"). Stored as key * 2 + minor.
    readonly property var keyOptions: {
        var names = ["C", "Des", "D", "Es", "E", "F", "Fis", "G", "As", "A", "B", "H"]
        var out = []
        for (var minor = 1; minor >= 0; --minor) {
            var row = []
            for (var k = 0; k < 12; ++k) {
                var major = minor ? (k + 3) % 12 : k
                var num = ((7 * major) % 12 + 7) % 12 + 1
                var name = minor ? names[k].toLowerCase() + "-Moll" : names[k] + "-Dur"
                row.push({ value: k * 2 + minor, label: num + (minor ? "A" : "B") + " · " + name, num: num })
            }
            row.sort(function (a, b) { return a.num - b.num })
            out = out.concat(row)
        }
        return out
    }
    readonly property var playlistOptions: Playlists.normalPlaylists
        .filter(function (p) { return p.id !== ed.playlistId })
        .map(function (p) { return { value: p.id, label: p.name } })

    ListModel { id: rules }

    function typeOf(field) {
        for (var i = 0; i < fields.length; ++i) if (fields[i].key === field) return fields[i].type
        return "text"
    }
    function defaultValue(type) {
        if (type === "key") return "16"            // 8A, a-Moll
        if (type === "playlist") return playlistOptions.length > 0 ? String(playlistOptions[0].value) : ""
        if (type === "date" || type === "played") return "30"
        return ""
    }
    function addRule(field) {
        var t = typeOf(field)
        rules.append({ field: field, op: opsByType[t][0].value, value: defaultValue(t) })
    }
    function num(v) { return Number(String(v).replace(",", ".")) }
    function needsValue(r) { return noValueOps.indexOf(r.op) < 0 }
    readonly property bool rulesValid: {
        var c = rules.count // re-evaluate on changes (see rulesTick)
        var tick = ed.rulesTick
        for (var i = 0; i < rules.count; ++i) {
            var r = rules.get(i)
            if (!needsValue(r)) continue
            var t = typeOf(r.field)
            if (String(r.value).trim() === "") return false
            if (t !== "text" && t !== "folder" && isNaN(num(r.value))) return false
        }
        return !limitOn || (num(limitText) > 0)
    }
    property int rulesTick: 0

    // ---------- rules <-> spec ----------
    function toFilter(r) {
        var t = typeOf(r.field)
        switch (r.op) {
        case "never": return { field: r.field, op: "eq", value: 0 }
        case "ever": return { field: r.field, op: "gt", value: 0 }
        case "local": return { field: "source", op: "eq", value: 1 }
        case "remote": return { field: "source", op: "ne", value: 1 }
        case "empty": case "notEmpty": return { field: r.field, op: r.op }
        }
        var v = r.value
        if (t === "minutes") v = Math.round(num(v) * 60000)
        else if (t === "khz") v = Math.round(num(v) * 1000)
        else if (t === "percent") v = num(v) / 100
        else if (t === "number" || t === "measured" || t === "date" || t === "played" || t === "key" || t === "playlist") v = num(v)
        else v = String(v)
        return { field: r.field, op: r.op, value: v }
    }
    function fromFilter(f) {
        var t = typeOf(f.field)
        var op = f.op, v = f.value
        if (f.field === "source") return { field: "source", op: f.op === "eq" ? "local" : "remote", value: "" }
        if (t === "played" && f.op === "eq" && Number(v) === 0) return { field: f.field, op: "never", value: "" }
        if (t === "played" && f.op === "gt" && Number(v) === 0) return { field: f.field, op: "ever", value: "" }
        if (v === undefined || v === null) v = ""
        else if (t === "minutes") v = String(Math.round(Number(v) / 6000) / 10)
        else if (t === "khz") v = String(Number(v) / 1000)
        else if (t === "percent") v = String(Math.round(Number(v) * 100))
        else v = String(v)
        return { field: f.field, op: op, value: v }
    }
    function spec() {
        var filters = []
        for (var i = 0; i < rules.count; ++i) filters.push(toFilter(rules.get(i)))
        var s = { match: match, filters: filters, sort: sortKey, desc: sortDesc }
        if (limitOn && num(limitText) > 0) s.limit = Math.round(num(limitText))
        return s
    }

    function openFor(id, name) {
        ++session
        playlistId = id
        rules.clear()
        previewText = ""
        previewError = false
        if (id > 0) {
            var p = Playlists.get(id)
            var r = p.rules || {}
            nameField.text = p.name
            match = r.match === "any" ? "any" : "all"
            sortKey = r.sort || "random"
            sortDesc = r.desc === true
            limitOn = r.limit > 0
            limitText = r.limit > 0 ? String(r.limit) : "50"
            var fs = r.filters || []
            for (var i = 0; i < fs.length; ++i) rules.append(fromFilter(fs[i]))
        } else {
            nameField.text = name && name !== "" ? name : Playlists.uniqueName(qsTr("Neue Smart-Playlist"))
            match = "all"
            sortKey = "added"
            sortDesc = true
            limitOn = false
            limitText = "50"
            addRule("added")
        }
        rulesTick++
        open()
        refresh()
    }
    function changed() { rulesTick++; refresh() }
    function refresh() { previewTimer.restart() }

    Timer {
        id: previewTimer
        interval: 250
        onTriggered: {
            if (!ed.rulesValid) { ed.previewText = qsTr("Bitte alle Werte ausfüllen"); return }
            ed.previewSerial = Playlists.preview(ed.spec())
        }
    }
    Connections {
        target: Playlists
        function onPreviewReady(serial, count, ms, error) {
            if (serial !== ed.previewSerial) return
            ed.previewError = error !== ""
            if (error !== "") ed.previewText = error
            else if (count === 0) ed.previewText = qsTr("Passt gerade auf keinen Titel")
            else ed.previewText = qsTr("Passt gerade auf %1 · %2")
                    .arg(count === 1 ? qsTr("1 Titel") : qsTr("%1 Titel").arg(Library.formatCount(count)))
                    .arg(Library.formatDuration(ms, true))
        }
    }

    onAccepted: {
        if (playlistId > 0) {
            Playlists.rename(playlistId, nameField.text)
            if (Playlists.setRules(playlistId, spec())) saved(playlistId, false)
        } else {
            var id = Playlists.create(nameField.text, spec())
            if (id > 0) saved(id, true)
        }
    }

    // ---------- layout ----------
    HTextField {
        id: nameField
        width: parent.width
        placeholder: qsTr("Name")
    }

    Row {
        spacing: 10
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Titel, für die")
            font.family: Theme.uiFont
            font.pixelSize: 14
            color: Theme.textDim
        }
        HSegmented {
            anchors.verticalCenter: parent.verticalCenter
            options: [{ value: "all", label: qsTr("alle Regeln gelten") }, { value: "any", label: qsTr("mindestens eine Regel gilt") }]
            current: ed.match
            onPicked: (v) => { ed.match = v; ed.changed() }
        }
    }

    Column {
        width: parent.width
        spacing: 8
        Repeater {
            model: rules
            Item {
                id: ruleRow
                width: parent.width
                height: 38
                readonly property string type: ed.typeOf(model.field)
                readonly property bool hasValue: ed.noValueOps.indexOf(model.op) < 0
                HCombo {
                    id: fieldBox
                    width: 200
                    options: ed.fieldOptions
                    current: model.field
                    onPicked: (v) => {
                        var t = ed.typeOf(v)
                        rules.setProperty(index, "field", v)
                        rules.setProperty(index, "op", ed.opsByType[t][0].value)
                        rules.setProperty(index, "value", ed.defaultValue(t))
                        ed.changed()
                    }
                }
                HCombo {
                    id: opBox
                    x: fieldBox.width + 8
                    width: 210
                    options: ed.opsByType[ruleRow.type]
                    current: model.op
                    onPicked: (v) => { rules.setProperty(index, "op", v); ed.changed() }
                }
                Item {
                    x: opBox.x + opBox.width + 8
                    width: ruleRow.width - x - 44
                    height: parent.height
                    visible: ruleRow.hasValue
                    HTextField {
                        visible: ruleRow.type !== "key" && ruleRow.type !== "playlist"
                        anchors.left: parent.left
                        anchors.right: pick.visible ? pick.left : parent.right
                        anchors.rightMargin: pick.visible ? 6 : 0
                        height: parent.height
                        numeric: ruleRow.type !== "text" && ruleRow.type !== "folder"
                        placeholder: ruleRow.type === "date" || ruleRow.type === "played" ? qsTr("Tage")
                                   : ruleRow.type === "folder" ? qsTr("/pfad/zum/ordner") : qsTr("Wert")
                        text: model.value
                        onTextChanged: if (text !== model.value) { rules.setProperty(index, "value", text); ed.changed() }
                    }
                    IconButton {
                        id: pick
                        visible: ruleRow.type === "folder"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "folder"
                        tip: qsTr("Ordner wählen")
                        onClicked: ed.pickFolder(index)
                    }
                    HCombo {
                        visible: ruleRow.type === "key" || ruleRow.type === "playlist"
                        anchors.fill: parent
                        options: ruleRow.type === "key" ? ed.keyOptions : ed.playlistOptions
                        current: model.value
                        onPicked: (v) => { rules.setProperty(index, "value", String(v)); ed.changed() }
                    }
                }
                IconButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "close"
                    tip: qsTr("Regel entfernen")
                    onClicked: { rules.remove(index); ed.changed() }
                }
            }
        }
        Text {
            visible: rules.count === 0
            text: qsTr("Ohne Regel gehören alle Titel dazu.")
            font.family: Theme.uiFont
            font.pixelSize: 13
            color: Theme.textFaint
        }
        PillButton {
            compact: true
            text: qsTr("Regel hinzufügen")
            iconName: "add"
            onClicked: { ed.addRule("genre"); ed.changed() }
        }
    }

    Flow {
        width: parent.width
        spacing: 10
        Text {
            height: 38
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Sortieren nach")
            font.family: Theme.uiFont
            font.pixelSize: 14
            color: Theme.textDim
        }
        HCombo {
            width: 200
            options: ed.sortOptions
            current: ed.sortKey
            onPicked: (v) => { ed.sortKey = v; ed.changed() }
        }
        HToggle {
            y: 3
            visible: ed.sortKey !== "random"
            label: qsTr("absteigend")
            checked: ed.sortDesc
            onToggled: { ed.sortDesc = checked; ed.changed() }
        }
        Item { width: 12; height: 1 }
        HToggle {
            y: 3
            label: qsTr("höchstens")
            checked: ed.limitOn
            onToggled: { ed.limitOn = checked; ed.changed() }
        }
        HTextField {
            visible: ed.limitOn
            width: 90
            numeric: true
            text: ed.limitText
            onTextChanged: if (text !== ed.limitText) { ed.limitText = text; ed.changed() }
        }
        Text {
            visible: ed.limitOn
            height: 38
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Titel")
            font.family: Theme.uiFont
            font.pixelSize: 14
            color: Theme.textDim
        }
    }

    Row {
        spacing: 8
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: ed.previewError ? "close" : "filter"
            size: 16
            color: ed.previewError ? "#ff8a80" : Theme.accent
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: ed.previewText
            font.family: Theme.uiFont
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: ed.previewError ? "#ff8a80" : Theme.text
        }
    }

    // The desktop's own folder dialog; Qt's one only where there is none.
    // The editor may have been closed or opened for another playlist meanwhile
    // (the dialog is not modal on Wayland): the answer only counts for the same rule.
    property int session: 0
    onClosed: ++session
    function pickFolder(row) {
        var s = session
        SystemDialogs.chooseFolder(folderPick.title, rules.get(row).value, function (path) {
            if (path === null) { folderPick.row = row; folderPick.open() }
            else if (path !== "" && s === session && row < rules.count && typeOf(rules.get(row).field) === "folder") setFolder(row, path)
        })
    }
    function setFolder(row, path) {
        if (row >= 0 && row < rules.count) { rules.setProperty(row, "value", path); ed.changed() }
    }
    FolderDialog {
        id: folderPick
        property int row: -1
        title: qsTr("Ordner wählen")
        onAccepted: ed.setFolder(row, decodeURIComponent(String(selectedFolder).replace(/^file:\/\//, "")))
    }
}
