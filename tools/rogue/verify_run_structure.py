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
  7. EVERY REGION VARIANT of every dungeon keeps its boss inside
     SHUFFLE_TOLERANCE of the curve at that dungeon's own slot

The party levels are read out of src/data/trainers.party rather than typed in,
so this notices if a boss is swapped for one at a different level.

THE POSITION PERMUTATION IS RETIRED, and check 7 shrank with it. It used to
enumerate 384 order words - the eight gyms in bands of two, four of the five
Elite Four slots - and prove that no arrangement moved a boss further off the
curve than SHUFFLE_TOLERANCE. A dungeon does not move any more, so the sweep is
the region variants alone: four bosses per slot, each authored against that
slot.

That is a strictly tighter statement than the one it replaces. The band was only
ever two wide because a wider one broke this check - bands of four reach -16.5,
and a free permutation of all eight is ruinous both ways, Juan on floor 10 at
+33.8 and a level-13 two-mon Roxanne on floor 80 at -32.0. None of those runs
are reachable now, and the numbers stay here because they are the reason the
feature was retired rather than widened.

Broken two ways to confirm it fires: tolerance tightened to 6, which trips on
Sidney at -6.6; and the free-permutation sweep above, which the tolerance
rejects by a factor of nearly four.

Check 6 is here rather than in check_encounter_flags.py because this is the file
that already mirrors the floor arithmetic. That tool asks whether an override's
MAP is registered; this one asks whether the override lands on the FLOORS it
was meant to. `within + lastFloors >= length_of(dungeon)` is the kind of
comparison that is off by one in silence - it would simply weather the wrong
floor, on the far side of ninety floors of run.
"""
import itertools
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

# Parallel to the SECOND block of sDungeonBosses - the Kanto counterpart of the
# identity at the same position. Paired by position in its own game, not by type.
#
# THE LEVELS ARE STOCK FIRERED AND ARE NOT SCALED, which was a decision and not
# an oversight: these bosses are only reachable after a clear, so they are
# allowed to be lumpier than the ordering a first playthrough walks - the same
# argument that lets the order shuffle ship with no scaling. What that costs is
# measured below rather than asserted, and it is concentrated in exactly two
# rows. See KANTO_TOLERANCE.
#
# Red is the fourteenth and is authored rather than ported; his levels are the
# GSC Mt. Silver team's. See constants/opponents.h.
KANTO_BOSSES = [
    "TRAINER_ROGUE_KANTO_BROCK", "TRAINER_ROGUE_KANTO_MISTY",
    "TRAINER_ROGUE_KANTO_LT_SURGE", "TRAINER_ROGUE_KANTO_ERIKA",
    "TRAINER_ROGUE_KANTO_KOGA", "TRAINER_ROGUE_KANTO_SABRINA",
    "TRAINER_ROGUE_KANTO_BLAINE", "TRAINER_ROGUE_KANTO_GIOVANNI",
    "TRAINER_ROGUE_KANTO_LORELEI", "TRAINER_ROGUE_KANTO_BRUNO",
    "TRAINER_ROGUE_KANTO_AGATHA", "TRAINER_ROGUE_KANTO_LANCE",
    "TRAINER_ROGUE_KANTO_BLUE",
    "TRAINER_ROGUE_KANTO_RED",
]

# Parallel to the THIRD block of sDungeonBosses.
#
# UNLIKE KANTO, THESE ARE SCALED, and the difference is the whole reason there
# is no JOHTO_TOLERANCE below. Johto's stock late game runs 5-10 levels UNDER
# this curve - Karen -10.0, Lance -9.3 - and under is not the same kind of wrong
# as over: a Kanto boss +10 is a hard fight, a Johto boss -10 at floor 105 is a
# pushover. tools/rogue/gen_johto_parties.py lifts slots 6-12 onto the curve by
# a per-party offset, targeting the SAME gap the Hoenn boss at that identity
# sits at, so all three regions have the same difficulty shape.
#
# The consequence to check for: these must fit inside SHUFFLE_TOLERANCE, the
# HOENN bound, with no allowance of their own. If a Johto row ever needs one,
# the scaling has drifted and the tool is what to fix.
#
# Koga, Bruno and Lance are their Gen 2 selves, with their own trainer ids -
# not the Kanto rows of the same name. Ethan is authored and is the finale,
# because Red is already Kanto's.
JOHTO_BOSSES = [
    "TRAINER_ROGUE_JOHTO_FALKNER", "TRAINER_ROGUE_JOHTO_BUGSY",
    "TRAINER_ROGUE_JOHTO_WHITNEY", "TRAINER_ROGUE_JOHTO_MORTY",
    "TRAINER_ROGUE_JOHTO_CHUCK", "TRAINER_ROGUE_JOHTO_JASMINE",
    "TRAINER_ROGUE_JOHTO_PRYCE", "TRAINER_ROGUE_JOHTO_CLAIR",
    "TRAINER_ROGUE_JOHTO_WILL", "TRAINER_ROGUE_JOHTO_KOGA",
    "TRAINER_ROGUE_JOHTO_BRUNO", "TRAINER_ROGUE_JOHTO_KAREN",
    "TRAINER_ROGUE_JOHTO_LANCE",
    "TRAINER_ROGUE_JOHTO_ETHAN",
]


# Parallel to the FOURTH block. Scaled like Johto and for the same reason,
# though Sinnoh drifts the other way: its stock teams run a uniform +5 to +8
# OVER this curve. That is Kanto's direction but not Kanto's shape - Kanto is
# two outlier rows on a decent fit, which a wider tolerance covers honestly,
# while Sinnoh is thirteen rows on a different curve.
#
# PLATINUM'S GYM ORDER: Fantina third, Maylene fourth. Diamond/Pearl's order
# puts a level 32 Lucario against a curve of 19.
#
# Dawn is the finale, her anime team fully evolved. Barry is the rival and is
# absent from the boss table, like Silver.
SINNOH_BOSSES = [
    "TRAINER_ROGUE_SINNOH_ROARK", "TRAINER_ROGUE_SINNOH_GARDENIA",
    "TRAINER_ROGUE_SINNOH_FANTINA", "TRAINER_ROGUE_SINNOH_MAYLENE",
    "TRAINER_ROGUE_SINNOH_CRASHER_WAKE", "TRAINER_ROGUE_SINNOH_BYRON",
    "TRAINER_ROGUE_SINNOH_CANDICE", "TRAINER_ROGUE_SINNOH_VOLKNER",
    "TRAINER_ROGUE_SINNOH_AARON", "TRAINER_ROGUE_SINNOH_BERTHA",
    "TRAINER_ROGUE_SINNOH_FLINT", "TRAINER_ROGUE_SINNOH_LUCIAN",
    "TRAINER_ROGUE_SINNOH_CYNTHIA",
    "TRAINER_ROGUE_SINNOH_DAWN",
]

REGIONS = {"H": BOSSES, "K": KANTO_BOSSES, "J": JOHTO_BOSSES,
           "S": SINNOH_BOSSES}

# Per-region bound. Hoenn's 9 is the tight one that guards the shuffle's band
# width; Kanto has 12 because its stock levels were deliberately left alone;
# JOHTO IS BACK ON HOENN'S 9 because its levels were scaled instead. That
# asymmetry is the record of two different decisions and is not tidiable.
TOLERANCE_FOR = {"H": None, "K": None, "J": None}   # filled in below

RIVAL = "TRAINER_ROGUE_RIVAL"

# How far above the curve a boss is allowed to sit before this complains. The
# gym stretch deliberately runs a few levels under its leaders, and Steven is
# deliberately further over than anyone - both are checked separately.
TOLERANCE = 5

# SHUFFLE_BAND, GYM_BANDS and E4_SHUFFLED are gone with the constants they
# mirrored. Nothing permutes positions any more.

# How far off the curve a SHUFFLED boss may sit. Wider than TOLERANCE on purpose:
# the shuffle is gated behind a first clear, so it is allowed to be lumpier than
# the ordering a first playthrough walks. 9 is the measured worst case at bands of
# two (-6.5 Norman at slot 5, +8.0 Brawly at slot 0) plus a level of slack, and it
# is deliberately tight enough that widening a band trips it.
SHUFFLE_TOLERANCE = 9

# The same measure for a KANTO boss, and it is a SEPARATE NUMBER ON PURPOSE.
#
# The obvious move was to widen SHUFFLE_TOLERANCE until Kanto fitted under it.
# That is wrong, and it is wrong in the way this repo's notes keep warning
# about: SHUFFLE_TOLERANCE at 9 is tight enough that raising DUNGEON_SHUFFLE_BAND
# trips it, which is the entire reason it exists. Widening it to admit Koga would
# have bought Kanto in and quietly retired the check that guards band width -
# a check that stops catching what it was written for is worth nothing.
#
# So the Hoenn rows keep their 9 and the Kanto rows get their own bound, and the
# sweep below picks by region. The cost of the decision is then visible as a
# number rather than hidden inside a loosened constant.
#
# 12 is the measured worst case plus a level and a half. The worst is +10.5,
# MISTY at slot 0 - which is worth reading twice, because the row that needed the
# widening is not the one the stock levels predict. Koga and Sabrina are the two
# that miss against their OWN slots; the shuffle then moves other rows into slots
# that expose them, and Misty at 19.5 against floor 10's curve of 9 is worse than
# either. The full list is printed by the sweep below rather than kept here, so
# it cannot go stale the way a hand-written one would.
#
# The underlying cause is the same in every case: FireRed's mid-game jumps where
# Emerald's does not, and the gym band swap is free to land either side of a jump.
KANTO_TOLERANCE = 12

# Resolved here rather than at the definition above, because SHUFFLE_TOLERANCE
# and KANTO_TOLERANCE are declared between the two.
# Kanto is the ONLY region with an allowance of its own, and that is the point:
# it is the one whose levels were deliberately left stock. Johto and Sinnoh were
# scaled instead, so they answer to Hoenn's bound like Hoenn does.
TOLERANCE_FOR = {"H": SHUFFLE_TOLERANCE, "K": KANTO_TOLERANCE,
                 "J": SHUFFLE_TOLERANCE, "S": SHUFFLE_TOLERANCE}
SCALED_REGIONS = ("J", "S")


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


# dungeon_for_slot and every_order are gone: BossRowForSlot is now the region
# roll alone, so a slot is its own identity and there is no order word to port.


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
    for tag, table in REGIONS.items():
        check(len(table) == len(BOSSES),
              f"region {tag} has {len(table)} bosses for {len(BOSSES)} "
              f"identities; every region block of sDungeonBosses must be the "
              f"same length")
    missing = [t for t in sum(REGIONS.values(), []) + [RIVAL]
               if t not in parties]
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

    # The finale is meant to stand above the curve, but not out of sight of it -
    # and BOTH finales are, because the region roll reaches slot 13 too.
    #
    # Red is held to Steven's band rather than to a Kanto one. He is authored,
    # not ported, so there is nothing to be faithful to and no reason to let him
    # drift: if his party ever needs a wider band than Steven's, the party is
    # what is wrong. That is the difference between this row and Koga's.
    #
    # DERIVED FROM THE TABLES, NOT NAMED. Writing "TRAINER_ROGUE_KANTO_RED" here
    # was the first version and it silently stopped testing the finale the moment
    # the last row of KANTO_BOSSES changed - the check went on measuring Red, who
    # was no longer standing there. Caught by the break harness, which is the
    # only reason it is not still written that way.
    finale_floor = boss_floors[-1]
    for finale in [t[-1] for t in REGIONS.values()]:
        lv = parties[finale]
        gap = sum(lv) / len(lv) - target_level(finale_floor)
        # Steven already has a row in the table above, printed by the loop over
        # BOSSES. Only the Kanto finale needs one adding.
        if finale not in BOSSES:
            print(f"{finale_floor + 1:>6}  {finale:<24} "
                  f"{min(lv):>5}-{max(lv):<6} {sum(lv) / len(lv):>5.1f} "
                  f"{target_level(finale_floor):>6} {gap:>+5.1f}")
        check(3 <= gap <= 8,
              f"{finale} sits {gap:+.1f} over the curve; intended +3 to +8")

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

    # 7. every permitted dungeon order keeps every boss near its slot's curve,
    #    IN EITHER REGION.
    #
    # Exhaustive, not sampled, and note what is NOT enumerated: the region word
    # is 14 independent bits, so 384 orders x 2^14 regions is 6.3 million runs.
    # It does not need enumerating. The tolerance is a per-boss test and the
    # region bits do not interact - whether Koga is too high at slot 4 does not
    # depend on who is standing at slot 7 - so sweeping (slot, identity, region)
    # triples covers every one of those 6.3 million runs with 392 comparisons.
    # Enumerating the product instead would take minutes to prove the same thing.
    #
    # The finale is excluded from the sweep: neither Steven nor Red permutes, and
    # both are checked above with a band of their own.
    print()
    boss_slots = [s for s in range(DUNGEON_COUNT - 1)]
    worst_low = (0.0, None)
    worst_high = (0.0, None)
    seen = {}

    for slot in boss_slots:
        identity = slot          # a dungeon stands where it was written to stand
        floor = sum(length_of(d) for d in range(slot + 1)) - 1

        for region, table in REGIONS.items():
            levels = parties[table[identity]]
            gap = sum(levels) / len(levels) - target_level(floor)
            bound = TOLERANCE_FOR[region]
            seen[(slot, identity, region)] = gap

            if gap < worst_low[0]:
                worst_low = (gap, (slot, identity, region, floor))
            if gap > worst_high[0]:
                worst_high = (gap, (slot, identity, region, floor))

            check(abs(gap) <= bound,
                  f"region {region}: {table[identity]} at slot {slot} "
                  f"(floor {floor + 1}) is {gap:+.1f} off the curve, past "
                  f"the {bound} that region allows")

    def describe(entry):
        gap, place = entry
        if place is None:
            return "none"
        slot, identity, region, floor = place
        return (f"{gap:+.1f}  {REGIONS[region][identity]} at slot {slot}, "
                f"floor {floor + 1}")

    print(f"shuffle: {len(REGIONS)} bosses per dungeon x "
          f"{len(REGIONS) ** DUNGEON_COUNT:,} region words, "
          f"{len(seen)} slot/boss pairings (positions do not permute)")
    print(f"  worst under curve  {describe(worst_low)}")
    print(f"  worst over curve   {describe(worst_high)}")
    print("  tolerance          " + ", ".join(
        f"{TOLERANCE_FOR[r]} {r}" for r in REGIONS))

    # Which rows need a bound wider than Hoenn's, named rather than left for
    # somebody to rediscover. Kanto's appear here by design - its levels were
    # deliberately left stock. JOHTO'S MUST NOT: they were scaled precisely so
    # they would fit the Hoenn bound, so a Johto row in this list means the
    # scaling has drifted and gen_johto_parties.py is what to fix.
    over_hoenn = sorted(
        (gap, slot, identity, region)
        for (slot, identity, region), gap in seen.items()
        if region != "H" and abs(gap) > SHUFFLE_TOLERANCE)
    if over_hoenn:
        print(f"  rows past the Hoenn {SHUFFLE_TOLERANCE}:")
        for gap, slot, identity, region in over_hoenn:
            print(f"    {gap:+.1f}  {REGIONS[region][identity]} at slot {slot}")
    for r in SCALED_REGIONS:
        bad = [x for x in over_hoenn if x[3] == r]
        check(not bad,
              f"a {r} row needs a wider bound than Hoenn's; that region's levels "
              f"are SCALED specifically so they would not, so the scaling has "
              f"drifted - fix the generator, not this number "
              f"({bad[0][1] if bad else ''})")

    # AND THE CONSTANT ITSELF, which is a separate assertion and was missing.
    # The rule above only fires once a Johto row actually exceeds Hoenn's bound,
    # so with the scaling healthy, raising TOLERANCE_FOR["J"] to 20 changes
    # nothing and is caught by nothing - the guard would simply be off, and stay
    # off until the day something drifted into the gap. Found by the break
    # harness, which is the only reason this line exists.
    for r in SCALED_REGIONS:
        check(TOLERANCE_FOR[r] == SHUFFLE_TOLERANCE,
              f"region {r}'s tolerance is {TOLERANCE_FOR[r]}, not Hoenn's "
              f"{SHUFFLE_TOLERANCE}. It is SCALED rather than left stock, which "
              f"is the whole reason it does not get an allowance of its own - "
              f"widening this hides scaling drift instead of reporting it")

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
