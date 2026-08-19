"""Give a MIDI the loop markers mid2agb needs, so the song repeats instead of stopping.

THE FAILURE. mid2agb emits a GOTO - an actual loop - only when the MIDI carries
text meta events reading exactly `[` (loop begin) and `]` (loop end). See
tools/mid2agb/midi.cpp, which maps those three strings and `:` onto block
events. A MIDI without them converts perfectly cleanly and produces a song that
plays once and stops.

Nothing reports it. `1 of 1 convert cleanly` is what the importer says, the song
is the right length, the right speed and the right instruments, and it simply
does not come back round. Direct cart-dump rips have this shape because the loop
lives in the SDAT sequence, not in the MIDI events; the GBA Music Pack's curated
rips carry the markers, which is why nothing imported from it ever showed this.

Measured, to make the difference concrete: every working BGM in this ROM emits
one GOTO per track - mus_petalburg_woods 9, mus_hgss_battle_gym_johto 14 - and
every hand-imported cart dump emitted ZERO.

MARKERS GO ON TRACK 0 AND ONLY TRACK 0. That is not a simplification: vanilla's
own mus_petalburg_woods is a 10-track format-1 file with the pair on track 0
alone, and mid2agb propagates the loop to all ten. Putting them on every track
would be wrong, not merely redundant.

WHERE THE LOOP POINT COMES FROM MATTERS MORE THAN THIS TOOL. `[` is rarely tick
0 - the pack's rips put it after a 1 to 10 bar intro, which is the part you do
NOT want repeating. This tool takes the ticks; deciding them is a separate
question, and lifting them from an older rip of the same song is far better than
guessing. Convert via BEATS (tick / division), never raw ticks, because two rips
of one song routinely use different divisions.

NEVER LIFT A LOOP POINT ACROSS RIPS OF DIFFERENT LENGTHS. This cost a
play-tested bug. DPPt's champion battle was lifted from a pack rip 8 bars longer
than the new one, on the assumption that a length difference is padding spread
through the song. It is not: the difference was an INTRO the pack rip had and
the new one did not, so the 10-bar loop point landed at bar 10 of 66 and the
song skipped eight bars of real music on every repeat. It converted cleanly,
looped, and sounded wrong - and only sounded wrong after the first pass, which
is why nothing caught it.

The rule that follows: if the two rips do not agree on length to within a few
percent, the loop point is NOT transferable. Use a whole-song loop, which is
never wrong in this way, and let a listen decide whether an intro is worth
skipping. --check-source below enforces it.

Run:  python3 tools/rogue/set_midi_loop.py FILE --begin TICK --end TICK
      python3 tools/rogue/set_midi_loop.py FILE --check
      python3 tools/rogue/set_midi_loop.py --selftest
"""
import argparse
import struct
import sys

LOOP_BEGIN = b'\xFF\x06\x01['
LOOP_END = b'\xFF\x06\x01]'
MARKER_TEXTS = ('[', ']', '][', ':')


