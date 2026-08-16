"""Derive a PSG-only voicegroup from an existing one, by slot.

WHAT THIS IS FOR. m4a can already drive the GBA's four legacy PSG channels --
asm/macros/music_voice.inc defines voice_square_1/2, voice_programmable_wave and
voice_noise, and most stock voicegroups already use them. So a song can be made
to play "in Game Boy voice" without transcribing a single note: point it at a
voicegroup whose entries are all CGB voices. No demixing required.

WHAT IT CANNOT DO, AND WHY THE MAPPING IS HAND-WRITTEN. There are four PSG
channels and m4a assigns them by voice TYPE -- every square_1 voice wants
hardware channel 1. Two tracks holding square_1 voices at once contend, and m4a
resolves that by priority stealing, which sounds like dropouts rather than like
an arrangement. mus_petalburg_woods has NINE note-carrying tracks and is sounding
five or more of them 27% of the time, so parts have to be dropped, and choosing
which is a musical decision no heuristic makes reliably. That is the same wall a
hand demix hits; this tool just makes the decision cheap to express and revert.

Dropping a part is spelled as a silent voice: attack/decay/sustain/release all
zero never reaches an audible level.

Usage:  python3 tools/rogue/make_gb_voicegroup.py [--repo PATH]
"""
import argparse
import re
import sys
from pathlib import Path

SRC = 'sound/voicegroups/petalburg_woods.inc'
OUT = 'sound/voicegroups/gb_woods.inc'
NAME = 'gb_woods'

SILENT = 'voice_square_1_alt 60, 0, 0, 3, 0, 0, 0, 0'

# Slot -> replacement, derived from a census of mus_petalburg_woods.mid
# (track/slot/range/notes) plus a measured overlap matrix. Slots not listed are
# left exactly as they were -- most of the group is unused filler.
#
# THIS IS THE "fuller" ARRANGEMENT, and it replaced a leaner first cut that kept
# the harmony and the bass-on-wave and was judged too empty on a listen. What
# the measurement showed: the counter line and the harmony sound for exactly the
# same 8.7 s of 42.2, so trading one for the other changes colour and not
# density. The space was missing because the two SUSTAINED parts -- piano at
# 27.6 s and strings at 20.4 s -- had both been dropped. So the strings pad
# takes the wave channel and the bass moves onto square 2 beside the counter
# line, which is idiomatic for GB music and collides far less than the harmony
# would have.
#
# Cost, measured rather than assumed: 20 of the 117 notes on square 2 start at
# the same instant as another and are never heard. Keep that in mind before
# adding anything else to that channel.
MAPPING = {
    # slot: (replacement, why)
    0:  ('voice_noise_alt 60, 0, 0, 0, 1, 0, 1',
         'track 9, drumset keysplit -> a short noise hit. Shares the noise '
         'channel with track 8; 8 of their 61 notes collide and are lost, '
         'which is acceptable for percussion.'),
    1:  (SILENT,
         'track 2, piano keysplit, 42 notes but sounding 27.6 s - the most '
         'CONTINUOUS part in the track. DROPPED only because there is no '
         'channel left; it is the first thing to try if this still feels thin.'),
    45: (SILENT,
         'track 1, pizzicato strings, 56 notes, sounding 6.5 s. DROPPED - '
         'sparse punctuation, the least costly thing to lose.'),
    48: ('voice_programmable_wave_alt 60, 0, ProgrammableWaveData_6, 1, 6, 14, 4',
         'track 7, strings keysplit, sounding 20.4 s. PROMOTED to the wave '
         'channel - a sustained pad is what the wave channel is for, and this '
         'is the part that fills the space the lean cut was missing.'),
    73: ('voice_square_1_alt 60, 0, 0, 2, 0, 3, 11, 1',
         'track 3, flute, range 74-99, 271 notes - FOUR TIMES any other part, '
         'so it is the melody and takes square 1 at 50% duty. DECAY 3 TO '
         'SUSTAIN 11 RATHER THAN A FLAT 15, and that is a fix not a taste: the '
         'track ends on a bare melody note held 1.33 s with every other part '
         'already stopped, and at sustain 15 a square sits at maximum for all '
         'of it and then the song loops. Audible in game as a bare tone at the '
         'loop seam. The sampled flute this replaces decays naturally, which is '
         'why vanilla has no such artifact. The duty is deliberately unchanged '
         'at 50% - the timbre was judged right on hardware.'),
    81: ('voice_square_2_alt 60, 0, 3, 0, 2, 11, 1',
         'track 6, was programmable_wave_alt, the BASS. Moved off the wave '
         'channel to square 2 at 75% duty so the strings pad can have the wave. '
         'A pulse bass is completely ordinary for GB music.'),
    82: ('voice_square_2_alt 60, 0, 1, 0, 1, 9, 1',
         'track 5, the COUNTER line, sounding 8.7 s. Was already a PSG part but '
         'held square 1, the channel the melody needs, so it moves to square 2 '
         'at 25% duty and shares with the bass above.'),
    80: (SILENT,
         'track 4, harmony, sounding 8.7 s. DROPPED in favour of the counter '
         'line, which occupies the same time for more notes. The two overlap '
         '68% of the time so they could not both have square 2.'),
    # 127 (track 8, noise_alt) is already correct and untouched.
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    args = ap.parse_args()

    src = (args.repo / SRC).read_text(encoding='utf-8').split('\n')

    header = src[0]
    if not header.startswith('voice_group '):
        sys.exit('%s does not start with a voice_group line' % SRC)

    out = ['@ GENERATED by tools/rogue/make_gb_voicegroup.py -- do not hand-edit.',
           '@',
           '@ A PSG-only rendering of voicegroup_petalburg_woods, so the same MIDI',
           '@ can play through the Game Boy sound channels with no transcription.',
           '@ See the tool for why four parts are dropped rather than remapped.',
           '',
           'voice_group %s' % NAME]

    slot = 0
    changed = 0
    for line in src[1:]:
        if not line.strip():
            continue
        if slot in MAPPING:
            repl, why = MAPPING[slot]
            for chunk in ('@ slot %d: %s' % (slot, why)).split('\n'):
                # wrap the reason at a sane width
                words, cur = chunk.split(), ''
                for w in words:
                    if len(cur) + len(w) + 1 > 76:
                        out.append('\t' + cur)
                        cur = '@ ' + w
                    else:
                        cur = (cur + ' ' + w).strip()
                out.append('\t' + cur)
            out.append('\t' + repl)
            changed += 1
        else:
            out.append(line.rstrip())
        slot += 1

    if changed != len(MAPPING):
        sys.exit('expected to replace %d slots but replaced %d -- the source '
                 'voicegroup changed shape' % (len(MAPPING), changed))

    out.append('')
    (args.repo / OUT).write_text('\n'.join(out), encoding='utf-8', newline='\n')
    print('wrote %s  (%d slots, %d replaced)' % (OUT, slot, changed))
    for s in sorted(MAPPING):
        kind = 'SILENT' if MAPPING[s][0] == SILENT else MAPPING[s][0].split()[0]
        print('  slot %3d -> %s' % (s, kind))
    return 0


if __name__ == '__main__':
    sys.exit(main())
