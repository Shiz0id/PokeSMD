#!/usr/bin/env python3
"""Find species a dungeon floor offers BELOW the level they could exist at.

Takes the repo POSITIONALLY, like check_safari_pool.py, check_species_in_rom.py,
check_craft_recipes.py and check_variant_colours.py. That makes five.

THE BUG THIS EXISTS FOR. A pool is a ladder read through a sliding window, and
the window is twelve wide on a gym theme -- so a dungeon's FIRST floor already
exposes the first twelve entries of its pool. If entry 6 is Dugtrio, the player
meets a Dugtrio on the first floor of the cave, at level 10, when a Diglett
cannot become one until 26. Nothing in the build says so: the species exists,
the encounter is legal, the level is legal, and the sixteen other checks are all
looking somewhere else.

It reports two different things and they want different fixes:

  EARLY   a species whose evolution chain needs a level the floor cannot reach.
          Concrete and objective -- Graveler needs Geodude at 25.

  STAGED  a fully evolved species that needs no LEVEL (it evolves by stone,
          trade or friendship) appearing in the shallow part of a pool. Not
          objectively wrong, but a Crobat on floor 3 reads exactly as odd as a
          Graveler does, and the fix is the same: move it up the ladder.

Exit 0 when clean, 1 on findings. Verify the harness too -- $? across the WSL
boundary has lied before. Use `cmd && echo pass || echo fail`.
"""

import re
import sys
from pathlib import Path

# Read off include/rogue_dungeon.h rather than restated, because these are the
# numbers the architecture note warns are off by one in the obvious direction.
CONSTS = {}
CONST_NAMES = [
    "DUNGEON_LONG_FLOORS", "DUNGEON_SHORT_FLOORS",
    "DUNGEON_GYM_DUNGEONS", "DUNGEON_E4_DUNGEONS",
    "DUNGEON_ENCOUNTER_BASE_LEVEL", "DUNGEON_ENCOUNTER_LEVEL_NUM",
    "DUNGEON_ENCOUNTER_E4_NUM", "DUNGEON_ENCOUNTER_FINAL_NUM",
    "DUNGEON_ENCOUNTER_LEVEL_DEN", "DUNGEON_ENCOUNTER_LEVEL_SPREAD",
    "DUNGEON_ENCOUNTER_WINDOW", "DUNGEON_ENCOUNTER_TIER_FLOORS",
]


# A stone, trade or friendship evolution has no level gate, so "too early" for
# one is a judgement rather than a fact. This is the level below which the
# judgement is safe: by 30 most level-based evolutions in Gen 1-4 have happened,
# so a fully evolved Pokemon below it is out of step with everything around it
# whatever route it took.
#
# The first version of this test asked whether the species arrived in the
# shallow HALF of its pool instead, which is the wrong question and produced
# thirty-odd confident false positives -- it flagged Aggron on floor 109 at
# level 65, where a fully evolved Pokemon is exactly what should be there. Depth
# within a pool is not depth within the run.
STAGED_LEVEL_CEILING = 30


def die(msg):
    print("check_pool_evolutions: %s" % msg, file=sys.stderr)
    sys.exit(2)


def read_consts(repo):
    text = (repo / "include/rogue_dungeon.h").read_text(encoding="utf-8")
    for name in CONST_NAMES:
        m = re.search(r"#define\s+%s\s+(\d+)" % name, text)
        if m is None:
            die("could not read %s from include/rogue_dungeon.h" % name)
        CONSTS[name] = int(m.group(1))

    CONSTS["DUNGEON_GYM_FLOORS"] = (CONSTS["DUNGEON_GYM_DUNGEONS"]
                                    * CONSTS["DUNGEON_LONG_FLOORS"])
    CONSTS["DUNGEON_E4_FLOORS"] = (CONSTS["DUNGEON_E4_DUNGEONS"]
                                   * CONSTS["DUNGEON_SHORT_FLOORS"])
    CONSTS["DUNGEON_E4_END_FLOOR"] = (CONSTS["DUNGEON_GYM_FLOORS"]
                                      + CONSTS["DUNGEON_E4_FLOORS"])
    CONSTS["DUNGEON_COUNT"] = (CONSTS["DUNGEON_GYM_DUNGEONS"]
                               + CONSTS["DUNGEON_E4_DUNGEONS"] + 1)


def dungeon_length(d):
    if d < CONSTS["DUNGEON_GYM_DUNGEONS"]:
        return CONSTS["DUNGEON_LONG_FLOORS"]
    if d < CONSTS["DUNGEON_GYM_DUNGEONS"] + CONSTS["DUNGEON_E4_DUNGEONS"]:
        return CONSTS["DUNGEON_SHORT_FLOORS"]
    return CONSTS["DUNGEON_LONG_FLOORS"]