def varlen(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.insert(0, (value & 0x7F) | 0x80)
        value >>= 7
    return bytes(out)


def chunks(data):
    i = 0
    while i < len(data) - 8:
        tag = data[i:i + 4]
        if tag not in (b'MThd', b'MTrk'):
            i += 1
            continue
        length = struct.unpack('>I', data[i + 4:i + 8])[0]
        yield i, i + 8, length, tag
        i += 8 + length


def events(body):
    j = 0
    tick = 0
    status = 0
    while j < len(body):
        delta = 0
        while j < len(body) and body[j] & 0x80:
            delta = (delta | (body[j] & 0x7F)) << 7
            j += 1
        if j >= len(body):
            return
        delta |= body[j]
        j += 1
        tick += delta
        head = j
        b = body[j]
        if b == 0xFF:
            j += 2
            length = 0
            while body[j] & 0x80:
                length = (length | (body[j] & 0x7F)) << 7
                j += 1
            length |= body[j]
            j += 1 + length
        elif b in (0xF0, 0xF7):
            j += 1
            length = 0
            while body[j] & 0x80:
                length = (length | (body[j] & 0x7F)) << 7
                j += 1
            length |= body[j]
            j += 1 + length
        else:
            if b & 0x80:
                status = b
                j += 1
            event = status & 0xF0
            j += 1 if event in (0xC0, 0xD0) else 2
        yield tick, body[head:j]


def has_markers(data):
    found = []
    track = -1
    for _, at, length, tag in chunks(data):
        if tag != b'MTrk':
            continue
        track += 1
        for tick, raw in events(data[at:at + length]):
            if len(raw) >= 3 and raw[0] == 0xFF and 1 <= raw[1] <= 7:
                text = raw[3:3 + raw[2]].decode('ascii', 'ignore')
                if text in MARKER_TEXTS:
                    found.append((track, tick, text))
    return found


def set_loop(path, begin, end, dry_run=False):
    data = open(path, 'rb').read()
    if has_markers(data):
        return None, 'already has loop markers; refusing to add a second pair'

    tracks = [(at, ln) for _, at, ln, tag in chunks(data) if tag == b'MTrk']
    if not tracks:
        return None, 'no MTrk chunks'
    at0, len0 = tracks[0]

    merged = list(events(data[at0:at0 + len0]))
    # End-of-track must remain the last event, so it is pulled out, the markers
    # are merged in, and it goes back on at the new maximum tick. A loop end
    # placed after it would be unreachable and silently do nothing.
    eot = [e for e in merged if len(e[1]) >= 2 and e[1][0] == 0xFF and e[1][1] == 0x2F]
    merged = [e for e in merged if e not in eot]
    merged.append((begin, LOOP_BEGIN))
    merged.append((end, LOOP_END))
    merged.sort(key=lambda e: e[0])
    if eot:
        merged.append((max(e[0] for e in merged), eot[-1][1]))

    body = bytearray()
    last = 0
    for tick, raw in merged:
        body += varlen(tick - last) + raw
        last = tick

    out = bytearray(data)
    out[at0 - 4:at0] = struct.pack('>I', len(body))
    out[at0:at0 + len0] = body
    if not dry_run:
        open(path, 'wb').write(bytes(out))
    return (begin, end), None


def song_length(path):
    """(division, last tick) - for comparing two rips in beats."""
    data = open(path, 'rb').read()
    div = struct.unpack('>H', data[12:14])[0]
    last = 0
    for _, at, ln, tag in chunks(data):
        if tag != b'MTrk':
            continue
        for tick, _ in events(data[at:at + ln]):
            last = max(last, tick)
    return div, last


def transferable(new_path, source_path, tolerance=0.03):
    """May a loop point be lifted from source to new?

    Only when the two rips agree on length in BEATS. A mismatch means one of
    them has material the other does not - almost always an intro - and a tick
    or beat offset taken from one lands somewhere arbitrary in the other.
    """
    nd, nl = song_length(new_path)
    sd, sl = song_length(source_path)
    nb, sb = nl / nd if nd else 0, sl / sd if sd else 0
    if not sb:
        return False, nb, sb
    return abs(nb - sb) / sb <= tolerance, nb, sb


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='*')
    ap.add_argument('--begin', type=int)
    ap.add_argument('--end', type=int)
    ap.add_argument('--check-source', metavar='MIDI',
                    help='refuse a non-zero --begin unless this rip is the '
                         'same length, in beats, as the file being written')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if not args.files:
        print(__doc__)
        return 2

    rc = 0
    for path in args.files:
        name = path.split('/')[-1]
        found = has_markers(open(path, 'rb').read())
        if args.check:
            if found:
                pretty = ', '.join(f'{t}@{tick}' for _, tick, t in found)
                print(f'  loops      {name}: {pretty}')
            else:
                print(f'  NO LOOP    {name}: mid2agb will emit no GOTO; the song '
                      f'will play once and stop')
                rc = 1
            continue
        if args.begin is None or args.end is None:
            print('--begin and --end are required unless --check')
            return 2
        if args.check_source and args.begin != 0:
            okay, nb, sb = transferable(path, args.check_source)
            if not okay:
                print(f'  REFUSED    {name}: source is {sb:.1f} beats and this '
                      f'is {nb:.1f} - a loop point is not transferable across '
                      f'rips of different lengths (the difference is an intro, '
                      f'not padding). Use --begin 0.')
                rc = 1
                continue
        res, err = set_loop(path, args.begin, args.end, args.dry_run)
        if err:
            print(f'  skipped    {name}: {err}')
        else:
            print(f'  {"would set" if args.dry_run else "set"} loop '
                  f'[@{res[0]} ]@{res[1]}  {name}')
    return rc


def selftest():
    import tempfile
    import os

    def build(n_tracks=2, events_per=3, division=48):
        out = b'MThd' + struct.pack('>I', 6) + struct.pack('>HHH', 1, n_tracks, division)
        for _ in range(n_tracks):
            body = b''
            for k in range(events_per):
                body += varlen(96) + b'\x90\x40\x64'
            body += b'\x00\xFF\x2F\x00'
            out += b'MTrk' + struct.pack('>I', len(body)) + body
        return out

    ok = 0
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, 't.mid')

        # 1. Markers land, on track 0, at the requested ticks.
        open(p, 'wb').write(build())
        set_loop(p, 96, 288)
        m = has_markers(open(p, 'rb').read())
        fired = sorted(m) == sorted([(0, 96, '['), (0, 288, ']')])
        print(f'  {"ok     " if fired else "*** FAILED ***"}  markers land on track 0 at the right ticks (got {m})')
        ok += fired

        # 2. The file still parses: every track survives the length rewrite.
        n = len([1 for _, _, _, tag in chunks(open(p, 'rb').read()) if tag == b'MTrk'])
        fired = n == 2
        print(f'  {"ok     " if fired else "*** FAILED ***"}  both tracks still parse (got {n})')
        ok += fired

        # 3. Running it twice does NOT stack a second pair.
        res, err = set_loop(p, 96, 288)
        fired = res is None and bool(err)
        print(f'  {"ok     " if fired else "*** FAILED ***"}  refuses to double-add')
        ok += fired

        # 4. End-of-track stays last, or the loop end is unreachable.
        open(p, 'wb').write(build())
        set_loop(p, 96, 288)
        d = open(p, 'rb').read()
        at = [a for _, a, _, tag in chunks(d) if tag == b'MTrk'][0]
        ln = [l for _, _, l, tag in chunks(d) if tag == b'MTrk'][0]
        evs = list(events(d[at:at + ln]))
        fired = evs[-1][1][:2] == b'\xFF\x2F'
        print(f'  {"ok     " if fired else "*** FAILED ***"}  end-of-track remains last')
        ok += fired

    print(f'\n{ok}/4 selftest cases behave')
    return 0 if ok == 4 else 1


if __name__ == '__main__':
    sys.exit(main())
