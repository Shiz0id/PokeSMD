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
import json
import math
import struct
import sys
import wave
from pathlib import Path

RATE = 44100
MIDI_REL = 'sound/songs/midi/mus_petalburg_woods.mid'   # default; see --song
WAVE_REL = 'sound/programmable_wave_samples/06.pcm'

# Which song each variant belongs to. Slot numbers are song-specific, so a
# variant is only meaningful against its own MIDI.
VARIANT_SONG = {
    'lean': 'mus_petalburg_woods', 'share': 'mus_petalburg_woods',
    'swap': 'mus_petalburg_woods', 'fuller': 'mus_petalburg_woods',
    'softer': 'mus_petalburg_woods', 'fuller_decay': 'mus_petalburg_woods',
    'trainer_auto': 'mus_vs_trainer',
    'champion_fix': 'mus_encounter_champion',
    'brendan_fix': 'mus_encounter_brendan',
    'brendan_fix2': 'mus_encounter_brendan',
    'hiker_fix': 'mus_encounter_hiker',
}

# Voice assignment, mirroring MAPPING in make_gb_voicegroup.py. Keyed by the
# voicegroup slot the MIDI track selects.
#   kind: square1 | square2 | wave | noise | drop
#   duty/attack/decay/sustain/release read off the voicegroup entries.
#
# TWO SLOTS MAY NAME THE SAME KIND. That is not a mistake -- m4a assigns PSG
# hardware by voice TYPE, so both then want the same physical channel and
# contend. The renderer models that (see resolve_channel) rather than quietly
# playing them on top of each other, because a preview that is fuller than the
# ROM is worse than no preview.
VARIANTS = {
    # The first cut. Four parts, and it was judged too empty.
    'lean': {
        73: dict(kind='square1', duty=2, a=0, d=0, s=15, r=1, name='flute -> MELODY'),
        80: dict(kind='square2', duty=0, a=0, d=1, s=7,  r=1, name='harmony'),
        81: dict(kind='wave',    duty=0, a=0, d=7, s=15, r=2, name='BASS'),
        127: dict(kind='noise',  duty=0, a=0, d=1, s=0,  r=3, name='percussion'),
        0:  dict(kind='noise',   duty=0, a=0, d=1, s=0,  r=1, name='drums'),
        1:  dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='piano (dropped)'),
        45: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='pizzicato (dropped)'),
        48: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='strings (dropped)'),
        82: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='counter (dropped)'),
    },
    # Counter line back, SHARING square 2 with the harmony. Both parts sound
    # wherever they do not collide; where they do, the later note takes the
    # channel. Worth it only if they are mostly not simultaneous.
    'share': {
        73: dict(kind='square1', duty=2, a=0, d=0, s=15, r=1, name='flute -> MELODY'),
        80: dict(kind='square2', duty=0, a=0, d=1, s=7,  r=1, name='harmony'),
        82: dict(kind='square2', duty=1, a=0, d=1, s=9,  r=1, name='COUNTER (shared)'),
        81: dict(kind='wave',    duty=0, a=0, d=7, s=15, r=2, name='BASS'),
        127: dict(kind='noise',  duty=0, a=0, d=1, s=0,  r=3, name='percussion'),
        0:  dict(kind='noise',   duty=0, a=0, d=1, s=0,  r=1, name='drums'),
        1:  dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='piano (dropped)'),
        45: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='pizzicato (dropped)'),
        48: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='strings (dropped)'),
    },
    # Counter line takes square 2 outright and the harmony goes. A straight
    # swap: no contention at all, but no harmony either.
    'swap': {
        73: dict(kind='square1', duty=2, a=0, d=0, s=15, r=1, name='flute -> MELODY'),
        82: dict(kind='square2', duty=1, a=0, d=1, s=9,  r=1, name='COUNTER'),
        81: dict(kind='wave',    duty=0, a=0, d=7, s=15, r=2, name='BASS'),
        127: dict(kind='noise',  duty=0, a=0, d=1, s=0,  r=3, name='percussion'),
        0:  dict(kind='noise',   duty=0, a=0, d=1, s=0,  r=1, name='drums'),
        1:  dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='piano (dropped)'),
        45: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='pizzicato (dropped)'),
        48: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='strings (dropped)'),
        80: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='harmony (dropped)'),
    },
    # Counter line back AND the hole filled. Measured overlaps drive this:
    # harmony/counter collide 68% of the time so they cannot share, but
    # counter/bass collide only 33%, which is tolerable stealing. That frees
    # the wave channel for the strings pad -- which sounds for 20.4 s of 42.2,
    # against 8.7 s for the harmony it replaces. Sustained coverage is what was
    # missing; the counter and harmony have identical sounding time, so trading
    # one for the other was never going to fix emptiness.
    'fuller': {
        73: dict(kind='square1', duty=2, a=0, d=0, s=15, r=1, name='flute -> MELODY'),
        82: dict(kind='square2', duty=1, a=0, d=1, s=9,  r=1, name='COUNTER'),
        81: dict(kind='square2', duty=3, a=0, d=2, s=11, r=1, name='bass (shares sq2)'),
        48: dict(kind='wave',    duty=0, a=1, d=6, s=14, r=4, name='STRINGS pad'),
        127: dict(kind='noise',  duty=0, a=0, d=1, s=0,  r=3, name='percussion'),
        0:  dict(kind='noise',   duty=0, a=0, d=1, s=0,  r=1, name='drums'),
        80: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='harmony (dropped)'),
        1:  dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='piano (dropped)'),
        45: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='pizzicato (dropped)'),
    },
    # 'fuller', but with the melody voice tamed. 255 of the melody's 271 notes
    # are above 1200 Hz and every other part tops out below that, so it sits
    # alone and exposed at the top -- on a 50% duty square at full sustain,
    # which is the loudest, most blaring voice on the chip. Narrowing the duty
    # to 25% thins it, and a real decay to sustain 11 stops long high notes
    # sitting at maximum. Both are plain voicegroup parameters, so this is a
    # change that carries to the ROM rather than only to the preview.
    'softer': {
        73: dict(kind='square1', duty=1, a=0, d=3, s=11, r=2, name='MELODY (tamed)'),
        82: dict(kind='square2', duty=1, a=0, d=1, s=9,  r=1, name='COUNTER'),
        81: dict(kind='square2', duty=3, a=0, d=2, s=11, r=1, name='bass (shares sq2)'),
        48: dict(kind='wave',    duty=0, a=1, d=6, s=14, r=4, name='STRINGS pad'),
        127: dict(kind='noise',  duty=0, a=0, d=1, s=0,  r=3, name='percussion'),
        0:  dict(kind='noise',   duty=0, a=0, d=1, s=0,  r=1, name='drums'),
        80: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='harmony (dropped)'),
        1:  dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='piano (dropped)'),
        45: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='pizzicato (dropped)'),
    },
    # 'fuller' with the loop-seam fix. The track ends on a bare melody note held
    # 1.33 s with every other part already gone, and at sustain 15 / decay 0 a
    # square sits at maximum volume for all of it and then the song loops. The
    # sampled flute it replaces decays naturally, which is why vanilla has no
    # such artifact. Decay 3 to sustain 11 restores that behaviour; the duty
    # stays at 50% because the timbre was already approved.
    'fuller_decay': {
        73: dict(kind='square1', duty=2, a=0, d=3, s=11, r=1, name='MELODY (decays)'),
        82: dict(kind='square2', duty=1, a=0, d=1, s=9,  r=1, name='COUNTER'),
        81: dict(kind='square2', duty=3, a=0, d=2, s=11, r=1, name='bass (shares sq2)'),
        48: dict(kind='wave',    duty=0, a=1, d=6, s=14, r=4, name='STRINGS pad'),
        127: dict(kind='noise',  duty=0, a=0, d=1, s=0,  r=3, name='percussion'),
        0:  dict(kind='noise',   duty=0, a=0, d=1, s=0,  r=1, name='drums'),
        80: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='harmony (dropped)'),
        1:  dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='piano (dropped)'),
        45: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='pizzicato (dropped)'),
    },
    # mus_encounter_brendan. Its PLAN is identical to mus_encounter_may's, which
    # passed -- same tracks, same roles, same channels. The difference is purely
    # register: Brendan is the same arrangement written about 16 semitones
    # lower, which puts 49 of its 163 bass notes UNDER the 64 Hz floor, where
    # they clamp and the line flattens. May has 4.
    #
    # So the bass goes up an octave. That is what a GB arranger does with a bass
    # written for sampled instruments, and it lands Brendan's bass in the same
    # register as May's, which is known to work.
    # SLOT NUMBERS ARE PER SONG. Brendan's melody is slot 1 and its bass slot
    # 38; May's are 17 and 36. An early cut of this variant used May's numbers,
    # which matched nothing here, so the render came out with no melody and no
    # bass at all -- and the floor check reported "clean" because the bass was
    # not in it. Always read the slots off the song being arranged.
    'brendan_fix': {
        1:  dict(kind='square1', duty=2, a=0, d=2, s=13, r=1, name='MELODY'),
        38: dict(kind='square2', duty=3, a=0, d=2, s=12, r=1, transpose=12,
                 name='bass (+1 oct)'),
        81: dict(kind='square2', duty=1, a=0, d=1, s=10, r=1, name='counter'),
        83: dict(kind='wave',    duty=0, a=1, d=5, s=14, r=3, name='melody double'),
        0:  dict(kind='noise',   duty=0, a=0, d=1, s=0,  r=2, name='drums'),
        126: dict(kind='noise',  duty=0, a=0, d=1, s=0,  r=2, name='perc'),
        127: dict(kind='noise',  duty=0, a=0, d=1, s=0,  r=2, name='perc'),
        56: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='dropped'),
        80: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='dropped'),
    },
    # mus_encounter_hiker. Reported as tinny against the original's horns.
    #
    # The voicegroup names the instruments: slot 60 is voicegroup_french_horn_
    # keysplit -- literally the horns -- and slot 82 doubles that same line
    # (both A3-B5) on the wave channel. So the horn tone is carried by those two
    # together, and the wave half was being rendered with the WRONG WAVEFORM:
    # the source picks ProgrammableWaveData_5 and the tooling hardcoded 6.
    # Restoring 5 puts the doubling back to the timbre the track was written
    # with. Slot 58 is a tuba, which is why the bass sits so low.
    'hiker_fix': {
        60: dict(kind='square1', duty=2, a=0, d=2, s=13, r=1, name='HORNS'),
        82: dict(kind='wave',    duty=0, a=1, d=4, s=14, r=3, wave_sample=5,
                 name='horn double (wave 5)'),
        58: dict(kind='square2', duty=3, a=0, d=2, s=12, r=1, transpose=12,
                 name='tuba bass (+1 oct)'),
        80: dict(kind='square2', duty=1, a=0, d=1, s=10, r=1, name='counter'),
        0:  dict(kind='noise',   duty=0, a=0, d=1, s=0,  r=2, name='drums'),
        81: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='counter harmony'),
        47: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='timpani'),
        1:  dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='piano'),
    },
    # brendan_fix, plus the melody octave. May's two melody tracks are in exact
    # unison (+0 st on all 151 notes) so it does not matter which one leads; in
    # Brendan they diverge, 72 of 151 notes an octave apart, and the heuristic
    # took the LOWER one for square 1. That put the lead at median F#5 against
    # May's F6 -- a whole octave below the version that works.
    #
    # So slot 83 leads and slot 17 goes to the wave channel. Same two parts,
    # swapped, no invented pitches. Both this and the bass octave are the same
    # lesson: when the plan is right and it still sounds wrong, the register is
    # what is wrong.
    'brendan_fix2': {
        83: dict(kind='square1', duty=2, a=0, d=2, s=13, r=1, name='MELODY (upper)'),
        1:  dict(kind='wave',    duty=0, a=1, d=5, s=14, r=3, name='melody (lower)'),
        38: dict(kind='square2', duty=3, a=0, d=2, s=12, r=1, transpose=12,
                 name='bass (+1 oct)'),
        81: dict(kind='square2', duty=1, a=0, d=1, s=10, r=1, name='counter'),
        0:  dict(kind='noise',   duty=0, a=0, d=1, s=0,  r=2, name='drums'),
        126: dict(kind='noise',  duty=0, a=0, d=1, s=0,  r=2, name='perc'),
        127: dict(kind='noise',  duty=0, a=0, d=1, s=0,  r=2, name='perc'),
        56: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='dropped'),
        80: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='dropped'),
    },
    # mus_encounter_champion, corrected by ear. The heuristic gave square 1 to
    # slot 80 -- 127 notes ranging 64-105, which is a high repetitive sparkle
    # layer, not a tune -- and DROPPED slot 17, the sweeping phrase at 7-8 s
    # that the track is recognisable by. Slot 17 tops out at 82 and every other
    # part reaches higher, so simply moving it onto a shared channel would have
    # let top-note silence it again; it is protected instead. Slot 80 keeps the
    # rest of square 1, filling the 21.5 s the sweep leaves idle.
    'champion_fix': {
        # Keyed by TRACK INDEX, not slot: tracks #1 and #9 both select slot 17.
        # #1 (153 notes, C2-G#6) is the chromatic sweep at 7-8 s; #9 (93 notes)
        # is a lower doubling of it and is dropped, because on one mono channel
        # the doubling can only take notes away from the line it doubles.
        '#1': dict(kind='square1', duty=2, a=0, d=2, s=13, r=1, protect=True,
                   name='SWEEP - the tune'),
        '#9': dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='sweep doubling'),
        # Dropped, not kept as filler. The protected sweep occupies 22.4 s of
        # 30.4, so only about 17 of this part's 127 notes could survive around
        # it -- scattered debris rather than a line. This is the part the
        # heuristic had mistaken for the melody.
        80: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='high sparkle'),
        36: dict(kind='square2', duty=3, a=0, d=2, s=12, r=1, name='bass'),
        29: dict(kind='square2', duty=1, a=0, d=1, s=10, r=1, name='counter'),
        83: dict(kind='wave',    duty=0, a=1, d=5, s=14, r=3, name='pad'),
        0:  dict(kind='noise',   duty=0, a=0, d=1, s=0,  r=2, name='drums'),
        126: dict(kind='noise',  duty=0, a=0, d=1, s=0,  r=2, name='perc'),
        127: dict(kind='noise',  duty=0, a=0, d=1, s=0,  r=2, name='perc'),
        73: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='dropped'),
    },
    # mus_vs_trainer, first pass straight from plan_gb_arrangement.py. Square 2
    # is deliberately NOT shared: the bass sounds for all 89.7 s of the track,
    # so anything placed beside it would be stolen from constantly rather than
    # heard. Four parts kept of eight, 70% of the sounding time.
    'trainer_auto': {
        1:  dict(kind='square1', duty=2, a=0, d=2, s=13, r=1, name='MELODY'),
        33: dict(kind='square2', duty=3, a=0, d=2, s=12, r=1, name='bass'),
        48: dict(kind='wave',    duty=0, a=1, d=5, s=14, r=3, name='PAD'),
        0:  dict(kind='noise',   duty=0, a=0, d=1, s=0,  r=2, name='percussion'),
        4:  dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='dropped'),
        80: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='dropped'),
        47: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='dropped'),
        81: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, name='dropped'),
    },
}
# For the *_full.wav render, the dropped parts need something audible.
FULL_SUBSTITUTE = {1: 'square2', 45: 'square1', 48: 'wave', 82: 'square1',
                   80: 'square2'}

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


