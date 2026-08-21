"""Guard the mini boss teams' music: every team sounds like itself.

THE RULE, stated once, here:

    A team on the mini boss roster has an approach jingle and a battle track of
    its OWN. No team may be filed under another team's jingle, every jingle a
    trainer names must resolve to a song, and every team class must appear in
    the battle-track switch rather than falling through to the ordinary trainer
    music.

WHY THIS EXISTS. Gen 1 Rocket arrived wearing Team Aqua's approach jingle and
fighting to the generic trainer theme, and neither was visible anywhere except
by standing in front of one.

  - `Music: Aqua` on all 53 is what FireRed itself says. That field picks an
    approach jingle, and Gen 1 Rocket had none of its own in a game with no Team
    Aqua, so AQUA was the shrug. Copied across verbatim it means the player
    hears Team Aqua coming and Team Rocket arrives.
  - The battle track is chosen by a switch on trainer CLASS in src/pokemon.c.
    A class with no case falls to `default` and gets MUS_VS_TRAINER, so a whole
    team can join the roster and nothing about the fight says so. Missing a case
    is not an error, it is silence where a theme should be.

Both are data-shaped faults with no wrong value in any table: the jingle was a
real constant, the fallthrough was a legal switch. Only a check that knows which
team a trainer BELONGS to can see either.

It also found a vanilla one. TRAINER_GRUNT_UNUSED is class Team Magma carrying
`Music: Aqua` -- harmless in Emerald, where nothing places it, and reachable here
because the roster takes every Team Magma trainer with a party.

WHAT IT ASSERTS.

  1. Every trainer whose class is a roster team names that team's own jingle.
     The class-to-jingle pairing is stated once, in TEAM_JINGLE below.
  2. Every jingle named is a TRAINER_ENCOUNTER_MUSIC_* that exists AND has a
     case in GetTrainerEncounterMusicId, so it resolves to a song rather than
     the default.
  3. Every roster team class has its own case in the battle-track switch in
     src/pokemon.c, and the song it returns is a real MUS_* in songs.h.
  4. THE FOUR-BIT BUDGET. `encounterMusic` is a `u16 :4` in struct Trainer, so
     the enum cannot exceed 15. Rocket took 14. One value is left, earmarked for
     Team Galactic; a sixteenth would truncate silently rather than fail to
     build, which is the whole reason this is asserted rather than trusted.

Usage:  python3 tools/rogue/check_team_music.py [REPO | --repo PATH]
        python3 tools/rogue/check_team_music.py --selftest
"""
import re
import sys
import tempfile
from pathlib import Path

PARTY = 'src/data/trainers.party'
TRAINERS_H = 'include/constants/trainers.h'
SETUP_C = 'src/battle_setup.c'
POKEMON_C = 'src/pokemon.c'
SONGS_H = 'include/constants/songs.h'

SOURCES = [PARTY, TRAINERS_H, SETUP_C, POKEMON_C, SONGS_H]

# The pairing, stated once: .party class -> its own jingle name, and the
# TRAINER_CLASS_* that carries its battle track.
TEAM_JINGLE = {
    'Team Aqua':        ('Aqua',   'TRAINER_CLASS_TEAM_AQUA'),
    'Aqua Admin':       ('Aqua',   'TRAINER_CLASS_AQUA_ADMIN'),
    'Aqua Leader':      ('Aqua',   'TRAINER_CLASS_AQUA_LEADER'),
    'Team Magma':       ('Magma',  'TRAINER_CLASS_TEAM_MAGMA'),
    'Magma Admin':      ('Magma',  'TRAINER_CLASS_MAGMA_ADMIN'),
    'Magma Leader':     ('Magma',  'TRAINER_CLASS_MAGMA_LEADER'),
    'Team Rocket Frlg': ('Rocket', 'TRAINER_CLASS_TEAM_ROCKET_FRLG'),
}

ENCOUNTER_MUSIC_BITS = 4

_failures = []


def fail(msg):
    _failures.append(msg)


