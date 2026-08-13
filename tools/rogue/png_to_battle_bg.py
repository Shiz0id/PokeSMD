"""Convert a flat battle-background PNG into the decomp's tiles/map/palette trio.

The Team Aqua repo ships forty-eight CFRU battle backgrounds as finished
256x512 indexed PNGs, but only seven of them in the layout the decomp wants -
tiles.png, map.bin, palette.pal. This makes the other forty-one usable, and it
runs headless, where the community answer (Tilemap Studio, esperance's Tilemap
Creator) is a GUI and therefore a per-image manual step.

THE SHAPES ARE MATCHED TO VANILLA, NOT INVENTED. Every
graphics/battle_environment/*/map.bin in the tree is 4096 bytes - 2048 entries
of u16 - and 2048 is exactly the 32x64 tile grid of a 256x512 image. So the
source PNG is the background laid out tile-for-tile and the conversion is a
deduplication, not a re-layout.

A tilemap entry is:

    bits 0-9   tile index
    bit 10     horizontal flip
    bit 11     vertical flip
    bits 12-15 palette number

so a tile that already exists mirrored costs nothing, which is most of the
saving on a symmetrical background.

PALETTES. A battle background owns THREE 16-colour palettes - battle_bg.c loads
3 * PLTT_SIZE_4BPP - so a source PNG carries up to 48 colours and every tile
must sit entirely inside one 16-colour block. That is a real constraint on the
art rather than a detail of this script: a tile straddling two blocks cannot be
drawn by the hardware at all. Straddling tiles are REPORTED AND REFUSED rather
than quietly forced into one block, because forcing one silently recolours the
image and the failure would only show up on hardware.

VERIFY IS ON BY DEFAULT and is the whole point. It rebuilds the image from the
tiles, the map and the palette that were just written and compares it pixel for
pixel with the input. A tile deduplicator that is subtly wrong - a flip bit the
wrong way round, a palette block misread - still produces plausible output, and
the round trip is what catches that rather than a look at the result.

Run:
    python3 tools/rogue/png_to_battle_bg.py IN.png OUTDIR [--write] [--no-verify]

Writes OUTDIR/{tiles.png,map.bin,palette.pal}. Without --write it reports only.
"""
import struct
import sys
from pathlib import Path

TILE = 8
SRC_W, SRC_H = 256, 512
COLS, ROWS = SRC_W // TILE, SRC_H // TILE     # 32 x 64
ENTRIES = COLS * ROWS                          # 2048, and map.bin is 2 * this
MAX_PALETTES = 3
COLOURS = 16

# THE BANK A TILEMAP ENTRY NAMES IS NOT THE PALETTE INDEX. battle_bg.c loads an
# environment with LoadPalette(info.palette, BG_PLTT_ID(2), 3 * PLTT_SIZE_4BPP),
# so a background's three palettes land in BG banks 2, 3 and 4. Banks 0 and 1
# hold the battle TEXTBOX palette.
#
# THIS SHIPPED WRONG ONCE, on all three backgrounds this tool has produced.
# Exporting a single-palette image gives every tile bank 0, which is right for a
# standalone image and wrong here - the palette loads and is never referenced,
# and every tile draws in the textbox colours. The art, the .pal and the table
# row were all correct; the only disagreement was between a tilemap and a load
# call in another file, which no build step can see.
#
# The round trip below CANNOT catch it, and that is the lesson. It rebuilds the
# image from the same map.bin it wrote, so bank 0 out matches bank 0 in and the
# verifier agrees with itself. check_battle_bg_palettes.py is what catches it,
# by holding the tilemap against the load call.
BG_FIRST_BANK = 2
MAX_TILES = 1024                               # a tilemap entry has 10 index bits
OUT_COLS = 16                                  # tiles.png grid width


def _key(px):
    return bytes(px)


def _flip_h(px):
    return [px[y * TILE + (TILE - 1 - x)] for y in range(TILE) for x in range(TILE)]


def _flip_v(px):
    return [px[(TILE - 1 - y) * TILE + x] for y in range(TILE) for x in range(TILE)]


# How many rows of a battle background are actually drawn. Measured, not
# assumed: every CFRU and Leob0505 source puts art in rows 0-111 and a
# do-not-draw key colour from 112 down, and the message box covers the rest.
ART_ROWS = 112

