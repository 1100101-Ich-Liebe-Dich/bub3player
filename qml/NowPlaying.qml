// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Full-window "now playing": big cover, big type, live spectrum.
Rectangle {
    id: np
    property bool open: false
    signal close()

    color: Theme.bg
    opacity: open ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

    Ambient {
        anchors.fill: parent
        coverId: Player.currentId
        strength: Theme.ambient > 0 ? Math.min(0.75, 0.35 + Theme.ambient) : 0.5
    }

    MouseArea { anchors.fill: parent; hoverEnabled: true } // block clicks to the UI underneath

    IconButton {
        x: 24
        y: 20
        iconName: "down"
        iconSize: 26
        tip: qsTr("Schließen (Esc)")
        onClicked: np.close()
    }

    readonly property real coverSize: Math.min(np.height - 280, np.width * 0.40, 640)
    readonly property bool wide: np.width > 980
    property real lyricsMix: window.lyricsOn ? 1 : 0
    Behavior on lyricsMix { NumberAnimation { duration: 320; easing.type: Easing.InOutCubic } }

    Item {
        id: stage
        anchors.fill: parent
        anchors.topMargin: 70
        anchors.bottomMargin: 150
        anchors.leftMargin: 72
        anchors.rightMargin: 72
        transform: Translate { y: np.open ? 0 : 24; Behavior on y { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } } }

        CoverArt {
            id: bigCover
            width: np.coverSize
            height: np.coverSize
            anchors.verticalCenter: parent.verticalCenter
            radius: Theme.radius + 6
            requestSize: 1024
            coverId: Player.currentId
            label: Player.album || Player.title
            opacity: 1 - np.lyricsMix
            visible: opacity > 0
        }
        // the lyrics take the place of the cover
        Loader {
            x: bigCover.x
            width: np.coverSize
            height: parent.height
            active: np.lyricsMix > 0
            opacity: np.lyricsMix
            sourceComponent: LyricsView {
                fontFamily: Theme.displayFont
                fontSize: Math.round(Math.max(22, Math.min(34, np.coverSize / 17)))
                color: Theme.text
                dimColor: Theme.alpha(Theme.text, 0.32)
            }
        }

        Column {
            anchors.left: bigCover.right
            anchors.leftMargin: 56
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            Text {
                width: parent.width
                text: Player.title
                font.family: Theme.displayFont
                font.pixelSize: Math.max(34, Math.min(64, np.width / 22))
                font.weight: Font.Bold
                font.letterSpacing: -1.2
                lineHeight: 0.95
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                color: Theme.text
            }
            Text {
                width: parent.width
                text: Player.artist
                font.family: Theme.uiFont
                font.pixelSize: 22
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                color: Theme.accent
            }
            Text {
                width: parent.width
                text: Player.album + (Player.year > 0 ? ", " + Player.year : "")
                font.family: Theme.uiFont
                font.pixelSize: 16
                elide: Text.ElideRight
                color: Theme.textDim
            }
            Rectangle {
                visible: Player.format !== ""
                height: 26
                width: npFmt.implicitWidth + 18
                radius: 6
                color: "transparent"
                border.color: Theme.alpha(Theme.text, 0.2)
                Text {
                    id: npFmt
                    anchors.centerIn: parent
                    text: Player.formatInfo
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: Theme.textDim
                }
            }

            Item { width: 1; height: 18 }

            SeekRibbon {
                width: parent.width
                height: 44
                bars: Math.max(32, Math.floor(width / 9))
                barGap: 3
            }
            Item {
                width: parent.width
                height: 18
                Text {
                    text: window.fmtTime(Player.position)
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: Theme.textDim
                }
                Text {
                    anchors.right: parent.right
                    text: window.fmtTime(Player.duration)
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: Theme.textDim
                }
            }

            Row {
                spacing: 16
                topPadding: 8
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "shuffle"; iconSize: 20; active: Queue.shuffle
                    onClicked: Queue.shuffle = !Queue.shuffle
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 48; height: 48
                    iconName: "prev"; iconSize: 28; iconColor: Theme.text
                    onClicked: Player.previous()
                }
                AbstractButton {
                    id: bigPlay
                    anchors.verticalCenter: parent.verticalCenter
                    width: 68; height: 68
                    hoverEnabled: true
                    onClicked: Player.toggle()
                    background: Rectangle {
                        radius: 34
                        color: bigPlay.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent
                        scale: bigPlay.pressed ? 0.95 : 1
                    }
                    contentItem: Item {
                        Icon {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: Player.playing ? 0 : 2
                            name: Player.playing ? "pause" : "play"
                            size: 32
                            color: Theme.onAccent
                        }
                    }
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 48; height: 48
                    iconName: "next"; iconSize: 28; iconColor: Theme.text
                    onClicked: Player.next()
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: Queue.repeat === 2 ? "repeat-one" : "repeat"; iconSize: 20; active: Queue.repeat > 0
                    onClicked: Queue.cycleRepeat()
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "lyrics"; iconSize: 20; active: Lyrics.shown
                    opacity: window.lyricsMissing ? 0.5 : 1
                    tip: window.lyricsTip
                    onClicked: window.toggleLyrics()
                }
                HeartButton {
                    anchors.verticalCenter: parent.verticalCenter
                    size: 36
                    on: Library.currentFavorite
                    onClicked: Library.toggleCurrentFavorite()
                }
                Item { width: 12; height: 1 }
                VolumeSlider { anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }

    // Live spectrum across the bottom edge.
    Row {
        id: spectrum
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 72
        anchors.rightMargin: 72
        anchors.bottomMargin: 40
        height: 96
        spacing: 6
        visible: Player.spectrumAvailable
        readonly property var spec: Player.spectrum
        Repeater {
            model: 40
            Rectangle {
                readonly property real level: spectrum.spec.length > index ? spectrum.spec[index] : 0
                width: (spectrum.width - 39 * spectrum.spacing) / 40
                height: Math.max(3, spectrum.height * level)
                anchors.bottom: parent.bottom
                radius: Math.min(width / 2, 3)
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.accent }
                    GradientStop { position: 1; color: Theme.alpha(Theme.accent, 0.25) }
                }
            }
        }
    }
}
