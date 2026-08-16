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
        out[slot] = line.split()[0]
        slot += 1
    return out


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


def plan(tracks, voices=None):
    """tracks: [(slot, notes)] -> {slot: (channel, role)}"""
    voices = voices or {}
    info = {}
    for slot, notes in tracks:
        pitches = [p for (_, _, p) in notes]
        info[slot] = dict(notes=notes, n=len(notes), med=median(pitches),
                          lo=min(pitches), hi=max(pitches),
                          snd=sounding(notes), flat=len(set(pitches)) == 1)

    free = set(info)
    out = {}

    # Percussion, from the voicegroup where it says so, falling back on a part
    # whose pitch never varies. Several may qualify; they share the one noise
    # channel.
    perc = [s for s in free
            if voices.get(s, '').startswith(('voice_keysplit_all', 'voice_noise'))
            or info[s]['flat']]
    for s in perc:
        out[s] = ('noise', 'percussion')
        free.discard(s)

    # Melody: most notes among the parts in the upper half of the pitch range.
    if free:
        top = max(info[s]['med'] for s in free)
        upper = [s for s in free if info[s]['med'] >= top - 12] or list(free)
        mel = max(upper, key=lambda s: info[s]['n'])
        out[mel] = ('square1', 'MELODY')
        free.discard(mel)

    # Bass: the part that NEVER GOES HIGH, i.e. lowest top note -- not lowest
    # median. Validated against the hand-made woods arrangement, where the
    # median rule picked the piano (range 38-86, a wide accompaniment that
    # happens to play low notes) over the actual bass (50-63) and then dropped
    # the real bass entirely. A bass line is defined by its ceiling.
    if free:
        bass = min(free, key=lambda s: (info[s]['hi'], info[s]['med']))
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
            voices = {}
            for slot, (ch, role) in out.items():
                v = dict(ROLE_VOICE[role])
                v['name'] = role
                if ch == 'wave':
                    v['wave_sample'] = pick_wave_sample(samples, slot)
                voices[str(slot)] = v
            name = song.replace('mus_', '')
            plans[name] = dict(song=song, voices=voices)

            kept = sum(i['snd'] for s, i in info.items() if out[s][0] != 'drop')
            allp = sum(i['snd'] for i in info.values())
            used = sorted({out[s][0] for s in out if out[s][0] != 'drop'})
            print('%-28s %4d/%-2d %5s %7.0f%%   %s'
                  % (song, sum(1 for s in out if out[s][0] != 'drop'), len(out),
                     '', 100 * kept / allp if allp else 0, ' '.join(used)))
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
    print('%-6s %-9s %-18s %6s %7s %8s' %
          ('slot', 'channel', 'role', 'notes', 'range', 'sounding'))
    print('-' * 62)
    order = {'square1': 0, 'square2': 1, 'wave': 2, 'noise': 3, 'drop': 4}
    for slot in sorted(out, key=lambda s: (order[out[s][0]], -info[s]['snd'])):
        ch, role = out[slot]
        i = info[slot]
        print('%-6d %-9s %-18s %6d %3d-%-3d %7.1fs'
              % (slot, ch, role, i['n'], i['lo'], i['hi'], i['snd']))

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
