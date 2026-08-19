"""Guard the mugshot battle transition on every boss a run can field.

THE INVARIANT. Every trainer in sDungeonBosses gets the mugshot transition, and
the colour it gets is decided by its IDENTITY -- its row within its region --
not by which region it belongs to. Identity is `position % DUNGEON_IDENTITIES`,
the same arithmetic BossRowForSlot does. Identities 0-7 are the gym leaders,
8-11 the Elite Four, 12 the Champion, 13 the finale.

The scheme is not invented here; it is read off what Hoenn and Kanto already
shipped. Their Elite Fours are Purple/Green/Pink/Blue and both Champions are
Yellow, in identity order, in two regions ported years apart. This check asserts
the other regions follow the same rows, so a boss's banner colour tells the
player where in the run they are regardless of which region rolled.

WHY IT NEEDS A CHECK. `Mugshot:` is one optional line in a trainers.party entry.
Leaving it off does not fail to build, does not warn, and does not crash -- the
trainer silently gets whatever generic transition GetTrainerBattleTransition
picks from its class instead. That is exactly what happened to the eight Kanto
gym leaders: port_kanto_leaders.py brought the Elite Four's mugshots across for
free because FRLG had set them, and nobody noticed the leaders had none. The
next region port lands the same way unless something is watching.

THE SECOND HALF. Adjacent identities must not share a colour. The run fights
these in identity order, so two consecutive floors with the same banner reads as
a bug even when every row is individually "correct". This is the half that
catches someone extending CYCLE without thinking about the wrap.

Usage:  python3 tools/rogue/check_boss_mugshots.py [REPO]
        python3 tools/rogue/check_boss_mugshots.py --repo REPO
        python3 tools/rogue/check_boss_mugshots.py --selftest
"""
import re
import sys
import tempfile
from pathlib import Path

DUNGEON_C = 'src/rogue_dungeon.c'
PARTY = 'src/data/trainers.party'

DUNGEON_IDENTITIES = 14

# Identity -> colour. Gym leaders cycle the five; the Elite Four take the first
# four in their own fought order; the Champion takes Yellow; the finale wraps to
# Purple so it never repeats the Champion it directly follows.
CYCLE = ['Purple', 'Green', 'Pink', 'Blue', 'Yellow']


def colour_for(identity):
    if identity <= 7:
        return CYCLE[identity % len(CYCLE)]
    if identity <= 11:
        return CYCLE[identity - 8]
    if identity == 12:
        return 'Yellow'
    return 'Purple'


