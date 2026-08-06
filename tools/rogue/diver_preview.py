"""
Render the divers on the surface they will actually stand on.

A diver judged on a contact sheet is judged against nothing. These are dark
sprites on a dark seafloor, and the only question that matters - can you tell
the three of them apart, and can you tell any of them from the PLAYER, who is
also a diver and is on screen at the same time - is unanswerable until they are
side by side on UNDERWATER_METATILE_FLOOR at the size the GBA draws them.

Both scales are rendered on purpose. 4x is for seeing what a pixel is doing;
2x is the one to trust, because it is roughly what the player sees.
"""
from pathlib import Path

from PIL import Image, ImageDraw

import tileset_atlas as ta
from tileset_resolve import TilesetResolver

FLOOR_METATILE = 0x216      # UNDERWATER_METATILE_FLOOR, from rogue_dungeon.h
SEAWEED_METATILE = 0x281    # UNDERWATER_METATILE_SEAWEED


def floor_tile(repo, scale):
    R = TilesetResolver(repo)
    pair = ta.TilesetPair(ta.Tileset(R.resolve('gTileset_General')),
                          ta.Tileset(R.resolve('gTileset_Underwater')))
    return (ta.render_metatile(pair, FLOOR_METATILE, scale),
            ta.render_metatile(pair, SEAWEED_METATILE, scale))


def backdrop(repo, w, h, scale):
    floor, weed = floor_tile(repo, scale)
    t = 16 * scale
    img = Image.new('RGB', (w, h))
    for y in range(0, h, t):
        for x in range(0, w, t):
            # A little seaweed, because the divers will stand among it and a
            # sprite that reads fine on bare floor can vanish against a patch.
            img.paste(weed if (x // t + y // t) % 7 == 3 else floor, (x, y))
    return img


def strip(repo, sprites, scale, label_h=16, min_w=0):
    """sprites: list of (name, 32x32 RGBA frame)."""
    t = 16 * scale
    cell_w = 32 * scale + t
    w = max(len(sprites) * cell_w + t, min_w)
    h = 32 * scale + t * 2 + label_h
    img = backdrop(repo, w, h, scale)
    d = ImageDraw.Draw(img)

    for n, (name, frame) in enumerate(sprites):
        x = t + n * cell_w
        y = t
        big = frame.resize((32 * scale, 32 * scale), Image.NEAREST)
        img.paste(big, (x, y), big)
        d.text((x, y + 32 * scale + 3), name, fill=(255, 255, 255))
    return img


def frames_of(sheet, palette, count=3):
    """Split an indexed sheet into RGBA frames, index 0 transparent."""
    rgba = []
    px = list(sheet.get_flattened_data())
    w = sheet.width
    for f in range(count):
        im = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
        out = []
        for y in range(32):
            for x in range(32):
                i = px[y * w + f * 32 + x]
                out.append((0, 0, 0, 0) if i == 0 else palette[i] + (255,))
        im.putdata(out)
        rgba.append(im)
    return rgba


def player_frames(repo, which='brendan'):
    src = Image.open(repo / 'graphics/object_events/pics/people' / which /
                     'underwater.png')
    pal = src.getpalette()
    colors = [tuple(pal[i * 3:i * 3 + 3]) for i in range(16)]
    return frames_of(src, colors)


def render(repo, sheets, palettes, out):
    out.parent.mkdir(parents=True, exist_ok=True)

    order = ['rogue_diver_m', 'rogue_diver_f', 'rogue_diver_juan']
    diver = {k: frames_of(sheets[k][0], palettes[sheets[k][1]]) for k in order}
    brendan = player_frames(repo, 'brendan')
    may = player_frames(repo, 'may')

    # Row 1 and 2: every diver beside BOTH player sprites, facing south. This
    # is the confusion test - the player is a diver too, and dressing an NPC as
    # the player is the failure this whole exercise exists to avoid.
    faces = []
    for k in order:
        short = k.replace('rogue_diver_', '')
        for f, face in enumerate(('S', 'N', 'W')):
            faces.append((f'{short} {face}', diver[k][f]))

    # The widest row sets the page, and every other row is padded with the same
    # seafloor rather than with letterbox. A sprite judged against black is
    # judged against a background it will never stand on.
    width = strip(repo, faces, 3).width

    rows = []
    for scale in (4, 2):
        line = [('player M', brendan[0]), ('player F', may[0])]
        line += [(k.replace('rogue_diver_', 'diver '), diver[k][0])
                 for k in order]
        rows.append(strip(repo, line, scale, min_w=width))

    # Last row: all three facings, so a silhouette that only works head-on is
    # caught here.
    rows.append(strip(repo, faces, 3, min_w=width))

    w = max(r.width for r in rows)
    h = sum(r.height for r in rows) + 8 * len(rows)
    img = Image.new('RGB', (w, h), (16, 16, 20))
    y = 0
    for r in rows:
        img.paste(r, (0, y))
        y += r.height + 8
    img.save(out)
    print('wrote', out)
