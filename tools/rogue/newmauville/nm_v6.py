"""New Mauville v6 - set pieces generalised.

Two objects in this tileset are not decorations at all. They are solid blocks
whose top row sits in the wall and whose body projects out over the floor:

    generator      4 wide x 4 tall   0x2D0.. / 0x2D8.. / 0x2E0.. / 0x2E8..
    supercomputer  2 wide x 3 tall   0x2D4,0x2D5 / 0x2DC,0x2DD / 0x2E4,0x2E5

Both are solid in every cell in vanilla. v4 had the supercomputer's bottom two
rows registered as a wall decoration with a cap, which drew it headless and
flush into the wall - the top row was never placed and the body never projected.

Placing either one turns floor into collision, so every placement is checked
for connectivity: a spot that looks fine can cut the floor in two and strand
the stairs, and nothing downstream would report it.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from PIL import Image, ImageDraw
import nm_proto as P
import nm_v2 as V
import nm_v4 as V4
import theme_mock as tm

OUT = HERE / 'nm_out'
CUR = tm.THEMES['newmauville']
CAP, FACE = 0x21F, 0x227

PIECES = {
    'generator': [[0x2D0, 0x2D1, 0x2D2, 0x2D3],
                  [0x2D8, 0x2D9, 0x2DA, 0x2DB],
                  [0x2E0, 0x2E1, 0x2E2, 0x2E3],
                  [0x2E8, 0x2E9, 0x2EA, 0x2EB]],
    'supercomputer': [[0x2D4, 0x2D5],
                      [0x2DC, 0x2DD],
                      [0x2E4, 0x2E5]],
}

# the supercomputer's lower two rows must come out of the decor table - they
# are two thirds of a set piece, not a decoration with a cap
STAMPS = [s for s in V4.STAMPS if s[3][0] != 0x2E4]


def _one_piece(solid, w, h):
    start = next(((x, y) for y in range(h) for x in range(w) if not solid[y][x]), None)
    if start is None:
        return True
    seen, st = {start}, [start]
    while st:
        cx, cy = st.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (cx + dx, cy + dy)
            if (0 <= n[0] < w and 0 <= n[1] < h and not solid[n[1]][n[0]]
                    and n not in seen):
                seen.add(n); st.append(n)
    return len(seen) == sum(1 for y in range(h) for x in range(w) if not solid[y][x])


def place(grid, solid, w, h, seed, rows, salt, taken):
    """Stamp one solid set piece. Top row replaces a run of wall face; the rest
    must currently be floor and becomes solid."""
    ph, pw = len(rows), len(rows[0])

    def is_wall(x, y):
        return True if not (0 <= x < w and 0 <= y < h) else solid[y][x]

    cands = []
    for y in range(1, h - ph - 1):
        for x in range(w - pw):
            ok = True
            for i in range(pw):
                if grid[y][x + i] != FACE or grid[y - 1][x + i] != CAP:
                    ok = False; break
                for j in range(1, ph):
                    if is_wall(x + i, y + j) or (x + i, y + j) in taken:
                        ok = False; break
                # one clear row beyond the body, so it is not flush against
                # the far wall of a shallow room
                if not ok or is_wall(x + i, y + ph):
                    ok = False; break
            if ok:
                cands.append((x, y))
    if not cands:
        return grid, set(), None

    start = tm.decor_hash(seed, salt, salt) % len(cands)
    for k in range(len(cands)):
        x, y = cands[(start + k) % len(cands)]
        filled = {(x + i, y + j) for j in range(1, ph) for i in range(pw)}
        probe = [r[:] for r in solid]
        for px, py in filled:
            probe[py][px] = True
        if _one_piece(probe, w, h):
            out = [r[:] for r in grid]
            for j in range(ph):
                for i in range(pw):
                    out[y + j][x + i] = rows[j][i]
            return out, filled, (x, y)
    return grid, set(), None


def paint(solid, w, h, seed=0, rarity=60, supers=2):
    grid = V.paint(solid, V4.NODECOR, w, h, seed, 'full')
    solid = [r[:] for r in solid]
    taken, placed = set(), {}

    for name, n in (('generator', 1), ('supercomputer', supers)):
        for k in range(n):
            grid, filled, at = place(grid, solid, w, h, seed,
                                     PIECES[name], 7 + k * 13 + len(name), taken)
            if not filled:
                break
            for x, y in filled:
                solid[y][x] = True
            taken |= filled
            placed.setdefault(name, []).append(at)

    saved, V4.STAMPS = V4.STAMPS, STAMPS
    try:
        grid = V4.decorate(grid, solid, w, h, seed, rarity)
    finally:
        V4.STAMPS = saved
    return grid, solid, placed


if __name__ == '__main__':
    panels = []
    for seed in (11, 29, 5):
        tm.W, tm.H = 48, 48
        s, _ = tm.carve(seed, CUR)
        g, s2, placed = paint(s, 48, 48, seed)
        n = {k: len(v) for k, v in placed.items()}
        panels.append((f'seed {seed}  {n}', V.draw(g, 48, 48, 1)))
        print(f'seed {seed}: {n}  connectivity ok = {_one_piece(s2, 48, 48)}')
    W = 48 * 16
    out = Image.new('RGB', (len(panels) * (W + 12) + 12, W + 30), (24, 24, 30))
    d = ImageDraw.Draw(out)
    for i, (lbl, im) in enumerate(panels):
        d.text((12 + i * (W + 12), 6), lbl, fill=(215, 215, 225))
        out.paste(im, (12 + i * (W + 12), 24))
    out.save(OUT / 'v6_floor.png')
    print('wrote', OUT / 'v6_floor.png')
