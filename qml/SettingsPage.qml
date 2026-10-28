// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Halcyon

// Settings in six tabs: general (library, performance, views), theme (pick a
// skin, then that skin's own options), mix (transitions, recommendations,
// analysis), sound (output, equalizer, ReplayGain), Jellyfin and about.
Item {
    id: page
    property string pageId: "settings"
    readonly property int colWidth: Math.min(920, flick.width - 72)
    // --ui-test scroll
    function scrollStep() { flick.contentY = Math.min(Math.max(0, flick.contentHeight - flick.height), flick.contentY + 300) }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: col.height + 80
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HScrollBar {}

        Column {
            id: col
            x: 36
            width: page.colWidth
            spacing: 30

            PageHeader {
                x: -36
                width: col.width + 72
                title: qsTr("Einstellungen")
            }

            // Tabs
            Row {
                spacing: 6
                Repeater {
                    model: [qsTr("Allgemein"), qsTr("Theme"), qsTr("Mix"), qsTr("Klang"), qsTr("Jellyfin"), qsTr("Über")]
                    AbstractButton {
                        id: tab
                        readonly property bool on: window.settingsTab === index
                        height: 40
                        width: tabText.implicitWidth + 36
                        hoverEnabled: true
                        focusPolicy: Qt.TabFocus
                        onClicked: { window.settingsTab = index; flick.contentY = 0 }
                        background: Surface {
                            radius: Math.min(20, Theme.radius + 10)
                            hovered: tab.hovered
                            pressed: tab.pressed
                            active: tab.on
                            primary: tab.on && Theme.style.controls !== "neu"
                            focused: tab.visualFocus
                            flatColor: tab.on ? Theme.accent : (tab.hovered ? Theme.alpha(Theme.text, 0.1) : Theme.alpha(Theme.text, 0.05))
                        }
                        contentItem: Text {
                            id: tabText
                            text: modelData
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.family: Theme.uiFont
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: tab.on ? (Theme.style.controls === "neu" ? Theme.accent : Theme.onAccent) : Theme.text
                        }
                    }
                }
            }

            // =============== Allgemein ===============
            Column {
                visible: window.settingsTab === 0
                width: col.width
                spacing: 34

                Section {
                    title: qsTr("Bibliothek")
                    text: qsTr("Diese Ordner und alle Unterordner werden gelesen. Nur geänderte Dateien werden neu eingelesen, nicht erreichbare Laufwerke werden nie aus der Bibliothek gelöscht.")

                    Repeater {
                        model: Library.roots
                        Rectangle {
                            width: col.width
                            height: 52
                            radius: Theme.radius
                            color: Theme.alpha(Theme.surface, 0.85)
                            border.color: Theme.alpha(Theme.border, 0.7)
                            Icon { x: 16; anchors.verticalCenter: parent.verticalCenter; name: "folder"; size: 20; color: Theme.accent }
                            Text {
                                x: 48
                                width: parent.width - 110
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData
                                elide: Text.ElideMiddle
                                font.family: Theme.uiFont
                                font.pixelSize: 14
                                color: Theme.text
                            }
                            IconButton {
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                iconName: "delete"
                                iconSize: 18
                                tip: qsTr("Aus der Bibliothek entfernen")
                                onClicked: Library.removeRoot(modelData)
                            }
                        }
                    }

                    Row {
                        spacing: 10
                        PillButton {
                            primary: Library.roots.length === 0
                            text: qsTr("Ordner hinzufügen")
                            iconName: "add"
                            // the desktop's own folder dialog; Qt's one only where there is none
                            onClicked: SystemDialogs.chooseFolder(folderDialog.title, "", function (path) {
                                if (path === null) folderDialog.open()
                                else if (path !== "") Library.addRoot(path)
                            })
                        }
                        PillButton {
                            readonly property bool fullScan: Library.scanner.running && !Library.scanner.background
                            text: fullScan ? qsTr("Scan abbrechen") : qsTr("Jetzt scannen")
                            iconName: "refresh"
                            enabled: Library.roots.length > 0
                            onClicked: fullScan ? Library.scanner.cancel() : Library.rescan()
                        }
                    }

                    // Path entry for systems where the folder dialog is not available.
                    Row {
                        spacing: 8
                        Rectangle {
                            width: Math.min(460, col.width - 150)
                            height: 38
                            radius: Math.min(19, Theme.radius + 9)
                            color: Theme.alpha(Theme.text, 0.06)
                            border.color: pathField.activeFocus ? Theme.accent : "transparent"
                            TextField {
                                id: pathField
                                anchors.fill: parent
                                leftPadding: 16
                                placeholderText: qsTr("oder Pfad eingeben, z. B. /mnt/musik")
                                placeholderTextColor: Theme.textFaint
                                color: Theme.text
                                font.family: Theme.uiFont
                                font.pixelSize: 14
                                background: null
                                selectByMouse: true
                                onAccepted: { Library.addRoot(text); text = "" }
                            }
                        }
                        PillButton {
                            compact: true
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Hinzufügen")
                            enabled: pathField.text.length > 0
                            onClicked: { Library.addRoot(pathField.text); pathField.text = "" }
                        }
                    }

                    Text {
                        visible: text !== ""
                        width: col.width
                        wrapMode: Text.Wrap
                        text: Library.scanner.running ? "" : Library.scanner.summary
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        color: Theme.textDim
                    }

                    HToggle {
                        width: col.width
                        label: qsTr("Ordner überwachen: neue und geänderte Musik sofort aufnehmen")
                        checked: Library.watchFolders
                        onToggled: Library.watchFolders = checked
                    }
                    Text {
                        visible: text !== ""
                        width: col.width
                        wrapMode: Text.Wrap
                        text: Library.watchStatus
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        color: Theme.textDim
                    }
                    Column {
                        visible: Library.watchHint !== ""
                        width: col.width
                        spacing: 6
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: qsTr("Damit alle Ordner überwacht werden, einmal im Terminal ausführen. Der Player merkt es bei der nächsten Prüfung von selbst:")
                            font.family: Theme.uiFont
                            font.pixelSize: 13
                            color: Theme.textDim
                        }
                        Rectangle {
                            width: parent.width
                            height: hintCmd.implicitHeight + 16
                            radius: Math.min(10, Theme.radius)
                            color: Theme.alpha(Theme.text, 0.06)
                            TextEdit {
                                id: hintCmd
                                x: 12
                                y: 8
                                width: parent.width - 24
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.WrapAnywhere
                                text: Library.watchHint
                                font.family: "monospace"
                                font.pixelSize: 12
                                color: Theme.text
                                selectionColor: Theme.alpha(Theme.accent, 0.45)
                            }
                        }
                    }
                    HToggle {
                        width: col.width
                        label: qsTr("Beim Start nach Änderungen suchen")
                        checked: Library.scanOnStart
                        onToggled: Library.scanOnStart = checked
                    }
                    Option {
                        label: qsTr("Lese-Threads beim Scan")
                        hint: qsTr("Automatisch nimmt 2 Threads für Festplatten (HDD) und mehr für SSDs.")
                        HSegmented {
                            width: col.width
                            options: [qsTr("Automatisch"), "1", "2", "4", "8"]
                            values: [0, 1, 2, 4, 8]
                            current: Library.scanThreads
                            onPicked: (v) => Library.scanThreads = v
                        }
                    }
                }

                Section {
                    title: qsTr("Leistung")
                    text: qsTr("Für ältere Rechner: Effekte lassen sich hier und in den Theme-Einstellungen zurückfahren.")
                    HToggle {
                        width: col.width
                        label: qsTr("Live-Spektrum und Musik-Analyse")
                        checked: window.visualizer
                        enabled: Player.spectrumAvailable
                        onToggled: window.visualizer = checked
                    }
                    HToggle {
                        width: col.width
                        label: qsTr("Sparmodus: Effekte mit höchstens 30 Bildern/s, Musik-Analyse halb so oft")
                        checked: window.lowPower
                        onToggled: window.lowPower = checked
                    }
                    Text {
                        width: col.width
                        wrapMode: Text.Wrap
                        // live while this page is open
                        Binding { target: Memory; property: "watching"; value: page.visible }
                        text: qsTr("Arbeitsspeicher gerade: %1 MB, davon %2 MB eigene Daten (Rest: geteilte Bibliotheken und Grafiktreiber).")
                              .arg(Memory.residentMb).arg(Memory.privateMb)
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        color: Theme.textDim
                    }
                    HToggle {
                        width: col.width
                        label: qsTr("Wellenform zum Spulen anzeigen (entsteht bei der Audioanalyse)")
                        checked: window.waveform
                        onToggled: window.waveform = checked
                    }
                    HToggle {
                        width: col.width
                        label: qsTr("Animationen anhalten, wenn das Fenster im Hintergrund ist")
                        checked: window.pauseInactive
                        onToggled: window.pauseInactive = checked
                    }
                }

                Section {
                    title: qsTr("Liedtexte")
                    text: qsTr("Die Großansicht zeigt den Liedtext statt des Covers, synchron zur Musik, wenn er Zeitmarken hat (ein Klick auf eine Zeile springt dorthin). Gesucht wird in einer .lrc-Datei mit dem Namen des Titels daneben, in den Tags des Titels und beim Jellyfin-Server.")
                    HToggle {
                        width: col.width
                        label: qsTr("Liedtext in der Großansicht zeigen")
                        checked: Lyrics.shown
                        onToggled: Lyrics.shown = checked
                    }
                    HToggle {
                        width: col.width
                        label: qsTr("Fehlende Liedtexte aus dem Netz laden (LRCLIB)")
                        checked: Lyrics.online
                        onToggled: Lyrics.online = checked
                    }
                    Text {
                        width: col.width
                        wrapMode: Text.Wrap
                        text: qsTr("Fragt lrclib.net nach Liedtexten für Titel ohne eigenen synchronen Text – nur, wenn die Großansicht offen ist und Liedtexte zeigen soll. Dabei gehen Titel, Künstler, Album und Länge an den Dienst. Gefundenes bleibt auf diesem Rechner gespeichert, neben die Musik wird nichts geschrieben.")
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        color: Theme.textDim
                    }
                }

                Section {
                    title: qsTr("Internetradio und Audio-CD")
                    text: (AudioCd.available ? qsTr("Beides findest du in der Seitenleiste unter „Hören“.")
                                             : qsTr("Das Internetradio findest du in der Seitenleiste unter „Hören“, die Audio-CD dort, wenn der Rechner ein CD-Laufwerk hat und GStreamer CDs lesen kann (cdparanoia aus gst-plugins-base)."))
                          + " " + qsTr("Ins Netz geht der Player dafür nur, wenn du es hier oder auf der Seite einschaltest.")
                    HToggle {
                        width: col.width
                        label: qsTr("Senderverzeichnis radio-browser.info verwenden (Suche, Senderlogos)")
                        checked: Stations.online
                        onToggled: Stations.online = checked
                    }
                    HToggle {
                        width: col.width
                        label: qsTr("Namen und Cover von CDs bei MusicBrainz nachschlagen")
                        checked: AudioCd.online
                        onToggled: AudioCd.online = checked
                    }
                    Text {
                        width: col.width
                        wrapMode: Text.Wrap
                        text: qsTr("Eingelesene CDs kommen als FLAC nach: %1").arg(AudioCd.folder)
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        color: Theme.textDim
                    }
                    CdFolderButton { compact: true }
                }

                Section {
                    title: qsTr("Infobereich und Benachrichtigungen")
                    text: qsTr("Das Symbol in der Leiste zeigt, was läuft. Ein Klick holt das Fenster hervor oder versteckt es, die mittlere Maustaste hält an, das Mausrad ändert die Lautstärke, ein Rechtsklick öffnet das Menü.")
                    HToggle {
                        width: col.width
                        label: qsTr("Symbol im Infobereich zeigen")
                        checked: Tray.enabled
                        onToggled: Tray.enabled = checked
                    }
                    HToggle {
                        width: col.width
                        label: qsTr("Beim Schließen des Fensters im Infobereich weiterspielen")
                        checked: Tray.closeToTray
                        enabled: Tray.enabled
                        onToggled: Tray.closeToTray = checked
                    }
                    Text {
                        visible: Tray.enabled && !Tray.available
                        width: col.width
                        wrapMode: Text.Wrap
                        text: qsTr("Diese Arbeitsumgebung zeigt gerade keine Symbole im Infobereich (unter GNOME braucht es dafür die Erweiterung »AppIndicator«). Das Fenster schließt dann wie gewohnt.")
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        color: Theme.textDim
                    }
                    HToggle {
                        width: col.width
                        label: qsTr("Benachrichtigung bei jedem neuen Titel, wenn das Fenster im Hintergrund ist")
                        checked: Tray.notify
                        onToggled: Tray.notify = checked
                    }
                }

                Section {
                    title: qsTr("Eigene Ansichten")
                    text: qsTr("Ansichten sind gespeicherte Filter, z. B. »Hi-Res Jazz der 70er«. Lege sie in views.json an; ein Beispiel liegt daneben (views.example.json).")
                    Text {
                        width: col.width
                        text: Library.viewsFile
                        elide: Text.ElideMiddle
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        color: Theme.textDim
                    }
                    Text {
                        visible: Library.viewsError !== ""
                        width: col.width
                        wrapMode: Text.Wrap
                        text: Library.viewsError
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        color: "#ff8a80"
                    }
                    Row {
                        spacing: 10
                        PillButton { compact: true; text: qsTr("Ordner öffnen"); iconName: "folder"; onClicked: Library.openViewsFolder() }
                        PillButton { compact: true; text: qsTr("Neu laden"); iconName: "refresh"; onClicked: Library.reloadViews() }
                    }
                }

                Section {
                    title: qsTr("Tastenkürzel")
                    Grid {
                        columns: 2
                        columnSpacing: 28
                        rowSpacing: 8
                        Repeater {
                            model: [
                                [qsTr("Leertaste"), qsTr("Abspielen / Pause")],
                                [qsTr("Strg + →  /  Strg + ←"), qsTr("Nächster / vorheriger Titel")],
                                [qsTr("Strg + ↑  /  Strg + ↓"), qsTr("Lauter / leiser")],
                                [qsTr("Strg + F"), qsTr("Suchen")],
                                [qsTr("Strg + J"), qsTr("Warteschlange")],
                                [qsTr("Strg + L"), qsTr("Großansicht")],
                                [qsTr("Strg + Umschalt + L"), qsTr("Liedtext oder Cover in der Großansicht")],
                                [qsTr("Strg + H"), qsTr("Herz für den laufenden Titel an oder aus")],
                                [qsTr("F11"), qsTr("Vollbild an und aus")],
                                [qsTr("Strg + T"), qsTr("Theme wechseln")],
                                [qsTr("Strg + E"), qsTr("Klang: Ausgabe und Equalizer")],
                                [qsTr("Alt + ←"), qsTr("Zurück")],
                                [qsTr("Esc"), qsTr("Großansicht schließen")],
                                [qsTr("Strg + Q"), qsTr("Beenden")]
                            ]
                            delegate: Row {
                                spacing: 0
                                Text {
                                    width: 210
                                    text: modelData[0]
                                    font.family: Theme.uiFont
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    color: Theme.text
                                }
                                Text {
                                    text: modelData[1]
                                    font.family: Theme.uiFont
                                    font.pixelSize: 14
                                    color: Theme.textDim
                                }
                            }
                        }
                    }
                }
            }

            // =============== Theme ===============
            Column {
                visible: window.settingsTab === 1
                width: col.width
                spacing: 34

                Section {
                    title: qsTr("Theme")
                    text: qsTr("Jedes Theme ist ein eigener Player-Aufbau mit eigenen Effekten. Inhalte und Funktionen sind überall dieselben, die Einstellungen merkt sich jedes Theme für sich.")

                    Grid {
                        id: skinGrid
                        width: col.width
                        columns: col.width > 640 ? 2 : 1
                        spacing: 16
                        Repeater {
                            model: Theme.skins
                            SkinCard {
                                width: (skinGrid.width - (skinGrid.columns - 1) * skinGrid.spacing) / skinGrid.columns
                                skin: modelData
                            }
                        }
                    }
                }

                Section {
                    title: qsTr("Einstellungen für %1").arg(Theme.skinName)
                    text: qsTr("Änderungen wirken sofort.")

                    Repeater {
                        model: Theme.schema
                        delegate: Loader {
                            width: col.width
                            property var s: modelData
                            sourceComponent: {
                                switch (s.type) {
                                case "palette": return paletteSetting
                                case "bool": return boolSetting
                                case "range": return rangeSetting
                                case "choice": return choiceSetting
                                }
                                return null
                            }
                        }
                    }
                    PillButton {
                        compact: true
                        text: qsTr("Auf Standard zurücksetzen")
                        iconName: "refresh"
                        onClicked: Theme.resetOptions()
                    }
                }
            }

            // =============== Mix ===============
            Loader {
                active: window.settingsTab === 2
                visible: active
                width: col.width
                sourceComponent: MixSettings { width: col.width }
            }

            // =============== Klang ===============
            Loader {
                active: window.settingsTab === 3
                visible: active
                width: col.width
                sourceComponent: SoundSettings { width: col.width }
            }

            // =============== Jellyfin ===============
            Loader {
                active: window.settingsTab === 4
                visible: active
                width: col.width
                sourceComponent: JellyfinSettings { width: col.width }
            }

            // =============== Über ===============
            Column {
                visible: window.settingsTab === 5
                width: col.width
                spacing: 24

                Brand {
                    nameSize: 44
                    signatureSize: 15
                    maxWidth: col.width
                    oneLine: true
                }
                Text {
                    width: col.width
                    wrapMode: Text.Wrap
                    textFormat: Text.StyledText
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    lineHeight: 1.35
                    color: Theme.textDim
                    linkColor: Theme.accent
                    onLinkActivated: (l) => Qt.openUrlExternally(l)
                    text: qsTr("Version %1, Projekt „Halcyon“.").arg(Qt.application.version)
                          + "<br>" + qsTr("Musikplayer für große lokale Bibliotheken. Wiedergabe mit GStreamer, Tags mit TagLib, Bibliothek in SQLite, Oberfläche mit Qt.")
                          + "<br><br>" + qsTr("Freie Software unter der GNU General Public License, Version 3 oder später. Du darfst das Programm nutzen, weitergeben und verändern; es gibt keine Gewährleistung.")
                          + " " + qsTr("Quellcode: %1").arg("<a href=\"https://github.com/1100101-Ich-Liebe-Dich/bub3player\">github.com/1100101-Ich-Liebe-Dich/bub3player</a>")
                          + "<br><br>" + qsTr("Schriften: Manrope, Bricolage Grotesque, Unbounded, Orbitron, Chakra Petch, Barlow, Barlow Condensed, VT323, Nunito, Righteous, IBM Plex Mono, DSEG7 und DSEG14 (SIL Open Font License 1.1); Permanent Marker (Apache 2.0). Symbole: Material Icons (Apache 2.0).")
                          + "<br>" + qsTr("Verwendet Qt (LGPL 3), GStreamer (LGPL 2.1), TagLib (LGPL 2.1 oder MPL 1.1) und SQLite (gemeinfrei).")
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Musikordner wählen")
        onAccepted: Library.addRoot(selectedFolder)
    }

    // ---------- generated theme settings ----------
    Component {
        id: paletteSetting
        Option {
            label: s.label
            Flow {
                width: col.width
                spacing: 12
                Repeater {
                    model: s.options
                    PaletteCard { pal: modelData; current: Theme.opt[s.key] === modelData.id; onPicked: Theme.setOption(s.key, modelData.id) }
                }
            }
        }
    }
    Component {
        id: boolSetting
        HToggle {
            label: s.label
            checked: Theme.opt[s.key] === true
            onToggled: Theme.setOption(s.key, checked)
        }
    }
    Component {
        id: rangeSetting
        Option {
            label: s.label
            value: {
                var v = Theme.opt[s.key] !== undefined ? Theme.opt[s.key] : s["default"]
                if (s.percent) return Math.round(v * 100) + " %"
                return (s.step < 1 ? Number(v).toFixed(1) : Math.round(v)) + (s.unit ? " " + s.unit : "")
            }
            HSlider {
                width: Math.min(420, col.width)
                from: s.min; to: s.max; stepSize: s.step
                value: Theme.opt[s.key] !== undefined ? Theme.opt[s.key] : s["default"]
                onMoved: Theme.setOption(s.key, value)
            }
        }
    }
    Component {
        id: choiceSetting
        Option {
            label: s.label
            HSegmented {
                width: col.width
                options: s.options
                current: Theme.opt[s.key]
                onPicked: (v) => Theme.setOption(s.key, v)
            }
        }
    }

    component Section: SettingsSection { width: page.colWidth }
    component Option: SettingsOption { width: page.colWidth }

    component SkinCard: AbstractButton {
        id: card
        property var skin
        readonly property bool current: Theme.skin === skin.id
        height: preview.height + 96
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        onClicked: if (!current) Theme.setSkin(skin.id)
        background: Rectangle {
            radius: Theme.radius + 4
            color: card.hovered ? Theme.alpha(Theme.text, 0.08) : Theme.alpha(Theme.text, 0.04)
            border.width: card.current ? 2 : 1
            border.color: card.current ? Theme.accent : Theme.alpha(Theme.text, card.hovered || card.visualFocus ? 0.4 : 0.12)
        }
        contentItem: Item {
            Rectangle {
                id: preview
                x: 10; y: 10
                width: card.width - 20
                height: Math.round(width * 0.56)
                radius: Theme.radius
                clip: true
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: card.skin.swatch.length > 0 ? card.skin.swatch[0] : Theme.surface }
                    GradientStop { position: 0.6; color: card.skin.swatch.length > 1 ? card.skin.swatch[1] : Theme.surface }
                    GradientStop { position: 1; color: card.skin.swatch.length > 2 ? card.skin.swatch[2] : Theme.accent }
                }
                Image {
                    anchors.fill: parent
                    source: card.skin.preview
                    sourceSize.width: 720
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: source !== ""
                }
                Rectangle {
                    visible: card.current
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    height: 26
                    width: activeText.implicitWidth + 20
                    radius: 13
                    color: Theme.accent
                    Text {
                        id: activeText
                        anchors.centerIn: parent
                        text: qsTr("Aktiv")
                        font.family: Theme.uiFont
                        font.pixelSize: 12
                        font.weight: Font.Bold
                        color: Theme.onAccent
                    }
                }
            }
            Text {
                x: 18
                anchors.top: preview.bottom
                anchors.topMargin: 12
                text: card.skin.name
                font.family: card.skin.font || Theme.displayFont
                font.pixelSize: 22
                font.weight: Font.Bold
                color: Theme.text
            }
            Text {
                x: 18
                width: card.width - 36
                anchors.top: preview.bottom
                anchors.topMargin: 44
                text: card.skin.tagline
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: 13
                color: Theme.textDim
            }
        }
    }

    component PaletteCard: AbstractButton {
        id: pc
        property var pal
        property bool current: false
        signal picked()
        width: 180
        height: 118
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        onClicked: pc.picked()
        background: Rectangle {
            radius: 12
            color: pc.pal.bg
            border.width: pc.current ? 2 : 1
            border.color: pc.current ? Theme.accent : (pc.hovered || pc.visualFocus ? Theme.alpha(Theme.text, 0.45) : Theme.alpha(Theme.text, 0.15))
        }
        contentItem: Item {
            // Miniature of the layout in the palette's own colours.
            Rectangle { x: 12; y: 12; width: 38; height: 64; radius: 5; color: pc.pal.surface }
            Rectangle { x: 58; y: 14; width: 66; height: 8; radius: 4; color: pc.pal.text }
            Rectangle { x: 58; y: 30; width: 96; height: 5; radius: 2.5; color: pc.pal.text; opacity: 0.35 }
            Rectangle { x: 58; y: 42; width: 82; height: 5; radius: 2.5; color: pc.pal.text; opacity: 0.35 }
            Rectangle { x: 58; y: 56; width: 20; height: 20; radius: 10; color: pc.pal.accent }
            Rectangle { x: 82; y: 56; width: 20; height: 20; radius: 10; color: pc.pal.accent2 || pc.pal.accent; visible: pc.pal.accent2 !== pc.pal.accent }
            Text {
                x: 12
                y: 88
                text: pc.pal.name
                font.family: Theme.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: pc.pal.text
            }
        }
    }
}
