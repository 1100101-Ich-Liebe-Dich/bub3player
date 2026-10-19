#!/usr/bin/env python3
"""A small stand-in for a Jellyfin server (API 10.9/10.10 shapes), for testing
BuB3Player without a real server.

    tools/mock-jellyfin.py --port 8097 --audio /path/to/some/music --tracks 1500

Login: demo / demo (Quick Connect is approved automatically after 3 s).
Serves real audio files (with HTTP Range, so seeking works) and cover images.
Extra endpoints for tests:
    POST /mock/change   add 5 new tracks, rename 1, remove 2 (incremental sync)
    GET  /mock/reports  playback reports the player sent (JSON lines)
Only meant for local testing; there is no real security.
"""
import argparse, datetime, hashlib, json, os, random, re, threading, time, uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

SERVER_ID = "f1e2d3c4b5a697887766554433221100"
USER_ID = "0a1b2c3d4e5f60718293a4b5c6d7e8f9"
TOKEN = "mocktoken0123456789abcdef"
LIB_MUSIC = "aa00000000000000000000000000music"
LIB_MOVIES = "bb0000000000000000000000000movies"
LIB_MUSIC2 = "cc0000000000000000000000000music2"

lock = threading.Lock()
items = {}          # id -> dto
order = []          # ids in insertion order
audio_files = []    # real files to stream
images = []         # real jpgs to serve
reports = []
quick = {}          # secret -> created
now_iso = lambda t=None: datetime.datetime.fromtimestamp(t or time.time(), datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.0000000Z")


def gid(*parts):
    return hashlib.md5("|".join(map(str, parts)).encode()).hexdigest()


def make_track(lib, artist, album, year, genre, no, title, ext, saved):
    tid = gid(lib, artist, album, no, title)
    album_id = gid(lib, artist, album)
    codec = {"flac": "flac", "mp3": "mp3", "ogg": "vorbis", "opus": "opus", "m4a": "aac"}.get(ext, "flac")
    hires = ext == "flac" and random.random() < 0.3
    return {
        "Name": title, "ServerId": SERVER_ID, "Id": tid, "Type": "Audio", "MediaType": "Audio",
        "IsFolder": False, "CollectionType": None,
        "Album": album, "AlbumId": album_id, "AlbumArtist": artist,
        "AlbumArtists": [{"Name": artist, "Id": gid("artist", artist)}],
        "Artists": [artist], "ArtistItems": [{"Name": artist, "Id": gid("artist", artist)}],
        "IndexNumber": no, "ParentIndexNumber": 1, "ProductionYear": year, "Genres": [genre],
        "RunTimeTicks": random.randint(150, 330) * 10_000_000, "Container": ext,
        "Path": f"/srv/media/music/{artist}/{year} - {album}/{no:02d} - {title}.{ext}",
        "DateCreated": now_iso(saved - random.randint(0, 400) * 86400),
        "_saved": saved, "_lib": lib,
        "ImageTags": {}, "AlbumPrimaryImageTag": gid("img", album_id)[:12],
        "UserData": {"PlayCount": random.choice([0, 0, 0, 1, 3, 12]), "IsFavorite": random.random() < 0.05,
                     "Played": False, "Key": tid},
        "MediaSources": [{"Id": tid, "Protocol": "File", "Container": ext, "Size": random.randint(3, 60) * 1_000_000,
                          "Bitrate": 900_000 if ext == "flac" else 320_000,
                          "MediaStreams": [{"Type": "Audio", "Codec": codec, "Index": 0, "Channels": 2,
                                            "SampleRate": 96000 if hires else 44100,
                                            "BitDepth": 24 if hires else (16 if ext == "flac" else None),
                                            "BitRate": 900_000 if ext == "flac" else 320_000}]}],
        "NormalizationGain": round(random.uniform(-9, -2), 2),
    }


def build(n_tracks, seed=5):
    random.seed(seed)
    words = ["Nordwind", "Lichtjahre", "Asphalt", "Sommerregen", "Glass Rivers", "Blaue Stunde", "Static Bloom",
             "Hafenlichter", "Midnight Ferry", "Kupfer", "Sundial", "Weiße Nächte", "Paper Planets", "Kompass",
             "Low Tide", "Neonwald", "Echo Park", "Fernlicht", "Slow Burn", "Seegang"]
    titles = ["Anfang", "Strom", "Fernweh", "Gravity", "Herbst", "Ozean", "Lantern", "Signal", "Drift", "Morgen",
              "Ember", "Sirene", "Aurora", "Nachtzug", "Velvet", "Brücken", "Monsun", "Ende"]
    genres = ["Indie", "Jazz", "Electronic", "Deutschpop", "Ambient", "Rock", "Klassik", "Soul"]
    exts = ["flac", "flac", "mp3", "ogg", "opus", "m4a"]
    base = time.time() - 3600
    count = 0
    a = 0
    while count < n_tracks:
        artist = f"{random.choice(['Die', 'The', 'Los', ''])} {random.choice(words)} {random.choice(['Band', 'Kollektiv', 'Trio', 'Club', 'Sisters'])}".strip()
        for k in range(random.randint(1, 4)):
            album = random.choice(words) + ("" if k == 0 else f" {['II', 'III', 'Live', 'Remixe'][k - 1]}")
            year = random.randint(1965, 2025)
            genre = random.choice(genres)
            ext = random.choice(exts)
            lib = LIB_MUSIC if random.random() < 0.85 else LIB_MUSIC2
            for no in range(1, random.randint(6, 13)):
                t = make_track(lib, artist, album, year, genre, no, random.choice(titles), ext, base)
                items[t["Id"]] = t
                order.append(t["Id"])
                count += 1
                if count >= n_tracks:
                    return
        a += 1


def view(id_, name, ctype):
    return {"Name": name, "ServerId": SERVER_ID, "Id": id_, "Type": "CollectionFolder", "IsFolder": True,
            "CollectionType": ctype, "ImageTags": {}}


def public_dto(t, fields):
    d = {k: v for k, v in t.items() if not k.startswith("_")}
    if "Path" not in fields:
        d.pop("Path", None)
    if "MediaSources" not in fields:
        d.pop("MediaSources", None)
    if "DateCreated" not in fields:
        d.pop("DateCreated", None)
    if "Genres" not in fields:
        d.pop("Genres", None)
    return d


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        if os.environ.get("MOCK_VERBOSE"):
            super().log_message(fmt, *args)

    # --- helpers ---
    def q(self):
        u = urlparse(self.path)
        return u.path, {k.lower(): v[0] for k, v in parse_qs(u.query).items()}

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b""
        try:
            return json.loads(raw or b"{}")
        except Exception:
            return {}

    def send_json(self, obj, code=200):
        data = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def empty(self, code=204):
        self.send_response(code)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def authed(self, query):
        auth = self.headers.get("X-Emby-Authorization") or self.headers.get("Authorization") or ""
        m = re.search(r'Token="([^"]*)"', auth)
        tok = (m.group(1) if m else None) or self.headers.get("X-Emby-Token") or query.get("api_key") or query.get("apikey")
        return tok == TOKEN

    def send_file(self, path, ctype):
        size = os.path.getsize(path)
        start, end = 0, size - 1
        rng = self.headers.get("Range")
        if rng:
            m = re.match(r"bytes=(\d*)-(\d*)", rng)
            if m:
                if m.group(1):
                    start = int(m.group(1))
                if m.group(2):
                    end = min(int(m.group(2)), size - 1)
            self.send_response(206)
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        else:
            self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(end - start + 1))
        self.end_headers()
        if self.command == "HEAD":
            return
        with open(path, "rb") as f:
            f.seek(start)
            left = end - start + 1
            try:
                while left > 0:
                    chunk = f.read(min(65536, left))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    left -= len(chunk)
            except (BrokenPipeError, ConnectionResetError):
                pass

    # --- routes ---
    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        path, q = self.q()
        p = path.lower()
        if p == "/system/info/public":
            return self.send_json({"LocalAddress": "http://127.0.0.1", "ServerName": "Mock-Jellyfin", "Version": "10.10.7",
                                   "ProductName": "Jellyfin Server", "Id": SERVER_ID, "StartupWizardCompleted": True})
        if p == "/quickconnect/enabled":
            return self.send_json(True)
        if p == "/quickconnect/initiate":
            return self.quick_initiate()
        if p == "/quickconnect/connect":
            s = q.get("secret", "")
            if s not in quick:
                return self.empty(404)
            ok = time.time() - quick[s] > 3
            return self.send_json({"Authenticated": ok, "Secret": s, "Code": "123456"})
        if p == "/mock/reports":
            with lock:
                return self.send_json(reports)
        if not self.authed(q):
            return self.empty(401)
        if p == "/userviews" or re.fullmatch(r"/users/[0-9a-f]+/views", p):
            v = [view(LIB_MUSIC, "Musik", "music"), view(LIB_MOVIES, "Filme", "movies"), view(LIB_MUSIC2, "Vinyl-Rips", "music")]
            return self.send_json({"Items": v, "TotalRecordCount": len(v), "StartIndex": 0})
        if p == "/items" or re.fullmatch(r"/users/[0-9a-f]+/items", p):
            return self.items(q)
        m = re.fullmatch(r"/audio/([0-9a-f]+)/(stream|universal)(\.\w+)?", p)
        if m:
            t = items.get(m.group(1))
            if not t:
                return self.empty(404)
            f = audio_files[int(t["Id"], 16) % len(audio_files)]
            ext = os.path.splitext(f)[1].lower()
            ctype = {".flac": "audio/flac", ".mp3": "audio/mpeg", ".ogg": "audio/ogg", ".opus": "audio/ogg", ".m4a": "audio/mp4"}.get(ext, "application/octet-stream")
            return self.send_file(f, ctype)
        if p == "/artists":  # artist items by name: their picture is /Items/<id>/Images/Primary
            term = q.get("searchterm", "").lower()
            names = sorted({t.get("AlbumArtist") or "" for t in items.values()} - {""})
            found = [{"Name": n, "Id": "%032x" % (sum(map(ord, n)) * 7919), "Type": "MusicArtist",
                      "ImageTags": {"Primary": "a%x" % sum(map(ord, n))}} for n in names if term in n.lower()]
            return self.send_json({"Items": found, "TotalRecordCount": len(found), "StartIndex": 0})
        m = re.fullmatch(r"/items/([0-9a-f]+)/images/primary(/\d+)?", p)
        if m:
            if not images:
                return self.empty(404)
            return self.send_file(images[int(m.group(1), 16) % len(images)], "image/jpeg")
        return self.empty(404)

    def do_POST(self):
        path, q = self.q()
        p = path.lower()
        b = self.body()
        if p == "/users/authenticatebyname":
            if b.get("Username") == "demo" and b.get("Pw") == "demo":
                return self.send_json(self.auth_result())
            return self.empty(401)
        if p == "/quickconnect/initiate":
            return self.quick_initiate()
        if p == "/users/authenticatewithquickconnect":
            s = b.get("Secret", "")
            if s in quick and time.time() - quick[s] > 3:
                return self.send_json(self.auth_result())
            return self.empty(401)
        if p == "/mock/change":
            return self.change()
        if not self.authed(q):
            return self.empty(401)
        if p.startswith("/sessions/playing") or p == "/sessions/capabilities/full":
            with lock:
                reports.append({"path": path, "body": b, "time": time.time()})
            return self.empty(204)
        return self.empty(404)

    def quick_initiate(self):
        s = uuid.uuid4().hex
        quick[s] = time.time()
        return self.send_json({"Authenticated": False, "Secret": s, "Code": "123456", "DeviceId": "x",
                               "DeviceName": "x", "AppName": "x", "AppVersion": "x", "DateAdded": now_iso()})

    def auth_result(self):
        return {"User": {"Name": "demo", "ServerId": SERVER_ID, "Id": USER_ID, "HasPassword": True,
                         "Policy": {"IsAdministrator": False}},
                "SessionInfo": {"Id": uuid.uuid4().hex}, "AccessToken": TOKEN, "ServerId": SERVER_ID}

    def items(self, q):
        parent = q.get("parentid")
        types = q.get("includeitemtypes", "")
        start = int(q.get("startindex", 0))
        limit = int(q.get("limit", 100))
        fields = q.get("fields", "")
        since = q.get("mindatelastsaved")
        ids_filter = set(q.get("ids", "").split(",")) if q.get("ids") else None
        since_t = 0
        if since:
            since_t = datetime.datetime.fromisoformat(since.replace("Z", "+00:00")).timestamp()
        with lock:
            sel = [items[i] for i in order if i in items]
        if parent:
            sel = [t for t in sel if t["_lib"] == parent]
        if types and "audio" not in types.lower():
            sel = []
        if since_t:
            sel = [t for t in sel if t["_saved"] >= since_t]
        if ids_filter:
            sel = [t for t in sel if t["Id"] in ids_filter]
        page = sel[start:start + limit]
        out = [public_dto(t, fields) for t in page]
        if q.get("enabletotalrecordcount", "true").lower() == "false":
            total = 0
        else:
            total = len(sel)
        return self.send_json({"Items": out, "TotalRecordCount": total, "StartIndex": start})

    def change(self):
        with lock:
            live = [i for i in order if i in items]
            gone = random.sample(live, 2)
            for i in gone:
                del items[i]
            r = items[random.choice([i for i in live if i in items])]
            r["Name"] = r["Name"] + " (Remaster)"
            r["_saved"] = time.time()
            for n in range(5):
                t = make_track(LIB_MUSIC, "Neue Band", "Frisch Eingetroffen", 2026, "Indie", n + 1, f"Neu {n + 1}", "flac", time.time())
                items[t["Id"]] = t
                order.append(t["Id"])
        return self.send_json({"removed": gone, "renamed": r["Id"], "added": 5})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8097)
    ap.add_argument("--audio", required=True, help="folder with audio files to stream")
    ap.add_argument("--images", default=None, help="folder with jpg covers (default: covers next to the audio)")
    ap.add_argument("--tracks", type=int, default=1500)
    a = ap.parse_args()
    for root, _, files in os.walk(a.audio):
        for f in files:
            if f.lower().endswith((".flac", ".mp3", ".ogg", ".opus", ".m4a")):
                audio_files.append(os.path.join(root, f))
    for root, _, files in os.walk(a.images or a.audio):
        for f in files:
            if f.lower().endswith(".jpg"):
                images.append(os.path.join(root, f))
    audio_files.sort()
    images.sort()
    build(a.tracks)
    print(f"mock-jellyfin: {len(items)} tracks, {len(audio_files)} audio files, {len(images)} images on :{a.port}", flush=True)
    ThreadingHTTPServer(("127.0.0.1", a.port), H).serve_forever()


if __name__ == "__main__":
    main()
