"""Transcribe a MIDI into real GBS song data -- the pokecrystal sequencer format.

WHAT THIS IS NOT. Everything shipped so far as "GB Sounds" is m4a: the original
Emerald song rebuilt against a voicegroup holding only voice_square_1/2,
voice_programmable_wave and voice_noise, assembled by mid2agb, sequenced by m4a,
reaching hardware through CgbSound writing REG_NR*. Not one of those thirty-six
songs contains a gbs_switch opcode; none of them ever enters GBSMain. They are
PSG timbre on an m4a sequencer, and the ceiling on that is real -- a
voice_square_1 is a fixed duty with an ADSR envelope and nothing else, so duty
cycle patterns, hardware pitch sweep and noise-register drums are simply
unavailable.

This emits the other thing: a .s file of GBS commands that GBSTrack_Update
walks, on the four CGB channels, with those effects available.

THE FORMAT, as this repo's engine actually implements it (src/gbs.c), because
several details differ from what the macros suggest:

  note byte      high nibble = pitch 1..12 (C_..B_), 0 = rest;
                 low nibble = length-1, so 1..16 UNITS, never more.

  duration       CalculateNoteLength = noteUnitLength * length * tempo, a 16.8
                 fixed point value: high byte is frames, low byte carries into
                 the next note. The carry is why a unit that does not divide
                 the frame rate exactly still does not drift.

  octave n       assembles to 0xd0 + 8 - n and the engine takes `& 7`, so the
                 shift is n-1 and octave 1 is C2 (65.4 Hz), the lowest note the
                 hardware can express at all. GBS octave = MIDI octave - 1.

  note_type      reads TWO bytes on channels 1-3 and ONE on channel 4. The
                 macro makes the envelope optional, so `note_type 6` on a
                 melodic channel assembles happily and then desynchronises the
                 whole track by one byte. Always emitted with an envelope here.

  channel 3      the envelope nibble is the WAVE SAMPLE id, not a fade, and the
                 velocity nibble is NR32's output level, so it is 0..3 and not
                 0..15 like the square channels.

  channel 4      duty_cycle selects the DRUM KIT and a note's pitch nibble
                 selects the instrument within it.

Usage:
  python3 tools/rogue/midi_to_gbs.py --repo . --song mus_petalburg_woods \\
      --out sound/songs/gbs/gbs_petalburg_woods.s
"""
import argparse
import struct
import sys
from pathlib import Path

FRAME_HZ = 59.7275          # GBA VBlank, what noteLength1 counts down in
PITCH_NAMES = ['C_', 'Cs', 'D_', 'Ds', 'E_', 'F_', 'Fs', 'G_', 'Gs', 'A_',
               'As', 'B_']
MAX_LEN = 16                # a length nibble holds 1..16 units
GBS_MIN_KEY = 36            # C2. Octave 1 is the bottom of the hardware range.
GBS_MAX_KEY = 36 + 12 * 8 - 1


# ---------------------------------------------------------------- midi reading

def _varlen(b, i):
    v = 0
    while True:
        c = b[i]
        i += 1
        v = (v << 7) | (c & 0x7F)
        if not c & 0x80:
            return v, i


def read_midi(path):
    """-> (division, usec_per_quarter, [track]) where track is [(on, off, key)].

    Ticks, not seconds: the whole transcription is a rhythm problem and every
    rounding done in seconds is a rounding that cannot be checked against the
    bar line afterwards.
    """
    b = path.read_bytes()
    if b[:4] != b'MThd':
        sys.exit('%s is not a MIDI file' % path)
    _, _fmt, ntrks, div = struct.unpack('>IHHH', b[4:14])
    i, tracks, tempo = 14, [], 500000
    for _ in range(ntrks):
        if b[i:i + 4] != b'MTrk':
            break
        (ln,) = struct.unpack('>I', b[i + 4:i + 8])
        j, end, status, now = i + 8, i + 8 + ln, 0, 0
        held, notes = {}, []
        while j < end:
            dt, j = _varlen(b, j)
            now += dt
            if b[j] & 0x80:
                status = b[j]
                j += 1
            ev = status & 0xF0
            if ev == 0x90 and b[j + 1]:
                held.setdefault(b[j], []).append(now)
                j += 2
            elif ev in (0x80, 0x90):
                if held.get(b[j]):
                    notes.append((held[b[j]].pop(0), now, b[j]))
                j += 2
            elif ev in (0xA0, 0xB0, 0xE0):
                j += 2
            elif ev in (0xC0, 0xD0):
                j += 1
            elif status in (0xFF, 0xF0, 0xF7):
                mt = b[j] if status == 0xFF else None
                if status == 0xFF:
                    j += 1
                ln2, j = _varlen(b, j)
                if mt == 0x51:
                    tempo = (b[j] << 16) | (b[j + 1] << 8) | b[j + 2]
                j += ln2
            else:
                j += 2
        if notes:
            tracks.append(sorted(notes))
        i = end
    return div, tempo, tracks


