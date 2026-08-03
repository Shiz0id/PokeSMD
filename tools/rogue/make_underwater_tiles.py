"""
The whirlpool again, this time appended to gTileset_Underwater.

The seafloor descends through the same vortex the ocean does. Metatile ids above
0x200 belong to whichever secondary is loaded, so 0x3D1 means nothing here and
the metatile has to exist twice - once per tileset. The SHAPE is shared, from
whirlpool_art.py, so the two exits cannot drift apart: it is the one thing on a
floor the player is looking for, and two dungeons whose exits looked different
would be teaching them two things instead of one.

The COLOURS are not shared, and must not be. The ocean's whirlpool is drawn in
primary palette 4, the sea's own; this one is drawn in gTileset_Underwater's
palette B, which is what the seafloor itself uses. A vortex reads as water only
when it is made of the water around it. Palette B carries two ramps and this
takes the BLUE one, leaving the violet to the floor, so the whirlpool reads as
open water opening in the seabed rather than as a recoloured patch of ground.

The seafloor rather than the sea goes underneath: unlike the ocean, where the
bottom layer is animated open water, here the player is standing ON something,
and outside the disc that something has to still be the floor they are walking
across.

THIS EDITS VANILLA ASSET FILES - metatiles.bin, metatile_attributes.bin and
tiles.png under data/tilesets/secondary/underwater/. Pulling upstream changes to
that tileset means taking upstream's files and re-running this, not merging.
Idempotent: the metatile tail is trimmed and the tile slots are overwritten.

Everything else the underwater theme uses is vanilla and untouched - the wall
table, the corners and the seaweed are all read straight off the sea routes, so
this one metatile is the whole of what the theme adds.

Run:  python3 tools/rogue/make_underwater_tiles.py
      Needs Pillow, so run it from Windows against //wsl.localhost/Ubuntu/...
"""
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import whirlpool_art
from tileset_resolve import TilesetResolver

BASE_COUNT = 236             # vanilla gTileset_Underwater metatile count
NUM_TILES_IN_PRIMARY = 512

FLOOR = 0x216                # the seafloor, and the bottom layer underneath
ATTR_DONOR = 0x2A9           # MB_NO_SURFACING, the stairs metatile being replaced

# Four consecutive blank, unreferenced slots inside one sheet row, well clear of
# the seaweed animation at 0x1F0-0x1F3. Measured, not assumed: 335 of the 512
# slots are blank AND referenced by no metatile, the longest free run being 86.
WHIRL_TILES = (0x19A, 0x19B, 0x19C, 0x19D)
WHIRL_PALETTE = 0xB          # the seafloor's own

# Palette B by role. It holds two ramps; this is the BLUE one, so the vortex
# separates from the violet floor instead of sinking into it. The rim is the one
# violet entry, which lets the disc's edge meet the seabed rather than ring it.
#   1 D5D5FF  2 9C9CFF  3 8383FF  4 6A6AF6  5 4A4ADE  8 00008B  C 734ACD  E 3910A4
WHIRL_ROLES = whirlpool_art.Roles(
    hole_deep=0x8, hole_mid=0xE,
    trough=0x5, trough_lit=0x4,
    rim=0xC,
    foam_fringe=0x3, foam=0x2, foam_hot=0x1)


def parse_jasc(path):
    lines = path.read_text().splitlines()
    return [tuple(int(v) for v in ln.split()) for ln in lines[3:3 + 16]]


def main():
    # Resolved rather than guessed: symbol names do not reliably give the
    # directory, and seven tilesets share assets across directories.
    R = TilesetResolver(REPO)
    uw = R.resolve('gTileset_Underwater')
    mt_path, at_path = uw['metatiles'], uw['attributes']

    mt = bytearray(mt_path.read_bytes())
    at = bytearray(at_path.read_bytes())
    count = len(mt) // 16

    if count > BASE_COUNT:
        print(f'trimming {count - BASE_COUNT} previously appended metatiles')
        del mt[BASE_COUNT * 16:]
        del at[BASE_COUNT * 2:]
    elif count != BASE_COUNT:
        raise SystemExit(f'unexpected metatile count {count}')

    # The seafloor's own bottom layer, so the disc sits on the ground the player
    # is walking across and the corners outside it are simply more floor.
    i = FLOOR - 0x200
    floor = list(struct.unpack('<8H', mt[i * 16:(i + 1) * 16]))[:4]
    top = [(NUM_TILES_IN_PRIMARY + t) | (WHIRL_PALETTE << 12) for t in WHIRL_TILES]

    j = ATTR_DONOR - 0x200
    attr = struct.unpack('<H', at[j * 2:j * 2 + 2])[0]

    gid = 0x200 + BASE_COUNT
    mt += struct.pack('<8H', *(floor + top))
    at += struct.pack('<H', attr)
    mt_path.write_bytes(bytes(mt))
    at_path.write_bytes(bytes(at))
    print(f'  0x{gid:03X}  whirlpool            attr 0x{attr:04X} '
          f'(behavior 0x{attr & 0x1FF:02X}, layer {(attr >> 12) & 0xF})')
    print(f'{len(mt) // 16} metatiles now in Underwater '
          f'({0x400 - 0x200 - len(mt) // 16} slots still free)')

    # resolve() hands back all sixteen palette files indexed directly, for a
    # secondary as much as for a primary, so this is palette B and not an offset.
    pal = parse_jasc(uw['palettes'][WHIRL_PALETTE])

    whirlpool_art.write_sheet_tiles(uw['tiles'], WHIRL_TILES, WHIRL_ROLES)
    n = whirlpool_art.write_frames(uw['tiles'].parent / 'anim' / 'whirlpool',
                                   WHIRL_ROLES, pal)
    print(f'whirlpool: frame 0 into Underwater tiles '
          f'0x{WHIRL_TILES[0]:03X}-0x{WHIRL_TILES[-1]:03X}, {n} frames written')
    print(f'\nset UNDERWATER_METATILE_STAIRS to 0x{gid:03X}')


if __name__ == '__main__':
    sys.exit(main())
