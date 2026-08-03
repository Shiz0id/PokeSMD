"""
The whirlpool, as a shape rather than as pixels.

Two dungeon themes descend through one - the ocean and the seafloor - and they
are on different secondary tilesets, so the metatile has to be appended twice.
The SHAPE lives here so the two cannot drift: the exit is the one thing on a
floor the player is hunting for, and two dungeons whose exits looked different
would be teaching them two things instead of one.

The colours do not live here, because they must not be shared. The vortex reads
as water only if it is drawn in the palette of the water it sits in - the sea's
own blues over the ocean, the seafloor's over the underwater floor - so each
caller passes a Roles giving the palette index for each part of the spiral.
That is the same rule as the woods stairs: draw inside an existing palette.

Index 0 is transparent wherever this is used as a top layer, which is what lets
the floor underneath show through outside the disc and keeps the whirlpool IN
the water rather than on top of it.
"""
import math
from collections import namedtuple

# Palette indices, darkest part of the throat first. Every field is an index
# into whichever palette the metatile entry names, never a colour.
Roles = namedtuple('Roles', 'hole_deep hole_mid trough trough_lit rim '
                            'foam_fringe foam foam_hot')

FRAMES = 4
ARMS = 2        # two-fold symmetric, so a quarter turn per frame closes the loop
TWIST = 1.15    # radians of sweep per pixel of radius
R_OUT = 7.6     # disc edge, inside the 16x16 block
R_HOLE = 1.7


def pixels(frame, roles, frames=FRAMES):
    """16x16 of palette indices for one frame. 0 means transparent."""
    px = [[0] * 16 for _ in range(16)]
    phase = -2.0 * math.pi * frame / (frames * ARMS)
    for y in range(16):
        for x in range(16):
            dx, dy = x - 7.5, y - 7.5
            r = math.hypot(dx, dy)
            if r > R_OUT:
                continue
            if r < R_HOLE:
                # The throat: darkest dead centre, so it reads as a hole being
                # looked down rather than a flat disc.
                px[y][x] = roles.hole_deep if r < R_HOLE - 0.6 else roles.hole_mid
                continue
            th = math.atan2(dy, dx)
            s = math.sin(ARMS * (th + TWIST * r) + ARMS * phase)
            edge = (R_OUT - r) / (R_OUT - R_HOLE)   # 1 at the throat, 0 at the rim
            # Foam is brightest where the water is pulled fastest - near the
            # throat - and gives out before the rim, so the vortex fades into
            # open water instead of ending on a hard bright ring.
            hot = 0.94 - 0.5 * edge
            if s > 0.72 and edge > 0.18:
                px[y][x] = roles.foam_hot if (s > hot and edge > 0.45) else roles.foam
            elif s > 0.30 and edge > 0.10:
                px[y][x] = roles.foam if edge > 0.75 else roles.foam_fringe
            elif s > -0.35:
                px[y][x] = roles.trough_lit if edge > 0.5 else roles.rim
            else:
                px[y][x] = roles.trough if edge > 0.35 else roles.rim
    return px


def write_frames(outdir, roles, palette, frames=FRAMES):
    """One indexed 16x16 PNG per frame. gbagfx turns each into four tiles, in
    TL, TR, BL, BR order - the order the metatile references them in.

    `palette` is only embedded so the file is viewable; the game takes its
    colours from the palette the metatile entry names.
    """
    from PIL import Image
    outdir.mkdir(parents=True, exist_ok=True)
    flat = []
    for r, g, b in palette:
        flat += [r, g, b]
    flat += [0] * (768 - len(flat))
    for f in range(frames):
        im = Image.new('P', (16, 16))
        im.putpalette(flat)
        im.putdata([v for row in pixels(f, roles, frames) for v in row])
        im.save(outdir / f'{f}.png', bits=4, optimize=False)
    return frames


def write_sheet_tiles(tiles_path, slots, roles):
    """Frame 0 into the reserved slots of a secondary tile sheet.

    The sheet will LOOK wrong opened in an editor when the roles index a palette
    the sheet does not carry. That is normal here and not a bug - only the
    metatile's palette field decides what the hardware draws.
    """
    from PIL import Image
    img = Image.open(tiles_path)
    if img.mode != 'P':
        raise SystemExit(f'{tiles_path} is not an indexed tile sheet')
    px = pixels(0, roles)
    dst = img.load()
    for k, slot in enumerate(slots):
        tx, ty = (slot % 16) * 8, (slot // 16) * 8
        ox, oy = (k % 2) * 8, (k // 2) * 8
        for y in range(8):
            for x in range(8):
                dst[tx + x, ty + y] = px[oy + y][ox + x]
    img.save(tiles_path, bits=4, optimize=False)
