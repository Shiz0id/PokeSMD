"""Check every dungeon theme can actually spawn wild Pokemon.

A metatile id means something different under every tileset pair, so a theme
whose floor id is right for one pair can silently resolve to unrelated art -
and to an unrelated BEHAVIOUR - under its own. Behaviour is what gates
encounters: MB_CAVE and MB_INDOOR_ENCOUNTER carry TILE_FLAG_HAS_ENCOUNTERS,
MB_NORMAL does not.

A theme passes if its floor carries encounters, or - for a grass theme like the
woods, where plain ground is deliberately safe - if its tall or long grass does.

Two separate things are checked, because the bug that motivated this file was a
divergence BETWEEN them:

1. The theme table is sane - every theme paints something encounterable.
2. The generator actually reads the table. CarveFloor used to hard-code
   DUNGEON_METATILE_FLOOR for every cave theme, so the table could be perfect
   and the game still paint Lavaridge 0x201 - a green bush with MB_NORMAL -
   giving Fiery Path ten floors with no wild Pokemon. Check 1 cannot see that,
   and neither could theme_mock.py, which fills the grid from theme['floor']
   and so rendered the table's intent rather than the game's behaviour.

Usage:  python3 tools/rogue/check_encounter_flags.py
"""
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tileset_resolve import TilesetResolver

REPO = Path(__file__).resolve().parents[2]


def behaviour_names():
    src = (REPO / 'include/constants/metatile_behaviors.h').read_text(errors='replace')
    body = src[src.index('enum {') + 6:src.index('};')]
    names, val = {}, 0
    for line in body.split(','):
        line = re.sub(r'//.*', '', line).strip()
        if not line:
            continue
        if '=' in line:
            name, raw = line.split('=')
            name, val = name.strip(), int(raw.strip(), 0)
        else:
            name = line
        names[val] = name
        val += 1
    return names


def encounter_behaviours():
    """Behaviour names carrying TILE_FLAG_HAS_ENCOUNTERS."""
    text = (REPO / 'src/metatile_behavior.c').read_text(errors='replace')
    body = re.search(r'sTileBitAttributes\[[^\]]*\]\s*=\s*\{(.*?)\n\};', text, re.S).group(1)
    return {m.group(1) for m in
            re.finditer(r'\[(MB_\w+)\]\s*=\s*([^,\n]*)', body)
            if 'HAS_ENCOUNTERS' in m.group(2)}


def defines():
    """Metatile #defines from the roguelike header."""
    text = (REPO / 'include/rogue_dungeon.h').read_text(errors='replace')
    return {m.group(1): int(m.group(2), 0) for m in
            re.finditer(r'#define\s+(\w+)\s+(0x[0-9A-Fa-f]+)\b', text)}


def themes(defs):
    """Parse sDungeonThemes into {theme name: {field: value}}."""
    text = (REPO / 'src/rogue_dungeon.c').read_text(errors='replace')
    block = re.search(r'sDungeonThemes\[DUNGEON_THEME_COUNT\]\s*=\s*\{(.*?)\n\};',
                      text, re.S).group(1)
    out = {}
    parts = re.split(r'\[(DUNGEON_THEME_\w+)\]\s*=', block)
    for name, body in zip(parts[1::2], parts[2::2]):
        entry = {}
        for field in ('layoutId', 'floor', 'tallGrass', 'longGrass'):
            m = re.search(r'\.' + field + r'\s*=\s*([A-Za-z0-9_]+)', body)
            if not m:
                continue
            token = m.group(1)
            entry[field] = (int(token, 0)
                            if re.fullmatch(r'0[xX][0-9A-Fa-f]+|\d+', token)
                            else defs.get(token, token))
        out[name] = entry
    return out


def layout_tilesets():
    data = json.loads((REPO / 'data/layouts/layouts.json').read_text(encoding='utf-8'))
    return {L['id']: (L['primary_tileset'], L['secondary_tileset'])
            for L in data['layouts']}


def behaviour_of(resolver, primary, secondary, metatile):
    """Metatile behaviour under a tileset pair, or None if out of range."""
    symbol = primary if metatile < 0x200 else secondary
    index = metatile if metatile < 0x200 else metatile - 0x200
    attrs = resolver.resolve(symbol)['attributes'].read_bytes()
    if (index + 1) * 2 > len(attrs):
        return None
    return struct.unpack_from('<H', attrs, index * 2)[0] & 0xFF


def strip_file_scope_tables(text):
    """Blank out every `static const ... = { ... };` initialiser.

    What is left is function bodies. Themed metatile constants belong in the
    tables - that is what a table is - and must never appear in a function,
    because a function runs under every theme's tileset.
    """
    out = []
    pos = 0
    for m in re.finditer(r'static const [^;{]*?=\s*\{', text):
        if m.start() < pos:
            continue
        depth, i = 0, m.end() - 1
        while i < len(text):
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        out.append(text[pos:m.start()])
        pos = i
    out.append(text[pos:])
    return ''.join(out)


def check_generator_reads_the_table():
    """No themed metatile constant may appear inside a function.

    A source lint rather than a simulation, but it targets the exact regression:
    CarveFloor named DUNGEON_METATILE_FLOOR directly, so every cave theme got
    the cave's floor id under its own tileset. Anything a generator paints has
    to come from the theme it was handed.
    """
    text = (REPO / 'src/rogue_dungeon.c').read_text(errors='replace')
    bodies = re.sub(r'//[^\n]*', '', strip_file_scope_tables(text))

    bad = {name for name in
           re.findall(r'\b([A-Z][A-Z0-9_]*_METATILE_[A-Z0-9_]+)\b', bodies)
           if not name.startswith('MAPGRID_')}   # an engine mask, not a metatile
    for name in sorted(bad):
        print(f'    FAIL: {name} named inside a function')
    return len(bad)


def main():
    defs = defines()
    names = behaviour_names()
    with_encounters = encounter_behaviours()
    tilesets = layout_tilesets()
    resolver = TilesetResolver(REPO)

    failures = 0
    for theme, entry in themes(defs).items():
        layout = entry.get('layoutId')
        if layout not in tilesets:
            print(f'{theme}: cannot resolve layout {layout}')
            failures += 1
            continue
        primary, secondary = tilesets[layout]
        print(f'{theme}  ({secondary})')

        ok = False
        for field in ('floor', 'tallGrass', 'longGrass'):
            metatile = entry.get(field)
            if not isinstance(metatile, int) or metatile == 0:
                continue
            beh = behaviour_of(resolver, primary, secondary, metatile)
            name = names.get(beh, '?') if beh is not None else 'OUT OF RANGE'
            hit = name in with_encounters
            ok = ok or hit
            print(f'    {field:10} 0x{metatile:03X}  {name:22}'
                  f'  {"ENCOUNTERS" if hit else "-"}')

        if not ok:
            print('    FAIL: nothing this theme paints can spawn a wild Pokemon')
            failures += 1

    print()
    print('generator reads the theme table')
    hard_coded = check_generator_reads_the_table()
    if not hard_coded:
        print('    no themed metatile hard-coded outside the table')

    print()
    total = failures + hard_coded
    print(f'{"FAILED" if total else "ok"}: '
          f'{failures} theme(s) with no encounter surface, '
          f'{hard_coded} hard-coded metatile(s)')
    return 1 if total else 0


if __name__ == '__main__':
    raise SystemExit(main())
