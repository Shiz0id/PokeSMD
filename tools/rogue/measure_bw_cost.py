"""
Measure what an animated species ACTUALLY costs in ROM, using the build's own
gbagfx and compresSmol rather than an extrapolation.

Why this exists: projecting from the shipped 2-frame sprites gave ~575 B per
frame, and that is badly wrong for animation. Frames of one species are highly
redundant, and stacking them in a single blob lets the compressor exploit that
- Treecko's 26 frames land at 218 B/frame, a third of the projection. Two-frame
sprites cannot show the effect, so it was invisible until measured.

Run:  python measure_bw_cost.py --gifs D:/PokemonTest/gifs --n 16
Needs Pillow, and a built repo (the tools are compiled by `make`).
"""
import argparse
import os
import random
import subprocess
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import downscale_bw as D


def emit_sheet(frames, path):
    w, h = frames[0].size
    tw, th = (w + 7) // 8 * 8, (h + 7) // 8 * 8
    sheet = Image.new('RGBA', (tw, th * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sheet.paste(f, (0, i * th), f)

    got = sheet.getcolors(300000) or []
    palette = [(255, 0, 255)] + sorted({c[1][:3] for c in got if c[1][3] > 0})
    if len(palette) > 16:
        return None
    px = sheet.load()
    data = []
    for y in range(sheet.height):
        for x in range(sheet.width):
            c = px[x, y]
            data.append(0 if c[3] == 0 else palette.index(c[:3]))
    idx = Image.new('P', sheet.size, 0)
    idx.putdata(data)
    flat = []
    for c in palette + [(0, 0, 0)] * (16 - len(palette)):
        flat.extend(c)
    idx.putpalette(flat)
    idx.save(path)
    return tw, th


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gifs', required=True)
    ap.add_argument('--repo', default=str(Path(__file__).resolve().parents[2]))
    ap.add_argument('--n', type=int, default=16)
    args = ap.parse_args()

    repo = Path(args.repo)
    gbagfx = repo / 'tools/gbagfx/gbagfx'
    smol = repo / 'tools/compresSmol/compresSmol'
    work = repo / '_bwtest'
    work.mkdir(exist_ok=True)

    gifs = [p for p in sorted(Path(args.gifs).glob('*/*.gif')) if '-' not in p.stem]
    random.seed(7)
    sample = random.sample(gifs, min(args.n, len(gifs)))

    rows = []
    for gp in sample:
        r = D.build_species(str(gp))
        if not r:
            continue
        frames, seq, durs, cols = r
        png = work / f'{gp.stem}.png'
        dims = emit_sheet(frames, png)
        if not dims:
            print(f'{gp.stem}: >15 colours, skipped')
            continue
        raw4 = work / f'{gp.stem}.4bpp'
        out = work / f'{gp.stem}.smol'
        try:
            subprocess.run([str(gbagfx), str(png), str(raw4)], check=True,
                           capture_output=True)
            subprocess.run([str(smol), '-w', str(raw4), str(out)], check=True,
                           capture_output=True)
        except subprocess.CalledProcessError as e:
            print(f'{gp.stem}: tool failed {e}')
            continue
        raw = raw4.stat().st_size
        comp = out.stat().st_size
        rows.append((gp.stem, dims[0], dims[1], len(frames), raw, comp))
        print(f'{gp.stem:14s} {dims[0]:3d}x{dims[1]:<3d} {len(frames):3d}f  '
              f'raw {raw:6d}  smol {comp:6d}  {100*comp//raw:3d}%  '
              f'{comp//max(len(frames),1):4d} B/frame')

    if not rows:
        raise SystemExit('nothing measured')
    tot = sum(r[5] for r in rows)
    per = tot / len(rows)
    print(f'\n{len(rows)} species measured')
    print(f'  mean per species {per:,.0f} B   median '
          f'{sorted(r[5] for r in rows)[len(rows)//2]:,} B')
    for label, n in (('Gen 1-3 base', 422), ('Gen 1-5 base', 639)):
        print(f'  {label} ({n}): {per*n/1024/1024:5.2f} MB')


if __name__ == '__main__':
    main()
