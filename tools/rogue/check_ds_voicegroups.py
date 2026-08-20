"""Guard the three ways a ripped DS voicegroup fails silently.

Every one of these shipped, built clean, and passed all sixty other checks.

1. SAMPLE ALIGNMENT. struct WaveData is {u16 type; u16 status; u32 freq;
   u32 loopStart; u32 size;} and the mixer reads the three words with 32-bit ARM
   loads. An unaligned LDR on ARM7 does not fault, it ROTATES the word - so a
   blob that does not start on a 4-byte boundary hands the mixer a garbage rate,
   a garbage loop point and a garbage length, and it walks off into whatever ROM
   follows. It sounds like a cart being pulled mid-game. 229 of 229 DS samples
   shipped this way because the generated block in direct_sound_data.inc had no
   `.align 2`, which every one of the 1363 hand-written entries beside it has.

2. PITCH PARKED IN DEAD DATA. m4a takes a note's pitch key three ways
   (src/m4a_1.s:996-1039): a plain voice and a voice_keysplit both keep the
   PLAYED note, and only voice_keysplit_all substitutes the voice's own key
   field (m4a_1.s:1035). So outside a keysplit_all, `voice_directsound <note>`
   is a byte nothing reads, and the whole transposition has to be in
   WaveData.freq - "the rate that would sound middle C", times 1024. 910 of 1156
   voices once carried the offset in that dead byte and played between three
   octaves flat and three octaves sharp.

3. A PROGRAM THAT LANDS NOWHERE. A song selecting a program past the end of its
   voicegroup reads whatever follows as voice structs; one selecting a filler
   slot plays a voice with attack 0, which never rises. Out of range is always
   wrong. A filler slot is only wrong if notes follow it - the DS banks have
   genuinely empty programs and a faithful rip selects them too.

Run:  python3 tools/rogue/check_ds_voicegroups.py [--repo PATH]
      python3 tools/rogue/check_ds_voicegroups.py --selftest
"""
import argparse
import re
import struct
import sys
from pathlib import Path

DS_DIR = "sound/voicegroups/imported/ds"


def find_repo(arg):
    if arg:
        return Path(arg)
    here = Path(__file__).resolve()
    for p in here.parents:
        if (p / DS_DIR).exists():
            return p
    return Path(".")


def midi_programs_with_notes(path):
    """{program: note count} for one MIDI."""
    d = Path(path).read_bytes()
    if d[:4] != b"MThd":
        return {}
    pos, out = 0, {}
    while pos < len(d):
        cid = d[pos:pos + 4]
        ln = struct.unpack(">I", d[pos + 4:pos + 8])[0]
        if cid == b"MTrk":
            body = d[pos + 8:pos + 8 + ln]
            p, run, cur = 0, None, {}
            while p < len(body):
                v = 0
                while True:
                    b = body[p]; p += 1; v = (v << 7) | (b & 0x7F)
                    if not b & 0x80:
                        break
                st = body[p]
                if st & 0x80:
                    run = st; p += 1
                else:
                    st = run
                if st is None:
                    break
                if st == 0xFF:
                    p += 1; l2 = 0
                    while True:
                        b = body[p]; p += 1; l2 = (l2 << 7) | (b & 0x7F)
                        if not b & 0x80:
                            break
                    p += l2
                elif st in (0xF0, 0xF7):
                    l2 = 0
                    while True:
                        b = body[p]; p += 1; l2 = (l2 << 7) | (b & 0x7F)
                        if not b & 0x80:
                            break
                    p += l2
                else:
                    hi = st & 0xF0
                    n = 1 if hi in (0xC0, 0xD0) else 2
                    ch = st & 0x0F
                    if hi == 0xC0:
                        cur[ch] = body[p]
                        out.setdefault(body[p], 0)
                    elif hi == 0x90 and body[p + 1] and ch in cur:
                        out[cur[ch]] = out.get(cur[ch], 0) + 1
                    p += n
        pos += 8 + ln
    return out


