"""
Generate a depth-indexed table of stock trainers for the procedural dungeon.

Parses src/data/trainers.party for each trainer's average party level, filters
out the ones that make bad random encounters, and emits a C header sorted by
level so the generator can pick a trainer matching a floor's target level.

Two tables come out of this:

  sRogueDungeonTrainers   ordinary opponents scattered through a floor
  sRogueDungeonMiniBosses Team Aqua/Magma, for the floor-5 mini boss

Run from anywhere:  python3 tools/rogue/gen_trainer_table.py
"""
import collections
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / 'include/constants/rogue_dungeon_trainers.h'

# Classes reserved for bosses, or that are frontier placeholders with a single
# level-5 Beldum standing in for a dynamically built party.
#
# THE FRLG CLASSES ARE SEPARATE STRINGS AND THAT IS THE WHOLE TRAP. A ported
# Kanto leader's class is "Leader Frlg", not "Leader", so the four stock class
# names above do not exclude one of them - and this file is GENERATED, so the
# leak does not appear when the trainers are added. It appears the next time
# somebody runs this tool, which may be months later and for an unrelated
# reason. Regenerating against the committed header is what caught it: all
# thirteen Kanto leaders and Blue were about to join the random pool, so a
# level-60 Champion could stand on a woods floor.
EXCLUDE_CLASSES = {
    'Leader', 'Elite Four', 'Champion', 'Rival',
    'Leader Frlg', 'Elite Four Frlg', 'Champion Frlg',
    'Salon Maiden', 'Dome Ace', 'Palace Maven', 'Arena Tycoon',
    'Factory Head', 'Pike Queen', 'Pyramid King',
    'Magma Leader', 'Aqua Leader', 'Magma Admin', 'Aqua Admin',
    # THE THIRD MISS OF THE SAME KIND, and it reached a screen. The two RS
    # Protag entries are upstream placeholders whose party is one level-5
    # GROUDON and one level-5 KYOGRE. Only seven pool entries sit at level 5 or
    # below, so on floor 1 the level window landed on one of them constantly.
    # The class is not "Rival" and the id is not TRAINER_RS_ -- which the
    # pattern below was reaching for and missed -- so neither guard saw them.
    'RS Protag',
}
# Partner/multi-battle and link trainers that are not standalone opponents.
#
# ^TRAINER_ROGUE_ excludes this project's OWN trainers as a class, and it is a
# prefix rather than a list on purpose: every one of them exists because some
# table already owns it. The seven divers belong to sUnderwaterTrainers, the
# rival to floor 110, the Kanto set to the boss tables. Excluding them by class
# name would have missed the divers outright - "Rogue Diver" is a class this
# project invented and nothing here would have known to name it.
EXCLUDE_ID_PATTERNS = [
    r'_PARTNER', r'^TRAINER_STEVEN$', r'^TRAINER_NONE$',
    r'^TRAINER_LINK', r'_VR_\d+$', r'^TRAINER_RS_',
    r'^TRAINER_ROGUE_',
    # Belt and braces on the RS Protag class above: a placeholder is a
    # placeholder whatever class upstream files it under, and the next one to
    # arrive will not announce itself either.
    r'_PLACEHOLDER$',
]


def blocks():
    text = (REPO / 'src/data/trainers.party').read_text(errors='replace')
    parts = re.split(r'^=== (TRAINER_\w+) ===$', text, flags=re.M)
    for i in range(1, len(parts), 2):
        yield parts[i], parts[i + 1]


def field(body, name):
    m = re.search(rf'^{name}: (.+)$', body, re.M)
    return m.group(1).strip() if m else ''


def parse():
    out = []
    for tid, body in blocks():
        if any(re.search(p, tid) for p in EXCLUDE_ID_PATTERNS):
            continue
        cls = field(body, 'Class')
        if not cls or cls in EXCLUDE_CLASSES:
            continue
        if re.search(r'^Double Battle: Yes', body, re.M):
            continue
        levels = [int(m) for m in re.findall(r'^Level: (\d+)', body, re.M)]
        if not levels:
            continue
        avg = round(sum(levels) / len(levels))
        if avg < 2 or avg > 90:
            continue
        out.append((avg, tid, cls, len(levels)))
    out.sort(key=lambda r: (r[0], r[1]))
    return out


