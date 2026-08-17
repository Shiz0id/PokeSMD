#!/usr/bin/env python3
"""Measure how loud a .mid actually is, and tune its -V in midi.cfg to match.

WHY THIS EXISTS
---------------
mid2agb's -V is a FLAT master multiplier (it becomes <label>_mvl in the .s)
applied on top of whatever CC7 and note velocity the .mid already contains.
Vanilla tunes it PER SONG across 36-120 precisely because the source files were
mastered at different levels -- -V is the compensation, not a constant.

import_midi_pack.py writes --volume 080 for every song it imports, so an
arrangement authored quiet stays quiet and one authored hot stays hot. That is
why the jorts pack sits well below everything else while BW sits above it.

THE METRIC, and why it is BGM-only
----------------------------------
loudness = mean(note velocity) * mean(CC7 at that note) / 127

Sound effects, rain ambience and menu blips are quiet ON PURPOSE, so pooling
se_* with mus_* drags the median down and hides the real outliers. This tool
looks at mus_* only, and skips anything under --min-notes as a jingle rather
than a track. Averaging across those categories is the classic way to make this
measurement say nothing.

THE CEILING IS REAL
-------------------
_mvl is a u8 and vanilla never exceeds 120. A song far enough below target
cannot be rescued by -V at all -- it needs velocity/CC7 scaling inside the .mid.
Those are reported separately rather than silently clamped, because a clamped
row looks fixed and is not.

    tune_midi_volume.py <repo> --report
    tune_midi_volume.py <repo> --pack jorts --dry-run
    tune_midi_volume.py <repo> --pack jorts --apply
"""

import argparse
import glob
import os
import re
import struct
import sys

V_CEILING = 120          # vanilla's highest, and _mvl is a u8
DEFAULT_MIN_NOTES = 50   # below this it is a jingle or a stinger, not BGM

PACK_PREFIXES = ["bw", "hgss", "ranger", "xy", "oras", "usum", "swsh", "guest",
                 "nico", "jorts", "aqua", "dppt", "dp", "pmd", "gm"]


