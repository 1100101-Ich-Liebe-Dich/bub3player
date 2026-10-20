## 1.2.1 - 2026-10-20

Vollbild und alles für das öffentliche Repository: Lizenz, Quellcode zum Release, Hinweise.

### Hinzugefügt
- **F11** schaltet das Vollbild an und aus; danach ist das Fenster wieder so groß wie vorher
  (auch maximiert). Ein Hinweis zeigt, wie man zurückkommt. In allen Themes, im Terminal-Theme
  auch in der Tastenleiste.
- Lizenz: BuB3Player ist freie Software unter der **GNU General Public License v3.0 oder
  später** (`LICENSE`). Die Lizenztexte der Schriften, Symbole und mitgelieferten Bibliotheken
  liegen bei (`LICENSES/`, im AppImage unter `usr/share/licenses/`).
- Jedes Release bekommt neben dem AppImage den Quellcode der darin mitgelieferten
  Ubuntu-Pakete (`BuB3Player-<Version>-quellen.tar`).
- AppStream-Daten mit Lizenz, Bildschirmfotos, Inhaltsangabe und Links für Software-Center.
- Hinweise zum Mitmachen (`CONTRIBUTING.md`), zum Melden von Sicherheitslücken (`SECURITY.md`)
  und eine Vorlage für Pull Requests.
- Dependabot hält die GitHub-Aktionen des Bau-Workflows aktuell.

### Geändert
- Terminal-Theme: In der Tastenleiste heißt `^L` jetzt „Großansicht“ (wie in den Einstellungen).
- Dieses Änderungsprotokoll ist nach Kategorien gegliedert.
- „Über“ in den Einstellungen nennt alle Schriften und die Lizenz.
- Bau-Workflow: `actions/upload-artifact` v6 (Node.js 24); nur noch das Release darf ins
  Repository schreiben.
- README: Liedtexte stehen unter „Ausblick“, bis sie fertig sind.

### Entfernt
- Die Verknüpfung mit Audiodateien im Desktop-Eintrag: „Öffnen mit BuB3Player“ tat nichts.
  Kommt wieder, wenn der Player Dateien direkt öffnen kann.

### Sicherheit
- Jellyfin: Ohne angegebenes Schema wird zuerst `https://` versucht, dann `http://`. Antwortet ein
  Server außerhalb des Heimnetzes nur über http, warnt der Player, dass Passwort und
  Zugangsschlüssel unverschlüsselt übertragen werden.

## 1.2.0 - 2026-10-20

### Hinzugefügt
- **Künstlerbilder**: Künstler zeigen ihr eigenes Bild statt eines Albumcovers – aus dem Ordner
  des Künstlers über den Alben (`MUSIK/KÜNSTLER/ALBUM/…`, auch tiefer wie
  `MUSIK/KÜNSTLER/EPs/ALBUM/CD1/…`): `artist.jpg`, `folder.jpg`, `poster.jpg` usw. Der Ordner muss
  wie der Künstler heißen (Groß-/Kleinschreibung, Akzente, Satzzeichen und „The“ egal), so landet
  kein Albumcover als Künstlerbild. Bei Jellyfin kommt das Künstlerbild vom Server. Ohne Bild
  bleibt es beim Cover. Die Seite eines Künstlers zeigt sein Bild groß und rund.
- **Suche** mit passenden **Künstlern** und **Alben** über den Titeln, jeweils in einer Reihe zum
  Seitwärtsblättern (Pfeile, Ziehen, Umschalt + Mausrad). Klick öffnet, Rechtsklick spielt ab,
  reiht ein oder fügt zur Playlist hinzu.
- **A–Z-Leiste**: an der Leiste entlangziehen blättert durch, der Buchstabe wird dabei groß
  angezeigt; Buchstaben ohne Einträge sind blass; auch bei Alben nach Albumtitel sortiert.

### Geändert
- **Bit-Perfect mit Bestätigung**: Nach dem Einschalten erscheint ein zweiter Schalter. Erst wenn
  man bestätigt, dass Verstärker, DAC oder Kopfhörer leise gestellt sind, geht Bit-Perfect an –
  der Ton kommt dabei ohne Lautstärkeregler mit voller Stärke an.
- A–Z-Leiste: Umlaute zählen beim Buchstaben ohne Punkte („Ä“ unter A).

### Behoben
- **A–Z-Leiste**: Ein späteres Neuladen der Liste (Scan, Ordnerüberwachung) zog die Ansicht nach
  einem Sprung wieder nach oben.

## 1.1.0 - 2026-10-20

### Hinzugefügt
- Neuer Reiter **Einstellungen → Klang** (`Strg + E`), in allen acht Themes.
- **10-Band-Equalizer** (31 Hz bis 16 kHz, ±12 dB in halben Schritten) mit 14 Voreinstellungen
  (Mehr Bass, Leise hören, Rock, Klassik, Stimme und Hörbuch, Kleine Lautsprecher …), eigenen
  gespeicherten Kurven und Vorverstärkung. Die automatische Vorverstärkung nimmt so viel zurück,
  wie das lauteste Band anhebt: Nichts übersteuert. Regler ändern den Klang sofort, Doppelklick
  setzt ein Band auf 0 dB.
