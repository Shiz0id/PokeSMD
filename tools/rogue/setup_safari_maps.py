#!/usr/bin/env python3
"""Build the run's Safari Zone: six maps sharing vanilla's Safari layouts.

WHY COPIES RATHER THAN THE VANILLA MAPS THEMSELVES. Two reasons, and the first
is not cosmetic:

  1. All six vanilla Safari maps are MAP_TYPE_ROUTE, and
     Overworld_MapTypeAllowsTeleportAndFly returns TRUE for ROUTE/TOWN/CITY/
     OCEAN_ROUTE. Every other map in a run is UNDERGROUND, UNDERWATER or
     INDOOR, so attaching vanilla's maps would be the FIRST PLACE IN A RUN
     WHERE FLY AND TELEPORT WORK - and the run sets all eight badge flags at
     start. Teleport is a level-up move on the Abra and Ralts lines, both of
     which are in the very set of species this area exists to hand out. It
     warps to the last heal location, which in a run is stale or unset.
     The copies are MAP_TYPE_UNDERGROUND, which closes that outright.
  2. Editing vanilla map data to fix it would poison a non-roguelike build of
     the same tree.

allow_escaping is already false on all six vanilla maps, so Dig and Escape Rope
are blocked here without any work. Do not "tidy" it to true.

WHAT IS SHARED AND WHAT IS NOT. The LAYOUTS are shared verbatim - no new block
data, no new tileset, no ROM cost for the art. Everything else is ours. This is
the same trick the five weather maps use: a second MAP, not a second layout, so
mapLayoutId still matches vanilla's and anything keyed on it is untouched.

MAPSEC IS VANILLA'S MAPSEC_SAFARI_ZONE, deliberately. A MAPSEC_ROGUE_SAFARI
would be wrong twice: check_dungeon_names.py fails on a MAPSEC_ROGUE_* that no
theme names, and "SAFARI ZONE" is already the right thing for the banner AND
for the met location, which GetCurrentRegionMapSectionId reads out of the ROM
header where the runtime mapsec patch cannot reach.

Idempotent: rewrites the six map directories every run, and inserts into the
four shared files only if its markers are absent.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from repo import REPO as _DEFAULT_REPO
except Exception:
    _DEFAULT_REPO = None

# vanilla zone -> our suffix. The six that connect to each other; the rest
# house and the entrance building are deliberately not copied, so the only way
# out is the one exit below.
ZONES = ['South', 'Southwest', 'Southeast', 'North', 'Northwest', 'Northeast']

PREFIX = 'RogueSafari'


def our_name(zone):
    return PREFIX + zone


def our_id(zone):
    return 'MAP_ROGUE_SAFARI_' + zone.upper()


def vanilla_dir(repo, zone):
    return repo / 'data/maps' / ('SafariZone_' + zone)


def out_dir(repo, zone):
    return repo / 'data/maps' / our_name(zone)


def write_text(path, text):
    """LF always - Path.write_text on Windows emits CRLF into repo files."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def write_json(path, obj):
    write_text(path, json.dumps(obj, indent=2, ensure_ascii=False) + '\n')


# --------------------------------------------------------------- the maps