def read_wave_table(repo, n=6):
    """The GB wave channel's 32 4-bit samples, from sound/programmable_wave_samples.

    WHICH SAMPLE MATTERS AND IS NOT INTERCHANGEABLE. There are 25 of them and
    they are different waveforms, so the choice is the wave channel's entire
    timbre. Every song's own voicegroup names the one its composer picked;
    hardcoding one here (this was ProgrammableWaveData_6) silently retimbres the
    channel and reads as 'tinny' or just vaguely wrong.
    """
    path = repo / 'sound/programmable_wave_samples' / ('%02d.pcm' % n)
    raw = path.read_bytes()
    nib = []
    for byte in raw:
        nib.append(byte >> 4)
        nib.append(byte & 0xF)
    return [(v / 7.5) - 1.0 for v in nib]


def envelope(t, dur, v):
    """Crude AD-S-R. Musical, not cycle-accurate."""
    # FLOOR THE ATTACK. With a=0 this jumped straight to full amplitude, and a
    # square reset to phase 0 at the same instant -- a step discontinuity, i.e.
    # a click, on every note onset. The melody has 271 of them, most in the top
    # octave, which is a large part of what read as "harsh" in the first render.
    # 1.5 ms is short enough to still sound instant.
    atk = max((v['a'] / 15.0) * 0.04, 0.0015)
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


