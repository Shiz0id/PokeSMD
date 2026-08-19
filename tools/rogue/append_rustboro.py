"""Append everything this project adds to the RUSTBORO secondary tileset.

ONE appender, deliberately, exactly as append_metatiles.py is for the Cave and
for the same reason: make_woods_stairs.py is idempotent by TRUNCATING Rustboro
back to its vanilla 350 metatiles and rewriting the tail, so a second script
appending to the same file is silently wiped the next time the first one runs.
Anything needing a Rustboro metatile is composed here instead.

Order fixes the ids - do not reorder:
  make_woods_stairs   the grassy stairs,      0x35E
  make_woods_trees    four autumn trees, 9    0x35F-0x382
                      metatiles each
  make_woods_grass    autumn grass and tall   0x383-0x384
                      grass, palette 12

TILE SLOTS ARE HANDED OUT HERE, not claimed independently. The trees take
theirs first and the grass is told which are gone; both modules pick from
"referenced by no VANILLA metatile", so without that hand-off they both start
at slot 1 and the second silently overwrites the first.

The stairs script runs FIRST and unchanged, because it is the one that does the
truncation. The trees then append on top of it without truncating. Running
make_woods_stairs.py on its own after this still works, but it drops the trees
and you have to come back here - which is why that script now says so.

Writes metatiles.bin, metatile_attributes.bin, tiles.png and palettes/06.pal.

Usage:  python append_rustboro.py
"""
import struct
import sys
from pathlib import Path

from PIL import Image

import make_woods_stairs as stairs
import make_woods_trees as trees
import make_woods_grass as grass

REPO = Path(__file__).resolve().parents[2]
RUSTBORO = REPO / 'data/tilesets/secondary/rustboro'
GENERAL = REPO / 'data/tilesets/primary/general'

TILES_PER_ROW = 16          # tiles.png is 128px wide

# include/global.fieldmap.h. Emerald attributes are u16: behaviour in bits 0-7,
# layer type in bits 12-15.
METATILE_ATTR_LAYER_MASK = 0xF000
METATILE_ATTR_LAYER_SHIFT = 12


def write_tiles(mapping):
    """Blit 8x8 index blocks into tiles.png at their slot positions.

    tiles.png is a 4bpp indexed image whose pixel values ARE palette indices;
    which palette they are read through is decided per metatile entry, not
    here. So writing indices 1..15 straight in is correct and needs no palette
    knowledge at all.
    """
    img = Image.open(RUSTBORO / 'tiles.png')
    if img.mode != 'P':
        raise SystemExit(f'rustboro tiles.png is {img.mode}, expected P')
    px = img.load()
    for slot, data in mapping.items():
        ox, oy = (slot % TILES_PER_ROW) * 8, (slot // TILES_PER_ROW) * 8
        for j in range(8):
            for i in range(0, 8, 2):
                b = data[j * 4 + i // 2]
                px[ox + i, oy + j] = b & 0xF
                px[ox + i + 1, oy + j] = b >> 4
    img.save(RUSTBORO / 'tiles.png', bits=4, optimize=False)
    return len(mapping)


def write_palette(slot, colours):
    lines = ['JASC-PAL', '0100', '16']
    # Entry 0 is the transparent one and is never drawn. Vanilla fills it with
    # something visible so a mistake shows up rather than hiding as black.
    lines.append('0 0 0')
    lines += [f'{r} {g} {b}' for r, g, b in colours]
    lines += ['0 0 0'] * (16 - 1 - len(colours))
    p = RUSTBORO / 'palettes' / f'{slot:02d}.pal'
    # newline='\n' or Windows writes CRLF into a repo file.
    p.write_text('\n'.join(lines) + '\n', newline='\n')
    return p


def main():
    print('== stairs (truncates Rustboro to vanilla, then appends) ==')
    stairs.main()

    print('\n== grass (first, so the trees can stand on it) ==')
    gwrites, gmts, (gslot, gcols) = grass.build()
    trees.TAKEN_SLOTS = set(gwrites)

    print('\n== trees ==')
    mt_path = RUSTBORO / 'metatiles.bin'
    at_path = RUSTBORO / 'metatile_attributes.bin'
    mt = bytearray(mt_path.read_bytes())
    at = bytearray(at_path.read_bytes())
    before = len(mt) // 16
    if before != stairs.RUSTBORO_BASE_METATILES + 1:
        raise SystemExit(f'expected {stairs.RUSTBORO_BASE_METATILES + 1} metatiles '
                         f'after the stairs, found {before} - did the order change?')

    trees.GRASS_BOTTOM = struct.unpack('<8H', gmts[0][1])[:4]

    gat = (GENERAL / 'metatile_attributes.bin').read_bytes()
    first = 0x200 + before
    for label, entry, donor, layer in trees.append_order():
        mt += entry
        # The ATTRIBUTE carries behaviour, and behaviour is what the engine
        # reads for encounters and collision. Copied from the vanilla woods
        # tree rather than synthesised, so these behave as trees have always
        # behaved; one invented constant here is the same class of bug as a
        # hard-coded per-theme metatile.
        #
        # THE LAYER TYPE IS THE ONE FIELD THE DONOR IS WRONG ABOUT, because our
        # tree sits in the top half of the metatile and vanilla's sits in the
        # bottom. make_woods_trees.py decides it per row and says why; here it
        # only replaces bits 12-15, so the donor still owns the behaviour byte.
        a = struct.unpack('<H', gat[donor * 2:donor * 2 + 2])[0]
        at += struct.pack('<H', (a & ~METATILE_ATTR_LAYER_MASK)
                                | (layer << METATILE_ATTR_LAYER_SHIFT))
    mt_path.write_bytes(bytes(mt))
    at_path.write_bytes(bytes(at))
    total_after_trees = len(mt) // 16

    tree_tiles = trees.tile_writes()
    overlap = set(gwrites) & set(tree_tiles)
    if overlap:
        raise SystemExit(f'grass and trees both claimed tiles {sorted(overlap)}')
    n = write_tiles(tree_tiles)
    write_tiles(gwrites)
    slot, colours = trees.palette_write()
    pal_path = write_palette(slot, colours)

    for label, entry, donor in gmts:
        mt += entry
        at += gat[donor * 2:donor * 2 + 2]
    mt_path.write_bytes(bytes(mt))
    at_path.write_bytes(bytes(at))
    gpal_path = write_palette(gslot, gcols)
    print(f'metatiles-> 0x{0x200 + total_after_trees:03X}-'
          f'0x{0x200 + len(mt)//16 - 1:03X}  {[m[0] for m in gmts]}')
    print(f'tiles    -> {len(gwrites)} into slots {sorted(gwrites)}')
    print(f'palette  -> {gpal_path.relative_to(REPO)} (rotated '
          f'{grass.ROTATE_DEG} deg)')

    total = len(mt) // 16
    print(f'metatiles-> 0x{first:03X}-0x{total + 0x200 - 1:03X}  '
          f'({total - before} tree metatiles)')
    print(f'tiles    -> {n} written into free Rustboro slots')
    print(f'palette  -> {pal_path.relative_to(REPO)}')
    print(f'           {total} Rustboro metatiles, '
          f'{0x400 - 0x200 - total} slots still free')
    print(f'\nset WOODS_METATILE_TREE_* from 0x{first:03X}')


if __name__ == '__main__':
    sys.exit(main())
