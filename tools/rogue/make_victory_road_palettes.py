"""
Build the Victory Road palette set, and preview it on real cave art first.

Victory Road runs on gTileset_General + gTileset_Cave in vanilla - the identical
pair to Granite Cave, which is already dungeon 2. So a plain theme entry would
be pixel-identical to a dungeon the player cleared sixty floors earlier, and the
whole job is finding a difference that is not a difference in geometry.

The difference is the palette. Every metatile in the cave wall table, both
stairs, and all seven of our composed slivers draw from palette 6 and ONLY
palette 6 - measured, not assumed - and palette 6 is the first SECONDARY slot
(NUM_PALS_IN_PRIMARY is 6). So a secondary tileset that shares Cave's tiles,
metatiles and attributes and ships its own palettes recolours one hundred
percent of what the generator paints, for the cost of one palette directory.

Vanilla does exactly this: gMetatiles_SecretBaseSecondary is shared by six
tilesets, and gTileset_MirageTower is 411/414 byte-identical to gTileset_Cave
with a sand-coloured ramp over it.

What it does NOT recolour is anything drawn from the primary: the cave's sand
pool region is 45% palette 5 and its decor 50% palette 3, both owned by
gTileset_General and shared with every other theme. Victory Road therefore runs
with no patch layer and no decor, which are optional per theme anyway.

  python make_victory_road_palettes.py --preview        # candidates, side by side
  python make_victory_road_palettes.py --write slate    # write the chosen one

Needs Pillow for --preview, so run that side from Windows over UNC. --write is
plain text and runs anywhere.
"""
import argparse
import colorsys
import struct
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SRC_PALETTES = REPO / 'data/tilesets/secondary/cave/palettes'


def out_dir(tone):
    return REPO / f'data/tilesets/secondary/rogue_victory_road_{tone}/palettes'

# The secondary window. LoadTilesetPalette hands a secondary tileset slots
# NUM_PALS_IN_PRIMARY..NUM_PALS_TOTAL-1, so only these files are ever loaded;
# the rest are copied verbatim to keep the array the 16 entries the
# declaration needs.
FIRST_SECONDARY_PAL = 6
LAST_SECONDARY_PAL = 12

# Index 0 of a tileset palette is the transparency marker and must survive
# untouched, and pure white is used as a highlight rather than as stone.
KEEP = {(255, 131, 123), (255, 255, 255)}

# One tone per Elite Four member, tinted to their type. Each is
# (hue degrees, saturation scale, lightness low, lightness high).
#
# The lightness band is a LINEAR remap, not a gamma. Gamma crushes one end of
# the ramp, and the ramp is the only thing separating a wall from the floor it
# meets - the cave's legibility comes from a continuous outline, not from
# floor/rock contrast, so the outline has to keep its spread. The band width
# (high - low) IS the contrast budget, and it is the number to look at when a
# tone is meant to be dark.
TONES = {
    # Sidney, Dark. Violet, full range.
    'sidney': (275.0, 0.55, 0.18, 0.78),
    # Phoebe, Ghost. Near-black, and the one with a legibility constraint:
    # 0.42 of spread kept so the outline still reads. Paired with fog, which
    # is why it can afford to be this dark.
    'phoebe': (250.0, 0.18, 0.10, 0.52),
    # Glacia, Ice. Pale and cold - ice reads bright, so the band sits high.
    'glacia': (198.0, 0.70, 0.30, 0.92),
    # Drake, Dragon. Muted red - but pulled to crimson rather than the orange
    # side, because at 2x an orange-leaning red is only a faded Granite Cave,
    # which is the one thing a new theme must not look like.
    'drake':  (358.0, 0.55, 0.14, 0.70),
}


def parse_pal(path):
    text = path.read_text(errors='replace')
    toks = [ln.split() for ln in text.replace('\r\n', '\n').split('\n')]
    nums = [t for t in toks if len(t) >= 3 and t[0].isdigit()]
    out = [(int(t[0]), int(t[1]), int(t[2])) for t in nums]
    while len(out) < 16:
        out.append((0, 0, 0))
    return out[:16]


def gba_quantize(c):
    """Round to the 15-bit colour the hardware actually stores, so what the
    asset file says is what gbagfx emits and a round-trip is stable."""
    out = []
    for v in c:
        q = max(0, min(255, int(round(v)))) >> 3
        out.append((q << 3) | (q >> 2))
    return tuple(out)


