#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A stand-in for internet radio, the directory radio-browser.info and (for audio
CDs) MusicBrainz and the Cover Art Archive, for testing BuB3Player without the
internet:

    tools/mock-radio.py --audio /path/to/some/mp3s [--port 8099]
    HALCYON_RADIOBROWSER_URL=http://127.0.0.1:8099 HALCYON_MUSICBRAINZ_URL=http://127.0.0.1:8099 \
        HALCYON_COVERART_URL=http://127.0.0.1:8099 build/halcyon

Stations (endless MP3 streams with ICY metadata: the song title changes every
--every seconds):
    /stream/<n>          station n (0…4), "Testwelle <n>"
    /list/<n>.pls|.m3u   a station list pointing at /stream/<n>
    /broken              answers 404
    /seite               a web page (text/html), not a station
radio-browser.info (only what the player uses):
    /json/stations/search?name=…|tag=…   finds the five stations
    /json/url/<uuid>     counts a click
    /logo/<n>.png        a station logo
MusicBrainz and Cover Art Archive:
    /ws/2/discid/<id>    knows every disc: "Testscheibe" by "Mara Vell" (10 titles,
                         cut to the disc by the player); the id "unbekannt-" is unknown
    /release/<id>/front-500   the front cover
    /mock/requests       what was asked (JSON)
