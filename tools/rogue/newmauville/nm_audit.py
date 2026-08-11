"""Audit every vanilla wall object for the mis-typing the supercomputer had.

A decoration substitutes metatiles and its lower row is PASSABLE.
A set piece is SOLID all the way down and its body projects over the floor.

For every non-plain metatile on a wall face, walk down the column while the
cells stay non-plain, and record the solidity of each row. Any object whose
body is solid below the wall face is a set piece, not a decoration - and if it
is in our decor table, it will be drawn headless and flush, exactly as
0x2E4/0x2E5 was.
"""
import sys, os, json, collections, struct
from pathlib import Path

os.environ['POKEDECOMP_REPO'] = '//wsl.localhost/Ubuntu/home/p50/decomps/pokeemerald-expansion'
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, r'D:\PokemonTest\.claude\skills\rogue-dungeon\scripts')
import tileset_atlas as ta
import nm_v6 as V6

REPO = ta.REPO
L = json.loads((REPO / 'data/layouts/layouts.json').read_text(encoding='utf-8'))
ENT = {e['name']: e for e in (L['layouts'] if isinstance(L, dict) else L) if e}

PLAIN = {0x227, 0x293, 0x294, 0x295, 0x296, 0x21F, 0x208, 0x270, 0x272,
         0x280, 0x282, 0x290, 0x288, 0x298, 0x210, 0x237, 0x22F, 0x27F,
         0x27D, 0x275, 0x279, 0x26F, 0x268, 0x26A, 0x28B, 0x28C, 0x27E,
         0x285, 0x28D, 0x28E, 0x29D, 0x29E, 0x236, 0x22E, 0x23E}

runs = collections.Counter()

for name in ('NewMauville_Inside_Layout', 'NewMauville_Entrance_Layout'):
    e = ENT[name]
    raw = (REPO / e['blockdata_filepath']).read_bytes()
    b = struct.unpack(f'<{len(raw)//2}H', raw)
    w, h = e['width'], e['height']

    def mid(x, y):
        return b[y * w + x] & 0x3FF

    def solid(x, y):
        return ((b[y * w + x] >> 10) & 3) != 0

    for y in range(h):
        for x in range(w):
            if mid(x, y) in PLAIN or not solid(x, y):
                continue
            # must be the TOP of its own column of non-plain cells
            if y > 0 and mid(x, y - 1) not in PLAIN:
                continue
            # walk down while the object continues: non-plain AND solid. The
            # first passable cell ENDS the object - carrying on past it merges
            # whatever happens to sit below (a crate stack on the floor, say)
            # into the same run and reports a decoration as a set piece.
            col = []
            projects = False        # any solid cell flanked by floor?
            yy = y
            while yy < h and mid(x, yy) not in PLAIN and solid(x, yy):
                col.append((mid(x, yy), True))
                # a cell of the wall has wall beside it; a cell standing out in
                # the room has floor beside it. That, not the solidity of row
                # two, is what separates a set piece from a capped decoration -
                # a bookcase's shelf row is solid because it IS the wall face.
                if ((x > 0 and not solid(x - 1, yy))
                        or (x + 1 < w and not solid(x + 1, yy))):
                    projects = True
                yy += 1
            # record the one cell below, which is what distinguishes the two:
            # a decoration's is its passable floor piece
            if yy < h and mid(x, yy) not in PLAIN:
                col.append((mid(x, yy), False))
            if len(col) < 2:
                continue
            runs[(tuple(col), projects)] += 1

# which face metatiles our decor table claims
OURS = {s[3][i] for s in V6.STAMPS for i in range(s[0])}

print('vanilla wall objects, by vertical column (W = solid, . = passable)\n')
setpieces = []
for (col, projects), n in runs.most_common():
    if col[0][0] < 0x200:          # primary-tileset structures: doors, stairs
        continue
    body_solid = projects and len(col) > 1
    tag = 'SET PIECE (body solid)' if body_solid else 'decoration'
    flag = ''
    if body_solid and col[0][0] in OURS:
        flag = '   <-- IN OUR DECOR TABLE, will draw flush'
        setpieces.append(col[0][0])
    chain = ' / '.join(f'0x{m:03X}{"W" if s else "."}' for m, s in col)
    print(f'  x{n:<2} {chain:<40} {tag}{flag}')

print()
print(f'mis-typed in our table: '
      f'{", ".join(f"0x{m:03X}" for m in sorted(set(setpieces))) or "none"}')
