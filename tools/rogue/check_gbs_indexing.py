"""Guard the two indexing rules in the vendored GBS engine.

WHY THIS EXISTS. src/gbs.c is third-party code that has now produced two freezes,
neither of which the build or any other check could see:

  1. ply_gbs_switch indexed soundInfo->cgbChans with a RAW gbChannel. GBS numbers
     channels 0-3 for music and 4-7 for sound effects, but gCgbChans has exactly
     FOUR elements -- and what follows it in m4a.c is gMPlayInfo_SE1 and
     gMPlayInfo_SE2, whose first member is a pointer. statusFlags is the first
     byte of CgbChannel, so a GBS sound effect wrote zeros straight into a live
     music player. The sound effects are the ONLY tracks using 4-7, so this froze
     the game the first time one played: obtaining an item.

  2. GetSong indexed gGBSSongTable having only tested the id against
     GBS_MUSIC_NONE, never against GBS_MUSIC_COUNT. That id is the third column
     of sound/song_table.inc -- 610 hand-editable rows.

Both are "an index into an engine table", which this project's own notes call out
as something a data check cannot catch. So this checks the SOURCE, the way
check_charms.py asserts a mirrored pair rather than a table value.

Rule 2 is checked PER LOOKUP SITE, keyed on the expression each site indexes
with. It used to count occurrences of the literal name "gbsSongId" and compare
that count against the number of lookups, which failed in both directions the
moment the jukebox added a third lookup under a different variable: it reported
correct, bounded code as unbounded, and it would equally have PASSED a genuinely
unbounded lookup as long as some other site still carried the string.

Usage:  python3 tools/rogue/check_gbs_indexing.py [REPO | --repo PATH]
        python3 tools/rogue/check_gbs_indexing.py --selftest
"""
import re
import sys
import tempfile
from pathlib import Path

GBS_C = 'src/gbs.c'
M4A_C = 'src/m4a.c'

# How far above a gGBSSongTable lookup its bound may sit. Every real site guards
# on the line immediately above; the nearest OTHER site is nine lines away, so
# this window cannot let one lookup borrow a neighbour's bound and pass.
GUARD_LINES = 4

# Index expressions that are safe for a four-element array.
SAFE_CGB_INDEX = re.compile(
    r'^(cgbChannel|CGBCHANNEL_[A-Z]+|track->channelId - 1|channel)$')


def fail(msg):
    print('FAIL  check_gbs_indexing.py: %s' % msg)
    return False


