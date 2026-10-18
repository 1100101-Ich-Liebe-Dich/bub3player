import QtQuick
import QtQuick.Controls.Basic
import Halcyon

Rectangle {
    id: panel
    property bool transparent: false
    color: transparent ? "transparent" : Theme.alpha(Theme.panel, Theme.ambient > 0 ? 0.86 : 1)
    clip: true

    Rectangle { visible: !panel.transparent; width: 1; height: parent.height; color: Theme.alpha(Theme.border, 0.8) }

    Item {
        id: head
        width: parent.width
        height: 88
        Text {
            x: 24
            y: 22
            text: qsTr("Warteschlange")
            font.family: Theme.displayFont
            font.pixelSize: 21
            font.weight: Font.DemiBold
            color: Theme.text
        }
        Text {
            x: 24
            y: 50
            text: Queue.count > 0 ? qsTr("Titel %1 von %2").arg(Library.formatCount(Queue.currentRow + 1)).arg(Library.formatCount(Queue.count)) : qsTr("Leer")
            font.family: Theme.uiFont
            font.pixelSize: 13
            color: Theme.textDim
        }
        IconButton {
            anchors.right: parent.right
            anchors.rightMargin: 14
            y: 22
            iconName: "delete"
            iconSize: 18
            tip: qsTr("Warteschlange leeren")
            enabled: Queue.count > 0
            onClicked: Queue.clear()
        }
    }

    ListView {
        id: list
        anchors.top: head.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        model: Queue
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HScrollBar {}
        function showCurrent(mode) {
            if (Queue.currentRow >= 0)
                Qt.callLater(function () { list.positionViewAtIndex(Queue.currentRow, mode) })
        }
        Component.onCompleted: showCurrent(ListView.Beginning)
        onHeightChanged: if (height > 100) showCurrent(ListView.Beginning)
        Connections {
            target: Queue
            function onCurrentChanged() { if (panel.visible) list.showCurrent(ListView.Contain) }
        }

        delegate: Item {
            id: row
            width: list.width
            height: 52
            opacity: model.isPast ? 0.5 : 1
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                radius: Theme.radius - 2
                color: model.isCurrent ? Theme.alpha(Theme.accent, 0.13) : mouse.containsMouse ? Theme.alpha(Theme.text, 0.05) : "transparent"
            }
            CoverArt {
                id: art
                x: 20
                anchors.verticalCenter: parent.verticalCenter
                width: 36; height: 36
                radius: 5
                requestSize: 64
                coverId: model.trackId
                label: model.album || ""
                showInitial: false
            }
            Column {
                anchors.left: art.right
                anchors.leftMargin: 12
                anchors.right: tail.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    width: parent.width
                    text: model.title || ""
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: model.isCurrent ? Theme.accent : Theme.text
                }
                Text {
                    width: parent.width
                    text: model.artist || ""
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    color: Theme.textDim
                }
            }
            Item {
                id: tail
                anchors.right: parent.right
                anchors.rightMargin: 18
                width: 44
                height: parent.height
                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !mouse.containsMouse
                    text: model.duration || ""
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    color: Theme.textFaint
                }
                IconButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30; height: 30
                    visible: mouse.containsMouse && !model.isCurrent
                    iconName: "close"
                    iconSize: 16
                    tip: qsTr("Entfernen")
                    onClicked: Queue.removeAt(index)
                }
            }
            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                z: -1
                onDoubleClicked: Player.playRow(index)
            }
        }
    }

    EmptyState {
        anchors.centerIn: list
        visible: Queue.count === 0
        width: parent.width - 48
        iconName: "queue"
        title: qsTr("Nichts geplant")
        text: qsTr("Doppelklick auf einen Titel spielt ihn ab, Rechtsklick reiht ihn ein.")
    }
}
