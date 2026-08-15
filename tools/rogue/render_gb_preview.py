"""Render a PSG voicegroup arrangement to WAV, so it can be judged by ear
without a ROM and an emulator.

WHAT THIS IS AND IS NOT. It is a preview synthesiser: it reads the MIDI, applies
the same slot -> voice mapping make_gb_voicegroup.py writes, allocates the four
PSG channels the way m4a does (by voice TYPE -- every square_1 voice wants
hardware channel 1), and renders square/wave/noise. It is NOT a capture of the
ROM. Envelopes are musical approximations rather than cycle-accurate GB envelope
steps, and it does not model m4a's priority stealing, tempo curves or vibrato.

It is accurate about the thing worth judging: WHICH PARTS SURVIVE, what the
melody sounds like carrying alone on square 1, and whether the reduction still
reads as the original tune.

Emits two files on purpose:
  *_psg.wav    the 4-channel reduction, i.e. what the Sound toggle plays
  *_full.wav   ALL parts through the same synth

The second exists so the comparison isolates the ARRANGEMENT decision from the
timbre change. Comparing the PSG render against the real sampled track would
conflate "we dropped four parts" with "it is a square wave now", and those are
different questions.

Usage:  python3 tools/rogue/render_gb_preview.py --repo PATH --out DIR
"""
import argparse
import math
import struct
import sys
import wave
from pathlib import Path

RATE = 44100
MIDI_REL = 'sound/songs/midi/mus_petalburg_woods.mid'
WAVE_REL = 'sound/programmable_wave_samples/06.pcm'

# Voice assignment, mirroring MAPPING in make_gb_voicegroup.py. Keyed by the
# voicegroup slot the MIDI track selects.
#   kind: square1 | square2 | wave | noise | drop
#   duty/attack/decay/sustain/release read off the voicegroup entries.
VOICES = {
    73: dict(kind='square1', duty=2, a=0, d=0,  s=15, r=1,  name='flute -> MELODY'),
    80: dict(kind='square2', duty=0, a=0, d=1,  s=7,  r=1,  name='harmony'),
    81: dict(kind='wave',    duty=0, a=0, d=7,  s=15, r=2,  name='BASS'),
    127: dict(kind='noise',  duty=0, a=0, d=1,  s=0,  r=3,  name='percussion'),
    0:  dict(kind='noise',   duty=0, a=0, d=1,  s=0,  r=1,  name='drums'),
    1:  dict(kind='drop',    duty=0, a=0, d=0,  s=0,  r=0,  name='piano (dropped)'),
    45: dict(kind='drop',    duty=0, a=0, d=0,  s=0,  r=0,  name='pizzicato (dropped)'),
    48: dict(kind='drop',    duty=0, a=0, d=0,  s=0,  r=0,  name='strings (dropped)'),
    82: dict(kind='drop',    duty=0, a=0, d=0,  s=0,  r=0,  name='counter (dropped)'),
}
# For the *_full.wav render, the dropped parts need something audible.
FULL_SUBSTITUTE = {1: 'square2', 45: 'square1', 48: 'wave', 82: 'square1'}

DUTY = {0: 0.125, 1: 0.25, 2: 0.5, 3: 0.75}


def read_varlen(b, i):
    v = 0
    while True:
        c = b[i]; i += 1
        v = (v << 7) | (c & 0x7F)
        if not c & 0x80:
            return v, i


def parse_midi(path):
    """-> (tracks, tempo_map). Each track is (slot, [(on_s, off_s, pitch)])."""
    b = path.read_bytes()
    if b[:4] != b'MThd':
        sys.exit('%s is not a MIDI file' % path)
    _, _fmt, ntrks, div = struct.unpack('>IHHH', b[4:14])

    # First pass for tempo (ticks -> seconds).
    tempo_events = []
    i = 14
    chunks = []
    for _ in range(ntrks):
        if b[i:i + 4] != b'MTrk':
            break
        (ln,) = struct.unpack('>I', b[i + 4:i + 8])
        chunks.append((i + 8, i + 8 + ln))
        i += 8 + ln

    for start, end in chunks:
        j, status, now = start, 0, 0
        while j < end:
            dt, j = read_varlen(b, j)
            now += dt
            if b[j] & 0x80:
                status = b[j]; j += 1
            if status == 0xFF:
                meta = b[j]; j += 1
                ln, j = read_varlen(b, j)
                if meta == 0x51:
                    tempo_events.append((now, (b[j] << 16) | (b[j+1] << 8) | b[j+2]))
                j += ln
            elif status in (0xF0, 0xF7):
                ln, j = read_varlen(b, j); j += ln
            elif (status & 0xF0) in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                j += 2
            else:
                j += 1
    tempo_events.sort()
    if not tempo_events:
        tempo_events = [(0, 500000)]

    def tick_to_sec(tick):
        sec, last_tick, upq = 0.0, 0, tempo_events[0][1]
        for t, u in tempo_events:
            if t >= tick:
                break
            sec += (t - last_tick) / div * (upq / 1e6)
            last_tick, upq = t, u
        return sec + (tick - last_tick) / div * (upq / 1e6)

    tracks = []
    for start, end in chunks:
        j, status, now = start, 0, 0
        slot, notes, held = None, [], {}
        while j < end:
            dt, j = read_varlen(b, j)
            now += dt
            if b[j] & 0x80:
                status = b[j]; j += 1
            ev = status & 0xF0
            if ev == 0x90 and b[j + 1] != 0:
                held[b[j]] = now
                j += 2
            elif ev == 0x80 or (ev == 0x90 and b[j + 1] == 0):
                st = held.pop(b[j], None)
                if st is not None:
                    notes.append((tick_to_sec(st), tick_to_sec(now), b[j]))
                j += 2
            elif ev == 0xC0:
                if slot is None:
                    slot = b[j]
                j += 1
            elif ev == 0xD0:
                j += 1
            elif ev in (0xA0, 0xB0, 0xE0):
                j += 2
            elif status == 0xFF:
                j += 1
                ln, j = read_varlen(b, j); j += ln
            elif status in (0xF0, 0xF7):
                ln, j = read_varlen(b, j); j += ln
            else:
                j += 2
        if notes:
            tracks.append((slot if slot is not None else -1, notes))
    return tracks


