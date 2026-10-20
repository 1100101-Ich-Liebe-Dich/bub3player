# Änderungen

Alle wichtigen Änderungen an BuB3Player. Die neueste Version steht oben.

## 1.2.1 – Vollbild

- **F11** schaltet das Vollbild an und aus; danach ist das Fenster wieder so groß wie vorher
  (auch maximiert). Ein Hinweis zeigt, wie man zurückkommt. In allen Themes, im Terminal-Theme
  auch in der Tastenleiste.

## 1.2.0 – Künstlerbilder, neue Suche

- **Künstlerbilder**: Künstler zeigen ihr eigenes Bild statt eines Albumcovers – aus dem Ordner
  des Künstlers über den Alben (`MUSIK/KÜNSTLER/ALBUM/…`, auch eine Ebene tiefer wie
  `MUSIK/KÜNSTLER/EPs/ALBUM/…`): `artist.jpg`, `folder.jpg`, `poster.jpg` usw. Der Ordner muss
  wie der Künstler heißen (Groß-/Kleinschreibung, Akzente, Satzzeichen und „The“ egal), so landet
  kein Albumcover als Künstlerbild. Bei Jellyfin kommt das Künstlerbild vom Server. Ohne Bild
  bleibt es beim Cover. Die Seite eines Künstlers zeigt sein Bild groß und rund.
- **Suche**: Über den Titeln stehen jetzt passende **Künstler** und passende **Alben**, jeweils in
  einer Reihe zum Seitwärtsblättern (Pfeile, Ziehen, Umschalt + Mausrad). Klick öffnet,
  Rechtsklick spielt ab oder reiht ein.
- **A–Z-Leiste** springt jetzt zuverlässig: Ein späteres Neuladen der Liste (Scan,
  Ordnerüberwachung) zog die Ansicht vorher wieder nach oben. Umlaute zählen beim Buchstaben ohne
  Punkte („Ä“ unter A), Buchstaben ohne Einträge sind blass, an der Leiste entlangziehen blättert
  durch, der Buchstabe wird dabei groß angezeigt. Auch bei Alben nach Albumtitel sortiert.
- **Bit-Perfect mit Bestätigung**: Nach dem Einschalten erscheint ein zweiter Schalter. Erst wenn
  man bestätigt, dass Verstärker, DAC oder Kopfhörer leise gestellt sind, geht Bit-Perfect an –
  der Ton kommt dabei ohne Lautstärkeregler mit voller Stärke an.

## 1.1.0 – Equalizer und Ausgabegerät

Neuer Reiter **Einstellungen → Klang** (`Strg + E`), in allen acht Themes:

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
- **Die bisherige Ausgabe bleibt unangetastet**: Mit „Systemstandard“ und ausgeschaltetem
  Equalizer ist die GStreamer-Kette Element für Element dieselbe wie in 1.0 (am Pipeline-Graphen
  verglichen). Equalizer, Gerät und Bit-Perfect kommen nur in die Kette, wenn man sie wählt.
- ReplayGain ist von „Allgemein“ nach „Klang“ umgezogen.
- Kleinigkeiten: `--add-folder` nimmt auch relative Pfade, „1 Datei geprüft“ in der Einzahl.

Für Tests: `HALCYON_TEST_SINK` ersetzt das Ausgabegerät durch eine beliebige GStreamer-Senke
(z. B. `filesink`), `GST_DEBUG_DUMP_DOT_DIR` schreibt die Kette beim Abspielen als Graph.

## 1.0.0 – Erste stabile Version

Alles aus den Versionen 0.1 bis 0.9 in einem Paket, ausgiebig getestet auf alter Hardware und
mit einer Bibliothek von einer Million Titeln:

- Bibliothek für 1 Mio+ Titel mit Zwei-Phasen-Scanner, Volltextsuche und vielen Ansichten
- acht komplett verschiedene Themes
- Wiedergabe lückenlos, mit Überblenden oder Automix; ReplayGain und Lautheitsausgleich
- Audioanalyse (Tempo, Tonart, Lautheit, Energie) im Hintergrund
- Empfehlungen, Radio, Autoplay und „Für dich“ – gelernt nur auf dem eigenen Rechner
- Jellyfin in derselben Bibliothek
- Playlists, Smart-Playlists, M3U, Ordnerüberwachung
- AppImage für alle Distributionen ab glibc 2.35

