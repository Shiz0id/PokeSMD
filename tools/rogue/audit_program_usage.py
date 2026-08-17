#!/usr/bin/env python3
"""Audit every GM program the imported songs use against what the bank provides.

Program 13 was found by accident -- a drum track pointed at a xylophone slot,
audible but invisible to every check in the tree. This asks the same question of
all 128 programs at once, so the next one is found by running something rather
than by noticing.

THREE FINDINGS, DELIBERATELY KEPT APART
---------------------------------------
They fail in different ways and have different fixes, and averaging them into one
"suspicion score" would hide all three.

1. DRUM-SHAPED USAGE ON A MELODIC SLOT -- the program 13 pattern. Notes cluster
   on the GM percussion map (35-81) and specifically on kick/snare/hat (36/38/42)
   while the slot is a pitched instrument. Fix: point those songs at a voicegroup
   with a kit at that slot (see docs/PROGRAM_13_SPLIT.md). This one is a JUDGMENT
   call -- confirm by ear before acting.

2. SLOT NAME vs GM PROGRAM NAME. voicegroup_all_instruments is a General MIDI
   bank, so slot N should be the GM instrument for program N. A slot whose name
   shares no word with its GM name means the bank is not GM-aligned there, and
   every song selecting that program gets something unintended. Reported for
   review rather than hard-failed, because the names are abbreviated by hand
   ("ac_grand_piano" for "Acoustic Grand Piano") and a fuzzy match would produce
   confident nonsense.

3. voice_keysplit_all INDEXED OUT OF RANGE. This one is objective, not taste.
   voice_keysplit_all maps note N to voice (N - base) with NO table in between,
   so a note outside [base, base+count) reads past the end of the sub-voicegroup
   -- garbage ToneData, not a wrong-but-valid instrument. (voice_keysplit slots
   cannot do this: decompile_voicegroup.py derives each sub-voicegroup's length
   from its keysplit table, so every index is in range by construction.)

    audit_program_usage.py <repo>
    audit_program_usage.py <repo> --all      # every used program, not just flagged
"""

import argparse
import collections
import glob
import os
import re
import struct
import sys

GM_DRUM_LO, GM_DRUM_HI = 35, 81
KIT_KEYS = {36, 38, 42}
DRUM_SHARE = 0.70
CORE_SHARE = 0.20

PACK_PREFIXES = ["modern", "bw", "hgss", "ranger", "xy", "guest", "nico",
                 "jorts", "aqua", "dppt", "pmd", "gm"]

GM = """Acoustic Grand Piano|Bright Acoustic Piano|Electric Grand Piano|Honky-tonk Piano
Electric Piano 1|Electric Piano 2|Harpsichord|Clavi|Celesta|Glockenspiel|Music Box
Vibraphone|Marimba|Xylophone|Tubular Bells|Dulcimer|Drawbar Organ|Percussive Organ
Rock Organ|Church Organ|Reed Organ|Accordion|Harmonica|Tango Accordion
Acoustic Guitar nylon|Acoustic Guitar steel|Electric Guitar jazz|Electric Guitar clean
Electric Guitar muted|Overdriven Guitar|Distortion Guitar|Guitar Harmonics
Acoustic Bass|Electric Bass finger|Electric Bass pick|Fretless Bass|Slap Bass 1
Slap Bass 2|Synth Bass 1|Synth Bass 2|Violin|Viola|Cello|Contrabass|Tremolo Strings
Pizzicato Strings|Orchestral Harp|Timpani|String Ensemble 1|String Ensemble 2
Synth Strings 1|Synth Strings 2|Choir Aahs|Voice Oohs|Synth Voice|Orchestra Hit
Trumpet|Trombone|Tuba|Muted Trumpet|French Horn|Brass Section|Synth Brass 1
Synth Brass 2|Soprano Sax|Alto Sax|Tenor Sax|Baritone Sax|Oboe|English Horn|Bassoon
Clarinet|Piccolo|Flute|Recorder|Pan Flute|Blown Bottle|Shakuhachi|Whistle|Ocarina
Lead 1 square|Lead 2 sawtooth|Lead 3 calliope|Lead 4 chiff|Lead 5 charang
Lead 6 voice|Lead 7 fifths|Lead 8 bass lead|Pad 1 new age|Pad 2 warm|Pad 3 polysynth
Pad 4 choir|Pad 5 bowed|Pad 6 metallic|Pad 7 halo|Pad 8 sweep|FX 1 rain
FX 2 soundtrack|FX 3 crystal|FX 4 atmosphere|FX 5 brightness|FX 6 goblins
FX 7 echoes|FX 8 sci-fi|Sitar|Banjo|Shamisen|Koto|Kalimba|Bag pipe|Fiddle|Shanai
Tinkle Bell|Agogo|Steel Drums|Woodblock|Taiko Drum|Melodic Tom|Synth Drum
Reverse Cymbal|Guitar Fret Noise|Breath Noise|Seashore|Bird Tweet|Telephone Ring
Helicopter|Applause|Gunshot""".replace("\n", "|").split("|")

