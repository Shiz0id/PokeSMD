"""Where does vanilla turn the cap row into the column?

The cap cell cannot be identified by any neighbour mask - its eight neighbours
are all wall, exactly like deep interior. What distinguishes it is distance to
floor going south. So census on that instead: for each solid cell, record how
far the nearest floor is to the S / E / W, and tally the metatile.
"""
import sys, os, json, collections, struct
from pathlib import Path

os.environ['POKEDECOMP_REPO'] = '//wsl.localhost/Ubuntu/home/p50/decomps/pokeemerald-expansion'
sys.path.insert(0, r'D:\PokemonTest\.claude\skills\rogue-dungeon\scripts')
import tileset_atlas as ta

REPO = ta.REPO
L = json.loads((REPO / 'data/layouts/layouts.json').read_text(encoding='utf-8'))
ENT = {e['name']: e for e in (L['layouts'] if isinstance(L, dict) else L) if e}


def grids():
    for name in ('NewMauville_Inside_Layout', 'NewMauville_Entrance_Layout'):
        e = ENT[name]
        raw = (REPO / e['blockdata_filepath']).read_bytes()
        b = struct.unpack(f'<{len(raw)//2}H', raw)
        yield b, e['width'], e['height']


def show(title, counter, n=6):
    tot = sum(counter.values())
    if not tot:
        print(f'{title}: none')
        return
    top = ' '.join(f'0x{m:03X}:{k}({100*k/tot:.0f}%)' for m, k in counter.most_common(n)
                   if m is not None)
    print(f'{title}  n={tot}\n    {top}')


# the corner: all four cardinals wall, exactly one diagonal floor
corner = collections.defaultdict(collections.Counter)
# a solid cell two south of floor, i.e. the cap row, split by what is west of it
capwest = collections.Counter()
capeast = collections.Counter()
# the cell directly above the top of a vertical run
runtop = collections.defaultdict(collections.Counter)

for b, w, h in grids():
    def mid(x, y):
        return None if not (0 <= x < w and 0 <= y < h) else b[y * w + x] & 0x3FF
    def solid(x, y):
        return True if not (0 <= x < w and 0 <= y < h) else ((b[y * w + x] >> 10) & 3) != 0

    for y in range(h):
        for x in range(w):
            if not solid(x, y):
                continue
            card = [solid(x, y - 1), solid(x, y + 1), solid(x - 1, y), solid(x + 1, y)]
            if all(card):
                diag = {'NW': not solid(x - 1, y - 1), 'NE': not solid(x + 1, y - 1),
                        'SW': not solid(x - 1, y + 1), 'SE': not solid(x + 1, y + 1)}
                open_ = [k for k, v in diag.items() if v]
                if len(open_) == 1:
                    corner[open_[0]][mid(x, y)] += 1
            # cap row: wall, wall below is a face (floor two south)
            if solid(x, y + 1) and not solid(x, y + 2):
                capwest[mid(x - 1, y)] += 1
                capeast[mid(x + 1, y)] += 1
            # top of a vertical run: floor to one side, wall above, and the cell
            # above does NOT have floor on that side
            for side, dx in (('E', 1), ('W', -1)):
                if not solid(x + dx, y) and solid(x, y - 1) and solid(x + dx, y - 1):
                    runtop[side][mid(x, y - 1)] += 1

for k in ('SE', 'SW', 'NE', 'NW'):
    show(f'all cardinals wall, open diagonal {k}', corner[k])
print()
show('west of a cap-row cell', capwest)
show('east of a cap-row cell', capeast)
print()
for side in ('E', 'W'):
    show(f'cell above the top of a run with floor to the {side}', runtop[side])
