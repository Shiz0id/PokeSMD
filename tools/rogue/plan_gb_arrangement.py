"""Propose a PSG arrangement for a song, from a census of its MIDI.

WHY A TOOL AND NOT A JUDGEMENT CALL EACH TIME. Dungeon 1's woods track was
arranged by hand and took two rounds of listening. The trainer battle track and
the seventeen MUS_ENCOUNTER_* stings all need the same treatment -- and every
one of them is DENSER than the woods was: 7-10 note-carrying parts with 7-9
sounding at once, against 4 channels. Nothing is mechanical, so hand-arranging
eighteen tracks means eighteen rounds of the same reasoning.

WHAT IT AUTOMATES, AND WHAT IT CANNOT. It proposes a first pass. The rules below
are exactly the ones the woods arrangement arrived at the hard way:

  - MELODY is the part with the most notes in the upper register. It takes
    square 1, because it must survive.
  - BASS is the lowest part by median pitch.
  - PAD is whichever remaining part has the greatest SOUNDING TIME. This is the
    rule that mattered most: the first woods cut kept three sparse parts and
    dropped both sustained ones, and sounded empty. Note count is the wrong
    metric -- the counter line and the harmony had identical sounding time and
    swapping them changed nothing.
  - PERCUSSION is any part whose pitch never varies, or the drum track.
  - The last free channel goes to whichever remaining part OVERLAPS LEAST with
    whatever already holds it, because two parts on one channel steal from each
    other and the loser is simply never heard.

What it cannot do is tell you whether the result sounds like the tune. Render it
and listen -- that is what render_gb_preview.py is for.

Usage:  python3 tools/rogue/plan_gb_arrangement.py --repo PATH --song mus_vs_trainer
"""
import argparse
import importlib.util
import json
import re
import sys
from pathlib import Path


