// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

Rectangle {
    id: bar
    property bool queueOpen: false
    signal toggleQueue()
    signal openNowPlaying()

    height: 96
    color: Theme.alpha(Theme.panel, Theme.ambient > 0 ? 0.9 : 1)
    Rectangle { width: parent.width; height: 1; color: Theme.alpha(Theme.border, 0.8) }

    function fmt(ms) {
        if (!(ms > 0)) return "0:00"
        var s = Math.floor(ms / 1000)
        var m = Math.floor(s / 60)
        var h = Math.floor(m / 60)
        var ss = (s % 60 < 10 ? "0" : "") + (s % 60)
        if (h > 0) return h + ":" + ((m % 60) < 10 ? "0" : "") + (m % 60) + ":" + ss
        return m + ":" + ss
    }

    // ---- now playing ----
    MouseArea {
        id: nowArea
        x: 20
        width: Math.min(340, bar.width * 0.27)
        height: parent.height
        hoverEnabled: true
        cursorShape: Player.currentId > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (Player.currentId > 0) bar.openNowPlaying()
        CoverArt {
            id: cover
            anchors.verticalCenter: parent.verticalCenter
            width: 60; height: 60
            radius: Theme.radius - 2
            requestSize: 128
            coverId: Player.currentId
            label: Player.album || Player.title
            showInitial: Player.currentId > 0
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: Theme.alpha("#000000", 0.45)
                visible: nowArea.containsMouse && Player.currentId > 0
                Icon { anchors.centerIn: parent; name: "expand"; size: 22; color: "white" }
            }
        }
        Column {
            anchors.left: cover.right
            anchors.leftMargin: 14
            anchors.right: barHeart.left
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Text {
                width: parent.width
                text: Player.currentId > 0 ? Player.title : qsTr("Nichts in der Wiedergabe")
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: 15
                font.weight: Font.Bold
                color: Player.currentId > 0 ? Theme.text : Theme.textDim
            }
            Text {
                width: parent.width
                text: Player.artist
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: 13
                color: Theme.textDim
            }
        }
        HeartButton {
            id: barHeart
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: Player.currentId > 0
            width: visible ? size : 0
            on: Library.currentFavorite
            onClicked: Library.toggleCurrentFavorite()
        }
    }

    // ---- transport + seek ----
    Column {
        id: center
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(620, bar.width * 0.42)
        spacing: 6

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "shuffle"
                iconSize: 18
                active: Queue.shuffle
                tip: Queue.shuffle ? qsTr("Zufallswiedergabe aus") : qsTr("Zufallswiedergabe an")
                onClicked: Queue.shuffle = !Queue.shuffle
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "prev"
                iconSize: 22
                iconColor: Theme.text
                tip: qsTr("Zurück")
                onClicked: Player.previous()
            }
            AbstractButton {
                id: playBtn
                anchors.verticalCenter: parent.verticalCenter
                width: 44; height: 44
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                Accessible.name: Player.playing ? qsTr("Pause") : qsTr("Abspielen")
                onClicked: Player.toggle()
                background: Rectangle {
                    radius: 22
                    color: playBtn.pressed ? Qt.darker(Theme.accent, 1.15) : (playBtn.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent)
                    scale: playBtn.pressed ? 0.94 : 1
                    Behavior on scale { NumberAnimation { duration: 90 } }
                    border.width: playBtn.visualFocus ? 2 : 0
                    border.color: Theme.text
                }
                contentItem: Item {
                    Icon {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: Player.playing ? 0 : 1
                        name: Player.playing ? "pause" : "play"
                        size: 22
                        color: Theme.onAccent
                    }
                }
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "next"
                iconSize: 22
                iconColor: Theme.text
                tip: qsTr("Weiter")
                onClicked: Player.next()
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: Queue.repeat === 2 ? "repeat-one" : "repeat"
                iconSize: 18
                active: Queue.repeat > 0
                tip: Queue.repeat === 0 ? qsTr("Alles wiederholen") : Queue.repeat === 1 ? qsTr("Titel wiederholen") : qsTr("Nicht wiederholen")
                onClicked: Queue.cycleRepeat()
            }
        }

        Item {
            width: parent.width
            height: 22
            Text {
                id: posText
                anchors.left: parent.left
                anchors.verticalCenter: ribbon.verticalCenter
                width: 44
                text: bar.fmt(ribbon.dragFrac >= 0 ? ribbon.dragFrac * Player.duration : Player.position)
                font.family: Theme.uiFont
                font.pixelSize: 12
                font.weight: Font.Medium
                color: Theme.textDim
            }
            SeekRibbon {
                id: ribbon
                anchors.left: posText.right
                anchors.right: durText.left
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                height: 22
                bars: Math.max(24, Math.floor(width / 7))
            }
            Text {
                id: durText
                anchors.right: parent.right
                anchors.verticalCenter: ribbon.verticalCenter
                width: 44
                horizontalAlignment: Text.AlignRight
                text: bar.fmt(Player.duration)
                font.family: Theme.uiFont
                font.pixelSize: 12
                font.weight: Font.Medium
                color: Theme.textDim
            }
        }
    }

    // ---- right side ----
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        Rectangle {
            visible: Player.format !== "" && bar.width > 1180
            anchors.verticalCenter: parent.verticalCenter
            height: 24
            width: fmtText.implicitWidth + 16
            radius: 6
            color: "transparent"
            border.color: Theme.alpha(Theme.text, 0.18)
            Text {
                id: fmtText
                anchors.centerIn: parent
                text: Player.format
                font.family: Theme.uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: Theme.textDim
            }
        }
        Item { width: 6; height: 1 }
        VolumeSlider { anchors.verticalCenter: parent.verticalCenter }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            iconName: "queue"
            active: bar.queueOpen
            tip: qsTr("Warteschlange")
            onClicked: bar.toggleQueue()
        }
    }

    Text {
        visible: Player.error !== ""
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        anchors.horizontalCenter: parent.horizontalCenter
        text: Player.error
        font.family: Theme.uiFont
        font.pixelSize: 11
        color: "#ff8a80"
    }
}
