"""Every weather a project map asks for, against every table indexed by it.

This is gap 22, and it exists because a novel weather fails in FIVE separate
places, none near the others, and ALL OF THEM SILENTLY. Nothing here would
crash, warn, or fail to build; the floor just loads wrong.

  1. TranslateWeatherNum. The one that bites. Without a `case` a map header
     asking for the weather falls through to `default: return WEATHER_NONE`
     and absolutely nothing happens - the map is simply not weathered.

  2. sWeatherFuncs. Indexed with NO bounds check. Vanilla's rows stop at 15.

  3. gWeatherStartsStringIds. Sized to WEATHER_COUNT on purpose after a
     blizzard read two entries past the end of a self-sized array and printed
     "Aargh! Almost had it!" in battle. An id past the end is out of bounds; an
     id inside it with no entry reads a defined 0, which is merely wrong.

  4. The id being inside WEATHER_COUNT at all. 16-19 were a free gap and are
     now full. Anything past 19 needs WEATHER_COUNT raised with it.

  5. battle_util.c's overworld-to-battle switch - MECHANICAL vs COSMETIC.
     A new weather is cosmetic until it is in that switch, and that is a
     decision rather than a default. It is checked against the declared
     intent below rather than merely reported, because the dangerous case is
     not omission in general: it is a weather REPLACING a mechanical one on a
     map that had it, where the default silently removes a battle effect. The
     blizzard is exactly that - WEATHER_SNOW already gave Glacia's floors an
     Ice-type Defence boost, and a blizzard missing from the switch would have
     taken it away on her own arena.

WHAT IT WALKS. Every `weather` field of every data/maps/Rogue*/map.json, so the
set is discovered rather than listed - a sixth weather map is covered the day
it is added, without touching this file. The vanilla maps are deliberately out
of scope; their weathers are vanilla's problem and are all long since correct.

--selftest breaks each of the five in turn and confirms this fires, because a
check that has never failed is worth nothing. It is not enough for the check to
be wrong in the selftest: it has to be wrong in the specific way the real bug
would be wrong, so each break below reproduces an actual failure mode rather
than merely corrupting the input.

Usage:  python3 tools/rogue/check_weather_ids.py [--repo PATH] [--selftest]
"""
import argparse
import json
import re
import sys
from pathlib import Path

# What each project weather is SUPPOSED to be. Mechanical means present in the
# overworld-to-battle switch in battle_util.c. Keeping the intent here rather
# than inferring it from the source is the entire value of checking item 5 -
# inferred intent can only ever agree with whatever the code currently does.
INTENT = {
    # Ours.
    'WEATHER_PETALS':   'cosmetic',    # Wallace's floor stays mechanically plain
    'WEATHER_MONSOON':  'mechanical',  # every rain sets B_WEATHER_RAIN_NORMAL
    'WEATHER_BLIZZARD': 'mechanical',  # or Glacia loses the boost SNOW gave her
    'WEATHER_LEAVES':   'cosmetic',    # dungeon 1, and the woods had no weather
    # The cave, dungeon 2, and the first id past the 16-19 gap - WEATHER_COUNT
    # moved from 24 to 25 for it. Cosmetic for the same reason as the leaves:
    # the theme had no weather at all before, so omission from the battle switch
    # removes nothing. Its art is a follower Pokemon's overworld sprite rather
    # than a weather sheet, which costs this check nothing - every table below
    # is indexed by the id, and the id is an ordinary one.
    'WEATHER_ZUBATS':   'cosmetic',
    # The ocean, and the second id past the gap. Cosmetic for the same reason as
    # the zubats and the leaves: the theme had no weather at all before, so
    # omission from the battle switch removes nothing. Shares its entire
    # implementation with the zubats - see the FLIERS block in
    # field_weather_effect.c - which costs this check nothing, because every
    # table it holds ids against is indexed by the id and these are ordinary.
    'WEATHER_SEABIRDS': 'cosmetic',
    # Vanilla's, but a project map asks for each of them, so the intent is ours
    # to state. Adding these was not bookkeeping - none of it was written down
    # anywhere before this check demanded it.
    'WEATHER_SNOW':     'mechanical',  # Glacia's three approach floors, 1.5x Ice Def
    'WEATHER_UNDERWATER_BUBBLES': 'cosmetic',
    # VANILLA'S AGAIN, and both arrived the same way: a theme was moved onto a
    # weather map, and the weather came with whatever battle behaviour the
    # vanilla constant already had. That is the reason these two rows matter
    # more than the ones above. A weather written here is a decision at every
    # step; a vanilla weather adopted for its art brings its switch membership
    # silently, in EITHER direction, and nothing about adding a map says which.
    'WEATHER_VOLCANIC_ASH': 'cosmetic',    # Fiery Path; not in the switch, and
                                           # the theme had no weather to lose
    # Mirage Tower, and the one row here that changes how a dungeon PLAYS.
    # WEATHER_SANDSTORM is already a case in battle_util.c and sets
    # B_WEATHER_SANDSTORM, so every battle on that dungeon now chips 1/16 max HP
    # a turn off anything not Rock, Ground or Steel -- the player's team
    # included -- and hands Rock types 1.5x Special Defence. The exact inverse
    # of the blizzard three rows up: there the default would have silently
    # REMOVED a battle effect, here the vanilla constant silently ADDS one.
    'WEATHER_SANDSTORM': 'mechanical',
    # In the switch, but READ THE GATE: its case body is `if (B_OVERWORLD_FOG ==
    # GEN_4)`, and this build is GEN_LATEST, so it sets no gBattleWeather at
    # all. What actually makes Phoebe's floors mechanical is the separate
    # B_OVERWORLD_FOG >= GEN_8 branch a few lines above, which sets MISTY
    # TERRAIN instead. Counted mechanical here because it is, but not by the
    # route the label suggests - if fog ever stops feeling like it does
    # something, that gate is where to look and not this table.
    'WEATHER_FOG_HORIZONTAL': 'mechanical',
}


