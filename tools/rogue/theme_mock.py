"""
Render a mock dungeon floor for a candidate theme table.

Mirrors the wall dispatch in PaintWalls() exactly, so what comes out here is
what the generator will paint. Use it to validate a new theme's metatile table
before writing any C - a wrong slot is obvious on sight and invisible in a diff.

Usage:  python theme_mock.py newmauville
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

import tileset_atlas as ta
from tileset_resolve import TilesetResolver

REPO = Path(__file__).resolve().parents[2]

W, H = 48, 48   # the real DUNGEON_WIDTH/HEIGHT, so blob positions and room
                # sizes mean the same here as they do in game
SLOTS = ('INTERIOR_LEFT', 'INTERIOR_MID', 'INTERIOR_RIGHT',
         'FACE_LEFT', 'FACE_MID', 'FACE_RIGHT',
         'NORTH_LEFT', 'NORTH_MID', 'NORTH_RIGHT',
         'CORNER_OPEN_SE', 'CORNER_OPEN_SW',
         'CORNER_OPEN_NW', 'CORNER_OPEN_NE',
         'SLIVER_VERT', 'SLIVER_HORZ', 'SLIVER_VERT_TOP', 'SLIVER_VERT_BOT',
         'SLIVER_HORZ_L', 'SLIVER_HORZ_R', 'SLIVER_ISOLATED')

PATCH_SLOTS = ('NW', 'N', 'NE', 'W', 'MID', 'E', 'SW', 'S', 'SE',
               'NW_WALL', 'N_WALL', 'NE_WALL')

THEMES = {
    'newmauville': dict(
        primary='gTileset_General', secondary='gTileset_BikeShop',
        floor=0x210, stairs=0x0AF,
        skirts={0x227: (0x22F, 0), 0x294: (0x27F, 0), 0x293: (0x22F, 0x27D),
                0x270: (0, 0x27D), 0x295: (0, 0x27D)},
        shadow_corner=0x275,
        decor=[(0x227, 0x277, 0), (0x227, 0x2B4, 0), (0x227, 0x2A1, 0x2A2)],
        decor_rarity=12,
        wall={
            'INTERIOR_LEFT': 0x272, 'INTERIOR_MID': 0x208, 'INTERIOR_RIGHT': 0x270,
            'FACE_LEFT': 0x294, 'FACE_MID': 0x227, 'FACE_RIGHT': 0x293,
            'NORTH_LEFT': 0x296, 'NORTH_MID': 0x227, 'NORTH_RIGHT': 0x295,
            'CORNER_OPEN_SE': 0x208, 'CORNER_OPEN_SW': 0x208,
            'CORNER_OPEN_NW': 0x208, 'CORNER_OPEN_NE': 0x208,
            'SLIVER_VERT': 0x290, 'SLIVER_HORZ': 0x227,
            'SLIVER_VERT_TOP': 0x288, 'SLIVER_VERT_BOT': 0x298,
            'SLIVER_HORZ_L': 0x227, 'SLIVER_HORZ_R': 0x227,
            'SLIVER_ISOLATED': 0x290,
        }),
    'fierypath': dict(
        primary='gTileset_General', secondary='gTileset_Lavaridge',
        floor=0x308, stairs=0x0A7,
        # Only a north shadow exists in this tileset; the red rock is matte.
        skirts={0x30C: (0x269, 0)},
        decor=[(0x308, 0x310, 0), (0x308, 0x311, 0),   # ember sparkle floors
               (0x271, 0x268, 0), (0x271, 0x26A, 0),   # embedded rocks
               (0x271, 0x30D, 0)],                     # boulder
        decor_rarity=12,
        wall={
            'INTERIOR_LEFT': 0x306, 'INTERIOR_MID': 0x271, 'INTERIOR_RIGHT': 0x307,
            'FACE_LEFT': 0x30E, 'FACE_MID': 0x274, 'FACE_RIGHT': 0x30F,
            'NORTH_LEFT': 0x30A, 'NORTH_MID': 0x30C, 'NORTH_RIGHT': 0x30B,
            'CORNER_OPEN_SE': 0x27B, 'CORNER_OPEN_SW': 0x27C,
            'CORNER_OPEN_NW': 0x27E, 'CORNER_OPEN_NE': 0x27E,
            'SLIVER_VERT': 0x3B9, 'SLIVER_HORZ': 0x30C,
            'SLIVER_VERT_TOP': 0x3BA, 'SLIVER_VERT_BOT': 0x3BB,
            'SLIVER_HORZ_L': 0x3BC, 'SLIVER_HORZ_R': 0x3BD,
            'SLIVER_ISOLATED': 0x3BE,
        }),
    # Kept in step with sDungeonThemes by hand. This entry had drifted - floor
    # 0x21C, NORTH_MID 0x221 and three wrong corners - which is the same class
    # of divergence that let CarveFloor paint the wrong floor for two themes.
    # A mock that disagrees with the C is worse than no mock.
    'cave': dict(
        primary='gTileset_General', secondary='gTileset_Cave',
        floor=0x201, stairs=0x214,
        # Pale sand pooling on the floor, as vanilla draws it in Shoal Cave and
        # the Desert Underpass. The _WALL row has the wall's base baked in.
        patch={'NW': 0x298, 'N': 0x299, 'NE': 0x29A,
               'W': 0x2A0, 'MID': 0x2A1, 'E': 0x2A2,
               'SW': 0x2A8, 'S': 0x2A9, 'SE': 0x2AA,
               'NW_WALL': 0x29B, 'N_WALL': 0x29C, 'NE_WALL': 0x29D},
        patch_blobs=8, patch_radius=4,
        wall={
            'INTERIOR_LEFT': 0x210, 'INTERIOR_MID': 0x211, 'INTERIOR_RIGHT': 0x212,
            'FACE_LEFT': 0x218, 'FACE_MID': 0x219, 'FACE_RIGHT': 0x21A,
            'NORTH_LEFT': 0x220, 'NORTH_MID': 0x209, 'NORTH_RIGHT': 0x222,
            'CORNER_OPEN_SE': 0x21B, 'CORNER_OPEN_SW': 0x21C,
            'CORNER_OPEN_NW': 0x223, 'CORNER_OPEN_NE': 0x223,
            'SLIVER_VERT': 0x39E, 'SLIVER_HORZ': 0x39F,
            'SLIVER_VERT_TOP': 0x3A0, 'SLIVER_VERT_BOT': 0x3A1,
            'SLIVER_HORZ_L': 0x3A2, 'SLIVER_HORZ_R': 0x3A3,
            'SLIVER_ISOLATED': 0x3A4,
        }),
    # The jungle runs on the CAVE generator even though it looks like woods: the
    # Fortree canopy is a continuous mass that tiles 1x1, not discrete 2x2 tree
    # stamps. One edge case only - the row with floor to its south.
    'jungle': dict(
        primary='gTileset_General', secondary='gTileset_Fortree',
        floor=0x001, stairs=0x245,
        decor=[(0x0C6, 0x0C7, 0), (0x017, 0x016, 0)],
        decor_rarity=2,
        patch={'NW': 0x015, 'N': 0x015, 'NE': 0x015,
               'W': 0x015, 'MID': 0x015, 'E': 0x015,
               'SW': 0x208, 'S': 0x208, 'SE': 0x208,
               'NW_WALL': 0x015, 'N_WALL': 0x015, 'NE_WALL': 0x015},
        patch_blobs=6, patch_radius=8,
        patch2={'NW': 0x0C8, 'N': 0x0C9, 'NE': 0x0CA,
                'W': 0x0D0, 'MID': 0x0D1, 'E': 0x0D2,
                'SW': 0x0D8, 'S': 0x0D9, 'SE': 0x0DA,
                'NW_WALL': 0x0C8, 'N_WALL': 0x0C9, 'NE_WALL': 0x0CA},
        patch2_blobs=10, patch2_radius=3,
        rooms=12, room_min=7, room_max=13, corridor=3,
        wall={
            'FACE_LEFT': 0x017, 'FACE_MID': 0x017, 'FACE_RIGHT': 0x017,
            'SLIVER_HORZ': 0x017, 'SLIVER_HORZ_L': 0x017,
            'SLIVER_HORZ_R': 0x017, 'SLIVER_VERT_BOT': 0x017,
            'SLIVER_ISOLATED': 0x017,
            'INTERIOR_LEFT': 0x0C6, 'INTERIOR_MID': 0x0C6,
            'INTERIOR_RIGHT': 0x0C6, 'NORTH_LEFT': 0x0C6,
            'NORTH_MID': 0x0C6, 'NORTH_RIGHT': 0x0C6,
            'CORNER_OPEN_SE': 0x0C6, 'CORNER_OPEN_SW': 0x0C6,
            'CORNER_OPEN_NW': 0x0C6, 'CORNER_OPEN_NE': 0x0C6,
            'SLIVER_VERT': 0x0C6, 'SLIVER_VERT_TOP': 0x0C6,
        }),
    # The open ocean, and the first theme where the player is SURFING rather
    # than walking. The floor is MB_OCEAN_WATER, which the engine notices under
    # the player on arrival and answers with a surf blob - no HM, no party
    # requirement. Its elevation must be 1, not the 3 every other theme uses.
    #
    # The wall mass is a 3x3 nine slice of rock, 0x338-0x34A, read off the
    # Mossdeep sea routes as a grid. A neighbour-mask census could not find it:
    # pooled with island shores and the map-edge barrier nothing beat 38%, but
    # taken alone every one of the nine is decisive for its own position.
    #
    # Corridors are 3 wide, as in the jungle, so the sliver slots never fire -
    # which is just as well, since this tileset has no one-block-thick rock.
    'ocean': dict(
        primary='gTileset_General', secondary='gTileset_Mossdeep',
        floor=0x170, stairs=0x3D1,      # the whirlpool
        rooms=12, room_min=7, room_max=13, corridor=5,
        wall={
            'NORTH_LEFT': 0x338, 'NORTH_MID': 0x339, 'NORTH_RIGHT': 0x33A,
            'INTERIOR_LEFT': 0x340, 'INTERIOR_MID': 0x341, 'INTERIOR_RIGHT': 0x342,
            'FACE_LEFT': 0x348, 'FACE_MID': 0x349, 'FACE_RIGHT': 0x34A,
            # Vanilla's own cliff inside corners under Mossdeep's palette; the
            # slivers below are composed by make_ocean_tiles.py, because the
            # nine slice is convex and the smallest vanilla sea rock is 2x2.
            'CORNER_OPEN_SE': 0x3CD, 'CORNER_OPEN_SW': 0x3CE,
            'CORNER_OPEN_NW': 0x3CF, 'CORNER_OPEN_NE': 0x3D0,
            'SLIVER_VERT': 0x3C6, 'SLIVER_HORZ': 0x3C7,
            'SLIVER_VERT_TOP': 0x3C8, 'SLIVER_VERT_BOT': 0x3C9,
            'SLIVER_HORZ_L': 0x3CA, 'SLIVER_HORZ_R': 0x3CB,
            'SLIVER_ISOLATED': 0x3CC,
        }),
    # The seafloor, and the first theme the player crosses DIVING. That is a
    # property of the MAP rather than of any metatile - MAP_TYPE_UNDERWATER
    # hands out the diving avatar on arrival - so this theme has its own map,
    # MAP_ROGUE_DUNGEON_UNDERWATER, sharing the dungeon layout.
    #
    # The best-supported wall table in the project: derive_wall_table.py on
    # LAYOUT_UNDERWATER_ROUTE126 gives every cardinal case at 68-93%, and a
    # corner-case scan over all twelve Underwater layouts gives all four inside
    # corners at 69-94% with 155-188 examples each. Nothing is composed here.
    #
    # Slivers never occur in vanilla underwater at all, so the seven sliver
    # slots are left on the wall interior: with 5-wide corridors they should
    # never fire, and the mock's unhit-slot report is what confirms it.
    'underwater': dict(
        primary='gTileset_General', secondary='gTileset_Underwater',
        floor=0x216, stairs=0x2EC,      # the whirlpool, as the ocean
        rooms=12, room_min=7, room_max=13, corridor=5,
        # Seaweed, in blobs, the way vanilla lays it. It is a single uniform 2x2
        # metatile with no edge art - 0x201 and 0x281 are the same four tiles
        # under different palettes - so every slot is the same id and the region
        # autotile degenerates to a plain fill. 0x281 is the NO_SURFACING one.
        patch={k: 0x281 for k in PATCH_SLOTS},
        patch_blobs=10, patch_radius=5,
        wall={
            'NORTH_LEFT': 0x20A, 'NORTH_MID': 0x20B, 'NORTH_RIGHT': 0x20C,
            'INTERIOR_LEFT': 0x212, 'INTERIOR_MID': 0x213, 'INTERIOR_RIGHT': 0x214,
            'FACE_LEFT': 0x21A, 'FACE_MID': 0x21B, 'FACE_RIGHT': 0x21C,
            'CORNER_OPEN_SE': 0x206, 'CORNER_OPEN_SW': 0x207,
            'CORNER_OPEN_NW': 0x222, 'CORNER_OPEN_NE': 0x223,
            'SLIVER_VERT': 0x212, 'SLIVER_HORZ': 0x21B,
            'SLIVER_VERT_TOP': 0x212, 'SLIVER_VERT_BOT': 0x212,
            'SLIVER_HORZ_L': 0x21B, 'SLIVER_HORZ_R': 0x21B,
            'SLIVER_ISOLATED': 0x21B,
        }),
    # gTileset_MirageTower is a pure art reskin of gTileset_Cave - 411 of 414
    # metatiles byte-identical, all attributes identical - so the wall table is
    # the cave's verbatim. The one difference is floor and interior swapping
    # roles: vanilla Mirage Tower walks on 0x211, so 0x201 becomes the wall
    # interior. Both are plain sand and both are MB_CAVE; keeping them distinct
    # is what lets floor decor and wall decor be told apart later.
    'miragetower': dict(
        primary='gTileset_General', secondary='gTileset_MirageTower',
        floor=0x201, stairs=0x217,      # 0x217 is a native MB_LADDER
        # Rocks embedded in the sand mass, keyed on the wall interior the same
        # way Fiery Path keys on 0x271. There is no floor decor: the tileset's
        # only floor variety is the sand drift, which is a 3x3 REGION autotile
        # and cannot be expressed as a single- or double-wide swap.
        decor=[(0x211, 0x202, 0), (0x211, 0x203, 0), (0x211, 0x229, 0)],
        decor_rarity=14,
        # The sand drift - denser and larger than the cave's, because in vanilla
        # Mirage Tower it is the dominant floor treatment rather than a pool.
        patch={'NW': 0x298, 'N': 0x299, 'NE': 0x29A,
               'W': 0x2A0, 'MID': 0x2A1, 'E': 0x2A2,
               'SW': 0x2A8, 'S': 0x2A9, 'SE': 0x2AA,
               'NW_WALL': 0x29B, 'N_WALL': 0x29C, 'NE_WALL': 0x29D},
        patch_blobs=11, patch_radius=5,
        wall={
            'INTERIOR_LEFT': 0x210, 'INTERIOR_MID': 0x211, 'INTERIOR_RIGHT': 0x212,
            'FACE_LEFT': 0x218, 'FACE_MID': 0x219, 'FACE_RIGHT': 0x21A,
            'NORTH_LEFT': 0x220, 'NORTH_MID': 0x209, 'NORTH_RIGHT': 0x222,
            'CORNER_OPEN_SE': 0x21B, 'CORNER_OPEN_SW': 0x21C,
            'CORNER_OPEN_NW': 0x223, 'CORNER_OPEN_NE': 0x223,
            'SLIVER_VERT': 0x39E, 'SLIVER_HORZ': 0x39F,
            'SLIVER_VERT_TOP': 0x3A0, 'SLIVER_VERT_BOT': 0x3A1,
            'SLIVER_HORZ_L': 0x3A2, 'SLIVER_HORZ_R': 0x3A3,
            'SLIVER_ISOLATED': 0x3A4,
        }),
    # Victory Road, one per Elite Four member. Identical tables - identical
    # METATILES, in fact, since these tilesets share gMetatiles_Cave - so the
    # only thing a mock can show that the cave's does not is the palette, which
    # is the entire point of the theme. Render them to check the recolour reads
    # at floor scale, not to check the slots.
    #
    # No patch and no decor on purpose: the cave's sand region and decor draw
    # partly from gTileset_General's palettes, which cannot be recoloured
    # without dragging every other theme along.
    # Glacia is the exception: her floor is the new snow metatile rather than
    # the cave's 0x201, and she is the only Victory Road theme with decor -
    # drifts and an ice rock, drawn in palette 6 so the recolour reaches them.
    **{f'victoryroad_{who.lower()}': dict(
        primary='gTileset_General', secondary=f'gTileset_RogueVictoryRoad{who}',
        floor=0x3A5 if who == 'Glacia' else 0x201, stairs=0x214,
        **(dict(decor=[(0x3A5, 0x3A6, 0x3A7), (0x3A5, 0x3A8, 0)],
                decor_rarity=14) if who == 'Glacia' else {}),
        wall={
            'INTERIOR_LEFT': 0x210, 'INTERIOR_MID': 0x211, 'INTERIOR_RIGHT': 0x212,
            'FACE_LEFT': 0x218, 'FACE_MID': 0x219, 'FACE_RIGHT': 0x21A,
            'NORTH_LEFT': 0x220, 'NORTH_MID': 0x209, 'NORTH_RIGHT': 0x222,
            'CORNER_OPEN_SE': 0x21B, 'CORNER_OPEN_SW': 0x21C,
            'CORNER_OPEN_NW': 0x223, 'CORNER_OPEN_NE': 0x223,
            'SLIVER_VERT': 0x39E, 'SLIVER_HORZ': 0x39F,
            'SLIVER_VERT_TOP': 0x3A0, 'SLIVER_VERT_BOT': 0x3A1,
            'SLIVER_HORZ_L': 0x3A2, 'SLIVER_HORZ_R': 0x3A3,
            'SLIVER_ISOLATED': 0x3A4,
        })
       for who in ('Sidney', 'Phoebe', 'Glacia', 'Drake')},
}


class Rng:
    """The generator's local LCG, so mock layouts look like real ones."""

    def __init__(self, seed):
        self.s = seed & 0xFFFF

    def next(self):
        self.s = (self.s * 1103515245 + 24691) & 0xFFFFFFFF
        return (self.s >> 16) & 0x7FFF


