// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs
import Halcyon

// Chooses the folder audio CDs are read into (the desktop's own dialog, Qt's one
// only where there is none).
PillButton {
    text: qsTr("Anderer Ordner …")
    iconName: "folder"
    onClicked: SystemDialogs.chooseFolder(dlg.title, AudioCd.folder, function (path) {
        if (path === null) dlg.open()
        else if (path !== "") AudioCd.folder = path
    })
    FolderDialog {
        id: dlg
        title: qsTr("Ordner für eingelesene CDs")
        onAccepted: AudioCd.folder = selectedFolder
    }
}