def build_map_json(repo, zone):
    src = json.loads((vanilla_dir(repo, zone) / 'map.json').read_text())
    van_to_ours = {'MAP_SAFARI_ZONE_' + z.upper(): our_id(z) for z in ZONES}

    conns = []
    for c in (src.get('connections') or []):
        if c['map'] not in van_to_ours:
            # A connection out of the six would walk the player into vanilla
            # Hoenn. There are none today; fail loudly if that ever changes.
            raise SystemExit(f'{zone}: connection to {c["map"]} is outside the '
                             f'six copied zones - it would leave the run')
        conns.append({'map': van_to_ours[c['map']],
                      'offset': c['offset'],
                      'direction': c['direction']})

    # Only South keeps a warp, repointed at the rest stop. Vanilla's other
    # warps lead to the entrance building and the rest house, neither of which
    # is copied, so they are dropped rather than left dangling.
    warps = []
    if zone == 'South':
        van_warp = src['warp_events'][0]
        warps = [{
            'x': van_warp['x'],
            'y': van_warp['y'],
            # ELEVATION_TRANSITION. A warp drawn at the map's walking elevation
            # is blocked by IsElevationMismatchAt on any map whose player
            # elevation differs - the trap that once made a map impossible to
            # leave.
            'elevation': 0,
            'dest_map': 'MAP_ROGUE_REST_STOP',
            'dest_warp_id': '2',
        }]

    return {
        'id': our_id(zone),
        'name': our_name(zone),
        # SHARED WITH VANILLA. No new layout, no new block data, no new
        # tileset - the whole art cost of this feature is zero.
        'layout': src['layout'],
        'music': src['music'],
        'region': src['region'],
        # Vanilla's own section, on purpose - see the module docstring.
        'region_map_section': src['region_map_section'],
        'requires_flash': False,
        'weather': 'WEATHER_NONE',
        # THE LOAD-BEARING FIELD. See the module docstring: ROUTE would open
        # Fly and Teleport and let the player leave the run.
        'map_type': 'MAP_TYPE_UNDERGROUND',
        'allow_cycling': False,
        # Vanilla's value, and it must stay false: nothing in this branch ever
        # calls SetEscapeWarp, so Dig or an Escape Rope would warp to whatever
        # stale destination the save block happens to hold.
        'allow_escaping': False,
        'allow_running': True,
        'show_map_name': True,
        'battle_scene': 'MAP_BATTLE_SCENE_NORMAL',
        'connections': conns or None,
        # Empty for now. NOTE for anyone adding generated objects here later:
        # the engine takes the object event COUNT from ROM
        # (gMapHeader.events->objectEventCount) while reading the templates
        # from the save block, so a template past the declared count is never
        # spawned and nothing says so. Declare the slots here FIRST.
        'object_events': [],
        'warp_events': warps,
        'coord_events': [],
        'bg_events': [],
    }


SCRIPTS_INC = '''@ {name} - part of the run's Safari Zone. GENERATED by tools/rogue/setup_safari_maps.py.
@
@ A copy of SafariZone_{zone} that shares its LAYOUT and differs in its header.
@ The header field that matters is map_type: vanilla's is MAP_TYPE_ROUTE, which
@ Overworld_MapTypeAllowsTeleportAndFly accepts, so on vanilla's own maps a run
@ could Fly or Teleport out of itself. These are MAP_TYPE_UNDERGROUND.
@
@ Nothing is defined here. The encounter table is substituted in C - see
@ RogueDungeon_GetWildMonInfo - and the entry in wild_encounters.json is a
@ placeholder that exists so the engine finds a non-NULL table for this map's
@ own encounter surface.

{name}_MapScripts::
\t.byte 0
'''


def write_maps(repo):
    for zone in ZONES:
        d = out_dir(repo, zone)
        write_json(d / 'map.json', build_map_json(repo, zone))
        write_text(d / 'scripts.inc',
                   SCRIPTS_INC.format(name=our_name(zone), zone=zone))
        print(f'  wrote data/maps/{our_name(zone)}/')


# ------------------------------------------------------- map_groups.json

def patch_map_groups(repo):
    path = repo / 'data/maps/map_groups.json'
    text = path.read_text()
    if our_name(ZONES[0]) in text:
        print('  map_groups.json already has them')
        return
    anchor = '    "RogueRestStopGames"'
    if anchor not in text:
        raise SystemExit('map_groups.json: RogueRestStopGames not found')
    added = ''.join(f',\n    "{our_name(z)}"' for z in ZONES)
    text = text.replace(anchor, anchor + added, 1)
    write_text(path, text)
    print(f'  map_groups.json += {len(ZONES)} maps in gMapGroup_Rogue')


# ------------------------------------------------------- event_scripts.s

def patch_event_scripts(repo):
    path = repo / 'data/event_scripts.s'
    text = path.read_text()
    if our_name(ZONES[0]) in text:
        print('  event_scripts.s already has them')
        return
    anchor = '\t.include "data/maps/RogueRestStopGames/scripts.inc"\n'
    if anchor not in text:
        raise SystemExit('event_scripts.s: RogueRestStopGames include not found')
    # The directive at the START OF A LINE, not the string anywhere. The
    # comment that warns about this block mentions it by name several lines
    # ABOVE the anchor, so a plain substring search finds the prose first and
    # concludes the anchor is inside the block it is documented as preceding.
    frlg = text.index('\n.if IS_FRLG')
    if text.index(anchor) > frlg:
        raise SystemExit('the anchor is inside the .if IS_FRLG block - an '
                         'include there is silently dropped from an Emerald build')
    added = ''.join(f'\t.include "data/maps/{our_name(z)}/scripts.inc"\n'
                    for z in ZONES)
    text = text.replace(anchor, anchor + added, 1)
    write_text(path, text)
    print(f'  event_scripts.s += {len(ZONES)} includes, above the IS_FRLG block')