def recolour(rgb, hue, sat_scale, l_lo, l_hi):
    if rgb in KEEP:
        return rgb
    r, g, b = (x / 255.0 for x in rgb)
    _, l, s = colorsys.rgb_to_hls(r, g, b)
    # Greys carry no meaningful hue; tinting them would colour the shadows and
    # cost the ramp the neutral end it uses as its darkest step.
    if s < 0.02:
        return gba_quantize(rgb)
    l = l_lo + l * (l_hi - l_lo)
    r, g, b = colorsys.hls_to_rgb(hue / 360.0, min(1.0, max(0.0, l)),
                                  min(1.0, s * sat_scale))
    return gba_quantize((r * 255, g * 255, b * 255))


def build(tone):
    hue, sat, l_lo, l_hi = TONES[tone]
    pals = []
    for i in range(16):
        src = parse_pal(SRC_PALETTES / f'{i:02d}.pal')
        if FIRST_SECONDARY_PAL <= i <= LAST_SECONDARY_PAL:
            pals.append([recolour(c, hue, sat, l_lo, l_hi) for c in src])
        else:
            pals.append(src)
    return pals


def write_pal(path, colours):
    # JASC-PAL is CRLF in every vanilla .pal, so match it rather than the
    # repo's usual LF rule - this is one of the few files where CRLF is right.
    body = 'JASC-PAL\r\n0100\r\n16\r\n'
    body += ''.join(f'{r} {g} {b}\r\n' for r, g, b in colours)
    path.write_bytes(body.encode('ascii'))


def relative_luminance(c):
    def lin(v):
        v /= 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (lin(x) for x in c)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast_ratio(a, b):
    la, lb = relative_luminance(a), relative_luminance(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def cmd_write(tone):
    d = out_dir(tone)
    d.mkdir(parents=True, exist_ok=True)
    pals = build(tone)
    for i, colours in enumerate(pals):
        write_pal(d / f'{i:02d}.pal', colours)
    ramp = pals[6][2:8]
    print(f'{tone:8s} -> {d.relative_to(REPO)}')
    print('         ramp', ' '.join('#%02X%02X%02X' % c for c in ramp))
    # The floor is the light end of the ramp and the wall's shadow the dark
    # end, so this is roughly the contrast the outline has to work with.
    print('         floor/shadow contrast %.2f:1' % contrast_ratio(ramp[0], ramp[-1]))


def cmd_preview(out_path, crop, scales):
    from PIL import Image, ImageDraw
    import tileset_atlas as ta
    from tileset_resolve import TilesetResolver
    import json

    R = TilesetResolver(REPO)
    layouts = json.loads((REPO / 'data/layouts/layouts.json').read_text(encoding='utf-8'))
    entry = next(e for e in layouts['layouts'] if e and e.get('id') == 'LAYOUT_VICTORY_ROAD_1F')
    w, h = entry['width'], entry['height']
    raw = (REPO / entry['blockdata_filepath']).read_bytes()
    blocks = struct.unpack(f'<{w * h}H', raw[:w * h * 2])

    x0, y0, cw, ch = crop
    variants = [('cave (Granite Cave today)', None)] + [(t, t) for t in TONES]

    panels = []
    for label, tone in variants:
        pair = ta.TilesetPair(ta.Tileset(R.resolve('gTileset_General')),
                              ta.Tileset(R.resolve('gTileset_Cave')))
        if tone:
            for i, colours in enumerate(build(tone)):
                if i < len(pair.secondary.palettes):
                    pair.secondary.palettes[i] = colours
        panels.append((label, pair))

    for scale in scales:
        cell = 16 * scale
        pad, lab = 10, 16
        img = Image.new('RGB', (len(panels) * (cw * cell + pad) + pad,
                                ch * cell + pad * 2 + lab), (28, 28, 34))
        d = ImageDraw.Draw(img)
        for n, (label, pair) in enumerate(panels):
            ox = pad + n * (cw * cell + pad)
            for yy in range(ch):
                for xx in range(cw):
                    mid = blocks[(y0 + yy) * w + (x0 + xx)] & 0x3FF
                    img.paste(ta.render_metatile(pair, mid, scale),
                              (ox + xx * cell, pad + lab + yy * cell))
            d.text((ox, pad // 2), label, fill=(230, 230, 240))
        p = out_path.with_name(f'{out_path.stem}_{scale}x.png')
        img.save(p)
        print('wrote', p)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--preview', action='store_true')
    ap.add_argument('--write', choices=sorted(TONES) + ['all'])
    ap.add_argument('--crop', type=int, nargs=4, default=[14, 12, 14, 14],
                    metavar=('X', 'Y', 'W', 'H'))
    a = ap.parse_args()
    if a.preview:
        cmd_preview(Path(__file__).resolve().parent / '_out' / 'victory_road_tones.png',
                    a.crop, (2, 5))
    if a.write:
        for t in (sorted(TONES) if a.write == 'all' else [a.write]):
            cmd_write(t)
    if not a.preview and not a.write:
        ap.error('nothing to do: pass --preview or --write')


if __name__ == '__main__':
    main()