def check(repo):
    del _failures[:]
    repo = Path(repo)
    src = {}
    for rel in SOURCES:
        path = repo / rel
        if not path.exists():
            print('FAIL  check_team_music.py: %s not found under %s'
                  % (rel, repo))
            return False
        src[rel] = path.read_text(encoding='utf-8', errors='replace')

    # -- 1. every team trainer names its own jingle --
    blocks = re.split(r'^=== (TRAINER_\w+) ===$', src[PARTY], flags=re.M)
    counted = {}
    for i in range(1, len(blocks), 2):
        tid, body = blocks[i], blocks[i + 1]
        cls = re.search(r'^Class: (.+)$', body, re.M)
        if not cls or cls.group(1).strip() not in TEAM_JINGLE:
            continue
        cls = cls.group(1).strip()
        want = TEAM_JINGLE[cls][0]
        got = re.search(r'^Music: (.+)$', body, re.M)
        got = got.group(1).strip() if got else '(none)'
        counted[cls] = counted.get(cls, 0) + 1
        if got != want:
            fail('%s is class "%s" but names the %s jingle, not %s. A team that '
                 'borrows another team\'s approach music is only findable by '
                 'standing in front of one' % (tid, cls, got, want))

    for cls in TEAM_JINGLE:
        if cls not in counted:
            fail('no trainer in %s has class "%s"; either the team was removed '
                 'or this table has drifted from the roster' % (PARTY, cls))

    # -- 2. every jingle exists and resolves --
    for cls, (jingle, _) in sorted(TEAM_JINGLE.items()):
        const = 'TRAINER_ENCOUNTER_MUSIC_' + jingle.upper()
        if not re.search(r'#define\s+' + const + r'\s+\d+', src[TRAINERS_H]):
            fail('%s is not defined in %s' % (const, TRAINERS_H))
        elif ('case ' + const + ':') not in src[SETUP_C]:
            fail('%s has no case in GetTrainerEncounterMusicId (%s), so it '
                 'falls to the default jingle and the team announces itself as '
                 'somebody else' % (const, SETUP_C))

    # -- 3. every team class has its own battle track --
    songs = set(re.findall(r'#define\s+(MUS_\w+)', src[SONGS_H]))
    for cls, (_, class_const) in sorted(TEAM_JINGLE.items()):
        m = re.search(r'case ' + class_const + r':\s*\n(?:\s*case \w+:\s*\n)*'
                      r'\s*return (MUS_\w+);', src[POKEMON_C])
        if m is None:
            fail('%s (class "%s") has no case in the battle-track switch in %s. '
                 'It falls through to default and fights to MUS_VS_TRAINER, '
                 'which is a whole team on the roster with nothing in the fight '
                 'to say so' % (class_const, cls, POKEMON_C))
        elif m.group(1) not in songs:
            fail('%s returns %s, which is not a MUS_* in %s'
                 % (class_const, m.group(1), SONGS_H))

    # -- 4. the four-bit budget --
    values = [int(v) for v in re.findall(
        r'#define\s+TRAINER_ENCOUNTER_MUSIC_\w+\s+(\d+)', src[TRAINERS_H])]
    if values:
        top = max(values)
        limit = (1 << ENCOUNTER_MUSIC_BITS) - 1
        if top > limit:
            fail('TRAINER_ENCOUNTER_MUSIC_* reaches %d, past the %d that a '
                 '`u16 encounterMusic:%d` can hold. It TRUNCATES rather than '
                 'failing to build, so the highest jingles silently become the '
                 'lowest ones' % (top, limit, ENCOUNTER_MUSIC_BITS))

    if _failures:
        for msg in _failures:
            print('FAIL  check_team_music.py: %s' % msg)
        return False

    spare = (1 << ENCOUNTER_MUSIC_BITS) - 1 - max(values or [0])
    print('ok    check_team_music.py: %d team classes each name their own '
          'jingle and battle track; %d encounter-music value(s) left of the %d '
          'a :%d field holds'
          % (len(TEAM_JINGLE), spare, (1 << ENCOUNTER_MUSIC_BITS) - 1,
             ENCOUNTER_MUSIC_BITS))
    return True


def selftest(repo):
    repo = Path(repo)
    base = {rel: (repo / rel).read_text(encoding='utf-8', errors='replace')
            for rel in SOURCES}

    cases = [
        ('Rocket goes back to wearing Aqua\'s jingle', PARTY,
         lambda s: s.replace('Music: Rocket', 'Music: Aqua')),
        ('one Magma grunt is filed under Aqua', PARTY,
         lambda s: s.replace('Class: Team Magma\nPic: Magma Grunt M\n'
                             'Gender: Male\nMusic: Magma',
                             'Class: Team Magma\nPic: Magma Grunt M\n'
                             'Gender: Male\nMusic: Aqua', 1)),
        ('the Rocket jingle constant is removed', TRAINERS_H,
         lambda s: s.replace('#define TRAINER_ENCOUNTER_MUSIC_ROCKET      14',
                             '#define TRAINER_ENCOUNTER_MUSIC_UNUSED_14   14', 1)),
        ('the jingle exists but resolves to nothing', SETUP_C,
         lambda s: s.replace('        case TRAINER_ENCOUNTER_MUSIC_ROCKET:\n'
                             '            music = MUS_RG_ENCOUNTER_ROCKET;\n'
                             '            break;\n', '', 1)),
        ('the Rocket battle track case is dropped', POKEMON_C,
         lambda s: s.replace('        case TRAINER_CLASS_TEAM_ROCKET_FRLG:\n'
                             '            return MUS_HGSS_BATTLE_TEAM_ROCKET;\n',
                             '', 1)),
        ('the battle track names a song that does not exist', POKEMON_C,
         lambda s: s.replace('return MUS_HGSS_BATTLE_TEAM_ROCKET;',
                             'return MUS_HGSS_BATTLE_TEAM_ROCKET_2;', 1)),
        ('a sixteenth jingle overflows the 4-bit field', TRAINERS_H,
         lambda s: s.replace('#define TRAINER_ENCOUNTER_MUSIC_ROCKET      14',
                             '#define TRAINER_ENCOUNTER_MUSIC_ROCKET      14\n'
                             '#define TRAINER_ENCOUNTER_MUSIC_GALACTIC    16', 1)),
    ]

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        for rel in SOURCES:
            (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
        for name, target, mutate in cases:
            mutated = dict(base)
            mutated[target] = mutate(base[target])
            if mutated[target] == base[target]:
                print('  SELFTEST INCONCLUSIVE (%s): mutation changed nothing'
                      % name)
                ok = False
                continue
            for rel, text in mutated.items():
                (tmp / rel).write_text(text, encoding='utf-8', newline='\n')
            if check(tmp):
                print('  SELFTEST FAILED (%s): the check still passed' % name)
                ok = False
            else:
                print('  selftest ok: fired on "%s"' % name)
    return ok


def main():
    args = list(sys.argv[1:])
    is_selftest = '--selftest' in args
    if is_selftest:
        args.remove('--selftest')
    # Accepts the repo both positionally and via --repo, so it cannot land on
    # the wrong side of run_all_checks.sh's hand-maintained list.
    if '--repo' in args:
        i = args.index('--repo')
        if i + 1 >= len(args):
            print('--repo needs a path')
            return 2
        repo = args[i + 1]
    else:
        repo = args[0] if args else '.'

    if is_selftest:
        print('--- selftest: breaking the rule on purpose ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1
    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
