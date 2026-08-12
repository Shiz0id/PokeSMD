#!/usr/bin/env python3
"""New Mauville's wall is a two-metatile assembly. Check that both rows land.

Two separate defects, same root cause - the theme painted one row of something
vanilla builds out of two:

  the wall face   0x28C over 0x294 8/8, 0x28B over 0x293 8/8, and a face with
                  wall above it capped 35/35. Without the cap, a partition
                  ending in open floor is one metatile of wall on nothing.
  the column      0x290 body, 0x298 shoulder, 0x2A0 flat brown base on the
                  floor, 3/3 over one-wide runs. The theme ended at the
                  shoulder, so the block meeting the floor was never drawn.

Neither can be found from a cell's own eight neighbours - a cap's are all wall,
exactly like deep interior - so neither is something the mask chain can express.
This check therefore derives what must be painted from the CARVE alone and holds
the painted grid against that. It does not re-use the dispatch it is checking:
reorder ApplyWallAutotiling so a branch stops being reached and this fires.

**Two fixtures, because one of them does not exercise the column at all.**
DUNGEON_GEN_FACILITY sets FACILITY_VTHICK 2, so a facility floor contains no
one-wide vertical run and the column slots would be guarded vacuously by it.
The second fixture is vanilla's own NewMauville_Inside collision, which is full
of them - 19 SLIVER_VERT, 3 bases, 3 shoulders - and is the shape any new carve
is aiming at anyway.

What it verifies, per instance and never as an aggregate:

  1. rogue_dungeon.c assigns every slot of both assemblies
  2. theme_mock.py's transcription of that table still agrees with the C
  3. every cell the carve says must be a cap is painted with the right one
  4. every painted cap sits directly above its own face, over open floor
  5. the ids are distinct from every other slot, or 3 and 4 pass vacuously
  6. every one-wide column ends base-over-shoulder, on the vanilla fixture
  7. each was actually placed at least once, or the check proved nothing

Run: python3 check_wall_caps.py [--repo PATH] [--seeds N]
"""
import argparse
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import check_facility_floor as F
import theme_mock as tm

THEME = 'newmauville'
CAP_SLOTS = ('CAP_LEFT', 'CAP_MID', 'CAP_RIGHT')
COLUMN_SLOTS = ('SLIVER_VERT', 'SLIVER_VERT_BOT', 'SLIVER_VERT_BOT_UPPER')
FACE_OF = {'CAP_LEFT': 'FACE_LEFT', 'CAP_MID': 'FACE_MID',
           'CAP_RIGHT': 'FACE_RIGHT'}
VANILLA_LAYOUT = 'NewMauville_Inside_Layout'


def metatile_constants(repo):
    """NEWMAUVILLE_METATILE_* -> id, from the header rather than from memory."""
    path = os.path.join(repo, 'include', 'rogue_dungeon.h')
    with open(path, encoding='utf-8') as fh:
        src = fh.read()
    return {m.group(1): int(m.group(2), 16) for m in
            re.finditer(r'^#define\s+(NEWMAUVILLE_METATILE_\w+)\s+(0x[0-9A-Fa-f]+)',
                        src, re.M)}


def c_wall_table(repo, theme_enum):
    """The .wall designated initialisers for one theme, as slot -> id."""
    path = os.path.join(repo, 'src', 'rogue_dungeon.c')
    with open(path, encoding='utf-8') as fh:
        src = fh.read()
    start = src.find('[%s] =' % theme_enum)
    if start < 0:
        return None
    wall = src.find('.wall', start)
    end = src.find('.skirts', wall)
    if wall < 0 or end < 0:
        return None
    consts = metatile_constants(repo)
    out = {}
    for m in re.finditer(r'\[WALL_(\w+)\]\s*=\s*(\w+)', src[wall:end]):
        slot, name = m.group(1), m.group(2)
        if name in consts:
            out[slot] = consts[name]
    return out


def required_caps(grid, wall):
    """Which cells the carve says must be caps, derived from geometry alone.

    Mirrors WallCapFor's rule, NOT the dispatch chain around it: this cell and
    the one below are wall, the cell two below is floor, the cell below is not
    a sliver, and this cell does not itself have floor to the north (vanilla
    never caps a wall it can see over the top of).
    """
    W, H = F.W, F.H

    def solid(x, y):
        return True if not (0 <= x < W and 0 <= y < H) else bool(grid[y][x])

    want = {}
    for y in range(H):
        for x in range(W):
            if not solid(x, y) or not solid(x, y + 1) or solid(x, y + 2):
                continue
            if not solid(x, y - 1):
                continue                      # floor above: keeps NORTH art
            westOpen, eastOpen = not solid(x - 1, y + 1), not solid(x + 1, y + 1)
            if westOpen and eastOpen:
                continue                      # below is a sliver, not a face
            slot = ('CAP_LEFT' if westOpen else
                    'CAP_RIGHT' if eastOpen else 'CAP_MID')
            want[(x, y)] = slot
    return want


