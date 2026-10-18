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

    // ---------- light show (big view) ----------
    Item {
        id: show
        anchors.fill: parent
        opacity: skin.npMix * (Theme.opt.lightshow !== false ? 1 : 0)
        visible: opacity > 0.01
        readonly property var spec: Player.spectrum
        function bandAvg(a, b) {
            var s = spec, n = s.length
            if (!Player.playing || n === 0) return 0
            var i0 = Math.floor(a * n), i1 = Math.max(i0 + 1, Math.floor(b * n)), m = 0
            for (var i = i0; i < i1; ++i) m += s[i]
            return m / (i1 - i0)
        }
        readonly property real bass: bandAvg(0, 0.12)
        readonly property real mid: bandAvg(0.12, 0.4)
        readonly property real high: bandAvg(0.4, 1.0)

        // floor
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width; height: parent.height * 0.16
            gradient: Gradient {
                GradientStop { position: 0; color: "#00000000" }
                GradientStop { position: 0.15; color: "#121418" }
                GradientStop { position: 1; color: "#060708" }
            }
        }
        // spotlights on a truss along the top
        Rectangle {
            y: 8; width: parent.width; height: 6
            gradient: Gradient {
                GradientStop { position: 0; color: "#5b6168" }
                GradientStop { position: 1; color: "#1c1f23" }
            }
        }
        Repeater {
            model: [["#FF2A4A", "bass", 0.0], ["#FFD23A", "mid", 1.3], ["#3AFF7A", "high", 2.1], ["#3A8BFF", "high", 3.4], ["#FF3AE0", "mid", 4.2], ["#FF8A2A", "bass", 5.5]]
            Item {
                id: spot
                readonly property real lvl: modelData[1] === "bass" ? show.bass : modelData[1] === "mid" ? show.mid : show.high
                readonly property real fx: (index + 0.5) / 6
                x: show.width * fx
                y: 10
                ShaderEffect {
                    visible: skin.canShade
                    width: show.height * 0.75
                    height: show.height * 1.05
                    x: -width / 2
                    y: 0
                    transform: Rotation {
                        origin.x: show.height * 0.375
                        origin.y: 0
                        angle: Math.sin(skin.t * (0.5 + index * 0.13) + modelData[2]) * 28 + (spot.fx - 0.5) * -30
                    }
                    property color color: modelData[0]
                    property real intensity: Math.min(1, 0.12 + spot.lvl * 1.6)
                    property real spread: 0.16
                    fragmentShader: "qrc:/shaders/beam.frag.qsb"
                }
                Rectangle { // the can
                    x: -11; y: -2
                    width: 22; height: 18; radius: 4
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#3a3e44" }
                        GradientStop { position: 1; color: "#0d0e10" }
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        width: 16; height: 5; radius: 2
                        color: modelData[0]
                        opacity: 0.3 + spot.lvl * 0.7
                    }
                }
            }
        }
        // mirror ball and its sparkles
        Item {
            visible: Theme.opt.mirrorball !== false
            anchors.fill: parent
            Rectangle { x: parent.width / 2 - 0.5; y: 0; width: 1; height: 54; color: "#6a7078" }
            Canvas {
                id: ball
                x: parent.width / 2 - 26; y: 50
                width: 52; height: 52
                onPaint: {
                    var c = getContext("2d"), r = 26
                    c.reset()
                    c.save(); c.beginPath(); c.arc(r, r, r, 0, Math.PI * 2); c.clip()
                    for (var yy = 0; yy < 52; yy += 5)
                        for (var xx = 0; xx < 52; xx += 5) {
                            var v = Math.floor(90 + Math.random() * 150)
                            c.fillStyle = "rgb(" + v + "," + v + "," + (v + 10) + ")"
                            c.fillRect(xx + 0.5, yy + 0.5, 4, 4)
                        }
                    var g = c.createRadialGradient(r * 0.7, r * 0.6, 2, r, r, r)
                    g.addColorStop(0, "rgba(255,255,255,0.35)"); g.addColorStop(1, "rgba(0,0,0,0.55)")
                    c.fillStyle = g; c.fillRect(0, 0, 52, 52)
                    c.restore()
                }
                Component.onCompleted: requestPaint()
            }
            Repeater {
                model: 36
                Rectangle {
                    readonly property real seedA: (index * 137.5) % 360 * Math.PI / 180
                    readonly property real dist: 0.25 + ((index * 53) % 70) / 100
                    readonly property real ang: seedA + skin.t * 0.18
                    x: show.width / 2 + Math.cos(ang) * show.width * 0.55 * dist
                    y: show.height * 0.42 + Math.sin(ang) * show.height * 0.45 * dist
                    width: 5; height: 5; radius: 2.5
                    color: "white"
                    opacity: (0.25 + 0.75 * Math.abs(Math.sin(skin.t * 2.2 + index))) * (0.4 + show.high)
                }
            }
        }
    }

    // ---------- library ----------
    Item {
        id: library
        anchors.fill: parent
        anchors.margins: skin.gap
        anchors.leftMargin: skin.gap * 2 + 420 * skin.libScale
        opacity: 1 - skin.npMix
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
    readonly property real npScale: Math.min(height * 0.84 / 860, width * 0.96 / (420 + 2 * 300 + 2 * 50))
    readonly property real s: libScale + (npScale - libScale) * npMix
    readonly property real floorY: height * (1 - 0.04 * npMix) - gap * (1 - npMix)
    TwSpeaker {
        width: 300; height: 700
        transformOrigin: Item.BottomRight
        scale: skin.npScale
        x: tower.x - 50 * skin.npScale - width + (1 - skin.npMix) * -400
        y: skin.floorY - height
        opacity: skin.npMix
        visible: opacity > 0.01
        ink: skin.ink
        beat: skin.beat
        energy: skin.energy
        phase: skin.t
        lights: Theme.opt.speakerLeds !== false
        side: "L"
    }
    TwSpeaker {
        width: 300; height: 700
        transformOrigin: Item.BottomLeft
        scale: skin.npScale
        x: tower.x + 420 * tower.scale + 50 * skin.npScale + (1 - skin.npMix) * 400
        y: skin.floorY - height
        opacity: skin.npMix
        visible: opacity > 0.01
        ink: skin.ink
        beat: skin.beat
        energy: skin.energy
        phase: skin.t + 1
        lights: Theme.opt.speakerLeds !== false
        side: "R"
    }

    // ---------- the tower ----------
    Item {
        id: tower
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
                spacing: 7
                TwButton { width: 50; label: "DISPLAY"; onClicked: Theme.setOption("mode", String((parseInt(Theme.opt.mode || "0") + 1) % 3)) }
                TwButton { width: 50; label: "DIMMER"; showLed: true; led: Theme.opt.dimmer === true; ledColor: skin.ink2; onClicked: Theme.setOption("dimmer", !(Theme.opt.dimmer === true)) }
                TwButton { width: 50; label: "DEMO"; showLed: true; led: skin.demo; ledColor: skin.ink2; onClicked: Theme.setOption("demo", !skin.demo) }
                TwButton { width: 56; label: "PROGRAM"; showLed: true; led: window.queueOpen; ledColor: skin.ink; onClicked: window.queueOpen = !window.queueOpen }
                TwButton { width: 56; label: "SHUFFLE"; showLed: true; led: Queue.shuffle; ledColor: skin.ink; onClicked: Queue.shuffle = !Queue.shuffle }
                TwButton { width: 56; label: Queue.repeat === 2 ? "REPEAT 1" : "REPEAT"; showLed: true; led: Queue.repeat > 0; ledColor: skin.ink; onClicked: Queue.cycleRepeat() }
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
                        enabled: Player.currentId > 0 || skin.np
                        onClicked: window.nowPlayingOpen = !window.nowPlayingOpen
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
