# Änderungen

Alle wichtigen Änderungen an BuB3Player. Die neueste Version steht oben.

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
