#!/usr/bin/env python3
"""Cross-validate audit_program_usage.py's MIDI parser against mid2agb itself.

WHY THIS EXISTS
---------------
audit_program_usage.py reads the .mid files with a hand-rolled parser and draws
conclusions about which GM program each note sounds under. If that parser is
wrong -- running status, meta-event lengths, note-on-with-velocity-0, anything --
then every finding it reports is noise that looks like data. Nothing else in the
tree would catch that.

mid2agb is the ground truth: it is the program that actually converts these files
for the ROM, and `make` leaves its output beside each .mid as a .s. So parse the
.s and compare. Two independent readings of the same file agreeing is worth far
more than either one looking plausible.

WHAT IS COMPARED, AND WHY IT IS SETS AND NOT COUNTS
--------------------------------------------------
mid2agb compresses repeated bars into PATT/PEND patterns, so a note written once
in the .s may sound many times. Note COUNTS therefore legitimately differ and
comparing them would produce a wall of false alarms. What must agree is the
STRUCTURE: which programs appear, and which pitches sound under each. That is
exactly what the audit's conclusions rest on.

Note names come from sound/MPlayDef.s: Cn0 = 24, +12 per octave.

    verify_program_audit.py <repo>
    verify_program_audit.py <repo> --verbose      # per-song detail
    verify_program_audit.py <repo> --selftest     # break the parser, require a catch
"""

import argparse
import collections
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from audit_program_usage import song_programs, GM_DRUM_LO, GM_DRUM_HI

# Disagreements allowed before this reports failure. NOT a fudge factor to be
# raised until things pass: it is the measured residue from mid2agb restructuring
# tracks -- dropping channels past its limit, splitting polyphonic channels
# across several m4a tracks, clamping extreme pitches -- categorised by
# diagnose_audit_gaps.py. Raising it is only correct alongside a diagnosis of the
# new cases. If it needs raising and you cannot say why, the parser has broken.
BASELINE_MAX = 115

SEMI = {"Cn": 0, "Cs": 1, "Dn": 2, "Ds": 3, "En": 4, "Fn": 5,
        "Fs": 6, "Gn": 7, "Gs": 8, "An": 9, "As": 10, "Bn": 11}
# m4a uses RUNNING STATUS for notes as well as for commands, so a note line may
# carry no Nxx at all:
#       .byte   N08   , Dn1 , v112
#       .byte           Dn1 , v112      <- same note command, omitted
# Matching only the first form silently drops most of the notes in the file and
# makes the .mid parser look wrong. Match the note NAME anywhere on a .byte
# line instead -- no m4a command mnemonic has the shape [A-G][ns]<digits>, so
# this cannot collide with W08, VOL, PAN, MOD, VOICE, TIE, GOTO or PATT.
NOTE_RE = re.compile(r"\b([A-G][ns])(-?\d+)\b")
VOICE_RE = re.compile(r"\.byte\s+VOICE\s*,\s*(\d+)")
TRACK_RE = re.compile(r"@\*+\s*Track\s+(\d+)")


def note_number(name, octave):
    return 24 + 12 * int(octave) + SEMI[name]


def parse_s(path):
    """program -> set(pitch), read from mid2agb's own output."""
    out = collections.defaultdict(set)
    voice = 0
    for line in open(path):
        if TRACK_RE.search(line):
            voice = 0            # a new track starts with no VOICE selected yet
            continue
        m = VOICE_RE.search(line)
        if m:
            voice = int(m.group(1))
            continue
        if ".byte" not in line:
            continue
        for m in NOTE_RE.finditer(line):
            out[voice].add(note_number(m.group(1), m.group(2)))
    return out


def parse_mid(path):
    """program -> set(pitch), from the audit's own parser."""
    return {p: set(c) for p, c in song_programs(path).items() if c}


