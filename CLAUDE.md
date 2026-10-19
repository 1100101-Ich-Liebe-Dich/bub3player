# BuB3Player – Hinweise für die Arbeit am Projekt

Lies diese Datei zuerst. Sie fasst zusammen, was in keinem einzelnen Code-Kommentar steht:
Vorgaben des Inhabers, Arbeitsablauf, Aufbau, Tests, Stolperfallen und Fahrplan.

## Das Projekt

BuB3Player (interner Name **Halcyon**, daher Binary `halcyon`, Ordner `~/.config/halcyon` usw.)
ist ein Musikplayer für Linux: C++20, Qt 6 (Quick/QML), GStreamer, TagLib, SQLite.
Gebaut für **sehr große lokale Bibliotheken (Maßstab: 1 000 000 Titel in 101 000 Ordnern)**
und **Jellyfin**. Signatur überall: „Projekt präsentiert von www.BAUENUNDBOOTEN.de“.

## Vorgaben des Inhabers (verbindlich)

- **Sprache**: Mit dem Inhaber Deutsch sprechen. Oberfläche, Meldungen, README, CHANGELOG,
  Commit-Nachrichten und PR-Texte auf Deutsch. Code-Kommentare auf Englisch (wie im Bestand).
  Deutsche Texte sauber: Einzahl/Mehrzahl, „Titeln“ im Dativ, keine Anglizismen, wo es ein gutes
  deutsches Wort gibt.
- **Nur Linux.** Andere Systeme sind kein Ziel.
- **Alte Hardware**: Der Inhaber testet auf einem alten Rechner mit ~2 GB freiem RAM.
  Der Player muss **deutlich unter 300 MB** bleiben (gesamt, Spitze), auch während der
  Hintergrund-Analyse und mit 1 Mio Titeln. Nichts, was mit der Bibliotheksgröße wächst, im
  UI-Thread. Speicher nach größeren Änderungen messen (siehe Tests).
- **Nie Musik verlieren**: Ein nicht erreichbares Laufwerk (ausgehängt, Netz weg, USB ab,
  leerer Einhängepunkt) darf **nie** Titel, Wiedergabezähler oder Playlist-Einträge löschen.
- **Alle acht Themes** bekommen jede Funktion: Klassik, Soft, Glas, Aurora, Vintage, Synthwave,
  Hi-Fi Turm (`tower`), Terminal. Neue Seiten/Dialoge in mehreren Themes ansehen.
- **Arbeitsablauf**: Jeder größere Brocken auf einem eigenen Branch, dann ein **Pull Request**
  (der Inhaber liest den Code im PR, testet, merged selbst). Kleine Korrekturen dürfen nach
  Absprache auch direkt auf `main`. `main` muss immer veröffentlichbar sein.
- **Releases** veröffentlicht der Inhaber selbst in der GitHub-Oberfläche (Tag `vX.Y.Z`).
  Der Workflow hängt das AppImage dann automatisch an.
- Der Inhaber schätzt große, durchdachte Brocken mit gründlichem Testen und einem
  unabhängigen Review (eigener Agent, der den Diff liest) vor dem PR. Keine doppelten Zeilen,
  keine halben Sachen; Funde aus dem Review beheben und erneut testen.

