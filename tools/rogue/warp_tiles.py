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

# Tiles written by import_tile_sheet.py, asserted for the same reason
# BASE_COUNT is: the chevron tiles are appended past this, and a stale figure
# would truncate imported art instead of a previous append.
BASE_TILE_COUNT = 288

NUM_TILES_IN_PRIMARY = 512
MAX_TILES_IN_SECONDARY = 512

# (name, metatile to clone, behaviour, chevron).
#
# The clone is the source metatile's art; what varies is how much of it
# survives.
#
#   chevron None -> the art exactly, only the ATTRIBUTE changed. No new tiles.
#   chevron 'W'  -> the art with a west-pointing chevron drawn into its four
#                   bottom tiles, appended as four NEW tiles.
#   chevron 'E'  -> those same four tiles, mirrored with the HFLIP BIT and the
#                   left/right slots swapped. Costs no tiles at all.
#
# THE DIRECTION IS NOT DECORATION - IT IS THE FACING FIX. GetAdjustedInitialDirection
# in src/overworld.c picks which way the player faces on arrival straight from
# this behaviour: a door faces them SOUTH, a west arrow faces them EAST, an east
# arrow WEST. Both alcoves are one-tile notches walled on three sides, so the
# door behaviour they used to carry faced the player into solid rock every time
# they came back from the game room or the Safari.
#
# It also changes the trigger. TryArrowWarp fires while the player is STANDING
# ON the tile and still holding the direction they face, where a door fires on
# stepping onto it. For a dead-end notch with exactly one open side that is the
# same gesture - walk west into it and keep holding west - and it is vanilla's
# own cave-exit feel.
DOORS = (
    # MURKY_METATILE_STAIRS, the cut stairwell. Stays a door: it is a DEPARTURE
    # tile only - the boss script warps to (8,6) by coordinate and never onto
    # this one - so its arrival facing is never seen, and a door can be stepped
    # onto from the north, east or west where an arrow warp would need the
    # player to approach it from exactly one side.
    ('REST_STOP_DESCENT', 0x295, 'MB_NON_ANIMATED_DOOR', None),
    # The murky floor, cut west into the rock, through to the game room.
    ('REST_STOP_ALCOVE_WEST', 0x239, 'MB_WEST_ARROW_WARP', 'W'),
    # The same notch mirrored, east, through to the Safari Zone.
    ('REST_STOP_ALCOVE_EAST', 0x239, 'MB_EAST_ARROW_WARP', 'E'),
)

# Palette 7 indices. THE FLOOR ART USES 1-10 AND NOTHING ELSE - measured, not
# assumed - so drawing in 13 and 11 disturbs no existing pixel and needs no
# palette change, no new palette slot and no colour matching.
CHEVRON_BRIGHT = 13    # 239 222 115, the palette's brightest
CHEVRON_OUTLINE = 11   # 74 41 0, its darkest, so the shape reads at 2x

# THERE IS NO ARROW ANYWHERE IN VANILLA TO REUSE, which is why this is drawn
# rather than lifted. Every MB_*_ARROW_WARP metatile in the tree was rendered
# and checked: gTileset_General 0x024, the only arrow warp in any Hoenn
# tileset, is plain gravel, and the FRLG cave arrows are wall and ground
# texture. In vanilla an arrow warp is only a TRIGGER - the visual cue is always
# contextual, a gap in a wall or a cave mouth. Do not go looking for the art
# again; it is not there.


def chevron_mask():
    """-> {(x, y): palette index} over the 16x16 metatile, pointing WEST.

    Kept free of Pillow so the checker runs where the image libraries are not.
    """
    body = set()
    apex_x, centre_y, half, thick = 5, 8, 5, 3

    for dy in range(-half, half + 1):
        x = apex_x + abs(dy)
        for t in range(thick):
            body.add((x + t, centre_y + dy))

    edge = set()
    for (x, y) in body:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if (x + dx, y + dy) not in body:
                    edge.add((x + dx, y + dy))

    out = {p: CHEVRON_OUTLINE for p in edge}
    out.update({p: CHEVRON_BRIGHT for p in body})
    return {p: v for p, v in out.items() if 0 <= p[0] < 16 and 0 <= p[1] < 16}


