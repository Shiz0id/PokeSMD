"""Author the rest stop's layout from a floor plan.

The rest stop was a spare Pokemon Center - LAYOUT_POKEMON_CENTER_1F, shared with
sixteen vanilla maps - which is tonally odd forty floors underground, because
nothing explains where the building came from. This writes a chamber instead: a
way-station cut into the same rock the dungeon is.

WHY IT IS AUTHORED RATHER THAN DRAWN IN AN EDITOR

There is no map editor wired up in this repo, and more importantly the walls
should be the DUNGEON'S walls. ApplyWallAutotiling in rogue_dungeon.c is
transcribed here exactly - same order of tests, same slots - so the chamber is
walled by the same rules that wall every generated floor. Hand-picking metatile
ids would drift from that the first time the cave theme's table changed.

The plan is a mask; everything else is derived.

Idempotent: rewrites map.bin, border.bin and the layouts.json entry each run.

Run:  python3 tools/rogue/make_rest_stop.py          # report + ascii preview
      python3 tools/rogue/make_rest_stop.py --write
"""
import json
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LAYOUTS = REPO / 'data/layouts/layouts.json'
NAME = 'RogueRestStop'
LAYOUT_ID = 'LAYOUT_ROGUE_REST_STOP'
# Both tables are copied slot for slot from the .wall arrays in
# rogue_dungeon.c - DUNGEON_THEME_CAVE and DUNGEON_THEME_MURKYCAVE - so the same
# source of truth feeds the chamber and the generated floors. Never derive these
# from the header's names alone: the cave points BOTH north corner slots at one
# metatile, and the murky one does not.
THEMES = {
    # vanilla's cave, the rock Granite Cave is cut from. Lumpy and natural.
    'cave': dict(
        primary='gTileset_General', secondary='gTileset_Cave',
        floor=0x201, void=0x200, stairs=0x214,
        wall=dict(
            INTERIOR_LEFT=0x210, INTERIOR_MID=0x211, INTERIOR_RIGHT=0x212,
            FACE_LEFT=0x218, FACE_MID=0x219, FACE_RIGHT=0x21A,
            NORTH_LEFT=0x220, NORTH_MID=0x209, NORTH_RIGHT=0x222,
            # the cave draws both north corners the same; the murky one does not
            CORNER_OPEN_SE=0x21B, CORNER_OPEN_SW=0x21C,
            CORNER_OPEN_NW=0x223, CORNER_OPEN_NE=0x223,
            SLIVER_VERT=0x39E, SLIVER_HORZ=0x39F,
            SLIVER_VERT_TOP=0x3A0, SLIVER_VERT_BOT=0x3A1,
            SLIVER_HORZ_L=0x3A2, SLIVER_HORZ_R=0x3A3,
            SLIVER_ISOLATED=0x3A4,
        )),
    # Steven's finale. Carved pillars and worked stone - the one tileset in the
    # project that reads as somewhere BUILT, which is what a way-station is. Its
    # descent is the cut stairwell rather than a hole, for the same reason.
    'murky': dict(
        primary='gTileset_General', secondary='gTileset_RogueMurkyCave',
        floor=0x239, void=0x200, stairs=0x295,
        wall=dict(
            INTERIOR_LEFT=0x203, INTERIOR_MID=0x204, INTERIOR_RIGHT=0x205,
            FACE_LEFT=0x206, FACE_MID=0x207, FACE_RIGHT=0x208,
            NORTH_LEFT=0x200, NORTH_MID=0x201, NORTH_RIGHT=0x202,
            CORNER_OPEN_SE=0x21D, CORNER_OPEN_SW=0x21E,
            CORNER_OPEN_NW=0x220, CORNER_OPEN_NE=0x21F,
            SLIVER_VERT=0x20C, SLIVER_HORZ=0x20A,
            SLIVER_VERT_TOP=0x210, SLIVER_VERT_BOT=0x214,
            SLIVER_HORZ_L=0x211, SLIVER_HORZ_R=0x213,
            SLIVER_ISOLATED=0x20D,
        )),
}
THEME = 'murky'

PRIMARY = THEMES[THEME]['primary']
SECONDARY = THEMES[THEME]['secondary']
FLOOR = THEMES[THEME]['floor']
VOID = THEMES[THEME]['void']
STAIRS_DOWN = THEMES[THEME]['stairs']
WALL = THEMES[THEME]['wall']
ELEV_FLOOR, ELEV_WALL = 3, 0

