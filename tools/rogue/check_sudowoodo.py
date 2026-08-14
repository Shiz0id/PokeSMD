"""Is there ever more than one Sudowoodo in a grove, and does it show up at all?

THE IMPOSTER IS DECIDED BY TWO HASHES, NOT ONE, and the split is the thing this
check exists to protect. The first asks whether this FLOOR hides one; only then
does the second pick which tree. The obvious single-roll implementation - ask
each tree "are you the imposter?" at 1-in-N - is binomial: it produces floors
with two and three of them, and floors with none at a rate nobody chose. A grove
of Sudowoodos is a joke rather than an ambush, and it would take a very long run
to notice by playing.

So this measures, over the whole 16-bit seed space and for every reachable tree
count:

  - a floor never has TWO imposters (the single-roll bug, stated directly)
  - the floor rate is 1/DUNGEON_SUDOWOODO_ODDS, not per-tree
  - every tree index is reachable, so the pick is not pinned to slot 0 by a
    hash that mixes badly over the tiny input - the same trap check_berry_rot.py
    guards, and the input here is even smaller

NOTE: the DecorHash port below must be updated if the C changes. --selftest
proves the measurement can fail by running it against the single-roll
implementation this check exists to reject.

Usage:  python3 tools/rogue/check_sudowoodo.py [--repo PATH] [--selftest]
"""
import argparse
import re
import sys
from pathlib import Path

M32 = 0xFFFFFFFF
TOLERANCE_PP = 3.0


def decor_hash(seed, x, y):
    """An exact port of DecorHash in src/rogue_dungeon.c."""
    h = (seed * 2654435761) & M32
    h ^= ((x + 1) * 40503) & M32
    h ^= ((y + 1) * 24593) & M32
    h ^= h >> 13
    h = (h * 1274126177) & M32
    h ^= h >> 16
    return h & M32


def constant(text, name):
    m = re.search(r'#define\s+' + name + r'\s+(0x[0-9A-Fa-f]+|\d+)', text)
    if not m:
        sys.exit('could not find %s' % name)
    return int(m.group(1), 0)


def imposter_of(seed, trees, odds, salt):
    """Which tree is the imposter this floor, or None. Ports RogueDungeon_IsSudowoodoTree."""
    if trees == 0:
        return None
    if decor_hash(seed, salt, 0) % odds != 0:
        return None
    return decor_hash(seed, salt, 1) % trees


def imposters_single_roll(seed, trees, odds, salt):
    """THE BUG THIS REJECTS: one roll per tree. Used only by --selftest."""
    return [t for t in range(trees) if decor_hash(seed, salt + t, 0) % odds == 0]


def evaluate(trees, odds, salt, pick=imposter_of):
    """Return (floors_with_one, per_index_counts) over the whole seed space."""
    hits = 0
    per_index = [0] * trees
    for seed in range(65536):
        who = pick(seed, trees, odds, salt)
        if who is not None:
            hits += 1
            per_index[who] += 1
    return hits, per_index


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.')
    ap.add_argument('--selftest', action='store_true')
    args = ap.parse_args()

    text = re.sub(r'//[^\n]*', '',
                  (Path(args.repo) / 'include/constants/rogue_dungeon.h')
                  .read_text(errors='replace'))
    odds = constant(text, 'DUNGEON_SUDOWOODO_ODDS')
    salt = constant(text, 'DUNGEON_SUDOWOODO_SALT')
    max_trees = constant(text, 'DUNGEON_MAX_BERRIES')
    min_trees = constant(text, 'DUNGEON_BERRY_MIN')

    if args.selftest:
        # The single-roll implementation must produce floors with more than one
        # imposter. If it does not, this check cannot tell the two apart and is
        # measuring nothing.
        worst = 0
        for seed in range(65536):
            worst = max(worst, len(imposters_single_roll(seed, max_trees, odds, salt)))
        if worst < 2:
            print('SELFTEST FAILED: the single-roll implementation never put two '
                  'imposters on a floor, so this check cannot reject it')
            return 1
        print('selftest OK: the single-roll bug reaches %d imposters on one '
              'floor; the shipped one is capped at 1 by construction' % worst)

    fails = []
    nominal = 100.0 / odds
    lines = []

    for trees in range(min_trees, max_trees + 1):
        hits, per_index = evaluate(trees, odds, salt)
        pct = 100.0 * hits / 65536

        # --- the floor rate is per FLOOR, not per tree ---
        if abs(pct - nominal) > TOLERANCE_PP:
            fails.append('with %d trees the floor rate is %.2f%% against a '
                         'nominal %.2f%%' % (trees, pct, nominal))

        # --- and every tree slot must be reachable ---
        for i, n in enumerate(per_index):
            share = 100.0 * n / hits if hits else 0.0
            if share < (100.0 / trees) - 10.0 or share > (100.0 / trees) + 10.0:
                fails.append('with %d trees, slot %d is chosen %.1f%% of the '
                             'time against an even %.1f%% - the pick does not '
                             'mix over so small an input'
                             % (trees, i, share, 100.0 / trees))
        lines.append('  %d trees: %.2f%% of floors, slots %s'
                     % (trees, pct, '/'.join('%.0f%%' % (100.0 * n / hits)
                                             for n in per_index) if hits else 'n/a'))

    if fails:
        print('FAIL check_sudowoodo.py')
        for f in fails:
            print('  - %s' % f)
        return 1

    print('check_sudowoodo.py: OK')
    print('  1 in %d floors with trees hides one, never two' % odds)
    for line in lines:
        print(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
