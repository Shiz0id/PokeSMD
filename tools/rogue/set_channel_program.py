"""Reassign one MIDI channel's program, for cart rips whose numbers are not GM.

WHY THIS IS NEEDED AT ALL. Songs imported from the GBA Music Pack play through
voicegroup_all_instruments, which is GENERAL MIDI ORDERED - program 47 really is
Timpani, 60 really is French Horn. Direct SDAT cart rips carry the DS game's own
bank indices instead, and those mean nothing to a GM bank. Most land somewhere
harmless; occasionally one lands somewhere absurd.

HOW TO SPOT ONE, because it is not obvious from the number. Compare the program
against the PITCH RANGE the channel actually plays. DPPt's Natural Disaster had
102 notes between MIDI 26 and 39 - deep bass, an octave below the bass clef - on
program 3, Honky-tonk Piano, while every other channel in the song was coherent
orchestral writing: strings at 48, French horn at 60, trumpet at 56, timpani at
47. A honky-tonk piano playing D1 in a GM bank is thin and quiet, so the part
read as BOTH wrong and inaudible, and the obvious diagnosis - "the song is
quiet, raise -V" - was the wrong one. Its effective loudness was already 62
against a vanilla median of 37.

So: a mallet or piano program playing below about MIDI 40, or a bass program up
in the treble, is worth looking at. Everything else is probably fine.

BUT THAT HEURISTIC HAS A BLIND SPOT, and it fired on a correct song immediately.
mus_dppt_opening has 108 notes on ONE pitch - 38, the GM snare - on program 13,
which looks exactly like a xylophone stranded in the bass. It is not: that song
is on voicegroup_all_instruments_kit_13, where program 13 IS a drum kit, so it
is a snare track working as intended. Check midi.cfg for _kit_13 before treating
a program-13 hit as a defect, and remember that ONE pitch struck a hundred times
is the signature of a drum track, not of a melody.

THIS IS A MUSICAL JUDGEMENT AND THE TOOL DOES NOT MAKE IT. It changes the number
you name on the channel you name and reports what it found there first, so the
before and after are both on the record and reverting is another run.

ATTACK MATTERS AS MUCH AS THE INSTRUMENT, and it is the subtler half. In
voicegroup_all_instruments, programs 48, 49 and 50 - String Ensemble 1 and 2 and
Synth Strings 1 - are the ONLY slow-attack voices in the bank; 48 is attack 128
and 49 is attack 9, against 255 for everything else. A part written in short
notes on one of those fades in and never reaches full volume, so the channel is
audibly quiet no matter what its velocity and CC7 say.

That is a whole-song effect when the strings carry the song. DPPt's Natural
Disaster had 198 of its 356 notes on program 48 and read as quiet even at an
effective loudness of 62, the highest measured anywhere in the ROM.

The swap that fixes it without changing the sound: program 44, Tremolo Strings,
is built from THE SAME SAMPLES as program 48 - DirectSoundWaveData_ai_tremolo_
strings_00 through _02 - with attack 255 instead of 128. Same strings, they just
speak immediately.

Run:  python3 tools/rogue/set_channel_program.py FILE --channel N --program P
      python3 tools/rogue/set_channel_program.py FILE --remap FROM:TO
      python3 tools/rogue/set_channel_program.py FILE --show
      python3 tools/rogue/set_channel_program.py --selftest
"""
import argparse
import struct
import sys
import collections


def chunks(data):
    i = 0
    while i < len(data) - 8:
        tag = data[i:i + 4]
        if tag not in (b'MThd', b'MTrk'):
            i += 1
            continue
        length = struct.unpack('>I', data[i + 4:i + 8])[0]
        yield i + 8, length, tag
        i += 8 + length


def walk(body):
    """Yield (offset_of_first_data_byte, status) for channel voice messages."""
    j = 0
    status = 0
    while j < len(body):
        while j < len(body) and body[j] & 0x80:
            j += 1
        j += 1
        if j >= len(body):
            return
        b = body[j]
        if b == 0xFF:
            j += 2
            length = 0
            while body[j] & 0x80:
                length = (length | (body[j] & 0x7F)) << 7
                j += 1
            length |= body[j]
            j += 1 + length
            continue
        if b in (0xF0, 0xF7):
            j += 1
            length = 0
            while body[j] & 0x80:
                length = (length | (body[j] & 0x7F)) << 7
                j += 1
            length |= body[j]
            j += 1 + length
            continue
        if b & 0x80:
            status = b
            j += 1
        yield j, status
        e = status & 0xF0
        j += 1 if e in (0xC0, 0xD0) else 2


def survey(path):
    """{channel: (programs, note count, lowest pitch, highest pitch)}"""
    data = open(path, 'rb').read()
    progs = collections.defaultdict(set)
    notes = collections.Counter()
    lo, hi = {}, {}
    for at, ln, tag in chunks(data):
        if tag != b'MTrk':
            continue
        body = data[at:at + ln]
        for off, status in walk(body):
            e, ch = status & 0xF0, status & 0x0F
            if e == 0xC0:
                progs[ch].add(body[off])
            elif e == 0x90 and body[off + 1] > 0:
                notes[ch] += 1
                k = body[off]
                lo[ch] = min(lo.get(ch, 127), k)
                hi[ch] = max(hi.get(ch, 0), k)
    return {ch: (sorted(progs.get(ch, [])), notes.get(ch, 0),
                 lo.get(ch), hi.get(ch))
            for ch in sorted(set(list(progs) + list(notes)))}


