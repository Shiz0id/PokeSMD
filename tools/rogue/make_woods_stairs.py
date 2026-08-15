"""
Draw a grassy stairs metatile into the Rustboro secondary tileset.

The woods theme was descending through metatile 0x0A7, the dark interior of the
vanilla cave mouth. Placed on its own it is a dark rectangle sitting in grass,
because the sandy surround that frames it (0x0A5/0x0A6) never gets painted.

This composes a proper one: three earth steps in vanilla's stair rhythm - tread,
bright front edge, riser in shadow - inside a rounded opening whose corners are
grass, so it seats into the turf instead of butting against it.

Everything is drawn in ONE palette that carries both the greens and a full
earth ramp, so the art needs no palette slot of its own. That palette used to be
General's 2; it is now Rustboro's 12, the autumn grass - see PALETTE below.

The grass is baked into the tiles rather than left to a bottom layer showing
through a transparent top layer. A top layer would be drawn OVER the player
(METATILE_LAYER_TYPE_NORMAL), which would hide them when they stand on it.

Idempotent: re-running overwrites the same tile slots and re-appends the same
metatile rather than stacking another copy.

Run:  python3 tools/rogue/make_woods_stairs.py
"""
import struct
import sys
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
RUSTBORO = REPO / 'data/tilesets/secondary/rustboro'
GENERAL = REPO / 'data/tilesets/primary/general'

NUM_TILES_IN_PRIMARY = 512
RUSTBORO_BASE_METATILES = 350        # vanilla count, ids 0x200-0x35D
GRASS_METATILE = 0x001               # the woods floor, in the primary tileset
# PALETTE 12, THE AUTUMN ONE, NOT GENERAL'S 2. The stairs are drawn ONTO the
# woods floor - the '.' cells in ART below keep the grass pixel underneath - so
# while this pointed at palette 2 the four corners of the stairs stayed mint
# after the floor around them went autumn. Found on hardware.
#
# NO ART CHANGE WAS NEEDED, and that is not luck. make_woods_grass rotates only
# indices 12-15, the mint family; every index this drawing uses for the stairs
# themselves (3, 4, 5, 8, 9) is in the untouched set and comes through
# identical. Only the grass pixels move, which is exactly the intent.
PALETTE = 12

# The last four tiles of the Rustboro sheet: blank, and referenced by no
# metatile in any pair that uses this tileset.
TILE_SLOTS = (508, 509, 510, 511)
NEW_METATILE = 0x200 + RUSTBORO_BASE_METATILES   # 0x35E

# palette 2:
#   3 398B31 dark green   4 395200 darkest olive   5 DE9473 tan
#   8 413931 near black   9 FFC594 pale tan
# '.' keeps the grass pixel underneath.
ART = (
    '..344444444443..',
    '.34888888888843.',
    '4888888888888884',
    '4885555555555884',
    '4885555555555884',
    '4889999999999884',
    '4888888888888884',
    '4885555555555884',
    '4885555555555884',
    '4889999999999884',
    '4888888888888884',
    '4885555555555884',
    '4885555555555884',
    '4889999999999884',
    '.34888888888843.',
    '..344444444443..',
)


def read_metatile(path, local_index):
    raw = path.read_bytes()
    off = local_index * 16
    return list(struct.unpack('<8H', raw[off:off + 16]))


def grass_pixels(tiles_img, entries):
    """The 16x16 of the grass metatile, as palette indices."""
    px = tiles_img.load()
    out = [[0] * 16 for _ in range(16)]
    for k in range(4):
        v = entries[k]
        tile, xf, yf = v & 0x3FF, v & 0x400, v & 0x800
        if tile >= NUM_TILES_IN_PRIMARY:
            raise SystemExit('grass metatile unexpectedly uses secondary tiles')
        tx, ty = (tile % 16) * 8, (tile // 16) * 8
        ox, oy = (k % 2) * 8, (k // 2) * 8
        for y in range(8):
            for x in range(8):
                sx, sy = (7 - x if xf else x), (7 - y if yf else y)
                out[oy + y][ox + x] = px[tx + sx, ty + sy]
    return out


def main():
    general_tiles = Image.open(GENERAL / 'tiles.png')
    rustboro_tiles = Image.open(RUSTBORO / 'tiles.png')
    if rustboro_tiles.mode != 'P' or general_tiles.mode != 'P':
        raise SystemExit('expected indexed tile sheets')

    grass = read_metatile(GENERAL / 'metatiles.bin', GRASS_METATILE)
    if any(grass[4:]):
        raise SystemExit('grass metatile has a top layer; art assumes it does not')
    canvas = grass_pixels(general_tiles, grass)

    for y, row in enumerate(ART):
        if len(row) != 16:
            raise SystemExit(f'art row {y} is {len(row)} px, expected 16')
        for x, c in enumerate(row):
            if c != '.':
                canvas[y][x] = int(c, 16)

    # blit the four quadrants into the chosen Rustboro slots
    dst = rustboro_tiles.load()
    for k, slot in enumerate(TILE_SLOTS):
        tx, ty = (slot % 16) * 8, (slot // 16) * 8
        ox, oy = (k % 2) * 8, (k // 2) * 8
        for y in range(8):
            for x in range(8):
                dst[tx + x, ty + y] = canvas[oy + y][ox + x]
    rustboro_tiles.save(RUSTBORO / 'tiles.png', bits=4, optimize=False)

    # one metatile: bottom layer only, all four new tiles, grass palette
    entries = [(NUM_TILES_IN_PRIMARY + s) | (PALETTE << 12) for s in TILE_SLOTS]
    entries += [0, 0, 0, 0]
    blob = struct.pack('<8H', *entries)

    # behaviour and layer type copied from the grass it replaces, so it walks
    # and draws exactly like ordinary floor. The stairs trigger is by metatile
    # id, not by behaviour, so nothing special is needed here.
    attr = struct.unpack('<H', (GENERAL / 'metatile_attributes.bin')
                         .read_bytes()[GRASS_METATILE * 2:][:2])[0]

    mt_path, at_path = RUSTBORO / 'metatiles.bin', RUSTBORO / 'metatile_attributes.bin'
    mt, at = bytearray(mt_path.read_bytes()), bytearray(at_path.read_bytes())
    count = len(mt) // 16
    if count < RUSTBORO_BASE_METATILES:
        raise SystemExit(f'unexpected Rustboro metatile count {count}')
    if count > RUSTBORO_BASE_METATILES:
        del mt[RUSTBORO_BASE_METATILES * 16:]
        del at[RUSTBORO_BASE_METATILES * 2:]
    mt += blob
    at += struct.pack('<H', attr)
    mt_path.write_bytes(bytes(mt))
    at_path.write_bytes(bytes(at))

    print(f'tiles   -> Rustboro local {TILE_SLOTS} '
          f'(global 0x{NUM_TILES_IN_PRIMARY + TILE_SLOTS[0]:03X}-'
          f'0x{NUM_TILES_IN_PRIMARY + TILE_SLOTS[-1]:03X})')
    print(f'metatile-> 0x{NEW_METATILE:03X}  attr 0x{attr:04X} '
          f'(behavior {attr & 0xFF}, layer {(attr >> 12) & 0xF})')
    print(f'         {len(mt) // 16} Rustboro metatiles, '
          f'{0x400 - 0x200 - len(mt) // 16} slots still free')
    print(f'\nset WOODS_METATILE_STAIRS to 0x{NEW_METATILE:03X}')


if __name__ == '__main__':
    sys.exit(main())
