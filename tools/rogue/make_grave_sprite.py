"""Lift the Mt Pyre gravestone out of its tileset and emit it as an object event sprite.

WHY THIS EXISTS. The memorial shrine wants the old woman to be tending something,
and a grave in this engine is TILESET ART, not a sprite. A metatile id means a
different thing under every tileset pair, so a grave placed by id would be correct
in one theme and garbage in the rest - and the dungeon runs fourteen themes.
Turning it into an object event sprite makes it theme-independent, which is what
lets the shrine appear anywhere.

WHICH METATILE, AND HOW IT WAS FOUND. 0x2EE under gTileset_General +
gTileset_Facility. NOT guessed from a name and not picked off a contact sheet:
render_layout.py censused MtPyre_2F_Layout, which is a 13x13 room whose whole
purpose is gravestones, and 0x2EE is used 42 times there against 0x326's 9. Both
render identically, so the count is what settles it. The summit was the obvious
place to look and was the wrong one - its graves are a different tileset pair.

Note the tileset pair itself: gTileset_Facility is not a name anyone would guess
carries gravestones. It was read out of layouts.json, never inferred.

ONLY THE TOP LAYER IS TAKEN. A metatile is two 2x2 layers, bottom then top, and
the grave sits on the top one over a floor. Rendering the top layer alone gives
the grave already cut out against transparency - no colour-keying, no guessing
which pale pixels were floor and which were stone.

Usage:  python3 tools/rogue/make_grave_sprite.py [--repo PATH]
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tileset_atlas as ta
from tileset_resolve import TilesetResolver

# The engine treats palette index 0 as transparent for object event sprites, and
# every vanilla overworld palette uses this colour for it. Matching it is what
# makes the sprite composite over whatever floor it is standing on.
TRANSPARENT = (115, 197, 164)

PRIMARY = 'gTileset_General'
SECONDARY = 'gTileset_Facility'
GRAVE_METATILE = 0x2EE

OUT_PNG = 'graphics/object_events/pics/misc/rogue_grave.png'
OUT_PAL = 'graphics/object_events/palettes/rogue_grave.pal'


def top_layer_pixels(pair, idx):
    """16x16 of (r,g,b) or None, taking ONLY the metatile's top layer."""
    entries = pair.get_metatile(idx)
    if entries is None:
        sys.exit('metatile 0x%X not found in %s + %s' % (idx, PRIMARY, SECONDARY))

    px = [[None] * 16 for _ in range(16)]

    for q in range(4):                      # 2x2 quadrants of the TOP layer
        e = entries[4 + q]
        tile, pal = e & ta.TILE_MASK, (e >> ta.PAL_SHIFT) & 0xF
        xf, yf = bool(e & ta.XFLIP_BIT), bool(e & ta.YFLIP_BIT)
        rows = pair.tile_pixels(tile)
        if rows is None:
            continue
        colors = pair.palette(pal)
        ox, oy = (q % 2) * 8, (q // 2) * 8
        for y in range(8):
            sy = 7 - y if yf else y
            for x in range(8):
                sx = 7 - x if xf else x
                ci = rows[sy][sx]
                if ci == 0:                 # transparent on the top layer
                    continue
                px[oy + y][ox + x] = colors[ci] if ci < len(colors) else None

    return px


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    args = ap.parse_args()

    ta.REPO = args.repo
    resolver = TilesetResolver(args.repo)
    # Resolved, never joined by hand: gTileset_Facility's assets do not live
    # where its symbol suggests, and seven tilesets share directories.
    pair = ta.TilesetPair(ta.Tileset(resolver.resolve(PRIMARY)),
                          ta.Tileset(resolver.resolve(SECONDARY)))

    px = top_layer_pixels(pair, GRAVE_METATILE)

    opaque = [c for row in px for c in row if c is not None]
    if not opaque:
        sys.exit('metatile 0x%X has an EMPTY top layer - the art is on the '
                 'bottom layer and this approach will not work for it'
                 % GRAVE_METATILE)

    # Index 0 is transparency; the rest are whatever the grave actually uses.
    # Sorted so the palette is stable across runs - a sprite whose palette
    # reshuffles on every regeneration is a diff nobody can review.
    colors = [TRANSPARENT] + sorted(set(opaque))
    if len(colors) > 16:
        sys.exit('grave uses %d colours, past the 16 a 4bpp sprite has'
                 % len(colors))

    index = {c: i for i, c in enumerate(colors)}

    img = Image.new('P', (16, 16), 0)
    flat = []
    for c in colors:
        flat += list(c)
    flat += [0, 0, 0] * (256 - len(colors))
    img.putpalette(flat)
    img.putdata([0 if px[y][x] is None else index[px[y][x]]
                 for y in range(16) for x in range(16)])

    png = args.repo / OUT_PNG
    png.parent.mkdir(parents=True, exist_ok=True)
    img.save(png)

    # JASC-PAL, padded to 16 entries. EXACTLY ONE SPACE between components -
    # gbagfx rejects aligned columns, which is not obvious from the error.
    rows = colors + [(0, 0, 0)] * (16 - len(colors))
    (args.repo / OUT_PAL).write_text(
        'JASC-PAL\n0100\n16\n' + '\n'.join('%d %d %d' % c for c in rows) + '\n',
        newline='\n')

    print('grave metatile 0x%X, top layer, %d colours + transparency'
          % (GRAVE_METATILE, len(colors) - 1))
    print('  wrote %s' % OUT_PNG)
    print('  wrote %s' % OUT_PAL)
    return 0


if __name__ == '__main__':
    sys.exit(main())
