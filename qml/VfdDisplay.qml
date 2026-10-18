import QtQuick
import Halcyon

// Vacuum-fluorescent display: glowing pixel font behind smoked glass and a
// dot mesh, with a dot-matrix spectrum analyser.
Rectangle {
    id: vfd
    property color ink: "#3EF2E0"
    property real scale: 1.0
    readonly property real k: Math.max(0.6, Math.min(2.4, height / 110))
    radius: 8
    gradient: Gradient {
        GradientStop { position: 0; color: "#0b0f0f" }
        GradientStop { position: 1; color: "#030505" }
    }
    border.color: Qt.rgba(0, 0, 0, 0.8)
    border.width: 2
    clip: true

    readonly property color dim: Qt.rgba(ink.r, ink.g, ink.b, 0.12)
    readonly property string status: Player.state === 1 ? "▶ PLAY" : Player.state === 2 ? "❚❚ PAUSE" : "■ STOP"
    readonly property string rg: Player.replayGain === 2 ? "RG ALBUM" : Player.replayGain === 1 ? "RG TRACK" : ""

    Item {
        id: content
        anchors.fill: parent
        anchors.margins: 12 * vfd.k
        // Everything scales with the display height, so it fits the compact
        // receiver as well as the full-window view.
        readonly property real unit: vfd.k

        // top row: status, format, replaygain, shuffle/repeat
        Row {
            id: topRow
            spacing: 16 * vfd.k
            Repeater {
                model: [vfd.status, Player.format, vfd.rg, Queue.shuffle ? "SHUFFLE" : "", Queue.repeat === 1 ? "REPEAT" : Queue.repeat === 2 ? "REPEAT 1" : ""]
                Text {
                    visible: modelData !== ""
                    text: modelData
                    font.family: "VT323"
                    font.pixelSize: Math.round(18 * vfd.k)
                    color: vfd.ink
                }
            }
        }

        // title (scrolls when too long) and elapsed time
        Item {
            id: titleBox
            anchors.top: topRow.bottom
            width: parent.width - timeText.width - 16 * vfd.k
            height: titleText.implicitHeight
            clip: true
            Text {
                id: titleText
                text: (Player.currentId > 0 ? Player.title : "BUB3PLAYER  READY").toUpperCase()
                font.family: "VT323"
                font.pixelSize: Math.round(38 * vfd.k)
                color: vfd.ink
                readonly property real overflow: implicitWidth - titleBox.width
                x: 0
                SequentialAnimation on x {
                    running: titleText.overflow > 0 && window.animationsActive
                    loops: Animation.Infinite
                    PauseAnimation { duration: 2500 }
                    NumberAnimation { to: -titleText.overflow; duration: Math.max(1500, titleText.overflow * 22) }
                    PauseAnimation { duration: 2000 }
                    NumberAnimation { to: 0; duration: 600 }
                }
                onTextChanged: x = 0
            }
        }
        Text {
            id: timeText
            anchors.right: parent.right
            anchors.baseline: titleBox.bottom
            anchors.baselineOffset: -6 * vfd.k
            text: window.fmtTime(Player.position)
            font.family: "VT323"
            font.pixelSize: Math.round(38 * vfd.k)
            color: vfd.ink
        }
        Text {
            id: artistText
            anchors.top: titleBox.bottom
            anchors.topMargin: -4 * vfd.k
            width: parent.width - spectrum.width - 16 * vfd.k
            text: (Player.artist + (Player.album !== "" ? "  -  " + Player.album : "")).toUpperCase()
            elide: Text.ElideRight
            font.family: "VT323"
            font.pixelSize: Math.round(21 * vfd.k)
            color: vfd.ink
            opacity: 0.85
        }

        // progress blocks + remaining time
        Row {
            id: progressRow
            anchors.bottom: parent.bottom
            spacing: 3 * vfd.k
            readonly property int n: Math.max(3, Math.floor((content.width - spectrum.width - remain.width - 30 * vfd.k) / (10 * vfd.k)))
            readonly property int lit: Player.duration > 0 ? Math.round(n * Player.position / Player.duration) : 0
            Repeater {
                model: progressRow.n
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: 7 * vfd.k
                    height: 9 * vfd.k
                    color: index < progressRow.lit ? vfd.ink : vfd.dim
                }
            }
        }
        Text {
            id: remain
            anchors.left: progressRow.right
            anchors.leftMargin: 10 * vfd.k
            anchors.bottom: parent.bottom
            anchors.bottomMargin: -4 * vfd.k
            text: "-" + window.fmtTime(Math.max(0, Player.duration - Player.position))
            font.family: "VT323"
            font.pixelSize: Math.round(20 * vfd.k)
            color: vfd.ink
            opacity: 0.8
        }

        // dot-matrix spectrum, bottom right
        Row {
            id: spectrum
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: 2 * vfd.k
            readonly property var spec: Player.spectrum
            readonly property int bars: 16
            readonly property int dots: Math.max(4, Math.min(8, Math.floor((content.height - titleBox.y - titleBox.height) / (7 * vfd.k))))
            readonly property real dotW: Math.min(5 * vfd.k, content.width * 0.26 / bars - 2 * vfd.k)
            Repeater {
                model: spectrum.bars
                Column {
                    readonly property real level: spectrum.spec.length > 0 ? spectrum.spec[Math.min(spectrum.spec.length - 1, Math.floor(index * spectrum.spec.length / spectrum.bars))] : 0
                    readonly property int lit: Math.round(level * spectrum.dots)
                    spacing: 2 * vfd.k
                    Repeater {
                        model: spectrum.dots
                        Rectangle {
                            width: spectrum.dotW
                            height: 4 * vfd.k
                            color: (spectrum.dots - 1 - index) < parent.lit ? vfd.ink : vfd.dim
                        }
                    }
                }
            }
        }
    }

    // Glow: the whole display rendered small and stretched over itself.
    ShaderEffectSource {
        anchors.fill: content
        sourceItem: content
        hideSource: false
        smooth: true
        live: true
        textureSize: Qt.size(Math.max(8, Math.round(content.width / 4)), Math.max(8, Math.round(content.height / 4)))
        opacity: 0.7
    }
    // dot mesh + smoked glass reflection
    Image {
        anchors.fill: parent
        source: "image://icon/mesh/4"
        fillMode: Image.Tile
        smooth: false
        opacity: 0.45
    }
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.07) }
            GradientStop { position: 0.35; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }
}
