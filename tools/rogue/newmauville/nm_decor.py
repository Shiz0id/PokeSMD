"""How big is a New Mauville wall decoration, really?

The theme pairs decor horizontally (a left and right half on the wall row).
If a bookcase is also two cells tall - wall part above, floor part below - then
every decor placement is a 2x2 and the current model paints half of one.

For each decor metatile: what sits above, below, left and right, and is the
cell below solid or floor.
"""
import sys, os, json, collections, struct
from pathlib import Path

os.environ['POKEDECOMP_REPO'] = '//wsl.localhost/Ubuntu/home/p50/decomps/pokeemerald-expansion'
sys.path.insert(0, r'D:\PokemonTest\.claude\skills\rogue-dungeon\scripts')
import tileset_atlas as ta

REPO = ta.REPO
L = json.loads((REPO / 'data/layouts/layouts.json').read_text(encoding='utf-8'))
ENT = {e['name']: e for e in (L['layouts'] if isinstance(L, dict) else L) if e}

DECOR = {0x277: 'vent', 0x2B4: 'counter', 0x2A1: 'bookcase L', 0x2A2: 'bookcase R',
         0x299: 'above bookcase L?', 0x29A: 'above bookcase R?',
         0x2C0: 'crate L?', 0x2C1: 'crate R?', 0x2B3: '?', 0x2A8: '?'}


def grids():
    for name in ('NewMauville_Inside_Layout', 'NewMauville_Entrance_Layout'):
        e = ENT[name]
        raw = (REPO / e['blockdata_filepath']).read_bytes()
        yield struct.unpack(f'<{len(raw)//2}H', raw), e['width'], e['height']


nb = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
solidity = collections.defaultdict(collections.Counter)

for b, w, h in grids():
    def mid(x, y):
        return None if not (0 <= x < w and 0 <= y < h) else b[y * w + x] & 0x3FF
    def solid(x, y):
        return True if not (0 <= x < w and 0 <= y < h) else ((b[y * w + x] >> 10) & 3) != 0
    for y in range(h):
        for x in range(w):
            m = mid(x, y)
            if m not in DECOR:
                continue
            nb[m]['above'][mid(x, y - 1)] += 1
            nb[m]['below'][mid(x, y + 1)] += 1
            nb[m]['west'][mid(x - 1, y)] += 1
            nb[m]['east'][mid(x + 1, y)] += 1
            solidity[m][('solid' if solid(x, y) else 'passable',
                         'solid below' if solid(x, y + 1) else 'floor below')] += 1


def fmt(c, n=3):
    tot = sum(c.values())
    return ' '.join(f'{"edge" if m is None else f"0x{m:03X}"}:{k}({100*k/tot:.0f}%)'
                    for m, k in c.most_common(n))


for m, name in DECOR.items():
    if m not in nb:
        continue
    tot = sum(nb[m]['above'].values())
    print(f'\n0x{m:03X}  {name}   n={tot}')
    print(f'   self     : ' + ' '.join(f'{a}/{b}:{k}' for (a, b), k in solidity[m].most_common(3)))
    for side in ('above', 'below', 'west', 'east'):
        print(f'   {side:<8} : {fmt(nb[m][side])}')
