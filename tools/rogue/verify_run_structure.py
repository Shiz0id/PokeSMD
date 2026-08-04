"""
Verify the run's shape and its level curve against the bosses it has to fit.

The floor-to-dungeon mapping stopped being a division once the Elite Four's
dungeons became five floors long, and a mapping with three regimes is exactly
the kind of thing that is off by one somewhere. This mirrors the C in
src/rogue_dungeon.c and checks:

  1. every floor of the run maps to exactly one dungeon, with a within-index
     inside that dungeon's length
  2. each dungeon has exactly one boss floor, on its last floor, and they land
     on the intended display floors
  3. mini bosses exist only in ten-floor dungeons, never in an Elite Four one
  4. the level curve never goes backwards
  5. the curve lands near each boss's actual stock party average
  6. every sFloorMapOverride covers a contiguous run of exactly lastFloors
     floors, ending ON its dungeon's boss floor

The party levels are read out of src/data/trainers.party rather than typed in,
so this notices if a boss is swapped for one at a different level.

Check 6 is here rather than in check_encounter_flags.py because this is the file
that already mirrors the floor arithmetic. That tool asks whether an override's
MAP is registered; this one asks whether the override lands on the FLOORS it
was meant to. `within + lastFloors >= length_of(dungeon)` is the kind of
comparison that is off by one in silence - it would simply weather the wrong
floor, on the far side of ninety floors of run.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# Mirrors include/rogue_dungeon.h.
LONG_FLOORS, SHORT_FLOORS = 10, 5
MINIBOSS_FLOOR = 4
GYM_DUNGEONS, E4_DUNGEONS = 8, 5
DUNGEON_COUNT = GYM_DUNGEONS + E4_DUNGEONS + 1

GYM_FLOORS = GYM_DUNGEONS * LONG_FLOORS          # 80
E4_FLOORS = E4_DUNGEONS * SHORT_FLOORS           # 25
E4_END_FLOOR = GYM_FLOORS + E4_FLOORS            # 105
TOTAL_FLOORS = E4_END_FLOOR + LONG_FLOORS        # 115

BASE_LEVEL = 5
LEVEL_NUM, E4_NUM, FINAL_NUM, LEVEL_DEN = 51, 48, 160, 100

# Parallel to sDungeonBosses.
BOSSES = [
    "TRAINER_ROXANNE_1", "TRAINER_BRAWLY_1", "TRAINER_WATTSON_1",
    "TRAINER_FLANNERY_1", "TRAINER_NORMAN_1", "TRAINER_WINONA_1",
    "TRAINER_TATE_AND_LIZA_1", "TRAINER_JUAN_1",
    "TRAINER_SIDNEY", "TRAINER_PHOEBE", "TRAINER_GLACIA", "TRAINER_DRAKE",
    "TRAINER_WALLACE", "TRAINER_STEVEN",
]
RIVAL = "TRAINER_ROGUE_RIVAL"

# How far above the curve a boss is allowed to sit before this complains. The
# gym stretch deliberately runs a few levels under its leaders, and Steven is
# deliberately further over than anyone - both are checked separately.
TOLERANCE = 5


def length_of(dungeon):
    if dungeon < GYM_DUNGEONS:
        return LONG_FLOORS
    if dungeon < GYM_DUNGEONS + E4_DUNGEONS:
        return SHORT_FLOORS
    return LONG_FLOORS


def index_of(floor):
    if floor < GYM_FLOORS:
        return floor // LONG_FLOORS
    if floor < E4_END_FLOOR:
        return GYM_DUNGEONS + (floor - GYM_FLOORS) // SHORT_FLOORS
    return DUNGEON_COUNT - 1


def within(floor):
    if floor < GYM_FLOORS:
        return floor % LONG_FLOORS
    if floor < E4_END_FLOOR:
        return (floor - GYM_FLOORS) % SHORT_FLOORS
    return floor - E4_END_FLOOR


def is_boss_floor(floor):
    return within(floor) == length_of(index_of(floor)) - 1


def is_miniboss_floor(floor):
    return (length_of(index_of(floor)) == LONG_FLOORS
            and within(floor) == MINIBOSS_FLOOR)


def target_level(floor):
    level = BASE_LEVEL
    if floor <= GYM_FLOORS:
        return level + (floor * LEVEL_NUM) // LEVEL_DEN
    level += (GYM_FLOORS * LEVEL_NUM) // LEVEL_DEN
    if floor < E4_END_FLOOR:
        return level + ((floor - GYM_FLOORS) * E4_NUM) // LEVEL_DEN
    level += (E4_FLOORS * E4_NUM) // LEVEL_DEN
    level += ((floor - E4_END_FLOOR) * FINAL_NUM) // LEVEL_DEN
    return level


def party_levels():
    """Average and range of every trainer's party, from the .party source."""
    txt = (REPO / "src/data/trainers.party").read_text(encoding="utf-8",
                                                       errors="replace")
    parts = re.split(r"^===\s*(\S+)\s*===\s*$", txt, flags=re.M)
    out = {}
    for i in range(1, len(parts), 2):
        levels = [int(m) for m in
                  re.findall(r"^Level:\s*(\d+)", parts[i + 1], flags=re.M)]
        if levels:
            out[parts[i]] = levels
    return out


