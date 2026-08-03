"""
The flower dungeon's wade-through overlay: FLDEFFOBJ_ROGUE_FLOWERS.

MB_LONG_GRASS is what makes Ever Grande's flowers an encounter surface, and it
drags FLDEFF_LONG_GRASS along with it. That effect's art is a 16x16 four-frame
curtain of GREEN BLADES, and drawn over pink and yellow blooms it is wrong -
not broken, but it tells the player they are standing in grass. This draws the
same curtain out of stems and blossoms instead.

Only the graphic and the palette change. FLDEFF_LONG_GRASS stays the effect id,
because every ground-effect flag and the FieldEffectStop in
UpdateLongGrassFieldEffect key on it, and the OAM clip that hides the player's
lower half (SetObjectEventSpriteOamTableForLongGrass) is purely geometric and is
exactly right for wading into a flower field. The jungle keeps the blades.

WHY A NEW PALETTE IS UNAVOIDABLE. general_1.pal already carries the grass ramp
at 1-5 and, usefully, Ever Grande's own mint at 12-15 - but indices 6-11 are
blues, brown and tan. There is no pink, no orange and no yellow anywhere in it,
so the stems could have been borrowed and the blooms could not.

WHY THE PALETTE IS LOADED IN C RATHER THAN BY THE SCRIPT. A field effect's
palette comes from data/field_effect_scripts.s -
`field_eff_loadfadedpal_callnative gSpritePalette_GeneralFieldEffect1,
FldEff_LongGrass` - and a script is fixed per FLDEFF id while a SpriteTemplate
is const. So neither of the two places that would normally choose a palette can
choose per theme, and FldEff_LongGrass does it itself. That path has to call
UpdateSpritePaletteWithWeather afterwards, because `loadfadedpal` is what
applies the current weather fade and we are stepping around it.

DENSITY IS THE POINT, NOT PRETTINESS. This sprite's job is to be opaque enough
to read as something the player is standing IN. Vanilla's is essentially noise
- 60% one dark green, everything else scattered - with almost no transparent
pixels, and the ragged top row alternates mint against dark so the mass has an
edge without a hard line. Those proportions are copied; only the hue and the
blooms differ.

Generated rather than hand-placed, for the whirlpool's reason: four frames of
16x16 is a thousand pixels, and a stem is a function of x, height and lean.

--preview needs Pillow; so does --write, since it writes a PNG.
"""
import sys
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PIC = REPO / 'graphics/field_effects/pics/rogue_flowers.png'
PAL = REPO / 'graphics/field_effects/palettes/rogue_flowers.pal'
OUT = REPO / 'tools/rogue/_out'

W = H = 16
FRAMES = 4

# Index 0 is transparent for a sprite, so its colour never shows; it is given
# general_1's own so the file looks like its neighbours in an editor.
#
# 1-4 are gTileset_General's grass ramp, the same four the blade curtain uses,
# so the stems sit in the same green as the meadow. 5-6 are Ever Grande's mint,
# for the ragged top edge, where vanilla puts its lightest tone.
#
# 7-14 are the blooms, taken from ever_grande palettes 10 and 11 rather than
# invented, so the overlay is made of the same colours as the flowers under it -
# the whirlpool's rule that a thing must be made of what it sits in.
PALETTE = [
    (0x73, 0xBD, 0xEE),   # 0  transparent
    (0xC5, 0xFF, 0xA4),   # 1  leaf highlight
    (0x83, 0xC5, 0x62),   # 2  leaf mid
    (0x5A, 0xA4, 0x31),   # 3  leaf dark        - the bulk, as vanilla's is
    (0x41, 0x62, 0x10),   # 4  leaf deep shade  - the bottom band
    (0xA4, 0xD5, 0xC5),   # 5  mint pale        - the ragged top
    (0x73, 0xC5, 0xA4),   # 6  mint
    (0xFF, 0xCD, 0xEE),   # 7  pink light
    (0xF6, 0x9C, 0xBD),   # 8  pink mid
    (0xD5, 0x52, 0x73),   # 9  pink dark
    (0xF6, 0xDE, 0x6A),   # 10 yellow light
    (0xF6, 0xAC, 0x20),   # 11 orange mid
    (0xCD, 0x73, 0x08),   # 12 orange dark
    (0xBD, 0xBD, 0xFF),   # 13 periwinkle light
    (0x73, 0x73, 0xFF),   # 14 periwinkle mid
    (0xFF, 0xFF, 0xEE),   # 15 bloom highlight
]