# ------------------------------------------------------------------- rhythm

def choose_tempo(div, usec_per_quarter, grid_ticks):
    """-> (gbs_tempo, unit_len, error) for one grid step = one length unit.

    The engine has two knobs and they multiply: a per-channel noteUnitLength
    and a global tempo, giving unit_frames = unit_len * tempo / 256. Anything
    that hits the target is as good as anything else numerically, so prefer a
    small unit_len -- it leaves headroom to LENGTHEN a channel's unit later for
    a part with long notes, which is the only way to hold a note past 16 units
    without re-articulating it.
    """
    sec = grid_ticks / div * (usec_per_quarter / 1e6)
    want = sec * FRAME_HZ
    best = None
    for unit in range(1, 65):
        tempo = round(want * 256 / unit)
        if not 1 <= tempo <= 0xFFFF:
            continue
        err = abs(unit * tempo / 256 - want) / want
        if best is None or err < best[2] - 1e-12:
            best = (tempo, unit, err)
    return best


def quantize(notes, grid, end_tick):
    """-> [(start_unit, length_units, key)] on the grid, monophonic, gaps left.

    A part is snapped to the grid rather than played at its recorded length
    because a GBS length is an integer count of units and nothing else. Where
    two notes overlap after snapping, the earlier one is cut: these channels
    are monophonic in hardware, so an overlap is not a chord, it is the second
    note stealing the channel.
    """
    out = []
    for (on, off, key) in notes:
        s = int(round(on / grid))
        e = int(round(off / grid))
        if e <= s:
            e = s + 1
        out.append([s, e, key])
    out.sort()
    for i in range(len(out) - 1):
        if out[i][1] > out[i + 1][0]:
            out[i][1] = out[i + 1][0]
    return [(s, e - s, k) for (s, e, k) in out if e > s]


def to_stream(events, end_unit):
    """-> [(key or None, units)] covering 0..end_unit with rests in the gaps."""
    stream, at = [], 0
    for (s, n, k) in events:
        if s > at:
            stream.append((None, s - at))
        stream.append((k, n))
        at = s + n
    if end_unit > at:
        stream.append((None, end_unit - at))
    return stream


def split_runs(stream, scale):
    """Re-express a stream in units of `scale`, splitting past MAX_LEN.

    A rest longer than the cap simply becomes several rests, which is free. A
    NOTE longer than the cap has to be struck again, which is audible -- so the
    caller raises the channel's unit until that stops happening where it can.
    """
    out, restruck = [], 0
    for (key, units) in stream:
        n = max(1, int(round(units / scale)))
        first = True
        while n > 0:
            take = min(n, MAX_LEN)
            out.append((key, take))
            if key is not None and not first:
                restruck += 1
            first = False
            n -= take
    return out, restruck


# ------------------------------------------------------------------- emitting

