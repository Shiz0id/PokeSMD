"""Autumn grass, tall grass and flower bush for the woods, in Rustboro.

WHY IT CANNOT BE DONE IN PLACE. WOODS_METATILE_GRASS is 0x001 in gTileset_
GENERAL, the primary tileset most of Hoenn shares. Recolouring it there repaints
Route 101. So the woods gets its own grass metatiles in the secondary, and the
theme's .floor / .tallGrass point at them - a table change, which is the shape
this project wants.

NO NEW ART, AND NONE NEEDED. A tile stores palette INDICES; the colours live in
the palette. So this copies General's grass tiles across byte for byte and
writes a rotated copy of General's palette 2 into Rustboro's palette 12. Six
tiles, one palette, zero pixels drawn.

THE ROTATION IS -65 DEGREES, and that is a taste call rather than the computed
one. Measured against vanilla's own tree-on-grass reading (-45.7 luminance,
-51.1 hue), the autumn trees were already correct in VALUE (-47.5) and wrong
only in HUE (-104.3); -53 lands the hue gap at -51.2, within 0.1 of vanilla.
-65 overshoots to -38.1 and also lifts the grass, taking the value gap to -54.1.
So it is not vanilla's relationship - it is a brighter, yellower floor with more
contrast under the trees. Chosen deliberately; the numbers are here so the next
person knows it was a choice and not a miss.

TALL GRASS KEEPS ITS OWN ATTRIBUTE, copied from 0x00D rather than synthesised.
That attribute carries MB_TALL_GRASS, which is what makes wild encounters fire
at all. Getting it wrong is silent until nothing spawns.

Neither grass nor tall grass is animated - General animates flowers and water
only - so there is no animation plumbing to follow.

Usage:  imported by append_rustboro.py; run directly for a report.
"""
import colorsys
import struct
import sys
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
GENERAL = REPO / 'data/tilesets/primary/general'
RUSTBORO = REPO / 'data/tilesets/secondary/rustboro'

NUM_TILES_IN_PRIMARY = 512
PALETTE = 12                 # the last free Rustboro slot; 6 is the trees
SRC_PALETTE = 2              # General's grass palette
ROTATE_DEG = -65

GRASS_MT, TALL_MT, BUSH_MT = 0x001, 0x00D, 0x004
GRASS_TILES = (0x002, 0x003)
TALL_TILES = (0x010, 0x011, 0x020, 0x021)
BUSH_TILES = (0x1FC, 0x1FD, 0x1FE, 0x1FF)   # top layer; bottom is GRASS_TILES

# ONLY THE MINT FAMILY IS ROTATED, and this is the whole fix for the maroon.
# General's palette 2 holds TWO greens: 12-15 are the mint grass (hue ~155) and
# 2,3,4 are a darker yellow-olive (hue 78-115) already sitting where the autumn
# floor wants to be. Rotating the lot dragged the olives 65 degrees out of the
# family - index 4, the tall-grass blade shadow, landed on #521000, a dark
# maroon, 71px of red in a yellow-green floor.
#
# The flowers (9 peach, 11 red) are left alone for the same reason: they are not
# grass and were never the thing that needed moving. Leaving them is also what
# makes the bush cost nothing beyond its four tiles.
ROTATE_INDICES = {12, 13, 14, 15}
TILES_PER_ROW = 16


