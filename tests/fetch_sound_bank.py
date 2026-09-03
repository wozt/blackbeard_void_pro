#!/usr/bin/env python3
"""Build the listening-test bank in test_sound_files/.

Sweeps say how the processing treats one sine at a time; they say nothing
about how it treats a waterfall, a gunshot or an orchestra. The bank is
deliberately spread over crest factor, bandwidth and stereo width, so a
filter that only flatters sweeps has somewhere to fail.

Two freely usable sources, no API key:
  - Wikimedia Commons, for music and field recordings
  - the BBC sound-effects archive, for the game-like effects Commons has
    little of (gunfire, explosions, engines, footsteps)

Every clip ends up 8 s, 48 kHz stereo 16-bit -- the format the dongle
receives, so nothing is resampled further down the chain -- normalised to
-12 dBFS peak. That last part matters: the hardware limiter engages around
39 % of full scale, and identification has to stay in the linear region.
"""
import io, json, subprocess, sys, urllib.parse, urllib.request, wave, zipfile
from pathlib import Path

import numpy as np

OUT = Path("test_sound_files")
UA = "bvp-research/1.0 (samuel.courtecuisse@gmail.com)"
SR, DUR, PEAK_DBFS = 48000, 8.0, -12.0

COMMONS = "commons"
BBC = "bbc"

# name -> (source, query, needs real stereo?)
WANTED = [
    ("water_falls",   COMMONS, "filetype:audio waterfall",               True),
    ("forest_birds",  COMMONS, "filetype:audio forest birds ambience",   True),
    ("orchestra",     COMMONS, "filetype:audio symphony orchestra",      True),
    ("piano",         COMMONS, "filetype:audio piano sonata",            False),
    ("choir_voices",  COMMONS, "filetype:audio choir a cappella",        False),
    ("electronic",    COMMONS, "filetype:audio electronic music track",  True),
    ("applause",      COMMONS, "filetype:audio applause crowd",          True),
    ("speech",        COMMONS, "filetype:audio spoken word recording",   False),
    ("rain_thunder",  BBC,     "thunder rain",                           True),
    ("gunshot",       BBC,     "gunfire rifle",                          True),
    ("explosion",     BBC,     "explosion",                              False),
    ("engine",        BBC,     "helicopter",                             True),
    ("footsteps",     BBC,     "footsteps gravel",                       True),
    ("vehicle_pass",  BBC,     "car pass by",                            True),
    ("crowd",         BBC,     "crowd stadium",                          True),
    ("room_tone",     BBC,     "atmosphere room",                        True),
]


def get(url, timeout=120):
    r = urllib.request.Request(url, headers={"User-Agent": UA})
    return urllib.request.urlopen(r, timeout=timeout).read()


# ---------- catalogues ----------

def commons_search(term, limit=14):
    q = urllib.parse.urlencode({
        "action": "query", "generator": "search", "gsrsearch": term,
        "gsrnamespace": 6, "gsrlimit": limit, "prop": "imageinfo",
        "iiprop": "url|size|mime", "format": "json"})
    d = json.loads(get("https://commons.wikimedia.org/w/api.php?" + q, 30))
    out = []
    for p in d.get("query", {}).get("pages", {}).values():
        ii = p["imageinfo"][0]
        if 200_000 < ii["size"] < 120_000_000:
            out.append((p["title"], ii["url"].split("?")[0]))
    return out


def bbc_search(term, limit=14):
    body = json.dumps({"criteria": {
        "from": 0, "size": limit, "query": term, "caption": None, "id": None,
        "categories": None, "durations": None, "continents": None,
        "sortBy": None, "source": None, "recordist": None, "habitat": None}})
    req = urllib.request.Request(
        "https://sound-effects-api.bbcrewind.co.uk/api/sfx/search",
        data=body.encode(), headers={"User-Agent": UA,
                                     "Content-Type": "application/json"})
    d = json.load(urllib.request.urlopen(req, timeout=30))
    out = []
    for r in d.get("results", []):
        tm = r.get("technicalMetadata", {})
        if int(tm.get("channels", 0)) < 2 or float(tm.get("duration", 0)) < DUR:
            continue
        out.append((f"BBC {r['id']}: {r['description']}",
                    f"https://sound-effects-media.bbcrewind.co.uk/wav/{r['id']}.wav"))
    return out


# ---------- conditioning ----------

def decode(raw, suffix):
    """Anything ffmpeg understands -> float array, 48 kHz stereo."""
    tmp = OUT / ("_dl" + suffix)
    tmp.write_bytes(raw)
    p = subprocess.run(["ffmpeg", "-v", "quiet", "-i", str(tmp), "-ac", "2",
                        "-ar", str(SR), "-f", "s16le", "-"],
                       capture_output=True)
    tmp.unlink(missing_ok=True)
    if p.returncode != 0 or not p.stdout:
        return None
    return np.frombuffer(p.stdout, dtype="<i2").astype(float).reshape(-1, 2) / 32768.0


def loudest_window(x):
    """The excerpt has to contain the event, not the silence before it."""
    n = int(DUR * SR)
    if len(x) <= n:
        return x
    b = SR // 4
    e = (x[:len(x) // b * b, :] ** 2).mean(axis=1).reshape(-1, b).mean(axis=1)
    w = n // b
    s = np.convolve(e, np.ones(w) / w, mode="valid")
    return x[int(np.argmax(s)) * b:][:n]


def usable(x, want_stereo):
    if x is None or len(x) < DUR * SR * 0.9:
        return "too short"
    if np.abs(x).max() < 0.02:
        return "near silent"
    L, R = x[:, 0], x[:, 1]
    if want_stereo:
        if L.std() < 1e-6 or R.std() < 1e-6:
            return "one dead channel"
        c = float(np.corrcoef(L, R)[0, 1])
        if c > 0.995:
            return f"dual mono (corr {c:+.3f})"
    return None


def write(path, x):
    x = x / max(np.abs(x).max(), 1e-9) * (10 ** (PEAK_DBFS / 20))
    w = wave.open(str(path), "wb")
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((x * 32767).astype("<i2").tobytes())
    w.close()


def fetch(name, source, term, want_stereo):
    dest = OUT / f"{name}.wav"
    if dest.exists():
        print(f"  {name}: already there")
        return True
    hits = (bbc_search if source == BBC else commons_search)(term)
    for title, url in hits:
        try:
            raw = get(url)
        except Exception as e:
            print(f"    {title[:60]}: download failed ({e})")
            continue
        x = decode(raw, Path(urllib.parse.unquote(url)).suffix or ".bin")
        x = loudest_window(x) if x is not None else None
        why = usable(x, want_stereo)
        if why:
            print(f"    {title[:60]}: {why}")
            continue
        write(dest, x)
        (OUT / f"{name}.source.txt").write_text(f"{title}\n{url}\n")
        print(f"  {name}: {title[:70]}")
        return True
    print(f"  {name}: nothing usable found")
    return False


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    only = sys.argv[1:]
    ok = 0
    for name, source, term, stereo in WANTED:
        if only and name not in only:
            continue
        print(f"{name} <- [{source}] {term}")
        ok += fetch(name, source, term, stereo)
    print(f"\n{ok}/{len(only) or len(WANTED)} clips in {OUT}/")
