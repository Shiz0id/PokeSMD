#!/usr/bin/env python3
"""Build the Safari Zone's species ladder out of what a run cannot otherwise get.

THE POOL IS A LADDER, NOT A LIST, for the same reason every theme pool is:
BuildWildEncounterTable reads a sliding WINDOW into it, so entries must be
sorted weakest to strongest or the window hands out a Dragonite on floor 3.

WHAT GOES IN IT is computed, not chosen: every enabled species that the run
cannot already obtain. "Obtain" means the fourteen theme pools plus the thirty
starter picks, CLOSED UNDER EVOLUTION - Crobat is obtainable because Zubat is,
and a list that forgets that would put half the roster in here twice over.

WHAT DECIDES ENABLED is the P_FAMILY_* toggles, not the species enum. A family
turned off still COMPILES - it becomes a zeroed gSpeciesInfo row that a wild
encounter rolls into - so this reads species_enabled.h and resolves each family
to its generation rather than assuming the enum is the roster.

THE ORDER IS BST ASCENDING AS A FIRST PASS AND BST LIES. It is a starting point
for a human to correct, which is why --report prints the evolution stage beside
it: a stage-1 species sorted above a fully evolved one is the signal to look.
Wobbuffet is the standing example - 405 BST that is almost all HP.

Usage:
    gen_safari_pool.py --report          what would go in, and what does not fit
    gen_safari_pool.py --write           emit include/constants/rogue_safari_pool.h
"""
import argparse
import re
import sys
from pathlib import Path
import sys as _sys
_sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parent))
from rom_paths import find_map

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from repo import REPO as _DEFAULT_REPO
except Exception:
    _DEFAULT_REPO = None

# Only the LAND surface is reliably reachable on these maps, and the numbers
# below follow from that:
#
#   land        12 slots  NUM_LAND_MONS_ENCOUNTER_SLOTS, always reachable
#   water        5 slots  needs Surf. Mudkip learns it at level 30 and is a
#                         starter pick, so SOME runs can and some cannot -
#                         treat anything here as optional content
#   fishing     10 slots  DEAD: no rod is given, sold or buried anywhere
#   rock smash   5 slots  DEAD: our map copies declare no object events, so
#                         there are no breakable rocks to smash
#
# THE WINDOWS ARE WIDER THAN THE SLOTS ON PURPOSE, and that only works because
# the encounter table RE-DEALS PER ROLL. The window then means "how many
# species can appear here at all" rather than "how many are present at once" -
# the same trick the ocean uses to get more than five species out of five
# water slots. 47 is chosen so that window + 115 - 1 covers the whole ladder
# and nothing has to be cut.
LAND_WINDOW = 47
WATER_WINDOW = 12

# Species the project has deliberately made unobtainable. Putting one here
# would quietly reverse a decision taken somewhere else, so they are named with
# the reason rather than filtered by a rule.
EXCLUDE = {
    'SPECIES_CLEFFA': 'breeding-only by design; "Clefairy and Clefable are all"',
    'SPECIES_PICHU': 'breeding-only baby of a starter line',
    'SPECIES_IGGLYBUFF': 'breeding-only baby',
    'SPECIES_AZURILL': 'breeding-only baby',
    'SPECIES_WYNAUT': 'breeding-only baby',
    'SPECIES_BUDEW': 'breeding-only baby',
    'SPECIES_TYROGUE': 'breeding-only baby',
    'SPECIES_MIME_JR': 'breeding-only baby',
    'SPECIES_HAPPINY': 'breeding-only baby',
    'SPECIES_MUNCHLAX': 'breeding-only baby',
    'SPECIES_BONSLY': 'breeding-only baby',
    'SPECIES_CHINGLING': 'breeding-only baby',
    'SPECIES_MANTYKE': 'breeding-only baby',
    'SPECIES_ELEKID': 'breeding-only baby',
    'SPECIES_MAGBY': 'breeding-only baby',
    'SPECIES_SMOOCHUM': 'breeding-only baby',
    'SPECIES_DITTO': 'transform has no meaning in a run and it cannot fight',
}