# The chamber. Walls are two thick everywhere so no cell ever has floor on
# opposite sides - the sliver slots exist for generated floors and are art this
# room has no reason to exercise.
#
# THE PLAYER ARRIVES AT THE CENTRE, (width/2, height/2), because the script
# warps with a bare `warp MAP_ROGUE_REST_STOP`: that is WARP_ID_NONE with dummy
# coords, and SetPlayerCoordsFromWarp falls through to the middle of the map.
# So the centre must be open floor, and everything is laid out around it.
#
#   #  wall        .  floor       X  the descent out
#   G  the alcove cut west into the rock, through to the game room
PLAN = (
    '#################',
    '#################',
    '####.........####',
    '###...........###',
    '##.............##',
    '##.............##',
    '#G.............##',
    '##.............##',
    '###...........###',
    '####....X....####',
    '#################',
    '#################',
    '#################',
)

# Everything that is not wall, by plan character. The alcove is plain floor -
# there is no door art in a tileset made of cut rock, and a one-tile notch in a
# wall reads as a way through on its own.
OPEN = {'.': FLOOR, 'X': STAIRS_DOWN, 'G': FLOOR}


def is_wall(plan, x, y):
    """Out of bounds counts as wall, so the map edge closes the room."""
    if y < 0 or y >= len(plan) or x < 0 or x >= len(plan[0]):
        return True
    return plan[y][x] not in OPEN


def wall_metatile(plan, x, y):
    """Transcribed from ApplyWallAutotiling, test for test and in order.

    The slivers are tested FIRST because a wall one block thick has floor on
    opposite sides, and the face and north cases would each match on a single
    open side and shade only that edge.
    """
    n = not is_wall(plan, x, y - 1)
    s = not is_wall(plan, x, y + 1)
    w = not is_wall(plan, x - 1, y)
    e = not is_wall(plan, x + 1, y)

    if n and s and w and e:
        return WALL['SLIVER_ISOLATED']
    if n and s:
        return WALL['SLIVER_HORZ_L'] if w else \
               WALL['SLIVER_HORZ_R'] if e else WALL['SLIVER_HORZ']
    if w and e:
        return WALL['SLIVER_VERT_TOP'] if n else \
               WALL['SLIVER_VERT_BOT'] if s else WALL['SLIVER_VERT']
    if s:
        # floor below, so this is the face the camera sees - highest priority
        return WALL['FACE_LEFT'] if w else \
               WALL['FACE_RIGHT'] if e else WALL['FACE_MID']
    if n:
        return WALL['NORTH_LEFT'] if w else \
               WALL['NORTH_RIGHT'] if e else WALL['NORTH_MID']
    if w:
        return WALL['INTERIOR_LEFT']
    if e:
        return WALL['INTERIOR_RIGHT']
    # every cardinal is wall, so only a diagonal can be open
    if not is_wall(plan, x + 1, y + 1):
        return WALL['CORNER_OPEN_SE']
    if not is_wall(plan, x - 1, y + 1):
        return WALL['CORNER_OPEN_SW']
    if not is_wall(plan, x - 1, y - 1):
        return WALL['CORNER_OPEN_NW']
    if not is_wall(plan, x + 1, y - 1):
        return WALL['CORNER_OPEN_NE']
    return WALL['INTERIOR_MID']


def block(metatile, collision, elevation):
    return (metatile & 0x3FF) | ((collision << 10) & 0xC00) \
        | ((elevation << 12) & 0xF000)


def build(plan):
    w, h = len(plan[0]), len(plan)
    out = []
    for y in range(h):
        for x in range(w):
            ch = plan[y][x]
            if ch in OPEN:
                out.append(block(OPEN[ch], 0, ELEV_FLOOR))
            else:
                out.append(block(wall_metatile(plan, x, y), 1, ELEV_WALL))
    return w, h, out


TEMPLATE = """    {{
      "id": "{id}",
      "name": "{name}_Layout",
      "width": {w},
      "height": {h},
      "primary_tileset": "{primary}",
      "secondary_tileset": "{secondary}",
      "border_filepath": "data/layouts/{name}/border.bin",
      "blockdata_filepath": "data/layouts/{name}/map.bin"
    }}"""


