import QtQuick
import QtQuick.Window
import Halcyon

// Album art with a generated placeholder underneath (shows through while loading
// or when a release has no art). Rounded corners via a tiny shader; plain image
// on the software renderer.
//
// Memory: the image is decoded at the size it is shown (64/128/256/512/1024 px
// buckets, never above requestSize) and only while it is visible; a hidden
// cover (e.g. the big view of a theme while the library is shown) lets go of
// its image after a few seconds.
Item {
    id: c
    property var coverId: 0
    property string artist: ""         // set: the artist's picture (album art of coverId if there is none)
    property real radius: 6
    property int requestSize: 256
    property string label: ""          // used for the placeholder colour and initial
    property bool showInitial: true
    property bool duotone: Theme.style.coverDuotone === true
    readonly property bool useShader: c.canShade && (c.radius > 0 || c.duotone)
    readonly property bool loaded: img.status === Image.Ready && img.implicitWidth > 1
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software

    readonly property int shownPx: Math.ceil(Math.max(width, height) * Screen.devicePixelRatio)
    readonly property int decodeSize: {
        var b = shownPx <= 64 ? 64 : shownPx <= 128 ? 128 : shownPx <= 256 ? 256 : shownPx <= 512 ? 512 : 1024
        return Math.min(c.requestSize, b)
    }
    property bool shown: visible
    onVisibleChanged: {
        if (visible) { releaseTimer.stop(); shown = true }
        else releaseTimer.restart()
    }
    Timer { id: releaseTimer; interval: 4000; onTriggered: c.shown = c.visible }

    function hue(s) {
        var h = 0
        for (var i = 0; i < s.length; ++i)
            h = (h * 31 + s.charCodeAt(i)) % 360
        return h / 360
    }

    Rectangle {
        anchors.fill: parent
        radius: c.radius
        visible: !c.loaded
        gradient: Gradient {
            GradientStop { position: 0; color: c.label === "" || c.duotone ? Theme.surfaceHover : Qt.hsla(c.hue(c.label), Theme.dark ? 0.32 : 0.35, Theme.dark ? 0.26 : 0.78, 1) }
            GradientStop { position: 1; color: c.label === "" || c.duotone ? Theme.surface : Qt.hsla((c.hue(c.label) + 0.08) % 1, Theme.dark ? 0.38 : 0.4, Theme.dark ? 0.16 : 0.68, 1) }
        }
        Text {
            visible: c.showInitial && c.width >= 40
            anchors.centerIn: parent
            text: c.label.length > 0 ? c.label.charAt(0).toUpperCase() : ""
            font.family: Theme.displayFont
            font.pixelSize: Math.round(c.width * 0.42)
            font.weight: Font.DemiBold
            color: Qt.rgba(1, 1, 1, Theme.dark ? 0.32 : 0.6)
        }
    }

    Image {
        id: img
        anchors.fill: parent
        source: c.coverId > 0 && c.shown && c.shownPx > 0
                ? (c.artist !== "" ? "image://cover/artist/" + c.coverId + "/" + encodeURIComponent(c.artist) : "image://cover/" + c.coverId)
                : ""
        sourceSize: Qt.size(c.decodeSize, c.decodeSize)
        asynchronous: true
        cache: true
        smooth: true
        fillMode: Image.PreserveAspectCrop
        visible: c.loaded && !c.useShader
    }

    ShaderEffect {
        anchors.fill: parent
        visible: c.loaded && c.useShader
        property variant source: img
        property real radius: c.radius
        property size itemSize: Qt.size(width, height)
        property real duo: c.duotone ? 1 : 0
        property color duoDark: Theme.bg
        property color duoLight: Theme.text
        fragmentShader: "qrc:/shaders/rounded.frag.qsb"
    }
}