def read_wave_table(path):
    raw = path.read_bytes()
    nib = []
    for byte in raw:
        nib.append(byte >> 4)
        nib.append(byte & 0xF)
    return [(v / 7.5) - 1.0 for v in nib]


def envelope(t, dur, v):
    """Crude AD-S-R. Musical, not cycle-accurate."""
    atk = (v['a'] / 15.0) * 0.04
    dec = (v['d'] / 15.0) * 0.45
    sus = v['s'] / 15.0
    rel = max((v['r'] / 15.0) * 0.35, 0.01)

    if t < 0:
        return 0.0
    if t < atk:
        return t / atk if atk > 0 else 1.0
    if t < dur:
        if dec > 0 and t - atk < dec:
            k = (t - atk) / dec
            return 1.0 + (sus - 1.0) * k
        return sus
    k = (t - dur) / rel
    return max(0.0, sus * (1.0 - k)) if k < 1.0 else 0.0


def render(tracks, wave_table, total, psg_only):
    buf = [0.0] * int(total * RATE + RATE)

    for slot, notes in tracks:
        v = VOICES.get(slot)
        if v is None:
            continue
        kind = v['kind']
        if kind == 'drop':
            if psg_only:
                continue
            kind = FULL_SUBSTITUTE.get(slot, 'square1')

        lfsr = 0x7FFF
        for (on, off, pitch) in notes:
            freq = 440.0 * (2.0 ** ((pitch - 69) / 12.0))
            dur = off - on
            tail = (v['r'] / 15.0) * 0.35 + 0.02
            i0 = int(on * RATE)
            n = int((dur + tail) * RATE)
            phase = 0.0
            step = freq / RATE
            for k in range(n):
                idx = i0 + k
                if idx >= len(buf):
                    break
                t = k / RATE
                env = envelope(t, dur, v)
                if env <= 0.0:
                    continue
                phase += step
                p = phase % 1.0
                if kind in ('square1', 'square2'):
                    smp = 1.0 if p < DUTY[v['duty']] else -1.0
                elif kind == 'wave':
                    smp = wave_table[int(p * len(wave_table)) % len(wave_table)]
                else:  # noise
                    if k % 24 == 0:
                        bit = ((lfsr ^ (lfsr >> 1)) & 1)
                        lfsr = (lfsr >> 1) | (bit << 14)
                    smp = 1.0 if (lfsr & 1) else -1.0
                buf[idx] += env * smp * 0.16
    return buf


def write_wav(path, buf):
    peak = max(1e-9, max(abs(s) for s in buf))
    norm = 0.89 / peak if peak > 0.89 else 1.0
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(
            struct.pack('<h', int(max(-1.0, min(1.0, s * norm)) * 32000))
            for s in buf))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    ap.add_argument('--out', required=True, type=Path)
    args = ap.parse_args()

    tracks = parse_midi(args.repo / MIDI_REL)
    wave_table = read_wave_table(args.repo / WAVE_REL)
    total = max(off for _, notes in tracks for (_, off, _) in notes)

    print('parsed %d note-carrying tracks, %.1f s' % (len(tracks), total))
    for slot, notes in tracks:
        v = VOICES.get(slot)
        label = v['name'] if v else 'unmapped'
        kind = v['kind'] if v else '?'
        print('  slot %3d  %-22s %-8s %3d notes' % (slot, label, kind, len(notes)))

    args.out.mkdir(parents=True, exist_ok=True)
    for tag, psg in (('psg', True), ('full', False)):
        buf = render(tracks, wave_table, total, psg)
        p = args.out / ('woods_%s.wav' % tag)
        write_wav(p, buf)
        print('wrote %s' % p)
    return 0


if __name__ == '__main__':
    sys.exit(main())
