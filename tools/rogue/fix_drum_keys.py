"""Bring a channel-10 drum track inside frlg_drumset's key range.

WHY THE RANGE IS WHAT IT IS. sound/voicegroups/drumsets/frlg.inc opens with
`voice_group frlg_drumset, 36`, and asm/macros/m4a.inc turns that into

    .set voicegroup_frlg_drumset, . - 36 * 0xC

- the symbol is placed 36 twelve-byte entries BEFORE the data, so that note 36
indexes the first real voice. With 54 voices the kit covers keys 36 to 89.

A NOTE BELOW 36 THEREFORE READS BEHIND THE TABLE. m4a does not range-check; it
computes group[key] and plays whatever twelve bytes are there, which is the tail
of whatever the linker put before this voicegroup. It is not silence and it is
not a wrong drum, it is undefined.

KEY 35 IS THE COMMON CASE AND IS A FREE FIX. GM 35 is Acoustic Bass Drum and 36
is Bass Drum 1 - the same instrument, and rips disagree about which to use. So
35 -> 36 is a true equivalence rather than a musical choice, and on the DPPt
battle rips it is the difference between 74% of the drum track reading
out of bounds and none of it.

WHAT IT DOES NOT DO. Keys below 35 - 27 to 34 are the extended GM2 percussion,
slap, scratch, sticks, square click - have no equivalent in this kit at all.
They are REPORTED, not moved, because clamping them to 36 would turn a stick
click into a bass drum. A song with a meaningful number of them should stay on
the melodic bank instead; this tool prints the count so that call can be made
from a number.

Run:  python3 tools/rogue/fix_drum_keys.py <file.mid> [...] [--dry-run]
      python3 tools/rogue/fix_drum_keys.py --selftest
"""
import struct
import sys

DRUM_CHANNEL = 9
KIT_LO, KIT_HI = 36, 89
FROM, TO = 35, 36

# Where the strays go with --strays, and why this key and not 36.
#
# GM 27-34 are the extended percussion block: high Q, slap, scratch push and
# pull, sticks, square click, metronome click and bell. Every one of them is a
# short dry tick, so the nearest thing this kit has is 37, SIDE STICK - not 36,
# the bass drum, which is why plain clamping to KIT_LO is refused.
#
# It is still an APPROXIMATION and is opt-in for that reason. A song with a
# handful of strays is better off with them as side sticks than reading behind
# the voicegroup; a song built around them wants a different kit.
STRAY_TO = 37


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


def note_offsets(body):
    """Yield (offset_of_key_byte, channel) for every note-on and note-off."""
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
        event, channel = status & 0xF0, status & 0x0F
        if event in (0x80, 0x90):
            yield j, channel
            j += 2
        elif event in (0xA0, 0xB0, 0xE0):
            j += 2
        elif event in (0xC0, 0xD0):
            j += 1
        else:
            j += 1


def fix(path, dry_run=False, strays=False):
    data = bytearray(open(path, 'rb').read())
    moved = 0
    strayed = 0
    stranded = {}

    for at, length, tag in chunks(data):
        if tag != b'MTrk':
            continue
        body = data[at:at + length]
        for off, channel in note_offsets(body):
            if channel != DRUM_CHANNEL:
                continue
            key = body[off]
            if key == FROM:
                data[at + off] = TO
                moved += 1
            elif key < KIT_LO or key > KIT_HI:
                if strays and key < KIT_LO:
                    data[at + off] = STRAY_TO
                    strayed += 1
                else:
                    stranded[key] = stranded.get(key, 0) + 1

    if (moved or strayed) and not dry_run:
        open(path, 'wb').write(bytes(data))
    return moved, stranded, strayed


def main():
    args = sys.argv[1:]
    if '--selftest' in args:
        return selftest()
    paths = [a for a in args if not a.startswith('--')]
    if not paths:
        print(__doc__)
        return 2
    dry = '--dry-run' in args
    strays = '--strays' in args
    for p in paths:
        moved, stranded, strayed = fix(p, dry, strays)
        name = p.split('/')[-1]
        left = sum(stranded.values())
        note = ''
        if left:
            keys = ', '.join(str(k) for k in sorted(stranded))
            note = f'  STILL OUT OF RANGE: {left} notes on key(s) {keys}'
        extra = f'  +{strayed} stray(s) -> {STRAY_TO}' if strayed else ''
        print(f'  {"would remap" if dry else "remapped"} {moved:>4} '
              f'note(s) 35->36  {name}{extra}{note}')
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

        # 1. A channel-10 note 35 becomes 36.
        open(p, 'wb').write(build([b'\x99\x23\x64']))
        moved, stranded, _ = fix(p)
        fired = moved == 1 and open(p, 'rb').read().find(b'\x99\x24\x64') > 0
        print(f'  {"ok     " if fired else "*** FAILED ***"}  key 35 on ch10 remapped to 36')
        ok += fired

        # 2. Note 35 on a MELODIC channel is left alone - it is a real pitch there.
        open(p, 'wb').write(build([b'\x90\x23\x64']))
        moved, _, _ = fix(p)
        fired = moved == 0 and open(p, 'rb').read().find(b'\x90\x23\x64') > 0
        print(f'  {"ok     " if fired else "*** FAILED ***"}  key 35 on a melodic channel untouched')
        ok += fired

        # 3. Keys below 35 are reported, never moved - clamping them would turn
        #    a stick click into a bass drum.
        open(p, 'wb').write(build([b'\x99\x1F\x64', b'\x99\x1C\x64']))
        moved, stranded, _ = fix(p)
        fired = moved == 0 and sum(stranded.values()) == 2 and set(stranded) == {31, 28}
        print(f'  {"ok     " if fired else "*** FAILED ***"}  sub-35 keys reported, not moved (got {stranded})')
        ok += fired

        # 4. Note-OFF is remapped too, or the note never stops.
        open(p, 'wb').write(build([b'\x99\x23\x64', b'\x89\x23\x00']))
        moved, _, _ = fix(p)
        fired = moved == 2
        print(f'  {"ok     " if fired else "*** FAILED ***"}  note-off remapped as well as note-on (got {moved})')
        ok += fired

        # 5. --strays sends sub-35 keys to the side stick, not the bass drum.
        open(p, 'wb').write(build([b'\x99\x1F\x64']))
        moved, stranded, strayed = fix(p, strays=True)
        got = open(p, 'rb').read()
        fired = (strayed == 1 and not stranded
                 and got.find(bytes([0x99, STRAY_TO, 0x64])) > 0)
        print(f'  {"ok     " if fired else "*** FAILED ***"}  --strays maps sub-35 to {STRAY_TO}, not {KIT_LO}')
        ok += fired

    print(f'\n{ok}/5 selftest cases behave')
    return 0 if ok == 5 else 1


if __name__ == '__main__':
    sys.exit(main())
