"""
WEATHER_PETALS: the art and the palette.

Blossom blowing across the flower dungeon. The effect itself is a copy of the
snow in src/field_weather_effect.c with a wider drift; this writes only what it
draws with.

TWO FRAMES, TWO COLOURS, not two sizes. The snow's two frames are a large flake
and a small one, picked 50/50 at spawn by StartSpriteAnim(sprite, rand & 1).
Petals inherit that coin flip and spend it on PINK and ORANGE instead, which is
exactly the pair the floor's flower beds are painted in - so what blows past is
made of the same blossoms that are growing underfoot.

The colours are Ever Grande palette 10's own, copied rather than invented, for
the reason the whirlpool established: a thing reads as belonging when it is made
of what it sits in.

THE PALETTE GOES IN PALTAG_WEATHER_2, NOT PALTAG_WEATHER. The latter holds
gFogPalette and is shared by rain, snow, ash, bubbles and the fog itself, so
recolouring it would repaint every weather in the game. PALTAG_WEATHER_2 is the
lazily-allocated second slot that LoadCustomWeatherSpritePalette fills, and
clouds and sandstorm already use exactly this route - see Petals_InitAll. It
also calls UpdateSpritePaletteWithWeather itself, so the fade comes free.

There is only ONE such slot, so petals cannot coexist with clouds or sandstorm.
A dungeon floor has one weather, so that never arises here.

Needs Pillow, so run it from Windows - see the README.
"""
import sys
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
GFX = REPO / 'graphics/weather'
OUT = REPO / 'tools/rogue/_out'

# Sprite is SPRITE_SHAPE(8x8) / SPRITE_SIZE(8x8), matching the snowflake's, so
# each frame is one 8x8 tile and the whole effect is two tiles of art.
W = H = 8

# Index 0 is transparent. 1-3 pink and 4-6 orange are ever_grande palette 10's
# own bloom ramps; 7 is its highlight. The rest is left black - a weather sprite
# palette is 16 entries whatever it uses.
PALETTE = [
    (0x00, 0x00, 0x00),   # 0  transparent
    (0xFF, 0xCD, 0xEE),   # 1  pink light
    (0xF6, 0x9C, 0xBD),   # 2  pink mid
    (0xD5, 0x52, 0x73),   # 3  pink dark
    (0xF6, 0xDE, 0x6A),   # 4  orange light
    (0xF6, 0xAC, 0x20),   # 5  orange mid
    (0xCD, 0x73, 0x08),   # 6  orange dark
    (0xFF, 0xFF, 0xEE),   # 7  highlight
] + [(0, 0, 0)] * 8

_ = 0
# A petal, not a disc. Asymmetric on purpose: it is drawn as though seen at an
# angle, wide at one end and pinched at the other, so the sine drift reads as
# the petal turning over rather than as a dot sliding sideways.
#
# Kept to five rows of a possible eight. At 8x8 the sprite is already at the
# limit of what reads as a petal at all, and filling the tile would make it a
# blob; the empty rows also keep the two frames from looking like one shape
# flickering between two colours.
# DRAWN AT THE LIGHT END OF THE RAMP, and that is not a style choice. These
# blow over a floor already carpeted in pink and orange blooms, so a petal in
# the bed's own mid tones disappears into it - the first version did exactly
# that and the preview showed flecks that could not be picked out from the
# flowers underneath. Vanilla's snow gets its separation for free by being white
# over dark ground; petals have to be given it. So the body is the highlight and
# the lightest ramp entry, with one mid tone for form and the dark one dropped
# entirely.
SHAPE = [
    [_, _, _, 7, 1, _, _, _],
    [_, _, 7, 1, 1, _, _, _],
    [_, _, 1, 1, 2, 2, _, _],
    [_, _, _, 1, 2, _, _, _],
    [_, _, _, _, 2, _, _, _],
    [_, _, _, _, _, _, _, _],
    [_, _, _, _, _, _, _, _],
    [_, _, _, _, _, _, _, _],
]

# The orange one is the same petal mirrored and shifted, not merely recoloured.
# Two frames that differ only in hue read as one object changing colour when
# they pass close together; mirroring makes them two petals.
PINK_TO_ORANGE = {0: 0, 1: 4, 2: 5, 3: 6, 7: 7}


