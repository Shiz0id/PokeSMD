"""
Every species in a theme's pool has to be reachable, and every pool has to be
long enough to fill its window.

WHY THIS EXISTS. A theme's pool is not read as a whole. BuildWildEncounterTable
takes a sliding window of at most DUNGEON_ENCOUNTER_WINDOW entries ending at

    tiers = STARTING_TIER + floorWithinDungeon / TIER_FLOORS

clamped to the pool length, and deals `slots` entries round-robin out of it. So
a pool longer than the deepest tier its dungeon reaches has a TAIL NOTHING CAN
ROLL, and a pool shorter than the window repeats species across the floor's
slots. Neither fails to build, neither crashes, and neither is visible without
counting - the floor just quietly offers less than it looks like it should.

This is the check for a bug that had already happened: when the ramp keyed off
the ABSOLUTE floor rather than the floor within the dungeon, 63 curated species
across the fourteen themes could never appear, eleven of them in Underwater
alone.

Model mirrors the C exactly; if that drifts, this fails rather than lying.

Run:  python check_species_pools.py [--repo PATH] [-v]
"""
import argparse
import os
import re
import sys
from pathlib import Path

LAND_SLOTS, WATER_SLOTS = 12, 5


def const(text, name):
    m = re.search(rf'#define\s+{name}\s+(\d+)', text)
    if not m:
        raise SystemExit(f'{name} not found - the header has moved')
    return int(m.group(1))


def parse(repo):
    src = (repo / 'src/rogue_dungeon.c').read_text(encoding='utf-8', errors='replace')
    hdr = (repo / 'include/rogue_dungeon.h').read_text(encoding='utf-8', errors='replace')

    cfg = dict(
        start=const(hdr, 'DUNGEON_ENCOUNTER_STARTING_TIER'),
        step=const(hdr, 'DUNGEON_ENCOUNTER_TIER_FLOORS'),
        window=const(hdr, 'DUNGEON_ENCOUNTER_WINDOW'),
        long=const(hdr, 'DUNGEON_LONG_FLOORS'),
        short=const(hdr, 'DUNGEON_SHORT_FLOORS'),
        gym=const(hdr, 'DUNGEON_GYM_DUNGEONS'),
        e4=const(hdr, 'DUNGEON_E4_DUNGEONS'),
    )

    #  Split into per-theme initialisers so wildArea is read from the SAME block
    #  as the species pointer. Reading it from the preceding text picks up the
    #  previous theme's value, which silently mislabels the two water themes.
    try:
        table = src.split('sDungeonThemes[DUNGEON_THEME_COUNT] =', 1)[1]
    except IndexError:
        raise SystemExit('sDungeonThemes not found - the table has been renamed')

    themes = []
    for block in re.findall(r'\{(.*?)\n    \},', table, re.S):
        m = re.search(r'\.species = s(\w+?)Species,', block)
        if not m:
            continue
        name = m.group(1)
        water = bool(re.search(r'\.wildArea\s*=\s*WILD_AREA_WATER', block))
        pool = re.search(
            rf'static const u16 s{name}Species\[\]\s*=\s*\{{(.*?)\}};', src, re.S)
        if not pool:
            raise SystemExit(f's{name}Species not found')
        species = re.findall(r'SPECIES_(\w+)', pool.group(1))
        themes.append(dict(name=name, water=water, species=species))
    return cfg, themes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=os.environ.get(
        'POKEDECOMP_REPO', str(Path(__file__).resolve().parents[2])))
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()

    cfg, themes = parse(Path(args.repo))
    expected = cfg['gym'] + cfg['e4'] + 1
    if len(themes) != expected:
        raise SystemExit(f'parsed {len(themes)} themes, expected {expected}')

    fails = []
    for i, t in enumerate(themes):
        #  Dungeon length decides how far the ramp climbs for this theme.
        length = cfg['long'] if (i < cfg['gym'] or i == expected - 1) else cfg['short']
        slots = WATER_SLOTS if t['water'] else LAND_SLOTS
        have = len(t['species'])

        reachable, widths = set(), set()
        for f in range(length):
            tiers = min(cfg['start'] + f // cfg['step'], have)
            bottom = max(0, tiers - cfg['window'])
            width = tiers - bottom
            widths.add(width)
            reachable |= set(range(bottom, bottom + width))

        dead = [t['species'][j] for j in range(have) if j not in reachable]
        #  A duplicate is not automatically wrong - but inside one window it
        #  costs a slot for nothing, which is never intended.
        dupes = {s for s in t['species'] if t['species'].count(s) > 1}

        if dead:
            fails.append(f'{t["name"]}: {len(dead)} unreachable species '
                         f'(pool is {have}, deepest tier reaches '
                         f'{min(cfg["start"] + (length - 1) // cfg["step"], have)}) '
                         f'-> {", ".join(s.title() for s in dead)}')
        if max(widths) < cfg['window']:
            fails.append(f'{t["name"]}: pool of {have} cannot fill the window of '
                         f'{cfg["window"]} - a floor offers only {max(widths)} '
                         f'species across {slots} slots, so it repeats')
        if dupes:
            fails.append(f'{t["name"]}: duplicate species in one pool -> '
                         f'{", ".join(s.title() for s in sorted(dupes))}')

        if args.verbose:
            print(f'{i:>2} {t["name"]:14s} {"water" if t["water"] else "land ":5s} '
                  f'{length:>2}fl  pool {have:>2}  reachable {len(reachable):>2}  '
                  f'{min(widths)}-{max(widths)} per floor of {cfg["window"]}')

    if fails:
        print(f'FAIL: {len(fails)} problem(s)')
        for f in fails:
            print(f'  {f}')
        return 1

    print(f'OK: {len(themes)} themes, every species reachable, '
          f'every pool fills the window of {cfg["window"]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
