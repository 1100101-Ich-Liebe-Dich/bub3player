// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Halcyon
import ".."

// Skin "Glas": glassmorphism. Soap bubbles rise slowly through soft colour
// glows (from the cover by default) and follow the music, gently; every part
// of the player is a frosted card above them. Navigation runs along the
// top, the player and queue live permanently in a column on the right.
Item {
    id: skin
    property alias stack: stack
    readonly property bool animated: speed > 0
    readonly property bool wantsLevels: false
    readonly property int toastMargin: 40
    function focusSearch() { searchField.forceActiveFocus(); searchField.selectAll() }
    function clearSearch() { searchField.text = "" }
    function setSearchText(t) { searchField.text = t }

    readonly property int gap: 18
    readonly property real corner: Theme.opt.corner !== undefined ? Theme.opt.corner : 28
    readonly property real frost: Theme.opt.frost !== undefined ? Theme.opt.frost : 0.6
    readonly property real tint: Theme.opt.tint !== undefined ? Theme.opt.tint : 0.12
    readonly property real speed: Theme.opt.speed !== undefined ? Theme.opt.speed : 1.0
    readonly property bool canShade: GraphicsInfo.api !== GraphicsInfo.Software
    readonly property bool np: window.nowPlayingOpen
    // texture the glass cards blur (soft spheres, tiny)
    readonly property var softTex: softSrc

    property real npMix: np ? 1 : 0
    Behavior on npMix { NumberAnimation { duration: 560; easing.type: Easing.InOutCubic } }
    // the year in review shows over the theme's background: the furniture steps aside
    property real rvMix: window.reviewOpen ? 1 : 0
    Behavior on rvMix { NumberAnimation { duration: 450; easing.type: Easing.InOutCubic } }

    // Music, heavily smoothed: bubbles rise a little faster and breathe with
    // fuller passages. Never a sudden change.
    readonly property real react: Theme.opt.react !== undefined ? Theme.opt.react : 0.5
    property real swell: Player.playing ? Math.min(1, Player.energy * 1.3) * react : 0
    Behavior on swell { NumberAnimation { duration: 2200; easing.type: Easing.InOutSine } }

    // time for the bubbles; integrating keeps speed changes smooth
    property real flow: 0
    property real lastFx: 0
    Connections {
        target: window
        function onFxTimeChanged() {
            var dt = window.fxTime - skin.lastFx
            skin.lastFx = window.fxTime
            if (dt > 0 && dt < 0.5) skin.flow += dt * skin.speed * (1 + skin.swell * 1.4)
        }
    }

    // mouse: only a soft light reflex on the glass
    property point mouse: Qt.point(-9999, -9999)
    HoverHandler {
        onPointChanged: if (Theme.opt.reflex !== false) skin.mouse = point.position
    }

    // ---------- colours ----------
    function vivid(c, l) {
        var h = c.hslHue < 0 ? 0.75 : c.hslHue
        return Qt.hsla(h, Math.max(0.6, Math.min(0.95, c.hslSaturation * 1.2)), l, 1)
    }
    readonly property var sets: ({
        beere: { b1: "#2B1B6E", b2: "#10073A", c: ["#FF5FA2", "#7B5CFF", "#3FE0D0", "#FFB15C", "#A3B0FF"], accent: "#FF7AC0" },
        eis:   { b1: "#0F3166", b2: "#06122E", c: ["#4FC3FF", "#3B5BFF", "#B8F6FF", "#2EE6C5", "#E4F1FF"], accent: "#7FE6FF" },
        sonne: { b1: "#4D1342", b2: "#1C0619", c: ["#FF8A3D", "#FF4F7B", "#FFD15C", "#FF6F61", "#FFC9A6"], accent: "#FFB347" }
    })
    readonly property var cols: {
        var mode = Theme.opt.orbs || "cover"
        if (mode === "cover" && Theme.coverPalette.length >= 3) {
            var cp = Theme.coverPalette
            var h0 = cp[0].hslHue < 0 ? 0.75 : cp[0].hslHue
            return { b1: Qt.hsla(h0, 0.55, 0.2, 1), b2: Qt.hsla((h0 + 0.08) % 1, 0.6, 0.07, 1),
                     c: [vivid(cp[0], 0.6), vivid(cp[1], 0.55), vivid(cp[2], 0.7), vivid(cp[1], 0.68), Qt.hsla(h0, 0.4, 0.88, 1)],
                     accent: vivid(cp[0], 0.72) }
        }
        var s = sets[mode] || sets.beere
        return { b1: Qt.color(s.b1), b2: Qt.color(s.b2), c: s.c.map(function (x) { return Qt.color(x) }), accent: Qt.color(s.accent) }
    }
    Binding { target: Theme; property: "accentOverride"; value: skin.cols.accent }

    // ---------- backdrop ----------
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: skin.cols.b1 }
            GradientStop { position: 1; color: skin.cols.b2 }
        }
    }
    // the cover, tiny, as a texture for the background
    Item {
        id: coverTex
        width: 48; height: 48
        visible: skin.canShade
        Rectangle { anchors.fill: parent; color: skin.cols.b1 }
        Image {
            anchors.fill: parent
            source: Player.hasTrack ? "image://cover/" + Player.currentId : ""
            sourceSize: Qt.size(64, 64)
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
            smooth: true
        }
    }
    ShaderEffectSource { id: coverSrc; width: 48; height: 48; sourceItem: coverTex; hideSource: true; smooth: true; visible: skin.canShade }

    ShaderEffect {
        id: orbsSoft
        width: 192
        height: Math.max(48, Math.round(192 * skin.height / Math.max(1, skin.width)))
        visible: skin.canShade
        property real time: skin.flow
        property real aspect: skin.width / Math.max(1, skin.height)
        property real soft: 1.0
        property real px: 1.0 / height
        property real coverMix: coverAmount
        property real scale: Theme.opt.size !== undefined ? Theme.opt.size : 1.0
        property real count: Theme.opt.count !== undefined ? Theme.opt.count : 9
        property real swell: skin.swell
        property color base1: skin.cols.b1
        property color base2: skin.cols.b2
        property color c1: skin.cols.c[0]
        property color c2: skin.cols.c[1]
        property color c3: skin.cols.c[2]
        property variant cover: coverSrc
        fragmentShader: "qrc:/shaders/bubbles.frag.qsb"
    }
    ShaderEffectSource {
        id: softSrc
        width: orbsSoft.width
        height: orbsSoft.height
        sourceItem: orbsSoft
        hideSource: true
        live: true
        smooth: true
        visible: skin.canShade
    }
    property real coverAmount: (Theme.opt.coverBg === true ? 0.75 : 0) + npMix * 0.35
    ShaderEffect {
        id: orbsView
        anchors.fill: parent
        visible: skin.canShade
        property real time: skin.flow
        property real aspect: width / Math.max(1, height)
        property real soft: 0.0
        property real px: 1.0 / Math.max(1, height)
        property real coverMix: Math.min(1, skin.coverAmount)
        property real scale: orbsSoft.scale
        property real count: Theme.opt.count !== undefined ? Theme.opt.count : 9
        property real swell: skin.swell
        property color base1: skin.cols.b1
        property color base2: skin.cols.b2
        property color c1: skin.cols.c[0]
        property color c2: skin.cols.c[1]
        property color c3: skin.cols.c[2]
        property variant cover: coverSrc
        fragmentShader: "qrc:/shaders/bubbles.frag.qsb"
    }

    // ---------- library ----------
    Item {
        id: frame
        anchors.fill: parent
        anchors.margins: skin.gap
        opacity: (1 - skin.npMix) * (1 - skin.rvMix)
        scale: 1 + skin.npMix * 0.03
        visible: opacity > 0.01

        // top navigation
        Pane {
            id: topBar
            width: parent.width
            height: 72
            radius: Math.min(skin.corner, 36)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 14
                spacing: 10
                Brand {
                    Layout.alignment: Qt.AlignVCenter
                    nameSize: 20
                    signatureSize: 10
                    maxWidth: 200
                    markColor: "white"
                    signatureColor: Theme.textFaint
                }
                Item { Layout.preferredWidth: 6 }
                Row {
                    id: tabs
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 6
                    readonly property bool compact: topBar.width < 1320
                    Repeater {
                        model: Library.views.filter(function (v) { return v.section === Library.views[0].section })
                        TabPill {
                            label: modelData.name
                            iconName: modelData.icon
                            compact: tabs.compact
                            current: window.currentViewId === modelData.id
                            onClicked: window.openView(modelData)
                        }
                    }
                    TabPill {
                        id: moreTab
                        label: skin.moreSections.length > 0 ? skin.moreSections[0] : qsTr("Mehr")
                        iconName: "sparkle"
                        compact: false
                        chevron: true
                        current: moreMenu.opened || skin.inMore(window.currentViewId)
                        onClicked: moreMenu.opened ? moreMenu.close() : moreMenu.open()
                    }
                }
                Item { Layout.fillWidth: true }
                // search
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: Math.max(150, Math.min(300, topBar.width * 0.2))
                    height: 42
                    radius: 21
                    color: Qt.rgba(1, 1, 1, searchField.activeFocus ? 0.2 : 0.1)
                    border.color: Qt.rgba(1, 1, 1, searchField.activeFocus ? 0.6 : 0.28)
                    Icon { id: sIcon; x: 14; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 18; color: Theme.textDim }
                    TextField {
                        id: searchField
                        anchors.left: sIcon.right
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: 8
                        background: null
                        color: Theme.text
                        placeholderText: qsTr("Suchen …")
                        placeholderTextColor: Theme.textFaint
                        font.family: Theme.uiFont
                        font.pixelSize: 14
                        selectByMouse: true
                        onTextEdited: searchDebounce.restart()
                        Keys.onEscapePressed: { text = ""; window.search(""); focus = false }
                        Keys.onReturnPressed: { searchDebounce.stop(); window.search(text) }
                    }
                    Timer { id: searchDebounce; interval: 280; onTriggered: window.search(searchField.text) }
                }
                GlassButton { Layout.alignment: Qt.AlignVCenter; size: 42; iconName: "queue"; active: window.queueOpen; onClicked: window.queueOpen = !window.queueOpen }
                GlassButton { Layout.alignment: Qt.AlignVCenter; size: 42; iconName: "tune"; active: window.currentViewId === "settings"; onClicked: window.openSettings() }
            }
        }

        // content
        Pane {
            id: contentCard
            anchors.top: topBar.bottom
            anchors.topMargin: skin.gap
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: rightCol.left
            anchors.rightMargin: skin.gap
            ContentStack {
                id: stack
                anchors.fill: parent
                anchors.margins: Math.round(skin.corner * 0.3)
            }
        }

        // player + queue
        Item {
            id: rightCol
            anchors.top: topBar.bottom
            anchors.topMargin: skin.gap
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: skin.width > 1500 ? 380 : 340

            readonly property real coverSize: Math.max(110, Math.min(width - 48, height - 262 - (window.queueOpen ? 310 : 150)))

            Pane {
                id: playerCard
                width: parent.width
                height: playerCol.implicitHeight + 44
                Column {
                    id: playerCol
                    x: 22; y: 22
                    width: parent.width - 44
                    spacing: 14
                    Item {
                        width: parent.width
                        height: rightCol.coverSize
                        // soft coloured glow under the cover
                        Rectangle {
                            anchors.centerIn: cov
                            width: cov.width * 0.9; height: cov.height * 0.9
                            anchors.verticalCenterOffset: 10
                            radius: 30
                            color: Theme.alpha(Theme.accent, 0.25)
                            visible: false
                        }
                        CoverArt {
                            id: cov
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: rightCol.coverSize; height: width
                            radius: Math.min(22, skin.corner - 6)
                            requestSize: 512
                            coverId: Player.currentId
                            label: Player.album || Player.title
                            showInitial: Player.hasTrack
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Player.hasTrack ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: if (Player.hasTrack) window.nowPlayingOpen = true
                            }
                        }
                        // little glass chip with the format, overlapping the cover
                        Chip {
                            visible: Player.format !== "" && cov.width > 160
                            anchors.left: cov.left
                            anchors.bottom: cov.bottom
                            anchors.margins: 10
                            text: Player.format
                        }
                    }
                    Item {
                        width: parent.width
                        height: nowText.implicitHeight
                        Column {
                            id: nowText
                            anchors.left: parent.left
                            anchors.right: sideHeart.left
                            spacing: 3
                            Text {
                                width: parent.width
                                text: Player.hasTrack ? Player.title : qsTr("Bereit")
                                elide: Text.ElideRight
                                font.family: Theme.displayFont
                                font.pixelSize: 21
                                font.weight: Font.Bold
                                color: Theme.text
                            }
                            Text {
                                width: parent.width
                                text: Player.hasTrack ? Player.artist : qsTr("Wähle Musik aus der Bibliothek")
                                elide: Text.ElideRight
                                font.family: Theme.uiFont
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: Theme.textDim
                            }
                        }
                        HeartButton {
                            id: sideHeart
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            visible: Player.currentId > 0
                            width: visible ? size : 0
                            onColor: "white"
                            offColor: Qt.rgba(1, 1, 1, 0.75)
                            on: Library.currentFavorite
                            onClicked: Library.toggleCurrentFavorite()
                        }
                    }
                    GlassSeek { width: parent.width }
                    Item {
                        width: parent.width
                        height: 64
                        GlassButton { anchors.verticalCenter: parent.verticalCenter; x: 0; size: 38; quiet: true; iconName: "shuffle"; active: Queue.shuffle; onClicked: Queue.shuffle = !Queue.shuffle }
                        Row {
                            anchors.centerIn: parent
                            spacing: 14
                            GlassButton { anchors.verticalCenter: parent.verticalCenter; size: 46; iconName: "prev"; onClicked: Player.previous() }
                            GlassButton { anchors.verticalCenter: parent.verticalCenter; size: 64; iconName: Player.playing ? "pause" : "play"; primary: true; onClicked: Player.toggle() }
                            GlassButton { anchors.verticalCenter: parent.verticalCenter; size: 46; iconName: "next"; onClicked: Player.next() }
                        }
                        GlassButton { anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; size: 38; quiet: true; iconName: Queue.repeat === 2 ? "repeat-one" : "repeat"; active: Queue.repeat > 0; onClicked: Queue.cycleRepeat() }
                    }
                    Row {
                        width: parent.width
                        spacing: 10
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: Player.volume === 0 ? "volume-off" : (Player.volume < 0.5 ? "volume-low" : "volume"); size: 18; color: Theme.textDim }
                        HSlider {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 66
                            from: 0; to: 1
                            value: Player.volume
                            onMoved: Player.volume = value
                        }
                        GlassButton { anchors.verticalCenter: parent.verticalCenter; size: 30; quiet: true; iconName: "expand"; enabled: Player.hasTrack; onClicked: window.nowPlayingOpen = true }
                    }
                }
            }

            Pane {
                id: lowerCard
                anchors.top: playerCard.bottom
                anchors.topMargin: skin.gap
                anchors.bottom: parent.bottom
                width: parent.width
                visible: height > 70
                QueuePanel {
                    anchors.fill: parent
                    anchors.margins: 6
                    transparent: true
                    clip: true
                    visible: window.queueOpen
                }
                // with the queue closed: glass bars dancing to the music
                Item {
                    anchors.fill: parent
                    anchors.margins: 20
                    visible: !window.queueOpen
                    Text {
                        id: upLabel
                        width: parent.width
                        text: Queue.upNext !== "" ? qsTr("Als Nächstes: %1").arg(Queue.upNext) : qsTr("Nichts mehr in der Warteschlange")
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: Theme.textDim
                    }
                    GlassBars {
                        anchors.top: upLabel.bottom
                        anchors.topMargin: 12
                        anchors.bottom: parent.bottom
                        width: parent.width
                        count: 18
                    }
                }
            }
        }
    }

    // "more" menu: the remaining views, as a floating glass card
    Popup {
        id: moreMenu
        parent: skin
        onAboutToShow: x = Math.min(skin.width - width - skin.gap, moreTab.mapToItem(skin, 0, 0).x)
        onOpened: background.refresh()
        y: frame.y + topBar.height + 8
        width: 290
        height: Math.min(skin.height - y - skin.gap, moreCol.implicitHeight + 24)
        padding: 0
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120 } }
        background: Pane {
            radius: 22
            tintAmount: Math.min(0.5, skin.tint + 0.12)
        }
        contentItem: Flickable {
            clip: true
            contentHeight: moreCol.implicitHeight + 24
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: moreCol
                y: 12
                width: parent.width
                Repeater {
                    model: Library.views.filter(function (v) { return v.section !== Library.views[0].section })
                    Column {
                        width: moreCol.width
                        readonly property var all: Library.views.filter(function (v) { return v.section !== Library.views[0].section })
                        readonly property bool newSection: index === 0 || all[index - 1].section !== modelData.section
                        Text {
                            visible: parent.newSection
                            leftPadding: 22
                            topPadding: index === 0 ? 2 : 12
                            bottomPadding: 6
                            text: modelData.section.toUpperCase()
                            font.family: Theme.uiFont
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            font.letterSpacing: 1.4
                            color: Theme.textFaint
                        }
                        AbstractButton {
                            id: mi
                            width: parent.width
                            height: 38
                            hoverEnabled: true
                            onClicked: { moreMenu.close(); window.openView(modelData) }
                            background: Rectangle {
                                x: 10; width: parent.width - 20; height: parent.height
                                radius: 12
                                color: Qt.rgba(1, 1, 1, window.currentViewId === modelData.id ? 0.2 : (mi.hovered ? 0.1 : 0))
                                border.color: Qt.rgba(1, 1, 1, window.currentViewId === modelData.id ? 0.35 : 0)
                            }
                            contentItem: Row {
                                leftPadding: 22
                                spacing: 12
                                Icon { anchors.verticalCenter: parent.verticalCenter; name: modelData.icon; size: 18; color: window.currentViewId === modelData.id ? Theme.accent : Theme.textDim }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.name
                                    font.family: Theme.uiFont
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    color: Theme.text
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    readonly property var moreSections: {
        var s = []
        for (var i = 0; i < Library.views.length; ++i) {
            var sec = Library.views[i].section
            if (sec !== Library.views[0].section && s.indexOf(sec) < 0) s.push(sec)
        }
        return s
    }
    function inMore(id) {
        for (var i = 0; i < Library.views.length; ++i)
            if (Library.views[i].id === id) return Library.views[i].section !== Library.views[0].section
        return false
    }

    // ---------- now playing ----------
    Item {
        id: nowPlaying
        anchors.fill: parent
        opacity: skin.npMix * (1 - skin.rvMix)
        visible: opacity > 0.01
        z: 10
        MouseArea { anchors.fill: parent }
        property real lyricsMix: window.lyricsOn ? 1 : 0
        Behavior on lyricsMix { NumberAnimation { duration: 320; easing.type: Easing.InOutCubic } }

        GlassButton {
            x: skin.gap + 8; y: skin.gap + 8
            size: 48
            iconName: "down"
            onClicked: window.nowPlayingOpen = false
        }
        Brand {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: skin.gap + 14
            nameSize: 18
            signatureSize: 11
            oneLine: true
            markColor: "white"
        }

        Pane {
            id: bigCard
            width: Math.min(parent.width - 140, 1180)
            height: Math.min(parent.height - 170, 640)
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 14 + (1 - skin.npMix) * 60
            radius: skin.corner + 8
            tintAmount: Math.min(0.5, skin.tint + 0.04)

            readonly property real coverSide: Math.min(height - 80, width * 0.46)
            CoverArt {
                id: bigCover
                x: 40
                anchors.verticalCenter: parent.verticalCenter
                width: bigCard.coverSide; height: width
                radius: skin.corner
                requestSize: 1024
                coverId: Player.currentId
                label: Player.album || Player.title
                opacity: 1 - nowPlaying.lyricsMix
                visible: opacity > 0
            }
            // the lyrics take the place of the cover
            Loader {
                x: bigCover.x
                y: 24
                width: bigCard.coverSide
                height: bigCard.height - 48
                active: nowPlaying.lyricsMix > 0
                opacity: nowPlaying.lyricsMix
                sourceComponent: LyricsView {
                    fontFamily: Theme.displayFont
                    fontSize: Math.round(Math.max(22, Math.min(32, bigCard.coverSide / 17)))
                    color: "white"
                    dimColor: Qt.rgba(1, 1, 1, 0.4)
                    sourceColor: Qt.rgba(1, 1, 1, 0.6)
                }
            }
            Chip {
                visible: Player.format !== "" && nowPlaying.lyricsMix < 1
                opacity: 1 - nowPlaying.lyricsMix
                anchors.left: bigCover.left
                anchors.bottom: bigCover.bottom
                anchors.margins: 16
                text: Player.formatInfo
                big: true
            }
            Column {
                anchors.left: bigCover.right
                anchors.leftMargin: 44
                anchors.right: parent.right
                anchors.rightMargin: 44
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                Text {
                    text: qsTr("JETZT LÄUFT")
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    font.weight: Font.ExtraBold
                    font.letterSpacing: 2.4
                    color: Theme.accent
                }
                Text {
                    width: parent.width
                    text: Player.title
                    font.family: Theme.displayFont
                    font.pixelSize: Math.max(28, Math.min(52, bigCard.width / 22))
                    font.weight: Font.Bold
                    font.letterSpacing: -1
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    lineHeight: 0.95
                    color: Theme.text
                }
                Text {
                    width: parent.width
                    text: Player.artist
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    color: Theme.textDim
                }
                Text {
                    width: parent.width
                    text: Player.album + (Player.year > 0 ? " · " + Player.year : "")
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    color: Theme.textDim
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: window.openCurrentAlbum() }
                }
                Item { width: 1; height: 8 }
                GlassBars {
                    width: parent.width
                    height: Math.max(50, Math.min(110, bigCard.height * 0.17))
                    count: 32
                }
                Item { width: 1; height: 4 }
                GlassSeek { width: parent.width; big: true }
                Item {
                    width: parent.width
                    height: 84
                    GlassButton { id: bigShuffle; anchors.verticalCenter: parent.verticalCenter; size: 46; quiet: true; iconName: "shuffle"; active: Queue.shuffle; onClicked: Queue.shuffle = !Queue.shuffle }
                    Row {
                        anchors.centerIn: parent
                        spacing: 18
                        GlassButton { anchors.verticalCenter: parent.verticalCenter; size: 58; iconName: "prev"; onClicked: Player.previous() }
                        GlassButton { anchors.verticalCenter: parent.verticalCenter; size: 80; iconName: Player.playing ? "pause" : "play"; primary: true; onClicked: Player.toggle() }
                        GlassButton { anchors.verticalCenter: parent.verticalCenter; size: 58; iconName: "next"; onClicked: Player.next() }
                    }
                    GlassButton { id: bigRepeat; anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; size: 46; quiet: true; iconName: Queue.repeat === 2 ? "repeat-one" : "repeat"; active: Queue.repeat > 0; onClicked: Queue.cycleRepeat() }
                    GlassButton { id: bigLyrics; anchors.verticalCenter: parent.verticalCenter; anchors.right: bigRepeat.left; anchors.rightMargin: 8; size: 46; quiet: true; iconName: "lyrics"; active: Lyrics.shown; opacity: window.lyricsMissing ? 0.5 : 1; tip: window.lyricsTip; onClicked: window.toggleLyrics() }
                    GlassButton { anchors.verticalCenter: parent.verticalCenter; anchors.left: bigShuffle.right; anchors.leftMargin: 8; size: 46; quiet: true; visible: Player.currentId > 0; iconName: Library.currentFavorite ? "heart" : "heart-outline"; active: Library.currentFavorite; tip: Library.currentFavorite ? qsTr("Aus den Favoriten entfernen") : qsTr("Zu den Favoriten"); onClicked: Library.toggleCurrentFavorite() }
                }
            }
        }
        // a second, smaller glass card floating over the corner of the first
        Pane {
            visible: Queue.upNext !== ""
            width: Math.min(360, bigCard.width * 0.36)
            height: 70
            x: bigCard.x + bigCard.width - width * 0.75
            y: bigCard.y - height * 0.55
            radius: 22
            tintAmount: Math.min(0.5, skin.tint + 0.1)
            Column {
                anchors.verticalCenter: parent.verticalCenter
                x: 20
                width: parent.width - 40
                spacing: 2
                Text {
                    text: qsTr("ALS NÄCHSTES")
                    font.family: Theme.uiFont
                    font.pixelSize: 10
                    font.weight: Font.ExtraBold
                    font.letterSpacing: 1.8
                    color: Theme.textFaint
                }
                Text {
                    width: parent.width
                    text: Queue.upNext
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    color: Theme.text
                }
            }
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: bigCard.bottom
            anchors.topMargin: 22
            spacing: 12
            Icon { anchors.verticalCenter: parent.verticalCenter; name: "volume"; size: 18; color: Theme.textDim }
            HSlider {
                anchors.verticalCenter: parent.verticalCenter
                width: 240
                from: 0; to: 1
                value: Player.volume
                onMoved: Player.volume = value
            }
        }
    }

    // ---------- parts ----------
    // A frosted card above the spheres.
    component Pane: GlassPanel {
        backdrop: window.skin ? window.skin.softTex : null
        ref: window.skin
        light: window.skin ? window.skin.mouse : Qt.point(-9999, -9999)
        radius: window.skin ? window.skin.corner : 28
        blur: window.skin ? window.skin.frost : 0.75
        tint: GraphicsInfo.api === GraphicsInfo.Software ? Theme.panel : "#ffffff"
        tintAmount: window.skin ? window.skin.tint : 0.16
        reflex: Theme.opt.reflex !== false
        shadowAlpha: 0.32
        rim: 1.0
        rim1: "#ffffff"
        rim2: Qt.rgba(1, 1, 1, 0.55)
    }

    component Chip: Rectangle {
        property alias text: chipText.text
        property bool big: false
        height: big ? 34 : 26
        width: chipText.implicitWidth + (big ? 28 : 20)
        radius: height / 2
        color: Qt.rgba(1, 1, 1, 0.22)
        border.color: Qt.rgba(1, 1, 1, 0.55)
        Text {
            id: chipText
            anchors.centerIn: parent
            font.family: Theme.uiFont
            font.pixelSize: parent.big ? 13 : 11
            font.weight: Font.ExtraBold
            color: "white"
            style: Text.Raised
            styleColor: Qt.rgba(0, 0, 0, 0.25)
        }
    }

    // Top navigation pill.
    component TabPill: AbstractButton {
        id: tp
        property string label
        property string iconName
        property bool current: false
        property bool compact: false
        property bool chevron: false
        height: 42
        width: compact ? 42 : row.implicitWidth + 30
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        ToolTip.visible: compact && hovered
        ToolTip.text: label
        ToolTip.delay: 400
        background: Rectangle {
            radius: height / 2
            color: Qt.rgba(1, 1, 1, tp.current ? 0.24 : (tp.hovered ? 0.1 : 0))
            border.color: Qt.rgba(1, 1, 1, tp.current ? 0.55 : (tp.visualFocus ? 0.8 : 0))
            Behavior on color { ColorAnimation { duration: 140 } }
        }
        contentItem: Item {
            Row {
                id: row
                anchors.centerIn: parent
                spacing: 8
                Icon { anchors.verticalCenter: parent.verticalCenter; name: tp.iconName; size: 18; color: tp.current ? "white" : Theme.textDim }
                Text {
                    visible: !tp.compact
                    anchors.verticalCenter: parent.verticalCenter
                    text: tp.label
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    font.weight: tp.current ? Font.ExtraBold : Font.DemiBold
                    color: tp.current ? "white" : Theme.textDim
                }
                Icon { visible: tp.chevron; anchors.verticalCenter: parent.verticalCenter; name: "down"; size: 14; color: Theme.textDim }
            }
        }
    }

    // Round glass button. `primary` is the bright, almost opaque play button.
    component GlassButton: AbstractButton {
        id: gb
        property string iconName
        property bool primary: false
        property bool active: false
        property bool quiet: false
        property real size: 44
        property string tip: ""
        width: size
        height: size
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        Accessible.name: tip
        Loader {
            active: gb.tip !== "" && gb.hovered
            sourceComponent: Tip { visible: true; text: gb.tip }
        }
        scale: pressed ? 0.94 : (hovered ? 1.05 : 1)
        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        background: Rectangle {
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0; color: gb.primary ? "#FFFFFF" : Qt.rgba(1, 1, 1, gb.active ? 0.32 : (gb.quiet ? (gb.hovered ? 0.1 : 0) : (gb.hovered ? 0.24 : 0.16))) }
                GradientStop { position: 1; color: gb.primary ? Qt.rgba(1, 1, 1, 0.78) : Qt.rgba(1, 1, 1, gb.active ? 0.18 : (gb.quiet ? (gb.hovered ? 0.05 : 0) : (gb.hovered ? 0.12 : 0.06))) }
            }
            border.width: gb.visualFocus ? 2 : 1
            border.color: gb.visualFocus ? Theme.accent : Qt.rgba(1, 1, 1, gb.primary ? 0.9 : (gb.quiet && !gb.active && !gb.hovered ? 0 : 0.45))
        }
        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: gb.iconName === "play" ? Math.round(gb.size * 0.04) : 0
                name: gb.iconName
                size: Math.round(gb.size * (gb.primary ? 0.4 : 0.44))
                color: !gb.enabled ? Theme.textFaint : gb.primary ? (window.skin && window.skin.cols ? window.skin.cols.b1 : "#1D1550") : (gb.active ? Theme.accent : "white")
            }
        }
    }

    // Seek bar: a glass groove filled with light.
    component GlassSeek: Column {
        id: gs
        property bool big: false
        property real dragFrac: -1
        readonly property real frac: Player.duration > 0 ? (dragFrac >= 0 ? dragFrac : Player.position / Player.duration) : 0
        spacing: 6
        Item {
            width: parent.width
            height: gs.big ? 22 : 18
            Rectangle {
                id: groove
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: gs.big ? 10 : 8
                radius: height / 2
                color: Qt.rgba(1, 1, 1, 0.14)
                border.color: Qt.rgba(1, 1, 1, 0.3)
                Rectangle {
                    width: Math.max(height, parent.width * gs.frac)
                    height: parent.height
                    radius: height / 2
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.55) }
                        GradientStop { position: 1; color: Theme.accent }
                    }
                }
            }
            Rectangle {
                width: gs.big ? 22 : 18; height: width
                radius: width / 2
                x: Math.max(0, Math.min(parent.width - width, parent.width * gs.frac - width / 2))
                anchors.verticalCenter: parent.verticalCenter
                color: "white"
                border.color: Theme.accent
                border.width: 3
            }
            MouseArea {
                anchors.fill: parent
                anchors.margins: -8
                cursorShape: Qt.PointingHandCursor
                function f(x) { return Math.max(0, Math.min(1, (x - 8) / groove.width)) }
                onPressed: (m) => gs.dragFrac = f(m.x)
                onPositionChanged: (m) => { if (pressed) gs.dragFrac = f(m.x) }
                onReleased: { if (Player.duration > 0) Player.seek(Math.round(gs.dragFrac * Player.duration)); gs.dragFrac = -1 }
            }
        }
        Item {
            width: parent.width
            height: tl.height
            Text {
                id: tl
                text: window.fmtTime(gs.dragFrac >= 0 ? gs.dragFrac * Player.duration : Player.position)
                font.family: Theme.uiFont
                font.pixelSize: gs.big ? 13 : 12
                font.weight: Font.Bold
                color: Theme.textDim
            }
            Text {
                anchors.right: parent.right
                text: Player.live ? window.durationText : "-" + window.fmtTime(Math.max(0, Player.duration - (gs.dragFrac >= 0 ? gs.dragFrac * Player.duration : Player.position)))
                font.family: Theme.uiFont
                font.pixelSize: gs.big ? 13 : 12
                font.weight: Font.Bold
                color: Theme.textDim
            }
        }
    }

    // Spectrum as frosted glass capsules.
    component GlassBars: Item {
        id: bars
        property int count: 24
        readonly property var spec: Player.spectrum
        Row {
            anchors.fill: parent
            spacing: Math.max(3, Math.round(bars.width / bars.count * 0.28))
            Repeater {
                model: bars.count
                Rectangle {
                    readonly property real level: {
                        var s = bars.spec
                        if (!Player.playing || s.length === 0) return 0
                        var i = Math.min(s.length - 1, Math.floor(Math.pow(index / bars.count, 1.3) * s.length))
                        return s[i]
                    }
                    width: (bars.width - (bars.count - 1) * parent.spacing) / bars.count
                    height: Math.max(width, bars.height * (0.06 + level * 0.94))
                    anchors.bottom: parent.bottom
                    radius: width / 2
                    gradient: Gradient {
                        GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.75) }
                        GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0.12) }
                    }
                    border.color: Qt.rgba(1, 1, 1, 0.55)
                    Behavior on height { NumberAnimation { duration: 90 } }
                }
            }
        }
    }
}