def first_floor_of(d):
    if d < CONSTS["DUNGEON_GYM_DUNGEONS"]:
        return d * CONSTS["DUNGEON_LONG_FLOORS"]
    if d < CONSTS["DUNGEON_GYM_DUNGEONS"] + CONSTS["DUNGEON_E4_DUNGEONS"]:
        return (CONSTS["DUNGEON_GYM_FLOORS"]
                + (d - CONSTS["DUNGEON_GYM_DUNGEONS"]) * CONSTS["DUNGEON_SHORT_FLOORS"])
    return CONSTS["DUNGEON_E4_END_FLOOR"]


def floor_target_level(floor):
    """Mirror of FloorTargetLevel in src/rogue_dungeon.c, integer division and all."""
    lvl = CONSTS["DUNGEON_ENCOUNTER_BASE_LEVEL"]
    den = CONSTS["DUNGEON_ENCOUNTER_LEVEL_DEN"]

    if floor <= CONSTS["DUNGEON_GYM_FLOORS"]:
        return lvl + (floor * CONSTS["DUNGEON_ENCOUNTER_LEVEL_NUM"]) // den

    lvl += (CONSTS["DUNGEON_GYM_FLOORS"] * CONSTS["DUNGEON_ENCOUNTER_LEVEL_NUM"]) // den

    if floor < CONSTS["DUNGEON_E4_END_FLOOR"]:
        return lvl + ((floor - CONSTS["DUNGEON_GYM_FLOORS"])
                      * CONSTS["DUNGEON_ENCOUNTER_E4_NUM"]) // den

    lvl += (CONSTS["DUNGEON_E4_FLOORS"] * CONSTS["DUNGEON_ENCOUNTER_E4_NUM"]) // den
    lvl += ((floor - CONSTS["DUNGEON_E4_END_FLOOR"])
            * CONSTS["DUNGEON_ENCOUNTER_FINAL_NUM"]) // den
    return lvl


def read_pools(repo):
    """Ordered species arrays, and the theme table's use of them in dungeon order."""
    text = (repo / "src/rogue_dungeon.c").read_text(encoding="utf-8")

    pools = {}
    for name, blob in re.findall(
            r"static const u16 (s\w*Species)\[\]\s*=\s*\{(.*?)\};", text, re.S):
        blob = re.sub(r"//[^\n]*", "", blob)
        pools[name] = re.findall(r"SPECIES_\w+", blob)

    # The theme table lists themes in DUNGEON ORDER, so the nth `.species =`
    # is dungeon n. Reading the order out of the file rather than hardcoding it
    # means adding a theme does not silently shift every report by one.
    themes = []
    for m in re.finditer(r"\.species\s*=\s*(s\w*Species)\s*,", text):
        name = m.group(1)
        tail = text[m.end():m.end() + 400]
        wm = re.search(r"\.encounterWindow\s*=\s*([^,]+),", tail)
        window = None
        if wm:
            w = wm.group(1).strip()
            if w.isdigit():
                window = int(w)
            else:
                am = re.match(r"ARRAY_COUNT\((s\w*Species)\)", w)
                if am:
                    window = len(pools.get(am.group(1), []))
        themes.append((name, window))
    return pools, themes


def read_evolutions(repo):
    """species -> list of (method, param, target), across every species_info file."""
    evo = {}
    root = repo / "src/data/pokemon/species_info"
    files = sorted(root.glob("*.h")) if root.is_dir() else []
    if not files:
        die("no species_info headers under %s" % root)

    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r"\[(SPECIES_\w+)\]\s*=", text):
            species = m.group(1)
            blob = text[m.end():m.end() + 12000]
            nxt = re.search(r"\n\s*\[SPECIES_\w+\]\s*=", blob)
            if nxt:
                blob = blob[:nxt.start()]

            i = blob.find(".evolutions")
            if i == -1:
                continue
            j = blob.find("EVOLUTION(", i)
            if j == -1:
                continue

            # EVOLUTION(...) SPANS LINES and its branches sit behind their own
            # #ifs. Read to the balanced paren; taking one line loses branches,
            # which is the trap gen_safari_pool.py records after it lost seven
            # of Eevee's eight.
            k, depth = j + len("EVOLUTION("), 1
            while k < len(blob) and depth:
                depth += (blob[k] == "(") - (blob[k] == ")")
                k += 1
            body = blob[j:k]

            branches = []
            for br in re.findall(r"\{([^{}]*)\}", body):
                parts = [p.strip() for p in br.split(",")]
                if len(parts) < 3:
                    continue
                method, param, target = parts[0], parts[1], parts[2]
                if not target.startswith("SPECIES_"):
                    continue
                branches.append((method, param, target))
            if branches:
                evo[species] = branches
    return evo


