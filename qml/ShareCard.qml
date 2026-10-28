// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// The year on one picture (4:5, saved as 1080 × 1350 pixels): minutes, the
// artist of the year, the top five titles, genre, time of day and tempo. Drawn
// without the live background of the window, so the picture looks the same
// everywhere – in the theme's colours and style.
Item {
    id: sc
    property var review: ({})
    property string persona: ""
    readonly property string skin: window.look.skin
    readonly property bool terminal: skin === "terminal"
    readonly property bool tower: skin === "tower"
    readonly property bool paper: skin === "vintage"
    readonly property color ink: paper ? "#2B2118" : skin === "klassik" ? "#FFFFFF" : Theme.text
    readonly property color dim: paper ? "#6B5A44" : skin === "klassik" ? Qt.rgba(1, 1, 1, 0.75) : Theme.textDim
    readonly property color hi: paper ? "#B4471C" : skin === "klassik" ? "#FFFFFF" : Theme.accent
    readonly property string head: tower ? "Barlow Condensed" : terminal ? Theme.monoFont : Theme.displayFont
    readonly property var artist: (review.topArtists || [])[0] || null
    width: 540
    height: 675

    // ---- the background, by theme ----
    Rectangle {
        anchors.fill: parent
        radius: sc.terminal || sc.tower ? 0 : 28
        clip: true
        gradient: Gradient {
            GradientStop {
                position: 0
                color: sc.skin === "klassik" ? Qt.hsla(Theme.accent.hslHue < 0 ? 0.6 : Theme.accent.hslHue, 0.55, 0.42, 1)
                     : sc.paper ? "#F4EAD2" : sc.skin === "synthwave" ? "#1A0633" : sc.skin === "glas" ? Qt.darker(Theme.accent, 1.7)
                     : sc.skin === "soft" ? Qt.lighter(Theme.bg, 1.04) : sc.skin === "aurora" ? "#071420" : Theme.bg
            }
            GradientStop {
                position: 1
                color: sc.skin === "klassik" ? Qt.hsla(((Theme.accent.hslHue < 0 ? 0.6 : Theme.accent.hslHue) + 0.12) % 1, 0.6, 0.3, 1)
                     : sc.paper ? "#E2CFA6" : sc.skin === "synthwave" ? "#3B0A45" : sc.skin === "glas" ? Qt.darker(Theme.accent2, 1.9)
                     : sc.skin === "aurora" ? "#120A26" : Theme.bg
            }
        }
        // Aurora: soft veils of light
        Repeater {
            model: sc.skin === "aurora" ? 3 : 0
            SoftGlow {
                x: [-160, 200, 20][index]
                y: [-120, 40, 380][index]
                width: 520
                height: 380
                rotation: [-20, 15, -8][index]
                color: Theme.alpha([Theme.accent, Theme.accent2, Qt.lighter(Theme.accent, 1.4)][index], 0.4)
            }
        }
        // Synthwave: the sun and the grid
        Rectangle {
            visible: sc.skin === "synthwave"
            anchors.horizontalCenter: parent.horizontalCenter
            y: 330
            width: 260
            height: 260
            radius: 130
            gradient: Gradient {
                GradientStop { position: 0; color: "#FFD23F" }
                GradientStop { position: 1; color: "#FF3F8E" }
            }
            opacity: 0.2
        }
        Repeater {
            model: sc.skin === "synthwave" ? 9 : 0
            Rectangle { y: 470 + index * index * 3.2; width: sc.width; height: 1; color: Theme.alpha(Theme.accent2, 0.3) }
        }
        // Vintage: stripes of the seventies down the right edge
        Repeater {
            model: sc.paper ? 4 : 0
            Rectangle {
                x: sc.width - 74 + index * 14
                width: 10
                height: sc.height
                color: ["#D2691E", "#C8962E", "#8C9A3A", "#5A3218"][index]
            }
        }
        // Glas: a frosted pane over the colour
        Rectangle {
            visible: sc.skin === "glas"
            anchors.fill: parent
            anchors.margins: 18
            radius: 22
            color: Qt.rgba(1, 1, 1, 0.12)
            border.color: Qt.rgba(1, 1, 1, 0.6)
        }
        // terminal and tower: a frame of lines / a display window
        Rectangle {
            visible: sc.terminal || sc.tower
            anchors.fill: parent
            anchors.margins: 14
            radius: sc.tower ? 8 : 0
            color: "transparent"
            border.width: sc.tower ? 3 : 1
            border.color: sc.tower ? "#1E2328" : Theme.text
        }
    }

    // ---- the content ----
    Column {
        x: 44
        y: 40
        width: sc.width - (sc.paper ? 140 : 88)
        spacing: 8
        Text {
            text: sc.terminal ? "$ bub3 --mein-jahr " + (sc.review.year || "") : qsTr("Mein Jahr in Musik")
            font.family: sc.terminal ? Theme.monoFont : Theme.uiFont
            font.pixelSize: 15
            font.weight: Font.DemiBold
            font.letterSpacing: sc.terminal ? 0 : 2
            font.capitalization: sc.terminal ? Font.MixedCase : Font.AllUppercase
            color: sc.hi
        }
        Text {
            visible: !sc.terminal
            text: String(sc.review.year || "")
            font.family: window.look.numberFont
            font.weight: window.look.numberWeight
            font.pixelSize: sc.tower ? 48 : 64
            color: sc.ink
        }
        Row {
            spacing: 10
            Text {
                text: Library.formatCount(Math.round((sc.review.listenedMs || 0) / 60000))
                font.family: window.look.numberFont
                font.weight: window.look.numberWeight
                font.pixelSize: sc.tower ? 40 : 52
                color: sc.hi
            }
            Text {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8
                text: qsTr("Minuten Musik")
                font.family: sc.head
                font.pixelSize: 20
                color: sc.ink
            }
        }
        Item { width: 1; height: 8 }
        Row {
            visible: sc.artist !== null
            spacing: 16
            CoverArt {
                visible: !sc.terminal
                width: 76
                height: 76
                radius: 38
                coverId: sc.artist ? sc.artist.trackId : 0
                artist: sc.artist ? sc.artist.name : ""
                label: sc.artist ? sc.artist.name : ""
                requestSize: 192
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Text {
                    text: qsTr("Künstler des Jahres")
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: sc.dim
                }
                Text {
                    width: sc.width - 88 - 110
                    text: sc.artist ? sc.artist.name : ""
                    elide: Text.ElideRight
                    font.family: sc.paper ? window.look.handFont : sc.head
                    font.pixelSize: 30
                    font.weight: Font.Bold
                    font.capitalization: window.look.upper ? Font.AllUppercase : Font.MixedCase
                    color: sc.ink
                }
            }
        }
        Item { width: 1; height: 6 }
        Text {
            text: qsTr("Meine Titel")
            font.family: Theme.uiFont
            font.pixelSize: 13
            color: sc.dim
        }
        Repeater {
            model: (sc.review.topTracks || []).slice(0, 5)
            Row {
                spacing: 12
                Text {
                    width: 26
                    text: index + 1
                    font.family: window.look.numberFont
                    font.pixelSize: 20
                    color: index === 0 ? sc.hi : sc.dim
                }
                Text {
                    width: sc.width - 88 - 40
                    text: modelData.name + "  –  " + modelData.artist
                    elide: Text.ElideRight
                    font.family: sc.paper ? window.look.handFont : sc.terminal ? Theme.monoFont : Theme.uiFont
                    font.pixelSize: sc.paper ? 19 : 17
                    font.weight: Font.DemiBold
                    color: sc.ink
                }
            }
        }
        Item { width: 1; height: 6 }
        Row {
            spacing: 26
            Repeater {
                model: [
                    { label: qsTr("Genre"), value: ((sc.review.topGenres || [])[0] || {}).name || "–" },
                    { label: qsTr("Typ"), value: sc.persona || "–" },
                    { label: qsTr("Tempo"), value: sc.review.bpm ? sc.review.bpm + " BPM" : "–" }
                ]
                Column {
                    spacing: 2
                    Text { text: modelData.label; font.family: Theme.uiFont; font.pixelSize: 13; color: sc.dim }
                    Text {
                        text: modelData.value
                        font.family: sc.head
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        font.capitalization: window.look.upper ? Font.AllUppercase : Font.MixedCase
                        color: sc.ink
                    }
                }
            }
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 22
        text: "BuB3Player · www.BAUENUNDBOOTEN.de"
        font.family: Theme.uiFont
        font.pixelSize: 12
        color: sc.dim
    }
}