def _rot(rgb):
    r, g, b = (v / 255 for v in rgb)
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    h = (h + ROTATE_DEG / 360.0) % 1.0
    nr, ng, nb = colorsys.hls_to_rgb(h, l, s)
    # 5 bits per channel, as the hardware stores it.
    return tuple((int(v * 255) >> 3) * 255 // 31 for v in (nr, ng, nb))


def palette():
    """General's palette SRC_PALETTE, rotated. Entry 0 is left alone."""
    txt = (GENERAL / 'palettes' / f'{SRC_PALETTE:02d}.pal').read_text().split('\n')
    cols = []
    for line in txt[3:3 + 16]:
        parts = line.split()
        if len(parts) == 3:
            cols.append(tuple(int(v) for v in parts))
    # FIFTEEN, NOT SIXTEEN. append_rustboro's write_palette supplies entry 0
    # itself - the transparent one, never drawn - so returning it here shifts
    # every colour down a slot. That shipped for one run and put MAGENTA in the
    # grass: index 13's pixels read index 12's colour, and an unrelated pink
    # from elsewhere in General's palette 2 landed where 13 should have been.
    # The give-away was the rendered grass being 22 luminance brighter than a
    # lightness-preserving rotation can make it.
    return [_rot(c) if i in ROTATE_INDICES else c
            for i, c in enumerate(cols) if i > 0]


def _tile_px(img, tile):
    a = img.load()
    ox, oy = (tile % TILES_PER_ROW) * 8, (tile // TILES_PER_ROW) * 8
    return bytes(((a[ox + i, oy + j] & 0xF) | ((a[ox + i + 1, oy + j] & 0xF) << 4))
                 for j in range(8) for i in range(0, 8, 2))


def _slots(taken):
    """Free Rustboro tiles, skipping any the caller has already handed out."""
    mt = (RUSTBORO / 'metatiles.bin').read_bytes()[:350 * 16]
    used = set()
    for i in range(0, len(mt), 2):
        t = struct.unpack('<H', mt[i:i + 2])[0] & 0x3FF
        if t >= NUM_TILES_IN_PRIMARY:
            used.add(t - NUM_TILES_IN_PRIMARY)
    return [s for s in range(1, 512)
            if s not in used and s not in taken and s not in (508, 509, 510, 511)]


def build(taken=()):
    """(tile writes, metatile entries, palette) for append_rustboro."""
    src = Image.open(GENERAL / 'tiles.png')
    order = list(GRASS_TILES) + list(TALL_TILES) + list(BUSH_TILES)
    slots = _slots(set(taken))[:len(order)]
    remap = {}
    writes = {}
    for k, t in enumerate(order):
        writes[slots[k]] = _tile_px(src, t)
        remap[t] = slots[k]

    gmt = (GENERAL / 'metatiles.bin').read_bytes()
    mts = []
    for name, m in (('grass', GRASS_MT), ('tallgrass', TALL_MT),
                    ('flowerbush', BUSH_MT)):
        e = list(struct.unpack('<8H', gmt[m * 16:m * 16 + 16]))
        for i in range(8):
            tile = e[i] & 0x3FF
            if tile in remap:
                # keep the flip bits, swap tile id and palette
                e[i] = (e[i] & 0x0C00) | (NUM_TILES_IN_PRIMARY + remap[tile]) \
                       | (PALETTE << 12)
            elif e[i] == 0:
                pass
            else:
                raise SystemExit(f'{name} references unexpected tile 0x{tile:03X}')
        mts.append((name, struct.pack('<8H', *e), m))
    return writes, mts, (PALETTE, palette())


def grass_bottom_entries():
    """The four bottom-layer entries a tree metatile should sit on.

    make_woods_trees.py imports this so a tree's own bottom layer is the SAME
    grass the floor uses. Left pointing at the primary grass, every tree stands
    on a rectangle of the old mint colour - which is exactly what the candidate
    render showed.
    """
    _, mts, _ = build()
    return list(struct.unpack('<8H', mts[0][1]))[:4]


if __name__ == '__main__':
    writes, mts, (slot, cols) = build()
    print(f'{len(writes)} tiles -> Rustboro slots {sorted(writes)}')
    print(f'{len(mts)} metatiles: {[m[0] for m in mts]}')
    print(f'palette {slot}, rotated {ROTATE_DEG} deg from General {SRC_PALETTE}')
    for i, c in enumerate(cols):
        print(f'  {i:2d} {c[0]:02X}{c[1]:02X}{c[2]:02X}')
