"""Wire a song's PSG rendering into the build, end to end and idempotently.

Converting one song by hand touches EIGHT files: the voicegroup, its include, a
duplicated MIDI, midi.cfg, gbs_songs.h, gbs_song_table.h, gbs_song_table.c and
song_table.inc. Doing that twelve times by hand is where a silent mistake gets
in -- a missed song_table row means the toggle simply does nothing for that
track, with no build error.

Re-running is safe: every edit checks for itself first, so adding a song later
does not disturb the ones already wired.

WHAT IT DOES NOT DO. It does not decide the arrangement. That comes from
plan_gb_arrangement.py, or from HAND_TUNED below for songs that were adjusted by
ear -- the woods track is one, and its mapping must not be regenerated from the
heuristic, which makes different (defensible, but not chosen) picks.

Usage:
  python3 tools/rogue/wire_gb_song.py --repo . --plans plans.json --songs a,b,c
  python3 tools/rogue/wire_gb_song.py --repo . --plans plans.json --all
"""
import argparse
import json
import re
import shutil
import struct
import sys
from pathlib import Path


# ---------------------------------------------------------------- midi edits
#
# SOME ARRANGEMENT DECISIONS CANNOT LIVE IN A VOICEGROUP, and the two that come
# up are:
#
#   transpose -- mid2agb has no transpose option (checked: -L -V -G -P -R -X -E
#     -N and nothing else) and a voicegroup cannot shift a part's pitch. Lifting
#     a bass off the 64 Hz floor therefore has to change the notes themselves.
#
#   dropping ONE of two tracks that select the same voicegroup slot -- m4a
#     indexes the voicegroup by the track's program change, so both tracks get
#     the same voice no matter what is written there. mus_encounter_champion
#     has its sweep and a doubling of it both on slot 17.
#
# Both are applied to the DUPLICATED _gb.mid, never the original.

def _chunks(b):
    if bytes(b[:4]) != b'MThd':
        raise ValueError('not a MIDI file')
    hdr = struct.unpack('>I', bytes(b[4:8]))[0]
    ntrks = struct.unpack('>H', bytes(b[10:12]))[0]
    i, out = 8 + hdr, []
    for _ in range(ntrks):
        if bytes(b[i:i + 4]) != b'MTrk':
            break
        ln = struct.unpack('>I', bytes(b[i + 4:i + 8]))[0]
        out.append((i, i + 8, i + 8 + ln))
        i += 8 + ln
    return ntrks, out


def _scan(b, start, end):
    """-> (ALL programs in order, [positions of note key bytes])

    ALL of them, not the first. A track may change program part way through --
    mus_route119's first track walks 58 -> 56 -> 46 -> 9 -> 46 -> 56 -- and the
    ROM honours every change. Treating a track as having one instrument leaves
    the later slots unnamed by the plan, so the generator copies whatever filler
    the source voicegroup had there: voice_square_1 at full sustain with no
    decay, fighting the melody for square 1. That is inaudible in a preview,
    which renders one voice per track, and it is why petalburg_woods, route119
    and route111 sounded nothing like theirs.
    """
    j, status, progs, keys = start, 0, [], []
    while j < end:
        while b[j] & 0x80:      # delta time
            j += 1
        j += 1
        if b[j] & 0x80:
            status = b[j]
            j += 1
        ev = status & 0xF0
        if ev in (0x80, 0x90, 0xA0):
            keys.append(j)
            j += 2
        elif ev in (0xB0, 0xE0):
            j += 2
        elif ev == 0xC0:
            if b[j] not in progs:
                progs.append(b[j])
            j += 1
        elif ev == 0xD0:
            j += 1
        elif status in (0xFF, 0xF0, 0xF7):
            if status == 0xFF:
                j += 1
            ln = 0
            while b[j] & 0x80:
                ln = (ln << 7) | (b[j] & 0x7F)
                j += 1
            ln = (ln << 7) | b[j]
            j += 1 + ln
        else:
            j += 2
    return progs, keys


