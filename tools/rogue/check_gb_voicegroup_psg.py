"""Every slot of every gb_* voicegroup must be a PSG voice.

WHAT THIS GUARDS, AND WHY IT FAILS SILENTLY WITHOUT IT.

The thirty-six "GB voice" songs are NOT played by the GBS sequencer. They are
ordinary m4a songs pointed at a voicegroup in which every entry is one of the
four CGB voice types, so m4a drives the same PSG hardware the Game Boy had. That
substitution is only true if the voicegroup really is all-PSG. One
voice_directsound left in a slot the song selects and that part plays on the
original sampled Emerald instrument, underneath the PSG reduction.

Nothing else in the tree can see that:

  - The BUILD cannot. A voice_directsound entry is valid asm in any voicegroup.
  - CONTENTION MEASUREMENTS cannot. m4a allocates sampled voices from a separate
    channel pool, so the sampled part steals nothing from the four PSG channels
    and every note-loss figure reads exactly the same.
  - The PREVIEW could not. render_gb_preview.py skips slots the arrangement does
    not mention, so it rendered those parts as ABSENT while the ROM played them
    as flutes and timpani. It printed a one-line 'note' about it, which is now a
    hard failure there too.

Twenty of thirty-six shipped songs were affected, and thirty-one voicegroups
carried at least one non-PSG slot. wire_gb_song.py's header had always claimed
unlisted slots were silenced; the code copied them through instead.

Usage:
  python3 tools/rogue/check_gb_voicegroup_psg.py [--repo PATH | PATH]
  python3 tools/rogue/check_gb_voicegroup_psg.py --selftest
"""
import re
import sys
import tempfile
from pathlib import Path

PSG_VOICES = frozenset(
    base + suffix
    for base in ('voice_square_1', 'voice_square_2',
                 'voice_programmable_wave', 'voice_noise')
    for suffix in ('', '_alt'))


def offenders(text):
    """-> [(slot, macro)] for every non-PSG slot in one voicegroup file.

    Slots are counted from the voice_group label, which is the macro that emits
    the symbol and is not itself a slot. Comments and blanks are not slots
    either -- getting that wrong shifts every slot number by one, which is how
    the first pass at this analysis misread the whole tree.
    """
    bad, slot, seen_label = [], -1, False
    for line in text.split('\n'):
        m = re.match(r'\s*(voice_\w+)', line)
        if not m:
            continue
        name = m.group(1)
        if name == 'voice_group':
            seen_label = True
            continue
        if not seen_label:
            continue
        slot += 1
        if name not in PSG_VOICES:
            bad.append((slot, name))
    return bad


def check(repo):
    vgdir = Path(repo) / 'sound/voicegroups'
    files = sorted(vgdir.glob('gb_*.inc'))
    if not files:
        print('FAIL: no gb_*.inc voicegroups found under %s' % vgdir)
        return False

    total = 0
    for path in files:
        bad = offenders(path.read_text(encoding='utf-8'))
        if bad:
            total += len(bad)
            print('FAIL: %s' % path.name)
            for slot, name in bad:
                print('    slot %3d  %s  <-- not a PSG voice; this part will '
                      'play on the sampled Emerald instrument' % (slot, name))

    if total:
        print('\n%d non-PSG slot(s) across %d voicegroup(s). Fix with:'
              % (total, sum(1 for f in files
                            if offenders(f.read_text(encoding='utf-8')))))
        print('    python3 tools/rogue/wire_gb_song.py --repo . --resilence')
        return False

    print('ok: %d gb_* voicegroups, every slot is a PSG voice' % len(files))
    return True


CLEAN = """@ generated
voice_group gb_selftest
\tvoice_square_1 60, 0, 0, 2, 0, 0, 15, 0
\tvoice_square_2_alt 60, 0, 0, 1, 7, 1
\tvoice_programmable_wave_alt 60, 0, ProgrammableWaveData_6, 0, 0, 15, 0
\tvoice_noise_alt 60, 0, 1, 0, 1, 0, 3
"""

CASES = [
    ('a directsound slot',
     CLEAN.replace('\tvoice_square_2_alt 60, 0, 0, 1, 7, 1',
                   '\tvoice_directsound 60, 0, DirectSoundWaveData_x, 255, 0, 255, 127')),
    ('a keysplit slot',
     CLEAN.replace('\tvoice_noise_alt 60, 0, 1, 0, 1, 0, 3',
                   '\tvoice_keysplit voicegroup_trumpet_keysplit, keysplit_trumpet')),
    ('a keysplit_all slot',
     CLEAN.replace('\tvoice_square_1 60, 0, 0, 2, 0, 0, 15, 0',
                   '\tvoice_keysplit_all voicegroup_x')),
]


def selftest(repo):
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        vg = Path(tmp) / 'sound/voicegroups'
        vg.mkdir(parents=True)

        # The clean tree must PASS, or every case below fires vacuously.
        (vg / 'gb_selftest.inc').write_text(CLEAN, encoding='utf-8', newline='\n')
        if not check(tmp):
            print('  selftest FAILED: the clean voicegroup did not pass')
            ok = False
        else:
            print('  selftest ok: clean voicegroup passes')

        for name, body in CASES:
            (vg / 'gb_selftest.inc').write_text(body, encoding='utf-8', newline='\n')
            if check(tmp):
                print('  selftest FAILED: did not fire on %s' % name)
                ok = False
            else:
                print('  selftest ok: fired on %s' % name)

        # A non-PSG line BEFORE the label is not a slot and must not fire --
        # otherwise the check reports on things that are not in the voicegroup.
        (vg / 'gb_selftest.inc').write_text(
            '@ voice_directsound in a comment\n' + CLEAN, encoding='utf-8',
            newline='\n')
        if not check(tmp):
            print('  selftest FAILED: fired on a commented-out voice')
            ok = False
        else:
            print('  selftest ok: a commented voice is not a slot')

        # An empty directory must FAIL rather than report success on nothing.
        (vg / 'gb_selftest.inc').unlink()
        if check(tmp):
            print('  selftest FAILED: passed with no voicegroups at all')
            ok = False
        else:
            print('  selftest ok: an empty tree fails instead of passing')

    return ok


def main():
    args = list(sys.argv[1:])

    is_selftest = '--selftest' in args
    if is_selftest:
        args.remove('--selftest')

    # Takes the repo BOTH ways on purpose -- see check_start_menu_pages.py.
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
