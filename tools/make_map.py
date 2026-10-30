#!/usr/bin/env python3
"""Build the radar's background map, src/map_data.c, from OpenStreetMap.

    python tools/make_map.py            # downloads what isn't in tools/map/ yet, then builds
    python tools/make_map.py --fetch    # downloads it all again

What it keeps, around Farnborough (EGLF):
  - the coastline, motorways and trunk roads out to 60 NM (the corners of the 40 NM range);
  - primary roads out to 25 NM;
  - runways, and city and town names, out to 60 NM.
Each line is simplified (Douglas-Peucker) and stored as 16-bit points in 10 m units, east and
north of EGLF's published reference point. The map has no idea where the receiver is: the
firmware places it at run time from the Pi's receiver.json.

Map data (c) OpenStreetMap contributors, under the ODbL (https://www.openstreetmap.org/copyright).
The firmware shows that credit on the radar.
"""
import argparse
import json
import math
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CACHE_DIR = ROOT / "tools" / "map"
OUT = ROOT / "src" / "map_data.c"

ORIGIN = (51.2758, -0.7763)    # EGLF's aerodrome reference point (public; fight-or-flight's airports.ts)
UNIT_M = 10.0
NM = 1852.0
OUTER_NM, INNER_NM = 60, 25
MIN_LENGTH_M = 400             # shorter fragments (after joining) are left out
MIN_RUNWAY_M = 800             # and shorter runways (grass strips): clutter at this scale
CHUNK = 32                     # points a line at most, each with its own bounding box
OVERPASS = "https://overpass-api.de/api/interpreter"
USER_AGENT = "presto-air-traffic map builder (+https://github.com/kieranhj/fight-or-flight)"

# class: (id, simplification tolerance in metres)
CLASSES = {
    "coast": (0, 60),
    "motorway": (1, 40),
    "trunk": (2, 40),
    "primary": (3, 30),
    "runway": (4, 5),
}


def bbox(nm):
    lat0, lon0 = ORIGIN
    dlat = nm / 60
    dlon = nm / (60 * math.cos(math.radians(lat0)))
    return f"{lat0 - dlat:.4f},{lon0 - dlon:.4f},{lat0 + dlat:.4f},{lon0 + dlon:.4f}"


def parts():
    """The download in four small queries, each cached on its own, so a busy server only costs
    the part that failed."""
    outer, inner = bbox(OUTER_NM), bbox(INNER_NM)
    return {
        "coast": f'way["natural"="coastline"]({outer});',
        "major": f'way["highway"~"^(motorway|trunk)$"]({outer});',
        "primary": f'way["highway"="primary"]({inner});',
        "places": f'way["aeroway"="runway"]({outer}); node["place"~"^(city|town)$"]({outer});',
    }


def fetch(endpoint, refetch):
    """Gentle on Overpass: one request at a time, and on any failure an exponential backoff
    (1, 2, 4, 8 minutes, or longer if Retry-After asks), then give up."""
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    for name, body in parts().items():
        path = CACHE_DIR / f"{name}.json"
        if path.exists() and not refetch:
            continue
        query = f"[out:json][timeout:120];\n({body});\nout tags geom qt;"
        data = urllib.parse.urlencode({"data": query}).encode()
        wait = 60
        for attempt in range(5):
            try:
                print(f"{name}: querying {endpoint}...", flush=True)
                req = urllib.request.Request(endpoint, data=data, headers={"User-Agent": USER_AGENT})
                with urllib.request.urlopen(req, timeout=180) as resp:
                    raw = resp.read()
                json.loads(raw)     # a complete answer, not a timeout page
                path.write_bytes(raw)
                print(f"{name}: saved {len(raw) / 1e6:.1f} MB", flush=True)
                break
            except (OSError, ValueError) as e:
                retry_after = getattr(e, "headers", None) and e.headers.get("Retry-After", "")
                if retry_after and str(retry_after).isdigit():
                    wait = max(wait, int(retry_after))
                if attempt == 4:
                    sys.exit(f"{name}: {e}; giving up for now")
                print(f"{name}: {e}; waiting {wait} s", flush=True)
                time.sleep(wait)
                wait *= 2
        time.sleep(5)       # a pause between parts


def load_elements():
    elements = []
    for name in parts():
        elements += json.loads((CACHE_DIR / f"{name}.json").read_text(encoding="utf-8"))["elements"]
    return elements


def to_units(lat, lon):
    lat0, lon0 = ORIGIN
    x = (lon - lon0) * 60 * NM * math.cos(math.radians(lat0)) / UNIT_M
    y = (lat - lat0) * 60 * NM / UNIT_M
    return x, y


def simplify(pts, tol):
    """Douglas-Peucker, tolerance in units."""
    if len(pts) < 3:
        return pts
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        ax, ay = pts[a]
        bx, by = pts[b]
        dx, dy = bx - ax, by - ay
        L = math.hypot(dx, dy) or 1e-9
        best, idx = -1.0, -1
        for i in range(a + 1, b):
            px, py = pts[i]
            d = abs(dy * (px - ax) - dx * (py - ay)) / L
            if d > best:
                best, idx = d, i
        if best > tol:
            keep[idx] = True
            stack += [(a, idx), (idx, b)]
    return [p for p, k in zip(pts, keep) if k]


