#!/usr/bin/env python3
"""Model BuildSafariEncounterTable and check what it can actually deal.

Four things, none of which has a failure mode louder than "the Safari Zone is
a bit samey":

  1. EVERY LADDER ENTRY IS REACHABLE at some floor. The window slides from the
     absolute run floor, so a ladder longer than window + floors - 1 has a tail
     nothing ever deals, and a species nobody can meet looks exactly like a
     species nobody has met yet.

  2. THE WINDOW STAYS WIDER THAN THE SLOTS at every floor. This is what makes
     RogueDungeon_GetWildMonInfo re-deal at all; if width ever falls to the
     slot count the re-deal stops firing and the area quietly becomes twelve
     species a visit, which is the whole feature gone with no symptom.

  3. THE LADDER IS SORTED WEAKEST TO STRONGEST. The window is a ladder, not a
     bag - an entry out of order hands out a fully evolved Pokemon at the depth
     its neighbours are cocoons.

  4. NO DUPLICATES, which would waste a rung and skew the deal.

Run after editing include/constants/rogue_safari_pool.h or either window in
include/rogue_dungeon.h. Regenerate rather than hand-editing the pool:
tools/rogue/gen_safari_pool.py --write.
"""
import re
import sys
from math import gcd
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_safari_pool import (enabled_families, read_config, read_species,
                             capacity)
try:
    from repo import REPO as _DEFAULT_REPO
except Exception:
    _DEFAULT_REPO = None


def read_int(text, name):
    m = re.search(rf'#define\s+{name}\s+\(?(\d+)\)?', text)
    if not m:
        raise SystemExit(f'{name} not found')
    return int(m.group(1))


def read_ladder(text, name):
    m = re.search(rf'{name}\[\]\s*=\s*\{{(.*?)\}};', text, re.S)
    if not m:
        raise SystemExit(f'{name} not found in rogue_safari_pool.h')
    return re.findall(r'SPECIES_\w+', m.group(1))


def reachable(count, window, floors):
    """Union of the live window over every floor, as BuildSafariEncounterTable
    computes it: tiers = min(window + floor, count), bottom = tiers - window.

    A whole window is reachable at a floor rather than just `slots` of it,
    because the per-roll re-deal walks the rotation through every value.
    """
    seen = set()
    widths = []
    for floor in range(floors):
        tiers = min(window + floor, count)
        bottom = max(tiers - window, 0)
        width = tiers - bottom
        widths.append(width)
        seen |= set(range(bottom, bottom + width))
    return seen, widths


def main(argv):
    repo = Path(argv[1]) if len(argv) > 1 else _DEFAULT_REPO
    if repo is None:
        raise SystemExit('usage: check_safari_pool.py <repo>')

    hdr = (repo / 'include/rogue_dungeon.h').read_text()
    slots_h = (repo / 'include/constants/wild_encounter.h').read_text()
    pool_h = (repo / 'include/constants/rogue_safari_pool.h').read_text()
    _, floors = capacity(repo, 0)

    cfg = read_config(repo)
    _, fam = enabled_families(repo)
    species, _ = read_species(repo, fam, cfg, [])

    failures = []
    print(f'run is {floors} floors')

    for label, ladder_name, window_name, slot_name in (
            ('land', 'sSafariLandSpecies', 'DUNGEON_SAFARI_LAND_WINDOW',
             'NUM_LAND_MONS_ENCOUNTER_SLOTS'),
            ('water', 'sSafariWaterSpecies', 'DUNGEON_SAFARI_WATER_WINDOW',
             'NUM_WATER_MONS_ENCOUNTER_SLOTS')):
        ladder = read_ladder(pool_h, ladder_name)
        window = read_int(hdr, window_name)
        slots = read_int(slots_h, slot_name)
        count = len(ladder)
        seen, widths = reachable(count, window, floors)

        print(f'\n{label}: {count} species, window {window}, {slots} slots')

        # 1. reachability
        missing = sorted(set(range(count)) - seen)
        if missing:
            failures.append(
                f'{label}: {len(missing)} unreachable rung(s), first index '
                f'{missing[0]} ({ladder[missing[0]]}) - the ladder is longer '
                f'than window + floors - 1 = {window + floors - 1}')
        else:
            print(f'  every rung reachable (deepest bottom '
                  f'{max(0, min(count, window + floors - 1) - window)})')

        # 2. the re-deal keeps firing
        narrow = [f for f, w in enumerate(widths) if w <= slots]
        if narrow:
            failures.append(
                f'{label}: width <= slots on {len(narrow)} floor(s) (first '
                f'floor {narrow[0]}, width {widths[narrow[0]]}) - the per-roll '
                f're-deal stops firing there and the area drops to {slots} '
                f'species a visit')
        else:
            print(f'  width {min(widths)}-{max(widths)}, always > {slots}, so '
                  f'the re-deal always fires')

        # 3. sorted weakest to strongest
        bst = []
        for s in ladder:
            sp = species.get(s)
            if sp is None:
                failures.append(f'{label}: {s} is not an enabled species')
                bst.append(None)
            else:
                bst.append(sp.bst)
        drops = [(i, ladder[i - 1], bst[i - 1], ladder[i], bst[i])
                 for i in range(1, len(ladder))
                 if bst[i] is not None and bst[i - 1] is not None
                 and bst[i] < bst[i - 1]]
        if drops:
            for i, a, ab, b, bb in drops[:5]:
                failures.append(f'{label}: rung {i} goes DOWN in BST - '
                                f'{a} ({ab}) then {b} ({bb})')
        else:
            print(f'  sorted weakest to strongest, BST '
                  f'{bst[0]}-{bst[-1]}')

        # 4. the rotation stride reaches every rung
        #
        # RogueDungeon_GetWildMonInfo advances the rotation by `slots` so
        # consecutive tables share nothing. That only walks the whole window if
        # the stride is coprime with the width - otherwise the rotation cycles
        # through width/gcd values and the rest of the ladder is dealt to
        # NOBODY, forever, with no symptom but a samey area. 47 happens to be
        # prime, which makes land safe by luck rather than by design, so this
        # is checked rather than assumed.
        g = gcd(slots, widths[0])
        if g != 1:
            failures.append(
                f'{label}: stride {slots} and width {widths[0]} share a factor '
                f'of {g}, so the rotation only ever takes '
                f'{widths[0] // g} of {widths[0]} values and '
                f'{widths[0] - widths[0] // g} rung(s) never reach a slot')
        else:
            print(f'  stride {slots} coprime with width {widths[0]}, so the '
                  f'rotation visits all {widths[0]} values')

        # 5. duplicates
        dupes = {s for s in ladder if ladder.count(s) > 1}
        if dupes:
            failures.append(f'{label}: duplicate rung(s) {sorted(dupes)}')
        else:
            print('  no duplicates')

    print()
    if failures:
        for f in failures:
            print(f'FAIL: {f}')
        print(f'\n{len(failures)} problem(s)')
        return 1
    print('ok: both Safari ladders are fully reachable and always re-deal')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