def carve(seed, theme=None):
    """Rooms with one block of padding, joined by L corridors.

    Takes the theme's openness knobs, so a theme that carves wider than the cave
    is mocked as it will actually look. Defaults are the cave's own constants.
    """
    theme = theme or {}
    cap = theme.get('rooms', 8)             # DUNGEON_ROOMS_DEFAULT
    rmin = theme.get('room_min', 5)         # DUNGEON_ROOM_MIN
    rmax = theme.get('room_max', 10)        # DUNGEON_ROOM_MAX
    cw = theme.get('corridor', 1)
    rng = Rng(seed)
    solid = [[True] * W for _ in range(H)]
    rooms = []
    for _ in range(cap * 8 + 32):
        if len(rooms) >= cap:
            break
        w = rmin + rng.next() % (rmax - rmin + 1)
        h = rmin + rng.next() % (rmax - rmin + 1)
        x = 1 + rng.next() % (W - w - 2)
        y = 1 + rng.next() % (H - h - 2)
        if any(not (x + w + 1 < b[0] or b[0] + b[2] + 1 < x or
                    y + h + 1 < b[1] or b[1] + b[3] + 1 < y) for b in rooms):
            continue
        rooms.append((x, y, w, h))
        for dy in range(h):
            for dx in range(w):
                solid[y + dy][x + dx] = False

    half = cw // 2

    def open_wide(x, y):
        for dy in range(-half, half + 1):
            for dx in range(-half, half + 1):
                if 0 <= x + dx < W and 0 <= y + dy < H:
                    solid[y + dy][x + dx] = False

    for a, b in zip(rooms, rooms[1:]):
        ax, ay = a[0] + a[2] // 2, a[1] + a[3] // 2
        bx, by = b[0] + b[2] // 2, b[1] + b[3] // 2
        for x in range(min(ax, bx), max(ax, bx) + 1):
            open_wide(x, ay)
        for y in range(min(ay, by), max(ay, by) + 1):
            open_wide(bx, y)
    return solid, rooms