CLEAR = 0
LEAF_HI, LEAF_MID, LEAF_DARK, LEAF_DEEP = 1, 2, 3, 4
MINT_PALE, MINT = 5, 6
BLOOMS = (
    (7, 8, 9),      # pink
    (10, 11, 12),   # orange and yellow
    (13, 14, 9),    # periwinkle, with pink as its shadow
)


# THE FOLIAGE IS VANILLA'S OWN CURTAIN, RECOLOURED.
#
# Two attempts at generating the mass from scratch failed the same way, and the
# second failure is the instructive one. Noise with the right PROPORTIONS still
# does not look like the vanilla sprite, because vanilla's light pixels are not
# scattered - they form blades, short diagonal strokes running up the tile. Get
# the histogram right and the texture is still wrong.
#
# Reusing the blades also settles two things that are hard to get right by eye
# and easy to get wrong: the mass is opaque enough to read as something the
# player is standing IN, and it TILES invisibly. The effect spawns one sprite
# per occupied tile, so several sit edge to edge whenever the player walks a run
# of flowers, and a curtain with any left or right edge structure would turn
# into a visible grid at exactly the moment it is being looked at.
#
# So only the hue moves, onto the flower metatiles' own leaf ramp - ever_grande
# palette 10 indices 1-4 rather than general_1's - so the overlay foliage is
# made of the same greens as the flowers under it. Vanilla's mint at index 13
# is the ragged top edge and stays mint.
BLADE_REMAP = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 13: 6}

# Blooms: x, which palette triple, and y of the centre. FOUR, not the seven the
# previous attempt had, and at four different heights. Seven in a 16x16 read as
# confetti - the tile has to stay mostly foliage with blossoms punctuating it,
# which is what the vanilla flower fields themselves look like.
#
# Positions are chosen off the LEFT and RIGHT edges as well: a bloom clipped by
# the tile boundary is duplicated against its other half when two of these sit
# side by side, which is the one way this could reintroduce a visible seam.
#
# PINK AND ORANGE ONLY, because that is the colourway the theme actually
# paints. Vanilla uses one per field and never mixes them, and the theme's
# patch layer is based at EVERGRANDE_METATILE_FLOWERS_PINK, so a periwinkle
# blossom in the overlay would be a flower the player is standing in that does
# not exist anywhere on the floor. The blue triple stays in the palette, ready
# for the yellow-and-blue set if a floor is ever based there.
FLOWERS = ((3, 0, 4), (8, 1, 9), (11, 0, 3), (6, 1, 13))


def lean(frame):
    """Sway. The blooms travel with the foliage, and the foliage is vanilla's,
    which already sways between its four frames - so this only has to move the
    blossoms by the same amount rather than invent a motion. One pixel is
    enough at this size; two reads as the flowers sliding off their stems."""
    return (0, 1, 1, -1)[frame]


def blades():
    """Vanilla's curtain, as index grids, one per frame."""
    src = Image.open(REPO / 'graphics/field_effects/pics/long_grass.png')
    if src.mode != 'P':
        raise SystemExit('long_grass.png is not paletted')
    px = src.load()
    out = []
    for f in range(FRAMES):
        grid = []
        for y in range(H):
            row = []
            for x in range(W):
                v = px[f * W + x, y]
                if v not in BLADE_REMAP:
                    raise SystemExit(f'blade index {v} has no remap - '
                                     f'long_grass.png has changed')
                row.append(BLADE_REMAP[v])
            grid.append(row)
        out.append(grid)
    return out