# HOW MUCH ONE COLOUR MAY WEIGH when a bank's palette is chosen. Median cut
# follows pixel counts, so an ocean of flat blue will take every slot and leave
# a small detailed object - a fish, a coral - sharing two. Capping each colour's
# vote lets rare detail compete with large flat areas.
#
# Not zero-cost: a cap this low also stops a genuinely dominant colour getting
# the several near-neighbours a smooth gradient needs, so a sky can band. 24 is
# where the underwater piece stopped smearing Magikarp without visibly banding
# its water. Raise it for art that is mostly gradient, lower it for art that is
# mostly detail.
FLAT_CAP = 64


def fit_to_canvas(path, out_path, top=0):
    """Reframe arbitrary art into the 256x512 shape the converter expects.

    Commissioned and community art does not arrive GBA-shaped. The jungle piece
    is 512x288 - a widescreen illustration - where a battle background is 256
    wide with its art in the top 112 rows.

    Scales to 256 wide PRESERVING ASPECT and keeps ART_ROWS starting at `top`.
    That is a crop, not a squash: 512x288 halves to 256x144 and loses 32 rows.
    Any other fit would distort, and distorted pixel art reads as broken rather
    than as scaled.

    WHICH ROWS TO KEEP IS A COMPOSITION DECISION, not a default. A battle
    background shows 112 rows and the combatants stand in front of them, so what
    belongs in frame is the middle distance - the thing the scene is ABOUT. The
    underwater piece is the case that forced the parameter: its top is empty
    sunlit water and everything worth seeing, the kelp and the silhouettes, sits
    lower down. `top` is in SCALED rows, so it is read off the same image the
    crop is taken from.

    The key colour is INVENTED here, because art that was never a background has
    no do-not-draw region. It is chosen to be absent from the art so it cannot
    swallow a real colour.
    """
    from PIL import Image

    im = Image.open(path).convert("RGB")
    scaled = im.resize((SRC_W, max(1, round(im.height * SRC_W / im.width))),
                       Image.LANCZOS)
    top = max(0, min(top, max(0, scaled.height - ART_ROWS)))
    band = scaled.crop((0, top, SRC_W, min(top + ART_ROWS, scaled.height)))

    present = set(band.getdata())
    key = next(((r, g, b) for r in (255, 254) for g in (0, 1) for b in (255, 254)
                if (r, g, b) not in present), None)
    if key is None:
        raise SystemExit("cannot find an unused key colour")

    canvas = Image.new("RGB", (SRC_W, SRC_H), key)
    canvas.paste(band, (0, 0))
    canvas.save(out_path)
    print(f"{path.name}: {im.width}x{im.height} -> {SRC_W}x{scaled.height}, "
          f"kept rows {top}-{top + band.height - 1}, key {key} -> {out_path}")
    return out_path


def _palette_from(weighted, n, pinned=()):
    """Median-cut a {colour: count} bag down to n colours.

    `pinned` colours are placed in the palette VERBATIM and the median cut fills
    what is left. That is the difference between a colour being approximated and
    a colour being kept: a quantiser optimises the average, and the average does
    not care about a fish, so a small object of unique hue gets blended into its
    surroundings however good the overall error looks.

    Pinning is not free. Each pinned colour costs a slot the rest of the bank
    would have had, so protecting a large area makes everything around it worse.
    It is for the handful of colours that carry a subject.
    """
    from PIL import Image

    pins = []
    for c in pinned:
        if c in weighted and c not in pins and len(pins) < n:
            pins.append(c)

    rest = {c: w for c, w in weighted.items() if c not in pins}
    remaining = n - len(pins)
    if remaining <= 0 or not rest:
        return pins

    pixels = []
    for colour, count in rest.items():
        # Cap the weight so one enormous flat region cannot starve the detail
        # of every palette slot.
        pixels.extend([colour] * min(count, FLAT_CAP))
    if not pixels:
        return pins
    strip = Image.new("RGB", (len(pixels), 1))
    strip.putdata(pixels)
    q = strip.quantize(colors=remaining, method=Image.MEDIANCUT,
                       dither=Image.Dither.NONE)
    pal = q.getpalette()
    used = len(set(q.getdata()))
    return pins + [tuple(pal[i * 3:i * 3 + 3]) for i in range(used)]


