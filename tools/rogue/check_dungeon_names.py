"""The dungeon names, and the three ways they fail without saying so.

EDIT src/data/region_map/region_map_sections.json, NOT THE HEADERS. Both
include/constants/region_map_sections.h and
src/data/region_map/region_map_entries.h are generated from that one JSON by
jsonproc, and both are gitignored. Editing them directly builds, links, runs
and passes this check - and then vanishes on the next regeneration, with
nothing in the commit. `git status` is what catches that; nothing else does.

1. A MAPSEC_ROGUE_* in the enum with no gRegionMapEntries row. GetMapName
   guards with `regionMapId < MAPSEC_NONE` and then indexes that array, which
   is UNSIZED - the two bounds are equal only by convention. A missing row is
   an out-of-bounds read into whatever follows the table, not a build error.
   Going through the JSON makes this one structurally impossible, since one
   list generates both; the check stays because a hand-edited header can still
   reach that state, and because it is the only thing that would notice.

2. A theme that does not set .mapSecId. A designated initialiser leaves it 0,
   and 0 is MAPSEC_LITTLEROOT_TOWN, so the floor announces a town in Hoenn
   rather than announcing nothing.

3. A name too long for the banner. Measured in PIXELS against the widest name
   vanilla actually ships, not against a guessed window width - the popup does
   an unbounded StringCopy, so nothing downstream complains either.

Glyph widths are indexed by the CHARMAP byte, not by ASCII: 'A' is 0xBB here.
Indexing by ord() reads unrelated glyphs and returns plausible nonsense.

Usage:  python3 tools/rogue/check_dungeon_names.py [--repo PATH]
"""
import argparse
import os
import re
import sys
from pathlib import Path

MAP_NAME_LENGTH = 16   # include/region_map.h


def charmap(repo):
    out = {}
    for line in (repo / 'charmap.txt').read_text(errors='replace').splitlines():
        m = re.match(r"^'(.)'\s*=\s*([0-9A-Fa-f]{2})\s*$", line)
        if m:
            out.setdefault(m.group(1), int(m.group(2), 16))
    return out


def glyph_widths(repo):
    text = (repo / 'src/fonts.c').read_text(errors='replace')
    m = re.search(r'gFontNormalLatinGlyphWidths\[\]\s*=\s*\{(.*?)\};', text, re.S)
    body = re.sub(r'//[^\n]*', '', m.group(1))
    return [int(v) for v in re.findall(r'\b(\d+)\b', body)]


def width_of(s, widths, cmap):
    total = 0
    for ch in s:
        code = cmap.get(ch)
        if code is None or code >= len(widths):
            return None
        total += widths[code]
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=os.environ.get('POKEDECOMP_REPO', '.'))
    args = ap.parse_args()
    repo = Path(args.repo).resolve()

    cmap = charmap(repo)
    widths = glyph_widths(repo)

    enum_text = (repo / 'include/constants/region_map_sections.h').read_text(errors='replace')
    declared = re.findall(r'^\s*(MAPSEC_ROGUE_\w+),', enum_text, re.M)

    # Every MAPSEC_ROGUE_* must sit before MAPSEC_NONE, or GetMapName's guard
    # rejects it and the banner silently prints spaces.
    order = re.findall(r'^\s*(MAPSEC_\w+),', enum_text, re.M)
    none_at = order.index('MAPSEC_NONE') if 'MAPSEC_NONE' in order else len(order)

    entries_text = (repo / 'src/data/region_map/region_map_entries.h').read_text(errors='replace')
    rows = dict(re.findall(
        r'\[(MAPSEC_ROGUE_\w+)\]\s*=\s*\{[^}]*?\.name\s*=\s*COMPOUND_STRING\("([^"]*)"\)',
        entries_text, re.S))
    total_rows = len(re.findall(r'^\s*\[MAPSEC_\w+\]\s*=', entries_text, re.M))

    themes_text = (repo / 'src/rogue_dungeon.c').read_text(errors='replace')
    used = re.findall(r'\.mapSecId\s*=\s*(MAPSEC_\w+),', themes_text)
    theme_count = int(re.search(r'DUNGEON_THEME_COUNT\s*\n?\s*\}', themes_text)
                      and len(re.findall(r'^\s*(DUNGEON_THEME_\w+),', themes_text, re.M)) or 0)

    # Widest name vanilla ships, as the empirical bound for the banner.
    vanilla = re.findall(r'\.name\s*=\s*COMPOUND_STRING\("([^"]*)"\)', entries_text)
    vanilla_only = [n for n in vanilla if n not in rows.values()]
    widest = max(((width_of(n, widths, cmap) or 0), n) for n in vanilla_only)

    print(f'declared MAPSEC_ROGUE_*: {len(declared)}')
    print(f'gRegionMapEntries rows : {total_rows} total, {len(rows)} of them rogue')
    print(f'themes naming themselves: {len(used)} of {theme_count}')
    print(f'vanilla widest name     : {widest[1]!r} at {widest[0]}px\n')

    failures = []

    for sec in declared:
        if order.index(sec) > none_at:
            failures.append(f'{sec} is declared AFTER MAPSEC_NONE - GetMapName will not resolve it')
        if sec not in rows:
            failures.append(f'{sec} has no gRegionMapEntries row - GetMapName would read out of bounds')

    for sec in used:
        if sec not in declared:
            failures.append(f'a theme uses {sec}, which is not a declared MAPSEC_ROGUE_*')
    if len(used) != theme_count:
        failures.append(f'{theme_count - len(used)} theme(s) do not set .mapSecId - they would name '
                        'MAPSEC_LITTLEROOT_TOWN')
    for sec in declared:
        if sec not in used:
            failures.append(f'{sec} is declared and named but no theme uses it')

    seen = {}
    hdr = f'{"section":<26} {"name":<18} {"len":>4} {"px":>4}'
    print(hdr)
    print('-' * (len(hdr) + 10))
    for sec in declared:
        name = rows.get(sec)
        if name is None:
            print(f'{sec:<26} {"-- NO ROW --":<18}')
            continue
        w = width_of(name, widths, cmap)
        note = ''
        if len(name) > MAP_NAME_LENGTH:
            note = f'   TOO LONG (> {MAP_NAME_LENGTH} chars)'
            failures.append(f'{sec}: {name!r} is {len(name)} chars, over MAP_NAME_LENGTH')
        if w is None:
            note = '   UNENCODABLE'
            failures.append(f'{sec}: {name!r} has a character with no charmap entry')
        elif w > widest[0]:
            note = f'   WIDER THAN ANY VANILLA NAME ({widest[0]}px)'
            failures.append(f'{sec}: {name!r} is {w}px, wider than vanilla\'s widest {widest[0]}px')
        if name in seen:
            note += f'   DUPLICATE of {seen[name]}'
            failures.append(f'{sec}: {name!r} duplicates {seen[name]}')
        seen[name] = sec
        print(f'{sec:<26} {name:<18} {len(name):>4} {w if w is not None else "?":>4}{note}')

    print()
    for f in failures:
        print(f'  FAIL: {f}')
    print(f'{"FAILED" if failures else "ok"}: {len(failures)} problem(s)')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
