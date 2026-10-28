// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// Double cassette deck. Deck A plays the current track: its label is written
// by hand, the reels turn (faster the emptier they are) and the tape moves
// from left to right as the track progresses. Deck B holds what comes next.
Item {
    id: tape
    property color ink: "#45E3FF"
    property bool active: true
    readonly property real progress: Player.duration > 0 ? Math.min(1, Player.position / Player.duration) : 0
    readonly property bool running: Player.playing && active && visible

    // reel angles: tape speed is constant, so the angular speed is v / r
    property real angL: 0
    property real angR: 0
    Timer {
        interval: 33
        repeat: true
        running: tape.running
        onTriggered: {
            var rl = 0.075 + 0.125 * (1 - tape.progress)
            var rr = 0.075 + 0.125 * tape.progress
            tape.angL = (tape.angL + 2.2 / rl) % 360
            tape.angR = (tape.angR + 2.2 / rr) % 360
        }
    }

    TwCd.Unit { anchors.fill: parent }

    Text {
        x: 16; y: 10
        text: "DOUBLE CASSETTE DECK"
        font.family: "Barlow Condensed"; font.pixelSize: 12; font.weight: Font.Bold; font.italic: true; font.letterSpacing: 1.5
        color: "#d6dce2"
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 16; y: 11
        text: "AUTO REVERSE  ·  HIGH SPEED DUBBING"
        font.family: "Barlow Condensed"; font.pixelSize: 10; font.weight: Font.Bold; font.letterSpacing: 1
        color: "#8c949d"
    }

    Row {
        id: decks
        x: 16
        y: 30
        spacing: 14
        readonly property real dw: (tape.width - 32 - 14) / 2
        readonly property real dh: tape.height - 30 - 62

        // ---- deck A ----
        Deck {
            width: decks.dw; height: decks.dh
            name: "DECK A"
            sub: "PLAYBACK"
            playing: tape.running
            Cassette {
                anchors.fill: parent
                anchors.margins: 8
                labelText: Player.hasTrack ? Player.title : "Mixtape"
                subText: Player.hasTrack ? Player.artist : "BuB3Player"
                side: "A"
                stripe: tape.ink
                prog: tape.progress
                hubL: tape.angL
                hubR: tape.angR
            }
        }
        // ---- deck B ----
        Deck {
            width: decks.dw; height: decks.dh
            name: "DECK B"
            sub: "REC / PLAY"
            playing: false
            Cassette {
                anchors.fill: parent
                anchors.margins: 8
                labelText: Queue.upNext !== "" ? Queue.upNext.split(" – ")[0] : "Leer"
                subText: Queue.upNext !== "" ? qsTr("als Nächstes") : ""
                side: "B"
                stripe: "#ff6a3d"
                prog: 0
                hubL: 0
                hubR: 0
            }
        }
    }

    // ---- counter, lamps and keys ----
    Item {
        x: 16
        y: decks.y + decks.dh + 8
        width: tape.width - 32
        height: 52

        // mechanical counter
        Row {
            id: counter
            y: 4
            spacing: 2
            readonly property int value: Math.floor(Player.position / 1000 * 1.6) % 1000
            Repeater {
                model: 3
                Rectangle {
                    width: 13; height: 18; radius: 2
                    color: "#0a0a0b"
                    border.color: "#3a3e44"
                    Text {
                        anchors.centerIn: parent
                        text: Math.floor(counter.value / Math.pow(10, 2 - index)) % 10
                        font.family: "Barlow Condensed"; font.pixelSize: 14; font.weight: Font.Bold
                        color: "#f1f1ef"
                    }
                    Rectangle { anchors.fill: parent; radius: 2; gradient: Gradient {
                        GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.5) }
                        GradientStop { position: 0.35; color: Qt.rgba(0, 0, 0, 0) }
                        GradientStop { position: 0.65; color: Qt.rgba(0, 0, 0, 0) }
                        GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.5) } } }
                }
            }
        }
        Text { anchors.left: counter.left; anchors.top: counter.bottom; anchors.topMargin: 2; text: "COUNTER"; font.family: "Barlow Condensed"; font.pixelSize: 8; font.weight: Font.Bold; color: "#8c949d" }

        // auto reverse arrows and NR lamps
        Column {
            x: counter.width + 14
            y: 2
            spacing: 3
            Row {
                spacing: 4
                Text { text: "◄"; font.pixelSize: 10; color: "#3a2a12" }
                Text { text: "►"; font.pixelSize: 10; color: tape.running ? "#ffb02e" : "#3a2a12" }
                Text { text: "DIR"; font.family: "Barlow Condensed"; font.pixelSize: 8; font.weight: Font.Bold; color: "#8c949d" }
            }
            Row {
                spacing: 4
                Rectangle { width: 5; height: 5; radius: 2.5; y: 2; color: Player.hasTrack ? "#4dff6a" : "#123a18" }
                Text { text: "NR B"; font.family: "Barlow Condensed"; font.pixelSize: 8; font.weight: Font.Bold; color: "#8c949d" }
                Rectangle { width: 5; height: 5; radius: 2.5; y: 2; color: "#123a18" }
                Text { text: "C"; font.family: "Barlow Condensed"; font.pixelSize: 8; font.weight: Font.Bold; color: "#8c949d" }
            }
        }

        Row {
            anchors.right: parent.right
            y: 0
            spacing: 5
            TwButton { width: 36; label: "REW"; iconName: "prev"; onClicked: Player.seekRelative(-10000) }
            TwButton { width: 36; label: "FF"; iconName: "next"; onClicked: Player.seekRelative(10000) }
            TwButton { width: 40; label: "PLAY"; iconName: "play"; showLed: true; led: Player.state === 1; ledColor: "#4dff6a"; onClicked: Player.play() }
            TwButton { width: 36; label: "STOP"; iconName: ""; onClicked: Player.stop()
                Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 23; width: 8; height: 8; color: "#e6eaee" } }
            TwButton { width: 36; label: "PAUSE"; iconName: "pause"; showLed: true; led: Player.state === 2; ledColor: "#ffb02e"; onClicked: Player.pause() }
        }
    }

    // ---------- parts ----------
    component Deck: Item {
        id: dk
        property string name
        property string sub
        property bool playing: false
        default property alias content: door.data
        Rectangle { // door frame
            anchors.fill: parent
            radius: 9
            gradient: Gradient {
                GradientStop { position: 0; color: "#3a3e45" }
                GradientStop { position: 1; color: "#15171a" }
            }
        }
        Rectangle {
            id: door
            anchors.fill: parent
            anchors.margins: 3
            anchors.topMargin: 16
            radius: 7
            color: "#08090b"
            clip: true
        }
        Rectangle { // smoked window glass over the cassette
            anchors.fill: door
            radius: 7
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.09) }
                GradientStop { position: 0.3; color: Qt.rgba(1, 1, 1, 0.0) }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.25) }
            }
            border.color: Qt.rgba(1, 1, 1, 0.06)
        }
        Text { x: 8; y: 2; text: dk.name; font.family: "Barlow Condensed"; font.pixelSize: 10; font.weight: Font.Bold; font.letterSpacing: 1; color: "#d6dce2" }
        Text { anchors.right: parent.right; anchors.rightMargin: 8; y: 2; text: dk.sub; font.family: "Barlow Condensed"; font.pixelSize: 9; font.weight: Font.Bold; color: "#8c949d" }
        Rectangle {
            anchors.right: parent.right; anchors.rightMargin: 8 + subW.width + 6
            y: 5; width: 6; height: 6; radius: 3
            color: dk.playing ? "#ff3b2e" : "#3a1410"
            SoftGlow { anchors.centerIn: parent; width: 20; height: 20; visible: dk.playing; color: Qt.rgba(1, 0.25, 0.15, 0.6) }
        }
        TextMetrics { id: subW; font.family: "Barlow Condensed"; font.pixelSize: 9; font.weight: Font.Bold; text: dk.sub }
    }

    component Cassette: Item {
        id: cas
        property string labelText
        property string subText
        property string side: "A"
        property color stripe: "#ff6a3d"
        property real prog: 0
        property real hubL: 0
        property real hubR: 0
        // keep the real proportions (100 × 64 mm), centred
        readonly property real cw: Math.min(width, height / 0.64)
        readonly property real ch: cw * 0.64
        Item {
            id: shell
            width: cas.cw; height: cas.ch
            anchors.centerIn: parent
            Rectangle { // body
                anchors.fill: parent
                radius: cas.cw * 0.035
                color: "#1c1d22"
                border.color: "#34363d"
            }
            Rectangle { // head area at the bottom
                x: parent.width * 0.17; y: parent.height * 0.78
                width: parent.width * 0.66; height: parent.height * 0.22
                radius: 3
                color: "#26282e"
                border.color: "#3a3d44"
                Repeater {
                    model: 3
                    Rectangle {
                        x: parent.width * (0.2 + index * 0.3) - width / 2; y: parent.height * 0.35
                        width: parent.width * 0.06; height: parent.height * 0.4; radius: 1
                        color: "#0b0b0d"
                    }
                }
            }
            Repeater { // screws
                model: [[0.04, 0.06], [0.96, 0.06], [0.04, 0.9], [0.96, 0.9], [0.5, 0.88]]
                Rectangle {
                    x: shell.width * modelData[0] - width / 2; y: shell.height * modelData[1] - height / 2
                    width: shell.width * 0.035; height: width; radius: width / 2
                    color: "#55585f"
                    Rectangle { anchors.centerIn: parent; width: parent.width * 0.7; height: 1; color: "#222"; rotation: 30 }
                }
            }
            // label sticker
            Rectangle {
                id: sticker
                x: parent.width * 0.07; y: parent.height * 0.07
                width: parent.width * 0.86; height: parent.height * 0.62
                radius: 2
                color: "#eee7d6"
                Rectangle { // colour band
                    y: parent.height * 0.62
                    width: parent.width; height: parent.height * 0.14
                    color: cas.stripe
                    Rectangle { y: parent.height; width: parent.width; height: parent.height * 0.35; color: Qt.darker(cas.stripe, 1.4) }
                }
                Text { // side letter
                    x: parent.width * 0.025; y: parent.height * 0.02
                    text: cas.side
                    font.family: "Barlow Condensed"; font.pixelSize: Math.max(8, parent.height * 0.24); font.weight: Font.Black
                    color: "#2a2a2a"
                }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: parent.width * 0.03; y: parent.height * 0.04
                    text: "C-90"
                    font.family: "Barlow Condensed"; font.pixelSize: Math.max(7, parent.height * 0.12); font.weight: Font.Bold
                    color: "#2a2a2a"
                }
                // the handwritten title
                Text {
                    x: parent.width * 0.1
                    width: parent.width * 0.8
                    y: -parent.height * 0.02
                    text: cas.labelText
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    font.family: "Permanent Marker"
                    font.pixelSize: Math.max(8, parent.height * 0.2)
                    color: "#1d2c7a"
                    rotation: -1.5
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: parent.width * 0.04
                    y: parent.height * 0.82 - height / 2
                    text: cas.subText
                    elide: Text.ElideRight
                    width: parent.width * 0.6
                    horizontalAlignment: Text.AlignRight
                    font.family: "Permanent Marker"
                    font.pixelSize: Math.max(7, parent.height * 0.11)
                    color: "#ffffff"
                }
                Text {
                    x: parent.width * 0.03
                    y: parent.height * 0.82 - height / 2
                    text: "TYPE II  HIGH"
                    font.family: "Barlow Condensed"; font.pixelSize: Math.max(6, parent.height * 0.09); font.weight: Font.Bold
                    color: "#ffffff"
                }
                // window onto the tape
                Rectangle {
                    id: tw
                    x: parent.width * 0.17; y: parent.height * 0.28
                    width: parent.width * 0.66; height: parent.height * 0.32
                    radius: height * 0.45
                    color: "#3b3a3e"
                    clip: true
                    border.color: "#151517"
                    border.width: 1.5
                    readonly property real hubY: height * 0.5
                    readonly property real hx1: width * 0.13
                    readonly property real hx2: width * 0.87
                    readonly property real full: shell.width * 0.2
                    readonly property real empty: shell.width * 0.075
                    Rectangle { // supply pack
                        readonly property real r: tw.empty + (tw.full - tw.empty) * (1 - cas.prog)
                        x: tw.hx1 - r; y: tw.hubY - r; width: r * 2; height: width; radius: r
                        color: "#3d2414"
                        border.color: "#2a180c"
                    }
                    Rectangle { // take-up pack
                        readonly property real r: tw.empty + (tw.full - tw.empty) * cas.prog
                        x: tw.hx2 - r; y: tw.hubY - r; width: r * 2; height: width; radius: r
                        color: "#3d2414"
                        border.color: "#2a180c"
                    }
                    Hub { x: tw.hx1 - width / 2; y: tw.hubY - height / 2; width: shell.width * 0.13; angle: cas.hubL }
                    Hub { x: tw.hx2 - width / 2; y: tw.hubY - height / 2; width: shell.width * 0.13; angle: cas.hubR }
                    Rectangle { // window reflection
                        anchors.fill: parent; radius: parent.radius
                        gradient: Gradient {
                            GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.18) }
                            GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.0) }
                        }
                    }
                }
            }
        }
    }

    component Hub: Item {
        property real angle: 0
        height: width
        rotation: angle
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "#f2f0ea"
            border.color: "#9a9890"
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.58; height: width; radius: width / 2
            color: "#1b1b1d"
        }
        Repeater {
            model: 6
            Rectangle {
                x: parent.width / 2 - width / 2
                y: parent.height / 2 - parent.height * 0.29
                width: parent.width * 0.1; height: parent.height * 0.14
                color: "#f2f0ea"
                transform: Rotation { origin.x: width / 2; origin.y: parent.height * 0.29; angle: index * 60 }
            }
        }
    }
}
