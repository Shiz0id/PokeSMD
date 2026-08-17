#!/usr/bin/env python3
"""Prove a decompiled voicegroup rebuilds to the bytes it came from.

decompile_voicegroup.py claims its .wav files are SOURCE for the samples it read
out of a ROM. This is the assertion behind that claim: run the repo's own
wav2agb over each .wav and require the result to equal, byte for byte, the
WaveData the decompiler saw. The expected bytes are recorded as a sha256 in the
manifest, so this needs only the repo -- never the patched ROM, which cannot be
committed.

WHY A HASH AND NOT A LISTEN: a wrong sample rate, an off-by-one loop end, or a
dropped loop flag are all inaudible as CAUSES. They present as "this instrument
sounds slightly off" three songs later, and by then the suspicion falls on the
arrangement. Either the bytes match or the decompile is wrong.

Usage:
    check_voicegroup_roundtrip.py [repo]
    check_voicegroup_roundtrip.py --repo PATH
    check_voicegroup_roundtrip.py --selftest

Takes the repo BOTH positionally and via --repo, so it cannot end up on the
wrong side of run_all_checks.sh's hand-maintained list.
"""

import argparse
import glob
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile


def find_manifests(repo):
    return sorted(glob.glob(os.path.join(repo, "tools", "rogue",
                                         "*_manifest.json")))


def wav2agb_path(repo):
    for name in ("wav2agb", "wav2agb.exe"):
        p = os.path.join(repo, "tools", "wav2agb", name)
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    return None


