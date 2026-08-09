"""Verify the crafting tree against what a run can actually pick up.

A recipe is a promise that its ingredients can be held at the same time, and
nothing in the build checks that: name an item no table drops and the recipe
simply never fires, silently, forever. That is the same failure the species
pools had before check_species_pools.py, and the same one the dungeon trainers
had -- a table that compiles, ships, and quietly cannot happen.

Reads the real tables out of src/rogue_dungeon.c and src/data/crafting_recipes.h
rather than restating them. Checks:

  1. every item named in a recipe or in sLootMaterials exists in items.h
  2. every ingredient is obtainable somewhere -- a loot table, a berry table,
     the materials table, the run's granted items, or another recipe's output
  3. every recipe has an EARLIEST FLOOR: the first floor on which all of its
     ingredients are simultaneously in band. A recipe with no such floor is
     dead, and one whose ingredients only overlap for a few floors is fragile.
  4. deep consumables are not free. Coming online before the loot table drops
     the same item is the FEATURE -- a Full Restore forty floors early is what
     crafting is for -- so a blanket "never earlier" is the wrong rule and the
     cheap tiers should be allowed to break it. What must not happen is a deep
     item costing only shallow materials. So: any result the loot table gates
     past DEEP_FLOOR must want at least one ingredient whose own earliest floor
     is at least SCARCE_FLOOR.

     The first version of this check did use "never earlier" and failed nine
     recipes, of which three were a real bug (the material-upgrade recipes made
     every late material reachable on floor 0) and six were the rule being
     wrong. Both halves of that are worth keeping in mind: the check found a
     defect reading could not, and it also mis-stated the design until the
     defect was fixed and the remainder looked at one at a time.
  5. hidden-item placements stay inside DUNGEON_MAX_HIDDEN at floor 115

Usage: check_craft_recipes.py <repo>
"""
import re
import sys
from pathlib import Path

if len(sys.argv) < 2:
    raise SystemExit("usage: check_craft_recipes.py <repo>")
REPO = Path(sys.argv[1])

RUN_FLOORS = 115

dungeon = (REPO / "src/rogue_dungeon.c").read_text(encoding="utf-8")
recipes_src = (REPO / "src/data/crafting_recipes.h").read_text(encoding="utf-8")
items_h = (REPO / "include/constants/items.h").read_text(encoding="utf-8")
constants = (REPO / "include/constants/rogue_dungeon.h").read_text(encoding="utf-8")
grant = (REPO / "data/maps/RogueDungeonFloor/scripts.inc").read_text(encoding="utf-8")

known_items = set(re.findall(r"\b(ITEM_[A-Z0-9_]+)\b", items_h))

failures = []
notes = []


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def parse_loot(name):
    m = re.search(r"static const struct RogueLootEntry %s\[\]\s*=\s*\{(.*?)\n\};"
                  % name, dungeon, re.S)
    if not m:
        raise SystemExit("could not find %s" % name)
    rows = []
    for line in strip_comments(m.group(1)).splitlines():
        e = re.match(r"\s*\{\s*(ITEM_[A-Z0-9_]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}",
                     line)
        if e:
            rows.append((e.group(1), int(e.group(2)), int(e.group(3)), int(e.group(4))))
    return rows


tables = {n: parse_loot(n) for n in
          ("sLootConsumables", "sLootHeld", "sLootBerries", "sLootMaterials")}

# Buried stones get a slot of their own rather than a band.
stones = re.search(r"static const u16 sBuriedStones\[\]\s*=\s*\{(.*?)\n\};", dungeon, re.S)
stone_items = set(re.findall(r"\b(ITEM_[A-Z0-9_]+)\b", strip_comments(stones.group(1))))
stone_first = int(re.search(r"#define DUNGEON_STONE_FIRST_FLOOR\s+(\d+)", constants).group(1))

granted = set(re.findall(r"additem\s+(ITEM_[A-Z0-9_]+)", grant))

# floors on which an item can be picked up
avail = {}
for name, rows in tables.items():
    for item, weight, lo, hi in rows:
        hi = RUN_FLOORS if hi >= 255 else hi
        cur = avail.setdefault(item, set())
        cur.update(range(lo, min(hi, RUN_FLOORS) + 1))
for item in stone_items:
    avail.setdefault(item, set()).update(range(stone_first, RUN_FLOORS + 1))
for item in granted:
    avail.setdefault(item, set()).update(range(0, RUN_FLOORS + 1))

# ---- parse the recipes -----------------------------------------------------
body = strip_comments(recipes_src)
recipes = []
for m in re.finditer(r"CRAFT_ONE\(\s*(ITEM_[A-Z0-9_]+)\s*,\s*(\d+)\s*,\s*\{(.*?)\}\s*\)",
                     body, re.S):
    result, qty, pattern = m.group(1), int(m.group(2)), m.group(3)
    ingredients = re.findall(r"\b(ITEM_[A-Z0-9_]+)\b", pattern)
    recipes.append((result, qty, ingredients))