# Alternate forms, regional variants and the like. A form is not a species the
# player can meet in grass, and several are not in the enum as base entries at
# all.
FORM_SUFFIX = re.compile(
    r'_(MEGA|MEGA_X|MEGA_Y|MEGA_Z|GMAX|ALOLA|GALAR|HISUI|PALDEA|TOTEM|STARTER|'
    r'PRIMAL|ORIGIN|THERIAN|BLACK|WHITE|RESOLUTE|PIROUETTE|ZEN|ATTACK|DEFENSE|'
    r'SPEED|SUNNY|RAINY|SNOWY|SKY|SANDY|TRASH|PLANT|OVERCAST|EAST|WEST|'
    r'ALTERED|LAND|LOW_KEY|RAPID_STRIKE|COMBAT|BLAZE|AQUA|TWO_SEGMENT|'
    r'THREE_SEGMENT|BATTLE_BOND|ASH|BLOODMOON|SPIKY_EARED|NORMAL|'
    # Rotom's appliances. Each is a real, differently-typed species and none is
    # a base form, so all five have to go by name.
    r'FAN|WASH|HEAT|FROST|MOW|'
    # Cosplay Pikachu. Found by the link-map check, not by reading the enum -
    # they parse as ordinary species and only the missing front pic says so.
    r'COSPLAY|ROCK_STAR|BELLE|POP_STAR|PHD|LIBRE|'
    r'ORIGINAL_CAP|HOENN_CAP|SINNOH_CAP|UNOVA_CAP|KALOS_CAP|ALOLA_CAP|'
    r'PARTNER_CAP|WORLD_CAP|HOENN|KALOS|SINNOH|UNOVA|ORIGINAL|PARTNER|WORLD)$')

LEGENDARY_FLAGS = ('isRestrictedLegendary', 'isSubLegendary', 'isMythical',
                   'isUltraBeast', 'isParadox', 'isTotem', 'isMegaEvolution',
                   'isPrimalReversion')

STAT_FIELDS = ('baseHP', 'baseAttack', 'baseDefense', 'baseSpeed',
               'baseSpAttack', 'baseSpDefense')

# GEN_1 is 0, so GEN_N is N-1 (include/config/general.h).
GENS = {f'GEN_{i}': i - 1 for i in range(1, 10)}
GENS['GEN_LATEST'] = GENS['GEN_9']

# A STAT IS FREQUENTLY A TERNARY, NOT A NUMBER:
#
#     .baseAttack    = P_UPDATED_STATS >= GEN_6 ? 90 : 80,
#
# A digit-only pattern silently reads zero there, and zero is a plausible-
# looking answer - Beedrill came out at 225 against a real 395 and sorted
# among the Caterpies, which is exactly the wrong end of a ladder. This is the
# same trap check_starter_moves.py records for `.power`. Anything that cannot
# be resolved is COUNTED AND PRINTED rather than defaulted, because a silent
# zero here is indistinguishable from a weak Pokemon.
_TERNARY = re.compile(r'^(?P<cond>.+?)\s*\?\s*(?P<yes>\w+)\s*:\s*(?P<no>\w+)$')
_CMP = re.compile(r'^(P_\w+)\s*(>=|==|>|<=|<)\s*(GEN_\w+)$')


def eval_cond(expr, cfg):
    """A boolean over `P_SOMETHING <cmp> GEN_N`, joined by || and &&.

    Returns None if any term is unknown, so callers can report rather than
    guess. `P_UPDATED_STATS >= GEN_6 || P_UPDATED_STATS == GEN_1` is real and
    appears on Pikachu.
    """
    expr = expr.strip().strip('()')
    for sep, fold in (('||', any), ('&&', all)):
        if sep in expr:
            parts = [eval_cond(p, cfg) for p in expr.split(sep)]
            return None if any(p is None for p in parts) else fold(parts)
    m = _CMP.match(expr)
    if not m:
        return None
    left, right = cfg.get(m.group(1)), GENS.get(m.group(3))
    if left is None or right is None:
        return None
    op = m.group(2)
    return {'>=': left >= right, '==': left == right, '>': left > right,
            '<=': left <= right, '<': left < right}[op]


