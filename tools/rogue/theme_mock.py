"""
Render a mock dungeon floor for a candidate theme table.

Mirrors the wall dispatch in PaintWalls() exactly, so what comes out here is
what the generator will paint. Use it to validate a new theme's metatile table
before writing any C - a wrong slot is obvious on sight and invisible in a diff.

Usage:  python theme_mock.py newmauville
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

import tileset_atlas as ta
from tileset_resolve import TilesetResolver

REPO = Path(__file__).resolve().parents[2]

W, H = 40, 32
SLOTS = ('INTERIOR_LEFT', 'INTERIOR_MID', 'INTERIOR_RIGHT',
         'FACE_LEFT', 'FACE_MID', 'FACE_RIGHT',
         'NORTH_LEFT', 'NORTH_MID', 'NORTH_RIGHT',
         'CORNER_NW', 'CORNER_NE', 'CORNER_SOUTH',
         'SLIVER_VERT', 'SLIVER_HORZ', 'SLIVER_VERT_TOP', 'SLIVER_VERT_BOT',
         'SLIVER_HORZ_L', 'SLIVER_HORZ_R', 'SLIVER_ISOLATED')

THEMES = {
    'newmauville': dict(
        primary='gTileset_General', secondary='gTileset_BikeShop',
        floor=0x210, stairs=0x0AF,
        skirts={0x227: (0x22F, 0), 0x294: (0x27F, 0), 0x293: (0x22F, 0x27D),
                0x270: (0, 0x27D), 0x295: (0, 0x27D)},
        shadow_corner=0x275,
        decor=[(0x227, 0x277, 0), (0x227, 0x2B4, 0), (0x227, 0x2A1, 0x2A2)],
        decor_rarity=12,
        wall={
            'INTERIOR_LEFT': 0x272, 'INTERIOR_MID': 0x208, 'INTERIOR_RIGHT': 0x270,
            'FACE_LEFT': 0x294, 'FACE_MID': 0x227, 'FACE_RIGHT': 0x293,
            'NORTH_LEFT': 0x296, 'NORTH_MID': 0x227, 'NORTH_RIGHT': 0x295,
            'CORNER_NW': 0x208, 'CORNER_NE': 0x208, 'CORNER_SOUTH': 0x208,
            'SLIVER_VERT': 0x290, 'SLIVER_HORZ': 0x227,
            'SLIVER_VERT_TOP': 0x288, 'SLIVER_VERT_BOT': 0x298,
            'SLIVER_HORZ_L': 0x227, 'SLIVER_HORZ_R': 0x227,
            'SLIVER_ISOLATED': 0x290,
        }),
    'fierypath': dict(
        primary='gTileset_General', secondary='gTileset_Lavaridge',
        floor=0x308, stairs=0x0A7,
        # Only a north shadow exists in this tileset; the red rock is matte.
        skirts={0x30C: (0x269, 0)},
        decor=[(0x308, 0x310, 0), (0x308, 0x311, 0),   # ember sparkle floors
               (0x271, 0x268, 0), (0x271, 0x26A, 0),   # embedded rocks
               (0x271, 0x30D, 0)],                     # boulder
        decor_rarity=12,
        wall={
            'INTERIOR_LEFT': 0x306, 'INTERIOR_MID': 0x271, 'INTERIOR_RIGHT': 0x307,
            'FACE_LEFT': 0x30E, 'FACE_MID': 0x274, 'FACE_RIGHT': 0x30F,
            'NORTH_LEFT': 0x30A, 'NORTH_MID': 0x30C, 'NORTH_RIGHT': 0x30B,
            'CORNER_NW': 0x27B, 'CORNER_NE': 0x27C, 'CORNER_SOUTH': 0x27E,
            'SLIVER_VERT': 0x3B9, 'SLIVER_HORZ': 0x30C,
            'SLIVER_VERT_TOP': 0x3BA, 'SLIVER_VERT_BOT': 0x3BB,
            'SLIVER_HORZ_L': 0x3BC, 'SLIVER_HORZ_R': 0x3BD,
            'SLIVER_ISOLATED': 0x3BE,
        }),
    'cave': dict(
        primary='gTileset_General', secondary='gTileset_Cave',
        floor=0x21C, stairs=0x214,
        wall={
            'INTERIOR_LEFT': 0x210, 'INTERIOR_MID': 0x211, 'INTERIOR_RIGHT': 0x212,
            'FACE_LEFT': 0x218, 'FACE_MID': 0x219, 'FACE_RIGHT': 0x21A,
            'NORTH_LEFT': 0x220, 'NORTH_MID': 0x221, 'NORTH_RIGHT': 0x222,
            'CORNER_NW': 0x213, 'CORNER_NE': 0x214, 'CORNER_SOUTH': 0x211,
            'SLIVER_VERT': 0x39E, 'SLIVER_HORZ': 0x39F,
            'SLIVER_VERT_TOP': 0x3A0, 'SLIVER_VERT_BOT': 0x3A1,
            'SLIVER_HORZ_L': 0x3A2, 'SLIVER_HORZ_R': 0x3A3,
            'SLIVER_ISOLATED': 0x3A4,
        }),
}


class Rng:
    """The generator's local LCG, so mock layouts look like real ones."""

    def __init__(self, seed):
        self.s = seed & 0xFFFF

    def next(self):
        self.s = (self.s * 1103515245 + 24691) & 0xFFFFFFFF
        return (self.s >> 16) & 0x7FFF


