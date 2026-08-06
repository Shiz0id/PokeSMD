"""
Compose the three dungeon diver overworld sprites and their palettes.

WHY THIS IS NOT A SWIMMER RECOLOUR. The seafloor theme dresses its trainers as
SWIMMER_M / SWIMMER_F, who are drawn treading the surface in swimwear. The
obvious fix - recolour the swimmers - does not work, because an underwater
sprite is a different SIZE and a different DRAWING CONVENTION:

    swimmers, Juan          16x32, 9 frames, oam 16x32, full walk cycle
    BRENDAN/MAY_UNDERWATER  32x32, 3 frames, oam 32x32, NO walk cycle

So the base is the player's own underwater sprite. Four measurements shaped
everything below, and three of them contradicted the obvious guess.

1. THREE FRAMES, NOT NINE. sPicTable_BrendanUnderwater has nine entries but
   names only frames 0, 1 and 2 - south, north, west - each repeated into the
   walk slots. The player does not animate underwater, so neither do these.
   (Both vanilla sheets carry a fourth frame no pic table references. Not
   copied.)

2. THERE IS NO SKIN. The player's sprite uses seven of sixteen palette indices
   and none of the skin ramp. The figure is a sealed suit: no face, no hair, no
   clothing. Only SUIT COLOUR and HELMET SILHOUETTE can tell two divers apart.

3. THE SPRITE IS THREE-QUARTERS BLACK, AND A NAIVE RECOLOUR FAILS. Censused
   over the three live frames:

       black (index 15)   58.0%      near-black navy (index 8)   19.7%
       everything else    22.3%

   The first attempt remapped only that 22.3% and produced three identical
   black bodies in differently coloured hats. The fix is that black is not one
   thing: 67% of it is INTERIOR FILL and only 33% is outline. Recolouring the
   interior while leaving the boundary black puts ~59% of the figure under each
   diver's own colour instead of 22%, which is the difference between a diver
   and a palette swap of the player.

4. THE SEAFLOOR IS BRIGHT PURPLE. Checked against shipped data, not guessed -
   vanilla LAYOUT_UNDERWATER_ROUTE126 renders the same lilac this theme paints.
   That is why the female diver is WARM RED rather than the magenta first
   chosen to match her surface counterpart: magenta sits a few degrees from the
   floor hue and sinks into it. Against a light purple floor the readable axes
   are value and warmth, so the three read as mid-cyan, warm red, and near
   white - and the player stays the near-black they always were.

TWO PALETTES, NOT ONE OR THREE. PrepareFloor calls PrepareArenaFloor and
RETURNS before PlaceTrainers, so a boss floor holds no ordinary trainers: Juan
is never on screen beside the other two. The male and female divers DO share
floors, so they share one palette and must fit in it together - six ramp steps
each, plus black and transparent, is exactly sixteen. Juan, sharing with
nobody, takes his own and spends the room on the silver-and-magenta that is the
only thing of his that survives having no hair.

Run:  python make_diver_sprites.py [--repo PATH] [--preview]
Needs Pillow, so here it runs from Windows over UNC. Writes checked-in files:
three PNGs under graphics/object_events/pics/people/rogue/ and two .pal files
under graphics/object_events/palettes/.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))


def c(r5, g5, b5):
    """5-bit channels, because that is what the GBA stores. The 8-bit values in
    a .pal file are only a spelling of these, so choosing them here means the
    numbers reaching the hardware are the ones intended."""
    return tuple(round(v * 255 / 31) for v in (r5, g5, b5))


# Copied verbatim from the vanilla underwater palette. Index 0 is the
# transparent slot so its colour never draws, but keeping it identical means
# these sheets diff cleanly against the sprites they came from.
TRANSPARENT = (115, 197, 164)
BLACK = (0, 0, 0)

# A ramp is six steps: lightest -> body. BODY is the interior fill that used to
# be black and is the single largest area of the sprite, so it carries most of
# the identity; LIGHT is the helmet crown, the smallest and brightest.
#                LIGHT        MIDLIGHT     MID          DARK        DEEP        BODY
# A blue ramp was tried for the male diver and dropped. Two things were wrong
# with it: against a lilac seafloor a cool mid-blue separates less than either
# of these, and its lightest step sat within a step or two of GLASS, so his
# visor - the one feature the suit exists to frame - disappeared into his own
# helmet at game scale. Silver has neither problem.
RAMP_SILVER = [c(31, 31, 31), c(26, 27, 29), c(20, 21, 24), c(14, 15, 18), c(9, 10, 13), c(6, 6, 9)]
RAMP_RUST = [c(31, 23, 18), c(28, 14, 10), c(22, 8, 6), c(16, 5, 4), c(11, 3, 3), c(7, 2, 2)]
# Juan's trim. Magenta is a poor SUIT colour against a purple floor but a fine
# ACCENT on a white one, and these are his canonical overworld colours.
TRIM_J = [c(26, 14, 20), c(20, 8, 14), c(12, 4, 9)]

# Visor glass, in TWO steps, because the visor is translucent rather than
# opaque. A solid band reads as a blindfold: it covers the face and replaces
# it. Painting the lit pixels bright and the shadowed ones dim keeps whatever
# vanilla drew under the band showing THROUGH it, so the eye sockets survive as
# dark slots behind the glass and the diver is looking at you rather than
# wearing a stripe.
#
# Both are the same entries at the same indices in both palettes, so one pair
# of constants paints all three divers and the palettes stay comparable.
GLASS = c(24, 31, 31)
GLASS_DARK = c(11, 19, 22)
IDX_GLASS = 13
IDX_GLASS_DARK = 14

# The ramp steps that count as LIT under the visor. Everything else under it is
# in shadow and bleeds through dim. Read as source indices, so this stays true
# regardless of which diver's ramp is being applied.
VISOR_LIT = (9, 5)

# The male and female divers share floors, so they share a palette and have to
# fit in one together. Six steps each plus transparent and black is fourteen,
# and the glass takes one of the two that were spare.
PAL_DIVER = [TRANSPARENT] + RAMP_SILVER + RAMP_RUST + [GLASS, GLASS_DARK, BLACK]
assert len(PAL_DIVER) == 16, len(PAL_DIVER)
assert PAL_DIVER[IDX_GLASS] == GLASS
assert PAL_DIVER[IDX_GLASS_DARK] == GLASS_DARK
IDX_M = list(range(1, 7))
IDX_F = list(range(7, 13))

# Juan shares with nobody, so his has room to spare. The unused tail is black
# rather than left undefined: an entry that is visibly wrong is easier to spot
# in an atlas than one that happens to look plausible.
# NOTE: Juan now shares the male diver's silver, because silver became the
# rank-and-file suit. His TRIM is therefore the ONLY thing separating him from
# an ordinary trainer, and it is still unpainted - see TRIM_J. This does not
# break anything in game, since a boss floor holds no ordinary trainers and the
# two are never on screen together, but it does mean he currently reads as
# diver number three rather than as a Gym Leader.
PAL_JUAN = ([TRANSPARENT] + RAMP_SILVER + TRIM_J + [BLACK] * 3 +
            [GLASS, GLASS_DARK, BLACK])
assert len(PAL_JUAN) == 16, len(PAL_JUAN)
assert PAL_JUAN[IDX_GLASS] == GLASS
assert PAL_JUAN[IDX_GLASS_DARK] == GLASS_DARK
IDX_J = list(range(1, 7))

# The player's suit ramp brightest to darkest, read off the sheet rather than
# assumed - these are the only non-black indices either vanilla sheet uses.
# Interior black is appended as the sixth step; see point 3 in the header.
PLAYER_RAMP = [9, 5, 6, 7, 8]

DIVERS = {
    'rogue_diver_m':    ('brendan/underwater.png', IDX_M, 'diver'),
    'rogue_diver_f':    ('may/underwater.png',     IDX_F, 'diver'),
    'rogue_diver_juan': ('brendan/underwater.png', IDX_J, 'juan'),
}

PALETTES = {'diver': PAL_DIVER, 'juan': PAL_JUAN}

FRAMES = 3
FRAME_W = FRAME_H = 32

# THE SURF SPOT, AND WHY IT GOES.
#
# The vanilla underwater figure is a diver on top of a large black flared mass.
# Censused per row, that mass is a separable thing rather than part of the body:
# every frame ends in rows that are PURE black with zero coloured pixels
# (brendan frame 0 is solid black from y=23 to y=26), and the rows above it run
# two-to-one black. It reads as a shadow blob, and it is what makes the sprite
# look like a lump resting ON the seafloor rather than a person swimming above
# it.
#
# A FLAT CUT IS NOT ENOUGH, and this is the part that was got wrong first.
# Removing the blob with a horizontal slice at 21 leaves a shape that is WIDEST
# AT THE BOTTOM - a tent, or a poncho. A figure seen from above is widest at
# the shoulders and narrows to trailing legs, so the flare has to be tapered
# away rather than chopped off.
#
# So: cut lower, at 23, and from row 14 down trim one pixel off each side of
# every row per row of depth. What survives is a helmet, shoulders, two arm
# nubs and a short tapering body. Steeper rates (1.6, 1.8) and higher starts
# round it into a bean and lose the arms; flatter ones keep the tent.
CUT_ROW = 23
TAPER_START = 14
TAPER_RATE = 1.0

# THE VISOR, and why it lands where it does.
#
# Not invented placement - read out of the base sprite. Censused row by row,
# the head already carries a band of SOLID darkest-navy across its full width
# where a face would be: frame 0 at y=12 spanning x=12-19, frame 2 at y=11.
# Vanilla drew the face in shadow. Lighting that exact band turns shadow into
# glass without moving a single pixel of the silhouette.
#
# FRAME 1 GETS NOTHING. It is the diver seen from BEHIND - it has the same dark
# band at y=11, and painting it would put a visor on the back of the head. The
# frame that looks most like the others is the one that must not be treated
# like them.
# TWO ROWS, NOT ONE. A one-pixel band is invisible at 2x - it survives only on
# a contact sheet, which is exactly the mistake this project keeps making. A/B
# rendered at game scale: at one row the visor reads as a slightly lighter
# face, at two it reads as goggles.
#
# Sat one row lower than first placed, so it covers the eyes rather than the
# brow. Dropping a second row was tried and is wrong - by y=14 the helmet has
# ended and the band lands on the shoulders, which reads as a collar.
VISOR = {
    0: [(13, 12, 19), (14, 12, 19)],
    1: [],
    2: [(12, 10, 15), (13, 10, 14)],
}


NEIGHBOURS = ((1, 0), (-1, 0), (0, 1), (0, -1))

# SMOOTHING THE CROWN, so the divers stop reading as the player.
#
# The thing that gave them away was never the colour - it was the hair. Both
# bases carry the player's own headgear as a protrusion above the helmet dome,
# and censusing the top rows as contiguous RUNS shows it is the same defect
# twice in two different shapes:
#
#   brendan  y=6 [(18,18)]                 a one-pixel cap crest
#   may      y=5 [(12,13), (18,19)]        two disconnected hair prongs
#
# May's are why the female diver kept reading as having ears. Both vanish under
# one rule rather than two hand-authored pixel lists: scanning down from the
# top, delete every row until the first that is a SINGLE contiguous run at
# least this wide - which is the row the helmet dome actually begins on. It
# lands on y=7/8 for brendan and y=6/7 for may, in every frame, without either
# being named.
#
# The re-outline pass below then draws a fresh border over the new top edge, so
# what is left is a smooth dome and not a sliced one.
CROWN_MIN_WIDTH = 6


def runs_in(row, skip=(0,)):
    """Contiguous runs of pixels not in `skip`, as (start, end) pairs."""
    xs = [x for x in range(FRAME_W) if row[x] not in skip]
    if not xs:
        return []
    out, s, p = [], xs[0], xs[0]
    for x in xs[1:]:
        if x == p + 1:
            p = x
        else:
            out.append((s, p))
            s = p = x
    out.append((s, p))
    return out


def build_sheet(repo, base, ramp):
    src = Image.open(repo / 'graphics/object_events/pics/people' / base)
    spx = list(src.get_flattened_data())
    sw = src.width

    # Per-frame grids of the ORIGINAL palette indices, with the surf spot
    # already removed. Everything downstream works off these rather than off
    # the source, so the re-outline below sees the cut shape and not the one
    # vanilla drew.
    grids = []
    for f in range(FRAMES):
        g = [[0] * FRAME_W for _ in range(FRAME_H)]
        for y in range(CUT_ROW):
            for x in range(FRAME_W):
                g[y][x] = spx[y * sw + f * FRAME_W + x]
        # Smooth the crown: drop everything above the first row whose COLOURED
        # pixels are one contiguous run of at least CROWN_MIN_WIDTH.
        #
        # Coloured, not merely non-transparent - and that distinction is the
        # whole rule. Testing non-transparent leaves May's crown in place,
        # because her hair survives as two coloured lobes with a BLACK PARTING
        # between them: the row looks contiguous while the head does not, and
        # the re-outline pass below then draws a border around each lobe and
        # turns them into ears. Skipping black finds the row where the dome is
        # actually solid.
        for y in range(FRAME_H):
            r = runs_in(g[y], skip=(0, 15))
            if len(r) == 1 and r[0][1] - r[0][0] + 1 >= CROWN_MIN_WIDTH:
                break
            g[y] = [0] * FRAME_W

        # Taper the flare. Trimming from the row's own span rather than from a
        # fixed centre keeps the west-facing frame, which is not symmetrical,
        # from being shaved off-centre.
        for y in range(TAPER_START, FRAME_H):
            xs = [x for x in range(FRAME_W) if g[y][x] != 0]
            k = int(round((y - TAPER_START) * TAPER_RATE))
            if not xs or not k:
                continue
            for x in xs[:k] + xs[max(len(xs) - k, k):]:
                g[y][x] = 0

        grids.append(g)

    # Re-outline the cut. The sides and top already carry vanilla's black
    # border, but the new bottom edge is a raw horizontal slice through the
    # body - so any transparent pixel touching a COLOURED one becomes black.
    # Testing against coloured rather than against any-non-transparent is what
    # stops this thickening the border that is already there.
    for g in grids:
        add = []
        for y in range(FRAME_H):
            for x in range(FRAME_W):
                if g[y][x] != 0:
                    continue
                for dx, dy in NEIGHBOURS:
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < FRAME_W and 0 <= ny < FRAME_H:
                        if g[ny][nx] not in (0, 15):
                            add.append((x, y))
                            break
        for x, y in add:
            g[y][x] = 15

    # player index -> diver index, for everything except black
    m = {0: 0}
    for src_i, dst in zip(PLAYER_RAMP, ramp[:5]):
        m[src_i] = dst
    body = ramp[5]

    def is_visor(f, x, y):
        for vy, x0, x1 in VISOR[f]:
            if y == vy and x0 <= x <= x1:
                return True
        return False

    flat = []
    for y in range(FRAME_H):
        for f in range(FRAMES):
            g = grids[f]
            for x in range(FRAME_W):
                i = g[y][x]
                # Interior only: a visor that overwrote the outline would eat
                # the silhouette's left and right edges at that row. And the
                # SOURCE index decides bright or dim, so the shading vanilla
                # drew under the band is what shows through the glass.
                if i not in (0, 15) and is_visor(f, x, y):
                    flat.append(IDX_GLASS if i in VISOR_LIT
                                else IDX_GLASS_DARK)
                elif i == 15:
                    # Outline stays black; interior fill becomes the diver's
                    # own darkest colour. A black pixel is outline when it
                    # touches transparency in any of the four directions -
                    # which keeps the silhouette crisp against a light floor
                    # while freeing the ~39% of the figure underneath it. That
                    # 39% is what makes these divers rather than the player in
                    # a different hat; see point 3 in the header.
                    edge = any(
                        g[y + dy][x + dx] == 0
                        for dx, dy in NEIGHBOURS
                        if 0 <= x + dx < FRAME_W and 0 <= y + dy < FRAME_H)
                    flat.append(15 if edge else body)
                elif i in m:
                    flat.append(m[i])
                else:
                    raise SystemExit(
                        f'{base} frame {f} uses palette index {i}, which the '
                        f'player ramp does not cover. The base sheet is not '
                        f'what this script assumes it is.')

    out = Image.new('P', (FRAME_W * FRAMES, FRAME_H), 0)
    out.putdata(flat)
    return out


def with_palette(sheet, palette):
    flat = []
    for rgb in palette:
        flat.extend(rgb)
    sheet.putpalette(flat)
    return sheet


def write_jasc(path, palette):
    lines = ['JASC-PAL', '0100', '16']
    for r, g, b in palette:
        lines.append(f'{r} {g} {b}')
    # newline='\n' because this runs from Windows and the repo must not gain
    # CRLF. gbagfx does not care; git and everything else here does.
    path.write_text('\n'.join(lines) + '\n', newline='\n')


def build_all(repo):
    out = {}
    for name, (base, ramp, pal) in DIVERS.items():
        out[name] = (with_palette(build_sheet(repo, base, ramp),
                                  PALETTES[pal]), pal)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=str(HERE.parents[1]))
    ap.add_argument('--preview', action='store_true',
                    help='render a comparison sheet instead of writing assets')
    ap.add_argument('--out', default=None)
    args = ap.parse_args()

    repo = Path(args.repo)
    sheets = build_all(repo)

    if args.preview:
        import diver_preview
        diver_preview.render(repo, sheets, PALETTES,
                             Path(args.out or (HERE / '_out/divers.png')))
        return

    pics = repo / 'graphics/object_events/pics/people/rogue'
    pics.mkdir(parents=True, exist_ok=True)
    for name, (im, _) in sheets.items():
        im.save(pics / f'{name}.png')
        print('wrote', pics / f'{name}.png')

    pals = repo / 'graphics/object_events/palettes'
    write_jasc(pals / 'rogue_diver.pal', PAL_DIVER)
    write_jasc(pals / 'rogue_diver_juan.pal', PAL_JUAN)
    print('wrote', pals / 'rogue_diver.pal', 'and rogue_diver_juan.pal')


if __name__ == '__main__':
    main()
