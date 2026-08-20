#!/usr/bin/env python3
"""Cut a SpriteCollab-style PMD portrait sheet into per-face indexed PNGs.

WHAT A SHEET IS. The PMD Sprite Repository ships one PNG per species holding a
5-wide grid of 40x40 portraits in a fixed face order. Empty slots are fully
transparent. A 200x320 sheet is therefore 5 x 8 = 40 slots, of which the second
half is the mirrored set that this project does not use.

WHY PER-FACE PNGs AND NOT ONE BLOB. The decomp build has catch-all rules
(%.4bpp: %.png and %.gbapal: %.png in the Makefile), so committing indexed PNGs
means gbagfx does the conversion, the art stays editable in any image editor,
and nothing binary lands in the tree. gbagfx also emits tiles in the row-major
8x8 order that RoguePortrait_Draw blits from, so no reordering is needed here.

THE PALETTE RULE, and it is the whole reason this tool exists rather than a
one-line convert. A GBA sprite treats palette index 0 as TRANSPARENT. These
portraits are fully opaque - the background is part of the art, exactly as in
the Mystery Dungeon original - so every real colour has to live at index 1..15
and index 0 has to be a placeholder that no pixel uses. A straight quantise puts
a real colour at 0 and punches a hole through the portrait in the shape of
whichever colour happened to sort first.

Each face carries its OWN palette. Across a whole sheet the union runs to 80+
colours, so there is no shared 16-colour palette to be had; per-face is not a
convenience, it is the only thing that fits.

Usage:
  python3 tools/rogue/import_portraits.py SHEET.png SPECIES [--repo PATH]
  python3 tools/rogue/import_portraits.py --batch DIR [--repo PATH]
  ... add --dry-run to either to see what it would do

--batch imports every portrait-NNNN.png in DIR, taking NNNN as the national dex
number and resolving the species from enum NationalDexOrder in the repo. That
mapping is READ, never written out here: a hand-kept dex table goes stale
silently, because a wrong name still imports cleanly and merely puts the art on
the wrong Pokemon.
"""
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print('Pillow is required: pip install Pillow')
    sys.exit(2)

CELL = 40
COLS = 5

# The SpriteCollab face order. INFERRED FROM THE GRID POSITION, because a bare
# sheet carries no metadata naming its slots - the repository's own directories
# have one file per name and that is the authority if you have them.
FACES = [
    'normal', 'happy', 'pain', 'angry', 'worried',
    'sad', 'crying', 'shouting', 'teary_eyed', 'determined',
    'joyous', 'inspired', 'surprised', 'dizzy', 'special0',
    'special1', 'sigh', 'stunned', 'special2', 'special3',
]

# Index 0 must be a colour no pixel uses. Checked against the art rather than
# assumed, because a collision would make part of the portrait transparent.
PLACEHOLDER = (255, 0, 255)

MAX_COLOURS = 15  # indices 1..15


def gba5(px):
    """The 5-bit key gbagfx will quantise to. Two source colours sharing a key
    are one GBA colour, so they must share a palette slot here too."""
    return (px[0] >> 3, px[1] >> 3, px[2] >> 3)


