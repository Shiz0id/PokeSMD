"""Lift the Pokemon Center's PC out of its tileset and emit it as an object event sprite.

WHY THIS EXISTS. The Pokerus injector wants a terminal rather than another
scientist standing in a cave, and a PC in this engine is TILESET ART. A metatile
id means a different thing under every tileset pair and the dungeon runs fourteen
themes, so a PC placed by id would be a computer in one and garbage in the rest.
Same argument as make_grave_sprite.py, which this follows.

WHICH METATILE, AND HOW IT WAS FOUND. 0x004 under gTileset_Building +
gTileset_PokemonCenter. Found by censusing PokemonCenter_1F_Layout with
render_layout.py, reading the block grid straight out of map.bin, and rendering
the candidates - not guessed, and not picked off a contact sheet.

The first guess was wrong in the instructive way: the 0x24A/0x24B/0x252/0x253
cluster looks like a bank of machines on a contact sheet and is actually the
NURSE'S HEALING MACHINE, orange cross and all. Rendering it settled that in one
look.

THE UNIT IS THREE BLOCKS TALL in the Center - 0x20C printer top, 0x004 screen,
0x224 base - and only the middle one is usable. The border ring of each says why:

    0x20C   26 casing, 18 white, and 8 px of ORANGE WALL
    0x004   46 casing, 14 highlight, and nothing else
    0x224   24 casing, and 34 px of CREAM FLOOR

So 0x004 is the only block that is entirely PC. The other two would drag a strip
of Pokemon Center wall or floor into a sprite meant to stand on a cave, and no
colour-keying is needed for 0x004 because there is no background in it to key.

BOTH LAYERS, NOT THE TOP ONE. The grave sits on top of a floor tile so its top
layer alone is the cut-out; the PC is drawn entirely on the BOTTOM layer and its
top layer is empty. Taking the top layer here yields a blank sprite - which is
exactly what make_grave_sprite.py warns about in its own error message. This
composes both so it stays correct if the art ever moves between layers.

Usage:  python3 tools/rogue/make_pc_sprite.py [--repo PATH]
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tileset_atlas as ta
from tileset_resolve import TilesetResolver

# The engine treats palette index 0 as transparent for object event sprites, and
# every vanilla overworld palette uses this colour for it. Nothing in this sprite
# is transparent - the PC fills its block - but index 0 still has to BE something,
# and matching the convention keeps it consistent with every sibling sprite.
TRANSPARENT = (115, 197, 164)

PRIMARY = 'gTileset_Building'
SECONDARY = 'gTileset_PokemonCenter'
PC_METATILE = 0x004

OUT_PNG = 'graphics/object_events/pics/misc/rogue_pc.png'
OUT_PAL = 'graphics/object_events/palettes/rogue_pc.pal'


def composed_pixels(pair, idx):
    """16x16 of (r,g,b) or None, bottom layer then top layer over it."""
    entries = pair.get_metatile(idx)
    if entries is None:
        sys.exit('metatile 0x%X not found in %s + %s' % (idx, PRIMARY, SECONDARY))

    px = [[None] * 16 for _ in range(16)]

    for layer in (0, 1):
        for q in range(4):
            e = entries[layer * 4 + q]
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
                    # Index 0 is transparent on the TOP layer only. On the
                    # bottom it is a real colour, because nothing shows through.
                    if layer == 1 and ci == 0:
                        continue
                    px[oy + y][ox + x] = colors[ci] if ci < len(colors) else None

    return px


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    args = ap.parse_args()

    ta.REPO = args.repo
    resolver = TilesetResolver(args.repo)
    # Resolved, never joined by hand - the same rule the grave tool follows.
    pair = ta.TilesetPair(ta.Tileset(resolver.resolve(PRIMARY)),
                          ta.Tileset(resolver.resolve(SECONDARY)))

    px = composed_pixels(pair, PC_METATILE)

    opaque = [c for row in px for c in row if c is not None]
    if len(opaque) != 256:
        sys.exit('metatile 0x%X is not fully opaque (%d/256) - it is no longer '
                 'the solid block this sprite assumes' % (PC_METATILE, len(opaque)))

    # THE BORDER RING IS THE ASSERTION THAT PICKED THIS BLOCK. If a wall or floor
    # colour ever appears in it, the metatile has changed into one of its
    # neighbours and the sprite would ship with a strip of Pokemon Center in it.
    ring = ([px[0][x] for x in range(16)] + [px[15][x] for x in range(16)]
            + [px[y][0] for y in range(1, 15)] + [px[y][15] for y in range(1, 15)])
    WALL = (246, 180, 115)
    FLOOR = (238, 222, 164)
    for bad, name in ((WALL, 'Pokemon Center wall'), (FLOOR, 'Pokemon Center floor')):
        if bad in ring:
            sys.exit('metatile 0x%X now has %s in its border - this is the '
                     'wrong block' % (PC_METATILE, name))

    colors = [TRANSPARENT] + sorted(set(opaque))
    if len(colors) > 16:
        sys.exit('PC uses %d colours, past the 16 a 4bpp sprite has' % len(colors))

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

    # JASC-PAL, padded to 16. EXACTLY ONE SPACE between components - gbagfx
    # rejects aligned columns and does not say so clearly.
    rows = colors + [(0, 0, 0)] * (16 - len(colors))
    (args.repo / OUT_PAL).write_text(
        'JASC-PAL\n0100\n16\n' + '\n'.join('%d %d %d' % c for c in rows) + '\n',
        newline='\n')

    print('PC metatile 0x%X, both layers, %d colours + transparency'
          % (PC_METATILE, len(colors) - 1))
    print('  wrote %s' % OUT_PNG)
    print('  wrote %s' % OUT_PAL)
    return 0


if __name__ == '__main__':
    sys.exit(main())
