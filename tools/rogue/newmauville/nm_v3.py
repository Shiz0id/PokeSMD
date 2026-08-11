"""New Mauville v3 - wall assembly plus three-row decorations.

A wall decoration is not one metatile. Censused over both vanilla layouts, each
occupies a cap row, the wall face, and the floor below it, and the bookcase is
two of those side by side:

    cap    0x299 0x29A        0x21F         0x21F
    face   0x2A1 0x2A2        0x2B4         0x2B3
    floor  0x2A9 0x2AA        0x2BC         0x2BB
           bookcase (2 wide)  counter       crate unit

The theme paints the face row only, so every bookcase is headless and footless.
A stamp only lands where all of its cells are available, which is what keeps it
off corners and out of one-cell walls.
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
VOID, CAP, FACE = 0x208, 0x21F, 0x227

# (width, needs, cap row, face row, floor row).
#   needs 'cap'   - the cell above must be a capped wall (a 2+ thick wall)
#   needs 'floor' - the cell above must be floor (a one-thick partition)
# None in the cap row means the piece has no special top; leave the cap.
#
# The vent is 'floor': vanilla only ever puts it on a one-thick partition
# (above it is 0x26F, passable, 100%). Placed on a thick wall it floats in the
# middle of the band, because its art is drawn against the floor above it.
STAMPS = [
    (2, 'cap',   (0x299, 0x29A), (0x2A1, 0x2A2), (0x2A9, 0x2AA)),  # bookcase
    (1, 'cap',   (None,),        (0x2B4,),       (0x2BC,)),         # counter
    (1, 'cap',   (None,),        (0x2B3,),       (0x2BB,)),         # crate
    (1, 'floor', (None,),        (0x277,),       (None,)),          # vent
]


def decorate(grid, solid, w, h, seed, rarity=12):
    def is_wall(x, y):
        return True if not (0 <= x < w and 0 <= y < h) else solid[y][x]

    out = [r[:] for r in grid]
    taken = set()
    for y in range(h):
        for x in range(w):
            hsh = tm.decor_hash(seed, x, y)
            if hsh % rarity:
                continue
            fits = []
            for wd, needs, cap, face, flr in STAMPS:
                ok = True
                for i in range(wd):
                    cx = x + i
                    # the face row must be plain wall face and the floor row
                    # must be floor
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


def paint(solid, theme, w, h, seed=0, mode='full'):
    # the old single-row decor pass is replaced wholesale, not layered under -
    # leaving it on puts a second, differently-aligned set of the same objects
    # on the wall
    if mode not in ('none',):
        theme = {k: v for k, v in theme.items() if k != 'decor'}
    grid = V.paint(solid, theme, w, h, seed, 'full' if mode != 'none' else 'none')
    if mode in ('none', 'wall'):
        return grid
    return decorate(grid, solid, w, h, seed)


def draw(grid, w, h, scale):
    return V.draw(grid, w, h, scale)


if __name__ == '__main__':
    tm.W, tm.H = 48, 48
    s, _ = tm.carve(11, CUR)
    panels = [('current', paint(s, CUR, 48, 48, 11, 'none')),
              ('v3 wall + 3-row decor', paint(s, CUR, 48, 48, 11, 'full'))]
    big = Image.new('RGB', (48 * 16 * 2 + 36, 48 * 16 + 30), (24, 24, 30))
    dd = ImageDraw.Draw(big)
    for i, (lbl, g) in enumerate(panels):
        dd.text((12 + i * (48 * 16 + 12), 6), lbl, fill=(215, 215, 225))
        big.paste(draw(g, 48, 48, 1), (12 + i * (48 * 16 + 12), 24))
    big.save(OUT / 'v3_floor.png')
    print('wrote', OUT / 'v3_floor.png')

    # a wall run long enough to show several stamps, zoomed
    w, h = 20, 9
    sol = [[True] * w for _ in range(h)]
    for y in range(4, 8):
        for x in range(2, 18):
            sol[y][x] = False
    g = paint(sol, CUR, w, h, 3, 'full')
    draw(g, w, h, 4).save(OUT / 'v3_decor_zoom.png')
    print('wrote', OUT / 'v3_decor_zoom.png')
