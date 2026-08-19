"""Raise a quiet MIDI's level inside the file, when -V has run out of room.

WHEN THIS IS NEEDED. tune_song_volume.py fixes a quiet import by raising -V, and
that is always the first choice because it changes nothing about the sequence.
But _mvl is a u8 and vanilla never goes past 120, so a song that would need more
than that cannot be reached from midi.cfg at all. This is the second lever.

CC7 FIRST, VELOCITY ONLY IF THERE IS NO CHOICE, and the difference is not a
preference:

  * CC7 is a per-channel volume control. Scaling it up moves the whole channel
    and leaves every note's velocity - and therefore every dynamic the arranger
    wrote - exactly as it was. A song sitting at CC7 49 has 2.6x of free
    headroom that costs nothing musically.
  * Velocity IS the dynamics. Scaling it compresses them the moment anything
    clips at 127, and the notes that clip first are the loud ones, so the effect
    is to flatten the peaks and leave the quiet parts quiet.

So the tool scales CC7 by default and only touches velocity when asked, and it
reports how many notes clipped so that cost is a number rather than a surprise.

Measured on the songs this was written for: Snowpoint City sits at velocity 58
with CC7 49 - a CC7 problem. Mipha's Grace sits at velocity 21.6 with CC7
ALREADY 127 - no headroom at all, and the only honest options there are velocity
scaling with visible clipping, or leaving it alone.

Run:  python3 tools/rogue/lift_song_gain.py FILE --cc7 TARGET [--dry-run]
      python3 tools/rogue/lift_song_gain.py FILE --velocity FACTOR
      python3 tools/rogue/lift_song_gain.py --selftest
"""
import argparse
import struct
import sys

MAX = 127


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


def events(body):
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


def lift(path, cc7_target=None, vel_factor=None, dry_run=False):
    data = bytearray(open(path, 'rb').read())
    cc7_before = []
    cc7_after = 0
    vel_scaled = 0
    vel_clipped = 0

    # Pass 1: what is the loudest CC7 in the file? The scale factor comes from
    # that, not from the mean, so the relationship BETWEEN channels survives -
    # scaling each channel to the target independently would flatten the mix.
    for at, ln, tag in chunks(data):
        if tag != b'MTrk':
            continue
        body = data[at:at + ln]
        for off, status in events(body):
            if (status & 0xF0) == 0xB0 and body[off] == 7:
                cc7_before.append(body[off + 1])

    factor = None
    if cc7_target is not None and cc7_before:
        peak = max(cc7_before)
        factor = min(cc7_target / peak, MAX / peak) if peak else 1.0

    for at, ln, tag in chunks(data):
        if tag != b'MTrk':
            continue
        body = data[at:at + ln]
        for off, status in events(body):
            e = status & 0xF0
            if factor is not None and e == 0xB0 and body[off] == 7:
                v = min(MAX, int(round(body[off + 1] * factor)))
                data[at + off + 1] = v
                cc7_after += 1
            elif vel_factor is not None and e == 0x90 and body[off + 1] > 0:
                v = int(round(body[off + 1] * vel_factor))
                if v > MAX:
                    vel_clipped += 1
                    v = MAX
                data[at + off + 1] = v
                vel_scaled += 1

    if not dry_run and (cc7_after or vel_scaled):
        open(path, 'wb').write(bytes(data))
    return {
        "cc7_peak": max(cc7_before) if cc7_before else None,
        "cc7_factor": factor, "cc7_events": cc7_after,
        "vel_scaled": vel_scaled, "vel_clipped": vel_clipped,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--cc7", type=int, help="scale CC7 so its PEAK reaches this")
    ap.add_argument("--velocity", type=float, help="multiply note velocity")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.files or (args.cc7 is None and args.velocity is None):
        print(__doc__)
        return 2
    for f in args.files:
        r = lift(f, args.cc7, args.velocity, args.dry_run)
        name = f.split('/')[-1]
        bits = []
        if r["cc7_events"]:
            bits.append(f'CC7 peak {r["cc7_peak"]} x{r["cc7_factor"]:.2f} '
                        f'over {r["cc7_events"]} events')
        if r["vel_scaled"]:
            bits.append(f'{r["vel_scaled"]} velocities scaled, '
                        f'{r["vel_clipped"]} CLIPPED at {MAX}')
        print(f'  {"would lift" if args.dry_run else "lifted"}  {name:<38} '
              + '; '.join(bits or ["nothing to do"]))
    return 0


def selftest():
    import tempfile
    import os

    def build(cc_values, vels):
        track = b''
        for c in cc_values:
            track += b'\x00\xB0\x07' + bytes([c])
        for v in vels:
            track += b'\x00\x90\x40' + bytes([v])
        track += b'\x00\xFF\x2F\x00'
        return (b'MThd' + struct.pack('>I', 6) + b'\x00\x00\x00\x01\x00\x60'
                + b'MTrk' + struct.pack('>I', len(track)) + track)

    ok = 0
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, 't.mid')

        # 1. CC7 peak reaches the target.
        open(p, 'wb').write(build([50, 25], [64]))
        r = lift(p, cc7_target=100)
        d = open(p, 'rb').read()
        fired = b'\xB0\x07\x64' in d and r["cc7_events"] == 2
        print(f'  {"ok     " if fired else "*** FAILED ***"}  CC7 peak lifted to the target')
        ok += fired

        # 2. The RATIO between channels survives - 50:25 must stay 2:1, or the
        #    mix is flattened rather than raised.
        i = d.find(b'\xB0\x07')
        first, second = d[i + 2], d[d.find(b'\xB0\x07', i + 2) + 2]
        fired = abs(first / second - 2.0) < 0.1
        print(f'  {"ok     " if fired else "*** FAILED ***"}  channel balance preserved ({first}:{second})')
        ok += fired

        # 3. CC7 never exceeds 127 even with an absurd target.
        open(p, 'wb').write(build([120], [64]))
        lift(p, cc7_target=250)
        d3 = open(p, 'rb').read()
        # The CC7 VALUE specifically, not the tail of the file - the first
        # version of this case read the last eight bytes and was asserting
        # something about the end-of-track marker.
        fired = d3[d3.find(bytes([0xB0, 0x07])) + 2] == 127
        print(f'  {"ok     " if fired else "*** FAILED ***"}  CC7 clamped at 127')
        ok += fired

        # 4. Velocity clipping is COUNTED, not hidden.
        open(p, 'wb').write(build([100], [100, 40]))
        r = lift(p, vel_factor=2.0)
        fired = r["vel_scaled"] == 2 and r["vel_clipped"] == 1
        print(f'  {"ok     " if fired else "*** FAILED ***"}  clipped velocities reported (got {r["vel_clipped"]})')
        ok += fired

    print(f'\n{ok}/4 selftest cases behave')
    return 0 if ok == 4 else 1


if __name__ == "__main__":
    sys.exit(main())
