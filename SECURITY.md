# Sicherheit

## Lücke melden

Bitte **nicht** als öffentliches Issue. Nutze stattdessen
[„Report a vulnerability“](https://github.com/1100101-Ich-Liebe-Dich/bub3player/security/advisories/new)
im Reiter **Security** des Repositorys – das ist nur für die Betreuer sichtbar.

Hilfreich sind: betroffene Version, wie man es nachstellt, und was jemand damit anstellen könnte.
Du bekommst innerhalb einer Woche eine Antwort. Behobene Lücken stehen mit Dank (wenn du magst)
im [CHANGELOG](CHANGELOG.md) unter **Sicherheit**.

## Unterstützte Versionen

Korrekturen gibt es für die jeweils neueste Version. Das AppImage enthält die Bibliotheken des
Bau-Systems; Updates dieser Bibliotheken kommen mit der nächsten Version.

## Was BuB3Player berührt

- **Dateien:** liest Musik nur (Tags und Cover werden nie geschrieben). Schreibt nur in
  `~/.config/halcyon`, `~/.local/share/halcyon` und `~/.cache/halcyon` – und M3U-Dateien, wenn
  du eine Playlist exportierst.
- **Netz:** nur zu Jellyfin-Servern, die du einträgst. Gespeichert wird der Zugangsschlüssel des
  Servers (unverschlüsselt in `~/.local/share/halcyon/library.db`), nie das Passwort. Ohne Angabe wird zuerst https versucht; bei http außerhalb des
  Heimnetzes warnt der Player.
- **Datenbank:** alle Werte aus Ansichten und Suche werden als Parameter gebunden, Felder und
  Operatoren sind auf eine feste Liste begrenzt.