- **Ausgabegerät wählen**: alle Geräte des Soundservers (PulseAudio, auch PipeWire), ohne die
  Systemeinstellungen anzufassen. Fehlt das Gerät (Kopfhörer abgezogen), spielt der Player über den
  Systemstandard weiter und sagt es. Beim nächsten Titel, den man startet, versucht er es wieder;
  die Wahl bleibt gespeichert.
- **Bit-Perfect über ALSA**: Die Samples gehen unverändert an den DAC, die Abtastrate folgt dem
  Titel. Lautstärke, ReplayGain, Equalizer und Übergänge sind dabei aus. Passt die Bittiefe nicht,
  bekommt das Gerät die Samples verlustfrei in einem breiteren Behälter (16 in 32 Bit); kann es die
  Abtastrate nicht (etwa 22,05 kHz bei Hörbüchern), rechnet der Player sie um und zeigt „nicht
  bit-perfect“. Ist das Gerät belegt oder weg, geht es über den Systemstandard weiter.
- Anzeige, was gerade ankommt und was beim Gerät landet (Bittiefe, Abtastrate, Kanäle).
- Für Tests: `HALCYON_TEST_SINK` ersetzt das Ausgabegerät durch eine beliebige GStreamer-Senke
  (z. B. `filesink`), `GST_DEBUG_DUMP_DOT_DIR` schreibt die Kette beim Abspielen als Graph.

### Geändert
- ReplayGain ist von „Allgemein“ nach „Klang“ umgezogen.
- **Die bisherige Ausgabe bleibt unangetastet**: Mit „Systemstandard“ und ausgeschaltetem
  Equalizer ist die GStreamer-Kette Element für Element dieselbe wie in 1.0 (am Pipeline-Graphen
  verglichen). Equalizer, Gerät und Bit-Perfect kommen nur in die Kette, wenn man sie wählt.

### Behoben
- `--add-folder` nimmt auch relative Pfade.
- „1 Datei geprüft“ in der Einzahl.

## 1.0.0 - 2026-10-19

Erste stabile Version: alles aus den Versionen 0.1 bis 0.9 in einem Paket, ausgiebig getestet
auf alter Hardware und mit einer Bibliothek von einer Million Titeln – Bibliothek für 1 Mio+ Titel
mit Zwei-Phasen-Scanner, Volltextsuche und vielen Ansichten; acht komplett verschiedene Themes;
Wiedergabe lückenlos, mit Überblenden oder Automix, ReplayGain und Lautheitsausgleich;
Audioanalyse im Hintergrund; Empfehlungen, Radio, Autoplay und „Für dich“ – gelernt nur auf dem
eigenen Rechner; Jellyfin in derselben Bibliothek; Playlists, Smart-Playlists, M3U,
Ordnerüberwachung; AppImage für alle Distributionen ab glibc 2.35.

### Hinzugefügt
- `CLAUDE.md` für Mitentwickler (Vorgaben, Aufbau, Tests, Fahrplan) und die Testwerkzeuge in
  `tools/`: Vorführ-Bibliothek, Riesen-Bibliothek, Klick- und Speichertests.

## 0.9.0 - 2026-10-19

### Hinzugefügt
- **Playlists**: anlegen, umbenennen, löschen; Titel, Alben, Künstler, Genres, Ordner,
  Suchergebnisse und die Warteschlange per Rechtsklick oder Knopf „Zur Playlist“ hinzufügen;
  Reihenfolge per Ziehen ändern; jeder Titel nur einmal. Einträge merken sich Pfad, Titel und
  Künstler: Ist ein Titel gerade nicht da (Laufwerk ab, Datei verschoben, Server weg), bleibt er in
  der Liste und wird beim nächsten Scan wieder verknüpft.
- **Smart-Playlists** aus Regeln über alle Felder, auch Tempo, Tonart, Energie, Lautheit, Quelle,
  Ordner und andere Playlists; „alle“ oder „mindestens eine“ Regel; Sortierung und Höchstzahl;
  Live-Zählung im Editor; „Als feste Playlist speichern“.
- **M3U/M3U8** importieren (UTF-8 und alte Kodierung, relative Pfade, Windows-Pfade, `#EXTINF`)
  und exportieren (relativ, wenn die Titel neben der Playlist liegen).
- **Mehrfachauswahl** in allen Titellisten: Strg + Klick, Umschalt + Klick, Strg + A.
- **Warteschlange**: Reihenfolge per Ziehen ändern, als Playlist speichern.
- **Ordnerüberwachung** (inotify): neue, geänderte, verschobene und gelöschte Dateien sind nach
  wenigen Sekunden in der Bibliothek, ohne Scan. Netzlaufwerke und Ordner über der Grenze des
  Systems werden alle 15 Minuten geprüft, ein wieder eingestecktes Laufwerk sofort.
  Nicht erreichbare Laufwerke löschen weiterhin nie etwas.
