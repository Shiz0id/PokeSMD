"""Can code that runs while the player is standing on a floor reach the seeded RNG?

THE INVARIANT. Every object on a dungeon floor - stairs, spawn, grass,
trainers, item balls, berry trees, mining rocks, the event and its prop, buried
items - is positioned by draws from one seeded stream, in one fixed order. No
placer may change HOW MANY times it draws: inserting or removing a single
DungeonRandom() relocates every object placed after it, on every floor, for
ever. Nothing fails to build and every other check still passes.

test/rogue_dungeon_placement.c DETECTS that (a real stray draw in PlaceItems was
proven to fail the suite). This file is the other half: it stops live code from
being able to touch the stream at all. The stream is only valid while a floor is
being decided; once the player is on the floor it is not live, and a draw taken
then is a bug of a different and worse kind - it makes the floor depend on what
the player did.

HOW IT CHECKS, and why it is not a list. The obvious implementation is an
allowlist of functions permitted to call DungeonRandom. This project has been
burned twice by exactly that shape - PLACEMENT_COUNTERS in
check_dungeon_objects.py listed six counters and hiddenCount was not on it, so a
boss floor published the previous floor's buried items for as long as nobody
noticed. A hand-maintained list of "the things that matter" goes stale silently.

So this derives the answer instead. It builds the call graph, finds every LIVE
ENTRY POINT - an exported function reachable from a map script or from another
translation unit - and asserts that none of them can reach DungeonRandom. New
live code is covered automatically the day it is written.

The one list is SEEDED_ENTRIES, and it is four names with a reason each. It is
self-guarding: every name on it must actually be able to reach the stream, so an
entry that stops being seeded fails rather than silently widening the exemption.

Usage:  python3 tools/rogue/check_seeded_stream.py [REPO | --repo REPO]
        python3 tools/rogue/check_seeded_stream.py --selftest
"""
import os
import re
import sys
from collections import defaultdict, deque

RNG = ('DungeonRandom', 'SeedDungeonRng')
RNG_STATE = 'sDungeonRngState'

# The entry points that are ALLOWED to reach the seeded stream, and why. Four
# names, each with a reason that can be argued with - unlike a list of forty
# callers, which can only be maintained.
#
# Every one of these is asserted to ACTUALLY reach the stream (see
# check_exemptions_are_real). An exemption that stops being true is a stale
# exemption, and a stale exemption is how a guard quietly stops guarding.
SEEDED_ENTRIES = {
    'GenerateRogueDungeonFloor':
        'the map-load hook - it calls PrepareFloor when the floor has not been '
        'prepared yet, which is exactly when the stream IS live',
    'RogueDungeon_LoadObjectEventTemplates':
        'replaces LoadObjEventTemplatesFromHeader for dungeon floors and rolls '
        'the floor first, because trainer placement needs to see the rooms',
    'RogueDungeon_PrepareNewFloor':
        'the explicit "roll a new floor" entry, called by the template loader',
    'RogueDungeon_SeedRestStopUnown':
        'a special invoked from RogueRestStop scripts.inc that RESEEDS the '
        'stream deterministically (floor * 3 + 1) and draws from it. Safe '
        'because it runs at the rest stop rather than on a generated floor, '
        'and because it reseeds rather than continuing a run in progress',
}

# TESTING-only hooks. Not exempt so much as not present in a shipping build.
TEST_ENTRIES = {'RogueDungeon_Test_HashFloorPlacements',
                'RogueDungeon_Test_SetStreamSkew'}


def read(path):
    with open(path, encoding='utf-8', errors='replace') as fh:
        return fh.read()


