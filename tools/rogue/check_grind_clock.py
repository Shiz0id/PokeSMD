"""Does the anti-grind clock actually bite, and does its counter survive its floor?

The clock tapers wild experience as a floor is farmed - see ROGUE_GRIND_FREE_KOS
in include/constants/rogue_dungeon.h. Both of its failure modes are silent.

It can be switched OFF by a constant. A step of zero, a minimum equal to the
base rate, or a free allowance wider than any floor will ever produce all leave
a taper that compiles, runs, and never changes a single experience award. There
is no symptom: experience looks right because it IS the old value, and the only
way to notice is to grind a floor for ten minutes with a calculator.

And its counter can ALIAS ITS FLOOR. The count is packed as (floor << 8) | kos
so that a floor change resets it implicitly and a save-and-reload does not, and
that packing is only sound while every floor number fits the byte it is shifted
into. DUNGEON_TOTAL_FLOORS past 255 would wrap floor 256 onto floor 0 and hand
a descending player the allowance they had already spent - a difficulty bug that
appears at one specific depth and nowhere else. A STATIC_ASSERT in the C guards
the build; this guards the intent, and says which number moved.

Usage:  python3 tools/rogue/check_grind_clock.py [--repo PATH]
"""
import argparse
import re
import sys
from pathlib import Path

# What a real floor can plausibly produce. Grass is ~41% of walkable floor and a
# floor is crossed once, so a player who fights everything they step in lands
# well under this - it is the ceiling on a thorough clear, not on a traverse.
# The free allowance has to sit below it or the clock never fires in real play.
PLAUSIBLE_KOS_PER_FLOOR = 40

# The taper has to reach its floor value inside a session someone would actually
# sit through. Reaching it after 300 knockouts is arithmetically a taper and
# practically an unlimited grind.
MAX_KOS_TO_BOTTOM = 40


def constants(repo):
    """Read the clock's constants out of the header rather than restating them."""
    text = (repo / 'include/constants/rogue_dungeon.h').read_text(errors='replace')
    text = re.sub(r'//[^\n]*', '', text)
    wanted = [
        'ROGUE_WILD_EXP_PERCENT',
        'ROGUE_GRIND_FREE_KOS',
        'ROGUE_GRIND_STEP',
        'ROGUE_GRIND_MIN_PERCENT',
    ]
    out = {}
    for name in wanted:
        m = re.search(r'#define\s+%s\s+(\d+)\b' % name, text)
        if not m:
            sys.exit('could not find %s in constants/rogue_dungeon.h' % name)
        out[name] = int(m.group(1))

    # DUNGEON_TOTAL_FLOORS lives in the other header and is a chain of
    # expressions over four more constants, none of which is a bare literal
    # except the two at the bottom. Substitute until it resolves rather than
    # restating the arithmetic here - the whole point of reading it is that the
    # run structure is allowed to change and this has to follow it.
    other = (repo / 'include/rogue_dungeon.h').read_text(errors='replace')
    other = re.sub(r'//[^\n]*', '', other)
    defs = dict(re.findall(r'#define\s+(DUNGEON_[A-Z0-9_]+)\s+(\(?[^\n]*?\)?)\s*$',
                           other, re.M))

    expr = defs.get('DUNGEON_TOTAL_FLOORS')
    if expr is None:
        sys.exit('could not find DUNGEON_TOTAL_FLOORS in rogue_dungeon.h')
    for _ in range(16):
        names = set(re.findall(r'\bDUNGEON_[A-Z0-9_]+\b', expr))
        if not names:
            break
        for name in names:
            if name not in defs:
                sys.exit('DUNGEON_TOTAL_FLOORS depends on %s, which is not defined here'
                         % name)
            expr = re.sub(r'\b%s\b' % name, '(%s)' % defs[name], expr)
    if not re.fullmatch(r'[\d\s()+*-]+', expr):
        sys.exit('could not resolve DUNGEON_TOTAL_FLOORS to arithmetic: %r' % expr)
    out['DUNGEON_TOTAL_FLOORS'] = int(eval(expr))
    return out


