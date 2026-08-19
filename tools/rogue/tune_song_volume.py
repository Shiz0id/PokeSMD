"""Bring quiet imported songs up to vanilla's loudness, one -V per song.

THE PROBLEM. import_midi_pack.py writes a flat --volume 080 for every song it
wires. Vanilla does not do that: it tunes -V per song across 36 to 120, because
-V is a FLAT MASTER MULTIPLIER applied on top of whatever CC7 and note velocity
the .mid already carries. It is the compensation, not a constant.

So an import whose .mid was authored quiet stays quiet, and one authored loud
comes out hot, and both sit next to vanilla tracks the player already knows.
That is audible: the DP-style title swap landed 12% under the vanilla title it
replaced, and the two are the SAME SEQUENCE - identical note count, near
identical velocity - so the entire difference was this default.

THE MEASURE, and its limits. Loudness proxy is

    mean note velocity x mean CC7 / 127

and effective loudness is proxy * V / 128. It is a proxy for what the sequence
ASKS for, not for what comes out of the mixer: it knows nothing about sample
amplitude in the voicegroup, or about how many voices sound at once. Two songs
with the same effective figure can still differ if their banks differ. Use it to
find outliers, not to certify anything.

DO NOT AVERAGE ACROSS CATEGORIES. Jingles, stings and SE are quiet on purpose -
mus_level_up, the rain loops, menu blips - and pooling them with BGM drags the
median down and hides the real tail. Anything under MIN_NOTES is excluded, and
se_* is excluded outright.

THE CEILING IS REAL. _mvl is a u8 and vanilla never exceeds 120. A song needing
more than that cannot be fixed here at all; its velocities and CC7 have to be
scaled inside the .mid. Those are reported separately rather than silently
clamped, because clamping would look like a fix and would not be one.

VANILLA IS NEVER RETUNED. Only songs this project imported are touched, and the
target is vanilla's own median effective loudness - so the result is imports
that sit where vanilla sits, not imports pushed to some absolute.

Run:  python3 tools/rogue/tune_song_volume.py [repo] --report
      python3 tools/rogue/tune_song_volume.py [repo] --apply [--floor N]
      python3 tools/rogue/tune_song_volume.py --selftest
"""
import argparse
import re
import statistics
import struct
import sys
from pathlib import Path

MIN_NOTES = 50          # below this it is a jingle, not BGM
V_CEILING = 120         # vanilla's own highest -V
V_FLOOR = 36            # vanilla's own lowest
DEFAULT_FLOOR = 0.90    # retune anything under 90% of the vanilla median

# Prefixes this project imported. Everything else is vanilla or FRLG/RSE stock
# and is left alone.
IMPORTED = ("mus_bw_", "mus_hgss_", "mus_dppt_", "mus_pmd_", "mus_ranger_",
            "mus_xy_", "mus_modern_", "mus_guest_", "mus_jorts_", "mus_nico_",
            "mus_aqua_", "mus_gm_")


def proxy(path):
    """(mean velocity, mean CC7, proxy, note count) for one MIDI."""
    d = path.read_bytes()
    i = 0
    vels = []
    cc7 = []
    while i < len(d) - 8:
        tag = d[i:i + 4]
        if tag not in (b'MThd', b'MTrk'):
            i += 1
            continue
        ln = struct.unpack('>I', d[i + 4:i + 8])[0]
        if tag == b'MTrk':
            c = d[i + 8:i + 8 + ln]
            j = 0
            st = 0
            while j < len(c):
                while j < len(c) and c[j] & 0x80:
                    j += 1
                j += 1
                if j >= len(c):
                    break
                b = c[j]
                if b == 0xFF:
                    j += 2
                    L = 0
                    while c[j] & 0x80:
                        L = (L | (c[j] & 0x7F)) << 7
                        j += 1
                    L |= c[j]
                    j += 1 + L
                    continue
                if b in (0xF0, 0xF7):
                    j += 1
                    L = 0
                    while c[j] & 0x80:
                        L = (L | (c[j] & 0x7F)) << 7
                        j += 1
                    L |= c[j]
                    j += 1 + L
                    continue
                if b & 0x80:
                    st = b
                    j += 1
                e = st & 0xF0
                if e == 0x90 and c[j + 1] > 0:
                    vels.append(c[j + 1])
                elif e == 0xB0 and c[j] == 7:
                    cc7.append(c[j + 1])
                j += 1 if e in (0xC0, 0xD0) else 2
        i += 8 + ln
    if not vels:
        return 0, 0, 0, 0
    mv = sum(vels) / len(vels)
    mc = sum(cc7) / len(cc7) if cc7 else 127.0
    return mv, mc, mv * mc / 127.0, len(vels)


CFG_RE = re.compile(r'^(?P<name>\S+)\.mid:(?P<pre>\s*.*?)-V(?P<v>\d+)(?P<post>.*)$')


def read_cfg(repo):
    """[(line_index, name, volume, raw_line)] for every song in midi.cfg."""
    path = repo / "sound/songs/midi/midi.cfg"
    rows = []
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(True)):
        m = CFG_RE.match(line)
        if m:
            rows.append((n, m.group("name"), int(m.group("v")), line))
    return path, rows