- **AppImage**: wird bei jedem Stand automatisch gebaut und ausprobiert; ein Release bekommt die
  Datei angehängt. App-Symbol, Desktop-Eintrag und AppStream-Daten.

### Behoben
- Titel werden nur noch lesend geöffnet (andere Programme sehen keine „Änderung“ mehr).

## 0.6.0 - 2026-10-18

### Hinzugefügt
- **Audioanalyse** jedes Titels in einem Durchgang: Tempo (BPM, Sicherheit, erster Schlag),
  Tonart (Camelot), Lautheit nach EBU R128, Energie, Helligkeit, Anschläge pro Sekunde, Stille,
  Ein- und Ausblenden, Wellenform. Rund 0,5 s für 4 Minuten FLAC. Hintergrund-Modi „Sanft“,
  „Zügig“, „Nur Gespieltes“; Fortschritt und Restzeit in den Einstellungen.
- **Übergänge**: Lückenlos, Überblenden (1–15 s, gleich laut) und **Automix** (Stille fällt weg,
  Einstieg in Ausblenden, auf dem Schlag bei passendem Tempo, Album-Titel bleiben lückenlos).
  Zwei Wiedergabe-Decks, Blenden sample-genau im GStreamer-Controller.
- **Lautstärke angleichen** für Titel ohne ReplayGain über die gemessene Lautheit.
- **Empfehlungen**, gelernt nur auf diesem Rechner: zusammen gehörte Künstler, Skips, Tageszeit,
  dazu Genre, Zeit, Tempo, Tonart und Energie. Entdeckerfreude einstellbar.
- **Radio** („Mehr wie dieser Titel“, „Radio dazu starten“), **Autoplay** mit Empfehlungen,
  Seite **Für dich** mit vier Mixen.
- Ansichten **Nach Tempo** und **Nach Tonart**; Felder `bpm tempo key energy loudness` für eigene
  Ansichten.
- Kommandozeile: `--analyze`, `--recommend`.

## 0.5.0 - 2026-10-18

### Hinzugefügt
- **Jellyfin**: Anmeldung mit Passwort oder Quick Connect, nur der Zugangsschlüssel wird
  gespeichert.
- Bibliotheken einzeln wählbar, Abgleich in dieselbe Datenbank (seitenweise, danach nur
  Geändertes), Ordneransicht mit der Struktur des Servers.
- Streaming im Original oder umgewandelt (320/192/128 kbit/s), Cover vom Server.
- Rückmeldung an den Server (Start, Fortschritt, Pause, Ende).
- Doppelte (lokal und auf dem Server) nur einmal zeigen, lokal spielen.
- `tools/mock-jellyfin.py` zum Testen ohne eigenen Server.

## 0.4.1 - 2026-10-18

### Geändert
- Cover nur in angezeigter Größe und nur solange sichtbar; Aufräumen nach Theme-Wechsel, im
  Hintergrund und alle drei Minuten; Verbrauch live in den Einstellungen.

### Behoben
- Speicherleck bei GStreamer-Nachrichten von Spektrum und Pegelmesser (gut 10 MB pro Minute
  Wiedergabe).

## 0.4.0 - 2026-10-18

### Hinzugefügt
- Wellenform zum Spulen (Klassik, Aurora, Synthwave).
- Autoplay, erste Stufe: passende Titel, wenn die Warteschlange zu Ende geht.

### Geändert
- Feinschliff an allen Themes.

## 0.3.0 - 2026-10-18

### Hinzugefügt
- Themes **Hi-Fi Turm** (90er-Minianlage) und **Terminal** (Textmodus auf einer Bildröhre).
- Vorschaubilder für alle Themes.

## 0.2.0 - 2026-10-18

### Hinzugefügt
- Theme-System: jedes Theme mit eigenem Aufbau, eigenen Effekten und einer aus einem Schema
  erzeugten Einstellungsseite.
- Themes **Aurora**, **Synthwave**, **Soft**, **Glas** und **Vintage**; Sparmodus für ältere Rechner.
- Musik-Analyse für die Oberfläche: Spektrum, Pegel, Energie, Takt.

## 0.1.0 - 2026-10-18

### Hinzugefügt
- Lokale Bibliothek mit Zwei-Phasen-Scanner für 1 Mio+ Titel, SQLite mit Volltextsuche.
- Ansichten: Titel, Alben, Künstler, Genres, Ordner, Jahrzehnte, Formate, Hörverlauf und mehr,
  eigene Ansichten per JSON.
- Wiedergabe mit GStreamer: lückenlos, ReplayGain, Warteschlange, MPRIS.
- Theme Klassik mit sieben Farbschemata.
