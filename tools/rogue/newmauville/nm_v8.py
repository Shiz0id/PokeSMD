"""New Mauville v8 - column head and foot, and decor at vanilla density.

A vertical wall run does not just stop. Censused over maximal runs (not over
every cell of a run, which only tells you the run continues):

    above the first cell   0x268 / 0x26A   47% / 50%
    below the last cell    0x278 / 0x27A   27% / 36%

Both are extra cells that currently hold the void, so painting them also prunes
the black above and below every column.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from PIL import Image, ImageDraw
import nm_v2 as V
import nm_v4 as V4
import nm_v6 as V6
import nm_v7 as V7
import theme_mock as tm

OUT = HERE / 'nm_out'
CUR = tm.THEMES['newmauville']
TIGHT = dict(CUR, rooms=30, room_min=4, room_max=7)
VOID = 0x208

# 0x268/0x26A were the census winners at 47%/50% but their top third is PURE
# BLACK - vanilla uses them where the wall really does continue into a dark
# recess. Painting one above a column adds void rather than pruning it.
# 0x295/0x296 are the runners-up (33%/21%) and are opaque top to bottom.
HEAD = {0x270: 0x295, 0x272: 0x296}
FOOT = {0x270: 0x278, 0x272: 0x27A}


def cap_columns(grid, solid, w, h):
    """Terminate every maximal vertical run, top and bottom."""
    def is_wall(x, y):
        return True if not (0 <= x < w and 0 <= y < h) else solid[y][x]

    out = [r[:] for r in grid]
    for x in range(w):
        y = 0
        while y < h:
            m = grid[y][x]
            if m not in HEAD:
                y += 1
                continue
            y0 = y
            while y + 1 < h and grid[y + 1][x] == m:
                y += 1
            # head: the cell above the run, if it is wall and still void
            if y0 > 0 and is_wall(x, y0 - 1) and grid[y0 - 1][x] == VOID:
                out[y0 - 1][x] = HEAD[m]
            # foot: the cell below the run, likewise
            if y + 1 < h and is_wall(x, y + 1) and grid[y + 1][x] == VOID:
                out[y + 1][x] = FOOT[m]
            y += 1
    return out


def paint(solid, w, h, seed=0, rarity=100, clutter=6, theme=None):
    theme = theme or TIGHT
    grid = V.paint(solid, {k: v for k, v in theme.items() if k != 'decor'},
                   w, h, seed, 'full')
    grid = cap_columns(grid, solid, w, h)

    solid = [r[:] for r in solid]
    taken = {}
    for name, n in (('generator', 1), ('supercomputer', 2)):
        for k in range(n):
            grid, filled, at = V6.place(grid, solid, w, h, seed,
                                        V6.PIECES[name], 7 + k * 13 + len(name),
                                        set())
            if not filled:
                break
            for x, y in filled:
                solid[y][x] = True

    saved, V4.STAMPS = V4.STAMPS, V6.STAMPS
    try:
        grid = V4.decorate(grid, solid, w, h, seed, rarity)
    finally:
        V4.STAMPS = saved
    grid, solid, n = V7.place_clutter(grid, solid, w, h, seed, clutter)
    return grid, solid


PLAIN = {0x227, 0x293, 0x294, 0x295, 0x296, 0x21F, 0x208, 0x270, 0x272,
         0x280, 0x282, 0x290, 0x288, 0x298, 0x268, 0x26A, 0x278, 0x27A}


def stats(n=30, rarity=100):
    gv = gt = faces = dec = voidtop = 0
    for seed in range(n):
        tm.W, tm.H = 48, 48
        s, _ = tm.carve(seed, TIGHT)
        g, s2 = paint(s, 48, 48, seed, rarity)
        for y in range(48):
            for x in range(48):
                gt += 1
                if g[y][x] == VOID:
                    gv += 1
                if s2[y][x] and (y + 1 >= 48 or not s2[y + 1][x]):
                    faces += 1
                    if g[y][x] not in PLAIN:
                        dec += 1
                # a column cell with raw void directly above it
                if g[y][x] in (0x270, 0x272) and y and g[y - 1][x] == VOID:
                    voidtop += 1
    return 100 * gv / gt, 100 * dec / max(faces, 1), voidtop


if __name__ == '__main__':
    print('vanilla: void 24.1%   decor 59%   bare column tops 0')
    for r in (60, 80, 100):
        v, d, vt = stats(20, r)
        print(f'   gate {r:>3}%  ->  void {v:.1f}%   decor {d:.0f}%   '
              f'bare column tops {vt}')
