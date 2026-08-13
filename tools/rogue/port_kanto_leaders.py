"""Port the Kanto leaders, Elite Four and Champion into the Emerald build.

The parties already exist, in src/data/trainers_frlg.party - which an Emerald
build does not read. This lifts the thirteen blocks we want into
src/data/trainers.party under project-owned ids, and prints the opponents.h
block to go with them.

WHAT IT CHANGES ON THE WAY ACROSS, and why each one is not a copy:

  * THE ID. TRAINER_LEADER_BROCK is an FRLG id and means something else in an
    Emerald build, so every entry is renamed TRAINER_ROGUE_KANTO_*, following
    TRAINER_ROGUE_RIVAL and TRAINER_ROGUE_DIVER_* as the marker for an id this
    project owns.

  * THE GENDER. ALL THIRTEEN ARE Gender: Male IN THE FRLG DATA, Misty, Erika,
    Sabrina, Lorelei and Agatha included. That is not a mis-parse: the Music
    field carries the female variant correctly, and FRLG simply leaves the
    gender bit at zero. Copying it would be silently wrong, and not only
    cosmetically - RogueDungeon_SetBossAceOriginalTrainer stamps the boss onto
    an adopted ace using trainer->gender, so a ported Misty would hand over a
    Starmie with a male original trainer.

WHAT IT DELIBERATELY DOES NOT CHANGE:

  * THE LEVELS. These are stock FireRed teams and Brock leads with a level 12
    Geodude, which is nowhere near this run curve. Scaling is a design decision
    and belongs wherever these are eventually placed, not baked into the port -
    verify_run_structure.py check 7 is what will judge it.

  * THE IVs, ITEMS AND AI. Left exactly as FRLG has them. See the report for
    how they differ from the Emerald leaders.

Run:  python3 tools/rogue/port_kanto_leaders.py            # report
      python3 tools/rogue/port_kanto_leaders.py --write    # append the blocks

Idempotent: it truncates everything from the marker down and rewrites it, so a
second run is a no-op rather than a duplicate.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SRC = REPO / 'src/data/trainers_frlg.party'
DST = REPO / 'src/data/trainers.party'

# A C BLOCK COMMENT, and it has to be. This format takes neither // nor @ for
# comments - see the header of trainers.party - and @ is actively harmful,
# because it is the HELD ITEM separator, so a line starting with it parses as a
# Pokemon holding an item and fails somewhere else entirely with
# "expected ':' or 'Nature'".
MARKER = ('/* Kanto leaders, Elite Four and Champion, ported from '
          'trainers_frlg.party by tools/rogue/port_kanto_leaders.py. '
          'Do not edit below this line by hand. */')

# (FRLG id, our id, female?)
#
# Giovanni is not with the other leaders in the source file - his gym is the
# Rocket hideout - so the order here is ours, not the file's.
#
# The Champion is three trainers in FRLG, one per starter the player chose.
# That does not map onto a run with thirty starter picks, so ONE is taken and
# the other two are left where they are.
TRAINERS = (
    ('TRAINER_LEADER_BROCK',            'TRAINER_ROGUE_KANTO_BROCK',    False),
    ('TRAINER_LEADER_MISTY',            'TRAINER_ROGUE_KANTO_MISTY',    True),
    ('TRAINER_LEADER_LT_SURGE',         'TRAINER_ROGUE_KANTO_LT_SURGE', False),
    ('TRAINER_LEADER_ERIKA',            'TRAINER_ROGUE_KANTO_ERIKA',    True),
    ('TRAINER_LEADER_KOGA',             'TRAINER_ROGUE_KANTO_KOGA',     False),
    ('TRAINER_LEADER_SABRINA',          'TRAINER_ROGUE_KANTO_SABRINA',  True),
    ('TRAINER_LEADER_BLAINE',           'TRAINER_ROGUE_KANTO_BLAINE',   False),
    ('TRAINER_LEADER_GIOVANNI',         'TRAINER_ROGUE_KANTO_GIOVANNI', False),
    ('TRAINER_ELITE_FOUR_LORELEI',      'TRAINER_ROGUE_KANTO_LORELEI',  True),
    ('TRAINER_ELITE_FOUR_BRUNO',        'TRAINER_ROGUE_KANTO_BRUNO',    False),
    ('TRAINER_ELITE_FOUR_AGATHA',       'TRAINER_ROGUE_KANTO_AGATHA',   True),
    ('TRAINER_ELITE_FOUR_LANCE',        'TRAINER_ROGUE_KANTO_LANCE',    False),
    ('TRAINER_CHAMPION_FIRST_SQUIRTLE', 'TRAINER_ROGUE_KANTO_BLUE',     False),
)

FIRST_ID = 863


def blocks():
    """-> {frlg id: block text}, each running to the next === header."""
    text = SRC.read_text(encoding='utf-8')
    out = {}
    for match in re.finditer(r'^=== (\w+) ===$', text, re.M):
        name = match.group(1)
        nxt = text.find('\n=== ', match.end())
        out[name] = text[match.start():nxt if nxt != -1 else len(text)].rstrip()
    return out


def port():
    found = blocks()
    parts, report = [], []
    for frlg, ours, female in TRAINERS:
        if frlg not in found:
            raise SystemExit(f'{frlg} is not in {SRC.name}')
        block = found[frlg]
        block = block.replace(f'=== {frlg} ===', f'=== {ours} ===', 1)

        # The gender fix. Asserted rather than assumed: if upstream ever
        # corrects the source data, this should stop rewriting rather than
        # quietly flip a corrected value back.
        want = 'Female' if female else 'Male'
        before = re.search(r'^Gender: (\w+)$', block, re.M)
        if before is None:
            raise SystemExit(f'{frlg} has no Gender line')
        if before.group(1) != want:
            block = re.sub(r'^Gender: \w+$', f'Gender: {want}', block, count=1, flags=re.M)
            report.append((ours, before.group(1), want))
        parts.append(block)
    return parts, report


def main(argv):
    parts, fixed = port()

    print('Kanto leaders, Elite Four and Champion')
    for i, (frlg, ours, _) in enumerate(TRAINERS):
        mons = parts[i].count('\nLevel:')
        print(f'  {ours:<32} {FIRST_ID + i}  {mons} mons  from {frlg}')

    print(f'\nGender corrected on {len(fixed)} of {len(TRAINERS)} '
          f'(FRLG leaves the bit at zero for everyone):')
    for name, was, now in fixed:
        print(f'  {name:<32} {was} -> {now}')

    print('\nopponents.h block:')
    for i, (_, ours, _) in enumerate(TRAINERS):
        print(f'#define {ours:<35} {FIRST_ID + i}')
    print(f'#define TRAINERS_COUNT_EMERALD     {FIRST_ID + len(TRAINERS)}')

    text = DST.read_text(encoding='utf-8')
    cut = text.find(MARKER)
    if cut != -1:
        text = text[:cut].rstrip() + '\n'
    body = '\n' + MARKER + '\n\n' + '\n\n'.join(parts) + '\n'

    if cut != -1 and DST.read_text(encoding='utf-8').endswith(body):
        print('\nalready ported (tail matches)')
        return
    if '--write' not in argv:
        print('\nnot written - re-run with --write')
        return
    DST.write_text(text.rstrip() + '\n' + body, encoding='utf-8', newline='\n')
    print(f'\nwrote {len(parts)} blocks to {DST.relative_to(REPO)}')


if __name__ == '__main__':
    main(sys.argv[1:])
