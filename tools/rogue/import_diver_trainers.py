"""
Import the two scuba diver trainer front pics from the ORAS-style sheet.

CREDIT. The sprites are by **Mega Recurso**, from their ORAS-style trainer
sheet; the original ORAS art was ripped by **kylepixl**. Neither is ours. Any
build shipping these has to carry that credit somewhere the player can reach -
this comment is not a substitute for it.

WHAT THESE ARE, AND ARE NOT. These are BATTLE front pics, not overworld
sprites. They do not and cannot replace the 32x32 three-frame overworld divers
in make_diver_sprites.py - that is a different slot, a different size and a
different drawing convention. They fix the other half of the illusion: the
seafloor's trainers are picked from sRogueDungeonTrainers, which is 709 stock
trainers of every class sorted by level, so the player currently walks up to a
diver and then fights a Bug Catcher.

MEASURING THE SHEET, and the mistake worth not repeating. Each cell on the
sheet is EXACTLY 64 rows tall with a five-row gutter (rows 64-68 are blank in
every column band). A first pass that scanned a taller window captured the top
of the NEXT row's sprite and reported the male as 74 tall and one colour over
budget - which produced a plan to crop his feet off. He is 64 tall, he is 15
colours, and the thing that looked croppable is his DIVE FLIPPERS.

So both fit 4bpp with nothing to negotiate:

    male    43x64   15 colours + transparent = 16
    female  42x64   15 colours + transparent = 16

Framing follows vanilla: every stock front pic is bottom-anchored in the 64x64
frame (swimmer_m ends at y=62, aqua_grunt_m at 62, leader_juan at 63). These
already fill the frame vertically, so only horizontal centring is applied.

Run:  python import_diver_trainers.py [--repo PATH] [--sheet PATH] [--preview]
Needs Pillow, so here it runs from Windows over UNC.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent

SHEET_DEFAULT = r"D:/PokemonTest/mega recurso Trainer's sprite ORAS Style.png"

FRAME = 64
CELL_H = 64          # verified: rows 64-68 of the sheet are blank in every band

# Column bands on the sheet's top row. Bands, not exact boxes - the real
# bounding box is measured inside each band, so a band only has to contain one
# sprite and no part of its neighbours.
SUBJECTS = {
    'diver_m': (66, 132),
    'diver_f': (132, 200),
}

# The sheet is RGB on white. Anything this bright is background; the artwork
# has no true white in it (checked - its lightest tone is well under this).
WHITE = 246


def extract(sheet, x0, x1):
    px = sheet.load()
    xs, ys = [], []
    for y in range(CELL_H):
        for x in range(x0, x1):
            r, g, b = px[x, y][:3]
            if not (r > WHITE and g > WHITE and b > WHITE):
                xs.append(x)
                ys.append(y)
    if not xs:
        raise SystemExit(f'no artwork found in band {x0}-{x1}')
    return sheet.crop((min(xs), min(ys), max(xs) + 1, max(ys) + 1))


def to_indexed(sub):
    """64x64 indexed image, palette entry 0 transparent, <=15 real colours."""
    px = sub.convert('RGB').load()
    colours = []
    for y in range(sub.height):
        for x in range(sub.width):
            c = px[x, y]
            if c[0] > WHITE and c[1] > WHITE and c[2] > WHITE:
                continue
            if c not in colours:
                colours.append(c)
    if len(colours) > 15:
        raise SystemExit(
            f'{len(colours)} colours; 4bpp allows 15 plus transparent. '
            f'Merge the rarest before importing.')

    # Index 0 is the transparent slot. Its colour never draws, but it must not
    # collide with a real one or gbagfx will map real pixels to transparent.
    # Magenta is the decomp's usual convention for this.
    palette = [(255, 0, 255)] + colours
    palette += [(0, 0, 0)] * (16 - len(palette))

    out = Image.new('P', (FRAME, FRAME), 0)
    ox = (FRAME - sub.width) // 2
    oy = FRAME - sub.height          # bottom-anchored, like every vanilla pic
    data = [0] * (FRAME * FRAME)
    for y in range(sub.height):
        for x in range(sub.width):
            c = px[x, y]
            if c[0] > WHITE and c[1] > WHITE and c[2] > WHITE:
                continue
            data[(y + oy) * FRAME + (x + ox)] = colours.index(c) + 1
    out.putdata(data)
    flat = []
    for rgb in palette:
        flat.extend(rgb)
    out.putpalette(flat)
    return out, len(colours)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=str(HERE.parents[1]))
    ap.add_argument('--sheet', default=SHEET_DEFAULT)
    ap.add_argument('--preview', action='store_true')
    ap.add_argument('--out', default=None)
    args = ap.parse_args()

    sheet = Image.open(args.sheet).convert('RGB')
    repo = Path(args.repo)

    made = {}
    for name, (x0, x1) in SUBJECTS.items():
        sub = extract(sheet, x0, x1)
        img, n = to_indexed(sub)
        made[name] = img
        print(f'{name}: {sub.width}x{sub.height} -> 64x64, {n} colours')

    if args.preview:
        S = 5
        out = Image.new('RGB', (len(made) * (FRAME * S + 20) + 20,
                                FRAME * S + 40), (40, 40, 50))
        for n, (name, img) in enumerate(made.items()):
            rgba = img.convert('RGBA')
            d = rgba.load()
            for y in range(FRAME):
                for x in range(FRAME):
                    if img.getpixel((x, y)) == 0:
                        d[x, y] = (40, 40, 50, 255)
            out.paste(rgba.resize((FRAME * S, FRAME * S), Image.NEAREST),
                      (20 + n * (FRAME * S + 20), 30))
        p = Path(args.out or (HERE / '_out/diver_trainers.png'))
        p.parent.mkdir(parents=True, exist_ok=True)
        out.save(p)
        print('wrote', p)
        return

    dest = repo / 'graphics/trainers/front_pics'
    for name, img in made.items():
        img.save(dest / f'rogue_{name}.png')
        print('wrote', dest / f'rogue_{name}.png')


if __name__ == '__main__':
    main()
