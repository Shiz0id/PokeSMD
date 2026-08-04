"""The tiles a warp_event can actually fire on.

A WARP_EVENT DOES NOT WARP ON ITS OWN. TryStartWarpEventScript in
src/field_control_avatar.c runs only when IsWarpMetatileBehavior is true for the
tile the player stepped onto, and TryArrowWarp only when the tile is the arrow
warp for the direction being held. Put a warp on plain floor and it is dead:
nothing fails at build time, nothing appears in the log, the player simply walks
over it.

That is how the rest stop shipped with three dead warps at once. Authoring the
chamber replaced LAYOUT_POKEMON_CENTER_1F - whose exits sit on real door
metatiles - with cut rock, whose do not, and the warp_events came across
unchanged and stopped working. The game room's exit went the same way.

This module does two things:

  * appends the door metatiles the way-station needs to gTileset_RogueMurkyCave.
    They are CLONES: the eight tile references of the descent art and of the
    plain floor copied verbatim, with only the ATTRIBUTE changed. No new art.
    The behaviour they get is MB_NON_ANIMATED_DOOR, which is what vanilla's own
    cave descent 0x0A7 in gTileset_General carries - so this is the tile the
    engine already expects to warp on, not an invention.

  * checks a map's warp_events against the behaviour of the tile under each one,
    so a dead warp fails a tool run instead of a playtest.

Run:  python3 tools/rogue/warp_tiles.py            # report
      python3 tools/rogue/warp_tiles.py --write    # append the door metatiles

It is the ONE appender for gTileset_RogueMurkyCave, the way append_metatiles.py
is the one appender for gTileset_Cave: it stays idempotent by truncating
everything past the imported count and rewriting the tail, so a second script
appending to the same file would be silently wiped the next time this one ran.

Note that import_tile_sheet.py OWNS the first BASE_COUNT entries and rewrites
the file from the sheet. Re-import murky and the doors are gone; re-run this and
they are back. make_rest_stop.py checks the behaviours either way, so the window
where that matters closes at the next tool run rather than at a playtest.
"""
import json
import re
import struct
import sys
from pathlib import Path

from tileset_resolve import TilesetResolver

REPO = Path(__file__).resolve().parents[2]
_RESOLVER = TilesetResolver(REPO)


def _behaviour_names():
    """metatile_behaviors.h is a C enum with IMPLICIT numbering, not defines.

    So a behaviour's value is its position, and the only way to be right about
    it is to count the entries rather than to remember a number.
    """
    text = (REPO / 'include/constants/metatile_behaviors.h').read_text()
    body = re.search(r'\{(.*)\}', text, re.S).group(1)
    return [line.strip().rstrip(',') for line in body.splitlines()
            if line.strip().startswith('MB_')]


MB = {name: value for value, name in enumerate(_behaviour_names())}
MB_NAME = {value: name for name, value in MB.items()}

# Everything IsWarpMetatileBehavior accepts that this project could plausibly
# author, plus the arrow warps TryArrowWarp accepts while the player holds that
# direction. The facility-specific ones (Lavaridge, Aqua Hideout, Mt Pyre,
# Mossdeep gym, union room) are deliberately absent - they warp through their
# own handlers and would be the wrong answer here.
WARP_BEHAVIOURS = {MB[name] for name in (
    'MB_NON_ANIMATED_DOOR', 'MB_ANIMATED_DOOR', 'MB_WATER_DOOR', 'MB_LADDER',
    'MB_UP_ESCALATOR', 'MB_DOWN_ESCALATOR',
    'MB_NORTH_ARROW_WARP', 'MB_SOUTH_ARROW_WARP',
    'MB_EAST_ARROW_WARP', 'MB_WEST_ARROW_WARP', 'MB_WATER_SOUTH_ARROW_WARP',
)}

NUM_METATILES_IN_PRIMARY = 0x200

# ---------------------------------------------------------------- the appender

TILESET = 'gTileset_RogueMurkyCave'
# What import_tile_sheet.py writes for this tileset. Asserted rather than
# inferred: if the import ever emits a different count, truncating past this
# would eat real metatiles.
BASE_COUNT = 150

# (name, metatile to clone). The clone is that metatile's art exactly; only the
# behaviour differs, so the descent still looks like the cut stairwell and the
# alcove still looks like the floor it is cut into.
DOORS = (
    ('REST_STOP_DESCENT', 0x295),   # MURKY_METATILE_STAIRS, the cut stairwell
    ('REST_STOP_ALCOVE', 0x239),    # the murky floor, so the notch reads as rock
)
DOOR_BEHAVIOUR = MB['MB_NON_ANIMATED_DOOR']


def door_ids():
    """-> {name: metatile id}. Import this rather than hard-coding the ids."""
    return {name: NUM_METATILES_IN_PRIMARY + BASE_COUNT + i
            for i, (name, _) in enumerate(DOORS)}


def _paths(symbol):
    ts = _RESOLVER.resolve(symbol)
    if not ts:
        raise SystemExit(f'cannot resolve {symbol}')
    return ts['metatiles'], ts['attributes']