def read_config(repo):
    """P_UPDATED_* and friends, resolved to integers."""
    cfg = {}
    for name in ('pokemon.h', 'general.h'):
        path = repo / 'include/config' / name
        if not path.exists():
            continue
        for m in re.finditer(r'#define\s+(P_\w+)\s+(GEN_\w+|\d+)',
                             path.read_text()):
            val = m.group(2)
            cfg[m.group(1)] = GENS.get(val, None) if val.startswith('GEN_') \
                else int(val)
    return cfg


def eval_stat(expr, cfg, macros, depth=0):
    """A number, a per-species macro, or a ternary. None if unresolvable.

    Stats arrive in three shapes and only the first looks like a number:
      .baseHP        = 65,
      .baseSpAttack  = CHARIZARD_SP_ATK,                 <- a #define nearby
      .baseAttack    = P_UPDATED_STATS >= GEN_6 ? 90 : 80,
    """
    expr = expr.strip().rstrip(',').strip()
    # The macro bodies are parenthesised - `(P_UPDATED_STATS >= GEN_2 ? 109 :
    # 85)` - and an unstripped paren makes the ternary pattern miss, which is
    # the same silent zero all over again.
    while expr.startswith('(') and expr.endswith(')'):
        expr = expr[1:-1].strip()
    if depth > 4:
        return None
    if expr.isdigit():
        return int(expr)
    m = _TERNARY.match(expr)
    if m:
        cond = eval_cond(m.group('cond'), cfg)
        if cond is None:
            return None
        return eval_stat(m.group('yes') if cond else m.group('no'),
                         cfg, macros, depth + 1)
    if expr in macros:
        return eval_stat(macros[expr], cfg, macros, depth + 1)
    return None


def eval_types(token, cfg, macros, depth=0):
    """A TYPE_ name, or a macro/ternary that yields one.

    `MON_TYPES(TYPE_PSYCHIC, RALTS_FAMILY_TYPE2)` where the macro is
    `(P_UPDATED_TYPES >= GEN_6 ? TYPE_FAIRY : TYPE_PSYCHIC)`. Taking every
    TYPE_ on that line gives a species BOTH branches - Ralts came out
    Psychic/Fairy/Psychic - so the ternary has to be evaluated, not scraped.
    """
    token = token.strip()
    while token.startswith('(') and token.endswith(')'):
        token = token[1:-1].strip()
    if depth > 4:
        return []
    if token.startswith('TYPE_'):
        return [token]
    m = _TERNARY.match(token)
    if m:
        cond = eval_cond(m.group('cond'), cfg)
        if cond is None:
            return []
        return eval_types(m.group('yes') if cond else m.group('no'),
                          cfg, macros, depth + 1)
    if token in macros:
        return eval_types(macros[token], cfg, macros, depth + 1)
    return []


def collect_macros(text, cfg):
    """Every `#define` in the file whose branch is actually live.

    A PROPER STACK, not a single flag. The naive version reset to "active" on
    every #endif, so a nested block left the enclosing #else looking live and
    BOTH branches of a two-branch define were collected - which made Ralts
    Psychic/Fairy/Psychic and would have silently picked whichever stat macro
    came last. Conditions this cannot evaluate (#if P_FAMILY_X) are pushed as
    live so the nesting still balances.
    """
    macros = []
    stack = []  # (live, taken) per open #if
    for line in text.splitlines():
        m = re.match(r'\s*#if\s+(.+?)\s*$', line)
        if m:
            v = eval_cond(m.group(1), cfg)
            live = True if v is None else v
            stack.append([live, live])
            continue
        m = re.match(r'\s*#elif\s+(.+?)\s*$', line)
        if m and stack:
            v = eval_cond(m.group(1), cfg)
            live = (not stack[-1][1]) and (True if v is None else v)
            stack[-1][0] = live
            stack[-1][1] = stack[-1][1] or live
            continue
        if re.match(r'\s*#else', line) and stack:
            stack[-1][0] = not stack[-1][1]
            continue
        if re.match(r'\s*#endif', line) and stack:
            stack.pop()
            continue
        m = re.match(r'\s*#define\s+(\w+)\s+(.+)', line)
        if m and all(f[0] for f in stack):
            macros.append((m.group(1), m.group(2).strip()))
    # Raw text, resolved on use - a macro body may be a number, a TYPE_ name,
    # a brace list or a ternary over any of those.
    return dict(macros)


