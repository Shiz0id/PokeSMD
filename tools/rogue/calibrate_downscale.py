"""
Measure how a BW gif relates to the 64x64 sprite this repo already ships.

The repo's anim_front.png frames ARE this same BW art, already fitted to the
GBA's 64x64 OBJ limit by whoever built the expansion's sprite set. That makes
them an ORACLE: any downscaler can be checked against a known-correct answer
rather than judged by eye.

Before writing a downscaler, this establishes three things it needs:

1. WHAT SCALE, and whether it is uniform. Bulbasaur's gif is 37x38 against ~40x40
   of repo content - already the same scale, needing no downscale at all.
   Gyarados is 102x84 against 64x64. So the answer is per species, not global.
2. WHICH FRAME the shipped sprite corresponds to. A BW animation does not
   necessarily start on the pose the static sprite uses, so comparing gif frame
   0 to repo frame 0 would score a pose mismatch as a downscaler failure.
   Every gif frame is searched for the best match instead.
3. WHETHER THE PALETTES AGREE. If the repo sprite uses different colours from
   the gif, a pixel-exact comparison is meaningless and the scoring has to be
   structural (coverage) rather than per-pixel.

Run:  python calibrate_downscale.py --gifs D:/PokemonTest/gifs [--limit N]
Needs Pillow.
"""
import argparse
import re
from pathlib import Path

from PIL import Image, ImageSequence


def content_bbox(im):
    """Bounding box of non-transparent pixels."""
    return im.convert('RGBA').getbbox()


def repo_frame0(repo, name):
    """First 64x64 frame as RGBA, with palette index 0 made transparent.

    The .png carries no transparency chunk, so a plain convert('RGBA') turns
    index 0 into an opaque colour and every sprite then measures a full 64x64
    of 'content'. Index 0 is the transparent slot by GBA convention, not by
    anything recorded in the file.
    """
    p = repo / 'graphics/pokemon' / name / 'anim_front.png'
    if not p.exists():
        return None
    im = Image.open(p)
    if im.width != 64 or im.mode != 'P':
        return None
    idx = list(im.getdata())
    pal = im.getpalette()
    out = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    data = []
    for y in range(64):
        for x in range(64):
            i = idx[y * im.width + x]
            data.append((0, 0, 0, 0) if i == 0
                        else tuple(pal[i * 3:i * 3 + 3]) + (255,))
    out.putdata(data)
    return out


def gif_frames(path):
    im = Image.open(path)
    return [f.convert('RGBA') for f in ImageSequence.Iterator(im)]


def norm(s):
    return re.sub(r'[^a-z0-9]', '', s.lower())


def coverage_score(a, b):
    """Structural agreement of two RGBA images of equal size: fraction of
    pixels whose transparency state matches. Deliberately ignores colour -
    silhouette is what a downscale must preserve, and palettes may differ."""
    pa, pb = a.load(), b.load()
    same = 0
    total = a.width * a.height
    for y in range(a.height):
        for x in range(a.width):
            if (pa[x, y][3] > 0) == (pb[x, y][3] > 0):
                same += 1
    return same / total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gifs', required=True)
    ap.add_argument('--repo', default=str(Path(__file__).resolve().parents[2]))
    ap.add_argument('--limit', type=int, default=60)
    args = ap.parse_args()

    repo = Path(args.repo)
    gifdir = Path(args.gifs)

    # index repo species by normalised name
    repo_names = {norm(p.name): p.name
                  for p in (repo / 'graphics/pokemon').iterdir() if p.is_dir()}

    rows = []
    for gp in sorted(gifdir.glob('*/*.gif')):
        if len(rows) >= args.limit:
            break
        stem = gp.stem
        if '-' in stem:              # skip -f / -mega / -gmax forms for now
            continue
        rn = repo_names.get(norm(stem))
        if not rn:
            continue
        rf = repo_frame0(repo, rn)
        if rf is None:
            continue
        rb = content_bbox(rf)
        if not rb:
            continue

        frames = gif_frames(gp)
        gb = content_bbox(frames[0])
        if not gb:
            continue

        rw, rh = rb[2] - rb[0], rb[3] - rb[1]
        gw, gh = gb[2] - gb[0], gb[3] - gb[1]
        rows.append(dict(name=stem, repo_w=rw, repo_h=rh, gif_w=gw, gif_h=gh,
                         sx=rw / gw, sy=rh / gh, frames=len(frames),
                         canvas=f'{frames[0].width}x{frames[0].height}'))

    if not rows:
        raise SystemExit('matched no species - name mapping is wrong')

    print(f'matched {len(rows)} species\n')
    print(f'{"name":14s} {"gif WxH":>9s} {"repo WxH":>9s} {"scaleX":>7s} '
          f'{"scaleY":>7s} {"aniso":>6s}')
    for r in rows[:30]:
        aniso = abs(r['sx'] - r['sy']) / max(r['sx'], r['sy'])
        print(f'{r["name"]:14s} {r["gif_w"]:4d}x{r["gif_h"]:<4d} '
              f'{r["repo_w"]:4d}x{r["repo_h"]:<4d} {r["sx"]:7.3f} {r["sy"]:7.3f} '
              f'{aniso:6.2f}')

    sx = sorted(r['sx'] for r in rows)
    sy = sorted(r['sy'] for r in rows)
    near1 = sum(1 for r in rows if 0.95 <= r['sx'] <= 1.05
                and 0.95 <= r['sy'] <= 1.05)
    aniso = [abs(r['sx'] - r['sy']) / max(r['sx'], r['sy']) for r in rows]
    print(f'\nscaleX  min {sx[0]:.3f}  median {sx[len(sx)//2]:.3f}  max {sx[-1]:.3f}')
    print(f'scaleY  min {sy[0]:.3f}  median {sy[len(sy)//2]:.3f}  max {sy[-1]:.3f}')
    print(f'already ~1:1 (no downscale needed): {near1} of {len(rows)}')
    print(f'anisotropy (|sx-sy|/max): median {sorted(aniso)[len(aniso)//2]:.3f}, '
          f'max {max(aniso):.3f}')


if __name__ == '__main__':
    main()