def strip_comments(text):
    """Comments mention these names constantly - PlaceEvents is discussed in a
    scripts.inc comment, and GetWildMonInfo's own comment says 'Deliberately NOT
    DungeonRandom()'. Both read as calls to a naive regex."""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def parse_functions(src):
    """name -> {static, body, line}. Bodies have comments stripped."""
    lines = src.split('\n')
    out = {}
    for i, ln in enumerate(lines):
        m = re.match(r'^(static\s+)?[A-Za-z_][\w \t\*]*?\b(\w+)\((?!\s*\*)', ln)
        if not m or ln.startswith((' ', '\t', '#', '//')):
            continue
        if ln.rstrip().endswith(';'):
            continue
        name = m.group(2)
        if name in ('if', 'for', 'while', 'switch', 'return', 'sizeof'):
            continue
        end = len(lines) - 1
        for j in range(i + 1, len(lines)):
            if lines[j] == '}':
                end = j
                break
        out[name] = {
            'static': bool(m.group(1)),
            'line': i + 1,
            'body': strip_comments('\n'.join(lines[i:end + 1])),
        }
    return out


def call_graph(funcs):
    calls = defaultdict(set)
    for name, f in funcs.items():
        for other in funcs:
            if other != name and re.search(r'\b%s\s*\(' % other, f['body']):
                calls[name].add(other)
    return calls


def path_to_rng(start, calls, funcs):
    """Shortest call path from `start` to a DungeonRandom/SeedDungeonRng call.

    Returns a list of names, or None. The PATH is the point - "something can
    reach the stream" is not actionable, "A calls B calls C which draws" is.
    """
    seen = {start}
    q = deque([(start, [start])])
    while q:
        cur, path = q.popleft()
        for callee in sorted(calls[cur]):
            if callee in RNG:
                return path + [callee]
            if callee not in seen and callee in funcs:
                seen.add(callee)
                q.append((callee, path + [callee]))
    return None


def live_entry_points(repo, funcs):
    """Exported functions reachable from a map script or another .c file.

    Derived, not listed. A special added to specials.inc tomorrow is covered
    without anyone remembering to come back here.
    """
    script_text = ''
    for root, _dirs, files in os.walk(os.path.join(repo, 'data')):
        for fn in files:
            if fn.endswith(('.inc', '.s')):
                script_text += read(os.path.join(root, fn))

    other_c = ''
    srcdir = os.path.join(repo, 'src')
    for fn in sorted(os.listdir(srcdir)):
        if fn.endswith('.c') and fn != 'rogue_dungeon.c':
            other_c += strip_comments(read(os.path.join(srcdir, fn)))

    entries = set()
    for name, f in funcs.items():
        if f['static']:
            continue
        if re.search(r'\b%s\b' % name, script_text) or \
           re.search(r'\b%s\s*\(' % name, other_c):
            entries.add(name)
    return entries


def check_exemptions_are_real(calls, funcs):
    """Every SEEDED_ENTRIES name must exist AND actually reach the stream.

    Guards the exemption list against itself. An entry that no longer reaches
    DungeonRandom is either renamed or no longer seeded, and either way leaving
    it here quietly widens what is permitted.
    """
    fails = []
    for name in SEEDED_ENTRIES:
        if name not in funcs:
            fails.append('SEEDED_ENTRIES names %s, which does not exist in '
                         'src/rogue_dungeon.c - renamed or removed' % name)
        elif path_to_rng(name, calls, funcs) is None:
            fails.append('SEEDED_ENTRIES exempts %s but it can no longer reach '
                         'the seeded stream, so the exemption is stale and is '
                         'permitting more than it describes' % name)
    return fails


def check_rng_state_is_private(funcs):
    """sDungeonRngState must be touched only by the two accessors.

    Anything else reading or writing it is reaching around them - which is how
    a stream gets advanced without a draw being counted.
    """
    fails = []
    for name, f in funcs.items():
        if name in RNG:
            continue
        if re.search(r'\b%s\b' % RNG_STATE, f['body']):
            fails.append('%s touches %s directly instead of going through '
                         'SeedDungeonRng/DungeonRandom' % (name, RNG_STATE))
    return fails


def check_rng_is_static(funcs):
    fails = []
    for name in RNG:
        if name not in funcs:
            fails.append('%s is not defined in src/rogue_dungeon.c' % name)
        elif not funcs[name]['static']:
            fails.append('%s is not static, so any translation unit can call '
                         'it and this check cannot see them' % name)
    return fails