def midi_transpose_slot(path, slot, semitones):
    b = bytearray(path.read_bytes())
    _, chunks = _chunks(b)
    moved = 0
    for (_, s, e) in chunks:
        progs, keys = _scan(b, s, e)
        if slot not in progs:
            continue
        for k in keys:
            b[k] = max(0, min(127, b[k] + semitones))
            moved += 1
    if not moved:
        return 'no track selects slot %d' % slot
    path.write_bytes(bytes(b))
    log('midi: transposed slot %d by %+d (%d notes)' % (slot, semitones, moved))
    return None


GB_FLOOR_KEY = 36   # C2, 65.4 Hz. Below this f = 131072/(2048-x) has no answer.


STRADDLE_FRACTION = 0.08


def midi_floor_lift_slot(path, slot):
    """Get a part into the square channel's range, bodily or note by note.

    WHICH OF THE TWO IS A PROPORTION QUESTION, and getting it wrong is audible.
    Lifting only the sub-floor notes suits a part that dips below occasionally:
    the note returns at its own pitch class instead of clamping sharp, and the
    register is otherwise untouched. But for a part sitting ACROSS the boundary
    it puts consecutive notes of one phrase an octave apart, and the line jumps
    about -- mus_vs_gym_leader's bass, 13.5% below, did that for eight seconds
    and was reported as sounding like a glitchy Game Boy.

    So more than a twelfth under the floor means the part is in the wrong
    register and moves bodily; whatever is still below after that is a real
    outlier and gets lifted alone. Computed per part rather than kept as a list
    of songs, which would go stale the first time an arrangement moved.
    """
    b = bytearray(path.read_bytes())
    _, chunks = _chunks(b)
    targets = [(s, e) for (_, s, e) in chunks if slot in _scan(b, s, e)[0]]
    if not targets:
        return None

    keys = [k for (s, e) in targets for k in _scan(b, s, e)[1]]
    if not keys:
        return None

    shift = 0
    while shift < 24:
        below = sum(1 for k in keys if b[k] + shift < GB_FLOOR_KEY)
        if below <= STRADDLE_FRACTION * len(keys):
            break
        shift += 12

    bulk = lifted = 0
    for k in keys:
        v = b[k] + shift
        if shift:
            bulk += 1
        if v < GB_FLOOR_KEY:
            while v < GB_FLOOR_KEY:
                v += 12
            lifted += 1
        b[k] = min(127, v)

    if not shift and not lifted:
        return None
    path.write_bytes(bytes(b))
    if shift:
        log('midi: slot %d straddled the floor -- whole part up %d semitones '
            '(%d notes)%s' % (slot, shift, bulk // 2,
                              ', %d stragglers lifted' % (lifted // 2) if lifted else ''))
    else:
        log('midi: lifted %d sub-floor notes on slot %d into range'
            % (lifted // 2, slot))
    return None


def note_tracks(b):
    """-> [(chunk index, program)] for tracks that CARRY NOTES.

    '#N' KEYS COUNT NOTE-BEARING TRACKS, NOT CHUNKS, because that is what
    render_gb_preview.py's parse_midi returns and the plans are written against
    its numbering. Every one of these MIDIs opens with a conductor track holding
    only tempo, so counting chunks shifts every index by one -- which silently
    wired mus_encounter_champion's melody voice onto its percussion slot and
    dropped its counter line instead of the intended doubling.
    """
    _, chunks = _chunks(b)
    out = []
    for ci, (_, s, e) in enumerate(chunks):
        progs, keys = _scan(b, s, e)
        if keys:
            out.append((ci, progs))
    return out


def track_slots(path):
    """-> [program per note-bearing MIDI track], matching parse_midi."""
    # First program per track: what a #N key resolves to.
    return [(progs[0] if progs else None)
            for _, progs in note_tracks(bytearray(path.read_bytes()))]


def midi_drop_track(path, index):
    b = bytearray(path.read_bytes())
    ntrks, chunks = _chunks(b)
    notes = note_tracks(b)
    if index >= len(notes):
        return 'note-bearing track #%d does not exist' % index
    # index counts note-bearing tracks; map it back to the real chunk.
    cs, _, ce = chunks[notes[index][0]]
    out = bytearray(b[:cs]) + bytearray(b[ce:])
    struct.pack_into('>H', out, 10, ntrks - 1)
    path.write_bytes(bytes(out))
    log('midi: dropped track #%d' % index)
    return None

SILENT = 'voice_square_1_alt 60, 0, 0, 3, 0, 0, 0, 0'

# The four CGB voice types, each with an _alt form. A gb_* voicegroup must
# contain NOTHING ELSE -- that is the whole claim its header makes.
#
# WHY A NON-PSG VOICE IN HERE IS INVISIBLE. It does not fail to build: a
# voice_directsound entry is valid asm anywhere. It does not steal a PSG
# channel either, because m4a allocates sampled voices from a separate pool --
# so no contention measurement moves. The song simply plays that part on the
# original Emerald instrument, UNDERNEATH the PSG reduction. And
# render_gb_preview.py skips slots the plan does not mention, so the preview is
# thinner than the ROM and sounds correct. Twenty of thirty-six songs shipped
# this way.
PSG_VOICES = frozenset(
    base + suffix
    for base in ('voice_square_1', 'voice_square_2',
                 'voice_programmable_wave', 'voice_noise')
    for suffix in ('', '_alt'))


def is_psg_voice(line):
    """True if this line is a PSG voice, or is not a slot at all.

    Comments, blanks and the voice_group label are not slots and pass through.
    """
    m = re.match(r'\s*(voice_\w+)', line)
    if not m or m.group(1) == 'voice_group':
        return True
    return m.group(1) in PSG_VOICES

# Songs whose arrangement was tuned by ear and must not be regenerated from the
# HEURISTIC. Empty now: the woods arrangement lives in a plan of its own, taken
# from the accepted variant, so it goes through this tool like everything else
# and gets the same full slot coverage. Leaving it out was what let its counter
# line hijack square 1 whenever the track changed program.
HAND_TUNED = set()

VOICE_FOR = {
    'square1': 'voice_square_1_alt 60, 0, 0, %(duty)d, %(a)d, %(d)d, %(s)d, %(r)d',
    'square2': 'voice_square_2_alt 60, 0, %(duty)d, %(a)d, %(d)d, %(s)d, %(r)d',
    # The sample number comes from the PLAN, which reads it from the song's own
    # voicegroup. There are 25 waveforms and the choice is the wave channel's
    # whole timbre -- hardcoding 6 here retimbred nine of the first twelve songs
    # converted, which is what 'tinny' turned out to mean.
    'wave':    'voice_programmable_wave_alt 60, 0, ProgrammableWaveData_%(wave_sample)d, '
               '%(a)d, %(d)d, %(s)d, %(r)d',
    'noise':   'voice_noise_alt 60, 0, 1, %(a)d, %(d)d, %(s)d, %(r)d',
}


def log(msg):
    print('    ' + msg)


def source_voicegroup(repo, song):
    cfg = (repo / 'sound/songs/midi/midi.cfg').read_text(encoding='utf-8')
    m = re.search(r'^%s\.mid:\s*(.*)$' % re.escape(song), cfg, re.M)
    if not m:
        return None, None
    g = re.search(r'-G(\S+)', m.group(1))
    if not g:
        return None, None
    stem = g.group(1).lstrip('_')
    return stem, repo / 'sound/voicegroups' / (stem + '.inc')


def wave_sample_for(lines, slot):
    """The waveform for a part going on the wave channel.

    Its own slot's if that slot was already a wave voice -- the exact timbre the
    composer gave this line. Otherwise the song's wave voice elsewhere, since
    that is what it chose for that channel at all. There are 25 of them and the
    choice is the channel's whole timbre, so guessing one is not neutral.
    """
    body = [l.strip() for l in lines[1:] if l.strip() and not l.strip().startswith('@')]
    if slot < len(body):
        m = re.search(r'ProgrammableWaveData_(\d+)', body[slot])
        if m:
            return int(m.group(1))
    found = sorted(int(m) for m in
                   re.findall(r'ProgrammableWaveData_(\d+)', '\n'.join(body)))
    return found[0] if found else 6


def write_voicegroup(repo, song, voices, out_stem):
    src_stem, src = source_voicegroup(repo, song)
    if src is None or not src.exists():
        return 'no source voicegroup for %s' % song
    lines = src.read_text(encoding='utf-8').split('\n')

    out = ['@ GENERATED by tools/rogue/wire_gb_song.py -- do not hand-edit.',
           '@',
           '@ PSG-only rendering of voicegroup_%s, so mus_%s can play through the'
           % (src_stem, song.replace('mus_', '')),
           '@ Game Boy sound channels with no transcription. Slots not listed in',
           '@ the arrangement are silenced: there are four channels and these',
           '@ songs have more parts than that.',
           '',
           'voice_group gb_%s' % out_stem]

    slot, replaced = 0, 0
    for line in lines[1:]:
        if not line.strip():
            continue
        v = voices.get(str(slot))
        if v is None:
            # A slot the plan does not name is filler ONLY IF IT IS ALREADY PSG.
            # The header above has always claimed unlisted slots are silenced;
            # until this branch existed it copied them through verbatim, so any
            # slot the song actually selects kept its sampled Emerald voice.
            if is_psg_voice(line):
                out.append(line.rstrip())
            else:
                out.append('\t@ slot %d: not in the arrangement and not a PSG '
                           'voice -- silenced' % slot)
                out.append('\t' + SILENT)
        else:
            kind = v['kind']
            if kind == 'drop':
                out.append('\t@ slot %d: %s' % (slot, v.get('name', 'dropped')))
                out.append('\t' + SILENT)
            else:
                if kind == 'wave' and 'wave_sample' not in v:
                    v = dict(v)
                    v['wave_sample'] = wave_sample_for(lines, slot)
                    log('slot %d wave sample %d (from the source voicegroup)'
                        % (slot, v['wave_sample']))
                # transpose is handled by editing the _gb.mid, not here.
                out.append('\t@ slot %d: %s -> %s' % (slot, v.get('name', ''), kind))
                out.append('\t' + VOICE_FOR[kind] % v)
            replaced += 1
        slot += 1

    if replaced != len(voices):
        missing = sorted(int(k) for k in voices if int(k) >= slot)
        return ('%d slots to set but only %d are within this voicegroup '
                '(%d entries); out of range: %s'
                % (len(voices), replaced, slot, missing))

    out.append('')
    dest = repo / 'sound/voicegroups' / ('gb_%s.inc' % out_stem)
    dest.write_text('\n'.join(out), encoding='utf-8', newline='\n')
    log('voicegroup gb_%s.inc (%d slots, %d set)' % (out_stem, slot, replaced))
    return None


def ensure_line(path, needle, addition, after=None):
    """Insert `addition` once. Returns True if it changed the file."""
    txt = path.read_text(encoding='utf-8')
    if needle in txt:
        return False
    if after:
        idx = txt.rindex(after) + len(after)
        # Leading newline: the anchor is a whole line, so appending directly
        # ran the new declaration onto the end of it.
        txt = txt[:idx] + '\n' + addition.rstrip('\n') + txt[idx:]
    else:
        txt = txt.rstrip('\n') + '\n' + addition.lstrip('\n')
    path.write_text(txt, encoding='utf-8', newline='\n')
    return True


def wire(repo, song, spec, const):
    stem = song.replace('mus_', '')
    voices = spec['voices']

    # Resolve '#N' track keys to voicegroup slots. A '#N' that is DROPPED is not
    # a voicegroup entry at all -- it is a MIDI edit, because its slot is shared
    # with a track being kept and silencing the slot would silence both.
    mid_src = repo / 'sound/songs/midi' / (song + '.mid')
    per_track = note_tracks(bytearray(mid_src.read_bytes()))   # [(chunk, [progs])]
    slots = [(p[0] if p else None) for _, p in per_track]

    # A TRACK'S VOICE MUST COVER EVERY SLOT IT EVER SELECTS. Tracks change
    # program part way through, and a slot no plan named keeps whatever filler
    # the source voicegroup had -- a full-sustain 50% square that then competes
    # for square 1 with the melody. Naming only the first program is what made
    # petalburg_woods, route119 and route111 sound nothing like their previews.
    #
    # Kept parts are written first so that where a kept and a dropped track
    # share a slot, the kept one wins and the part is still heard.
    vg_voices = {}
    dropped_slots = []
    for idx, (_, progs) in enumerate(per_track):
        first = progs[0] if progs else None
        v = voices.get('#%d' % idx)
        if v is None and first is not None:
            v = voices.get(str(first))
        if v is None:
            continue
        if v['kind'] == 'drop':
            dropped_slots.append((idx, progs))
            continue
        for slot in progs:
            vg_voices.setdefault(str(slot), v)

    for idx, progs in dropped_slots:
        for slot in progs:
            vg_voices.setdefault(str(slot), dict(voices.get('#%d' % idx)
                                                 or voices.get(str(progs[0]))))

    # Any slot named by the plan but selected by no track still gets written,
    # so a hand-tuned variant naming a slot directly is not lost.
    for key, v in voices.items():
        if not (isinstance(key, str) and key.startswith('#')):
            vg_voices.setdefault(str(key), v)

    if song not in HAND_TUNED:
        err = write_voicegroup(repo, song, vg_voices, stem)
        if err:
            return err
    else:
        log('voicegroup left alone (hand tuned)')

    # The include.
    vg_inc = repo / 'sound/voice_groups.inc'
    if ensure_line(vg_inc, 'gb_%s.inc' % stem,
                   '.include "sound/voicegroups/gb_%s.inc"\n' % stem):
        log('include added')

    # The duplicated MIDI and its build line.
    mid = repo / 'sound/songs/midi' / (song + '.mid')
    gb_mid = repo / 'sound/songs/midi' / (song + '_gb.mid')
    # ALWAYS re-copy from the original before editing. The edits below are not
    # idempotent -- transposing twice moves a part two octaves -- so the copy
    # has to start clean on every run.
    shutil.copyfile(mid, gb_mid)

    for slot_s, v in sorted(voices.items()):
        if isinstance(slot_s, str) and slot_s.startswith('#'):
            continue
        if v.get('transpose'):
            err = midi_transpose_slot(gb_mid, int(slot_s), v['transpose'])
            if err:
                return err
        if v.get('floor_lift'):
            err = midi_floor_lift_slot(gb_mid, int(slot_s))
            if err:
                return err
    # DESCENDING, and that is not a tidiness preference. Removing a track
    # renumbers every track after it, so dropping #0 and then #1 removes what
    # was originally #2 -- silently, with a well-formed result. Ascending order
    # only fails loudly when the last index runs off the end, which is how this
    # was noticed at all: mus_route119 errored while mus_sealed_chamber and
    # mus_victory_road would have shipped having dropped the wrong parts.
    drops = sorted((int(k[1:]) for k, v in voices.items()
                    if isinstance(k, str) and k.startswith('#')
                    and v['kind'] == 'drop'), reverse=True)
    for idx in drops:
        err = midi_drop_track(gb_mid, idx)
        if err:
            return err
    cfg = repo / 'sound/songs/midi/midi.cfg'
    if ensure_line(cfg, '%s_gb.mid:' % song,
                   '%s_gb.mid: -E -R50 -G_gb_%s -V080\n' % (song, stem)):
        log('midi.cfg entry added')

    # The GBS constant, inserted before the COUNT which then increments.
    songs_h = repo / 'include/constants/gbs_songs.h'
    txt = songs_h.read_text(encoding='utf-8')
    if const not in txt:
        m = re.search(r'#define GBS_MUSIC_COUNT (\d+)', txt)
        n = int(m.group(1))
        txt = txt.replace(m.group(0),
                          '#define %s %d\n\n#define GBS_MUSIC_COUNT %d'
                          % (const, n, n + 1))
        songs_h.write_text(txt, encoding='utf-8', newline='\n')
        log('%s = %d' % (const, n))

    # extern + table row.
    if ensure_line(repo / 'include/gbs_song_table.h', '%s_gb;' % song,
                   'extern const struct SongHeader %s_gb;\n' % song,
                   after='extern const struct SongHeader mus_petalburg_woods_gb;'):
        log('extern added')

    tbl = repo / 'src/gbs_song_table.c'
    txt = tbl.read_text(encoding='utf-8')
    if '%s_gb' % song not in txt:
        row = '    [%s] = SONG(%s_gb, 0),\n' % (const, song)
        idx = txt.rindex('};')
        txt = txt[:idx] + row + txt[idx:]
        tbl.write_text(txt, encoding='utf-8', newline='\n')
        log('song table row added')

    # The mapping that actually makes the toggle do something. Easiest thing to
    # forget, and it fails silently.
    st = repo / 'sound/song_table.inc'
    txt = st.read_text(encoding='utf-8')
    pat = re.compile(r'^(\tsong %s, [A-Z_0-9]+, )(\S+)$' % re.escape(song), re.M)
    m = pat.search(txt)
    if not m:
        return 'no song_table.inc row for %s' % song
    if m.group(2) != const:
        txt = pat.sub(lambda mm: mm.group(1) + const, txt, count=1)
        st.write_text(txt, encoding='utf-8', newline='\n')
        log('song_table row -> %s' % const)
    return None


def resilence(repo):
    """Apply the PSG-only invariant to voicegroups already on disk.

    THIS EXISTS BECAUSE THE ARRANGEMENTS CANNOT BE REGENERATED. wire() needs a
    plans.json, and the one that produced the thirty-five generated voicegroups
    was a working file that was never committed. wire() is also not idempotent
    for MIDI edits -- it transposes parts and drops tracks in the _gb.mid, so
    re-running it would apply both a second time.

    This pass touches the voicegroup and nothing else, and is idempotent: a
    silenced slot is itself a PSG voice, so a second run finds nothing to do.
    """
    changed = []
    for path in sorted((repo / 'sound/voicegroups').glob('gb_*.inc')):
        lines = path.read_text(encoding='utf-8').split('\n')
        out, slot, n, seen_label = [], -1, 0, False
        for line in lines:
            if re.match(r'\s*voice_group\s', line):
                seen_label = True
                out.append(line)
                continue
            if not seen_label or not re.match(r'\s*voice_\w+', line):
                out.append(line)
                continue
            slot += 1
            if is_psg_voice(line):
                out.append(line)
            else:
                out.append('\t@ slot %d: not in the arrangement and not a PSG '
                           'voice -- silenced' % slot)
                out.append('\t' + SILENT)
                n += 1
        if n:
            path.write_text('\n'.join(out), encoding='utf-8', newline='\n')
            changed.append((path.name, n))
    return changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    ap.add_argument('--plans', type=Path)
    ap.add_argument('--resilence', action='store_true',
                    help='silence non-PSG slots in existing gb_*.inc and exit')
    ap.add_argument('--songs', help='comma-separated plan names')
    ap.add_argument('--all', action='store_true')
    args = ap.parse_args()

    if args.resilence:
        changed = resilence(args.repo)
        for name, n in changed:
            print('%s: %d slot(s) silenced' % (name, n))
        print('\n%d voicegroup(s) changed' % len(changed))
        return 0

    if not args.plans:
        sys.exit('need --plans (or --resilence)')
    plans = json.loads(args.plans.read_text(encoding='utf-8'))
    if args.all:
        names = sorted(plans)
    elif args.songs:
        names = [s.strip() for s in args.songs.split(',') if s.strip()]
    else:
        sys.exit('need --songs or --all')

    bad = [n for n in names if n not in plans]
    if bad:
        sys.exit('not in %s: %s' % (args.plans, ', '.join(bad)))

    fails = 0
    for name in names:
        spec = plans[name]
        song = spec['song']
        const = 'GBS_MUSIC_%s_PSG' % name.upper()
        print('%s:' % song)
        err = wire(args.repo, song, spec, const)
        if err:
            print('    FAILED: %s' % err)
            fails += 1
    print('\n%d wired, %d failed' % (len(names) - fails, fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
