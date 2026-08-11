"""New Mauville v7 - free-standing floor clutter.

A third category, after wall decorations and wall-attached set pieces: solid
objects standing in open floor, touching no wall. Vanilla scatters crate stacks
like this all over, and we had none - every object we place is welded to a wall.

Shapes taken from vanilla's own clusters of 0x2C0..0x2C3.

Like the set pieces these are collision, so each placement is connectivity
checked. Unlike them they also need a clear ring, or a crate lands flush
against a wall and reads as part of it.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from PIL import Image, ImageDraw
import nm_v2 as V
import nm_v4 as V4
import nm_v6 as V6
import theme_mock as tm

OUT = HERE / 'nm_out'
CUR = tm.THEMES['newmauville']

CRATES = [
    [[0x2C0]],
    [[0x2C0, 0x2C0]],
    [[0x2C1, 0x2C0]],
    [[0x2C1, 0x2C0], [0x2C0, 0x2C0]],
    [[0x2C3], [0x2C2]],
]


def place_clutter(grid, solid, w, h, seed, n=6):
    placed = 0
    for k in range(n * 6):
        if placed >= n:
            break
        hsh = tm.decor_hash(seed, 31 + k, 17 + k)
        rows = CRATES[hsh % len(CRATES)]
        ph, pw = len(rows), len(rows[0])
        x = (hsh >> 4) % max(w - pw - 2, 1) + 1
        y = (hsh >> 12) % max(h - ph - 2, 1) + 1
        # every cell of the stack, and a one-cell ring around it, must be floor
        ok = True
        for j in range(-1, ph + 1):
            for i in range(-1, pw + 1):
                cx, cy = x + i, y + j
                if not (0 <= cx < w and 0 <= cy < h) or solid[cy][cx]:
                    ok = False
                    break
            if not ok:
                break
        if not ok:
            continue
        probe = [r[:] for r in solid]
        for j in range(ph):
            for i in range(pw):
                probe[y + j][x + i] = True
        if not V6._one_piece(probe, w, h):
            continue
        for j in range(ph):
            for i in range(pw):
                grid[y + j][x + i] = rows[j][i]
                solid[y + j][x + i] = True
        placed += 1
    return grid, solid, placed


def paint(solid, w, h, seed=0, rarity=60, clutter=6):
    grid, solid, pieces = V6.paint(solid, w, h, seed, rarity)
    grid = [r[:] for r in grid]
    solid = [r[:] for r in solid]
    grid, solid, n = place_clutter(grid, solid, w, h, seed, clutter)
    return grid, solid, pieces, n


if __name__ == '__main__':
    panels = []
    for seed in (11, 29, 5):
        tm.W, tm.H = 48, 48
        s, _ = tm.carve(seed, CUR)
        g, s2, pieces, n = paint(s, 48, 48, seed)
        ok = V6._one_piece(s2, 48, 48)
        print(f'seed {seed}: set pieces {{k: len(v) for k, v in pieces.items()}} '
              f'crates {n}  connectivity {ok}'.replace('{k: len(v) for k, v in pieces.items()}',
                                                        str({k: len(v) for k, v in pieces.items()})))
        panels.append((f'seed {seed}  crates {n}', V.draw(g, 48, 48, 1)))
    W = 48 * 16
    out = Image.new('RGB', (len(panels) * (W + 12) + 12, W + 30), (24, 24, 30))
    d = ImageDraw.Draw(out)
    for i, (lbl, im) in enumerate(panels):
        d.text((12 + i * (W + 12), 6), lbl, fill=(215, 215, 225))
        out.paste(im, (12 + i * (W + 12), 24))
    out.save(OUT / 'v7_floor.png')
    print('wrote', OUT / 'v7_floor.png')
