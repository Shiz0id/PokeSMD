"""
Compose new metatiles for the 1-wide wall cases the cave tileset has no art for.

No pixel art needed: a metatile is 8 references to existing 8x8 tiles, so a
1-wide wall is the west-facing half of one existing metatile spliced to the
east-facing half of another.

Entry layout per layer is [TL, TR, BL, BR]; u16 is tile bits 0-9, xflip 10,
yflip 11, palette 12-15.
"""
import struct
from PIL import Image, ImageDraw
import tileset_atlas as ta
from tileset_atlas import OUTDIR
from tileset_resolve import TilesetResolver

R = TilesetResolver(ta.REPO)
PAIR = ta.TilesetPair(ta.Tileset(R.resolve('gTileset_General')),
                      ta.Tileset(R.resolve('gTileset_Cave')))
CAVE = R.resolve('gTileset_Cave')
MT = CAVE['metatiles'].read_bytes()

TL, TR, BL, BR = 0, 1, 2, 3


def entries(gid):
    """8 raw u16 entries of an existing secondary metatile."""
    i = gid - PAIR.num_tiles_primary
    return list(struct.unpack('<8H', MT[i * 16:(i + 1) * 16]))


def splice(**parts):
    """Build 8 entries by taking named quadrants from source metatiles.
    parts maps quadrant name -> (source_gid, source_quadrant)."""
    out = [0] * 8
    base = entries(parts.get('base', (0x211, None))[0]) if 'base' in parts else entries(0x211)
    out[:] = base
    for name, idx in (('tl', TL), ('tr', TR), ('bl', BL), ('br', BR)):
        if name in parts:
            gid, q = parts[name]
            src = entries(gid)
            out[idx] = src[q]           # bottom layer
            out[idx + 4] = src[q + 4]   # top layer
    return out


def render_entries(ent, scale=1):
    """Render arbitrary metatile entries, including ones not in the tileset."""
    img = Image.new('RGB', (16, 16), (255, 0, 255))
    px = img.load()
    for layer in (0, 1):
        for q in range(4):
            e = ent[layer * 4 + q]
            tile, pal = e & 0x3FF, (e >> 12) & 0xF
            xf, yf = bool(e & 0x400), bool(e & 0x800)
            rows = PAIR.tile_pixels(tile + PAIR.num_tiles_primary if tile < 0x200 else tile)
            # secondary metatiles index the combined space directly
            rows = PAIR.tile_pixels(e & 0x3FF)
            if rows is None:
                continue
            colors = PAIR.palette(pal)
            ox, oy = (q % 2) * 8, (q // 2) * 8
            for y in range(8):
                sy = 7 - y if yf else y
                for x in range(8):
                    sx = 7 - x if xf else x
                    ci = rows[sy][sx]
                    if layer == 1 and ci == 0:
                        continue
                    px[ox + x, oy + y] = colors[ci] if ci < len(colors) else (255, 0, 255)
    return img.resize((16 * scale, 16 * scale), Image.NEAREST) if scale != 1 else img


# --- the two new metatiles -------------------------------------------------
# vertical sliver: west edge on the left, east edge on the right
VERT = splice(tl=(0x210, TL), bl=(0x210, BL), tr=(0x212, TR), br=(0x212, BR))
# horizontal sliver: rock top on the upper half, wall face on the lower half
HORZ = splice(tl=(0x209, TL), tr=(0x209, TR), bl=(0x219, BL), br=(0x219, BR))

# End caps. A sliver that also has floor at one end needs that end closed;
# without these it falls through to a single-sided edge and stops abruptly.
#
# The north edge is drawn as a TOP-LAYER overlay over a base (see 0x220/0x222),
# so the vertical top cap keeps the sliver base and overlays both north corners.
VERT_TOP = [0x62C6, 0x62CB, 0x62D6, 0x62DB,   # sliver base
            0x62A0, 0x62A5, 0x62B0, 0x62B5]   # NW + NE overlay
VERT_BOT = [0x62E6, 0x62EB, 0x62F6, 0x62FB, 0, 0, 0, 0]  # face-west + face-east
HORZ_L   = [0x62A6, 0x62A9, 0x62F6, 0x62F9, 0, 0, 0, 0]
HORZ_R   = [0x62A8, 0x62AB, 0x62F8, 0x62FB, 0, 0, 0, 0]
# Isolated single block: closed on all four sides.
ISO      = [0x62E6, 0x62EB, 0x62F6, 0x62FB,
            0x62A0, 0x62A5, 0x62B0, 0x62B5]

NEW = {'1-wide vertical': VERT, '1-wide horizontal': HORZ}

# Order here defines the appended metatile ids; do not reorder.
APPEND_ORDER = [
    ('sliver vertical',    VERT),
    ('sliver horizontal',  HORZ),
    ('sliver vert top',    VERT_TOP),
    ('sliver vert bottom', VERT_BOT),
    ('sliver horz left',   HORZ_L),
    ('sliver horz right',  HORZ_R),
    ('sliver isolated',    ISO),
]


def main():
    print('composed entries:')
    for name, ent in NEW.items():
        print(f'  {name:20} ' + ' '.join(f'{v:04X}' for v in ent))

    S = 5
    cells = [('0x210 left', entries(0x210)), ('0x212 right', entries(0x212)),
             ('NEW vertical', VERT), ('0x209 north', entries(0x209)),
             ('0x219 face', entries(0x219)), ('NEW horizontal', HORZ),
             ('0x211 interior', entries(0x211))]
    cw, ch = 16 * S + 12, 16 * S + 22
    im = Image.new('RGB', (len(cells) * cw, ch), (28, 28, 34))
    d = ImageDraw.Draw(im)
    for i, (label, ent) in enumerate(cells):
        im.paste(render_entries(ent, S), (i * cw + 6, 6))
        d.text((i * cw + 6, 10 + 16 * S), label[:15], fill=(220, 220, 235))
    im.save(str(OUTDIR / '_new_metatiles.png'))
    print('saved', OUTDIR / '_new_metatiles.png', im.size)


if __name__ == '__main__':
    main()
