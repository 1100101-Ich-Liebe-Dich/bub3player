// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Halcyon
import ".."

// Skin "Vintage": a 70s living room. Wallpaper on the wall, furniture in wood:
// a record shelf for the library, a turntable, the sleeve of what is playing,
// and a stereo receiver along the bottom with a backlit tuning dial (the dial
// pointer is the playback position), bat toggles, piano keys, VU meters and
// knurled knobs. The "PHONO" key opens the big turntable view.
Item {
    id: skin
    property alias stack: stack
    readonly property bool animated: false
    readonly property bool wantsLevels: true
    readonly property int toastMargin: receiver.height + 40
    function focusSearch() { sidebar.focusSearch() }
    function clearSearch() { sidebar.clearSearch() }
    function setSearchText(t) { sidebar.searchText = t }

    readonly property int gap: 18
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software
    readonly property var pal: Theme.paletteData
    readonly property string wood: pal.wood || "walnut"
    readonly property var wall: pal.wall || ["#D2691E", "#5A3218", "#E8D3A6", "#C8962E"]
    readonly property string face: Theme.opt.faceplate || "aluminium"
    readonly property bool darkFace: face === "blackmetal"
    readonly property color engrave: darkFace ? "#E2DACB" : "#2A231B"
    readonly property color engraveSoft: darkFace ? "#9C9486" : "#5C5142"
    readonly property color dialLight: Theme.opt.dial === "blue" ? "#5FC8F0" : Theme.opt.dial === "green" ? "#7DE38A" : "#FFB45A"
    readonly property real rpm: Theme.opt.rpm === "45" ? 45 : 33.333
    // Lower right: the queue by default; the COVER key shows the record sleeve instead.
    readonly property bool showCover: Theme.opt.cover === true

    property real npMix: window.nowPlayingOpen ? 1 : 0
    Behavior on npMix { NumberAnimation { duration: 520; easing.type: Easing.InOutCubic } }

    // ---------- the wall ----------
    Rectangle { anchors.fill: parent; color: Theme.bg }
    ShaderEffect {
        anchors.fill: parent
        visible: skin.canShade
        property size itemSize: Qt.size(width, height)
        property real pattern: ({ circles: 0, waves: 1, panels: 2, flowers: 3, plain: 4 })[Theme.opt.wallpaper || "circles"]
        property real tile: Theme.opt.wallpaper === "waves" ? 120 : Theme.opt.wallpaper === "flowers" ? 150 : 132
        property real dim: 1 - (Theme.opt.room !== undefined ? Theme.opt.room : 0.6)
        property color c1: skin.wall[0]
        property color c2: skin.wall[1]
        property color c3: skin.wall[2]
        property color c4: skin.wall[3]
        fragmentShader: "qrc:/shaders/wallpaper.frag.qsb"
        layer.enabled: true
    }

    // ---------- upper area: library furniture ----------
    Item {
        id: upper
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: receiver.top
        anchors.margins: skin.gap
        anchors.bottomMargin: skin.gap

        Item {
            id: library
            anchors.fill: parent
            opacity: 1 - skin.npMix
            visible: opacity > 0.01

            // nav: a wooden panel with a tolex inlay
            Cabinet {
                id: navCab
                width: 256
                height: parent.height
                Sidebar {
                    id: sidebar
                    anchors.fill: parent
                    anchors.topMargin: 6
                    transparent: true
                    currentId: window.currentViewId
                    onOpen: (v) => window.openView(v)
                    onSearchRequested: (t) => window.search(t)
                }
            }
            // content: the record shelf
            Cabinet {
                id: contentCab
                anchors.left: navCab.right
                anchors.leftMargin: skin.gap
                anchors.right: rightCol.left
                anchors.rightMargin: skin.gap
                height: parent.height
                inlay: "bg"
                ContentStack {
                    id: stack
                    anchors.fill: parent
                    anchors.margins: 6
                }
            }
            // turntable and sleeve / queue
            Item {
                id: rightCol
                anchors.right: parent.right
                width: skin.width > 1500 ? 380 : 340
                height: parent.height
                VtPhono {
                    id: phono
                    width: parent.width
                    height: Math.min(parent.height * 0.52, width * 0.82)
                    wood: skin.wood
                    plate: skin.face
                    rpm: skin.rpm
                    followTrack: Theme.opt.tonearm !== false
                    showControls: false
                    onSpeedRequested: (r) => Theme.setOption("rpm", r)
                }
                Cabinet {
                    anchors.top: phono.bottom
                    anchors.topMargin: skin.gap
                    anchors.bottom: parent.bottom
                    width: parent.width
                    visible: height > 80
                    QueuePanel {
                        anchors.fill: parent
                        anchors.margins: 2
                        transparent: true
                        clip: true
                        visible: !skin.showCover
                    }
                    Item {
                        anchors.fill: parent
                        anchors.margins: 14
                        visible: skin.showCover
                        VtSleeve {
                            id: smallSleeve
                            width: parent.width
                            height: Math.max(40, parent.height - info.height - 12)
                            coverId: Player.currentId
                            label: Player.album || Player.title
                            peek: 0.28
                            inner: true
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Player.hasTrack ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: if (Player.hasTrack) window.nowPlayingOpen = true
                            }
                        }
                        Column {
                            id: info
                            anchors.bottom: parent.bottom
                            width: parent.width
                            spacing: 2
                            Text {
                                width: parent.width
                                text: Player.hasTrack ? Player.title : qsTr("Keine Platte aufgelegt")
                                elide: Text.ElideRight
                                font.family: Theme.displayFont
                                font.pixelSize: 19
                                color: Theme.text
                            }
                            Text {
                                width: parent.width
                                text: Player.hasTrack ? Player.artist + (Player.album !== "" ? " · " + Player.album : "") : qsTr("Wähle Musik aus der Bibliothek")
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: Theme.textDim
                            }
                        }
                    }
                }
            }
        }

        // ---------- big turntable view ----------
        Item {
            id: bigView
            anchors.fill: parent
            opacity: skin.npMix
            visible: opacity > 0.01
            MouseArea { anchors.fill: parent }

            VtPhono {
                id: bigPhono
                height: parent.height
                width: Math.min(parent.width * 0.58, height * 1.32)
                y: (1 - skin.npMix) * 40
                wood: skin.wood
                plate: skin.face
                rpm: skin.rpm
                followTrack: Theme.opt.tonearm !== false
                onSpeedRequested: (r) => Theme.setOption("rpm", r)
            }
            Item {
                id: npRight
                anchors.left: bigPhono.right
                anchors.leftMargin: skin.gap * 2
                anchors.right: parent.right
                height: parent.height
                VtSleeve {
                    id: bigSleeve
                    width: parent.width
                    height: Math.min(parent.height * 0.6, parent.width / 1.14)
                    rotation: -2
                    coverId: Player.currentId
                    label: Player.album || Player.title
                    peek: 0.14
                    inner: true
                }
                // the lyric insert, taken out of the sleeve and laid on top
                Rectangle {
                    id: lyricSheet
                    property real mix: window.lyricsOn ? 1 : 0
                    Behavior on mix { NumberAnimation { duration: 360; easing.type: Easing.OutCubic } }
                    visible: mix > 0
                    opacity: mix
                    x: bigSleeve.side * 0.1
                    y: (bigSleeve.height - bigSleeve.side) / 2 + (1 - mix) * 50
                    width: bigSleeve.side * 0.96
                    height: bigSleeve.side
                    rotation: 1.6
                    radius: 2
                    color: "#F1E8D2"
                    border.color: Qt.rgba(0, 0, 0, 0.22)
                    Rectangle { anchors.fill: parent; anchors.topMargin: 6; anchors.leftMargin: 4; anchors.rightMargin: -6; anchors.bottomMargin: -7; z: -1; radius: 2; color: Qt.rgba(0, 0, 0, 0.45) }
                    Text {
                        id: sheetHead
                        x: 22; y: 16
                        text: qsTr("LIEDTEXT")
                        font.family: "Barlow Condensed"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 2
                        color: "#7a2f14"
                    }
                    Loader {
                        anchors.fill: parent
                        anchors.topMargin: 38
                        anchors.leftMargin: 22
                        anchors.rightMargin: 22
                        anchors.bottomMargin: 14
                        active: lyricSheet.visible
                        sourceComponent: LyricsView {
                            fontFamily: "Barlow"
                            fontSize: Math.round(Math.max(17, Math.min(26, bigSleeve.side / 19)))
                            fontWeight: Font.DemiBold
                            color: "#2a1c10"
                            dimColor: Qt.rgba(0.35, 0.25, 0.15, 0.45)
                            sourceColor: "#6b5d48"
                        }
                    }
                }
                // liner notes: the back of the sleeve, on cream paper
                Rectangle {
                    id: notes
                    anchors.top: bigSleeve.bottom
                    anchors.topMargin: skin.gap
                    anchors.bottom: parent.bottom
                    width: Math.min(parent.width, bigSleeve.side * 1.08)
                    rotation: 1.2
                    radius: 2
                    color: "#EDE3CC"
                    border.color: Qt.rgba(0, 0, 0, 0.25)
                    Rectangle { anchors.fill: parent; anchors.topMargin: 5; anchors.leftMargin: 3; anchors.rightMargin: -5; anchors.bottomMargin: -6; z: -1; radius: 2; color: Qt.rgba(0, 0, 0, 0.4) }
                    Column {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 4
                        clip: true
                        Row {
                            spacing: 10
                            Text { text: qsTr("SEITE A"); font.family: "Barlow Condensed"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 2; color: "#7a2f14" }
                            Text { text: "·  33⅓ U/min  ·  STEREO"; font.family: "Barlow Condensed"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 1.5; color: "#6b5d48" }
                        }
                        Item {
                            width: parent.width
                            height: Math.max(notesTitle.height, notesHeart.height + 2)
                            Text {
                                id: notesTitle
                                width: parent.width - notesHeart.width - 6
                                text: Player.title
                                font.family: "Righteous"
                                font.pixelSize: Math.max(20, Math.min(36, notes.width / 13))
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                color: "#2a1c10"
                            }
                            // a heart drawn on the paper
                            HeartButton {
                                id: notesHeart
                                visible: Player.currentId > 0
                                anchors.right: parent.right
                                y: 2
                                size: 34
                                onColor: "#a3321a"
                                offColor: "#6b5d48"
                                on: Library.currentFavorite
                                onClicked: Library.toggleCurrentFavorite()
                            }
                        }
                        Text {
                            width: parent.width
                            text: Player.artist
                            elide: Text.ElideRight
                            font.family: "Barlow"
                            font.pixelSize: 17
                            font.weight: Font.Bold
                            color: "#5a3a1c"
                        }
                        Text {
                            width: parent.width
                            text: Player.album + (Player.year > 0 ? " (" + Player.year + ")" : "") + (Player.format !== "" ? "  ·  " + Player.formatInfo : "")
                            elide: Text.ElideRight
                            font.family: "Barlow"
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: "#6b5d48"
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: window.openCurrentAlbum() }
                        }
                        Item { width: 1; height: 6 }
                        Rectangle { width: parent.width; height: 1; color: Qt.rgba(0.35, 0.25, 0.15, 0.35) }
                        Text {
                            width: parent.width
                            visible: Queue.upNext !== ""
                            text: qsTr("Danach: %1").arg(Queue.upNext)
                            elide: Text.ElideRight
                            font.family: "Barlow"
                            font.pixelSize: 13
                            font.italic: true
                            color: "#6b5d48"
                        }
                        Text {
                            width: parent.width
                            text: window.fmtTime(Player.position) + " / " + window.durationText
                            font.family: "Barlow Condensed"
                            font.pixelSize: 15
                            font.weight: Font.Bold
                            color: "#2a1c10"
                        }
                    }
                }
            }
        }
    }

    // ---------- the receiver ----------
    Item {
        id: receiver
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: skin.gap
        height: 204

        Rectangle { anchors.fill: parent; anchors.topMargin: 8; anchors.bottomMargin: -6; radius: 12; color: Qt.rgba(0, 0, 0, 0.5) }
        Material { anchors.fill: parent; material: skin.wood; radius: 10; seed: 1 }
        // little feet
        Repeater {
            model: 2
            Rectangle { y: receiver.height - 2; x: index === 0 ? 40 : receiver.width - 100; width: 60; height: 6; radius: 2; color: "#0d0a08" }
        }
        Material {
            id: fp
            anchors.fill: parent
            anchors.leftMargin: 28
            anchors.rightMargin: 28
            anchors.topMargin: 13
            anchors.bottomMargin: 13
            material: skin.face
            radius: 3
            seed: 2
        }
        Rectangle { anchors.fill: fp; anchors.margins: -1; radius: 4; color: "transparent"; border.color: Qt.rgba(0, 0, 0, 0.6) }

        RowLayout {
            anchors.fill: fp
            anchors.margins: 14
            anchors.leftMargin: 18
            anchors.rightMargin: 22
            spacing: 14

            // brand plate
            Item {
                Layout.preferredWidth: 168
                Layout.fillHeight: true
                visible: fp.width > 1180
                Column {
                    spacing: 2
                    Text {
                        text: "BuB3Player"
                        font.family: "Righteous"
                        font.pixelSize: 27
                        color: skin.engrave
                        style: Text.Raised
                        styleColor: skin.darkFace ? Qt.rgba(0, 0, 0, 0.7) : Qt.rgba(1, 1, 1, 0.55)
                    }
                    Text {
                        text: "STEREO RECEIVER  ·  MODEL 1975"
                        font.family: "Barlow Condensed"
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        font.letterSpacing: 1.5
                        color: skin.engraveSoft
                    }
                    Item { width: 1; height: 6 }
                    Text {
                        text: "Projekt präsentiert von"
                        font.family: "Barlow Condensed"
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        color: skin.engraveSoft
                    }
                    Text {
                        text: "www.BAUENUNDBOOTEN.de"
                        font.family: "Barlow Condensed"
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        font.letterSpacing: 0.5
                        color: skin.engrave
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally("https://www.bauenundbooten.de") }
                    }
                }
                // power lamp and headphone jack
                Row {
                    anchors.bottom: parent.bottom
                    spacing: 16
                    Column {
                        spacing: 3
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 12; height: 12; radius: 6
                            color: "#ff5a2a"
                            border.color: Qt.rgba(0, 0, 0, 0.5)
                            Rectangle { x: 3; y: 2; width: 4; height: 4; radius: 2; color: Qt.rgba(1, 1, 1, 0.75) }
                            SoftGlow { anchors.centerIn: parent; width: 38; height: 38; color: Qt.rgba(1, 0.35, 0.15, 0.45) }
                        }
                        Text { text: "POWER"; font.family: "Barlow Condensed"; font.pixelSize: 9; font.weight: Font.Bold; font.letterSpacing: 1; color: skin.engrave }
                    }
                    Column {
                        spacing: 3
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 20; height: 20; radius: 10
                            gradient: Gradient {
                                GradientStop { position: 0; color: "#f0f1f3" }
                                GradientStop { position: 1; color: "#6f7277" }
                            }
                            Rectangle { anchors.centerIn: parent; width: 9; height: 9; radius: 4.5; color: "#050505" }
                        }
                        Text { text: "PHONES"; font.family: "Barlow Condensed"; font.pixelSize: 9; font.weight: Font.Bold; font.letterSpacing: 1; color: skin.engrave }
                    }
                }
            }

            // dial and the switch row
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10
                VtDial {
                    id: dialItem
                    Layout.fillWidth: true
                    Layout.preferredHeight: 92
                    light: skin.dialLight
                    warmup: Theme.opt.warmup !== false
                    previewFrac: tuning.preview
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 6
                    VtToggle { label: "SHUFFLE"; engrave: skin.engrave; on: Queue.shuffle; onClicked: Queue.shuffle = !Queue.shuffle }
                    VtToggle { label: Queue.repeat === 2 ? "REPEAT 1" : "REPEAT"; engrave: skin.engrave; on: Queue.repeat > 0; lamp: true; lampColor: Queue.repeat === 2 ? "#ffd04a" : "#ff4a2a"; onClicked: Queue.cycleRepeat() }
                    VtToggle {
                        visible: Player.replayGainAvailable
                        label: "AUTO GAIN"; engrave: skin.engrave
                        on: Player.replayGain > 0
                        onClicked: Player.replayGain = Player.replayGain > 0 ? 0 : 1
                    }
                    VtToggle { label: "MUTING"; engrave: skin.engrave; on: Player.muted; lamp: true; lampColor: "#ff4a2a"; onClicked: Player.muted = !Player.muted }
                    Item { Layout.fillWidth: true }
                    Row {
                        // as wide as before the TEXT key: the receiver keeps its layout
                        spacing: 3
                        Layout.alignment: Qt.AlignBottom
                        VtKey { width: 42; iconName: "prev"; label: ""; onClicked: Player.previous() }
                        VtKey { width: 56; iconName: Player.playing ? "pause" : "play"; label: "PLAY"; latched: Player.playing; onClicked: Player.toggle() }
                        VtKey { width: 42; iconName: "next"; label: ""; onClicked: Player.next() }
                        Item { width: 4; height: 1 }
                        VtKey { width: 48; label: "COVER"; latched: skin.showCover; onClicked: Theme.setOption("cover", !skin.showCover) }
                        VtKey { width: 48; label: "PHONO"; latched: window.nowPlayingOpen; enabled: Player.hasTrack || window.nowPlayingOpen; onClicked: window.nowPlayingOpen = !window.nowPlayingOpen }
                        VtKey { width: 48; label: "TEXT"; latched: Lyrics.shown; opacity: window.lyricsMissing ? 0.6 : 1; enabled: Player.currentId > 0; onClicked: window.toggleLyrics() }
                    }
                }
            }

            // VU meters
            Column {
                visible: fp.width > 1020
                Layout.alignment: Qt.AlignTop
                spacing: 6
                Row {
                    spacing: 8
                    VuMeter {
                        width: 128; height: 92
                        channel: "L"
                        style: Theme.opt.vu === "classic" ? "classic" : "amber"
                        level: Player.levelL
                        peak: Player.peakL
                        inertia: Theme.opt.needle !== undefined ? Theme.opt.needle : 0.5
                        active: window.animationsActive
                    }
                    VuMeter {
                        width: 128; height: 92
                        channel: "R"
                        style: Theme.opt.vu === "classic" ? "classic" : "amber"
                        level: Player.levelR
                        peak: Player.peakR
                        inertia: Theme.opt.needle !== undefined ? Theme.opt.needle : 0.5
                        active: window.animationsActive
                    }
                }
                Item {
                    width: 264; height: 46
                    Text {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -4
                        text: window.fmtTime(tuning.preview >= 0 ? tuning.preview * Player.duration : Player.position) + "  /  " + window.durationText
                        font.family: "Barlow Condensed"
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        font.letterSpacing: 1
                        color: skin.engrave
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        text: "SPIELZEIT"
                        font.family: "Barlow Condensed"
                        font.pixelSize: 9
                        font.weight: Font.Bold
                        font.letterSpacing: 2
                        color: skin.engraveSoft
                    }
                }
            }

            // knobs
            Row {
                spacing: 10
                Layout.alignment: Qt.AlignVCenter
                VtKnob {
                    id: tuning
                    width: 112; height: 150
                    endless: true
                    label: "TUNING"
                    engrave: skin.engrave
                    champagne: skin.face === "champagne"
                    property real preview: -1
                    property real acc: 0
                    onTurned: (d) => {
                        if (Player.duration <= 0) return
                        if (preview < 0) preview = Player.position / Player.duration
                        // one full turn = 60 seconds
                        preview = Math.max(0, Math.min(1, preview + d / 360 * 60000 / Player.duration))
                    }
                    onReleased: {
                        if (preview >= 0 && Player.duration > 0) Player.seek(Math.round(preview * Player.duration))
                        preview = -1
                    }
                }
                VtKnob {
                    width: 128; height: 150
                    label: "VOLUME"
                    value: Player.volume
                    engrave: skin.engrave
                    pointerColor: Theme.accent
                    champagne: skin.face === "champagne"
                    onMoved: (v) => Player.volume = v
                }
            }
        }
    }

    // ---------- parts ----------
    // Wooden cabinet with a recessed inlay (tolex, or the page background).
    component Cabinet: Item {
        id: cab
        property string inlay: "tolex"
        default property alias content: inner.data
        Rectangle { anchors.fill: parent; anchors.topMargin: 6; anchors.bottomMargin: -6; radius: 10; color: Qt.rgba(0, 0, 0, 0.45) }
        Material { anchors.fill: parent; material: window.skin ? window.skin.wood : "walnut"; radius: 8; seed: cab.x * 0.01 }
        Item {
            id: inner
            anchors.fill: parent
            anchors.margins: 10
            Material {
                anchors.fill: parent
                visible: cab.inlay === "tolex"
                material: "tolex"
                radius: 4
                bevel: 0
            }
            Rectangle {
                anchors.fill: parent
                visible: cab.inlay === "bg"
                radius: 4
                color: Theme.bg
            }
            // inner shadow along the top and left edges
            Rectangle {
                anchors.fill: parent
                radius: 4
                color: "transparent"
                border.width: 1
                border.color: Qt.rgba(0, 0, 0, 0.7)
                z: 100
            }
            Rectangle {
                width: parent.width; height: 8
                z: 100
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.45) }
                    GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0) }
                }
            }
        }
    }
}