def read(repo, rel):
    return (repo / rel).read_text(errors='replace')


def weather_constants(text):
    return {m.group(1): int(m.group(2))
            for m in re.finditer(r'#define\s+(WEATHER_[A-Z0-9_]+)\s+(\d+)\b', text)}


def project_weathers(repo):
    """Every weather named by a Rogue* map header, with the maps asking for it."""
    out = {}
    for p in sorted((repo / 'data/maps').glob('Rogue*/map.json')):
        w = json.loads(p.read_text()).get('weather')
        if w and w != 'WEATHER_NONE':
            out.setdefault(w, []).append(p.parent.name)
    return out


def designated_keys(text, table):
    """The [WEATHER_X] = labels of a designated-initialiser table."""
    # (\[...\])* and not (\[...\])? -- sWeatherNames is [WEATHER_COUNT][24], and
    # matching only the first dimension finds nothing at all.
    m = re.search(re.escape(table) + r'(?:\s*\[[^\]]*\])*\s*=\s*\{(.*?)\n\};',
                  text, re.S)
    if not m:
        sys.exit('could not find %s' % table)
    return set(re.findall(r'\[\s*(WEATHER_[A-Z0-9_]+)\s*\]', m.group(1)))


def translate_cases(text):
    """Weathers TranslateWeatherNum returns as themselves.

    A `case X: return Y;` where Y is not X is a cycle or an aggregate, not an
    identity, so matching the pair rather than just the label is what stops
    this passing on a case that quietly maps somewhere else.
    """
    m = re.search(r'static u8 TranslateWeatherNum\(u8 weather\)\s*\{(.*?)\n\}',
                  text, re.S)
    if not m:
        sys.exit('could not find TranslateWeatherNum')
    return {a for a, b in re.findall(
        r'case\s+(WEATHER_[A-Z0-9_]+)\s*:\s*return\s+(WEATHER_[A-Z0-9_]+)\s*;',
        m.group(1)) if a == b}


def battle_cases(text):
    """Weathers labelled in battle_util.c's FIELD_EFFECT_OVERWORLD_WEATHER switch."""
    m = re.search(r'case FIELD_EFFECT_OVERWORLD_WEATHER:(.*?)\n        break;',
                  text, re.S)
    if not m:
        sys.exit('could not find the overworld-to-battle weather switch')
    return set(re.findall(r'case\s+(WEATHER_[A-Z0-9_]+)\s*:', m.group(1)))


