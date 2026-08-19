"""Four autumn trees for the woods, into the Rustboro secondary tileset.

Source is 9.png from Team Aqua's Asset Repo, "Voluptuas Collected Tilesets":
Red-eX, Alistair, Hydragirium, Mew1993. Free to use, credit required.

WHAT IS BUILT. Four 3x3 metatile trees, nine metatiles each, 36 in total. The
generator's stamp is 3x3 with an ALTERNATE bottom row (STAMP_BASE_*) for where
a tree mass meets open ground, and here that row is the SAME art: this sheet
draws whole individual trees with their own trunk and shadow, so a tree with a
tree below it should still show its trunk. Vanilla swaps the row because its
trees tile into an undifferentiated mass; these do not.

SO THE ART IS 48x48, NOT 48x64. The tree is 51px tall and the cell is 48, so
three rows come off the TOP of the crown - never the bottom, which would cut a
flat line through the trunk and shadow and read as a broken tree. The
alternative was letting the crown poke into the cell above via aboveTree*, which
is what vanilla does; that needs crown-over-grass AND crown-over-tallgrass
composites per tree and buys a flourish, so the woods sets aboveTree* to 0 and
the generator's existing `leftTop != 0` guard skips the whole path.

THE TREES ARE NOT FLUSH WITH THE 48px GRID: each sits at +8 in its cell and is
47 wide. Cropping on the nominal boundary silently loses 7 columns off every
tree's right edge. See CELL_X and --verify, which exists because that happened.

TOP LAYER FOR THE TREE, BOTTOM LAYER FOR GRASS. Vanilla's woods tree (0x1D4)
puts everything in the bottom layer with grass baked into the tile, so its
palette must cover tree and grass together. Ours takes the grass quadrant
entries from primary metatile 0x001 verbatim for the bottom layer and puts the
tree on top, which spends all 15 colours on the tree and shows real floor
through the canopy gaps.

ONE SHARED PALETTE, slot 6 - Rustboro's metatiles use 7-11, so 6 and 12 are
free. A palette each looks slightly better but costs 4 of the 7 a secondary
owns, and sharing DEDUPES BETTER because two trees can only share a tile if
they share the palette it is expressed in. No dithering: it reads as
checkerboard at 1x and defeats the dedupe.

IT DOES NOT WRITE THE TILESET. append_rustboro.py owns those files, for the
reason its docstring gives. This module exports what to write.

Usage:
    python make_woods_trees.py            # report
    python make_woods_trees.py --preview  # render
    python make_woods_trees.py --verify   # prove no tree is truncated
"""
import struct
import sys
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
SHEET = Path(r"D:\PokemonTest\Team-Aquas-Asset-Repo\Tilesets\Other Tilesets"
             r"\Voluptuas Collected Tilesets\9.png")
OUT = REPO / 'tools/rogue/_out'
GENERAL = REPO / 'data/tilesets/primary/general'
RUSTBORO = REPO / 'data/tilesets/secondary/rustboro'

NUM_TILES_IN_PRIMARY = 512
PALETTE = 6
RUSTBORO_BASE_METATILES = 350        # vanilla count, ids 0x200-0x35D
GRASS_METATILE = 0x001               # the woods floor, primary
TREE_ATTR_DONOR = 0x1D4              # vanilla woods tree, primary

