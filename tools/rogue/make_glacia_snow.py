"""
Snow art for the Glacia floors: flat snow, snow drifts, and an icy rock.

Adapted from a Red Rescue Team snow field to this engine's constraints - 16x16
metatiles built from 8x8 tiles, 4bpp, and one 16-colour palette. The reference
has three things on it and they map onto three things the generator already
does, which is why it is worth copying: flat snow is a FLOOR, and the drifts and
rocks are DECOR - single blocks swapped for a variant, collision and elevation
copied from the base, so they can never change reachability.

Two constraints shaped all of it.

**The art goes in the SHARED cave files.** gTileset_RogueVictoryRoadGlacia
shares gTilesetTiles_Cave and gMetatiles_Cave with Granite Cave and four other
themes - that sharing is the whole reason the Victory Road themes are cheap.
So these tiles land in cave/tiles.png and these metatiles are appended to the
cave's metatiles.bin, where Granite Cave can see them and simply never paints
them. They cost it nothing but ROM. Nothing here may touch an EXISTING cave
tile or metatile, or five other themes change.

**Everything is drawn in palette 6.** That is the palette the whole cave wall
table already uses, and the only one the Victory Road recolours control. Under
Glacia's it is a genuine snow ramp with no new palette needed - white at 8,
pale ice at 1 and 9, a blue shading ladder at 2-7, saturated cyan at 10-11.
The same tiles under Granite Cave's palette 6 are brown, which does not matter
because the cave never paints them.

Parametric rather than text art, following whirlpool_art.py: these are organic
curves, and a tapered crescent is far easier to tune as a function than to
place by hand.

  python make_glacia_snow.py --preview   # render, touch nothing
  python make_glacia_snow.py --write     # draw tiles into cave/tiles.png

--write only writes the TILES. The metatile entries are exported as
APPEND_ORDER and appended by append_metatiles.py, which owns the tail of the
cave's metatiles.bin and truncates anything past the vanilla 414 every time it
runs. A second appender here would be silently wiped by the next run of that
one.
"""
import argparse
import math
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

NUM_TILES_IN_PRIMARY = 512
PALETTE = 6

# Palette 6 indices, named for what they are under Glacia's recolour.
WHITE = 8    # #FFFFFF - full highlight
PALE = 1     # #ADDEF7 - the snow base
ICE = 9      # #BDE7F7 - a touch paler than the base, for soft mottling
SHADE1 = 2   # #A5CEDE
SHADE2 = 3   # #9CBDCE
SHADE3 = 4   # #94ADB5
SHADE4 = 5   # #849CAD
DEEP = 6     # #638C9C
DEEPEST = 7  # #5A7B8C


def _hash(x, y):
    """Deterministic per-pixel noise. This script writes a checked-in asset, so
    it must not touch `random`."""
    h = (x * 374761393 + y * 668265263) & 0xFFFFFFFF
    h = (h ^ (h >> 13)) * 1274126177 & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0


