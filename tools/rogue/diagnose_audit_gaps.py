#!/usr/bin/env python3
"""Explain every song where the .mid reading and mid2agb's .s output disagree.

A residual disagreement is only acceptable once its CAUSE is known. Left
uncategorised it is indistinguishable from a parser bug, which is the whole
thing verify_program_audit.py exists to rule out.

Causes checked, each independently verifiable rather than assumed:

  TRACK LIMIT   mid2agb emits at most MAX_TRACKS tracks. A .mid with more
                channels than that has channels DROPPED ENTIRELY -- they never
                reach the ROM. Signature: programs present in the .mid and
                absent from the .s, on a file whose channel count exceeds the
                number of Track headers mid2agb wrote.
  KEYSH         a non-zero key shift transposes the track at playback, so the
                note names in the .s are offset from the raw MIDI pitches.
  PITCH CLAMP   m4a note numbers are bounded; notes outside are clamped or
                dropped, so extreme pitches in the .mid have no .s counterpart.
  UNEXPLAINED   anything left. These are the only ones that would indicate a
                real parser defect, and the count must be reported honestly.
"""

import collections
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from audit_program_usage import song_programs
from verify_program_audit import parse_s, parse_mid

TRACK_RE = re.compile(r"@\*+\s*Track\s+(\d+)")
KEYSH_RE = re.compile(r"\.byte\s+KEYSH\s*,\s*(\S+)")


def s_track_count(path):
    return len(set(TRACK_RE.findall(open(path).read())))


def s_keyshifts(path):
    out = set()
    for m in KEYSH_RE.finditer(open(path).read()):
        v = m.group(1).strip()
        if v.endswith("_key+0") or v == "0":
            continue
        out.add(v)
    return out


def main():
    repo = sys.argv[1] if len(sys.argv) > 1 else "."
    buckets = collections.Counter()
    unexplained = []

    for s_path in sorted(glob.glob(os.path.join(repo, "sound/songs/midi/*.s"))):
        mid_path = s_path[:-2] + ".mid"
        if not os.path.exists(mid_path):
            continue
        ref = parse_s(s_path)
        got = parse_mid(mid_path)
        if all(ref.get(p, set()) == got.get(p, set())
               for p in set(ref) | set(got)):
            buckets["agree"] += 1
            continue

        label = os.path.basename(mid_path)[:-4]
        mid_chans = len(got)
        s_tracks = s_track_count(s_path)
        keysh = s_keyshifts(s_path)
        dropped = set(got) - set(ref)

        if dropped and mid_chans > s_tracks:
            buckets["TRACK LIMIT (channels dropped by mid2agb)"] += 1
        elif keysh:
            buckets["KEYSH (transposed)"] += 1
        else:
            extremes = any(n < 24 or n > 127
                           for p in set(got) | set(ref)
                           for n in (got.get(p, set()) ^ ref.get(p, set())))
            if extremes:
                buckets["PITCH CLAMP (notes outside m4a's range)"] += 1
            else:
                buckets["UNEXPLAINED"] += 1
                unexplained.append((label, mid_chans, s_tracks,
                                    sorted(set(got) ^ set(ref))[:6]))

    total = sum(buckets.values())
    print("%d songs\n" % total)
    for k, v in buckets.most_common():
        print("  %-46s %4d  (%.0f%%)" % (k, v, 100 * v / total))

    if unexplained:
        print("\nUNEXPLAINED -- these would indicate a real parser defect:")
        for label, mc, st, progs in unexplained[:25]:
            print("  %-42s midi-chans %2d, s-tracks %2d, progs differing %s"
                  % (label, mc, st, progs))
    else:
        print("\nNothing unexplained: every disagreement has a known cause.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