def run_wav2agb(tool, wav_path, out_path):
    proc = subprocess.run([tool, "-b", wav_path, out_path],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        return proc.stderr.strip() or "wav2agb exited %d" % proc.returncode
    return None


def check(repo, wav_override=None):
    """Returns (problems, checked_count)."""
    problems = []
    checked = 0

    tool = wav2agb_path(repo)
    if tool is None:
        return (["tools/wav2agb/wav2agb is not built -- run `make tools` first;"
                 " this check cannot verify anything without it"], 0)

    manifests = find_manifests(repo)
    if not manifests:
        return (["no *_manifest.json in tools/rogue -- nothing has been"
                 " decompiled, so there is nothing to verify"], 0)

    smp_dir = os.path.join(repo, "sound", "direct_sound_samples")

    with tempfile.TemporaryDirectory() as tmp:
        for man_path in manifests:
            with open(man_path) as f:
                man = json.load(f)

            group = man.get("voicegroup", os.path.basename(man_path))

            for name, meta in sorted(man.get("samples", {}).items()):
                wav = os.path.join(smp_dir, name + ".wav")

                # The selftest swaps one file in without touching the repo.
                if wav_override and name == wav_override[0]:
                    wav = wav_override[1]

                if not os.path.isfile(wav):
                    problems.append("%s: %s.wav is missing" % (group, name))
                    continue

                out = os.path.join(tmp, name + ".bin")
                err = run_wav2agb(tool, wav, out)
                if err:
                    problems.append("%s: wav2agb failed on %s.wav: %s"
                                    % (group, name, err))
                    continue

                with open(out, "rb") as f:
                    got = f.read()

                checked += 1

                want_len = meta.get("bin_bytes")
                if want_len is not None and len(got) != want_len:
                    problems.append(
                        "%s: %s rebuilt to %d bytes, the ROM had %d -- a loop"
                        " end or sample count is wrong"
                        % (group, name, len(got), want_len))
                    continue

                got_hash = hashlib.sha256(got).hexdigest()
                if got_hash != meta["bin_sha256"]:
                    # Say WHERE it diverged; "hash differs" sends you nowhere.
                    detail = ""
                    if len(got) >= 16:
                        import struct
                        flags, pitch, ls, le = struct.unpack_from("<IIII", got)
                        if pitch != meta["freq"]:
                            detail = (" -- pitch is %d, should be %d"
                                      % (pitch, meta["freq"]))
                        elif ls != meta["loop_start"]:
                            detail = (" -- loop start is %d, should be %d"
                                      % (ls, meta["loop_start"]))
                        elif le != meta["size"]:
                            detail = (" -- loop end is %d, should be %d"
                                      % (le, meta["size"]))
                        elif bool(flags & 0x40000000) != meta["looped"]:
                            detail = " -- loop flag is wrong"
                        else:
                            detail = " -- header matches, so the PCM differs"
                    problems.append("%s: %s does not round-trip%s"
                                    % (group, name, detail))

    return problems, checked


# --------------------------------------------------------------------------
# Self-test: corrupt a .wav in four ways and require each to be caught.
# --------------------------------------------------------------------------

def mutate_wav(data, kind):
    b = bytearray(data)

    def find(cid):
        i = 12
        while i + 8 <= len(b):
            size = int.from_bytes(b[i + 4:i + 8], "little")
            if b[i:i + 4] == cid:
                return i, size
            i += 8 + size + (size % 2)
        return None, None

    if kind == "pitch":
        i, _ = find(b"agbp")
        if i is None:
            return None
        old = int.from_bytes(b[i + 8:i + 12], "little")
        b[i + 8:i + 12] = (old + 1024).to_bytes(4, "little")
    elif kind == "loop_end":
        i, _ = find(b"agbl")
        if i is None:
            return None
        old = int.from_bytes(b[i + 8:i + 12], "little")
        if old < 2:
            return None
        b[i + 8:i + 12] = (old - 1).to_bytes(4, "little")
    elif kind == "pcm":
        i, size = find(b"data")
        if i is None or size < 100:
            return None
        b[i + 8 + 50] ^= 0x7F
    elif kind == "loop_flag":
        i, size = find(b"smpl")
        if i is None:
            return None
        # numSampleLoops lives at offset 28 of the chunk payload.
        n = int.from_bytes(b[i + 8 + 28:i + 8 + 32], "little")
        if n == 0:
            return None
        b[i + 8 + 28:i + 8 + 32] = (0).to_bytes(4, "little")
    else:
        return None

    return bytes(b)


def selftest(repo):
    problems, checked = check(repo)
    if problems:
        print("SELFTEST INCONCLUSIVE: the round-trip does not currently pass.")
        for p in problems[:10]:
            print("  " + p)
        return 1
    if not checked:
        print("SELFTEST INCONCLUSIVE: nothing was checked.")
        return 1

    print("baseline: %d samples round-trip exactly.\n" % checked)

    manifests = find_manifests(repo)
    with open(manifests[0]) as f:
        man = json.load(f)

    # Pick a looped sample so every mutation is applicable.
    victim = None
    for name, meta in sorted(man["samples"].items()):
        if meta["looped"] and meta["pcm_bytes"] > 200:
            victim = name
            break
    if victim is None:
        print("SELFTEST INCONCLUSIVE: no looped sample to corrupt.")
        return 1

    src = os.path.join(repo, "sound", "direct_sound_samples", victim + ".wav")
    with open(src, "rb") as f:
        original = f.read()

    failures = 0
    kinds = ["pitch", "loop_end", "pcm", "loop_flag"]

    with tempfile.TemporaryDirectory() as tmp:
        for kind in kinds:
            broken = mutate_wav(original, kind)
            if broken is None:
                print("  ?? %-10s -- could not apply, so it proves nothing" % kind)
                failures += 1
                continue

            path = os.path.join(tmp, victim + ".wav")
            with open(path, "wb") as f:
                f.write(broken)

            probs, _ = check(repo, wav_override=(victim, path))
            hit = [p for p in probs if victim in p]
            if hit:
                print("  ok %-10s -- caught: %s" % (kind, hit[0]))
            else:
                print("  NO %-10s -- NOT CAUGHT" % kind)
                failures += 1

    if failures:
        print("\n%d of %d corruptions went undetected." % (failures, len(kinds)))
        return 1

    print("\nAll %d deliberate corruptions were caught." % len(kinds))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("repo_pos", nargs="?")
    ap.add_argument("--repo", dest="repo_opt")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    repo = args.repo_pos or args.repo_opt or os.environ.get("POKEDECOMP_REPO")
    if not repo:
        repo = os.path.abspath(
            os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

    if args.selftest:
        sys.exit(selftest(repo))

    problems, checked = check(repo)
    if problems:
        for p in problems:
            print("FAIL: " + p)
        print("\n%d problem(s)." % len(problems))
        sys.exit(1)

    print("OK: %d decompiled samples rebuild to the exact bytes they came from."
          % checked)


if __name__ == "__main__":
    main()
