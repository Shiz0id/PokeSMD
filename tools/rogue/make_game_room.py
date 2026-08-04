"""Author the rest stop's game room, composed from vanilla Game Corner blocks.

WHY THE IDS ARE LIFTED RATHER THAN CHOSEN

gTileset_MauvilleGameCorner is a tileset this project has never touched, and a
slot machine is four metatiles that only read as a slot machine in the right
arrangement - pick one id wrong off a contact sheet and you get half a cabinet.
So every assembly here is a rectangle read straight out of
data/layouts/MauvilleCity_GameCorner/map.bin, with its collisions and
elevations, and the coordinates it came from recorded beside it.

That also carries the metatile BEHAVIOURS across for free, which is what makes
the prize counter work: the counter face is a blocking row the player talks
across to reach a clerk standing behind it, exactly as in every vanilla mart.
Rebuilding that from scratch would have needed the behaviour looked up; copying
the rows means it cannot be got wrong.

The room is deliberately incongruous. The way-station around it is cut rock and
this is neon carpet, because the dungeon is the Unown's illusion and the game
room is a piece of Mauville they dreamt out of something they found.

Idempotent. Run:  python3 tools/rogue/make_game_room.py [--write]
"""
import json
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LAYOUTS = REPO / 'data/layouts/layouts.json'
NAME = 'RogueRestStopGames'
LAYOUT_ID = 'LAYOUT_ROGUE_REST_STOP_GAMES'
PRIMARY, SECONDARY = 'gTileset_Building', 'gTileset_MauvilleGameCorner'

W, H = 16, 11

# base fill, by row band. (metatile, collision, elevation)
WALL_TOP = (0x212, 1, 0)      # vanilla y=0, the plain wall run
WALL_LOW = (0x21A, 1, 0)      # vanilla y=1
SKIRT = (0x204, 0, 3)         # vanilla y=2, walkable base of the wall
FLOOR = (0x202, 0, 3)         # the red carpet

# --- assemblies, each a rectangle lifted from MauvilleCity_GameCorner ---

# the prize counter, vanilla (9,0)-(15,4). Row 2 is the walkable strip the
# clerks stand on; row 3 is the counter face they are talked to across.
COUNTER = (
    ((0x23C, 1, 0), (0x23D, 1, 0), (0x23E, 1, 0), (0x23E, 1, 0), (0x23E, 1, 0), (0x23E, 1, 0), (0x23F, 1, 0)),
    ((0x244, 1, 0), (0x246, 1, 0), (0x245, 1, 0), (0x246, 1, 0), (0x246, 1, 0), (0x246, 1, 0), (0x247, 1, 0)),
    ((0x24C, 1, 0), (0x243, 0, 3), (0x24D, 0, 3), (0x24E, 0, 3), (0x24D, 0, 3), (0x24D, 0, 3), (0x24F, 1, 0)),
    ((0x254, 1, 0), (0x256, 1, 0), (0x255, 1, 0), (0x256, 1, 0), (0x255, 1, 0), (0x255, 1, 0), (0x257, 1, 0)),
    ((0x25C, 0, 3), (0x25D, 0, 3), (0x25E, 0, 3), (0x25E, 0, 3), (0x25E, 0, 3), (0x25D, 0, 3), (0x25F, 0, 3)),
)

# a bank of four machines with its stools, vanilla (1,5)-(4,10). The outer
# columns are the walkable stool tiles the player sits at to play.
SLOT_BANK = (
    ((0x202, 0, 3), (0x224, 0, 3), (0x225, 0, 3), (0x202, 0, 3)),
    ((0x226, 0, 3), (0x22C, 1, 0), (0x22D, 1, 0), (0x227, 0, 3)),
    ((0x226, 0, 3), (0x22C, 1, 0), (0x22D, 1, 0), (0x227, 0, 3)),
    ((0x226, 0, 3), (0x22C, 1, 0), (0x22D, 1, 0), (0x227, 0, 3)),
    ((0x226, 0, 3), (0x234, 1, 0), (0x235, 1, 0), (0x227, 0, 3)),
    ((0x202, 0, 3), (0x214, 0, 3), (0x204, 0, 3), (0x215, 0, 3)),
)

# one roulette table, vanilla (14,6)-(16,9).
ROULETTE = (
    ((0x222, 1, 0), (0x223, 1, 0), (0x20D, 0, 3)),
    ((0x22A, 1, 0), (0x22B, 1, 0), (0x20C, 0, 3)),
    ((0x232, 1, 0), (0x233, 1, 0), (0x20C, 0, 3)),
    ((0x214, 0, 3), (0x204, 0, 3), (0x215, 0, 3)),
)

