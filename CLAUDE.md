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
  (der Inhaber liest den Code im PR, testet, merged selbst). Auch kleine Korrekturen gehen über
  einen PR (`main` ist geschützt). `main` muss immer veröffentlichbar sein.
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
./build/halcyon --lyrics DATEI…      # Liedtext eines Titels (.lrc daneben, Tags), wie die Großansicht ihn findet
./build/halcyon --recommend ID --db x.db   # was die Empfehlungen nach Titel ID wählen würden
```

Abhängigkeiten: siehe README (Arch, Fedora, Debian/Ubuntu). Mindestens Qt 6.4, GStreamer 1.18,
TagLib 1.11, SQLite 3.35. Die Version steht in `CMakeLists.txt` (`project(Halcyon VERSION …)`)
und wird als `HALCYON_VERSION` überall verwendet; dazu `dist/*.metainfo.xml` und `CHANGELOG.md`
pflegen.

**Änderungsprotokoll:** `CHANGELOG.md`, neueste Version oben, so schlicht, wie der Inhaber es
will: keine Kopfzeile, kein Abschnitt „Unveröffentlicht“, keine Links. Jede Version beginnt mit
`## X.Y.Z - JJJJ-MM-TT` (Datum des PRs, der Inhaber passt es beim Release an, falls nötig),
optional ein Satz, dann `### Hinzugefügt`, `### Geändert`, `### Behoben`, `### Entfernt`,
`### Sicherheit` (nur die, die es gibt).

**Lizenz:** GPL-3.0-or-later (`LICENSE`). Jede neue Quelldatei (C++, QML, Skripte, CMake) beginnt
mit `SPDX-License-Identifier: GPL-3.0-or-later` (Shader und JSON nicht). Fremde Teile: Schriften
mit ihren Lizenztexten in `assets/fonts/`, Apache/LGPL-Texte in `LICENSES/`; das AppImage legt
die copyright-Dateien aller mitgelieferten Ubuntu-Pakete nach `usr/share/licenses/`.

## Aufbau

```
src/core        Database (Schema + Migration, WAL, FTS5), Scanner (Zwei-Phasen-Scan, Modi
                Full/Subset/Folders), FolderWatcher (inotify), TagReader, Cover-/Icon-Provider,
                Http (blockierendes GET für Worker-Threads)
src/library     Library (QML-API, Wurzelordner, Überwachung), QueryBuilder (Ansicht → SQL),
                TrackListModel (nur IDs, seitenweise Zeilen, Auswahl als Bitset), GroupModel,
                FolderModel, ViewRegistry (eingebaute + eigene Ansichten), PlaylistStore
src/player      Player (zwei GStreamer-Decks, Überblenden/Automix, Filterkette je Deck),
                AudioOutput (QML „Audio“: Ausgabegerät, Bit-Perfect, Equalizer), PlayQueue, MPRIS,
                Lyrics (QML „Lyrics“: .lrc, Tags, Jellyfin, LRCLIB; LRC-Parser)
src/analysis    AudioAnalyzer (ein Durchgang: Lautheit, Tempo, Tonart, Wellenform …),
                AnalysisService (Vorrang- und Hintergrund-Warteschlange)
src/recommend   Recommender (Lernen aus dem Hörverlauf, Radio, Autoplay, „Für dich“)
src/jellyfin    Anmeldung, Abgleich in dieselbe Datenbank, Streaming, Rückmeldung
src/radio       Stations (QML „Stations“: Senderliste in der Tabelle `stations`, Hinzufügen per
                Adresse/Liste, radio-browser.info), StreamList (Parser für .pls/.m3u/.xspf/.asx)
src/cd          AudioCd (QML „AudioCd“: Laufwerk, Inhalt über die GStreamer-CD-Quelle, MusicBrainz,
                Einlesen als FLAC)
src/ui          Theme-System, Memory (Aufräumen, malloc-Arenen), SystemDialogs (QML
                „SystemDialogs“: Datei-/Ordnerwahl über XDG-Portal, sonst kdialog/zenity),
                Tray (QML „Tray“: Symbol im Infobereich als StatusNotifierItem + dbusmenu,
                Benachrichtigung beim Titelwechsel), SingleInstance (ein Player pro Datenbank,
                org.freedesktop.Application: „Öffnen mit“ an den laufenden Player)
qml/            gemeinsame Seiten und Bausteine (H*-Bausteine: HDialog, HCombo, HTextField,
                HMenu …), PlaylistUi (alle Playlist-Dialoge), SmartEditor, LyricsView
                (Liedtext für die Großansichten; die Themes wählen nur Schrift, Farben, Ausrichtung)
qml/skins/      die acht Themes; jedes hostet dieselben Seiten
skins/, themes/ Theme-Beschreibungen mit Einstellungs-Schema, Farbschemata
shaders/        alle Effekte (qsb, zur Bauzeit übersetzt)
dist/           Desktop-Eintrag, AppStream, Symbol, appimage/ (Bau- und Testskripte)
tools/          Test- und Hilfswerkzeuge (siehe unten)
```

Daten: `~/.local/share/halcyon/library.db` (SQLite, Schema-Version in `PRAGMA user_version`,
derzeit 5), `~/.config/halcyon/halcyon.conf` (QSettings) und `views.json`,
`~/.cache/halcyon/` (Cover, Wellenformen, `lyrics/` mit Antworten von LRCLIB, `stations/` mit
Senderlogos, `cd/` mit Namen und Covern von CDs je MusicBrainz-Kennung).
Desktop-ID: `de.bauenundbooten.BuB3Player`.

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
- Favoriten: das Herz ist `tracks.rating = 5` (`TrackRow::FavoriteRating`), nie in die Dateien.
  Setzen über `Library.setFavorite/setFavoriteRows/toggleCurrentFavorite` (Listen sofort, Datenbank
  in einem eigenen Schreib-Thread, bis dahin gilt das Herz vorläufig im `TrackCache`);
  Jellyfin-Titel gehen über `remoteFavoritesChanged` an den Server (höchstens 4 Anfragen, eine je
  Titel). Offen Gebliebenes steht in `<datenbank>-jellyfin-favorites.txt` (eine Zeile je Herz)
  und wird nachgeschickt; der Abgleich übernimmt `IsFavorite` vom Server, schreibt danach aber die
  eigenen offenen Herzen zurück. Jedes Theme zeigt das Herz in Leiste und Großansicht
  (`Library.currentFavorite`).
- Infobereich und Benachrichtigungen direkt über D-Bus, ohne QtWidgets und ohne Qt.labs.platform
  (auf Debian/Ubuntu nicht immer installiert). Schließen in den Infobereich nur, wenn ein Infobereich
  das Symbol angenommen hat (`Tray.available`), sonst wäre das Fenster nicht mehr erreichbar.
- Dateien von außen (Ziehen und Ablegen, „Öffnen mit“, Kommandozeile) laufen über
  `window.openPaths` → `Library.openPaths`: gespielt wird nur, was in der Bibliothek ist;
  außerhalb fragt der Player, ob der Ordner dazukommen soll. Senderlisten (`.pls`, M3U nur mit
  Internetadressen) und `http(s)`-Adressen gehen an `Stations` und spielen gleich.
- **Sender und CD-Titel haben negative IDs** (`library/External.h`): Sender n ist `-n` (bleibt über
  einen Neustart in der Warteschlange), CD-Titel n ist `-(10^12 + n)` (fällt beim Neustart aus der
  Warteschlange). `TrackCache` baut ihre Zeilen über `External::row`, `Player`, Warteschlange, MPRIS
  und Infobereich behandeln sie wie Titel. Alles, was nur für die Bibliothek ist (Herz, Zähler,
  Analyse, Liedtexte, Playlists, Empfehlungen), prüft `id > 0`; Anzeigen fragen `Player.hasTrack`
  statt `Player.currentId > 0`. `Player.live` ist ein Sender: kein Spulen, „Live“ statt Länge
  (`window.durationText`), ICY-Titel über `GST_MESSAGE_TAG`, kein lückenloses Vorladen. Ins Netz
  (radio-browser.info, MusicBrainz) nur, wenn eingeschaltet; Senderadressen nur `http(s)`.
- CD einlesen: nie etwas überschreiben – jeder Titel entsteht als `.bub3player-….flac.part` im
  Zielordner und wird mit `QFile::rename` (ersetzt nichts) umbenannt; vorhandene Dateien werden
  übersprungen, `cover.jpg` nur, wenn es keines gibt.
- Liedtexte: gesucht nur, solange eine Großansicht offen ist (`Lyrics.active`); ins Netz (LRCLIB)
  nur, wenn eingeschaltet und Liedtexte gezeigt werden sollen. Nie etwas neben die Musik schreiben.
  Jede Großansicht zeigt `LyricsView` statt des Covers, wenn `window.lyricsOn`; Schalter ruft
  `window.toggleLyrics()`, Hinweis `window.lyricsTip`, blass bei `window.lyricsMissing`.
- **Der Standard-Ausgabeweg ist heilig** (Wunsch des Inhabers: „die Ausgabe geht perfekt“):
  Mit „Systemstandard“ und ausgeschaltetem Equalizer setzt der Player kein `audio-sink` und kein
  zusätzliches Element. Alles Neue kommt nur in die Kette, wenn es gewählt ist; Änderungen an der
  Kette laufen über `Player::rebuildChains()` (anhalten, neu bauen, an derselben Stelle weiter).
  Nach Änderungen am Player den Pipeline-Graphen mit `main` vergleichen (siehe Tests).
- QML: keine `QtCore`-Imports (auf Debian/Ubuntu nicht immer installiert); Dialoge über
  `HDialog`, Menüs über `HMenu`/`HMenuItem`, Farben/Schriften nur über `Theme`.
- Datei- und Ordnerwahl immer über `SystemDialogs.chooseFolder/openFile/saveFile(…, function (path))`
  (Wunsch des Inhabers: der gewohnte Dialog des Desktops). `path === null` heißt „kein Dialog des
  Desktops da“ → dann Qts `FolderDialog`/`FileDialog` als Rückfall öffnen; `""` = abgebrochen.
  Beim Speichern kommt als zweiter Wert `confirmed`: false, wenn eine Endung ergänzt wurde und die
  Datei schon existiert (selbst nachfragen). Unter Wayland ist der Dialog nicht modal – Antworten
  dürfen nur wirken, wenn der Zusammenhang noch stimmt (siehe `SmartEditor.session`).

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
| `tools/mock-jellyfin.py --audio ORDNER` | kleiner Jellyfin-Server (Benutzer/Passwort `demo`), auch mit Liedtexten |
| `tools/mock-portal.py --answer PFAD` | Ersatz für den Dateidialog des XDG-Portals (`pip install dbus-next`), in `dbus-run-session` zusammen mit dem Player starten; zeigt die Aufrufe |
| `tools/mock-tray.py` | Ersatz für Infobereich (StatusNotifierWatcher) und Benachrichtigungen; Befehle auf stdin (`menu`, `click ID`, `activate` …), zusammen mit dem Player in `dbus-run-session` |
| `tools/mock-radio.py --audio ORDNER` | Radiosender (`/stream/N`, `/list/N.pls`) mit wechselnden ICY-Titeln, radio-browser.info, MusicBrainz und Cover Art Archive auf Port 8099; Player mit `HALCYON_RADIOBROWSER_URL`, `HALCYON_MUSICBRAINZ_URL`, `HALCYON_COVERART_URL=http://127.0.0.1:8099` starten |
| `tools/mock-lrclib.py` | Ersatz für LRCLIB; Player mit `HALCYON_LRCLIB_URL=http://127.0.0.1:8098` starten, `/mock/requests` zeigt die Anfragen |
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
- Internetradio: `--ui-test "funkadd=URL funkon=N funksuche=WORT"` (Sender hinzufügen, den N-ten
  spielen, im Verzeichnis suchen) mit `tools/mock-radio.py`.
- Audio-CD ohne Laufwerk: `HALCYON_TEST_CDROM=DATEI.cue` (Abbild aus .cue und .bin, gelesen mit
  `cdiocddasrc`, cdparanoia wird dafür abgeschaltet), `--view cd --ui-test "scheibe=N scheibelesen"`
  (N-ten Titel ab 0 spielen, einlesen). Ein Abbild: 16-Bit-PCM (Little Endian, 44,1 kHz, Stereo) als
  `.bin`, dazu eine `.cue` mit `TRACK nn AUDIO` und `INDEX 01 mm:ss:ff`.
- Liedtexte: `.lrc`-Dateien neben die Vorführ-Bibliothek legen (Kopie mit `cp -al`), dann
  `--ui-test "playall now-playing lyrics seek=30000"` (`lyrics-off` schaltet sie aus). Echte
  USLT-/SYLT-Tags mit `mutagen` schreiben: `ffmpeg -metadata lyrics=…` legt bei MP3 nur ein
  `TXXX:USLT` an (wird auch gelesen).

Typischer Ablauf vor einem PR:
1. `cmake --build build` ohne Warnungen.
2. Neue Funktion in mehreren Themes ansehen (`tools/ui-test.sh shot …` und das Bild öffnen).
3. Bei Bibliotheks-/Speicherthemen mit 1 Mio Titeln messen (`make-biglib.py`, `ui-test.sh mem`).
4. Unabhängiges Review (eigener Agent liest den Diff), Funde beheben.
5. Push → der Workflow „Bauen“ baut das AppImage auf Ubuntu 22.04 mit Qt 6.8 und probiert es
   aus. QML-Meldungen erscheinen als Hinweis am PR.

## GitHub und CI

- Repo: `1100101-Ich-Liebe-Dich/bub3player`, Standard-Branch `main`. Neu angelegt am 2026-10-20
  mit umgeschriebener Historie (E-Mail des Inhabers → GitHub-Ersatzadresse); das alte Repo heißt
  `bub3player-alt` (privat, mit den PR-Seiten #1–#10 und den Releases bis 1.2.0).
  Hier gibt es Tags erst ab v1.2.1; die älteren Versionen sind die Commits 87c3290 (1.0.0),
  9d70c6e (1.1.0) und ae6b144 (1.2.0).
- Die echte E-Mail des Inhabers darf nie in Commits landen: Commits der Sitzung laufen als
  `Claude <noreply@anthropic.com>`, Merges des Inhabers über die Ersatzadresse.
- `gh pr create` funktioniert in der Sitzung nicht; PRs über
  `gh api repos/1100101-Ich-Liebe-Dich/bub3player/pulls --method POST --input body.json`.
  Tags, Releases und Repo-Einstellungen kann nur der Inhaber anlegen (auch per API gesperrt).
- Dependabot öffnet monatlich PRs für die GitHub-Actions; mergen, wenn der Bau grün ist.
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
- Der Jellyfin-Test-Server: `pkill -f "^python3 tools/[m]ock-jellyfin"`. Ohne `^python3` trifft
  das Muster auch die eigene Shell, sobald derselbe Befehl den Namen noch einmal enthält (z. B.
  beim Neustart des Servers in derselben Zeile). Den Player beenden: `pkill -x halcyon`.
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
- **1.1.0**: Equalizer, Ausgabegerät, Bit-Perfect über ALSA (Reiter „Klang“).
- **1.2.0**: Künstlerbilder (Ordner/Jellyfin), Suche mit Künstler- und Albenreihe, A–Z-Leiste
  repariert, Bit-Perfect nur mit Bestätigung (Lautstärke runter).
- **1.2.1**: Vollbild mit F11; Lizenz und alles für das öffentliche Repository.
- **1.3.0**: Liedtexte in der Großansicht (.lrc, Tags, Jellyfin, auf Wunsch LRCLIB), synchron mit
  Sprung per Klick, in allen acht Themes.
- **1.3.1**: Rechtsklick in der Warteschlange (Abspielen, Zum Album, Zum Künstler, Entfernen),
  Datei- und Ordnerwahl über den Dialog des Desktops.
- **1.4.0** „Alltag“: Favoriten (Herz, auch zu Jellyfin), Symbol im Infobereich und
  Benachrichtigung, Ziehen und Ablegen und „Öffnen mit“ (ein Player).
- **Als Nächstes** (Wunsch des Inhabers: weiter als 1.4.x, nur bis Paket 3):
  - **1.4.1**: Internetradio (Sender per Adresse, .pls/.m3u, ICY-Titel, auf Wunsch
    radio-browser.info) und Audio-CD (abspielen, als FLAC in einen gewählten Ordner einlesen, nie
    vorhandene Dateien anfassen, auf Wunsch MusicBrainz). Gebaut, PR offen.
  - **1.4.2**: Statistik/„Dein Jahr in Musik“ (Tabelle `plays`).
  - Englische Oberfläche (Paket 4) vorerst nicht. ReplayGain bleibt titelweise (Wunsch des
    Inhabers: alle Titel gleich laut), nichts in die Dateien.
- **Ideen 1.x** (README „Ausblick“): Scrobbeln (ListenBrainz/Last.fm), Tag-Editor, Mini-Player,
  Schlaf-Timer, Fernbedienung übers Handy/Browser, fehlende Cover aus dem Netz, englische
  Oberfläche, weitere Themes.
- **Öffentlich** seit 2026-10-20 (mit 1.2.1): Lizenz GPL-3.0-or-later, CONTRIBUTING.md,
  SECURITY.md, PR-Vorlage, Dependabot für die Actions; Social Preview, Beschreibung, „Private
  vulnerability reporting“ und die Regel für `main` (nur über PR, Bau muss grün sein) hat der
  Inhaber gesetzt. Direkt auf `main` schieben geht deshalb nicht mehr.
