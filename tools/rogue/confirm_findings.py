#!/usr/bin/env python3
"""Re-derive the audit's findings from mid2agb's .s output instead of the .mid.

THE POINT
---------
audit_program_usage.py flagged five programs as carrying drum tracks. Those
findings came from a hand-rolled .mid parser. verify_program_audit.py showed that
parser agrees with mid2agb on 86% of songs exactly, with the remainder explained
by mid2agb dropping channels over its track limit, splitting polyphonic channels
across tracks, and clamping extreme pitches.

86% is not good enough to act on by itself. So this asks the same question of a
completely independent source -- the .s files mid2agb actually generated, which
are what becomes the ROM -- and reports whether the SAME programs get flagged.

A finding that appears in both readings is real regardless of the parser's edge
cases. A finding that appears in only one is an artefact, and must be dropped.

This is the check that decides whether the split in docs/PROGRAM_13_SPLIT.md
rests on anything.
"""

import collections
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from audit_program_usage import (song_programs, GM_DRUM_LO, GM_DRUM_HI,
                                 KIT_KEYS, DRUM_SHARE, CORE_SHARE, GM)
from verify_program_audit import parse_s

BANKS = ("all_instruments", "all_instruments_drums")


def songs_on_banks(repo):
    cfg = open(os.path.join(repo, "sound/songs/midi/midi.cfg")).read()
    out = set()
    for m in re.finditer(r"^(\S+)\.mid:\s+(.*)$", cfg, re.M):
        for b in BANKS:
            if re.search(r"-G_%s(?![A-Za-z0-9_])" % b, m.group(2)):
                out.add(m.group(1))
    return out


SEMI = {"Cn": 0, "Cs": 1, "Dn": 2, "Ds": 3, "En": 4, "Fn": 5,
        "Fs": 6, "Gn": 7, "Gs": 8, "An": 9, "As": 10, "Bn": 11}
NOTE_RE = re.compile(r"\b([A-G][ns])(-?\d+)\b")
VOICE_RE = re.compile(r"\.byte\s+VOICE\s*,\s*(\d+)")
TRACK_RE = re.compile(r"@\*+\s*Track\s+(\d+)")


def s_counts(path):
    """program -> Counter(pitch), counting every note OCCURRENCE in the .s.

    Deliberately NOT reduced to distinct pitches. The signal being measured is
    that a drum track strikes a handful of keys over and over; collapsing it to
    a set destroys exactly that and makes a kick-snare-hat pattern look like a
    three-note melody. mid2agb's pattern compression means these counts are not
    equal to the raw MIDI counts -- but each source is scored against its own
    total, so the SHARE is what is compared, and that survives compression.
    """
    out = collections.defaultdict(collections.Counter)
    voice = 0
    for line in open(path):
        if TRACK_RE.search(line):
            voice = 0
            continue
        m = VOICE_RE.search(line)
        if m:
            voice = int(m.group(1))
            continue
        if ".byte" not in line:
            continue
        for m in NOTE_RE.finditer(line):
            out[voice][24 + 12 * int(m.group(2)) + SEMI[m.group(1)]] += 1
    return out


def from_s(repo, labels):
    out = collections.defaultdict(collections.Counter)
    for label in sorted(labels):
        p = os.path.join(repo, "sound/songs/midi", label + ".s")
        if not os.path.exists(p):
            continue
        for prog, c in s_counts(p).items():
            out[prog].update(c)
    return out


def from_mid(repo, labels):
    out = collections.defaultdict(collections.Counter)
    for label in sorted(labels):
        p = os.path.join(repo, "sound/songs/midi", label + ".mid")
        if not os.path.exists(p):
            continue
        for prog, c in song_programs(p).items():
            out[prog].update(c)
    return out


def score(counter):
    n = sum(counter.values())
    if not n:
        return 0, 0.0, 0.0
    drum = sum(v for k, v in counter.items() if GM_DRUM_LO <= k <= GM_DRUM_HI) / n
    core = sum(v for k, v in counter.items() if k in KIT_KEYS) / n
    return n, drum, core


def flagged(data):
    out = {}
    for prog, c in data.items():
        n, drum, core = score(c)
        if n and drum >= DRUM_SHARE and core >= CORE_SHARE:
            out[prog] = (n, drum, core)
    return out


def main():
    repo = sys.argv[1] if len(sys.argv) > 1 else "."
    labels = songs_on_banks(repo)
    print("%d songs on the two all_instruments banks\n" % len(labels))

    a = flagged(from_mid(repo, labels))
    b = flagged(from_s(repo, labels))

    both = sorted(set(a) & set(b))
    only_mid = sorted(set(a) - set(b))
    only_s = sorted(set(b) - set(a))

    print("prog  GM name                    .mid says            .s says")
    print("----  -------------------------  -------------------  -------------------")
    for prog in sorted(set(a) | set(b)):
        gm = GM[prog] if prog < len(GM) else "?"
        fmt = lambda t: "%6d n %3.0f%%/%3.0f%%" % (t[0], 100 * t[1], 100 * t[2])
        sa = fmt(a[prog]) if prog in a else "        --         "
        sb = fmt(b[prog]) if prog in b else "        --         "
        mark = "  CONFIRMED" if prog in both else "  <-- ONLY ONE SOURCE"
        print("%4d  %-25s  %s  %s%s" % (prog, gm[:25], sa, sb, mark))

    print("\nCONFIRMED by both readings: %s" % (both or "none"))
    if only_mid:
        print("ONLY in the .mid parser (suspect -- likely artefact): %s" % only_mid)
    if only_s:
        print("ONLY in mid2agb's output (the .mid parser MISSED these): %s" % only_s)

    if not only_mid and not only_s:
        print("\nBoth readings agree exactly on which programs carry drum tracks.")
        return 0
    print("\nThe two readings disagree. Findings that appear in only one source"
          " must not be acted on without listening first.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