def resolve_channel(notes):
    """Make one channel's notes monophonic, the way the hardware is.

    m4a assigns PSG hardware by voice TYPE, so two tracks holding the same kind
    of voice want the same physical channel. Later note takes it; whatever was
    sounding is cut off there. Returns (notes, stolen_count).
    """
    # WHICH NOTE SURVIVES A COLLISION IS A MUSICAL CHOICE, not an implementation
    # detail. A single MIDI track can be polyphonic while a PSG channel cannot,
    # so a chordal part loses notes even alone on its channel -- half of
    # mus_encounter_interviewer's melody, for one. Keeping the TOP note is the
    # standard reduction and preserves the line a listener is following.
    #
    # EXCEPT WHEN IT DOES NOT. mus_encounter_champion's tune is a sweeping
    # phrase topping out at 82 while every other part reaches higher, so
    # top-note silenced precisely the part the track is recognisable by. A voice
    # may therefore set protect=True: it is laid down first and keeps its full
    # duration, and everything else fills the gaps around it. Use it for the
    # part the track would not be itself without -- not as a general preference,
    # because every protected note is taken out of some other part.
    prot = [n for n in notes if n[3].get('protect')]
    rest = [n for n in notes if not n[3].get('protect')]

    def mono(seq, blockers=()):
        """One note at a time, truncated against already-placed blockers."""
        seq = sorted(seq, key=lambda n: (n[0], -n[2]))
        kept, cut, lost = [], 0, 0
        i = 0
        while i < len(seq):
            on, off, pitch, v = seq[i]
            j = i + 1
            while j < len(seq) and seq[j][0] - on < 1e-6:
                lost += 1          # same onset, lower pitch: never heard
                j += 1
            if j < len(seq) and seq[j][0] < off:
                off = seq[j][0]
                cut += 1
            for (bon, boff, _, _) in blockers:
                if bon <= on < boff:      # starts inside a protected note
                    off = on
                    break
                if on < bon < off:        # runs into one
                    off = bon
                    cut += 1
            if off - on > 0.001:
                kept.append((on, off, pitch, v))
            else:
                lost += 1
            i = j
        return kept, cut, lost

    keep_p, cut_p, lost_p = mono(prot)
    keep_r, cut_r, lost_r = mono(rest, blockers=keep_p)
    return keep_p + keep_r, cut_p + cut_r, lost_p + lost_r


