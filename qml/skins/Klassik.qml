// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import Halcyon
import ".."

// Skin "Klassik": calm sidebar layout, palette of choice, cover glow.
Item {
    id: skin
    property alias stack: stack
    readonly property bool animated: false
    readonly property bool wantsLevels: false
    readonly property int toastMargin: playerBar.height + 18
    // the year in review shows over the theme's background: the furniture steps aside
    property real rvMix: window.reviewOpen ? 1 : 0
    Behavior on rvMix { NumberAnimation { duration: 450; easing.type: Easing.InOutCubic } }
    function focusSearch() { sidebar.focusSearch() }
    function clearSearch() { sidebar.clearSearch() }
    function setSearchText(t) { sidebar.searchText = t }

    Rectangle { anchors.fill: parent; color: Theme.bg }

    Ambient {
        anchors.fill: parent
        coverId: Player.currentId
        strength: Theme.ambient
        visible: !nowPlaying.open || nowPlaying.opacity < 1
    }

    RowLayout {
        anchors.fill: parent
        anchors.bottomMargin: playerBar.height
        spacing: 0
        // Hidden under the full-screen view: nothing to draw, nothing to pay for.
        visible: !(nowPlaying.open && nowPlaying.opacity === 1) && skin.rvMix < 1
        opacity: 1 - skin.rvMix

        Sidebar {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: 252
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
            Layout.preferredWidth: window.queueOpen ? 340 : 0
            visible: Layout.preferredWidth > 1
            Behavior on Layout.preferredWidth { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        }
    }

    PlayerBar {
        id: playerBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        queueOpen: window.queueOpen
        onToggleQueue: window.queueOpen = !window.queueOpen
        onOpenNowPlaying: window.nowPlayingOpen = true
        opacity: 1 - skin.rvMix
        visible: opacity > 0.01
        z: 5
    }

    NowPlaying {
        id: nowPlaying
        anchors.fill: parent
        open: window.nowPlayingOpen
        onClose: window.nowPlayingOpen = false
        z: 10
    }
}