# How much luminance counts when deciding WHICH BANK a tile belongs to. Below 1
# the grouping follows hue and saturation instead of lighting. 0.25 is enough to
# stop a beam-lit sea splitting into three shades of itself while still keeping
# genuinely dark material apart from genuinely light material.
CHROMA_WEIGHT = 0.25


def _chroma(c):
    """-> a colour in a space where luminance is de-emphasised."""
    r, g, b = c
    return (r - g, g - b, (r + g + b) * CHROMA_WEIGHT / 3.0)


def _chroma_dist2(a, b):
    ca, cb = _chroma(a), _chroma(b)
    return sum((ca[i] - cb[i]) ** 2 for i in range(3))


def _nearest_chroma(colour, palette):
    """-> squared chroma distance to the closest palette entry."""
    best = None
    for p in palette:
        d = _chroma_dist2(colour, p)
        if best is None or d < best:
            best = d
    return best or 0


def _nearest(colour, palette):
    """-> (index, squared distance) of the closest palette entry."""
    r, g, b = colour
    best, best_d = 0, None
    for i, (pr, pg, pb) in enumerate(palette):
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if best_d is None or d < best_d:
            best, best_d = i, d
    return best, best_d


def protected_colours(im, key, boxes, limit=6):
    """-> the colours DISTINCTIVE to a protected region, most distinctive first.

    RANKING BY FREQUENCY INSIDE THE BOX IS WRONG, and wrong in a way that looks
    right until measured. A subject worth protecting is small, so its own
    bounding box is mostly background: ranking by count pinned six shades of
    water around Magikarp and made the whole image worse than not pinning at
    all. What deserves a slot is what the region has and the rest of the image
    does not.

    Scored by enrichment - how much more of this colour is inside than outside -
    so a body tone that appears nowhere else outranks a water tone that appears
    everywhere, however much water is in the box.

    Capped, because pinning costs a slot each. Six carries a small subject: a
    body tone, two shades of it, an eye, an outline, a highlight.

    PINNING ONLY HELPS A SUBJECT THAT IS CHROMATICALLY DISTINCT, and the case
    that prompted it was not one. The underwater piece looked like it needed
    Magikarp protecting; measuring found the whole 256x112 band holds 162
    non-blue pixels and his body tones sit two or three pixels apart right
    beside the water in colour space. He is drawn as a low-contrast shape IN the
    water rather than against it, so there was nothing distinctive to keep -
    pinning spent six slots on near-duplicate blues and took the mean error from
    74 to 109, dithering the coral to pay for it.

    So: check that the subject actually differs in hue from its surroundings
    before reaching for this. It is for a red coral in a blue sea, not for a
    blue fish in one.
    """
    px = im.load()
    inside, outside = {}, {}
    boxes = [tuple(b) for b in boxes]
    for y in range(SRC_H):
        for x in range(SRC_W):
            c = px[x, y]
            if c == key:
                continue
            hit = any(x0 <= x < x1 and y0 <= y < y1 for (x0, y0, x1, y1) in boxes)
            bag = inside if hit else outside
            bag[c] = bag.get(c, 0) + 1

    def score(c):
        return inside[c] / (1.0 + outside.get(c, 0))

    # A colour needs to actually cover something to be worth a slot; one stray
    # pixel of a unique tone scores infinitely and is not what carries a subject.
    worth = [c for c in inside if inside[c] >= 4]
    return sorted(worth, key=score, reverse=True)[:limit]