## Bauen und starten

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/halcyon                      # Oberfläche
./build/halcyon --help               # Optionen (--db, --add-folder, --skin, --view, --no-scan …)
./build/halcyon --scan DIR --db x.db # Scan ohne Oberfläche, mit Zeiten
./build/halcyon --analyze DATEI…     # Tempo, Tonart, Lautheit … eines Titels
./build/halcyon --recommend ID --db x.db   # was die Empfehlungen nach Titel ID wählen würden
```

Abhängigkeiten: siehe README (Arch, Fedora, Debian/Ubuntu). Mindestens Qt 6.4, GStreamer 1.18,
TagLib 1.11, SQLite 3.35. Die Version steht in `CMakeLists.txt` (`project(Halcyon VERSION …)`)
und wird als `HALCYON_VERSION` überall verwendet; dazu `dist/*.metainfo.xml` und `CHANGELOG.md`
pflegen.

## Aufbau

```
src/core        Database (Schema + Migration, WAL, FTS5), Scanner (Zwei-Phasen-Scan, Modi
                Full/Subset/Folders), FolderWatcher (inotify), TagReader, Cover-/Icon-Provider
src/library     Library (QML-API, Wurzelordner, Überwachung), QueryBuilder (Ansicht → SQL),
                TrackListModel (nur IDs, seitenweise Zeilen, Auswahl als Bitset), GroupModel,
                FolderModel, ViewRegistry (eingebaute + eigene Ansichten), PlaylistStore
src/player      Player (zwei GStreamer-Decks, Überblenden/Automix, Filterkette je Deck),
                AudioOutput (QML „Audio“: Ausgabegerät, Bit-Perfect, Equalizer), PlayQueue, MPRIS
src/analysis    AudioAnalyzer (ein Durchgang: Lautheit, Tempo, Tonart, Wellenform …),
                AnalysisService (Vorrang- und Hintergrund-Warteschlange)
src/recommend   Recommender (Lernen aus dem Hörverlauf, Radio, Autoplay, „Für dich“)
src/jellyfin    Anmeldung, Abgleich in dieselbe Datenbank, Streaming, Rückmeldung
src/ui          Theme-System, Memory (Aufräumen, malloc-Arenen)
qml/            gemeinsame Seiten und Bausteine (H*-Bausteine: HDialog, HCombo, HTextField,
                HMenu …), PlaylistUi (alle Playlist-Dialoge), SmartEditor
qml/skins/      die acht Themes; jedes hostet dieselben Seiten
skins/, themes/ Theme-Beschreibungen mit Einstellungs-Schema, Farbschemata
shaders/        alle Effekte (qsb, zur Bauzeit übersetzt)
dist/           Desktop-Eintrag, AppStream, Symbol, appimage/ (Bau- und Testskripte)
tools/          Test- und Hilfswerkzeuge (siehe unten)
```

Daten: `~/.local/share/halcyon/library.db` (SQLite, Schema-Version in `PRAGMA user_version`,
derzeit 4), `~/.config/halcyon/halcyon.conf` (QSettings) und `views.json`,
`~/.cache/halcyon/` (Cover, Wellenformen). Desktop-ID: `de.bauenundbooten.BuB3Player`.

### Grundsätze im Code

- Listen halten nur IDs (8 Byte pro Titel) und laden sichtbare Zeilen seitenweise
  (`TrackCache`). Abfragen über die ganze Bibliothek laufen in `QtConcurrent` mit eigener
  SQLite-Verbindung (eine Verbindung pro Thread, nie Statements zwischen Threads teilen).
- Alles aus Ansichten/Nutzerdaten geht durch `QueryBuilder`: nur Felder/Operatoren aus der
  Whitelist, alle Werte gebunden. Spec-Schlüssel: `filters`, `match` (all/any), `require`,
  `playlist`, `sort`, `desc`, `limit` (auch für Gruppen), `seed`, `search`, `searchIn`
  (`artist`/`album`: Suche nur in Albumkünstler bzw. Album + Albumkünstler), `groupBy`, `groupSort`.
- Bilder: `image://cover/<trackId>` (Albumcover), `image://cover/artist/<trackId>/<Künstler>`
  (Künstlerbild aus dem Künstler-Ordner bzw. von Jellyfin, sonst das Cover); `CoverArt.artist`.
- Playlists: Schreiben nur im Writer-Thread (`PlaylistStore::writeAsync`, eine Reihenfolge,
  Rollback bei Fehler); Einträge werden über (Position, Titel-ID) geprüft. Einträge haben einen
  `hint` (Pfad, Titel, Künstler, Album, Länge), fehlende Titel bleiben als `track_id IS NULL`
  und werden nach Scans wieder verknüpft.
- Scanner: Ordner mit Gerät (`folders.dev`); verschwundene Einhängepunkte und leere
  Einhängepunkte mit bekannter Musik gelten als „nicht da“ und werden nie gelöscht.
- Jede optionale GStreamer-Komponente vorher prüfen; fehlt sie, fällt nur die Funktion weg.
- **Der Standard-Ausgabeweg ist heilig** (Wunsch des Inhabers: „die Ausgabe geht perfekt“):
  Mit „Systemstandard“ und ausgeschaltetem Equalizer setzt der Player kein `audio-sink` und kein
  zusätzliches Element. Alles Neue kommt nur in die Kette, wenn es gewählt ist; Änderungen an der
  Kette laufen über `Player::rebuildChains()` (anhalten, neu bauen, an derselben Stelle weiter).
  Nach Änderungen am Player den Pipeline-Graphen mit `main` vergleichen (siehe Tests).
- QML: keine `QtCore`-Imports (auf Debian/Ubuntu nicht immer installiert); Dialoge über
  `HDialog`, Menüs über `HMenu`/`HMenuItem`, Farben/Schriften nur über `Theme`.

## Tests

Es gibt keine Unit-Test-Suite; getestet wird am laufenden Programm, ohne Bildschirm (Xvfb).
Werkzeuge in `tools/`:

| Werkzeug | Zweck |
|---|---|
| `tools/make-showcase.py ZIEL` | Vorführ-Bibliothek: 16 Alben erfundener Künstler mit Covern und synthetischer Musik in passendem Tempo (für Screenshots, Automix, Analyse) |
| `tools/make-biglib.py QUELLE ZIEL [N]` | Riesen-Bibliothek aus Hardlinks (Standard 1 Mio Titel, 101 000 Ordner) |
| `tools/ui-test.sh shot NAME SEK [Optionen]` | Start, Screenshot, Ende; jedes `PROFIL` hat eigene Daten unter `/tmp/halcyon-test/` |
| `tools/ui-test.sh start/click/drag/key/snap/stop` | Klicktests mit `xdotool` am laufenden Fenster (1380 × 880) |
| `tools/ui-test.sh mem NAME SEK [Optionen]` | Speicher: gesamt und eigene Daten, Spitze und Ende |
| `tools/mock-jellyfin.py --audio ORDNER` | kleiner Jellyfin-Server (Benutzer/Passwort `demo`) |
| `dist/appimage/build-appimage.sh` | AppImage lokal bauen |
| `dist/appimage/smoke-test.sh DATEI` | AppImage ausprobieren (Analyse, Start, alle Themes, QML-Meldungen) |

`--ui-test "…"` steuert den Player für Tests (Liste in `tools/ui-test.sh` und `qml/Main.qml`,
`uiTestTimer`), z. B. `playall now-playing`, `mix-tab`, `klang-tab eqonat=2000`, `mkpl plopen=1 pledit`.
Automix-Protokoll (auch Neuaufbau der Kette): `RULES="halcyon.mix.debug=true"`.

Ton prüfen ohne Soundkarte:
- `HALCYON_TEST_SINK="filesink location=/tmp/x.raw"` ersetzt das Ausgabegerät durch eine beliebige
  GStreamer-Senke (auch Ketten: `capsfilter caps=audio/x-raw,format=S24LE ! filesink …`).
  Bit-Perfect prüfen: `[audio] bitPerfect=true`, `hwDevice="hw:CARD=Test,DEV=0"` (Anführungszeichen,
  sonst liest QSettings das Komma als Liste) und die Datei mit `ffmpeg -i titel.flac -f s32le`
  vergleichen – sie muss bytegleich sein.
- `GST_DEBUG_DUMP_DOT_DIR=DIR` schreibt die ganze Kette als Graph, sobald ein Titel spielt. Für den
  Vergleich mit `main` dort dieselbe Zeile einbauen (Worktree), Graphen ohne Adressen und Nummern
  vergleichen.
- Geräteliste und Gerätewechsel: `pulseaudio` mit `module-null-sink` starten, mitschneiden mit
  `parec -d NAME.monitor`.
- Soundkarten für Bit-Perfect ohne Hardware: `HALCYON_TEST_ASOUND=DIR` mit Dateien `cards` und
  `pcm` im Format von `/proc/asound`.
- Suche testen: `--ui-test "search=wort"`.

Typischer Ablauf vor einem PR:
1. `cmake --build build` ohne Warnungen.
2. Neue Funktion in mehreren Themes ansehen (`tools/ui-test.sh shot …` und das Bild öffnen).
3. Bei Bibliotheks-/Speicherthemen mit 1 Mio Titeln messen (`make-biglib.py`, `ui-test.sh mem`).
4. Unabhängiges Review (eigener Agent liest den Diff), Funde beheben.
5. Push → der Workflow „Bauen“ baut das AppImage auf Ubuntu 22.04 mit Qt 6.8 und probiert es
   aus. QML-Meldungen erscheinen als Hinweis am PR.

## GitHub und CI

- Repo: `1100101-Ich-Liebe-Dich/bub3player` (privat), Standard-Branch `main`.
- `gh pr create` funktioniert in der Sitzung nicht; PRs über
  `gh api repos/1100101-Ich-Liebe-Dich/bub3player/pulls --method POST --input body.json`.
  Tags, Releases und Repo-Einstellungen kann nur der Inhaber anlegen.
- CI-Logs und Artefakte lassen sich aus der Sitzung nicht herunterladen (Blob-Speicher
  gesperrt). Deshalb schreibt `dist/appimage/ci-run.sh` bei Fehlern das Ende der Ausgabe als
  Annotation an den Lauf: `gh api repos/…/check-runs/<job-id>/annotations`.
- Der Ubuntu-22.04-Runner hat ein `libunwind-14-dev`, das `libgstreamer1.0-dev` blockiert
  (wird im Workflow entfernt).

## Stolperfallen (schon einmal passiert)

- **TagLib öffnet Dateien sonst zum Schreiben** → inotify meldet `CLOSE_WRITE` → Endlosschleife
  aus Scan und Neuladen. Tags und Cover immer über `TagLib::FileStream(…, readOnly = true)`.
- `Transaction` in `Sql.h` committet im Destruktor; für Schreibvorgänge, die bei Fehlern ganz
  zurückgerollt werden müssen, `WriteTx` (PlaylistStore) verwenden.
- `NOT IN (SELECT … )` mit NULL-Werten liefert nichts – fehlende Playlist-Einträge sind NULL.
- Pfadbereiche immer als `path = p OR (path >= p||'/' AND path < p||'0')` (sonst passt
  „/musik“ auch auf „/musik2“).
- Speicher: mehrere SQLite-Verbindungen mit `mmap` zählen doppelt im RSS; kurze Hilfs-
  verbindungen mit `PRAGMA mmap_size=0`, große Temp-Tabellen mit `temp_store=FILE`.
- Bus-Nachrichten von GStreamer (Spektrum, Pegel) immer freigeben – ein Leck dort kostete
  einmal 10 MB pro Minute.
- `QString::arg` kennt kein `%%`.
- Der Jellyfin-Test-Server: `pkill -f "[m]ock-jellyfin.py"` (mit Klammern, sonst trifft es die
  eigene Shell). Genauso `pkill -f "[b]uild/halcyon"`.
- playbin mit `NATIVE_AUDIO` und festem `audio-sink` nimmt nur Decoder, deren Ausgabe die Senke
  direkt annimmt (die `audio-filter`-Kette zählt nicht) → „No decoder available“. Deshalb stecken
  bei Bit-Perfect `audioconvert` (ohne Dither) und `audioresample` mit der ALSA-Senke in einem Bin
  (beide reichen unverändert durch, wenn das Gerät Format und Rate kann). Ohne Resampler laufen
  MP3s mit fremder Rate stumm durch die ganze Warteschlange (mpg123 meldet keinen Fehler).
- Ein PulseAudio-Gerät, das es nicht mehr gibt, meldet keinen Fehler der Senke, sondern
  „No decoder available“ (MISSING_PLUGIN) – das zählt bei gewähltem Gerät als Ausgabefehler.
- Die Null-Senke von PulseAudio im Container braucht nach jedem Start ~1–2 s, bis die Position
  läuft. Kein Fehler des Players.
- Eine neue Senke mit eigener Lautstärke (pulsesink) übernimmt die Lautstärke von playsink nur bei
  einer Änderung; nach `rebuildChains` wird sie deshalb beim nächsten PAUSED/PLAYING neu gesetzt.
- `level` meldet bei „not-negotiated“ GStreamer-CRITICALs (Division durch 0) – harmlos, nur im
  Fehlerfall.

## Fahrplan

- **1.0.0**: veröffentlicht nach Test des Inhabers (Playlists, Smart-Playlists, Ordnerüberwachung,
  AppImage sind drin).
- **1.1.0**: Equalizer, Ausgabegerät, Bit-Perfect über ALSA (Reiter „Klang“). Das Repo bleibt
  privat, bis der Inhaber alles getestet hat.
- **1.2.0**: Künstlerbilder (Ordner/Jellyfin), Suche mit Künstler- und Albenreihe, A–Z-Leiste
  repariert, Bit-Perfect nur mit Bestätigung (Lautstärke runter).
- **Als Nächstes** (in der README schon als vorhanden beschrieben, vor dem öffentlich Schalten
  des Repos fertig bauen!):
  - **Liedtexte**: aus Tags und `.lrc` neben der Datei, synchronisiert in der Großansicht
    (Klick auf Zeile springt), optional aus dem Netz über LRCLIB (nur wenn eingeschaltet).
- **Ideen 1.x** (README „Ausblick“): Scrobbeln (ListenBrainz/Last.fm), Tag-Editor, Mini-Player,
  Schlaf-Timer, Fernbedienung übers Handy/Browser, fehlende Cover aus dem Netz, englische
  Oberfläche, weitere Themes.
- Offene Punkte für den Inhaber: Lizenz wählen (noch keine), Social Preview hochladen
  (`docs/images/banner.png`), Repo-Beschreibung/Topics setzen.
