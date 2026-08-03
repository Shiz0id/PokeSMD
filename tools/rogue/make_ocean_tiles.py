"""
Everything the ocean theme appends to gTileset_Mossdeep, in one place.

One script owns the whole append because the three groups share a single trim:
they are written after the 454 vanilla metatiles, and re-running has to remove
the previous append before rewriting it. Splitting them would leave two scripts
fighting over the same tail.

    0x3C6-0x3CC   seven one-block-thick rocks   (slivers)
    0x3CD-0x3D0   four concave corners          (the room corners)
    0x3D1         the whirlpool                 (the way down)

THIS EDITS VANILLA ASSET FILES - metatiles.bin, metatile_attributes.bin and
tiles.png under data/tilesets/secondary/mossdeep/. Pulling upstream changes to
that tileset means taking upstream's files and re-running this, not merging.
Idempotent: the metatile tail is trimmed and the tile slots are overwritten.


THE SLIVERS
-----------
Vanilla's smallest sea rock is 2x2 and a carved dungeon produces one-block walls
constantly. Without art for them the generator fell back on the rock interior,
which paints a bright bar with no shoreline on either side and reads as a
rendering fault.

All nine pieces of the rock nine slice share one identical bottom layer

    41C6 41C7 41C7 41C6

- animated sea, from the primary tileset's water range 0x1B0-0x1CD - and every
edge is purely a TOP-layer overlay laid out as a regular 6x6 tile grid. So each
sliver is four quadrant copies, taking a quadrant's bottom AND top from whichever
nine-slice piece already draws that corner correctly. No pixel art.


THE INSIDE CORNERS
------------------
The generator's WALL_CORNER_OPEN_* cases are the corners of a room: every
cardinal neighbour is rock and one diagonal is open water. The nine slice is
convex throughout, so all four were standing on the rock interior and the wall's
dark edge stopped dead at every room corner.

An inside corner has ONE job, and it is not to show water. It carries the dark
wall edge from one neighbour round to the other in a continuous L - here, from
FACE_MID's bottom band to INTERIOR_RIGHT's side band. Granite Cave does exactly
this with a native piece (0x21B, four dedicated tiles), and putting the ocean's
attempt beside it is what showed the ocean's had no dark edge in it at all.

These are vanilla's own, straight out of gTileset_General's cliff set, and they
were sitting in the scan the whole time: run the corner case over every Mossdeep
layout and the top answer for each is one of these four, at 27-43%. They were
misread once as land art because they are in the primary and look like plain
rock on a contact sheet. They are not composed and not drawn - only the palette
changes, from the cliff set's own to Mossdeep's rock palette, which is the same
recolouring vanilla applies to the nine slice.

Two earlier attempts, recorded because both looked plausible:

  - windows into the water-pocket block at columns 6-B. That art comes from a
    ROUNDED pocket, so its corner pixels are transparent; over the sea they
    became a blue bite, and a room corner may never show water at all - the open
    diagonal is a full block away and no edge of the block borders water.
  - the same windows over an opaque rock underlay. No water, but no dark edge
    either, so it was barely distinguishable from plain rock.


THE WHIRLPOOL
-------------
The way down used to be 0x14E, the sea routes' deep-water dive spot. It is a
flat darker square - fine as scenery, poor as the one thing on the floor the
player is looking for.

There is no whirlpool art anywhere in the repo, so this draws one: a two-armed
spiral over a dark throat, in PRIMARY palette 4, the sea's own palette. Using
the water's palette is what makes the vortex read as the same body of water
rather than a decal - the foam is the water's foam and the blues are its blues.
Index 0 is transparent, so outside the disc the animated sea shows through and
the whirlpool sits IN the water instead of on top of it.

Four frames, and the arms are two-fold symmetric, so a quarter turn per frame
is a seamless loop.

The four tiles must be CONSECUTIVE: AppendTilesetAnimToBuffer DMAs them to VRAM
in one run. A 16x16 frame PNG converts to four tiles in TL, TR, BL, BR order,
which is the order the metatile references them in.

The sheet copy is frame 0. It is overwritten by the animation within a few
frames of the map loading, and exists so the tileset is sane to look at and so
the map renders correctly before the first tick.

Behaviour is copied from 0x14E, so it stays MB_DEEP_WATER: surfable, which is
the whole reason a water floor is not a softlock. Nothing walkable may ever be
painted on this theme.

Run:  python3 tools/rogue/make_ocean_tiles.py
      Needs Pillow for the tile and frame art, so run it from Windows against
      //wsl.localhost/Ubuntu/... - WSL has no Pillow.
"""
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import whirlpool_art
from tileset_resolve import TilesetResolver

BASE_COUNT = 454                     # vanilla Mossdeep metatile count
NUM_TILES_IN_PRIMARY = 512