PLACEMENTS = ((COUNTER, 9, 0), (SLOT_BANK, 1, 5), (ROULETTE, 11, 6))

# Where the scripts have to point. Kept here so the map.json and this file
# cannot drift apart silently - the report prints them.
EXIT = (7, 10)
CLERK_COINS, CLERK_PRIZES = (11, 2), (14, 2)
SLOTS_EAST = [(2, y) for y in range(6, 10)]    # player sits at x=1
SLOTS_WEST = [(3, y) for y in range(6, 10)]    # player sits at x=4
ROULETTE_EAST, ROULETTE_WEST = (11, 7), (12, 7)


def block(cell):
    metatile, collision, elevation = cell
    return (metatile & 0x3FF) | ((collision << 10) & 0xC00) \
        | ((elevation << 12) & 0xF000)


def build():
    grid = [[FLOOR] * W for _ in range(H)]
    for x in range(W):
        grid[0][x], grid[1][x], grid[2][x] = WALL_TOP, WALL_LOW, SKIRT
    for art, ox, oy in PLACEMENTS:
        for dy, row in enumerate(art):
            for dx, cell in enumerate(row):
                if not (0 <= ox + dx < W and 0 <= oy + dy < H):
                    raise SystemExit(f'assembly at ({ox},{oy}) runs off the map')
                grid[oy + dy][ox + dx] = cell
    return grid


def walkable(grid, x, y):
    return grid[y][x][1] == 0


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


def upsert_layout():
    text = LAYOUTS.read_text(encoding='utf-8')
    entry = TEMPLATE.format(id=LAYOUT_ID, name=NAME, w=W, h=H,
                            primary=PRIMARY, secondary=SECONDARY)
    if f'"{LAYOUT_ID}"' in text:
        start = text.index('    {\n      "id": "' + LAYOUT_ID + '"')
        end = text.index('    }', start) + len('    }')
        if text[start:end] == entry:
            return 'unchanged'
        text = text[:start] + entry + text[end:]
    else:
        marker = '\n  ]\n}'
        text = text.replace(marker, ',\n' + entry + marker)
    LAYOUTS.write_text(text, encoding='utf-8', newline='\n')
    return 'written'


def main(argv):
    grid = build()

    print(f'{LAYOUT_ID}  {W}x{H}  {PRIMARY} + {SECONDARY}')
    for y in range(H):
        print('  ' + ''.join('.' if walkable(grid, x, y) else '#'
                             for x in range(W)))

    # Every interaction point must be reachable: the player has to be able to
    # STAND next to the thing they are talking to, and a copied assembly makes
    # that easy to get subtly wrong.
    checks = [('exit', EXIT, None),
              ('coins clerk', CLERK_COINS, None),
              ('prize clerk', CLERK_PRIZES, None)]
    for label, (x, y), _ in checks:
        state = 'walkable' if walkable(grid, x, y) else 'BLOCKED'
        print(f'  {label:<12} ({x:>2},{y:>2})  {state}')
    for (x, y) in SLOTS_EAST:
        assert not walkable(grid, x, y) and walkable(grid, x - 1, y), (x, y)
    for (x, y) in SLOTS_WEST:
        assert not walkable(grid, x, y) and walkable(grid, x + 1, y), (x, y)
    print(f'  {len(SLOTS_EAST)} machines played from the west stool, '
          f'{len(SLOTS_WEST)} from the east')
    for label, (x, y), side in (('roulette E', ROULETTE_EAST, -1),
                                ('roulette W', ROULETTE_WEST, +1)):
        assert not walkable(grid, x, y), (x, y)
        assert walkable(grid, x + side, y), (x + side, y)
        print(f'  {label}   table ({x},{y}) played from ({x + side},{y})')
    if not walkable(grid, *EXIT):
        raise SystemExit('the exit tile is blocked')

    if '--write' in argv:
        d = REPO / 'data/layouts' / NAME
        d.mkdir(parents=True, exist_ok=True)
        flat = [block(grid[y][x]) for y in range(H) for x in range(W)]
        (d / 'map.bin').write_bytes(struct.pack(f'<{len(flat)}H', *flat))
        (d / 'border.bin').write_bytes(
            struct.pack('<4H', *([block(WALL_TOP)] * 4)))
        print(f'  wrote {d}/map.bin and border.bin')
        print(f'  layouts.json {upsert_layout()}')


if __name__ == '__main__':
    main(sys.argv[1:])