class Species:
    __slots__ = ('name', 'bst', 'types', 'flags', 'gen', 'stage')

    def __init__(self, name, bst, types, flags, gen):
        self.name, self.bst, self.types = name, bst, types
        self.flags, self.gen, self.stage = flags, gen, 0

    @property
    def short(self):
        return self.name.replace('SPECIES_', '')


# ------------------------------------------------------- reading the repo

def enabled_families(repo):
    """P_FAMILY_* -> bool, resolved through the generation toggles."""
    text = (repo / 'include/config/species_enabled.h').read_text()
    gens = {m.group(1): m.group(2) == 'TRUE' for m in
            re.finditer(r'#define\s+(P_GEN_\d_POKEMON)\s+(TRUE|FALSE)', text)}
    fam = {}
    for m in re.finditer(r'#define\s+(P_FAMILY_\w+)\s+(\S+)', text):
        name, val = m.group(1), m.group(2)
        fam[name] = (val == 'TRUE') if val in ('TRUE', 'FALSE') else gens.get(val, False)
    return gens, fam


def read_species(repo, fam, cfg, unresolved):
    """Every species whose family is on, with stats, types and flags."""
    out, evo = {}, {}
    info_dir = repo / 'src/data/pokemon/species_info'
    for path in sorted(info_dir.glob('gen_*_families.h')):
        gen = int(re.search(r'gen_(\d)', path.name).group(1))
        text = path.read_text()
        macros = collect_macros(text, cfg)
        stack, cur, body = [], None, []

        def flush():
            if cur is None:
                return
            blob = '\n'.join(body)
            total = 0
            for f in STAT_FIELDS:
                m = re.search(rf'\.{f}\s*=\s*([^,\n]+)', blob)
                if not m:
                    continue
                v = eval_stat(m.group(1), cfg, macros)
                if v is None:
                    unresolved.append(f'{cur}.{f} = {m.group(1).strip()}')
                else:
                    total += v
            tm = re.search(r'\.types\s*=\s*(?:MON_TYPES\((.*)\)|(\w+))', blob)
            types = ()
            if tm:
                raw = tm.group(1) if tm.group(1) is not None else tm.group(2)
                # Split on commas that are not inside parentheses - a ternary
                # branch carries its own.
                parts, depth_, buf = [], 0, ''
                for ch in raw:
                    if ch == '(':
                        depth_ += 1
                    elif ch == ')':
                        depth_ -= 1
                    if ch == ',' and depth_ == 0:
                        parts.append(buf)
                        buf = ''
                    else:
                        buf += ch
                parts.append(buf)
                seen = []
                for p in parts:
                    for t in eval_types(p, cfg, macros):
                        if t not in seen:
                            seen.append(t)
                types = tuple(seen)
            flags = {f for f in LEGENDARY_FLAGS
                     if re.search(rf'\.{f}\s*=\s*TRUE', blob)}
            out[cur] = Species(cur, total, types, flags, gen)
            # EVOLUTION(...) SPANS LINES and its branches sit behind their own
            # #if guards. Taking only the first line loses every branch after
            # it - which made seven of the eight eeveelutions look
            # unobtainable and put them in a pool of things the run cannot
            # get, when Eevee is a starter pick. Read to the balanced paren.
            i = blob.find('.evolutions')
            if i != -1:
                j = blob.find('EVOLUTION(', i)
                if j != -1:
                    k, d = j + len('EVOLUTION('), 1
                    while k < len(blob) and d:
                        d += (blob[k] == '(') - (blob[k] == ')')
                        k += 1
                    names = re.findall(r'SPECIES_\w+', blob[j:k])
                    evo[cur] = [n for n in names if n != cur]

        # A FRAME FOR EVERY #if, not just the family ones. Pushing only
        # P_FAMILY_ gates and popping on any #endif means a nested
        # `#if P_UPDATED_STATS ... #endif` closes the FAMILY frame early, and
        # every species after it reads as enabled. That is how Kingambit -
        # whose family is Gen 5, and Gen 5 is off - got into the ladder. A
        # disabled family still compiles to a zeroed gSpeciesInfo row that a
        # wild encounter will happily roll into, so this is the difference
        # between a pool and a pool with holes in it.
        for line in text.splitlines():
            m = re.match(r'\s*#if(?:def)?\s+(.+?)\s*$', line)
            if m:
                fm = re.match(r'^(P_FAMILY_\w+)$', m.group(1).strip())
                stack.append([fam.get(fm.group(1), False) if fm else True, bool(fm)])
                continue
            if re.match(r'\s*#else', line) and stack:
                # Only meaningful for a family gate; an #else on anything else
                # stays live either way.
                if stack[-1][1]:
                    stack[-1][0] = not stack[-1][0]
                continue
            if re.match(r'\s*#endif', line) and stack:
                stack.pop()
                continue
            m = re.match(r'\s*\[(SPECIES_\w+)\]\s*=', line)
            if m:
                flush()
                on = all(f[0] for f in stack)
                cur, body = (m.group(1) if on else None), []
                continue
            if cur is not None:
                body.append(line)
        flush()
    return out, evo