def carve(seed):
    """Rooms with one block of padding, joined by L corridors."""
    rng = Rng(seed)
    solid = [[True] * W for _ in range(H)]
    rooms = []
    for _ in range(80):
        if len(rooms) >= 9:
            break
        w = 4 + rng.next() % 6
        h = 3 + rng.next() % 4
        x = 1 + rng.next() % (W - w - 2)
        y = 1 + rng.next() % (H - h - 2)
        if any(not (x + w + 1 < b[0] or b[0] + b[2] + 1 < x or
                    y + h + 1 < b[1] or b[1] + b[3] + 1 < y) for b in rooms):
            continue
        rooms.append((x, y, w, h))
        for dy in range(h):
            for dx in range(w):
                solid[y + dy][x + dx] = False
    for a, b in zip(rooms, rooms[1:]):
        ax, ay = a[0] + a[2] // 2, a[1] + a[3] // 2
        bx, by = b[0] + b[2] // 2, b[1] + b[3] // 2
        for x in range(min(ax, bx), max(ax, bx) + 1):
            solid[ay][x] = False
        for y in range(min(ay, by), max(ay, by) + 1):
            solid[y][bx] = False
    return solid, rooms


def decor_hash(seed, x, y):
    """The same hash as DecorHash() in src/rogue_dungeon.c."""
    h = (seed * 2654435761) & 0xFFFFFFFF
    h ^= ((x + 1) * 40503) & 0xFFFFFFFF
    h ^= ((y + 1) * 24593) & 0xFFFFFFFF
    h ^= h >> 13
    h = (h * 2246822519) & 0xFFFFFFFF
    h ^= h >> 15
    return h & 0xFFFF