# THE MINI BOSS ROSTER, ONE ROW PER TEAM CLASS: the class as trainers.party
# spells it, mapped to (male sprite, female sprite).
#
# A TABLE RATHER THAN A CHAIN OF IFS, and that is the whole point of this shape.
# This used to end in `team = 'MAGMA' if 'Magma' in cls else 'AQUA'`, so ANY team
# added to the roster that was not Magma silently came out in an AQUA uniform --
# walking the floor as a Team Aqua grunt while opening the battle with its own
# trainer pic. Nothing would have failed: the sprite is a real member of a real
# enum, the roster generates, the build is clean. That is the same shape as the
# Ruby gemstone that stood in for a trainer, and it was sitting here loaded.
#
# ADDING A TEAM IS A ROW HERE AND NOTHING ELSE. An unknown class now stops the
# tool rather than guessing, so the failure arrives at generation time with the
# class name in it.
#
# The leaders are in deliberately. Leaving them out capped the mini boss pool at
# 38 while the last gym dungeon targets 45, so the floor-75 mini boss was seven
# levels light. Archie and Maxie cover 41-43 and close that.
#
# See docs/TEAM_MINIBOSSES.md for what each candidate team still needs.
TEAM_GFX = {
    'Team Aqua':    ('OBJ_EVENT_GFX_AQUA_MEMBER_M',  'OBJ_EVENT_GFX_AQUA_MEMBER_F'),
    'Aqua Admin':   ('OBJ_EVENT_GFX_AQUA_MEMBER_M',  'OBJ_EVENT_GFX_AQUA_MEMBER_F'),
    'Aqua Leader':  ('OBJ_EVENT_GFX_ARCHIE',         'OBJ_EVENT_GFX_ARCHIE'),
    'Team Magma':   ('OBJ_EVENT_GFX_MAGMA_MEMBER_M', 'OBJ_EVENT_GFX_MAGMA_MEMBER_F'),
    'Magma Admin':  ('OBJ_EVENT_GFX_MAGMA_MEMBER_M', 'OBJ_EVENT_GFX_MAGMA_MEMBER_F'),
    'Magma Leader': ('OBJ_EVENT_GFX_MAXIE',          'OBJ_EVENT_GFX_MAXIE'),

    # Gen 1 Rocket. The sprites were in the build long before the parties
    # were; both are here now. See tools/rogue/gen_rocket_parties.py.
    'Team Rocket Frlg': ('OBJ_EVENT_GFX_ROCKET_M', 'OBJ_EVENT_GFX_ROCKET_F'),
}

TEAM_CLASSES = set(TEAM_GFX)


def team_gfx(cls, gender, pic=''):
    """Overworld sprite for a mini boss. Leaders get their own, so a mini boss
    that is actually Archie does not walk around dressed as a grunt.

    THE PIC DECIDES THE SEX, NOT THE Gender FIELD, and the two really do
    disagree. Three of the fifty-three FireRed Rocket trainers are
    `Gender: Male` carrying `Pic: Rocket Grunt F Frlg` -- GRUNT_42, GRUNT_51 and
    ADMIN_1. In FireRed that field drives encounter music and costs nothing
    visible, so the mismatch never showed there. Here it decides the figure
    standing on the floor, and keying on it would put a man on the tile who
    opens the battle as a woman: the exact fault check_theme_trainers.py was
    written for after a diver did it.

    So the pic wins where it names a sex, and Gender is the fallback for a pic
    that does not. Every Aqua and Magma row agrees either way, which is why this
    changes exactly three rows and nothing else.
    """
    if re.search(r'(^| )F( |$)', pic):
        gender = 'Female'
    elif re.search(r'(^| )M( |$)', pic):
        gender = 'Male'
    # UNREACHABLE WHILE TEAM_CLASSES IS DERIVED FROM THIS TABLE, and that is
    # the stronger guarantee: a team cannot join the roster without bringing its
    # sprites, because the roster IS the sprite table. Kept for the day someone
    # splits the two again, which is the edit that reopens the bug above.
    if cls not in TEAM_GFX:
        raise SystemExit(
            f"gen_trainer_table.py: no sprite row for team class {cls!r}. "
            f"Add one to TEAM_GFX -- do not let it fall back, which is how a "
            f"team ends up wearing another team's uniform.")

    male, female = TEAM_GFX[cls]
    return female if gender == 'Female' else male


