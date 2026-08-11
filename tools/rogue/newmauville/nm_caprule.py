"""Characterise vanilla's cap row precisely.

Open questions from the last pass:
  1. is 0x237 a cap at all, or the floor of the room above a thin partition?
  2. when the cell above a face is wall, is the cap unconditional?
  3. does the cap need wall above IT (a 3-thick mass) or does 2-thick suffice?
  4. what caps the vertical runs, and what sits above a cap?
"""
import sys, os, json, collections, struct
from pathlib import Path

os.environ['POKEDECOMP_REPO'] = '//wsl.localhost/Ubuntu/home/p50/decomps/pokeemerald-expansion'
sys.path.insert(0, r'D:\PokemonTest\.claude\skills\rogue-dungeon\scripts')

import tileset_atlas as ta

REPO = ta.REPO
L = json.loads((REPO / 'data/layouts/layouts.json').read_text(encoding='utf-8'))
ENT = {e['name']: e for e in (L['layouts'] if isinstance(L, dict) else L) if e}
MAPS = ('NewMauville_Inside_Layout', 'NewMauville_Entrance_Layout')


def load(name):
    e = ENT[name]
    raw = (REPO / e['blockdata_filepath']).read_bytes()
    return struct.unpack(f'<{len(raw)//2}H', raw), e['width'], e['height']


def cells():
    for name in MAPS:
        b, w, h = load(name)
        def mid(x, y):
            return None if not (0 <= x < w and 0 <= y < h) else b[y * w + x] & 0x3FF
        def solid(x, y):
            return True if not (0 <= x < w and 0 <= y < h) else ((b[y * w + x] >> 10) & 3) != 0
        for y in range(h):
            for x in range(w):
                yield mid, solid, x, y


print('Q1  is 0x237 solid or passable?  (and 0x21F)')
occ = collections.Counter()
for mid, solid, x, y in cells():
    m = mid(x, y)
    if m in (0x21F, 0x237, 0x227, 0x28B, 0x28C):
        occ[(m, solid(x, y))] += 1
for (m, s), n in sorted(occ.items()):
    print(f'   0x{m:03X}  {"solid" if s else "passable":9s} x{n}')

print('\nQ2  face 0x227 with WALL above -> what is that cell?')
c = collections.Counter()
for mid, solid, x, y in cells():
    if mid(x, y) == 0x227 and not solid(x, y + 1) and solid(x, y - 1):
        c[mid(x, y - 1)] += 1
tot = sum(c.values())
for m, n in c.most_common(6):
    print(f'   0x{m:03X} x{n}  ({100*n/tot:.0f}%)')
print(f'   n={tot}')

print('\nQ2b face 0x227 with FLOOR above -> what is that cell?')
c = collections.Counter()
for mid, solid, x, y in cells():
    if mid(x, y) == 0x227 and not solid(x, y + 1) and not solid(x, y - 1):
        c[mid(x, y - 1)] += 1
for m, n in c.most_common(5):
    print(f'   {"edge" if m is None else f"0x{m:03X}"} x{n}')

print('\nQ3  above a 0x21F cap: wall or floor, and which metatile?')
c = collections.Counter(); k = collections.Counter()
for mid, solid, x, y in cells():
    if mid(x, y) != 0x21F:
        continue
    k['wall' if solid(x, y - 1) else 'floor'] += 1
    c[mid(x, y - 1)] += 1
print('  ', dict(k))
for m, n in c.most_common(6):
    print(f'   {"edge" if m is None else f"0x{m:03X}"} x{n}')

print('\nQ4  cap-row end pieces: what is west/east of a 0x21F run?')
w_ = collections.Counter(); e_ = collections.Counter()
for mid, solid, x, y in cells():
    if mid(x, y) != 0x21F:
        continue
    w_[mid(x - 1, y)] += 1
    e_[mid(x + 1, y)] += 1
print('   west of cap :', ' '.join(f'{"edge" if m is None else f"0x{m:03X}"}:{n}' for m, n in w_.most_common(5)))
print('   east of cap :', ' '.join(f'{"edge" if m is None else f"0x{m:03X}"}:{n}' for m, n in e_.most_common(5)))

print('\nQ5  what is ABOVE a vertical run cell (0x270 floor-to-E / 0x272 floor-to-W)?')
for run in (0x270, 0x272):
    c = collections.Counter()
    for mid, solid, x, y in cells():
        if mid(x, y) == run:
            c[mid(x, y - 1)] += 1
    print(f'   above 0x{run:03X}: ' + ' '.join(f'{"edge" if m is None else f"0x{m:03X}"}:{n}' for m, n in c.most_common(6)))

print('\nQ6  full vertical column signature: (above, cell, below) for wall cells\n'
      '    whose south is floor, most common triples')
tri = collections.Counter()
for mid, solid, x, y in cells():
    if solid(x, y) and not solid(x, y + 1):
        tri[(mid(x, y - 1), mid(x, y))] += 1
for (a, m), n in tri.most_common(12):
    print(f'   above {"edge" if a is None else f"0x{a:03X}"}  face 0x{m:03X}   x{n}')
