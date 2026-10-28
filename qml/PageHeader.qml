// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Halcyon

// Page title block. With a cover (album pages) it becomes a large hero.
Item {
    id: h
    property string title
    property string subtitle
    property string detail
    property var coverId: 0
    property string coverVersion: ""  // changes when the picture behind coverId does (CD cover found)
    property string artist: ""     // round artist picture instead of the album art
    property bool showBack: false
    property bool loading: false
    property int inset: 36
    default property alias actions: actionRow.data
    signal back()

    readonly property bool hero: !!coverId
    readonly property int topSpace: showBack ? 64 : 36
    implicitHeight: Math.round(Math.max(hero ? topSpace + 188 + 28 : 0, topSpace + content.implicitHeight + 24))

    IconButton {
        id: backBtn
        visible: h.showBack
        x: h.inset - 8
        y: 18
        iconName: "back"
        tip: qsTr("Zurück")
        onClicked: h.back()
    }

    CoverArt {
        id: cover
        visible: h.hero
        x: h.inset
        y: h.topSpace
        width: 188
        height: 188
        radius: h.artist !== "" ? width / 2 : Theme.radius + 2
        requestSize: 512
        coverId: h.coverId
        version: h.coverVersion
        artist: h.artist
        label: h.title
    }

    ColumnLayout {
        id: content
        anchors.left: h.hero ? cover.right : parent.left
        anchors.leftMargin: h.hero ? 28 : h.inset
        anchors.right: parent.right
        anchors.rightMargin: h.inset
        anchors.bottom: parent.bottom
        anchors.bottomMargin: h.hero ? 28 : 24
        spacing: 6

        Text {
            Layout.fillWidth: true
            text: h.title
            font.family: Theme.displayFont
            font.pixelSize: (Theme.style.titleSize || 40) + (h.hero ? 6 : 0)
            font.weight: Theme.style.titleWeight || Font.Bold
            font.letterSpacing: Theme.style.titleSpacing !== undefined ? Theme.style.titleSpacing : -0.8
            font.capitalization: Theme.style.titleUpper ? Font.AllUppercase : Font.MixedCase
            color: Theme.text
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.Wrap
            lineHeight: 0.95
        }
        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: h.subtitle
            font.family: Theme.uiFont
            font.pixelSize: h.hero ? 17 : 14
            font.weight: h.hero ? Font.DemiBold : Font.Medium
            color: h.hero ? Theme.text : Theme.textDim
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: h.detail
            font.family: Theme.uiFont
            font.pixelSize: 13
            color: Theme.textDim
            elide: Text.ElideRight
        }
        Row {
            id: actionRow
            Layout.topMargin: 10
            spacing: 10
        }
    }

    LoadingBar {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: h.inset
        anchors.rightMargin: h.inset
        running: h.loading
    }
}
