#!/usr/bin/env python3
"""Builds the demo library used for the screenshots on GitHub: 16 albums by
invented artists, 4 tracks each, with generated cover art and synthesized
music (drums, chords, bass) in the album's tempo, so the audio analysis,
automix and "Nach Tempo" / "Nach Tonart" have something real to show.

    tools/make-showcase.py ZIELORDNER          (about 2 minutes, ~100 MB)

Needs: python3-numpy, python3-pil, ffmpeg. Everything is made up: no real
artist, album or cover appears anywhere.
"""
import colorsys, math, os, random, subprocess, sys, tempfile

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

if len(sys.argv) != 2:
    sys.exit(__doc__)
OUT = sys.argv[1]
FONTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "fonts")
random.seed(42)
rng = np.random.default_rng(7)

# ---------- covers ----------
S = 1000

def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size)

def grad(c1, c2):
    im = Image.new("RGB", (S, S)); d = ImageDraw.Draw(im)
    for i in range(S):
        t = i / S
        d.line([(0, i), (S, i)], fill=tuple(int(c1[k] * (1 - t) + c2[k] * t) for k in range(3)))
    return im

def grain(im, a=18):
    n = Image.effect_noise((S, S), a).convert("RGB")
    return Image.blend(im, ImageChops.overlay(im, n), 0.25)

SUN = [((255, 94, 98), (44, 20, 80), (255, 80, 180)), ((20, 160, 200), (10, 20, 60), (80, 220, 255)),
       ((255, 170, 60), (60, 20, 30), (255, 120, 40))]

