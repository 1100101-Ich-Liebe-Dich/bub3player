import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Halcyon
import ".."

// Skin "Soft": neumorphism. One calm surface; cards and controls are
// extruded from it or pressed into it. The player is a round dial.
Item {
    id: skin
    property alias stack: stack
    // only the big view moves (clouds), and only very gently
    readonly property bool animated: window.nowPlayingOpen && Theme.opt.clouds !== false
    readonly property bool wantsLevels: false
    readonly property int toastMargin: dock.height + 40
    function focusSearch() { sidebar.focusSearch() }
    function clearSearch() { sidebar.clearSearch() }
    function setSearchText(t) { sidebar.searchText = t }

    readonly property real corner: Theme.opt.corner !== undefined ? Theme.opt.corner : 24
    readonly property int gap: 22

    property real npMix: window.nowPlayingOpen ? 1 : 0
    Behavior on npMix { NumberAnimation { duration: 480; easing.type: Easing.InOutCubic } }

    Rectangle { anchors.fill: parent; color: Theme.bg }

    // ---------- library ----------
    Item {
        id: frame
        anchors.fill: parent
        anchors.margins: skin.gap
        anchors.bottomMargin: dock.height + skin.gap * 2
        opacity: 1 - skin.npMix
        visible: opacity > 0.01

        NeuBox {
            id: navCard
            width: 262
            height: parent.height
            radius: skin.corner
            mode: "raised"
            Sidebar {
                id: sidebar
                anchors.fill: parent
                anchors.topMargin: 14
                anchors.bottomMargin: 8
                transparent: true
                currentId: window.currentViewId
                onOpen: (v) => window.openView(v)
                onSearchRequested: (t) => window.search(t)
            }
        }
        ContentStack {
            id: stack
            anchors.left: navCard.right
            anchors.leftMargin: skin.gap - 6
            anchors.right: queueCard.left
            anchors.rightMargin: queueCard.width > 1 ? skin.gap : 0
            height: parent.height
        }
        NeuBox {
            id: queueCard
            anchors.right: parent.right
            width: window.queueOpen ? 330 : 0
            visible: width > 1
            height: parent.height
            radius: skin.corner
            mode: "raised"
            Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
            QueuePanel {
                anchors.fill: parent
                anchors.margins: 6
                transparent: true
                clip: true
            }
        }
    }

    // ---------- dock ----------
    NeuBox {
        id: dock
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: skin.gap
        anchors.bottomMargin: skin.gap - skin.npMix * 160
        height: 104
        radius: skin.corner
        mode: "raised"
        opacity: 1 - skin.npMix
        visible: opacity > 0.01

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 22
            spacing: 18

            // cover in a pressed-in ring
            Item {
                Layout.preferredWidth: 80
                Layout.preferredHeight: 80
                NeuBox { anchors.fill: parent; radius: width / 2; mode: "inset"; depth: 4 }
                ProgressRing {
                    anchors.fill: parent
                    anchors.margins: 2
                    progress: Player.duration > 0 ? Player.position / Player.duration : 0
                    thickness: 0.08
                    glow: 0.0
                    track: "transparent"
                }
                CoverArt {
                    anchors.centerIn: parent
                    width: 60; height: 60
                    radius: 30
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
                Layout.preferredWidth: Math.min(250, dock.width * 0.2)
                spacing: 3
                Text {
                    width: parent.width
                    text: Player.currentId > 0 ? Player.title : qsTr("Bereit")
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 16
                    font.weight: Font.ExtraBold
                    color: Theme.text
                }
                Text {
                    width: parent.width
                    text: Player.currentId > 0 ? Player.artist : qsTr("Wähle Musik aus der Bibliothek")
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: Theme.textDim
                }
            }
            Row {
                spacing: 16
                SoftButton { anchors.verticalCenter: parent.verticalCenter; iconName: "prev"; onClicked: Player.previous() }
                SoftButton {
                    anchors.verticalCenter: parent.verticalCenter
                    size: 64
                    iconName: Player.playing ? "pause" : "play"
                    primary: true
                    onClicked: Player.toggle()
                }
                SoftButton { anchors.verticalCenter: parent.verticalCenter; iconName: "next"; onClicked: Player.next() }
            }
            SoftSeek { Layout.fillWidth: true; Layout.preferredHeight: 40 }
            Row {
                spacing: 12
                SoftButton { anchors.verticalCenter: parent.verticalCenter; size: 40; iconName: "shuffle"; active: Queue.shuffle; onClicked: Queue.shuffle = !Queue.shuffle }
                SoftButton { anchors.verticalCenter: parent.verticalCenter; size: 40; iconName: Queue.repeat === 2 ? "repeat-one" : "repeat"; active: Queue.repeat > 0; onClicked: Queue.cycleRepeat() }
                HSlider {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 110
                    visible: dock.width > 1150
                    from: 0; to: 1
                    value: Player.volume
                    onMoved: Player.volume = value
                }
                SoftButton { anchors.verticalCenter: parent.verticalCenter; size: 40; iconName: "queue"; active: window.queueOpen; onClicked: window.queueOpen = !window.queueOpen }
                SoftButton { anchors.verticalCenter: parent.verticalCenter; size: 40; iconName: "expand"; enabled: Player.currentId > 0; onClicked: window.nowPlayingOpen = true }
            }
        }
    }

    // ---------- now playing: the dial ----------
    Item {
        id: nowPlaying
        anchors.fill: parent
        opacity: skin.npMix
        visible: opacity > 0.01
        z: 10
        MouseArea { anchors.fill: parent }

        readonly property real dial: Math.min(height * 0.66, width * 0.42)

        // clouds drifting by; music only lets them billow and drift a little faster
        property real calm: Player.playing ? Math.min(1, Player.energy * 1.4) : 0
        Behavior on calm { NumberAnimation { duration: 2400; easing.type: Easing.InOutSine } }
        property real flow: 0
        property real lastFx: 0
        Connections {
            target: window
            enabled: skin.animated
            function onFxTimeChanged() {
                var dt = window.fxTime - nowPlaying.lastFx
                nowPlaying.lastFx = window.fxTime
                if (dt > 0 && dt < 0.5) nowPlaying.flow += dt * (0.6 + nowPlaying.calm * 0.9)
            }
        }
        ShaderEffect {
            id: clouds
            visible: Theme.opt.clouds !== false && GraphicsInfo.api !== GraphicsInfo.Software
            width: 220
            height: Math.max(60, Math.round(220 * parent.height / Math.max(1, parent.width)))
            property real time: nowPlaying.flow
            property real aspect: parent.width / Math.max(1, parent.height)
            property real swell: nowPlaying.calm
            property color base: Theme.bg
            property color light: Theme.paletteData.light || Qt.lighter(Theme.bg, 1.2)
            property color dark: Theme.paletteData.shadow || Qt.darker(Theme.bg, 1.3)
            fragmentShader: "qrc:/shaders/clouds.frag.qsb"
        }
        ShaderEffectSource {
            anchors.fill: parent
            visible: clouds.visible
            sourceItem: clouds
            hideSource: true
            smooth: true
        }

        SoftButton {
            x: 26; y: 24
            size: 46
            iconName: "down"
            onClicked: window.nowPlayingOpen = false
        }
        Brand {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 28
            nameSize: 18
            signatureSize: 11
            oneLine: true
        }

        Item {
            id: dialBox
            width: nowPlaying.dial
            height: width
            x: Math.max(70, nowPlaying.width * 0.3 - width / 2)
            anchors.verticalCenter: parent.verticalCenter

            // outer raised ring, inner pressed ring, cover in the middle
            NeuBox { anchors.fill: parent; radius: width / 2; mode: "convex"; depth: 14 }
            NeuBox {
                anchors.fill: parent
                anchors.margins: dialBox.width * 0.07
                radius: width / 2
                mode: "inset"
                depth: 8
            }
            ProgressRing {
                id: ring
                anchors.fill: parent
                anchors.margins: dialBox.width * 0.075
                progress: seekDrag.dragging ? seekDrag.frac : (Player.duration > 0 ? Player.position / Player.duration : 0)
                thickness: 0.045
                glow: 0.0
                track: "transparent"
            }
            NeuBox {
                anchors.fill: parent
                anchors.margins: dialBox.width * 0.15
                radius: width / 2
                mode: "raised"
                depth: 10
            }
            CoverArt {
                anchors.fill: parent
                anchors.margins: dialBox.width * 0.17
                radius: width / 2
                requestSize: 1024
                coverId: Player.currentId
                label: Player.album || Player.title
            }
            // knob on the ring
            Item {
                anchors.fill: ring
                rotation: ring.progress * 360
                NeuBox {
                    width: dialBox.width * 0.075
                    height: width
                    radius: width / 2
                    x: parent.width / 2 - width / 2
                    y: parent.height * 0.0225 - height / 2
                    mode: "convex"
                    depth: 4
                    fill: Theme.accent
                }
            }
            MouseArea {
                id: seekDrag
                anchors.fill: parent
                property bool dragging: false
                property real frac: 0
                function angleFrac(mx, my) {
                    var a = Math.atan2(mx - width / 2, -(my - height / 2))
                    if (a < 0) a += Math.PI * 2
                    return a / (Math.PI * 2)
                }
                function onRing(mx, my) {
                    var r = Math.hypot(mx - width / 2, my - height / 2) / (width / 2)
                    return r > 0.78
                }
                cursorShape: containsMouse && onRing(mouseX, mouseY) ? Qt.PointingHandCursor : Qt.ArrowCursor
                hoverEnabled: true
                onPressed: (m) => { if (onRing(m.x, m.y)) { dragging = true; frac = angleFrac(m.x, m.y) } else m.accepted = false }
                onPositionChanged: (m) => { if (dragging) frac = angleFrac(m.x, m.y) }
                onReleased: { if (dragging && Player.duration > 0) Player.seek(Math.round(frac * Player.duration)); dragging = false }
            }
        }

        Column {
            anchors.left: dialBox.right
            anchors.leftMargin: Math.max(50, nowPlaying.width * 0.06)
            anchors.right: parent.right
            anchors.rightMargin: 70
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12

            Text {
                width: parent.width
                text: Player.title
                font.family: Theme.displayFont
                font.pixelSize: Math.max(30, Math.min(54, nowPlaying.width / 24))
                font.weight: Font.ExtraBold
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                lineHeight: 1.0
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
            Text {
                width: parent.width
                text: Player.album + (Player.year > 0 ? ", " + Player.year : "") + (Player.format !== "" ? "   " + Player.formatInfo : "")
                font.family: Theme.uiFont
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                color: Theme.textDim
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: window.openCurrentAlbum() }
            }
            Item { width: 1; height: 16 }
            Row {
                spacing: 12
                Text {
                    text: window.fmtTime(seekDrag.dragging ? seekDrag.frac * Player.duration : Player.position)
                    font.family: Theme.uiFont
                    font.pixelSize: 40
                    font.weight: Font.ExtraBold
                    color: Theme.text
                }
                Text {
                    anchors.baseline: parent.children[0].baseline
                    text: "/ " + window.fmtTime(Player.duration)
                    font.family: Theme.uiFont
                    font.pixelSize: 18
                    font.weight: Font.Bold
                    color: Theme.textFaint
                }
            }
            // soft spectrum: capsules rising from a groove
            Item {
                visible: Theme.opt.bars === true && Player.spectrumAvailable
                width: parent.width
                height: 74
                NeuBox { anchors.fill: parent; radius: 18; mode: "inset"; depth: 5 }
                Row {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 6
                    readonly property var spec: Player.spectrum
                    readonly property int n: Math.max(12, Math.floor(width / 16))
                    Repeater {
                        model: parent.n
                        Rectangle {
                            readonly property real level: parent.spec.length > 0 ? parent.spec[Math.min(parent.spec.length - 1, Math.floor(index * parent.spec.length / parent.n))] : 0
                            width: (parent.width - (parent.n - 1) * 6) / parent.n
                            height: Math.max(width, parent.height * level)
                            anchors.bottom: parent.bottom
                            radius: width / 2
                            gradient: Gradient {
                                GradientStop { position: 0; color: Theme.accent2 }
                                GradientStop { position: 1; color: Theme.accent }
                            }
                        }
                    }
                }
            }
            Item { width: 1; height: 10 }
            Row {
                spacing: 22
                SoftButton { anchors.verticalCenter: parent.verticalCenter; size: 48; iconName: "shuffle"; active: Queue.shuffle; onClicked: Queue.shuffle = !Queue.shuffle }
                SoftButton { anchors.verticalCenter: parent.verticalCenter; size: 58; iconName: "prev"; onClicked: Player.previous() }
                SoftButton { anchors.verticalCenter: parent.verticalCenter; size: 84; iconName: Player.playing ? "pause" : "play"; primary: true; onClicked: Player.toggle() }
                SoftButton { anchors.verticalCenter: parent.verticalCenter; size: 58; iconName: "next"; onClicked: Player.next() }
                SoftButton { anchors.verticalCenter: parent.verticalCenter; size: 48; iconName: Queue.repeat === 2 ? "repeat-one" : "repeat"; active: Queue.repeat > 0; onClicked: Queue.cycleRepeat() }
            }
            Row {
                spacing: 14
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "volume"; size: 20; color: Theme.textDim }
                HSlider {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 220
                    from: 0; to: 1
                    value: Player.volume
                    onMoved: Player.volume = value
                }
            }
            Text {
                visible: Queue.upNext !== ""
                width: parent.width
                elide: Text.ElideRight
                text: qsTr("Als Nächstes: %1").arg(Queue.upNext)
                font.family: Theme.uiFont
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: Theme.textFaint
            }
        }
    }

    // ---------- parts ----------
    // Round soft button: raised at rest, pressed in when held or switched on.
    component SoftButton: AbstractButton {
        id: sbtn
        property string iconName
        property bool primary: false
        property bool active: false
        property real size: 48
        width: size
        height: size
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        background: NeuBox {
            radius: sbtn.width / 2
            mode: sbtn.pressed || sbtn.active ? "inset" : (sbtn.primary || sbtn.hovered ? "convex" : "raised")
            depth: Math.max(3, sbtn.size / 10)
            fill: sbtn.primary ? Theme.accent : Theme.bg
            Rectangle {
                visible: sbtn.visualFocus
                anchors.fill: parent
                anchors.margins: -3
                radius: width / 2
                color: "transparent"
                border.width: 2
                border.color: Theme.accent
            }
        }
        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: sbtn.iconName === "play" ? 2 : 0
                name: sbtn.iconName
                size: Math.round(sbtn.size * 0.42)
                color: !sbtn.enabled ? Theme.textFaint : sbtn.primary ? Theme.onAccent : (sbtn.active ? Theme.accent : Theme.textDim)
            }
        }
    }

    // Seek groove with time on both sides.
    component SoftSeek: Item {
        id: seekBox
        property real dragFrac: -1
        readonly property real frac: Player.duration > 0 ? (dragFrac >= 0 ? dragFrac : Player.position / Player.duration) : 0
        Text {
            id: tPos
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 42
            text: window.fmtTime(seekBox.dragFrac >= 0 ? seekBox.dragFrac * Player.duration : Player.position)
            font.family: Theme.uiFont
            font.pixelSize: 13
            font.weight: Font.Bold
            color: Theme.textDim
        }
        Item {
            id: groove
            anchors.left: tPos.right
            anchors.right: tDur.left
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            height: 14
            NeuBox { anchors.fill: parent; radius: height / 2; mode: "inset"; depth: 3 }
            Rectangle {
                x: 3; y: 3
                width: Math.max(height, (parent.width - 6) * seekBox.frac)
                height: parent.height - 6
                radius: height / 2
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.accent2 }
                    GradientStop { position: 1; color: Theme.accent }
                }
            }
            NeuBox {
                width: 24; height: 24
                radius: 12
                x: Math.max(0, Math.min(parent.width - width, (parent.width - 6) * seekBox.frac - width / 2 + 3))
                anchors.verticalCenter: parent.verticalCenter
                mode: "convex"
                depth: 3
            }
            MouseArea {
                anchors.fill: parent
                anchors.margins: -10
                cursorShape: Qt.PointingHandCursor
                function f(x) { return Math.max(0, Math.min(1, (x - 10) / groove.width)) }
                onPressed: (m) => seekBox.dragFrac = f(m.x)
                onPositionChanged: (m) => { if (pressed) seekBox.dragFrac = f(m.x) }
                onReleased: { if (Player.duration > 0) Player.seek(Math.round(seekBox.dragFrac * Player.duration)); seekBox.dragFrac = -1 }
            }
        }
        Text {
            id: tDur
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 42
            horizontalAlignment: Text.AlignRight
            text: window.fmtTime(Player.duration)
            font.family: Theme.uiFont
            font.pixelSize: 13
            font.weight: Font.Bold
            color: Theme.textDim
        }
    }
}
