"""Move a MIDI's tempo events onto track 0, where mid2agb will actually read them.

THE FAILURE, and it is silent in the worst way. mid2agb takes its tempo from the
CONDUCTOR TRACK - track 0 of a format-1 file - and ignores tempo meta events
anywhere else. A sequencer that writes them onto the first *music* track instead
produces a perfectly valid MIDI that every desktop player renders correctly, and
mid2agb emits no TEMPO command at all. The song then plays at m4a's default
tempo.

It does not fail to convert. It does not warn. `1 of 1 convert cleanly` is what
you get, and the song is simply slow.

FOUND ON HGSS CHAMPION LANCE, whose three 184 BPM events all sit at tick 0 of
TRACK 1 while track 0 is a 12-byte stub. Its .s had no TEMPO line; the known-good
HGSS gym theme's had `TEMPO , 189`. Comparing the two assemblies is what
identified it - the MIDI header, the tempo values and the note data all looked
completely normal.

WHAT IT DOES. Collects tempo events from every track, merges them into track 0 at
their correct absolute ticks, and rewrites that track. It does NOT delete them
from where they were: mid2agb ignores them there, desktop players use the first
one they meet, and removing them is a change with no upside.

IT REFUSES IF TRACK 0 ALREADY HAS TEMPO EVENTS, because then the conductor track
is already doing its job and anything else in the file is a deliberate
per-track override that this would flatten.

Run:  python3 tools/rogue/fix_midi_tempo_track.py <file.mid> [...] [--dry-run]
      python3 tools/rogue/fix_midi_tempo_track.py --check <file.mid> [...]
      python3 tools/rogue/fix_midi_tempo_track.py --selftest
"""
import struct
import sys


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
    """Yield (absolute_tick, raw_event_bytes) for one track body."""
    j = 0
    tick = 0
    status = 0
    while j < len(body):
        start = j
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
        del start


def tempo_map(data):
    """[(track, tick, raw_bytes)] for every tempo meta event in the file."""
    found = []
    track = -1
    for _, body_at, length, tag in chunks(data):
        if tag != b'MTrk':
            continue
        track += 1
        for tick, raw in events(data[body_at:body_at + length]):
            if len(raw) >= 3 and raw[0] == 0xFF and raw[1] == 0x51:
                found.append((track, tick, raw))
    return found


def fix(path, dry_run=False):
    data = open(path, 'rb').read()
    tempos = tempo_map(data)
    if not tempos:
        return 0, 'no tempo events anywhere; nothing to move'
    if any(t == 0 for t, _, _ in tempos):
        return 0, None                      # already correct
    stray = [(tick, raw) for _, tick, raw in tempos]

    tracks = [(at, ln) for _, at, ln, tag in chunks(data) if tag == b'MTrk']
    if not tracks:
        return 0, 'no MTrk chunks'
    at0, len0 = tracks[0]

    merged = [(tick, raw) for tick, raw in events(data[at0:at0 + len0])]
    # End-of-track must stay last, so pull it off, merge, put it back.
    tail = [e for e in merged if len(e[1]) >= 2 and e[1][0] == 0xFF and e[1][1] == 0x2F]
    merged = [e for e in merged if e not in tail]
    merged += stray
    merged.sort(key=lambda e: e[0])
    if tail:
        merged.append((max(e[0] for e in merged) if merged else 0, tail[-1][1]))

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
    return len(stray), None


def main():
    args = sys.argv[1:]
    if '--selftest' in args:
        return selftest()
    paths = [a for a in args if not a.startswith('--')]
    if not paths:
        print(__doc__)
        return 2
    dry = '--dry-run' in args or '--check' in args
    rc = 0
    for p in paths:
        tempos = tempo_map(open(p, 'rb').read())
        on0 = [t for t, _, _ in tempos if t == 0]
        name = p.split('/')[-1]
        if not tempos:
            print(f'  no tempo   {name}')
            continue
        if on0:
            print(f'  ok         {name}: tempo already on track 0')
            continue
        if '--check' in args:
            print(f'  WOULD FIX  {name}: {len(tempos)} tempo event(s) on '
                  f'track(s) {sorted(set(t for t, _, _ in tempos))}, not 0 - '
                  f'mid2agb will emit NO tempo and the song will play slow')
            rc = 1
            continue
        moved, err = fix(p, dry)
        print(f'  {"would move" if dry else "moved"} {moved} tempo event(s) to '
              f'track 0  {name}' + (f'  [{err}]' if err else ''))
    return rc


def selftest():
    import tempfile
    import os

    def build(tracks, division=96):
        out = b'MThd' + struct.pack('>I', 6) + struct.pack('>HHH', 1, len(tracks), division)
        for evs in tracks:
            body = b''.join(varlen(d) + e for d, e in evs) + b'\x00\xFF\x2F\x00'
            out += b'MTrk' + struct.pack('>I', len(body)) + body
        return out

    TEMPO = b'\xFF\x51\x03\x05\x16\x15'      # ~184 BPM
    ok = 0
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, 't.mid')

        # 1. Tempo stranded on track 1 is moved to track 0.
        open(p, 'wb').write(build([[], [(0, TEMPO)]]))
        moved, err = fix(p)
        after = tempo_map(open(p, 'rb').read())
        fired = moved == 1 and any(t == 0 for t, _, _ in after)
        print(f'  {"ok     " if fired else "*** FAILED ***"}  stray tempo moved to track 0')
        ok += fired

        # 2. Tempo already on track 0 is left completely alone.
        before = build([[(0, TEMPO)], []])
        open(p, 'wb').write(before)
        moved, err = fix(p)
        fired = moved == 0 and open(p, 'rb').read() == before
        print(f'  {"ok     " if fired else "*** FAILED ***"}  correct file untouched')
        ok += fired

        # 3. The moved event keeps its tick, not just its existence.
        open(p, 'wb').write(build([[], [(480, TEMPO)]]))
        fix(p)
        after = tempo_map(open(p, 'rb').read())
        on0 = [(t, tick) for t, tick, _ in after if t == 0]
        fired = on0 and on0[0][1] == 480
        print(f'  {"ok     " if fired else "*** FAILED ***"}  tick preserved through the move'
              f' (got {on0})')
        ok += fired

        # 4. The file still parses as MIDI afterwards - a wrong length field
        #    would corrupt every later chunk and is invisible to case 1.
        open(p, 'wb').write(build([[], [(0, TEMPO)], [(0, b'\x90\x40\x64')]]))
        fix(p)
        n = len([1 for _, _, _, tag in chunks(open(p, 'rb').read()) if tag == b'MTrk'])
        fired = n == 3
        print(f'  {"ok     " if fired else "*** FAILED ***"}  all 3 tracks still parse (got {n})')
        ok += fired

    print(f'\n{ok}/4 selftest cases behave')
    return 0 if ok == 4 else 1


if __name__ == '__main__':
    sys.exit(main())
