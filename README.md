# BuB3Player

*Projekt präsentiert von [www.BAUENUNDBOOTEN.de](https://www.bauenundbooten.de)*

Musikplayer für Linux, gebaut für **sehr große lokale Bibliotheken** (1 Mio+ Titel) und
später auch für **Jellyfin**. Interner Projektname: **Halcyon**.
C++20, Qt 6 / QML, GStreamer, TagLib, SQLite.

![Alle Themes](docs/screenshots/alle-themes.jpg)

## Themes

Acht Themes, die sich komplett unterscheiden: eigener Aufbau, eigene Effekte, eigene
Einstellungen. Inhalte und Funktionen sind in allen gleich. Umschalten: Einstellungen → Theme
(oder `Strg + T`), jedes Theme merkt sich seine Einstellungen für sich.

| Theme | Was es ausmacht | Eigene Einstellungen |
|---|---|---|
| **Klassik** | Ruhige Seitenleiste, Leuchten aus dem Cover, Wellenform zum Spulen | 7 Farbschemata, Akzent aus Cover, Leuchten |
| **Soft** | Neumorphism: Karten und Tasten wachsen weich aus der Fläche oder sind hineingedrückt. Runde Tasten, eingelassene Regler, Großansicht als Drehscheibe mit Fortschrittsring (Knopf ziehen zum Spulen), dahinter ziehen langsam weiche Wolken, die sanft der Musik folgen | Regenwolke, Sand, Mint, Nacht; Tiefe (Standard 4 px), Rundung (24 px), Akzent aus Cover, Spektrum, Wolken, Animation |
| **Glas** | Glassmorphism: Milchglas-Karten über Seifenblasen mit schillerndem Rand, die langsam aufsteigen und mit der Musik sanft schneller werden und atmen. Farben aus dem Cover. Navigation oben als Glasleiste, Player und Warteschlange immer rechts, Großansicht als große Glaskarte | Farben (Cover, Beere, Eis, Sonne), Unschärfe (60 %), Glas-Helligkeit (12 %), Anzahl, Größe, Aufsteigen, Musik-Reaktion, Cover im Hintergrund, Lichtreflex, Eckenradius, Animation |
| **Aurora** | Fließendes Licht in den Cover-Farben, Milchglas mit irisierenden Kanten, Dock mit Fortschrittsring, Großansicht mit drehender Cover-Scheibe und Rundum-Spektrum. Reagiert geglättet auf die Musik, ohne Aufblitzen. Wellenform zum Spulen | Glas hell/dunkel, Lichtfarben, Leuchtkraft, Geschwindigkeit, Musik-Reaktion, Unschärfe, Tönung, Eckenradius, Lichtpunkte, Reflex, Körnung, Scheibe, Animation |
| **Vintage** | 60er/70er-Wohnzimmer: Tapete, Möbel aus Holz, Plattenspieler mit Stroboskop-Punkten und Tonarm, abgegriffene Plattenhülle. Receiver mit beleuchteter Senderskala (der rote Zeiger ist die Spielposition), glühenden Lämpchen, Kipphebeln, Klaviertasten, VU-Metern und gerändelten Knöpfen (TUNING spult). Rechts unten die Warteschlange, Taste COVER zeigt stattdessen die Plattenhülle, Taste PHONO öffnet den großen Plattenspieler mit Rückseite der Hülle | Nussbaum & Orange, Teak & Avocado, Palisander & Petrol; Frontplatte (Silber, Champagner, Schwarz), Skalenlicht, Tapete (Kreise, Wellen, Blumen, Holzvertäfelung, schlicht), Raumlicht, VU-Meter, Zeiger-Trägheit, 33⅓/45, Tonarm, Hülle statt Liste, Aufglühen |
| **Synthwave** | Retro-Sonne, Neon-Gitter im Takt, HUD-Panels, LED-Spektrum, Neon-Wellenform zum Spulen, Glitch beim Titelwechsel | Farbwelten, Gitter-Tempo, Takt-Puls, Scanlines, Sonne, Sterne, Berge, Glitch, Neon, Animation |
| **Hi-Fi Turm** | 90er-Minianlage: Leuchtdisplay mit 14-Segment-Lauftext, Zeit, Disc-Anzeige und 20-Band-Spektrum mit Spitzenwert, Lautstärkerad mit LED-Kranz, CD-Wechsler mit regenbogenschillernder CD, Doppel-Kassettendeck mit handbeschriftetem Etikett, drehenden Spulen und Bandwickel, der mit dem Titel wandert, Zählwerk, Lauflicht. Großansicht: Turm und Boxen vor einer dunklen Kunststoffwand, darüber ein Oszilloskop (oder eine Chrom-Welle), das Bässen, Mitten und Höhen folgt | Displayfarbe (Türkis, Orange, Grün, Rot), Displayanzeige, Demo/Blinken, Dimmer, Hintergrund (Oszilloskop, Chrom-Welle, schlicht), LED-Ringe, Animation |
| **Terminal** | Textmodus auf einer Bildröhre: Kästen aus Linien, Cover als ASCII-Art, Spektrum aus Blockzeichen, Fortschritt als `[====>····]`, Tasten in eckigen Klammern, Tastenhilfe unten, Startsequenz, Scanlines | Phosphor-Grün, Bernstein, Papierweiß, Brotkasten (8-Bit); Scanlines, Bildrand, Flimmern, ASCII-Raster, ASCII in Farbe, Startsequenz |

Für ältere Rechner: Einstellungen → Allgemein → **Leistung** (Sparmodus, Animationen im
Hintergrund anhalten, Musik-Analyse aus) und in jedem animierten Theme „Animation: Sparsam/Aus“.
Ohne GPU-Beschleunigung schalten alle Themes automatisch auf einfache Flächen um.
Wer vorher „Studio“ benutzt hat, landet automatisch im neuen Vintage-Theme.

Bilder: [Soft](docs/screenshots/soft-bibliothek.jpg), [Soft Großansicht mit Wolken](docs/screenshots/soft-regenwolke-grossansicht.jpg),
[Glas](docs/screenshots/glas-bibliothek.jpg), [Glas Großansicht](docs/screenshots/glas-grossansicht.jpg),
[Vintage](docs/screenshots/vintage-bibliothek.jpg), [Vintage Plattenspieler](docs/screenshots/vintage-plattenspieler.jpg),
[Hi-Fi Turm](docs/screenshots/turm-bibliothek.jpg), [Turm Großansicht (Oszilloskop)](docs/screenshots/turm-grossansicht.jpg),
[Turm Chrom-Welle](docs/screenshots/turm-chrom.jpg), [Terminal](docs/screenshots/terminal-bibliothek.jpg),
[Terminal Bernstein](docs/screenshots/terminal-bernstein.jpg), [Aurora mit Wellenform](docs/screenshots/aurora-grossansicht.jpg),
[Synthwave Wellenform](docs/screenshots/synthwave-wellenform.jpg), [Autoplay](docs/screenshots/autoplay-warteschlange.jpg),
[Klassik](docs/screenshots/klassik.jpg), [Theme-Einstellungen](docs/screenshots/theme-einstellungen.jpg)

## Was schon geht

**Neu in 0.4:**
- **Wellenform zum Spulen** (Klassik, Aurora, Synthwave): Jeder Titel wird einmal im Hintergrund
  analysiert (eigene, gedrosselte GStreamer-Pipeline, unabhängig von der Wiedergabe, ca. 0,25 s
  für 4 Minuten FLAC) und als 480-Byte-Datei in `~/.cache/halcyon/waves/` abgelegt. Der nächste
  Titel wird schon vorbereitet. Abschaltbar unter Einstellungen → Leistung.
- **Autoplay (erste Stufe)**: Geht die Warteschlange zu Ende, werden passende Titel angehängt
  (gleiches Genre und ähnliche Zeit, ein paar vom selben Künstler, nichts aus den letzten zwei
  Tagen, nichts doppelt). Schalter: Stern-Symbol in der Warteschlange, Einstellungen → Autoplay,
  im Terminal `[auto]`. Bei „Wiederholen“ pausiert Autoplay. Die Empfehlungs-Engine (Phase 3)
  liefert später die Titel für genau diese Stelle.

**Bibliothek**
- Inkrementeller Zwei-Phasen-Scanner: erst Ordner (Bibliothek sofort nutzbar), dann Tags im
  Hintergrund in Plattenreihenfolge. Unveränderte Dateien (Größe + Änderungszeit) werden nicht
  angefasst. Ein abgebrochener Scan wird beim nächsten Mal fortgesetzt.
- Nicht erreichbare Laufwerke (ausgehängt, Netzwerk weg) werden **nie** aus der Bibliothek gelöscht.
- Symlink-Schleifen und doppelt eingebundene Ordner werden erkannt.
- Automatisch 2 Lese-Threads auf HDDs (weniger Kopfspringen), mehr auf SSDs; einstellbar.
- Erstimport: Sortier- und Suchindizes werden abgeschaltet und am Ende in einem Rutsch gebaut.

**Ansichten** (alle virtualisiert, auch bei 1 Mio Zeilen flüssig)
- Alle Titel, Alben (Cover-Raster), Künstler, Genres, **Ordneransicht** wie auf der Platte
- Zuletzt hinzugefügt, nach Monat hinzugefügt, Erscheinungsjahr, Jahrzehnte,
  Formate und Qualität (z. B. „FLAC 24 bit, 96 kHz“), **Dateien nach Alter**, Hi-Res
- Hörverlauf: zuletzt gespielt, meistgespielt, lange nicht gehört, nie gespielt
- Jede Liste nach Titel, Künstler, Album, Jahr, Hinzugefügt, Datei-Datum, Genre, Format,
  Dauer, Wiedergaben, Zuletzt gespielt oder Dateipfad sortierbar
- Volltextsuche (SQLite FTS5, Präfixsuche, ohne Akzente)
- **Eigene Ansichten** per JSON (`~/.config/halcyon/views.json`), siehe unten

**Wiedergabe**
- GStreamer: lückenlos (gapless), ReplayGain (Titel/Album) mit Limiter, Vorverstärkung
- Warteschlange mit Zufall (Verlauf bleibt erhalten), Wiederholen, Als Nächstes spielen
- Sitzung wird gespeichert: Warteschlange, Titel und Position
- MPRIS (Medientasten, Desktop-Widgets, KDE Connect, `playerctl`)
- Wiedergabestatistik (Wiedergaben, Skips, Verlauf) als Basis für Empfehlungen

**Oberfläche**
- 4 grundverschiedene Themes (siehe oben), Klassik mit 7 Farbschemata + eigenen als JSON
- Musik-Analyse: Spektrum, Pegel links/rechts, Energie und Takt-Erkennung (nur aktiv, wenn sichtbar)
- Software-Renderer-Fallback für sehr alte Grafik

## Gemessen

Testrechner: VM mit 2 vCPUs, 8 GB RAM, SSD. 1 000 000 Dateien in 101 000 Ordnern.

| Vorgang | Zeit | Speicher |
|---|---|---|
| Erstimport Phase 1 (Ordner, Bibliothek ab hier nutzbar) | 22 s | |
| Erstimport komplett (Tags, Sortier- und Suchindex) | 87 s | 128 MB Spitze |
| Erneuter Scan, nichts geändert | 5 s | 58 MB |
| App mit „Alle Titel“ (1 Mio Zeilen) geöffnet | < 0,5 s | ~100 MB |
| Volltextsuche | < 0,1 s | |

Details: [docs/benchmark.md](docs/benchmark.md)

### Arbeitsspeicher

Gemessen mit 1 080 Titeln und 360 Covern in 2000 × 2000 px, Fenster 1380 × 880, Grafik über
Mesa llvmpipe (Software-OpenGL; zählt seine Puffer mit). „Eigene Daten“ ist der anonyme
Speicher des Players, „gesamt“ enthält zusätzlich geteilte Bibliotheken und Grafiktreiber.

| Situation | eigene Daten | gesamt |
|---|---|---|
| ein Theme, Wiedergabe, Alben durchblättern, Großansicht | 108–165 MB | 239–296 MB |
| 4 min Dauerbelastung: alle 1,6 s Theme wechseln, Titel springen, blättern | 200–250 MB, stabil | ~380 MB |
| dasselbe mit Software-Renderer (`QT_QUICK_BACKEND=software`) | 80–90 MB | ~160 MB |

Was dafür sorgt:
- **Speicherleck behoben** (0.4.1): GStreamer-Nachrichten von Spektrum und Pegelmesser wurden
  nie freigegeben, gut 10 MB pro Minute Wiedergabe. Daher kamen die 600 MB.
- Cover werden nur in der angezeigten Größe dekodiert (64–1024 px) und nur solange sie zu
  sehen sind; verborgene Cover (z. B. die Großansicht) geben ihr Bild nach 4 s frei.
- Nach jedem Theme-Wechsel, im Hintergrund und alle 3 Minuten: QML-Speicher einsammeln,
  Grafik-Caches leeren, freien Speicher ans System zurückgeben. Höchstens 2 malloc-Arenen.
- Einstellungen → Allgemein → Leistung zeigt den aktuellen Verbrauch live.

Testwerkzeuge: `--ui-test playall,skip,scroll,cycleloop` (Dauerbelastung), `swap-<theme>`, `pair`.

Wichtig: Beim Test lagen die Dateien im RAM-Cache. Auf einer **HDD** bestimmt beim Erstimport
die Platte das Tempo (jede Datei braucht 1–3 Lesezugriffe). Rechne dort mit deutlich längeren
Zeiten für Phase 2. Phase 1 und alle späteren Scans bleiben schnell, und die Bibliothek ist
schon während Phase 2 benutzbar. Selbst messen:

```sh
halcyon --scan /pfad/zur/musik --db /tmp/test.db
```

## Bauen

Abhängigkeiten:

**Arch / Manjaro / EndeavourOS**
```sh
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-declarative qt6-svg qt6-shadertools \
  gstreamer gst-plugins-base gst-plugins-good gst-libav taglib sqlite
```

**Fedora**
```sh
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel \
  qt6-qtshadertools-devel gstreamer1-devel gstreamer1-plugins-base-devel gstreamer1-plugins-good \
  gstreamer1-libav taglib-devel sqlite-devel
```

**Debian 13 / Ubuntu 24.04+**
```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-declarative-dev qt6-svg-dev \
  qt6-shadertools-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-good \
  gstreamer1.0-libav libtag1-dev libsqlite3-dev qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-layouts qml6-module-qtquick-window qml6-module-qtquick-dialogs \
  qml6-module-qtquick-templates qml6-module-qtqml-workerscript
```

Bauen und starten:
```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/halcyon
# optional systemweit:
sudo cmake --install build
```

`gst-libav` wird für AAC/ALAC, WMA und APE gebraucht. MP3, FLAC, Ogg, Opus und WAV laufen ohne.

## Bedienung

| Taste | Aktion |
|---|---|
| Leertaste | Abspielen / Pause |
| Strg + → / ← | Nächster / vorheriger Titel |
| Strg + ↑ / ↓ | Lauter / leiser |
| Strg + F | Suchen |
| Strg + J | Warteschlange |
| Strg + L | Großansicht |
| Strg + T | Theme wechseln |
| Alt + ← | Zurück |
| Strg + Q | Beenden |

Doppelklick spielt ab, Rechtsklick bietet „Als Nächstes spielen“, „Zur Warteschlange“,
„Album öffnen“ usw.

Kommandozeile: `halcyon --help` (u. a. `--add-folder`, `--view`, `--skin`, `--theme`, `--skin-opt`, `--db`, `--scan`).

## Eigene Ansichten

`~/.config/halcyon/views.json` (Vorlage: `views.example.json` im selben Ordner):

```json
[
  {
    "id": "hires-jazz",
    "name": "Hi-Res Jazz",
    "icon": "sparkle",
    "kind": "tracks",
    "spec": {
      "filters": [
        { "field": "genre", "op": "contains", "value": "Jazz" },
        { "field": "bits",  "op": "ge",       "value": 24 }
      ],
      "sort": "year",
      "desc": true
    }
  }
]
```

- `kind`: `tracks` (Liste) oder `groups` (mit `"layout": "grid"` oder `"tiles"` und `spec.groupBy`)
- Felder: `title artist album albumArtist genre composer codec filename year decade bits sampleRate
  bitrate added mtime playCount skipCount lastPlayed duration rating folder addedMonth fileYear quality`
- Operatoren: `eq ne lt le gt ge contains startsWith empty notEmpty withinDays olderThanDays under`
  (`under` = Ordner inklusive Unterordner, Wert ist ein Pfad)
- Sortierung: `album title artist year added mtime duration playCount lastPlayed codec genre path random`
- Gruppierung: `album albumArtist artist genre year decade quality addedMonth fileYear composer`

Alle Werte werden als Parameter gebunden, Felder und Operatoren sind auf eine feste Liste begrenzt.

## Eigene Farbschemata (Klassik)

JSON-Datei nach `~/.local/share/halcyon/themes/` legen, z. B. eine Kopie von
`themes/01-tiefsee.json` mit neuer `id`. Gleiche `id` überschreibt ein mitgeliefertes Schema.

Ein neues Theme besteht aus einer Beschreibung in `skins/<id>.json` (Name, Schriften,
Farbschemata, Einstellungs-Schema) und einer QML-Datei in `qml/skins/`. Die Einstellungsseite
wird aus dem Schema erzeugt (Typen `palette`, `bool`, `range`, `choice`).

## Aufbau

```
src/core      Datenbank (Schema, WAL, FTS5), Scanner, TagReader, Cover- und Icon-Provider
src/library   Abfragen (QueryBuilder), Listen-/Gruppen-/Ordnermodelle, Ansichten, Library-API für QML
src/player    GStreamer-Player, Warteschlange, MPRIS
src/ui        Theme-System
qml/          gemeinsame Seiten und Bausteine
qml/skins/    die acht Themes (Aufbau, Effekte, Player, Großansicht)
skins/        Theme-Beschreibungen, Einstellungs-Schemata, Vorschaubilder
themes/       Farbschemata für Klassik
shaders/      Aurora-Licht, Milchglas, Kugeln, Neumorphism, Synthwave-Szene, HUD, Glitch,
              Holz/Metall/Kunststoff, Tapete, Vinyl, CD, Scheinwerfer, ASCII, Bildröhre …
```

Daten: `~/.local/share/halcyon/` (Datenbank, Warteschlange, eigene Themes),
`~/.config/halcyon/` (Einstellungen, eigene Ansichten), `~/.cache/halcyon/covers/` (Cover-Cache).

Grundsätze: Listen halten nur IDs (8 Byte pro Titel) und laden sichtbare Zeilen seitenweise.
Alles, was dauert, läuft außerhalb des UI-Threads. Jede optionale GStreamer-Komponente wird
vorher geprüft; fehlt sie, fällt nur die Funktion weg, nie die Wiedergabe.

## Fahrplan

1. **Phase 1 – fertig:** lokale Bibliothek, Scanner, Ansichten, Wiedergabe
1b. **Themes – fertig:** Klassik, Soft, Glas, Aurora, Vintage, Synthwave, Hi-Fi Turm, Terminal,
    jedes mit eigenen Einstellungen; Wellenform zum Spulen, Autoplay (einfach)
2. **Phase 2 – Jellyfin:** Anmeldung, inkrementeller Abgleich in dieselbe Datenbank
   (Tabelle `sources` ist schon da), Streaming, Ordneransicht über Jellyfin-Pfade, Scrobbling
3. **Phase 3 – Empfehlungen:** aus dem Hörverlauf (zusammen gehört, Skips, Tageszeit),
   „Mehr wie dieser Titel“, endlose Radio-Warteschlange. Andockpunkt ist schon da:
   `Library::autoplayPicks()` – heute Genre/Zeit/Künstler, später die Empfehlungen
4. **Phase 4 – Audioanalyse und Automix:** BPM, Tonart, Energie, Lautheit im Hintergrund;
   Übergänge an Stille-/Intro-Grenzen, Crossfade mit zwei Pipelines
5. **Phase 5:** Equalizer, Ausgabegerät wählen / Bit-Perfect über ALSA, Playlists und
   Smart-Playlists im Editor, Ordnerüberwachung (inotify), Liedtexte, mehr Themes

## Lizenzen

Schriften: Manrope, Bricolage Grotesque, Unbounded, Orbitron, Chakra Petch, Barlow,
Barlow Condensed, VT323, Nunito, Righteous, IBM Plex Mono, DSEG7/DSEG14 – SIL Open Font
License; Permanent Marker – Apache 2.0 (`assets/fonts/`).
Icons: Pfade aus Material Icons (Apache 2.0).