def chevron_tile_ids():
    """-> the four tile ids the west chevron occupies, in slot order."""
    first = NUM_TILES_IN_PRIMARY + BASE_TILE_COUNT
    return [first + i for i in range(4)]


def door_ids():
    """-> {name: metatile id}. Import this rather than hard-coding the ids."""
    return {entry[0]: NUM_METATILES_IN_PRIMARY + BASE_COUNT + i
            for i, entry in enumerate(DOORS)}


def door_sources():
    """-> {name: metatile it clones}. For callers asserting the theme matches."""
    return {entry[0]: entry[1] for entry in DOORS}


def _paths(symbol):
    ts = _RESOLVER.resolve(symbol)
    if not ts:
        raise SystemExit(f'cannot resolve {symbol}')
    return ts['metatiles'], ts['attributes']


def _tiles_path(symbol):
    ts = _RESOLVER.resolve(symbol)
    if not ts:
        raise SystemExit(f'cannot resolve {symbol}')
    return ts['tiles']


def _write_chevron_tiles(src_entries, write):
    """Draw the west chevron into the source metatile's four bottom tiles.

    Returns True when tiles.png already holds exactly these tiles. Pillow is
    imported HERE rather than at module scope so the checker - which every
    check run and make_rest_stop.py call goes through - still works on a machine
    with no image library. The repo lives in WSL and Pillow is on the Windows
    side, which is precisely the split this guards.
    """
    from PIL import Image

    path = _tiles_path(TILESET)
    im = Image.open(path).convert('P')
    palette = im.getpalette()
    width, height = im.size
    per_row = width // 8
    have = per_row * (height // 8)

    # A tiles.png is a GRID, so its capacity rounds up to a whole row: four
    # appended tiles turn 288 (18 exact rows) into 19 rows and therefore 304
    # slots, not 292. Comparing against the unrounded figure reads a correctly
    # appended sheet as a stale one.
    def grid_slots(tiles):
        return -(-tiles // per_row) * per_row

    expected = (grid_slots(BASE_TILE_COUNT), grid_slots(BASE_TILE_COUNT + 4))
    if have not in expected:
        raise SystemExit(f'{TILESET} tiles.png holds {have} tiles, expected '
                         f'{expected[0]} or {expected[1]} - '
                         f'BASE_TILE_COUNT is stale, fix it before appending')
    if grid_slots(BASE_TILE_COUNT + 4) > MAX_TILES_IN_SECONDARY:
        raise SystemExit('exceeds secondary tileset tile capacity')

    # Lift the four source tiles into one 16x16 index grid, draw into it, and
    # cut it back into four. Drawing on the assembled metatile rather than
    # per-tile is what lets the chevron cross the tile seams without having to
    # reason about which pixel lands in which quadrant.
    grid = [[0] * 16 for _ in range(16)]
    src = im.load()
    for slot in range(4):
        tid = src_entries[slot] & 0x3FF
        idx = tid - NUM_TILES_IN_PRIMARY
        tx, ty = (idx % per_row) * 8, (idx // per_row) * 8
        ox, oy = (slot % 2) * 8, (slot // 2) * 8
        for y in range(8):
            for x in range(8):
                grid[oy + y][ox + x] = src[tx + x, ty + y] & 0xF

    for (x, y), value in chevron_mask().items():
        grid[y][x] = value

    rows_needed = -(-(BASE_TILE_COUNT + 4) // per_row)
    out = Image.new('P', (width, rows_needed * 8), 0)
    out.putpalette(palette)
    out.paste(im.crop((0, 0, width, (BASE_TILE_COUNT // per_row) * 8)), (0, 0))

    dst = out.load()
    for slot in range(4):
        tile = BASE_TILE_COUNT + slot
        tx, ty = (tile % per_row) * 8, (tile // per_row) * 8
        ox, oy = (slot % 2) * 8, (slot // 2) * 8
        for y in range(8):
            for x in range(8):
                dst[tx + x, ty + y] = grid[oy + y][ox + x]

    # grid_slots again, not the raw count, or this never matches and the sheet
    # is rewritten on every run - which looks like churn in git for no change.
    if have == grid_slots(BASE_TILE_COUNT + 4) and im.size == out.size \
            and list(im.getdata()) == list(out.getdata()):
        return True
    if write:
        out.save(path)
        print(f'  tiles.png {have} -> {BASE_TILE_COUNT + 4} tiles '
              f'(4 chevron tiles at 0x{chevron_tile_ids()[0]:03X})')
    return False


def append(write=False):
    mt_path, at_path = _paths(TILESET)
    mt = bytearray(mt_path.read_bytes())
    at = bytearray(at_path.read_bytes())
    count = len(mt) // 16

    # A RANGE, not the two exact counts, because the tail already on disk was
    # written by a PREVIOUS version of the DOORS table and need not be the
    # length this one produces - growing the table from two entries to three is
    # exactly that case. What the guard has to prevent is truncating into the
    # imported set, so the floor is what matters and the ceiling is only a
    # sanity bound.
    if not BASE_COUNT <= count <= BASE_COUNT + max(len(DOORS), 8):
        raise SystemExit(f'{TILESET} has {count} metatiles, expected '
                         f'{BASE_COUNT} plus a short appended tail - '
                         f'BASE_COUNT is stale, fix it before appending')

    def entry_of(metatile):
        off = (metatile - NUM_METATILES_IN_PRIMARY) * 16
        return struct.unpack_from('<8H', mt, off)

    def attr_of(metatile):
        return struct.unpack_from('<H', at, (metatile - NUM_METATILES_IN_PRIMARY) * 2)[0]

    chevron_src = None
    for name, src, _behaviour, chevron in DOORS:
        if chevron == 'W':
            chevron_src = entry_of(src)
    if chevron_src is None:
        raise SystemExit('no west chevron entry - the east one mirrors it')

    # Build the tail against the BASE entries, so this is the same answer
    # whether or not a previous append is still sitting on the end.
    tail_mt, tail_at = b'', b''
    for name, src, behaviour, chevron in DOORS:
        if src >= NUM_METATILES_IN_PRIMARY + BASE_COUNT:
            raise SystemExit(f'{name} clones 0x{src:03X}, which is itself '
                             f'appended - clone from the imported set only')
        entry = list(entry_of(src))
        if chevron:
            tiles = chevron_tile_ids()
            palette = chevron_src[0] & 0xF000
            if chevron == 'W':
                bottom = [tiles[0], tiles[1], tiles[2], tiles[3]]
                flip = 0
            else:
                # The horizontal mirror: swap the left and right slots and set
                # HFLIP on each. Costs no tiles, which is the whole reason the
                # chevron is only ever drawn once.
                bottom = [tiles[1], tiles[0], tiles[3], tiles[2]]
                flip = 1 << 10
            for slot in range(4):
                entry[slot] = bottom[slot] | flip | palette
        tail_mt += struct.pack('<8H', *entry)
        # keep the source's layer type, replace only the behaviour
        tail_at += struct.pack('<H', (attr_of(src) & ~0xFF) | MB[behaviour])

    ids = door_ids()
    for name, src, behaviour, chevron in DOORS:
        art = {'W': 'west chevron', 'E': 'east chevron (hflip)'}.get(chevron, 'art unchanged')
        print(f'  {name:<24} 0x{ids[name]:03X}  from 0x{src:03X}  '
              f'{MB_NAME[attr_of(src) & 0xFF]} -> {MB_NAME[MB[behaviour]]}  {art}')

    tiles_ok = _write_chevron_tiles(chevron_src, write)

    current = bytes(mt[BASE_COUNT * 16:])
    if count == BASE_COUNT + len(DOORS) and current == tail_mt \
            and bytes(at[BASE_COUNT * 2:]) == tail_at and tiles_ok:
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