# LAYER TYPE, WHICH THE DONOR CANNOT SUPPLY. The attribute is copied from the
# vanilla tree so behaviour is never invented, but its LAYER TYPE is wrong for
# this art and copying it shipped a visible bug: the player walking along the
# bottom of a tree was drawn behind the trunk.
#
# Vanilla's tree bakes tree and grass together into the BOTTOM half of the
# metatile, so NORMAL - middle plus top - puts its art on the middle layer,
# under the player, and leaves the top half blank. Ours does the opposite (see
# the docstring): grass in the bottom half, tree in the TOP half, which under
# NORMAL lands the entire tree on BG1, above the player. Correct for the two
# canopy rows, which the player really does walk behind. Wrong for the third,
# which is trunk and ground shadow at the player's own feet.
#
# COVERED shifts both halves down a layer - grass to BG3, tree to BG2 - so the
# bottom row draws under the sprite while the canopy above keeps drawing over
# it. Nothing else moves: layer type is bits 12-15 and the behaviour byte the
# donor supplies is untouched.
METATILE_LAYER_TYPE_NORMAL = 0
METATILE_LAYER_TYPE_COVERED = 1
TREE_ROW_LAYERS = {
    0: METATILE_LAYER_TYPE_NORMAL,   # canopy - draws over the player
    1: METATILE_LAYER_TYPE_NORMAL,   # canopy - draws over the player
    2: METATILE_LAYER_TYPE_COVERED,  # trunk and shadow - draws under the player
}
STAIRS_TILES = (508, 509, 510, 511)  # make_woods_stairs.py owns these

BG = (255, 255, 255)                 # the sheet has NO alpha; white is the key
ROW_Y, PITCH, CELL_H = 54, 48, 58
CELL_X = 8                           # see module docstring

TREES = [
    ('red',    744),
    ('olive',  456),
    ('tan',    600),
    ('fruit',  504),
]

TW = TH = 3
BOXW = BOXH = 48

# The four bottom-layer entries a tree stands on. None means "the primary grass
# from metatile 0x001"; append_rustboro overrides it with the AUTUMN grass once
# that exists.
#
# THIS IS NOT COSMETIC. A tree's own bottom layer is the floor showing through
# its canopy gaps, so if it keeps pointing at the primary mint grass while the
# floor around it is autumn, every tree stands on a visible rectangle of the old
# colour. That is exactly what the grass candidate render showed.
GRASS_BOTTOM = None

# Tile slots another module has already claimed. Set by append_rustboro.
TAKEN_SLOTS = set()


def cell(x):
    x += CELL_X
    return Image.open(SHEET).convert('RGB').crop((x, ROW_Y, x + PITCH, ROW_Y + CELL_H))


def keyed(x):
    """The tree, white knocked out, bottom-aligned in a 48x48 box.

    Largest connected component only: the canopies touch on this sheet, so a
    cell carries fragments of its neighbours whatever the offset.

    BOTTOM-aligned, so the 3px that will not fit come off the TOP of the crown.
    Top-aligning was tried and is clearly worse: it cuts a flat line through the
    trunk and ground shadow, and a tree that does not meet the ground reads as
    broken. The crown's top three rows are a thin arc, and losing them only
    makes the tree very slightly less domed.
    """
    c = cell(x)
    a = c.load()
    seen = [[False] * c.width for _ in range(c.height)]
    best = []
    for sy in range(c.height):
        for sx in range(c.width):
            if seen[sy][sx] or a[sx, sy] == BG:
                continue
            stack, comp = [(sx, sy)], []
            seen[sy][sx] = True
            while stack:
                px, py = stack.pop()
                comp.append((px, py))
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = px + dx, py + dy
                    if (0 <= nx < c.width and 0 <= ny < c.height
                            and not seen[ny][nx] and a[nx, ny] != BG):
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            if len(comp) > len(best):
                best = comp

    bottom = max(j for _, j in best)
    out = Image.new('RGBA', (BOXW, BOXH), (0, 0, 0, 0))
    o = out.load()
    for i, j in best:
        y = j - bottom + BOXH - 1
        if 0 <= y < BOXH and 0 <= i < BOXW:
            o[i, y] = a[i, j] + (255,)
    return out


