"""What each starter actually holds at DUNGEON_STARTER_LEVEL.

Every starter carries a fallback elemental attack so a fresh pick can fight.
Whether it is NEEDED depends on two settings that can drift apart from each
other: DUNGEON_STARTER_LEVEL and P_LVL_UP_LEARNSETS. Under Gen 9 learnsets at
level 10, 21 of the 27 starters already know their fallback from levelling.

That mattered because the grant used to write the move into the first empty
slot, clamping to the last slot when all four were full - so a starter that
already knew the move lost a real one for a duplicate. Squirtle traded Rapid
Spin for a second Water Gun. The grant now skips a known move and never
displaces one, and this reports what that leaves.

The check that matters: every starter must end up with at least one damaging
move. Status moves alone would mean four turns of Growl.

Usage:  python3 tools/rogue/check_starter_moves.py
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MAX_MON_MOVES = 4

# Whether a move can damage is read from the battle move table's power field
# rather than listed here, so a rebalance upstream cannot rot this check.


def starter_level():
    text = (REPO / 'include/constants/rogue_dungeon.h').read_text(errors='replace')
    m = re.search(r'#define\s+DUNGEON_STARTER_LEVEL\s+(\d+)', text)
    return int(m.group(1))


def learnset_gen():
    text = (REPO / 'include/config/pokemon.h').read_text(errors='replace')
    m = re.search(r'#define\s+P_LVL_UP_LEARNSETS\s+(\w+)', text)
    token = m.group(1)
    if token == 'GEN_LATEST':
        gens = sorted((REPO / 'src/data/pokemon/level_up_learnsets').glob('gen_*.h'))
        return int(re.search(r'gen_(\d+)', gens[-1].name).group(1))
    return int(re.search(r'GEN_(\d+)', token).group(1))


def starters():
    """(SPECIES, fallback move) straight out of the generated header."""
    text = (REPO / 'include/constants/rogue_dungeon_starters.h').read_text(errors='replace')
    return re.findall(r'\{\s*SPECIES_(\w+)\s*,\s*MOVE_(\w+)\s*\}', text)


def damaging_moves():
    """Move names with nonzero power.

    Split on the [MOVE_X] = headers rather than trying to brace-match, because
    entries contain #if blocks, and read .power as text rather than an int
    because it is often a ternary - Vine Whip is
    `B_UPDATED_MOVE_DATA >= GEN_6 ? 45 : 35`, which a \\d+ pattern silently
    misses, and missing it made this check call Bulbasaur unarmed.
    """
    text = (REPO / 'src/data/moves_info.h').read_text(errors='replace')
    parts = re.split(r'\n\s*\[MOVE_(\w+)\]\s*=', text)
    out = set()
    for name, body in zip(parts[1::2], parts[2::2]):
        m = re.search(r'\.power\s*=\s*([^,\n]+)', body)
        if m and m.group(1).strip() != '0':
            out.add(name)
    return out


def main():
    level = starter_level()
    gen = learnset_gen()
    path = REPO / f'src/data/pokemon/level_up_learnsets/gen_{gen}.h'
    text = path.read_text(errors='replace')
    hitters = damaging_moves()

    print(f'starter level {level}, gen {gen} learnsets\n')
    dup = harmless = failures = 0
    for species, fallback in starters():
        name = species.capitalize()
        m = re.search(r's' + name + r'LevelUpLearnset\[\]\s*=\s*\{(.*?)\n\};',
                      text, re.S)
        if not m:
            print(f'{name:<12} learnset not found')
            failures += 1
            continue

        pairs = re.findall(r'LEVEL_UP_MOVE\s*\(\s*(\d+)\s*,\s*MOVE_(\w+)\s*\)',
                           m.group(1))
        learned = [mv for lv, mv in pairs if int(lv) <= level]
        held = learned[-MAX_MON_MOVES:]

        already = fallback in held
        if already:
            dup += 1
        elif len(held) < MAX_MON_MOVES:
            held = held + [fallback]          # lands in a free slot
        else:
            harmless += 1                     # no room, and no longer forced in

        ok = any(mv in hitters for mv in held)
        note = ''
        if already:
            note = f'(already knows {fallback})'
        elif fallback in held:
            note = f'(+{fallback})'
        else:
            note = f'({fallback} not added - no free slot)'
        if not ok:
            note += '   FAIL: no damaging move'
            failures += 1
        print(f'  {name:<12} {",".join(held):<52} {note}')

    print()
    print(f'{dup} of 27 already know their fallback; '
          f'{harmless} have no room for it and no longer lose a move to it')
    print(f'{"FAILED" if failures else "ok"}: '
          f'{failures} starter(s) with nothing to attack with')
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