def floor_map_overrides():
    """(theme name, lastFloors, map) from sFloorMapOverrides in the C.

    The Elite Four's theme index IS their dungeon index - that is what
    ThemeForFloor's modulo relies on and what MapForFloor compares through - so
    the theme name resolves to a dungeon by its position in enum DungeonThemeId.
    Both are parsed rather than assumed, so a reordered enum shows up here.
    """
    text = (REPO / 'src/rogue_dungeon.c').read_text(errors='replace')
    enum = re.search(r'enum DungeonThemeId\s*\{(.*?)\n\};', text, re.S)
    order = [n for n in re.findall(r'^\s*(DUNGEON_THEME_\w+)\s*,', enum.group(1), re.M)
             if n != 'DUNGEON_THEME_COUNT']
    table = re.search(r'sFloorMapOverrides\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if not table:
        return []
    out = []
    for theme, last, mapped in re.findall(
            r'\{\s*(DUNGEON_THEME_\w+)\s*,\s*(\d+)\s*,\s*(MAP_\w+)\s*\}', table.group(1)):
        out.append((theme, order.index(theme), int(last), mapped))
    return out


def main():
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)

    # 1. the mapping covers the run exactly once, with sane within-indices.
    seen = {}
    for floor in range(TOTAL_FLOORS):
        d, w = index_of(floor), within(floor)
        check(0 <= d < DUNGEON_COUNT, f"floor {floor + 1}: dungeon {d} out of range")
        check(0 <= w < length_of(d),
              f"floor {floor + 1}: within {w} outside dungeon {d} "
              f"of length {length_of(d)}")
        seen.setdefault(d, []).append(w)

    check(len(seen) == DUNGEON_COUNT,
          f"{len(seen)} dungeons reached, expected {DUNGEON_COUNT}")
    for d, ws in seen.items():
        check(ws == list(range(length_of(d))),
              f"dungeon {d}: within-indices {ws} are not 0..{length_of(d) - 1}")

    # 2. one boss per dungeon, on the intended display floor.
    boss_floors = [f for f in range(TOTAL_FLOORS) if is_boss_floor(f)]
    expected = [10, 20, 30, 40, 50, 60, 70, 80, 85, 90, 95, 100, 105, 115]
    check([f + 1 for f in boss_floors] == expected,
          f"boss floors {[f + 1 for f in boss_floors]} != {expected}")
    check(len(BOSSES) == DUNGEON_COUNT,
          f"{len(BOSSES)} bosses for {DUNGEON_COUNT} dungeons")

    # 3. mini bosses only in full-length dungeons.
    mini = [f + 1 for f in range(TOTAL_FLOORS) if is_miniboss_floor(f)]
    check(mini == [5, 15, 25, 35, 45, 55, 65, 75, 110],
          f"mini boss floors are {mini}")
    for f in range(TOTAL_FLOORS):
        check(not (is_boss_floor(f) and is_miniboss_floor(f)),
              f"floor {f + 1} is both a boss and a mini boss floor")

    # 4. the curve never goes backwards.
    for f in range(1, TOTAL_FLOORS):
        check(target_level(f) >= target_level(f - 1),
              f"curve dips at floor {f + 1}: "
              f"{target_level(f - 1)} -> {target_level(f)}")

    # 5. the curve against the parties it has to fit.
    parties = party_levels()
    missing = [t for t in BOSSES + [RIVAL] if t not in parties]
    if missing:
        print("MISSING from trainers.party: " + ", ".join(missing))
        return 1

    print(f"{'floor':>6}  {'boss':<24} {'party':>12} {'avg':>5} "
          f"{'curve':>6} {'gap':>5}")
    for d, floor in enumerate(boss_floors):
        lv = parties[BOSSES[d]]
        avg = sum(lv) / len(lv)
        curve = target_level(floor)
        gap = avg - curve
        print(f"{floor + 1:>6}  {BOSSES[d]:<24} "
              f"{min(lv):>5}-{max(lv):<6} {avg:>5.1f} {curve:>6} {gap:>+5.1f}")
        if BOSSES[d] != "TRAINER_STEVEN":
            check(abs(gap) <= TOLERANCE,
                  f"floor {floor + 1}: {BOSSES[d]} avg {avg:.1f} is "
                  f"{gap:+.1f} off the curve's {curve}")

    # Steven is meant to stand above the curve, but not out of sight of it.
    steven_floor = boss_floors[-1]
    steven = parties["TRAINER_STEVEN"]
    gap = sum(steven) / len(steven) - target_level(steven_floor)
    check(3 <= gap <= 8,
          f"Steven sits {gap:+.1f} over the curve; intended +3 to +8")

    rival_floor = mini[-1] - 1
    rival = parties[RIVAL]
    rgap = sum(rival) / len(rival) - target_level(rival_floor)
    print(f"{rival_floor + 1:>6}  {RIVAL:<24} "
          f"{min(rival):>5}-{max(rival):<6} {sum(rival) / len(rival):>5.1f} "
          f"{target_level(rival_floor):>6} {rgap:>+5.1f}")
    check(abs(rgap) <= TOLERANCE,
          f"floor {rival_floor + 1}: rival is {rgap:+.1f} off the curve")

    # 6. per-floor map overrides land where they were meant to.
    overrides = floor_map_overrides()
    if overrides:
        print()
        for theme, dungeon, last, mapped in overrides:
            # MapForFloor's own condition, transcribed.
            covered = [f for f in range(TOTAL_FLOORS)
                       if index_of(f) == dungeon
                       and within(f) + last >= length_of(dungeon)]
            shown = [f + 1 for f in covered]
            print(f"{mapped} covers floors {shown} ({theme})")

            check(len(covered) == last,
                  f"{mapped} covers {len(covered)} floors, table says {last}")
            check(covered == list(range(covered[0], covered[-1] + 1)) if covered
                  else False,
                  f"{mapped} covers a non-contiguous set of floors: {shown}")
            # The tail of a dungeon ends on its boss, so the last covered floor
            # must be one - and none of the earlier ones may be. If this drifts,
            # the weather stops at the arena door or starts inside it.
            check(covered and is_boss_floor(covered[-1]),
                  f"{mapped}'s last floor {shown[-1] if shown else '-'} "
                  f"is not a boss floor")
            check(not any(is_boss_floor(f) for f in covered[:-1]),
                  f"{mapped} covers a boss floor that is not its last")

    print(f"\nrun: {TOTAL_FLOORS} floors, {DUNGEON_COUNT} dungeons, "
          f"levels {target_level(0)} to {target_level(TOTAL_FLOORS - 1)}")

    if fails:
        print("\nFAILED:")
        for f in fails:
            print("  " + f)
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