# --------------------------------------------------- wild_encounters.json

# An unmistakable placeholder. If the C substitution ever stops firing, a Zubat
# in the Safari Zone says so immediately, where a plausible species would let
# the failure pass for real content.
PLACEHOLDER = 'SPECIES_ZUBAT'


def placeholder_table(src_table):
    """Same shape and rate as vanilla's, every slot the same wrong species.

    The SHAPE matters and the species does not: TryGenerateWildMon picks the
    land or water branch from the metatile behaviour under the player and
    returns immediately if that branch's table is NULL, so a copied map must
    declare every surface its art actually has. encounter_rate is read off
    this static entry before any hook runs, so it stays live.
    """
    return {
        'encounter_rate': src_table['encounter_rate'],
        'mons': [{'min_level': 5, 'max_level': 7, 'species': PLACEHOLDER}
                 for _ in src_table['mons']],
    }


def patch_wild_encounters(repo):
    path = repo / 'src/data/wild_encounters.json'
    text = path.read_text()
    if our_id(ZONES[0]) in text:
        print('  wild_encounters.json already has them')
        return

    data = json.loads(text)
    group = data['wild_encounter_groups'][0]
    by_map = {h['map']: h for h in group['encounters']}

    entries = []
    for zone in ZONES:
        src = by_map.get('MAP_SAFARI_ZONE_' + zone.upper())
        if src is None:
            raise SystemExit(f'no vanilla wild entry for {zone}')
        e = {'map': our_id(zone), 'base_label': 'g' + our_name(zone)}
        for field in ('land_mons', 'water_mons', 'rock_smash_mons',
                      'fishing_mons'):
            if field in src:
                e[field] = placeholder_table(src[field])
        entries.append(e)

    # Inserted as text rather than by re-dumping the whole file, which would
    # reformat 37,000 lines and bury the change.
    anchor = '        {\n          "map": "MAP_ROGUE_DUNGEON_FLOOR",'
    if anchor not in text:
        raise SystemExit('wild_encounters.json: dungeon floor entry not found')
    blob = ''
    for e in entries:
        body = json.dumps(e, indent=2, ensure_ascii=False)
        body = '\n'.join('        ' + ln for ln in body.splitlines())
        blob += body + ',\n'
    text = text.replace(anchor, blob + anchor, 1)
    write_text(path, text)
    print(f'  wild_encounters.json += {len(entries)} placeholder entries')


# ------------------------------------------------------------ rest stop

def patch_rest_stop(repo):
    """Add the east exit as warp 2.

    WARP 0 MUST STAY THE DESCENT - RogueDungeon_SetRestStopExit only ever
    writes index 0 - and warp 1 is the game room, so this appends. The tile it
    sits on is not a door yet; make_rest_stop.py has to cut the east alcove and
    give it a warp behaviour before the player can step on it.
    """
    path = repo / 'data/maps/RogueRestStop/map.json'
    d = json.loads(path.read_text())
    if len(d['warp_events']) > 2:
        print('  rest stop already has warp 2')
        return
    if d['warp_events'][0]['dest_map'] != 'MAP_DYNAMIC':
        raise SystemExit('rest stop warp 0 is not the descent - do not append')
    d['warp_events'].append({
        'x': 15, 'y': 6, 'elevation': 3,
        'dest_map': our_id('South'), 'dest_warp_id': '0',
    })
    write_json(path, d)
    print('  rest stop += warp 2 (east alcove -> RogueSafariSouth)')


def main(argv):
    repo = Path(argv[1]) if len(argv) > 1 else _DEFAULT_REPO
    if repo is None:
        raise SystemExit('usage: setup_safari_maps.py <repo>')
    repo = Path(repo)
    print(f'repo: {repo}')
    write_maps(repo)
    patch_map_groups(repo)
    patch_event_scripts(repo)
    patch_wild_encounters(repo)
    patch_rest_stop(repo)
    print('\ndone. Still to do: the east alcove in make_rest_stop.py, and the '
          'RogueDungeon_GetWildMonInfo branch.')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
