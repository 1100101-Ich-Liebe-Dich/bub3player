import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// "Zur Playlist hinzufügen" submenu: a new playlist or one of the existing ones.
HMenu {
    id: m
    title: qsTr("Zur Playlist hinzufügen")
    signal chosen(var playlistId) // 0: create a new one first
    HMenuItem {
        text: qsTr("Neue Playlist …")
        onTriggered: m.chosen(0)
    }
    MenuSeparator {
        visible: Playlists.normalPlaylists.length > 0
        height: visible ? implicitHeight : 0
        contentItem: Rectangle { implicitHeight: 1; color: Theme.border }
    }
    Repeater {
        model: Playlists.normalPlaylists
        HMenuItem {
            text: modelData.name
            onTriggered: m.chosen(modelData.id)
        }
    }
}
