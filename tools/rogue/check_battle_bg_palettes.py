"""Does every battle background ask for a palette bank the engine actually fills?

THE BUG THIS EXISTS FOR. battle_bg.c loads an environment's palette with

    LoadPalette(info.palette, BG_PLTT_ID(2), 3 * PLTT_SIZE_4BPP)

so a background's own 48 colours land in BG palette banks 2, 3 and 4. Banks 0 and
1 are something else entirely - BattleUI_GetTextboxPalette, loaded at
BG_PLTT_ID(0) - and a tilemap entry's top nibble names the bank that tile draws
with.

BANK 0 IS NOT WRONG BY ITSELF, and that is the trap. Every stock background uses
it: they park 1024 entries of blank tile 0 there for the unused half of the 64x32
tilemap and put all their art on 2 and 3. So "uses bank 0" is normal and cannot
be the test.

What IS wrong is a background that references NONE of its own banks, because then
its palette is loaded and never used and every tile draws in the textbox's
colours. Three shipped that way, with all their art on bank 0. The art was right,
the .pal was right, the table entry was right, and the only disagreement was
between a tilemap and a load call in another file - nothing a build could see.

Converters are the usual source. Exporting a single-palette image gives every
tile bank 0, which is correct for a standalone image and wrong for a battle
background, and there is no step in between that would notice.

Checks every environment that ships its own tilemap, so a background added
tomorrow is covered without anyone remembering this file exists.

Run:  python3 tools/rogue/check_battle_bg_palettes.py            # report
      python3 tools/rogue/check_battle_bg_palettes.py --write    # repoint them
"""
import re
import struct
import sys
from pathlib import Path

# From battle_bg.c: BG_PLTT_ID(2), three 16-colour banks.
FIRST_BANK = 2
BANK_COUNT = 3
VALID = tuple(range(FIRST_BANK, FIRST_BANK + BANK_COUNT))

GFX_HEADER = 'src/data/graphics/battle_environment.h'


def tilemaps(repo):
    """-> [(name, path)] for every environment tilemap the header names."""
    text = (repo / GFX_HEADER).read_text(errors='replace')
    out = []
    for m in re.finditer(r'gBattleEnvironmentTilemap_(\w+)\[\]\s*=\s*INCGFX_U32\("([^"]+)"',
                         text):
        out.append((m.group(1), repo / m.group(2)))
    if not out:
        sys.exit('parsed no tilemaps out of %s' % GFX_HEADER)
    return out


def banks(path):
    raw = path.read_bytes()
    n = len(raw) // 2
    entries = struct.unpack('<%dH' % n, raw[:n * 2])
    return entries, sorted({e >> 12 for e in entries})


def main(argv):
    repo = Path('.')
    for i, a in enumerate(argv):
        if a == '--repo':
            repo = Path(argv[i + 1])

    write = '--write' in argv
    failed = False

    print('battle backgrounds load into BG palette banks %s'
          % ', '.join(str(b) for b in VALID))

    for name, path in tilemaps(repo):
        if not path.exists():
            print('  %-18s MISSING %s' % (name, path))
            failed = True
            continue

        entries, used = banks(path)
        own = [b for b in used if b in VALID]

        if own:
            print('  %-18s banks %s  ok' % (name, used))
            continue

        print('  %-18s banks %s  <-- references NONE of %s, so its own palette '
              'is never used' % (name, used, list(VALID)))

        if not write:
            failed = True
            continue

        # Only a single-bank tilemap can be repointed automatically: it maps
        # cleanly onto the first loaded bank. A tilemap spread over several wrong
        # banks needs someone to decide which of its palettes is which, and
        # guessing that would be worse than reporting it.
        if len(used) != 1:
            print('  %-18s      uses %d banks - not repointing automatically'
                  % ('', len(used)))
            failed = True
            continue

        fixed = bytearray()
        for e in entries:
            fixed += struct.pack('<H', (e & 0x0FFF) | (FIRST_BANK << 12))
        path.write_bytes(bytes(fixed))
        print('  %-18s      repointed bank %d -> %d' % ('', used[0], FIRST_BANK))

    if failed:
        raise SystemExit('battle background tilemaps name banks that are never '
                         'loaded - re-run with --write')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