def compare(repo, verbose=False, mangle=None):
    """Returns (songs, agree, disagreements)."""
    disagree = []
    n = agree = 0
    for s_path in sorted(glob.glob(os.path.join(repo, "sound/songs/midi/*.s"))):
        mid_path = s_path[:-2] + ".mid"
        if not os.path.exists(mid_path):
            continue
        n += 1
        ref = parse_s(s_path)
        got = parse_mid(mid_path)
        if mangle:
            got = mangle(got)

        label = os.path.basename(mid_path)[:-4]
        probs = []

        missing = set(ref) - set(got)
        extra = set(got) - set(ref)
        if missing:
            probs.append("programs mid2agb uses that we miss: %s"
                         % sorted(missing))
        if extra:
            probs.append("programs we report that mid2agb does not: %s"
                         % sorted(extra))

        for prog in sorted(set(ref) & set(got)):
            miss = ref[prog] - got[prog]
            ext = got[prog] - ref[prog]
            if miss or ext:
                probs.append("program %d pitches differ (missing %s, extra %s)"
                             % (prog, sorted(miss)[:8], sorted(ext)[:8]))

        if probs:
            disagree.append((label, probs))
            if verbose:
                print("  MISMATCH %s" % label)
                for p in probs:
                    print("      %s" % p)
        else:
            agree += 1
    return n, agree, disagree


# --------------------------------------------------------------------------
# Self-test. Break the reading on purpose, four ways, and require each to show
# up as a disagreement -- otherwise this comparison proves nothing.
# --------------------------------------------------------------------------

SELFTEST_BREAKS = [
    ("a program dropped entirely",
     lambda g: {p: v for p, v in g.items() if p != sorted(g)[0]} if g else g),
    ("a program invented that is not in the file",
     lambda g: {**g, 125: {60, 61}}),
    ("every pitch shifted by an octave (a note-table error)",
     lambda g: {p: {n + 12 for n in v} for p, v in g.items()}),
    ("notes attributed to the wrong program (an off-by-one VOICE)",
     lambda g: {(p + 1) % 128: v for p, v in g.items()}),
]


def selftest(repo):
    # The baseline is deliberately NOT required to be zero. mid2agb drops
    # channels past its track limit, splits polyphonic channels across tracks
    # and clamps extreme pitches, so a residue of real disagreement exists and
    # is explained by diagnose_audit_gaps.py. Demanding a clean baseline here
    # would mean either never running the selftest or fudging the comparison
    # until it passed. Instead: a break must make things MEASURABLY worse.
    n, agree, dis = compare(repo)
    base = len(dis)
    print("baseline: %d of %d songs agree, %d disagree (see"
          " diagnose_audit_gaps.py)\n" % (agree, n, base))

    failures = 0
    for name, mangle in SELFTEST_BREAKS:
        _, _, d = compare(repo, mangle=mangle)
        if len(d) > base:
            print("  ok %s -- disagreements %d -> %d" % (name, base, len(d)))
        else:
            print("  NO %s -- NOT CAUGHT (still %d)" % (name, len(d)))
            failures += 1
    if failures:
        print("\n%d of %d breaks went undetected." % (failures, len(SELFTEST_BREAKS)))
        return 1
    print("\nAll %d deliberate breaks were caught." % len(SELFTEST_BREAKS))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("repo_pos", nargs="?",
                    default=os.environ.get("POKEDECOMP_REPO", "."))
    ap.add_argument("--repo", dest="repo_kw")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    repo = args.repo_kw or args.repo_pos

    if args.selftest:
        return selftest(repo)

    n, agree, dis = compare(repo, verbose=args.verbose)
    if not n:
        print("no .s files found -- run make first, mid2agb writes them"
              " beside each .mid")
        return 1

    print("%d songs compared, %d agree, %d disagree" % (n, agree, len(dis)))
    if len(dis) > BASELINE_MAX:
        print("\nFAIL: %d disagreements, over the known baseline of %d."
              " Something changed in the parser or in the songs -- run"
              " diagnose_audit_gaps.py to categorise the new ones."
              % (len(dis), BASELINE_MAX))
        for label, probs in dis[:20]:
            print("  %-42s %s" % (label, probs[0]))
        return 1
    print("OK: within the known baseline of %d. mid2agb restructures tracks"
          " (drops channels past its limit, splits polyphonic channels, clamps"
          " extreme pitches), so a residue is expected -- but the audit's"
          " findings do not rest on this: confirm_findings.py re-derives them"
          " from mid2agb's output directly." % BASELINE_MAX)
    return 0


if __name__ == "__main__":
    sys.exit(main())
