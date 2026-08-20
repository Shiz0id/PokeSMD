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

  6. A DYNAMIC POOL'S MEMBERS. WEATHER_DYNAMIC is not a weather, it is an
     AGGREGATE: TranslateWeatherNum hands it to GetDynamicWeather(), which
     picks one entry of a pool chosen by mapSec. So the aggregate itself has
     no sWeatherFuncs row and no string id and must not be checked for one -
     but every weather its pools can RETURN needs all of them, and needs an
     INTENT entry of its own, because that is where the battle behaviour of a
     dynamic floor actually comes from. A pool is also the easiest place in
     the tree to change how a dungeon plays by accident: adding one line to an
     array can hand a theme sandstorm chip damage with nothing else edited and
     nothing to see in any map header.

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
    # POOL MEMBERS. These four are reachable only through a dynamic pool - no
    # map header names any of them - so nothing above would ever have asked
    # about them. That is precisely why item 6 exists.
    'WEATHER_RAIN':          'mechanical',  # B_WEATHER_RAIN_NORMAL, as monsoon
    'WEATHER_SUNNY_CLOUDS':  'cosmetic',    # absent from the switch
    'WEATHER_SHADE':         'cosmetic',    # absent from the switch
    # The other half of the fog pair, and mechanical by the same indirect route
    # spelled out under WEATHER_FOG_HORIZONTAL: not the GEN_4 gate its case body
    # suggests, but the B_OVERWORLD_FOG >= GEN_8 branch that sets MISTY TERRAIN.
    'WEATHER_FOG_DIAGONAL':  'mechanical',
    # AN AGGREGATE, not a weather. It never reaches battle_util.c's switch
    # itself and never should - what reaches battle is whatever its pool
    # resolved to that floor, and every one of those carries its own row above.
    # Declaring it mechanical or cosmetic would be stating something about a
    # value that is never the current weather by the time anything looks.
    'WEATHER_DYNAMIC':  'aggregate',
}

# Weathers that RESOLVE to another weather instead of being one. Their
# TranslateWeatherNum case must NOT return themselves, and they are exempt from
# the sWeatherFuncs and gWeatherStartsStringIds rows that a concrete weather
# needs - the weather they resolve to is what gets indexed.
AGGREGATES = {'WEATHER_DYNAMIC'}


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


def translate_returns(text):
    """{weather: the expression its TranslateWeatherNum case returns}.

    Deliberately loose on the right-hand side: an aggregate returns a CALL
    (GetDynamicWeather()) or a table lookup, not a WEATHER_ constant, and a
    pattern that only matched constants would report the aggregate as having no
    case at all - which is the same message a genuinely missing case produces
    and would send the reader hunting for the wrong bug.
    """
    m = re.search(r'static u8 TranslateWeatherNum\(u8 weather\)\s*\{(.*?)\n\}',
                  text, re.S)
    if not m:
        sys.exit('could not find TranslateWeatherNum')
    return {a: b.strip() for a, b in re.findall(
        r'case\s+(WEATHER_[A-Z0-9_]+)\s*:\s*return\s+([^;]+);', m.group(1))}


def translate_cases(text):
    """Weathers TranslateWeatherNum returns as themselves."""
    return {a for a, b in translate_returns(text).items() if a == b}


def strip_comments(text):
    """Comments name weathers in prose; counting those would be nonsense."""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def dynamic_pools(text):
    """{array symbol: [weathers it can return]}."""
    out = {}
    # Strip first, then scan. Upstream leaves sDynamicWeathers_DewfordTown in
    # the file wrapped in a block comment as an example, and a regex over the
    # raw text matches it happily - which reported a real-looking failure about
    # a pool that does not exist.
    for m in re.finditer(
            r'static const u8 (sDynamicWeathers_\w+)\[\]\s*=\s*\{(.*?)\n\};',
            strip_comments(text), re.S):
        out[m.group(1)] = re.findall(r'\b(WEATHER_[A-Z0-9_]+)\b', m.group(2))
    return out


