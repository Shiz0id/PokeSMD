"""What does vanilla put at a cap position, and when does it decline to cap?

A cap position is: solid, solid below, floor two below. Key each one on whether
its west and east neighbours are themselves cap positions - that is the run-end
question - and tally. Also count how often the answer is simply the void, which
is what tells us the current rule is too permissive.
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
        yield struct.unpack(f'<{len(raw)//2}H', raw), e['width'], e['height']


by_key = collections.defaultdict(collections.Counter)
run_len = collections.defaultdict(collections.Counter)
above_cap = collections.Counter()

for b, w, h in grids():
    def mid(x, y):
        return None if not (0 <= x < w and 0 <= y < h) else b[y * w + x] & 0x3FF
    def solid(x, y):
        return True if not (0 <= x < w and 0 <= y < h) else ((b[y * w + x] >> 10) & 3) != 0

    def is_cap_pos(x, y):
        return solid(x, y) and solid(x, y + 1) and not solid(x, y + 2)

    for y in range(h):
        for x in range(w):
            if not is_cap_pos(x, y):
                continue
            kw = is_cap_pos(x - 1, y)
            ke = is_cap_pos(x + 1, y)
            key = ('mid' if kw and ke else 'west-end' if ke else
                   'east-end' if kw else 'lone')
            by_key[key][mid(x, y)] += 1
            # how wide is the face run underneath?
            n = 1
            xx = x - 1
            while is_cap_pos(xx, y):
                n += 1; xx -= 1
            xx = x + 1
            while is_cap_pos(xx, y):
                n += 1; xx += 1
            run_len[min(n, 6)][mid(x, y)] += 1
            if mid(x, y) == 0x21F:
                above_cap[mid(x, y - 1)] += 1


def show(title, c):
    tot = sum(c.values())
    parts = ' '.join(f'{"edge" if m is None else f"0x{m:03X}"}:{k}({100*k/tot:.0f}%)'
                     for m, k in c.most_common(5))
    print(f'{title:<28} n={tot:<4} {parts}')


print('cap position, keyed on whether the neighbours are also cap positions')
for k in ('mid', 'west-end', 'east-end', 'lone'):
    show(f'  {k}', by_key[k])

print('\ncap position, keyed on the width of the run it belongs to')
for n in sorted(run_len):
    show(f'  run of {n}{"+" if n == 6 else ""}', run_len[n])

print('\nwhat sits above a 0x21F cap')
show('  above cap', above_cap)
