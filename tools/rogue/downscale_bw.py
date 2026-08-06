"""
Downscale BW animation frames to the GBA's 64x64 OBJ limit, and PROVE it works
by scoring against the sprites this repo already ships.

Half of Gen 1-3's BW gifs exceed 64x64, which is the one constraint with no
cheap workaround. The saving grace is that the repo's own anim_front.png frames
are this same art already fitted to 64x64 - so a downscaler can be checked
against a known-correct answer instead of judged by eye.

WHY NOT JUST RESIZE. These are 15-colour indexed images. Any interpolating
filter (bilinear, Lanczos) invents colours that are not in the palette, and a
15-entry palette has no room to absorb them. Every method here is
palette-preserving by construction.

THE ORACLE IS NOT ALIGNED WITH FRAME 0. A BW animation does not necessarily
open on the pose the static sprite uses, so scoring a downscale of frame 0
against the shipped sprite would charge a pose difference to the downscaler.
Every frame is searched and the best match reported.

SCORING is silhouette-first: the fraction of pixels whose transparent/opaque
state agrees. Colour is scored separately over the pixels both agree are
opaque. Silhouette is what a downscale can actually destroy - colour mostly
survives because the palette is shared.

Run:  python downscale_bw.py --gifs D:/PokemonTest/gifs --validate [--limit N]
      python downscale_bw.py --gifs ... --species gyarados --dump out.png
Needs Pillow.
"""
import argparse
import re
from collections import Counter
from pathlib import Path

from PIL import Image, ImageSequence

TARGET = 64


# ----------------------------------------------------------------- methods
def ds_nearest(src, w, h):
    return src.resize((w, h), Image.NEAREST)


def _blocks(src, w, h):
    sw, sh = src.size
    px = src.load()
    for ty in range(h):
        y0, y1 = ty * sh // h, max(ty * sh // h + 1, (ty + 1) * sh // h)
        for tx in range(w):
            x0, x1 = tx * sw // w, max(tx * sw // w + 1, (tx + 1) * sw // w)
            yield tx, ty, [px[x, y] for y in range(y0, y1) for x in range(x0, x1)]


def ds_mode(src, w, h):
    """Most common colour in each source block; transparent only if the block
    is majority transparent. Palette-preserving, and biased to keep thin
    features rather than dissolve them."""
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    data = [(0, 0, 0, 0)] * (w * h)
    for tx, ty, block in _blocks(src, w, h):
        opaque = [c for c in block if c[3] > 0]
        if len(opaque) * 2 < len(block):
            continue
        data[ty * w + tx] = Counter(opaque).most_common(1)[0][0]
    out.putdata(data)
    return out


def ds_mode_keep(src, w, h):
    """As ds_mode but keeps a pixel opaque if ANY source pixel was. Preserves
    thin limbs and tails that a majority vote erodes, at the cost of fattening
    the silhouette."""
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    data = [(0, 0, 0, 0)] * (w * h)
    for tx, ty, block in _blocks(src, w, h):
        opaque = [c for c in block if c[3] > 0]
        if not opaque:
            continue
        data[ty * w + tx] = Counter(opaque).most_common(1)[0][0]
    out.putdata(data)
    return out


METHODS = {'nearest': ds_nearest, 'mode': ds_mode, 'mode_keep': ds_mode_keep}


# ----------------------------------------------------------------- helpers
def norm(s):
    return re.sub(r'[^a-z0-9]', '', s.lower())


def repo_sprite(repo, name):
    """Frame 0 of anim_front.png as RGBA; palette index 0 is transparent.

    The file has no transparency chunk - index 0 is the transparent slot by GBA
    convention. A plain convert('RGBA') makes it opaque and every sprite then
    measures a full 64x64 of content."""
    p = repo / 'graphics/pokemon' / name / 'anim_front.png'
    if not p.exists():
        return None
    im = Image.open(p)
    if im.width != 64 or im.mode != 'P':
        return None
    idx = list(im.get_flattened_data())
    pal = im.getpalette()
    out = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    out.putdata([(0, 0, 0, 0) if idx[y * im.width + x] == 0
                 else tuple(pal[idx[y * im.width + x] * 3:
                                idx[y * im.width + x] * 3 + 3]) + (255,)
                 for y in range(64) for x in range(64)])
    return out


def crop_content(im):
    bb = im.getbbox()
    return im.crop(bb) if bb else None


def q5(c):
    """Quantise 8-bit RGB to the GBA's actual 5-bit channels.

    LOAD-BEARING. The repo's palettes and the gifs' hold the SAME colours in
    different spellings: the repo writes c5*8 (Absol 65,74,106), the gifs write
    round(c5*255/31) (66,74,107). Comparing 8-bit values scored that as a total
    mismatch and reported 8.8% colour agreement on art that is identical.
    Both collapse onto the same 5-bit triple, which is all the hardware sees.
    """
    return tuple(round(v * 31 / 255) for v in c[:3])


def score(a, b):
    """(silhouette agreement, colour agreement over shared-opaque pixels)."""
    pa, pb = a.load(), b.load()
    same = shared = colour = 0
    for y in range(a.height):
        for x in range(a.width):
            oa, ob = pa[x, y][3] > 0, pb[x, y][3] > 0
            if oa == ob:
                same += 1
            if oa and ob:
                shared += 1
                if q5(pa[x, y]) == q5(pb[x, y]):
                    colour += 1
    return same / (a.width * a.height), (colour / shared if shared else 0.0)