def check(src, quiet=False):
    consts = weather_constants(src['weather.h'])
    count = consts.get('WEATHER_COUNT')
    funcs = designated_keys(src['field_weather.c'], 'sWeatherFuncs')
    names = designated_keys(src['field_weather.c'], 'sWeatherNames')
    strids = designated_keys(src['battle_message.c'], 'gWeatherStartsStringIds')
    translated = translate_cases(src['field_weather_effect.c'])
    mechanical = battle_cases(src['battle_util.c'])

    problems = []
    for name, maps in sorted(src['weathers'].items()):
        where = ', '.join(maps)
        idn = consts.get(name)
        if idn is None:
            problems.append(f'{name}: no #define (asked for by {where})')
            continue
        if count is not None and idn >= count:
            problems.append(
                f'{name} = {idn} is >= WEATHER_COUNT ({count}); the tables '
                f'sized by it do not reach it (asked for by {where})')
        if name not in translated:
            problems.append(
                f'{name}: no identity case in TranslateWeatherNum -- the map '
                f'header falls through to WEATHER_NONE and nothing happens '
                f'({where})')
        if name not in funcs:
            problems.append(f'{name}: no sWeatherFuncs row ({where})')
        if name not in strids:
            problems.append(f'{name}: no gWeatherStartsStringIds entry ({where})')
        if name not in names:
            problems.append(f'{name}: no sWeatherNames entry, debug menu only')

        want = INTENT.get(name)
        if want is None:
            problems.append(
                f'{name} is used by {where} but has no INTENT entry in this '
                f'check -- say whether it should be mechanical or cosmetic')
        else:
            is_mech = name in mechanical
            if want == 'mechanical' and not is_mech:
                problems.append(
                    f'{name} is declared mechanical but is absent from '
                    f"battle_util.c's switch -- it is cosmetic in game ({where})")
            if want == 'cosmetic' and is_mech:
                problems.append(
                    f'{name} is declared cosmetic but IS in battle_util.c\'s '
                    f'switch -- it sets gBattleWeather ({where})')

    if not quiet:
        for name in sorted(src['weathers']):
            idn = consts.get(name)
            kind = 'mechanical' if name in mechanical else 'cosmetic'
            print(f'  {name:<20} = {str(idn):>3}  {kind:<10} '
                  f'{", ".join(src["weathers"][name])}')
    return problems


def load(repo):
    return {
        'weather.h': read(repo, 'include/constants/weather.h'),
        'field_weather.c': read(repo, 'src/field_weather.c'),
        'field_weather_effect.c': read(repo, 'src/field_weather_effect.c'),
        'battle_message.c': read(repo, 'src/battle_message.c'),
        'battle_util.c': read(repo, 'src/battle_util.c'),
        'weathers': project_weathers(repo),
    }


# Each break reproduces a real failure mode: the missing case, the missing row,
# the short table, the id past the end, and the mechanical weather that quietly
# went cosmetic.
BREAKS = [
    ('TranslateWeatherNum case removed', 'field_weather_effect.c',
     lambda t: t.replace('    case WEATHER_LEAVES:             return WEATHER_LEAVES;\n', '')),
    ('sWeatherFuncs row removed', 'field_weather.c',
     lambda t: re.sub(r'\n\s*\[WEATHER_LEAVES\]\s*=\s*\{[^}]*\},', '', t, count=1)),
    ('gWeatherStartsStringIds entry removed', 'battle_message.c',
     lambda t: re.sub(r'\n\s*\[WEATHER_LEAVES\][^\n]*,', '', t, count=1)),
    ('id moved past WEATHER_COUNT', 'weather.h',
     lambda t: t.replace('#define WEATHER_LEAVES                  19',
                         '#define WEATHER_LEAVES                  99')),
    ('mechanical weather dropped from the battle switch', 'battle_util.c',
     lambda t: t.replace('            case WEATHER_BLIZZARD:\n', '')),
]


def selftest(repo):
    base = load(repo)
    if check(base, quiet=True):
        sys.exit('FAIL: selftest needs a clean tree to start from')
    ok = True
    for label, key, breaker in BREAKS:
        broken = dict(base)
        broken[key] = breaker(base[key])
        if broken[key] == base[key]:
            print(f'  INCONCLUSIVE  {label}: the break did not change the source')
            ok = False
            continue
        found = check(broken, quiet=True)
        print(f'  {"detected  " if found else "MISSED    "}{label}')
        if found:
            print(f'                -> {found[0]}')
        ok = ok and bool(found)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=None)
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    repo = Path(a.repo) if a.repo else Path(__file__).resolve().parents[2]

    if a.selftest:
        print('selftest: breaking each failure mode in turn')
        if not selftest(repo):
            print('FAIL: this check cannot detect a break it is supposed to')
            return 1
        print('PASS: every break was detected')
        return 0

    print('project weathers:')
    problems = check(load(repo))
    if problems:
        print()
        for p in problems:
            print('FAIL: ' + p)
        return 1
    print('PASS: every project weather is in all five places and matches intent')
    return 0


if __name__ == '__main__':
    sys.exit(main())