def pool_rows(text):
    """[(mapSec, array symbol)] - which theme selects which pool."""
    m = re.search(r'sDynamicWeatherPools\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if not m:
        sys.exit('could not find sDynamicWeatherPools')
    return re.findall(
        r'\{\s*(MAPSEC_[A-Z0-9_]+)\s*,\s*DYNAMIC_WEATHER_POOL\(\s*(\w+)\s*\)',
        strip_comments(m.group(1)))


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
    returns = translate_returns(src['field_weather_effect.c'])
    mechanical = battle_cases(src['battle_util.c'])
    pools = dynamic_pools(src['field_weather_effect.c'])
    rows = pool_rows(src['field_weather_effect.c'])

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
        if name in AGGREGATES:
            # An aggregate is exempt from the funcs row and the string id, but
            # it still has to HAVE a case, and that case must not be an
            # identity - one that returned itself would be an infinite
            # indirection that resolves to nothing.
            if name not in returns:
                problems.append(
                    f'{name}: no case in TranslateWeatherNum -- the map header '
                    f'falls through to WEATHER_NONE and nothing happens ({where})')
            elif name in translated:
                problems.append(
                    f'{name} is listed in AGGREGATES but its TranslateWeatherNum '
                    f'case returns itself, so it resolves to no real weather '
                    f'({where})')
        else:
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
        elif want == 'aggregate':
            if name not in AGGREGATES:
                problems.append(
                    f'{name} is declared an aggregate in INTENT but is not in '
                    f'AGGREGATES, so it is still being checked as a real weather')
            if name in mechanical:
                problems.append(
                    f'{name} is an aggregate but appears in battle_util.c\'s '
                    f'switch -- battle sees the RESOLVED weather, never this one')
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

    # 6. Everything a pool can hand back is a REAL weather, fully wired, with a
    # declared intent. This is the assertion the rest of the file cannot make:
    # a pool member never appears in any map header, so project_weathers never
    # sees it and none of the five checks above would ever look at it.
    seen_pools = set()
    for mapsec, sym in rows:
        seen_pools.add(sym)
        members = pools.get(sym)
        if members is None:
            problems.append(
                f'{sym} is selected by {mapsec} but no such array exists')
            continue
        if not members:
            problems.append(
                f'{sym} ({mapsec}) is empty, so the theme gets WEATHER_NONE')
        for w in members:
            idn = consts.get(w)
            if idn is None:
                problems.append(f'{sym} ({mapsec}) lists {w}, which has no #define')
                continue
            if count is not None and idn >= count:
                problems.append(
                    f'{sym} ({mapsec}) lists {w} = {idn}, past WEATHER_COUNT ({count})')
            if w not in translated:
                problems.append(
                    f'{sym} ({mapsec}) lists {w}, which has no identity case in '
                    f'TranslateWeatherNum -- rolling it gives WEATHER_NONE')
            if w not in funcs:
                problems.append(
                    f'{sym} ({mapsec}) lists {w}, which has no sWeatherFuncs row '
                    f'-- rolling it indexes past the table')
            if w not in strids:
                problems.append(
                    f'{sym} ({mapsec}) lists {w}, which has no '
                    f'gWeatherStartsStringIds entry')
            if w not in INTENT:
                problems.append(
                    f'{sym} ({mapsec}) lists {w}, which has no INTENT entry -- a '
                    f'pool member decides how a dungeon PLAYS, so say whether it '
                    f'is mechanical or cosmetic')

    for sym in sorted(set(pools) - seen_pools):
        problems.append(
            f'{sym} is defined but no sDynamicWeatherPools row selects it -- '
            f'the theme it was written for is getting WEATHER_NONE')

    if not quiet:
        for mapsec, sym in rows:
            ms = ', '.join(pools.get(sym, []))
            print(f'  {mapsec:<26} {ms}')
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
    # Item 6. A pool member is invisible to every other check here, so these
    # break the pool rather than the tables: an undeclared weather appearing in
    # a pool is how a dungeon's battle behaviour changes with nothing in any map
    # header to show for it.
    ('undeclared weather added to a pool', 'field_weather_effect.c',
     lambda t: t.replace('    WEATHER_SHADE,            // cosmetic\n    WEATHER_ZUBATS,',
                         '    WEATHER_SHADE,            // cosmetic\n'
                         '    WEATHER_DROUGHT,\n    WEATHER_ZUBATS,')),
    ('pool member with no sWeatherFuncs row', 'field_weather.c',
     lambda t: re.sub(r'\n\s*\[WEATHER_SHADE\]\s*=\s*\{[^}]*\},', '', t, count=1)),
    ('a pool nothing selects', 'field_weather_effect.c',
     lambda t: t.replace(
         '    { MAPSEC_ROGUE_MURKYCAVE, DYNAMIC_WEATHER_POOL(sDynamicWeathers_MurkyCave) },\n', '')),
    ('aggregate case removed from TranslateWeatherNum', 'field_weather_effect.c',
     lambda t: t.replace('    case WEATHER_DYNAMIC:            return GetDynamicWeather();\n', '')),
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
    print('PASS: every project weather is in all five places, every dynamic\n      pool member is a real weather, and both match intent')
    return 0


if __name__ == '__main__':
    sys.exit(main())
