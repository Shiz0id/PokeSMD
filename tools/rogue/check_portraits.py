"""Guard the imported PMD portrait art, and the table that points at it.

THE RULE, stated once, here:

    A portrait PNG is 4bpp-indexable art whose PALETTE INDEX 0 IS NEVER USED,
    whose size is a whole number of 8x8 tiles, and which is named by exactly one
    face slot of exactly one species' data file. The tile size a species declares
    in gRoguePortraits is the size its PNGs actually are.

WHY INDEX 0 IS THE WHOLE POINT. A GBA sprite reads palette index 0 as
TRANSPARENT. These portraits are fully opaque - the background is part of the
art, exactly as in the Mystery Dungeon original - so every colour has to live at
1..15. If an import ever lets a real colour land at index 0, the game does not
fail: it punches a transparent hole through the portrait in the shape of
whichever colour it was, and the frame's fill tile shows through. On a green
Bulbasaur against a green background that is easy to look straight past, and
nothing in the build says a word.

This is a DATA check, unlike check_portrait_lifetime.py which guards the
module's lifetime. Both are needed and neither covers the other: a perfect
palette cannot save a leaked sprite, and a perfect teardown cannot save art with
a hole in it.

NO PILLOW. The importer needs it, but Pillow is not installed in the WSL
toolchain this suite runs under, so the PNGs are decoded here with zlib and
about thirty lines of unfiltering. A check that cannot run is a check that never
fails.

WHAT IT ASSERTS.

  1. Every PNG under graphics/portraits/ is colour type 3 (indexed) with at most
     16 palette entries. Anything else cannot become a 4bpp sprite sheet.

  2. Index 0 is unused in every one of them.

  3. Dimensions are a whole number of tiles. gbagfx tiles the image blindly, so a
     41-pixel-wide portrait silently shears.

  4. Every face declared in src/data/portraits/<species>.h has its PNG on disk,
     and every PNG on disk is declared. An undeclared PNG is dead weight; a
     declared-but-missing one fails the build, but late and unhelpfully.

  5. The tile size in gRoguePortraits matches the art. Declaring 5x5 for 48x48
     art would blit past the row stride and shear the portrait.

  6. Every data file included by src/data/rogue_portraits.h exists, and every
     data file in src/data/portraits/ is included. An orphaned import is art
     nobody can see.

Usage:  python3 tools/rogue/check_portraits.py [REPO | --repo PATH]
        python3 tools/rogue/check_portraits.py --selftest
"""
import re
import struct
import sys
import tempfile
import zlib
from pathlib import Path

GFX_DIR = 'graphics/portraits'
DATA_DIR = 'src/data/portraits'
TABLE = 'src/data/rogue_portraits.h'

MAX_COLOURS = 16
TILE = 8


def fail(msg):
    print('FAIL  check_portraits.py: %s' % msg)
    return False


