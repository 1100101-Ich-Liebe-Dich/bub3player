import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Albums, artists, genres, years, formats ... as a cover grid or as tiles.
Item {
    id: page
    property string pageId: ""
    property var view: ({})
    property string subtitle: ""
    property bool showBack: StackView.view ? StackView.view.depth > 1 : false
    readonly property var baseSpec: view.spec || ({})
    readonly property string groupBy: baseSpec.groupBy || "album"
    readonly property string layout: view.layout || "grid"
    readonly property bool round: groupBy === "albumArtist" || groupBy === "artist"
    property string groupSort: baseSpec.groupSort || "name"
    readonly property bool letterJump: groupSort === "name" && layout === "grid" && groups.count > 40

    GroupModel {
        id: groups
        spec: {
            var s = Object.assign({}, page.baseSpec)
            s.groupSort = page.groupSort
            return s
        }
    }

    readonly property int side: 36
    readonly property int minCell: layout === "grid" ? 184 : 280
    readonly property int columns: Math.max(1, Math.floor((grid.width - side * 2 - (letterJump ? 24 : 0)) / minCell))
    readonly property real cellW: (grid.width - side * 2 - (letterJump ? 24 : 0)) / columns

    // --ui-test scroll: page through the whole grid (memory tests)
    function scrollStep() {
        if (grid.contentHeight <= grid.height) return
        grid.contentY = grid.atYEnd ? grid.originY : Math.min(grid.contentY + grid.height * 0.9, grid.originY + grid.contentHeight - grid.height)
    }
    function openFirst(row) {
        if (groups.count > 0)
            window.openGroup(groups, Math.min(groups.count - 1, row || 0), page.groupBy)
    }

    function nounFor(n) {
        var one, many
        switch (groupBy) {
        case "album": one = qsTr("Album"); many = qsTr("Alben"); break
        case "albumArtist": case "artist": one = qsTr("Künstler"); many = qsTr("Künstler"); break
        case "genre": one = qsTr("Genre"); many = qsTr("Genres"); break
        default: one = qsTr("Gruppe"); many = qsTr("Gruppen")
        }
        return n === 1 ? "1 " + one : Library.formatCount(n) + " " + many
    }

    GridView {
        id: grid
        anchors.fill: parent
        model: groups
        clip: true
        reuseItems: true
        cacheBuffer: 600
        boundsBehavior: Flickable.StopAtBounds
        cellWidth: page.cellW
        cellHeight: page.layout === "grid" ? page.cellW + 58 : 100
        leftMargin: page.side
        rightMargin: page.side + (page.letterJump ? 24 : 0)
        bottomMargin: 24
        ScrollBar.vertical: HScrollBar {}
        // The header grows once data arrives; stay pinned to the top until the user scrolls.
        property bool pinnedTop: true
        onMovementEnded: pinnedTop = atYBeginning
        Connections {
            target: grid.headerItem
            ignoreUnknownSignals: true
            function onHeightChanged() { if (grid.pinnedTop) grid.positionViewAtBeginning() }
        }

        header: PageHeader {
            width: grid.width - grid.leftMargin - grid.rightMargin
            inset: 8
            title: page.view.name || ""
            subtitle: page.subtitle
            showBack: page.showBack
            loading: groups.loading
            onBack: page.StackView.view.pop()
            detail: groups.count > 0 ? page.nounFor(groups.count) : (groups.loading ? qsTr("Wird geladen …") : "")
            PillButton {
                primary: true
                text: qsTr("Alle abspielen")
                iconName: "play"
                enabled: groups.count > 0
                onClicked: Library.playSpec({ filters: page.baseSpec.filters || [], sort: "album" }, false)
            }
            PillButton {
                text: qsTr("Zufällig")
                iconName: "shuffle"
                enabled: groups.count > 0
                onClicked: Library.playSpec({ filters: page.baseSpec.filters || [], sort: "album" }, true)
            }
            PillButton {
                text: qsTr("Sortieren")
                iconName: "list"
                onClicked: sortMenu.popup()
                HMenu {
                    id: sortMenu
                    Repeater {
                        model: page.groupBy === "album"
                               ? [{ k: "name", l: qsTr("Künstler") }, { k: "title", l: qsTr("Albumtitel") },
                                  { k: "year", l: qsTr("Erscheinungsjahr") }, { k: "added", l: qsTr("Zuletzt hinzugefügt") },
                                  { k: "count", l: qsTr("Anzahl Titel") }]
                               : [{ k: "name", l: qsTr("Name") }, { k: "count", l: qsTr("Anzahl Titel") }]
                        HMenuItem {
                            text: modelData.l
                            checkable: true
                            checked: page.groupSort === modelData.k
                            onTriggered: page.groupSort = modelData.k
                        }
                    }
                }
            }
        }

        delegate: Item {
            id: cell
            width: grid.cellWidth
            height: grid.cellHeight

            // ---- cover grid ----
            Item {
                visible: page.layout === "grid"
                anchors.fill: parent
                anchors.margins: 8
                NeuBox {
                    visible: Theme.style.controls === "neu"
                    x: -6; y: -6
                    width: art.width + 12
                    height: art.height + 12
                    radius: art.radius + 6
                    mode: hover.containsMouse ? "convex" : "raised"
                    depth: 6
                }
                CoverArt {
                    id: art
                    width: parent.width
                    height: width
                    radius: page.round ? width / 2 : Theme.radius
                    requestSize: 256
                    coverId: page.layout === "grid" ? model.coverId : 0
                    label: model.title || ""
                    scale: hover.containsMouse ? 1.02 : 1
                    Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                }
                // Play straight from the grid without opening the album.
                Rectangle {
                    visible: hover.containsMouse
                    width: 44; height: 44; radius: 22
                    anchors.right: art.right
                    anchors.bottom: art.bottom
                    anchors.margins: page.round ? art.width * 0.12 : 10
                    color: Theme.accent
                    Icon { anchors.centerIn: parent; anchors.horizontalCenterOffset: 1; name: "play"; size: 20; color: Theme.onAccent }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Library.playGroup(groups, index, false)
                    }
                }
                Text {
                    anchors.top: art.bottom
                    anchors.topMargin: 10
                    width: parent.width
                    horizontalAlignment: page.round ? Text.AlignHCenter : Text.AlignLeft
                    text: model.title || ""
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                Text {
                    anchors.top: art.bottom
                    anchors.topMargin: 30
                    width: parent.width
                    horizontalAlignment: page.round ? Text.AlignHCenter : Text.AlignLeft
                    text: page.groupBy === "album" && model.year > 0 ? model.subtitle + ", " + model.year : (model.subtitle || "")
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: Theme.textDim
                }
            }

            // ---- tiles ----
            Rectangle {
                visible: page.layout !== "grid"
                anchors.fill: parent
                anchors.margins: 6
                radius: Theme.radius
                readonly property string kind: Theme.style.controls || "flat"
                color: kind === "neu" ? "transparent"
                     : kind === "glass" ? Qt.rgba(1, 1, 1, hover.containsMouse ? 0.18 : 0.1)
                     : (hover.containsMouse ? Theme.surfaceHover : Theme.alpha(Theme.surface, 0.8))
                border.color: kind === "glass" ? Qt.rgba(1, 1, 1, 0.3) : (kind === "neu" ? "transparent" : Theme.alpha(Theme.border, 0.7))
                NeuBox {
                    visible: parent.kind === "neu"
                    anchors.fill: parent
                    radius: parent.radius
                    mode: hover.containsMouse ? "convex" : "raised"
                    depth: 5
                }
                CoverArt {
                    id: tileArt
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 64; height: 64
                    radius: Theme.radius - 2
                    requestSize: 128
                    coverId: page.layout !== "grid" ? model.coverId : 0
                    label: model.title || ""
                    showInitial: false
                }
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 18
                    anchors.right: tileArt.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text {
                        width: parent.width
                        text: model.title || ""
                        elide: Text.ElideRight
                        font.family: Theme.displayFont
                        font.pixelSize: 19
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }
                    Text {
                        width: parent.width
                        text: model.subtitle || ""
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        color: Theme.textDim
                    }
                }
            }

            MouseArea {
                id: hover
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                z: -1
                onClicked: (m) => {
                    if (m.button === Qt.RightButton) {
                        ctx.row = index
                        ctx.popup()
                    } else {
                        window.openGroup(groups, index, page.groupBy)
                    }
                }
            }
        }
    }

    // A–Z jump bar
    Column {
        visible: page.letterJump
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0
        Repeater {
            model: ["#", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"]
            Text {
                width: 20
                height: Math.min(18, (page.height - 80) / 27)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: modelData
                font.family: Theme.uiFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
                color: jump.containsMouse ? Theme.accent : Theme.textFaint
                MouseArea {
                    id: jump
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: grid.positionViewAtIndex(groups.indexForLetter(modelData), GridView.Beginning)
                }
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 60
        visible: !groups.loading && groups.count === 0
        iconName: "album"
        title: Library.empty ? qsTr("Noch keine Musik hier") : qsTr("Diese Ansicht ist leer")
        text: Library.empty ? qsTr("Füge in den Einstellungen deinen Musikordner hinzu. Die ersten Titel erscheinen schon während des Scans.") : ""
        actionText: Library.empty ? qsTr("Musikordner hinzufügen") : ""
        onAction: window.openSettings()
    }

    HMenu {
        id: ctx
        property int row: -1
        HMenuItem { text: qsTr("Abspielen"); onTriggered: Library.playGroup(groups, ctx.row, false) }
        HMenuItem { text: qsTr("Zufällig abspielen"); onTriggered: Library.playGroup(groups, ctx.row, true) }
        HMenuItem { text: qsTr("Als Nächstes spielen"); onTriggered: Library.enqueueGroup(groups, ctx.row, true) }
        HMenuItem { text: qsTr("Zur Warteschlange hinzufügen"); onTriggered: Library.enqueueGroup(groups, ctx.row, false) }
    }
}