def obtainable(repo, species, evo):
    """Theme pools + starters, closed under evolution."""
    rd = (repo / 'src/rogue_dungeon.c').read_text()
    pool = set()
    for _, blob in re.findall(r'static const u16 (s\w*Species)\[\] =\s*\{(.*?)\};',
                              rd, re.S):
        pool |= set(re.findall(r'SPECIES_\w+', blob))
    st = (repo / 'include/constants/rogue_dungeon_starters.h').read_text()
    pool |= set(re.findall(r'\{\s*(SPECIES_\w+)', st))

    reach, frontier = set(), []
    for s in pool:
        if s in species:
            reach.add(s)
            frontier.append(s)
    while frontier:
        for t in evo.get(frontier.pop(), []):
            if t in species and t not in reach:
                reach.add(t)
                frontier.append(t)
    return reach


def tag_stages(species, evo):
    """How many evolutions deep a species is, for spotting BST lies."""
    parent = {}
    for src, tgts in evo.items():
        for t in tgts:
            parent[t] = src
    for name, sp in species.items():
        depth, cur, guard = 0, name, 0
        while cur in parent and guard < 5:
            cur = parent[cur]
            depth += 1
            guard += 1
        sp.stage = depth


# ------------------------------------------------------------ the ladder

def build_pools(repo):
    _, fam = enabled_families(repo)
    cfg = read_config(repo)
    unresolved = []
    species, evo = read_species(repo, fam, cfg, unresolved)
    tag_stages(species, evo)
    reach = obtainable(repo, species, evo)

    dropped = {}
    cands = []
    for name, sp in species.items():
        if name in reach:
            continue
        if name in EXCLUDE:
            dropped.setdefault(EXCLUDE[name], []).append(sp)
            continue
        if FORM_SUFFIX.search(name) or name.startswith('SPECIES_UNOWN_'):
            dropped.setdefault('an alternate form, not a species met in grass',
                               []).append(sp)
            continue
        if sp.flags:
            dropped.setdefault('legendary / mythical / special form',
                               []).append(sp)
            continue
        if sp.bst == 0:
            dropped.setdefault('no stats - family disabled or a stub',
                               []).append(sp)
            continue
        cands.append(sp)

    # TYPE_WATER goes to the water ladder, everything else to land. A first cut
    # for review, not a claim: several water types are perfectly ordinary on
    # land and the split is the easiest thing here to argue about.
    water = sorted((s for s in cands if 'TYPE_WATER' in s.types),
                   key=lambda s: (s.bst, s.short))
    land = sorted((s for s in cands if 'TYPE_WATER' not in s.types),
                  key=lambda s: (s.bst, s.short))
    return land, water, dropped, species, unresolved


