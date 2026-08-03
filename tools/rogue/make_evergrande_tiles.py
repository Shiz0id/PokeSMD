"""
Everything the flower dungeon adds to gTileset_EverGrande.

THIS IS THE ONLY THING THAT MAY APPEND TO THIS TILESET. It stays idempotent by
truncating the metatile and attribute files back to the vanilla count and
rewriting the whole tail, so a second appender's entries would be silently wiped
the next time this one runs. Anything else needing a new Ever Grande metatile
adds itself here. That rule is the cave's, learned the hard way - see
docs/ROGUELIKE.md section 5.

Two things go in:

  1. THE EXIT (one metatile). Ever Grande has no staircase art - what looks like
     the League's steps is a curved entrance terrace, five metatiles wide and not
     tileable. So this draws one, the way make_woods_stairs.py did: a rounded
     dark frame with three treads inside, which is the shape the player has
     already learned means "down" in this project.

     It is far cheaper than the woods stairs were, because Ever Grande's cobble
     turns out to be pure overlay work - 0x232 is General's plain grass on the
     bottom layer with a palette-8 stone overlay on top. So the exit is the mint
     floor's own bottom layer plus four new top-layer tiles, and the rounded
     corners come free: index 0 in a top layer is transparent, so the grass
     shows through without a single pixel of grass being drawn.

     The attribute is COVERED, not NORMAL. Under NORMAL a top layer draws OVER
     the player, which would hide them while they stand on the exit. COVERED
     puts both layers below them. This is the whirlpool's reasoning exactly.

  2. THE ENCOUNTER FLOWERS (sixteen metatiles, no new art at all). Neither
     gTileset_EverGrande nor gTileset_Mauville contains a single metatile
     carrying TILE_FLAG_HAS_ENCOUNTERS - every flower in both is MB_NORMAL - so
     a flower the player can find wild Pokemon in has to be a new entry. These
     reference the vanilla flower tiles byte for byte and differ only in the
     behaviour, which becomes MB_LONG_GRASS.

     Attributes are PER ENTRY. The exit copies 0x27C's and must stay walkable;
     the flowers must each carry MB_LONG_GRASS or the dungeon has no wild
     Pokemon at all. One shared attribute is the same shape of bug as a
     hard-coded per-theme constant.

Run this, then rebuild. --preview needs Pillow; so does --write, since it edits
a PNG, so both run from Windows against //wsl.localhost/... - see the README.
"""
import re
import struct
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tileset_resolve import TilesetResolver

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / 'tools/rogue/_out'

# Vanilla gTileset_EverGrande. Everything at or past this index is ours, and the
# writer truncates to it before appending, which is what keeps this idempotent.
VANILLA_METATILES = 168
BASE = 0x200 + VANILLA_METATILES        # 0x2A8, the first id we own

METATILE_STAIRS = BASE                  # 0x2A8
METATILE_FLOWER_BASE = BASE + 1         # 0x2A9 .. 0x2B8, the sixteen

# The vanilla flower sets these copy. 0x288-0x28F are pink and orange, 0x290-
# 0x297 yellow and blue, and each set is EIGHT PHASES OF A DIAGONAL BANDING
# rather than eight interchangeable variants: 85% of Ever Grande City's 341
# flower blocks satisfy variant = (y - x + k) mod 8 for one of three per-field
# offsets. The generator paints that, so the order here matters and must stay
# ascending.
FLOWER_SOURCES = tuple(range(0x288, 0x298))

# The four tile slots the exit's overlay is written into. MEASURED ONCE, THEN
# PINNED - writing the art fills the slots, so a script that re-measures on
# every call gets a different answer the second time and writes metatile entries
# pointing at blank tiles. That is exactly how Glacia's snow floor rendered as
# palette index 0. Validated below against the tiles VANILLA references, never
# against the current sheet, or the check is circular.
STAIRS_TILES = (0x01C, 0x01D, 0x01E, 0x01F)

# gTileset_EverGrande palette 8, the cobble's. It carries a cream-to-olive stone
# ramp at 11-14 and a grey-to-navy shadow ramp at 5-8, which is everything a
# stone stairwell needs. Index 0 is transparent in a top layer.
P_CLEAR, P_FRAME, P_WELL = 0x0, 0x7, 0x8      # -, 414A6A slate, 29315A navy
P_TREAD, P_TREAD_SHADE = 0xC, 0xD             # DEDEC5 cream, C5C5A4 sage
STAIRS_PALETTE = 8