Für Mitentwickler: `CLAUDE.md` beschreibt Vorgaben, Aufbau, Tests und Fahrplan, `tools/` enthält
die Testwerkzeuge (Vorführ-Bibliothek, Riesen-Bibliothek, Klick- und Speichertests).

## 0.9.0 – Playlists, Ordnerüberwachung, AppImage

- **Playlists**: anlegen, umbenennen, löschen; Titel, Alben, Künstler, Genres, Ordner, Suchergebnisse
  und die Warteschlange per Rechtsklick oder Knopf „Zur Playlist“ hinzufügen; Reihenfolge per
  Ziehen ändern; jeder Titel nur einmal. Einträge merken sich Pfad, Titel und Künstler: Ist ein
  Titel gerade nicht da (Laufwerk ab, Datei verschoben, Server weg), bleibt er in der Liste und
  wird beim nächsten Scan wieder verknüpft.
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
- Titel werden nur noch lesend geöffnet (andere Programme sehen keine „Änderung“ mehr).
- **AppImage**: wird bei jedem Stand automatisch gebaut und ausprobiert; ein Release bekommt die
  Datei angehängt. App-Symbol, Desktop-Eintrag und AppStream-Daten.

## 0.6.0 – Audioanalyse, Empfehlungen, Automix

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

## 0.5.0 – Jellyfin

- Anmeldung mit Passwort oder Quick Connect, nur der Zugangsschlüssel wird gespeichert.
- Bibliotheken einzeln wählbar, Abgleich in dieselbe Datenbank (seitenweise, danach nur
  Geändertes), Ordneransicht mit der Struktur des Servers.
- Streaming im Original oder umgewandelt (320/192/128 kbit/s), Cover vom Server.
- Rückmeldung an den Server (Start, Fortschritt, Pause, Ende).
- Doppelte (lokal und auf dem Server) nur einmal zeigen, lokal spielen.
- `tools/mock-jellyfin.py` zum Testen ohne eigenen Server.

## 0.4.1 – Speicher

- Speicherleck bei GStreamer-Nachrichten von Spektrum und Pegelmesser behoben (gut 10 MB pro
  Minute Wiedergabe).
- Cover nur in angezeigter Größe und nur solange sichtbar; Aufräumen nach Theme-Wechsel, im
  Hintergrund und alle drei Minuten; Verbrauch live in den Einstellungen.

## 0.4.0 – Wellenform und Autoplay

- Wellenform zum Spulen (Klassik, Aurora, Synthwave).
- Autoplay, erste Stufe: passende Titel, wenn die Warteschlange zu Ende geht.
- Feinschliff an allen Themes.

## 0.3.0 – Hi-Fi Turm und Terminal

- Themes **Hi-Fi Turm** (90er-Minianlage) und **Terminal** (Textmodus auf einer Bildröhre).
- Vorschaubilder für alle Themes.

## 0.2.0 – Themes

- Theme-System: jedes Theme mit eigenem Aufbau, eigenen Effekten und einer aus einem Schema
  erzeugten Einstellungsseite.
- Themes **Aurora**, **Synthwave**, **Soft**, **Glas** und **Vintage**; Sparmodus für ältere Rechner.
- Musik-Analyse für die Oberfläche: Spektrum, Pegel, Energie, Takt.

## 0.1.0 – Erste Version

- Lokale Bibliothek mit Zwei-Phasen-Scanner für 1 Mio+ Titel, SQLite mit Volltextsuche.
- Ansichten: Titel, Alben, Künstler, Genres, Ordner, Jahrzehnte, Formate, Hörverlauf und mehr,
  eigene Ansichten per JSON.
- Wiedergabe mit GStreamer: lückenlos, ReplayGain, Warteschlange, MPRIS.
- Theme Klassik mit sieben Farbschemata.
