"""
Compose and append the 1-wide wall metatiles the Fiery Path theme needs.

Lavaridge, like Cave, has no art for a wall one block thick - vanilla never
draws one. Unlike Cave, every edge here is a TOP-layer overlay over a shared
bumpy base (0x306/0x307/0x30E/0x30F all share the same bottom four tiles), so
every sliver is a clean quadrant splice: take a quadrant's bottom AND top from
the metatile that already draws that corner correctly.

The horizontal sliver is NOT composed: 0x30C is native, used by vanilla with
100% consistency wherever floor sits both north and south of a wall.

Appends after the 441 vanilla metatiles:

    0x3B9 sliver vertical        west edge + east edge
    0x3BA sliver vert top        + north corners
    0x3BB sliver vert bottom     + south face corners
    0x3BC sliver horz left cap   0x30C closed with NW+SW corners
    0x3BD sliver horz right cap  0x30C closed with NE+SE corners
    0x3BE sliver isolated        all four corners

Idempotent: trims any previous append before re-appending.

Run:  python3 tools/rogue/make_fiery_slivers.py   (no PIL needed to append;
      preview PNG is written only if PIL is available, so run from Windows to
      get the picture)
"""
import struct
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LAVARIDGE = REPO / 'data/tilesets/secondary/lavaridge'

BASE_COUNT = 441                     # vanilla Lavaridge metatile count
ATTR_DONOR = 0x271                   # ordinary wall interior

TL, TR, BL, BR = 0, 1, 2, 3

INTERIOR_L, INTERIOR_R = 0x306, 0x307    # floor west / floor east
NORTH_L, NORTH_R = 0x30A, 0x30B          # north-west / north-east corner
FACE_L, FACE_R = 0x30E, 0x30F            # south-west / south-east corner
HORZ = 0x30C                             # native 1-wide horizontal


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


def compose(mt):
    vert = splice(mt, tl=(INTERIOR_L, TL), tr=(INTERIOR_R, TR),
                      bl=(INTERIOR_L, BL), br=(INTERIOR_R, BR))
    vert_top = splice(mt, tl=(NORTH_L, TL), tr=(NORTH_R, TR),
                          bl=(INTERIOR_L, BL), br=(INTERIOR_R, BR))
    vert_bot = splice(mt, tl=(INTERIOR_L, TL), tr=(INTERIOR_R, TR),
                          bl=(FACE_L, BL), br=(FACE_R, BR))
    horz_l = splice(mt, tl=(NORTH_L, TL), tr=(HORZ, TR),
                        bl=(FACE_L, BL), br=(HORZ, BR))
    horz_r = splice(mt, tl=(HORZ, TL), tr=(NORTH_R, TR),
                        bl=(HORZ, BL), br=(FACE_R, BR))
    iso = splice(mt, tl=(NORTH_L, TL), tr=(NORTH_R, TR),
                     bl=(FACE_L, BL), br=(FACE_R, BR))
    return [('sliver vertical', vert), ('sliver vert top', vert_top),
            ('sliver vert bottom', vert_bot), ('sliver horz left', horz_l),
            ('sliver horz right', horz_r), ('sliver isolated', iso)]


def main():
    mt_path = LAVARIDGE / 'metatiles.bin'
    at_path = LAVARIDGE / 'metatile_attributes.bin'
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

    attr = at[ATTR_DONOR * 2 - 0x200 * 2:][:2]
    for i, (name, ent) in enumerate(new):
        mt += struct.pack('<8H', *ent)
        at += attr
        print(f'  0x{0x200 + BASE_COUNT + i:03X}  {name}')
    mt_path.write_bytes(bytes(mt))
    at_path.write_bytes(bytes(at))
    print(f'{len(mt) // 16} metatiles now in Lavaridge')

    try:
        from PIL import Image, ImageDraw
        import tileset_atlas as ta
        from tileset_resolve import TilesetResolver
        R = TilesetResolver(REPO)
        pair = ta.TilesetPair(ta.Tileset(R.resolve('gTileset_General')),
                              ta.Tileset(R.resolve('gTileset_Lavaridge')))
        S, cw, ch = 5, 16 * 5 + 12, 16 * 5 + 22
        im = Image.new('RGB', ((len(new) + 1) * cw, ch), (28, 28, 34))
        d = ImageDraw.Draw(im)
        for i, (name, _) in enumerate([('0x30C native', None)] + new):
            gid = HORZ if i == 0 else 0x200 + BASE_COUNT + i - 1
            im.paste(ta.render_metatile(pair, gid, S), (i * cw + 6, 6))
            d.text((i * cw + 6, 10 + 16 * S), name[:16], fill=(220, 220, 235))
        out = ta.OUTDIR / '_fiery_slivers.png'
        im.save(out)
        print(f'preview: {out}')
    except ImportError:
        print('no PIL here - run from Windows for the preview')


if __name__ == '__main__':
    main()
