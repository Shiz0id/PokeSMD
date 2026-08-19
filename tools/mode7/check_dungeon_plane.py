#!/usr/bin/env python3
"""
Check the dungeon plane the title screen builds, by replaying the C's own
rasteriser against generated floor plans.

WHAT THIS GUARDS

BuildPlane in src/rogue_mode7.c turns a rooms-and-corridors plan into a 64x64
affine tilemap over a nine-tile vocabulary. Three ways that goes wrong quietly:

  * A DECLARED TILE THAT IS NEVER EMITTED. If a classifier condition is wrong --
    say the wall-highlight test can never be true -- the tile is still built,
    still costs VRAM, and simply never appears. Nothing fails; the plane just
    looks slightly flatter than intended.

  * FLOOR TOUCHING VOID. Rock is grown outward from the floor in two passes. If
    that grows wrong, floor ends up adjacent to transparent nothing, and the
    plane shows a lit floor with no wall edge -- it reads as a hole, not a room.

  * A DISCONNECTED PLAN. Rooms placed but never linked give an island the
    corridors do not reach. On a title screen that is only cosmetic, but it is
    the same defect that would matter if this plan were ever played.

The tile vocabulary and thresholds are parsed out of the C, so this cannot drift
from what ships. Room sizes and counts are parsed too.

The floor PLAN here is generated with Python's RNG, not a replay of the GBA
Random(). That is deliberate: the plan differs run to run anyway, and what is
being checked is the RASTERISER and the invariants, not one particular floor.

--render <file.png> draws a plan using the C's palette, for comparing against
the host prototype. That needs Pillow and so only works where Pillow is
installed -- the checks themselves are pure Python, because run_all_checks.sh
runs under WSL where Pillow is not.

--selftest breaks the rock growth and confirms the adjacency check fires.

Takes the repo positionally or as --repo.
"""

import argparse
import os
import random
import re
import sys
from collections import Counter, deque

DCELL_VOID, DCELL_FLOOR, DCELL_STAIRS, DCELL_ROCK_A, DCELL_ROCK_B = range(5)

TILE_NAMES = [
    "VOID", "FLOOR", "FLOOR_LIT", "STAIRS",
    "WALL_FACE", "WALL_TOP", "WALL_HI", "WALL_SPECK", "WALL_HISPK",
]

PLANS = 40


def parse_defines(repo):
    src = open(os.path.join(repo, "src", "rogue_mode7.c"), encoding="utf-8").read()
    out = {}
    for name in ("DUN_W", "DUN_H", "DUN_MAX_ROOMS", "DUN_ROOM_MIN",
                 "DUN_ROOM_MAX", "DUN_PLACE_TRIES"):
        m = re.search(rf"#define\s+{name}\s+(\d+)", src)
        if not m:
            raise SystemExit(f"could not find #define {name} in src/rogue_mode7.c")
        out[name] = int(m.group(1))

    # The two speckle thresholds, read from TileForCell so they stay in sync.
    lit = re.search(r"hash > (\d+)\) \? DTILE_FLOOR_LIT", src)
    speck = re.search(r"speck = hash > (\d+)", src)
    if not lit or not speck:
        raise SystemExit("could not find the speckle thresholds in TileForCell")
    out["LIT"] = int(lit.group(1))
    out["SPECK"] = int(speck.group(1))

    # Every DTILE_* the C declares must be accounted for.
    declared = re.findall(r"#define\s+DTILE_(\w+)\s+(\d+)", src)
    out["TILES"] = {n: int(v) for n, v in declared}
    return out


def cell_hash(x, y):
    return ((x * 73) ^ (y * 151)) & 0xFF


