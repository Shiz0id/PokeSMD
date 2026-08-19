"""Point a MIDI's channel-10 drum track at program 13, so kit_13 catches it.

WHY THIS EXISTS. voicegroup_all_instruments_kit_13 puts a real drum kit at
program 13 and is what 64 imported songs already use - see PROGRAM_13_SPLIT.md.
It works because the GBA Music Pack's rips put their drum track ON PROGRAM 13
with GM drum-map note numbers, and m4a has no channel-10 concept at all: the
program byte is the only thing selecting a voice.

Rips from other sources do not follow that convention. Three imported by hand -
DPPt's gym leader theme, DPPt's Eterna City and HGSS's Champion Lance - put
their drums on MIDI CHANNEL 10, which is the correct GM place for them, and
select programs 1, 16 and 0 respectively. Those are melodic voices, so every
drum note plays as a pitched Bright Piano, Drawbar Organ or Grand Piano hit.
The gym leader theme is 603 snare notes; it is not subtle.

WHAT IT DOES, and what it deliberately does not. It rewrites the PROGRAM CHANGE
on channel 10 to 13 and touches nothing else - not the notes, not the timing,
not any other channel. The notes are already GM drum keys (35 kick, 38 snare,
42 closed hat), which is exactly what the kit at 13 is keyed for, so retargeting
the program is the whole fix. Pair it with -G_all_instruments_kit_13 in
midi.cfg; on its own it would make the drums a xylophone instead.

IT REFUSES IF PROGRAM 13 IS ALREADY IN USE on any channel, because then the
song has a real xylophone part and moving drums onto 13 would collide with it -
both would play through the kit and the melody would become percussion. All
three songs this was written for have 13 free, and the check is what makes that
a fact rather than an assumption.

Run:  python3 tools/rogue/retarget_drum_channel.py <file.mid> [...]
      python3 tools/rogue/retarget_drum_channel.py --selftest
"""
import struct
import sys

DRUM_CHANNEL = 9      # MIDI channel 10, zero-based
KIT_PROGRAM = 13


def walk(track):
    """Yield (offset_of_program_byte, channel) for every program change."""
    j = 0
    status = 0
    while j < len(track):
        while j < len(track) and track[j] & 0x80:
            j += 1
        j += 1
        if j >= len(track):
            return
        b = track[j]
        if b == 0xFF:
            j += 2
            length = 0
            while track[j] & 0x80:
                length = (length | (track[j] & 0x7F)) << 7
                j += 1
            length |= track[j]
            j += 1 + length
            continue
        if b in (0xF0, 0xF7):
            j += 1
            length = 0
            while track[j] & 0x80:
                length = (length | (track[j] & 0x7F)) << 7
                j += 1
            length |= track[j]
            j += 1 + length
            continue
        if b & 0x80:
            status = b
            j += 1
        event, channel = status & 0xF0, status & 0x0F
        if event == 0xC0:
            yield j, channel
            j += 1
        elif event in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
            j += 2
        elif event == 0xD0:
            j += 1
        else:
            j += 1


def chunks(data):
    """Yield (start_of_body, length, tag) for each MIDI chunk."""
    i = 0
    while i < len(data) - 8:
        tag = data[i:i + 4]
        if tag not in (b'MThd', b'MTrk'):
            i += 1
            continue
        length = struct.unpack('>I', data[i + 4:i + 8])[0]
        yield i + 8, length, tag
        i += 8 + length


def retarget(path, dry_run=False):
    data = bytearray(open(path, 'rb').read())
    moved = 0
    collides = []

    for start, length, tag in chunks(data):
        if tag != b'MTrk':
            continue
        body = data[start:start + length]
        for offset, channel in walk(body):
            if body[offset] == KIT_PROGRAM and channel != DRUM_CHANNEL:
                collides.append(channel)

    if collides:
        return None, f'program {KIT_PROGRAM} already used on channel(s) '                      f'{sorted(set(c + 1 for c in collides))}; retargeting '                      f'would collide with a real part'

    for start, length, tag in chunks(data):
        if tag != b'MTrk':
            continue
        body = data[start:start + length]
        for offset, channel in walk(body):
            if channel == DRUM_CHANNEL and body[offset] != KIT_PROGRAM:
                data[start + offset] = KIT_PROGRAM
                moved += 1

    if moved and not dry_run:
        open(path, 'wb').write(bytes(data))
    return moved, None


def selftest():
    """Build MIDIs in memory and require the tool to move exactly the right byte."""
    import tempfile
    import os

    def build(events):
        track = bytearray()
        for e in events:
            track += b'\x00' + e
        track += b'\x00\xFF\x2F\x00'
        return (b'MThd' + struct.pack('>I', 6) + b'\x00\x00\x00\x01\x00\x60'
                + b'MTrk' + struct.pack('>I', len(track)) + bytes(track))

    ok = 0
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, 't.mid')

        # 1. A channel-10 program change is moved to 13.
        open(p, 'wb').write(build([b'\xC9\x01', b'\x99\x26\x64']))
        moved, err = retarget(p)
        got = open(p, 'rb').read()
        fired = moved == 1 and err is None and b'\xC9\x0D' in got
        print(f'  {"ok     " if fired else "*** FAILED ***"}  channel 10 retargeted to 13')
        ok += fired

        # 2. A melodic channel is NOT touched.
        open(p, 'wb').write(build([b'\xC0\x01', b'\x90\x40\x64']))
        moved, err = retarget(p)
        got = open(p, 'rb').read()
        fired = moved == 0 and b'\xC0\x01' in got
        print(f'  {"ok     " if fired else "*** FAILED ***"}  melodic channel left alone')
        ok += fired

        # 3. Program 13 already in melodic use is REFUSED, not silently merged.
        open(p, 'wb').write(build([b'\xC0\x0D', b'\xC9\x01']))
        before = open(p, 'rb').read()
        moved, err = retarget(p)
        fired = moved is None and err and open(p, 'rb').read() == before
        print(f'  {"ok     " if fired else "*** FAILED ***"}  refuses when program 13 is taken')
        ok += fired

    print(f'\n{ok}/3 selftest cases behave')
    return 0 if ok == 3 else 1


def main():
    args = sys.argv[1:]
    if '--selftest' in args:
        return selftest()
    if not args:
        print(__doc__)
        return 2
    dry = '--dry-run' in args
    rc = 0
    for path in [a for a in args if not a.startswith('--')]:
        moved, err = retarget(path, dry)
        name = path.split('/')[-1]
        if err:
            print(f'  REFUSED  {name}: {err}')
            rc = 1
        else:
            print(f'  {"would move" if dry else "moved"} {moved} program change(s)  {name}')
    return rc


if __name__ == '__main__':
    sys.exit(main())
