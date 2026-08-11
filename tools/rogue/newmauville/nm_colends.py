"""What terminates a vertical wall run, top and bottom?

We paint 0x270/0x272 for the whole run and stop. Vanilla appears to put a
distinct piece at the foot, and something other than raw void at the head.
Census both ends of every maximal run.
"""
import sys, os, json, collections, struct
from pathlib import Path

os.environ['POKEDECOMP_REPO'] = '//wsl.localhost/Ubuntu/home/p50/decomps/pokeemerald-expansion'
sys.path.insert(0, r'D:\PokemonTest\.claude\skills\rogue-dungeon\scripts')
import tileset_atlas as ta

REPO = ta.REPO
L = json.loads((REPO / 'data/layouts/layouts.json').read_text(encoding='utf-8'))
ENT = {e['name']: e for e in (L['layouts'] if isinstance(L, dict) else L) if e}

RUN = {0x270, 0x272}
below = collections.defaultdict(collections.Counter)
above = collections.defaultdict(collections.Counter)
below_solid = collections.defaultdict(collections.Counter)

for name in ('NewMauville_Inside_Layout', 'NewMauville_Entrance_Layout'):
    e = ENT[name]
    raw = (REPO / e['blockdata_filepath']).read_bytes()
    b = struct.unpack(f'<{len(raw)//2}H', raw)
    w, h = e['width'], e['height']

    def mid(x, y):
        return None if not (0 <= x < w and 0 <= y < h) else b[y * w + x] & 0x3FF

    def solid(x, y):
        return True if not (0 <= x < w and 0 <= y < h) else ((b[y * w + x] >> 10) & 3) != 0

    for y in range(h):
        for x in range(w):
            m = mid(x, y)
            if m not in RUN:
                continue
            # bottom of a run: the cell below is not the same run metatile
            if mid(x, y + 1) != m:
                below[m][mid(x, y + 1)] += 1
                below_solid[m]['solid' if solid(x, y + 1) else 'floor'] += 1
            if mid(x, y - 1) != m:
                above[m][mid(x, y - 1)] += 1


def show(t, c, n=8):
    tot = sum(c.values())
    print(f'{t}  n={tot}')
    for m, k in c.most_common(n):
        lbl = 'edge' if m is None else f'0x{m:03X}'
        print(f'      {lbl}  x{k}  ({100*k/tot:.0f}%)')


for m in (0x270, 0x272):
    print(f'=== run of 0x{m:03X} ===')
    show('  BELOW the last cell', below[m])
    print('     ', dict(below_solid[m]))
    show('  ABOVE the first cell', above[m])
    print()
