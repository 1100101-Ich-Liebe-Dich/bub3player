// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Right-click menu of a group (album, artist, genre …): play, queue, radio, playlist.
HMenu {
    id: m
    property var model
    property int row: -1
    function openFor(groupModel, groupRow) {
        m.model = groupModel
        m.row = groupRow
        m.popup()
    }
    HMenuItem { text: qsTr("Abspielen"); onTriggered: Library.playGroup(m.model, m.row, false) }
    HMenuItem { text: qsTr("Zufällig abspielen"); onTriggered: Library.playGroup(m.model, m.row, true) }
    HMenuItem { text: qsTr("Als Nächstes spielen"); onTriggered: Library.enqueueGroup(m.model, m.row, true) }
    HMenuItem { text: qsTr("Zur Warteschlange hinzufügen"); onTriggered: Library.enqueueGroup(m.model, m.row, false) }
    HMenuItem { text: qsTr("Radio dazu starten"); onTriggered: Recommend.startRadioFromGroup(m.model, m.row) }
    PlaylistMenu {
        onChosen: (id) => {
            var gm = m.model, r = m.row
            window.playlistUi.addTo(id, function (pid) { Playlists.addGroup(pid, gm, r) })
        }
    }
}
