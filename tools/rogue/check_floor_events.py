"""Is the floor event table reachable end to end, and does every floor have one?

sFloorEvents in src/rogue_dungeon.c bands each event by depth the way
sLootConsumables bands loot. PlaceEvents builds an eligible list for the floor
and picks from it, and if that list comes out EMPTY it simply returns - so a band
of floors with no eligible event has no events at all, reports nothing, and looks
exactly like the three floors in four that legitimately roll none. A gap in the
middle of the table is invisible in play and invisible in the build.

The same is true one entry at a time: an event whose band is unreachable, or
whose min and max are the wrong way round, is dead weight that nothing mentions.
Both mistakes are one mistyped floor number away, and the table is meant to grow.

Also reports the effective rate, because DUNGEON_EVENT_PERCENT is a per-floor
roll and what matters to the design is events per run - which is a different
number, and one nobody should be deriving in their head. Same for the per-event
expected count: a weight is relative to what else is eligible at that depth, so
a constant weight is NOT a constant frequency.

Usage:  python3 tools/rogue/check_floor_events.py [--repo PATH]
"""
import argparse
import re
import sys
from pathlib import Path


def constant(text, name):
    m = re.search(r'#define\s+%s\s+(\d+)\b' % name, text)
    if not m:
        sys.exit('could not find %s' % name)
    return int(m.group(1))


def total_floors(repo):
    """Resolve DUNGEON_TOTAL_FLOORS through its whole #define chain."""
    text = re.sub(r'//[^\n]*', '',
                  (repo / 'include/rogue_dungeon.h').read_text(errors='replace'))
    defs = dict(re.findall(r'#define\s+(DUNGEON_[A-Z0-9_]+)\s+(\(?[^\n]*?\)?)\s*$',
                           text, re.M))
    expr = defs.get('DUNGEON_TOTAL_FLOORS')
    if expr is None:
        sys.exit('could not find DUNGEON_TOTAL_FLOORS')
    for _ in range(16):
        names = set(re.findall(r'\bDUNGEON_[A-Z0-9_]+\b', expr))
        if not names:
            break
        for n in names:
            if n not in defs:
                sys.exit('DUNGEON_TOTAL_FLOORS depends on undefined %s' % n)
            expr = re.sub(r'\b%s\b' % n, '(%s)' % defs[n], expr)
    if not re.fullmatch(r'[\d\s()+*-]+', expr):
        sys.exit('could not resolve DUNGEON_TOTAL_FLOORS: %r' % expr)
    return int(eval(expr))


