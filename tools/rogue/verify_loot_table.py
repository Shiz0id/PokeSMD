"""
Verify what item balls hand out, floor by floor, against the one thing the
table is for: healing that gets stronger and more plentiful the deeper a run
goes.

The table is a set of floor BANDS rather than floor minimums, so an entry can
retire as well as arrive - that is what stops Potions being the commonest find
on floor 100, and it is the same lesson the species window already learned.
Bands are easy to get subtly wrong: one typo closes a gap nobody notices until
a floor rolls nothing, or leaves a strong item live from floor 0.

CATEGORIES ARE CHECKED SEPARATELY, and getting that wrong was the first version
of this script. Measuring one average over the whole table makes every arrival
of a cheap-but-useful item - Full Heal, Ether - look like healing going
BACKWARDS, because anything below the running mean pulls it down. It is not: a
floor that starts dropping Ethers has not lost any healing. Potions are scored
against potions, revives against revives, and utility is only checked for
presence.

That separation left two REAL regressions behind, which is the point of it: a
single Max Potion is worth less raw HP than the two Hyper Potions it replaces,
and a single Full Restore less again. Both are quantity 2 in the table now.

Parsed out of src/rogue_dungeon.c rather than restated here, so the check reads
the table the game actually ships. Checks:

  1. every floor has at least one entry in band and a nonzero total weight -
     RollFloorItem divides by that
  2. every floor has at least one POTION in band, so healing is always findable
  3. expected potion HP per ball never falls as floors deepen
  4. expected potion HP per FLOOR never falls - count climbs too, and the two
     together are what "more healing the deeper you go" means
  5. the best item available in each category never regresses
  6. every item named exists in constants/items.h, and every band is well formed

Percentage and full-restore items are priced against a late-run mon rather than
by their stock text, because the question is whether a floor's healing keeps up
with the party. Those numbers are a judgement call and are named here rather
than buried.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / 'src/rogue_dungeon.c'
HEADER = REPO / 'include/constants/rogue_dungeon.h'
ITEMS = REPO / 'include/constants/items.h'

POTION, REVIVE, UTILITY = 'potion', 'revive', 'utility'

# (category, HP-equivalent for ONE of them)
VALUE = {
    'ITEM_POTION':        (POTION,   20),
    'ITEM_SUPER_POTION':  (POTION,   60),
    'ITEM_HYPER_POTION':  (POTION,  120),
    'ITEM_MAX_POTION':    (POTION,  200),   # full, on a late mon
    'ITEM_FULL_RESTORE':  (POTION,  200),   # full, and clears status
    'ITEM_REVIVE':        (REVIVE,  100),   # half of that, from fainted
    'ITEM_MAX_REVIVE':    (REVIVE,  200),
    'ITEM_FULL_HEAL':     (UTILITY,   0),
    'ITEM_ANTIDOTE':      (UTILITY,   0),
    'ITEM_PARALYZE_HEAL': (UTILITY,   0),
    'ITEM_AWAKENING':     (UTILITY,   0),
    'ITEM_ETHER':         (UTILITY,   0),
    'ITEM_MAX_ETHER':     (UTILITY,   0),
    'ITEM_ELIXIR':        (UTILITY,   0),
}


def constant(name, default=None):
    text = HEADER.read_text(encoding='utf-8')
    m = re.search(rf'^#define\s+{name}\s+(\d+)\s*$', text, re.M)
    if not m:
        if default is not None:
            return default
        raise SystemExit(f'cannot read {name}')
    return int(m.group(1))


def parse_table(name='sLootConsumables'):
    """-> [(item, weight, minFloor, maxFloor, quantity)] from the C initialiser."""
    text = SOURCE.read_text(encoding='utf-8')
    m = re.search(rf'{name}\[\]\s*=\s*\{{(.*?)\n\}};', text, re.S)
    if not m:
        raise SystemExit(f'cannot find {name} in rogue_dungeon.c')

    rows = []
    for line in m.group(1).splitlines():
        line = line.split('//')[0].strip()
        e = re.match(r'\{\s*(ITEM_\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}', line)
        if e:
            rows.append((e.group(1), int(e.group(2)), int(e.group(3)),
                         int(e.group(4)), int(e.group(5))))
    if not rows:
        raise SystemExit('parsed no entries - the table changed shape')
    return rows


def known_items():
    return set(re.findall(r'^\s*(ITEM_[A-Z0-9_]+)', ITEMS.read_text(encoding='utf-8'), re.M))


def main(argv):
    rows = parse_table()
    total_floors = constant('DUNGEON_TOTAL_FLOORS', 115)
    item_min = constant('DUNGEON_ITEM_MIN')
    per_extra = constant('DUNGEON_ITEM_FLOORS_PER_EXTRA')
    item_max = constant('DUNGEON_MAX_ITEMS')

    print(f'{len(rows)} entries, {total_floors} floors, '
          f'{item_min}..{item_max} balls per floor (+1 per {per_extra})')

    failures = []

    # 6. the entries themselves
    known = known_items()
    for item, weight, lo, hi, qty in rows:
        if item not in known:
            failures.append(f'{item} is not in constants/items.h')
        if item not in VALUE:
            failures.append(f'{item} is uncategorised in this script')
        if lo >= hi:
            failures.append(f'{item} band {lo}..{hi} is empty')
        if lo >= total_floors:
            failures.append(f'{item} arrives on floor {lo}, past the last floor')
        if weight == 0 or qty == 0:
            failures.append(f'{item} has zero weight or quantity')
    if failures:
        for f in failures:
            print(f'FAIL  {f}')
        raise SystemExit(f'{len(failures)} problem(s)')

    prev_ball = prev_floor = -1.0
    best_seen = {POTION: -1, REVIVE: -1}
    samples = []

    for floor in range(total_floors):
        band = [r for r in rows if r[2] <= floor < r[3]]
        if not band or sum(r[1] for r in band) == 0:
            failures.append(f'floor {floor} has nothing in band')
            continue

        potions = [r for r in band if VALUE[r[0]][0] == POTION]
        revives = [r for r in band if VALUE[r[0]][0] == REVIVE]
        utility = [r for r in band if VALUE[r[0]][0] == UTILITY]

        # 2. healing must always be findable
        if not potions:
            failures.append(f'floor {floor} has no potion in band')
            continue

        # 3/4. weighted AMONG POTIONS ONLY, so a utility arrival cannot move it
        pw = sum(r[1] for r in potions)
        per_ball = sum(VALUE[r[0]][1] * r[4] * r[1] for r in potions) / pw
        balls = min(item_min + floor // per_extra, item_max)
        per_floor = per_ball * balls

        if per_ball + 1e-9 < prev_ball:
            failures.append(f'floor {floor}: potion HP per ball fell '
                            f'{prev_ball:.1f} -> {per_ball:.1f}')
        if per_floor + 1e-9 < prev_floor:
            failures.append(f'floor {floor}: potion HP per floor fell '
                            f'{prev_floor:.1f} -> {per_floor:.1f}')
        prev_ball, prev_floor = per_ball, per_floor

        # 5. the ceiling in each category may not drop
        for group, name in ((potions, POTION), (revives, REVIVE)):
            if not group:
                continue
            best = max(VALUE[r[0]][1] * r[4] for r in group)
            if best < best_seen[name]:
                failures.append(f'floor {floor}: best {name} fell '
                                f'{best_seen[name]} -> {best}')
            best_seen[name] = max(best_seen[name], best)

        if floor % 10 == 0 or floor == total_floors - 1:
            samples.append((floor, len(potions), len(revives), len(utility),
                            balls, per_ball, per_floor))

    print()
    print('floor  pot  rev  util  balls   HP/ball   HP/floor')
    for floor, np_, nr, nu, balls, per_ball, per_floor in samples:
        print(f'{floor:>5}  {np_:>3}  {nr:>3}  {nu:>4}  {balls:>5}  '
              f'{per_ball:>8.1f}  {per_floor:>9.1f}')

    failures.extend(check_held(total_floors))
    failures.extend(check_berries(total_floors))

    print()
    if failures:
        for f in failures:
            print(f'FAIL  {f}')
        raise SystemExit(f'{len(failures)} problem(s)')
    print(f'ok - potion healing rises monotonically, floor 0 to {total_floors - 1}')
    print('ok - held items always available, and the best tier never regresses')
    print('ok - every berry is a berry, and every floor has one in band')


def check_berries(total_floors):
    """Berries are planted through ItemIdToBerryType, which returns
    BERRY_ID_NONE for anything that is not a berry - so a wrong name here plants
    a BLANK TREE rather than failing. Nothing in the build would say so, and in
    game it looks like a patch of dirt. Hence the name test."""
    rows = parse_table('sLootBerries')
    known = known_items()
    failures = []

    for item, weight, lo, hi, qty in rows:
        if item not in known:
            failures.append(f'berry {item} is not in constants/items.h')
        elif not item.endswith('_BERRY'):
            failures.append(f'berry {item} is not a berry - it would plant nothing')
        if lo >= hi:
            failures.append(f'berry {item} band {lo}..{hi} is empty')
        if lo >= total_floors:
            failures.append(f'berry {item} arrives on floor {lo}, past the last floor')
        if weight == 0:
            failures.append(f'berry {item} has zero weight')
    if failures:
        return failures

    samples = []
    for floor in range(total_floors):
        band = [r for r in rows if r[2] <= floor < r[3]]
        if not band or sum(r[1] for r in band) == 0:
            failures.append(f'floor {floor} has no berry in band')
            continue
        if floor % 20 == 0 or floor == total_floors - 1:
            samples.append((floor, len(band)))

    print()
    print(f'{len(rows)} berry entries')
    print('floor  in band')
    for floor, n in samples:
        print(f'{floor:>5}  {n:>7}')
    return failures


# Held items are graded rather than scored: there is no HP number to average, and
# inventing one would be the same mistake the potion metric already made. What
# has to hold is that a floor always has SOMETHING worth picking up and that the
# ceiling never drops - a run should not pass a floor where the best possible
# find is worse than one it already walked through.
HELD_TIER = {
    'ITEM_QUICK_CLAW': 1, 'ITEM_SHELL_BELL': 1, 'ITEM_SITRUS_BERRY': 1,
    'ITEM_EVIOLITE': 2, 'ITEM_MUSCLE_BAND': 2, 'ITEM_WISE_GLASSES': 2,
    'ITEM_SCOPE_LENS': 2,
    'ITEM_FOCUS_SASH': 3, 'ITEM_ROCKY_HELMET': 3, 'ITEM_EXPERT_BELT': 3,
    'ITEM_AIR_BALLOON': 3, 'ITEM_CHOICE_SCARF': 3, 'ITEM_CHOICE_BAND': 3,
    'ITEM_CHOICE_SPECS': 3, 'ITEM_ASSAULT_VEST': 3, 'ITEM_LIFE_ORB': 3,
    'ITEM_LEFTOVERS': 3,
}


def check_held(total_floors):
    rows = parse_table('sLootHeld')
    known = known_items()
    failures = []

    for item, weight, lo, hi, qty in rows:
        if item not in known:
            failures.append(f'held {item} is not in constants/items.h')
        if item not in HELD_TIER:
            failures.append(f'held {item} is ungraded in this script')
        if lo >= hi:
            failures.append(f'held {item} band {lo}..{hi} is empty')
        if lo >= total_floors:
            failures.append(f'held {item} arrives on floor {lo}, past the last floor')
        if weight == 0 or qty == 0:
            failures.append(f'held {item} has zero weight or quantity')
    if failures:
        return failures

    best_seen = 0
    samples = []
    for floor in range(total_floors):
        band = [r for r in rows if r[2] <= floor < r[3]]
        if not band or sum(r[1] for r in band) == 0:
            failures.append(f'floor {floor} has no held item in band')
            continue
        best = max(HELD_TIER[r[0]] for r in band)
        if best < best_seen:
            failures.append(f'floor {floor}: best held tier fell {best_seen} -> {best}')
        best_seen = max(best_seen, best)
        if floor % 20 == 0 or floor == total_floors - 1:
            samples.append((floor, len(band), best))

    print()
    print(f'{len(rows)} held entries')
    print('floor  in band  best tier')
    for floor, n, best in samples:
        print(f'{floor:>5}  {n:>7}  {best:>9}')
    return failures


if __name__ == '__main__':
    main(sys.argv[1:])
