"""Compose and append the 1-wide wall metatiles the Mirage Tower theme needs.

gTileset_MirageTower is a pure art reskin of gTileset_Cave: 411 of its 414
metatiles are byte-identical and all 414 attributes are, so it inherits the
cave's problem along with the cave's layout - vanilla never draws a cave wall
one block thick, so there is no art for a sliver.

Because the metatile definitions match, the cave's splice recipe transfers
exactly, and the composed entries come out byte-identical to the cave's own
appended 0x39E-0x3A4. They render as sand rather than rock only because the
tile indices resolve against mirage_tower/tiles.png. The script composes from
Mirage Tower's own metatiles rather than copying the cave's bytes, so it stays
correct if either tileset is ever changed, and cross-checks against the cave
at the end when the cave has been composed too.

Appends after the 414 vanilla metatiles:

    0x39E sliver vertical        west edge + east edge
    0x39F sliver horizontal      wall top over wall face
    0x3A0 sliver vert top        + north corner overlay
    0x3A1 sliver vert bottom     + south face corners
    0x3A2 sliver horz left cap   west end closed
    0x3A3 sliver horz right cap  east end closed
    0x3A4 sliver isolated        closed on all four sides

Idempotent: trims any previous append before re-appending.

Run:  python3 tools/rogue/make_mirage_slivers.py   (no PIL needed to append;
      the preview PNG is written only if PIL is available, so run from Windows
      to get the picture)
"""
import struct
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MIRAGE = REPO / 'data/tilesets/secondary/mirage_tower'
CAVE = REPO / 'data/tilesets/secondary/cave'

BASE_COUNT = 414        # vanilla Mirage Tower metatile count
ATTR_DONOR = 0x201      # the wall interior; MB_CAVE, same as the cave's donor

TL, TR, BL, BR = 0, 1, 2, 3

# Wall pieces, read off MirageTower_1F/3F/4F. The north edge is a three-piece
# run - 0x208 west end, 0x209 middle, 0x20A east end - which is what closes the
# ends of a horizontal sliver.
INTERIOR_L, INTERIOR_R = 0x210, 0x212     # floor west / floor east
NORTH_CAP_L, NORTH_MID, NORTH_CAP_R = 0x208, 0x209, 0x20A
FACE_L, FACE_MID, FACE_R = 0x218, 0x219, 0x21A

# Drawn as TOP-LAYER overlays over a plain base, which is what lets the
# vertical top cap keep the sliver's own base and just add the corners.
NORTH_CORNER_L, NORTH_CORNER_R = 0x220, 0x222


def entries(mt, gid):
    i = gid - 0x200
    return list(struct.unpack('<8H', mt[i * 16:(i + 1) * 16]))


def splice(mt, **parts):
    """Each quadrant (bottom and top layer together) from a named source."""
    out = [0] * 8
    for name, idx in (('tl', TL), ('tr', TR), ('bl', BL), ('br', BR)):
        gid, q = parts[name]
        src = entries(mt, gid)
        out[idx] = src[q]
        out[idx + 4] = src[q + 4]
    return out


def overlay(base, mt, left, right):
    """Keep a spliced base, add two top-layer corners over it."""
    out = list(base)
    out[4 + TL] = entries(mt, left)[4 + TL]
    out[4 + BL] = entries(mt, left)[4 + BL]
    out[4 + TR] = entries(mt, right)[4 + TR]
    out[4 + BR] = entries(mt, right)[4 + BR]
    return out