def verify_against_link_map(repo, pools):
    """Every species in the ladder really is in the ROM.

    THE ONLY EVIDENCE A SPECIES SURVIVED THE FAMILY TOGGLES is its
    gMonFrontPic_* symbol in pokeemerald.map. Everything else - the enum, the
    build succeeding, this script's own parse - is satisfied by a species that
    compiles to a zeroed gSpeciesInfo row, which a wild encounter will roll
    into and hand the player a blank. This check found Kingambit and
    Chandelure in a ladder that had every reason to look correct.

    Needs a built ROM; skipped with a warning if there is not one.
    """
    mapfile = find_map(repo)
    if not mapfile.exists():
        return None
    present = set(re.findall(r'gMonFrontPic_(\w+)', mapfile.read_text(errors='replace')))
    missing = []
    for pool in pools:
        for s in pool:
            sym = ''.join(p.capitalize() for p in s.short.split('_'))
            if sym not in present:
                missing.append((s.short, sym))
    return missing


def capacity(repo, window):
    """reachable pool = window + span - 1, with span the whole run.

    THE SAFARI TIER COUNTS FROM THE ABSOLUTE RUN FLOOR, which is the opposite
    of what BuildWildEncounterTable does and right for the opposite reason: a
    theme pool is written for one dungeon's depth, this one spans the run. Do
    not "fix" it to DungeonFloorWithin.
    """
    h = (repo / 'include/rogue_dungeon.h').read_text()
    long_f = int(re.search(r'#define DUNGEON_LONG_FLOORS\s+(\d+)', h).group(1))
    short_f = int(re.search(r'#define DUNGEON_SHORT_FLOORS\s+(\d+)', h).group(1))
    gym_d = int(re.search(r'#define DUNGEON_GYM_DUNGEONS\s+(\d+)', h).group(1))
    e4_d = int(re.search(r'#define DUNGEON_E4_DUNGEONS\s+(\d+)', h).group(1))
    total = gym_d * long_f + e4_d * short_f + long_f
    return window + total - 1, total


# ------------------------------------------------------------- reporting

def report(repo):
    land, water, dropped, _, unresolved = build_pools(repo)
    land_cap, floors = capacity(repo, LAND_WINDOW)
    water_cap, _ = capacity(repo, WATER_WINDOW)

    print(f'run is {floors} floors\n')
    if unresolved:
        print(f'!! {len(unresolved)} STAT(S) COULD NOT BE RESOLVED - the ladder '
              f'below is wrong wherever they appear:')
        for u in unresolved[:10]:
            print(f'   {u}')
        print()
    missing = verify_against_link_map(repo, (land, water))
    if missing is None:
        print('   (no pokeemerald.map - link-map verification SKIPPED)\n')
    elif missing:
        print(f'!! {len(missing)} SPECIES ARE NOT IN THE ROM - their family is '
              f'off and they would roll as a zeroed row:')
        for short, sym in missing[:15]:
            print(f'   {short}  (looked for gMonFrontPic_{sym})')
        print()
    else:
        print('   link map: every species in both ladders is really in the ROM\n')

    print(f'{"LAND":6} {len(land):4} candidates, capacity {land_cap} '
          f'at window {LAND_WINDOW}')
    print(f'{"WATER":6} {len(water):4} candidates, capacity {water_cap} '
          f'at window {WATER_WINDOW}   (needs Surf - optional content)')
    print()
    for label, pool, cap in (('LAND', land, land_cap), ('WATER', water, water_cap)):
        over = len(pool) - cap
        print(f'--- {label}: {"fits" if over <= 0 else f"OVER BY {over}"} ---')
        prev = 0
        for i, s in enumerate(pool):
            mark = ''
            # A fully evolved species sorted below a first-stage one is where
            # BST is lying. Not an error - a prompt to look.
            if i and s.stage < pool[i - 1].stage - 1:
                mark = '  <-- stage drops, check the order'
            if i == cap:
                print(f'    ---- capacity {cap} ends here; {len(pool) - cap} '
                      f'below this line are unreachable ----')
            print(f'  {i:3} {s.short:<16} {s.bst:4}  st{s.stage} '
                  f'{"/".join(t.replace("TYPE_", "") for t in s.types):<14}{mark}')
            prev = s.bst
        print()
    print('--- dropped ---')
    for reason, items in sorted(dropped.items()):
        print(f'  {len(items):3}  {reason}')
        if len(items) <= 12:
            print('       ' + ', '.join(s.short for s in items))