def shared_palette(imgs, n=15):
    strip = Image.new('RGB', (BOXW * len(imgs), BOXH), (0, 0, 0))
    for k, im in enumerate(imgs):
        strip.paste(im, (k * BOXW, 0), im)
    q = strip.quantize(colors=n, method=Image.MEDIANCUT, dither=Image.NONE)
    pal = q.getpalette()[:n * 3]
    # 5 bits per channel, as the hardware stores it, applied HERE so the
    # indices and the palette cannot disagree later.
    return [tuple((v >> 3) * 255 // 31 for v in pal[i * 3:i * 3 + 3])
            for i in range(n)]


def index(img, pal):
    w, h = img.size
    a = img.load()
    idx = [[0] * w for _ in range(h)]
    for j in range(h):
        for i in range(w):
            r, g, b, al = a[i, j]
            if not al:
                continue
            best, bd = 1, 1 << 30
            for k, (pr, pg, pb) in enumerate(pal):
                d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
                if d < bd:
                    best, bd = k + 1, d
            idx[j][i] = best
    return idx


def _flips(t):
    g = lambda i, j: t[j * 8 + i]
    return ((t, 0),
            (tuple(g(7 - i, j) for j in range(8) for i in range(8)), 1),
            (tuple(g(i, 7 - j) for j in range(8) for i in range(8)), 2),
            (tuple(g(7 - i, 7 - j) for j in range(8) for i in range(8)), 3))


def free_slots():
    """Rustboro tiles referenced by no VANILLA metatile.

    Read off the first 350 metatiles so the answer does not depend on whether
    an append has already run. Tile 0 is excluded because a metatile entry of 0
    means "nothing here", and the stairs' four are excluded because
    make_woods_stairs.py owns them.
    """
    mt = RUSTBORO.joinpath('metatiles.bin').read_bytes()[:RUSTBORO_BASE_METATILES * 16]
    used = set()
    for i in range(0, len(mt), 2):
        t = struct.unpack('<H', mt[i:i + 2])[0] & 0x3FF
        if t >= NUM_TILES_IN_PRIMARY:
            used.add(t - NUM_TILES_IN_PRIMARY)
    return [s for s in range(1, 512) if s not in used and s not in STAIRS_TILES]


def build():
    imgs = [keyed(x) for _, x in TREES]
    pal = shared_palette(imgs)
    idxs = [index(im, pal) for im in imgs]

    bank, order = {}, []

    def intern(t):
        for cand, fl in _flips(t):
            if cand in bank:
                return bank[cand], fl
        bank[t] = len(order)
        order.append(t)
        return bank[t], 0

    # Grass under every tree: primary metatile 0x001's bottom-layer quadrants,
    # copied verbatim so the floor beneath a canopy is the real floor.
    if GRASS_BOTTOM is not None:
        grass = tuple(GRASS_BOTTOM)
    else:
        gmt = GENERAL.joinpath('metatiles.bin').read_bytes()
        grass = struct.unpack('<8H',
                              gmt[GRASS_METATILE * 16:GRASS_METATILE * 16 + 16])[:4]

    slots = [s for s in free_slots() if s not in TAKEN_SLOTS]
    mts = []                                   # (label, 8 entries)
    for (name, _), idx in zip(TREES, idxs):
        for my in range(TH):
            for mx in range(TW):
                top = []
                for sy in (0, 1):
                    for sx in (0, 1):
                        t = tuple(idx[(my * 2 + sy) * 8 + j][(mx * 2 + sx) * 8 + i]
                                  for j in range(8) for i in range(8))
                        if not any(t):
                            top.append(0)
                            continue
                        slot, fl = intern(t)
                        top.append((NUM_TILES_IN_PRIMARY + slots[slot])
                                   | ((fl & 1) << 10) | ((fl >> 1) << 11)
                                   | (PALETTE << 12))
                # bottom TL,TR,BL,BR then top TL,TR,BL,BR
                mts.append((f'{name}{my}{mx}', list(grass) + top))
    return pal, order, slots, mts, idxs


def tile_bytes(t):
    """64 nibbles -> 32 bytes, low nibble first, the 4bpp order gbagfx emits."""
    return bytes((t[i] & 0xF) | ((t[i + 1] & 0xF) << 4) for i in range(0, 64, 2))


def append_order():
    """(label, entry, attribute donor, layer type) for append_rustboro.

    The layer type is carried separately from the donor because the donor is
    right about behaviour and wrong about layers - see TREE_ROW_LAYERS.
    """
    _, _, _, mts, _ = build()
    # The label ends in the metatile's row and column within its tree, which is
    # how build() names them; the row is what picks the layer.
    return [(lbl, struct.pack('<8H', *e), TREE_ATTR_DONOR,
             TREE_ROW_LAYERS[int(lbl[-2])])
            for lbl, e in mts]


def tile_writes():
    """{rustboro tile slot: 32 bytes} for append_rustboro."""
    _, order, slots, _, _ = build()
    return {slots[i]: tile_bytes(t) for i, t in enumerate(order)}


def palette_write():
    pal, *_ = build()
    return PALETTE, pal


def verify():
    im = Image.open(SHEET).convert('RGB')
    bad = 0
    for name, x in TREES:
        got = keyed(x)
        a = got.load()
        mine = sum(1 for j in range(got.height) for i in range(got.width) if a[i, j][3])
        X0 = x - 24
        c = im.crop((X0, ROW_Y, x + 96, ROW_Y + CELL_H))
        ca = c.load()
        seed = (x + CELL_X + 24 - X0, 30)
        if ca[seed] == BG:
            seed = (seed[0], 40)
        seen, st = {seed}, [seed]
        while st:
            px, py = st.pop()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (px + dx, py + dy)
                if (0 <= n[0] < c.width and 0 <= n[1] < c.height
                        and n not in seen and ca[n] != BG):
                    seen.add(n)
                    st.append(n)
        # The top rows that will not fit 48px are dropped on purpose; any other
        # loss is a bug, and losing them off the BOTTOM would be the bad kind.
        lost = len(seen) - mine
        bot = max(q[1] for q in seen)
        dropped = sum(1 for (px, py) in seen if py <= bot - BOXH)
        ok = lost == dropped
        bad += not ok
        print(f'  {"ok  " if ok else "LOST"} {name:6s} kept {mine}, tree {len(seen)}, '
              f'above the 48px box {dropped}')
    if bad:
        print('FAIL: a tree lost pixels that were not the deliberate top crop')
        return 1
    print('PASS: every tree whole but for the documented crown crop')
    return 0


def main():
    if '--verify' in sys.argv:
        print('verify: extraction against a flood fill of the source')
        sys.exit(verify())

    pal, order, slots, mts, idxs = build()
    print(f'{len(TREES)} trees, {len(mts)} metatiles ({TW}x{TH} each), palette {PALETTE}')
    print(f'unique tiles {len(order)}, into Rustboro slots '
          f'{slots[0]}..{slots[len(order)-1]} ({len(free_slots())} free)')
    print(f'metatiles 0x{0x200 + RUSTBORO_BASE_METATILES + 1:03X}.. '
          f'(after the stairs at 0x{0x200 + RUSTBORO_BASE_METATILES:03X})')
    print('palette: ' + ' '.join(f'{r:02X}{g:02X}{b:02X}' for r, g, b in pal))

    if '--preview' in sys.argv:
        OUT.mkdir(parents=True, exist_ok=True)
        Z = 4
        sheet = Image.new('RGB', ((BOXW + 8) * len(idxs) * Z, (BOXH + 8) * Z),
                          (0x6A, 0xC5, 0xA4))
        for k, idx in enumerate(idxs):
            im = Image.new('RGBA', (BOXW, BOXH), (0, 0, 0, 0))
            o = im.load()
            for j in range(BOXH):
                for i in range(BOXW):
                    if idx[j][i]:
                        o[i, j] = pal[idx[j][i] - 1] + (255,)
            sheet.paste(im.resize((BOXW * Z, BOXH * Z), Image.NEAREST),
                        ((BOXW + 8) * k * Z + 4 * Z, 4 * Z),
                        im.resize((BOXW * Z, BOXH * Z), Image.NEAREST))
        sheet.save(OUT / 'woods_trees.png')
        print(f'wrote {OUT / "woods_trees.png"}')


if __name__ == '__main__':
    main()