def run(repo, quiet=False):
    problems = []
    ds = repo / DS_DIR
    if not ds.exists():
        print("no DS voicegroups in this tree; nothing to check")
        return 0

    # --- 1. alignment.
    #
    # THE MAP IS THE AUTHORITY, not the source. Vanilla's Phoneme block carries
    # no `.align 2` per label and is aligned anyway, because every blob before it
    # happens to be a multiple of four long - so a source-only rule reports 51
    # samples that are perfectly fine. Read the built ROM's symbol addresses when
    # there is a build, and fall back to requiring `.align 2` on OUR generated
    # block only, which is the part we control and the part that got it wrong.
    mapfile = next((p for p in repo.glob("*.map")), None)
    if mapfile:
        for l in mapfile.read_text(errors="ignore").splitlines():
            m = re.match(r"\s+0x0*([0-9a-f]{8})\s+(DirectSoundWaveData_\S+)$", l)
            if m and int(m.group(1), 16) % 4:
                problems.append(
                    f"{m.group(2)} is at 0x{m.group(1).upper()}, not 4-byte "
                    f"aligned - the mixer will rotate freq/loopStart/size")
    else:
        dsd = [l.strip() for l in (repo / "sound/direct_sound_data.inc")
               .read_text(errors="replace").splitlines()]
        for i, s in enumerate(dsd):
            m = re.match(r"DirectSoundWaveData_(dp_wa\S+)::", s)
            if not m:
                continue
            # walk back over blanks to the previous meaningful line
            j = i - 1
            while j >= 0 and not dsd[j]:
                j -= 1
            if j < 0 or not dsd[j].startswith(".align"):
                problems.append(f"sample {m.group(1)} has no `.align 2`")

    # --- 2. base keys
    for f in sorted(ds.glob("*.inc")):
        rhy = set()
        text = f.read_text()
        for ln in text.splitlines():
            m = re.match(r"\s*voice_keysplit_all voicegroup_(\S+?)\s*(?:@.*)?$", ln)
            if m:
                rhy.add(m.group(1))
        cur = None
        for ln in text.splitlines():
            m = re.match(r"^voice_group (\S+)", ln)
            if m:
                cur = m.group(1)
                continue
            m = re.match(r"\s*voice_directsound (\d+),", ln)
            if m and int(m.group(1)) != 60 and cur not in rhy:
                problems.append(
                    f"{f.name}: base key {m.group(1)} outside a keysplit_all - "
                    f"the engine ignores it, so the pitch is lost")

    # --- 3. program selections
    roots = {}
    for f in sorted(ds.glob("*.inc")):
        lines = f.read_text().splitlines()
        try:
            i = lines.index(f"voice_group {f.stem}")
        except ValueError:
            continue
        slots = []
        for ln in lines[i + 1:]:
            if not ln.startswith("\t"):
                break
            s = ln.rstrip()
            if s.endswith("@ unused"):
                slots.append("empty")      # empty on the DS too - faithful
            elif "@ DROPPED" in s:
                slots.append("dropped")    # the DS plays something here, we do not
            else:
                slots.append("ok")
        roots[f.stem] = slots

    cfg = repo / "sound/songs/midi/midi.cfg"
    if cfg.exists():
        for line in cfg.read_text().splitlines():
            m = re.match(r"(\S+\.mid):\s*(.*)", line.strip())
            if not m:
                continue
            g = re.search(r"-G_(dp_\w+)", m.group(2))
            if not g or g.group(1) not in roots:
                continue
            mid = repo / "sound/songs/midi" / m.group(1)
            if not mid.exists():
                continue
            slots = roots[g.group(1)]
            for prog, notes in midi_programs_with_notes(mid).items():
                if prog >= len(slots):
                    problems.append(
                        f"{m.group(1)} selects program {prog}, past the end of "
                        f"{g.group(1)} ({len(slots)} slots) - {notes} notes read "
                        f"whatever follows the voicegroup")
                elif slots[prog] == "dropped" and notes:
                    problems.append(
                        f"{m.group(1)} plays {notes} notes on program {prog}, "
                        f"an instrument the ripper dropped from {g.group(1)} - "
                        f"silent here, audible on the DS")

    if not quiet:
        for p in problems:
            print("  " + p)
    return len(problems)


