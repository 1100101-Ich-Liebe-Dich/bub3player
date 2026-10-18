# Benchmark: 1 000 000 Titel

Rechner: VM, 2 vCPUs, 8 GB RAM, SSD. Release-Build. 1 000 000 Audiodateien in 101 001 Ordnern
(FLAC 16/24 bit, MP3, Ogg, Opus, AAC). Gemessen mit `halcyon --scan` (gleicher Code wie in der App).

| Vorgang | Zeit | Speicher (anonym) |
|---|---|---|
| Erstimport Phase 1: Ordner einlesen, Bibliothek nutzbar | 22,4 s | |
| Erstimport Phase 2: Tags lesen (2 Threads) | 48,1 s | |
| Erstimport Phase 3: Sortier- und Suchindex aufbauen | 16,4 s | |
| **Erstimport gesamt** | **86,9 s** | **128 MB Spitze** |
| Erneuter Scan, nichts geändert | 5,3 s | 58 MB |
| App mit 1 Mio Titeln, „Alle Titel“ geöffnet | | ~100 MB |
| Datenbankgröße | 533 MB | |

Abfragen auf der 1-Mio-Datenbank (Python-Messung, inklusive Übergabe aller Zeilen):

| Abfrage | Zeit |
|---|---|
| Alle Titel nach Album sortiert (1 Mio IDs) | ~0,45 s |
| Alben gruppieren | ~0,2 s |
| Genres gruppieren | ~0,2 s |
| Volltextsuche | < 0,1 s |

Optimierungen, die das gebracht haben:
- Abfrage der offenen Tag-Jobs über einen Teilindex (vorher O(n²): 4 min für 40 %, nachher 48 s für 100 %)
- Erstimport ohne Sortierindizes und Such-Trigger, danach einmaliger Aufbau (Phase 1: 48 s → 22 s)
- Album-Sortierung komplett über den Index (2,3 s → 0,45 s)

**Einschränkung:** Die Testdateien waren Hardlinks auf 312 echte Dateien, lagen also im RAM-Cache.
Gemessen ist damit die Software, nicht die Platte. Auf einer HDD braucht Phase 2 beim Erstimport
deutlich länger, weil jede Datei 1–3 Lesezugriffe mit Kopfbewegung kostet. Halcyon liest deshalb
in Ordnerreihenfolge und mit nur 2 Threads. Phase 1, jeder spätere Scan und die Bedienung bleiben
schnell, und die Bibliothek ist schon während Phase 2 benutzbar.

Selbst messen:

```sh
./build/halcyon --scan /pfad/zur/musik --db /tmp/halcyon-test.db
```
