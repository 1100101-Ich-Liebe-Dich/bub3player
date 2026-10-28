// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// One slide of the year in review (see YearReview for what is on them). The
// layout is the same in every theme; card, fonts, colours, numbers and the way
// text appears come from window.look (and RvCard, RvNumber, RvText).
Item {
    id: sl
    property var slide: ({})
    property var review: ({})
    readonly property bool bare: window.look.card === "none"       // Klassik: straight on the colour field
    readonly property bool terminal: window.look.skin === "terminal"
    readonly property bool tower: window.look.skin === "tower"
    readonly property bool paper: window.look.card === "paper"
    // colours on the card (or on Klassik's colour field)
    // the intro stands on the background itself, the other slides on a card
    readonly property bool onCard: !bare && slide.kind !== "intro"
    readonly property color ink: bare ? "#FFFFFF" : paper && onCard ? "#2B2118" : Theme.text
    readonly property color dim: bare ? Qt.rgba(1, 1, 1, 0.72) : paper && onCard ? "#6B5A44" : Theme.textDim
    readonly property color hi: bare ? "#FFFFFF" : paper && onCard ? "#B4471C" : Theme.accent
    readonly property string headFont: tower ? "Barlow Condensed" : Theme.displayFont
    readonly property bool upper: window.look.upper
    // the terminal's frame says which year; the kicker inside says what
    readonly property string frameTitle: terminal ? qsTr("Dein Jahr %1").arg(review.year || "") : ""
    signal play(var ids, var startId)

    Loader {
        anchors.fill: parent
        sourceComponent: ({ intro: intro, number: number, hero: hero, list: list, shares: list, bars: bars, facts: facts })[sl.slide.kind] || null
    }

    // ---- building blocks ----
    component Kicker: RvText {
        font.family: sl.terminal ? Theme.monoFont : Theme.uiFont
        font.pixelSize: 17
        font.weight: Font.DemiBold
        font.letterSpacing: sl.terminal ? 0 : 2.5
        font.capitalization: sl.terminal ? Font.MixedCase : Font.AllUppercase
        color: sl.hi
        full: (sl.terminal ? "$ " : "") + (sl.slide.kicker || "")
    }
    component Title: RvText {
        font.family: sl.paper ? window.look.handFont : sl.headFont
        font.pixelSize: sl.paper ? 46 : 52
        font.weight: Theme.style.titleWeight || Font.Bold
        font.capitalization: sl.upper ? Font.AllUppercase : Font.MixedCase
        font.letterSpacing: Theme.style.titleSpacing !== undefined ? Theme.style.titleSpacing : -1
        lineHeight: 0.95
        color: sl.ink
    }
    component Body: RvText {
        font.family: sl.terminal ? Theme.monoFont : Theme.uiFont
        font.pixelSize: 20
        lineHeight: 1.25
        color: sl.dim
    }

    // ---- intro: the year, big ----
    Component {
        id: intro
        Item {
            Column {
                anchors.centerIn: parent
                width: parent.width
                spacing: 16
                Kicker { width: parent.width; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 22 }
                RvNumber {
                    anchors.horizontalCenter: parent.horizontalCenter
                    value: sl.review.year || 0
                    size: 220
                    color: sl.hi
                    grouped: false // a year is no amount
                    counting: false
                }
                Body {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    full: sl.slide.text || ""
                    delay: 500
                    font.pixelSize: 24
                    color: sl.ink
                }
                Body {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    full: qsTr("Weiter mit → oder einem Klick")
                    delay: 1100
                    font.pixelSize: 15
                }
            }
        }
    }

    // ---- one big number ----
    Component {
        id: number
        Item {
            RvCard {
                id: c
                anchors.centerIn: parent
                width: Math.min(parent.width, 880)
                height: col.height + 2 * padding
                title: frameTitle
                Column {
                    id: col
                    width: parent.width
                    spacing: 14
                    Kicker {}
                    Row {
                        spacing: 18
                        RvNumber {
                            id: big
                            value: sl.slide.value || 0
                            size: Math.min(150, 1000 / Math.max(4, String(Library.formatCount(sl.slide.value || 0)).length + 1))
                            color: sl.hi
                        }
                        RvText {
                            anchors.baseline: big.bottom
                            anchors.baselineOffset: -big.height * 0.2
                            full: sl.slide.unit || ""
                            delay: 300
                            font.family: sl.headFont
                            font.pixelSize: 36
                            font.capitalization: sl.upper ? Font.AllUppercase : Font.MixedCase
                            color: sl.ink
                        }
                    }
                    Repeater {
                        model: sl.slide.lines || []
                        Body { width: col.width; full: modelData; delay: 900 + index * 450 }
                    }
                }
            }
        }
    }

    // ---- one artist, title or album with its picture ----
    Component {
        id: hero
        Item {
            RvCard {
                anchors.centerIn: parent
                width: Math.min(parent.width, 1000)
                height: Math.max(pic.height, info.height) + 2 * padding
                title: frameTitle
                CoverArt {
                    id: pic
                    width: Math.min(340, sl.height - 160)
                    height: width
                    radius: sl.slide.round ? width / 2 : (window.look.square ? 0 : 18)
                    coverId: sl.slide.trackId || 0
                    artist: sl.slide.round ? (sl.slide.name || "") : ""
                    label: sl.slide.name || ""
                    requestSize: 512
                    opacity: 0
                    scale: 0.9
                    Component.onCompleted: { opacity = 1; scale = 1 }
                    Behavior on opacity { NumberAnimation { duration: window.animationsActive ? 700 : 0 } }
                    Behavior on scale { NumberAnimation { duration: window.animationsActive ? 900 : 0; easing.type: Easing.OutBack } }
                }
                Column {
                    id: info
                    anchors.left: pic.right
                    anchors.leftMargin: 40
                    anchors.right: parent.right
                    anchors.verticalCenter: pic.verticalCenter
                    spacing: 14
                    Kicker { width: parent.width }
                    Title { width: parent.width; full: sl.slide.name || ""; delay: 250; maximumLineCount: 3; elide: Text.ElideRight }
                    Body { width: parent.width; visible: (sl.slide.artist || "") !== ""; full: sl.slide.artist || ""; delay: 500; font.pixelSize: 24; color: sl.ink }
                    Repeater {
                        model: sl.slide.lines || []
                        Body { width: info.width; full: modelData; delay: 900 + index * 400 }
                    }
                    PillButton {
                        visible: sl.slide.playable === true && (sl.slide.trackId || 0) > 0
                        text: qsTr("Abspielen")
                        iconName: "play"
                        primary: true
                        onClicked: sl.play(sl.slide.ids || [sl.slide.trackId], sl.slide.trackId)
                    }
                }
            }
        }
    }

    // ---- a top five (with pictures) or shares (genres) ----
    Component {
        id: list
        Item {
            RvCard {
                anchors.centerIn: parent
                width: Math.min(parent.width, 900)
                height: listCol.height + 2 * padding
                title: frameTitle
                Column {
                    id: listCol
                    width: parent.width
                    spacing: 12
                    Kicker {}
                    Title { width: parent.width; full: sl.slide.title || ""; delay: 200; font.pixelSize: 40 }
                    Item { width: 1; height: 6 }
                    Repeater {
                        model: sl.slide.items || []
                        delegate: Item {
                            id: row
                            width: listCol.width
                            height: sl.slide.kind === "shares" || sl.terminal ? 44 : 66
                            opacity: 0
                            x: sl.paper ? 0 : 30
                            Component.onCompleted: { appear.start() }
                            SequentialAnimation {
                                id: appear
                                PauseAnimation { duration: window.animationsActive ? 450 + index * 220 : 0 }
                                ParallelAnimation {
                                    NumberAnimation { target: row; property: "opacity"; to: 1; duration: window.animationsActive ? 400 : 0 }
                                    NumberAnimation { target: row; property: "x"; to: 0; duration: window.animationsActive ? 500 : 0; easing.type: Easing.OutCubic }
                                }
                            }
                            Text {
                                id: rank
                                width: 54
                                anchors.verticalCenter: parent.verticalCenter
                                text: sl.terminal || sl.tower ? String(index + 1).padStart(2, "0") : String(index + 1)
                                font.family: sl.paper ? window.look.handFont : window.look.numberFont
                                font.pixelSize: sl.tower ? 24 : 34
                                font.weight: window.look.numberWeight
                                color: index === 0 ? sl.hi : sl.dim
                            }
                            CoverArt {
                                id: rowPic
                                visible: sl.slide.kind !== "shares" && !sl.terminal
                                anchors.left: rank.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: visible ? 56 : 0
                                height: 56
                                radius: sl.slide.round ? 28 : (window.look.square ? 0 : 8)
                                coverId: modelData.trackId || 0
                                artist: sl.slide.round ? modelData.name : ""
                                label: modelData.name
                                requestSize: 96
                            }
                            Column {
                                anchors.left: rowPic.visible ? rowPic.right : rank.right
                                anchors.leftMargin: rowPic.visible ? 18 : 6
                                anchors.right: count.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 5
                                Text {
                                    width: parent.width
                                    text: modelData.name + (sl.terminal && modelData.artist ? " – " + modelData.artist : "")
                                    elide: Text.ElideRight
                                    font.family: sl.paper ? window.look.handFont : sl.terminal ? Theme.monoFont : sl.headFont
                                    font.pixelSize: sl.paper ? 26 : 24
                                    font.weight: sl.paper ? Font.Normal : Font.DemiBold
                                    font.capitalization: sl.upper ? Font.AllUppercase : Font.MixedCase
                                    color: sl.ink
                                }
                                Text {
                                    visible: !sl.terminal && (modelData.artist || "") !== ""
                                    width: parent.width
                                    text: modelData.artist || ""
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: 15
                                    color: sl.dim
                                }
                                StatMeter {
                                    visible: sl.slide.kind === "shares"
                                    width: parent.width
                                    value: (modelData.share || 0) / Math.max(0.0001, (sl.slide.items[0] || {}).share || 1)
                                    strong: index === 0
                                    color: sl.bare ? "#FFFFFF" : "transparent"
                                }
                            }
                            Text {
                                id: count
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: sl.slide.kind === "shares" ? Math.round((modelData.share || 0) * 100) + " %"
                                                                 : Library.formatCount(modelData.plays || 0) + "×"
                                font.family: sl.terminal ? Theme.monoFont : Theme.uiFont
                                font.pixelSize: 18
                                color: sl.dim
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- a chart: hours of the day, months, decades ----
    Component {
        id: bars
        Item {
            RvCard {
                anchors.centerIn: parent
                width: Math.min(parent.width, 1000)
                height: barsCol.height + 2 * padding
                title: frameTitle
                Column {
                    id: barsCol
                    width: parent.width
                    spacing: 12
                    Kicker {}
                    Title { width: parent.width; full: sl.slide.title || ""; delay: 200 }
                    Repeater {
                        model: sl.slide.lines || []
                        Body { width: barsCol.width; full: modelData; delay: 600 + index * 350 }
                    }
                    Item { width: 1; height: 18 }
                    StatBars {
                        id: chart
                        width: parent.width
                        height: Math.min(260, sl.height - 330)
                        values: sl.slide.bars || []
                        labels: sl.slide.labels || []
                        labelEvery: values.length > 16 ? 3 : 1
                        highlight: sl.slide.highlight !== undefined ? sl.slide.highlight : -1
                        color: sl.bare ? "#FFFFFF" : "transparent"
                        labelColor: sl.dim
                        slot: sl.paper ? Qt.rgba(0.35, 0.25, 0.1, 0.12) : Theme.alpha("#000000", 0.35)
                        tipText: function (i) { return sl.slide.tip ? sl.slide.tip(chart.values[i]) : "" }
                    }
                }
            }
        }
    }

    // ---- two or three facts side by side ----
    Component {
        id: facts
        Item {
            RvCard {
                anchors.centerIn: parent
                width: Math.min(parent.width, 1080)
                height: factsCol.height + 2 * padding
                title: frameTitle
                Column {
                    id: factsCol
                    width: parent.width
                    spacing: 18
                    Kicker {}
                    Title { width: parent.width; full: sl.slide.title || ""; delay: 200; font.pixelSize: 40 }
                    Item { width: 1; height: 8 }
                    Row {
                        width: parent.width
                        spacing: 36
                        Repeater {
                            model: sl.slide.facts || []
                            Column {
                                width: (factsCol.width - 36 * ((sl.slide.facts || []).length - 1)) / Math.max(1, (sl.slide.facts || []).length)
                                spacing: 6
                                RvNumber {
                                    visible: modelData.big === undefined
                                    value: modelData.value || 0
                                    suffix: modelData.suffix || ""
                                    size: 78
                                    color: sl.hi
                                    delay: 400 + index * 350
                                }
                                RvText { // a word instead of a number (a key: "8A")
                                    visible: modelData.big !== undefined
                                    full: modelData.big || ""
                                    delay: 400 + index * 350
                                    font.family: window.look.numberFont
                                    font.weight: window.look.numberWeight
                                    font.pixelSize: sl.tower ? 56 : 78
                                    color: sl.hi
                                }
                                RvText {
                                    width: parent.width
                                    full: modelData.unit || ""
                                    delay: 500 + index * 350
                                    font.family: sl.headFont
                                    font.pixelSize: 24
                                    font.weight: Font.DemiBold
                                    font.capitalization: sl.upper ? Font.AllUppercase : Font.MixedCase
                                    color: sl.ink
                                }
                                Body { width: parent.width; full: modelData.text || ""; delay: 800 + index * 350; font.pixelSize: 17 }
                            }
                        }
                    }
                }
            }
        }
    }
}
