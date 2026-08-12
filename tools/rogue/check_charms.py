"""Is the charm table complete, consistent, and reachable by the code that runs it?

sCharms in src/rogue_charms.c is indexed by ROGUE_CHARM_*, and everything that
touches a charm - the battle-start applier, the announcer, the script wrappers -
trusts that indexing. A row that is missing, or that names an effect kind the
applier has no branch for, or that carries a magnitude of zero, does not fail to
build. It produces a charm the player is told about that then does nothing, which
is the exact failure this project keeps paying for.

What it checks:

  1. Every id from ROGUE_CHARM_NONE+1 to ROGUE_CHARM_COUNT-1 has a row, and no
     row exists for an id past the end.
  2. Every row names an effect kind that ApplyStatDrops or RogueCharm_OnBattleStart
     actually handles. A kind with no applier is a silent no-op.
  3. Every non-placeholder row has a name, a description and a battle string id -
     the visibility rule, enforced rather than remembered.
  4. Every row's magnitude is non-zero, and STAT_DROP rows name at least one stat.
  5. gRogueCharmStringIds covers every id, because printfromtable indexes it by
     charm id and a short table reads off the end.
  6. Every STRINGID_ROGUECHARM_* named in the table exists in battle_message.c.

  7. Mirrored effect kinds are handled together, and no max-HP row asks for more
     than the runtime clamps to.

WHAT IT CANNOT CHECK, and this is the important part: nothing here touches the
personality resync, the save migration, or whether the battle hooks are wired to
the right sites. Two of the six effect kinds are no longer battle-start-only -
MAX_HP is read from CalculateMonStats and DAMAGE_TAKEN from the damage
calculation - and this file cannot tell whether either is reached at all. Those
fail by crashing or by applying to the wrong Pokemon, not by being wrong in a
table, and the only thing that finds them is playing the game. See the Checks
section of roguelike-state.md.

Usage:  python3 tools/rogue/check_charms.py [--repo PATH] [--selftest]
"""
import argparse
import re
import sys
from pathlib import Path


STAT_EFFECTS = ('ROGUE_CHARM_EFFECT_STAT_DROP', 'ROGUE_CHARM_EFFECT_STAT_BOOST')

# RogueCharm_MaxHpPercentLost clamps to this. A table row above it would be
# silently clamped at runtime, which means the table would be claiming a number
# the game does not honour - the sort of quiet disagreement between data and code
# that gets rediscovered as a balance mystery months later.
MAX_HP_CAP = 90

# Kinds that come in mirrored pairs. If one half of a pair is handled by the
# applier and the other is not, the table can name a charm that reads as the
# opposite of a working one and silently does nothing - a failure that looks
# like a balance problem rather than a bug.
EFFECT_PAIRS = (
    ('ROGUE_CHARM_EFFECT_RECOIL', 'ROGUE_CHARM_EFFECT_HEAL'),
    ('ROGUE_CHARM_EFFECT_STAT_DROP', 'ROGUE_CHARM_EFFECT_STAT_BOOST'),
)


def strip_comments(text):
    return re.sub(r'//[^\n]*', '', text)


def constant(text, name):
    m = re.search(r'#define\s+%s\s+(\d+)\b' % name, text)
    if not m:
        sys.exit('could not find %s' % name)
    return int(m.group(1))


def charm_ids(consts):
    """id name -> numeric value, for every ROGUE_CHARM_* that is an id."""
    ids = {}
    for m in re.finditer(r'#define\s+(ROGUE_CHARM_[A-Z_]+)\s+(\d+)\b', consts):
        name, value = m.group(1), int(m.group(2))
        if name.startswith('ROGUE_CHARM_EFFECT_') or name.startswith('ROGUE_CHARM_STAT_'):
            continue
        if name in ('ROGUE_CHARM_COUNT', 'ROGUE_CHARMS_PER_MON',
                    'ROGUE_PARTY_CHARM_SLOTS', 'ROGUE_CHARMS_SAVE_VERSION',
                    'ROGUE_CHARM_DURATION_RUN', 'ROGUE_CHARM_DURATION_ACT'):
            continue
        ids[name] = value
    return ids


