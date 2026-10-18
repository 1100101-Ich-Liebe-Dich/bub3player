import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// "Für dich": mixes from the recommendations (like what you played last,
// your time-of-day mix, rediscover, new to you). Each mix plays as a radio:
// when it ends, it carries on in the same direction.
Item {
    id: page
    property string pageId: "foryou"
    readonly property int inset: 36

    Component.onCompleted: Recommend.loadMixes(false)

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: col.height + 60
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HScrollBar {}

        Column {
            id: col
            width: flick.width
            spacing: 30

            PageHeader {
                width: parent.width
                title: qsTr("Für dich")
                subtitle: Recommend.learnedText
                loading: Recommend.mixesLoading
                PillButton {
                    text: qsTr("Neu mischen")
                    iconName: "refresh"
                    enabled: !Recommend.mixesLoading
                    onClicked: Recommend.loadMixes(true)
                }
            }

            Text {
                x: page.inset
                visible: Recommend.mixesLoading && Recommend.mixes.length === 0
                text: qsTr("Wird zusammengestellt …")
                font.family: Theme.uiFont
                font.pixelSize: 14
                color: Theme.textDim
            }

            Repeater {
                model: Recommend.mixes
                delegate: Column {
                    id: mix
                    readonly property int mixIndex: index
                    width: col.width
                    spacing: 14

                    Item {
                        x: page.inset
                        width: col.width - 2 * page.inset
                        height: Math.max(head.height, buttons.height)
                        Column {
                            id: head
                            width: parent.width - buttons.width - 20
                            spacing: 3
                            Text {
                                width: parent.width
                                elide: Text.ElideRight
                                text: modelData.title
                                font.family: Theme.displayFont
                                font.pixelSize: 22
                                font.weight: Font.Bold
                                font.capitalization: Theme.style.titleUpper ? Font.AllUppercase : Font.MixedCase
                                color: Theme.text
                            }
                            Text {
                                width: parent.width
                                elide: Text.ElideRight
                                text: modelData.subtitle + " · " + qsTr("%1 Titel").arg(modelData.ids.length)
                                font.family: Theme.uiFont
                                font.pixelSize: 13
                                color: Theme.textDim
                            }
                        }
                        Row {
                            id: buttons
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            PillButton {
                                compact: true
                                primary: true
                                text: qsTr("Abspielen")
                                iconName: "play"
                                onClicked: Recommend.playMix(mix.mixIndex, 0, false)
                            }
                            PillButton {
                                compact: true
                                text: qsTr("Zufällig")
                                iconName: "shuffle"
                                onClicked: Recommend.playMix(mix.mixIndex, 0, true)
                            }
                        }
                    }

                    ListView {
                        id: strip
                        width: col.width
                        height: 212
                        orientation: ListView.Horizontal
                        leftMargin: page.inset - 8
                        rightMargin: page.inset - 8
                        spacing: 0
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        reuseItems: true
                        ScrollBar.horizontal: HScrollBar {}
                        model: TrackListModel { spec: ({ ids: modelData.ids }) }
                        delegate: Item {
                            width: 166
                            height: strip.height
                            Item {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                CoverArt {
                                    id: art
                                    width: parent.width
                                    height: width
                                    radius: Theme.radius
                                    requestSize: 256
                                    coverId: model.trackId
                                    label: model.album || ""
                                    scale: hover.containsMouse ? 1.03 : 1
                                    Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                                }
                                Rectangle {
                                    visible: hover.containsMouse
                                    width: 40; height: 40; radius: 20
                                    anchors.right: art.right
                                    anchors.bottom: art.bottom
                                    anchors.margins: 8
                                    color: Theme.accent
                                    Icon { anchors.centerIn: parent; anchors.horizontalCenterOffset: 1; name: "play"; size: 18; color: Theme.onAccent }
                                }
                                Text {
                                    anchors.top: art.bottom
                                    anchors.topMargin: 9
                                    width: parent.width
                                    text: model.title || ""
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    color: Theme.text
                                }
                                Text {
                                    anchors.top: art.bottom
                                    anchors.topMargin: 28
                                    width: parent.width
                                    text: model.artist || ""
                                    elide: Text.ElideRight
                                    font.family: Theme.uiFont
                                    font.pixelSize: 13
                                    color: Theme.textDim
                                }
                                MouseArea {
                                    id: hover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Recommend.playMix(mix.mixIndex, index, false)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 60
        visible: !Recommend.mixesLoading && Recommend.mixes.length === 0
        iconName: "heart"
        title: Library.empty ? qsTr("Noch keine Musik hier") : qsTr("Noch keine Mischungen")
        text: Library.empty ? qsTr("Füge in den Einstellungen deinen Musikordner hinzu.")
                            : qsTr("Hör ein bisschen Musik: aus deinem Hörverlauf entstehen hier Mixe für dich.")
        actionText: Library.empty ? qsTr("Musikordner hinzufügen") : ""
        onAction: window.openSettings()
    }
}
