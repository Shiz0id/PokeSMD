"""New Mauville v4 - the full decoration set, at vanilla's density.

Vanilla puts a decoration on 59% of its visible wall faces, drawn from 30
distinct metatiles. v3 had four stamps at 7%, so raising density alone would
just repeat the same bookcase. This adds the rest, each censused for its own
cap row and floor row.

0x2A0 is left out deliberately: it renders as flat brown, which is wall mass,
not furniture. Guessing it in would put a hole in the wall.
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
NODECOR = {k: v for k, v in CUR.items() if k != 'decor'}
VOID, CAP, FACE = 0x208, 0x21F, 0x227

# (width, needs, cap row, face row, floor row); None = leave that cell alone
STAMPS = [
    (2, 'cap',   (0x299, 0x29A), (0x2A1, 0x2A2), (0x2A9, 0x2AA)),  # bookcase
    (2, 'cap',   (None, None),   (0x2D6, 0x2D7), (0x2DE, 0x2DF)),  # console
    (2, 'cap',   (0x2DC, 0x2DD), (0x2E4, 0x2E5), (None, None)),    # machine
    (2, 'cap',   (0x2A8, None),  (0x2B0, 0x2B1), (0x2B8, 0x2B9)),  # crate shelf
    (1, 'cap',   (None,),        (0x2B2,),       (0x2BA,)),         # box shelf
    (1, 'cap',   (None,),        (0x2B4,),       (0x2BC,)),         # counter
    (1, 'cap',   (None,),        (0x2B3,),       (0x2BB,)),         # crate unit
    (1, 'floor', (None,),        (0x277,),       (None,)),          # vent
]


def decorate(grid, solid, w, h, seed, rarity):
    def is_wall(x, y):
        return True if not (0 <= x < w and 0 <= y < h) else solid[y][x]

    out = [r[:] for r in grid]
    taken = set()
    for y in range(h):
        for x in range(w):
            hsh = tm.decor_hash(seed, x, y)
            # a percentage gate rather than a modulo: the modulo form cannot
            # reach vanilla's density, because most of the cells it selects
            # cannot host a stamp at all, so the rate it controls is not the
            # rate you see on the wall
            if (hsh >> 3) % 100 >= rarity:
                continue
            fits = []
            for wd, needs, cap, face, flr in STAMPS:
                ok = True
                for i in range(wd):
                    cx = x + i
                    if (cx, y) in taken or not (0 <= cx < w):
                        ok = False; break
                    if grid[y][cx] != FACE or is_wall(cx, y + 1):
                        ok = False; break
                    if needs == 'floor' and is_wall(cx, y - 1):
                        ok = False; break
                    if needs == 'cap' and out[y - 1][cx] != CAP:
                        ok = False; break
                if ok:
                    fits.append((wd, cap, face, flr))
            if not fits:
                continue
            wd, cap, face, flr = fits[(hsh >> 8) % len(fits)]
            for i in range(wd):
                cx = x + i
                if cap[i] is not None:
                    out[y - 1][cx] = cap[i]
                out[y][cx] = face[i]
                if flr[i] is not None:
                    out[y + 1][cx] = flr[i]
                taken.add((cx, y))
    return out


def paint(solid, w, h, seed=0, rarity=45, walls=True):
    grid = V.paint(solid, NODECOR, w, h, seed, 'full' if walls else 'none')
    return decorate(grid, solid, w, h, seed, rarity) if walls else grid


PLAIN = {0x227, 0x293, 0x294, 0x295, 0x296, 0x21F, 0x208, 0x270, 0x272,
         0x280, 0x282, 0x290, 0x288, 0x298}


def coverage(rarity, n=25):
    faces = dec = 0
    for seed in range(n):
        tm.W, tm.H = 48, 48
        s, _ = tm.carve(seed, CUR)
        g = paint(s, 48, 48, seed, rarity)
        for y in range(48):
            for x in range(48):
                if s[y][x] and not (y + 1 < 48 and s[y + 1][x]):
                    faces += 1
                    if g[y][x] not in PLAIN:
                        dec += 1
    return 100.0 * dec / max(faces, 1)


if __name__ == '__main__':
    print('vanilla is 59% of wall faces decorated\n')
    for r in (10, 20, 30, 45, 60, 80, 100):
        print(f'   gate {r:>3}% -> {coverage(r):.0f}% of faces decorated')
