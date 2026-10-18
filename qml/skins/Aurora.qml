import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Halcyon
import ".."

// Skin "Aurora": flowing, music-reactive light in the cover's colours behind
// floating panels of real frosted glass, a glass dock, and a big spinning-disc
// now playing view with a radial spectrum.
Item {
    id: skin
    property alias stack: stack
    readonly property bool animated: true
    readonly property bool wantsLevels: true
    readonly property int toastMargin: dock.height + gap * 2 + 10
    function focusSearch() { sidebar.focusSearch() }
    function clearSearch() { sidebar.clearSearch() }
    function setSearchText(t) { sidebar.searchText = t }

    readonly property int gap: 14
    readonly property real corner: Theme.opt.corner !== undefined ? Theme.opt.corner : 26
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software
    readonly property bool np: window.nowPlayingOpen

    // Music reactivity (0..1), scaled by the user's setting.
    readonly property real react: Theme.opt.reactive !== undefined ? Theme.opt.reactive : 0.6
    // Smoothed on purpose: the light swells with the music but never flashes.
    property real energy: Player.playing ? Player.energy * react : 0
    Behavior on energy { NumberAnimation { duration: 900; easing.type: Easing.InOutSine } }
    property real beat: Player.playing ? Player.beat * react * 0.6 : 0
    Behavior on beat { NumberAnimation { duration: 700; easing.type: Easing.OutSine } }

    // Full-screen view blends in over ~0.7 s; the background brightens with it.
    property real npMix: np ? 1 : 0
    Behavior on npMix { NumberAnimation { duration: 650; easing.type: Easing.InOutCubic } }

    // Time for the flowing light: integrates speed so changing it never jumps.
    property real flow: 0
    property real lastFx: 0
    Connections {
        target: window
        function onFxTimeChanged() {
            var dt = window.fxTime - skin.lastFx
            skin.lastFx = window.fxTime
            if (dt > 0 && dt < 0.5)
                skin.flow += dt * (Theme.opt.speed !== undefined ? Theme.opt.speed : 0.6) * (1 + skin.energy * 0.8)
        }
    }

    // Mouse for the light reflex on the glass.
    property point mouse: Qt.point(-9999, -9999)
    HoverHandler {
        id: hover
        onPointChanged: if (Theme.opt.reflex) skin.mouse = point.position
    }

    // ---------- colours of the light ----------
    function vivid(c, light) {
        var h = c.hslHue < 0 ? 0.6 : c.hslHue
        if (light)
            return Qt.hsla(h, Math.max(0.5, c.hslSaturation), 0.72, 1)
        return Qt.hsla(h, Math.max(0.55, Math.min(0.95, c.hslSaturation * 1.1)), Math.min(0.62, Math.max(0.45, c.hslLightness)), 1)
    }
    readonly property var lights: {
        var mode = Theme.opt.lights || "cover"
        var light = !Theme.dark
        var presets = {
            aurora: ["#19E6A0", "#3D7BFF", "#9B4DFF", "#050816"],
            sunset: ["#FF7A3D", "#FF3D7F", "#FFC94D", "#14050E"],
            ocean:  ["#1DE9B6", "#0091EA", "#304FFE", "#020814"],
            candy:  ["#FF5EDF", "#7B5CFF", "#4DE1FF", "#0E0618"]
        }
        var src = presets[mode]
        var cols
        if (mode === "cover" && Theme.coverPalette.length >= 3) {
            var cp = Theme.coverPalette
            cols = [vivid(cp[0], light), vivid(cp[1], light), vivid(cp[2], light),
                    Qt.hsla(cp[0].hslHue < 0 ? 0.7 : cp[0].hslHue, 0.45, 0.05, 1)]
        } else {
            if (!src) src = presets.aurora
            cols = [Qt.color(src[0]), Qt.color(src[1]), Qt.color(src[2]), Qt.color(src[3])]
            if (light)
                cols = [Qt.lighter(cols[0], 1.35), Qt.lighter(cols[1], 1.45), Qt.lighter(cols[2], 1.45), cols[3]]
        }
        if (light)
            cols[3] = Theme.bg
        return cols
    }

    // With preset light colours, the accent follows the light instead of the cover.
    Binding {
        target: Theme
        property: "accentOverride"
        value: (Theme.opt.lights || "cover") === "cover" ? Qt.rgba(0, 0, 0, 0) : skin.lights[0]
    }

    // Iridescent glass edges, tinted by the light.
    readonly property color rimA: Theme.mix("#ffffff", lights[0], 0.55)
    readonly property color rimB: Theme.mix("#ffffff", lights[1], 0.55)

    // ---------- background ----------
    Rectangle {
        anchors.fill: parent
        color: skin.lights[3]
        Behavior on color { ColorAnimation { duration: 1200 } }
    }

    ShaderEffect {
        id: aurora
        // Low-resolution render target, stretched by `backdrop` below.
        width: 256
        height: Math.max(64, Math.round(256 * skin.height / Math.max(1, skin.width)))
        visible: skin.canShade
        property real time: skin.flow
        property real aspect: skin.width / Math.max(1, skin.height)
        property real intensity: Theme.opt.intensity !== undefined ? Theme.opt.intensity : 0.75
        property real energy: skin.energy
        property real beat: skin.beat
        property real bokeh: Theme.opt.bokeh ? 1 : 0
        property real boost: skin.npMix
        property color c1: skin.lights[0]
        property color c2: skin.lights[1]
        property color c3: skin.lights[2]
        property color base: skin.lights[3]
        Behavior on c1 { ColorAnimation { duration: 1400 } }
        Behavior on c2 { ColorAnimation { duration: 1400 } }
        Behavior on c3 { ColorAnimation { duration: 1400 } }
        Behavior on base { ColorAnimation { duration: 1400 } }
        fragmentShader: "qrc:/shaders/aurora.frag.qsb"
    }
    ShaderEffectSource {
        id: backdrop
        anchors.fill: parent
        visible: skin.canShade
        sourceItem: aurora
        hideSource: true
        live: true
        smooth: true
    }
    // Software renderer: a calm gradient instead of the shader.
    Rectangle {
        anchors.fill: parent
        visible: !skin.canShade
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.darker(skin.lights[0], 2.2) }
            GradientStop { position: 1; color: skin.lights[3] }
        }
    }

    // ---------- library: nav, content, queue ----------
    Item {
        id: frame
        anchors.fill: parent
        anchors.margins: skin.gap
        anchors.bottomMargin: dock.height + skin.gap * 2
        opacity: 1 - skin.npMix
        scale: 1 - skin.npMix * 0.04
        visible: opacity > 0.01

        GlassPanel {
            id: navPanel
            width: 268
            height: parent.height
            backdrop: backdrop
            ref: skin
            light: skin.mouse
            radius: skin.corner
            blur: Theme.opt.blur
            tintAmount: Theme.opt.tint
            reflex: Theme.opt.reflex
            tint: Theme.panel
            rim1: skin.rimA
            rim2: skin.rimB
            Sidebar {
                id: sidebar
                anchors.fill: parent
                anchors.topMargin: 10
                anchors.bottomMargin: 6
                transparent: true
                currentId: window.currentViewId
                onOpen: (v) => window.openView(v)
                onSearchRequested: (t) => window.search(t)
            }
        }

        GlassPanel {
            id: contentPanel
            anchors.left: navPanel.right
            anchors.leftMargin: skin.gap
            anchors.right: queueGlass.left
            anchors.rightMargin: queueGlass.width > 1 ? skin.gap : 0
            height: parent.height
            backdrop: backdrop
            ref: skin
            light: skin.mouse
            radius: skin.corner
            blur: Theme.opt.blur
            tintAmount: Math.min(0.92, (Theme.opt.tint || 0.45) + 0.12)
            reflex: Theme.opt.reflex
            tint: Theme.panel
            rim1: skin.rimA
            rim2: skin.rimB
            ContentStack {
                id: stack
                anchors.fill: parent
                anchors.margins: Math.round(skin.corner * 0.3)
            }
        }

        GlassPanel {
            id: queueGlass
            anchors.right: parent.right
            width: window.queueOpen ? 340 : 0
            visible: width > 1
            height: parent.height
            backdrop: backdrop
            ref: skin
            light: skin.mouse
            radius: skin.corner
            blur: Theme.opt.blur
            tintAmount: Theme.opt.tint
            reflex: Theme.opt.reflex
            tint: Theme.panel
            rim1: skin.rimA
            rim2: skin.rimB
            Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
            QueuePanel {
                anchors.fill: parent
                anchors.margins: 6
                transparent: true
                clip: true
            }
        }
    }

    // ---------- the dock ----------
    GlassPanel {
        id: dock
        height: 96
        width: Math.min(skin.width - skin.gap * 2, 1240)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: skin.gap - skin.npMix * (height + 40)
        opacity: 1 - skin.npMix
        visible: opacity > 0.01
        backdrop: backdrop
        ref: skin
        light: skin.mouse
        radius: Math.min(height / 2, skin.corner + 12)
        blur: Theme.opt.blur
        tintAmount: Math.min(0.92, (Theme.opt.tint || 0.45) + 0.08)
        reflex: Theme.opt.reflex
        tint: Theme.panel
        rim1: skin.rimA
        rim2: skin.rimB
        rim: 0.85

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 22
            spacing: 14

            // cover with progress ring
            Item {
                Layout.preferredWidth: 74
                Layout.preferredHeight: 74
                ProgressRing {
                    anchors.fill: parent
                    progress: Player.duration > 0 ? Player.position / Player.duration : 0
                    thickness: 0.07
                    glow: 0.05
                }
                CoverArt {
                    anchors.centerIn: parent
                    width: 58; height: 58
                    radius: 29
                    requestSize: 128
                    coverId: Player.currentId
                    label: Player.album || Player.title
                    showInitial: Player.currentId > 0
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Player.currentId > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: if (Player.currentId > 0) window.nowPlayingOpen = true
                }
            }
            Column {
                Layout.preferredWidth: Math.min(240, dock.width * 0.2)
                spacing: 3
                Text {
                    width: parent.width
                    text: Player.currentId > 0 ? Player.title : qsTr("Bereit")
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 15
                    font.weight: Font.Bold
                    color: Theme.text
                }
                Text {
                    width: parent.width
                    text: Player.currentId > 0 ? Player.artist : qsTr("Wähle Musik aus der Bibliothek")
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: Theme.textDim
                }
            }
            Row {
                spacing: 6
                IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: "prev"; iconSize: 22; iconColor: Theme.text; tip: qsTr("Zurück"); onClicked: Player.previous() }
                PlayOrb { width: 58; height: 58 }
                IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: "next"; iconSize: 22; iconColor: Theme.text; tip: qsTr("Weiter"); onClicked: Player.next() }
            }
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                Text {
                    id: pos
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    text: window.fmtTime(ribbon.dragFrac >= 0 ? ribbon.dragFrac * Player.duration : Player.position)
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: Theme.textDim
                }
                SeekRibbon {
                    id: ribbon
                    anchors.left: pos.right
                    anchors.right: dur.left
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    height: 30
                    bars: Math.max(24, Math.floor(width / 7))
                    waveColor1: Theme.accent2
                }
                Text {
                    id: dur
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    horizontalAlignment: Text.AlignRight
                    text: window.fmtTime(Player.duration)
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: Theme.textDim
                }
            }
            Row {
                spacing: 2
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "shuffle"; iconSize: 18; active: Queue.shuffle
                    tip: Queue.shuffle ? qsTr("Zufallswiedergabe aus") : qsTr("Zufallswiedergabe an")
                    onClicked: Queue.shuffle = !Queue.shuffle
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: Queue.repeat === 2 ? "repeat-one" : "repeat"; iconSize: 18; active: Queue.repeat > 0
                    tip: qsTr("Wiederholen")
                    onClicked: Queue.cycleRepeat()
                }
                VolumeSlider { anchors.verticalCenter: parent.verticalCenter; visible: dock.width > 1060 }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "queue"; active: window.queueOpen
                    tip: qsTr("Warteschlange")
                    onClicked: window.queueOpen = !window.queueOpen
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "expand"
                    tip: qsTr("Großansicht")
                    enabled: Player.currentId > 0
                    onClicked: window.nowPlayingOpen = true
                }
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

        MouseArea { anchors.fill: parent; hoverEnabled: false } // keep clicks away from the library

        readonly property real disc: Math.min(height * 0.56, width * 0.36)

        IconButton {
            x: 24; y: 22
            iconName: "down"; iconSize: 26; iconColor: Theme.text
            tip: qsTr("Schließen (Esc)")
            onClicked: window.nowPlayingOpen = false
        }
        Brand {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 26
            nameSize: 18
            signatureSize: 10
            oneLine: true
            showMark: true
        }

        // --- the disc ---
        Item {
            id: discArea
            width: nowPlaying.disc
            height: width
            x: Math.max(60, nowPlaying.width * 0.28 - width / 2)
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -10

            // tilt towards the mouse
            readonly property real tx: skin.np && skin.mouse.x > -9000 ? (skin.mouse.y - (y + height / 2)) / skin.height * -10 : 0
            readonly property real ty: skin.np && skin.mouse.x > -9000 ? (skin.mouse.x - (x + width / 2)) / skin.width * 14 : 0
            transform: [
                Rotation { origin.x: discArea.width / 2; origin.y: discArea.height / 2; axis { x: 1; y: 0; z: 0 } angle: discArea.tx
                    Behavior on angle { NumberAnimation { duration: 300 } } },
                Rotation { origin.x: discArea.width / 2; origin.y: discArea.height / 2; axis { x: 0; y: 1; z: 0 } angle: discArea.ty
                    Behavior on angle { NumberAnimation { duration: 300 } } }
            ]

            SoftGlow {
                anchors.centerIn: parent
                width: parent.width * (1.75 + skin.beat * 0.25)
                height: width
                color: Theme.alpha(skin.lights[0], 0.5)
            }

            // radial spectrum
            Item {
                id: ring
                anchors.fill: parent
                readonly property var spec: Player.spectrum
                readonly property int n: 72
                Repeater {
                    model: ring.n
                    Rectangle {
                        readonly property real a: index / ring.n
                        readonly property int band: Math.floor((index < ring.n / 2 ? index : ring.n - 1 - index) * 2 * Math.max(1, ring.spec.length) / ring.n)
                        readonly property real level: ring.spec.length > 0 ? ring.spec[Math.min(ring.spec.length - 1, band)] : 0
                        width: Math.max(3, discArea.width * 0.012)
                        height: 6 + level * discArea.width * 0.2
                        radius: width / 2
                        x: discArea.width / 2 - width / 2
                        y: -height - discArea.width * 0.045
                        color: Theme.mix(skin.lights[0], skin.lights[1], Math.abs(a - 0.5) * 2)
                        opacity: 0.9
                        transform: Rotation { origin.x: width / 2; origin.y: height + discArea.width * 0.545; angle: a * 360 }
                    }
                }
            }

            CoverArt {
                id: discCover
                anchors.fill: parent
                radius: width / 2
                requestSize: 1024
                coverId: Player.currentId
                label: Player.album || Player.title
                RotationAnimator on rotation {
                    from: 0; to: 360
                    duration: 24000
                    loops: Animation.Infinite
                    running: skin.np && Player.playing && Theme.opt.disc === true && window.animationsActive
                }
            }
            // spindle
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.12; height: width; radius: width / 2
                color: Theme.alpha(skin.lights[3], 0.85)
                border.color: Theme.alpha("#ffffff", 0.35)
                border.width: 2
            }
            Rectangle {
                anchors.centerIn: parent
                width: parent.width; height: width; radius: width / 2
                color: "transparent"
                border.color: Theme.alpha("#ffffff", 0.22)
                border.width: 1.5
            }
        }

        // --- info card ---
        GlassPanel {
            id: infoCard
            anchors.left: discArea.right
            anchors.leftMargin: Math.max(48, nowPlaying.width * 0.06)
            anchors.right: parent.right
            anchors.rightMargin: 56
            anchors.verticalCenter: parent.verticalCenter
            height: info.implicitHeight + 64
            backdrop: backdrop
            ref: skin
            light: skin.mouse
            radius: skin.corner + 6
            blur: Theme.opt.blur
            tintAmount: Math.max(0.2, (Theme.opt.tint || 0.45) - 0.1)
            reflex: Theme.opt.reflex
            tint: Theme.panel
            rim1: skin.rimA
            rim2: skin.rimB

            Column {
                id: info
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 34
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                Text {
                    width: parent.width
                    text: Player.title
                    font.family: Theme.displayFont
                    font.pixelSize: Math.max(30, Math.min(52, infoCard.width / 13))
                    font.weight: Font.ExtraBold
                    font.letterSpacing: -0.5
                    lineHeight: 1.0
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
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                    color: Theme.accent
                }
                Row {
                    spacing: 10
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Player.album + (Player.year > 0 ? ", " + Player.year : "")
                        font.family: Theme.uiFont
                        font.pixelSize: 15
                        color: Theme.textDim
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, info.width - fmtChip.width - 12)
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: window.openCurrentAlbum() }
                    }
                    Rectangle {
                        id: fmtChip
                        visible: Player.format !== ""
                        anchors.verticalCenter: parent.verticalCenter
                        height: 24
                        width: chipText.implicitWidth + 18
                        radius: 12
                        color: Theme.alpha(Theme.text, 0.1)
                        Text {
                            id: chipText
                            anchors.centerIn: parent
                            text: Player.format
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                            font.weight: Font.Bold
                            color: Theme.text
                        }
                    }
                }
                Item { width: 1; height: 14 }
                SeekRibbon {
                    width: parent.width
                    height: 46
                    bars: Math.max(32, Math.floor(width / 9))
                    barGap: 3
                    waveColor1: Theme.accent2
                }
                Item {
                    width: parent.width
                    height: 18
                    Text { text: window.fmtTime(Player.position); font.family: Theme.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.textDim }
                    Text { anchors.right: parent.right; text: window.fmtTime(Player.duration); font.family: Theme.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.textDim }
                }
                Item {
                    width: parent.width
                    height: 92
                    Row {
                        anchors.centerIn: parent
                        spacing: 18
                        IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: "shuffle"; iconSize: 20; active: Queue.shuffle; onClicked: Queue.shuffle = !Queue.shuffle }
                        IconButton { anchors.verticalCenter: parent.verticalCenter; width: 48; height: 48; iconName: "prev"; iconSize: 28; iconColor: Theme.text; onClicked: Player.previous() }
                        PlayOrb { width: 78; height: 78 }
                        IconButton { anchors.verticalCenter: parent.verticalCenter; width: 48; height: 48; iconName: "next"; iconSize: 28; iconColor: Theme.text; onClicked: Player.next() }
                        IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: Queue.repeat === 2 ? "repeat-one" : "repeat"; iconSize: 20; active: Queue.repeat > 0; onClicked: Queue.cycleRepeat() }
                    }
                }
                Row {
                    spacing: 14
                    VolumeSlider { anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: Queue.upNext !== ""
                        width: Math.max(0, info.width - 180)
                        elide: Text.ElideRight
                        text: qsTr("Als Nächstes: %1").arg(Queue.upNext)
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        color: Theme.textDim
                    }
                }
            }
        }
    }

    // Film grain over everything: hides colour banding in the gradients.
    Image {
        anchors.fill: parent
        visible: Theme.opt.grain === true
        source: "image://icon/noise/128"
        fillMode: Image.Tile
        opacity: Theme.dark ? 0.035 : 0.05
        smooth: false
        z: 50
    }
}
