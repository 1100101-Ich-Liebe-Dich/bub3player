import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Halcyon

Item {
    id: page
    property string pageId: "settings"

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: col.height + 60
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HScrollBar {}

        Column {
            id: col
            x: 36
            width: Math.min(860, flick.width - 72)
            spacing: 34

            PageHeader {
                x: -36
                width: col.width + 72
                implicitHeight: 120
                title: qsTr("Einstellungen")
            }

            // ---------------- Library ----------------
            Section {
                title: qsTr("Bibliothek")
                text: qsTr("Halcyon liest diese Ordner und alle Unterordner. Nur geänderte Dateien werden neu eingelesen, nicht erreichbare Laufwerke werden nie aus der Bibliothek gelöscht.")

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
                        onClicked: folderDialog.open()
                    }
                    PillButton {
                        text: Library.scanner.running ? qsTr("Scan abbrechen") : qsTr("Jetzt scannen")
                        iconName: "refresh"
                        enabled: Library.roots.length > 0
                        onClicked: Library.scanner.running ? Library.scanner.cancel() : Library.rescan()
                    }
                }

                // Path entry for systems where the folder dialog is not available.
                Row {
                    spacing: 8
                    Rectangle {
                        width: Math.min(460, col.width - 150)
                        height: 38
                        radius: 19
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

                Toggle {
                    label: qsTr("Beim Start nach Änderungen suchen")
                    checked: Library.scanOnStart
                    onToggled: Library.scanOnStart = checked
                }
                Row {
                    spacing: 12
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Lese-Threads beim Scan")
                        font.family: Theme.uiFont
                        font.pixelSize: 14
                        color: Theme.text
                    }
                    Segmented {
                        anchors.verticalCenter: parent.verticalCenter
                        options: [qsTr("Automatisch"), "1", "2", "4", "8"]
                        values: [0, 1, 2, 4, 8]
                        current: Library.scanThreads
                        onPicked: (v) => Library.scanThreads = v
                    }
                }
                Text {
                    width: col.width
                    wrapMode: Text.Wrap
                    text: qsTr("Automatisch nimmt 2 Threads für Festplatten (HDD) und mehr für SSDs.")
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: Theme.textFaint
                }
            }

            // ---------------- Appearance ----------------
            Section {
                title: qsTr("Darstellung")
                text: qsTr("Eigene Themes: JSON-Datei in %1 ablegen (Vorlage: eines der mitgelieferten Themes).").arg(Theme.userDir)

                Flow {
                    width: col.width
                    spacing: 14
                    Repeater {
                        model: Theme.themes
                        ThemeCard { theme: modelData }
                    }
                }
                Toggle {
                    label: qsTr("Akzentfarbe aus dem Cover übernehmen")
                    checked: Theme.dynamicAccent
                    onToggled: Theme.dynamicAccent = checked
                }
                Toggle {
                    label: qsTr("Farbiges Leuchten aus dem Cover im Hintergrund")
                    checked: Theme.ambientEnabled
                    onToggled: Theme.ambientEnabled = checked
                }
                Toggle {
                    label: qsTr("Live-Spektrum (abschalten spart auf alter Hardware Leistung)")
                    checked: window.visualizer
                    enabled: Player.spectrumAvailable
                    onToggled: window.visualizer = checked
                }
            }

            // ---------------- Playback ----------------
            Section {
                title: qsTr("Wiedergabe")
                text: Player.replayGainAvailable
                      ? qsTr("ReplayGain gleicht die Lautstärke zwischen Titeln an. Ein Limiter verhindert Übersteuerung.")
                      : qsTr("ReplayGain ist nicht verfügbar: Das GStreamer-Plugin rgvolume fehlt (gst-plugins-good).")
                Row {
                    spacing: 12
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("ReplayGain")
                        font.family: Theme.uiFont
                        font.pixelSize: 14
                        color: Theme.text
                    }
                    Segmented {
                        anchors.verticalCenter: parent.verticalCenter
                        enabled: Player.replayGainAvailable
                        options: [qsTr("Aus"), qsTr("Titel"), qsTr("Album")]
                        values: [0, 1, 2]
                        current: Player.replayGain
                        onPicked: (v) => Player.replayGain = v
                    }
                }
                Row {
                    spacing: 12
                    enabled: Player.replayGainAvailable && Player.replayGain > 0
                    opacity: enabled ? 1 : 0.5
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 150
                        text: qsTr("Vorverstärkung: %1 dB").arg(Player.preamp.toFixed(1))
                        font.family: Theme.uiFont
                        font.pixelSize: 14
                        color: Theme.text
                    }
                    Slider {
                        id: pre
                        width: 240
                        from: -10; to: 10; stepSize: 0.5
                        value: Player.preamp
                        onMoved: Player.preamp = value
                        background: Rectangle {
                            x: pre.leftPadding
                            y: pre.topPadding + pre.availableHeight / 2 - 2
                            width: pre.availableWidth
                            height: 4
                            radius: 2
                            color: Theme.alpha(Theme.text, 0.16)
                            Rectangle { width: pre.visualPosition * parent.width; height: 4; radius: 2; color: Theme.accent }
                        }
                        handle: Rectangle {
                            x: pre.leftPadding + pre.visualPosition * (pre.availableWidth - 14)
                            y: pre.topPadding + pre.availableHeight / 2 - 7
                            width: 14; height: 14; radius: 7
                            color: Theme.text
                        }
                    }
                }
            }

            // ---------------- Custom views ----------------
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

            // ---------------- Shortcuts ----------------
            Section {
                title: qsTr("Tastenkürzel")
                Grid {
                    columns: 2
                    columnSpacing: 28
                    rowSpacing: 8
                    Repeater {
                        model: [
                            ["Leertaste", qsTr("Abspielen / Pause")],
                            ["Strg + →  /  Strg + ←", qsTr("Nächster / vorheriger Titel")],
                            ["Strg + ↑  /  Strg + ↓", qsTr("Lauter / leiser")],
                            ["Strg + F", qsTr("Suchen")],
                            ["Strg + Q", qsTr("Beenden")],
                            ["Strg + J", qsTr("Warteschlange")],
                            ["Strg + L", qsTr("Großansicht")],
                            ["Esc", qsTr("Großansicht schließen")]
                        ]
                        Text {
                            text: modelData[index % 2]
                            font.family: Theme.uiFont
                            font.pixelSize: 14
                            font.weight: index % 2 === 0 ? Font.DemiBold : Font.Normal
                            color: index % 2 === 0 ? Theme.text : Theme.textDim
                        }
                    }
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Musikordner wählen")
        onAccepted: Library.addRoot(selectedFolder)
    }

    component Section: Column {
        id: sec
        property string title
        property string text
        default property alias content: body.data
        width: col.width
        spacing: 12
        Text {
            text: sec.title
            font.family: Theme.displayFont
            font.pixelSize: 24
            font.weight: Font.DemiBold
            color: Theme.text
        }
        Text {
            visible: sec.text !== ""
            width: col.width
            wrapMode: Text.Wrap
            text: sec.text
            font.family: Theme.uiFont
            font.pixelSize: 14
            lineHeight: 1.25
            color: Theme.textDim
        }
        Column {
            id: body
            width: col.width
            spacing: 12
            topPadding: 4
        }
    }

    component Toggle: AbstractButton {
        id: tg
        property string label
        checkable: true
        width: col.width
        height: 32
        hoverEnabled: true
        contentItem: Item {
            Rectangle {
                id: track
                width: 40; height: 22; radius: 11
                anchors.verticalCenter: parent.verticalCenter
                color: tg.checked ? Theme.accent : Theme.alpha(Theme.text, 0.18)
                border.width: tg.visualFocus ? 2 : 0
                border.color: Theme.text
                Behavior on color { ColorAnimation { duration: 140 } }
                Rectangle {
                    width: 16; height: 16; radius: 8
                    y: 3
                    x: tg.checked ? parent.width - width - 3 : 3
                    color: tg.checked ? Theme.onAccent : Theme.text
                    Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                }
            }
            Text {
                anchors.left: track.right
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: tg.label
                font.family: Theme.uiFont
                font.pixelSize: 14
                color: tg.enabled ? Theme.text : Theme.textFaint
            }
        }
        background: null
    }

    component Segmented: Row {
        id: seg
        property var options: []
        property var values: []
        property var current
        signal picked(var value)
        spacing: 0
        Repeater {
            model: seg.options
            AbstractButton {
                id: sb
                readonly property bool on: seg.values[index] === seg.current
                height: 32
                width: Math.max(52, segText.implicitWidth + 24)
                hoverEnabled: true
                onClicked: seg.picked(seg.values[index])
                background: Rectangle {
                    radius: 8
                    color: sb.on ? Theme.accent : (sb.hovered ? Theme.alpha(Theme.text, 0.1) : Theme.alpha(Theme.text, 0.05))
                    border.width: sb.visualFocus ? 2 : 0
                    border.color: Theme.text
                }
                contentItem: Text {
                    id: segText
                    text: modelData
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: sb.on ? Theme.onAccent : Theme.text
                }
            }
        }
    }

    component ThemeCard: AbstractButton {
        id: card
        property var theme
        readonly property bool current: Theme.id === theme.id
        width: 190
        height: 128
        hoverEnabled: true
        onClicked: Theme.setTheme(theme.id)
        background: Rectangle {
            radius: 12
            color: card.theme.bg
            border.width: card.current ? 2 : 1
            border.color: card.current ? Theme.accent : (card.hovered ? Theme.alpha(Theme.text, 0.4) : Theme.alpha(Theme.text, 0.15))
        }
        contentItem: Item {
            // Miniature of the layout in the theme's own colours.
            Rectangle { x: 12; y: 12; width: 40; height: 72; radius: 5; color: card.theme.surface }
            Rectangle { x: 60; y: 14; width: 70; height: 8; radius: 4; color: card.theme.text }
            Rectangle { x: 60; y: 30; width: 104; height: 5; radius: 2.5; color: card.theme.text; opacity: 0.35 }
            Rectangle { x: 60; y: 42; width: 90; height: 5; radius: 2.5; color: card.theme.text; opacity: 0.35 }
            Rectangle { x: 60; y: 62; width: 22; height: 22; radius: 11; color: card.theme.accent }
            Text {
                x: 12
                y: 98
                text: card.theme.name
                font.family: Theme.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: card.theme.text
            }
        }
    }
}