_ = P_CLEAR
F, W, T, D = P_FRAME, P_WELL, P_TREAD, P_TREAD_SHADE

# 16x16, drawn as one picture and cut into quadrants below. Three treads in a
# rounded frame - the woods stairs' shape, in Ever Grande's stone - because the
# exit is the one block on a floor the player is hunting for, and teaching them
# two different shapes for it across fourteen dungeons would be a waste of the
# lesson. The corners are transparent so the mint floor rounds them.
STAIRS_ART = [
    [_, _, F, F, F, F, F, F, F, F, F, F, F, F, _, _],
    [_, F, F, W, W, W, W, W, W, W, W, W, W, F, F, _],
    [F, F, W, W, W, W, W, W, W, W, W, W, W, W, F, F],
    [F, W, W, T, T, T, T, T, T, T, T, T, T, W, W, F],
    [F, W, W, T, T, T, T, T, T, T, T, T, T, W, W, F],
    [F, W, W, D, D, D, D, D, D, D, D, D, D, W, W, F],
    [F, W, W, W, W, W, W, W, W, W, W, W, W, W, W, F],
    [F, W, W, T, T, T, T, T, T, T, T, T, T, W, W, F],
    [F, W, W, T, T, T, T, T, T, T, T, T, T, W, W, F],
    [F, W, W, D, D, D, D, D, D, D, D, D, D, W, W, F],
    [F, W, W, W, W, W, W, W, W, W, W, W, W, W, W, F],
    [F, W, W, T, T, T, T, T, T, T, T, T, T, W, W, F],
    [F, W, W, T, T, T, T, T, T, T, T, T, T, W, W, F],
    [F, F, W, D, D, D, D, D, D, D, D, D, D, W, F, F],
    [_, F, F, F, F, F, F, F, F, F, F, F, F, F, F, _],
    [_, _, F, F, F, F, F, F, F, F, F, F, F, F, _, _],
]


def resolver():
    return TilesetResolver(REPO)


def read_metatile(mt: bytes, mid: int):
    return struct.unpack_from('<8H', mt, (mid - 0x200) * 16)


def entry(tile, palette, xflip=False, yflip=False):
    return (tile & 0x3FF) | (0x400 if xflip else 0) | (0x800 if yflip else 0) \
        | ((palette & 0xF) << 12)


def quadrants(art):
    """16x16 -> four 8x8 tiles in TL, TR, BL, BR order, which is the order a
    metatile references them in."""
    out = []
    for qy in (0, 8):
        for qx in (0, 8):
            out.append([row[qx:qx + 8] for row in art[qy:qy + 8]])
    return [out[0], out[1], out[2], out[3]]


def vanilla_referenced_tiles(mt: bytes) -> set:
    """Secondary tile indices the VANILLA metatiles use. Deliberately ignores
    anything we appended - validating the pinned slots against the current file
    would pass trivially once we have written to them."""
    used = set()
    for i in range(VANILLA_METATILES):
        for v in struct.unpack_from('<8H', mt, i * 16):
            if v:
                t = v & 0x3FF
                if t >= 512:
                    used.add(t - 512)
    return used


def behavior_value(name: str) -> int:
    txt = (REPO / 'include/constants/metatile_behaviors.h').read_text(encoding='utf-8')
    val = 0
    for m in re.finditer(r'^\s*(MB_[A-Z0-9_]+)\s*(?:=\s*(0x[0-9A-Fa-f]+|\d+))?\s*,',
                         txt, re.M):
        if m.group(2):
            val = int(m.group(2), 0)
        if m.group(1) == name:
            return val
        val += 1
    raise SystemExit(f'no such behaviour {name}')


