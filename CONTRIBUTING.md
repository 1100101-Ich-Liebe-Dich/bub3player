# Mitmachen

Schön, dass du BuB3Player besser machen willst! Ein paar Hinweise, damit es für alle leicht bleibt.

## Fehler und Wünsche

- **Fehler** und **Wünsche** über [Issues](https://github.com/1100101-Ich-Liebe-Dich/bub3player/issues/new/choose)
  – die Vorlagen fragen nach allem, was zum Nachstellen nötig ist (Version, Distribution,
  Theme, Größe der Bibliothek).
- Hilfreich bei Fehlern: Ausgabe im Terminal (`./BuB3Player-x86_64.AppImage` aus dem Terminal
  starten) und, wenn es um die Wiedergabe geht, `QT_LOGGING_RULES="halcyon.mix.debug=true"`.
- **Sicherheitslücken** bitte nicht als Issue, sondern wie in [SECURITY.md](SECURITY.md).

## Code beitragen

1. Repository forken, einen Branch anlegen (`git switch -c mein-thema`).
2. Bauen und starten – Abhängigkeiten stehen in der [README](README.md#-installieren):
   ```sh
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build && ./build/halcyon
   ```
3. Änderung machen, testen (siehe unten), `CHANGELOG.md` unter **Unveröffentlicht** ergänzen.
4. Pull Request gegen `main` öffnen. Der Workflow „Bauen“ baut ein AppImage und probiert alle
   Themes aus; Meldungen erscheinen am Pull Request.

Größere Vorhaben am besten vorher in einem Issue ansprechen.

## Was BuB3Player ausmacht

[CLAUDE.md](CLAUDE.md) beschreibt Aufbau, Grundsätze und Stolperfallen ausführlich. Das Wichtigste:

- **Nur Linux.**
- **Große Bibliotheken, alte Rechner:** 1 Million Titel, deutlich unter 300 MB Arbeitsspeicher.
  Nichts, was mit der Bibliothek wächst, gehört in den UI-Thread.
- **Nie Musik verlieren:** Ein ausgehängtes Laufwerk darf nie Titel, Wiedergabezähler oder
  Playlist-Einträge löschen.
- **Alle acht Themes** bekommen jede Funktion.
- **Die Standard-Ausgabe bleibt unangetastet:** Ohne gewähltes Gerät und ohne Equalizer kommt
  kein zusätzliches Element in die GStreamer-Kette.
- Oberfläche, Meldungen, Commits und Pull Requests auf **Deutsch**, Kommentare im Code auf Englisch.

## Testen

Es gibt keine Unit-Tests; getestet wird am laufenden Programm ohne Bildschirm, mit den Werkzeugen
in `tools/` (Vorführ-Bibliothek, Riesen-Bibliothek, Klick- und Speichertests, Jellyfin-Testserver).
Die Tabelle in [CLAUDE.md](CLAUDE.md#tests) erklärt sie.

## Lizenz

Mit deinem Beitrag stimmst du zu, dass er unter der [GPL-3.0-or-later](LICENSE) steht, wie der
Rest des Projekts. Neue Quelldateien tragen oben die Zeile
`SPDX-License-Identifier: GPL-3.0-or-later`.
