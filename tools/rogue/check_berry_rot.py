"""Is a berry tree's rotten roll actually 1-in-N, for EVERY tree slot?

PlantFloorBerryTrees decides a tree is rotten with
DecorHash(floorSeed, treeIndex, 0) % DUNGEON_BERRY_ROTTEN_ODDS == 0. It uses the
hash rather than the generation stream because it runs from the template loader
rather than from PrepareFloor - see the comment there.

THE RISK IS THE TINY INPUT. DUNGEON_MAX_BERRIES is 4, so x only ever takes the
values 0, 1, 2, 3. A hash that mixes poorly over inputs that small could make one
slot always rotten, or never rotten, across every seed in the game - and the
AGGREGATE over all four trees would still read a perfect 25%. That is the
mixed-category averaging trap in its purest form: the number that looks right is
computed over exactly the axis that hides the bug.

So this measures per tree index, over the whole 16-bit seed space, which is
exhaustive rather than sampled. It also checks that "rotten" is actually worse
than a normal tree and still worth picking.

NOTE: the DecorHash port below must be updated if the C changes. --selftest
proves the measurement can fail, by running it against a deliberately biased
hash; a check that has never failed is worth nothing, and this one carries its
own break test so it cannot rot.

Usage:  python3 tools/rogue/check_berry_rot.py [--repo PATH] [--selftest]
"""
import argparse
import re
import sys
from pathlib import Path

M32 = 0xFFFFFFFF

# How far a single tree slot's rate may sit from the nominal 1/ODDS before this
# complains, in percentage points. The real spread measured over all 65536 seeds
# is under 0.15pp, so 3pp is loose enough never to be noise and tight enough to
# catch a slot that has drifted to a structurally wrong rate.
TOLERANCE_PP = 3.0


def decor_hash(seed, x, y):
    """An exact port of DecorHash in src/rogue_dungeon.c."""
    h = (seed * 2654435761) & M32
    h ^= ((x + 1) * 40503) & M32
    h ^= ((y + 1) * 24593) & M32
    h ^= h >> 13
    h = (h * 2246822519) & M32
    h ^= h >> 15
    return h & 0xFFFF


def biased_hash(odds):
    """A hash that mixes badly over x, for --selftest.

    Returns seed*odds + x, so the value mod odds is exactly x: slot 0 is rotten
    on every seed in the game and slots 1..odds-1 on none of them. That is the
    structural failure this check exists to catch, and the aggregate over the
    slots is still almost exactly 1/odds - which is the whole point.

    An earlier version used seed + x*odds. That is ALSO a bad hash, but its value
    mod odds is seed mod odds, so every slot came out at a clean 1/odds and the
    selftest reported that the check could not detect anything. The lesson is
    that a break test has to express the specific invariant, not merely be wrong.
    """
    return lambda seed, x, y: (seed * odds + x) & 0xFFFF


def constant(text, name):
    m = re.search(r'#define\s+%s\s+(\d+)\b' % name, text)
    if not m:
        sys.exit('could not find %s' % name)
    return int(m.group(1))


ROTTEN_SALT = 0x5B0


def is_rotten(hash_fn, seed, tree, trees, odds, guaranteed):
    """Exact port of RogueDungeon_IsBerryRotten.

    Two ways in, and the check has to model BOTH: a floor's guaranteed dud, and
    the independent per-tree roll. Modelling only the roll is how this file
    passed for as long as it did while a player never met a rotten tree.
    """
    if guaranteed and trees and hash_fn(seed, ROTTEN_SALT, 0) % trees == tree:
        return True
    return hash_fn(seed, tree, 0) % odds == 0


def rates(hash_fn, trees, odds, guaranteed=False):
    """Rotten rate per tree slot, over the whole seed space."""
    out = []
    for i in range(trees):
        rotten = sum(1 for seed in range(65536)
                     if is_rotten(hash_fn, seed, i, trees, odds, guaranteed))
        out.append(100.0 * rotten / 65536)
    return out


