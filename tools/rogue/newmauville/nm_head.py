"""A real column head - two NEW metatiles, still no new pixels.

Vanilla has no piece for a column that ends inside a wall mass, because vanilla
places its walls by hand and they always terminate against something. Every id
I tried was wrong for a different reason:

    0x268 / 0x26A   capped stripe, but the other half is EMPTY - 100% black
                    across its top rows, identical to the void. Painting it
                    changed nothing on screen.
    0x295 / 0x296   opaque, but they carry half a tan wall - corner pieces,
                    so they read as a block stuck to the column.

The piece needed is 0x268's capped stripe joined to 0x21F's white top surface:

    0x21F top = [t20B, t20B, t212, t213]   the cap row's own surface
    0x268 top = [----, t20B, ----, t216]   the capped stripe, left half empty
    NEW   top = [t20B, t20B, t212, t216]   surface on the left, cap on the right

t20B, t212, t213 and t216 all already exist, so this is a splice: two new
metatile entries, zero new 8x8 tiles.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from PIL import Image, ImageDraw
import nm_proto as P
import nm_v2 as V
import theme_mock as tm

OUT = HERE / 'nm_out'
CUR = tm.THEMES['newmauville']
E, meta, BLK = P.E, P.meta, P.BLK

HEAD_E, HEAD_W = 0x2F8, 0x2F9          # column head, stripe east / stripe west
meta(HEAD_E, (BLK,) * 4,
     (E(0x20B), E(0x20B), E(0x212), E(0x216)))
meta(HEAD_W, (BLK,) * 4,
     (E(0x20B), E(0x20B), E(0x216, xf=True), E(0x213)))

HEAD = {0x270: HEAD_E, 0x272: HEAD_W}
FOOT = {0x270: 0x278, 0x272: 0x27A}
VOID = 0x208


def cap_columns(grid, solid, w, h, head=None):
    head = head or HEAD

    def is_wall(x, y):
        return True if not (0 <= x < w and 0 <= y < h) else solid[y][x]

    out = [r[:] for r in grid]
    for x in range(w):
        y = 0
        while y < h:
            m = grid[y][x]
            if m not in FOOT:
                y += 1
                continue
            y0 = y
            while y + 1 < h and grid[y + 1][x] == m:
                y += 1
            if y0 > 0 and is_wall(x, y0 - 1) and grid[y0 - 1][x] == VOID:
                out[y0 - 1][x] = head[m]
            if y + 1 < h and is_wall(x, y + 1) and grid[y + 1][x] == VOID:
                out[y + 1][x] = FOOT[m]
            y += 1
    return out


def black_top(mid):
    """fraction of the top 4 pixel rows that are pure black"""
    px = P.render_metatile(mid, 1).load()
    return sum(1 for y in range(4) for x in range(16)
               if px[x, y] == (0, 0, 0)) / 64.0


if __name__ == '__main__':
    print('black in the top 4 pixel rows:')
    for m, n in ((0x208, 'void'), (0x268, '0x268 shipped'), (0x295, '0x295'),
                 (HEAD_E, 'NEW head')):
        print(f'   {n:<16} {black_top(m)*100:5.1f}%')

    w, h = 13, 12
    s = [[True] * w for _ in range(h)]
    for y in range(4, 9):
        for x in range(2, 11):
            s[y][x] = False
    for y in range(4, 9):
        s[y][6] = True
    t = {k: v for k, v in CUR.items() if k != 'decor'}
    base = V.paint(s, t, w, h, 0, 'full')

    def draw(g, S=5):
        im = Image.new('RGB', (w * 16, h * 16))
        for y in range(h):
            for x in range(w):
                im.paste(P.render_metatile(g[y][x]), (x * 16, y * 16))
        return im.resize((w * 16 * S, h * 16 * S), Image.NEAREST)

    panels = [('original: column cut into black', draw(base)),
              ('0x268 - what I shipped (no change)',
               draw(cap_columns(base, s, w, h, {0x270: 0x268, 0x272: 0x26A}))),
              ('NEW spliced head', draw(cap_columns(base, s, w, h)))]
    pad, hdr = 12, 20
    out = Image.new('RGB', (pad + len(panels) * (panels[0][1].width + pad),
                            hdr + panels[0][1].height + pad * 2), (24, 24, 30))
    d = ImageDraw.Draw(out)
    x = pad
    for lbl, im in panels:
        d.text((x, pad), lbl, fill=(215, 215, 225))
        out.paste(im, (x, pad + hdr))
        x += im.width + pad
    out.save(OUT / 'head_real.png')
    print('wrote', OUT / 'head_real.png')