def compose(mt):
    vert = splice(mt, tl=(INTERIOR_L, TL), tr=(INTERIOR_R, TR),
                      bl=(INTERIOR_L, BL), br=(INTERIOR_R, BR))
    horz = splice(mt, tl=(NORTH_MID, TL), tr=(NORTH_MID, TR),
                      bl=(FACE_MID, BL), br=(FACE_MID, BR))
    vert_bot = splice(mt, tl=(FACE_L, TL), tr=(FACE_R, TR),
                          bl=(FACE_L, BL), br=(FACE_R, BR))
    horz_l = splice(mt, tl=(NORTH_CAP_L, TL), tr=(NORTH_MID, TR),
                        bl=(FACE_L, BL), br=(FACE_MID, BR))
    horz_r = splice(mt, tl=(NORTH_MID, TL), tr=(NORTH_CAP_R, TR),
                        bl=(FACE_MID, BL), br=(FACE_R, BR))
    vert_top = overlay(vert, mt, NORTH_CORNER_L, NORTH_CORNER_R)
    iso = overlay(vert_bot, mt, NORTH_CORNER_L, NORTH_CORNER_R)

    # Order defines the appended ids; do not reorder.
    return [('sliver vertical', vert), ('sliver horizontal', horz),
            ('sliver vert top', vert_top), ('sliver vert bottom', vert_bot),
            ('sliver horz left', horz_l), ('sliver horz right', horz_r),
            ('sliver isolated', iso)]


def cross_check(new):
    """The cave's own appended slivers should be byte-identical."""
    cave_mt = (CAVE / 'metatiles.bin').read_bytes()
    if len(cave_mt) // 16 < BASE_COUNT + len(new):
        print('cave not composed yet - skipping cross-check')
        return
    for i, (name, ent) in enumerate(new):
        want = entries(cave_mt, 0x200 + BASE_COUNT + i)
        if want != ent:
            print(f'  NOTE: {name} differs from the cave version')
            print(f'    cave   ' + ' '.join(f'{v:04X}' for v in want))
            print(f'    mirage ' + ' '.join(f'{v:04X}' for v in ent))
            return
    print('cross-check: byte-identical to the cave slivers, as expected')


def main():
    mt_path = MIRAGE / 'metatiles.bin'
    at_path = MIRAGE / 'metatile_attributes.bin'
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

    attr = bytes(at[(ATTR_DONOR - 0x200) * 2:][:2])
    print(f'attribute donor 0x{ATTR_DONOR:03X} = 0x{attr[0] | attr[1] << 8:04X}')
    for i, (name, ent) in enumerate(new):
        mt += struct.pack('<8H', *ent)
        at += attr
        print(f'  0x{0x200 + BASE_COUNT + i:03X}  {name}')

    assert len(mt) // 16 == len(at) // 2, 'metatile/attribute count mismatch'
    mt_path.write_bytes(bytes(mt))
    at_path.write_bytes(bytes(at))
    print(f'{len(mt) // 16} metatiles now in MirageTower')
    cross_check(new)

    try:
        from PIL import Image, ImageDraw
        import tileset_atlas as ta
        from tileset_resolve import TilesetResolver
        R = TilesetResolver(REPO)
        pair = ta.TilesetPair(ta.Tileset(R.resolve('gTileset_General')),
                              ta.Tileset(R.resolve('gTileset_MirageTower')))
        S, cw, ch = 6, 16 * 6 + 12, 16 * 6 + 22
        cells = [('0x211 floor', 0x211), ('0x210 west', INTERIOR_L),
                 ('0x212 east', INTERIOR_R)]
        cells += [(name, 0x200 + BASE_COUNT + i) for i, (name, _) in enumerate(new)]
        im = Image.new('RGB', (len(cells) * cw, ch), (28, 28, 34))
        d = ImageDraw.Draw(im)
        for i, (name, gid) in enumerate(cells):
            im.paste(ta.render_metatile(pair, gid, S), (i * cw + 6, 6))
            d.text((i * cw + 6, 10 + 16 * S), name[:17], fill=(220, 220, 235))
        out = ta.OUTDIR / '_mirage_slivers.png'
        im.save(out)
        print(f'preview: {out}')
    except ImportError:
        print('no PIL here - run from Windows for the preview')


if __name__ == '__main__':
    main()
