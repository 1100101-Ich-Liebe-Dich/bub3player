// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// The audio CD in the drive: play it (Next/Previous go through its tracks), give
// it names (by hand or, only when switched on, from MusicBrainz) and read it into
// FLAC files. Reading never changes a file that is already there.
Item {
    id: page
    property string pageId: "cd"
    readonly property int inset: 36
    readonly property bool ready: AudioCd.status === 3

    Component.onCompleted: AudioCd.watching = true
    Component.onDestruction: AudioCd.watching = false

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
                title: page.ready ? (AudioCd.album || qsTr("Audio-CD")) : qsTr("Audio-CD")
                subtitle: page.ready ? AudioCd.artist : ""
                detail: page.ready
                        ? [AudioCd.year > 0 ? String(AudioCd.year) : "",
                           AudioCd.tracks.length === 1 ? qsTr("1 Titel") : qsTr("%1 Titel").arg(AudioCd.tracks.length),
                           Library.formatDuration(page.totalMs(), true)].filter(function (s) { return s !== "" }).join(" · ")
                        : ""
                coverId: page.ready ? AudioCd.coverItem : 0
                coverVersion: AudioCd.coverVersion
                loading: AudioCd.status === 2 || AudioCd.ripping
                PillButton {
                    visible: page.ready
                    text: qsTr("Abspielen")
                    iconName: "play"
                    primary: true
                    onClicked: AudioCd.play(0)
                }
                PillButton {
                    visible: page.ready
                    enabled: AudioCd.canRip && !AudioCd.ripping
                    text: qsTr("Einlesen")
                    iconName: "download"
                    onClicked: ripDlg.open()
                }
                PillButton {
                    visible: page.ready
                    enabled: !AudioCd.ripping
                    text: qsTr("Namen bearbeiten")
                    iconName: "edit"
                    onClicked: namesDlg.open()
                }
                PillButton {
                    visible: AudioCd.status >= 3
                    enabled: !AudioCd.ripping
                    text: qsTr("Auswerfen")
                    iconName: "eject"
                    onClicked: AudioCd.eject()
                }
            }

            EmptyState {
                visible: !page.ready
                anchors.horizontalCenter: parent.horizontalCenter
                iconName: "disc"
                title: AudioCd.status === 0 ? qsTr("Kein CD-Laufwerk")
                     : AudioCd.status === 2 ? qsTr("Die CD wird gelesen …")
                     : AudioCd.status === 4 ? qsTr("Das ist keine Audio-CD")
                     : qsTr("Keine CD im Laufwerk")
                text: AudioCd.status === 0 ? qsTr("Der Player hat kein CD-Laufwerk gefunden (oder GStreamer kann keine CDs lesen: gst-plugins-base mit cdparanoia).")
                    : AudioCd.status === 2 ? qsTr("Das dauert einen Moment, bis das Laufwerk angelaufen ist.")
                    : AudioCd.status === 4 ? qsTr("Auf dieser Scheibe sind keine Musiktitel.")
                    : qsTr("Lege eine Audio-CD ein; sie erscheint hier von selbst.")
                actionText: AudioCd.status === 1 ? qsTr("Erneut prüfen") : ""
                actionIcon: "refresh"
                onAction: AudioCd.refresh()
            }

            // ---- the tracks ----
            Column {
                visible: page.ready
                x: page.inset - 12
                width: Math.min(col.width - 2 * page.inset + 24, 960)
                Repeater {
                    model: page.ready ? AudioCd.tracks : []
                    delegate: Item {
                        id: row
                        readonly property bool current: Player.currentId === modelData.item
                        width: parent.width
                        height: 46
                        Surface {
                            anchors.fill: parent
                            anchors.topMargin: Theme.style.controls === "neu" ? 4 : 0
                            anchors.bottomMargin: Theme.style.controls === "neu" ? 4 : 0
                            radius: Theme.radius - 2
                            quiet: true
                            hovered: rowHover.containsMouse && Theme.style.controls !== "neu"
                            active: row.current
                            flatColor: row.current ? Theme.alpha(Theme.accent, 0.09)
                                     : rowHover.containsMouse ? Theme.alpha(Theme.text, 0.045) : "transparent"
                        }
                        Text {
                            id: num
                            x: 12
                            width: 34
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.current && Player.playing ? "♪" : modelData.number
                            horizontalAlignment: Text.AlignRight
                            font.family: Theme.monoFont
                            font.pixelSize: 13
                            color: row.current ? Theme.accent : Theme.textDim
                        }
                        Column {
                            anchors.left: num.right
                            anchors.leftMargin: 18
                            anchors.right: len.left
                            anchors.rightMargin: 18
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1
                            Text {
                                width: parent.width
                                text: modelData.title
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: row.current ? Theme.accent : Theme.text
                            }
                            Text {
                                width: parent.width
                                visible: modelData.artist !== "" && modelData.artist !== AudioCd.artist
                                text: modelData.artist
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: 12
                                color: Theme.textDim
                            }
                        }
                        Text {
                            id: len
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            text: window.fmtTime(modelData.duration)
                            font.family: Theme.monoFont
                            font.pixelSize: 13
                            color: Theme.textDim
                        }
                        MouseArea {
                            id: rowHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: row.current ? Player.toggle() : AudioCd.play(index)
                        }
                    }
                }
            }

            // ---- reading into files ----
            Column {
                visible: page.ready && (AudioCd.ripping || AudioCd.ripText !== "")
                x: page.inset
                width: Math.min(col.width - 2 * page.inset, 960)
                spacing: 10
                Text {
                    text: qsTr("Einlesen")
                    font.family: Theme.displayFont
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    font.capitalization: Theme.style.titleUpper ? Font.AllUppercase : Font.MixedCase
                    color: Theme.text
                }
                Rectangle {
                    visible: AudioCd.ripping
                    width: parent.width
                    height: 6
                    radius: 3
                    color: Theme.alpha(Theme.text, 0.1)
                    Rectangle {
                        width: parent.width * AudioCd.ripProgress
                        height: parent.height
                        radius: 3
                        color: Theme.accent
                        Behavior on width { NumberAnimation { duration: 300 } }
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: AudioCd.ripText
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: Theme.textDim
                }
                PillButton {
                    visible: AudioCd.ripping
                    compact: true
                    text: qsTr("Abbrechen")
                    iconName: "close"
                    onClicked: AudioCd.cancelRip()
                }
                PillButton {
                    // read into a folder outside the library: offer to take it in
                    visible: !AudioCd.ripping && !Library.roots.some(function (r) {
                        return AudioCd.folder === r || AudioCd.folder.indexOf(r.replace(/\/$/, "") + "/") === 0
                    })
                    compact: true
                    text: qsTr("Ordner in die Bibliothek aufnehmen")
                    iconName: "add"
                    onClicked: Library.addRoot(AudioCd.folder)
                }
            }

            // ---- names from MusicBrainz ----
            Column {
                visible: page.ready && !AudioCd.online
                x: page.inset
                width: Math.min(col.width - 2 * page.inset, 720)
                spacing: 10
                Text {
                    text: qsTr("Namen aus dem Netz")
                    font.family: Theme.displayFont
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    font.capitalization: Theme.style.titleUpper ? Font.AllUppercase : Font.MixedCase
                    color: Theme.text
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("Die freie Musikdatenbank MusicBrainz kennt Album, Künstler und Titel der meisten CDs, das Cover kommt aus dem Cover Art Archive. Eingeschaltet geht dafür die Kennung der CD (aus der Länge ihrer Titel berechnet) an musicbrainz.org.")
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    lineHeight: 1.2
                    color: Theme.textDim
                }
                PillButton {
                    text: qsTr("MusicBrainz einschalten")
                    iconName: "search"
                    onClicked: AudioCd.online = true
                }
            }
        }
    }

    function totalMs() {
        var t = 0
        for (var i = 0; i < AudioCd.tracks.length; ++i) t += AudioCd.tracks[i].duration
        return t
    }

    HDialog {
        id: ripDlg
        title: qsTr("CD einlesen")
        text: qsTr("Die Titel werden als FLAC (verlustfrei) gespeichert, in „%1“ unter Albumkünstler/Album. Dateien, die es dort schon gibt, bleiben, wie sie sind. Während des Einlesens kann die CD nicht spielen.")
              .arg(AudioCd.folder)
        acceptText: qsTr("Einlesen")
        acceptIcon: "download"
        extraButtons: CdFolderButton {}
        onAccepted: AudioCd.rip()
    }

    HDialog {
        id: namesDlg
        title: qsTr("Namen bearbeiten")
        text: qsTr("So heißen die Titel beim Abspielen und in den eingelesenen Dateien. Der Player merkt sich die Namen für diese CD.")
        acceptText: qsTr("Übernehmen")
        dialogWidth: 560
        NameField { id: albumField; label: qsTr("Album") }
        NameField { id: artistField; label: qsTr("Albumkünstler") }
        NameField { id: yearField; label: qsTr("Jahr"); fieldWidth: 100 }
        Repeater {
            id: titleFields
            model: namesDlg.opened ? AudioCd.tracks : []
            delegate: NameField {
                label: qsTr("Titel %1").arg(modelData.number)
                Component.onCompleted: text = modelData.title
            }
        }
        onAboutToShow: {
            albumField.text = AudioCd.album
            artistField.text = AudioCd.artist
            yearField.text = AudioCd.year > 0 ? String(AudioCd.year) : ""
        }
        onOpened: albumField.field.input.forceActiveFocus()
        onAccepted: {
            var titles = []
            for (var i = 0; i < titleFields.count; ++i) titles.push(titleFields.itemAt(i).text)
            AudioCd.setNames(albumField.text, artistField.text, parseInt(yearField.text) || 0, titles)
        }
    }

    // one line of the names dialog: what it is, then the field
    component NameField: Item {
        property string label
        property alias text: f.text
        property alias field: f
        property int fieldWidth: width - 120
        width: namesDlg.width - 56
        height: f.height
        Text {
            width: 110
            anchors.verticalCenter: parent.verticalCenter
            text: parent.label
            elide: Text.ElideRight
            font.family: Theme.uiFont
            font.pixelSize: 13
            color: Theme.textDim
        }
        HTextField {
            id: f
            x: 120
            width: parent.fieldWidth
            onAccepted: namesDlg.accept()
        }
    }
}
