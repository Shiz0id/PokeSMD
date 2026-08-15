"""Build the SOUND start menu icon: an eighth note.

WHY IT IS DRAWN AND NOT LIFTED. make_charm_icon.py takes FRLG's Volcano Badge
because a shield-with-a-flame already reads as "your team is afflicted". There is
no equivalent in this repo for sound - the nearest candidates are item icons,
which are 24x24 with their own palettes, so remapping one into the shared start
menu palette would be guesswork about which source index is which. A note is
simple enough to draw exactly, and drawing it means every pixel is a known
palette index by construction.

WHAT THE START MENU EXPECTS. A 32x64 indexed PNG - TWO stacked 32x32 frames, not
one. Frame 0 is the inactive icon and every stock icon draws it in greys; frame 1
is the selected icon and draws it in colour. Getting that wrong produces an icon
that is silently half the height it should be.

So the note body is a single swapping index - grey when inactive, blue when
selected - over a constant outline, which is the same trick the charm icon plays
with its flame. The icon lights up when you select it.

Every colour below is an index into graphics/unbound_start_menu/sprites/icons.pal,
which all the start menu icons share - USM_PALTAG_ICON is one palette for the
whole menu, so a new icon cannot bring its own.

Pillow is not installed in WSL, so run this from Windows against a UNC path:
    python tools/rogue/make_sound_icon.py --repo //wsl.localhost/Ubuntu/home/...
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

ICON_PAL = 'graphics/unbound_start_menu/sprites/icons.pal'
OUT = 'graphics/unbound_start_menu/sprites/sound.png'

SIZE = 32

OUTLINE = 1        # 72 80 88, the soft dark the stock icons outline with
BORDER = 15        # 255 255 255. Censused from the stock icons rather than
                   # assumed: every one of them is a WHITE outer border over a
                   # dark inner outline, which is what gives them their sticker
                   # look. A dark-outline-only icon reads as a blob beside them.
HIGHLIGHT = 14     # 204 212 216, constant in both frames so the head keeps shape
BODY_INACTIVE = 11 # 144 144 144, a flat grey - matches every other icon at rest
BODY_ACTIVE = 10   # 49 137 186, the blue already in the shared palette
TRANSPARENT = 0


def read_jasc(path):
    lines = path.read_text().split('\n')
    return [tuple(int(v) for v in ln.split()) for ln in lines[3:3 + 16]]


def bezier_points(p0, p1, p2, steps=200):
    out = []
    for i in range(steps + 1):
        t = i / steps
        u = 1.0 - t
        out.append((u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
                    u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]))
    return out


def build_mask():
    """The filled note, as a set of (x, y). Drawn at final resolution."""
    mask = set()

    # Note head. Kept small and only slightly sheared: the first cut used
    # rx 7.2 with a 0.45 shear and the head swallowed the stem, which at 32px
    # beside the stock icons read as one blob rather than as a note.
    cx, cy, rx, ry = 10.0, 23.0, 5.4, 4.2
    for y in range(SIZE):
        for x in range(SIZE):
            dx, dy = x - cx, y - cy
            dx += dy * 0.22          # shear
            if (dx / rx) ** 2 + (dy / ry) ** 2 <= 1.0:
                mask.add((x, y))

    # Stem.
    for y in range(6, 24):
        for x in range(15, 18):
            mask.add((x, y))

    # Flag: a thinner stroke swinging off the top of the stem, so it stays a
    # distinct shape instead of fusing with the stem.
    for (fx, fy) in bezier_points((17, 6), (24, 10), (19, 17)):
        r = 1.9
        for y in range(int(fy - r) - 1, int(fy + r) + 2):
            for x in range(int(fx - r) - 1, int(fx + r) + 2):
                if 0 <= x < SIZE and 0 <= y < SIZE:
                    if (x - fx) ** 2 + (y - fy) ** 2 <= r * r:
                        mask.add((x, y))

    return {p for p in mask if 0 <= p[0] < SIZE and 0 <= p[1] < SIZE}


def outline_of(mask):
    out = set()
    for (x, y) in mask:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                p = (x + dx, y + dy)
                if p not in mask and 0 <= p[0] < SIZE and 0 <= p[1] < SIZE:
                    out.add(p)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    args = ap.parse_args()

    palette = read_jasc(args.repo / ICON_PAL)
    if len(palette) != 16:
        sys.exit('%s is not 16 colours' % ICON_PAL)

    mask = build_mask()
    outline = outline_of(mask)
    border = outline_of(mask | outline)

    # A couple of pixels on the head's upper left, so it is not a flat blob.
    highlight = {(x, y) for (x, y) in mask
                 if 6 <= x <= 8 and 20 <= y <= 22}

    img = Image.new('P', (SIZE, SIZE * 2), TRANSPARENT)
    flat = []
    for c in palette:
        flat += list(c)
    flat += [0, 0, 0] * (256 - len(palette))
    img.putpalette(flat)

    px = img.load()
    for frame, body in enumerate((BODY_INACTIVE, BODY_ACTIVE)):
        oy = frame * SIZE
        for y in range(SIZE):
            for x in range(SIZE):
                if (x, y) in border:
                    idx = BORDER
                elif (x, y) in outline:
                    idx = OUTLINE
                elif (x, y) in highlight:
                    idx = HIGHLIGHT
                elif (x, y) in mask:
                    idx = body
                else:
                    idx = TRANSPARENT
                px[x, oy + y] = idx

    used = {px[x, y] for y in range(SIZE * 2) for x in range(SIZE)}
    stray = used - {TRANSPARENT, OUTLINE, BORDER, HIGHLIGHT,
                    BODY_INACTIVE, BODY_ACTIVE}
    if stray:
        sys.exit('emitted palette indices %s that were not intended' % sorted(stray))

    img.save(args.repo / OUT)
    print('wrote %s  (%dx%d, two frames)' % (OUT, img.size[0], img.size[1]))
    print('  body is palette %d when inactive, %d when selected'
          % (BODY_INACTIVE, BODY_ACTIVE))
    print('  %d body px, %d outline px' % (len(mask), len(outline)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
