"""
Survey a directory of BW animated GIFs against what the GBA can actually take.

Four questions, none of which can be answered from one or two samples:

1. HOW MANY BUST 64x64? That is the GBA's maximum OBJ size, and it is the wall
   that has no cheap workaround - a bigger sprite needs subsprite composition or
   a downscale, and a downscale of pixel art is a redraw. Treecko is 43x63 and
   fits; Ho-Oh is 107x101 and does not.
2. HOW MANY UNIQUE FRAMES? A BW gif replays a small pool - Treecko is 77 frames
   drawn from 26, Gyarados 11 from 7. Storage is the pool, not the playback.
3. ARE THEY ALL 4bpp-LEGAL? Every sample so far has exactly 15 opaque colours
   across all frames, which would mean one shared palette per species and no
   quantisation. Worth confirming over the whole set rather than three.
4. WHAT DOES IT ALL COST? Measured compression on this ROM is 28.1%, or ~575 B
   per 64x64 frame.

Run:  python survey_bw_gifs.py --gifs D:/PokemonTest/gifs [--repo PATH]
Needs Pillow.
"""
import argparse
import hashlib
import re
import sys
from collections import Counter
from pathlib import Path

from PIL import Image, ImageSequence

# Measured off this ROM's link map: 28.1% of raw, raw being 2048 B for 64x64.
BYTES_PER_FRAME = 575
GBA_MAX_OBJ = 64


def gen_of_species(repo):
    """species name (lowercased, no punctuation) -> generation number."""
    out = {}
    for gen in range(1, 10):
        f = repo / f'src/data/pokemon/species_info/gen_{gen}_families.h'
        if not f.exists():
            continue
        text = f.read_text(encoding='utf-8', errors='replace')
        for m in re.finditer(r'\[SPECIES_([A-Z0-9_]+)\]\s*=', text):
            out.setdefault(m.group(1).lower().replace('_', ''), gen)
    return out


def survey_one(path):
    im = Image.open(path)
    frames, durs = [], []
    for fr in ImageSequence.Iterator(im):
        frames.append(fr.convert('RGBA'))
        durs.append(fr.info.get('duration', 0) or 0)
    if not frames:
        return None
    hashes = {hashlib.md5(f.tobytes()).digest() for f in frames}
    cols = set()
    for f in frames:
        got = f.getcolors(300000)
        if got:
            cols |= {c[1] for c in got}
    opaque = {c for c in cols if c[3] > 0}
    w, h = im.size
    return dict(w=w, h=h, frames=len(frames), uniq=len(hashes),
                colours=len(opaque), ms=sum(durs))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gifs', required=True)
    ap.add_argument('--repo', default=str(Path(__file__).resolve().parents[2]))
    ap.add_argument('--csv', default=None)
    args = ap.parse_args()

    gens = gen_of_species(Path(args.repo))
    root = Path(args.gifs)
    files = sorted(root.rglob('*.gif'))
    if not files:
        raise SystemExit(f'no gifs under {root}')

    rows = []
    for n, p in enumerate(files):
        if n % 200 == 0:
            print(f'  ... {n}/{len(files)}', file=sys.stderr)
        try:
            r = survey_one(p)
        except Exception as e:
            print(f'  SKIP {p.name}: {e}', file=sys.stderr)
            continue
        if not r:
            continue
        # "pikachu-f" / "abomasnow-mega" -> base species for the gen lookup
        base = re.sub(r'-.*$', '', p.stem).replace('-', '')
        r['name'] = p.stem
        r['gen'] = gens.get(base, 0)
        rows.append(r)

    print(f'\nsurveyed {len(rows)} gifs\n')

    over = [r for r in rows if r['w'] > GBA_MAX_OBJ or r['h'] > GBA_MAX_OBJ]
    print(f'=== 1. SIZE vs the GBA 64x64 OBJ limit ===')
    print(f'  over 64 in either axis : {len(over):5d} / {len(rows)} '
          f'({100*len(over)/len(rows):.0f}%)')
    for axis in ('w', 'h'):
        c = Counter()
        for r in rows:
            v = r[axis]
            c['<=64' if v <= 64 else '65-80' if v <= 80 else
              '81-96' if v <= 96 else '>96'] += 1
        print(f'  {axis}: ' + '  '.join(f'{k}={v}' for k, v in
                                        sorted(c.items())))

    print(f'\n=== 2. UNIQUE FRAMES ===')
    u = sorted(r['uniq'] for r in rows)
    tot_f = sum(r['frames'] for r in rows)
    tot_u = sum(r['uniq'] for r in rows)
    print(f'  min {u[0]}  median {u[len(u)//2]}  mean {tot_u/len(u):.1f}  '
          f'p90 {u[int(len(u)*0.9)]}  max {u[-1]}')
    print(f'  dedup: {tot_u:,} unique of {tot_f:,} played '
          f'({100*tot_u/tot_f:.0f}%)')

    print(f'\n=== 3. PALETTE ===')
    bad = [r for r in rows if r['colours'] > 15]
    print(f'  >15 opaque colours: {len(bad)} / {len(rows)}')
    if bad:
        worst = sorted(bad, key=lambda r: -r['colours'])[:6]
        print('  worst: ' + ', '.join(f"{r['name']}({r['colours']})"
                                      for r in worst))

    print(f'\n=== 4. ROM COST at {BYTES_PER_FRAME} B/frame ===')
    for label, hi in (('Gen 1-3', 3), ('Gen 1-5', 5), ('Gen 1-9', 9)):
        sel = [r for r in rows if 1 <= r['gen'] <= hi]
        fits = [r for r in sel if r['w'] <= GBA_MAX_OBJ and r['h'] <= GBA_MAX_OBJ]
        b = sum(r['uniq'] for r in sel) * BYTES_PER_FRAME
        bf = sum(r['uniq'] for r in fits) * BYTES_PER_FRAME
        print(f'  {label}: {len(sel):4d} sprites  {b/1024/1024:6.2f} MB   '
              f'| of those {len(fits)} fit 64x64 -> {bf/1024/1024:6.2f} MB')

    if args.csv:
        with open(args.csv, 'w', encoding='utf-8', newline='') as f:
            f.write('name,gen,w,h,frames,uniq,colours,ms\n')
            for r in rows:
                f.write(f"{r['name']},{r['gen']},{r['w']},{r['h']},"
                        f"{r['frames']},{r['uniq']},{r['colours']},{r['ms']}\n")
        print(f'\nwrote {args.csv}')


if __name__ == '__main__':
    main()