def paint(solid, theme, seed=0):
    """A direct transcription of PaintWalls() in src/rogue_dungeon.c."""
    wall = theme['wall']
    out = [[theme['floor']] * W for _ in range(H)]
    used = {}

    def is_wall(x, y):
        return True if not (0 <= x < W and 0 <= y < H) else solid[y][x]

    for y in range(H):
        for x in range(W):
            if not is_wall(x, y):
                continue
            n, s = not is_wall(x, y - 1), not is_wall(x, y + 1)
            w, e = not is_wall(x - 1, y), not is_wall(x + 1, y)
            if n and s and w and e:
                slot = 'SLIVER_ISOLATED'
            elif n and s:
                slot = 'SLIVER_HORZ_L' if w else 'SLIVER_HORZ_R' if e else 'SLIVER_HORZ'
            elif w and e:
                slot = ('SLIVER_VERT_TOP' if n else
                        'SLIVER_VERT_BOT' if s else 'SLIVER_VERT')
            elif s:
                slot = 'FACE_LEFT' if w else 'FACE_RIGHT' if e else 'FACE_MID'
            elif n:
                slot = 'NORTH_LEFT' if w else 'NORTH_RIGHT' if e else 'NORTH_MID'
            elif w:
                slot = 'INTERIOR_LEFT'
            elif e:
                slot = 'INTERIOR_RIGHT'
            elif not is_wall(x + 1, y + 1):
                slot = 'CORNER_NW'
            elif not is_wall(x - 1, y + 1):
                slot = 'CORNER_NE'
            elif not is_wall(x - 1, y - 1) or not is_wall(x + 1, y - 1):
                slot = 'CORNER_SOUTH'
            else:
                slot = 'INTERIOR_MID'
            out[y][x] = wall[slot]
            used[slot] = used.get(slot, 0) + 1

    # Wall skirts, exactly as ApplySkirts: keyed to the specific wall metatile
    # north or west, deterministic.
    if theme.get('skirts'):
        sk = theme['skirts']
        corner = theme.get('shadow_corner', 0)

        def skirt(x, y, east):
            if not is_wall(x, y):
                return 0
            entry = sk.get(out[y][x])
            return entry[1 if east else 0] if entry else 0

        for y in range(H):
            for x in range(W):
                if is_wall(x, y):
                    continue
                s_ = skirt(x, y - 1, False)
                e_ = skirt(x - 1, y, True)
                if s_ and e_:
                    m = corner or s_
                elif s_ or e_:
                    m = s_ or e_
                elif (corner and not is_wall(x, y - 1) and not is_wall(x - 1, y)
                      and skirt(x - 1, y - 1, False)):
                    m = corner
                else:
                    m = 0
                if m:
                    out[y][x] = m

    # Cosmetic wall swaps, position-hashed exactly as ApplyWallDecor does.
    if theme.get('decor') and theme.get('decor_rarity'):
        for y in range(H):
            for x in range(W):
                if not is_wall(x, y):
                    continue
                hsh = decor_hash(seed, x, y)
                if hsh % theme['decor_rarity']:
                    continue
                hits = [(v, ve) for b, v, ve in theme['decor']
                        if b == out[y][x]
                        and (ve == 0 or (x + 1 < W and out[y][x + 1] == b))]
                if hits:
                    v, ve = hits[(hsh >> 8) % len(hits)]
                    out[y][x] = v
                    if ve:
                        out[y][x + 1] = ve
    return out, used


def main(name):
    theme = THEMES[name]
    R = TilesetResolver(REPO)
    pair = ta.TilesetPair(ta.Tileset(R.resolve(theme['primary'])),
                          ta.Tileset(R.resolve(theme['secondary'])))
    cache = {}

    def tile(mid):
        if mid not in cache:
            cache[mid] = ta.render_metatile(pair, mid, 1)
        return cache[mid]

    panels, totals = [], {}
    for seed in (11, 29):
        solid, rooms = carve(seed)
        grid, used = paint(solid, theme, seed)
        if theme.get('stairs') and rooms:          # as PrepareFloor places it
            rx, ry, rw, rh = rooms[-1]
            grid[ry + rh // 2][rx + rw // 2] = theme['stairs']
        for k, v in used.items():
            totals[k] = totals.get(k, 0) + v
        im = Image.new('RGB', (W * 16, H * 16))
        for y in range(H):
            for x in range(W):
                im.paste(tile(grid[y][x]), (x * 16, y * 16))
        panels.append(im)

    out = Image.new('RGB', (panels[0].width * 2 + 24, panels[0].height + 16),
                    (24, 24, 30))
    out.paste(panels[0], (8, 8))
    out.paste(panels[1], (16 + panels[0].width, 8))
    out = out.resize((out.width * 2, out.height * 2), Image.NEAREST)
    path = ta.OUTDIR / f'mock_{name}.png'
    out.save(path)
    print(f'wrote {path}')
    print('\nslot usage across both floors:')
    for s in SLOTS:
        n = totals.get(s, 0)
        mark = '' if n else '   <-- never hit, unvalidated'
        print(f'  {s:<18} {n:>5}  0x{theme["wall"][s]:03X}{mark}')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'newmauville')
