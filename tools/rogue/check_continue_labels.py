"""Does the continue screen's right column still fit after relabelling BADGES?

The main menu draws its save info as two label/value pairs per row, with the
label at a fixed x and the value RIGHT-aligned to a fixed x. Nothing checks
that they do not meet: a label too long for its column silently draws under
the number instead of erroring, and the only way to see it is to boot the ROM
and look at the save-select screen.

Measures real glyph widths out of gFontNormalLatinGlyphWidths rather than
assuming a fixed advance, because FONT_NORMAL is proportional - 'I' and 'W'
are not the same width and a 7-character label is not necessarily wider than
a 6-character one.

Usage:  python3 tools/rogue/check_continue_labels.py [--repo PATH]
"""
import argparse
import os
import re
import sys
from pathlib import Path

# From MainMenu_FormatSavegame* in src/main_menu.c. label_x, value_right_edge.
LEFT_COL = (0, 100)
RIGHT_COL = (0x6C, 0xD0)

# (label, widest value it must never collide with, column, where it is drawn)
CASES = [
    ('PLAYER',  'PLAYERNAME', LEFT_COL,  'row 1 left'),
    ('TIME',    '999 59',     RIGHT_COL, 'row 1 right'),
    ('POKEDEX', '9999',       LEFT_COL,  'row 2 left'),
    ('CLEARED', '65535',      RIGHT_COL, 'row 2 right'),
    ('BADGES',  '8',          RIGHT_COL, 'row 2 right (old, for comparison)'),
]


def glyph_widths(repo):
    text = (repo / 'src/fonts.c').read_text(errors='replace')
    m = re.search(r'gFontNormalLatinGlyphWidths\[\]\s*=\s*\{(.*?)\};', text, re.S)
    if not m:
        sys.exit('could not find gFontNormalLatinGlyphWidths in src/fonts.c')
    body = re.sub(r'//[^\n]*', '', m.group(1))
    vals = [int(v) for v in re.findall(r'\b(\d+)\b', body)]
    return vals


def charmap(repo):
    """ASCII char -> the game's encoded byte, from charmap.txt.

    The width table is indexed by the ENCODED byte, not by ASCII: 'A' is 0xBB
    here, not 0x41. Indexing by ord() reads a completely unrelated glyph and
    produces plausible-looking totals that are simply wrong - it made this
    script report a 6-character label as 23px, about four pixels per glyph,
    which no proportional font of this size does.
    """
    out = {}
    for line in (repo / 'charmap.txt').read_text(errors='replace').splitlines():
        m = re.match(r"^'(.)'\s*=\s*([0-9A-Fa-f]{2})\s*$", line)
        if m:
            out.setdefault(m.group(1), int(m.group(2), 16))
    return out


def width_of(s, widths, cmap):
    total = 0
    for ch in s:
        code = cmap.get(ch)
        if code is None:
            sys.exit(f'no charmap entry for {ch!r} - cannot measure {s!r} honestly')
        if code >= len(widths):
            sys.exit(f'{ch!r} encodes to {code:#x}, past the {len(widths)}-entry width table')
        total += widths[code]
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=os.environ.get('POKEDECOMP_REPO', '.'))
    args = ap.parse_args()
    repo = Path(args.repo).resolve()

    widths = glyph_widths(repo)
    cmap = charmap(repo)
    print(f'glyph width table: {len(widths)} entries, '
          f'charmap: {len(cmap)} single-char entries')
    print(f"sanity: 'A' encodes to {cmap['A']:#04x}, width {widths[cmap['A']]}px\n")

    hdr = f'{"label":<9} {"lbl w":>6} {"value":<11} {"val w":>6} {"gap":>5}  {"where"}'
    print(hdr)
    print('-' * (len(hdr) + 8))

    failures = 0
    for label, value, (lx, vright), where in CASES:
        lw = width_of(label, widths, cmap)
        vw = width_of(value, widths, cmap)
        value_left = vright - vw
        gap = value_left - (lx + lw)
        flag = ''
        if gap < 0:
            flag = '   OVERLAP'
            if 'old' not in where:
                failures += 1
        elif gap < 4:
            flag = '   tight'
        print(f'{label:<9} {lw:>6} {value:<11} {vw:>6} {gap:>5}  {where}{flag}')

    print()
    print(f'{"FAILED" if failures else "ok"}: {failures} label(s) collide with their value')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