def read_png(path):
    """Return (width, height, palette_count, set_of_used_indices).

    Indexed PNGs only - that is what rule 1 is for, and anything else raises.
    """
    data = path.read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    i = 8
    width = height = depth = ctype = None
    palette_count = 0
    idat = b''
    while i + 8 <= len(data):
        length = struct.unpack('>I', data[i:i + 4])[0]
        ctag = data[i + 4:i + 8]
        body = data[i + 8:i + 8 + length]
        if ctag == b'IHDR':
            width, height, depth, ctype = struct.unpack('>IIBB', body[:10])
        elif ctag == b'PLTE':
            palette_count = length // 3
        elif ctag == b'IDAT':
            idat += body
        elif ctag == b'IEND':
            break
        i += 12 + length

    if ctype != 3:
        raise ValueError('colour type %s, expected 3 (indexed)' % ctype)
    if depth not in (1, 2, 4, 8):
        raise ValueError('bit depth %s' % depth)

    raw = zlib.decompress(idat)
    stride = (width * depth + 7) // 8
    used = set()
    prev = bytearray(stride)
    pos = 0
    for _ in range(height):
        ftype = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        # Filtering for sub-byte depths uses a 1-byte pixel distance.
        for x in range(stride):
            a = line[x - 1] if x >= 1 else 0
            b = prev[x]
            c = prev[x - 1] if x >= 1 else 0
            if ftype == 0:
                pass
            elif ftype == 1:
                line[x] = (line[x] + a) & 0xFF
            elif ftype == 2:
                line[x] = (line[x] + b) & 0xFF
            elif ftype == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 0xFF
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
            else:
                raise ValueError('bad filter type %d' % ftype)
        prev = line
        # Unpack the row's indices, ignoring padding past the image width.
        per_byte = 8 // depth
        mask = (1 << depth) - 1
        for px in range(width):
            byte = line[px // per_byte]
            shift = 8 - depth * (px % per_byte + 1)
            used.add((byte >> shift) & mask)

    return width, height, palette_count, used


def check(repo):
    repo = Path(repo)
    gfx_root = repo / GFX_DIR
    data_root = repo / DATA_DIR
    table = repo / TABLE

    if not table.exists():
        return fail('%s not found' % TABLE)
    if not gfx_root.is_dir():
        return fail('%s not found' % GFX_DIR)

    table_text = table.read_text(encoding='utf-8', errors='replace')

    # 6. Includes and data files line up.
    included = set(re.findall(r'#include\s+"portraits/([A-Za-z0-9_]+)\.h"', table_text))
    on_disk = {p.stem for p in data_root.glob('*.h')} if data_root.is_dir() else set()
    for name in sorted(included - on_disk):
        return fail('%s includes portraits/%s.h, which does not exist' % (TABLE, name))
    for name in sorted(on_disk - included):
        return fail('%s/%s.h exists but %s does not include it -- the art is in '
                    'the tree and nothing can reach it' % (DATA_DIR, name, TABLE))

    if not included:
        print('ok    check_portraits.py: no portraits imported yet, nothing to check')
        return True

    # 5. The declared tile size, per species.
    declared = {}
    for m in re.finditer(r'\[SPECIES_([A-Z0-9_]+)\]\s*=\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*'
                         r'sPortraitFaces_([A-Za-z0-9_]+)', table_text):
        declared[m.group(4).lower()] = (int(m.group(2)), int(m.group(3)), m.group(1))

    total = 0
    for species in sorted(included):
        data_file = data_root / ('%s.h' % species)
        text = data_file.read_text(encoding='utf-8', errors='replace')
        faces = set(re.findall(
            r'INCBIN_U32\("graphics/portraits/%s/([A-Za-z0-9_]+)\.4bpp"\)' % re.escape(species),
            text))
        pals = set(re.findall(
            r'INCBIN_U16\("graphics/portraits/%s/([A-Za-z0-9_]+)\.gbapal"\)' % re.escape(species),
            text))
        if faces != pals:
            return fail('%s/%s.h declares gfx and palettes for different faces: '
                        '%s. Every face needs BOTH, because each portrait carries '
                        'its own 15 colours'
                        % (DATA_DIR, species, sorted(faces ^ pals)))
        if not faces:
            return fail('%s/%s.h declares no faces' % (DATA_DIR, species))

        sp_dir = gfx_root / species
        if not sp_dir.is_dir():
            return fail('%s/%s does not exist, but its data file declares %d face(s)'
                        % (GFX_DIR, species, len(faces)))
        pngs = {p.stem for p in sp_dir.glob('*.png')}

        for missing in sorted(faces - pngs):
            return fail('%s/%s.h declares face "%s" but %s/%s/%s.png is missing'
                        % (DATA_DIR, species, missing, GFX_DIR, species, missing))
        for extra in sorted(pngs - faces):
            return fail('%s/%s/%s.png exists but is not declared in %s/%s.h -- '
                        're-run import_portraits.py rather than adding art by hand'
                        % (GFX_DIR, species, extra, DATA_DIR, species))

        if species not in declared:
            return fail('%s/%s.h is included but no gRoguePortraits row names '
                        'sPortraitFaces_%s' % (DATA_DIR, species, species.capitalize()))
        wTiles, hTiles, const = declared[species]

        for face in sorted(faces):
            png = sp_dir / ('%s.png' % face)
            try:
                w, h, ncolours, used = read_png(png)
            except Exception as exc:  # noqa: BLE001 - the message is the point
                return fail('%s/%s/%s.png could not be read as an indexed PNG '
                            '(%s). Only colour type 3 can become a 4bpp sheet'
                            % (GFX_DIR, species, face, exc))
            if ncolours > MAX_COLOURS:
                return fail('%s/%s/%s.png has %d palette entries; a 4bpp sprite '
                            'has %d' % (GFX_DIR, species, face, ncolours, MAX_COLOURS))
            if 0 in used:
                return fail('%s/%s/%s.png USES PALETTE INDEX 0, which a GBA sprite '
                            'reads as transparent. That colour would be punched '
                            'out of the portrait and the frame would show through '
                            '-- silently, and hardest to spot when the colour is '
                            'close to the background' % (GFX_DIR, species, face))
            if w % TILE or h % TILE:
                return fail('%s/%s/%s.png is %dx%d, which is not a whole number of '
                            '%dpx tiles -- gbagfx tiles it blindly and the portrait '
                            'shears' % (GFX_DIR, species, face, w, h, TILE))
            if (w // TILE, h // TILE) != (wTiles, hTiles):
                return fail('SPECIES_%s declares %dx%d tiles but %s/%s/%s.png is '
                            '%dx%d pixels (%dx%d tiles). The blit uses the declared '
                            'width as its row stride, so a mismatch shears the art'
                            % (const, wTiles, hTiles, GFX_DIR, species, face,
                               w, h, w // TILE, h // TILE))
            total += 1

    print('ok    check_portraits.py: %d portrait(s) across %d species are indexed, '
          'tile-aligned, and leave index 0 free'
          % (total, len(included)))
    return True


def _png(width, height, ncolours, indices, depth=4):
    """Minimal indexed PNG writer, for the selftest only."""
    def chunk(tag, body):
        return (struct.pack('>I', len(body)) + tag + body
                + struct.pack('>I', zlib.crc32(tag + body) & 0xFFFFFFFF))

    ihdr = struct.pack('>IIBBBBB', width, height, depth, 3, 0, 0, 0)
    plte = bytes()
    for i in range(ncolours):
        plte += bytes(((i * 16) % 256, (i * 7) % 256, (i * 3) % 256))
    per_byte = 8 // depth
    stride = (width * depth + 7) // 8
    raw = b''
    for y in range(height):
        row = bytearray(stride)
        for x in range(width):
            v = indices(x, y)
            shift = 8 - depth * (x % per_byte + 1)
            row[x // per_byte] |= (v & ((1 << depth) - 1)) << shift
        raw += b'\x00' + bytes(row)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'PLTE', plte)
            + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def selftest(repo):
    repo = Path(repo)
    if not check(repo):
        print('  SELFTEST INCONCLUSIVE: the check does not pass unmutated')
        return False

    good_table = ('#include "portraits/testmon.h"\n\n'
                  'const struct RoguePortraitEntry gRoguePortraits[NUM_SPECIES] =\n{\n'
                  '    [SPECIES_TESTMON] = { 5, 5, sPortraitFaces_Testmon, '
                  'ARRAY_COUNT(sPortraitFaces_Testmon) },\n};\n')
    good_data = ('static const u32 sPortraitGfx_Testmon_normal[] = '
                 'INCBIN_U32("graphics/portraits/testmon/normal.4bpp");\n'
                 'static const u16 sPortraitPal_Testmon_normal[] = '
                 'INCBIN_U16("graphics/portraits/testmon/normal.gbapal");\n')

    def build(tmp, table=None, data=None, png=None, pngname='normal.png',
              extra_png=False):
        tmp = Path(tmp)
        (tmp / 'src' / 'data' / 'portraits').mkdir(parents=True, exist_ok=True)
        (tmp / 'graphics' / 'portraits' / 'testmon').mkdir(parents=True, exist_ok=True)
        (tmp / TABLE).write_text(table if table is not None else good_table)
        (tmp / DATA_DIR / 'testmon.h').write_text(data if data is not None else good_data)
        blob = png if png is not None else _png(40, 40, 16, lambda x, y: 1 + (x + y) % 15)
        (tmp / GFX_DIR / 'testmon' / pngname).write_bytes(blob)
        if extra_png:
            (tmp / GFX_DIR / 'testmon' / 'undeclared.png').write_bytes(blob)
        return tmp

    cases = [
        ('index 0 is used by real art',
         dict(png=_png(40, 40, 16, lambda x, y: (x + y) % 16))),
        ('the art is not a whole number of tiles',
         dict(png=_png(44, 40, 16, lambda x, y: 1 + (x + y) % 15))),
        ('the art size does not match the declared tiles',
         dict(png=_png(48, 48, 16, lambda x, y: 1 + (x + y) % 15))),
        ('an undeclared PNG is sitting in the tree',
         dict(extra_png=True)),
        ('a declared face has no PNG',
         dict(pngname='something_else.png')),
        ('gfx and palette are declared for different faces',
         dict(data=good_data.replace('normal.gbapal', 'happy.gbapal', 1))),
        ('the table includes a data file that does not exist',
         dict(table=good_table.replace('testmon.h', 'ghostmon.h', 1))),
        ('a data file exists that nothing includes',
         dict(table=good_table.replace('#include "portraits/testmon.h"\n', '', 1))),
        ('no gRoguePortraits row names the face array',
         dict(table=good_table.replace('[SPECIES_TESTMON] = { 5, 5, '
                                       'sPortraitFaces_Testmon, '
                                       'ARRAY_COUNT(sPortraitFaces_Testmon) },\n', '', 1))),
    ]

    ok = True
    for name, kwargs in cases:
        with tempfile.TemporaryDirectory() as tmp:
            build(tmp, **kwargs)
            if check(tmp):
                print('  SELFTEST FAILED (%s): the check still passed' % name)
                ok = False
            else:
                print('  selftest ok: fired on "%s"' % name)

    # And the fixture itself must pass, or every case above is vacuous.
    with tempfile.TemporaryDirectory() as tmp:
        build(tmp)
        if not check(tmp):
            print('  SELFTEST FAILED: the unmutated fixture does not pass, so '
                  'every case above may be firing for the wrong reason')
            ok = False
        else:
            print('  selftest ok: the unmutated fixture passes')
    return ok


def main():
    args = list(sys.argv[1:])
    is_selftest = '--selftest' in args
    if is_selftest:
        args.remove('--selftest')
    # Accepts the repo both positionally and via --repo, so it cannot land on
    # the wrong side of run_all_checks.sh's hand-maintained list.
    if '--repo' in args:
        i = args.index('--repo')
        if i + 1 >= len(args):
            print('--repo needs a path')
            return 2
        repo = args[i + 1]
    else:
        repo = args[0] if args else '.'

    if is_selftest:
        print('--- selftest: breaking the rule on purpose ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1
    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