def render(tracks, voices, wave_table, total, psg_only, analyze_only=False):
    buf = [0.0] * (1 if analyze_only else int(total * RATE + RATE))

    # Group by the hardware channel each voice kind lands on.
    #
    # A voice may be keyed by voicegroup SLOT (an int) or by TRACK INDEX (the
    # string '#3'). Track index wins. mus_encounter_champion needs it: two of
    # its midi tracks select slot 17, and they are not duplicates -- one is the
    # chromatic sweep the track is known by, the other a lower doubling. Keyed
    # by slot alone there is no way to keep one and drop the other, and anything
    # that builds a dict keyed on slot silently discards one of them.
    channels = {}
    for idx, (slot, notes) in enumerate(tracks):
        v = voices.get('#%d' % idx, voices.get(slot))
        if v is None:
            continue
        kind = v['kind']
        if kind == 'drop':
            if psg_only:
                continue
            kind = FULL_SUBSTITUTE.get(slot, 'square1')
        # transpose is in semitones, applied BEFORE channel resolution so the
        # top-note rule sees the pitches that will actually sound. Its usual job
        # is lifting a bass line off the floor -- see the report below.
        tr = v.get('transpose', 0)

        def place(pitch):
            p = pitch + tr
            # floor_lift raises ONLY what the channel cannot represent, by
            # octaves, rather than moving the whole part. See the note in
            # wire_gb_song.py for when each is right.
            if v.get('floor_lift'):
                while p < 36:
                    p += 12
            return p

        channels.setdefault(kind, []).extend(
            (on, off, place(pitch), v) for (on, off, pitch) in notes)

    report = []
    for kind, notes in sorted(channels.items()):
        if psg_only:
            total_in = len(notes)
            notes, stolen, silenced = resolve_channel(notes)
            if stolen or silenced:
                report.append('    %-8s %d of %d notes cut short, %d never heard'
                              % (kind, stolen, total_in, silenced))
            # THE SQUARE CHANNELS HAVE A HARD FLOOR. The period register is 11
            # bits and f = 131072/(2048-x), so 64 Hz is the lowest note that
            # exists; anything under it is clamped and sounds at the wrong
            # pitch. mus_encounter_brendan's bass had 49 of 163 notes down
            # there and the line flattened toward a monotone. Reported because
            # it is inaudible as a cause -- it just sounds vaguely wrong.
            if kind in ('square1', 'square2'):
                low = sum(1 for (_, _, p, _) in notes
                          if 440.0 * 2 ** ((p - 69) / 12.0) < 64.0)
                if low:
                    report.append('    %-8s %d of %d notes BELOW THE 64 Hz FLOOR '
                                  '-- clamped, wrong pitch; transpose the part up'
                                  % (kind, low, len(notes)))
        if analyze_only:
            continue
        lfsr = 0x7FFF
        phase = 0.0     # carried ACROSS notes on a channel: the hardware
                        # oscillator is not reset per note, and restarting it
                        # at zero every time is another source of onset clicks.
        for (on, off, pitch, v) in notes:
            freq = 440.0 * (2.0 ** ((pitch - 69) / 12.0))
            dur = off - on
            tail = (v['r'] / 15.0) * 0.35 + 0.02
            i0 = int(on * RATE)
            n = int((dur + tail) * RATE)
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
                    tbl = wave_table[v.get('wave_sample', 6)]
                    smp = tbl[int(p * len(tbl)) % len(tbl)]
                else:  # noise
                    if k % 24 == 0:
                        bit = ((lfsr ^ (lfsr >> 1)) & 1)
                        lfsr = (lfsr >> 1) | (bit << 14)
                    smp = 1.0 if (lfsr & 1) else -1.0
                buf[idx] += env * smp * 0.16

    if not analyze_only:
        # A naive square has odd harmonics forever; at 44.1 kHz the ones past
        # Nyquist fold back as inharmonic grit, and the melody sits at 1.2-2.5
        # kHz where that bites. Real hardware output is bandlimited by its
        # analog stage, so an unfiltered render is harsher than the ROM by
        # construction. One-pole lowpass, ~7 kHz, applied once at the end.
        k = math.exp(-2.0 * math.pi * 7000.0 / RATE)
        prev = 0.0
        for i in range(len(buf)):
            prev = buf[i] * (1.0 - k) + prev * k
            buf[i] = prev
    return buf, report


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
    ap.add_argument('--variants', default='lean,share,swap',
                    help='comma-separated names from VARIANTS')
    ap.add_argument('--analyze', action='store_true',
                    help='report channel contention only, write no audio')
    ap.add_argument('--plans', type=Path,
                    help='JSON from plan_gb_arrangement.py --emit; adds every '
                         'plan in it as a variant')
    ap.add_argument('--all', action='store_true',
                    help='render every variant in --plans')
    ap.add_argument('--loops', type=int, default=1,
                    help='render N passes back to back. THE PREVIEW IS BLIND TO '
                         'LOOP SEAMS AT 1: every track in the game loops, and a '
                         'bare held note running into the restart is audible in '
                         'the ROM and absent here. Use 2 to hear the seam.')
    args = ap.parse_args()

    # Every sample, keyed by number: a variant may name any of them per part.
    wave_table = {n: read_wave_table(args.repo, n) for n in range(1, 26)}
    args.out.mkdir(parents=True, exist_ok=True)
    cache = {}

    names = [v.strip() for v in args.variants.split(',') if v.strip()]
    if args.plans:
        loaded = json.loads(args.plans.read_text(encoding='utf-8'))
        for key, spec in loaded.items():
            # '#N' track-index keys stay strings; only slot keys are ints.
            VARIANTS[key] = {(k if k.startswith('#') else int(k)): v
                             for k, v in spec['voices'].items()}
            VARIANT_SONG[key] = spec['song']
        if args.all:
            names = sorted(loaded)

    for name in names:
        voices = VARIANTS.get(name)
        if voices is None:
            sys.exit('unknown variant %r -- have %s'
                     % (name, ', '.join(sorted(VARIANTS))))
        # Slots are song-specific, so each variant parses its own MIDI.
        song = VARIANT_SONG.get(name, 'mus_petalburg_woods')
        if song not in cache:
            cache[song] = parse_midi(args.repo / 'sound/songs/midi' / (song + '.mid'))
        tracks = cache[song]
        total = max(off for _, notes in tracks for (_, off, _) in notes)
        if args.loops > 1:
            # Concatenate N passes. Approximates the GOTO the song assembles to:
            # the whole track is the loop body, which is true of these songs.
            tracks = [(slot, [(on + i * total, off + i * total, p)
                              for i in range(args.loops)
                              for (on, off, p) in notes])
                      for slot, notes in tracks]
            total *= args.loops
        print('variant %r  (%s, %d parts, %.1f s):' % (name, song, len(tracks), total))
        for idx, (slot, notes) in enumerate(tracks):
            v = voices.get('#%d' % idx, voices.get(slot))
            if v and v['kind'] != 'drop':
                print('    #%-2d slot %3d  %-20s %-8s %3d notes'
                      % (idx, slot, v['name'], v['kind'], len(notes)))

        # A key that matches nothing is silent otherwise: the part simply does
        # not play, and every other measurement still reports "clean" because
        # the missing part is not in them. An early brendan variant used another
        # song's slot numbers and rendered with no melody and no bass at all.
        present = {s for s, _ in tracks} | {'#%d' % i for i in range(len(tracks))}
        orphan = [k for k in voices if k not in present]
        if orphan:
            print('    WARNING: %s named in the variant but NOT IN THIS SONG -- '
                  'those parts are silently absent' % sorted(map(str, orphan)))
        unnamed = [s for s, _ in tracks
                   if s not in voices
                   and not any('#%d' % i in voices
                               for i, (ss, _) in enumerate(tracks) if ss == s)]
        if unnamed:
            print('    note: slots %s carry notes but the variant does not '
                  'mention them (they are silent)' % sorted(set(unnamed)))
        buf, report = render(tracks, voices, wave_table, total, True, args.analyze)
        for line in report:
            print(line)
        if not report:
            print('    no channel contention')
        if not args.analyze:
            # Named for the variant, not the song: the tool used to render only
            # the woods track and the prefix was hardcoded.
            p = args.out / ('%s.wav' % name)
            write_wav(p, buf)
            print('    wrote %s' % p)
        print()

    return 0


if __name__ == '__main__':
    sys.exit(main())