def cut(sheet_path, species, repo, dry_run):
    im = Image.open(sheet_path).convert('RGBA')
    w, h = im.size
    if w % CELL or h % CELL:
        print('sheet is %dx%d, which is not a whole number of %dpx cells'
              % (w, h, CELL, CELL))
        return 1
    cols, rows = w // CELL, h // CELL
    if cols != COLS:
        print('warning: sheet is %d columns wide, expected %d - the face order '
              'below assumes %d' % (cols, COLS, COLS))

    outdir = Path(repo) / 'graphics' / 'portraits' / species
    px = im.load()
    written = []
    mirrored = 0

    # SLOTS PAST THE FACE LIST ARE THE MIRRORED SET AND ARE DELIBERATELY DROPPED.
    # A full sheet is 40 slots: the first 20 are the faces, the next 20 are the
    # same faces flipped, for when the speaker is drawn on the RIGHT. This
    # project's portrait is always top-left, so the flipped half is never
    # reachable - importing it would cost ~10 KB of ROM per species and emit
    # [PORTRAIT_FACE_SLOT20] rows for enum members that do not exist.
    #
    # They are NOT pixel-exact mirrors - the repository hand-adjusts them - so a
    # "drop anything identical to a flip of an earlier slot" rule would keep them
    # all. The slot index is the only reliable signal.
    for slot in range(cols * rows):
        if slot >= len(FACES):
            cx, cy = (slot % cols) * CELL, (slot // cols) * CELL
            if any(px[cx + x, cy + y][3] > 0 for y in range(CELL) for x in range(CELL)):
                mirrored += 1
            continue
        cx, cy = (slot % cols) * CELL, (slot // cols) * CELL
        pixels = [px[cx + x, cy + y] for y in range(CELL) for x in range(CELL)]
        if all(p[3] == 0 for p in pixels):
            continue  # empty slot
        if any(p[3] == 0 for p in pixels):
            print('slot %d is partly transparent; the portrait box has no '
                  'backdrop behind it, so holes would show the field through '
                  'the frame' % slot)
            return 1

        # One palette slot per distinct GBA colour, deterministic order.
        keys = sorted({gba5(p) for p in pixels})
        if len(keys) > MAX_COLOURS:
            print('slot %d has %d GBA colours; only %d fit alongside the '
                  'transparent index 0' % (slot, len(keys), MAX_COLOURS))
            return 1
        if gba5(PLACEHOLDER + (255,)) in keys:
            print('slot %d actually uses the placeholder colour %s, so index 0 '
                  'would not be safe' % (slot, PLACEHOLDER))
            return 1

        # Keep the original 8-bit value for each key: gbagfx quantises the same
        # way, so this round-trips the source exactly instead of darkening it.
        first = {}
        for p in pixels:
            first.setdefault(gba5(p), p[:3])

        index_of = {k: i + 1 for i, k in enumerate(keys)}
        out = Image.new('P', (CELL, CELL))
        flat = [PLACEHOLDER[0], PLACEHOLDER[1], PLACEHOLDER[2]]
        for k in keys:
            flat.extend(first[k])
        # Pad to EXACTLY 16 entries, not 256. Pillow sizes the PNG's PLTE chunk
        # from the palette it is given, and gbagfx sizes the .gbapal from the
        # PLTE - so a 256-entry palette here emits a 512-byte .gbapal per face
        # instead of 32. The colours would still be right, because a sprite
        # palette only ever loads the first 16, but it is 480 wasted bytes of ROM
        # times every face of every species.
        flat.extend([0, 0, 0] * (16 - 1 - len(keys)))
        out.putpalette(flat)
        out.putdata([index_of[gba5(p)] for p in pixels])

        name = FACES[slot]
        written.append((slot, name, len(keys)))
        if not dry_run:
            outdir.mkdir(parents=True, exist_ok=True)
            out.save(outdir / ('%s.png' % name))

    for slot, name, n in written:
        print('  slot %2d -> %-12s %d colours' % (slot, name, n))
    print('%d face(s) %s %s%s'
          % (len(written), 'would be written to' if dry_run else 'written to',
             outdir,
             ', %d mirrored slot(s) skipped' % mirrored if mirrored else ''))

    # The data file has to carry LITERAL INCBIN paths: preproc scans the source
    # text for INCBIN_U32("...") before the C preprocessor runs, so a macro that
    # builds the path from tokens expands to nothing it can see. Hence generating
    # the file rather than writing one clever macro by hand.
    tag = species.capitalize()
    lines = [
        '// GENERATED by tools/rogue/import_portraits.py - do not edit by hand.',
        '// Re-run the tool against the sheet to change anything here.',
        '//',
        '// Face names are the Sprite Repository grid order. Slots the sheet left',
        '// empty are simply absent, and portrait selection skips them.',
        '',
    ]
    for _, name, _n in written:
        lines.append('static const u32 sPortraitGfx_%s_%s[] = INCBIN_U32("graphics/portraits/%s/%s.4bpp");'
                     % (tag, name, species, name))
        lines.append('static const u16 sPortraitPal_%s_%s[] = INCBIN_U16("graphics/portraits/%s/%s.gbapal");'
                     % (tag, name, species, name))
    lines.append('')
    lines.append('static const struct RoguePortraitImage sPortraitFaces_%s[] =' % tag)
    lines.append('{')
    for _, name, _n in written:
        lines.append('    [PORTRAIT_FACE_%s] = { sPortraitGfx_%s_%s, sPortraitPal_%s_%s },'
                     % (name.upper(), tag, name, tag, name))
    lines.append('};')
    lines.append('')

    datafile = Path(repo) / 'src' / 'data' / 'portraits' / ('%s.h' % species)
    if not dry_run:
        datafile.parent.mkdir(parents=True, exist_ok=True)
        datafile.write_text('\n'.join(lines), encoding='utf-8', newline='\n')
    print('data file %s %s'
          % ('would be written to' if dry_run else 'written to', datafile))

    print('\nAdd to src/data/rogue_portraits.h:\n')
    print('    #include "portraits/%s.h"' % species)
    print('    [SPECIES_%s] = { %d, %d, sPortraitFaces_%s, ARRAY_COUNT(sPortraitFaces_%s) },'
          % (species.upper(), CELL // 8, CELL // 8, tag, tag))
    return 0


def dex_names(repo):
    """National dex number -> species name, read from the repo.

    DERIVED, not a table kept here. enum NationalDexOrder in
    include/constants/pokedex.h is the game's own numbering and is the only thing
    that can be trusted to agree with SPECIES_* - a mapping written out by hand
    goes stale the moment a form or a gen is added, and it goes stale silently,
    because a wrong name still imports perfectly and just puts the art on the
    wrong Pokemon.
    """
    text = (Path(repo) / 'include' / 'constants' / 'pokedex.h').read_text(
        encoding='utf-8', errors='replace')
    m = re.search(r'enum\s+NationalDexOrder\s*\{(.*?)\n\};', text, re.S)
    if m is None:
        raise ValueError('enum NationalDexOrder not found in constants/pokedex.h')
    body = re.sub(r'//[^\n]*', ' ', m.group(1))
    body = re.sub(r'/\*.*?\*/', ' ', body, flags=re.S)

    names, nxt = {}, 0
    for entry in body.split(','):
        entry = entry.strip()
        if not entry:
            continue
        em = re.match(r'^NATIONAL_DEX_([A-Z0-9_]+)\s*(?:=\s*(\w+))?$', entry)
        if em is None:
            continue
        if em.group(2) is not None:
            try:
                nxt = int(em.group(2), 0)
            except ValueError:
                continue  # aliased to another constant; the running count still holds
        names[nxt] = em.group(1).lower()
        nxt += 1
    return names


def batch(directory, repo, dry_run):
    """Import every portrait-NNNN.png in a directory, NNNN being the dex number."""
    try:
        names = dex_names(repo)
    except Exception as exc:  # noqa: BLE001 - the message is the point
        print('could not read the national dex order: %s' % exc)
        return 1

    species_h = (Path(repo) / 'include' / 'constants' / 'species.h').read_text(
        encoding='utf-8', errors='replace')

    sheets = sorted(Path(directory).glob('portrait-*.png'))
    if not sheets:
        print('no portrait-NNNN.png files in %s' % directory)
        return 1

    done, rows = [], []
    for sheet in sheets:
        sm = re.match(r'portrait-(\d+)$', sheet.stem)
        if sm is None:
            print('skipping %s: name is not portrait-NNNN.png' % sheet.name)
            continue
        num = int(sm.group(1))
        species = names.get(num)
        if species is None:
            print('%s: no NATIONAL_DEX entry for %d' % (sheet.name, num))
            return 1
        # Fail here rather than emitting a row that breaks the build later, and
        # far more importantly rather than emitting a row that COMPILES against
        # the wrong species.
        if not re.search(r'\bSPECIES_%s\b' % species.upper(), species_h):
            print('%s: dex %d is %s, but SPECIES_%s is not in constants/species.h'
                  % (sheet.name, num, species, species.upper()))
            return 1

        print('== %s -> %s (dex %d)' % (sheet.name, species, num))
        rc = cut(str(sheet), species, repo, dry_run)
        if rc != 0:
            return rc
        done.append(species)
        rows.append(species)

    print('\n%d species imported.\n' % len(done))
    print('Includes for src/data/rogue_portraits.h:')
    for s in rows:
        print('    #include "portraits/%s.h"' % s)
    print('\nRows for gRoguePortraits:')
    width = max(len(s) for s in rows)
    for s in rows:
        tag = s.capitalize()
        print('    [SPECIES_%s]%s = { %d, %d, sPortraitFaces_%s,%s '
              'ARRAY_COUNT(sPortraitFaces_%s) },'
              % (s.upper(), ' ' * (width - len(s)), CELL // 8, CELL // 8,
                 tag, ' ' * (width - len(s)), tag))
    return 0


def main():
    args = [a for a in sys.argv[1:]]
    dry_run = '--dry-run' in args
    if dry_run:
        args.remove('--dry-run')
    repo = '.'
    if '--repo' in args:
        i = args.index('--repo')
        if i + 1 >= len(args):
            print('--repo needs a path')
            return 2
        repo = args[i + 1]
        del args[i:i + 2]
    if '--batch' in args:
        i = args.index('--batch')
        if i + 1 >= len(args):
            print('--batch needs a directory')
            return 2
        return batch(args[i + 1], repo, dry_run)
    if len(args) != 2:
        print(__doc__)
        return 2
    return cut(args[0], args[1].lower(), repo, dry_run)


if __name__ == '__main__':
    sys.exit(main())
