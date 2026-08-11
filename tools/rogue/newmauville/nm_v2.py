"""New Mauville wall, v2 - the whole rule, censused.

  corners   the column continues through them
              SE 0x270  SW 0x272  NE 0x280  NW 0x282
  cap row   a cap POSITION is geometric: solid, solid below, floor two below.
            Vanilla never declines to cap one, and 0x21F beats the turn pieces
            at both ends (35% vs 21%/15%), so the run is plain 0x21F throughout
            and there are no turns. Defining the position geometrically also
            caps the corner columns, which is what closes the top corners.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from PIL import Image, ImageDraw
import nm_proto as P
import theme_mock as tm

OUT = HERE / 'nm_out'
CUR = tm.THEMES['newmauville']
VOID = 0x208
CAP = 0x21F
CORNER = {'CORNER_OPEN_SE': 0x270, 'CORNER_OPEN_SW': 0x272,
          'CORNER_OPEN_NE': 0x280, 'CORNER_OPEN_NW': 0x282}


def themed(theme):
    t = {k: v for k, v in theme.items()}
    t['wall'] = dict(theme['wall'])
    t['wall'].update(CORNER)
    return t


def paint(solid, theme, w, h, seed=0, mode='full'):
    if mode != 'none':
        theme = themed(theme)
    tm.W, tm.H = w, h
    grid, _ = tm.paint(solid, theme, seed)
    if mode in ('none', 'corner'):
        return grid

    def is_wall(x, y):
        return True if not (0 <= x < w and 0 <= y < h) else solid[y][x]

    out = [r[:] for r in grid]
    for y in range(h):
        for x in range(w):
            # a cap position: this cell and the one below are wall, and the
            # cell two below is floor - so the wall's face is one row down and
            # this is its top surface
            if (is_wall(x, y) and is_wall(x, y + 1) and not is_wall(x, y + 2)
                    and grid[y][x] == VOID):
                out[y][x] = CAP
    return out


def draw(grid, w, h, scale):
    im = Image.new('RGB', (w * 16, h * 16))
    for y in range(h):
        for x in range(w):
            im.paste(P.render_metatile(grid[y][x]), (x * 16, y * 16))
    return im.resize((w * 16 * scale, h * 16 * scale), Image.NEAREST)


def one_room():
    w, h = 14, 12
    s = [[True] * w for _ in range(h)]
    for y in range(4, 9):
        for x in range(4, 10):
            s[y][x] = False
    return s, w, h


def corridors():
    w, h = 22, 12
    s = [[True] * w for _ in range(h)]
    for rr in (range(3, 6), range(7, 10)):
        for yy in rr:
            for x in range(2, 8):
                s[yy][x] = False
    for yy in range(3, 10):
        s[yy][12] = False
    for x in range(8, 18):
        s[6][x] = False
    for yy in range(3, 10):
        for x in range(15, 20):
            s[yy][x] = False
    s[6][10] = False
    return s, w, h


if __name__ == '__main__':
    rows = []
    for fx, scale in ((one_room, 3), (corridors, 2)):
        s, w, h = fx()
        t = dict(CUR); t.pop('decor', None)
        rows.append([(lbl, draw(paint(s, t, w, h, 0, m), w, h, scale))
                     for lbl, m in (('current', 'none'), ('v2', 'full'))])
    pad, hdr, gap = 12, 18, 16
    width = pad * 2 + max(sum(i.width for _, i in r) + gap for r in rows)
    height = pad + sum(hdr + max(i.height for _, i in r) + gap for r in rows)
    out = Image.new('RGB', (width, height), (24, 24, 30))
    d = ImageDraw.Draw(out)
    y = pad
    for r in rows:
        x = pad
        for label, im in r:
            d.text((x, y), label, fill=(215, 215, 225))
            out.paste(im, (x, y + hdr))
            x += im.width + gap
        y += hdr + max(i.height for _, i in r) + gap
    out.save(OUT / 'v2_fixture.png')
    print('wrote', OUT / 'v2_fixture.png')

    tm.W, tm.H = 48, 48
    s, _ = tm.carve(11, CUR)
    big = Image.new('RGB', (48 * 16 * 2 + 36, 48 * 16 + 30), (24, 24, 30))
    dd = ImageDraw.Draw(big)
    for i, (lbl, m) in enumerate((('before', 'none'), ('v2', 'full'))):
        dd.text((12 + i * (48 * 16 + 12), 6), lbl, fill=(215, 215, 225))
        big.paste(draw(paint(s, CUR, 48, 48, 11, m), 48, 48, 1),
                  (12 + i * (48 * 16 + 12), 24))
    big.save(OUT / 'v2_floor.png')
    print('wrote', OUT / 'v2_floor.png')
