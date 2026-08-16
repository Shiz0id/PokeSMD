"""Guard the GB arrangement planner against the arrangements accepted by ear.

WHY THIS EXISTS. plan_gb_arrangement.py decides which musical part goes on which
of the four PSG channels. Its output is judged by LISTENING, which is expensive
and cannot be automated -- so every arrangement that has been accepted is a
scarce, hard-won data point. Twenty-two songs are in the ROM and fourteen more
dungeon tracks are coming, with a possible full-soundtrack pass after that, so
the planner will keep changing. This pins what it already gets right.

WHAT IT ASSERTS. For each song whose arrangement was accepted on a listen, the
planner must still choose the same MELODY part. The melody is the pick that
matters: five of the six hand corrections in this project were the planner
calling an ornament the tune -- a harp arpeggio, a 15-note accent, a high
sparkle layer, a bass duplicate.

TWO SONGS ARE LISTED AS KNOWN DIFFERENCES rather than failures, because they are
not the planner being wrong:

  vs_trainer -- the planner prefers the strings part; the accepted arrangement
    leads with the piano. Both are defensible and piano is genuinely ambiguous
    (it leads here, accompanies in rich and woods). Forcing agreement would mean
    encoding a rule that is really a per-song taste.

  brendan -- the accepted pick differs by OCTAVE, not by part. Its two melody
    tracks are the same line an octave apart and the upper was chosen by ear.
    The planner has no notion of picking between octaves of one melody.

Usage:  python3 tools/rogue/check_gb_planner.py [REPO | --repo PATH]
        python3 tools/rogue/check_gb_planner.py --selftest
"""
import importlib.util
import sys
from pathlib import Path

# song -> (track index, slot) of the melody in the ACCEPTED arrangement.
ACCEPTED = {
    'mus_petalburg_woods':       (2, 73),
    'mus_surf':                  (0, 48),
    'mus_underwater':            (4, 9),
    'mus_encounter_rich':        (1, 73),
    'mus_encounter_champion':    (1, 17),
    'mus_encounter_swimmer':     (4, 81),
    'mus_encounter_interviewer': (1, 56),
}
# Documented differences -- see the module docstring. Listed so that a change
# which accidentally FIXES one is noticed too.
KNOWN_DIFFERENT = {
    'mus_vs_trainer':        (0, 1),
    'mus_encounter_brendan': (9, 83),
}

# Parts that are duplicates of another part and must be recognised as such. A
# copy scores identically to the real part on pitch, note count and sounding
# time, so without this it can win a channel and contribute nothing -- which is
# what left mus_encounter_rich playing its bass twice while the strings, the
# longest-sounding part in the track, were dropped.
REDUNDANT = {
    'mus_encounter_rich': (4, 3),      # track 4 is +0 from the tuba, all 73 notes
    'mus_vs_trainer':     (3, 4),
}


def load(repo, name):
    spec = importlib.util.spec_from_file_location(name, repo / 'tools/rogue' /
                                                  (name + '.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def melody_of(repo, rgp, pl, song):
    mid = repo / 'sound/songs/midi' / (song + '.mid')
    if not mid.exists():
        return None
    tracks = rgp.parse_midi(mid)
    if not tracks:
        return None
    info, out = pl.plan(list(tracks), pl.source_voices(repo, song))
    for i, (_, role) in out.items():
        if role == 'MELODY':
            return (i, info[i]['slot'])
    return None


def check(repo):
    repo = Path(repo)
    rgp = load(repo, 'render_gb_preview')
    pl = load(repo, 'plan_gb_arrangement')

    bad = []
    for song, want in sorted(ACCEPTED.items()):
        got = melody_of(repo, rgp, pl, song)
        if got != want:
            bad.append((song, want, got))
    if bad:
        print('FAIL  check_gb_planner.py: the planner no longer reproduces '
              'arrangements that were accepted by ear')
        for song, want, got in bad:
            print('        %-27s accepted #%s slot %s, now %s'
                  % (song, want[0], want[1],
                     'no melody' if got is None else '#%s slot %s' % got))
        return False

    for song, (copy, orig) in sorted(REDUNDANT.items()):
        mid = repo / 'sound/songs/midi' / (song + '.mid')
        tracks = rgp.parse_midi(mid)
        info, out = pl.plan(list(tracks), pl.source_voices(repo, song))
        ch, role = out.get(copy, ('?', '?'))
        if ch != 'drop' or 'copy' not in role:
            bad.append((song, 'track #%d dropped as a copy of #%d' % (copy, orig),
                        '%s / %s' % (ch, role)))
    if bad:
        print('FAIL  check_gb_planner.py: a duplicate part is no longer '
              'recognised as one')
        for song, want, got in bad:
            print('        %-27s expected %s, got %s' % (song, want, got))
        return False

    fixed = [s for s, want in sorted(KNOWN_DIFFERENT.items())
             if melody_of(repo, rgp, pl, s) == want]
    print('PASS  check_gb_planner.py  (%d accepted arrangements reproduced%s)'
          % (len(ACCEPTED),
             '; %s now agree too -- move them out of KNOWN_DIFFERENT'
             % ', '.join(fixed) if fixed else ''))
    return True


def selftest(repo):
    """Break the melody rule and require the check to fire."""
    repo = Path(repo)
    src = (repo / 'tools/rogue/plan_gb_arrangement.py').read_text(encoding='utf-8')
    cases = [
        ('instrument names ignored',
         lambda s: s.replace("named = [s for s in free\n                 if has_word",
                             "named = [] or [s for s in free\n                 if not has_word", 1)),
        # NOT a case: blanking TEXTURE_WORDS leaves all seven melody picks
        # unchanged, because the named-instrument tier already resolves them.
        # A selftest case that never fires is noise, so it is not listed --
        # and the fact that the exclusion is not load-bearing is worth knowing.
        ('redundant copies kept',
         lambda s: s.replace('return same > 0.85 * len(d)', 'return False', 1)),
    ]
    # MUTATE IN PLACE and restore. A first cut copied tools/ into a temp dir and
    # symlinked sound/; every mutation then reported "no melody" for every song,
    # because the songs were not being found at all. That is a selftest which
    # proves the check notices a broken repo -- not that it notices a wrong
    # pick. Patching the real file is uglier and actually tests the thing.
    target = repo / 'tools/rogue/plan_gb_arrangement.py'
    ok = True
    for name, mutate in cases:
        broken = mutate(src)
        if broken == src:
            print('  SELFTEST INCONCLUSIVE (%s): mutation changed nothing' % name)
            ok = False
            continue
        try:
            target.write_text(broken, encoding='utf-8', newline='\n')
            try:
                passed = check(repo)
            except Exception as e:
                print('  SELFTEST INCONCLUSIVE (%s): planner raised %s -- the '
                      'mutation broke it rather than changing its answer'
                      % (name, e.__class__.__name__))
                ok = False
                continue
            if passed:
                print('  SELFTEST FAILED (%s): the check still passed' % name)
                ok = False
            else:
                print('  selftest ok: fired on "%s"' % name)
        finally:
            target.write_text(src, encoding='utf-8', newline='\n')
    return ok


def main():
    args = list(sys.argv[1:])
    is_self = '--selftest' in args
    if is_self:
        args.remove('--selftest')
    if '--repo' in args:
        i = args.index('--repo')
        repo = args[i + 1] if i + 1 < len(args) else '.'
    else:
        repo = args[0] if args else '.'
    if is_self:
        print('--- selftest: breaking the planner on purpose ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1
    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
