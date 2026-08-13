"""Build the CHARMS start menu icon from FRLG's Volcano Badge.

WHY THIS BADGE. It is a shield with a flame set inside it, which is already the
visual language of a status condition - so it reads as "something is affecting
your team" before the player has learned what a charm is.

WHERE IT COMES FROM. graphics/trainer_card/frlg/badges.png, eight 16x16 Kanto
badges in Gym order, so the Volcano Badge is index 6. That sheet is drawn in
GREYS: its colour comes from whichever trainer-card palette is loaded over it
(blue.pal, gold.pal and the rest live beside it), which is exactly why it can be
recoloured into the start menu's palette without fighting anything.

WHAT THE START MENU EXPECTS. A 32x64 indexed PNG - TWO stacked 32x32 frames, not
one. Frame 0 is the inactive icon and every stock icon draws it in greys; frame 1
is the selected icon and draws it in colour. Getting that wrong produces an icon
that is silently half the height it should be.

That convention is what makes this badge the right pick twice over: the flame is
a single palette index in the source, so it can be grey in frame 0 and lit in
frame 1 with no redrawing at all. The icon lights up when you select it.

Every colour below is an index into graphics/unbound_start_menu/sprites/icons.pal,
which all the start menu icons share - USM_PALTAG_ICON is one palette for the
whole menu, so a new icon cannot bring its own.

Usage:  python3 tools/rogue/make_charm_icon.py [--repo PATH]
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

BADGES = 'graphics/trainer_card/frlg/badges.png'
ICON_PAL = 'graphics/unbound_start_menu/sprites/icons.pal'
OUT = 'graphics/unbound_start_menu/sprites/charms.png'

VOLCANO_INDEX = 6      # Kanto gym order: Boulder, Cascade, Thunder, Rainbow,
                       # Soul, Marsh, VOLCANO, Earth
BADGE_SIZE = 16
SCALE = 2              # 16x16 -> the 32x32 the menu draws

# Source index -> icons.pal index. The badge sheet uses 0 transparent, 15 for the
# outline, 1/2/3 for the shield from highlight to shadow, and 4 for the flame.
SHARED = {
    0:  0,   # transparent
    15: 1,   # outline; the stock icons use this soft dark rather than pure black
    1:  15,  # highlight -> white
    2:  14,  # body
    3:  12,  # shadow
}
FLAME_INACTIVE = 11      # a grey, so frame 0 matches every other icon
FLAME_ACTIVE = 5         # 248 104 0, the orange already in the shared palette


def read_jasc(path):
    lines = path.read_text().split('\n')
    return [tuple(int(v) for v in ln.split()) for ln in lines[3:3 + 16]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    args = ap.parse_args()

    badges = Image.open(args.repo / BADGES)
    x0 = VOLCANO_INDEX * BADGE_SIZE
    badge = badges.crop((x0, 0, x0 + BADGE_SIZE, BADGE_SIZE))
    src = list(badge.getdata())

    unknown = set(src) - set(SHARED) - {4}
    if unknown:
        sys.exit('badge uses source indices %s that this mapping does not cover '
                 '- the sheet changed shape' % sorted(unknown))

    palette = read_jasc(args.repo / ICON_PAL)
    if len(palette) != 16:
        sys.exit('%s is not 16 colours' % ICON_PAL)

    out = Image.new('P', (BADGE_SIZE * SCALE, BADGE_SIZE * SCALE * 2), 0)
    flat = []
    for c in palette:
        flat += list(c)
    flat += [0, 0, 0] * (256 - len(palette))
    out.putpalette(flat)

    px = out.load()
    for frame, flame in enumerate((FLAME_INACTIVE, FLAME_ACTIVE)):
        oy = frame * BADGE_SIZE * SCALE
        for y in range(BADGE_SIZE):
            for x in range(BADGE_SIZE):
                v = src[y * BADGE_SIZE + x]
                idx = flame if v == 4 else SHARED[v]
                for dy in range(SCALE):
                    for dx in range(SCALE):
                        px[x * SCALE + dx, oy + y * SCALE + dy] = idx

    out.save(args.repo / OUT)
    print('wrote %s  (%dx%d, two frames)' % (OUT, out.size[0], out.size[1]))
    print('  flame is palette %d when inactive, %d when selected'
          % (FLAME_INACTIVE, FLAME_ACTIVE))
    return 0


if __name__ == '__main__':
    sys.exit(main())
