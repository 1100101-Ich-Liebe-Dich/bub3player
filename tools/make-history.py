#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A listening history for testing the statistics and "Dein Jahr in Musik":
plays of the titles already in a database, spread over the last years like a
person would listen (favourite artists, evenings and weekends, binges of one
title, skips) plus some internet radio. The player must not be running.

    tools/make-history.py DATENBANK [JAHRE] [WIEDERGABEN_PRO_TAG]

Defaults: 2 years back from today, about 25 plays a day. Existing plays stay;
running it again adds more. The database must have schema 6 (start the player
once).
"""
import random, sqlite3, sys, time
from datetime import datetime, timedelta


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    db = sqlite3.connect(sys.argv[1])
    years = float(sys.argv[2]) if len(sys.argv) > 2 else 2
    per_day = float(sys.argv[3]) if len(sys.argv) > 3 else 25
    cols = {r[1] for r in db.execute("PRAGMA table_info(plays)")}
    if "aa" not in cols:
        sys.exit("Datenbank hat noch kein Schema 6: den Player einmal damit starten")
    rnd = random.Random(42)
    tracks = db.execute("SELECT id, title, artist, album, aa, genre, year, duration_ms FROM tracks "
                        "WHERE dup = 0 AND title <> '' ORDER BY id").fetchall()
    if not tracks:
        sys.exit("keine Titel in der Datenbank")
    # taste: artists with Zipf weights, changing a little each year
    artists = sorted({t[2] for t in tracks})
    by_artist = {}
    for t in tracks:
        by_artist.setdefault(t[2], []).append(t)
    stations = db.execute("SELECT id, name FROM stations").fetchall() if "station_plays" in {
        r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type = 'table'")} else []
    # hour weights: a little in the morning, lunch, much in the evening, some night
    hour_w = [2, 1, 0.5, 0.3, 0.3, 0.8, 3, 5, 4, 3, 3, 3, 4, 4, 3, 3, 4, 6, 8, 9, 9, 8, 6, 4]
    end = datetime.now()
    day = end - timedelta(days=int(years * 365))
    plays, radio = [], []
    weights = None
    year_seen = None
    while day < end:
        if day.year != year_seen:  # a new year: favourites shift
            year_seen = day.year
            order = artists[:]
            rnd.shuffle(order)
            weights = {a: 1.0 / (i + 1) ** 1.1 for i, a in enumerate(order)}
        weekend = day.weekday() >= 5
        n = max(0, int(rnd.gauss(per_day * (1.4 if weekend else 1.0), per_day * 0.4)))
        if rnd.random() < 0.12:
            n = 0  # days without music
        binge = rnd.choice(tracks) if rnd.random() < 0.04 else None
        hour = rnd.choices(range(24), hour_w)[0]
        t = day.replace(hour=hour, minute=rnd.randrange(60), second=0, microsecond=0)
        for i in range(n):
            if binge and i % 3 == 0:
                tr = binge
            else:
                a = rnd.choices(list(weights), list(weights.values()))[0]
                tr = rnd.choice(by_artist[a])
            dur = tr[7] or 200000
            skipped = rnd.random() < 0.15
            listened = int(dur * (rnd.uniform(0.05, 0.25) if skipped else rnd.uniform(0.9, 1.0)))
            when = int(t.timestamp())
            if when < time.time():
                plays.append((tr[0], when, listened, 0 if skipped else 1) + tuple(tr[1:7]))
            t += timedelta(milliseconds=listened + 2000)
            if rnd.random() < 0.08:  # a pause, then later that day
                t += timedelta(hours=rnd.uniform(1, 5))
        if stations and rnd.random() < 0.2:
            st = rnd.choice(stations[:3])
            at = day.replace(hour=rnd.choice([7, 8, 12, 18]), minute=0, second=0, microsecond=0)
            if at.timestamp() < time.time():
                radio.append((st[0], st[1], int(at.timestamp()), int(rnd.uniform(10, 90) * 60000)))
        day += timedelta(days=1)
    db.executemany("INSERT INTO plays(track_id, played_at, listened_ms, completed, title, artist, album, aa, genre, year) "
                   "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", plays)
    db.executemany("INSERT INTO station_plays(station_id, name, played_at, listened_ms) VALUES (?, ?, ?, ?)", radio)
    # the counters of the titles follow the history
    db.execute("UPDATE tracks SET play_count = (SELECT COUNT(*) FROM plays p WHERE p.track_id = tracks.id AND p.completed = 1), "
               "skip_count = (SELECT COUNT(*) FROM plays p WHERE p.track_id = tracks.id AND p.completed = 0), "
               "last_played = COALESCE((SELECT MAX(played_at) FROM plays p WHERE p.track_id = tracks.id AND p.completed = 1), 0)")
    db.execute("INSERT OR REPLACE INTO meta(key, value) VALUES ('learn_rebuild', '1')")
    db.commit()
    print("%d Wiedergaben und %d Radiozeiten über %.1f Jahre eingetragen" % (len(plays), len(radio), years))


if __name__ == "__main__":
    main()
