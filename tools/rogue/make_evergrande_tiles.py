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
METATILE_LONGGRASS_BASE = BASE + 17     # 0x2B9 .. 0x2C0, the eight

# gTileset_General's long grass, MB_LONG_GRASS, bottom layer only and four
# plain tiles. The flowery version is that verbatim with a bloom overlaid on
# top - the cobble's structure, where 0x232 is General's grass under a
# palette-8 stone overlay.
GENERAL_LONG_GRASS = 0x015

# Where a blossom sits in each of the eight, as (x, y) of its centre in the
# 16x16, or None. Palette 10 is the pink-and-orange flower bed's own, so the
# blooms up here are the same colours as the beds across the floor.
#
# THREE OF THE EIGHT ARE BARE, and that is the point. A feature baked into a
# metatile repeats every 16 pixels and becomes a lattice - section 5 says so
# about floors and it is just as true of a patch layer. Leaving gaps in the
# cycle means the flowers punctuate the grass instead of ruling it, and the
# bare ones cost no tiles at all: an entirely transparent top-layer quadrant is
# the entry 0x0000 and references nothing.
#
# Positions are kept clear of the quadrant boundaries so each blossom lands
# inside ONE quadrant. That is not cosmetic - it is what keeps this to five new
# tiles instead of twenty.
LONGGRASS_BLOOMS = (
    None,
    (4, 5, 'pink'),
    (11, 3, 'orange'),
    None,
    (6, 11, 'orange'),
    (13, 9, 'pink'),
    None,
    (3, 12, 'pink'),
)
LONGGRASS_PALETTE = 10
# Palette 10: 6-8 are the orange ramp, 9-11 the pink one, 0 transparent.
BLOOM_COLOURS = {'orange': (6, 7, 8), 'pink': (9, 10, 11)}

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
# One per flowered long-grass variant. Five, not twenty, because each blossom
# is placed inside a single quadrant and the other three quadrants of its top
# layer are the entry 0x0000, which references no tile at all.
LONGGRASS_TILES = (0x02B, 0x02C, 0x02D, 0x02E, 0x02F)

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


def mt_primary():
    """gTileset_General's metatiles. The long grass is a PRIMARY metatile, so
    its bottom layer is borrowed rather than copied - primary tile indices mean
    the same thing under every pair, which is the one case where sharing an id
    across themes is safe."""
    return resolver().resolve('gTileset_General')['metatiles'].read_bytes()


def primary_attr(mid):
    at = resolver().resolve('gTileset_General')['attributes'].read_bytes()
    return struct.unpack_from('<H', at, mid * 2)[0]


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


def bloom_quadrant(bloom):
    """-> (quadrant index 0-3, an 8x8 tile) for one blossom.

    A five-pixel blossom: lit centre, mid ring, one dark pixel below for a
    shadow. Everything else is index 0, which in a TOP layer is transparent, so
    the long grass shows through around it."""
    bx, by, which = bloom
    light, mid, dark = BLOOM_COLOURS[which]
    q = (0 if by < 8 else 2) + (0 if bx < 8 else 1)
    tile = [[0] * 8 for _ in range(8)]
    ox, oy = bx % 8, by % 8
    for dx, dy, c in ((0, 0, light), (-1, 0, mid), (1, 0, mid),
                      (0, -1, mid), (0, 1, dark)):
        x, y = ox + dx, oy + dy
        if not (0 <= x < 8 and 0 <= y < 8):
            raise SystemExit(f'blossom at {bx},{by} crosses a quadrant edge; '
                             f'move it or this needs more than one tile')
        tile[y][x] = c
    return q, tile


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

    # The short beds: vanilla's pixels, our behaviour.
    #
    # MB_UNUSED_05, not MB_LONG_GRASS. It carries TILE_FLAG_HAS_ENCOUNTERS and
    # NOTHING else - its only reference in the whole engine is
    # Unref_MetatileBehavior_IsUnused05, which nothing calls - so these spawn
    # wild Pokemon and draw no overlay and do not clip the player. That is how
    # Ever Grande City itself treats these beds: you walk over them.
    #
    # They were MB_LONG_GRASS first, and seeing it in situ is what changed it.
    # The bed is bold and LOW - half-tile blooms with hard dark outlines - and
    # the long grass overlay is a full-height curtain that hides the player's
    # lower half. The two disagreed about how tall the thing underfoot was.
    unused_05 = behavior_value('MB_UNUSED_05')
    for n, src in enumerate(FLOWER_SOURCES):
        src_attr = struct.unpack_from('<H', at, (src - 0x200) * 2)[0]
        rows.append((METATILE_FLOWER_BASE + n,
                     list(read_metatile(mt, src)),
                     (src_attr & 0xFF00) | unused_05))

    # The tall surface, which is what the curtain belongs to: General's own long
    # grass under a bloom overlay. Keeps 0x015's attribute, so it stays
    # MB_LONG_GRASS and the overlay and the OAM clip both fire.
    # Indexed directly, not through read_metatile, which subtracts 0x200 for a
    # secondary id and would run off the front of a primary file.
    lg = struct.unpack_from('<8H', mt_primary(), GENERAL_LONG_GRASS * 16)
    lg_attr = primary_attr(GENERAL_LONG_GRASS)
    tile_iter = iter(LONGGRASS_TILES)
    for n, bloom in enumerate(LONGGRASS_BLOOMS):
        top = [0, 0, 0, 0]
        if bloom is not None:
            q, _ = bloom_quadrant(bloom)
            top[q] = entry(512 + next(tile_iter), LONGGRASS_PALETTE)
        rows.append((METATILE_LONGGRASS_BASE + n, list(lg[:4]) + top, lg_attr))
    return rows