STOP = {"1", "2", "3", "4", "5", "6", "7", "8", "the", "of"}
ABBREV = {"acoustic": "ac", "electric": "el", "orchestral": "orch",
          "percussive": "perc", "ensemble": "ens", "harmonics": "harmonics"}


def tokens(s):
    out = set()
    for w in re.split(r"[^a-z0-9]+", s.lower()):
        if not w or w in STOP:
            continue
        out.add(w)
        if w in ABBREV:
            out.add(ABBREV[w])
        for long, short in ABBREV.items():
            if w == short:
                out.add(long)
    return out


def read_varlen(d, i):
    v = 0
    while True:
        b = d[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not (b & 0x80):
            return v, i


def song_programs(path):
    """program -> Counter(pitch -> count)."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"MThd":
        return {}
    ntrk = struct.unpack(">H", d[10:12])[0]
    i = 14
    cur = {}
    out = collections.defaultdict(collections.Counter)
    for _ in range(ntrk):
        if d[i:i + 4] != b"MTrk":
            break
        ln = struct.unpack(">I", d[i + 4:i + 8])[0]
        end = i + 8 + ln
        j = i + 8
        status = 0
        while j < end:
            _, j = read_varlen(d, j)
            if j >= end:
                break
            b = d[j]
            if b & 0x80:
                status = b
                j += 1
            ev = status & 0xF0
            ch = status & 0x0F
            if status == 0xFF:
                j += 1
                ln2, j = read_varlen(d, j)
                j += ln2
            elif status in (0xF0, 0xF7):
                ln2, j = read_varlen(d, j)
                j += ln2
            elif ev == 0xC0:
                cur[ch] = d[j]
                j += 1
            elif ev == 0xD0:
                j += 1
            elif ev == 0x90:
                note, vel = d[j], d[j + 1]
                j += 2
                if vel > 0:
                    out[cur.get(ch, 0)][note] += 1
            else:
                j += 2
        i = end
    return out


def load_bank(repo, name):
    blob = ""
    for p in sorted(glob.glob(os.path.join(repo, "sound/voicegroups/**/*.inc"),
                              recursive=True)):
        blob += "\n" + open(p).read()
    m = re.search(r"^voice_group %s\b[^\n]*\n(.*?)(?=^voice_group |\Z)"
                  % re.escape(name), blob, re.M | re.S)
    if not m:
        return None, blob
    slots = []
    for line in m.group(1).splitlines():
        s = line.strip()
        if s.startswith("@") or not s:
            continue
        if s.startswith("voice_"):
            slots.append(s)
        if len(slots) == 128:
            break
    return slots, blob


def group_span(blob, name):
    m = re.search(r"^voice_group %s\s*(?:,\s*(\d+))?\s*$(.*?)(?=^voice_group |\Z)"
                  % re.escape(name), blob, re.M | re.S)
    if not m:
        return None
    base = int(m.group(1)) if m.group(1) else 0
    n = len([l for l in m.group(2).splitlines() if l.strip().startswith("voice_")])
    return base, n


def slot_label(slot):
    m = re.match(r"voice_keysplit(_all)? voicegroup_(\S+?),?$", slot)
    if m:
        return m.group(2)
    m = re.match(r"voice_keysplit voicegroup_(\S+),", slot)
    if m:
        return m.group(1)
    m = re.search(r"DirectSoundWaveData_(\w+)", slot)
    if m:
        return m.group(1)
    return slot.split()[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("repo_pos", nargs="?",
                    default=os.environ.get("POKEDECOMP_REPO", "."))
    ap.add_argument("--repo", dest="repo_kw")
    ap.add_argument("--all", action="store_true", help="list every used program")
    ap.add_argument("--bank", default="all_instruments",
                    help="which voicegroup to audit (default all_instruments;"
                         " pass all_instruments_drums for the songs moved there)")
    args = ap.parse_args()
    repo = args.repo_kw or args.repo_pos

    slots, blob = load_bank(repo, args.bank)
    if not slots:
        print("voice_group %s not found" % args.bank)
        return 1

    cfg = open(os.path.join(repo, "sound/songs/midi/midi.cfg")).read()
    on_bank = set()
    for m in re.finditer(r"^(\S+)\.mid:\s+(.*)$", cfg, re.M):
        # -G_all_instruments must not also match -G_all_instruments_drums, so
        # require the next character to be a non-name one.
        if re.search(r"-G_%s(?![A-Za-z0-9_])" % re.escape(args.bank), m.group(2)):
            on_bank.add(m.group(1))

    used = collections.defaultdict(collections.Counter)
    songs = collections.defaultdict(set)
    for p in sorted(glob.glob(os.path.join(repo, "sound/songs/midi/mus_*.mid"))):
        label = os.path.basename(p)[:-4]
        if label not in on_bank:
            continue
        for prog, pitches in song_programs(p).items():
            used[prog].update(pitches)
            songs[prog].add(label)

    print("%d songs on voicegroup_%s, using %d distinct programs\n"
          % (len(on_bank), args.bank, len(used)))

    drum_shaped, name_odd, oor = [], [], []
    rows = []
    for prog in sorted(used):
        c = used[prog]
        n = sum(c.values())
        drum = sum(v for k, v in c.items() if GM_DRUM_LO <= k <= GM_DRUM_HI) / n
        core = sum(v for k, v in c.items() if k in KIT_KEYS) / n
        slot = slots[prog] if prog < len(slots) else "<out of range>"
        label = slot_label(slot)
        gm = GM[prog] if prog < len(GM) else "?"
        rows.append((prog, n, drum, core, len(c), label, gm, len(songs[prog])))

        if drum >= DRUM_SHARE and core >= CORE_SHARE and not slot.startswith(
                "voice_keysplit_all"):
            drum_shaped.append((prog, n, drum, core, label, gm, sorted(songs[prog])))

        if not (tokens(label) & tokens(gm)):
            name_odd.append((prog, label, gm, n))

        if slot.startswith("voice_keysplit_all"):
            span = group_span(blob, label)
            if span:
                base, cnt = span
                bad = sum(v for k, v in c.items() if not (base <= k < base + cnt))
                if bad:
                    oor.append((prog, label, base, cnt, bad, n))

    print("=" * 78)
    print("1. DRUM-SHAPED USAGE ON A MELODIC SLOT  (the program 13 pattern)")
    print("=" * 78)
    if not drum_shaped:
        print("  none -- no other program looks like a misrouted drum track.")
    for prog, n, drum, core, label, gm, sl in drum_shaped:
        print("\n  program %3d  GM '%s'  slot voicegroup_%s" % (prog, gm, label))
        print("    %d notes, %.0f%% on GM drum keys, %.0f%% on kick/snare/hat,"
              " %d songs" % (n, 100 * drum, 100 * core, len(sl)))
        for s in sl[:12]:
            print("      %s" % s)
        if len(sl) > 12:
            print("      ... and %d more" % (len(sl) - 12))

    print("\n" + "=" * 78)
    print("2. SLOT NAME SHARES NO WORD WITH ITS GM PROGRAM NAME  (review by eye)")
    print("=" * 78)
    if not name_odd:
        print("  none -- every used slot's name matches its GM program.")
    for prog, label, gm, n in name_odd:
        print("  program %3d  slot '%s'  vs GM '%s'   (%d notes)"
              % (prog, label, gm, n))

    print("\n" + "=" * 78)
    print("3. voice_keysplit_all INDEXED OUT OF RANGE  (objective: reads garbage)")
    print("=" * 78)
    if not oor:
        print("  none -- every keysplit_all slot is played within its span.")
    for prog, label, base, cnt, bad, n in oor:
        print("  program %3d  voicegroup_%s covers %d-%d, but %d of %d notes"
              " fall outside" % (prog, label, base, base + cnt - 1, bad, n))

    if args.all:
        print("\n" + "=" * 78)
        print("ALL USED PROGRAMS")
        print("=" * 78)
        print("prog  notes  songs  drum%  kit%  pitches  slot / GM name")
        for prog, n, drum, core, np_, label, gm, ns in rows:
            print("%4d %6d %6d  %4.0f%% %5.0f%% %8d  %s / %s"
                  % (prog, n, ns, 100 * drum, 100 * core, np_, label, gm))
    return 0


if __name__ == "__main__":
    sys.exit(main())
