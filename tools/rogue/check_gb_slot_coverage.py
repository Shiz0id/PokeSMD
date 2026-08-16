"""Every voicegroup slot a GB song's MIDI selects must be one the plan set.

THE BUG THIS EXISTS FOR SHIPPED THIRTY-SIX SONGS WRONG. A MIDI track may change
program part way through -- mus_route119's first track walks
58 -> 56 -> 46 -> 9 -> 46 -> 56, and mus_petalburg_woods' melody alternates
73 -> 48 -> 73 -> 48. The planner and the preview both take a track's FIRST
program and treat it as the track's instrument, which is fine for them: the
preview renders one voice per track and sounds exactly as intended.

The ROM does not. m4a honours every program change, so after the first switch
the track selects a slot the plan never named, and the generator copies whatever
the SOURCE voicegroup had there. That filler is typically
voice_square_1 60, 0, 0, 2, 0, 0, 15, 0 -- a 50% duty square at full sustain
with no decay, on square 1, where it fights the melody for the channel.

Reported in game as three tracks sounding "nothing like the previews". Nothing
could see it: the build was clean, the voicegroup was well formed, every check
passed, and the preview was correct. Only the ROM was wrong.

WHAT IS ASSERTED. For every song wired into GB Sounds, the set of programs its
_gb.mid selects is a subset of the slots its generated voicegroup explicitly
set. The generator marks those with an "@ slot N:" comment, so the file records
its own coverage.

Usage:  python3 tools/rogue/check_gb_slot_coverage.py [REPO | --repo PATH]
        python3 tools/rogue/check_gb_slot_coverage.py --selftest
"""
import importlib.util
import re
import sys
from pathlib import Path


def load_wire(repo):
    spec = importlib.util.spec_from_file_location(
        'wire_gb_song', repo / 'tools/rogue/wire_gb_song.py')
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def songs_and_voicegroups(repo):
    cfg = (repo / 'sound/songs/midi/midi.cfg').read_text(encoding='utf-8')
    for m in re.finditer(r'^(mus_\S+)_gb\.mid:\s*(.*)$', cfg, re.M):
        g = re.search(r'-G(\S+)', m.group(2))
        if not g:
            continue
        yield (m.group(1),
               repo / 'sound/songs/midi' / (m.group(1) + '_gb.mid'),
               repo / 'sound/voicegroups' / (g.group(1).lstrip('_') + '.inc'))


def check(repo):
    repo = Path(repo)
    wire = load_wire(repo)

    checked, bad = 0, []
    for song, mid, vg in songs_and_voicegroups(repo):
        if not mid.exists():
            bad.append((song, 'no %s' % mid.name))
            continue
        if not vg.exists():
            bad.append((song, 'no %s' % vg.name))
            continue
        used = set()
        for _, progs in wire.note_tracks(bytearray(mid.read_bytes())):
            used |= set(progs)
        text = vg.read_text(encoding='utf-8')
        managed = {int(x) for x in re.findall(r'@ slot (\d+):', text)}
        body = [l.strip() for l in text.split('\n')
                if l.strip() and not l.strip().startswith('@')][1:]
        uncovered = sorted(s for s in used if s not in managed and s < len(body))
        if uncovered:
            bad.append((song, 'slots %s are selected by the midi but left on '
                              'the source filler' % uncovered))
        checked += 1

    if bad:
        print('FAIL  check_gb_slot_coverage.py: a track changes program onto a '
              'slot the arrangement never set')
        for song, why in bad:
            print('        %-28s %s' % (song, why))
        return False

    if checked == 0:
        print('FAIL  check_gb_slot_coverage.py: no GB songs found to check')
        return False

    print('PASS  check_gb_slot_coverage.py  (%d songs, every selected slot set)'
          % checked)
    return True


def selftest(repo):
    """Put a slot back on filler and require the check to fire."""
    repo = Path(repo)
    target = None
    for song, mid, vg in songs_and_voicegroups(repo):
        text = vg.read_text(encoding='utf-8')
        if len(re.findall(r'@ slot (\d+):', text)) > 2:
            target = vg
            break
    if target is None:
        print('  SELFTEST INCONCLUSIVE: no voicegroup with enough set slots')
        return False

    original = target.read_text(encoding='utf-8')
    ok = True
    try:
        # Strip one slot's marker AND its voice, restoring the filler beneath.
        broken = re.sub(r'\t@ slot \d+:[^\n]*\n\t\S[^\n]*\n', '', original, count=1)
        if broken == original:
            print('  SELFTEST INCONCLUSIVE: mutation changed nothing')
            return False
        target.write_text(broken, encoding='utf-8', newline='\n')
        if check(repo):
            print('  SELFTEST FAILED: the check still passed with a slot on filler')
            ok = False
        else:
            print('  selftest ok: fired on "a set slot reverts to filler"')
    finally:
        target.write_text(original, encoding='utf-8', newline='\n')
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
        print('--- selftest: putting a slot back on filler ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1
    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