def write(res):
    mt_path, at_path, tiles_path = res['metatiles'], res['attributes'], res['tiles']
    mt = mt_path.read_bytes()
    if len(mt) // 16 < VANILLA_METATILES:
        raise SystemExit('metatile file is shorter than the vanilla count')

    # Pinned slots must be free in VANILLA. They will not be free after we run.
    pinned = set(STAIRS_TILES) | set(LONGGRASS_TILES)
    if len(pinned) != len(STAIRS_TILES) + len(LONGGRASS_TILES):
        raise SystemExit('two pinned tile slots are the same')
    clash = pinned & vanilla_referenced_tiles(mt)
    if clash:
        raise SystemExit(f'pinned tile slots collide with vanilla art: '
                         f'{[hex(c) for c in sorted(clash)]}')

    sheet = Image.open(tiles_path)
    if sheet.mode != 'P':
        raise SystemExit('tiles.png is not paletted; refusing to touch it')
    cols = sheet.width // 8

    art = list(zip(STAIRS_TILES, quadrants(STAIRS_ART)))
    tile_iter = iter(LONGGRASS_TILES)
    for bloom in LONGGRASS_BLOOMS:
        if bloom is not None:
            art.append((next(tile_iter), bloom_quadrant(bloom)[1]))

    for slot, tile in art:
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

    verify(res)
    print(f'wrote {len(art)} tiles into {tiles_path.name}')
    print(f'appended {len(rows)} metatiles: 0x{rows[0][0]:03X}..0x{rows[-1][0]:03X}')
    print(f'  0x{METATILE_STAIRS:03X}                 exit, COVERED')
    print(f'  0x{METATILE_FLOWER_BASE:03X}..0x{METATILE_FLOWER_BASE + 15:03X}   '
          f'short beds, MB_UNUSED_05 ({behavior_value("MB_UNUSED_05")}) '
          f'- encounters, no overlay')
    print(f'  0x{METATILE_LONGGRASS_BASE:03X}..0x{METATILE_LONGGRASS_BASE + 7:03X}   '
          f'flowery long grass, MB_LONG_GRASS '
          f'({behavior_value("MB_LONG_GRASS")}) - encounters and the curtain')


def encounter_behaviours():
    """Behaviour names carrying TILE_FLAG_HAS_ENCOUNTERS, read out of the engine
    rather than listed here, so a change upstream cannot silently pass."""
    txt = (REPO / 'src/metatile_behavior.c').read_text(encoding='utf-8')
    return {m.group(1) for m in
            re.finditer(r'\[(MB_[A-Z0-9_]+)\]\s*=\s*([^,\n]+)', txt)
            if 'HAS_ENCOUNTERS' in m.group(2)}


def behaviour_names():
    txt = (REPO / 'include/constants/metatile_behaviors.h').read_text(encoding='utf-8')
    out, val = {}, 0
    for m in re.finditer(r'^\s*(MB_[A-Z0-9_]+)\s*(?:=\s*(0x[0-9A-Fa-f]+|\d+))?\s*,',
                         txt, re.M):
        if m.group(2):
            val = int(m.group(2), 0)
        out[val] = m.group(1)
        val += 1
    return out


def verify(res):
    """Re-read what was just written and hold BOTH encounter surfaces to
    account.

    check_encounter_flags.py cannot do this. It reads .floor, .tallGrass and
    .longGrass off the theme table and passes a theme that has ONE surface
    carrying encounters - so the tall grass alone would satisfy it, and the
    sixteen short beds silently losing their flag would go unnoticed until a
    playthrough turned up a floor where half the flowers were inert. Nothing in
    the theme table names the beds at all; they are painted by a patch layer.
    """
    at = res['attributes'].read_bytes()
    names, enc = behaviour_names(), encounter_behaviours()

    def behaviour_of(mid):
        return names.get(struct.unpack_from('<H', at, (mid - 0x200) * 2)[0] & 0xFF)

    bad = []
    for n in range(16):
        mid = METATILE_FLOWER_BASE + n
        if behaviour_of(mid) not in enc:
            bad.append((mid, behaviour_of(mid)))
    for n in range(len(LONGGRASS_BLOOMS)):
        mid = METATILE_LONGGRASS_BASE + n
        if behaviour_of(mid) not in enc:
            bad.append((mid, behaviour_of(mid)))
    if bad:
        raise SystemExit('these carry no encounter flag: '
                         + ', '.join(f'0x{m:03X} ({b})' for m, b in bad))

    # And the exit must NOT, or a wild battle fires as the player leaves.
    if behaviour_of(METATILE_STAIRS) in enc:
        raise SystemExit(f'the exit 0x{METATILE_STAIRS:03X} carries encounters')


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