def check(repo):
    repo = Path(repo)
    gbs = (repo / GBS_C).read_text(encoding='utf-8', errors='replace')
    m4a = (repo / M4A_C).read_text(encoding='utf-8', errors='replace')

    # 0. The premise: gCgbChans really is four elements. If someone grows it,
    #    this check's whole reason to exist changes and it should be re-read.
    m = re.search(r'struct CgbChannel gCgbChans\[(\d+)\]', m4a)
    if not m:
        return fail('gCgbChans declaration not found in %s' % M4A_C)
    if int(m.group(1)) != 4:
        return fail('gCgbChans is now %s elements, not 4 -- re-read this check'
                    % m.group(1))

    # 1. Every cgbChans index must be one of the safe forms.
    bad = [i for i in re.findall(r'cgbChans\[([^\]]+)\]', gbs)
           if not SAFE_CGB_INDEX.match(i.strip())]
    if bad:
        return fail('cgbChans indexed by %s -- GBS channel ids run 0..7 and this '
                    'array has 4 elements; use the %% 4 form' % sorted(set(bad)))

    # 2. ply_gbs_switch must still derive the wrapped channel, and must not use
    #    the raw one for the used-bit shift either.
    m = re.search(r'void ply_gbs_switch\([^)]*\)\s*\{(.*?)\n\}', gbs, re.S)
    if not m:
        return fail('ply_gbs_switch not found in %s' % GBS_C)
    body = m.group(1)
    if not re.search(r'cgbChannel\s*=\s*gbChannel\s*%\s*4', body):
        return fail('ply_gbs_switch no longer derives cgbChannel = gbChannel % 4')
    for shift in re.findall(r'gUsedCGBChannels\s*&=\s*~\(1 << ([^)]+)\)', body):
        if shift.strip() != 'cgbChannel':
            return fail('gUsedCGBChannels cleared with bit "%s" -- it is a '
                        'four-bit mask, so a raw 4..7 shift clears nothing and '
                        'leaves the real bit set' % shift.strip())

    # 3. Every gGBSSongTable index must be bounds-checked in the condition
    #    guarding it, against the SAME expression it is indexed with. See the
    #    module docstring for why this is per-site rather than a count.
    lines = gbs.split('\n')
    sites = [(n, m.group(1).strip())
             for n, line in enumerate(lines, 1)
             for m in re.finditer(r'gGBSSongTable\[([^\]]+)\]', line)]
    if not sites:
        return fail('gGBSSongTable is never indexed -- has GetSong moved?')

    unbounded = []
    for n, var in sites:
        guard = '\n'.join(lines[max(0, n - 1 - GUARD_LINES):n])
        if not re.search(r'%s\s*<\s*GBS_MUSIC_COUNT' % re.escape(var), guard):
            unbounded.append('line %d, gGBSSongTable[%s]' % (n, var))
    if unbounded:
        return fail('%d of %d gGBSSongTable lookups have no "< GBS_MUSIC_COUNT" '
                    'on the index expression within %d lines above them (%s) -- '
                    'an out-of-range id in song_table.inc or the music player '
                    'table would return garbage as a song header'
                    % (len(unbounded), len(sites), GUARD_LINES,
                       '; '.join(unbounded)))

    print('PASS  check_gbs_indexing.py  (cgbChans 4-safe, %d bounded '
          'gGBSSongTable lookups)' % len(sites))
    return True


def selftest(repo):
    repo = Path(repo)
    gbs = (repo / GBS_C).read_text(encoding='utf-8', errors='replace')
    m4a = (repo / M4A_C).read_text(encoding='utf-8', errors='replace')

    cases = [
        ('the real bug: raw gbChannel indexing cgbChans', GBS_C,
         lambda s: s.replace('cgbChans[cgbChannel]', 'cgbChans[gbChannel]', 1)),
        ('raw gbChannel in the used-bit shift', GBS_C,
         lambda s: s.replace('~(1 << cgbChannel)', '~(1 << gbChannel)', 1)),
        ('the % 4 derivation removed', GBS_C,
         lambda s: s.replace('cgbChannel = gbChannel % 4', 'cgbChannel = gbChannel', 1)),
        # One case per lookup site, because the point of checking per site is
        # that breaking ONE of them fires while the others still carry a bound.
        ('gGBSSongTable bound dropped in GetSong', GBS_C,
         lambda s: s.replace(' && gbsSongId < GBS_MUSIC_COUNT', '', 1)),
        ('gGBSSongTable bound dropped on the jukebox lookup', GBS_C,
         lambda s: s.replace(' && forcedGbsId < GBS_MUSIC_COUNT', '', 1)),
        ('gCgbChans grown without re-reading this check', M4A_C,
         lambda s: s.replace('struct CgbChannel gCgbChans[4]',
                             'struct CgbChannel gCgbChans[8]', 1)),
    ]

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / 'src').mkdir(parents=True)
        for name, target, mutate in cases:
            g, m = gbs, m4a
            if target == GBS_C:
                g = mutate(gbs)
                unchanged = (g == gbs)
            else:
                m = mutate(m4a)
                unchanged = (m == m4a)
            if unchanged:
                print('  SELFTEST INCONCLUSIVE (%s): mutation changed nothing' % name)
                ok = False
                continue
            (tmp / GBS_C).write_text(g, encoding='utf-8', newline='\n')
            (tmp / M4A_C).write_text(m, encoding='utf-8', newline='\n')
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
        print('--- selftest: breaking the invariant on purpose ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1
    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