"""
import argparse, json, os, re, struct, threading, time, zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

args = None
frames = []          # MP3 frames to loop
requests = []
lock = threading.Lock()
NAMES = ["Testwelle 0", "Jazz Nacht", "Klassik Morgen", "Rock Kanal", "Nordsee Radio"]
TAGS = ["pop", "jazz", "classical", "rock", "news,pop"]
SONGS = [("Amber Lane", "Rauch"), ("Kobaltblau", "Puls"), ("Hafenkind", "Lichter"), ("Mara Vell", "Glas"), ("Orbit 9", "Stern")]


def mp3_frames(path):
    """Split an MP3 file into frames (MPEG-1 Layer III, skipping ID3)."""
    data = open(path, "rb").read()
    i = 0
    if data[:3] == b"ID3":
        size = (data[6] << 21) | (data[7] << 14) | (data[8] << 7) | data[9]
        i = 10 + size
    out = []
    rates = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0]
    srates = [44100, 48000, 32000, 0]
    while i + 4 <= len(data):
        h = struct.unpack(">I", data[i:i + 4])[0]
        if (h >> 21) & 0x7FF != 0x7FF:
            i += 1
            continue
        br = rates[(h >> 12) & 0xF] * 1000
        sr = srates[(h >> 10) & 0x3]
        pad = (h >> 9) & 1
        if br == 0 or sr == 0:
            i += 1
            continue
        n = 144 * br // sr + pad
        out.append(data[i:i + n])
        i += n
    return out


def png(color):
    w = h = 32
    raw = b"".join(b"\x00" + bytes(color) * w for _ in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def station(n, base):
    return {"stationuuid": "uuid-%d" % n, "name": NAMES[n], "url": base + "/list/%d.pls" % n,
            "url_resolved": base + "/stream/%d" % n, "homepage": "http://example.org/%d" % n,
            "favicon": base + "/logo/%d.png" % n, "tags": TAGS[n], "country": "Germany", "codec": "MP3",
            "bitrate": 128}


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *a):
        pass

    def send_body(self, body, ctype, code=200):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        base = "http://%s" % self.headers.get("Host", "127.0.0.1:%d" % args.port)
        with lock:
            requests.append({"path": self.path, "ua": self.headers.get("User-Agent", ""), "time": time.time()})
        m = re.fullmatch(r"/stream/(\d)", u.path)
        if m:
            return self.stream(int(m.group(1)))
        m = re.fullmatch(r"/list/(\d)\.(pls|m3u)", u.path)
        if m:
            n = int(m.group(1))
            if m.group(2) == "pls":
                body = "[playlist]\nNumberOfEntries=1\nFile1=%s/stream/%d\nTitle1=%s\nLength1=-1\nVersion=2\n" % (base, n, NAMES[n])
                return self.send_body(body.encode(), "audio/x-scpls")
            body = "#EXTM3U\n#EXTINF:-1,%s\n%s/stream/%d\n" % (NAMES[n], base, n)
            return self.send_body(body.encode(), "audio/x-mpegurl")
        if u.path == "/seite":
            return self.send_body(b"<html><body>Kein Radio</body></html>", "text/html; charset=utf-8")
        m = re.fullmatch(r"/logo/(\d)\.png", u.path)
        if m:
            n = int(m.group(1))
            return self.send_body(png([(40 + 50 * n) % 256, 120, (200 - 30 * n) % 256]), "image/png")
        if u.path == "/json/stations/search":
            text = (q.get("name", [""])[0] or q.get("tag", [""])[0]).lower()
            field = "name" if "name" in q else "tag"
            hits = [station(n, base) for n in range(len(NAMES))
                    if text in (NAMES[n].lower() if field == "name" else TAGS[n])]
            return self.send_body(json.dumps(hits).encode(), "application/json")
        m = re.fullmatch(r"/ws/2/discid/([A-Za-z0-9._-]+)", u.path)
        if m:
            disc = m.group(1)
            if disc.startswith("unbekannt"):
                return self.send_body(json.dumps({"error": "Not Found"}).encode(), "application/json", 404)
            credit = [{"name": "Mara Vell", "joinphrase": ""}]
            tracks = [{"title": t, "artist-credit": credit if i != 1 else
                       [{"name": "Mara Vell", "joinphrase": " feat. "}, {"name": "Orbit 9", "joinphrase": ""}]}
                      for i, t in enumerate(["Glas", "Nebelhorn", "Leuchtturm", "Ebbe", "Flut",
                                             "Salz", "Möwen", "Deich", "Brandung", "Heimkehr"])]
            release = {"id": "rel-1", "title": "Testscheibe", "date": "2024-05-01", "artist-credit": credit,
                       "media": [{"discs": [{"id": "andere"}], "tracks": []},
                                 {"discs": [{"id": disc}], "tracks": tracks}]}
            return self.send_body(json.dumps({"id": disc, "releases": [release]}).encode(), "application/json")
        m = re.fullmatch(r"/release/([A-Za-z0-9-]+)/front-500", u.path)
        if m:
            return self.send_body(png([200, 90, 60]), "image/png")
        if u.path.startswith("/json/url/"):
            return self.send_body(json.dumps({"ok": True}).encode(), "application/json")
        if u.path == "/mock/requests":
            with lock:
                return self.send_body(json.dumps(requests).encode(), "application/json")
        return self.send_body(b"not found", "text/plain", 404)

    def stream(self, n):
        icy = self.headers.get("Icy-MetaData") == "1"
        metaint = 8192
        self.send_response(200)
        self.send_header("Content-Type", "audio/mpeg")
        self.send_header("icy-name", NAMES[n])
        self.send_header("icy-genre", TAGS[n])
        self.send_header("icy-br", "128")
        self.send_header("icy-url", "http://example.org/%d" % n)
        if icy:
            self.send_header("icy-metaint", str(metaint))
        self.end_headers()
        start = time.time()
        sent = 0         # audio bytes since the last metadata block
        fi = 0
        pending = b""
        frame_time = 1152 / 44100
        try:
            while not args.cut or time.time() - start < args.cut:
                pending += frames[fi % len(frames)]
                fi += 1
                if not icy:
                    self.wfile.write(pending)
                    pending = b""
                else:
                    while len(pending) >= metaint - sent:
                        part = pending[:metaint - sent]
                        pending = pending[metaint - sent:]
                        self.wfile.write(part)
                        artist, title = SONGS[(int((time.time() - start) / args.every) + n) % len(SONGS)]
                        meta = ("StreamTitle='%s - %s';" % (artist, title)).encode()
                        meta += b"\0" * ((16 - len(meta) % 16) % 16)
                        self.wfile.write(bytes([len(meta) // 16]) + meta)
                        sent = 0
                    self.wfile.write(pending)
                    sent += len(pending)
                    pending = b""
                # real time, a little ahead
                ahead = fi * frame_time - (time.time() - start)
                if ahead > 2:
                    time.sleep(ahead - 2)
        except (BrokenPipeError, ConnectionResetError):
            pass


def main():
    global args
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--audio", required=True, help="folder with MP3 files (44.1 kHz)")
    ap.add_argument("--every", type=float, default=12, help="seconds per song title")
    ap.add_argument("--cut", type=float, default=0, help="end every stream after this many seconds (dropped connection)")
    args = ap.parse_args()
    for root, _, files in os.walk(args.audio):
        for f in sorted(files):
            if f.lower().endswith(".mp3"):
                frames.extend(mp3_frames(os.path.join(root, f)))
        if len(frames) > 20000:
            break
    if not frames:
        raise SystemExit("keine MP3-Dateien in " + args.audio)
    print("mock-radio: %d frames on :%d" % (len(frames), args.port), flush=True)
    ThreadingHTTPServer(("127.0.0.1", args.port), H).serve_forever()


if __name__ == "__main__":
    main()
