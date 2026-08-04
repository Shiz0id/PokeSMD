"""Check every dungeon theme can actually spawn wild Pokemon.

A metatile id means something different under every tileset pair, so a theme
whose floor id is right for one pair can silently resolve to unrelated art -
and to an unrelated BEHAVIOUR - under its own. Behaviour is what gates
encounters: MB_CAVE and MB_INDOOR_ENCOUNTER carry TILE_FLAG_HAS_ENCOUNTERS,
MB_NORMAL does not.

A theme passes if its floor carries encounters, or - for a grass theme like the
woods, where plain ground is deliberately safe - if its tall or long grass does.

Three separate things are checked, because every bug that has motivated this
file was a divergence BETWEEN them:

1. The theme table is sane - every theme paints something encounterable.
2. The generator actually reads the table. CarveFloor used to hard-code
   DUNGEON_METATILE_FLOOR for every cave theme, so the table could be perfect
   and the game still paint Lavaridge 0x201 - a green bush with MB_NORMAL -
   giving Fiery Path ten floors with no wild Pokemon. Check 1 cannot see that,
   and neither could theme_mock.py, which fills the grid from theme['floor']
   and so rendered the table's intent rather than the game's behaviour.
3. The engine will actually ASK. Checks 1 and 2 both passed while five of the
   thirteen themes had no wild Pokemon at all, because StandardWildEncounter
   gets there first:

     * It looks the map up in wild_encounters.json by mapGroup/mapNum, and
       bails on HEADER_NONE. Only MAP_ROGUE_DUNGEON_FLOOR was ever registered,
       so the four maps that exist only to carry a weather setting - snow, fog,
       petals, underwater - never got as far as the roguelike hook.
     * It then picks the land or the water branch from the tile under the
       player and bails if THAT branch's table is NULL. The ocean paints
       MB_OCEAN_WATER, took the water branch, and found a land-only entry.

   So this check derives each theme's branch from its own encounter surface -
   water is exactly TILE_FLAG_SURFABLE plus TILE_FLAG_HAS_ENCOUNTERS - and
   proves the theme declares that branch, that its map is registered, and that
   the branch's table is present and full length.

Usage:  python3 tools/rogue/check_encounter_flags.py
"""
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tileset_resolve import TilesetResolver

REPO = Path(__file__).resolve().parents[2]


def behaviour_names():
    src = (REPO / 'include/constants/metatile_behaviors.h').read_text(errors='replace')
    body = src[src.index('enum {') + 6:src.index('};')]
    names, val = {}, 0
    for line in body.split(','):
        line = re.sub(r'//.*', '', line).strip()
        if not line:
            continue
        if '=' in line:
            name, raw = line.split('=')
            name, val = name.strip(), int(raw.strip(), 0)
        else:
            name = line
        names[val] = name
        val += 1
    return names


def tile_flags():
    """{behaviour name: set of TILE_FLAG_ names} from sTileBitAttributes."""
    text = (REPO / 'src/metatile_behavior.c').read_text(errors='replace')
    body = re.search(r'sTileBitAttributes\[[^\]]*\]\s*=\s*\{(.*?)\n\};', text, re.S).group(1)
    return {m.group(1): set(re.findall(r'TILE_FLAG_\w+', m.group(2)))
            for m in re.finditer(r'\[(MB_\w+)\]\s*=\s*([^,\n]*)', body)}


def encounter_behaviours(flags):
    """Behaviour names carrying TILE_FLAG_HAS_ENCOUNTERS."""
    return {name for name, f in flags.items() if 'TILE_FLAG_HAS_ENCOUNTERS' in f}


def area_of_behaviour(flags, name):
    """WILD_AREA_ the engine routes this behaviour to.

    MetatileBehavior_IsWaterWildEncounter is IsSurfableWaterOrUnderwater and
    IsEncounterTile, and both of those are bare flag tests on sTileBitAttributes
    - so the branch is decided entirely by these two bits. IsLandWildEncounter
    is the same test with the surfable bit inverted.

    Note this is TILE_FLAG_SURFABLE, not MetatileBehavior_IsSurfableFishableWater
    - the two differ, and the difference is exactly the underwater seaweed, which
    is surfable-flagged but not in the fishable list. Encounters follow the flag.
    """
    return ('WILD_AREA_WATER' if 'TILE_FLAG_SURFABLE' in flags.get(name, set())
            else 'WILD_AREA_LAND')


def slot_counts():
    """{WILD_AREA_: encounter slot count} from the engine's constants."""
    text = (REPO / 'include/constants/wild_encounter.h').read_text(errors='replace')
    out = {}
    for area, macro in (('WILD_AREA_LAND', 'NUM_LAND_MONS_ENCOUNTER_SLOTS'),
                        ('WILD_AREA_WATER', 'NUM_WATER_MONS_ENCOUNTER_SLOTS')):
        out[area] = int(re.search(r'#define\s+' + macro + r'\s+\((\d+)\)', text).group(1))
    return out


