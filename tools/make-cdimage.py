#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""An audio CD as an image (disc.cue + disc.bin), for testing the CD page without a
drive:

    tools/make-cdimage.py ZIEL [TITEL] [SEKUNDEN]
    HALCYON_TEST_CDROM=ZIEL/disc.cue build/halcyon --view cd

Every track is a chord of its own (so tracks can be told apart by ear and in the
ripped FLAC files), 16-bit stereo at 44.1 kHz like on a real CD. Defaults: 3
tracks of 20 seconds.
"""
import array, math, os, sys


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    target = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    seconds = int(sys.argv[3]) if len(sys.argv) > 3 else 20
    if not 1 <= count <= 99 or seconds < 4:
        sys.exit("1 bis 99 Titel, mindestens 4 Sekunden")
    os.makedirs(target, exist_ok=True)
    rate = 44100
    cue = ['FILE "disc.bin" BINARY']
    frames = 0  # CD frames of 1/75 s (588 samples)
    with open(os.path.join(target, "disc.bin"), "wb") as out:
        for n in range(1, count + 1):
            base = 220 * 2 ** ((n - 1) * 2 / 12)  # a whole tone higher each track
            notes = [base, base * 5 / 4, base * 3 / 2]
            samples = array.array("h")
            total = seconds * rate
            for i in range(total):
                t = i / rate
                fade = min(1.0, i / 2000, (total - i) / 2000)
                v = sum(math.sin(2 * math.pi * f * t) for f in notes) / 3 * 0.4 * fade
                s = int(v * 32767)
                samples.extend((s, s))
            if sys.byteorder != "little":
                samples.byteswap()
            out.write(samples.tobytes())
            m, rest = divmod(frames, 75 * 60)
            sec, f = divmod(rest, 75)
            cue += ["  TRACK %02d AUDIO" % n, "    INDEX 01 %02d:%02d:%02d" % (m, sec, f)]
            frames += total // 588
    with open(os.path.join(target, "disc.cue"), "w") as f:
        f.write("\n".join(cue) + "\n")
    print("CD-Abbild: %s/disc.cue (%d Titel zu %d s)" % (target, count, seconds))


if __name__ == "__main__":
    main()
