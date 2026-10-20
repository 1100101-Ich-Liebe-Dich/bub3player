// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import Halcyon

// Modal dialog in the look of the active theme. Put the content inside;
// Enter in a text field should call accept().
Popup {
    id: d
    property string title
    property string text
    property string acceptText: qsTr("OK")
    property string rejectText: qsTr("Abbrechen")
    property string acceptIcon: ""
    property bool acceptEnabled: true
    property bool showReject: true
    property bool destructive: false
    property int dialogWidth: 500
    default property alias content: body.data
    property alias extraButtons: extra.data
    signal accepted()

    function accept() {
        if (!acceptEnabled) return
        d.accepted()
        d.close()
    }

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(dialogWidth, (parent ? parent.width : 800) - 48)
    height: Math.min(implicitHeight, (parent ? parent.height : 600) - 48)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    T.Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }

    background: Item {
        Rectangle { anchors.fill: parent; radius: Theme.radius + 6; color: Theme.bg }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radius + 6
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
        }
    }

    contentItem: Flickable {
        implicitHeight: col.implicitHeight
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HScrollBar {}
        Column {
            id: col
            width: parent.width
            padding: 28
            spacing: 18
            Text {
                width: col.width - 56
                text: d.title
                wrapMode: Text.Wrap
                font.family: Theme.displayFont
                font.pixelSize: 22
                font.weight: Font.DemiBold
                color: Theme.text
            }
            Text {
                visible: d.text !== ""
                width: col.width - 56
                text: d.text
                wrapMode: Text.Wrap
                font.family: Theme.uiFont
                font.pixelSize: 14
                lineHeight: 1.15
                color: Theme.textDim
            }
            Column {
                id: body
                width: col.width - 56
                spacing: 12
            }
            Item {
                width: col.width - 56
                height: buttons.height
                Row {
                    id: extra
                    anchors.left: parent.left
                    spacing: 8
                }
                Row {
                    id: buttons
                    anchors.right: parent.right
                    spacing: 8
                    PillButton {
                        visible: d.showReject
                        text: d.rejectText
                        onClicked: d.close()
                    }
                    PillButton {
                        primary: true
                        text: d.acceptText
                        iconName: d.acceptIcon
                        enabled: d.acceptEnabled
                        opacity: enabled ? 1 : 0.5
                        onClicked: d.accept()
                    }
                }
            }
        }
    }
}