def build_entries(res):
    """-> [(metatile_id, eight u16 entries, attribute)] in append order."""
    mt = res['metatiles'].read_bytes()
    at = res['attributes'].read_bytes()
    rows = []

    # The exit: the mint floor's bottom layer, our stone on top, COVERED.
    floor = read_metatile(mt, 0x27C)
    top = [entry(512 + t, STAIRS_PALETTE) for t in STAIRS_TILES]
    stairs_attr = (struct.unpack_from('<H', at, (0x27C - 0x200) * 2)[0] & 0x0FFF) \
        | (1 << 12)                                  # layer type COVERED
    rows.append((METATILE_STAIRS, list(floor[:4]) + top, stairs_attr))

    # The flowers: vanilla's pixels, our behaviour.
    long_grass = behavior_value('MB_LONG_GRASS')
    for n, src in enumerate(FLOWER_SOURCES):
        src_attr = struct.unpack_from('<H', at, (src - 0x200) * 2)[0]
        rows.append((METATILE_FLOWER_BASE + n,
                     list(read_metatile(mt, src)),
                     (src_attr & 0xFF00) | long_grass))
    return rows


def write(res):
    mt_path, at_path, tiles_path = res['metatiles'], res['attributes'], res['tiles']
    mt = mt_path.read_bytes()
    if len(mt) // 16 < VANILLA_METATILES:
        raise SystemExit('metatile file is shorter than the vanilla count')

    # Pinned slots must be free in VANILLA. They will not be free after we run.
    clash = set(STAIRS_TILES) & vanilla_referenced_tiles(mt)
    if clash:
        raise SystemExit(f'pinned tile slots collide with vanilla art: '
                         f'{[hex(c) for c in sorted(clash)]}')

    sheet = Image.open(tiles_path)
    if sheet.mode != 'P':
        raise SystemExit('tiles.png is not paletted; refusing to touch it')
    cols = sheet.width // 8
    for slot, tile in zip(STAIRS_TILES, quadrants(STAIRS_ART)):
        tx, ty = (slot % cols) * 8, (slot // cols) * 8
        for y in range(8):
            for x in range(8):
                sheet.putpixel((tx + x, ty + y), tile[y][x])
    # 4bpp keeps the asset diff clean; gbagfx would convert anything, but the
    # checked-in file should look like the one it replaced.
    sheet.save(tiles_path, bits=4)

    rows = build_entries(res)
    head_mt = mt[:VANILLA_METATILES * 16]
    head_at = at_path.read_bytes()[:VANILLA_METATILES * 2]
    tail_mt = b''.join(struct.pack('<8H', *e) for _, e, _ in rows)
    tail_at = b''.join(struct.pack('<H', a) for _, _, a in rows)
    mt_path.write_bytes(head_mt + tail_mt)
    at_path.write_bytes(head_at + tail_at)

    print(f'wrote {len(STAIRS_TILES)} tiles into {tiles_path.name}')
    print(f'appended {len(rows)} metatiles: 0x{rows[0][0]:03X}..0x{rows[-1][0]:03X}')
    print(f'  0x{METATILE_STAIRS:03X}                 exit, COVERED')
    print(f'  0x{METATILE_FLOWER_BASE:03X}..0x{METATILE_FLOWER_BASE + 15:03X}   '
          f'flowers, MB_LONG_GRASS ({behavior_value("MB_LONG_GRASS")})')


def preview(res):
    """The exit alone, and the exit sitting in the mint floor it will sit in.
    A wall metatile is judged against its neighbours, never on a contact sheet -
    the same is true of the one block the player is looking for."""
    import tileset_atlas as ta
    pair = ta.TilesetPair(ta.Tileset(resolver().resolve('gTileset_General')),
                          ta.Tileset(res))
    OUT.mkdir(parents=True, exist_ok=True)
    cell, scale = 16, 6
    grid = 5
    img = Image.new('RGB', (grid * cell * scale, grid * cell * scale), (0, 0, 0))
    floor = ta.render_metatile(pair, 0x27C)
    for gy in range(grid):
        for gx in range(grid):
            tile = floor
            if gx == 2 and gy == 2:
                tile = ta.render_metatile(pair, METATILE_STAIRS)
            img.paste(tile.resize((cell * scale,) * 2, Image.NEAREST),
                      (gx * cell * scale, gy * cell * scale))
    img.save(OUT / 'evergrande_exit.png')
    print(f'wrote {OUT / "evergrande_exit.png"}')


def main():
    res = resolver().resolve('gTileset_EverGrande')
    if '--write' in sys.argv:
        write(res)
    if '--preview' in sys.argv:
        preview(res)
    if len(sys.argv) == 1:
        print(__doc__)
        print(f'vanilla metatiles: {VANILLA_METATILES}, we own '
              f'0x{BASE:03X} upward')


if __name__ == '__main__':
    main()