def selftest():
    """Break each property on purpose and confirm the check fires."""
    import shutil
    import tempfile

    repo = find_repo(None)
    fails = []

    def case(name, mutate):
        with tempfile.TemporaryDirectory() as td:
            t = Path(td) / "repo"
            t.mkdir()
            for rel in (DS_DIR, "sound/songs/midi"):
                (t / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(repo / DS_DIR, t / DS_DIR)
            (t / "sound/songs/midi").mkdir(parents=True, exist_ok=True)
            shutil.copy(repo / "sound/songs/midi/midi.cfg",
                        t / "sound/songs/midi/midi.cfg")
            for mid in (repo / "sound/songs/midi").glob("mus_dppt_*.mid"):
                shutil.copy(mid, t / "sound/songs/midi" / mid.name)
            shutil.copy(repo / "sound/direct_sound_data.inc",
                        t / "sound/direct_sound_data.inc")
            before = run(t, quiet=True)
            mutate(t)
            after = run(t, quiet=True)
            fired = after > before
            print(("PASS  " if fired else "FAIL  ") + name)
            if not fired:
                fails.append(name)

    def strip_align(t):
        # Exercise the path that actually runs: a map with a misaligned symbol.
        # This is the real bug, byte for byte - 229 samples sat at 0x...86.
        (t / "PokeSMD.map").write_text(
            "                0x08123454                DirectSoundWaveData_ok_one\n"
            "                0x09786ca6                DirectSoundWaveData_dp_wa1000_000_k34\n",
            newline="\n")

    def strip_align_source(t):
        # Remove the WHOLE run of .align before the label, not one of them.
        # Deleting a single directive left two more behind when this was first
        # written, and the case passed vacuously - direct_sound_data.inc had
        # accumulated 226 orphaned .align lines from an earlier cleanup that
        # dropped labels and .incbin lines but not their alignment.
        p = t / "sound/direct_sound_data.inc"
        lines = p.read_text().splitlines()
        for i, ln in enumerate(lines):
            if ln.startswith("DirectSoundWaveData_dp_wa"):
                j = i - 1
                while j >= 0 and (not lines[j].strip()
                                  or lines[j].strip().startswith(".align")):
                    if lines[j].strip().startswith(".align"):
                        del lines[j]
                    j -= 1
                break
        p.write_text("\n".join(lines) + "\n", newline="\n")

    def park_pitch(t):
        p = next((t / DS_DIR).glob("*.inc"))
        txt = p.read_text()
        txt = txt.replace("voice_directsound 60,", "voice_directsound 48,", 1)
        p.write_text(txt, newline="\n")

    def out_of_range(t):
        p = t / DS_DIR / "dp_dungeon.inc"
        lines = p.read_text().splitlines()
        i = lines.index("voice_group dp_dungeon")
        # cut the root down so a real program selection falls off the end
        keep = [ln for ln in lines[i + 1:] if ln.startswith("\t")][:4]
        p.write_text("\n".join(lines[:i + 1] + keep) + "\n", newline="\n")

    case("a misaligned sample in the map is caught", strip_align)
    case("a missing .align 2 in the source is caught (no-build path)",
         strip_align_source)
    case("a base key parked outside a keysplit_all is caught", park_pitch)
    case("a program past the end of a voicegroup is caught", out_of_range)

    print()
    print(f"{len(fails)} failed" if fails else "all passed")
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo", nargs="?")
    ap.add_argument("--repo", dest="repo_opt")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    repo = find_repo(args.repo_opt or args.repo)
    n = run(repo)
    print("check_ds_voicegroups: " + ("OK" if n == 0 else f"{n} problems"))
    return 1 if n else 0


if __name__ == "__main__":
    sys.exit(main())