def run(repo, verbose=True):
    src = read(os.path.join(repo, 'src', 'rogue_dungeon.c'))
    funcs = parse_functions(src)
    calls = call_graph(funcs)

    fails = []
    fails += check_rng_is_static(funcs)
    fails += check_rng_state_is_private(funcs)
    fails += check_exemptions_are_real(calls, funcs)

    entries = live_entry_points(repo, funcs)
    exempt = set(SEEDED_ENTRIES) | TEST_ENTRIES
    checked, reaching = 0, []
    for name in sorted(entries - exempt):
        checked += 1
        path = path_to_rng(name, calls, funcs)
        if path is not None:
            reaching.append((name, path))

    for name, path in reaching:
        fails.append(
            'LIVE ENTRY POINT %s can reach the seeded stream:\n        %s\n'
            '      The stream is only valid while a floor is being decided. A '
            'draw taken while the player is standing on the floor makes that '
            'floor depend on what the player did.' % (name, ' -> '.join(path)))

    if fails:
        print('FAIL check_seeded_stream.py')
        for f in fails:
            print('  - %s' % f)
        return 1

    if verbose:
        print('check_seeded_stream.py: OK')
        print('  %d functions, %d live entry points checked, %d exempt'
              % (len(funcs), checked, len(exempt)))
        print('  none of them can reach DungeonRandom')
        for name in sorted(SEEDED_ENTRIES):
            path = path_to_rng(name, calls, funcs)
            print('  exempt: %-38s %s'
                  % (name, ' -> '.join(path[1:]) if path and len(path) > 1
                     else '(draws directly)'))
    return 0


def selftest(repo):
    """Break it three ways and require a fire on each.

    A check that has never failed is worth nothing, and this one is guarding
    something no test can see, so the break cases are the evidence it works.
    """
    path = os.path.join(repo, 'src', 'rogue_dungeon.c')
    good = read(path)

    cases = []

    # 1. A live special quietly draws from the stream. This is the real bug.
    m = re.search(r'^(void RogueDungeon_HideMinedRock\(void\)\n\{\n)', good, re.M)
    anchor = m.group(1) if m else None
    if anchor:
        cases.append(('a live special draws from the stream',
                      anchor, anchor + '    DungeonRandom();\n'))

    # 2. The accessors stop being static, so other files could call them.
    cases.append(('DungeonRandom is not static',
                  'static u16 DungeonRandom(void)', 'u16 DungeonRandom(void)'))

    # 3. Something reaches around the accessors into the state.
    m2 = re.search(r'^(void RogueDungeon_HideMinedRock\(void\)\n\{\n)', good, re.M)
    if m2:
        cases.append(('the RNG state is poked directly',
                      m2.group(1), m2.group(1) + '    sDungeonRngState = 1;\n'))

    if not cases:
        print('selftest could not find its anchors - the file changed shape')
        return 1

    missed = []
    try:
        for label, old, new in cases:
            if good.count(old) != 1:
                missed.append('%s (anchor matched %d times)'
                              % (label, good.count(old)))
                continue
            with open(path, 'w', encoding='utf-8', newline='\n') as fh:
                fh.write(good.replace(old, new))
            rc = run(repo, verbose=False)
            print('== %-42s exit=%d' % (label, rc))
            if rc == 0:
                missed.append(label)
    finally:
        with open(path, 'w', encoding='utf-8', newline='\n') as fh:
            fh.write(good)

    print()
    if missed:
        print('FAIL selftest: not detected: %s' % ', '.join(missed))
        return 1
    print('selftest OK: every injected fault was detected')
    return 0


def main(argv):
    # Takes the repo BOTH ways on purpose. run_all_checks.sh keeps a hand-written
    # list of which checks want a positional path and which want --repo, and a
    # check that accepts either can never end up on the wrong side of it.
    args = list(argv[1:])
    want_selftest = '--selftest' in args
    if want_selftest:
        args.remove('--selftest')

    if '--repo' in args:
        i = args.index('--repo')
        if i + 1 >= len(args):
            print('--repo needs a path')
            return 2
        repo = args[i + 1]
    elif args:
        repo = args[0]
    else:
        repo = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')

    repo = os.path.abspath(repo)
    if want_selftest:
        return selftest(repo)
    return run(repo)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
