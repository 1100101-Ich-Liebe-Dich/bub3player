// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Halcyon
import ".."

// Skin "Hi-Fi Turm": a 90s mini system. The tower (amplifier with a VFD,
// CD changer, double cassette deck) is the player; the library sits next to
// it. The big view moves the tower to the middle of a dark room, brings in the
// speakers and switches on the light show.
Item {
    id: skin
    property alias stack: stack
    readonly property bool animated: true
    readonly property bool wantsLevels: true
    readonly property int toastMargin: 40
    function focusSearch() { window.queueOpen = false; sidebar.focusSearch() }
    function clearSearch() { sidebar.clearSearch() }
    function setSearchText(t) { sidebar.searchText = t }

    readonly property int gap: 16
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software
    readonly property var inks: ({ cyan: ["#45E3FF", "#FF7A3A"], amber: ["#FFA53A", "#FF3B2E"], green: ["#5CFF9A", "#FFD23A"], red: ["#FF4A4A", "#FFD23A"] })
    readonly property color ink: (inks[Theme.opt.display] || inks.cyan)[0]
    readonly property color ink2: (inks[Theme.opt.display] || inks.cyan)[1]
    Binding { target: Theme; property: "accentOverride"; value: skin.ink }

    readonly property bool np: window.nowPlayingOpen
    property real npMix: np ? 1 : 0
    Behavior on npMix { NumberAnimation { duration: 750; easing.type: Easing.InOutCubic } }
    // the year in review shows over the theme's background: the furniture steps aside
    property real rvMix: window.reviewOpen ? 1 : 0
    Behavior on rvMix { NumberAnimation { duration: 450; easing.type: Easing.InOutCubic } }
    readonly property real beat: Player.playing ? Player.beat : 0
    readonly property real energy: Player.playing ? Player.energy : 0
    readonly property bool demo: Theme.opt.demo !== false
    property real t: 0
    property real lastFx: 0
    Connections {
        target: window
        function onFxTimeChanged() {
            var dt = window.fxTime - skin.lastFx
            skin.lastFx = window.fxTime
            if (dt > 0 && dt < 0.5) skin.t += dt
        }
    }

    // ---------- room ----------
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#0d0f13" }
            GradientStop { position: 0.75; color: "#08090b" }
            GradientStop { position: 1; color: "#050506" }
        }
    }
    // glow of the display on the wall
    SoftGlow {
        x: tower.x + tower.width * tower.scale / 2 - width / 2
        y: tower.y + 150 * tower.scale - height / 2
        width: 700 * tower.scale; height: width
        color: Qt.rgba(skin.ink.r, skin.ink.g, skin.ink.b, 0.13 + skin.energy * 0.08)
    }

    // ---------- big view backdrop: dark plastic wall with a 90s scope ----------
    Item {
        id: show
        anchors.fill: parent
        opacity: skin.npMix * (1 - skin.rvMix)
        visible: opacity > 0.01
        readonly property var spec: Player.spectrum
        function bandAvg(a, b) {
            var s = spec, n = s.length
            if (!Player.playing || n === 0) return 0
            var i0 = Math.floor(a * n), i1 = Math.max(i0 + 1, Math.floor(b * n)), m = 0
            for (var i = i0; i < i1; ++i) m += s[i]
            return m / (i1 - i0)
        }
        // smoothed: the trace swells with the music, it never jumps
        property real bass: visible ? bandAvg(0, 0.12) : 0
        property real mid: visible ? bandAvg(0.12, 0.4) : 0
        property real high: visible ? bandAvg(0.4, 1.0) : 0
        Behavior on bass { NumberAnimation { duration: 280; easing.type: Easing.OutQuad } }
        Behavior on mid { NumberAnimation { duration: 240; easing.type: Easing.OutQuad } }
        Behavior on high { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }

        ShaderEffect {
            id: scope
            anchors.fill: parent
            visible: skin.canShade
            property real time: skin.t
            property real aspect: width / Math.max(1, height)
            property real mode: Theme.opt.backdrop === "chrome" ? 1 : Theme.opt.backdrop === "plain" ? 2 : 0
            property real bass: show.bass
            property real mid: show.mid
            property real high: show.high
            property real centre: 0.12
            property color ink: skin.ink
            property color wall: "#17191d"
            fragmentShader: "qrc:/shaders/scope.frag.qsb"
        }
        Rectangle { // software renderer
            anchors.fill: parent
            visible: !skin.canShade
            color: "#15171a"
        }
        // floor
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width; height: parent.height * 0.1
            gradient: Gradient {
                GradientStop { position: 0; color: "#00000000" }
                GradientStop { position: 0.2; color: "#0d0e10" }
                GradientStop { position: 1; color: "#060607" }
            }
        }
    }

    // ---------- library ----------
    Item {
        id: library
        anchors.fill: parent
        anchors.margins: skin.gap
        anchors.leftMargin: skin.gap * 2 + 420 * skin.libScale
        opacity: (1 - skin.npMix) * (1 - skin.rvMix)
        visible: opacity > 0.01

        // side: menu or programme (queue)
        Panel {
            id: side
            width: 270
            height: parent.height
            Row {
                id: sideTabs
                x: 14; y: 12
                spacing: 8
                TwButton { width: 112; height: 24; labelOnCap: true; label: "MENÜ"; showLed: true; led: !window.queueOpen; ledColor: skin.ink; onClicked: window.queueOpen = false }
                TwButton { width: 112; height: 24; labelOnCap: true; label: "PROGRAMM"; showLed: true; led: window.queueOpen; ledColor: skin.ink; onClicked: window.queueOpen = true }
            }
            Sidebar {
                id: sidebar
                anchors.fill: parent
                anchors.topMargin: 44
                transparent: true
                showBrand: false
                visible: !window.queueOpen
                currentId: window.currentViewId
                onOpen: (v) => window.openView(v)
                onSearchRequested: (t) => window.search(t)
            }
            QueuePanel {
                anchors.fill: parent
                anchors.topMargin: 40
                anchors.margins: 4
                transparent: true
                clip: true
                visible: window.queueOpen
            }
        }
        Panel {
            anchors.left: side.right
            anchors.leftMargin: skin.gap
            anchors.right: parent.right
            height: parent.height
            ContentStack {
                id: stack
                anchors.fill: parent
                anchors.margins: 6
            }
        }
    }

    // ---------- speakers ----------
    readonly property real libScale: Math.min((height - 2 * gap) / 860, (width * 0.34) / 420)
    readonly property real npScale: Math.min(height * 0.74 / 860, width * 0.96 / (420 + 2 * 300 + 2 * 50))
    readonly property real s: libScale + (npScale - libScale) * npMix
    readonly property real floorY: height * (1 - 0.04 * npMix) - gap * (1 - npMix)
    TwSpeaker {
        width: 300; height: 700
        transformOrigin: Item.BottomRight
        scale: skin.npScale
        x: tower.x - 50 * skin.npScale - width + (1 - skin.npMix) * -400
        y: skin.floorY - height
        opacity: skin.npMix * (1 - skin.rvMix)
        visible: opacity > 0.01
        ink: skin.ink
        beat: skin.beat
        energy: skin.energy
        phase: skin.t
        lights: Theme.opt.speakerLeds === true
        side: "L"
    }
    TwSpeaker {
        width: 300; height: 700
        transformOrigin: Item.BottomLeft
        scale: skin.npScale
        x: tower.x + 420 * tower.scale + 50 * skin.npScale + (1 - skin.npMix) * 400
        y: skin.floorY - height
        opacity: skin.npMix * (1 - skin.rvMix)
        visible: opacity > 0.01
        ink: skin.ink
        beat: skin.beat
        energy: skin.energy
        phase: skin.t + 1
        lights: Theme.opt.speakerLeds === true
        side: "R"
    }

    // ---------- the tower ----------
    Item {
        id: tower
        opacity: 1 - skin.rvMix
        visible: opacity > 0.01
        width: 420
        height: 860
        transformOrigin: Item.TopLeft
        scale: skin.s
        x: skin.gap + ((skin.width - 420 * skin.npScale) / 2 - skin.gap) * skin.npMix
        y: skin.floorY - 860 * skin.s

        // amplifier
        Item {
            id: amp
            width: 420; height: 330
            TwCd.Unit { anchors.fill: parent }
            Text {
                x: 18; y: 12
                text: "BuB3Player"
                font.family: "Barlow Condensed"; font.pixelSize: 30; font.weight: Font.Black; font.italic: true; font.letterSpacing: 0.5
                color: "#e9edf1"
                style: Text.Raised; styleColor: "#000"
            }
            Text {
                x: 20; y: 46
                text: "Projekt präsentiert von www.BAUENUNDBOOTEN.de"
                font.family: "Barlow Condensed"; font.pixelSize: 11; font.weight: Font.DemiBold
                color: "#8c949d"
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally("https://www.bauenundbooten.de") }
            }
            Column {
                anchors.right: parent.right; anchors.rightMargin: 18; y: 14
                Text { anchors.right: parent.right; text: "MINI HI-FI COMPONENT SYSTEM"; font.family: "Barlow Condensed"; font.pixelSize: 10; font.weight: Font.Bold; font.letterSpacing: 1; color: "#9aa2ab" }
                Text { anchors.right: parent.right; text: "HX-990"; font.family: "Barlow Condensed"; font.pixelSize: 18; font.weight: Font.Black; font.italic: true; color: "#c9d0d7" }
            }
            TwDisplay {
                id: display
                x: 16; y: 68
                width: 296; height: 150
                ink: skin.ink
                ink2: skin.ink2
                mode: parseInt(Theme.opt.mode || "0")
                dimmed: Theme.opt.dimmer === true
                demo: skin.demo
                active: window.animationsActive
                beat: skin.beat
            }
            JogDial {
                x: 322; y: 82
                width: 86; height: 86
            }
            Text {
                x: 322; y: 172; width: 86
                horizontalAlignment: Text.AlignHCenter
                text: "VOLUME"
                font.family: "Barlow Condensed"; font.pixelSize: 10; font.weight: Font.Bold; font.letterSpacing: 1.5
                color: "#9aa2ab"
            }
            Row {
                x: 322 + 43 - width / 2; y: 192
                spacing: 6
                TwButton { round: true; width: 24; height: 24; label: "–"; onClicked: Player.volume = Math.max(0, Player.volume - 0.05) }
                TwButton { round: true; width: 24; height: 24; label: "+"; onClicked: Player.volume = Math.min(1, Player.volume + 0.05) }
            }
            Row {
                x: 16; y: 232
                spacing: 6
                TwButton { width: 50; label: "DISPLAY"; onClicked: Theme.setOption("mode", String((parseInt(Theme.opt.mode || "0") + 1) % 3)) }
                TwButton { width: 50; label: "DIMMER"; showLed: true; led: Theme.opt.dimmer === true; ledColor: skin.ink2; onClicked: Theme.setOption("dimmer", !(Theme.opt.dimmer === true)) }
                TwButton { width: 50; label: "DEMO"; showLed: true; led: skin.demo; ledColor: skin.ink2; onClicked: Theme.setOption("demo", !skin.demo) }
                TwButton { width: 52; label: "PROGRAM"; showLed: true; led: window.queueOpen; ledColor: skin.ink; onClicked: window.queueOpen = !window.queueOpen }
                TwButton { width: 52; label: "SHUFFLE"; showLed: true; led: Queue.shuffle; ledColor: skin.ink; onClicked: Queue.shuffle = !Queue.shuffle }
                TwButton { width: 52; label: Queue.repeat === 2 ? "REPEAT 1" : "REPEAT"; showLed: true; led: Queue.repeat > 0; ledColor: skin.ink; onClicked: Queue.cycleRepeat() }
                TwButton { width: 40; label: "TEXT"; showLed: true; led: Lyrics.shown && !window.lyricsMissing; ledColor: skin.ink; enabled: Player.currentId > 0; onClicked: window.toggleLyrics() }
            }
            // bottom strip of little things
            Item {
                x: 16; y: 278
                width: 388; height: 40
                Rectangle { width: parent.width; height: 1; color: "#2b2f35" }
                Row {
                    y: 10
                    spacing: 14
                    Column {
                        spacing: 2
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 16; height: 16; radius: 8
                            gradient: Gradient { GradientStop { position: 0; color: "#c9ced4" } GradientStop { position: 1; color: "#5b6067" } }
                            Rectangle { anchors.centerIn: parent; width: 7; height: 7; radius: 3.5; color: "#000" }
                        }
                        Text { text: "PHONES"; font.family: "Barlow Condensed"; font.pixelSize: 8; font.weight: Font.Bold; color: "#8c949d" }
                    }
                    Column {
                        spacing: 4
                        Row {
                            spacing: 4
                            Rectangle { width: 6; height: 6; radius: 3; y: 1; color: Player.playing ? "#4dff6a" : "#ff3b2e"
                                SoftGlow { anchors.centerIn: parent; width: 18; height: 18; color: Player.playing ? Qt.rgba(0.3, 1, 0.4, 0.5) : Qt.rgba(1, 0.25, 0.2, 0.5) } }
                            Text { text: Player.playing ? "ON" : "STANDBY"; font.family: "Barlow Condensed"; font.pixelSize: 9; font.weight: Font.Bold; color: "#9aa2ab" }
                        }
                        Row {
                            spacing: 4
                            Rectangle { width: 6; height: 6; radius: 3; y: 1; color: skin.beat > 0.35 ? skin.ink2 : Qt.darker(skin.ink2, 4) }
                            Text { text: "BEAT"; font.family: "Barlow Condensed"; font.pixelSize: 9; font.weight: Font.Bold; color: "#9aa2ab" }
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "SUPER T-BASS"
                        font.family: "Barlow Condensed"; font.pixelSize: 15; font.weight: Font.Black; font.italic: true; font.letterSpacing: 1
                        color: skin.beat > 0.5 ? skin.ink2 : "#5f6770"
                        Behavior on color { ColorAnimation { duration: 90 } }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "20-BAND\nSPECTRUM"
                        lineHeight: 0.9
                        font.family: "Barlow Condensed"; font.pixelSize: 9; font.weight: Font.Bold
                        color: "#8c949d"
                    }
                    TwButton {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 62; height: 22
                        labelOnCap: true
                        label: skin.np ? "MENU" : "BIG VIEW"
                        enabled: Player.hasTrack || skin.np
                        onClicked: window.nowPlayingOpen = !window.nowPlayingOpen
                    }
                    TwButton { // the heart: favourite track
                        anchors.verticalCenter: parent.verticalCenter
                        round: true
                        width: 24; height: 24
                        iconName: Library.currentFavorite ? "heart" : "heart-outline"
                        cap: Library.currentFavorite ? "#5a1f2a" : "#26282d"
                        enabled: Player.currentId > 0
                        Accessible.name: Library.currentFavorite ? qsTr("Aus den Favoriten entfernen") : qsTr("Zu den Favoriten")
                        onClicked: Library.toggleCurrentFavorite()
                    }
                }
            }
        }
        TwCd {
            y: 338
            width: 420; height: 190
            ink: skin.ink
            active: window.animationsActive
            beat: skin.beat
        }
        TwTape {
            y: 536
            width: 420; height: 230
            ink: skin.ink
            active: window.animationsActive
        }
        // base with a running light
        Item {
            y: 774
            width: 420; height: 86
            TwCd.Unit { anchors.fill: parent; anchors.bottomMargin: 10 }
            Text {
                x: 18; y: 12
                text: "SUPER T-BASS SYSTEM"
                font.family: "Barlow Condensed"; font.pixelSize: 13; font.weight: Font.Black; font.italic: true; font.letterSpacing: 2
                color: "#c9d0d7"
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 18; y: 14
                text: "DIGITAL SIGNAL PROCESSOR"
                font.family: "Barlow Condensed"; font.pixelSize: 10; font.weight: Font.Bold; font.letterSpacing: 1
                color: "#8c949d"
            }
            Row {
                x: 18; y: 42
                spacing: 4
                Repeater {
                    model: 30
                    Rectangle {
                        width: 8.8; height: 8; radius: 2
                        readonly property bool run: Player.playing && (Math.floor(skin.t * 16) % 30 === index || Math.floor(skin.t * 16 + 15) % 30 === index)
                        readonly property real lvl: Math.max(run ? 1 : 0, skin.beat > 0.3 && index % 4 === Math.floor(skin.t * 4) % 4 ? skin.beat : 0)
                        color: Qt.rgba(skin.ink.r, skin.ink.g, skin.ink.b, 0.1 + 0.9 * lvl)
                    }
                }
            }
            Repeater { // feet
                model: 2
                Rectangle { y: 76; x: index === 0 ? 30 : 330; width: 60; height: 10; radius: 3; color: "#050506" }
            }
        }
    }

    // ---------- lyrics: a wide display on the wall above the tower ----------
    Item {
        id: lyricWall
        property real mix: window.lyricsOn ? 1 : 0
        Behavior on mix { NumberAnimation { duration: 420; easing.type: Easing.InOutCubic } }
        opacity: skin.npMix * mix
        visible: opacity > 0.01
        width: Math.min(skin.width - 160, 1100)
        height: Math.max(90, tower.y - 44)
        x: (skin.width - width) / 2
        y: 22
        Rectangle { // the display: black glass in a dark frame
            anchors.fill: parent
            radius: 10
            color: "#030405"
            border.color: "#2b2f35"
            border.width: 2
            Rectangle {
                anchors.fill: parent
                anchors.margins: 6
                radius: 6
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.rgba(skin.ink.r, skin.ink.g, skin.ink.b, 0.07) }
                    GradientStop { position: 1; color: "#000000" }
                }
            }
        }
        Loader {
            anchors.fill: parent
            anchors.margins: 10
            active: lyricWall.visible
            sourceComponent: LyricsView {
                align: Text.AlignHCenter
                focusAt: 0.5
                fadeEdge: 0.36
                upper: true
                fontFamily: "Barlow Condensed"
                fontSize: Math.round(Math.max(22, Math.min(36, lyricWall.height / 4.4)))
                lineGap: 4
                color: skin.ink
                dimColor: Qt.rgba(skin.ink.r, skin.ink.g, skin.ink.b, 0.3)
                sourceColor: Qt.rgba(skin.ink.r, skin.ink.g, skin.ink.b, 0.5)
            }
        }
    }

    // ---------- parts ----------
    // Black plastic panel with a chrome edge for the library.
    component Panel: Item {
        default property alias content: inner.data
        Rectangle { anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: -4; radius: 12; color: Qt.rgba(0, 0, 0, 0.6) }
        Rectangle {
            anchors.fill: parent
            radius: 12
            gradient: Gradient {
                GradientStop { position: 0; color: "#9aa2ab" }
                GradientStop { position: 0.1; color: "#3a3f46" }
                GradientStop { position: 1; color: "#1c1f23" }
            }
        }
        Material { anchors.fill: parent; anchors.margins: 1.5; material: "plastic"; radius: 11 }
        Item { id: inner; anchors.fill: parent; anchors.margins: 2 }
    }

    // Volume jog with an LED ring.
    component JogDial: Item {
        id: jog
        Repeater {
            model: 24
            Rectangle {
                readonly property real a: (-135 + index / 23 * 270) * Math.PI / 180
                x: jog.width / 2 + Math.sin(a) * jog.width * 0.5 - 2.5
                y: jog.height / 2 - Math.cos(a) * jog.height * 0.5 - 2.5
                width: 5; height: 5; radius: 2.5
                color: index / 23 <= Player.volume + 0.001 ? (index > 19 ? skin.ink2 : skin.ink) : "#14181c"
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.86; height: width; radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#d7dce1" }
                GradientStop { position: 1; color: "#4d5258" }
            }
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.9; height: width; radius: width / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: "#34373c" }
                    GradientStop { position: 1; color: "#0d0e10" }
                }
                Item {
                    anchors.fill: parent
                    rotation: -135 + Player.volume * 270
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: parent.height * 0.1
                        width: parent.width * 0.16; height: width; radius: width / 2
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#000" }
                            GradientStop { position: 1; color: "#3a3d42" }
                        }
                        Rectangle { anchors.centerIn: parent; width: 4; height: 4; radius: 2; color: skin.ink }
                    }
                }
                Rectangle { // gloss
                    x: parent.width * 0.2; y: parent.height * 0.08
                    width: parent.width * 0.6; height: parent.height * 0.3
                    radius: height / 2
                    color: Qt.rgba(1, 1, 1, 0.07)
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            property real startY
            property real startV
            onPressed: (m) => { startY = m.y; startV = Player.volume }
            onPositionChanged: (m) => { if (pressed) Player.volume = Math.max(0, Math.min(1, startV + (startY - m.y) / 160)) }
            onWheel: (w) => Player.volume = Math.max(0, Math.min(1, Player.volume + (w.angleDelta.y > 0 ? 0.04 : -0.04)))
        }
    }
}