def boss_ids(repo):
    src = (Path(repo) / DUNGEON_C).read_text(encoding='utf-8', errors='replace')
    m = re.search(r'static const u16 sDungeonBosses\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
    if not m:
        return None
    body = re.sub(r'//[^\n]*', '', m.group(1))
    return [t.strip() for t in body.split(',') if t.strip()]


def header_span(party, tid):
    """The header block of a trainer, from `=== ID ===` to the first blank line.

    Anchor on the newlines rather than on `^$` -- the `$(.*?)^$` spelling
    silently matches nothing here and makes every entry look empty.
    """
    return re.search(r'^=== ' + re.escape(tid) + r' ===\n(.*?)\n\n', party, re.M | re.S)


def mugshot_of(party, tid):
    m = header_span(party, tid)
    if not m:
        return None, False
    c = re.search(r'^Mugshot: *(\w+)', m.group(1), re.M)
    return (c.group(1) if c else None), True


def check(repo):
    repo = Path(repo)
    ids = boss_ids(repo)
    if ids is None:
        print('FAIL  check_boss_mugshots.py: sDungeonBosses not found in %s' % DUNGEON_C)
        return False
    if len(ids) % DUNGEON_IDENTITIES != 0:
        print('FAIL  check_boss_mugshots.py: %d boss ids is not a multiple of %d'
              % (len(ids), DUNGEON_IDENTITIES))
        return False

    party = (repo / PARTY).read_text(encoding='utf-8', errors='replace')
    regions = len(ids) // DUNGEON_IDENTITIES
    bad = []

    for pos, tid in enumerate(ids):
        identity = pos % DUNGEON_IDENTITIES
        region = pos // DUNGEON_IDENTITIES
        want = colour_for(identity)
        got, found = mugshot_of(party, tid)
        if not found:
            bad.append('%s (region %d identity %d) has no entry in %s'
                       % (tid, region, identity, PARTY))
        elif got is None:
            bad.append('%s (region %d identity %d) has no Mugshot line -- it falls '
                       'back to its class transition, silently'
                       % (tid, region, identity))
        elif got != want:
            bad.append('%s (region %d identity %d) is %s, identity wants %s'
                       % (tid, region, identity, got, want))

    # Adjacent identities must differ, or two consecutive floors share a banner.
    for i in range(DUNGEON_IDENTITIES - 1):
        if colour_for(i) == colour_for(i + 1):
            bad.append('identities %d and %d are both %s -- consecutive bosses '
                       'would share a banner' % (i, i + 1, colour_for(i)))

    if bad:
        print('FAIL  check_boss_mugshots.py')
        for b in bad:
            print('  ' + b)
        return False

    print('PASS  check_boss_mugshots.py  (%d bosses, %d regions x %d identities)'
          % (len(ids), regions, DUNGEON_IDENTITIES))
    return True


def selftest(repo):
    """Break each half on purpose and require the check to fire."""
    repo = Path(repo)
    dun = (repo / DUNGEON_C).read_text(encoding='utf-8', errors='replace')
    party = (repo / PARTY).read_text(encoding='utf-8', errors='replace')

    ids = boss_ids(repo)
    victim = ids[0]
    other = ids[DUNGEON_IDENTITIES]   # same identity, next region

    def drop_mugshot(s, tid):
        m = header_span(s, tid)
        return s[:m.start(1)] + re.sub(r'\nMugshot: *\w+', '', m.group(1)) + s[m.end(1):]

    def wrong_colour(s, tid):
        m = header_span(s, tid)
        head = m.group(1)
        cur = re.search(r'^Mugshot: *(\w+)', head, re.M).group(1)
        swap = 'Yellow' if cur != 'Yellow' else 'Purple'
        return (s[:m.start(1)]
                + re.sub(r'^Mugshot: *\w+', 'Mugshot: ' + swap, head, flags=re.M)
                + s[m.end(1):])

    def pad_table(s):
        return s.replace('    TRAINER_ROGUE_SINNOH_DAWN,\n',
                         '    TRAINER_ROGUE_SINNOH_DAWN,\n    TRAINER_NONE,\n', 1)

    cases = [
        ('a boss loses its Mugshot line', PARTY, lambda s: drop_mugshot(s, victim)),
        ('a boss gets the wrong colour for its identity', PARTY, lambda s: wrong_colour(s, other)),
        ('sDungeonBosses gains a row no region fills', DUNGEON_C, pad_table),
    ]

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / 'src' / 'data').mkdir(parents=True)

        for name, target, mutate in cases:
            base_d, base_p = dun, party
            if target == PARTY:
                base_p = mutate(party)
                changed = base_p != party
            else:
                base_d = mutate(dun)
                changed = base_d != dun
            if not changed:
                print('  SELFTEST INCONCLUSIVE (%s): mutation changed nothing '
                      '-- the source moved under this test' % name)
                ok = False
                continue

            (tmp / DUNGEON_C).write_text(base_d, encoding='utf-8', newline='\n')
            (tmp / PARTY).write_text(base_p, encoding='utf-8', newline='\n')

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

    # Takes the repo BOTH ways on purpose -- see check_start_menu_pages.py for
    # why the hand-maintained list in run_all_checks.sh makes this the pattern.
    if '--repo' in args:
        i = args.index('--repo')
        if i + 1 >= len(args):
            print('--repo needs a path')
            return 2
        repo = args[i + 1]
    elif args:
        repo = args[0]
    else:
        repo = '.'

    if is_selftest:
        print('--- selftest: breaking the invariant on purpose ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1

    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