TL, TR, BL, BR = 0, 1, 2, 3

# The convex nine slice, read off the sea routes as a grid. A neighbour-mask
# census cannot find this - pooled with island shores and the map-edge barrier
# nothing beats 38% - but each piece alone is decisive for its own position.
NORTH_L, NORTH_M, NORTH_R = 0x338, 0x339, 0x33A
INTER_L, INTER_M, INTER_R = 0x340, 0x341, 0x342
FACE_L,  FACE_M,  FACE_R  = 0x348, 0x349, 0x34A

ATTR_INTERIOR = 0x341        # plain rock: COVERED, draws below the player
ATTR_STAIRS   = 0x14E        # MB_DEEP_WATER, the dive spot being replaced

# The cliff set's inside corners, lifted whole out of the vanilla metatiles that
# the corner-case scan names for each diagonal. Named for the OPEN diagonal, the
# convention the wall table uses. Note NW and NE are not mirrors of anything -
# they mix the water-pocket block with one tile of the convex block - which is
# why guessing at windows never landed on them.
CORNERS = (
    ('corner open SE', (0x04C, 0x04D, 0x05C, 0x05D)),   # vanilla 0x074, 32.8%
    ('corner open SW', (0x04E, 0x04F, 0x05E, 0x05F)),   # vanilla 0x089, 42.9%
    ('corner open NW', (0x084, 0x08B, 0x09A, 0x09B)),   # vanilla 0x07D, 27.0%
    ('corner open NE', (0x086, 0x081, 0x096, 0x097)),   # vanilla 0x07B, 33.1%
)
CORNER_PALETTE = 0xB         # Mossdeep's rock palette, as the nine slice uses

# Four consecutive blank, unreferenced Mossdeep tile slots, inside row 14 so the
# run does not wrap the sheet. Measured, not assumed: 258 of the 512 slots are
# blank AND referenced by no metatile.
WHIRL_TILES = (0x0E9, 0x0EA, 0x0EB, 0x0EC)
WHIRL_PALETTE = 4            # the primary sea palette
WHIRL_FRAMES = whirlpool_art.FRAMES

# Palette 4 by role.  0 182952 is the transparent index.
#   1 FFFFFF  2 DEE6EE  7 526AD5  8 415ABD  9 39529C  A 29418B  B ACC5E6  C 8BA4DE
WHIRL_ROLES = whirlpool_art.Roles(
    hole_deep=0xA, hole_mid=0x9,
    trough=0x8, trough_lit=0x7,
    rim=0xC,
    foam_fringe=0xB, foam=0x2, foam_hot=0x1)


# ---------------------------------------------------------------- composition

def entries(mt, gid):
    i = gid - 0x200
    return list(struct.unpack('<8H', mt[i * 16:(i + 1) * 16]))


def splice(mt, **parts):
    """Each quadrant, bottom and top layer together, from a named source."""
    out = [0] * 8
    for name, idx in (('tl', TL), ('tr', TR), ('bl', BL), ('br', BR)):
        gid, q = parts[name]
        src = entries(mt, gid)
        out[idx] = src[q]
        out[idx + 4] = src[q + 4]
    return out


def compose_slivers(mt):
    vert = splice(mt, tl=(INTER_L, TL), tr=(INTER_R, TR),
                      bl=(INTER_L, BL), br=(INTER_R, BR))
    horz = splice(mt, tl=(NORTH_M, TL), tr=(NORTH_M, TR),
                      bl=(FACE_M, BL),  br=(FACE_M, BR))
    vert_top = splice(mt, tl=(NORTH_L, TL), tr=(NORTH_R, TR),
                          bl=(INTER_L, BL), br=(INTER_R, BR))
    vert_bot = splice(mt, tl=(INTER_L, TL), tr=(INTER_R, TR),
                          bl=(FACE_L, BL),  br=(FACE_R, BR))
    horz_l = splice(mt, tl=(NORTH_L, TL), tr=(NORTH_M, TR),
                        bl=(FACE_L, BL),  br=(FACE_M, BR))
    horz_r = splice(mt, tl=(NORTH_M, TL), tr=(NORTH_R, TR),
                        bl=(FACE_M, BL),  br=(FACE_R, BR))
    iso = splice(mt, tl=(NORTH_L, TL), tr=(NORTH_R, TR),
                     bl=(FACE_L, BL),  br=(FACE_R, BR))

    return [('sliver vertical', vert, ATTR_INTERIOR),
            ('sliver horizontal', horz, ATTR_INTERIOR),
            ('sliver vert top', vert_top, ATTR_INTERIOR),
            ('sliver vert bottom', vert_bot, ATTR_INTERIOR),
            ('sliver horz left', horz_l, ATTR_INTERIOR),
            ('sliver horz right', horz_r, ATTR_INTERIOR),
            ('sliver isolated', iso, ATTR_INTERIOR)]


