import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Halcyon

Rectangle {
    id: side
    property string currentId: ""
    property alias searchText: search.text
    signal open(var view)
    signal searchRequested(string text)
    function focusSearch() { search.forceActiveFocus(); search.selectAll() }
    function clearSearch() { search.text = "" }

    property bool transparent: false
    property bool showBrand: true
    property bool showSearch: true
    color: transparent ? "transparent" : Theme.alpha(Theme.panel, Theme.ambient > 0 ? 0.78 : 1)

    Rectangle {
        visible: !side.transparent
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.alpha(Theme.border, 0.6)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 18
        anchors.bottomMargin: 12
        spacing: 0

        Brand {
            visible: side.showBrand
            Layout.leftMargin: 22
            Layout.bottomMargin: 16
            maxWidth: 210
        }

        // Search
        Rectangle {
            visible: side.showSearch
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 14
            height: 38
            radius: Theme.radius >= 6 ? 19 : Theme.radius
            readonly property string kind: Theme.style.controls || "flat"
            color: kind === "neu" ? "transparent"
                 : kind === "glass" ? Qt.rgba(1, 1, 1, search.activeFocus ? 0.18 : 0.1)
                 : (search.activeFocus ? Theme.surface : Theme.alpha(Theme.text, 0.06))
            border.width: kind === "glass" ? 1 : (search.activeFocus ? 1 : 0)
            border.color: kind === "glass" ? Qt.rgba(1, 1, 1, search.activeFocus ? 0.55 : 0.3) : Theme.alpha(Theme.accent, 0.7)
            NeuBox {
                visible: parent.kind === "neu"
                anchors.fill: parent
                radius: parent.radius
                mode: "inset"
                depth: 4
            }
            Icon {
                id: searchIcon
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: 18
                color: Theme.textDim
            }
            TextField {
                id: search
                anchors.left: searchIcon.right
                anchors.right: clear.left
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: 8
                placeholderText: qsTr("Titel, Künstler, Album …")
                placeholderTextColor: Theme.textFaint
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: 14
                selectByMouse: true
                background: null
                onTextEdited: debounce.restart()
                Keys.onEscapePressed: { text = ""; side.searchRequested(""); focus = false }
                Keys.onReturnPressed: { debounce.stop(); side.searchRequested(text) }
            }
            IconButton {
                id: clear
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 30; height: 30
                iconName: "close"
                iconSize: 16
                visible: search.text.length > 0
                onClicked: { search.text = ""; side.searchRequested("") }
            }
            Timer {
                id: debounce
                interval: 280
                onTriggered: side.searchRequested(search.text)
            }
        }

        // Views
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: navColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 6 }

            Column {
                id: navColumn
                width: parent.width
                Repeater {
                    model: Library.views
                    Column {
                        width: navColumn.width
                        readonly property bool newSection: index === 0 || Library.views[index - 1].section !== modelData.section
                        Text {
                            visible: parent.newSection
                            leftPadding: 24
                            topPadding: index === 0 ? 2 : 18
                            bottomPadding: 6
                            text: modelData.section
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            color: Theme.textFaint
                        }
                        NavItem {
                            width: parent.width
                            label: modelData.name
                            iconName: modelData.icon
                            current: side.currentId === modelData.id
                            onClicked: side.open(modelData)
                        }
                    }
                }
            }
        }

        // Scan status
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Library.scanner.running && !Library.scanner.background ? 54 : 0
            visible: Layout.preferredHeight > 0
            clip: true
            Behavior on Layout.preferredHeight { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            Column {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 20
                anchors.topMargin: 8
                spacing: 6
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    color: Theme.textDim
                    text: {
                        var s = Library.scanner
                        if (s.phase === 1) return qsTr("Ordner einlesen: %1 Dateien").arg(Library.formatCount(s.filesSeen))
                        if (s.phase === 2) return qsTr("Tags lesen: %1 von %2").arg(Library.formatCount(s.tagsDone)).arg(Library.formatCount(s.tagsTotal))
                        if (s.phase === 3) return qsTr("Suchindex wird aufgebaut")
                        return qsTr("Bibliothek wird geprüft")
                    }
                }
                Rectangle {
                    width: parent.width
                    height: 3
                    radius: 1.5
                    color: Theme.alpha(Theme.text, 0.1)
                    Rectangle {
                        height: parent.height
                        radius: 1.5
                        color: Theme.accent
                        width: Library.scanner.phase === 2 && Library.scanner.tagsTotal > 0
                               ? parent.width * Library.scanner.tagsDone / Library.scanner.tagsTotal
                               : parent.width * 0.25
                        Behavior on width { NumberAnimation { duration: 200 } }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 4
            height: 1
            color: Theme.alpha(Theme.border, 0.8)
        }
        NavItem {
            Layout.fillWidth: true
            Layout.topMargin: 8
            label: qsTr("Einstellungen")
            iconName: "tune"
            current: side.currentId === "settings"
            onClicked: side.open({ id: "settings", kind: "settings", name: qsTr("Einstellungen") })
        }
        Text {
            Layout.leftMargin: 24
            Layout.topMargin: 6
            text: Library.trackCount > 0 ? qsTr("%1 Titel").arg(Library.formatCount(Library.trackCount)) : ""
            font.family: Theme.uiFont
            font.pixelSize: 12
            color: Theme.textFaint
        }
    }

    component NavItem: AbstractButton {
        id: nav
        property string label
        property string iconName
        property bool current: false
        height: 36
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        background: Item {
            Surface {
                x: 12
                width: nav.width - 24
                height: nav.height
                radius: Theme.radius - 2
                quiet: true
                hovered: nav.hovered
                pressed: nav.pressed
                active: nav.current
                focused: nav.visualFocus
                flatColor: nav.current ? Theme.alpha(Theme.accent, Theme.dark ? 0.16 : 0.13)
                                       : nav.hovered ? Theme.alpha(Theme.text, 0.06) : "transparent"
            }
        }
        contentItem: Item {
            Icon {
                id: navIcon
                x: 24
                anchors.verticalCenter: parent.verticalCenter
                name: nav.iconName
                size: 18
                color: nav.current ? Theme.accent : Theme.textDim
            }
            Text {
                anchors.left: navIcon.right
                anchors.leftMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: nav.label
                elide: Text.ElideRight
                font.family: Theme.uiFont
                font.pixelSize: 14
                font.weight: nav.current ? Font.DemiBold : Font.Medium
                color: nav.current ? Theme.text : Theme.textDim
            }
        }
    }
}