def draw_frame(frame, blade_grids):
    px = [row[:] for row in blade_grids[frame]]
    dx_sway = lean(frame)

    # Blooms over the foliage, so nothing is drawn on top of them.
    for bx, which, by in FLOWERS:
        light, mid, dark = BLOOMS[which]
        cx = bx + dx_sway
        # A five-pixel blossom: a lit centre, a mid ring, one dark pixel at the
        # bottom for a shadow. Bigger than this and it stops being a flower seen
        # among leaves and starts being a decal.
        for ox, oy, c in ((0, 0, light), (-1, 0, mid), (1, 0, mid),
                          (0, -1, mid), (0, 1, dark)):
            x, y = cx + ox, by + oy
            if 0 <= x < W and 0 <= y < H:
                px[y][x] = c
    return px


def build_sheet():
    img = Image.new('P', (W * FRAMES, H))
    flat = []
    for c in PALETTE:
        flat.extend(c)
    img.putpalette(flat + [0] * (768 - len(flat)))
    grids = blades()
    for f in range(FRAMES):
        for y, row in enumerate(draw_frame(f, grids)):
            for x, v in enumerate(row):
                img.putpixel((f * W + x, y), v)
    return img


def write():
    img = build_sheet()
    PIC.parent.mkdir(parents=True, exist_ok=True)
    img.save(PIC, bits=4)

    lines = ['JASC-PAL', '0100', '16']
    lines += [f'{r} {g} {b}' for r, g, b in PALETTE]
    PAL.parent.mkdir(parents=True, exist_ok=True)
    # newline='\n' or Windows writes CRLF into a repo file.
    PAL.write_text('\n'.join(lines) + '\n', newline='\n')
    print(f'wrote {PIC.relative_to(REPO)}  {img.size[0]}x{img.size[1]}')
    print(f'wrote {PAL.relative_to(REPO)}')


def preview():
    """Beside the curtain it replaces, and tiled.

    Compositing it OVER the flowers tells you nothing, which the first version
    of this got wrong: the sprite is opaque by design, so the flowers vanish
    underneath it and the render just shows the sprite again.

    Tiling is the check that matters instead. The effect spawns one sprite per
    occupied tile, so several sit edge to edge whenever the player walks a run
    of flowers, and anything with a strong left or right edge turns into a
    visible grid at exactly the moment it is being looked at. Vanilla's blades
    tile invisibly because they are noise.

    Rendered at 2x as well as 5x, because a seam that reads as texture when
    zoomed reads as a grid at game scale."""
    OUT.mkdir(parents=True, exist_ok=True)
    ours = build_sheet().convert('RGB')
    theirs = Image.open(REPO / 'graphics/field_effects/pics/long_grass.png').convert('RGB')

    pad, tiled_n = 8, 5
    rows = []
    for scale, label in ((5, 'zoomed'), (2, 'game scale')):
        sheet_w = W * FRAMES * scale
        band = Image.new('RGB', (max(sheet_w, W * tiled_n * scale),
                                 H * scale * 3 + pad * 2), (26, 26, 32))
        band.paste(theirs.resize((sheet_w, H * scale), Image.NEAREST), (0, 0))
        band.paste(ours.resize((sheet_w, H * scale), Image.NEAREST),
                   (0, H * scale + pad))
        # Frame 0, five copies edge to edge.
        one = ours.crop((0, 0, W, H))
        strip = Image.new('RGB', (W * tiled_n, H))
        for i in range(tiled_n):
            strip.paste(one, (i * W, 0))
        band.paste(strip.resize((W * tiled_n * scale, H * scale), Image.NEAREST),
                   (0, (H * scale + pad) * 2))
        rows.append(band)

    width = max(b.width for b in rows)
    img = Image.new('RGB', (width, sum(b.height for b in rows) + pad), (26, 26, 32))
    y = 0
    for b in rows:
        img.paste(b, (0, y))
        y += b.height + pad
    path = OUT / 'flower_fldeff.png'
    img.save(path)
    print(f'wrote {path}')
    print('  each block: vanilla blades / ours / ours tiled five wide')


def main():
    if '--write' in sys.argv:
        write()
    if '--preview' in sys.argv:
        preview()
    if len(sys.argv) == 1:
        print(__doc__)


if __name__ == '__main__':
    main()
