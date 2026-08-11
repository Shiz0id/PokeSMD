"""Extract every multi-cell furniture object from vanilla New Mauville.

Rather than censusing one decoration at a time, find connected clusters of
"not plain wall or floor" metatiles and dump each distinct cluster shape as a
block, with per-cell solidity. That surfaces objects of any size - including
ones that straddle the wall/floor boundary, like the generator, where only the
top row is part of the wall.
"""
import sys, os, json, collections, struct
from pathlib import Path

os.environ['POKEDECOMP_REPO'] = '//wsl.localhost/Ubuntu/home/p50/decomps/pokeemerald-expansion'
sys.path.insert(0, r'D:\PokemonTest\.claude\skills\rogue-dungeon\scripts')
import tileset_atlas as ta

REPO = ta.REPO
L = json.loads((REPO / 'data/layouts/layouts.json').read_text(encoding='utf-8'))
ENT = {e['name']: e for e in (L['layouts'] if isinstance(L, dict) else L) if e}

# the structural vocabulary: walls, caps, columns, floors, skirts
PLAIN = {0x227, 0x293, 0x294, 0x295, 0x296, 0x21F, 0x208, 0x270, 0x272,
         0x280, 0x282, 0x290, 0x288, 0x298, 0x210, 0x237, 0x22F, 0x27F,
         0x27D, 0x275, 0x279, 0x26F, 0x268, 0x26A, 0x28B, 0x28C, 0x21E,
         0x2C6, 0x2C7, 0x2CE, 0x2CF, 0x0AF}

shapes = collections.Counter()
examples = {}

for name in ('NewMauville_Inside_Layout', 'NewMauville_Entrance_Layout'):
    e = ENT[name]
    raw = (REPO / e['blockdata_filepath']).read_bytes()
    b = struct.unpack(f'<{len(raw)//2}H', raw)
    w, h = e['width'], e['height']

    def mid(x, y):
        return b[y * w + x] & 0x3FF

    def solid(x, y):
        return ((b[y * w + x] >> 10) & 3) != 0

    seen = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if seen[y][x] or mid(x, y) in PLAIN:
                continue
            # flood fill the cluster, 4-connected
            stack, cells = [(x, y)], []
            seen[y][x] = True
            while stack:
                cx, cy = stack.pop()
                cells.append((cx, cy))
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = cx + dx, cy + dy
                    if (0 <= nx < w and 0 <= ny < h and not seen[ny][nx]
                            and mid(nx, ny) not in PLAIN):
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            if len(cells) < 2:
                continue
            xs = [c[0] for c in cells]; ys = [c[1] for c in cells]
            x0, y0 = min(xs), min(ys)
            bw, bh = max(xs) - x0 + 1, max(ys) - y0 + 1
            if bw * bh > 12 or len(cells) != bw * bh:
                continue                    # only clean rectangles
            block = tuple(tuple(mid(x0 + i, y0 + j) for i in range(bw))
                          for j in range(bh))
            shapes[(bw, bh, block)] += 1
            examples.setdefault((bw, bh, block),
                                tuple(tuple(solid(x0 + i, y0 + j) for i in range(bw))
                                      for j in range(bh)))

print(f'{len(shapes)} distinct rectangular objects\n')
for (bw, bh, block), n in shapes.most_common():
    if bh < 2:
        continue
    sol = examples[(bw, bh, block)]
    print(f'{bw}x{bh}  seen {n}x')
    for j in range(bh):
        row = '  '.join(f'0x{block[j][i]:03X}{"W" if sol[j][i] else "."}'
                        for i in range(bw))
        print(f'     {row}')
    print()