def append(write=False):
    mt_path, at_path = _paths(TILESET)
    mt = bytearray(mt_path.read_bytes())
    at = bytearray(at_path.read_bytes())
    count = len(mt) // 16

    if count not in (BASE_COUNT, BASE_COUNT + len(DOORS)):
        raise SystemExit(f'{TILESET} has {count} metatiles, expected '
                         f'{BASE_COUNT} or {BASE_COUNT + len(DOORS)} - '
                         f'BASE_COUNT is stale, fix it before appending')

    def entry_of(metatile):
        off = (metatile - NUM_METATILES_IN_PRIMARY) * 16
        return struct.unpack_from('<8H', mt, off)

    def attr_of(metatile):
        return struct.unpack_from('<H', at, (metatile - NUM_METATILES_IN_PRIMARY) * 2)[0]

    # Build the tail against the BASE entries, so this is the same answer
    # whether or not a previous append is still sitting on the end.
    tail_mt, tail_at = b'', b''
    for name, src in DOORS:
        if src >= NUM_METATILES_IN_PRIMARY + BASE_COUNT:
            raise SystemExit(f'{name} clones 0x{src:03X}, which is itself '
                             f'appended - clone from the imported set only')
        tail_mt += struct.pack('<8H', *entry_of(src))
        # keep the source's layer type, replace only the behaviour
        tail_at += struct.pack('<H', (attr_of(src) & ~0xFF) | DOOR_BEHAVIOUR)

    ids = door_ids()
    for name, src in DOORS:
        print(f'  {name:<20} 0x{ids[name]:03X}  clone of 0x{src:03X}  '
              f'{MB_NAME[attr_of(src) & 0xFF]} -> {MB_NAME[DOOR_BEHAVIOUR]}')

    current = bytes(mt[BASE_COUNT * 16:])
    if count == BASE_COUNT + len(DOORS) and current == tail_mt \
            and bytes(at[BASE_COUNT * 2:]) == tail_at:
        print(f'  already appended ({count} metatiles, tail matches)')
        return ids
    if not write:
        print('  TAIL DOES NOT MATCH - re-run with --write')
        return ids

    del mt[BASE_COUNT * 16:]
    del at[BASE_COUNT * 2:]
    mt += tail_mt
    at += tail_at
    assert len(mt) // 16 == len(at) // 2, 'metatile/attribute count mismatch'
    assert len(mt) // 16 <= 512, 'exceeds secondary tileset capacity'
    mt_path.write_bytes(bytes(mt))
    at_path.write_bytes(bytes(at))
    print(f'  wrote {len(mt) // 16} metatiles / {len(at) // 2} attributes')
    return ids


# ----------------------------------------------------------------- the checker

def _layout(layout_id):
    layouts = json.loads((REPO / 'data/layouts/layouts.json')
                         .read_text(encoding='utf-8'))['layouts']
    for entry in layouts:
        if entry['id'] == layout_id:
            return entry
    raise SystemExit(f'no layout {layout_id}')


def behaviours(primary, secondary):
    """-> {metatile id: behaviour} across the pair's combined id space."""
    out = {}
    for symbol, base in ((primary, 0), (secondary, NUM_METATILES_IN_PRIMARY)):
        if not symbol or symbol == '0':
            continue
        _, at_path = _paths(symbol)
        data = at_path.read_bytes()
        for i, attr in enumerate(struct.unpack(f'<{len(data) // 2}H', data)):
            out[base + i] = attr & 0xFF
    return out


def check_map(map_name):
    """Every warp_event in this map must sit on a tile that can warp.

    Returns the number of dead warps found, and prints one line per warp.
    """
    mapjson = json.loads((REPO / 'data/maps' / map_name / 'map.json')
                         .read_text(encoding='utf-8'))
    layout = _layout(mapjson['layout'])
    width, height = layout['width'], layout['height']
    raw = (REPO / layout['blockdata_filepath']).read_bytes()
    blocks = struct.unpack(f'<{width * height}H', raw[:width * height * 2])
    table = behaviours(layout['primary_tileset'], layout['secondary_tileset'])

    dead = 0
    for i, warp in enumerate(mapjson['warp_events']):
        metatile = blocks[warp['y'] * width + warp['x']] & 0x3FF
        behaviour = table.get(metatile)
        name = MB_NAME.get(behaviour, f'?{behaviour}')
        ok = behaviour in WARP_BEHAVIOURS
        dead += not ok
        print(f'  warp {i} ({warp["x"]:>2},{warp["y"]:>2}) '
              f'0x{metatile:03X} {name:<24} '
              f'{"ok" if ok else "DEAD - this tile cannot warp"}')
    return dead


def main(argv):
    print(f'{TILESET} door metatiles')
    append('--write' in argv)
    dead = 0
    for name in ('RogueRestStop', 'RogueRestStopGames'):
        print(f'{name} warps')
        dead += check_map(name)
    if dead:
        raise SystemExit(f'{dead} dead warp(s)')


if __name__ == '__main__':
    main(sys.argv[1:])
