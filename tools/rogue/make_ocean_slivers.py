"""
Compose and append the one-block-thick rock metatiles the ocean theme needs.

gTileset_Mossdeep draws its sea rocks as a 3x3 nine slice, 0x338-0x34A, and
vanilla never draws one a single block thick - the smallest rock in the sea
routes is 2x2. A carved dungeon produces thin walls constantly, and without art
for them the generator falls back on the rock interior, which paints a bright
bar with no shoreline on either side. It reads as a rendering fault.

This is the Fiery Path case rather than the cave's, and cleaner than either.
All nine pieces share one identical bottom layer:

    41C6 41C7 41C7 41C6

and every edge is purely a TOP-layer overlay, laid out as a regular 6x6 tile
grid. So each sliver is four quadrant copies - take a quadrant's bottom AND top
from whichever nine-slice piece already draws that corner correctly. No
hand-built entries, no pixel art, and unlike the cave nothing has to rely on a
piece happening to be an overlay rather than standalone art.

Fiery Path needed six because 0x30C gave it a native horizontal. Mossdeep has
no native thin rock in either direction, so all seven slots are composed.

Appends after the 454 vanilla metatiles:

    0x3C6 sliver vertical       west edge  + east edge
    0x3C7 sliver horizontal     north edge + south edge
    0x3C8 sliver vert top       + north corners
    0x3C9 sliver vert bottom    + south corners
    0x3CA sliver horz left cap  + west corners
    0x3CB sliver horz right cap + east corners
    0x3CC sliver isolated       all four corners

THIS EDITS A VANILLA ASSET FILE. Pulling upstream changes to gTileset_Mossdeep
means taking upstream's metatiles.bin and re-running this. Idempotent: it trims
a previous append before rewriting.

Run:  python3 tools/rogue/make_ocean_slivers.py   (no PIL needed to append; the
      preview PNG is written only if PIL is available, so run it from Windows
      to get the picture)
"""
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from tileset_resolve import TilesetResolver

BASE_COUNT = 454                     # vanilla Mossdeep metatile count
ATTR_DONOR = 0x341                   # the rock interior

TL, TR, BL, BR = 0, 1, 2, 3

# The nine slice, read off the Mossdeep sea routes as a grid. A neighbour-mask
# census cannot find this - pooled with island shores and the map-edge barrier
# nothing beats 38% - but each piece alone is decisive for its own position.
NORTH_L, NORTH_M, NORTH_R = 0x338, 0x339, 0x33A
INTER_L, INTER_M, INTER_R = 0x340, 0x341, 0x342
FACE_L,  FACE_M,  FACE_R  = 0x348, 0x349, 0x34A


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


def compose(mt):
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

    return [('sliver vertical', vert), ('sliver horizontal', horz),
            ('sliver vert top', vert_top), ('sliver vert bottom', vert_bot),
            ('sliver horz left', horz_l), ('sliver horz right', horz_r),
            ('sliver isolated', iso)]


def main():
    # Resolved rather than guessed: symbol names do not reliably give the
    # directory, and seven tilesets share assets across directories.
    resolved = TilesetResolver(REPO).resolve('gTileset_Mossdeep')
    mt_path = resolved['metatiles']
    at_path = resolved['attributes']

    mt = bytearray(mt_path.read_bytes())
    at = bytearray(at_path.read_bytes())
    count = len(mt) // 16

    new = compose(mt)

    if count > BASE_COUNT:
        print(f'trimming {count - BASE_COUNT} previously appended metatiles')
        del mt[BASE_COUNT * 16:]
        del at[BASE_COUNT * 2:]
    elif count != BASE_COUNT:
        raise SystemExit(f'unexpected metatile count {count}')

    # Every sliver is still rock, so behaviour and layer type come from the
    # interior. Collision lives in the block, not here, so the generator stays
    # in charge of what is solid.
    attr = at[(ATTR_DONOR - 0x200) * 2:][:2]
    for i, (name, ent) in enumerate(new):
        mt += struct.pack('<8H', *ent)
        at += attr
        print(f'  0x{0x200 + BASE_COUNT + i:03X}  {name}')
    mt_path.write_bytes(bytes(mt))
    at_path.write_bytes(bytes(at))
    print(f'{len(mt) // 16} metatiles now in Mossdeep')

    try:
        from PIL import Image, ImageDraw
        import tileset_atlas as ta
        R = TilesetResolver(REPO)
        pair = ta.TilesetPair(ta.Tileset(R.resolve('gTileset_General')),
                              ta.Tileset(R.resolve('gTileset_Mossdeep')))
        S, cw, ch = 5, 16 * 5 + 12, 16 * 5 + 22
        shown = [('0x341 interior', INTER_M)] + [
            (name, 0x200 + BASE_COUNT + i) for i, (name, _) in enumerate(new)]
        im = Image.new('RGB', (len(shown) * cw, ch), (28, 28, 34))
        d = ImageDraw.Draw(im)
        for i, (name, gid) in enumerate(shown):
            im.paste(ta.render_metatile(pair, gid, S), (i * cw + 6, 6))
            d.text((i * cw + 6, 10 + 16 * S), name[:16], fill=(220, 220, 235))
        out = ta.OUTDIR / '_ocean_slivers.png'
        im.save(out)
        print(f'preview: {out}')
    except ImportError:
        print('no PIL here - run from Windows for the preview')


if __name__ == '__main__':
    main()