def frame(which):
    px = [[0] * W for _ in range(H)]
    for y, row in enumerate(SHAPE):
        for x, v in enumerate(row):
            if not v:
                continue
            if which == 0:
                px[y][x] = v
            else:
                # Mirrored horizontally and dropped one row, so the pair does
                # not sit in the same place on screen when both are on it.
                if y + 1 < H:
                    px[y + 1][W - 1 - x] = PINK_TO_ORANGE[v]
    return px


def save(which, path):
    img = Image.new('P', (W, H))
    flat = []
    for c in PALETTE:
        flat.extend(c)
    img.putpalette(flat + [0] * (768 - len(flat)))
    for y, row in enumerate(frame(which)):
        for x, v in enumerate(row):
            img.putpixel((x, y), v)
    img.save(path, bits=4)
    return img


def write():
    GFX.mkdir(parents=True, exist_ok=True)
    save(0, GFX / 'petal0.png')
    save(1, GFX / 'petal1.png')

    lines = ['JASC-PAL', '0100', '16']
    lines += [f'{r} {g} {b}' for r, g, b in PALETTE]
    # newline='\n' or Windows writes CRLF into a repo file.
    (GFX / 'petals.pal').write_text('\n'.join(lines) + '\n', newline='\n')
    print('wrote graphics/weather/petal0.png, petal1.png, petals.pal')


def preview():
    """Both petals big, and a scatter of them over the flower floor at game
    scale - a weather sprite is only ever seen small and moving, so judging it
    zoomed alone says nothing about whether it reads."""
    import tileset_atlas as ta
    from tileset_resolve import TilesetResolver

    OUT.mkdir(parents=True, exist_ok=True)
    R = TilesetResolver(REPO)
    pair = ta.TilesetPair(ta.Tileset(R.resolve('gTileset_General')),
                          ta.Tileset(R.resolve('gTileset_EverGrande')))
    # Index 0 is transparent in a sprite, so the preview has to make it so too.
    # Left opaque, every petal renders as a black box over the floor and the
    # thing being judged is invisible - which is what the first run of this did.
    petals = []
    for i in (0, 1):
        p = save(i, OUT / f'_petal{i}.png').convert('RGBA')
        grid = frame(i)
        p.putalpha(Image.frombytes(
            'L', (W, H),
            bytes(0 if grid[y][x] == 0 else 255
                  for y in range(H) for x in range(W))))
        petals.append(p)

    big = Image.new('RGB', (W * 12 * 2, H * 12), (26, 26, 32))
    for i, p in enumerate(petals):
        big.paste(p.convert('RGB').resize((W * 12,) * 2, Image.NEAREST), (i * W * 12, 0))

    # 10x7 blocks of real floor with petals scattered over it, at 2x.
    bw, bh, s = 10, 7, 2
    field = Image.new('RGBA', (16 * bw, 16 * bh))
    for gy in range(bh):
        for gx in range(bw):
            mid = (0x2B9 if (gx + gy) % 3 == 0 else 0x2A9) + ((gy - gx) % 8)
            field.paste(ta.render_metatile(pair, mid).convert('RGBA'),
                        (gx * 16, gy * 16))
    spots = [(9, 4), (34, 21), (61, 9), (88, 40), (117, 17), (140, 63),
             (23, 71), (52, 52), (96, 88), (131, 95), (70, 31), (13, 47)]
    for n, (x, y) in enumerate(spots):
        field.alpha_composite(petals[n & 1], (x, y))
    scaled = field.convert('RGB').resize((16 * bw * s, 16 * bh * s), Image.NEAREST)

    img = Image.new('RGB', (max(big.width, scaled.width),
                            big.height + scaled.height + 8), (26, 26, 32))
    img.paste(big, (0, 0))
    img.paste(scaled, (0, big.height + 8))
    path = OUT / 'petal_weather.png'
    img.save(path)
    print(f'wrote {path}')


def main():
    if '--write' in sys.argv:
        write()
    if '--preview' in sys.argv:
        preview()
    if len(sys.argv) == 1:
        print(__doc__)


if __name__ == '__main__':
    main()