def fit_banks(im, key, name, banks=MAX_PALETTES, rounds=12, protect=()):
    """Split an image across `banks` palettes so every TILE uses only one.

    THIS IS THE CONSTRAINT THAT MAKES BATTLE BACKGROUNDS HARD TO SOURCE. A
    background gets three 16-colour banks, but an 8x8 tile names ONE bank in its
    tilemap entry, so it is not enough to fit 48 colours overall - each tile must
    fit 16. Ordinary quantisers optimise the total and ignore that, which is why
    a median cut of the jungle art left 79% of its tiles straddling two banks and
    therefore undrawable.

    The method is k-means over TILES rather than over pixels. Start by clustering
    tiles on their average colour, then alternate: build each bank's palette from
    the tiles currently assigned to it, and reassign every tile to whichever bank
    reproduces it with least error. Both halves only ever lower the total error,
    so it settles.

    INDEX 0 OF EVERY BANK IS THE KEY COLOUR AND IS NOT AVAILABLE TO ART. On a BG
    layer, palette index 0 is transparent whatever colour sits there, so a real
    colour placed at 0 would render as the backdrop instead. That leaves FIFTEEN
    usable colours a bank, not sixteen - 45 in total rather than 48, which is a
    tighter budget than the naive figure and the reason a 48-colour quantise
    still is not enough.
    """
    from PIL import Image

    px = im.load()
    tiles = {}          # (tx, ty) -> {colour: count}
    for ty in range(SRC_H // TILE):
        for tx in range(SRC_W // TILE):
            bag = {}
            for y in range(TILE):
                for x in range(TILE):
                    c = px[tx * TILE + x, ty * TILE + y]
                    bag[c] = bag.get(c, 0) + 1
            tiles[(tx, ty)] = bag

    # Tiles that are nothing but key need no bank; they draw as index 0.
    art = {t: bag for t, bag in tiles.items() if set(bag) != {key}}
    if not art:
        raise SystemExit(f"{name}: the whole image is the key colour")

    def mean(bag):
        n = sum(bag.values())
        return tuple(sum(c[i] * w for c, w in bag.items()) / n for i in range(3))

    # Seed by splitting on the dominant axis of tile means, which puts visibly
    # different material - canopy, water, sand - in different banks to begin
    # with. A random seed converges to the same place more slowly.
    #
    # SEEDED AND MEASURED ON CHROMA, NOT BRIGHTNESS, and that is the whole
    # difference between grouping by material and grouping by shade. The first
    # version split on the widest axis of the raw RGB mean, which on any image
    # with lighting is BRIGHTNESS - so the underwater piece, which is lit by
    # beams, came out with three near-identical blue ramps: light water, medium
    # water, dark water. All 45 colours went on the sea and nothing was left for
    # the pink fish or the green kelp, which is exactly the muddiness that got
    # noticed.
    #
    # CHROMA_WEIGHT scales luminance down in the distance used for grouping.
    # Final pixel mapping still uses true RGB - a colour must be reproduced
    # accurately once its bank is chosen - but which bank a tile BELONGS to is a
    # question about what the tile is made of, and lighting is not that.
    means = {t: mean(b) for t, b in art.items()}
    chroma = {t: _chroma(m) for t, m in means.items()}
    axis = max(range(3), key=lambda i: max(c[i] for c in chroma.values())
               - min(c[i] for c in chroma.values()))
    order = sorted(art, key=lambda t: chroma[t][axis])
    assign = {}
    for i, t in enumerate(order):
        assign[t] = min(i * banks // len(order), banks - 1)

    palettes = []
    for _ in range(rounds):
        palettes = []
        for b in range(banks):
            bag = {}
            for t, a in assign.items():
                if a != b:
                    continue
                for c, w in art[t].items():
                    if c != key:
                        bag[c] = bag.get(c, 0) + w
            # Only pin a protected colour into a bank that actually contains
            # it. Pinning it everywhere would spend a slot in every bank to
            # protect a subject that lives in one.
            pins = [c for c in protect if c in bag]
            palettes.append(_palette_from(bag, COLOURS - 1, pins))

        moved = 0
        for t, bag in art.items():
            best, best_err = assign[t], None
            for b, pal in enumerate(palettes):
                if not pal:
                    continue
                err = 0
                for c, w in bag.items():
                    if c == key:
                        continue
                    # Chroma-weighted, matching the seed. Plain RGB error here
                    # pulls every tile back toward whichever bank is closest in
                    # brightness and undoes the grouping the seed just made.
                    err += _nearest_chroma(c, pal) * w
                if best_err is None or err < best_err:
                    best, best_err = b, err
            if best != assign[t]:
                assign[t] = best
                moved += 1
        if moved == 0:
            break

    # Report the damage honestly: worst-case per-pixel error tells you whether a
    # bank is being asked to hold two unrelated materials.
    total, worst = 0, 0
    for t, bag in art.items():
        pal = palettes[assign[t]]
        for c, w in bag.items():
            if c == key:
                continue
            d = _nearest(c, pal)[1]
            total += d * w
            worst = max(worst, d)
    pixels = sum(w for bag in art.values() for c, w in bag.items() if c != key)
    print(f"{name}: banked into {banks} palettes of {COLOURS - 1}, "
          f"mean error {total / max(pixels, 1):.1f}, worst {worst} "
          f"(squared RGB distance)")

    flat = []
    for b in range(banks):
        flat.extend(key)
        for c in palettes[b]:
            flat.extend(c)
        flat.extend([0, 0, 0] * (COLOURS - 1 - len(palettes[b])))
    flat.extend([0, 0, 0] * (256 - banks * COLOURS))

    out = Image.new("P", im.size, 0)
    out.putpalette(flat)
    dst = out.load()
    for (tx, ty), bag in tiles.items():
        b = assign.get((tx, ty), 0)
        pal = palettes[b]
        for y in range(TILE):
            for x in range(TILE):
                sx, sy = tx * TILE + x, ty * TILE + y
                c = px[sx, sy]
                if c == key or not pal:
                    dst[sx, sy] = b * COLOURS
                else:
                    dst[sx, sy] = b * COLOURS + 1 + _nearest(c, pal)[0]
    return out


def load_indexed(path, protect=()):
    """-> an indexed image, quantising a truecolour source when it is safe to.

    Not every background ships indexed. Leob0505's are RGBA with a uniform alpha
    of 255, so the extra channels carry nothing and the image is really a
    16-colour picture in a truecolour container. Quantising that is exact, so it
    is done rather than refused - but ONLY when the source genuinely holds no
    more colours than one bank, because anything else would be a lossy guess
    dressed up as a conversion.

    THE KEY COLOUR IS THE ONE FILLING THE MOST COMPLETE ROWS, and it becomes
    index 0. Battle backgrounds draw art in the top 112 rows and fill the rest
    with a do-not-draw colour the message box covers - black in the CFRU set,
    hot pink in Leob0505's.

    Two simpler rules were tried and are wrong. THE BOTTOM-LEFT PIXEL fails on
    building.png, whose key band is rows 112 to 495 with other content below it,
    so the corner is ordinary grey - and grey as index 0 would have turned every
    grey pixel of the rock face transparent. THE MOST COMMON COLOUR happens to
    work on all three of these and breaks on the first background whose sky is
    larger than its message box. Counting uniform rows keys on the thing that is
    actually true of a message-box fill: it spans the full width, repeatedly.
    """
    from PIL import Image

    im = Image.open(path)
    if im.size != (SRC_W, SRC_H):
        raise SystemExit(f"{path.name}: {im.size[0]}x{im.size[1]}, expected "
                         f"{SRC_W}x{SRC_H} - that is the 32x64 tile grid every "
                         f"vanilla map.bin is sized for")
    if im.mode == "P":
        return im
    if im.mode not in ("RGB", "RGBA"):
        raise SystemExit(f"{path.name}: mode {im.mode} is not something this "
                         f"can index")

    if im.mode == "RGBA":
        alpha = {p[3] for p in im.getdata()}
        if alpha != {255}:
            raise SystemExit(f"{path.name}: has real transparency "
                            f"(alpha {sorted(alpha)[:4]}) - index it by hand, "
                            f"because which colour becomes index 0 is a "
                            f"decision this cannot make")
        im = im.convert("RGB")

    px = im.load()
    uniform = {}
    for y in range(SRC_H):
        first = px[0, y]
        if all(px[x, y] == first for x in range(SRC_W)):
            uniform[first] = uniform.get(first, 0) + 1
    key = max(uniform, key=uniform.get) if uniform else px[0, SRC_H - 1]
    colours = [key]
    too_many = False
    for y in range(SRC_H):
        for x in range(SRC_W):
            c = px[x, y]
            if c not in colours:
                colours.append(c)
                if len(colours) > COLOURS:
                    too_many = True
                    break
        if too_many:
            break

    if too_many:
        pins = protected_colours(im, key, protect) if protect else ()
        if pins:
            print(f"{path.name}: pinning {len(pins)} protected colours {pins}")
        return fit_banks(im, key, path.name, protect=pins)

    out = Image.new("P", im.size, 0)
    flat = []
    for c in colours:
        flat.extend(c)
    flat.extend([0, 0, 0] * (256 - len(colours)))
    out.putpalette(flat)
    dst = out.load()
    lookup = {c: i for i, c in enumerate(colours)}
    for y in range(SRC_H):
        for x in range(SRC_W):
            dst[x, y] = lookup[px[x, y]]
    print(f"{path.name}: quantised {im.mode} to {len(colours)} colours, "
          f"key {key} -> index 0")
    return out


def repack(im):
    """Collapse an image into one 16-colour block when its colours fit.

    THE STRADDLING PROBLEM IS USUALLY AN EXPORT ARTEFACT, NOT AN ART PROBLEM.
    CFRU backgrounds are exported across a 48-colour palette even when they use
    a dozen colours, and the spare entries are frequently DUPLICATES - Torma
    Depths spends index 47 on pure black when index 0 is already pure black, and
    then has 128 tiles that touch both blocks and so cannot be drawn at all.

    Where every distinct COLOUR fits in one block this is fixable with no loss
    whatsoever: rebuild the palette from the distinct RGB values and remap every
    pixel. Same picture, one palette, hardware-legal.

    Returns (image, report) with the image unchanged when no repack is possible,
    so a genuine multi-palette background still reaches the straddling check.
    """
    from PIL import Image

    pal = im.getpalette() or []
    src = im.load()

    used = []
    seen = {}
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            v = src[x, y] & 0xFF
            rgb = tuple(pal[v * 3 : v * 3 + 3]) if v * 3 + 2 < len(pal) else (0, 0, 0)
            if v not in seen:
                seen[v] = rgb
                if rgb not in used:
                    used.append(rgb)

    # Index 0 is the transparent entry and keeps its slot; everything else has
    # to fit in the remaining fifteen.
    zero = tuple(pal[0:3]) if len(pal) >= 3 else (0, 0, 0)
    others = [c for c in used if c != zero]
    if len(others) > COLOURS - 1:
        return im, (f"{len(others)} distinct non-transparent colours - too many "
                    f"for one block, leaving the palette layout alone")

    order = [zero] + others
    remap = {v: order.index(rgb) for v, rgb in seen.items()}
    if all(v == n for v, n in remap.items()):
        return im, None

    out = Image.new("P", im.size, 0)
    flat = []
    for c in order:
        flat.extend(c)
    flat.extend([0, 0, 0] * (256 - len(order)))
    out.putpalette(flat)
    dst = out.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            dst[x, y] = remap[src[x, y] & 0xFF]

    moved = sorted(v for v, n in remap.items() if v != n)
    return out, (f"repacked {len(seen)} palette entries into {len(order)} "
                 f"colours in one block (moved {moved})")


def cut_tiles(im):
    """-> [(palette_number, [64 pixel values 0-15])] in tilemap order."""
    px = im.load()
    out, straddling = [], []
    for ty in range(ROWS):
        for tx in range(COLS):
            raw = [px[tx * TILE + x, ty * TILE + y] & 0xFF
                   for y in range(TILE) for x in range(TILE)]
            blocks = {v // COLOURS for v in raw if v != 0}
            # index 0 is transparent and shared, so it belongs to no block
            if len(blocks) > 1:
                straddling.append((tx, ty, sorted(blocks)))
                blocks = {min(blocks)}
            pal = blocks.pop() if blocks else 0
            if pal >= MAX_PALETTES:
                straddling.append((tx, ty, [pal]))
                pal = MAX_PALETTES - 1
            out.append((pal, [0 if v == 0 else (v % COLOURS) for v in raw]))
    return out, straddling


def dedupe(tiles):
    """-> (unique tile pixel lists, tilemap entries).

    Tries all four orientations before adding a tile, so a mirrored repeat costs
    an entry rather than a tile.
    """
    # KEYED ON THE PIXELS ALONE, NOT ON (palette, pixels). 4bpp tile data is
    # palette-independent - the entry's palette bits pick the 16-colour bank at
    # draw time - so one pattern serves every palette it appears under. Keying
    # on the pair is correct but wasteful, and cost five extra tiles on
    # BG_Cave.png against the hand conversion CFRU ships for the same image.
    lookup, unique, entries = {}, [], []
    for pal, px in tiles:
        found = None
        for hf in (0, 1):
            for vf in (0, 1):
                cand = px
                if hf:
                    cand = _flip_h(cand)
                if vf:
                    cand = _flip_v(cand)
                idx = lookup.get(_key(cand))
                if idx is not None:
                    found = (idx, hf, vf)
                    break
            if found:
                break
        if found is None:
            idx = len(unique)
            if idx >= MAX_TILES:
                raise SystemExit(f"more than {MAX_TILES} unique tiles - a "
                                 f"tilemap entry only has 10 index bits")
            unique.append(px)
            lookup[_key(px)] = idx
            found = (idx, 0, 0)
        idx, hf, vf = found
        entries.append(idx | (hf << 10) | (vf << 11)
                       | ((pal + BG_FIRST_BANK) << 12))
    return unique, entries


def write_outputs(outdir, im, unique, entries):
    from PIL import Image

    rows = -(-len(unique) // OUT_COLS)
    sheet = Image.new("P", (OUT_COLS * TILE, rows * TILE), 0)
    sheet.putpalette(im.getpalette())
    dst = sheet.load()
    for i, px in enumerate(unique):
        ox, oy = (i % OUT_COLS) * TILE, (i // OUT_COLS) * TILE
        for y in range(TILE):
            for x in range(TILE):
                dst[ox + x, oy + y] = px[y * TILE + x]
    outdir.mkdir(parents=True, exist_ok=True)
    sheet.save(outdir / "tiles.png")
    (outdir / "map.bin").write_bytes(struct.pack(f"<{len(entries)}H", *entries))

    pal = im.getpalette()[: MAX_PALETTES * COLOURS * 3]
    lines = ["JASC-PAL", "0100", str(MAX_PALETTES * COLOURS)]
    for i in range(MAX_PALETTES * COLOURS):
        r, g, b = pal[i * 3 : i * 3 + 3] if i * 3 + 2 < len(pal) else (0, 0, 0)
        lines.append(f"{r} {g} {b}")
    (outdir / "palette.pal").write_text("\r\n".join(lines) + "\r\n", newline="")


def verify(im, unique, entries):
    """Rebuild the source from what was emitted. Returns the mismatch count."""
    src = im.load()
    bad = 0
    for i, entry in enumerate(entries):
        idx, hf, vf = entry & 0x3FF, (entry >> 10) & 1, (entry >> 11) & 1
        pal = (entry >> 12) - BG_FIRST_BANK
        px = unique[idx]
        if hf:
            px = _flip_h(px)
        if vf:
            px = _flip_v(px)
        tx, ty = (i % COLS) * TILE, (i // COLS) * TILE
        for y in range(TILE):
            for x in range(TILE):
                want = src[tx + x, ty + y] & 0xFF
                got = px[y * TILE + x]
                got = 0 if got == 0 else got + pal * COLOURS
                if want != got:
                    bad += 1
    return bad


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__.strip().splitlines()[-3].strip())
    src = Path(argv[0])
    outdir = Path(argv[1])

    if "--fit" in argv:
        top = 0
        if "--top" in argv:
            top = int(argv[argv.index("--top") + 1])
        outdir.mkdir(parents=True, exist_ok=True)
        src = fit_to_canvas(src, outdir / "source_fitted.png", top)

    protect = []
    if "--protect" in argv:
        for spec in argv[argv.index("--protect") + 1].split(";"):
            protect.append(tuple(int(v) for v in spec.split(",")))
    im = load_indexed(src, protect)
    im, note = repack(im)
    if note:
        print(f"{src.name}: {note}")
    tiles, straddling = cut_tiles(im)
    unique, entries = dedupe(tiles)

    saved = 100 * (1 - len(unique) / ENTRIES)
    print(f"{src.name}: {ENTRIES} tiles -> {len(unique)} unique ({saved:.1f}% deduped)")
    print(f"  map.bin {len(entries) * 2} bytes, palettes used "
          f"{sorted({e >> 12 for e in entries})}")

    if straddling:
        print(f"  {len(straddling)} tile(s) span more than one 16-colour block "
              f"or exceed {MAX_PALETTES} palettes - the hardware cannot draw "
              f"these, the art needs fixing:")
        for tx, ty, blocks in straddling[:8]:
            print(f"    tile ({tx},{ty}) touches blocks {blocks}")

    if "--no-verify" not in argv:
        bad = verify(im, unique, entries)
        print(f"  round trip: {'OK' if bad == 0 else f'{bad} MISMATCHED PIXELS'}")
        if bad:
            raise SystemExit("refusing to write output that does not rebuild "
                             "the source")

    if "--write" in argv:
        write_outputs(outdir, im, unique, entries)
        print(f"  wrote {outdir}/tiles.png, map.bin, palette.pal")
    else:
        print("  not written - pass --write")


if __name__ == "__main__":
    main(sys.argv[1:])
