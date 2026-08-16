"""The melody accepted by ear must still be audible in the files that SHIP.

WHY THIS IS SEPARATE FROM check_gb_planner. That one guards the planner: given
a song, does the heuristic still pick the part that was chosen on a listen. It
reads the original .mid and reasons about intent, and it passed throughout every
fault reported in game -- because intent was never what was wrong.

What ships is a different pair of files: the generated _gb.mid, which has had
tracks dropped and programs pinned, and the generated gb_*.inc, which maps slots
to CGB voices. Everything that went wrong lived between the plan and those two:

  a melody wired onto the percussion slot, because '#N' keys count note-bearing
  tracks and the code counted chunks (mus_encounter_champion);

  a melody silenced outright, because an early hand-written variant used another
  song's slot numbers and the render had no melody and no bass in it at all
  (mus_encounter_brendan);

  a melody sharing square 1 with a counter line that changed program onto it
  part way through, so the two took turns holding the channel
  (mus_petalburg_woods, and thirty others).

None of those are visible in a plan. All of them are visible here, because this
reads only what the assembler will read.

WHAT IS ASSERTED. For every song with an arrangement accepted by ear, the slot
carrying its melody is selected by some track of the shipped _gb.mid, and the
shipped voicegroup gives that slot a real CGB voice rather than the silent
entry. Track INDEX is deliberately not asserted: dropping a track renumbers the
ones after it, so the index in the _gb.mid is not the index the plan was written
against, and asserting it would fail on correct output.

Usage:  python3 tools/rogue/check_gb_shipped_melody.py [REPO | --repo PATH]
        python3 tools/rogue/check_gb_shipped_melody.py --selftest
"""
import importlib.util
import re
import sys
from pathlib import Path


def load(repo, name):
    spec = importlib.util.spec_from_file_location(
        name, Path(repo) / 'tools/rogue' / (name + '.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def voicegroup_for(repo, song):
    cfg = (Path(repo) / 'sound/songs/midi/midi.cfg').read_text(encoding='utf-8')
    m = re.search(r'^%s_gb\.mid:\s*(.*)$' % re.escape(song), cfg, re.M)
    if not m:
        return None
    g = re.search(r'-G(\S+)', m.group(1))
    if not g:
        return None
    return Path(repo) / 'sound/voicegroups' / (g.group(1).lstrip('_') + '.inc')


def check(repo):
    repo = Path(repo)
    wire = load(repo, 'wire_gb_song')
    rgp = load(repo, 'render_gb_preview')
    planner_check = load(repo, 'check_gb_planner')

    wanted = dict(planner_check.ACCEPTED)
    wanted.update(planner_check.KNOWN_DIFFERENT)

    bad, checked = [], 0
    for song, (_idx, slot) in sorted(wanted.items()):
        inc = voicegroup_for(repo, song)
        gb_mid = repo / 'sound/songs/midi' / (song + '_gb.mid')
        if inc is None or not inc.exists():
            bad.append((song, 'no shipped voicegroup'))
            continue
        if not gb_mid.exists():
            bad.append((song, 'no shipped %s' % gb_mid.name))
            continue

        selected = [p[0] for _, p in
                    wire.note_tracks(bytearray(gb_mid.read_bytes())) if p]
        if slot not in selected:
            bad.append((song, 'the accepted melody is slot %d and NO track of '
                              'the shipped midi selects it -- it cannot sound '
                              '(tracks are on %s)'
                              % (slot, sorted(set(selected)))))
            continue

        voice = rgp.voices_from_inc(inc).get(slot)
        if voice is None or voice['kind'] == 'drop':
            bad.append((song, 'slot %d carries the accepted melody but the '
                              'shipped voicegroup silences it' % slot))
            continue
        checked += 1

    if bad:
        print('FAIL  check_gb_shipped_melody.py: an accepted melody does not '
              'survive into the files the ROM plays')
        for song, why in bad:
            print('        %-27s %s' % (song, why))
        return False

    if not checked:
        print('FAIL  check_gb_shipped_melody.py: nothing to check')
        return False

    print('PASS  check_gb_shipped_melody.py  (%d accepted melodies audible in '
          'the shipped files)' % checked)
    return True


def selftest(repo):
    """Silence one accepted melody in its shipped voicegroup; require a fail."""
    repo = Path(repo)
    rgp = load(repo, 'render_gb_preview')
    wire = load(repo, 'wire_gb_song')
    planner_check = load(repo, 'check_gb_planner')

    song, (_idx, slot) = sorted(planner_check.ACCEPTED.items())[0]
    inc = voicegroup_for(repo, song)
    if inc is None or not inc.exists():
        print('  SELFTEST INCONCLUSIVE: %s has no shipped voicegroup' % song)
        return False

    original = inc.read_text(encoding='utf-8')
    # Rewrite that slot's voice line to the generator's silent entry, in place,
    # so the mutation is the real failure mode rather than a missing file.
    lines = original.split('\n')
    at, n = None, 0
    start = next(k for k, l in enumerate(lines) if l.startswith('voice_group'))
    for k in range(start + 1, len(lines)):
        s = lines[k].strip()
        if not s or s.startswith('@'):
            continue
        if n == slot:
            at = k
            break
        n += 1
    if at is None:
        print('  SELFTEST INCONCLUSIVE: slot %d is past the end' % slot)
        return False

    ok = True
    try:
        lines[at] = '\t' + wire.SILENT
        inc.write_text('\n'.join(lines), encoding='utf-8', newline='\n')
        if rgp.voices_from_inc(inc).get(slot, {}).get('kind') != 'drop':
            print('  SELFTEST INCONCLUSIVE: the mutation did not read back '
                  'as silent')
            return False
        if check(repo):
            print('  SELFTEST FAILED: the check passed with %s\'s melody '
                  'silenced' % song)
            ok = False
        else:
            print('  selftest ok: fired on "an accepted melody is silenced in '
                  'the shipped voicegroup"')
    finally:
        inc.write_text(original, encoding='utf-8', newline='\n')
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
        print('--- selftest: silencing an accepted melody ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1
    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
