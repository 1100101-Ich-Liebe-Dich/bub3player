#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds a huge test library from a few real audio files: N tracks in
"Artist NNNN/Album NN/NN - Track N.ext" (1000 tracks per artist, 10 per
album), as hard links, so a million tracks cost almost no disk space.

    tools/make-biglib.py QUELLORDNER ZIELORDNER [ANZAHL]   (default 1000000)

The source folder can be any music (e.g. made with make-showcase.py); the
target must be on the same file system for hard links (else symlinks).
Then: halcyon --scan ZIELORDNER --db /tmp/big.db  (timings), or add it as a
library folder in a test profile (tools/ui-test.sh).
"""
import os, sys

if len(sys.argv) < 3:
    sys.exit(__doc__)
src_dir, root = sys.argv[1], sys.argv[2]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 1_000_000
exts = (".mp3", ".flac", ".ogg", ".opus", ".m4a", ".wav")
src = sorted(os.path.join(dp, f) for dp, _, fs in os.walk(src_dir) for f in fs if f.lower().endswith(exts))
if not src:
    sys.exit("Keine Audiodateien in " + src_dir)
i = 0
for a in range((n + 999) // 1000):
    for b in range(100):
        if i >= n:
            break
        d = f"{root}/Artist {a:04d}/Album {b:02d}"
        os.makedirs(d, exist_ok=True)
        for t in range(10):
            if i >= n:
                break
            s = src[i % len(src)]
            dst = f"{d}/{t + 1:02d} - Track {i}{os.path.splitext(s)[1]}"
            if not os.path.exists(dst):
                try:
                    os.link(s, dst)
                except OSError:
                    os.symlink(os.path.abspath(s), dst)
            i += 1
    if a % 50 == 0:
        print(f"{i} Titel …", flush=True)
print("Fertig:", i, "Titel in", root)