def blob_hash(seed, index, salt):
    """The same hash as BlobHash() in src/rogue_dungeon.c."""
    M = 0xFFFFFFFF
    h = (seed * 2654435761) & M
    h ^= ((index + 1) * 2654435769) & M
    h ^= ((salt + 1) * 2246822519) & M
    h ^= h >> 13
    h = (h * 2654435761) & M
    h ^= h >> 15
    return h & 0xFFFF


def decor_hash(seed, x, y):
    """The same hash as DecorHash() in src/rogue_dungeon.c."""
    h = (seed * 2654435761) & 0xFFFFFFFF
    h ^= ((x + 1) * 40503) & 0xFFFFFFFF
    h ^= ((y + 1) * 24593) & 0xFFFFFFFF
    h ^= h >> 13
    h = (h * 2246822519) & 0xFFFFFFFF
    h ^= h >> 15
    return h & 0xFFFF


def paint(solid, theme, seed=0):
    """A direct transcription of PaintWalls() in src/rogue_dungeon.c."""
    wall = theme['wall']
    out = [[theme['floor']] * W for _ in range(H)]
    used = {}

    def is_wall(x, y):
        return True if not (0 <= x < W and 0 <= y < H) else solid[y][x]

    for y in range(H):
        for x in range(W):
            if not is_wall(x, y):
                continue
            n, s = not is_wall(x, y - 1), not is_wall(x, y + 1)
            w, e = not is_wall(x - 1, y), not is_wall(x + 1, y)
            if n and s and w and e:
                slot = 'SLIVER_ISOLATED'
            elif n and s:
                slot = 'SLIVER_HORZ_L' if w else 'SLIVER_HORZ_R' if e else 'SLIVER_HORZ'
            elif w and e:
                slot = ('SLIVER_VERT_TOP' if n else
                        'SLIVER_VERT_BOT' if s else 'SLIVER_VERT')
            elif s:
                slot = 'FACE_LEFT' if w else 'FACE_RIGHT' if e else 'FACE_MID'
            elif n:
                slot = 'NORTH_LEFT' if w else 'NORTH_RIGHT' if e else 'NORTH_MID'
            elif w:
                slot = 'INTERIOR_LEFT'
            elif e:
                slot = 'INTERIOR_RIGHT'
            elif not is_wall(x + 1, y + 1):
                slot = 'CORNER_OPEN_SE'
            elif not is_wall(x - 1, y + 1):
                slot = 'CORNER_OPEN_SW'
            elif not is_wall(x - 1, y - 1):
                slot = 'CORNER_OPEN_NW'
            elif not is_wall(x + 1, y - 1):
                slot = 'CORNER_OPEN_NE'
            else:
                slot = 'INTERIOR_MID'
            out[y][x] = wall[slot]
            used[slot] = used.get(slot, 0) + 1

    # Floor regions, exactly as ApplyFloorPatches. Runs before the skirts so a
    # wall's own edge art still wins for a theme that has both.
    #
    # Layers are painted in order, so a later one wins where they overlap: the
    # jungle lays long grass and then punches puddles through it.
    layers = []
    for which, key in enumerate(('patch', 'patch2')):
        blobs_key = key + '_blobs'
        if theme.get(key) and theme.get(blobs_key):
            layers.append((which, theme[key], theme[blobs_key],
                           theme.get(key + '_radius', 4)))

    for which, p, nblobs, radius in layers:
        blobs = []
        for i in range(min(nblobs, 12)):
            # The layer index is folded into the salt, as BuildPatchBlobs does,
            # so two layers never stamp the same blobs.
            a = blob_hash(seed, i, which * 2)
            b = blob_hash(seed, i, which * 2 + 1)
            blobs.append((a % W, b % H,
                          radius + ((a >> 8) % 3) - 1,
                          radius + ((b >> 8) % 3) - 1))

        def in_blob(x, y):
            """Raw membership, as InPatchBlob."""
            if not (0 <= x < W and 0 <= y < H) or is_wall(x, y):
                return False
            for cx, cy, rx, ry in blobs:
                dx, dy = abs(x - cx), abs(y - cy)
                if dx > rx or dy > ry:
                    continue
                if dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry:
                    return True
            return False

        def in_patch(x, y):
            """The raw blob eroded by one, as IsPatchCell - drops the one- and
            two-block specks left where a blob only clips a room."""
            if not in_blob(x, y):
                return False
            return (in_blob(x, y - 1) + in_blob(x, y + 1)
                    + in_blob(x - 1, y) + in_blob(x + 1, y)) >= 2

        for y in range(H):
            for x in range(W):
                if is_wall(x, y):
                    continue
                if which == 0:      # count the denominator once, not per layer
                    used['FLOOR_CELLS'] = used.get('FLOOR_CELLS', 0) + 1
                if not in_patch(x, y):
                    continue
                n, s = not in_patch(x, y - 1), not in_patch(x, y + 1)
                w, e = not in_patch(x - 1, y), not in_patch(x + 1, y)
                if n:
                    # A wall above bakes its own base into the art, so those are
                    # a separate row rather than the same one shaded.
                    if is_wall(x, y - 1):
                        slot = 'NW_WALL' if w else 'NE_WALL' if e else 'N_WALL'
                    else:
                        slot = 'NW' if w else 'NE' if e else 'N'
                elif s:
                    slot = 'SW' if w else 'SE' if e else 'S'
                elif w:
                    slot = 'W'
                elif e:
                    slot = 'E'
                else:
                    slot = 'MID'
                if p.get(slot):
                    out[y][x] = p[slot]
                    key = 'PATCH%d_%s' % (which, slot)
                    used[key] = used.get(key, 0) + 1

    # Wall skirts, exactly as ApplySkirts: keyed to the specific wall metatile
    # north or west, deterministic.
    if theme.get('skirts'):
        sk = theme['skirts']
        corner = theme.get('shadow_corner', 0)

        def skirt(x, y, east):
            if not is_wall(x, y):
                return 0
            entry = sk.get(out[y][x])
            return entry[1 if east else 0] if entry else 0

        for y in range(H):
            for x in range(W):
                if is_wall(x, y):
                    continue
                s_ = skirt(x, y - 1, False)
                e_ = skirt(x - 1, y, True)
                if s_ and e_:
                    m = corner or s_
                elif s_ or e_:
                    m = s_ or e_
                elif (corner and not is_wall(x, y - 1) and not is_wall(x - 1, y)
                      and skirt(x - 1, y - 1, False)):
                    m = corner
                else:
                    m = 0
                if m:
                    out[y][x] = m

    # Cosmetic swaps, position-hashed exactly as ApplyDecor does. NOT limited to
    # wall cells - it keys on the painted metatile, so a floor base like Fiery
    # Path's ember sparkles is as valid as a wall one. This used to skip
    # non-wall cells and so could never show a floor variant.
    if theme.get('decor') and theme.get('decor_rarity'):
        for y in range(H):
            for x in range(W):
                hsh = decor_hash(seed, x, y)
                if hsh % theme['decor_rarity']:
                    continue
                hits = [(v, ve) for b, v, ve in theme['decor']
                        if b == out[y][x]
                        and (ve == 0 or (x + 1 < W and out[y][x + 1] == b))]
                if hits:
                    v, ve = hits[(hsh >> 8) % len(hits)]
                    out[y][x] = v
                    if ve:
                        out[y][x + 1] = ve
    return out, used