# The JSON key each branch reads its table out of.
AREA_TABLE = {'WILD_AREA_LAND': 'land_mons', 'WILD_AREA_WATER': 'water_mons'}


def registered_maps():
    """{map id: {table name: mon count}} for every map with wild encounters.

    Only the for_maps group counts: GetCurrentMapWildMonHeaderId walks
    gWildMonHeaders and matches on mapGroup/mapNum, and that is the group it is
    built from. The Battle Pyramid's headers are reached another way entirely.
    """
    data = json.loads((REPO / 'src/data/wild_encounters.json').read_text(encoding='utf-8'))
    out = {}
    for group in data['wild_encounter_groups']:
        if not group.get('for_maps'):
            continue
        for entry in group['encounters']:
            if 'map' not in entry:
                continue
            out.setdefault(entry['map'], {}).update(
                {k: len(v['mons']) for k, v in entry.items() if k.endswith('_mons')})
    return out


def defines():
    """Metatile #defines from the roguelike header."""
    text = (REPO / 'include/rogue_dungeon.h').read_text(errors='replace')
    return {m.group(1): int(m.group(2), 0) for m in
            re.finditer(r'#define\s+(\w+)\s+(0x[0-9A-Fa-f]+)\b', text)}


def themes(defs):
    """Parse sDungeonThemes into {theme name: {field: value}}."""
    text = (REPO / 'src/rogue_dungeon.c').read_text(errors='replace')
    block = re.search(r'sDungeonThemes\[DUNGEON_THEME_COUNT\]\s*=\s*\{(.*?)\n\};',
                      text, re.S).group(1)
    out = {}
    parts = re.split(r'\[(DUNGEON_THEME_\w+)\]\s*=', block)
    for name, body in zip(parts[1::2], parts[2::2]):
        entry = {}
        for field in ('layoutId', 'mapId', 'wildArea', 'floor', 'tallGrass', 'longGrass'):
            m = re.search(r'\.' + field + r'\s*=\s*([A-Za-z0-9_]+)', body)
            if not m:
                continue
            token = m.group(1)
            entry[field] = (int(token, 0)
                            if re.fullmatch(r'0[xX][0-9A-Fa-f]+|\d+', token)
                            else defs.get(token, token))
        # wildArea is left out of every land theme, because WILD_AREA_LAND is 0
        # and eleven of the thirteen are land. Omission is the declaration.
        entry.setdefault('wildArea', 'WILD_AREA_LAND')
        out[name] = entry
    return out