def set_program(path, channel, program, dry_run=False):
    data = bytearray(open(path, 'rb').read())
    before = set()
    changed = 0
    for at, ln, tag in chunks(data):
        if tag != b'MTrk':
            continue
        body = data[at:at + ln]
        for off, status in walk(body):
            if (status & 0xF0) == 0xC0 and (status & 0x0F) == channel:
                before.add(body[off])
                data[at + off] = program
                changed += 1
    if changed and not dry_run:
        open(path, 'wb').write(bytes(data))
    return sorted(before), changed


def remap_program(path, old_program, new_program, dry_run=False):
    """Change one program number to another wherever it appears.

    Needed because a channel is not always one instrument: Natural Disaster's
    channels 1 and 4 select BOTH strings (48) and trumpet (56), so setting the
    whole channel would have silently replaced the trumpet too.
    """
    data = bytearray(open(path, 'rb').read())
    changed = 0
    for at, ln, tag in chunks(data):
        if tag != b'MTrk':
            continue
        body = data[at:at + ln]
        for off, status in walk(body):
            if (status & 0xF0) == 0xC0 and body[off] == old_program:
                data[at + off] = new_program
                changed += 1
    if changed and not dry_run:
        open(path, 'wb').write(bytes(data))
    return changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--channel", type=int)
    ap.add_argument("--program", type=int)
    ap.add_argument("--remap", metavar="FROM:TO",
                    help="change program FROM to TO on every channel")
    ap.add_argument("--show", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.files:
        print(__doc__)
        return 2

    for f in args.files:
        name = f.split('/')[-1]
        if args.remap:
            a, _, b = args.remap.partition(':')
            n = remap_program(f, int(a), int(b), args.dry_run)
            print(f'  {"would remap" if args.dry_run else "remapped"} program '
                  f'{a} -> {b} on {n} event(s)  {name}')
            continue
        if args.show or args.channel is None or args.program is None:
            print(f'--- {name}')
            for ch, (progs, n, lo, hi) in survey(f).items():
                print(f'    ch{ch:>2}  {n:>5} notes  pitch '
                      f'{lo if lo is not None else "-"}-'
                      f'{hi if hi is not None else "-":<4}  '
                      f'programs {progs or "(none)"}')
            continue
        before, changed = set_program(f, args.channel, args.program, args.dry_run)
        if not changed:
            print(f'  no program change on channel {args.channel}  {name}')
        else:
            print(f'  {"would set" if args.dry_run else "set"} ch{args.channel} '
                  f'program {before} -> {args.program} '
                  f'({changed} event(s))  {name}')
    return 0


def selftest():
    import tempfile
    import os

    def build(events):
        track = b''.join(b'\x00' + e for e in events) + b'\x00\xFF\x2F\x00'
        return (b'MThd' + struct.pack('>I', 6) + b'\x00\x00\x00\x01\x00\x60'
                + b'MTrk' + struct.pack('>I', len(track)) + track)

    ok = 0
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, 't.mid')

        # 1. The named channel's program changes.
        open(p, 'wb').write(build([b'\xC0\x03', b'\x90\x40\x64']))
        before, n = set_program(p, 0, 48)
        fired = before == [3] and n == 1 and b'\xC0\x30' in open(p, 'rb').read()
        print(f'  {"ok     " if fired else "*** FAILED ***"}  channel 0 program 3 -> 48')
        ok += fired

        # 2. Other channels are untouched - the bug this would cause is silent.
        open(p, 'wb').write(build([b'\xC0\x03', b'\xC1\x03']))
        set_program(p, 0, 48)
        d = open(p, 'rb').read()
        fired = b'\xC1\x03' in d and b'\xC0\x30' in d
        print(f'  {"ok     " if fired else "*** FAILED ***"}  other channels left alone')
        ok += fired

        # 3. EVERY program change on the channel moves, not just the first - a
        #    song that re-selects mid-piece would otherwise revert halfway.
        open(p, 'wb').write(build([b'\xC0\x03', b'\x90\x40\x64', b'\xC0\x03']))
        _, n = set_program(p, 0, 48)
        fired = n == 2
        print(f'  {"ok     " if fired else "*** FAILED ***"}  all program changes on the channel move (got {n})')
        ok += fired

        # 4. survey() reports the pitch range, which is how a wrong program is
        #    spotted in the first place.
        open(p, 'wb').write(build([b'\xC0\x03', b'\x90\x1A\x64', b'\x90\x27\x64']))
        s = survey(p)
        fired = s[0][2] == 26 and s[0][3] == 39
        print(f'  {"ok     " if fired else "*** FAILED ***"}  survey reports the pitch range (got {s[0][2]}-{s[0][3]})')
        ok += fired

        # 5. --remap moves the program wherever it appears and leaves other
        #    programs on the SAME channel alone - the case that made a plain
        #    channel set unusable here.
        open(p, 'wb').write(build([b'\xC1\x30', b'\xC1\x38', b'\xC3\x30']))
        n = remap_program(p, 48, 44)
        d5 = open(p, 'rb').read()
        fired = (n == 2 and b'\xC1\x2C' in d5 and b'\xC3\x2C' in d5
                 and b'\xC1\x38' in d5)
        print(f'  {"ok     " if fired else "*** FAILED ***"}  --remap moves 48->44 but leaves 56 alone (got {n})')
        ok += fired

    print(f'\n{ok}/5 selftest cases behave')
    return 0 if ok == 5 else 1


if __name__ == "__main__":
    sys.exit(main())
