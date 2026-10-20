// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Settings tab "Jellyfin": add a server (password or Quick Connect), then per
// server: libraries, streaming quality, playback reporting and sync schedule.
Column {
    id: jf
    spacing: 34

    // Sign-in flow: 0 address, 1 user + password, 2 Quick Connect code
    property int step: 0
    property string url: ""
    property string serverName: ""
    property string serverVersion: ""
    property bool quickAvailable: false
    property string error: ""
    property var confirmRemove: -1

    function ago(secs) {
        if (!secs) return qsTr("noch nie")
        var d = Math.max(0, Date.now() / 1000 - secs)
        if (d < 90) return qsTr("gerade eben")
        if (d < 3600) return qsTr("vor %1 Minuten").arg(Math.round(d / 60))
        if (d < 2 * 3600) return qsTr("vor einer Stunde")
        if (d < 86400) return qsTr("vor %1 Stunden").arg(Math.round(d / 3600))
        if (d < 2 * 86400) return qsTr("gestern")
        return qsTr("am %1").arg(new Date(secs * 1000).toLocaleDateString(Qt.locale(), Locale.ShortFormat))
    }

    Connections {
        target: Jellyfin
        function onProbeResult(ok, url, name, version, quick, err) {
            jf.error = err
            if (!ok) return
            jf.url = url
            jf.serverName = name
            jf.serverVersion = version
            jf.quickAvailable = quick
            jf.step = 1
            userField.focusInput()
        }
        function onQuickChanged() { if (Jellyfin.quickCode !== "") jf.step = 2 }
        function onLoginResult(ok, err) {
            jf.error = err
            if (ok) {
                jf.step = 0
                urlField.text = ""
                userField.text = ""
                passField.text = ""
            } else if (jf.step === 2) {
                jf.step = 1
            }
        }
    }

    // ---------------- servers ----------------
    SettingsSection {
        title: qsTr("Jellyfin")
        text: qsTr("Musik von deinem Jellyfin-Server erscheint zusammen mit deinen lokalen Titeln in allen Ansichten, in der Ordneransicht als eigene Bibliothek. Abgeglichen werden nur die Titelinfos; gestreamt wird erst, was du abspielst.")

        Repeater {
            model: Jellyfin.servers
            delegate: Rectangle {
                id: card
                property var srv: modelData
                width: jf.width
                height: cardCol.height + 36
                radius: Theme.radius
                color: Theme.alpha(Theme.surface, 0.85)
                border.color: Theme.alpha(Theme.border, 0.7)

                Column {
                    id: cardCol
                    x: 20
                    y: 18
                    width: parent.width - 40
                    spacing: 16

                    // Head: name, status, account
                    Row {
                        spacing: 14
                        width: parent.width
                        Rectangle {
                            width: 44; height: 44
                            radius: Math.min(22, Theme.radius + 8)
                            color: Theme.alpha(Theme.accent, 0.15)
                            Icon { anchors.centerIn: parent; name: "server"; size: 24; color: Theme.accent }
                        }
                        Column {
                            width: parent.width - 58
                            spacing: 3
                            Row {
                                spacing: 10
                                Text {
                                    text: card.srv.name
                                    font.family: Theme.uiFont
                                    font.pixelSize: 17
                                    font.weight: Font.Bold
                                    color: Theme.text
                                }
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: statusText.implicitWidth + 16
                                    height: 22
                                    radius: 11
                                    readonly property color c: card.srv.status === "online" ? "#4caf50"
                                                             : card.srv.status === "offline" ? "#ff9800"
                                                             : card.srv.status === "denied" ? "#f44336" : Theme.textFaint
                                    color: Qt.rgba(c.r, c.g, c.b, 0.18)
                                    Text {
                                        id: statusText
                                        anchors.centerIn: parent
                                        text: card.srv.status === "online" ? qsTr("verbunden")
                                            : card.srv.status === "offline" ? qsTr("nicht erreichbar")
                                            : card.srv.status === "denied" ? qsTr("bitte neu anmelden") : qsTr("wird geprüft …")
                                        font.family: Theme.uiFont
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        color: parent.c
                                    }
                                }
                            }
                            Text {
                                width: parent.width
                                elide: Text.ElideMiddle
                                text: qsTr("%1 auf %2").arg(card.srv.userName).arg(card.srv.url)
                                      + (card.srv.version ? " · Jellyfin " + card.srv.version : "")
                                font.family: Theme.uiFont
                                font.pixelSize: 13
                                color: Theme.textDim
                            }
                            Text {
                                width: parent.width
                                wrapMode: Text.Wrap
                                text: qsTr("%1 Titel · zuletzt abgeglichen %2").arg(Library.formatCount(card.srv.trackCount)).arg(jf.ago(card.srv.lastSync))
                                      + (!card.srv.syncing && card.srv.syncText ? " · " + card.srv.syncText : "")
                                font.family: Theme.uiFont
                                font.pixelSize: 13
                                color: Theme.textDim
                            }
                        }
                    }

                    // Sync progress
                    Column {
                        visible: card.srv.syncing
                        width: parent.width
                        spacing: 6
                        Rectangle {
                            width: parent.width
                            height: 6
                            radius: 3
                            color: Theme.alpha(Theme.text, 0.1)
                            Rectangle {
                                width: Math.max(6, parent.width * card.srv.progress)
                                height: parent.height
                                radius: 3
                                color: Theme.accent
                                Behavior on width { NumberAnimation { duration: 300 } }
                            }
                        }
                        Text {
                            text: card.srv.syncText
                            font.family: Theme.uiFont
                            font.pixelSize: 13
                            color: Theme.textDim
                        }
                    }

                    Flow {
                        width: parent.width
                        spacing: 10
                        PillButton {
                            compact: true
                            primary: card.srv.status === "denied"
                            visible: card.srv.status === "denied"
                            text: qsTr("Neu anmelden")
                            iconName: "server"
                            onClicked: { urlField.text = card.srv.url; jf.error = ""; Jellyfin.probe(card.srv.url) }
                        }
                        PillButton {
                            compact: true
                            text: card.srv.syncing ? qsTr("Abgleich abbrechen") : qsTr("Jetzt abgleichen")
                            iconName: "refresh"
                            onClicked: card.srv.syncing ? Jellyfin.cancelSync(card.srv.id) : Jellyfin.sync(card.srv.id, false)
                        }
                        PillButton {
                            compact: true
                            enabled: !card.srv.syncing
                            text: qsTr("Vollständig neu abgleichen")
                            iconName: "layers"
                            onClicked: Jellyfin.sync(card.srv.id, true)
                        }
                        PillButton {
                            compact: true
                            text: jf.confirmRemove === card.srv.id ? qsTr("Wirklich entfernen?") : qsTr("Abmelden und entfernen")
                            iconName: "delete"
                            primary: jf.confirmRemove === card.srv.id
                            onClicked: {
                                if (jf.confirmRemove === card.srv.id) {
                                    jf.confirmRemove = -1
                                    Jellyfin.remove(card.srv.id)
                                } else {
                                    jf.confirmRemove = card.srv.id
                                    confirmReset.restart()
                                }
                            }
                        }
                    }

                    SettingsOption {
                        width: parent.width
                        label: qsTr("Musikbibliotheken")
                        hint: card.srv.libraries.length === 0 ? qsTr("Keine Musikbibliothek gefunden (oder noch nicht geladen).")
                                                               : qsTr("Abgewählte Bibliotheken werden beim nächsten Abgleich aus BuB3Player entfernt.")
                        Column {
                            width: parent.width
                            spacing: 4
                            Repeater {
                                model: card.srv.libraries
                                HToggle {
                                    label: modelData.name
                                    checked: modelData.enabled
                                    onToggled: Jellyfin.setLibraryEnabled(card.srv.id, modelData.id, checked)
                                }
                            }
                        }
                    }

                    SettingsOption {
                        width: parent.width
                        label: qsTr("Streaming-Qualität")
                        hint: card.srv.quality === "original"
                              ? qsTr("Die Originaldatei, unverändert: verlustfrei, lückenlos, ReplayGain bleibt erhalten.")
                              : qsTr("Größere Dateien wandelt der Server in MP3 mit höchstens dieser Bitrate um (spart Datenvolumen, Spulen geht dann nur eingeschränkt).")
                        HSegmented {
                            width: parent.width
                            options: [qsTr("Original"), "320 kbit/s", "192 kbit/s", "128 kbit/s"]
                            values: ["original", "320", "192", "128"]
                            current: card.srv.quality
                            onPicked: (v) => Jellyfin.setServerOption(card.srv.id, "quality", v)
                        }
                    }

                    SettingsOption {
                        width: parent.width
                        label: qsTr("Automatisch abgleichen")
                        HSegmented {
                            width: parent.width
                            options: [qsTr("Beim Start"), qsTr("Alle 6 Stunden"), qsTr("Täglich"), qsTr("Nur von Hand")]
                            values: ["start", "6h", "24h", "manual"]
                            current: card.srv.autoSync
                            onPicked: (v) => Jellyfin.setServerOption(card.srv.id, "autoSync", v)
                        }
                    }

                    HToggle {
                        width: parent.width
                        label: qsTr("Wiedergabe an den Server melden (Wiedergabezähler, „Zuletzt gespielt“, im Dashboard sichtbar)")
                        checked: card.srv.report
                        onToggled: Jellyfin.setServerOption(card.srv.id, "report", checked)
                    }
                }
            }
        }

        Timer { id: confirmReset; interval: 4000; onTriggered: jf.confirmRemove = -1 }

        HToggle {
            width: jf.width
            visible: Jellyfin.servers.length > 0
            label: qsTr("Lokale Titel bevorzugen: was es lokal und auf dem Server gibt, nur einmal zeigen und lokal abspielen")
            checked: Jellyfin.preferLocal
            onToggled: Jellyfin.preferLocal = checked
        }
    }

    // ---------------- add a server ----------------
    SettingsSection {
        title: Jellyfin.servers.length > 0 ? qsTr("Weiteren Server hinzufügen") : qsTr("Server hinzufügen")
        text: jf.step === 0 ? qsTr("Adresse deines Jellyfin-Servers, z. B. 192.168.1.20:8096 oder https://jellyfin.example.de")
            : jf.step === 1 ? qsTr("Gefunden: %1 (Jellyfin %2). Melde dich mit deinem Jellyfin-Konto an.").arg(jf.serverName).arg(jf.serverVersion)
            : qsTr("Öffne Jellyfin in einem Browser oder in der App, gehe zu Profil → Quick Connect und gib diesen Code ein:")

        // Step 0: address
        Row {
            visible: jf.step === 0
            spacing: 8
            Field {
                id: urlField
                width: Math.min(460, jf.width - 150)
                placeholder: qsTr("Serveradresse")
                onAccepted: if (text.length > 0) { jf.error = ""; Jellyfin.probe(text) }
            }
            PillButton {
                anchors.verticalCenter: parent.verticalCenter
                primary: true
                compact: true
                text: Jellyfin.busy ? qsTr("Suche …") : qsTr("Verbinden")
                iconName: "server"
                enabled: urlField.text.length > 0 && !Jellyfin.busy
                onClicked: { jf.error = ""; Jellyfin.probe(urlField.text) }
            }
        }

        // Step 1: user + password
        Column {
            visible: jf.step === 1
            spacing: 10
            Field {
                id: userField
                width: Math.min(360, jf.width)
                placeholder: qsTr("Benutzername")
                onAccepted: passField.focusInput()
            }
            Field {
                id: passField
                width: Math.min(360, jf.width)
                placeholder: qsTr("Passwort (leer lassen, falls keins)")
                password: true
                onAccepted: if (userField.text.length > 0) { jf.error = ""; Jellyfin.login(jf.url, userField.text, text) }
            }
            Flow {
                width: jf.width
                spacing: 10
                PillButton {
                    compact: true
                    primary: true
                    text: Jellyfin.busy ? qsTr("Anmelden …") : qsTr("Anmelden")
                    enabled: userField.text.length > 0 && !Jellyfin.busy
                    onClicked: { jf.error = ""; Jellyfin.login(jf.url, userField.text, passField.text) }
                }
                PillButton {
                    compact: true
                    visible: jf.quickAvailable
                    text: qsTr("Mit Quick Connect anmelden")
                    iconName: "sparkle"
                    onClicked: { jf.error = ""; jf.step = 2; Jellyfin.startQuickConnect(jf.url) }
                }
                PillButton {
                    compact: true
                    text: qsTr("Andere Adresse")
                    iconName: "back"
                    onClicked: { jf.error = ""; jf.step = 0 }
                }
            }
            Text {
                width: jf.width
                wrapMode: Text.Wrap
                text: qsTr("Das Passwort wird nicht gespeichert. BuB3Player merkt sich nur den Zugangsschlüssel, den der Server bei der Anmeldung vergibt; unter Jellyfin → Dashboard → Geräte kannst du ihn jederzeit widerrufen.")
                font.family: Theme.uiFont
                font.pixelSize: 13
                color: Theme.textFaint
            }
        }

        // Step 2: Quick Connect code
        Column {
            visible: jf.step === 2
            spacing: 14
            Text {
                text: Jellyfin.quickCode !== "" ? Jellyfin.quickCode.replace(/(\d{3})(\d{3})/, "$1 $2") : "… …"
                font.family: Theme.displayFont
                font.pixelSize: 46
                font.weight: Font.Bold
                font.letterSpacing: 4
                color: Theme.accent
            }
            Row {
                spacing: 10
                LoadingBar { width: 120; running: jf.step === 2; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: qsTr("Warte auf Bestätigung …")
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    color: Theme.textDim
                }
            }
            PillButton {
                compact: true
                text: qsTr("Abbrechen")
                iconName: "close"
                onClicked: { Jellyfin.cancelQuickConnect(); jf.step = 1 }
            }
        }

        Text {
            visible: jf.error !== ""
            width: jf.width
            wrapMode: Text.Wrap
            text: jf.error
            font.family: Theme.uiFont
            font.pixelSize: 13
            color: "#ff8a80"
        }
    }

    component Field: Rectangle {
        id: fld
        property alias text: input.text
        property string placeholder
        property bool password: false
        signal accepted()
        function focusInput() { input.forceActiveFocus() }
        height: 38
        radius: Math.min(19, Theme.radius + 9)
        color: Theme.alpha(Theme.text, 0.06)
        border.color: input.activeFocus ? Theme.accent : "transparent"
        TextField {
            id: input
            anchors.fill: parent
            leftPadding: 16
            rightPadding: 12
            placeholderText: fld.placeholder
            placeholderTextColor: Theme.textFaint
            echoMode: fld.password ? TextInput.Password : TextInput.Normal
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: 14
            background: null
            selectByMouse: true
            inputMethodHints: fld.password ? Qt.ImhSensitiveData | Qt.ImhNoPredictiveText : Qt.ImhNoAutoUppercase
            onAccepted: fld.accepted()
        }
    }
}