HEADER = '''#ifndef GUARD_CONSTANTS_ROGUE_SAFARI_POOL_H
#define GUARD_CONSTANTS_ROGUE_SAFARI_POOL_H

// GENERATED by tools/rogue/gen_safari_pool.py - do not edit by hand.
//
// The Safari Zone's species, ordered weakest to strongest because
// the encounter table reads a sliding WINDOW into this, not a prefix.
//
// Every entry is a species the run cannot obtain any other way -
// not in any theme pool, not a starter, and not reachable by
// evolving one that is. Legendaries, alternate forms and the
// breeding-only babies are excluded; see the script.
//
// THE TIER COUNTS FROM THE ABSOLUTE RUN FLOOR, unlike every theme
// pool, which counts from the floor within its own dungeon. That is
// deliberate: a theme pool is written for one depth, this one spans
// the whole run. See capacity() in the generator.

'''


def write(repo):
    land, water, _, _, unresolved = build_pools(repo)
    if unresolved:
        raise SystemExit(f'{len(unresolved)} stat(s) unresolved - refusing to '
                         f'emit a ladder sorted on numbers that are wrong. '
                         f'Run --report to see them.')
    missing = verify_against_link_map(repo, (land, water))
    if missing:
        raise SystemExit(f'{len(missing)} species are not in the ROM (first: '
                         f'{missing[0][0]}) - refusing to emit a pool that '
                         f'would roll a zeroed species_info row.')
    land_cap, _ = capacity(repo, LAND_WINDOW)
    water_cap, _ = capacity(repo, WATER_WINDOW)
    body = HEADER
    for label, pool, cap, window in (('Land', land, land_cap, LAND_WINDOW),
                                     ('Water', water, water_cap, WATER_WINDOW)):
        kept = pool[:cap]
        body += (f'// {len(kept)} species, window {window}.\n'
                 f'static const u16 sSafari{label}Species[] =\n{{\n')
        for i in range(0, len(kept), 4):
            row = kept[i:i + 4]
            body += '    ' + ' '.join(f'{s.name},' for s in row)
            body += f'  // {row[0].bst}-{row[-1].bst}\n'
        body += '};\n\n'
    body += '#endif // GUARD_CONSTANTS_ROGUE_SAFARI_POOL_H\n'
    out = repo / 'include/constants/rogue_safari_pool.h'
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(body)
    print(f'wrote {out}')
    print(f'  land {min(len(land), land_cap)}, water {min(len(water), water_cap)}')


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=_DEFAULT_REPO)
    ap.add_argument('--write', action='store_true')
    args = ap.parse_args(argv)
    if args.repo is None:
        raise SystemExit('--repo is required')
    repo = Path(args.repo)
    if args.write:
        write(repo)
    else:
        report(repo)
    return 0


if __name__ == '__main__':
    sys.exit(main())