def percent_for(kos, c):
    """An exact port of RogueDungeon_TakeWildExpPercent's arithmetic.

    kos is the count BEFORE the knockout being paid for, which is the value the
    C reads out of the var before it steps it.
    """
    if kos < c['ROGUE_GRIND_FREE_KOS']:
        return c['ROGUE_WILD_EXP_PERCENT']
    spent = (kos - c['ROGUE_GRIND_FREE_KOS'] + 1) * c['ROGUE_GRIND_STEP']
    if spent >= c['ROGUE_WILD_EXP_PERCENT'] - c['ROGUE_GRIND_MIN_PERCENT']:
        return c['ROGUE_GRIND_MIN_PERCENT']
    return c['ROGUE_WILD_EXP_PERCENT'] - spent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    args = ap.parse_args()
    c = constants(args.repo)
    fails = []

    base = c['ROGUE_WILD_EXP_PERCENT']
    free = c['ROGUE_GRIND_FREE_KOS']
    step = c['ROGUE_GRIND_STEP']
    low = c['ROGUE_GRIND_MIN_PERCENT']

    # --- the clock is switched on at all ---
    if step == 0:
        fails.append('ROGUE_GRIND_STEP is 0: the taper never descends and every '
                     'knockout pays the full rate forever')
    if low >= base:
        fails.append('ROGUE_GRIND_MIN_PERCENT (%d) is not below ROGUE_WILD_EXP_PERCENT '
                     '(%d): the taper has nowhere to go' % (low, base))
    if free >= PLAUSIBLE_KOS_PER_FLOOR:
        fails.append('ROGUE_GRIND_FREE_KOS (%d) is at or above the %d knockouts a '
                     'thorough floor clear produces: the clock can never fire in play'
                     % (free, PLAUSIBLE_KOS_PER_FLOOR))

    # --- and it bottoms out somewhere a player would reach ---
    bottom = next((k for k in range(0, 4096) if percent_for(k, c) == low), None)
    if bottom is None:
        fails.append('the taper never reaches ROGUE_GRIND_MIN_PERCENT (%d)' % low)
    elif bottom > MAX_KOS_TO_BOTTOM:
        fails.append('the taper needs %d knockouts to reach its floor value, past the '
                     '%d a player would sit through' % (bottom, MAX_KOS_TO_BOTTOM))

    # --- the curve is well formed over the whole u8 range the var can hold ---
    prev = None
    for kos in range(0, 256):
        p = percent_for(kos, c)
        if p > base:
            fails.append('knockout %d pays %d%%, above the base rate %d%%' % (kos, p, base))
            break
        if p < low:
            fails.append('knockout %d pays %d%%, below the floor %d%%' % (kos, p, low))
            break
        if prev is not None and p > prev:
            fails.append('knockout %d pays %d%% after %d%%: the taper goes back up'
                         % (kos, p, prev))
            break
        prev = p

    # --- the free allowance is exactly as wide as it claims ---
    if percent_for(free - 1, c) != base:
        fails.append('the last free knockout (%d) does not pay the full rate' % (free - 1))
    if free < PLAUSIBLE_KOS_PER_FLOOR and percent_for(free, c) >= base:
        fails.append('the first knockout past the allowance (%d) still pays the full rate'
                     % free)

    # --- and the packing cannot alias one floor onto another ---
    total = c['DUNGEON_TOTAL_FLOORS']
    if total > 255:
        fails.append('DUNGEON_TOTAL_FLOORS is %d: floor %d aliases floor %d in the packed '
                     'counter, so descending there inherits a spent allowance'
                     % (total, 256, 0))

    if fails:
        print('FAIL check_grind_clock.py')
        for f in fails:
            print('  - %s' % f)
        return 1

    print('check_grind_clock.py: OK')
    print('  %d knockouts at %d%%, then -%d%% each to a floor of %d%% at knockout %d'
          % (free, base, step, low, bottom))
    print('  packed counter safe to floor 255; the run is %d floors' % total)
    return 0


if __name__ == '__main__':
    sys.exit(main())