def parse_rows(src):
    """Designated rows of sCharms: id name -> {field: raw text}."""
    m = re.search(r'sCharms\[ROGUE_CHARM_COUNT\]\s*=\s*\{(.*?)\n\};', src, re.S)
    if not m:
        sys.exit('could not find sCharms in src/rogue_charms.c')
    body = m.group(1)

    rows = {}
    for rm in re.finditer(r'\[(ROGUE_CHARM_[A-Z_]+)\]\s*=\s*\{(.*?)\},', body, re.S):
        fields = {}
        for fm in re.finditer(r'\.(\w+)\s*=\s*([^,]+?)\s*,', rm.group(2) + ','):
            fields[fm.group(1)] = fm.group(2).strip()
        rows[rm.group(1)] = fields
    return rows


def string_id_table(src):
    m = re.search(r'gRogueCharmStringIds\[ROGUE_CHARM_COUNT\]\s*=\s*\{(.*?)\n\};',
                  src, re.S)
    if not m:
        sys.exit('could not find gRogueCharmStringIds in src/rogue_charms.c')
    return dict(re.findall(r'\[(ROGUE_CHARM_[A-Z_]+)\]\s*=\s*(STRINGID_\w+)', m.group(1)))


def applied_effects(src):
    """Effect kinds the running code actually branches on."""
    return set(re.findall(r'ROGUE_CHARM_EFFECT_[A-Z_]+', re.sub(
        r'sCharms\[ROGUE_CHARM_COUNT\]\s*=\s*\{.*?\n\};', '', src, flags=re.S)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    ap.add_argument('--selftest', action='store_true',
                    help='prove the checks fire on a deliberately broken table')
    args = ap.parse_args()

    consts = strip_comments((args.repo / 'include/constants/rogue_charms.h')
                            .read_text(errors='replace'))
    src = strip_comments((args.repo / 'src/rogue_charms.c')
                         .read_text(errors='replace'))
    messages = (args.repo / 'src/battle_message.c').read_text(errors='replace')

    count = constant(consts, 'ROGUE_CHARM_COUNT')
    ids = charm_ids(consts)
    rows = parse_rows(src)
    strings = string_id_table(src)
    handled = applied_effects(src)

    fails = check(count, ids, rows, strings, handled, messages)

    if args.selftest:
        rc = selftest(count, ids, rows, strings, handled, messages)
        if rc:
            return rc

    if fails:
        print('FAIL check_charms.py')
        for f in fails:
            print('  - %s' % f)
        return 1

    real = [n for n, v in ids.items() if v != 0]
    print('check_charms.py: OK')
    print('  %d charms, %d effect kinds handled by the applier'
          % (len(real), len(handled)))
    for name in sorted(real, key=lambda n: ids[n]):
        r = rows[name]
        print('    %-24s %-28s mag %s, duration %s'
              % (name, r.get('effect', '?'), r.get('magnitude', '0'),
                 r.get('defaultDuration', '?')))
    return 0


def check(count, ids, rows, strings, handled, messages):
    fails = []

    # --- 1. every id has a row, and no row is an id that does not exist ---
    for name, value in sorted(ids.items(), key=lambda kv: kv[1]):
        if value >= count:
            fails.append('%s is %d, past ROGUE_CHARM_COUNT (%d)' % (name, value, count))
        if name not in rows:
            fails.append('%s has no row in sCharms - reading it returns the '
                         'placeholder and the charm silently does nothing' % name)
    for name in rows:
        if name not in ids:
            fails.append('sCharms has a row for %s, which is not a charm id' % name)

    placeholder = 'ROGUE_CHARM_NONE'

    for name, fields in sorted(rows.items()):
        if name == placeholder:
            continue

        # --- 2. the applier has to have a branch for this kind ---
        effect = fields.get('effect')
        if effect is None:
            fails.append('%s names no effect kind' % name)
        elif effect not in handled:
            fails.append('%s uses %s, which no applier branches on - the charm '
                         'would be granted, announced, and do nothing'
                         % (name, effect))

        # --- 3. visibility, enforced rather than remembered ---
        for field, why in (('name', 'nothing could list it'),
                           ('description', 'nothing could explain it'),
                           ('battleStringId', 'it would apply in silence')):
            if not fields.get(field) or fields.get(field) == 'NULL':
                fails.append('%s has no %s, so %s' % (name, field, why))

        # --- 4. a charm the player was told about must do something ---
        magnitude = fields.get('magnitude', '0')
        if magnitude in ('0', '', None):
            fails.append('%s has magnitude 0, so it applies nothing' % name)

        if effect in STAT_EFFECTS and not fields.get('param'):
            fails.append('%s changes stats but names none, so it changes nothing'
                         % name)

        if effect == 'ROGUE_CHARM_EFFECT_MAX_HP':
            try:
                if int(magnitude) > MAX_HP_CAP:
                    fails.append('%s asks for %s%% max HP, past the %d%% the '
                                 'runtime clamps to - the table would be claiming '
                                 'a number the game does not honour'
                                 % (name, magnitude, MAX_HP_CAP))
            except ValueError:
                fails.append('%s has a non-numeric max-HP magnitude (%s), so the '
                             'runtime cap cannot be checked against it'
                             % (name, magnitude))

    # --- 5. printfromtable indexes this by id; a short table reads off the end ---
    for name, value in ids.items():
        if value < count and name not in strings:
            fails.append('gRogueCharmStringIds has no entry for %s, and '
                         'printfromtable indexes it by charm id' % name)

    # --- 6. the string ids have to exist where battle_message.c can find them ---
    for name, fields in rows.items():
        sid = fields.get('battleStringId')
        if sid and sid.startswith('STRINGID_') and ('[%s]' % sid) not in messages:
            fails.append('%s names %s, which battle_message.c does not define'
                         % (name, sid))

    # --- 7. mirrored kinds are handled together or not at all ---
    for a, b in EFFECT_PAIRS:
        if (a in handled) != (b in handled):
            missing, present = (b, a) if a in handled else (a, b)
            fails.append('the applier handles %s but not its mirror %s, so a '
                         'charm using %s would be granted and do nothing'
                         % (present, missing, missing))

    return fails


def selftest(count, ids, rows, strings, handled, messages):
    """Break the table on purpose and confirm each check fires.

    A check that has never failed is worth nothing, and this project has shipped
    checks that passed vacuously. Rather than trusting anyone's memory of whether
    this one has ever gone red, it goes red here on demand.
    """
    cases = []

    # An effect kind nothing applies - the silent no-op this file exists for.
    broken = {k: dict(v) for k, v in rows.items()}
    victim = next(n for n in broken if n != 'ROGUE_CHARM_NONE')
    broken[victim]['effect'] = 'ROGUE_CHARM_EFFECT_MADE_UP'
    cases.append(('an effect kind with no applier', broken, strings, handled))

    # A charm with no battle string - granted and applied in total silence.
    broken = {k: dict(v) for k, v in rows.items()}
    broken[victim]['battleStringId'] = ''
    cases.append(('a charm with no battle string', broken, strings, handled))

    # A magnitude of zero - the charm reads as real and does nothing.
    broken = {k: dict(v) for k, v in rows.items()}
    broken[victim]['magnitude'] = '0'
    cases.append(('a charm with magnitude 0', broken, strings, handled))

    # A max-HP charm past the runtime clamp - the table lying about its own
    # magnitude, which no amount of playing would make obvious.
    broken = {k: dict(v) for k, v in rows.items()}
    hp_victim = next((n for n, f in broken.items()
                      if f.get('effect') == 'ROGUE_CHARM_EFFECT_MAX_HP'), None)
    if hp_victim:
        broken[hp_victim]['magnitude'] = str(MAX_HP_CAP + 5)
        cases.append(('a max-HP charm past the runtime clamp', broken, strings, handled))

    # Half a mirrored pair - a boost kind with no branch behind it.
    half = set(handled) - {'ROGUE_CHARM_EFFECT_STAT_BOOST'}
    cases.append(('half a mirrored effect pair', rows, strings, half))

    # A missing row - RogueCharm_Info returns the placeholder for it.
    broken = {k: dict(v) for k, v in rows.items() if k != victim}
    cases.append(('a missing table row', broken, strings, handled))

    # A short gRogueCharmStringIds - printfromtable would read off the end.
    short = {k: v for k, v in strings.items() if k != victim}
    cases.append(('a short gRogueCharmStringIds', rows, short, handled))

    for label, r, s, h in cases:
        if not check(count, ids, r, s, h, messages):
            print('SELFTEST FAILED: %s still passed, so this check cannot '
                  'detect the thing it exists for' % label)
            return 1
        print('selftest OK: %s is caught' % label)

    return 0


if __name__ == '__main__':
    sys.exit(main())