if not recipes:
    failures.append("parsed no recipes at all -- the CRAFT_ONE form changed")

# 1. every named item exists
for item, _, lo, hi in [r for rows in tables.values() for r in rows]:
    if item not in known_items:
        failures.append("loot table names %s, which is not in items.h" % item)
for result, _, ingredients in recipes:
    for item in [result] + ingredients:
        if item not in known_items:
            failures.append("recipe names %s, which is not in items.h" % item)

# a recipe output is itself obtainable, so later recipes may build on earlier
craftable_from = {}
for result, _, ingredients in recipes:
    craftable_from.setdefault(result, []).append(ingredients)


def earliest(item, depth=0):
    """First floor `item` can be held, by pickup or by crafting."""
    best = min(avail[item]) if avail.get(item) else None
    if depth < 4:
        for ing in craftable_from.get(item, []):
            floors = [earliest(i, depth + 1) for i in ing]
            if all(f is not None for f in floors):
                made = max(floors)
                best = made if best is None else min(best, made)
    return best


# 2 + 3. ingredients obtainable, recipe has an earliest floor
for result, qty, ingredients in recipes:
    missing = [i for i in ingredients if not avail.get(i) and i not in craftable_from]
    if missing:
        failures.append("%s: ingredient(s) %s are dropped by nothing and crafted by nothing"
                        % (result, ", ".join(sorted(set(missing)))))
        continue

    common = None
    for i in ingredients:
        floors = set(avail.get(i, set()))
        for ing in craftable_from.get(i, []):
            sub = [earliest(x) for x in ing]
            if all(f is not None for f in sub):
                floors.update(range(max(sub), RUN_FLOORS + 1))
        common = floors if common is None else (common & floors)
    if not common:
        failures.append("%s: its ingredients are never all available on the same floor"
                        % result)
        continue
    notes.append((result, min(common), len(common)))

# 4. a deep consumable must cost at least one scarce material.
DEEP_FLOOR = 50     # loot bands at or past this are the run's top tier
SCARCE_FLOOR = 30   # a material arriving here or later counts as scarce

loot_first = {}
for item, _, lo, hi in tables["sLootConsumables"]:
    loot_first[item] = min(loot_first.get(item, 999), lo)

material_first = {}
for item, _, lo, hi in tables["sLootMaterials"]:
    material_first[item] = min(material_first.get(item, 999), lo)

for result, qty, ingredients in recipes:
    drop = loot_first.get(result)
    if drop is None or drop < DEEP_FLOOR:
        continue
    scarce = [i for i in ingredients if material_first.get(i, -1) >= SCARCE_FLOOR]
    if not scarce:
        failures.append("%s is not dropped until floor %d but its recipe wants no "
                        "material scarcer than floor %d -- it is a deep item at a "
                        "shallow price" % (result, drop, SCARCE_FLOOR))

for result, first, span in sorted(notes, key=lambda r: r[1]):
    drop = loot_first.get(result)
    if drop is not None and first < drop:
        print("  note: %s craftable at %d, dropped from %d (%d floors early)"
              % (result.replace("ITEM_", ""), first, drop, drop - first))

# 5. hidden placement cap
hid_min = int(re.search(r"#define DUNGEON_HIDDEN_MIN\s+(\d+)", constants).group(1))
per_extra = int(re.search(r"#define DUNGEON_HIDDEN_FLOORS_PER_EXTRA\s+(\d+)", constants).group(1))
materials = int(re.search(r"#define DUNGEON_MATERIALS_PER_FLOOR\s+(\d+)", constants).group(1))
cap = int(re.search(r"#define DUNGEON_MAX_HIDDEN\s+(\d+)", constants).group(1))
worst = hid_min + RUN_FLOORS // per_extra + materials
if worst > cap:
    failures.append("floor %d buries %d items against DUNGEON_MAX_HIDDEN %d"
                    % (RUN_FLOORS, worst, cap))

# ---- report ----------------------------------------------------------------
print("%d recipes, %d materials, %d buried at the deepest floor (cap %d)"
      % (len(recipes), len(tables["sLootMaterials"]), worst, cap))
print()
print("%-22s %-14s %s" % ("recipe", "first floor", "floors available"))
for result, first, span in sorted(notes, key=lambda r: (r[1], r[0])):
    print("  %-20s %-14d %d" % (result.replace("ITEM_", ""), first, span))

if failures:
    print()
    for f in failures:
        print("FAIL: %s" % f)
    raise SystemExit(1)

print()
print("ok: every recipe is reachable, and every deep item costs a scarce material")
