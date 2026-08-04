"""
Prototype Petalburg Woods generation before committing any C.

Trees are 2x2 metatile blocks aligned to even coordinates, so the generator
works on a HALF RESOLUTION grid: 24x24 cells, each stamped as 2x2 metatiles.
That makes tree placement correct by construction and removes the need for the
9-case autotile table the cave floors need.
"""
from PIL import Image, ImageDraw
import tileset_atlas as ta
from tileset_atlas import OUTDIR
from tileset_resolve import TilesetResolver
from verify_seeding import DungeonRng

# metatile ids, from mining LAYOUT_PETALBURG_WOODS
GRASS = 0x001                          # plain, no encounters
TALL = 0x00D                           # MB_TALL_GRASS
HAS_LONG = False                       # the woods theme has no long grass
LONG = 0x015                           # MB_LONG_GRASS, kept for the RNG draw
TREE = (0x1D4, 0x1D5, 0x1DC, 0x1DD)   # TL, TR, BL, BR
TREE_BASE = (0x1E4, 0x1E5)             # ground contact, where a mass ends
LONG_BASE = (0x016, 0x017)             # NOT a long grass base - see the header
ABOVE_TREE = (0x1CE, 0x1CF)            # a tree's crown over plain grass
ABOVE_TREE_TALL = (0x1C6, 0x1C7)       # ... and over tall grass

# The decor pass, which the woods only started reaching once the three cosmetic
# passes were lifted out of ApplyWallAutotiling - the woods generator returns
# before that function ever runs and so had none of them.
#
# Mirrored here rather than eyeballed on hardware because decorRarity is the
# only knob and what it controls is a DENSITY: one variant repeated too often
# reads as wallpaper, too rarely as an accident. That is a thing to count, and
# counting it in an emulator means one floor at a time.
FLOWER_BUSH = 0x004                    # passable in all 53 vanilla placements
DECOR = [(GRASS, FLOWER_BUSH)]         # (base, variant), mirroring sWoodsDecor
DECOR_RARITY = 28

CELL_PLAIN, CELL_TALL, CELL_LONG, CELL_TREE = 0, 1, 2, 3
CELL_METATILE = {CELL_PLAIN: GRASS, CELL_TALL: TALL, CELL_LONG: LONG}

CELLS_W, CELLS_H = 24, 24             # 48x48 metatiles at 2x2 per cell
MAX_ROOMS, RMIN, RMAX = 8, 3, 5       # in cells, so 6-10 metatiles across

R = TilesetResolver(ta.REPO)
PAIR = ta.TilesetPair(ta.Tileset(R.resolve('gTileset_General')),
                      ta.Tileset(R.resolve('gTileset_Rustboro')))


