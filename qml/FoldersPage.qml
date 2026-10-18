import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// The library as it is on disk: folders first, then files, natural order.
Item {
    id: page
    property string pageId: "folders"

    FolderModel {
        id: folders
        onFolderChanged: { list.pinnedTop = true; list.positionViewAtBeginning() }
    }

    ListView {
        id: list
        anchors.fill: parent
        model: folders
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: 24
        currentIndex: -1
        keyNavigationEnabled: true
        focus: true
        ScrollBar.vertical: HScrollBar {}
        // The header grows once data arrives; stay pinned to the top until the user scrolls.
        property bool pinnedTop: true
        onMovementEnded: pinnedTop = atYBeginning
        Connections {
            target: list.headerItem
            ignoreUnknownSignals: true
            function onHeightChanged() { if (list.pinnedTop) list.positionViewAtBeginning() }
        }
        Keys.onReturnPressed: page.activate(currentIndex)
        Keys.onBackPressed: folders.up()
        Keys.onPressed: (e) => { if (e.key === Qt.Key_Backspace && folders.folderId > 0) { folders.up(); e.accepted = true } }

        header: Column {
            width: list.width
            PageHeader {
                width: parent.width
                title: folders.folderId > 0 ? folders.name : qsTr("Ordner")
                showBack: folders.folderId > 0
                onBack: folders.up()
                subtitle: folders.folderId > 0 ? folders.path : qsTr("Deine Musikordner, genau so wie sie auf der Platte liegen")
                detail: {
                    var parts = []
                    if (folders.folderCount > 0) parts.push(folders.folderCount === 1 ? qsTr("1 Ordner") : qsTr("%1 Ordner").arg(Library.formatCount(folders.folderCount)))
                    if (folders.trackCount > 0) parts.push(folders.trackCount === 1 ? qsTr("1 Titel") : qsTr("%1 Titel").arg(Library.formatCount(folders.trackCount)))
                    return parts.join(", ")
                }
                PillButton {
                    visible: folders.folderId > 0
                    primary: true
                    text: qsTr("Ordner abspielen")
                    iconName: "play"
                    onClicked: Library.playFolderTree(folders.path, false)
                }
                PillButton {
                    visible: folders.folderId > 0
                    text: qsTr("Zufällig")
                    iconName: "shuffle"
                    onClicked: Library.playFolderTree(folders.path, true)
                }
            }
            // Breadcrumbs
            Flow {
                visible: folders.crumbs.length > 1
                x: 36
                width: parent.width - 72
                spacing: 2
                bottomPadding: 10
                Repeater {
                    model: folders.crumbs
                    Row {
                        spacing: 2
                        Icon {
                            visible: index > 0
                            anchors.verticalCenter: parent.verticalCenter
                            name: "chevron"
                            size: 16
                            color: Theme.textFaint
                        }
                        Text {
                            text: modelData.name
                            font.family: Theme.uiFont
                            font.pixelSize: 13
                            font.weight: index === folders.crumbs.length - 1 ? Font.DemiBold : Font.Normal
                            color: crumbMouse.containsMouse ? Theme.accent : (index === folders.crumbs.length - 1 ? Theme.text : Theme.textDim)
                            MouseArea {
                                id: crumbMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: folders.folderId = modelData.id
                            }
                        }
                    }
                }
            }
        }

        delegate: Item {
            id: row
            width: list.width
            height: model.isFolder ? 58 : 50
            readonly property bool isCurrent: !model.isFolder && model.itemId === Player.currentId

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 1
                anchors.bottomMargin: 1
                radius: Theme.radius - 2
                color: row.ListView.isCurrentItem ? Theme.alpha(Theme.text, 0.09)
                     : row.isCurrent ? Theme.alpha(Theme.accent, 0.09)
                     : mouse.containsMouse ? Theme.alpha(Theme.text, 0.045) : "transparent"
            }

            // Folder row
            Item {
                visible: model.isFolder
                anchors.fill: parent
                CoverArt {
                    id: fcover
                    x: 36
                    anchors.verticalCenter: parent.verticalCenter
                    width: 42; height: 42
                    radius: 6
                    requestSize: 64
                    coverId: model.isFolder ? model.coverId : 0
                    label: model.name || ""
                    showInitial: false
                    Icon {
                        anchors.centerIn: parent
                        visible: !fcover.loaded
                        name: "folder"
                        size: 20
                        color: Theme.alpha(Theme.text, 0.7)
                    }
                }
                Column {
                    anchors.left: fcover.right
                    anchors.leftMargin: 14
                    anchors.right: chev.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        width: parent.width
                        text: model.name || ""
                        elide: Text.ElideMiddle
                        font.family: Theme.uiFont
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }
                    Text {
                        width: parent.width
                        text: model.detail || ""
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        color: Theme.textDim
                    }
                }
                Icon {
                    id: chev
                    anchors.right: parent.right
                    anchors.rightMargin: 36
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron"
                    size: 18
                    color: Theme.textFaint
                }
            }

            // File row
            Item {
                visible: !model.isFolder
                anchors.fill: parent
                Text {
                    x: 36
                    width: 42
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: row.isCurrent ? "" : (model.trackNo || "")
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: Theme.textFaint
                }
                Icon {
                    visible: row.isCurrent
                    x: 36 + 12
                    anchors.verticalCenter: parent.verticalCenter
                    name: Player.playing ? "eq" : "pause"
                    size: 16
                    color: Theme.accent
                }
                Column {
                    x: 36 + 56
                    width: parent.width - x - 36 - 200
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Text {
                        width: parent.width
                        text: model.title || ""
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: row.isCurrent ? Theme.accent : Theme.text
                    }
                    Text {
                        width: parent.width
                        text: model.name || ""
                        elide: Text.ElideMiddle
                        font.family: Theme.uiFont
                        font.pixelSize: 12
                        color: Theme.textFaint
                    }
                }
                Text {
                    anchors.right: dur.left
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: model.format || ""
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    color: Theme.textFaint
                }
                Text {
                    id: dur
                    anchors.right: parent.right
                    anchors.rightMargin: 36
                    anchors.verticalCenter: parent.verticalCenter
                    text: model.duration || ""
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: Theme.textDim
                }
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: model.isFolder ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    list.currentIndex = index
                    list.forceActiveFocus()
                    if (model.isFolder) {
                        list.currentIndex = -1
                        folders.folderId = model.itemId
                    }
                }
                onDoubleClicked: if (!model.isFolder) Library.playFolder(folders, index)
            }
        }
    }

    function activate(i) {
        if (i < 0) return
        if (i < folders.folderCount) {
            var id = folders.data(folders.index(i, 0), 258) // itemId role
            list.currentIndex = -1
            folders.folderId = id
        } else {
            Library.playFolder(folders, i)
        }
    }

    EmptyState {
        anchors.centerIn: parent
        visible: folders.folderId === 0 && folders.folderCount === 0
        iconName: "folder"
        title: qsTr("Noch keine Musikordner")
        text: qsTr("Füge einen Ordner hinzu, den Rest erledigt der Scan im Hintergrund.")
        actionText: qsTr("Musikordner hinzufügen")
        onAction: window.openSettings()
    }
}
