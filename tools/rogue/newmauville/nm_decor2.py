"""Vertical structure of every vanilla wall decoration, so each becomes a stamp."""
import sys, os, json, collections, struct
from pathlib import Path

os.environ['POKEDECOMP_REPO'] = '//wsl.localhost/Ubuntu/home/p50/decomps/pokeemerald-expansion'
sys.path.insert(0, r'D:\PokemonTest\.claude\skills\rogue-dungeon\scripts')
import tileset_atlas as ta

REPO = ta.REPO
L = json.loads((REPO / 'data/layouts/layouts.json').read_text(encoding='utf-8'))
ENT = {e['name']: e for e in (L['layouts'] if isinstance(L, dict) else L) if e}

TARGETS = [0x2C0, 0x2C1, 0x2D6, 0x2D7, 0x2A5, 0x2A6, 0x2E4, 0x2E5,
           0x2B0, 0x2B1, 0x2B2, 0x2A0, 0x2C2]

nb = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
solidity = collections.defaultdict(collections.Counter)

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
            if m not in TARGETS or not solid(x, y) or solid(x, y + 1):
                continue           # only count it as a wall-face occurrence
            nb[m]['above'][mid(x, y - 1)] += 1
            nb[m]['below'][mid(x, y + 1)] += 1
            nb[m]['east'][mid(x + 1, y)] += 1
            solidity[m]['wall above' if solid(x, y - 1) else 'floor above'] += 1


def fmt(c, n=2):
    tot = sum(c.values())
    return ' '.join(f'{"edge" if m is None else f"0x{m:03X}"}:{k}({100*k/tot:.0f}%)'
                    for m, k in c.most_common(n))


for m in TARGETS:
    if m not in nb:
        continue
    tot = sum(nb[m]['above'].values())
    print(f'0x{m:03X} n={tot:<3} {dict(solidity[m])}')
    print(f'     above {fmt(nb[m]["above"])}   below {fmt(nb[m]["below"])}'
          f'   east {fmt(nb[m]["east"])}')