def rooms_overlap(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return not (ax + aw < bx or bx + bw < ax or ay + ah < by or by + bh < ay)


def generate(seed):
    rng = DungeonRng(seed)
    open_ = [[False] * CELLS_W for _ in range(CELLS_H)]

    def carve(x, y):
        if 0 <= x < CELLS_W and 0 <= y < CELLS_H:
            open_[y][x] = True

    rooms = []
    for _ in range(64):
        if len(rooms) >= MAX_ROOMS:
            break
        w = RMIN + rng.next() % (RMAX - RMIN + 1)
        h = RMIN + rng.next() % (RMAX - RMIN + 1)
        x = 1 + rng.next() % (CELLS_W - w - 2)
        y = 1 + rng.next() % (CELLS_H - h - 2)
        r = (x, y, w, h)
        if not any(rooms_overlap(r, o) for o in rooms):
            rooms.append(r)

    for (x, y, w, h) in rooms:
        for dy in range(h):
            for dx in range(w):
                carve(x + dx, y + dy)

    for i in range(1, len(rooms)):
        x0, y0 = rooms[i-1][0] + rooms[i-1][2]//2, rooms[i-1][1] + rooms[i-1][3]//2
        x1, y1 = rooms[i][0] + rooms[i][2]//2, rooms[i][1] + rooms[i][3]//2
        x, y = x0, y0
        while x != x1:
            carve(x, y); x += 1 if x1 > x else -1
        while y != y1:
            carve(x, y); y += 1 if y1 > y else -1
        carve(x, y)

    # Grass patches, as blobs rather than per-cell noise - scattered single
    # cells read as static, whereas blobs read as undergrowth. Long grass is
    # rarer and only appears deeper in, so it stays a landmark.
    grass = [[CELL_PLAIN] * CELLS_W for _ in range(CELLS_H)]
    for (rx, ry, rw, rh) in rooms:
        for _ in range(rng.next() % 3):          # 0-2 patches per clearing
            # The draw happens either way, matching the C: a theme without long
            # grass must not shift the RNG stream and relay out every floor.
            want_long = rng.next() % 4 == 0
            kind = CELL_LONG if (HAS_LONG and want_long) else CELL_TALL
            cx = rx + rng.next() % rw
            cy = ry + rng.next() % rh
            rad = 1 + rng.next() % 2
            for y in range(cy - rad, cy + rad + 1):
                for x in range(cx - rad, cx + rad + 1):
                    if 0 <= x < CELLS_W and 0 <= y < CELLS_H and open_[y][x]:
                        if abs(x - cx) + abs(y - cy) <= rad:
                            grass[y][x] = kind

    return open_, rooms, grass


def paint(open_, grass):
    """The block grid StampCell would leave behind, before any cosmetic pass."""
    blocks = [[0] * (CELLS_W * 2) for _ in range(CELLS_H * 2)]
    for cy in range(CELLS_H):
        for cx in range(CELLS_W):
            x, y = cx * 2, cy * 2
            below_open = open_[cy + 1][cx] if cy + 1 < CELLS_H else False
            # Distinct from `not below_open`, which is also true off the bottom
            # edge, where there is no tree to draw the crown of.
            tree_below = cy + 1 < CELLS_H and not open_[cy + 1][cx]

            if open_[cy][cx]:
                mid = CELL_METATILE[grass[cy][cx]]
                lower_l = lower_r = mid

                # A tree's crown pokes up into the block above its canopy. Long
                # grass has no vanilla variant, so it falls through untouched.
                if tree_below and grass[cy][cx] == CELL_TALL:
                    lower_l, lower_r = ABOVE_TREE_TALL
                elif tree_below and grass[cy][cx] == CELL_PLAIN:
                    lower_l, lower_r = ABOVE_TREE
                # Long grass needs its base row where it meets open ground, or
                # the blades are cut off flat.
                elif grass[cy][cx] == CELL_LONG and below_open:
                    lower_l, lower_r = LONG_BASE

                top_l = top_r = mid
            else:
                # Where a tree mass ends, the bottom row is ground contact
                # rather than the trunk, which vanilla never leaves exposed.
                top_l, top_r = TREE[0], TREE[1]
                lower_l, lower_r = TREE_BASE if below_open else (TREE[2], TREE[3])

            blocks[y][x], blocks[y][x + 1] = top_l, top_r
            blocks[y + 1][x], blocks[y + 1][x + 1] = lower_l, lower_r
    return blocks


def decor_hash(seed, x, y):
    """DecorHash(), 32-bit. Position and seed, never the dungeon RNG: the pass
    runs in WriteFloorBlocks, which repaints a floor the player re-enters."""
    M = 0xFFFFFFFF
    h = (seed * 2654435761) & M
    h ^= ((x + 1) * 40503) & M
    h ^= ((y + 1) * 24593) & M
    h ^= h >> 13
    h = (h * 2246822519) & M
    h ^= h >> 15
    return h & 0xFFFF


def apply_decor(blocks, seed):
    """ApplyDecor(), for the single-wide case the woods table uses. Returns how
    many landed, which is the number the rarity is really chosen against."""
    placed = 0
    for y in range(CELLS_H * 2):
        for x in range(CELLS_W * 2):
            h = decor_hash(seed, x, y)
            if h % DECOR_RARITY != 0:
                continue
            matches = [v for base, v in DECOR if base == blocks[y][x]]
            if not matches:
                continue
            blocks[y][x] = matches[(h >> 8) % len(matches)]
            placed += 1
    return placed


def render(blocks, scale=2):
    cache = {}

    def tile(mid):
        if mid not in cache:
            cache[mid] = ta.render_metatile(PAIR, mid, 1)
        return cache[mid]

    im = Image.new('RGB', (CELLS_W * 32, CELLS_H * 32))
    for y in range(CELLS_H * 2):
        for x in range(CELLS_W * 2):
            im.paste(tile(blocks[y][x]), (x * 16, y * 16))
    return im.resize((im.width * scale, im.height * scale), Image.NEAREST) if scale != 1 else im


def main():
    panels = []
    bushes = []
    for seed in (3, 7):
        open_, rooms, grass = generate(seed)
        tall = sum(r.count(CELL_TALL) for r in grass)
        long_ = sum(r.count(CELL_LONG) for r in grass)
        blocks = paint(open_, grass)
        placed = apply_decor(blocks, seed)
        bushes.append(placed)
        print(f'seed {seed}: {len(rooms)} clearings, {tall} tall-grass cells, '
              f'{long_} long-grass cells, {placed} flower bushes')
        panels.append((seed, len(rooms), render(blocks, 1)))

    # Over many seeds, because two floors say nothing about a density.
    counts = []
    for seed in range(1, 401):
        open_, _, grass = generate(seed)
        counts.append(apply_decor(paint(open_, grass), seed))
    counts.sort()
    print(f'flower bushes over 400 seeds: min {counts[0]}, '
          f'median {counts[len(counts)//2]}, max {counts[-1]}, '
          f'mean {sum(counts)/len(counts):.1f}')

    w = panels[0][2].width
    out = Image.new('RGB', (w * 2 + 20, panels[0][2].height + 26), (20, 20, 26))
    d = ImageDraw.Draw(out)
    for i, (seed, nrooms, im) in enumerate(panels):
        out.paste(im, (i * (w + 20), 24))
        d.text((i * (w + 20) + 4, 8), f'seed {seed} - {nrooms} clearings',
               fill=(200, 235, 200))
    out.save(str(OUTDIR / '_woods_prototype.png'))
    print('saved', OUTDIR / '_woods_prototype.png', out.size)


if __name__ == '__main__':
    main()