def vanilla_carve(repo):
    """A vanilla layout's collision, as a solid grid. No generator involved -
    this is the shape the theme's art was drawn for, so it is the one fixture
    guaranteed to contain every case the tileset has art for."""
    path = os.path.join(repo, 'data', 'layouts', 'layouts.json')
    with open(path, encoding='utf-8') as fh:
        data = json.load(fh)
    for e in (data['layouts'] if isinstance(data, dict) else data):
        if e and e.get('name') == VANILLA_LAYOUT:
            w, h = e['width'], e['height']
            with open(os.path.join(repo, e['blockdata_filepath']), 'rb') as fh:
                b = struct.unpack('<%dH' % (w * h), fh.read(w * h * 2))
            return w, h, [[((b[y * w + x] >> 10) & 3) != 0 for x in range(w)]
                          for y in range(h)]
    return None


def column_problems(solid, painted, wall, w, h):
    """Every one-wide vertical run that ends in floor must read
    ... body / shoulder / base, with the base on the floor."""
    def is_wall(x, y):
        return True if not (0 <= x < w and 0 <= y < h) else solid[y][x]

    def sliver(x, y):
        return (is_wall(x, y) and not is_wall(x - 1, y)
                and not is_wall(x + 1, y))

    bad, bases, shoulders = [], 0, 0
    for y in range(h):
        for x in range(w):
            if not sliver(x, y) or is_wall(x, y + 1):
                continue
            if not is_wall(x, y - 1):
                continue                      # a lone block, the ISOLATED slot
            bases += 1
            if painted[y][x] != wall['SLIVER_VERT_BOT']:
                bad.append('(%d,%d): column base is 0x%03X, wants 0x%03X'
                           % (x, y, painted[y][x], wall['SLIVER_VERT_BOT']))
            if sliver(x, y - 1) and is_wall(x, y - 2):
                shoulders += 1
                if painted[y - 1][x] != wall['SLIVER_VERT_BOT_UPPER']:
                    bad.append('(%d,%d): shoulder is 0x%03X, wants 0x%03X'
                               % (x, y - 1, painted[y - 1][x],
                                  wall['SLIVER_VERT_BOT_UPPER']))
    return bad, bases, shoulders


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=os.path.join(HERE, '..', '..'))
    ap.add_argument('--seeds', type=int, default=200)
    a = ap.parse_args()
    repo = os.path.abspath(a.repo)

    problems = []
    wall = tm.THEMES[THEME]['wall']

    # 1 and 2: the C table, and the mock's copy of it
    c = c_wall_table(repo, 'DUNGEON_THEME_NEWMAUVILLE')
    if c is None:
        problems.append('could not read the New Mauville wall table out of '
                        'src/rogue_dungeon.c')
    else:
        for slot in CAP_SLOTS:
            if slot not in c:
                problems.append('rogue_dungeon.c does not assign WALL_%s, so '
                                'the wall is one metatile tall again' % slot)
        for slot in COLUMN_SLOTS:
            if slot not in c:
                problems.append('rogue_dungeon.c does not assign WALL_%s, so '
                                'the column stands on nothing again' % slot)
        for slot, mid in sorted(c.items()):
            if slot in wall and wall[slot] != mid:
                problems.append('theme_mock has %s = 0x%03X, the C has 0x%03X'
                                % (slot, wall[slot], mid))

    missing = [s for s in CAP_SLOTS + COLUMN_SLOTS if not wall.get(s)]
    if missing:
        problems.append('theme_mock has no %s' % ', '.join(missing))
    if problems:
        for p in problems:
            print('FAIL %s' % p)
        return 1

    # 5: a cap id shared with another slot would make 3 and 4 meaningless
    for slot in CAP_SLOTS:
        clash = [k for k, v in wall.items()
                 if v == wall[slot] and k not in CAP_SLOTS]
        if clash:
            problems.append('%s (0x%03X) is also %s - this check cannot tell '
                            'them apart' % (slot, wall[slot], ', '.join(clash)))

    # ApplyDecor runs after the wall pass and swaps a face for a variant of
    # itself, so a cap legitimately sits over one of those too. Note that the
    # variants have caps of their OWN in vanilla (the bookcase's is 0x299/0x29A,
    # and the vent is only ever placed on a one-thick partition at all) - that
    # is the three-row decoration gap in docs/NEWMAUVILLE_TILESET.md, a separate
    # defect from this one. Counted below rather than failed on, so this check
    # keeps meaning exactly one thing.
    decorated = {}
    for base, variant, variantEast in tm.THEMES[THEME].get('decor', []):
        for v in (variant, variantEast):
            if v:
                decorated.setdefault(base, set()).add(v)

    # A three-row stamp replaces the face with its own, and may replace the cap
    # above it too (the bookcase and the crate shelf carry their own). Both are
    # correct, so both are admitted - but only these, so a cap over anything
    # else still fires.
    stampCaps = set()
    for wd, needsFloor, chance, cp, fc, bl in tm.THEMES[THEME].get('stamps', []):
        for k in range(wd):
            decorated.setdefault(wall['FACE_MID'], set()).add(fc[k])
            if cp[k] is not None:
                stampCaps.add(cp[k])

    placed = 0
    faces = 0
    bad = 0
    overDecor = 0
    for seed in range(1, a.seeds + 1):
        r = F.build(seed, 14, 8, 3)
        if r is None:
            continue
        grid = r[0]
        solid = [[bool(grid[y][x]) for x in range(F.W)] for y in range(F.H)]
        painted, _ = tm.paint(solid, tm.THEMES[THEME], seed)

        want = required_caps(grid, wall)
        faces += len(want)

        # 3: everything that must be capped, is
        for (x, y), slot in want.items():
            got = painted[y][x]
            if got != wall[slot] and got not in stampCaps:
                bad += 1
                if bad <= 6:
                    problems.append(
                        'seed %d (%d,%d): face below wants %s 0x%03X, painted '
                        '0x%03X' % (seed, x, y, slot, wall[slot], got))

        # 4: and nothing else is
        capids = {wall[s]: s for s in CAP_SLOTS if wall[s] not in stampCaps}
        for y in range(F.H):
            for x in range(F.W):
                slot = capids.get(painted[y][x])
                if slot is None:
                    continue
                placed += 1
                below = painted[y + 1][x] if y + 1 < F.H else None
                if want.get((x, y)) != slot:
                    bad += 1
                    if bad <= 6:
                        problems.append(
                            'seed %d (%d,%d): %s painted where the carve wants '
                            '%s' % (seed, x, y, slot, want.get((x, y))))
                    continue
                face = wall[FACE_OF[slot]]
                if below == face:
                    pass
                elif below in decorated.get(face, ()):
                    overDecor += 1
                else:
                    bad += 1
                    if bad <= 6:
                        problems.append(
                            'seed %d (%d,%d): %s sits over 0x%03X, which is '
                            'neither its face 0x%03X nor a decor variant of it'
                            % (seed, x, y, slot, below, face))

    # 6: the column, on the one fixture that contains any. A facility floor has
    # no one-wide vertical run at all (FACILITY_VTHICK is 2), so checking the
    # column against it would pass without ever looking at one.
    van = vanilla_carve(repo)
    bases = shoulders = 0
    if van is None:
        problems.append('could not read %s - the column assembly went '
                        'unchecked' % VANILLA_LAYOUT)
    else:
        vw, vh, vsolid = van
        savedW, savedH = tm.W, tm.H
        tm.W, tm.H = vw, vh
        try:
            vpainted, _ = tm.paint(vsolid, tm.THEMES[THEME], 11)
        finally:
            tm.W, tm.H = savedW, savedH
        bad2, bases, shoulders = column_problems(vsolid, vpainted, wall, vw, vh)
        problems.extend('%s: %s' % (VANILLA_LAYOUT, b) for b in bad2[:6])

    # 7: a check that never saw the thing it checks has proved nothing
    if placed == 0:
        problems.append('no cap was placed across %d floors - this check '
                        'verified nothing' % a.seeds)
    if bases == 0:
        problems.append('no column base was found in %s - the column assembly '
                        'was verified against nothing' % VANILLA_LAYOUT)

    if problems:
        for p in problems[:12]:
            print('FAIL %s' % p)
        return 1

    print('check_wall_caps: %d floors, %d faces needing a cap, %d caps placed, '
          'all matched' % (a.seeds, faces, placed))
    print('  %s: %d column bases, %d shoulders, all matched'
          % (VANILLA_LAYOUT, bases, shoulders))
    if overDecor:
        print('  note: %d of those caps (%.0f%%) sit over a DECORATED face, '
              'which in vanilla carries a cap of its own - the three-row '
              'decoration gap, not this one'
              % (overDecor, 100.0 * overDecor / placed))
    return 0


if __name__ == '__main__':
    sys.exit(main())
