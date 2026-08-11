"""Vanilla vs generated, on the two axes that still differ.

  1 wall thickness - how many cells deep is a wall mass, vertically
  2 decor density  - what fraction of visible wall faces carry a decoration
  3 open fraction  - how much of the map is walkable at all
"""
import sys, os, json, collections, struct
from pathlib import Path

os.environ['POKEDECOMP_REPO'] = '//wsl.localhost/Ubuntu/home/p50/decomps/pokeemerald-expansion'
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, r'D:\PokemonTest\.claude\skills\rogue-dungeon\scripts')
import tileset_atlas as ta
import nm_v3 as V3
import theme_mock as tm

CUR = tm.THEMES['newmauville']
REPO = ta.REPO
L = json.loads((REPO / 'data/layouts/layouts.json').read_text(encoding='utf-8'))
ENT = {e['name']: e for e in (L['layouts'] if isinstance(L, dict) else L) if e}

# a decoration is any metatile on a wall face that is not the plain face set
PLAIN = {0x227, 0x293, 0x294, 0x295, 0x296, 0x21F, 0x208, 0x270, 0x272,
         0x280, 0x282, 0x290, 0x288, 0x298, 0x2F8, 0x2F9}


def stats(is_wall, mid, w, h):
    thick = collections.Counter()
    faces = dec = 0
    open_ = 0
    for y in range(h):
        for x in range(w):
            if not is_wall(x, y):
                open_ += 1
                continue
            # visible face: floor directly below
            if not is_wall(x, y + 1):
                faces += 1
                m = mid(x, y)
                if m is not None and m not in PLAIN:
                    dec += 1
                # measure the mass depth going up from this face
                n = 0
                yy = y
                while is_wall(x, yy) and n < 20:
                    n += 1; yy -= 1
                thick[min(n, 8)] += 1
    return thick, faces, dec, open_, w * h


def show(label, thick, faces, dec, open_, total):
    tot = sum(thick.values())
    dist = ' '.join(f'{k}{"+" if k == 8 else ""}:{100*v/tot:.0f}%'
                    for k, v in sorted(thick.items()))
    print(f'{label}')
    print(f'   wall depth above a face : {dist}')
    print(f'   faces carrying decor    : {dec}/{faces} = {100*dec/max(faces,1):.0f}%')
    print(f'   walkable                : {100*open_/total:.0f}%')


for name in ('NewMauville_Inside_Layout',):
    e = ENT[name]
    raw = (REPO / e['blockdata_filepath']).read_bytes()
    b = struct.unpack(f'<{len(raw)//2}H', raw)
    w, h = e['width'], e['height']
    def is_wall(x, y, b=b, w=w, h=h):
        return True if not (0 <= x < w and 0 <= y < h) else ((b[y*w+x] >> 10) & 3) != 0
    def mid(x, y, b=b, w=w, h=h):
        return None if not (0 <= x < w and 0 <= y < h) else b[y*w+x] & 0x3FF
    show(f'VANILLA {name}', *stats(is_wall, mid, w, h))

agg = [collections.Counter(), 0, 0, 0, 0]
for seed in range(40):
    tm.W, tm.H = 48, 48
    s, _ = tm.carve(seed, CUR)
    g = V3.paint(s, CUR, 48, 48, seed, 'full')
    def is_wall(x, y, s=s):
        return True if not (0 <= x < 48 and 0 <= y < 48) else s[y][x]
    def mid(x, y, g=g):
        return None if not (0 <= x < 48 and 0 <= y < 48) else g[y][x]
    t, f, d, o, tt = stats(is_wall, mid, 48, 48)
    agg[0].update(t); agg[1] += f; agg[2] += d; agg[3] += o; agg[4] += tt
print()
show('GENERATED v3 (40 floors)', *agg)
