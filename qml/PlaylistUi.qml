import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import Halcyon

// Dialogs around playlists, shared by all pages and themes (window.playlistUi).
Item {
    id: ui

    function openPlaylist(id) {
        var v = Library.viewById("playlist-" + id)
        if (v && v.id) window.openView(v)
    }
    // addFn(id): called with the new playlist (e.g. to add the right-clicked tracks).
    function newPlaylist(addFn) {
        createDlg.addFn = addFn || null
        createDlg.smart = false
        nameField.text = Playlists.uniqueName(qsTr("Neue Playlist"))
        createDlg.open()
        nameField.focusInput()
    }
    // Menu entry "Zur Playlist hinzufügen": id 0 = create one first.
    function addTo(id, addFn) {
        if (id > 0) addFn(id)
        else newPlaylist(addFn)
    }
    function rename(id) {
        var p = Playlists.get(id)
        if (!p.id) return
        renameDlg.playlistId = id
        renameField.text = p.name
        renameDlg.open()
        renameField.focusInput()
    }
    function confirmDelete(id) {
        var p = Playlists.get(id)
        if (!p.id) return
        deleteDlg.playlistId = id
        deleteDlg.text = (p.smart ? qsTr("Die Smart-Playlist »%1« wird gelöscht.") : qsTr("Die Playlist »%1« wird gelöscht."))
                         .arg(p.name) + " " + qsTr("Die Titel selbst bleiben in der Bibliothek.")
        deleteDlg.open()
    }
    function editRules(id) { editor.openFor(id, "") }
    function importFile() { importDlg.open() }
    function exportFile(id) {
        exportDlg.playlistId = id
        exportDlg.selectedFile = exportDlg.currentFolder + "/" + Playlists.suggestedExportName(id)
        exportDlg.open()
    }

    HDialog {
        id: createDlg
        property var addFn: null
        property bool smart: false
        title: qsTr("Neue Playlist")
        acceptText: smart ? qsTr("Weiter") : qsTr("Anlegen")
        acceptEnabled: nameField.text.trim() !== ""
        HSegmented {
            visible: createDlg.addFn === null
            options: [{ value: false, label: qsTr("Playlist") }, { value: true, label: qsTr("Smart-Playlist") }]
            current: createDlg.smart
            onPicked: (v) => {
                createDlg.smart = v
                var plain = Playlists.uniqueName(qsTr("Neue Playlist")), smart = Playlists.uniqueName(qsTr("Neue Smart-Playlist"))
                if (nameField.text === plain || nameField.text === smart)
                    nameField.text = v ? smart : plain
            }
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: createDlg.smart ? qsTr("Füllt sich selbst nach Regeln, zum Beispiel „Hi-Res, lange nicht gehört“, und bleibt immer aktuell.")
                                  : qsTr("Titel kommen per Rechtsklick → „Zur Playlist hinzufügen“ dazu, die Reihenfolge lässt sich per Ziehen ändern.")
            font.family: Theme.uiFont
            font.pixelSize: 13
            color: Theme.textDim
        }
        HTextField {
            id: nameField
            width: parent.width
            placeholder: qsTr("Name")
            onAccepted: createDlg.accept()
        }
        extraButtons: PillButton {
            visible: createDlg.addFn === null
            text: qsTr("M3U importieren …")
            iconName: "upload"
            onClicked: { createDlg.close(); importDlg.open() }
        }
        onAccepted: {
            if (smart) {
                editor.openFor(0, nameField.text)
                return
            }
            var id = Playlists.create(nameField.text)
            if (id <= 0) return
            if (addFn) addFn(id)
            else ui.openPlaylist(id)
        }
    }

    HDialog {
        id: renameDlg
        property var playlistId: 0
        title: qsTr("Umbenennen")
        acceptText: qsTr("Speichern")
        acceptEnabled: renameField.text.trim() !== ""
        HTextField {
            id: renameField
            width: parent.width
            placeholder: qsTr("Name")
            onAccepted: renameDlg.accept()
        }
        onAccepted: Playlists.rename(playlistId, renameField.text)
    }

    HDialog {
        id: deleteDlg
        property var playlistId: 0
        title: qsTr("Löschen?")
        acceptText: qsTr("Löschen")
        acceptIcon: "delete"
        onAccepted: {
            var wasOpen = window.currentViewId === "playlist-" + playlistId
            Playlists.remove(playlistId)
            if (wasOpen) window.openView(Library.viewById("albums"))
        }
    }

    SmartEditor {
        id: editor
        onSaved: (id, created) => { if (created) ui.openPlaylist(id) }
    }

    FileDialog {
        id: importDlg
        title: qsTr("Playlist importieren")
        fileMode: FileDialog.OpenFile
        currentFolder: Playlists.defaultFolder()
        nameFilters: [qsTr("Playlists (*.m3u *.m3u8)"), qsTr("Alle Dateien (*)")]
        onAccepted: {
            var id = Playlists.importFile(selectedFile)
            if (id > 0) ui.openPlaylist(id)
        }
    }

    FileDialog {
        id: exportDlg
        property var playlistId: 0
        title: qsTr("Playlist exportieren")
        fileMode: FileDialog.SaveFile
        currentFolder: Playlists.defaultFolder()
        defaultSuffix: "m3u8"
        nameFilters: [qsTr("Playlist (*.m3u8)"), qsTr("Playlist, alte Form (*.m3u)")]
        onAccepted: Playlists.exportFile(playlistId, selectedFile)
    }
}