def floor_map_overrides():
    """Parse sFloorMapOverrides into {theme name: [map, ...]}.

    These are maps a theme's LAST FEW FLOORS use instead of its own mapId. They
    need every registration the mapId needs, and they are reachable on two
    floors deep inside a run - so a missing entry would be found by playing to
    the Elite Four, if at all. Parsed from the table the game itself reads,
    rather than listed here, for the reason make_rest_stop.py parses the Unown
    spots: a duplicated table stops being true the moment the original moves.
    """
    text = (REPO / 'src/rogue_dungeon.c').read_text(errors='replace')
    m = re.search(r'sFloorMapOverrides\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if not m:
        return {}
    out = {}
    for theme, _last, mapped in re.findall(
            r'\{\s*(DUNGEON_THEME_\w+)\s*,\s*(\d+)\s*,\s*(MAP_\w+)\s*\}', m.group(1)):
        out.setdefault(theme, []).append(mapped)
    return out


def check_registration(mapped, table, derived, slots, registered):
    """The one place a map is held to its wild_encounters.json entry.

    Shared by a theme's own mapId and by its floor overrides deliberately. Two
    code paths checking the same property will eventually check it differently,
    and the override maps are precisely the ones nobody would notice drifting.
    """
    if mapped not in registered:
        return 1, (f'FAIL: {mapped} has no wild_encounters.json entry, so '
                   f'GetCurrentMapWildMonHeaderId returns HEADER_NONE')
    if table not in registered[mapped]:
        return 1, (f'FAIL: {mapped} has no {table}, so the {derived} branch '
                   f'reads NULL and returns')
    if registered[mapped][table] != slots[derived]:
        return 1, (f'FAIL: {mapped} {table} has {registered[mapped][table]} '
                   f'mons, need exactly {slots[derived]}')
    return 0, f'{mapped} {table} [{registered[mapped][table]} slots]'


def layout_tilesets():
    data = json.loads((REPO / 'data/layouts/layouts.json').read_text(encoding='utf-8'))
    return {L['id']: (L['primary_tileset'], L['secondary_tileset'])
            for L in data['layouts']}


def behaviour_of(resolver, primary, secondary, metatile):
    """Metatile behaviour under a tileset pair, or None if out of range."""
    symbol = primary if metatile < 0x200 else secondary
    index = metatile if metatile < 0x200 else metatile - 0x200
    attrs = resolver.resolve(symbol)['attributes'].read_bytes()
    if (index + 1) * 2 > len(attrs):
        return None
    return struct.unpack_from('<H', attrs, index * 2)[0] & 0xFF


def strip_file_scope_tables(text):
    """Blank out every `static const ... = { ... };` initialiser.

    What is left is function bodies. Themed metatile constants belong in the
    tables - that is what a table is - and must never appear in a function,
    because a function runs under every theme's tileset.
    """
    out = []
    pos = 0
    for m in re.finditer(r'static const [^;{]*?=\s*\{', text):
        if m.start() < pos:
            continue
        depth, i = 0, m.end() - 1
        while i < len(text):
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        out.append(text[pos:m.start()])
        pos = i
    out.append(text[pos:])
    return ''.join(out)


def check_generator_reads_the_table():
    """No themed metatile constant may appear inside a function.

    A source lint rather than a simulation, but it targets the exact regression:
    CarveFloor named DUNGEON_METATILE_FLOOR directly, so every cave theme got
    the cave's floor id under its own tileset. Anything a generator paints has
    to come from the theme it was handed.
    """
    text = (REPO / 'src/rogue_dungeon.c').read_text(errors='replace')
    bodies = re.sub(r'//[^\n]*', '', strip_file_scope_tables(text))

    bad = {name for name in
           re.findall(r'\b([A-Z][A-Z0-9_]*_METATILE_[A-Z0-9_]+)\b', bodies)
           if not name.startswith('MAPGRID_')}   # an engine mask, not a metatile
    for name in sorted(bad):
        print(f'    FAIL: {name} named inside a function')
    return len(bad)


def main():
    defs = defines()
    names = behaviour_names()
    flags = tile_flags()
    with_encounters = encounter_behaviours(flags)
    tilesets = layout_tilesets()
    slots = slot_counts()
    registered = registered_maps()
    resolver = TilesetResolver(REPO)
    overrides = floor_map_overrides()

    failures = 0
    all_themes = themes(defs)

    # A typo'd theme id in sFloorMapOverrides does not fail to compile - the
    # enum is a u8 field - it just names a theme the loop below never matches,
    # and the override silently never fires. Caught here rather than never.
    for theme in overrides:
        if theme not in all_themes:
            print(f'sFloorMapOverrides names {theme}, which is not a theme')
            failures += 1

    for theme, entry in all_themes.items():
        layout = entry.get('layoutId')
        if layout not in tilesets:
            print(f'{theme}: cannot resolve layout {layout}')
            failures += 1
            continue
        primary, secondary = tilesets[layout]
        print(f'{theme}  ({secondary})')

        surfaces = []
        for field in ('floor', 'tallGrass', 'longGrass'):
            metatile = entry.get(field)
            if not isinstance(metatile, int) or metatile == 0:
                continue
            beh = behaviour_of(resolver, primary, secondary, metatile)
            name = names.get(beh, '?') if beh is not None else 'OUT OF RANGE'
            hit = name in with_encounters
            if hit:
                surfaces.append((field, name))
            print(f'    {field:10} 0x{metatile:03X}  {name:22}'
                  f'  {"ENCOUNTERS" if hit else "-"}')

        if not surfaces:
            print('    FAIL: nothing this theme paints can spawn a wild Pokemon')
            failures += 1
            continue

        # Every encounter surface a theme paints must route to the same branch:
        # only one table is swapped in, so a theme with one of each would have
        # half its surfaces silently fall through to the JSON placeholder.
        areas = {area_of_behaviour(flags, name) for _, name in surfaces}
        if len(areas) > 1:
            print(f'    FAIL: encounter surfaces disagree on branch - '
                  f'{", ".join(f"{f} is {area_of_behaviour(flags, n)}" for f, n in surfaces)}')
            failures += 1
            continue

        derived = areas.pop()
        declared = entry['wildArea']
        mapped = entry.get('mapId')
        table = AREA_TABLE[derived]
        print(f'    {"branch":10} {derived:<28} declared {declared}')

        if declared != derived:
            print(f'    FAIL: .wildArea says {declared} but the surface is {derived}')
            failures += 1

        bad, line = check_registration(mapped, table, derived, slots, registered)
        failures += bad
        print(f'    {"table":10} {line}' if not bad else f'    {line}')

        # ... and every map this theme's last floors swap in, held to the same
        # standard by the same function. They inherit the theme's SURFACE, so
        # they inherit its branch and slot count too - an override does not
        # change what the floor is painted with, only which header carries it.
        for extra in overrides.get(theme, []):
            bad, line = check_registration(extra, table, derived, slots, registered)
            failures += bad
            print(f'    {"override":10} {line}' if not bad else f'    {line}')

    print()
    print('generator reads the theme table')
    hard_coded = check_generator_reads_the_table()
    if not hard_coded:
        print('    no themed metatile hard-coded outside the table')

    print()
    total = failures + hard_coded
    print(f'{"FAILED" if total else "ok"}: '
          f'{failures} theme failure(s), '
          f'{hard_coded} hard-coded metatile(s)')
    return 1 if total else 0


if __name__ == '__main__':
    raise SystemExit(main())