def events(repo, floors):
    """Parse sFloorEvents.

    Rows are positional and the last four fields are OPTIONAL - weight, theme
    mask, prop and movement type, all default-safe - so a row may carry four to
    EIGHT fields. A parser that demanded eight would reject the six rows that
    predate every one of them.
    """
    text = (repo / 'src/rogue_dungeon.c').read_text(errors='replace')
    m = re.search(r'sFloorEvents\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if not m:
        sys.exit('could not find sFloorEvents in src/rogue_dungeon.c')
    body = re.sub(r'//[^\n]*', '', m.group(1))

    out = []
    for entry in re.finditer(r'\{([^{}]*)\}', body):
        parts = [p.strip() for p in entry.group(1).split(',') if p.strip()]
        if not 4 <= len(parts) <= 8:
            sys.exit('sFloorEvents entry has %d fields, want 4 to 8: %r'
                     % (len(parts), entry.group(1).strip()))
        gfx, script, lo, hi = parts[:4]
        weight = parts[4] if len(parts) > 4 else '0'
        mask = parts[5] if len(parts) > 5 else 'DUNGEON_EVENT_ANY_THEME'
        prop = parts[6] if len(parts) > 6 else 'DUNGEON_EVENT_NO_PROP'
        move = parts[7] if len(parts) > 7 else '0'
        out.append({
            'move': move,
            'script': script,
            'lo': resolve_floor(lo, floors),
            'hi': resolve_floor(hi, floors),
            'weight': weight,
            'mask': mask,
            'prop': prop,
            'any_theme': mask in ('DUNGEON_EVENT_ANY_THEME', '0'),
        })
    if not out:
        sys.exit('sFloorEvents parsed as empty')
    return out


def resolve_floor(tok, floors):
    if tok == 'DUNGEON_TOTAL_FLOORS':
        return floors
    if not tok.isdigit():
        sys.exit('unhandled floor expression in sFloorEvents: %r' % tok)
    return int(tok)


def script_bodies(repo):
    """label -> body text, for every script in the dungeon floor's scripts.inc."""
    text = (repo / 'data/maps/RogueDungeonFloor/scripts.inc').read_text(errors='replace')
    out, label, buf = {}, None, []
    for line in text.split(chr(10)):
        if line.endswith('::'):
            if label:
                out[label] = chr(10).join(buf)
            label, buf = line[:-2], []
        elif label:
            buf.append(line)
    if label:
        out[label] = chr(10).join(buf)
    return out


def check_one_shot(table, bodies):
    """EVERY EVENT MUST REFUSE TO FIRE TWICE ON ONE FLOOR.

    This check exists because the opposite shipped and was found by playing.
    Nothing removes the NPC after use - a floor is never revisited, so there was
    reckoned to be nothing to persist - but that reasoning is about coming BACK
    to a floor and says nothing about talking to the same NPC again while
    standing on it. Every event was repeatable: infinite eggs, infinite full
    heals, infinite money from the fossil, and a trader that ratcheted a Pokemon
    up five levels per conversation.

    An event needs BOTH halves: a guard that sends a spent event away, and a
    setvar on the path that commits. A guard with no setvar never triggers; a
    setvar with no guard never gets read.
    """
    fails = []
    for e in table:
        label = e['script']
        body = bodies.get(label)
        if body is None:
            fails.append('%s is in sFloorEvents but has no script of that name'
                         % label)
            continue
        if 'EventScript_EventSpent' not in body:
            fails.append('%s never checks VAR_ROGUE_EVENT_SPENT, so the player '
                         'can take it again by talking to the NPC twice' % label)
    # The setvar can live in a branch script rather than the entry one, so it is
    # counted across the whole file rather than per event.
    whole = chr(10).join(bodies.values())
    if 'setvar VAR_ROGUE_EVENT_SPENT' not in whole:
        fails.append('nothing ever sets VAR_ROGUE_EVENT_SPENT, so every guard '
                     'above is dead code and every event repeats')
    return fails


def resolve_weight(tok, consts):
    """Weight tokens are 0, a number, or a DUNGEON_EVENT_WEIGHT_* name."""
    if tok in ('0', ''):
        return constant(consts, 'DUNGEON_EVENT_WEIGHT_DEFAULT')
    if tok.isdigit():
        return int(tok)
    if tok.startswith('DUNGEON_EVENT_WEIGHT_'):
        return constant(consts, tok)
    sys.exit('unhandled weight expression in sFloorEvents: %r' % tok)


def expected_per_run(table, floors, percent):
    """Expected appearances of each event across one run.

    THE NUMBER THAT MATTERS TO THE DESIGN IS NOT THE WEIGHT. A weight is
    relative to whatever else is eligible on that floor, and what is eligible
    changes with depth - so an event can hold a constant weight and still be
    twice as common early as late, purely because fewer things compete with it
    down there. Nobody should be deriving that in their head, which is the same
    reason this file already reports events-per-run instead of DUNGEON_EVENT_ODDS.
    """
    out = {e['script']: 0.0 for e in table}
    for f in range(floors):
        live = [e for e in table if e['lo'] <= f <= e['hi']]
        total = sum(e['w'] for e in live)
        if total == 0:
            continue
        for e in live:
            out[e['script']] += (percent / 100.0) * (e['w'] / total)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    args = ap.parse_args()

    consts = re.sub(r'//[^\n]*', '',
                    (args.repo / 'include/constants/rogue_dungeon.h')
                    .read_text(errors='replace'))
    percent = constant(consts, 'DUNGEON_EVENT_PERCENT')
    slots = constant(consts, 'DUNGEON_MAX_EVENTS')
    floors = total_floors(args.repo)
    table = events(args.repo, floors)
    for e in table:
        e['w'] = resolve_weight(e['weight'], consts)
    expected = expected_per_run(table, floors, percent)
    fails = check_one_shot(table, script_bodies(args.repo))

    if slots < 1:
        fails.append('DUNGEON_MAX_EVENTS is %d: no floor can ever hold an event'
                     % slots)
    if not 1 <= percent <= 100:
        fails.append('DUNGEON_EVENT_PERCENT is %d, outside 1-100: floors would '
                     'either never roll an event or always roll one' % percent)

    # --- every entry is well formed and reachable ---
    for e in table:
        script, lo, hi = e['script'], e['lo'], e['hi']
        if lo > hi:
            fails.append('%s has minFloor %d above maxFloor %d, so it never '
                         'appears' % (script, lo, hi))
        elif lo >= floors:
            fails.append('%s arrives on floor %d, past the %d-floor run'
                         % (script, lo + 1, floors))

    # --- A THEME-RESTRICTED EVENT MUST NEVER BE THE ONLY THING ELIGIBLE ---
    #
    # PlaceEvents filters by theme as well as by depth, so a band covered only by
    # theme-restricted rows leaves every floor of every OTHER theme with nothing
    # to draw - and a floor that rolls nothing is indistinguishable from the
    # three in four that legitimately roll none. Invisible in the build,
    # invisible in play.
    #
    # Checked against unrestricted rows alone, which is conservative on purpose:
    # under dungeon shuffle a given floor can carry several different themes, and
    # requiring one always-eligible event is a sufficient condition that does not
    # need this script to reimplement DungeonForSlot.
    unthemed = [e for e in table if e['any_theme']]
    starved = [f for f in range(floors)
               if not any(e['lo'] <= f <= e['hi'] for e in unthemed)]
    if starved:
        fails.append('%d floor(s) have NO theme-independent event, so under some '
                     'themes they roll nothing at all - first is floor %d'
                     % (len(starved), starved[0] + 1))

    # --- and every floor of the run has something to draw ---
    empty = [f for f in range(floors)
             if not any(e['lo'] <= f <= e['hi'] for e in table)]
    if empty:
        runs = []
        start = empty[0]
        for a, b in zip(empty, empty[1:] + [None]):
            if b != (a + 1 if a is not None else None):
                runs.append((start, a))
                start = b
        shown = ', '.join('%d' % (a + 1) if a == b else '%d-%d' % (a + 1, b + 1)
                          for a, b in runs)
        fails.append('floors with NO eligible event, so they silently never roll '
                     'one: %s' % shown)

    if fails:
        print('FAIL check_floor_events.py')
        for f in fails:
            print('  - %s' % f)
        return 1

    print('check_floor_events.py: OK')
    # DUNGEON_MAX_EVENTS counts OBJECT slots, not events: one floor rolls at most
    # one event, and the second slot is its optional prop.
    print('  %d events, %d object slot(s) per event, %d%% of floors roll one'
          % (len(table), slots, percent))
    print('  ~%.0f events per %d-floor run, before placement collisions'
          % (floors * percent / 100.0, floors))
    print('  %-14s %-10s %-7s %-6s %-7s %s'
          % ('event', 'floors', 'weight', 'wt', 'per run', 'theme'))
    for e in sorted(table, key=lambda e: -expected[e['script']]):
        name = e['script'].replace('RogueDungeonFloor_EventScript_Event', '')
        span = '%d-%d' % (e['lo'] + 1, min(e['hi'] + 1, floors))
        weight = 'default' if e['weight'] in ('0', '') else e['weight'][21:] or e['weight']
        theme = 'any' if e['any_theme'] else e['mask'].replace('DUNGEON_EVENT_THEME', '')
        print('  %-14s %-10s %-7s %-6d %-7.2f %s'
              % (name[:14], span, weight[:7], e['w'],
                 expected[e['script']], theme[:30]))

    # Where the choice is narrowest is where repetition is felt, so say it.
    counts = {f: sum(1 for e in table if e['lo'] <= f <= e['hi'])
              for f in range(floors)}
    worst = min(counts, key=lambda f: counts[f])
    print('  narrowest choice: floor %d has %d eligible'
          % (worst + 1, counts[worst]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