def generate_plan(d, rng):
    W, H = d["DUN_W"], d["DUN_H"]
    g = [[DCELL_VOID] * W for _ in range(H)]
    rooms = []

    def carve_h(y, x0, x1):
        for x in range(min(x0, x1), max(x0, x1) + 1):
            g[y][x] = DCELL_FLOOR

    def carve_v(x, y0, y1):
        for y in range(min(y0, y1), max(y0, y1) + 1):
            g[y][x] = DCELL_FLOOR

    def carve_l(ax, ay, bx, by, hf):
        if hf:
            carve_h(ay, ax, bx)
            carve_v(bx, ay, by)
        else:
            carve_v(ax, ay, by)
            carve_h(by, ax, bx)

    for _ in range(d["DUN_PLACE_TRIES"]):
        if len(rooms) >= d["DUN_MAX_ROOMS"]:
            break
        w = rng.randint(d["DUN_ROOM_MIN"], d["DUN_ROOM_MAX"])
        h = rng.randint(d["DUN_ROOM_MIN"], d["DUN_ROOM_MAX"])
        x = 2 + rng.randrange(W - w - 4)
        y = 2 + rng.randrange(H - h - 4)
        if any(x < rx + rw + 2 and rx < x + w + 2
               and y < ry + rh + 2 and ry < y + h + 2 for rx, ry, rw, rh in rooms):
            continue
        rooms.append((x, y, w, h))
        for j in range(h):
            carve_h(y + j, x, x + w - 1)

    if not rooms:
        return g, rooms

    order = sorted(range(len(rooms)), key=lambda i: rooms[i][0] + rooms[i][2] // 2)
    for i in range(len(order) - 1):
        a, b = rooms[order[i]], rooms[order[i + 1]]
        carve_l(a[0] + a[2] // 2, a[1] + a[3] // 2,
                b[0] + b[2] // 2, b[1] + b[3] // 2, rng.getrandbits(1))
    if len(rooms) > 3:
        for _ in range(2):
            a, b = rooms[rng.randrange(len(rooms))], rooms[rng.randrange(len(rooms))]
            if a != b:
                carve_l(a[0] + a[2] // 2, a[1] + a[3] // 2,
                        b[0] + b[2] // 2, b[1] + b[3] // 2, rng.getrandbits(1))

    last = rooms[order[-1]]
    g[last[1] + last[3] // 2][last[0] + last[2] // 2] = DCELL_STAIRS
    return g, rooms


def grow_rock(g, d, single_pass=False):
    W, H = d["DUN_W"], d["DUN_H"]
    N8 = [(-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1)]

    def walkable(x, y):
        return 0 <= x < W and 0 <= y < H and g[y][x] in (DCELL_FLOOR, DCELL_STAIRS)

    def solid(x, y):
        return 0 <= x < W and 0 <= y < H and g[y][x] != DCELL_VOID

    for y in range(H):
        for x in range(W):
            if g[y][x] == DCELL_VOID and any(walkable(x + dx, y + dy) for dx, dy in N8):
                g[y][x] = DCELL_ROCK_A
    if single_pass:
        return
    for y in range(H):
        for x in range(W):
            if g[y][x] == DCELL_VOID and any(solid(x + dx, y + dy) for dx, dy in N8):
                g[y][x] = DCELL_ROCK_B


def tile_for_cell(g, d, x, y):
    W, H = d["DUN_W"], d["DUN_H"]
    T = d["TILES"]
    c = g[y][x]
    h = cell_hash(x, y)

    def walkable(xx, yy):
        return 0 <= xx < W and 0 <= yy < H and g[yy][xx] in (DCELL_FLOOR, DCELL_STAIRS)

    def solid(xx, yy):
        return 0 <= xx < W and 0 <= yy < H and g[yy][xx] != DCELL_VOID

    if c == DCELL_STAIRS:
        return T["STAIRS"]
    if c == DCELL_FLOOR:
        return T["FLOOR_LIT"] if h > d["LIT"] else T["FLOOR"]
    if c == DCELL_VOID:
        return T["VOID"]
    if walkable(x, y + 1):
        return T["WALL_FACE"]
    hi = not solid(x, y - 1)
    speck = h > d["SPECK"]
    if hi and speck:
        return T["WALL_HISPK"]
    if hi:
        return T["WALL_HI"]
    if speck:
        return T["WALL_SPECK"]
    return T["WALL_TOP"]


def floor_touches_void(g, d):
    W, H = d["DUN_W"], d["DUN_H"]
    N8 = [(-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1)]
    bad = 0
    for y in range(H):
        for x in range(W):
            if g[y][x] not in (DCELL_FLOOR, DCELL_STAIRS):
                continue
            for dx, dy in N8:
                nx, ny = x + dx, y + dy
                if 0 <= nx < W and 0 <= ny < H and g[ny][nx] == DCELL_VOID:
                    bad += 1
                    break
    return bad


def disconnected_floor(g, d):
    W, H = d["DUN_W"], d["DUN_H"]
    cells = {(x, y) for y in range(H) for x in range(W)
             if g[y][x] in (DCELL_FLOOR, DCELL_STAIRS)}
    if not cells:
        return 0
    start = next(iter(cells))
    seen = {start}
    q = deque([start])
    while q:
        x, y = q.popleft()
        for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            n = (x + dx, y + dy)
            if n in cells and n not in seen:
                seen.add(n)
                q.append(n)
    return len(cells) - len(seen)


def run(repo, single_pass=False, verbose=True):
    d = parse_defines(repo)
    seen_tiles = Counter()
    touching = 0
    orphaned = 0
    rng = random.Random(20260819)

    for _ in range(PLANS):
        g, rooms = generate_plan(d, rng)
        if not rooms:
            continue
        touching += floor_touches_void(g, d) if single_pass else 0
        orphaned += disconnected_floor(g, d)
        grow_rock(g, d, single_pass)
        if not single_pass:
            touching += floor_touches_void(g, d)
        for y in range(d["DUN_H"]):
            for x in range(d["DUN_W"]):
                seen_tiles[tile_for_cell(g, d, x, y)] += 1

    declared = set(d["TILES"].values())
    unused = sorted(declared - set(seen_tiles))
    stray = sorted(set(seen_tiles) - declared)

    ok = not unused and not stray and touching == 0 and orphaned == 0

    if verbose:
        print(f"  plans generated    : {PLANS}  ({d['DUN_W']}x{d['DUN_H']}, "
              f"up to {d['DUN_MAX_ROOMS']} rooms of {d['DUN_ROOM_MIN']}-{d['DUN_ROOM_MAX']})")
        print(f"  tile vocabulary    : {len(declared)} declared, {len(seen_tiles)} emitted")
        for tid in sorted(seen_tiles):
            name = TILE_NAMES[tid] if tid < len(TILE_NAMES) else "?"
            print(f"      {tid} {name:<10s} {seen_tiles[tid]:>8d}")
        if unused:
            print(f"  UNREACHABLE TILES  : {[TILE_NAMES[t] for t in unused]}"
                  f"  <-- declared, built, never emitted")
        if stray:
            print(f"  UNDECLARED TILES   : {stray}")
        if touching:
            print(f"  FLOOR TOUCHING VOID: {touching} cells  <-- floor with no wall edge")
        if orphaned:
            print(f"  DISCONNECTED FLOOR : {orphaned} cells unreachable from the rest")
    return ok


def render(repo, path):
    from PIL import Image

    d = parse_defines(repo)
    pal = {
        0: (0, 0, 0), 1: (72, 80, 88), 2: (48, 48, 56), 3: (32, 32, 40),
        4: (24, 24, 32), 5: (16, 16, 24), 6: (8, 8, 16),
        7: (168, 144, 104), 8: (128, 112, 80), 9: (88, 72, 56),
        10: (248, 224, 128), 11: (120, 96, 168),
    }
    # Reproduce each tile's 8x8 pixels the way BuildDungeonTiles writes them.
    def tile_px(t):
        px = [[0] * 8 for _ in range(8)]
        if t == 1 or t == 2:
            base = 8 if t == 1 else 7
            for y in range(8):
                for x in range(8):
                    px[y][x] = 9 if (y == 0 or x == 0) else base
        elif t == 3:
            for y in range(8):
                for x in range(8):
                    px[y][x] = 8
            for y in range(1, 7):
                for x in range(1, 7):
                    px[y][x] = 10
            for y in range(2, 6):
                for x in range(2, 6):
                    px[y][x] = 11
        elif t == 4:
            for y in range(8):
                for x in range(8):
                    px[y][x] = 3 if y < 4 else (6 if y == 7 else 5)
        elif t in (5, 6, 7, 8):
            for y in range(8):
                for x in range(8):
                    px[y][x] = 2
            if t in (6, 8):
                for x in range(8):
                    px[0][x] = 1
            if t in (7, 8):
                for y in range(3, 5):
                    for x in range(3, 5):
                        px[y][x] = 3
        return px

    rng = random.Random(20260819)
    g, _ = generate_plan(d, rng)
    grow_rock(g, d)
    W, H = d["DUN_W"], d["DUN_H"]
    img = Image.new("RGB", (W * 8, H * 8), (10, 9, 14))
    p = img.load()
    for cy in range(H):
        for cx in range(W):
            px = tile_px(tile_for_cell(g, d, cx, cy))
            for y in range(8):
                for x in range(8):
                    v = px[y][x]
                    if v:
                        p[cx * 8 + x, cy * 8 + y] = pal[v]
    img.resize((W * 8 * 2, H * 8 * 2), Image.NEAREST).save(path)
    print(f"wrote {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo_pos", nargs="?", default=None)
    ap.add_argument("--repo", default=None)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--render", default=None, help="write a preview PNG (needs Pillow)")
    args = ap.parse_args()

    repo = args.repo or args.repo_pos or os.environ.get("POKEDECOMP_REPO") or "."

    print("check_dungeon_plane")
    ok = run(repo)

    if args.selftest:
        print("\n  --selftest: growing rock in ONE pass instead of two")
        if run(repo, single_pass=True, verbose=False):
            print("  SELFTEST FAILED: a one-ring plan passed")
            ok = False
        else:
            print("  selftest ok: the check fires when rock does not enclose the floor")

    if args.render:
        render(repo, args.render)

    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
