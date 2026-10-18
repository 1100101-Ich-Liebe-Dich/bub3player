import QtQuick
import QtQuick.Window
import Halcyon

// Turntable: the cover is the record label and turns at 33⅓ or 45 rpm.
// The tonearm lowers onto the record when playback starts and travels
// towards the label as the track progresses.
Item {
    id: tt
    property string material: "walnut"
    property real rpm: 33.333
    property bool followTrack: true
    readonly property bool spinning: Player.playing && rpm > 0 && window.animationsActive && visible
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software

    // plinth
    Material {
        anchors.fill: parent
        material: tt.material
        radius: 10
        darken: 0.3
    }
    // power LED
    Rectangle {
        x: 14; y: tt.height - 20
        width: 7; height: 7; radius: 3.5
        color: Player.playing ? "#ff5a3a" : "#3a1a12"
        SoftGlow { anchors.centerIn: parent; width: 26; height: 26; visible: Player.playing; color: Qt.rgba(1, 0.35, 0.2, 0.55) }
    }
    Text {
        x: 26; y: tt.height - 24
        text: tt.rpm > 40 ? "45" : "33"
        font.family: "Barlow Condensed"
        font.pixelSize: 12
        font.weight: Font.Bold
        color: Qt.rgba(1, 1, 1, 0.45)
    }

    // platter
    Item {
        id: platter
        width: Math.min(tt.width * 0.86, tt.height * 0.9)
        height: width
        x: tt.width * 0.06
        anchors.verticalCenter: parent.verticalCenter
        Rectangle { // shadow
            anchors.fill: parent
            anchors.topMargin: 4
            radius: width / 2
            color: Qt.rgba(0, 0, 0, 0.5)
        }
        Rectangle { // metal rim with strobe dots
            anchors.fill: parent
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#c9ccd1" }
                GradientStop { position: 0.5; color: "#6e7177" }
                GradientStop { position: 1; color: "#a4a7ad" }
            }
        }
        Item {
            id: record
            anchors.centerIn: parent
            width: parent.width * 0.955
            height: width

            CoverArt {
                id: label
                anchors.centerIn: parent
                width: record.width * 0.36
                height: width
                radius: width / 2
                requestSize: 256
                coverId: Player.currentId
                label: Player.album || "BuB3"
                RotationAnimator on rotation {
                    id: spin
                    from: 0; to: 360
                    duration: 60000 / Math.max(1, tt.rpm)
                    loops: Animation.Infinite
                    running: tt.spinning
                }
            }
            ShaderEffect {
                anchors.fill: parent
                visible: tt.canShade
                property real labelRadius: 0.36
                property color sheenColor: "#ffffff"
                fragmentShader: "qrc:/shaders/vinyl.frag.qsb"
                layer.enabled: true
            }
            Rectangle { // software fallback vinyl ring
                visible: !tt.canShade
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: width * 0.32
                border.color: "#111"
            }
            // spindle
            Rectangle {
                anchors.centerIn: parent
                width: Math.max(6, record.width * 0.025); height: width; radius: width / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: "#f0f0f0" }
                    GradientStop { position: 1; color: "#7a7a7a" }
                }
            }
        }
    }

    // ---------- tonearm ----------
    readonly property point pivot: Qt.point(tt.width * 0.9, tt.height * 0.15)
    readonly property real armLength: platter.width * 0.86
    readonly property point center: Qt.point(platter.x + platter.width / 2, platter.y + platter.height / 2)

    // Angle (degrees, clockwise from pointing down) at which the needle sits at radius r.
    function angleFor(r) {
        var best = 0, bestErr = 1e9
        for (var a = -20; a <= 70; a += 0.25) {
            var rad = a * Math.PI / 180
            var tx = pivot.x - Math.sin(rad) * armLength * 0.97
            var ty = pivot.y + Math.cos(rad) * armLength * 0.97
            var err = Math.abs(Math.hypot(tx - center.x, ty - center.y) - r)
            if (err < bestErr) { bestErr = err; best = a }
        }
        return best
    }
    readonly property real startAngle: angleFor(record.width * 0.47)
    readonly property real endAngle: angleFor(record.width * 0.2)
    readonly property real restAngle: startAngle - 16
    readonly property real progress: Player.duration > 0 ? Math.min(1, Player.position / Player.duration) : 0
    readonly property bool onRecord: Player.currentId > 0 && Player.state !== 0
    property real armAngle: !onRecord ? restAngle : (followTrack ? startAngle + (endAngle - startAngle) * progress : startAngle + 2)
    Behavior on armAngle { SpringAnimation { spring: 1.6; damping: 0.35; epsilon: 0.05 } }

    // arm rest
    Rectangle {
        x: tt.pivot.x - Math.sin(tt.restAngle * Math.PI / 180) * tt.armLength * 0.8 - 8
        y: tt.pivot.y + Math.cos(tt.restAngle * Math.PI / 180) * tt.armLength * 0.8 - 6
        width: 16; height: 12; radius: 3
        color: "#1a1a1c"
        border.color: "#3a3a3e"
    }

    Item {
        id: arm
        x: tt.pivot.x
        y: tt.pivot.y
        rotation: tt.armAngle
        // counterweight
        Rectangle {
            x: -11; y: -tt.armLength * 0.22
            width: 22; height: tt.armLength * 0.16
            radius: 4
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#5c5f64" }
                GradientStop { position: 0.45; color: "#e4e6e9" }
                GradientStop { position: 1; color: "#6a6d72" }
            }
        }
        // tube
        Rectangle {
            x: -3.5; y: -tt.armLength * 0.08
            width: 7; height: tt.armLength * 0.94
            radius: 3.5
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#7d8086" }
                GradientStop { position: 0.4; color: "#f3f4f6" }
                GradientStop { position: 1; color: "#8a8d92" }
            }
        }
        // headshell + cartridge
        Item {
            x: 0
            y: tt.armLength * 0.86
            rotation: 24
            Rectangle {
                x: -9; y: 0
                width: 18; height: tt.armLength * 0.14
                radius: 3
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#2b2b2e" }
                    GradientStop { position: 0.5; color: "#55555a" }
                    GradientStop { position: 1; color: "#232326" }
                }
            }
            Rectangle {
                x: -6; y: tt.armLength * 0.06
                width: 12; height: tt.armLength * 0.07
                radius: 2
                color: tt.onRecord ? "#c0281b" : "#7a1a12"
            }
        }
    }
    // pivot base
    Rectangle {
        x: tt.pivot.x - 20; y: tt.pivot.y - 20
        width: 40; height: 40; radius: 20
        gradient: Gradient {
            GradientStop { position: 0; color: "#e6e8eb" }
            GradientStop { position: 1; color: "#6f7277" }
        }
        border.color: Qt.rgba(0, 0, 0, 0.5)
        Rectangle { anchors.centerIn: parent; width: 14; height: 14; radius: 7; color: "#2a2a2d" }
    }
}