def main(name):
    theme = THEMES[name]
    R = TilesetResolver(REPO)
    pair = ta.TilesetPair(ta.Tileset(R.resolve(theme['primary'])),
                          ta.Tileset(R.resolve(theme['secondary'])))
    cache = {}

    def tile(mid):
        if mid not in cache:
            cache[mid] = ta.render_metatile(pair, mid, 1)
        return cache[mid]

    panels, totals = [], {}
    for seed in (11, 29):
        solid, rooms = carve(seed, theme)
        grid, used = paint(solid, theme, seed)
        if theme.get('stairs') and rooms:          # as PrepareFloor places it
            rx, ry, rw, rh = rooms[-1]
            grid[ry + rh // 2][rx + rw // 2] = theme['stairs']
        for k, v in used.items():
            totals[k] = totals.get(k, 0) + v
        im = Image.new('RGB', (W * 16, H * 16))
        for y in range(H):
            for x in range(W):
                im.paste(tile(grid[y][x]), (x * 16, y * 16))
        panels.append(im)

    out = Image.new('RGB', (panels[0].width * 2 + 24, panels[0].height + 16),
                    (24, 24, 30))
    out.paste(panels[0], (8, 8))
    out.paste(panels[1], (16 + panels[0].width, 8))
    out = out.resize((out.width * 2, out.height * 2), Image.NEAREST)
    path = ta.OUTDIR / f'mock_{name}.png'
    out.save(path)
    print(f'wrote {path}')
    print('\nslot usage across both floors:')
    for s in SLOTS:
        n = totals.get(s, 0)
        mark = '' if n else '   <-- never hit, unvalidated'
        print(f'  {s:<18} {n:>5}  0x{theme["wall"][s]:03X}{mark}')

    floor_cells = totals.get('FLOOR_CELLS', 0)
    for which, key in enumerate(('patch', 'patch2')):
        if not theme.get(key):
            continue
        print(f'\nfloor region layer {which} ({key}):')
        covered = 0
        for s in PATCH_SLOTS:
            n = totals.get('PATCH%d_%s' % (which, s), 0)
            covered += n
            mark = '' if n else '   <-- never hit, unvalidated'
            print(f'  {s:<18} {n:>5}  0x{theme[key][s]:03X}{mark}')
        if floor_cells:
            print(f'  {"":<18} {covered:>5}  of {floor_cells} floor blocks '
                  f'({100.0 * covered / floor_cells:.0f}%)')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'newmauville')
