// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Halcyon
import ".."

// Skin "Terminal": a text-mode music player on a phosphor screen. Boxes drawn
// with lines, the cover as ASCII art, a spectrum of block characters, text
// buttons in brackets, key hints along the bottom, scanlines over everything.
Item {
    id: skin
    property alias stack: stack
    readonly property bool animated: Theme.opt.flicker === true
    readonly property bool wantsLevels: false
    readonly property int toastMargin: 70
    function focusSearch() { sidebar.focusSearch() }
    function clearSearch() { sidebar.clearSearch() }
    function setSearchText(t) { sidebar.searchText = t }

    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software
    readonly property string mono: "IBM Plex Mono"
    readonly property int fs: 14
    readonly property real cw: fmMono.advanceWidth("M")
    FontMetrics { id: fmMono; font.family: skin.mono; font.pixelSize: skin.fs }

    property real npMix: window.nowPlayingOpen ? 1 : 0
    Behavior on npMix { NumberAnimation { duration: 260 } }

    // cursor blink shared by all
    property bool blink: true
    Timer { interval: 530; repeat: true; running: window.animationsActive; onTriggered: skin.blink = !skin.blink }

    Rectangle { anchors.fill: parent; color: Theme.bg }

    // ---------- header ----------
    Item {
        id: header
        x: 12; y: 6
        width: parent.width - 24
        height: 26
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            Text { text: "BuB3Player"; font.family: skin.mono; font.pixelSize: skin.fs; font.weight: Font.Bold; color: Theme.accent }
            Text { text: " v" + Qt.application.version.split(".").slice(0, 2).join(".") + " │ Projekt präsentiert von "; font.family: skin.mono; font.pixelSize: skin.fs; color: Theme.textDim }
            Text {
                text: "www.BAUENUNDBOOTEN.de"
                font.family: skin.mono; font.pixelSize: skin.fs; font.underline: true
                color: Theme.text
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally("https://www.bauenundbooten.de") }
            }
        }
        Text {
            id: clockText
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            property date now: new Date()
            Timer { interval: 10000; repeat: true; running: true; triggeredOnStart: true; onTriggered: clockText.now = new Date() }
            text: (Player.state === 1 ? "[▶ PLAY]" : Player.state === 2 ? "[‖ PAUSE]" : "[■ STOP]")
                  + " │ " + (Queue.count > 0 ? (Queue.currentRow + 1) + "/" + Queue.count : "0/0")
                  + " │ vol " + Math.round(Player.volume * 100) + "%"
                  + " │ " + Qt.formatTime(now, "hh:mm")
            font.family: skin.mono; font.pixelSize: skin.fs
            color: Theme.textDim
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
    }

    // ---------- footer: key hints ----------
    Row {
        id: footer
        x: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 6
        height: 22
        spacing: 14
        Repeater {
            model: [["Leer", "Play/Pause"], ["^F", "Suche"], ["^J", "Liste"], ["^L", "Großansicht"], ["^⇧L", "Liedtext"], ["F11", "Vollbild"], ["^T", "Themes"], ["^←/→", "Titel"], ["Esc", "zurück"]]
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Rectangle {
                    width: keyT.implicitWidth + 8; height: keyT.implicitHeight
                    color: Theme.text
                    Text { id: keyT; anchors.centerIn: parent; text: modelData[0]; font.family: skin.mono; font.pixelSize: 12; font.weight: Font.Bold; color: Theme.bg }
                }
                Text { text: modelData[1]; font.family: skin.mono; font.pixelSize: 12; color: Theme.textDim }
            }
        }
    }

    // ---------- library ----------
    Item {
        id: lib
        x: 12
        anchors.top: header.bottom
        anchors.topMargin: 10
        anchors.bottom: footer.top
        anchors.bottomMargin: 8
        width: parent.width - 24
        opacity: 1 - skin.npMix
        visible: opacity > 0.01

        TBox {
            id: navBox
            title: "Bibliothek"
            width: 262
            anchors.top: parent.top
            anchors.bottom: statusBox.top
            anchors.bottomMargin: 10
            Sidebar {
                id: sidebar
                anchors.fill: parent
                anchors.topMargin: 2
                transparent: true
                showBrand: false
                currentId: window.currentViewId
                onOpen: (v) => window.openView(v)
                onSearchRequested: (t) => window.search(t)
            }
        }
        TBox {
            id: contentBox
            title: !stack.currentItem ? "Inhalt" : (stack.currentItem.title || (stack.currentItem.view && stack.currentItem.view.name) || "Inhalt")
            anchors.left: navBox.right
            anchors.leftMargin: 10
            anchors.right: queueBox.visible ? queueBox.left : parent.right
            anchors.rightMargin: queueBox.visible ? 10 : 0
            anchors.top: parent.top
            anchors.bottom: statusBox.top
            anchors.bottomMargin: 10
            ContentStack {
                id: stack
                anchors.fill: parent
                anchors.margins: 2
                slide: 0
            }
        }
        TBox {
            id: queueBox
            title: "Warteschlange"
            visible: window.queueOpen
            width: 330
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: statusBox.top
            anchors.bottomMargin: 10
            QueuePanel {
                anchors.fill: parent
                transparent: true
                clip: true
            }
        }

        // ---------- status box ----------
        TBox {
            id: statusBox
            title: "Wiedergabe"
            width: parent.width
            height: 146
            anchors.bottom: parent.bottom
            AsciiCover {
                id: smallArt
                x: 10; y: 12
                height: parent.height - 22
                width: Math.round(height * 1.1)
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: if (Player.currentId > 0) window.nowPlayingOpen = true }
            }
            Column {
                anchors.left: smallArt.right
                anchors.leftMargin: 16
                anchors.right: parent.right
                anchors.rightMargin: 12
                y: 10
                spacing: 3
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: "> " + (Player.currentId > 0 ? Player.title : "keine Wiedergabe") + (skin.blink ? "█" : " ")
                    font.family: skin.mono; font.pixelSize: 17; font.weight: Font.Bold
                    color: Theme.accent
                }
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: Player.currentId > 0 ? "  " + Player.artist + " ─ " + Player.album + (Player.year > 0 ? " (" + Player.year + ")" : "") + "   " + Player.format : "  Wähle Musik aus der Bibliothek."
                    font.family: skin.mono; font.pixelSize: skin.fs
                    color: Theme.textDim
                }
                TextBar { width: parent.width }
                BlockSpectrum { width: parent.width; rows: 1; cols: Math.floor(parent.width / skin.cw) }
                Row {
                    spacing: skin.cw
                    TButton { text: "[|<]"; onClicked: Player.previous() }
                    TButton { text: Player.playing ? "[ || ]" : "[ >  ]"; strong: true; onClicked: Player.toggle() }
                    TButton { text: "[>|]"; onClicked: Player.next() }
                    TButton { text: "[zufall:" + (Queue.shuffle ? "an " : "aus") + "]"; on: Queue.shuffle; onClicked: Queue.shuffle = !Queue.shuffle }
                    TButton { text: "[wdh:" + (Queue.repeat === 2 ? "eins" : Queue.repeat === 1 ? "alle" : "aus ") + "]"; on: Queue.repeat > 0; onClicked: Queue.cycleRepeat() }
                    TButton { text: "[fav:" + (Library.currentFavorite ? "an " : "aus") + "]"; on: Library.currentFavorite; enabled: Player.currentId > 0; onClicked: Library.toggleCurrentFavorite() }
                    TButton {
                        text: "vol [" + "#".repeat(Math.round(Player.volume * 10)) + "-".repeat(10 - Math.round(Player.volume * 10)) + "]"
                        onClicked: {}
                        MouseArea {
                            anchors.fill: parent
                            onPressed: (m) => setVol(m.x)
                            onPositionChanged: (m) => { if (pressed) setVol(m.x) }
                            onWheel: (w) => Player.volume = Math.max(0, Math.min(1, Player.volume + (w.angleDelta.y > 0 ? 0.05 : -0.05)))
                            function setVol(x) {
                                var c0 = 5 * skin.cw
                                Player.volume = Math.max(0, Math.min(1, (x - c0) / (10 * skin.cw)))
                            }
                        }
                    }
                    TButton { text: "[auto:" + (Queue.autoplay ? "an " : "aus") + "]"; on: Queue.autoplay; onClicked: Queue.autoplay = !Queue.autoplay }
                    TButton { text: "[liste]"; on: window.queueOpen; onClicked: window.queueOpen = !window.queueOpen }
                    TButton { text: "[großansicht]"; onClicked: window.nowPlayingOpen = true }
                }
            }
        }
    }

    // ---------- now playing ----------
    Item {
        id: np
        x: 12
        anchors.top: header.bottom
        anchors.topMargin: 10
        anchors.bottom: footer.top
        anchors.bottomMargin: 8
        width: parent.width - 24
        opacity: skin.npMix
        visible: opacity > 0.01
        MouseArea { anchors.fill: parent }
        TBox {
            anchors.fill: parent
            title: "Jetzt läuft"
            AsciiCover {
                id: bigArt
                x: 16; y: 18
                height: parent.height - 34
                width: Math.min(parent.width * 0.5, Math.round(height * 1.1))
                big: true
                visible: !window.lyricsOn
            }
            // the lyrics in place of the picture, the line being sung marked
            Loader {
                x: bigArt.x + 8
                y: bigArt.y
                width: bigArt.width - 8
                height: bigArt.height
                active: window.lyricsOn
                sourceComponent: LyricsView {
                    fontFamily: "VT323"
                    fontSize: Math.round(Math.max(24, Math.min(36, np.width / 40)))
                    fontWeight: Font.Normal
                    marker: "> "
                    lineGap: 4
                    color: Theme.accent
                    dimColor: Theme.textDim
                    sourceColor: Theme.textFaint
                }
            }
            Column {
                anchors.left: bigArt.right
                anchors.leftMargin: 28
                anchors.right: parent.right
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Text {
                    text: "$ bub3 --now-playing"
                    font.family: skin.mono; font.pixelSize: skin.fs
                    color: Theme.textFaint
                }
                Text {
                    width: parent.width
                    text: Player.title + (skin.blink ? "█" : "")
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    font.family: "VT323"
                    font.pixelSize: Math.max(40, Math.min(84, np.width / 16))
                    lineHeight: 0.85
                    color: Theme.accent
                }
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: Player.artist
                    font.family: "VT323"
                    font.pixelSize: 34
                    color: Theme.text
                }
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: "album: " + Player.album + (Player.year > 0 ? "  jahr: " + Player.year : "") + (Player.format !== "" ? "  format: " + Player.format : "") + (Player.bpm > 0 ? "  bpm: " + Math.round(Player.bpm) : "") + (Player.musicalKey !== "" ? "  tonart: " + Player.musicalKey : "")
                    font.family: skin.mono; font.pixelSize: skin.fs
                    color: Theme.textDim
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: window.openCurrentAlbum() }
                }
                Item { width: 1; height: 8 }
                BlockSpectrum { width: parent.width; rows: 6; cols: Math.floor(parent.width / skin.cw) }
                TextBar { width: parent.width }
                Row {
                    spacing: skin.cw * 2
                    TButton { text: "[|<]"; onClicked: Player.previous() }
                    TButton { text: Player.playing ? "[ || pause ]" : "[ >  play  ]"; strong: true; onClicked: Player.toggle() }
                    TButton { text: "[>|]"; onClicked: Player.next() }
                    TButton { text: "[zufall]"; on: Queue.shuffle; onClicked: Queue.shuffle = !Queue.shuffle }
                    TButton { text: "[wdh]"; on: Queue.repeat > 0; onClicked: Queue.cycleRepeat() }
                    TButton { text: "[fav]"; on: Library.currentFavorite; onClicked: Library.toggleCurrentFavorite() }
                    TButton { text: "[text]"; on: Lyrics.shown; opacity: window.lyricsMissing ? 0.5 : 1; onClicked: window.toggleLyrics() }
                    TButton { text: "[esc]"; onClicked: window.nowPlayingOpen = false }
                }
                Text {
                    visible: Queue.upNext !== ""
                    width: parent.width
                    elide: Text.ElideRight
                    text: "danach: " + Queue.upNext
                    font.family: skin.mono; font.pixelSize: skin.fs
                    color: Theme.textFaint
                }
            }
        }
    }

    // ---------- CRT ----------
    ShaderEffect {
        anchors.fill: parent
        visible: skin.canShade
        z: 50
        property size itemSize: Qt.size(width, height)
        property real scan: Theme.opt.scan !== undefined ? Theme.opt.scan : 0.55
        property real vignette: Theme.opt.vignette !== undefined ? Theme.opt.vignette : 0.6
        property real flicker: Theme.opt.flicker === true ? 1 : 0
        property real time: skin.animated ? window.fxTime : 0
        property color glow: Theme.text
        fragmentShader: "qrc:/shaders/crt.frag.qsb"
        layer.enabled: !skin.animated
    }

    // ---------- boot sequence ----------
    Rectangle {
        id: boot
        anchors.fill: parent
        z: 40
        color: Theme.bg
        visible: opacity > 0.01
        opacity: Theme.opt.boot === false ? 0 : 1
        property int shown: 0
        readonly property var lines: [
            "BuB3 BIOS v1.0   (C) BAUENUNDBOOTEN.de",
            "",
            "Speichertest .......... 640 KB OK",
            "Klangkarte ............ GStreamer OK",
            "Musikordner ........... " + Library.roots.length,
            "Bibliothek ............ " + Library.formatCount(Library.trackCount) + " Titel",
            "Lade Halcyon-Kern ..... OK",
            "",
            "C:\\> BUB3PLAYER.EXE"
        ]
        Column {
            x: 40; y: 40
            spacing: 2
            Repeater {
                model: boot.shown
                Text { text: boot.lines[index]; font.family: skin.mono; font.pixelSize: 16; color: index === 0 ? Theme.accent : Theme.text }
            }
            Text { text: skin.blink ? "█" : " "; font.family: skin.mono; font.pixelSize: 16; color: Theme.text }
        }
        Timer {
            running: boot.opacity > 0
            interval: 110
            repeat: true
            onTriggered: {
                if (boot.shown < boot.lines.length) boot.shown++
                else { stop(); fade.start() }
            }
        }
        NumberAnimation { id: fade; target: boot; property: "opacity"; to: 0; duration: 350 }
        MouseArea { anchors.fill: parent; enabled: boot.opacity > 0.5; onClicked: fade.start() }
    }

    // ---------- parts ----------
    // A box drawn with lines, the title set into the top border.
    component TBox: Item {
        id: box
        property string title
        default property alias content: inner.data
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 8
            color: "transparent"
            border.color: Theme.border
            border.width: 1
        }
        Rectangle {
            x: 14
            width: titleT.implicitWidth + 8
            height: titleT.implicitHeight
            color: Theme.bg
            Text { id: titleT; x: 4; text: "┤ " + box.title + " ├"; font.family: "IBM Plex Mono"; font.pixelSize: 13; font.weight: Font.Bold; color: Theme.text }
        }
        Item { id: inner; anchors.fill: parent; anchors.topMargin: 16; anchors.margins: 3 }
    }

    // [text] button: inverse video on hover.
    component TButton: AbstractButton {
        id: tb
        property bool on: false
        property bool strong: false
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        implicitWidth: tbt.implicitWidth
        implicitHeight: tbt.implicitHeight
        background: Rectangle { color: tb.hovered || tb.visualFocus ? Theme.text : "transparent" }
        contentItem: Text {
            id: tbt
            text: tb.text
            font.family: "IBM Plex Mono"
            font.pixelSize: 14
            font.weight: tb.strong || tb.on ? Font.Bold : Font.Normal
            color: tb.hovered || tb.visualFocus ? Theme.bg : (tb.on || tb.strong ? Theme.accent : Theme.text)
        }
    }

    // [=====>------]  1:23 / 4:56   (click or drag to seek)
    component TextBar: Item {
        id: bar
        height: barT.implicitHeight
        readonly property string times: " " + window.fmtTime(dragFrac >= 0 ? dragFrac * Player.duration : Player.position) + " / " + window.fmtTime(Player.duration)
        readonly property int n: Math.max(5, Math.floor(width / skin.cw) - times.length - 2)
        property real dragFrac: -1
        readonly property real frac: Player.duration > 0 ? (dragFrac >= 0 ? dragFrac : Player.position / Player.duration) : 0
        readonly property int k: Math.min(n, Math.round(frac * n))
        Text {
            id: barT
            text: "[" + "=".repeat(Math.max(0, bar.k - 1)) + (bar.k > 0 ? ">" : "") + "·".repeat(bar.n - bar.k) + "]" + bar.times
            font.family: "IBM Plex Mono"
            font.pixelSize: 14
            color: Theme.text
        }
        MouseArea {
            width: (bar.n + 2) * skin.cw
            height: parent.height
            cursorShape: Qt.PointingHandCursor
            function f(x) { return Math.max(0, Math.min(1, (x - skin.cw) / (bar.n * skin.cw))) }
            onPressed: (m) => bar.dragFrac = f(m.x)
            onPositionChanged: (m) => { if (pressed) bar.dragFrac = f(m.x) }
            onReleased: { if (Player.duration > 0) Player.seek(Math.round(bar.dragFrac * Player.duration)); bar.dragFrac = -1 }
        }
    }

    // Spectrum from block characters, built as text.
    component BlockSpectrum: Text {
        id: bs
        property int rows: 1
        property int cols: 40
        readonly property string blocks: " ▁▂▃▄▅▆▇█"
        readonly property var spec: Player.spectrum
        readonly property bool live: visible && Player.playing && spec.length > 0
        text: {
            var s = live ? spec : []
            var lines = []
            for (var r = rows - 1; r >= 0; --r) {
                var line = ""
                for (var c = 0; c < cols; ++c) {
                    var v = s.length > 0 ? s[Math.min(s.length - 1, Math.floor(Math.pow(c / cols, 1.4) * s.length))] : 0
                    var h = v * rows * 8 - r * 8
                    line += h >= 8 ? "█" : (h <= 0 ? (r === 0 ? "▁" : " ") : blocks.charAt(Math.max(1, Math.round(h))))
                }
                lines.push(line)
            }
            return lines.join("\n")
        }
        font.family: "IBM Plex Mono"
        font.pixelSize: 14
        lineHeight: 0.82
        color: Theme.textDim
    }

    // The cover as ASCII art.
    component AsciiCover: Item {
        id: art
        property bool big: false
        readonly property real cell: Theme.opt.cell !== undefined ? Theme.opt.cell : 7
        Image {
            id: src
            source: Player.currentId > 0 ? "image://cover/" + Player.currentId : ""
            sourceSize: Qt.size(art.big ? 256 : 128, art.big ? 256 : 128)
            asynchronous: true
            visible: false
            fillMode: Image.PreserveAspectCrop
        }
        ShaderEffect {
            anchors.fill: parent
            visible: src.status === Image.Ready && GraphicsInfo.api !== GraphicsInfo.Software
            property variant source: src
            property size cells: Qt.size(Math.floor(width / art.cell), Math.floor(height / (art.cell * 7 / 6)))
            property color ink: Theme.text
            property color bg: Theme.bg
            property real colorMix: Theme.opt.asciiColor === true ? 1 : 0
            property real contrast: 1.35
            fragmentShader: "qrc:/shaders/ascii.frag.qsb"
        }
        CoverArt { // software renderer: no ASCII shader, show the cover itself
            anchors.fill: parent
            visible: src.status === Image.Ready && GraphicsInfo.api === GraphicsInfo.Software
            radius: 0
            coverId: Player.currentId
            requestSize: 256
        }
        Text {
            visible: src.status !== Image.Ready
            anchors.centerIn: parent
            horizontalAlignment: Text.AlignHCenter
            text: "+--------+\n|  kein  |\n| Cover  |\n+--------+"
            font.family: "IBM Plex Mono"
            font.pixelSize: 13
            color: Theme.textFaint
        }
    }
}
