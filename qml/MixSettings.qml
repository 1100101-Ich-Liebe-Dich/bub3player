import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Settings tab "Mix": transitions between tracks, recommendations (autoplay,
// radio) and the audio analysis both build on.
Column {
    id: mx
    spacing: 34
    property bool confirmReset: false

    function hours(sec) {
        if (sec < 3600) return qsTr("unter einer Stunde")
        if (sec < 2 * 86400) return qsTr("etwa %1 Stunden").arg(Math.round(sec / 3600))
        return qsTr("etwa %1 Tage").arg(Math.round(sec / 86400))
    }

    // ---------------- transitions ----------------
    SettingsSection {
        title: qsTr("Übergänge")
        text: Player.transition === 0
              ? qsTr("Lückenlos: ein Titel geht ohne Pause in den nächsten über, genau wie auf der Platte. Live-Alben und Konzeptalben klingen so, wie sie gedacht sind.")
              : Player.transition === 1
                ? qsTr("Überblenden: der nächste Titel wird gleichmäßig eingeblendet, während der laufende ausklingt (gleiche Lautheit über den ganzen Übergang).")
                : qsTr("Automix: nutzt die Analyse jedes Titels. Stille am Anfang und Ende fällt weg, endet ein Titel mit eigenem Ausblenden, kommt der nächste mitten hinein, bei passendem Tempo wird länger und auf dem Schlag gemischt.")
        Text {
            visible: Player.bitPerfect
            width: mx.width
            wrapMode: Text.Wrap
            text: qsTr("Bit-Perfect ist eingeschaltet (Einstellungen → Klang): Solange spielt alles lückenlos, ohne Überblenden.")
            font.family: Theme.uiFont
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: Theme.accent
        }
        HSegmented {
            width: mx.width
            options: [qsTr("Lückenlos"), qsTr("Überblenden"), qsTr("Automix")]
            values: [0, 1, 2]
            current: Player.transition
            onPicked: (v) => Player.transition = v
        }
        SettingsOption {
            width: mx.width
            visible: Player.transition !== 0
            label: Player.transition === 2 ? qsTr("Länge der Übergänge (Grundwert)") : qsTr("Länge der Überblendung")
            value: Player.fadeSeconds + " s"
            hint: Player.transition === 2 ? qsTr("Automix nimmt bei gleichem Tempo bis zu anderthalbmal so lang, bei sehr unterschiedlichem Tempo kürzer.") : ""
            HSlider {
                width: Math.min(420, mx.width)
                from: 1; to: 15; stepSize: 1
                value: Player.fadeSeconds
                onMoved: Player.fadeSeconds = value
            }
        }
        HToggle {
            width: mx.width
            visible: Player.transition !== 0
            label: qsTr("Alben bleiben lückenlos (aufeinanderfolgende Titel eines Albums werden nie überblendet)")
            checked: Player.albumGapless
            onToggled: Player.albumGapless = checked
        }
        HToggle {
            width: mx.width
            visible: Player.transition !== 0
            label: qsTr("Stille am Anfang und am Ende überspringen")
            checked: Player.skipSilence
            onToggled: Player.skipSilence = checked
        }
        HToggle {
            width: mx.width
            enabled: Player.replayGainAvailable && Player.replayGain > 0
            label: qsTr("Lautstärke angleichen: Titel ohne ReplayGain bekommen ihren Wert aus der Analyse (statt pauschal −6 dB)")
            checked: Player.loudnessMatch
            onToggled: Player.loudnessMatch = checked
        }
    }

    // ---------------- recommendations ----------------
    SettingsSection {
        title: qsTr("Empfehlungen")
        text: qsTr("Autoplay, Radio („Mehr wie dieser Titel“ im Rechtsklick-Menü) und „Für dich“ wählen Musik nach deinem Geschmack: welche Künstler du zusammen hörst, was du überspringst, was du zu welcher Tageszeit hörst, dazu Genre, Zeit, Tempo, Tonart und Energie. Alles bleibt auf deinem Rechner.")
        HToggle {
            width: mx.width
            label: qsTr("Autoplay: am Ende der Warteschlange mit passender Musik weitermachen")
            checked: Queue.autoplay
            onToggled: Queue.autoplay = checked
        }
        SettingsOption {
            width: mx.width
            label: qsTr("Entdeckerfreude")
            hint: Recommend.adventure === 0 ? qsTr("Vor allem Bekanntes und Lieblinge.")
                : Recommend.adventure === 1 ? qsTr("Lieblinge und Neues gemischt.")
                                            : qsTr("Mehr Unbekanntes und Überraschungen.")
            HSegmented {
                width: mx.width
                options: [qsTr("Vertraut"), qsTr("Ausgewogen"), qsTr("Abenteuerlich")]
                values: [0, 1, 2]
                current: Recommend.adventure
                onPicked: (v) => Recommend.adventure = v
            }
        }
        HToggle {
            width: mx.width
            label: qsTr("Aus meinem Hörverlauf lernen")
            checked: Recommend.learning
            onToggled: Recommend.learning = checked
        }
        Text {
            width: mx.width
            wrapMode: Text.Wrap
            text: Recommend.learnedText
            font.family: Theme.uiFont
            font.pixelSize: 13
            color: Theme.textDim
        }
        PillButton {
            compact: true
            text: mx.confirmReset ? qsTr("Wirklich alles Gelernte löschen?") : qsTr("Gelerntes löschen")
            iconName: "delete"
            primary: mx.confirmReset
            onClicked: {
                if (mx.confirmReset) {
                    mx.confirmReset = false
                    Recommend.resetLearning()
                } else {
                    mx.confirmReset = true
                    resetTimer.restart()
                }
            }
            Timer { id: resetTimer; interval: 4000; onTriggered: mx.confirmReset = false }
        }
    }

    // ---------------- analysis ----------------
    SettingsSection {
        title: qsTr("Audioanalyse")
        text: qsTr("Jeder Titel wird einmal angehört: Tempo, Tonart, Lautheit, Energie und wo er anfängt und ausklingt. Der laufende und der nächste Titel kommen immer sofort dran; der Rest der Bibliothek nach und nach im Hintergrund.")
        HSegmented {
            width: mx.width
            options: [qsTr("Nur Gespieltes"), qsTr("Sanft"), qsTr("Zügig")]
            values: [0, 1, 2]
            current: Analysis.mode
            onPicked: (v) => Analysis.mode = v
        }
        Text {
            width: mx.width
            wrapMode: Text.Wrap
            text: Analysis.mode === 0 ? qsTr("Im Hintergrund passiert nichts; analysiert wird nur, was gerade läuft oder als Nächstes kommt.")
                : Analysis.mode === 1 ? qsTr("Ein Kern, etwa ein Drittel davon, mit Pausen. Hält an, solange der Scan läuft oder der Rechner viel zu tun hat. Richtig für ältere Rechner.")
                                      : qsTr("Die Hälfte der Kerne mit voller Kraft. Hält nur an, wenn der Rechner überlastet ist.")
            font.family: Theme.uiFont
            font.pixelSize: 13
            color: Theme.textFaint
        }
        Column {
            width: mx.width
            spacing: 6
            Rectangle {
                width: Math.min(420, mx.width)
                height: 6
                radius: 3
                color: Theme.alpha(Theme.text, 0.1)
                Rectangle {
                    width: Analysis.totalCount > 0 ? Math.max(6, parent.width * Math.min(1, Analysis.analysedCount / Analysis.totalCount)) : 0
                    height: parent.height
                    radius: 3
                    color: Theme.accent
                }
            }
            Text {
                width: mx.width
                wrapMode: Text.Wrap
                text: {
                    if (Analysis.totalCount <= 0)
                        return Analysis.status
                    var pct = Math.floor(100 * Analysis.analysedCount / Analysis.totalCount)
                    var s = qsTr("%1 von %2 lokalen Titeln analysiert (%3 %)").arg(Library.formatCount(Analysis.analysedCount))
                                .arg(Library.formatCount(Analysis.totalCount)).arg(pct)
                    if (Analysis.failedCount > 0)
                        s += ", " + qsTr("davon %1 nicht lesbar").arg(Library.formatCount(Analysis.failedCount))
                    s += ". " + Analysis.status
                    var left = Analysis.totalCount - Analysis.analysedCount
                    if (left > 0 && Analysis.secondsPerTrack > 0 && Analysis.mode > 0)
                        s += " " + qsTr("Rest: %1.").arg(mx.hours(left * Analysis.secondsPerTrack))
                    return s
                }
                font.family: Theme.uiFont
                font.pixelSize: 13
                color: Theme.textDim
            }
        }
        HToggle {
            width: mx.width
            label: qsTr("Jellyfin-Titel beim Abspielen auch analysieren (lädt jeden Titel ein zweites Mal herunter)")
            checked: Analysis.remoteEnabled
            onToggled: Analysis.remoteEnabled = checked
        }
        Text {
            width: mx.width
            wrapMode: Text.Wrap
            text: qsTr("Tempo und Tonart des laufenden Titels stehen in der Großansicht. Unter Entdecken gibt es „Nach Tempo“ und „Nach Tonart“; Tonarten in Camelot-Schreibweise (8A = a-Moll), benachbarte Zahlen passen harmonisch zusammen.")
            font.family: Theme.uiFont
            font.pixelSize: 13
            color: Theme.textFaint
        }
    }
}
