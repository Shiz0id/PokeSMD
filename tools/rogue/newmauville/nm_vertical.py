"""Is vanilla's New Mauville wall one metatile tall or two?

For every wall cell with floor to the SOUTH (the face you actually see), tally
what sits directly above it. If the wall is a two-row assembly - a white cap
row over a tan face row - the cell above will be a consistent second metatile
rather than the void.
"""
import sys, os, json, collections, struct
from pathlib import Path

os.environ['POKEDECOMP_REPO'] = '//wsl.localhost/Ubuntu/home/p50/decomps/pokeemerald-expansion'
sys.path.insert(0, r'D:\PokemonTest\.claude\skills\rogue-dungeon\scripts')

import tileset_atlas as ta

REPO = ta.REPO
L = json.loads((REPO / 'data/layouts/layouts.json').read_text(encoding='utf-8'))
entries = {e['name']: e for e in (L['layouts'] if isinstance(L, dict) else L) if e}


def load(name):
    e = entries[name]
    w, hgt = e['width'], e['height']
    raw = (REPO / e['blockdata_filepath']).read_bytes()
    b = struct.unpack(f'<{len(raw)//2}H', raw)
    return b, w, hgt


above = collections.Counter()      # metatile above a south-facing face
face = collections.Counter()
below = collections.Counter()
above_by_face = collections.defaultdict(collections.Counter)
# and the same question for a vertical run: what is west of a floor-to-east wall
west_of = collections.Counter()

for name in ('NewMauville_Inside_Layout', 'NewMauville_Entrance_Layout'):
    b, w, h = load(name)
    def mid(x, y):
        if not (0 <= x < w and 0 <= y < h):
            return None
        return b[y * w + x] & 0x3FF
    def solid(x, y):
        if not (0 <= x < w and 0 <= y < h):
            return True
        return ((b[y * w + x] >> 10) & 3) != 0
    for y in range(h):
        for x in range(w):
            if not solid(x, y):
                continue
            if not solid(x, y + 1):                 # floor to the south
                face[mid(x, y)] += 1
                a = mid(x, y - 1)
                above[a] += 1
                above_by_face[mid(x, y)][a] += 1
            if not solid(x + 1, y) and solid(x, y - 1) and solid(x, y + 1):
                west_of[mid(x, y)] += 1

print('metatile used for a wall cell with FLOOR TO THE SOUTH (the visible face)')
for m, n in face.most_common(8):
    print(f'   0x{m:03X}  x{n}')
print('\nwhat sits DIRECTLY ABOVE that face cell')
for m, n in above.most_common(8):
    print(f'   0x{m:03X}  x{n}')
print('\nabove, split by which face metatile it caps')
for f, c in sorted(above_by_face.items(), key=lambda kv: -sum(kv[1].values()))[:4]:
    tot = sum(c.values())
    top = ' '.join(f'0x{m:03X}:{n}' for m, n in c.most_common(4))
    print(f'   face 0x{f:03X} (n={tot}): {top}')
print('\nmetatile for a wall cell with FLOOR TO THE EAST, walls above and below')
for m, n in west_of.most_common(8):
    print(f'   0x{m:03X}  x{n}')