def load_parser(repo):
    spec = importlib.util.spec_from_file_location(
        "rgp", repo / "tools/rogue/render_gb_preview.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def sounding(notes):
    return sum(off - on for (on, off, _) in notes)


def median(xs):
    xs = sorted(xs)
    return xs[len(xs) // 2] if xs else 0


def overlap(a, b):
    tot = 0.0
    for (s1, e1, _) in a:
        for (s2, e2, _) in b:
            lo, hi = max(s1, s2), min(e1, e2)
            if hi > lo:
                tot += hi - lo
    return tot


def source_voices(repo, song):
    """slot -> macro name, read from the song's own voicegroup.

    Classifying percussion by 'the pitch never changes' worked for the woods
    track, whose drum part plays one key, and failed on the trainer battle,
    whose drum part uses several -- leaving the noise channel unused and the
    drums among the dropped parts. The voicegroup states it outright:
    voice_keysplit_all points at a drumset, voice_noise is already noise.
    """
    cfg = (repo / 'sound/songs/midi/midi.cfg').read_text(
        encoding='utf-8', errors='replace')
    m = re.search(r'^%s\.mid:\s*(.*)$' % re.escape(song), cfg, re.M)
    if not m:
        return {}
    g = re.search(r'-G(\S+)', m.group(1))
    if not g:
        return {}
    path = repo / 'sound/voicegroups' / (g.group(1).lstrip('_') + '.inc')
    if not path.exists():
        return {}
    out, slot = {}, 0
    for line in path.read_text(encoding='utf-8', errors='replace').split('\n')[1:]:
        line = line.strip()
        if not line or line.startswith('@'):
            continue
        out[slot] = line          # the WHOLE line: the sample name is the signal
        slot += 1
    return out


# The voicegroup names its instruments, and that is a far better signal for what
# a part IS than pitch and note count are. Six arrangements had to be corrected
# by ear because the pitch rules called an ornament the melody: surf's harp
# arpeggio, swimmer's 15-note accent, champion's sparkle layer.
LEAD_WORDS = ('trumpet', 'horn', 'strings', 'flute', 'sax', 'oboe', 'clarinet',
              'violin', 'brass', 'choir', 'organ', 'accordion', 'whistle',
              'glockenspiel', 'marimba', 'vibraphone')
TEXTURE_WORDS = ('harp', 'bubble', 'bell', 'chime', 'pizzicato', 'timpani',
                 'crystal')
BASS_WORDS = ('bass', 'tuba')


def has_word(line, words):
    line = (line or '').lower()
    return any(w in line for w in words)


def wave_samples(repo, song):
    """slot -> ProgrammableWaveData number, from the song's own voicegroup.

    There are 25 of these and they are different waveforms, so the choice IS the
    wave channel's timbre. Hardcoding one (this was 6) silently retimbres the
    channel: it happened to nine of the first twelve songs converted, and reads
    as 'tinny' or vaguely wrong with no obvious cause.
    """
    cfg = (repo / 'sound/songs/midi/midi.cfg').read_text(encoding='utf-8')
    m = re.search(r'^%s\.mid:\s*(.*)$' % re.escape(song), cfg, re.M)
    if not m:
        return {}
    g = re.search(r'-G(\S+)', m.group(1))
    if not g:
        return {}
    path = repo / 'sound/voicegroups' / (g.group(1).lstrip('_') + '.inc')
    if not path.exists():
        return {}
    out, slot = {}, 0
    for line in path.read_text(encoding='utf-8').split('\n')[1:]:
        line = line.strip()
        if not line or line.startswith('@'):
            continue
        w = re.search(r'ProgrammableWaveData_(\d+)', line)
        if w:
            out[slot] = int(w.group(1))
        slot += 1
    return out


def pick_wave_sample(samples, slot):
    """The sample for the part going on the wave channel.

    Its own slot's if that slot was already a wave voice -- that is the exact
    timbre the composer gave this line. Otherwise the song's wave voice
    elsewhere, since that is the timbre it chose for that channel at all. Only
    then a default.
    """
    if slot in samples:
        return samples[slot]
    if samples:
        return sorted(samples.values())[0]
    return 6


def redundant_of(tracks, i, j):
    """Is track j a duplicate or octave copy of track i?

    THE PLANNER CANNOT SEE REDUNDANCY OTHERWISE. It ranks by pitch, note count
    and sounding time, and a duplicate scores identically well on every one of
    them -- so a copy of the bass can win the wave channel and contribute
    nothing, which is exactly what happened to mus_encounter_rich. Five of the
    six arrangements corrected by ear were this: rich's bass copy, interviewer's
    bass octave, swimmer's second harp, champion's sweep doubling, surf's second
    strings.

    A copy is one that lands on the same pitch, or an exact octave from it, on
    most of the onsets the two share.
    """
    a, b = tracks[i][1], tracks[j][1]
    ai = {round(o, 3): p for (o, _, p) in a}
    d = [p - ai[round(o, 3)] for (o, _, p) in b if round(o, 3) in ai]
    if len(d) < 8 or len(d) < 0.5 * min(len(a), len(b)):
        return False
    same = sum(1 for x in d if x in (0, 12, -12, 24, -24))
    return same > 0.85 * len(d)


def drop_redundant(tracks, info, free):
    """Remove copies from consideration, keeping the fuller of each pair."""
    dropped = {}
    for i in sorted(free):
        if i in dropped:
            continue
        for j in sorted(free):
            if j <= i or j in dropped:
                continue
            if redundant_of(tracks, i, j):
                # keep whichever sounds for longer; a copy adds nothing
                loser = j if info[i]['snd'] >= info[j]['snd'] else i
                keeper = i if loser == j else j
                dropped[loser] = keeper
                if loser == i:
                    break
    return dropped


def plan(tracks, voices=None):
    """tracks: [(slot, notes)] -> {track index: (channel, role)}

    KEYED BY TRACK INDEX, NOT SLOT. Two midi tracks may select the same
    voicegroup slot and not be duplicates at all -- champion's chromatic sweep
    and a doubling of it, surf's two string parts, underwater's two counters.
    Keying the census by slot silently discarded one of each pair, so those
    songs were planned on incomplete data and three of them had to be corrected
    by ear afterwards.
    """
    voices = voices or {}
    info = {}
    for idx, (slot, notes) in enumerate(tracks):
        pitches = [p for (_, _, p) in notes]
        info[idx] = dict(slot=slot, notes=notes, n=len(notes),
                         med=median(pitches), lo=min(pitches), hi=max(pitches),
                         snd=sounding(notes), flat=len(set(pitches)) == 1)

    free = set(info)
    out = {}

    # Copies first: they are indistinguishable from the real part on every
    # metric below, so they must go before anything is ranked.
    for loser, keeper in drop_redundant(tracks, info, free).items():
        out[loser] = ('drop', 'copy of #%d' % keeper)
        free.discard(loser)

    # Percussion, from the voicegroup where it says so, falling back on a part
    # whose pitch never varies. Several may qualify; they share the one noise
    # channel.
    perc = [s for s in free
            if voices.get(info[s]['slot'], '').split()[0].startswith(
                ('voice_keysplit_all', 'voice_noise'))
            or info[s]['flat']]
    for s in perc:
        out[s] = ('noise', 'percussion')
        free.discard(s)

    # Melody. Named melodic instruments are preferred over ornaments, and only
    # if the voicegroup says nothing useful does it fall back to pitch and note
    # count -- which is the rule that kept picking harps and high accents.
    if free:
        # Three tiers, most specific first:
        #   1. a NAMED melodic instrument -- the voicegroup saying what it is
        #   2. failing that, a voice_square_1 part: on a song whose chip layer
        #      is already written, square 1 is where its lead was meant to go
        #   3. failing that, anything that is not obviously texture
        named = [s for s in free
                 if has_word(voices.get(info[s]['slot']), LEAD_WORDS)
                 and not has_word(voices.get(info[s]['slot']), TEXTURE_WORDS)
                 and not has_word(voices.get(info[s]['slot']), BASS_WORDS)]
        sq1 = [s for s in free
               if (voices.get(info[s]['slot']) or '').startswith('voice_square_1')]
        pool = (named or sq1
                or [s for s in free
                    if not has_word(voices.get(info[s]['slot']), TEXTURE_WORDS)]
                or list(free))
        top = max(info[s]['med'] for s in pool)
        upper = [s for s in pool if info[s]['med'] >= top - 12] or pool
        mel = max(upper, key=lambda s: (info[s]['n'], info[s]['snd']))
        out[mel] = ('square1', 'MELODY')
        free.discard(mel)

    # Bass: the part that NEVER GOES HIGH, i.e. lowest top note -- not lowest
    # median. Validated against the hand-made woods arrangement, where the
    # median rule picked the piano (range 38-86, a wide accompaniment that
    # happens to play low notes) over the actual bass (50-63) and then dropped
    # the real bass entirely. A bass line is defined by its ceiling.
    if free:
        named = [s for s in free
                 if has_word(voices.get(info[s]['slot']), BASS_WORDS)]
        pool = named or list(free)
        bass = min(pool, key=lambda s: (info[s]['hi'], info[s]['med']))
        out[bass] = ('square2', 'bass')
        free.discard(bass)

    # Pad: greatest sounding time of what is left -- the fullness rule.
    if free:
        pad = max(free, key=lambda s: info[s]['snd'])
        out[pad] = ('wave', 'PAD')
        free.discard(pad)

    # One channel is shareable. Maximise the time that actually SURVIVES, which
    # is sounding time minus what the bass steals -- not minimum overlap. The
    # minimum-overlap rule handed square 2 to a 28-note part on the trainer
    # battle track while 260-note parts were dropped, because a bass that sounds
    # continuously makes every candidate collide and the emptiest one then wins
    # by default.
    if free:
        bass_slot = [s for s, (c, r) in out.items() if r == 'bass']
        if bass_slot:
            ref = info[bass_slot[0]]['notes']
            best = max(free, key=lambda s: info[s]['snd']
                       - overlap(info[s]['notes'], ref))
            survives = info[best]['snd'] - overlap(info[best]['notes'], ref)
            # If nothing meaningfully survives, leave the channel to the bass
            # alone rather than adding a part that is stolen from constantly.
            if survives > 1.0:
                out[best] = ('square2', 'counter (shares)')
                free.discard(best)

    for s in free:
        out[s] = ('drop', 'dropped')
    return info, out


# Envelope per role, copied from the trainer battle pass that was accepted
# as-is. duty 2 (50%) is the standard GB lead; 3 (75%) is fat and suits a bass;
# 1 (25%) is thin and stays out of the melody's way.
ROLE_VOICE = {
    'MELODY':           dict(kind='square1', duty=2, a=0, d=2, s=13, r=1),
    'bass':             dict(kind='square2', duty=3, a=0, d=2, s=12, r=1),
    'counter (shares)': dict(kind='square2', duty=1, a=0, d=1, s=10, r=1),
    'PAD':              dict(kind='wave',    duty=0, a=1, d=5, s=14, r=3),
    'percussion':       dict(kind='noise',   duty=0, a=0, d=1, s=0,  r=2),
    'dropped':          dict(kind='drop',    duty=0, a=0, d=0, s=0,  r=0),
}


def plan_song(repo, song):
    rgp = load_parser(repo)
    mid = repo / 'sound/songs/midi' / (song + '.mid')
    if not mid.exists():
        return None, None, None
    tracks = rgp.parse_midi(mid)
    if not tracks:
        return None, None, None
    info, out = plan(tracks, source_voices(repo, song))
    return tracks, info, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    ap.add_argument('--song')
    ap.add_argument('--glob', help='e.g. "mus_encounter_*" to plan a whole family')
    ap.add_argument('--emit', type=Path,
                    help='write a plans JSON that render_gb_preview.py --plans reads')
    args = ap.parse_args()

    if args.glob or (args.song and args.emit):
        if args.glob:
            songs = sorted(p.stem for p in
                           (args.repo / 'sound/songs/midi').glob(args.glob + '.mid'))
        else:
            songs = [args.song]
        if not songs:
            sys.exit('no midi matched %r' % args.glob)
        plans = {}
        print('%-28s %5s %6s %8s   %s'
              % ('track', 'kept', 'parts', 'sounding', 'channels used'))
        print('-' * 78)
        for song in songs:
            tracks, info, out = plan_song(args.repo, song)
            if out is None:
                print('%-28s  (no note data)' % song)
                continue
            samples = wave_samples(args.repo, song)
            # A slot that only one track uses can be addressed by slot; a slot
            # two tracks share must be addressed by track index, or the two get
            # the same voice and cannot be told apart.
            counts = {}
            for slot, _ in tracks:
                counts[slot] = counts.get(slot, 0) + 1
            voices = {}
            for idx, (ch, role) in out.items():
                slot = info[idx]['slot']
                v = dict(ROLE_VOICE[role])
                v['name'] = role
                if ch == 'wave':
                    v['wave_sample'] = pick_wave_sample(samples, slot)
                if ch in ('square1', 'square2'):
                    v['floor_lift'] = True
                key = '#%d' % idx if counts[slot] > 1 else str(slot)
                voices[key] = v
            name = song.replace('mus_', '')
            plans[name] = dict(song=song, voices=voices)

            kept = sum(i['snd'] for s, i in info.items() if out[s][0] != 'drop')
            allp = sum(i['snd'] for i in info.values())
            used = sorted({out[s][0] for s in out if out[s][0] != 'drop'})
            dupes = sum(1 for c in counts.values() if c > 1)
            print('%-28s %4d/%-2d %11.0f%%   %s%s'
                  % (song, sum(1 for s in out if out[s][0] != 'drop'), len(out),
                     100 * kept / allp if allp else 0, ' '.join(used),
                     '   (%d shared slot%s)' % (dupes, '' if dupes == 1 else 's')
                     if dupes else ''))
        if args.emit:
            # Merge rather than overwrite, so families can be accumulated into
            # one plans file across several runs.
            if args.emit.exists():
                old = json.loads(args.emit.read_text(encoding='utf-8'))
                old.update(plans)
                plans = old
            args.emit.write_text(json.dumps(plans, indent=2), encoding='utf-8',
                                 newline='\n')
            print('\nwrote %s  (%d plans)' % (args.emit, len(plans)))
        return 0

    if not args.song:
        sys.exit('need --song or --glob')

    rgp = load_parser(args.repo)
    mid = args.repo / 'sound/songs/midi' / (args.song + '.mid')
    if not mid.exists():
        sys.exit('no such midi: %s' % mid)

    tracks = rgp.parse_midi(mid)
    if not tracks:
        sys.exit('%s has no note-carrying tracks' % args.song)
    info, out = plan(tracks, source_voices(args.repo, args.song))

    total = max(off for _, ns in tracks for (_, off, _) in ns)
    print('%s -- %d parts, %.1f s\n' % (args.song, len(tracks), total))
    # BOTH numbers, always. Track index and slot are different things and a
    # column showing one under the other's heading has already cost a round:
    # a comparison script printed one song's slots beside another's tracks and
    # an arrangement was hand-written against numbers that matched nothing.
    print('%-5s %-6s %-9s %-18s %6s %7s %8s' %
          ('trk', 'slot', 'channel', 'role', 'notes', 'range', 'sounding'))
    print('-' * 68)
    order = {'square1': 0, 'square2': 1, 'wave': 2, 'noise': 3, 'drop': 4}
    counts = {}
    for slot, _ in tracks:
        counts[slot] = counts.get(slot, 0) + 1
    for idx in sorted(out, key=lambda s: (order[out[s][0]], -info[s]['snd'])):
        ch, role = out[idx]
        i = info[idx]
        shared = '*' if counts[i['slot']] > 1 else ' '
        print('#%-4d %-5d%s %-9s %-18s %6d %3d-%-3d %7.1fs'
              % (idx, i['slot'], shared, ch, role, i['n'], i['lo'], i['hi'],
                 i['snd']))
    if any(c > 1 for c in counts.values()):
        print('  * shared slot: addressed by track index, since a voicegroup '
              'cannot tell two tracks on one slot apart')

    kept = sum(i['snd'] for s, i in info.items() if out[s][0] != 'drop')
    allp = sum(i['snd'] for i in info.values())
    print('\nsounding time kept: %.1f s of %.1f s (%.0f%%)'
          % (kept, allp, 100 * kept / allp))
    print('\nVARIANT dict for render_gb_preview.py:')
    print("    '%s': {" % args.song.replace('mus_', ''))
    for slot in sorted(out, key=lambda s: order[out[s][0]]):
        ch, role = out[slot]
        if ch == 'drop':
            print("        %d: dict(kind='drop', duty=0, a=0, d=0, s=0, r=0, "
                  "name='%s')," % (slot, role))
        else:
            duty = {'square1': 2, 'square2': 1, 'wave': 0, 'noise': 0}[ch]
            print("        %d: dict(kind='%s', duty=%d, a=0, d=2, s=12, r=1, "
                  "name='%s')," % (slot, ch, duty, role))
    print('    },')
    return 0


if __name__ == '__main__':
    sys.exit(main())