def sunset():
    a, b, line = SUN[sunset.n % 3]; sunset.n += 1
    im = grad(a, b); d = ImageDraw.Draw(im)
    for r in range(260, 0, -4):
        c = (255, int(200 - r * 0.3), int(80 + r * 0.2)) if a[0] > 200 else (int(150 + r * 0.3), 230, 255)
        d.ellipse([500 - r, 560 - r, 500 + r, 560 + r], fill=c)
    for y in range(560, 820, 22):
        d.rectangle([0, y, S, y + 8 + (y - 560) // 30], fill=b)
    d.rectangle([0, 820, S, S], fill=tuple(int(v * 0.7) for v in b))
    for i in range(-20, 21):
        d.line([(500, 820), (500 + i * 80, S)], fill=line, width=3)
    for y in (840, 870, 910, 960):
        d.line([(0, y), (S, y)], fill=line, width=3)
    return im
sunset.n = 0

def bauhaus():
    im = Image.new("RGB", (S, S), random.choice([(240, 232, 214), (22, 26, 34), (236, 226, 206)])); d = ImageDraw.Draw(im)
    cols = [(220, 60, 45), (30, 80, 170), (245, 190, 40), (20, 20, 20), (60, 160, 130)]
    for _ in range(7):
        c = random.choice(cols); x = random.randint(0, 800); y = random.randint(0, 800); s = random.randint(150, 420); t = random.random()
        if t < .4: d.ellipse([x, y, x + s, y + s], fill=c)
        elif t < .7: d.rectangle([x, y, x + s, y + s // 2], fill=c)
        else: d.polygon([(x, y + s), (x + s // 2, y), (x + s, y + s)], fill=c)
    return im

def waves():
    im = Image.new("RGB", (S, S), [(10, 30, 60), (5, 40, 40), (30, 10, 50)][random.randrange(3)]); d = ImageDraw.Draw(im)
    hue = random.random()
    for k in range(60):
        r, g, b = colorsys.hsv_to_rgb((hue + k * 0.006) % 1, 0.7, 0.6 + 0.4 * k / 60)
        pts = [(x, 500 + math.sin(x / 90 + k * 0.18) * (120 + k * 3) + (k - 30) * 9) for x in range(0, S + 10, 10)]
        d.line(pts, fill=(int(r * 255), int(g * 255), int(b * 255)), width=2)
    return im

def blobs():
    h = random.random(); im = Image.new("RGB", (S, S), tuple(int(v * 255) for v in colorsys.hsv_to_rgb(h, 0.5, 0.25)))
    d = ImageDraw.Draw(im)
    for _ in range(6):
        c = tuple(int(v * 255) for v in colorsys.hsv_to_rgb((h + random.uniform(-.15, .25)) % 1, 0.8, 1.0))
        x, y, r = random.randint(100, 900), random.randint(100, 900), random.randint(150, 380)
        d.ellipse([x - r, y - r, x + r, y + r], fill=c)
    return im.filter(ImageFilter.GaussianBlur(90))

def mountains():
    im = grad((250, 200, 150), (120, 160, 200)); d = ImageDraw.Draw(im)
    for k, c in enumerate([(90, 110, 150), (60, 80, 120), (40, 55, 90), (25, 35, 60)]):
        pts = [(0, S)] + [(x, 420 + k * 110 + random.randint(-90, 60)) for x in range(0, S + 50, 50)] + [(S, S)]
        d.polygon(pts, fill=c)
    d.ellipse([650, 170, 790, 310], fill=(255, 240, 220))
    return im

def neon():
    im = Image.new("RGB", (S, S), (8, 6, 20)); d = ImageDraw.Draw(im)
    col = random.choice([(0, 255, 220), (255, 60, 200), (255, 200, 0)])
    for r in range(380, 60, -40):
        d.rectangle([500 - r, 500 - r, 500 + r, 500 + r], outline=col, width=6)
    return ImageChops.add(im.filter(ImageFilter.GaussianBlur(14)), im)

def mono():
    bg = random.choice([(18, 18, 18), (242, 240, 235)]); fg = (242, 240, 235) if bg[0] < 100 else (18, 18, 18)
    im = Image.new("RGB", (S, S), bg); d = ImageDraw.Draw(im)
    for i in range(0, S, 24):
        d.line([(i, 0), (S - i, S)], fill=fg, width=2)
    d.rectangle([180, 180, 820, 820], fill=bg); d.ellipse([300, 300, 700, 700], outline=fg, width=10)
    return im

STYLES = [sunset, bauhaus, waves, blobs, mountains, neon, mono]

# ---------- music ----------
SR = 44100

def hz(n):
    return 440 * 2 ** ((n - 69) / 12)

def kick(d=.25):
    t = np.arange(int(d * SR)) / SR
    return np.sin(2 * np.pi * np.cumsum(50 + 100 * np.exp(-t * 30)) / SR) * np.exp(-t * 12)

def hat(d=.05):
    t = np.arange(int(d * SR)) / SR
    return rng.standard_normal(len(t)) * np.exp(-t * 80) * 0.3

def chord(root, minor, dur):
    t = np.arange(int(dur * SR)) / SR
    x = sum(0.1 * np.sin(2 * np.pi * hz(48 + root + i) * t) + 0.05 * np.sin(2 * np.pi * 2 * hz(48 + root + i) * t)
            for i in (0, 3 if minor else 4, 7))
    x += 0.12 * np.sin(2 * np.pi * hz(36 + root) * t)
    n = len(t); i = np.arange(n)
    return x * np.minimum(1, i / (0.02 * SR)) * np.minimum(1, (n - i) / (0.05 * SR))

def song(bpm, root, minor, secs, drums, fadein, fadeout, gain):
    n = int(secs * SR); x = np.zeros(n); beat = 60 / bpm
    if drums:
        b = 0
        while b * beat < secs:
            i = int(b * beat * SR); k = kick(); e = min(n, i + len(k)); x[i:e] += k[:e - i]
            j = int((b + .5) * beat * SR)
            if j < n:
                h = hat(); e = min(n, j + len(h)); x[j:e] += h[:e - j]
            b += 1
    prog = [0, 8, 3, 10] if minor else [0, 5, 7, 0]
    t0, c = 0.0, 0
    while t0 < secs:
        seg = chord((root + prog[c % 4]) % 12, minor and c % 4 == 0, 4 * beat)
        i = int(t0 * SR); e = min(n, i + len(seg)); x[i:e] += seg[:e - i]; t0 += 4 * beat; c += 1
    if fadein: k = int(fadein * SR); x[:k] *= np.linspace(0, 1, k) ** 2
    if fadeout: k = int(fadeout * SR); x[-k:] *= np.linspace(1, 0, k) ** 2
    x = x / np.max(np.abs(x)) * gain
    return (np.stack([x, x * 0.98], 1) * 32767).astype("<i2")

ALBUMS = [("Lumen Drive", "Nachtfahrt", "Synthwave", 118), ("Mara Vell", "Glass Gardens", "Pop", 104),
          ("Kobaltblau", "Signale", "Techno", 130), ("The Paper Tides", "Low Season", "Indie", 96),
          ("Nordlicht Ensemble", "Aurora Suite", "Ambient", 72), ("Copper & Smoke", "Late Hours", "Jazz", 88),
          ("Hafenkind", "Elbstrom", "Drum & Bass", 174), ("Sola Ferreira", "Atlântico", "Soul", 92),
          ("Static Bloom", "Overgrown", "Electronic", 122), ("Juna Weiss", "Kreise", "Pop", 110),
          ("Velvet Engines", "Chrome Heart", "Rock", 140), ("Orbit 9", "Weltraumpost", "House", 124),
          ("Tilia", "Moos & Stein", "Folk", 84), ("Neon Harbor", "Midnight Ferry", "House", 126),
          ("Grauzone Kollektiv", "Beton", "Hip-Hop", 90), ("Amber Lane", "Golden Room", "Soul", 98)]
TITLES = ["Morgenlicht", "Kilometer", "Echo", "Salz", "Glas", "Tiefe", "Fernweh", "Sterne", "Wellen", "Puls",
          "Rauch", "Kompass", "Nebel", "Funken", "Drift", "Horizont", "Atem", "Lichter"]

with tempfile.TemporaryDirectory() as tmp:
    for i, (artist, album, genre, bpm) in enumerate(ALBUMS):
        year = 2008 + i
        folder = os.path.join(OUT, artist, f"{year} - {album}")
        os.makedirs(folder, exist_ok=True)
        im = grain(STYLES[i % len(STYLES)]())
        d = ImageDraw.Draw(im)
        tc = (20, 20, 20) if sum(im.getpixel((60, 60))) > 380 else (245, 245, 245)
        if i % 3 != 2:
            d.text((60, 60), artist.upper(), font=font("Manrope-Medium.ttf", 34), fill=tc)
            d.text((60, 105), album, font=font("Manrope-Bold.ttf", 46), fill=tc)
        im.resize((800, 800)).save(os.path.join(folder, "cover.jpg"), quality=90)
        for tn, title in enumerate(random.sample(TITLES, 4), 1):
            minor = random.random() < 0.5
            pcm = song(bpm * random.uniform(0.97, 1.03), random.randrange(12), minor, random.uniform(80, 120),
                       genre != "Ambient", random.choice([0, 0, 6]), random.choice([0, 0, 8]), random.uniform(0.3, 0.7))
            raw = os.path.join(tmp, "x.raw"); pcm.tofile(raw)
            out = os.path.join(folder, f"{tn:02d} {title}.mp3")
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "s16le", "-ar", str(SR), "-ac", "2", "-i", raw,
                            "-b:a", "128k", "-metadata", f"title={title}", "-metadata", f"artist={artist}",
                            "-metadata", f"album_artist={artist}", "-metadata", f"album={album}",
                            "-metadata", f"track={tn}", "-metadata", f"genre={genre}", "-metadata", f"date={year}", out],
                           check=True)
        print(f"{artist} – {album}")
print("Fertig:", OUT)
