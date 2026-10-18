import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Halcyon
import ".."

// Skin "Studio": a hi-fi receiver across the top (turntable, fluorescent
// display, hardware keys, volume knob, twin VU meters) above the library.
// The now playing view simply lets the receiver fill the whole window.
Item {
    id: skin
    property alias stack: stack
    readonly property bool animated: false
    readonly property bool wantsLevels: true
    readonly property int toastMargin: 30
    function focusSearch() { sidebar.focusSearch() }
    function clearSearch() { sidebar.clearSearch() }
    function setSearchText(t) { sidebar.searchText = t }

    readonly property string material: Theme.opt.material || "walnut"
    readonly property bool lightKeys: material === "walnut" || material === "aluminium"
    readonly property color ink: {
        switch (Theme.opt.vfd) {
        case "amber": return "#FFB547"
        case "green": return "#7CFF6B"
        case "white": return "#E8F4FF"
        }
        return "#3EF2E0"
    }
    readonly property real rpm: Theme.opt.rpm === "45" ? 45 : Theme.opt.rpm === "0" ? 0 : 33.333
    readonly property int normalHeight: Theme.opt.compact ? 214 : 272

    property real npMix: window.nowPlayingOpen ? 1 : 0
    Behavior on npMix { NumberAnimation { duration: 520; easing.type: Easing.InOutCubic } }

    Rectangle { anchors.fill: parent; color: Theme.bg }

    // ---------- receiver ----------
    Item {
        id: receiver
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: skin.normalHeight + (skin.height - skin.normalHeight) * skin.npMix
        z: 2

        Material {
            anchors.fill: parent
            material: skin.material
            radius: 0
        }
        // screws
        Repeater {
            model: [[12, 12], [receiver.width - 26, 12], [12, receiver.height - 26], [receiver.width - 26, receiver.height - 26]]
            Rectangle {
                x: modelData[0]; y: modelData[1]
                width: 14; height: 14; radius: 7
                gradient: Gradient {
                    GradientStop { position: 0; color: "#d8d9db" }
                    GradientStop { position: 1; color: "#6c6e72" }
                }
                Rectangle { anchors.centerIn: parent; width: 10; height: 2; rotation: 35 + index * 20; color: "#3a3b3e" }
            }
        }

        // ----- compact receiver (library mode) -----
        RowLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: skin.normalHeight
            anchors.margins: 22
            anchors.leftMargin: 34
            anchors.rightMargin: 34
            spacing: 24
            opacity: 1 - skin.npMix
            visible: opacity > 0.01

            Turntable {
                Layout.preferredHeight: skin.normalHeight - 44
                Layout.preferredWidth: Layout.preferredHeight * 1.12
                material: skin.material
                rpm: skin.rpm
                followTrack: Theme.opt.tonearm === true
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12
                Nameplate { big: false }
                VfdDisplay {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ink: skin.ink
                }
                Keys { Layout.fillWidth: true }
            }
            ColumnLayout {
                Layout.fillHeight: true
                spacing: 8
                Meters { meterW: 200; meterH: Theme.opt.compact ? 108 : 128 }
                Item { Layout.fillHeight: true }
                VolumeBlock { Layout.alignment: Qt.AlignRight; knobSize: Theme.opt.compact ? 74 : 92 }
            }
        }

        // ----- big receiver (now playing) -----
        Item {
            id: bigLayout
            anchors.fill: parent
            anchors.margins: 44
            opacity: skin.npMix
            visible: opacity > 0.01

            Turntable {
                id: bigTT
                height: Math.min(parent.height, parent.width * 0.5 / 1.12)
                width: height * 1.12
                anchors.verticalCenter: parent.verticalCenter
                material: skin.material
                rpm: skin.rpm
                followTrack: Theme.opt.tonearm === true
            }
            Item {
                id: bigRight
                x: bigTT.width + 44
                width: bigLayout.width - x
                height: parent.height
                Nameplate { id: bigPlate; big: true; width: Layout.preferredWidth; height: Layout.preferredHeight }
                VfdDisplay {
                    id: bigVfd
                    anchors.top: bigPlate.bottom
                    anchors.topMargin: 18
                    width: parent.width
                    height: bigLayout.height * 0.32
                    ink: skin.ink
                }
                Meters {
                    anchors.top: bigVfd.bottom
                    anchors.topMargin: 22
                    meterW: Math.min(360, (bigRight.width - 12) / 2)
                    meterH: Math.min(meterW * 0.62, bigLayout.height * 0.3)
                }
                Keys {
                    anchors.left: parent.left
                    anchors.right: bigKnob.left
                    anchors.rightMargin: 20
                    anchors.bottom: parent.bottom
                }
                VolumeBlock {
                    id: bigKnob
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    knobSize: 104
                }
            }
        }

        // close hint in big mode
        Text {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 14
            anchors.rightMargin: 44
            visible: skin.npMix > 0.9
            text: qsTr("Esc oder LIB: zurück zur Bibliothek")
            font.family: "Barlow Condensed"
            font.pixelSize: 15
            font.letterSpacing: 1
            color: skin.material === "aluminium" ? "#3a3a3d" : Qt.rgba(1, 1, 1, 0.5)
        }
    }
    // shadow under the receiver
    Rectangle {
        anchors.top: receiver.bottom
        width: parent.width
        height: 18
        z: 1
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.45) }
            GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0) }
        }
    }

    // ---------- library ----------
    RowLayout {
        anchors.top: receiver.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 0
        opacity: 1 - skin.npMix
        visible: opacity > 0.01

        Sidebar {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: 250
            showBrand: false
            currentId: window.currentViewId
            onOpen: (v) => window.openView(v)
            onSearchRequested: (t) => window.search(t)
        }
        ContentStack {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        QueuePanel {
            Layout.fillHeight: true
            Layout.preferredWidth: window.queueOpen ? 330 : 0
            visible: Layout.preferredWidth > 1
            Behavior on Layout.preferredWidth { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        }
    }

    // ---------- parts ----------
    component Nameplate: Rectangle {
        property bool big: false
        Layout.preferredHeight: plate.implicitHeight + (big ? 18 : 12)
        Layout.preferredWidth: plate.implicitWidth + (big ? 40 : 28)
        radius: 4
        gradient: Gradient {
            GradientStop { position: 0; color: skin.material === "aluminium" ? "#2c2c2f" : "#d9c08a" }
            GradientStop { position: 1; color: skin.material === "aluminium" ? "#151517" : "#9b7a3e" }
        }
        border.color: Qt.rgba(0, 0, 0, 0.5)
        Brand {
            id: plate
            anchors.centerIn: parent
            nameFont: "Barlow Condensed"
            nameSize: parent.big ? 30 : 22
            nameWeight: Font.Bold
            nameSpacing: 3
            upper: true
            showMark: true
            nameColor: skin.material === "aluminium" ? "#e8e2d6" : "#2b1e0c"
            markColor: skin.material === "aluminium" ? Theme.accent : "#6e4410"
            signatureColor: skin.material === "aluminium" ? "#a59d90" : "#4a3414"
            linkColor: skin.material === "aluminium" ? "#e8e2d6" : "#2b1e0c"
            signatureFont: "Barlow"
            signatureSize: parent.big ? 13 : 11
            oneLine: true
        }
    }

    component Keys: RowLayout {
        spacing: 10
        HwKey { iconName: "prev"; label: qsTr("Zurück"); lightKey: skin.lightKeys; onClicked: Player.previous() }
        HwKey { iconName: "play"; label: qsTr("Abspielen"); lightKey: skin.lightKeys; lit: Player.state === 1; onClicked: Player.play() }
        HwKey { iconName: "pause"; label: qsTr("Pause"); lightKey: skin.lightKeys; lit: Player.state === 2; ledColor: "#FFB547"; onClicked: Player.pause() }
        HwKey { iconName: "next"; label: qsTr("Weiter"); lightKey: skin.lightKeys; onClicked: Player.next() }
        Item { Layout.preferredWidth: 10 }
        HwKey { label: "SHUF"; lightKey: skin.lightKeys; lit: Queue.shuffle; ledColor: "#FFB547"; onClicked: Queue.shuffle = !Queue.shuffle }
        HwKey { label: Queue.repeat === 2 ? "REP 1" : "REP"; lightKey: skin.lightKeys; lit: Queue.repeat > 0; ledColor: "#FFB547"; onClicked: Queue.cycleRepeat() }
        HwKey { label: "QUEUE"; lightKey: skin.lightKeys; lit: window.queueOpen; ledColor: "#3EF2E0"; implicitWidth: 64; visible: !window.nowPlayingOpen; onClicked: window.queueOpen = !window.queueOpen }
        HwKey {
            label: window.nowPlayingOpen ? "LIB" : "BIG"
            lightKey: skin.lightKeys
            lit: window.nowPlayingOpen
            ledColor: "#3EF2E0"
            onClicked: window.nowPlayingOpen = !window.nowPlayingOpen
        }
        Item { Layout.fillWidth: true }
    }

    component Meters: Row {
        id: meters
        property real meterW: 200
        property real meterH: 128
        readonly property bool led: Theme.opt.vu === "led"
        spacing: 12
        Column {
            spacing: 10
            VuMeter {
                width: meters.led ? meters.meterW * 1.5 : meters.meterW
                height: meters.led ? Math.max(40, meters.meterH * 0.36) : meters.meterH
                channel: "L"
                style: Theme.opt.vu || "classic"
                level: Player.levelL
                peak: Player.peakL
                inertia: Theme.opt.needle !== undefined ? Theme.opt.needle : 0.45
                active: window.onScreen && visible
            }
            VuMeter {
                visible: meters.led
                width: meters.meterW * 1.5
                height: Math.max(40, meters.meterH * 0.36)
                channel: "R"
                style: "led"
                level: Player.levelR
                peak: Player.peakR
            }
        }
        VuMeter {
            visible: !meters.led
            width: meters.meterW
            height: meters.meterH
            channel: "R"
            style: Theme.opt.vu || "classic"
            level: Player.levelR
            peak: Player.peakR
            inertia: Theme.opt.needle !== undefined ? Theme.opt.needle : 0.45
            active: window.onScreen && visible
        }
    }

    component VolumeBlock: Row {
        property real knobSize: 92
        spacing: 14
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                anchors.right: parent.right
                text: "VOLUME"
                font.family: "Barlow Condensed"
                font.pixelSize: 13
                font.weight: Font.Bold
                font.letterSpacing: 2
                color: skin.material === "aluminium" ? "#2b2b2e" : "#e8dcc4"
            }
            Text {
                anchors.right: parent.right
                text: Math.round(Player.volume * 100)
                font.family: "VT323"
                font.pixelSize: 22
                color: skin.ink
            }
        }
        Knob {
            width: parent.knobSize
            height: width
            lightKnob: skin.lightKeys
            ledColor: Theme.accent
            onMoved: (v) => Player.volume = v
        }
    }
}
