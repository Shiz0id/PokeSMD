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

Also reports the effective rate, because DUNGEON_EVENT_ODDS is a per-floor roll
and what matters to the design is events per run - which is a different number,
and one nobody should be deriving in their head.

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
    """Parse sFloorEvents. Entries are { gfx, script, minFloor, maxFloor }."""
    text = (repo / 'src/rogue_dungeon.c').read_text(errors='replace')
    m = re.search(r'sFloorEvents\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if not m:
        sys.exit('could not find sFloorEvents in src/rogue_dungeon.c')
    body = re.sub(r'//[^\n]*', '', m.group(1))

    out = []
    for entry in re.finditer(r'\{([^{}]*)\}', body):
        parts = [p.strip() for p in entry.group(1).split(',') if p.strip()]
        if len(parts) != 4:
            sys.exit('sFloorEvents entry has %d fields, want 4: %r'
                     % (len(parts), entry.group(1).strip()))
        gfx, script, lo, hi = parts
        out.append((script, resolve_floor(lo, floors), resolve_floor(hi, floors)))
    if not out:
        sys.exit('sFloorEvents parsed as empty')
    return out


def resolve_floor(tok, floors):
    if tok == 'DUNGEON_TOTAL_FLOORS':
        return floors
    if not tok.isdigit():
        sys.exit('unhandled floor expression in sFloorEvents: %r' % tok)
    return int(tok)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    args = ap.parse_args()

    consts = re.sub(r'//[^\n]*', '',
                    (args.repo / 'include/constants/rogue_dungeon.h')
                    .read_text(errors='replace'))
    odds = constant(consts, 'DUNGEON_EVENT_ODDS')
    slots = constant(consts, 'DUNGEON_MAX_EVENTS')
    floors = total_floors(args.repo)
    table = events(args.repo, floors)
    fails = []

    if slots < 1:
        fails.append('DUNGEON_MAX_EVENTS is %d: no floor can ever hold an event'
                     % slots)
    if odds < 1:
        fails.append('DUNGEON_EVENT_ODDS is %d: the modulo would divide by zero'
                     % odds)

    # --- every entry is well formed and reachable ---
    for script, lo, hi in table:
        if lo > hi:
            fails.append('%s has minFloor %d above maxFloor %d, so it never '
                         'appears' % (script, lo, hi))
        elif lo >= floors:
            fails.append('%s arrives on floor %d, past the %d-floor run'
                         % (script, lo + 1, floors))

    # --- and every floor of the run has something to draw ---
    empty = [f for f in range(floors)
             if not any(lo <= f <= hi for _, lo, hi in table)]
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
    print('  %d events, %d slot(s) per floor, 1 floor in %d rolls one'
          % (len(table), slots, odds))
    print('  ~%.0f events per %d-floor run, before placement collisions'
          % (floors / odds, floors))
    print('  %-46s %s' % ('event', 'floors'))
    for script, lo, hi in table:
        name = script.replace('RogueDungeonFloor_EventScript_Event', '')
        span = '%d-%d' % (lo + 1, min(hi + 1, floors))
        print('  %-46s %-10s (eligible on %d)'
              % (name, span, min(hi, floors - 1) - lo + 1))

    # Where the choice is narrowest is where repetition is felt, so say it.
    counts = {f: sum(1 for _, lo, hi in table if lo <= f <= hi)
              for f in range(floors)}
    worst = min(counts, key=lambda f: counts[f])
    print('  narrowest choice: floor %d has %d eligible'
          % (worst + 1, counts[worst]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
