"""Lift the party-menu egg icon out of graphics/pokemon and emit it as an
object event sprite, for the floor event that leaves an egg lying on the ground.

WHY THIS EXISTS. The egg event used to be an NPC handing one over, which is not
what it wants to be - it wants to be a thing you FIND. That needs an overworld
egg sprite, and the engine has none: there is no OBJ_EVENT_GFX for an egg, and
OBJ_EVENT_MON + SPECIES_EGG is not a route either, because SPECIES_EGG carries
no .overworldData (see src/data/pokemon/species_info.h) and the follower path
would dereference nothing. The Team Aqua asset repo has no egg overworld sprite
either - it was searched, and every "egg" hit in it is an Exeggcute.

WHICH ART, AND WHY IT NEEDS NO RESAMPLING. graphics/pokemon/egg/icon.png is the
party-menu icon: a 32x32 canvas whose egg is only 12x15 of actual ink. That is
the one piece of egg art in the tree already drawn at overworld scale - the
front pic is 64x64 and hatch.png's frames are ~26 wide, and both would have to
be downscaled, which on a 15-pixel-tall subject destroys the outline. The icon
is a STRAIGHT PIXEL COPY into a 16x16 sprite: no resampling, no colour loss, no
new art. It is also the egg the player already recognises from the party menu.

FRAME 0, not frame 1. The icon is two frames of a bob - upright, then squashed -
and a floor prop is inanimate, so the upright one is the one that reads as an
egg sitting in the dirt rather than as an egg mid-wobble.

THE PALETTE IS READ FROM pal1.pal, NOT FROM THE PNG. A mon icon's colours do
not come from its own file: the engine loads gMonIconPalettes[iconPalIndex],
and SPECIES_EGG sets .iconPalIndex = 1, so pal1.pal is what the game actually
draws the egg in. The two happen to agree today; this asserts that rather than
assuming it, because if they ever diverge the icon in the party menu and the
egg on the floor would be different colours and nothing else would say so.

Usage:  python3 tools/rogue/make_egg_sprite.py [--repo PATH]
Needs Pillow.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

# The engine treats palette index 0 as transparent for object event sprites, and
# every vanilla overworld palette uses this colour for it. Matching it is what
# makes the sprite composite over whatever floor it is lying on. Same constant
# as make_grave_sprite.py, and for the same reason.
TRANSPARENT = (115, 197, 164)

ICON = 'graphics/pokemon/egg/icon.png'
ICON_PAL = 'graphics/pokemon/icon_palettes/pal1.pal'   # SPECIES_EGG iconPalIndex
FRAME = 0
FRAME_SIZE = 32
SPRITE = 16

OUT_PNG = 'graphics/object_events/pics/misc/rogue_egg.png'
OUT_PAL = 'graphics/object_events/palettes/rogue_egg.pal'


def read_jasc(path):
    lines = path.read_text(errors='replace').split('\n')
    if lines[0].strip() != 'JASC-PAL':
        sys.exit('%s is not a JASC palette' % path)
    count = int(lines[2])
    out = []
    for line in lines[3:3 + count]:
        r, g, b = (int(v) for v in line.split())
        out.append((r, g, b))
    return out


def write_jasc(path, colors):
    body = ['JASC-PAL', '0100', '16']
    for i in range(16):
        r, g, b = colors[i] if i < len(colors) else (0, 0, 0)
        body.append('%d %d %d' % (r, g, b))
    path.write_text('\n'.join(body) + '\n', newline='\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    args = ap.parse_args()
    repo = args.repo

    icon = Image.open(repo / ICON)
    if icon.mode != 'P':
        sys.exit('%s is not an indexed image' % ICON)

    engine_pal = read_jasc(repo / ICON_PAL)
    flat = icon.getpalette()
    file_pal = [tuple(flat[i * 3:i * 3 + 3]) for i in range(16)]
    if file_pal != engine_pal:
        sys.exit('%s and %s DISAGREE. The engine draws the icon in the second '
                 'one, so the sprite must be built from that - but a mismatch '
                 'means the two eggs would differ on screen. Resolve it before '
                 'regenerating.' % (ICON, ICON_PAL))

    px = icon.load()
    top = FRAME * FRAME_SIZE

    # The 32x32 icon frame is mostly empty. Find what is actually drawn.
    cells = [(x, y) for y in range(FRAME_SIZE) for x in range(FRAME_SIZE)
             if px[x, top + y] != 0]
    if not cells:
        sys.exit('frame %d of %s is empty' % (FRAME, ICON))
    x0 = min(c[0] for c in cells)
    x1 = max(c[0] for c in cells)
    y0 = min(c[1] for c in cells)
    y1 = max(c[1] for c in cells)
    w, h = x1 - x0 + 1, y1 - y0 + 1
    if w > SPRITE or h > SPRITE:
        sys.exit('the egg is %dx%d, past the %dx%d a 16x16 sprite has - it '
                 'would have to be resampled, which this tool deliberately '
                 'does not do' % (w, h, SPRITE, SPRITE))

    # Centred across, and sitting on the BOTTOM ROW. A 16x16 object event is
    # drawn over its tile the way the item ball is, so bottom-aligned is what
    # puts the egg on the ground rather than hovering above it.
    ox = (SPRITE - w) // 2
    oy = SPRITE - h

    # Index 0 is transparency; the rest are the icon's own colours, kept in
    # SOURCE ORDER so the file is stable across runs and the diff is reviewable.
    used = sorted(set(px[x, top + y] for x, y in cells))
    colors = [TRANSPARENT] + [engine_pal[i] for i in used]
    if len(colors) > 16:
        sys.exit('egg uses %d colours, past the 16 a 4bpp sprite has'
                 % len(colors))
    remap = {src: i + 1 for i, src in enumerate(used)}

    img = Image.new('P', (SPRITE, SPRITE), 0)
    palette = []
    for c in colors:
        palette += list(c)
    palette += [0, 0, 0] * (256 - len(colors))
    img.putpalette(palette)
    out = img.load()
    for x, y in cells:
        out[ox + x - x0, oy + y - y0] = remap[px[x, top + y]]

    png = repo / OUT_PNG
    pal = repo / OUT_PAL
    png.parent.mkdir(parents=True, exist_ok=True)
    pal.parent.mkdir(parents=True, exist_ok=True)
    img.save(png)
    write_jasc(pal, colors)

    print('%s  %dx%d egg from %s frame %d, %d colours'
          % (OUT_PNG, w, h, ICON, FRAME, len(colors)))
    print('%s  written from %s' % (OUT_PAL, ICON_PAL))


if __name__ == '__main__':
    main()