def upsert_layout(w, h):
    """Append the entry as TEXT, never by reserialising.

    add_theme_layout.py takes the same care and for the same reason: dumping the
    parsed json back out reformats all 785 layouts and buries the one line that
    actually changed.
    """
    text = LAYOUTS.read_text(encoding='utf-8')
    entry = TEMPLATE.format(id=LAYOUT_ID, name=NAME, w=w, h=h,
                            primary=PRIMARY, secondary=SECONDARY)
    if f'"{LAYOUT_ID}"' in text:
        # replace the existing block in place, so re-running stays idempotent
        start = text.index('    {\n      "id": "' + LAYOUT_ID + '"')
        end = text.index('    }', start) + len('    }')
        if text[start:end] == entry:
            return 'unchanged'
        text = text[:start] + entry + text[end:]
    else:
        marker = '\n  ]\n}'
        if marker not in text:
            raise SystemExit('layouts.json does not end as expected; add by hand')
        text = text.replace(marker, ',\n' + entry + marker)
    LAYOUTS.write_text(text, encoding='utf-8', newline='\n')
    return 'written'


def check_unown(plan):
    """The Unown spots are hand-picked in C; check them against the plan.

    Parsed out of rogue_dungeon.c rather than copied here, because a duplicated
    coordinate table is a drift waiting to happen - the moment the plan gains a
    wall the copy stops being true and nothing says so. Same reason the wall
    slots are read off the theme table.
    """
    import re

    src = (REPO / 'src/rogue_dungeon.c').read_text(encoding='utf-8')
    m = re.search(r'sUnownSpots\[\]\[2\]\s*=\s*\{(.*?)\};', src, re.S)
    if not m:
        print('  WARNING: sUnownSpots not found in rogue_dungeon.c')
        return
    spots = [(int(a), int(b)) for a, b in
             re.findall(r'\{\s*(\d+)\s*,\s*(\d+)\s*\}', m.group(1))]

    # everything the room has already promised to something else
    reserved = {}
    mapjson = json.loads((REPO / 'data/maps/RogueRestStop/map.json')
                         .read_text(encoding='utf-8'))
    for o in mapjson['object_events']:
        if 'UNOWN' not in o['graphics_id']:
            reserved[(o['x'], o['y'])] = o['script'].split('_')[-1]
    for w in mapjson['warp_events']:
        reserved[(w['x'], w['y'])] = 'warp'
    reserved[(len(plan[0]) // 2, len(plan) // 2)] = 'arrival'

    bad = 0
    for (x, y) in spots:
        if plan[y][x] not in OPEN:
            print(f'  UNOWN SPOT ({x},{y}) IS NOT FLOOR')
            bad += 1
        elif (x, y) in reserved:
            print(f'  UNOWN SPOT ({x},{y}) COLLIDES WITH {reserved[(x, y)]}')
            bad += 1
    if len(set(spots)) != len(spots):
        print('  UNOWN SPOTS CONTAIN DUPLICATES')
        bad += 1
    print(f'  {len(spots)} Unown spots, all clear'
          if not bad else f'  {bad} bad Unown spot(s)')


def main(argv):
    plan = PLAN
    if len({len(r) for r in plan}) != 1:
        raise SystemExit('plan rows are not all the same width')
    w, h, blocks = build(plan)

    cx, cy = w // 2, h // 2
    if plan[cy][cx] not in OPEN:
        raise SystemExit(f'the arrival tile ({cx},{cy}) is not open floor - a '
                         f'bare `warp` lands the player in the map centre')

    print(f'{LAYOUT_ID}  {w}x{h}  {PRIMARY} + {SECONDARY}')
    print(f'  arrival tile (centre): ({cx},{cy})')
    for y, row in enumerate(plan):
        marks = ''.join('@' if (x, y) == (cx, cy) else c
                        for x, c in enumerate(row))
        print(f'  {marks}')
    for y, row in enumerate(plan):
        for x, c in enumerate(row):
            if c == 'X':
                print(f'  descent   at ({x},{y})  -> warp_event goes here')
            elif c == 'G':
                print(f'  games door at ({x},{y})  -> warp_event goes here')

    used = sorted({b & 0x3FF for b in blocks})
    print(f'  {len(used)} distinct metatiles: '
          + ' '.join(f'0x{m:03X}' for m in used))
    check_unown(plan)

    if '--write' in argv:
        d = REPO / 'data/layouts' / NAME
        d.mkdir(parents=True, exist_ok=True)
        (d / 'map.bin').write_bytes(struct.pack(f'<{len(blocks)}H', *blocks))
        # 2x2 border of the cave's void, the same black the dungeon edges use
        (d / 'border.bin').write_bytes(
            struct.pack('<4H', *([block(VOID, 1, 0)] * 4)))
        print(f'  wrote {d}/map.bin and border.bin')
        print(f'  layouts.json {upsert_layout(w, h)}')


if __name__ == '__main__':
    main(sys.argv[1:])
