"""Prototype both candidate wall languages for the New Mauville theme and
render them against the same fixtures. Nothing here touches the repo.

  A - stay in vanilla's lit-edges-over-void language, close the room outline
      at the four convex corners. Pure splice: no new pixels at all.
  B - light the facility: solid tan wall mass with trim on floor-facing sides,
      void dropped for the mass. New 8x8 tiles, drawn from palette 6's ramp.

Renders its own metatiles rather than going through ta.render_metatile, so
virtual tiles (ids >= VBASE) composite exactly as the hardware would.
"""
import sys, os, struct
from pathlib import Path

os.environ['POKEDECOMP_REPO'] = '//wsl.localhost/Ubuntu/home/p50/decomps/pokeemerald-expansion'
SP = Path(r'C:\Users\P50\AppData\Local\Temp\claude\D--PokemonTest\b5cdbeaf-d7a6-4be1-8e42-d9e72e5b72ab\scratchpad\mock')
sys.path.insert(0, str(SP))

from PIL import Image, ImageDraw
import tileset_atlas as ta
import theme_mock as tm
from tileset_resolve import TilesetResolver

OUT = Path(__file__).resolve().parent / 'nm_out'
OUT.mkdir(parents=True, exist_ok=True)

R = TilesetResolver(tm.REPO)
SECR = R.resolve('gTileset_BikeShop')
pair = ta.TilesetPair(ta.Tileset(R.resolve('gTileset_General')), ta.Tileset(SECR))
MT = SECR['metatiles'].read_bytes()
PALS = [ta.parse_jasc_pal(p) for p in SECR['palettes']]

VBASE = 0x380          # virtual tile ids, above anything bike_shop uses
VTILES = {}            # id -> 8x8 grid of palette indices
VMETA = {}             # metatile id -> 8 entries

# palette 6 ramp, by name
DK, BR, TAN, HI, CR = 1, 2, 3, 4, 5


def T(rows):
    """8 strings of 8 hex digits -> grid."""
    g = [[int(c, 16) for c in r] for r in rows]
    assert len(g) == 8 and all(len(r) == 8 for r in g)
    return g


def add_tile(name, rows):
    tid = VBASE + len(VTILES)
    VTILES[tid] = T(rows)
    globals()[name] = tid
    return tid


def entries(gid):
    i = gid - pair.num_tiles_primary
    return list(struct.unpack('<8H', MT[i * 16:(i + 1) * 16]))


def E(tile, pal=6, xf=False, yf=False):
    return (tile & 0x3FF) | (0x400 if xf else 0) | (0x800 if yf else 0) | (pal << 12)


def meta(gid, bottom, top=(0, 0, 0, 0)):
    VMETA[gid] = list(bottom) + list(top)


def tile_grid(tid):
    if tid >= VBASE:
        return VTILES[tid]
    return pair.tile_pixels(tid)