def best_match(gif_frames, oracle, method):
    """Downscale every frame to the oracle's content size and keep the best."""
    oc = crop_content(oracle)
    if oc is None:
        return None
    best = None
    for n, fr in enumerate(gif_frames):
        fc = crop_content(fr)
        if fc is None:
            continue
        cand = METHODS[method](fc, oc.width, oc.height)
        sil, col = score(cand, oc)
        # Rank on silhouette AND colour together. Ranking on silhouette alone
        # picks a pose that matches in outline but not in content - Altaria
        # scored a 100% silhouette against a frame whose colours were wrong,
        # and every such species then looked like a downscaler failure.
        rank = sil + col
        if best is None or rank > best[1] + best[2]:
            best = (n, sil, col, cand)
    return best


def build_species(gif_path, method='mode', target=TARGET):
    """Downscale a whole species consistently.

    ONE SCALE AND ONE ORIGIN FOR ALL FRAMES. Cropping each frame to its own
    bounding box and fitting that to 64x64 is the obvious approach and it is
    wrong: a limb that extends in frame 12 enlarges that frame's box, which
    shrinks its scale, and the sprite visibly breathes and drifts through the
    animation. The union of every frame's box is the species' true extent, so
    it is what gets fitted - every frame is then cropped to that same window at
    that same scale and stays registered.

    Dedupe happens AFTER downscaling, not before: frames that differ by a pixel
    or two at full resolution frequently collapse into the same 64x64 image, and
    every collapse is a frame that costs no ROM.

    Returns (frames, sequence, durations, colours) where sequence indexes frames.
    """
    src = Image.open(gif_path)
    raw, durs = [], []
    for fr in ImageSequence.Iterator(src):
        raw.append(fr.convert('RGBA'))
        durs.append(fr.info.get('duration', 0) or 0)

    union = None
    for f in raw:
        bb = f.getbbox()
        if not bb:
            continue
        union = bb if union is None else (min(union[0], bb[0]), min(union[1], bb[1]),
                                          max(union[2], bb[2]), max(union[3], bb[3]))
    if union is None:
        return None

    uw, uh = union[2] - union[0], union[3] - union[1]
    scale = min(target / uw, target / uh, 1.0)
    tw, th = max(1, round(uw * scale)), max(1, round(uh * scale))

    frames, seq, seen = [], [], {}
    for f in raw:
        small = METHODS[method](f.crop(union), tw, th)
        key = small.tobytes()
        if key not in seen:
            seen[key] = len(frames)
            frames.append(small)
        seq.append(seen[key])

    cols = set()
    for f in frames:
        got = f.getcolors(70000)
        if got:
            cols |= {q5(c[1]) for c in got if c[1][3] > 0}
    return frames, seq, durs, cols


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gifs', required=True)
    ap.add_argument('--repo', default=str(Path(__file__).resolve().parents[2]))
    ap.add_argument('--limit', type=int, default=40)
    ap.add_argument('--species')
    ap.add_argument('--dump')
    ap.add_argument('--validate', action='store_true')
    args = ap.parse_args()

    repo = Path(args.repo)
    gifdir = Path(args.gifs)
    repo_names = {norm(p.name): p.name
                  for p in (repo / 'graphics/pokemon').iterdir() if p.is_dir()}

    targets = []
    for gp in sorted(gifdir.glob('*/*.gif')):
        if args.species:
            if norm(gp.stem) != norm(args.species):
                continue
        elif '-' in gp.stem or len(targets) >= args.limit:
            if len(targets) >= args.limit:
                break
            continue
        rn = repo_names.get(norm(gp.stem))
        if rn:
            targets.append((gp, rn))

    if not targets:
        raise SystemExit('no species matched')

    totals = {m: [0.0, 0.0, 0] for m in METHODS}
    for gp, rn in targets:
        oracle = repo_sprite(repo, rn)
        if oracle is None:
            continue
        frames = [f.convert('RGBA') for f in ImageSequence.Iterator(Image.open(gp))]
        line = f'{gp.stem:14s}'
        for m in METHODS:
            r = best_match(frames, oracle, m)
            if not r:
                continue
            _, sil, col, _ = r
            totals[m][0] += sil
            totals[m][1] += col
            totals[m][2] += 1
            line += f'  {m}: sil {sil*100:5.1f}% col {col*100:5.1f}%'
        print(line)

        if args.dump and args.species:
            oc = crop_content(oracle)
            best = best_match(frames, oracle, 'mode')
            S = 6
            out = Image.new('RGB', (3 * (oc.width * S + 12) + 12,
                                    oc.height * S + 40), (35, 35, 42))
            for i, (lab, im) in enumerate([('oracle (repo)', oc),
                                           ('downscaled', best[3]),
                                           ('gif frame', crop_content(frames[best[0]]))]):
                b = im.resize((oc.width * S, oc.height * S), Image.NEAREST)
                out.paste(b, (12 + i * (oc.width * S + 12), 30), b)
            out.save(args.dump)
            print(f'wrote {args.dump} (best gif frame {best[0]})')

    if args.validate:
        print('\n=== averages ===')
        for m, (s, c, n) in totals.items():
            if n:
                print(f'  {m:10s} silhouette {100*s/n:5.1f}%   colour {100*c/n:5.1f}%   '
                      f'({n} species)')


if __name__ == '__main__':
    main()
