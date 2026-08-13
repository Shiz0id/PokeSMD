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
MAX_TILES = 1024                               # a tilemap entry has 10 index bits
OUT_COLS = 16                                  # tiles.png grid width


def _key(px):
    return bytes(px)


def _flip_h(px):
    return [px[y * TILE + (TILE - 1 - x)] for y in range(TILE) for x in range(TILE)]


def _flip_v(px):
    return [px[(TILE - 1 - y) * TILE + x] for y in range(TILE) for x in range(TILE)]


def load_indexed(path):
    from PIL import Image

    im = Image.open(path)
    if im.mode != "P":
        raise SystemExit(f"{path.name}: not an indexed PNG (mode {im.mode}) - "
                         f"a battle background must be 4bpp-able")
    if im.size != (SRC_W, SRC_H):
        raise SystemExit(f"{path.name}: {im.size[0]}x{im.size[1]}, expected "
                         f"{SRC_W}x{SRC_H} - that is the 32x64 tile grid every "
                         f"vanilla map.bin is sized for")
    return im


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
        entries.append(idx | (hf << 10) | (vf << 11) | (pal << 12))
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
        idx, hf, vf, pal = entry & 0x3FF, (entry >> 10) & 1, (entry >> 11) & 1, entry >> 12
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

    im = load_indexed(src)
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
