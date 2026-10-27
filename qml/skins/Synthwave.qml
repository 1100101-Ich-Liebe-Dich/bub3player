// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Halcyon
import ".."

// Skin "Synthwave": retro-future sunset with a neon grid racing to the beat,
// HUD panels with chamfered corners, LED spectrum, glitches on track change.
Item {
    id: skin
    property alias stack: stack
    readonly property bool animated: true
    readonly property bool wantsLevels: true
    readonly property int toastMargin: deck.height + 30
    function focusSearch() { searchField.forceActiveFocus(); searchField.selectAll() }
    function clearSearch() { searchField.text = "" }
    function setSearchText(t) { searchField.text = t }

    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software
    readonly property bool np: window.nowPlayingOpen
    readonly property var pal: Theme.paletteData
    readonly property var sky: pal.sky || ["#05001A", "#3B0A6B", "#B0186E", "#FF7A3D"]
    readonly property var sun: pal.sun || ["#FFE45C", "#FF2E88"]
    readonly property bool glow: Theme.opt.glow === true
    readonly property real beatAmt: Theme.opt.beat !== undefined ? Theme.opt.beat : 0.7
    readonly property real beat: Player.playing ? Player.beat * beatAmt : 0
    readonly property real energy: Player.playing ? Player.energy * beatAmt : 0

    property real npMix: np ? 1 : 0
    Behavior on npMix { NumberAnimation { duration: 600; easing.type: Easing.InOutCubic } }

    // Grid travel: integrates speed, faster with the music.
    property real travel: 0
    property real lastFx: 0
    Connections {
        target: window
        function onFxTimeChanged() {
            var dt = window.fxTime - skin.lastFx
            skin.lastFx = window.fxTime
            if (dt > 0 && dt < 0.5)
                skin.travel += dt * (Theme.opt.gridSpeed !== undefined ? Theme.opt.gridSpeed : 1) * (0.6 + skin.energy * 1.6 + skin.beat * 1.2)
        }
    }
    Connections {
        target: Player
        function onTrackChanged() {
            if (Theme.opt.glitch) {
                deckTitle.trigger()
                npTitle.trigger()
            }
        }
    }

    // ---------- scene ----------
    Rectangle { anchors.fill: parent; color: skin.sky[0] }
    ShaderEffect {
        anchors.fill: parent
        visible: skin.canShade
        property real time: skin.travel
        property real clock: window.fxTime
        property real aspect: width / Math.max(1, height)
        property real beat: skin.beat
        property real energy: skin.energy
        property real horizon: 0.56 + skin.npMix * 0.06
        property real sunOn: Theme.opt.sun ? 1 : 0
        property real starsOn: Theme.opt.stars ? 1 : 0
        property real mountOn: Theme.opt.mountains ? 1 : 0
        property real scan: Theme.opt.scanlines !== undefined ? Theme.opt.scanlines : 0.45
        property real glowOn: skin.glow ? 1 : 0
        property color sky0: skin.sky[0]
        property color sky1: skin.sky[1]
        property color sky2: skin.sky[2]
        property color sky3: skin.sky[3]
        property color sun0: skin.sun[0]
        property color sun1: skin.sun[1]
        property color grid: Theme.accent
        property color grid2: Theme.accent2
        fragmentShader: "qrc:/shaders/synth.frag.qsb"
    }
    Rectangle {
        anchors.fill: parent
        visible: !skin.canShade
        gradient: Gradient {
            GradientStop { position: 0; color: skin.sky[0] }
            GradientStop { position: 0.55; color: skin.sky[2] }
            GradientStop { position: 0.56; color: skin.sky[0] }
            GradientStop { position: 1; color: skin.sky[1] }
        }
    }

    // ---------- top bar ----------
    Item {
        id: topBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 78
        opacity: 1 - skin.npMix
        visible: opacity > 0.01

        Brand {
            id: brand
            x: 26
            anchors.verticalCenter: parent.verticalCenter
            nameFont: "Orbitron"
            nameSize: 24
            nameWeight: Font.Black
            nameSpacing: 2
            upper: true
            nameColor: "#ffffff"
            markColor: Theme.accent
            signatureColor: Theme.textDim
            linkColor: Theme.accent2
            signatureFont: Theme.uiFont
            glowColor: skin.glow ? Theme.accent : "transparent"
            oneLine: true
        }

        // Search
        HudPanel {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: 80
            width: Math.min(460, parent.width - 900)
            visible: width > 160
            height: 40
            chamfer: 10
            fill: Theme.alpha(Theme.bg, 0.7)
            border: Theme.alpha(searchField.activeFocus ? Theme.accent : Theme.accent2, searchField.activeFocus ? 0.9 : 0.5)
            glow: searchField.activeFocus ? 10 : 5
            scan: 0
            Icon { x: 14; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 18; color: Theme.accent2 }
            TextField {
                id: searchField
                anchors.fill: parent
                leftPadding: 42
                rightPadding: 14
                placeholderText: qsTr("SUCHEN …")
                placeholderTextColor: Theme.textFaint
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: 15
                font.letterSpacing: 1
                selectByMouse: true
                background: null
                onTextEdited: searchDebounce.restart()
                Keys.onEscapePressed: { text = ""; window.search(""); focus = false }
                Keys.onReturnPressed: { searchDebounce.stop(); window.search(text) }
            }
            Timer { id: searchDebounce; interval: 280; onTriggered: window.search(searchField.text) }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 26
            anchors.verticalCenter: parent.verticalCenter
            spacing: 22
            Column {
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    anchors.right: parent.right
                    text: clock.now
                    font.family: "Orbitron"
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    font.letterSpacing: 2
                    color: Theme.accent2
                }
                Text {
                    anchors.right: parent.right
                    text: qsTr("%1 TITEL").arg(Library.formatCount(Library.trackCount))
                    font.family: Theme.uiFont
                    font.pixelSize: 11
                    font.letterSpacing: 2
                    color: Theme.textDim
                }
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "palette"
                iconColor: Theme.accent
                tip: qsTr("Theme wechseln")
                onClicked: window.openSettings(1)
            }
        }
        Timer {
            id: clock
            property string now: Qt.formatTime(new Date(), "hh:mm")
            interval: 10000
            running: true
            repeat: true
            triggeredOnStart: true
            onTriggered: now = Qt.formatTime(new Date(), "hh:mm")
        }
    }

    // ---------- library ----------
    Item {
        id: frame
        anchors.fill: parent
        anchors.topMargin: topBar.height
        anchors.leftMargin: 22
        anchors.rightMargin: 22
        anchors.bottomMargin: deck.height + 40
        opacity: 1 - skin.npMix
        visible: opacity > 0.01

        HudPanel {
            id: navPanel
            width: 250
            height: parent.height
            Sidebar {
                id: sidebar
                anchors.fill: parent
                anchors.topMargin: 14
                anchors.bottomMargin: 8
                transparent: true
                showBrand: false
                showSearch: false
                currentId: window.currentViewId
                onOpen: (v) => window.openView(v)
                onSearchRequested: (t) => window.search(t)
            }
        }
        HudPanel {
            id: contentPanel
            anchors.left: navPanel.right
            anchors.leftMargin: 18
            anchors.right: queueHud.left
            anchors.rightMargin: queueHud.width > 1 ? 18 : 0
            height: parent.height
            fill: Theme.alpha(Theme.panel, 0.8)
            border: Theme.alpha(Theme.accent2, 0.6)
            bracket: Theme.accent
            ContentStack {
                id: stack
                anchors.fill: parent
                anchors.margins: 8
                slide: 60
            }
        }
        HudPanel {
            id: queueHud
            anchors.right: parent.right
            width: window.queueOpen ? 330 : 0
            visible: width > 1
            height: parent.height
            Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            QueuePanel {
                anchors.fill: parent
                anchors.margins: 6
                transparent: true
                clip: true
            }
        }
    }

    // ---------- deck ----------
    HudPanel {
        id: deck
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 22
        anchors.bottomMargin: 18 - skin.npMix * 180
        height: 112
        opacity: 1 - skin.npMix
        visible: opacity > 0.01
        fill: Theme.alpha(Theme.bg, 0.82)
        border: Theme.alpha(Theme.accent, 0.9)
        bracket: Theme.accent2
        glow: 12

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 18

            // cover in a neon frame
            Item {
                Layout.preferredWidth: 78
                Layout.preferredHeight: 78
                HudPanel {
                    anchors.fill: parent
                    anchors.margins: -4
                    chamfer: 8
                    fill: "transparent"
                    border: Theme.accent2
                    glow: 8
                    scan: 0
                }
                CoverArt {
                    anchors.fill: parent
                    radius: 0
                    requestSize: 128
                    coverId: Player.currentId
                    label: Player.album || Player.title
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Player.currentId > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: if (Player.currentId > 0) window.nowPlayingOpen = true
                }
            }
            Column {
                Layout.preferredWidth: Math.min(300, deck.width * 0.22)
                spacing: 4
                GlitchLayer {
                    id: deckTitle
                    width: parent.width
                    height: 28
                    enabledFx: skin.glow
                    split: 0.0035
                    Text {
                        width: parent.width
                        text: Player.currentId > 0 ? Player.title : qsTr("Bereit")
                        elide: Text.ElideRight
                        font.family: "Orbitron"
                        font.pixelSize: 19
                        font.weight: Font.Bold
                        font.letterSpacing: 1
                        font.capitalization: Font.AllUppercase
                        color: "#ffffff"
                    }
                }
                Text {
                    width: parent.width
                    text: Player.currentId > 0 ? Player.artist : qsTr("Wähle Musik aus der Bibliothek")
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    font.letterSpacing: 1
                    color: Theme.accent2
                }
                Text {
                    width: parent.width
                    visible: Player.format !== ""
                    text: Player.formatInfo
                    font.family: Theme.uiFont
                    font.pixelSize: 11
                    font.letterSpacing: 2
                    color: Theme.textFaint
                }
            }
            HeartButton {
                visible: Player.currentId > 0
                onColor: Theme.accent
                offColor: Theme.accent2
                on: Library.currentFavorite
                onClicked: Library.toggleCurrentFavorite()
            }
            Row {
                spacing: 8
                NeonButton { iconName: "prev"; onClicked: Player.previous() }
                NeonButton {
                    width: 58; height: 58
                    iconName: Player.playing ? "pause" : "play"
                    big: true
                    onClicked: Player.toggle()
                }
                NeonButton { iconName: "next"; onClicked: Player.next() }
            }
            // time + spectrum + progress
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Text {
                    id: digits
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.topMargin: 12
                    text: window.fmtTime(seekArea.dragFrac >= 0 ? seekArea.dragFrac * Player.duration : Player.position)
                    font.family: "Orbitron"
                    font.pixelSize: 26
                    font.weight: Font.Bold
                    font.letterSpacing: 2
                    color: Theme.accent
                }
                Text {
                    anchors.left: digits.right
                    anchors.leftMargin: 8
                    anchors.baseline: digits.baseline
                    text: "/ " + window.fmtTime(Player.duration)
                    font.family: "Orbitron"
                    font.pixelSize: 13
                    font.letterSpacing: 1
                    color: Theme.textDim
                }
                LedSpectrum {
                    anchors.left: digits.right
                    anchors.leftMargin: 110
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 10
                    height: 46
                    bars: Math.max(12, Math.floor(width / 12))
                    segments: 9
                    low: Theme.accent2
                    high: Theme.accent
                }
                // progress line
                Item {
                    id: seekArea
                    property real dragFrac: -1
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 12
                    height: 24
                    readonly property real frac: Player.duration > 0 ? (dragFrac >= 0 ? dragFrac : Player.position / Player.duration) : 0
                    // neon waveform of the track behind the progress line
                    Waveform {
                        id: neonWave
                        anchors.fill: parent
                        interactive: false
                        showHead: false
                        dragFrac: seekArea.dragFrac
                        style: "mirror"
                        barWidth: 2
                        barGap: 2
                        color1: Theme.accent2
                        color2: Theme.accent
                        dimColor: Theme.alpha(Theme.text, 0.13)
                    }
                    Glow { target: neonWave; visible: skin.glow && neonWave.ready; spread: 1.0; downscale: 3; opacity: 0.6 }
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 2; color: Theme.alpha(Theme.text, 0.15) }
                    Rectangle {
                        id: fillLine
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width * seekArea.frac
                        height: 3
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: Theme.accent2 }
                            GradientStop { position: 1; color: Theme.accent }
                        }
                    }
                    Glow { target: fillLine; visible: skin.glow; spread: 1.0; downscale: 3; opacity: 0.9; height: 14; y: (seekArea.height - 14) / 2 }
                    Rectangle {
                        x: fillLine.width - 5
                        anchors.verticalCenter: parent.verticalCenter
                        width: 10; height: 10
                        rotation: 45
                        color: "#ffffff"
                        border.color: Theme.accent
                        border.width: 2
                    }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        cursorShape: Qt.PointingHandCursor
                        function f(x) { return Math.max(0, Math.min(1, (x - 8) / seekArea.width)) }
                        onPressed: (m) => seekArea.dragFrac = f(m.x)
                        onPositionChanged: (m) => { if (pressed) seekArea.dragFrac = f(m.x) }
                        onReleased: { if (Player.duration > 0) Player.seek(Math.round(seekArea.dragFrac * Player.duration)); seekArea.dragFrac = -1 }
                    }
                }
            }
            Row {
                spacing: 2
                IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: "shuffle"; iconSize: 18; active: Queue.shuffle; activeColor: Theme.accent2; onClicked: Queue.shuffle = !Queue.shuffle }
                IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: Queue.repeat === 2 ? "repeat-one" : "repeat"; iconSize: 18; active: Queue.repeat > 0; activeColor: Theme.accent2; onClicked: Queue.cycleRepeat() }
                VolumeSlider { anchors.verticalCenter: parent.verticalCenter; visible: deck.width > 1180 }
                IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: "queue"; active: window.queueOpen; activeColor: Theme.accent2; onClicked: window.queueOpen = !window.queueOpen }
                IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: "expand"; enabled: Player.currentId > 0; onClicked: window.nowPlayingOpen = true }
            }
        }
    }

    // ---------- now playing ----------
    Item {
        id: nowPlaying
        anchors.fill: parent
        opacity: skin.npMix
        visible: opacity > 0.01
        z: 10
        MouseArea { anchors.fill: parent }

        readonly property real horizonY: height * (0.56 + skin.npMix * 0.06)
        readonly property real coverSize: Math.min(height * 0.36, 380)
        property real lyricsMix: window.lyricsOn ? 1 : 0
        Behavior on lyricsMix { NumberAnimation { duration: 380; easing.type: Easing.InOutCubic } }
        property real bob: 0
        NumberAnimation on bob {
            from: 0; to: Math.PI * 2; duration: 6000; loops: Animation.Infinite
            running: skin.np && window.animationsActive
        }

        IconButton {
            x: 24; y: 22
            iconName: "down"; iconSize: 26; iconColor: "#ffffff"
            onClicked: window.nowPlayingOpen = false
        }
        Brand {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 26
            nameFont: "Orbitron"
            nameSize: 16
            nameWeight: Font.Black
            nameSpacing: 2
            upper: true
            nameColor: "#ffffff"
            signatureColor: Theme.textDim
            linkColor: Theme.accent2
            oneLine: true
        }

        // Title block at the top
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(56, nowPlaying.horizonY - nowPlaying.coverSize - 200)
            width: Math.min(parent.width - 160, 1100)
            spacing: 8
            GlitchLayer {
                id: npTitle
                width: parent.width
                height: npTitleText.height
                enabledFx: skin.glow
                split: 0.003
                Text {
                    id: npTitleText
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: Player.title
                    elide: Text.ElideRight
                    font.family: "Orbitron"
                    font.pixelSize: Math.max(28, Math.min(54, nowPlaying.width / 24))
                    font.weight: Font.Black
                    font.letterSpacing: 4
                    font.capitalization: Font.AllUppercase
                    color: "#ffffff"
                }
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: Player.artist + (Player.album !== "" ? "  //  " + Player.album : "") + (Player.year > 0 ? "  //  " + Player.year : "")
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: 18
                font.letterSpacing: 2
                font.capitalization: Font.AllUppercase
                color: Theme.accent2
            }
        }

        // Floating cover with reflection on the grid
        Item {
            id: monolith
            // wider for the lyrics
            width: nowPlaying.coverSize + (Math.min(nowPlaying.width - 260, nowPlaying.coverSize * 2.4) - nowPlaying.coverSize) * nowPlaying.lyricsMix
            height: nowPlaying.coverSize
            anchors.horizontalCenter: parent.horizontalCenter
            y: nowPlaying.horizonY - height - 26 + Math.sin(nowPlaying.bob) * 6
            HudPanel {
                anchors.fill: parent
                anchors.margins: -8
                chamfer: 16
                fill: Qt.rgba(0.04, 0.0, 0.09, 0.62 * nowPlaying.lyricsMix) // dark glass behind the lyrics
                border: Theme.accent
                bracket: Theme.accent2
                glow: 16
                scan: 0
                borderWidth: 2
            }
            CoverArt {
                id: bigCover
                anchors.centerIn: parent
                width: nowPlaying.coverSize
                height: width
                radius: 0
                requestSize: 1024
                coverId: Player.currentId
                label: Player.album || Player.title
                opacity: 1 - nowPlaying.lyricsMix
            }
            Loader {
                anchors.fill: parent
                anchors.margins: 14
                active: nowPlaying.lyricsMix > 0
                opacity: nowPlaying.lyricsMix
                sourceComponent: LyricsView {
                    align: Text.AlignHCenter
                    focusAt: 0.45
                    fadeEdge: 0.3
                    upper: true
                    fontFamily: Theme.uiFont
                    fontSize: Math.round(Math.max(18, Math.min(30, nowPlaying.coverSize / 12)))
                    color: "#ffffff"
                    dimColor: Theme.alpha(Theme.accent2, 0.5)
                    sourceColor: Theme.textDim
                }
            }
        }
        ShaderEffect {
            visible: skin.canShade && opacity > 0
            width: nowPlaying.coverSize
            height: nowPlaying.coverSize * 0.55
            anchors.horizontalCenter: parent.horizontalCenter
            y: nowPlaying.horizonY + 4 - (Math.sin(nowPlaying.bob) * 6) * 0.3
            property variant source: ShaderEffectSource { sourceItem: bigCover; sourceRect: Qt.rect(0, bigCover.height * 0.45, bigCover.width, bigCover.height * 0.55); live: true }
            property real strength: 0.32
            opacity: 1 - nowPlaying.lyricsMix
            fragmentShader: "qrc:/shaders/reflect.frag.qsb"
        }

        // Controls and LED spectrum on the floor
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 34
            width: Math.min(parent.width - 160, 1000)
            spacing: 14
            LedSpectrum {
                width: parent.width
                height: 64
                bars: 40
                segments: 12
                low: Theme.accent2
                high: Theme.accent
                off: Theme.alpha("#ffffff", 0.05)
            }
            Item {
                width: parent.width
                height: 44
                Text { anchors.verticalCenter: parent.verticalCenter; text: window.fmtTime(Player.position); font.family: "Orbitron"; font.pixelSize: 16; font.weight: Font.Bold; color: Theme.accent }
                Waveform {
                    x: 80; width: parent.width - 160; height: 40
                    anchors.verticalCenter: parent.verticalCenter
                    style: "mirror"
                    barWidth: 3
                    barGap: 2
                    showHead: false
                    color1: Theme.accent2
                    color2: Theme.accent
                    dimColor: Theme.alpha(Theme.text, 0.12)
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 80; width: parent.width - 160; height: 3
                    color: Theme.alpha("#ffffff", 0.15)
                    Rectangle {
                        width: parent.width * (Player.duration > 0 ? Player.position / Player.duration : 0)
                        height: parent.height
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: Theme.accent2 }
                            GradientStop { position: 1; color: Theme.accent }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -10
                        cursorShape: Qt.PointingHandCursor
                        onClicked: (m) => { if (Player.duration > 0) Player.seek(Math.round(Math.max(0, Math.min(1, (m.x - 10) / parent.width)) * Player.duration)) }
                    }
                }
                Text { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: window.fmtTime(Player.duration); font.family: "Orbitron"; font.pixelSize: 16; font.weight: Font.Bold; color: Theme.textDim }
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 16
                IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: "shuffle"; iconSize: 20; active: Queue.shuffle; activeColor: Theme.accent2; onClicked: Queue.shuffle = !Queue.shuffle }
                NeonButton { anchors.verticalCenter: parent.verticalCenter; iconName: "prev"; onClicked: Player.previous() }
                NeonButton { anchors.verticalCenter: parent.verticalCenter; width: 70; height: 70; big: true; iconName: Player.playing ? "pause" : "play"; onClicked: Player.toggle() }
                NeonButton { anchors.verticalCenter: parent.verticalCenter; iconName: "next"; onClicked: Player.next() }
                IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: Queue.repeat === 2 ? "repeat-one" : "repeat"; iconSize: 20; active: Queue.repeat > 0; activeColor: Theme.accent2; onClicked: Queue.cycleRepeat() }
                IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: "lyrics"; iconSize: 20; active: Lyrics.shown; activeColor: Theme.accent2; opacity: window.lyricsMissing ? 0.5 : 1; tip: window.lyricsTip; onClicked: window.toggleLyrics() }
                HeartButton { anchors.verticalCenter: parent.verticalCenter; size: 36; onColor: Theme.accent; offColor: Theme.accent2; on: Library.currentFavorite; onClicked: Library.toggleCurrentFavorite() }
                Item { width: 10; height: 1 }
                VolumeSlider { anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }

    // Neon-outlined square button.
    component NeonButton: AbstractButton {
        id: nb
        property string iconName
        property bool big: false
        width: 46
        height: 46
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        HudPanel {
            anchors.fill: parent
            chamfer: nb.big ? 12 : 8
            fill: nb.big ? Theme.alpha(Theme.accent, nb.pressed ? 0.6 : (nb.hovered ? 0.42 : 0.28)) : Theme.alpha(Theme.accent, nb.hovered ? 0.18 : 0.06)
            border: nb.big ? Theme.accent : Theme.alpha(Theme.accent2, nb.hovered ? 1 : 0.65)
            bracket: "#ffffff"
            glow: nb.big ? 12 + skin.beat * 10 : (nb.hovered ? 9 : 5)
            scan: 0
            borderWidth: nb.big ? 2 : 1.5
            margin: 22
        }
        background: null
        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                name: nb.iconName
                size: nb.big ? Math.round(nb.width * 0.44) : 20
                color: "#ffffff"
            }
        }
    }
}
