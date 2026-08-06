"""
Guard a theme's own trainer table against the two ways it can silently rot.

A theme that brings its own trainers has to keep two files agreeing, and
neither the compiler nor any existing check looks at both:

  src/rogue_dungeon.c   sUnderwaterTrainers - trainerId, overworld gfx, level
  src/data/trainers.party                   - that trainer's Pic and Gender

1. THE SPRITE AND THE PIC MUST BE THE SAME PERSON. This is the whole reason a
   theme entry carries its own gfxId. Before that, the overworld sprite came
   from alternating theme->trainerGfx by index parity while the trainer came
   from a level match - two independent choices - so a diver drawn female could
   open the battle as a man. Nothing crashes when they disagree; the player just
   sees one person become another.

2. THE POOL MUST BE DEEP ENOUGH AT EVERY FLOOR. PlaceTrainers wants
   1 + floor/DUNGEON_TRAINER_FLOORS_PER_EXTRA opponents, capped at
   DUNGEON_MAX_TRAINERS, and it requires them DISTINCT - a repeat is skipped and
   that slot is simply lost, because the defeat flag is derived from the trainer
   id. PickTrainerForLevel opens with a window of +/-3 around the floor's
   target, so if fewer entries than that fall inside the window the floor
   quietly comes up short of trainers rather than failing.

Run:  python3 tools/rogue/check_theme_trainers.py
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

WINDOW = 3          # PickTrainerForLevel's opening window


def constants():
    """These are split across two headers, so read both rather than guess
    which one owns a given name."""
    text = ''
    for h in ('include/constants/rogue_dungeon.h', 'include/rogue_dungeon.h'):
        text += (REPO / h).read_text()
    out = {}
    wanted = ('DUNGEON_MAX_TRAINERS', 'DUNGEON_TRAINER_FLOORS_PER_EXTRA',
              'DUNGEON_ENCOUNTER_BASE_LEVEL', 'DUNGEON_ENCOUNTER_LEVEL_NUM',
              'DUNGEON_ENCOUNTER_LEVEL_DEN', 'DUNGEON_LONG_FLOORS')
    for name in wanted:
        m = re.search(r'#define\s+' + name + r'\s+(\d+)', text)
        if m:
            out[name] = int(m.group(1))
    missing = [n for n in wanted if n not in out]
    if missing:
        raise SystemExit('could not read ' + ', '.join(missing) +
                         ' - have they been renamed or made expressions?')
    return out


def theme_tables():
    """name -> [(trainerId, gfxId, avgLevel)] for every RogueThemeTrainer table."""
    text = (REPO / 'src/rogue_dungeon.c').read_text()
    tables = {}
    for m in re.finditer(
            r'static const struct RogueThemeTrainer (\w+)\[\] =\s*\{(.*?)\n\};',
            text, re.S):
        rows = re.findall(r'\{\s*(\w+)\s*,\s*(\w+)\s*,\s*(\d+)\s*\}',
                          m.group(2))
        tables[m.group(1)] = [(a, b, int(c)) for a, b, c in rows]
    return tables


def party_pics():
    """TRAINER_X -> (pic, gender) from trainers.party."""
    text = (REPO / 'src/data/trainers.party').read_text(encoding='utf-8')
    out = {}
    for block in text.split('=== ')[1:]:
        name = block.split(' ===')[0].strip()
        pic = re.search(r'^Pic:\s*(.+)$', block, re.M)
        gen = re.search(r'^Gender:\s*(.+)$', block, re.M)
        out[name] = (pic.group(1).strip() if pic else None,
                     gen.group(1).strip() if gen else None)
    return out


def gfx_gender(gfx):
    if gfx.endswith('_M'):
        return 'Male'
    if gfx.endswith('_F'):
        return 'Female'
    return None


def pic_gender(pic):
    if pic is None:
        return None
    if pic.endswith(' M'):
        return 'Male'
    if pic.endswith(' F'):
        return 'Female'
    return None


def floor_target(floor, k):
    return (k['DUNGEON_ENCOUNTER_BASE_LEVEL']
            + floor * k['DUNGEON_ENCOUNTER_LEVEL_NUM']
            // k['DUNGEON_ENCOUNTER_LEVEL_DEN'])


def main():
    k = constants()
    tables = theme_tables()
    party = party_pics()
    bad = []

    if not tables:
        print('no RogueThemeTrainer tables found - has the struct been renamed?')
        return 1

    for name, rows in tables.items():
        # 1. sprite and pic agree
        for tid, gfx, _lvl in rows:
            pic, gender = party.get(tid, (None, None))
            if pic is None:
                bad.append(f'{name}: {tid} is not in trainers.party')
                continue
            want = gfx_gender(gfx)
            got = pic_gender(pic)
            if want and got and want != got:
                bad.append(f'{name}: {tid} stands up as {gfx} but battles as '
                           f'"{pic}" - the sprite and the pic are different people')
            if want and gender and want != gender:
                bad.append(f'{name}: {tid} stands up as {gfx} but is '
                           f'Gender: {gender}')

        # 2. sorted by level, as PickTrainerForLevel's contiguous-run scan needs
        levels = [lvl for _t, _g, lvl in rows]
        if levels != sorted(levels):
            bad.append(f'{name}: levels are not ascending {levels} - the picker '
                       f'scans for one contiguous run and will miss candidates')

        # 3. deep enough at every floor of the dungeon it serves. The floor
        #    range is not in the table, so check the whole gym span and report
        #    only where the pool is actually reachable-thin.
        if name == 'sUnderwaterTrainers':
            lo = 71
            hi = lo + k['DUNGEON_LONG_FLOORS'] - 1
            for floor in range(lo, hi + 1):
                target = floor_target(floor, k)
                want = min(1 + floor // k['DUNGEON_TRAINER_FLOORS_PER_EXTRA'],
                           k['DUNGEON_MAX_TRAINERS'])
                have = sum(1 for _t, _g, lvl in rows
                           if lvl + WINDOW >= target and lvl <= target + WINDOW)
                if have < want:
                    bad.append(
                        f'{name}: floor {floor} wants {want} distinct trainers '
                        f'at level {target} but only {have} are inside the '
                        f'+/-{WINDOW} window')

    for line in bad:
        print('FAIL:', line)
    if bad:
        return 1

    for name, rows in tables.items():
        print(f'{name}: {len(rows)} trainers, levels '
              f'{rows[0][2]}-{rows[-1][2]}, sprites and pics agree')
    return 0


if __name__ == '__main__':
    sys.exit(main())