def pitch_of(key):
    """-> (octave 1..8, name) for a MIDI key, or None if off the hardware."""
    if not GBS_MIN_KEY <= key <= GBS_MAX_KEY:
        return None
    return (key // 12) - 2, PITCH_NAMES[key % 12]


def emit_melodic(lines, stream, label):
    octave = None
    for (key, n) in stream:
        if key is None:
            lines.append('\trest %d' % n)
            continue
        got = pitch_of(key)
        if got is None:
            lines.append('\trest %d' % n)
            continue
        o, name = got
        if o != octave:
            lines.append('\toctave %d' % o)
            octave = o
        lines.append('\tnote %s, %d' % (name, n))


def emit_noise(lines, stream, drums):
    """Percussion: the pitch nibble is an instrument index into the kit."""
    for (key, n) in stream:
        if key is None:
            lines.append('\trest %d' % n)
        else:
            lines.append('\tdrum_note %d, %d' % (drums.get(key, 1), n))


def transcribe(repo, song, spec, out_path, grid_ticks=3):
    repo = Path(repo)
    div, usec, tracks = read_midi(repo / 'sound/songs/midi' / (song + '.mid'))
    tempo, base_unit, err = choose_tempo(div, usec, grid_ticks)
    end_tick = max(off for t in tracks for (_o, off, _k) in t)
    end_unit = int(round(end_tick / grid_ticks))

    print('%s: %d ticks/quarter, %.1f BPM, %d note tracks'
          % (song, div, 60e6 / usec, len(tracks)))
    print('  grid %d ticks -> tempo %d, base note_type unit %d (%.3f%% off)'
          % (grid_ticks, tempo, base_unit, err * 100))

    stem = song.replace('mus_', '')
    name = ''.join(p.title() for p in stem.split('_'))
    lines = ['\t.include "asm/macros.inc"', '',
             '\t.section .rodata',
             '\t.global gbs_Music_%s' % name,
             '\t.align 2', '',
             '@ GENERATED by tools/rogue/midi_to_gbs.py from %s.mid' % song,
             '@ Real GBS song data on the GBS sequencer, not an m4a song with a',
             '@ PSG voicegroup. Expression (duty, vibrato, kit, wave sample) is',
             '@ from the spec in that tool and is meant to be tuned by ear.',
             '',
             'gbs_Music_%s:' % name,
             '\tchannel_count 4']
    for ch in range(1, 5):
        lines.append('\tchannel %d, Music_%s_Ch%d' % (ch, name, ch))
    lines.append('')

    for ch in range(1, 5):
        cfg = spec['channels'].get(ch)
        lines.append('gbs_Music_%s_Ch%d:' % (name, ch))
        lines.append('\tgbs_switch %d' % (ch - 1))
        lines.append('Music_%s_Ch%d:' % (name, ch))
        if ch == 1:
            lines.append('\ttempo %d' % tempo)
        if cfg is None:
            # A channel with nothing on it still has to exist and still has to
            # end, or the engine walks off into whatever follows in ROM.
            lines.append('\tnote_type %d, 0, 0' % base_unit)
            lines.append('\trest 16')
            lines.append('\tsound_ret')
            lines.append('')
            continue

        idx = cfg['track']
        notes = tracks[idx]
        events = quantize(notes, grid_ticks, end_tick)
        stream = to_stream(events, end_unit)
        scale = cfg.get('unit_scale', 1)
        stream, restruck = split_runs(stream, scale)
        unit = base_unit * scale

        keys = [k for (k, _n) in stream if k is not None]
        rng = ('keys %d-%d' % (min(keys), max(keys))) if keys else 'silent'
        low = sum(1 for k in keys if k < GBS_MIN_KEY)
        print('    ch%d  track #%-2d %-16s %4d events, unit %3d%s%s'
              % (ch, idx, rng, len(stream), unit,
                 ', %d re-struck' % restruck if restruck else '',
                 ', %d BELOW C2 -> rests' % low if low else ''))

        lines.append('\tvolume 7, 7')
        for pre in cfg.get('prelude', []):
            lines.append('\t' + pre)
        if ch == 4:
            lines.append('\ttoggle_noise %d' % cfg.get('kit', 0))
            lines.append('\tdrum_speed %d' % unit)
        else:
            lines.append('\tnote_type %d, %d, %d'
                         % (unit, cfg['volume'], cfg['envelope']))
        lines.append('Music_%s_Ch%d_loop:' % (name, ch))
        if ch == 4:
            emit_noise(lines, stream, cfg.get('drums', {}))
        else:
            emit_melodic(lines, stream, name)
        lines.append('\tsound_loop 0, Music_%s_Ch%d_loop' % (name, ch))
        lines.append('')

    out = repo / out_path
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    print('  wrote %s (%d lines)' % (out_path, len(lines)))
    return 0


# The arrangement. Channel assignment is an EAR decision, not a derivation, and
# it differs from the m4a reduction on purpose: that one had five parts fighting
# for four channels and put the strings pad on wave with the bass sharing square
# 2. The idiomatic Game Boy layout, and what GSC actually does, is melody on
# square 1, counter on square 2, BASS ON WAVE and drums on noise -- so the pad
# is the part that goes, not the bass.
SPECS = {
    'mus_petalburg_woods': {
        'channels': {
            1: dict(track=2, volume=10, envelope=2,          # flute melody
                    prelude=['duty_cycle 2', 'vibrato 12, 2, 4']),
            2: dict(track=4, volume=8, envelope=3,           # counter line
                    prelude=['duty_cycle 1', 'vibrato 16, 1, 4']),
            3: dict(track=5, volume=1, envelope=1,           # bass, wave ch:
                    unit_scale=2),                           # volume 0-3, env
            4: dict(track=7, kit=0, unit_scale=1,            # = wave sample id
                    drums={80: 1, 36: 2}),
        },
    },
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    ap.add_argument('--song', required=True)
    ap.add_argument('--out')
    ap.add_argument('--grid', type=int, default=3,
                    help='ticks per length unit (3 = 32nd notes at 24 ppq)')
    args = ap.parse_args()

    spec = SPECS.get(args.song)
    if spec is None:
        sys.exit('no channel spec for %s -- add one to SPECS' % args.song)
    out = args.out or ('sound/songs/gbs/gbs_%s.s'
                       % args.song.replace('mus_', ''))
    return transcribe(args.repo, args.song, spec, out, args.grid)


if __name__ == '__main__':
    sys.exit(main())
