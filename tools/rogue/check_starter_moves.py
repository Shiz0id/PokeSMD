"""What each starter actually holds at DUNGEON_STARTER_LEVEL.

Every pick carries a fallback elemental attack so a fresh one can fight.
Whether it is NEEDED depends on two settings that can drift apart from each
other: DUNGEON_STARTER_LEVEL and P_LVL_UP_LEARNSETS. Under Gen 9 learnsets at
level 10, 23 of the 30 already know their fallback from levelling.

That mattered because the grant used to write the move into the first empty
slot, clamping to the last slot when all four were full - so a pick that
already knew the move lost a real one for a duplicate. Squirtle traded Rapid
Spin for a second Water Gun.

This models the grant as it now stands, in the same order: skip a known move,
else take a free slot, else displace a status move but only for a pick left
with no damaging move of its own type.

The check that matters: every pick must end up with a damaging move OF ITS OWN
TYPE. The weaker "any damaging move" version of this check passed Clefairy,
which arrived holding Charm, Copycat, Encore and a 20 BP Stored Power - armed
on paper and helpless in practice. Type-matching is what makes it fail.

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


def config_gen(name, default_latest=9):
    """A P_* generation config from include/config/pokemon.h, as an int."""
    text = (REPO / 'include/config/pokemon.h').read_text(errors='replace')
    m = re.search(r'#define\s+%s\s+(\w+)' % name, text)
    token = m.group(1)
    if token == 'GEN_LATEST':
        return default_latest
    return int(re.search(r'GEN_(\d+)', token).group(1))


def starters():
    """(SPECIES, fallback move) straight out of the generated header."""
    text = (REPO / 'include/constants/rogue_dungeon_starters.h').read_text(errors='replace')
    return re.findall(r'\{\s*SPECIES_(\w+)\s*,\s*MOVE_(\w+)\s*\}', text)


def move_types():
    """MOVE name -> TYPE name, off the same split damaging_moves() uses."""
    text = (REPO / 'src/data/moves_info.h').read_text(errors='replace')
    parts = re.split(r'\n\s*\[MOVE_(\w+)\]\s*=', text)
    out = {}
    for name, body in zip(parts[1::2], parts[2::2]):
        m = re.search(r'\.type\s*=\s*TYPE_(\w+)', body)
        if m:
            out[name] = m.group(1)
    return out


def species_types(updated_gen):
    """SPECIES name -> {TYPE names}.

    Two spellings in species_info: MON_TYPES(A[, B]) inline, and a shared
    <FAMILY>_TYPES macro for families whose typing changed between gens. The
    macro is defined twice under `#if P_UPDATED_TYPES >= GEN_n`, so resolve it
    against the configured gen rather than taking whichever define is first -
    picking wrong would call Clefairy Normal and mask the very case this check
    exists for.
    """
    macros = {}
    entries = {}
    for path in sorted((REPO / 'src/data/pokemon/species_info').glob('*.h')):
        text = path.read_text(errors='replace')

        gate = None
        for line in text.splitlines():
            g = re.match(r'\s*#if\s+P_UPDATED_TYPES\s*>=\s*GEN_(\d+)', line)
            if g:
                gate = updated_gen >= int(g.group(1))
                continue
            if re.match(r'\s*#else', line) and gate is not None:
                gate = not gate
                continue
            if re.match(r'\s*#endif', line):
                gate = None
                continue
            d = re.match(r'\s*#define\s+(\w+_TYPES)\s*\{([^}]*)\}', line)
            if d and gate is not False:
                macros[d.group(1)] = set(re.findall(r'TYPE_(\w+)', d.group(2)))

        parts = re.split(r'\n\s*\[SPECIES_(\w+)\]\s*=', text)
        for name, body in zip(parts[1::2], parts[2::2]):
            body = body[:body.find('.levelUpLearnset')] if '.levelUpLearnset' in body else body
            m = re.search(r'\.types\s*=\s*MON_TYPES\(([^)]*)\)', body)
            if m:
                entries[name] = set(re.findall(r'TYPE_(\w+)', m.group(1)))
                continue
            m = re.search(r'\.types\s*=\s*(\w+_TYPES)', body)
            if m:
                entries[name] = m.group(1)   # resolved below, macro may follow

    for name, val in list(entries.items()):
        if isinstance(val, str):
            entries[name] = macros.get(val, set())
    return entries


def status_moves():
    """Move names whose category is STATUS.

    Split on the [MOVE_X] = headers rather than trying to brace-match, because
    entries contain #if blocks.

    Read .category, not .power, for two reasons. It is what the C reads, so the
    model cannot disagree with the grant it is modelling. And .power is often a
    ternary - Vine Whip is `B_UPDATED_MOVE_DATA >= GEN_6 ? 45 : 35` - which a
    \\d+ pattern silently misses, and missing it once made this check call
    Bulbasaur unarmed. Fixed-damage moves like Seismic Toss are the same trap
    from the other side: zero power, still an attack.
    """
    text = (REPO / 'src/data/moves_info.h').read_text(errors='replace')
    parts = re.split(r'\n\s*\[MOVE_(\w+)\]\s*=', text)
    out = set()
    for name, body in zip(parts[1::2], parts[2::2]):
        m = re.search(r'\.category\s*=\s*DAMAGE_CATEGORY_(\w+)', body)
        if m and m.group(1) == 'STATUS':
            out.add(name)
    return out


def main():
    level = starter_level()
    gen = learnset_gen()
    path = REPO / f'src/data/pokemon/level_up_learnsets/gen_{gen}.h'
    text = path.read_text(errors='replace')
    status = status_moves()
    mtypes = move_types()
    stypes = species_types(config_gen('P_UPDATED_TYPES'))

    def own_type_attack(moves, own):
        return any(mv not in status and mtypes.get(mv) in own for mv in moves)

    print(f'starter level {level}, gen {gen} learnsets\n')
    dup = harmless = displacements = failures = 0
    rows = starters()
    for species, fallback in rows:
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

        own = stypes.get(species, set())
        already = fallback in held
        displaced = None

        # Same order as RogueDungeon_GiveChosenStarter.
        if already:
            dup += 1
        elif len(held) < MAX_MON_MOVES:
            held = held + [fallback]          # lands in a free slot
        elif not own_type_attack(held, own):
            for i, mv in enumerate(held):     # displace the first status move
                if mv in status:
                    displaced = mv
                    held = held[:i] + [fallback] + held[i + 1:]
                    displacements += 1
                    break
            if displaced is None:
                harmless += 1
        else:
            harmless += 1                     # no room, but armed already

        ok = own_type_attack(held, own)
        if already:
            note = f'(already knows {fallback})'
        elif displaced:
            note = f'(+{fallback} over {displaced})'
        elif fallback in held:
            note = f'(+{fallback})'
        else:
            note = f'({fallback} not added - no free slot)'
        if not own:
            note += '   WARN: types unresolved'
        if not ok:
            note += '   FAIL: no damaging move of its own type'
            failures += 1
        print(f'  {name:<12} {",".join(held):<52} {note}')

    print()
    print(f'{dup} of {len(rows)} already know their fallback; '
          f'{harmless} have no room for it and keep every move; '
          f'{displacements} displaced a status move to get armed')
    print(f'{"FAILED" if failures else "ok"}: '
          f'{failures} pick(s) with no damaging move of their own type')
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