def flat_snow():
    """16x16 of flat snow: fine noise over three ADJACENT indices, nothing else.

    The first attempt gave this a diagonal swell and a few white glints and both
    were wrong in kind, for one reason: the floor metatile repeats every 16
    pixels, so ANY interior feature becomes a lattice. The swell tiled into
    continuous candy-stripes across the whole floor and the glints into a
    regular dot grid. The reference image is a hand-drawn field and never has to
    tile; this does, everywhere, against copies of itself.

    Vanilla's answer, read off cave 0x201 rather than guessed: 55% index 4, 34%
    index 5, 9% index 6, 1.5% index 3 - four ADJACENT indices as per-pixel
    noise, never the extremes of the ramp. Noise has no structure to repeat, and
    neighbouring indices are too close in value to read as a pattern even where
    it does. Those proportions are reused here, shifted to the pale end.

    In particular there is no WHITE. White on pale blue is high contrast, and
    high contrast is exactly what makes a 16-pixel period visible. The glints
    belong in the decor, which is placed by position hash and so is irregular.
    """
    g = [[PALE] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            r = _hash(x, y)
            if r < 0.015:
                g[y][x] = SHADE2     # rarest, the darkest step
            elif r < 0.105:
                g[y][x] = SHADE1
            elif r < 0.445:
                g[y][x] = ICE
    return g


def snow_drift():
    """32x16 - a tapered crescent ridge, drawn across TWO metatiles.

    Two wide because the reference's drifts sweep far further than 16 pixels
    and a crescent cut in half at 16 reads as a smear. The decor pass supports
    this directly: a nonzero variantEast lands the pair only where the block to
    the east is also undecorated base, and writes both halves.
    """
    base = flat_snow()
    g = [base[y][:] + base[y][:] for y in range(16)]
    for x in range(32):
        t = x / 31.0
        # sin^0.6 rather than sin: a fuller middle and a quicker taper, so it
        # reads as a wind-piled crescent instead of a lens.
        thick = 3.4 * math.sin(math.pi * t) ** 0.6
        if thick < 0.5:
            continue
        ridge = 9.0 - 3.0 * math.sin(math.pi * t * 0.85 + 0.35)
        for y in range(16):
            d = y - ridge
            if abs(d) <= thick:
                f = (d + thick) / (2 * thick)
                # Only the very crest takes white; the first attempt used it
                # across a third of the body and the drift read as a blade.
                if f < 0.16:
                    g[y][x] = WHITE
                elif f < 0.52:
                    g[y][x] = ICE
                elif f < 0.80:
                    g[y][x] = PALE
                else:
                    g[y][x] = SHADE1
            else:
                below = d - thick
                if 0 < below <= 1.4:
                    g[y][x] = SHADE2      # cast shadow, the thing that seats it
                elif 1.4 < below <= 2.6:
                    g[y][x] = SHADE1
    return g


def ice_rock():
    """16x16 - a rounded ice boulder sitting on snow, lit from the upper left.

    Bottom layer only. A top layer would draw OVER the player under
    METATILE_LAYER_TYPE_NORMAL and hide them when they stand on it, which the
    woods stairs already learnt; this is walkable decor, so the snow behind the
    rock is baked into the same tiles.
    """
    g = flat_snow()
    cx, cy, rx, ry = 7.7, 7.9, 5.6, 4.7
    for y in range(16):
        for x in range(16):
            nx, ny = (x - cx) / rx, (y - cy) / ry
            d = nx * nx + ny * ny
            if d > 1.0:
                continue
            # Lambert-ish term from the upper left, so the facet reads convex.
            lit = (-nx - ny) / 1.9
            if d > 0.80:
                # Rim. Dark all the way round the lower half is what separates
                # the rock from the snow it stands in; a pale rim disappears.
                g[y][x] = DEEP if lit < -0.1 else SHADE3
            elif lit > 0.55:
                g[y][x] = WHITE          # the snow cap on top
            elif lit > 0.10:
                g[y][x] = ICE
            elif lit > -0.30:
                g[y][x] = SHADE2
            elif lit > -0.62:
                g[y][x] = SHADE3
            else:
                g[y][x] = SHADE4         # the ice showing through underneath
    # Contact shadow, so it sits ON the snow instead of floating in it.
    for x in range(16):
        nx = (x - cx) / (rx * 1.02)
        if abs(nx) >= 1.0:
            continue
        depth = math.sqrt(1.0 - nx * nx)
        for k in range(int(round(2.1 * depth))):
            y = int(cy + ry * 0.90) + k
            if 0 <= y < 16 and g[y][x] in (PALE, ICE, SHADE1, SHADE2):
                g[y][x] = SHADE2 if k == 0 else SHADE1
    return g


VANILLA_METATILES = 414   # the cave's count before this project appended to it

# The tile slots this art owns, PINNED rather than measured at run time.
#
# "Free slots have to be measured, not assumed" needs a corollary: once
# measured they have to be RECORDED, because writing the art invalidates the
# measurement. These were originally chosen by free_tile_slots() below - slots
# that no metatile referenced and whose pixels were blank - and pinning them
# cost a real bug first: plan() re-measured on every call, so after --write
# filled these, append_metatiles.py re-ran the search, got a completely
# different set, and wrote metatile entries pointing at blank tiles. The floor
# rendered as palette index 0.
#
# Local tile 0 is deliberately absent. It measures as free - it is VRAM 512,
# not the primary's blank tile 0, and nothing references it - but index 0 is
# conventionally the empty tile and a top-layer entry of 0x0000 means exactly
# that. Skipping it costs one slot of seventy-nine.
SLOTS = [0x043, 0x046, 0x056, 0x082,                    # snow floor
         0x083, 0x092, 0x093, 0x0B9,                    # drift, left half
         0x0C8, 0x0C9, 0x0CC, 0x0CD,                    # drift, right half
         0x0CE, 0x0CF, 0x0D8, 0x0D9]                    # ice rock


def validate_slots(cave):
    """None of our slots may be referenced by a VANILLA cave metatile.

    Deliberately checks only the first 414. Anything past that is this
    project's own appends, and our snow metatiles are themselves the only
    things referencing SLOTS - including them would make the check circular and
    it would pass or fail depending on whether the appender had run yet.
    """
    mt = cave['metatiles'].read_bytes()
    used = set()
    for i in range(min(VANILLA_METATILES, len(mt) // 16)):
        for t in struct.unpack('<8H', mt[i * 16:(i + 1) * 16]):
            used.add(t & 0x3FF)
    clash = [s for s in SLOTS if NUM_TILES_IN_PRIMARY + s in used]
    if clash:
        raise SystemExit('slots referenced by vanilla cave metatiles: '
                         + ' '.join(f'0x{s:03X}' for s in clash))


def free_tile_slots():
    """Slots that no metatile references AND whose pixels are blank.

    Not used by plan() - see SLOTS. Kept because it is how SLOTS was chosen and
    how more would be chosen, and because it reports what is left.
    """
    from PIL import Image
    from tileset_resolve import TilesetResolver
    cave = TilesetResolver(REPO).resolve('gTileset_Cave')
    mt = cave['metatiles'].read_bytes()
    used = set()
    for i in range(len(mt) // 16):
        for t in struct.unpack('<8H', mt[i * 16:(i + 1) * 16]):
            used.add(t & 0x3FF)
    img = Image.open(cave['tiles'])
    img = img.convert('P') if img.mode != 'P' else img
    per = img.width // 8
    total = per * (img.height // 8)
    px = img.load()
    out = []
    for t in range(total):
        if t == 0 or NUM_TILES_IN_PRIMARY + t in used:
            continue
        tx, ty = (t % per) * 8, (t // per) * 8
        if all(px[tx + x, ty + y] == 0 for x in range(8) for y in range(8)):
            out.append(t)
    return out, img, cave


def split_tiles(grid):
    """A w x h grid of palette indices -> list of 8x8 tiles, TL TR BL BR order
    per 16x16 block, left block first. That is the order a metatile's entries
    reference them in."""
    h, w = len(grid), len(grid[0])
    tiles = []
    for bx in range(w // 16):
        for (oy, ox) in ((0, 0), (0, 8), (8, 0), (8, 8)):
            tiles.append([[grid[oy + y][bx * 16 + ox + x] for x in range(8)]
                          for y in range(8)])
    return tiles


# Layout of what gets drawn, in allocation order.
PIECES = [
    ('snow floor', flat_snow, 1),
    ('snow drift', snow_drift, 2),
    ('ice rock',   ice_rock,  1),
]


def plan():
    """-> (assignments, entries) without writing anything.

    assignments: list of (name, [(slot, tile_pixels), ...])
    entries:     list of (label, 8-tuple metatile entry)
    """
    from PIL import Image
    from tileset_resolve import TilesetResolver
    cave = TilesetResolver(REPO).resolve('gTileset_Cave')
    validate_slots(cave)
    img = Image.open(cave['tiles'])
    img = img.convert('P') if img.mode != 'P' else img

    need = sum(blocks * 4 for _, _, blocks in PIECES)
    if len(SLOTS) != need:
        raise SystemExit(f'SLOTS has {len(SLOTS)} entries, PIECES need {need}')

    assignments, entries, cursor = [], [], 0
    for name, fn, blocks in PIECES:
        tiles = split_tiles(fn())
        slots = SLOTS[cursor:cursor + blocks * 4]
        cursor += blocks * 4
        assignments.append((name, list(zip(slots, tiles))))
        for b in range(blocks):
            quad = slots[b * 4:(b + 1) * 4]
            ent = [(PALETTE << 12) | (NUM_TILES_IN_PRIMARY + s) for s in quad]
            ent += [0, 0, 0, 0]      # bottom layer only; see ice_rock()
            label = name if blocks == 1 else f'{name} {"LR"[b]}'
            entries.append((label, tuple(ent)))
    return assignments, entries, img, cave


# Consumed by append_metatiles.py. Order defines the appended ids, so appending
# AFTER compose_metatiles' seven slivers keeps those at 0x39E-0x3A4.
def append_order():
    return [(label, ent) for label, ent in plan()[1]]


def cmd_write():
    assignments, entries, img, cave = plan()
    px = img.load()
    per = img.width // 8
    n = 0
    for name, pairs in assignments:
        for slot, tile in pairs:
            tx, ty = (slot % per) * 8, (slot // per) * 8
            for y in range(8):
                for x in range(8):
                    px[tx + x, ty + y] = tile[y][x]
            n += 1
    # 4bpp keeps the asset diff clean; gbagfx would convert either way.
    img.save(cave['tiles'], bits=4)
    print(f'wrote {n} tiles into {cave["tiles"].relative_to(REPO)}')
    for name, pairs in assignments:
        print(f'  {name:12} tiles ' + ' '.join(f'0x{s:03X}' for s, _ in pairs))
    print('\nmetatile entries (append_metatiles.py assigns the ids):')
    for label, ent in entries:
        print(f'  {label:14} ' + ' '.join(f'{v:04X}' for v in ent))
    print('\nnext: python3 append_metatiles.py')


def cmd_preview(scale=6):
    from PIL import Image, ImageDraw
    import tileset_atlas as ta
    from tileset_resolve import TilesetResolver

    assignments, entries, _, _ = plan()
    R = TilesetResolver(REPO)
    pals = {}
    for sym, label in (('gTileset_RogueVictoryRoadGlacia', 'Glacia palette'),
                       ('gTileset_Cave', 'Granite Cave palette (never painted)')):
        p6 = [p for p in R.resolve(sym)['palettes'] if p.name == '06.pal'][0]
        pals[label] = ta.parse_jasc_pal(p6)

    # Build the pieces straight from the grids rather than from the tileset,
    # so this renders before anything is written.
    grids = [(name, fn()) for name, fn, _ in PIECES]
    # A patch of floor with one of each decor dropped in, which is the only way
    # to judge decor - alone on a sheet it always looks fine.
    floor = flat_snow()
    scene_w, scene_h = 8, 4
    scene = [[None] * scene_w for _ in range(scene_h)]
    scene[1][1] = ('drift', 0)
    scene[1][2] = ('drift', 1)
    scene[2][5] = ('rock', 0)
    scene[0][4] = ('rock', 0)
    scene[3][3] = ('drift', 0)
    scene[3][4] = ('drift', 1)
    drift = snow_drift()
    rock = ice_rock()

    pad = 12
    cell = 16 * scale
    W = scene_w * cell + pad * 2
    H = len(pals) * (scene_h * cell + pad + 18) + pad
    out = Image.new('RGB', (W, H), (26, 26, 32))
    d = ImageDraw.Draw(out)
    for n, (label, cols) in enumerate(pals.items()):
        oy = pad + n * (scene_h * cell + pad + 18)
        d.text((pad, oy - 2), label, fill=(232, 232, 240))
        for by in range(scene_h):
            for bx in range(scene_w):
                what = scene[by][bx]
                for y in range(16):
                    for x in range(16):
                        if what is None:
                            v = floor[y][x]
                        elif what[0] == 'drift':
                            v = drift[y][what[1] * 16 + x]
                        else:
                            v = rock[y][x]
                        c = cols[v]
                        out.paste(c, (pad + bx * cell + x * scale,
                                      oy + 16 + by * cell + y * scale,
                                      pad + bx * cell + (x + 1) * scale,
                                      oy + 16 + by * cell + (y + 1) * scale))
    p = Path(__file__).resolve().parent / '_out' / 'glacia_snow.png'
    p.parent.mkdir(exist_ok=True)
    out.save(p)
    print('wrote', p)

    # Game scale, because a floor judged only at 6x is a floor judged wrong.
    small = out.resize((W * 2 // scale, H * 2 // scale), Image.NEAREST)
    p2 = p.with_name('glacia_snow_2x.png')
    small.save(p2)
    print('wrote', p2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--preview', action='store_true')
    ap.add_argument('--write', action='store_true')
    a = ap.parse_args()
    if a.preview:
        cmd_preview()
    if a.write:
        cmd_write()
    if not a.preview and not a.write:
        ap.error('nothing to do: pass --preview or --write')


if __name__ == '__main__':
    main()