def floors_with_no_rot(hash_fn, trees, odds, guaranteed):
    """How many seeds produce a floor with NOT ONE rotten tree.

    THE POINT OF THE GUARANTEE, stated as the thing that must be zero. A per-slot
    rate cannot express it: four slots at a perfect 25% still leave 32% of floors
    with nothing rotten on them, which is the state the game shipped in.
    """
    return sum(1 for seed in range(65536)
               if not any(is_rotten(hash_fn, seed, i, trees, odds, guaranteed)
                          for i in range(trees)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    ap.add_argument('--selftest', action='store_true',
                    help='prove the measurement fires on a biased hash')
    args = ap.parse_args()

    consts = re.sub(r'//[^\n]*', '',
                    (args.repo / 'include/constants/rogue_dungeon.h')
                    .read_text(errors='replace'))
    trees = constant(consts, 'DUNGEON_MAX_BERRIES')
    odds = constant(consts, 'DUNGEON_BERRY_ROTTEN_ODDS')
    good = constant(consts, 'DUNGEON_BERRY_YIELD')
    rot = constant(consts, 'DUNGEON_BERRY_ROTTEN_YIELD')
    guaranteed = bool(re.search(r'DUNGEON_BERRY_ROTTEN_GUARANTEED\s+TRUE', consts))

    # With a guaranteed dud per floor, a slot is rotten if it IS the chosen one
    # or if its own roll comes up - so the nominal is no longer 1/odds and a
    # check still comparing against that would fail on a correct build.
    if guaranteed and trees:
        nominal = 100.0 * (1.0 / trees + (1.0 - 1.0 / trees) / odds)
    else:
        nominal = 100.0 / odds
    fails = []

    # --- rotten has to mean something, and not mean nothing ---
    if rot >= good:
        fails.append('DUNGEON_BERRY_ROTTEN_YIELD %d is not below '
                     'DUNGEON_BERRY_YIELD %d, so a rotten tree is not worse'
                     % (rot, good))
    # ROTTEN IS DELIBERATELY ZERO NOW. This used to assert the opposite - that a
    # tree giving nothing is a dud rather than a gamble - and that reasoning was
    # wrong in play: a yield of 1 against 6 is invisible unless the player counts,
    # so the mechanic existed, worked, and was never once noticed. A dud that
    # says so teaches the rule in one interaction.
    if good > 31 or rot > 31:
        fails.append('berryYield is a 5-BIT field; %d does not fit'
                     % max(good, rot))

    # --- and the rate has to hold for every slot, not just on average ---
    measured = rates(decor_hash, trees, odds, guaranteed)
    for i, pct in enumerate(measured):
        if abs(pct - nominal) > TOLERANCE_PP:
            fails.append('tree slot %d is rotten %.2f%% of the time against a '
                         'nominal %.2f%% - the hash does not mix over x=%d'
                         % (i, pct, nominal, i))

    # --- and no floor may come up with nothing rotten on it at all ---
    if guaranteed:
        barren = floors_with_no_rot(decor_hash, trees, odds, True)
        if barren:
            fails.append('%d seeds in 65536 produce a floor with NO rotten tree, '
                         'but DUNGEON_BERRY_ROTTEN_GUARANTEED is TRUE'
                         % barren)

    if args.selftest:
        bad = rates(biased_hash(odds), trees, odds, guaranteed)
        worst = max(abs(p - nominal) for p in bad)
        print('selftest: biased hash gives %s'
              % ', '.join('%.2f%%' % p for p in bad))
        if worst <= TOLERANCE_PP:
            print('SELFTEST FAILED: a hash that mixes badly still passed, so '
                  'this check cannot detect the thing it exists for')
            return 1
        print('selftest OK: worst slot is %.2fpp off, past the %.1fpp tolerance'
              % (worst, TOLERANCE_PP))

        # And the guarantee itself: with it off, some floors must come up barren.
        # A check that cannot tell the guarantee is missing is the check this
        # file already was.
        barren_off = floors_with_no_rot(decor_hash, trees, odds, False)
        if barren_off == 0:
            print('SELFTEST FAILED: floors come up rotten even with the '
                  'guarantee disabled, so this check cannot tell whether the '
                  'guarantee is doing anything')
            return 1
        print('selftest OK: with the guarantee off, %d seeds in 65536 give a '
              'floor with no rotten tree' % barren_off)

    if fails:
        print('FAIL check_berry_rot.py')
        for f in fails:
            print('  - %s' % f)
        return 1

    print('check_berry_rot.py: OK')
    print('  %d tree slots, 1 in %d rotten, yield %d against %d'
          % (trees, odds, rot, good))
    print('  measured over all 65536 seeds: %s (nominal %.2f%%)'
          % (', '.join('%.2f%%' % p for p in measured), nominal))
    return 0


if __name__ == '__main__':
    sys.exit(main())
