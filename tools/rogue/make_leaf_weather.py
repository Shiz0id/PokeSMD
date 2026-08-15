"""
WEATHER_LEAVES: the palette, and only the palette.

Falling leaves for the woods dungeon - the first ten floors, so this is most
players' first impression of the run. Calm on purpose.

THE ART IS VANILLA'S AND IS NOT COPIED. graphics/battle_anims/sprites/leaf.png
is a nine-frame 16x16 tumbling-leaf rotation, already in the ROM as
ANIM_TAG_LEAF (Razor Leaf, Magical Leaf, Absorb). field_weather_effect.c
INCGFXes that same path a second time as plain .4bpp - the battle anim keeps
its compressed .4bpp.smol copy - so the weather and the move share one PNG and
nothing had to be drawn. Verified host-side that a 16x144 sheet converts to
nine CONTIGUOUS 128-byte frames in OBJ 1D order (TL,TR,BL,BR), which is the
only reason SpriteFrameImage can index it by multiplication.

SO THE RECOLOUR LIVES ENTIRELY HERE, IN 32 BYTES. The tiles reference palette
indices 2-7; vanilla points those at a green ramp, and this points them
somewhere else. Nothing about the pixels changes.

WHY NOT GREEN. Rendered against a real woods floor - gTileset_General +
gTileset_Rustboro, the pair LAYOUT_ROGUE_DUNGEON_WOODS actually uses - vanilla's
own green ramp DISAPPEARS over tall grass and over the tree canopies, which
between them are most of what a woods floor is made of. It reads fine over open
grass and nowhere else. This is the same trap the petals hit and it is written
up in make_petal_weather.py: a weather sprite in its floor's own mid tones is
invisible, and the fix is to move off the floor's hue rather than to brighten.

WHY NOT THE COIN FLIP. The petals ship two colours picked 50/50 at spawn,
because Ever Grande's floor is pink AND orange and both halves have a bed to
belong to. Tried here and rejected on the render: the woods floor is uniformly
green, so a green/amber mix just makes half the leaves invisible half the time.
One ramp, nine frames.

WHY NOT THE TRUNK BROWNS. Also tried - sampled from the tree base metatiles, on
the reasoning that a fallen leaf belongs to the tree above it. Too dark: they
read as twigs, and the ramp's bottom end vanishes into the canopy shadow.

Needs Pillow, so run it from Windows - see the README.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
GFX = REPO / 'graphics/weather'
OUT = REPO / 'tools/rogue/_out'

# The vanilla sheet, referenced not copied. Only read here, for the preview.
SRC = REPO / 'graphics/battle_anims/sprites/leaf.png'

# Which palette indices the vanilla tiles actually use, light to dark. Index 0
# is transparent; 1 is unused by the art and 8-15 are an orange ramp the sheet
# carries for a variant and never draws with. Only 2-7 need an answer.
INK = [2, 3, 4, 5, 6, 7]

# Muted amber. Warm enough to separate from every green on the floor, dull
# enough not to read as embers - vanilla's own 8-12 ramp is right there and was
# the first thing tried, but at #FF6210 it looks like Fiery Path, which is the
# opposite of the note this dungeon is meant to open on.
RAMP = [
    (0xF6, 0xD6, 0x8C),   # 2  highlight, catching the light through the canopy
    (0xE6, 0xB4, 0x5A),   # 3
    (0xD5, 0x8C, 0x31),   # 4
    (0xB4, 0x6A, 0x20),   # 5
    (0x8C, 0x4A, 0x18),   # 6
    (0x63, 0x31, 0x10),   # 7  shadowed edge
]

# 16 entries whatever is used. Index 0 is the transparent one and its value is
# never drawn, but gbagfx writes it, so keep vanilla's there rather than black -
# a stray 0 that leaks is then the same colour the vanilla sheet would have shown.
PALETTE = ([(0x94, 0xC5, 0xF6), (0x00, 0x00, 0x00)]
           + RAMP
           + [(0, 0, 0)] * 8)


def write():
    GFX.mkdir(parents=True, exist_ok=True)
    lines = ['JASC-PAL', '0100', '16']
    lines += [f'{r} {g} {b}' for r, g, b in PALETTE]
    # newline='\n' or Windows writes CRLF into a repo file.
    (GFX / 'leaves.pal').write_text('\n'.join(lines) + '\n', newline='\n')
    print('wrote graphics/weather/leaves.pal')


def frames():
    """The nine vanilla frames under this palette, index 0 transparent."""
    from PIL import Image
    src = Image.open(SRC)
    px = src.load()
    out = []
    for f in range(9):
        im = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
        ip = im.load()
        for y in range(16):
            for x in range(16):
                v = px[x, f * 16 + y]
                if v:
                    ip[x, y] = PALETTE[v] + (255,)
        out.append(im)
    return out


def preview():
    """A scatter of leaves over a real woods floor at game scale. A weather
    sprite is only ever seen small and moving, so judging one zoomed and alone
    says nothing about whether it reads - see make_petal_weather.py."""
    import random
    from PIL import Image
    sys.path.insert(0, str(REPO / 'tools/rogue'))
    import tileset_atlas as ta
    from tileset_resolve import TilesetResolver

    OUT.mkdir(parents=True, exist_ok=True)
    R = TilesetResolver(REPO)
    pair = ta.TilesetPair(ta.Tileset(R.resolve('gTileset_General')),
                          ta.Tileset(R.resolve('gTileset_Rustboro')))

    GRASS, TALL, FLOWER = 0x001, 0x00D, 0x004
    TL, TR, BL, BR, BASE_L, BASE_R = 0x1D4, 0x1D5, 0x1DC, 0x1DD, 0x1E4, 0x1E5
    W, H, Z = 15, 10, 3
    scene = [[GRASS] * W for _ in range(H)]
    for y in range(4, 7):
        for x in range(1, 6):
            scene[y][x] = TALL
    for y in range(7, 10):
        for x in range(9, 14):
            scene[y][x] = TALL
    scene[2][3] = scene[8][2] = FLOWER
    for ox, oy in ((7, 1), (11, 2), (2, 0)):
        scene[oy][ox], scene[oy][ox + 1] = TL, TR
        scene[oy + 1][ox], scene[oy + 1][ox + 1] = BL, BR
        if oy + 2 < H:
            scene[oy + 2][ox], scene[oy + 2][ox + 1] = BASE_L, BASE_R

    field = Image.new('RGB', (W * 16 * Z, H * 16 * Z))
    for y in range(H):
        for x in range(W):
            field.paste(ta.render_metatile(pair, scene[y][x], Z),
                        (x * 16 * Z, y * 16 * Z))

    fr = frames()
    rng = random.Random(7)
    for _ in range(14):
        sx, sy = rng.randrange(0, W * 16 - 16), rng.randrange(0, H * 16 - 16)
        big = fr[rng.randrange(0, 9)].resize((16 * Z, 16 * Z), Image.NEAREST)
        field.paste(big, (sx * Z, sy * Z), big)

    path = OUT / 'leaf_weather.png'
    field.save(path)
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
