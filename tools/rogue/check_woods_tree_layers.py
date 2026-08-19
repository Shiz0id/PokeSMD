"""Guard the LAYER TYPE of the woods' autumn trees.

THE OWNERSHIP RULE, stated once, here:

    A woods tree metatile draws its art in the metatile's TOP half. The two
    canopy rows must be METATILE_LAYER_TYPE_NORMAL, so that half lands on BG1
    and draws OVER the player. The third row is trunk and ground shadow at the
    player's own feet, and must be METATILE_LAYER_TYPE_COVERED, so it lands on
    BG2 and draws UNDER the player.

WHY IT NEEDS A CHECK. append_rustboro.py copies each tree's attribute from
vanilla's woods tree 0x1D4, which is the right thing to do for the behaviour
byte and the wrong thing to do for the layer bits: vanilla bakes tree and grass
together into the BOTTOM half of its metatile, ours puts grass in the bottom
half and the tree on top. Copying the donor wholesale therefore put the entire
tree on BG1 -- and that shipped, as a player walking along the foot of a tree
being drawn behind its trunk. It builds, it generates, every other check passes,
and nothing in a table is wrong.

The check asserts BOTH halves of the rule, because either one alone is a
different visible bug: canopy set to COVERED loses the walk-behind entirely, and
the trunk row left NORMAL is the bug that shipped.

IT ALSO ASSERTS THE PREMISE. The layer rule is only correct while the art is in
the top half; if a future sheet moves it to the bottom the required layers
invert. So the tile halves are checked too, and a tree whose art moved fails
here rather than being quietly given the wrong layers.

Usage:  python3 tools/rogue/check_woods_tree_layers.py [REPO]
        python3 tools/rogue/check_woods_tree_layers.py --repo REPO
        python3 tools/rogue/check_woods_tree_layers.py --selftest
"""
import shutil
import struct
import sys
import tempfile
from pathlib import Path

ATTRS = 'data/tilesets/secondary/rustboro/metatile_attributes.bin'
METATILES = 'data/tilesets/secondary/rustboro/metatiles.bin'

NUM_METATILES_IN_PRIMARY = 512

# include/global.fieldmap.h
METATILE_ATTR_LAYER_MASK = 0xF000
METATILE_ATTR_LAYER_SHIFT = 12
NORMAL, COVERED, SPLIT = 0, 1, 2
LAYER_NAME = {NORMAL: 'NORMAL', COVERED: 'COVERED', SPLIT: 'SPLIT'}

# include/rogue_dungeon.h -- four variants at a stride of nine, row-major 3x3.
# Kept here as literals rather than parsed out of the header on purpose: the
# header names only the first variant and the stride lives in a C constant, so
# re-deriving it here would re-implement the same arithmetic the generator does
# and would agree with it for the wrong reason.
TREE_BASES = (0x35F, 0x368, 0x371, 0x37A)
ROWS = COLS = 3

ROW_LAYERS = {
    0: NORMAL,   # canopy -- the player walks behind it
    1: NORMAL,   # canopy -- the player walks behind it
    2: COVERED,  # trunk and shadow -- the player walks in front of it
}


def read_u16(path):
    b = path.read_bytes()
    return list(struct.unpack('<%dH' % (len(b) // 2), b))


def check(repo):
    repo = Path(repo)
    attrs = read_u16(repo / ATTRS)
    mt = (repo / METATILES).read_bytes()
    ok = True

    for base in TREE_BASES:
        for r in range(ROWS):
            for c in range(COLS):
                mid = base + r * COLS + c
                idx = mid - NUM_METATILES_IN_PRIMARY
                if idx < 0 or idx >= len(attrs):
                    print('FAIL: 0x%03X is not in the Rustboro secondary' % mid)
                    ok = False
                    continue

                # The premise: art in the top half, floor in the bottom half.
                e = struct.unpack_from('<8H', mt, idx * 16)
                if not any(e[4:]):
                    print('FAIL: 0x%03X (r%dc%d) has an EMPTY top half -- the '
                          'art moved, so the layer rule no longer holds'
                          % (mid, r, c))
                    ok = False

                got = (attrs[idx] & METATILE_ATTR_LAYER_MASK) >> METATILE_ATTR_LAYER_SHIFT
                want = ROW_LAYERS[r]
                if got != want:
                    why = ('the canopy must draw over the player'
                           if want == NORMAL else
                           'the trunk row must draw under the player')
                    print('FAIL: 0x%03X (r%dc%d) is %s, expected %s -- %s'
                          % (mid, r, c, LAYER_NAME.get(got, got),
                             LAYER_NAME[want], why))
                    ok = False

    if ok:
        print('ok: %d woods tree metatiles, canopy NORMAL and trunk row COVERED'
              % (len(TREE_BASES) * ROWS * COLS))
    return ok


def selftest(repo):
    """Break each half of the rule on purpose and confirm the check fires.

    Two breaks, not one: the trunk row left NORMAL is the bug that shipped, and
    the canopy set to COVERED is the bug that fixing it carelessly would cause.
    A third breaks the premise the layers rest on.
    """
    src = Path(repo)
    breaks = [
        ('trunk row left NORMAL (the bug that shipped)',
         ATTRS, lambda a, m: set_layer(a, 0x365, NORMAL)),
        ('canopy set to COVERED (loses the walk-behind)',
         ATTRS, lambda a, m: set_layer(a, 0x35F, COVERED)),
        ('trunk row set to SPLIT',
         ATTRS, lambda a, m: set_layer(a, 0x382, SPLIT)),
        ('tree art moved out of the top half',
         METATILES, lambda a, m: clear_top(m, 0x366)),
    ]

    ok = True
    for name, _, mutate in breaks:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td) / 'repo'
            (tmp / ATTRS).parent.mkdir(parents=True)
            shutil.copy(src / ATTRS, tmp / ATTRS)
            shutil.copy(src / METATILES, tmp / METATILES)
            mutate(tmp / ATTRS, tmp / METATILES)
            if check(tmp):
                print('  SELFTEST FAILED: check passed on "%s"' % name)
                ok = False
            else:
                print('  selftest ok: fired on "%s"' % name)
    return ok


def set_layer(path, mid, layer):
    b = bytearray(path.read_bytes())
    off = (mid - NUM_METATILES_IN_PRIMARY) * 2
    a = struct.unpack_from('<H', b, off)[0]
    struct.pack_into('<H', b, off,
                     (a & ~METATILE_ATTR_LAYER_MASK) | (layer << METATILE_ATTR_LAYER_SHIFT))
    path.write_bytes(bytes(b))


def clear_top(path, mid):
    b = bytearray(path.read_bytes())
    off = (mid - NUM_METATILES_IN_PRIMARY) * 16 + 8
    b[off:off + 8] = b'\x00' * 8
    path.write_bytes(bytes(b))


def main():
    args = list(sys.argv[1:])

    is_selftest = '--selftest' in args
    if is_selftest:
        args.remove('--selftest')

    # Takes the repo BOTH ways on purpose -- see check_start_menu_pages.py.
    if '--repo' in args:
        i = args.index('--repo')
        if i + 1 >= len(args):
            print('--repo needs a path')
            return 2
        repo = args[i + 1]
    elif args:
        repo = args[0]
    else:
        repo = '.'

    if is_selftest:
        print('--- selftest: breaking the rule on purpose ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1

    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
