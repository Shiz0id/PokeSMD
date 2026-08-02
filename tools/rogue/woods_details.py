"""What sits above/below a vanilla tree, and what edges long grass uses."""
import json, struct, collections
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
L = json.loads((REPO / 'data/layouts/layouts.json').read_text(encoding='utf-8'))
e = next(x for x in (L['layouts'] if isinstance(L, dict) else L)
         if x and x.get('id') == 'LAYOUT_PETALBURG_WOODS')
w, h = e['width'], e['height']
b = struct.unpack(f'<{w*h}H', (REPO / e['blockdata_filepath']).read_bytes())
mt = lambda x, y: b[y*w+x] & 0x3FF if 0 <= x < w and 0 <= y < h else -1

TREE_TL, TREE_TR, TREE_BL, TREE_BR = 0x1D4, 0x1D5, 0x1DC, 0x1DD
LONG = 0x015

above = collections.Counter()
below = collections.Counter()
for y in range(h):
    for x in range(w):
        if mt(x, y) == TREE_TL and mt(x+1, y) == TREE_TR:
            above[(mt(x, y-1), mt(x+1, y-1))] += 1
        if mt(x, y) == TREE_BL and mt(x+1, y) == TREE_BR:
            below[(mt(x, y+1), mt(x+1, y+1))] += 1

print('directly ABOVE a tree top row (most common pairs):')
for (a, c), n in above.most_common(5):
    print(f'   0x{a:03X},0x{c:03X}  x{n}')
print('directly BELOW a tree bottom row:')
for (a, c), n in below.most_common(5):
    print(f'   0x{a:03X},0x{c:03X}  x{n}')

print()
lg = [(x, y) for y in range(h) for x in range(w) if mt(x, y) == LONG]
print(f'long grass 0x015 occurrences: {len(lg)}')
under = collections.Counter(mt(x, y+1) for x, y in lg)
print('directly below long grass:')
for m, n in under.most_common(5):
    print(f'   0x{m:03X}  x{n}')