def render_metatile(gid, scale=1):
    if gid not in VMETA:                 # an existing metatile: let the
        return ta.render_metatile(pair, gid, scale)   # library draw it
    ent = VMETA[gid]
    img = Image.new('RGB', (16, 16), (0, 0, 0))
    px = img.load()
    for layer in (0, 1):
        for q in range(4):
            v = ent[layer * 4 + q]
            tid, pal = v & 0x3FF, (v >> 12) & 0xF
            if layer == 1 and v == 0:
                continue
            g = tile_grid(tid)
            if g is None:
                continue
            xf, yf = bool(v & 0x400), bool(v & 0x800)
            ox, oy = (q % 2) * 8, (q // 2) * 8
            for yy in range(8):
                sy = 7 - yy if yf else yy
                for xx in range(8):
                    sx = 7 - xx if xf else xx
                    ci = g[sy][sx]
                    if layer == 1 and ci == 0:
                        continue
                    px[ox + xx, oy + yy] = PALS[pal][ci]
    if scale != 1:
        img = img.resize((16 * scale, 16 * scale), Image.NEAREST)
    return img


# --------------------------------------------------------------- option A
# The band's own halves, extended one half-metatile past the room so the tan
# band meets the cream side edge instead of stepping to black. No new pixels:
# 0x204/0x205 are the band's top halves, 0x214/0x215 its bottom halves.
A_SE, A_SW, A_NE, A_NW = 0x2F8, 0x2F9, 0x2FA, 0x2FB
BLK = E(0x201, 7)
meta(A_SE, (BLK, BLK, BLK, BLK), (0, E(0x204), 0, E(0x214)))   # floor to SE
meta(A_SW, (BLK, BLK, BLK, BLK), (E(0x205), 0, E(0x215), 0))   # floor to SW
meta(A_NE, (BLK, BLK, BLK, BLK), (0, E(0x204), 0, E(0x214)))   # floor to NE
meta(A_NW, (BLK, BLK, BLK, BLK), (E(0x205), 0, E(0x215), 0))   # floor to NW

THEME_A = {k: (dict(v) if isinstance(v, dict) else v)
           for k, v in tm.THEMES['newmauville'].items()}
THEME_A['wall'] = dict(tm.THEMES['newmauville']['wall'])
THEME_A['wall'].update({'CORNER_OPEN_SE': A_SE, 'CORNER_OPEN_SW': A_SW,
                        'CORNER_OPEN_NE': A_NE, 'CORNER_OPEN_NW': A_NW})

# --------------------------------------------------------------- option B
# Fine noise over two adjacent ramp indices, never the extremes, per the floor
# rule in metatile-art.md - a feature here becomes a lattice every 16px.
add_tile('B_FILL', ['33333433', '33333333', '34333333', '33333343',
                    '33333333', '33433333', '33333333', '43333333'])
# south-facing front: tan surface, highlight, brown face, contact line
add_tile('B_S',    ['33333333', '33433333', '44444444', '33333333',
                    '22222222', '22222222', '22222222', '11111111'])
# north-facing: dark contact line, highlight, then surface
add_tile('B_N',    ['11111111', '44444444', '33333333', '33334333',
                    '33333333', '33333333', '34333333', '33333333'])
# west-facing: dark edge, brown, highlight, surface
add_tile('B_W',    ['12243333', '12243333', '12243333', '12243343',
                    '12243333', '12243333', '12433333', '12243333'])
# outer corner where the north edge turns into the west edge
add_tile('B_NW',   ['11111111', '14444444', '12243333', '12243333',
                    '12243333', '12243333', '12243333', '12243333'])
# outer corner where the west edge turns into the south front
add_tile('B_SW',   ['12243333', '12243333', '12444444', '12333333',
                    '12222222', '12222222', '12222222', '11111111'])
# concave corner: the mass is solid, one diagonal is floor. Its job is to carry
# the dark edge round in a continuous L and show no floor of its own.
add_tile('B_ISE',  ['33333333', '33333333', '33333344', '33333341',
                    '33333412', '33334122', '33334122', '33334122'])

B = {}
def bmeta(gid, tl, tr, bl, br, name):
    meta(gid, (E(0x201, 7),) * 4, (tl, tr, bl, br))
    B[name] = gid

F, S, N, W = E(B_FILL), E(B_S), E(B_N), E(B_W)
Ec = E(B_W, xf=True)
NW, NE = E(B_NW), E(B_NW, xf=True)
SW, SE = E(B_SW), E(B_SW, xf=True)
ISE = E(B_ISE)
ISW, INE, INW = E(B_ISE, xf=True), E(B_ISE, yf=True), E(B_ISE, xf=True, yf=True)

g = 0x300
bmeta(g + 0, F, F, S, S, 'FACE_MID');        bmeta(g + 1, N, N, F, F, 'NORTH_MID')
bmeta(g + 2, W, F, W, F, 'INTERIOR_LEFT');   bmeta(g + 3, F, Ec, F, Ec, 'INTERIOR_RIGHT')
bmeta(g + 4, F, F, F, F, 'INTERIOR_MID')
bmeta(g + 5, W, F, SW, S, 'FACE_LEFT');      bmeta(g + 6, F, Ec, S, SE, 'FACE_RIGHT')
bmeta(g + 7, NW, N, W, F, 'NORTH_LEFT');     bmeta(g + 8, N, NE, F, Ec, 'NORTH_RIGHT')
bmeta(g + 9, F, F, F, ISE, 'CORNER_OPEN_SE');bmeta(g + 10, F, F, ISW, F, 'CORNER_OPEN_SW')
bmeta(g + 11, INW, F, F, F, 'CORNER_OPEN_NW');bmeta(g + 12, F, INE, F, F, 'CORNER_OPEN_NE')
bmeta(g + 13, W, Ec, W, Ec, 'SLIVER_VERT')
bmeta(g + 14, NW, NE, SW, SE, 'SLIVER_ISOLATED')
bmeta(g + 15, NW, NE, W, Ec, 'SLIVER_VERT_TOP')
bmeta(g + 16, W, Ec, SW, SE, 'SLIVER_VERT_BOT')
bmeta(g + 17, N, N, S, S, 'SLIVER_HORZ')
bmeta(g + 18, NW, N, SW, S, 'SLIVER_HORZ_L')
bmeta(g + 19, N, NE, S, SE, 'SLIVER_HORZ_R')

THEME_B = {k: v for k, v in tm.THEMES['newmauville'].items()}
THEME_B['wall'] = {slot: B[slot] for slot in tm.SLOTS}
# the mass is lit now, so the skirt shadows the wall used to need are wrong
THEME_B['skirts'] = {}
THEME_B['shadow_corner'] = 0