def survey(repo):
    """Every BGM song with its proxy and effective loudness, split by origin."""
    _, rows = read_cfg(repo)
    midi = repo / "sound/songs/midi"
    out = []
    for idx, name, vol, _ in rows:
        if name.startswith("se_"):
            continue
        f = midi / f"{name}.mid"
        if not f.exists():
            continue
        mv, mc, p, n = proxy(f)
        if n < MIN_NOTES:
            continue
        out.append({
            "i": idx, "name": name, "v": vol, "proxy": p, "notes": n,
            "eff": p * vol / 128.0,
            "imported": name.startswith(IMPORTED),
        })
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo", nargs="?", default=None)
    ap.add_argument("--repo", dest="repo_kw", default=None)
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--floor", type=float, default=DEFAULT_FLOOR)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    repo = Path(args.repo_kw or args.repo or Path(__file__).resolve().parents[2])

    songs = survey(repo)
    vanilla = [s for s in songs if not s["imported"]]
    imported = [s for s in songs if s["imported"]]
    if not vanilla or not imported:
        print("could not split vanilla from imported; check the IMPORTED prefixes")
        return 1

    target = statistics.median(s["eff"] for s in vanilla)
    cutoff = target * args.floor

    print(f"{len(songs)} BGM songs ({len(vanilla)} vanilla, {len(imported)} imported), "
          f"jingles under {MIN_NOTES} notes excluded")
    print(f"vanilla median effective loudness {target:.1f}; "
          f"retuning imports below {cutoff:.1f} ({args.floor:.0%})")

    # Per-group medians, so a whole pack that is quiet is visible as one fact
    # rather than as thirty separate lines.
    print("\nby group (median effective):")
    groups = {}
    for s in imported:
        g = next((p for p in IMPORTED if s["name"].startswith(p)), "other")
        groups.setdefault(g, []).append(s["eff"])
    for g, vals in sorted(groups.items(), key=lambda kv: statistics.median(kv[1])):
        med = statistics.median(vals)
        mark = "  <-- under vanilla" if med < cutoff else ""
        print(f"  {g:<14} {len(vals):>4} songs   median {med:>5.1f}{mark}")

    quiet = sorted((s for s in imported if s["eff"] < cutoff),
                   key=lambda s: s["eff"])
    fixable, unfixable = [], []
    for s in quiet:
        if s["proxy"] <= 0:
            continue
        want = target * 128.0 / s["proxy"]
        if want > V_CEILING:
            unfixable.append((s, want))
        else:
            fixable.append((s, max(V_FLOOR, int(round(want)))))

    print(f"\n{len(quiet)} imported songs under the cutoff: "
          f"{len(fixable)} fixable by -V, {len(unfixable)} not")

    if unfixable:
        print(f"\nCANNOT be fixed by -V (would need over {V_CEILING}); these need"
              f" velocity/CC7 scaled inside the .mid:")
        for s, want in unfixable:
            print(f"  {s['name']:<40} eff {s['eff']:>5.1f}  would need -V{want:.0f}")

    if fixable:
        print(f"\nretune ({'applying' if args.apply else 'proposed'}):")
        for s, v in fixable:
            print(f"  {s['name']:<40} eff {s['eff']:>5.1f} -> "
                  f"{s['proxy'] * v / 128.0:>5.1f}   -V{s['v']:03d} -> -V{v:03d}")

    if args.apply and fixable:
        path, _ = read_cfg(repo)
        lines = path.read_text(encoding="utf-8").splitlines(True)
        for s, v in fixable:
            lines[s["i"]] = re.sub(r'-V\d+', f'-V{v:03d}', lines[s["i"]], count=1)
        path.write_text("".join(lines), newline="\n", encoding="utf-8")
        print(f"\nwrote {len(fixable)} changes to sound/songs/midi/midi.cfg")
    return 0


def selftest():
    """Prove the measure and the arithmetic, on constructed cases."""
    import tempfile
    import os

    def build(vel, cc, n=100):
        track = b''
        if cc is not None:
            track += b'\x00\xB0\x07' + bytes([cc])
        for _ in range(n):
            track += b'\x00\x90\x40' + bytes([vel]) + b'\x30\x80\x40\x00'
        track += b'\x00\xFF\x2F\x00'
        return (b'MThd' + struct.pack('>I', 6) + b'\x00\x00\x00\x01\x00\x60'
                + b'MTrk' + struct.pack('>I', len(track)) + track)

    ok = 0
    with tempfile.TemporaryDirectory() as td:
        p = Path(td) / "t.mid"

        # 1. proxy = velocity * cc7 / 127
        p.write_bytes(build(100, 127))
        _, _, pr, n = proxy(p)
        fired = abs(pr - 100.0) < 0.01 and n == 100
        print(f'  {"ok     " if fired else "*** FAILED ***"}  proxy is velocity x CC7 / 127 (got {pr:.2f})')
        ok += fired

        # 2. Half the CC7, half the proxy.
        p.write_bytes(build(100, 64))
        _, _, pr2, _ = proxy(p)
        fired = abs(pr2 - 100 * 64 / 127) < 0.01
        print(f'  {"ok     " if fired else "*** FAILED ***"}  CC7 scales the proxy (got {pr2:.2f})')
        ok += fired

        # 3. No CC7 at all means full scale, not zero - a song that never sends
        #    volume is at 127, and treating it as silent would rank it as the
        #    quietest thing in the ROM and "fix" it by making it deafening.
        p.write_bytes(build(80, None))
        _, mc, pr3, _ = proxy(p)
        fired = mc == 127.0 and abs(pr3 - 80.0) < 0.01
        print(f'  {"ok     " if fired else "*** FAILED ***"}  absent CC7 counts as 127, not 0 (got {pr3:.2f})')
        ok += fired

        # 4. Jingles are excluded by note count, not by name.
        p.write_bytes(build(100, 127, n=10))
        _, _, _, n4 = proxy(p)
        fired = n4 < MIN_NOTES
        print(f'  {"ok     " if fired else "*** FAILED ***"}  a 10-note file is under the jingle cutoff')
        ok += fired

    print(f'\n{ok}/4 selftest cases behave')
    return 0 if ok == 4 else 1


if __name__ == "__main__":
    sys.exit(main())
