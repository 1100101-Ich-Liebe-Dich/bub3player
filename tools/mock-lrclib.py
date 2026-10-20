#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A small stand-in for LRCLIB (lrclib.net), for testing the lyrics download
without the internet:

    tools/mock-lrclib.py --port 8098
    HALCYON_LRCLIB_URL=http://127.0.0.1:8098 build/halcyon …

GET /api/get?track_name&artist_name&album_name&duration and
GET /api/search?track_name&artist_name like the real service. Titles whose
name starts with A–F have synchronised lyrics, G–L plain ones, M–P are
instrumental, the rest are unknown (404 / empty search).
GET /mock/requests lists the requests the player made (JSON), so tests can see
that answers are kept and not asked for twice.
"""
import argparse, json, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

lock = threading.Lock()
requests = []


def record(title, artist, album, duration):
    c = (title[:1] or "z").upper()
    rec = {"id": abs(hash((title, artist))) % 100000, "trackName": title, "artistName": artist, "albumName": album,
           "duration": float(duration or 0), "instrumental": False, "plainLyrics": None, "syncedLyrics": None}
    lines = [f"{title}: Netzzeile {i + 1}" if i % 6 != 5 else "" for i in range(30)]
    if "A" <= c <= "F":
        rec["syncedLyrics"] = "\n".join(f"[{(3 + i * 3) // 60:02d}:{(3 + i * 3) % 60:02d}.00]{l}" for i, l in enumerate(lines))
        rec["plainLyrics"] = "\n".join(lines)
    elif "G" <= c <= "L":
        rec["plainLyrics"] = "\n".join(lines)
    elif "M" <= c <= "P":
        rec["instrumental"] = True
    else:
        return None
    return rec


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def send_json(self, obj, code=200):
        data = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        if u.path == "/mock/requests":
            with lock:
                return self.send_json(requests)
        with lock:
            requests.append({"path": u.path, "query": q, "agent": self.headers.get("User-Agent", "")})
        title, artist = q.get("track_name", ""), q.get("artist_name", "")
        rec = record(title, artist, q.get("album_name", ""), q.get("duration"))
        if u.path == "/api/get":
            if not rec:
                return self.send_json({"code": 404, "name": "TrackNotFound", "message": "Failed to find specified track"}, 404)
            return self.send_json(rec)
        if u.path == "/api/search":
            return self.send_json([rec] if rec else [])
        return self.send_json({"code": 404}, 404)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8098)
    a = ap.parse_args()
    ThreadingHTTPServer(("127.0.0.1", a.port), H).serve_forever()