def read_varlen(d, i):
    v = 0
    while True:
        b = d[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not (b & 0x80):
            return v, i


def measure(path):
    """(loudness, mean_velocity, mean_cc7, note_count, channels) or None."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"MThd":
        return None
    ntrk = struct.unpack(">H", d[10:12])[0]
    i = 14
    vels = []
    cc7 = {}
    cc7_at_note = []
    channels = set()
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
            elif ev in (0xC0, 0xD0):
                j += 1
            elif ev == 0x90:
                vel = d[j + 1]
                j += 2
                if vel > 0:
                    vels.append(vel)
                    cc7_at_note.append(cc7.get(ch, 100))
                    channels.add(ch)
            elif ev == 0xB0:
                cc, val = d[j], d[j + 1]
                j += 2
                if cc == 7:
                    cc7[ch] = val
            else:
                j += 2
        i = end
    if not vels:
        return None
    mv = sum(vels) / len(vels)
    mc = sum(cc7_at_note) / len(cc7_at_note)
    return mv * mc / 127.0, mv, mc, len(vels), len(channels)


def pack_of(label):
    if not label.startswith("mus_"):
        return None
    rest = label[4:]
    for p in PACK_PREFIXES:
        if rest.startswith(p + "_"):
            return p
    return "vanilla"


def parse_cfg(text):
    """label -> (full_line, current_V). Order preserved by the caller's regex."""
    out = {}
    for m in re.finditer(r"^(\S+)\.mid:\s+(.*)$", text, re.M):
        v = re.search(r"-V(\d+)", m.group(2))
        out[m.group(1)] = (m.group(0), int(v.group(1)) if v else None)
    return out


def survey(repo, min_notes):
    cfg = parse_cfg(open(os.path.join(repo, "sound/songs/midi/midi.cfg")).read())
    rows = []
    for p in sorted(glob.glob(os.path.join(repo, "sound/songs/midi/*.mid"))):
        label = os.path.basename(p)[:-4]
        pack = pack_of(label)
        if pack is None or label not in cfg:
            continue
        r = measure(p)
        if r is None:
            continue
        loud, mv, mc, n, nch = r
        if n < min_notes:
            continue
        V = cfg[label][1]
        if V is None:
            continue
        rows.append(dict(label=label, pack=pack, loud=loud, vel=mv, cc7=mc,
                         notes=n, chans=nch, V=V, eff=loud * V / 128.0))
    return rows


def median(xs):
    xs = sorted(xs)
    if not xs:
        return 0.0
    return xs[len(xs) // 2]


def do_report(rows):
    packs = {}
    for r in rows:
        packs.setdefault(r["pack"], []).append(r)
    target = median([r["loud"] for r in packs.get("vanilla", [])])
    print("target = vanilla median MIDI loudness = %.1f\n" % target)
    print("pack       n   median-loud   median-V   median-eff")
    print("-------  ---  ------------  ---------  -----------")
    for p in sorted(packs, key=lambda k: median([r["loud"] for r in packs[k]])):
        rs = packs[p]
        print("%-7s  %3d  %12.1f  %9d  %11.1f"
              % (p, len(rs), median([r["loud"] for r in rs]),
                 median([r["V"] for r in rs]), median([r["eff"] for r in rs])))
    return target


def plan(rows, pack, target):
    """Rows needing a change, each with the -V that would hit target."""
    out = []
    for r in rows:
        if pack and r["pack"] != pack:
            continue
        if r["loud"] >= target:
            continue
        want = int(round(r["V"] * target / r["loud"]))
        capped = min(want, V_CEILING)
        if capped == r["V"]:
            continue
        out.append(dict(r, want=want, newV=capped, clamped=want > V_CEILING))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("repo", nargs="?", default=os.environ.get("POKEDECOMP_REPO", "."))
    ap.add_argument("--repo", dest="repo_kw")
    ap.add_argument("--pack", help="only this pack, e.g. jorts")
    ap.add_argument("--target", type=float,
                    help="loudness to aim for (default: vanilla BGM median)")
    ap.add_argument("--min-notes", type=int, default=DEFAULT_MIN_NOTES)
    ap.add_argument("--report", action="store_true", help="survey only")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args()

    repo = args.repo_kw or args.repo
    rows = survey(repo, args.min_notes)
    if not rows:
        print("no songs measured -- wrong repo?")
        return 1

    target = args.target if args.target else do_report(rows)
    if args.report:
        return 0

    changes = plan(rows, args.pack, target)
    if not changes:
        print("\nnothing to change.")
        return 0

    print("\n%d songs to retune (target loudness %.1f):\n" % (len(changes), target))
    print("  loud   -V -> new   song")
    print("  -----  ----------  ----")
    clamped = []
    for c in sorted(changes, key=lambda c: c["loud"]):
        flag = "  CLAMPED (wanted %d)" % c["want"] if c["clamped"] else ""
        print("  %5.1f  %3d -> %3d   %s%s"
              % (c["loud"], c["V"], c["newV"], c["label"], flag))
        if c["clamped"]:
            clamped.append(c)

    if clamped:
        print("\n%d hit the -V ceiling of %d and are STILL below target."
              % (len(clamped), V_CEILING))
        print("-V cannot fix these; they need velocity/CC7 scaling in the .mid:")
        for c in clamped:
            print("   %s (would need -V%d)" % (c["label"], c["want"]))

    if not args.apply:
        print("\n(dry run -- pass --apply to write midi.cfg)")
        return 0

    path = os.path.join(repo, "sound/songs/midi/midi.cfg")
    text = open(path).read()
    for c in changes:
        pat = re.compile(r"^(%s\.mid:\s+.*?)-V\d+" % re.escape(c["label"]), re.M)
        text, n = pat.subn(lambda m: m.group(1) + "-V%03d" % c["newV"], text, count=1)
        if n != 1:
            print("FAILED to rewrite %s" % c["label"])
            return 1
    with open(path, "w", newline="\n") as f:
        f.write(text)
    print("\nwrote %d rows to sound/songs/midi/midi.cfg" % len(changes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