def join(ways):
    """Joins ways that share an end node into longer chains: OSM splits a road into many short
    ways, and every line costs a table entry and a cull test on the board."""
    ends = {}
    for i, w in enumerate(ways):
        ends.setdefault(w[0], []).append(i)
        ends.setdefault(w[-1], []).append(i)
    used = [False] * len(ways)

    def take(at):
        for j in ends.get(at, []):
            if not used[j]:
                used[j] = True
                w = ways[j]
                return w if w[0] == at else w[::-1]
        return None

    chains = []
    for i, w in enumerate(ways):
        if used[i]:
            continue
        used[i] = True
        chain = list(w)
        while (nxt := take(chain[-1])) is not None:
            chain += nxt[1:]
        while (prev := take(chain[0])) is not None:
            # prev starts at chain[0]: reversed, it ends there
            chain = prev[::-1][:-1] + chain
        chains.append(chain)
    return chains


def length(pts):
    return sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(pts, pts[1:]))


def classify(tags):
    if tags.get("natural") == "coastline":
        return "coast"
    if tags.get("aeroway") == "runway":
        return "runway"
    return tags.get("highway")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fetch", action="store_true", help="download the OSM data again")
    ap.add_argument("--overpass", default=OVERPASS, help="the Overpass server to ask")
    args = ap.parse_args()
    fetch(args.overpass, args.fetch)
    elements = load_elements()

    lines, places, ways = [], [], {}
    limit = 32000
    for e in elements:
        if e["type"] == "node":
            tags = e.get("tags", {})
            name = tags.get("name")
            if not name:
                continue
            x, y = to_units(e["lat"], e["lon"])
            try:
                pop = int(str(tags.get("population", "0")).replace(",", "").split(";")[0] or 0)
            except ValueError:
                pop = 0
            places.append((tags.get("place"), pop, name, round(x), round(y)))
            continue
        cls = classify(e.get("tags", {}))
        if cls not in CLASSES or "geometry" not in e:
            continue
        ways.setdefault(cls, []).append([(g["lat"], g["lon"]) for g in e["geometry"]])

    for cls, raw in ways.items():
        cid, tol_m = CLASSES[cls]
        for chain in join(raw):
            pts = simplify([to_units(lat, lon) for lat, lon in chain], tol_m / UNIT_M)
            pts = [(round(x), round(y)) for x, y in pts if abs(x) < limit and abs(y) < limit]
            min_m = MIN_RUNWAY_M if cls == "runway" else MIN_LENGTH_M
            if len(pts) >= 2 and length(pts) * UNIT_M >= min_m:
                # in chunks (sharing their end points) so the board can skip what is off screen
                for i in range(0, len(pts) - 1, CHUNK - 1):
                    lines.append((cid, pts[i:i + CHUNK]))

    # draw order: coast, then smaller roads under bigger ones, runways last
    order = {0: 0, 3: 1, 2: 2, 1: 3, 4: 4}
    lines.sort(key=lambda l: order[l[0]])

    # importance: 0 city or a town over 50k people (shown at every range), 1 over 15k, 2 the rest
    def rank(p):
        place, pop = p[0], p[1]
        if place == "city" or pop >= 50000:
            return 0
        return 1 if pop >= 15000 else 2
    places = sorted({p[2]: p for p in places}.values(), key=lambda p: (rank(p), -p[1]))

    points = [pt for _, pts in lines for pt in pts]
    out = [
        "// Generated by tools/make_map.py: don't edit.",
        "// Map data (c) OpenStreetMap contributors, ODbL (https://www.openstreetmap.org/copyright).",
        '#include "map.h"',
        "",
        f"static const int16_t map_xy[{len(points) * 2}] = {{",
    ]
    for i in range(0, len(points), 8):
        out.append("    " + " ".join(f"{x}, {y}," for x, y in points[i:i + 8]))
    out.append("};")
    out.append(f"static const map_line_t map_lines_[{len(lines)}] = {{")
    start = 0
    for cid, pts in lines:
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        out.append(f"    {{{start}, {len(pts)}, {cid}, {{{min(xs)}, {min(ys)}, {max(xs)}, {max(ys)}}}}},")
        start += len(pts)
    out.append("};")
    out.append(f"static const map_place_t map_places_[{len(places)}] = {{")
    for p in places:
        name = p[2].replace("\\", "\\\\").replace('"', '\\"').encode("ascii", "replace").decode()
        out.append(f'    {{"{name}", {p[3]}, {p[4]}, {rank(p)}}},')
    out.append("};")
    out.append(f"const map_t map = {{{ORIGIN[0]}, {ORIGIN[1]}, {UNIT_M:.1f}f, map_xy, map_lines_, "
               f"{len(lines)}, map_places_, {len(places)}}};")
    OUT.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    counts = {}
    for cid, _ in lines:
        counts[cid] = counts.get(cid, 0) + 1
    names = {v[0]: k for k, v in CLASSES.items()}
    print(f"{len(lines)} lines, {len(points)} points ({len(points) * 4 / 1024:.0f} KB), "
          f"{len(places)} places: " + ", ".join(f"{names[c]} {n}" for c, n in sorted(counts.items())))


if __name__ == "__main__":
    sys.exit(main())