def parse_team():
    """Team Aqua and Magma trainers, for mini bosses. Kept separate from the
    ordinary table, which excludes them."""
    out = []
    for tid, body in blocks():
        cls = field(body, 'Class')
        if cls not in TEAM_CLASSES:
            continue
        if re.search(r'^Double Battle: Yes', body, re.M):
            continue
        levels = [int(m) for m in re.findall(r'^Level: (\d+)', body, re.M)]
        if not levels:
            continue
        gfx = team_gfx(cls, field(body, 'Gender'), field(body, 'Pic'))
        out.append((round(sum(levels) / len(levels)), tid, cls, gfx))
    out.sort(key=lambda r: (r[0], r[1]))
    return out


def emit_team(rows):
    lines = [
        '',
        '// Mini bosses: Team Aqua and Magma, sorted by average party level.',
        '// Picked by depth like the ordinary table, because using a fixed list',
        '// meant a floor-5 mini boss could be a mid-game grunt with a level 31',
        '// Zubat. The leaders are included: without them the pool stopped at 38',
        '// while the last gym dungeon asks for 45.',
        f'// {len(rows)} entries, levels {rows[0][0]}-{rows[-1][0]}.',
        '',
        'struct RogueDungeonMiniBoss',
        '{',
        '    u16 trainerId;',
        '    u16 gfxId;',
        '    u8 avgLevel;',
        '};',
        '',
        'static const struct RogueDungeonMiniBoss sRogueDungeonMiniBosses[] =',
        '{',
    ]
    for avg, tid, cls, gfx in rows:
        lines.append(f'    {{ {tid}, {gfx}, {avg} }},'.ljust(78) + f'// {cls}')
    lines.append('};')
    return lines


def main():
    rows = parse()
    lines = [
        '#ifndef GUARD_CONSTANTS_ROGUE_DUNGEON_TRAINERS_H',
        '#define GUARD_CONSTANTS_ROGUE_DUNGEON_TRAINERS_H',
        '',
        '// GENERATED by tools/rogue/gen_trainer_table.py - do not edit by hand.',
        '//',
        '// Stock trainers usable as random dungeon opponents, sorted by average',
        '// party level so a floor can pick one matching its target. Gym leaders,',
        '// the Elite Four, rivals, admins and frontier brains are excluded: they',
        f'// are reserved for boss floors. {len(rows)} entries, levels '
        f'{rows[0][0]}-{rows[-1][0]}.',
        '',
        'struct RogueDungeonTrainer',
        '{',
        '    u16 trainerId;',
        '    u8 avgLevel;',
        '};',
        '',
        'static const struct RogueDungeonTrainer sRogueDungeonTrainers[] =',
        '{',
    ]
    for avg, tid, cls, n in rows:
        lines.append(f'    {{ {tid}, {avg} }},  // {cls}, {n} mon'
                     f'{"s" if n != 1 else ""}')
    lines += ['};']
    team = parse_team()
    lines += emit_team(team)
    lines += ['', '#endif // GUARD_CONSTANTS_ROGUE_DUNGEON_TRAINERS_H', '']

    OUT.write_text('\n'.join(lines), encoding='utf-8', newline='\n')
    print(f'wrote {OUT.name}: {len(rows)} trainers, levels {rows[0][0]}-{rows[-1][0]}')
    print(f'  mini bosses: {len(team)} team trainers, '
          f'levels {team[0][0]}-{team[-1][0]}')
    band = collections.Counter(r[0] // 10 * 10 for r in rows)
    print('level distribution:')
    for k in sorted(band):
        print(f'  {k:>2}-{k+9:<3} {band[k]:>4}  {"#" * (band[k] // 4)}')


if __name__ == '__main__':
    main()