def build_prevo(evo):
    prevo = {}
    for src, branches in evo.items():
        for method, param, target in branches:
            prevo.setdefault(target, []).append((src, method, param))
    return prevo


def min_level(species, prevo, seen=None):
    """Lowest level at which this species could legitimately exist.

    Walks back to the base form. A level-based step contributes its level; a
    stone, trade or friendship step contributes nothing, because those have no
    level requirement at all -- but the CHAIN still has to get through any
    level steps below it. Where a species has several pre-evolutions the
    cheapest route wins, which is the honest reading: the player only needs one.
    """
    if seen is None:
        seen = set()
    if species in seen:
        return 0
    seen = seen | {species}

    parents = prevo.get(species)
    if not parents:
        return 0

    best = None
    for src, method, param in parents:
        need = min_level(src, prevo, seen)
        if method.startswith("EVO_LEVEL"):
            try:
                need = max(need, int(param))
            except ValueError:
                pass
        best = need if best is None else min(best, need)
    return best or 0


def stage_of(species, prevo, seen=None):
    if seen is None:
        seen = set()
    if species in seen:
        return 0
    seen = seen | {species}
    parents = prevo.get(species)
    if not parents:
        return 0
    return 1 + max(stage_of(s, prevo, seen) for s, _, _ in parents)


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        die("usage: check_pool_evolutions.py <repo>")
    repo = Path(argv[1])
    if not repo.is_dir():
        die("not a directory: %s" % repo)

    read_consts(repo)
    pools, themes = read_pools(repo)
    evo = read_evolutions(repo)
    prevo = build_prevo(evo)

    spread = CONSTS["DUNGEON_ENCOUNTER_LEVEL_SPREAD"]
    default_window = CONSTS["DUNGEON_ENCOUNTER_WINDOW"]
    tier_floors = CONSTS["DUNGEON_ENCOUNTER_TIER_FLOORS"]

    early = []      # (deficit, dungeon, pool, floor, level, species, need)
    staged = []     # (dungeon, pool, floor, level, species, stage)

    for d, (pool_name, window) in enumerate(themes):
        species_list = pools.get(pool_name, [])
        if not species_list:
            continue
        window = window or default_window
        length = dungeon_length(d)
        base = first_floor_of(d)

        # Earliest floor each species becomes live, so each is reported once at
        # its worst rather than once per floor it survives on.
        first_seen = {}
        for within in range(length):
            floor = base + within
            tiers = min(window + within // tier_floors, len(species_list))
            bottom = tiers - window if tiers > window else 0
            for s in species_list[bottom:tiers]:
                if s not in first_seen:
                    first_seen[s] = floor

        for s, floor in sorted(first_seen.items(), key=lambda kv: kv[1]):
            top_level = floor_target_level(floor) + spread
            need = min_level(s, prevo)
            if need > top_level:
                early.append((need - top_level, d, pool_name, floor,
                              top_level, s, need))
            elif stage_of(s, prevo) >= 1 and top_level < STAGED_LEVEL_CEILING:
                staged.append((d, pool_name, floor, top_level, s,
                               stage_of(s, prevo)))

    early.sort(reverse=True)

    print("check_pool_evolutions: %d species arrive below their evolution level, "
          "%d evolved species arrive in the shallow half of a pool"
          % (len(early), len(staged)))

    if early:
        print()
        print("BELOW THEIR EVOLUTION LEVEL (floor is 0-based, level includes the "
              "+%d spread)" % spread)
        print("  %-5s %-22s %-16s %6s %6s %6s" %
              ("dgn", "pool", "species", "floor", "level", "needs"))
        for deficit, d, pool, floor, lvl, s, need in early:
            print("  %-5d %-22s %-16s %6d %6d %6d   (%d early)"
                  % (d, pool.replace("Species", ""), s[len("SPECIES_"):],
                     floor, lvl, need, deficit))

    if staged:
        print()
        print("FULLY EVOLVED, NO LEVEL REQUIREMENT, ARRIVING IN THE SHALLOW HALF")
        for d, pool, floor, lvl, s, stage in staged:
            print("  %-5d %-22s %-16s %6d %6d   stage %d"
                  % (d, pool.replace("Species", ""), s[len("SPECIES_"):],
                     floor, lvl, stage))

    return 1 if early else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
