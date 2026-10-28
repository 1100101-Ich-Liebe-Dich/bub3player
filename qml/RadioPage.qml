// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import Halcyon

// Internet radio: the user's stations as tiles (click plays; Next/Previous change
// the station), adding by address or from a .pls/.m3u, and – only when switched
// on – searching the free directory radio-browser.info.
Item {
    id: page
    property string pageId: "radio"
    readonly property int inset: 36
    readonly property int tile: 168
    property string lastFolder: ""

    function importList() {
        SystemDialogs.openFile(qsTr("Senderliste importieren"), lastFolder || Playlists.defaultFolder(),
                               [{ name: qsTr("Senderlisten"), patterns: ["*.pls", "*.m3u", "*.m3u8", "*.xspf"] },
                                { name: qsTr("Alle Dateien"), patterns: ["*"] }],
                               function (path) {
                                   if (path === null) importDlg.open()
                                   else if (path !== "") {
                                       page.lastFolder = path.substring(0, path.lastIndexOf("/"))
                                       Stations.importFile(path)
                                   }
                               })
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
            spacing: 26

            PageHeader {
                width: parent.width
                title: qsTr("Internetradio")
                subtitle: Stations.list.length === 1 ? qsTr("1 Sender") : qsTr("%1 Sender").arg(Stations.list.length)
                loading: Stations.busy
                PillButton {
                    text: qsTr("Sender hinzufügen")
                    iconName: "add"
                    primary: true
                    onClicked: addDlg.open()
                }
                PillButton {
                    text: qsTr("Liste importieren")
                    iconName: "upload"
                    onClicked: page.importList()
                }
            }

            EmptyState {
                visible: Stations.list.length === 0
                anchors.horizontalCenter: parent.horizontalCenter
                iconName: "broadcast"
                title: qsTr("Noch keine Sender")
                text: Stations.online
                      ? qsTr("Füge einen Sender mit seiner Internetadresse hinzu oder suche unten im Senderverzeichnis.")
                      : qsTr("Füge einen Sender mit seiner Internetadresse hinzu – auch Senderlisten (.pls, .m3u) von Radio-Webseiten gehen.")
            }

            // ---- the stations ----
            Flow {
                x: page.inset
                width: col.width - 2 * page.inset
                spacing: 18
                visible: Stations.list.length > 0
                Repeater {
                    model: Stations.list
                    delegate: Item {
                        id: cell
                        readonly property bool playing: Player.currentId === modelData.item
                        width: page.tile
                        height: page.tile + 52
                        CoverArt {
                            id: art
                            width: page.tile
                            height: width
                            radius: Theme.radius
                            coverId: modelData.item
                            version: String(modelData.logo)
                            label: modelData.name
                            requestSize: 256
                            scale: hover.containsMouse ? 1.02 : 1
                            Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                        }
                        Rectangle {
                            anchors.fill: art
                            radius: art.radius
                            color: "transparent"
                            border.width: cell.playing ? 3 : 0
                            border.color: Theme.accent
                        }
                        Rectangle {
                            visible: hover.containsMouse || cell.playing
                            width: 44; height: 44; radius: 22
                            anchors.right: art.right
                            anchors.bottom: art.bottom
                            anchors.margins: 10
                            color: Theme.accent
                            Icon {
                                anchors.centerIn: parent
                                anchors.horizontalCenterOffset: cell.playing && Player.playing ? 0 : 1
                                name: cell.playing && Player.playing ? "pause" : "play"
                                size: 20
                                color: Theme.onAccent
                            }
                        }
                        Text {
                            anchors.top: art.bottom
                            anchors.topMargin: 10
                            width: parent.width
                            text: modelData.name
                            elide: Text.ElideRight
                            font.family: Theme.uiFont
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: cell.playing ? Theme.accent : Theme.text
                        }
                        Text {
                            anchors.top: art.bottom
                            anchors.topMargin: 30
                            width: parent.width
                            text: cell.playing && Player.title !== modelData.name ? Player.artist + " – " + Player.title
                                  : [modelData.format, modelData.tags.split(",")[0]].filter(function (s) { return s !== "" }).join(" · ")
                            elide: Text.ElideRight
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                            color: Theme.textDim
                        }
                        MouseArea {
                            id: hover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: (m) => {
                                if (m.button === Qt.RightButton) {
                                    stationMenu.station = modelData
                                    stationMenu.popup()
                                } else if (cell.playing) {
                                    Player.toggle()
                                } else {
                                    Stations.play(modelData.id)
                                }
                            }
                        }
                    }
                }
            }

            // ---- the directory ----
            Column {
                x: page.inset
                width: col.width - 2 * page.inset
                spacing: 12

                Text {
                    text: qsTr("Sender suchen")
                    font.family: Theme.displayFont
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    font.capitalization: Theme.style.titleUpper ? Font.AllUppercase : Font.MixedCase
                    color: Theme.text
                }
                Text {
                    visible: !Stations.online
                    width: Math.min(parent.width, 720)
                    wrapMode: Text.Wrap
                    text: qsTr("Das freie Verzeichnis radio-browser.info kennt Zehntausende Sender. Eingeschaltet gehen deine Suchwörter und die Sender, die du hörst, an radio-browser.info; die Senderlogos kommen von den Webseiten der Sender.")
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    lineHeight: 1.2
                    color: Theme.textDim
                }
                PillButton {
                    visible: !Stations.online
                    text: qsTr("Senderverzeichnis einschalten")
                    iconName: "search"
                    onClicked: Stations.online = true
                }
                HTextField {
                    id: searchField
                    visible: Stations.online
                    width: Math.min(parent.width, 520)
                    placeholder: qsTr("Name oder Richtung, z. B. Jazz, Nachrichten, Klassik")
                    onAccepted: Stations.search(text)
                }
                Text {
                    visible: Stations.online && Stations.resultsText !== ""
                    text: Stations.resultsText
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: Theme.textDim
                }
                Repeater {
                    model: Stations.online ? Stations.results : []
                    delegate: Rectangle {
                        width: Math.min(col.width - 2 * page.inset, 860)
                        height: 54
                        radius: Theme.radius
                        color: rowHover.containsMouse ? Theme.surfaceHover : "transparent"
                        CoverArt {
                            id: resultArt
                            x: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 38; height: 38
                            radius: Theme.radius / 2
                            label: modelData.name
                        }
                        Column {
                            anchors.left: resultArt.right
                            anchors.leftMargin: 12
                            anchors.right: addButton.left
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                width: parent.width
                                text: modelData.name
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: Theme.text
                            }
                            Text {
                                width: parent.width
                                text: [modelData.country, modelData.format, modelData.tags.split(",").slice(0, 3).join(", ")]
                                      .filter(function (s) { return s !== "" }).join(" · ")
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: 12
                                color: Theme.textDim
                            }
                        }
                        MouseArea { id: rowHover; anchors.fill: parent; hoverEnabled: true }
                        PillButton {
                            id: addButton
                            readonly property bool known: Stations.list.some(function (s) {
                                return (modelData.uuid !== "" && s.uuid === modelData.uuid) || s.stream === modelData.stream
                            })
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            compact: true
                            enabled: !known
                            text: known ? qsTr("In der Liste") : qsTr("Hinzufügen")
                            iconName: known ? "check" : "add"
                            onClicked: Stations.addResult(index)
                        }
                    }
                }
            }
        }
    }

    HMenu {
        id: stationMenu
        property var station: ({})
        HMenuItem {
            text: qsTr("Abspielen")
            onTriggered: Stations.play(stationMenu.station.id)
        }
        HMenuItem {
            text: qsTr("Umbenennen")
            onTriggered: {
                renameDlg.stationId = stationMenu.station.id
                renameField.text = stationMenu.station.name
                renameDlg.open()
            }
        }
        HMenuItem {
            text: qsTr("Webseite öffnen")
            enabled: (stationMenu.station.homepage || "") !== ""
            onTriggered: Qt.openUrlExternally(stationMenu.station.homepage)
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
        HMenuItem {
            text: qsTr("Entfernen")
            onTriggered: Stations.remove(stationMenu.station.id)
        }
    }

    HDialog {
        id: addDlg
        title: qsTr("Sender hinzufügen")
        text: qsTr("Die Internetadresse des Senders oder seiner Senderliste (.pls, .m3u). Der Name ist freiwillig; ohne nimmt der Player den, den der Sender selbst angibt.")
        acceptText: qsTr("Hinzufügen")
        acceptIcon: "add"
        acceptEnabled: addressField.text.trim() !== ""
        HTextField {
            id: addressField
            width: parent.width
            placeholder: qsTr("https://… ")
            onAccepted: addDlg.accept()
        }
        HTextField {
            id: nameField
            width: parent.width
            placeholder: qsTr("Name (freiwillig)")
            onAccepted: addDlg.accept()
        }
        onOpened: { addressField.text = ""; nameField.text = ""; addressField.input.forceActiveFocus() }
        onAccepted: Stations.add(addressField.text, nameField.text)
    }

    HDialog {
        id: renameDlg
        property var stationId: 0
        title: qsTr("Sender umbenennen")
        acceptText: qsTr("Umbenennen")
        acceptEnabled: renameField.text.trim() !== ""
        HTextField {
            id: renameField
            width: parent.width
            onAccepted: renameDlg.accept()
        }
        onOpened: renameField.input.forceActiveFocus()
        onAccepted: Stations.rename(stationId, renameField.text)
    }

    FileDialog {
        id: importDlg
        title: qsTr("Senderliste importieren")
        fileMode: FileDialog.OpenFile
        currentFolder: Playlists.defaultFolder()
        nameFilters: [qsTr("Senderlisten (*.pls *.m3u *.m3u8 *.xspf)"), qsTr("Alle Dateien (*)")]
        onAccepted: Stations.importFile(selectedFile)
    }
}