def compose_corners(mt):
    """The four room corners: vanilla's cliff pieces under Mossdeep's palette.

    They keep the nine slice's sea bottom layer so the whole ocean wall set has
    one shape, which is safe only because the art is fully opaque - checked.
    """
    sea = entries(mt, INTER_M)[:4]
    pal = CORNER_PALETTE << 12
    return [(name, sea + [t | pal for t in tiles], ATTR_INTERIOR)
            for name, tiles in CORNERS]


def compose_whirlpool(mt):
    sea = entries(mt, INTER_M)[:4]
    top = [(NUM_TILES_IN_PRIMARY + t) | (WHIRL_PALETTE << 12) for t in WHIRL_TILES]
    return [('whirlpool', sea + top, ATTR_STAIRS)]


def check_corners_opaque(tiles_path):
    """A room corner may not show one pixel of water: no edge of it borders any.
    The corner art sits over the sea like the rest of the set, so it has to be
    fully opaque for that to hold. Measured rather than assumed - the first two
    attempts at these corners both failed exactly here."""
    from PIL import Image
    px = Image.open(tiles_path).load()
    for name, tiles in CORNERS:
        for t in tiles:
            tx, ty = (t % 16) * 8, (t // 16) * 8
            holes = sum(1 for y in range(8) for x in range(8)
                        if px[tx + x, ty + y] == 0)
            if holes:
                raise SystemExit(f'{name}: tile 0x{t:03X} has {holes} '
                                 f'transparent pixels, so sea would show through')
    print(f'  all {len(CORNERS)} corners are fully opaque')


# ---------------------------------------------------------------------- main

def parse_jasc(path):
    lines = path.read_text().splitlines()
    return [tuple(int(v) for v in ln.split()) for ln in lines[3:3 + 16]]


def main():
    # Resolved rather than guessed: symbol names do not reliably give the
    # directory, and seven tilesets share assets across directories.
    R = TilesetResolver(REPO)
    moss = R.resolve('gTileset_Mossdeep')
    gen = R.resolve('gTileset_General')
    mt_path, at_path = moss['metatiles'], moss['attributes']

    mt = bytearray(mt_path.read_bytes())
    at = bytearray(at_path.read_bytes())
    count = len(mt) // 16

    if count > BASE_COUNT:
        print(f'trimming {count - BASE_COUNT} previously appended metatiles')
        del mt[BASE_COUNT * 16:]
        del at[BASE_COUNT * 2:]
    elif count != BASE_COUNT:
        raise SystemExit(f'unexpected metatile count {count}')

    # The corner art lives in the PRIMARY sheet, so check that one.
    check_corners_opaque(gen['tiles'])

    new = compose_slivers(mt) + compose_corners(mt) + compose_whirlpool(mt)

    # Every sliver and corner is still rock, so behaviour and layer type come
    # from the nine-slice piece each one belongs with. Collision lives in the
    # block, not here, so the generator stays in charge of what is solid.
    gen_attrs = gen['attributes'].read_bytes()
    for i, (name, ent, donor) in enumerate(new):
        src = gen_attrs if donor < 0x200 else at
        off = (donor if donor < 0x200 else donor - 0x200) * 2
        attr = struct.unpack('<H', src[off:off + 2])[0]
        mt += struct.pack('<8H', *ent)
        at += struct.pack('<H', attr)
        gid = 0x200 + BASE_COUNT + i
        print(f'  0x{gid:03X}  {name:<20} attr 0x{attr:04X} '
              f'(behavior 0x{attr & 0x1FF:02X}, layer {(attr >> 12) & 0xF})')

    mt_path.write_bytes(bytes(mt))
    at_path.write_bytes(bytes(at))
    print(f'{len(mt) // 16} metatiles now in Mossdeep '
          f'({0x400 - 0x200 - len(mt) // 16} slots still free)')

    # resolve() hands back the palette files in declaration order, so the index
    # is the palette number the metatile entry will name.
    sea = parse_jasc(gen['palettes'][WHIRL_PALETTE])

    whirlpool_art.write_sheet_tiles(moss['tiles'], WHIRL_TILES, WHIRL_ROLES)
    n = whirlpool_art.write_frames(moss['tiles'].parent / 'anim' / 'whirlpool',
                                   WHIRL_ROLES, sea)
    print(f'whirlpool: frame 0 into Mossdeep tiles '
          f'0x{WHIRL_TILES[0]:03X}-0x{WHIRL_TILES[-1]:03X}, {n} frames written')


if __name__ == '__main__':
    sys.exit(main())
