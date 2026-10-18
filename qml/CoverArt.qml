import QtQuick
import QtQuick.Window
import Halcyon

// Album art with a generated placeholder underneath (shows through while loading
// or when a release has no art). Rounded corners via a tiny shader; plain image
// on the software renderer.
Item {
    id: c
    property var coverId: 0
    property real radius: 6
    property int requestSize: 256
    property string label: ""          // used for the placeholder colour and initial
    property bool showInitial: true
    readonly property bool loaded: img.status === Image.Ready && img.implicitWidth > 1
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software

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
            GradientStop { position: 0; color: c.label === "" ? Theme.surfaceHover : Qt.hsla(c.hue(c.label), Theme.dark ? 0.32 : 0.35, Theme.dark ? 0.26 : 0.78, 1) }
            GradientStop { position: 1; color: c.label === "" ? Theme.surface : Qt.hsla((c.hue(c.label) + 0.08) % 1, Theme.dark ? 0.38 : 0.4, Theme.dark ? 0.16 : 0.68, 1) }
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
        source: c.coverId > 0 ? "image://cover/" + c.coverId : ""
        sourceSize: Qt.size(c.requestSize, c.requestSize)
        asynchronous: true
        cache: true
        smooth: true
        fillMode: Image.PreserveAspectCrop
        visible: c.loaded && (!c.canShade || c.radius <= 0)
    }

    ShaderEffect {
        anchors.fill: parent
        visible: c.loaded && c.canShade && c.radius > 0
        property variant source: img
        property real radius: c.radius
        property size itemSize: Qt.size(width, height)
        fragmentShader: "qrc:/shaders/rounded.frag.qsb"
    }
}
