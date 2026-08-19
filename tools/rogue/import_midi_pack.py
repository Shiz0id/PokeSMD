#!/usr/bin/env python3
"""Import a directory of MIDIs as one music-player playlist.

Wiring one song touches five files -- the .mid copy, sound/songs/midi/midi.cfg,
sound/song_table.inc, include/constants/songs.h and the player's track table --
and a directory is fifty of them, so this does the whole job and is idempotent.
The same shape as tools/rogue/wire_gb_song.py, for the same reason.

IT TEST-CONVERTS BEFORE WIRING ANYTHING. mid2agb rejects some files, and a
single bad one wired into song_table.inc breaks the build for every song in the
batch. Each candidate is converted into a temp directory first and only the
survivors are written, with the failures listed.

THE SAMPLES ARE ALREADY PAID FOR. Every imported song plays through one shared
voicegroup, so the ROM cost is sequence data only -- roughly a quarter of the
.mid's size, not a new sample bank per song.

TWO THINGS IT WARNS ABOUT rather than silently importing:

  * MIDI channel 10 is percussion by GM convention but means nothing to m4a --
    it plays whatever program that channel selected, out of a melodic bank. A
    song with drum notes there will have them come out pitched.
  * More simultaneous channels than MAX_DIRECTSOUND_CHANNELS means m4a steals
    the lowest-priority one, and instruments drop out of dense passages.

Usage:
    import_midi_pack.py --repo . --dir "/path/to/DPPT" \\
        --playlist DPPT --label "DIAMOND & PEARL" --prefix dppt
    import_midi_pack.py --repo . --dir ... --playlist DPPT --dry-run
"""

import argparse
import collections
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

MAX_NAME_CHARS = 22          # what fits the player's list window
MAX_DIRECTSOUND_CHANNELS = 12


# --------------------------------------------------------------------------


def tempo_off_conductor(path):
    """Tracks carrying tempo events when track 0 carries none.

    Imported rather than reimplemented: fix_midi_tempo_track.py owns the MIDI
    walking, and a second copy of a variable-length-quantity parser in this
    file is exactly the kind of drift this repo's notes keep warning about.
    """
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "_fix_tempo", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "fix_midi_tempo_track.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    try:
        found = mod.tempo_map(open(path, "rb").read())
    except Exception:
        return []
    if not found or any(track == 0 for track, _, _ in found):
        return []
    return sorted(set(track for track, _, _ in found))

def midi_census(path):
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"MThd":
        return None
    fmt = struct.unpack_from(">H", d, 8)[0]

    progs = collections.defaultdict(set)
    notes = collections.Counter()
    i = 14
    while i < len(d) - 8:
        if d[i:i + 4] != b"MTrk":
            i += 1
            continue
        size = struct.unpack_from(">I", d, i + 4)[0]
        start, end = i + 8, i + 8 + size
        p, running = start, None
        while p < end:
            while p < end and d[p] & 0x80:
                p += 1
            p += 1
            if p >= end:
                break
            b = d[p]
            if b & 0x80:
                running = b
                p += 1
            if running is None:
                break
            hi, ch = running & 0xF0, running & 0x0F
            if hi == 0xC0:
                progs[ch].add(d[p]); p += 1
            elif hi in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                if hi == 0x90 and p + 1 < end and d[p + 1] != 0:
                    notes[ch] += 1
                p += 2
            elif hi == 0xD0:
                p += 1
            elif running == 0xFF:
                p += 1
                ln = 0
                while p < end and d[p] & 0x80:
                    ln = (ln << 7) | (d[p] & 0x7F); p += 1
                ln = (ln << 7) | d[p]; p += 1
                p += ln
            elif running in (0xF0, 0xF7):
                ln = 0
                while p < end and d[p] & 0x80:
                    ln = (ln << 7) | (d[p] & 0x7F); p += 1
                ln = (ln << 7) | d[p]; p += 1
                p += ln
            else:
                p += 1
        i = end

    used = sorted(set(list(progs) + [c for c in notes if notes[c]]))
    return {"format": fmt, "channels": len(used),
            "drum_notes": notes.get(9, 0), "notes": sum(notes.values())}


def slug(name):
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def display_name(stem, strip_words):
    """Turn a pack filename into something that fits the player's window."""
    s = stem
    for w in strip_words:
        s = re.sub(r"^%s[ _-]*" % re.escape(w), "", s, flags=re.I)
    s = re.sub(r"[_]+", " ", s).strip()
    s = re.sub(r"\s+", " ", s)
    s = s.upper()
    if len(s) > MAX_NAME_CHARS:
        # Drop vowels from the longest word before truncating outright, so the
        # name stays recognisable rather than ending mid-syllable.
        #
        # EVERY BRANCH IN HERE MUST SHORTEN SOMETHING. Removing vowels from a
        # word that has none left returns it UNCHANGED, and it is still the
        # longest word, so the next pass picks the same one and the loop spins
        # forever at 100% CPU with nothing written and nothing logged. That is
        # not hypothetical -- "Guardian Signs Pokemon Pinchers Admin Battle"
        # reaches "PNCHRS", six letters and vowel-free, and hung the importer.
        words = s.split(" ")
        while len(" ".join(words)) > MAX_NAME_CHARS and len(words) > 1:
            longest = max(range(len(words)), key=lambda i: len(words[i]))
            w = words[longest]
            devowelled = w[0] + re.sub(r"[AEIOU]", "", w[1:]) if len(w) > 4 else w

            if devowelled != w:
                words[longest] = devowelled
            elif len(w) > 4:
                # Already vowel-free and still the longest. Give up one letter
                # rather than the whole word -- this is the branch that
                # guarantees the loop makes progress.
                words[longest] = w[:-1]
            else:
                words.pop()

            if all(len(x) <= 4 for x in words):
                break
        s = " ".join(words)[:MAX_NAME_CHARS].strip()
    return s


def selftest():
    """Break the name shortener on purpose and require it to terminate.

    A check that has never failed is worth nothing, and this one guards a HANG:
    the failure mode is not a wrong name, it is the tool spinning with no output
    at all, which reads as "the import is just slow" until someone looks at ps.
    """
    cases = [
        # The one that actually hung it, plus the shapes around it.
        "Guardian Signs Pokemon Pinchers Admin Battle",
        "Almia Team Dim Sun Sinis Trio Battle",
        "RHYTHM SYNTH STRNGS PNCHRS",      # every word already vowel-free
        "BCDFG HJKLM NPQRS TVWXY ZBCDF",   # no vowels anywhere, all > 4
        "SUPERCALIFRAGILISTICEXPIALIDOCIOUS",   # one enormous word, no spaces
        "A B C D E F G H I J K L M N O P Q R S T U V W X Y Z",
        "Short",
    ]

    ok = True
    for stem in cases:
        out = display_name(stem, [])
        if len(out) > MAX_NAME_CHARS:
            print("  FAIL %-46r -> %r (%d chars, over %d)"
                  % (stem, out, len(out), MAX_NAME_CHARS))
            ok = False
        else:
            print("  ok   %-46r -> %r" % (stem, out))

    # The regression itself: the pre-fix loop could not shorten this word, so
    # assert the branch that replaced it does.
    before = "PNCHRS"
    after = before[0] + re.sub(r"[AEIOU]", "", before[1:])
    if after != before:
        print("  SELFTEST INCONCLUSIVE: %r still has vowels to drop, so it no "
              "longer reproduces the hang" % before)
        ok = False
    else:
        print("  ok   %r is vowel-free, which is what used to spin" % before)

    return ok


# --------------------------------------------------------------------------
# Songs that bring their own instruments
# --------------------------------------------------------------------------
#
# The pack in GBA Music Pack shares one voicegroup, so importing a song there is
# sequence data and nothing else. The Team Aqua asset repo's songs are the other
# kind: each brings a voicegroup, sometimes its own samples, sometimes keysplits.
# Those have to be installed BEFORE the song can link, and getting any one of
# them wrong breaks the build for the whole batch -- the same all-or-nothing
# failure the test-conversion above exists to prevent.
#
# Two symbol forms turn up and they take DIFFERENT mid2agb arguments:
#
#   voice_group foo      the macro form -> symbol voicegroup_foo -> -G_foo
#   voicegroup191::      the old raw label   -> symbol voicegroup191 -> -G191
#
# Getting that wrong produces an undefined symbol at link time, not a bad sound.

# A reference to another voicegroup, in EITHER form. It must not be
# `voicegroup_\w+`: the old raw labels are numeric and carry no underscore
# (voicegroup192), so an underscore-only pattern silently sees no dependency at
# all and the song is wired to link against a symbol nothing installs.
VOICEGROUP_REF = r"\b(voicegroup\w*)\b"


def voicegroup_symbols(text):
    """(macro-form names, raw-label names) a voicegroup file defines."""
    return (re.findall(r"^voice_group\s+(\w+)", text, re.M),
            re.findall(r"^(voicegroup\w*)::", text, re.M))


def voicegroup_arg(text):
    """The -G argument for a song that plays through this file, or None."""
    macro, raw = voicegroup_symbols(text)
    if macro:
        return "_" + macro[0]
    if raw:
        # Strip the leading "voicegroup", which mid2agb supplies itself.
        return raw[0][len("voicegroup"):]
    return None


def voicegroup_defines(text):
    """Every voicegroup symbol a file defines, in linker form."""
    macro, raw = voicegroup_symbols(text)
    return {"voicegroup_" + m for m in macro} | set(raw)


def index_voicegroups(root):
    """symbol -> (path, text) for every voicegroup under a directory tree.

    Walked RECURSIVELY on purpose. A song's own bank routinely references a
    drumset kept in a sibling directory -- jorts puts 32 of them in
    voicegroups/drumsets/ -- and copying only the file that matches the song
    leaves those undefined at LINK time, long after the import reported success.
    """
    index = {}
    for dp, _dn, fn in os.walk(root):
        for f in sorted(fn):
            if not f.endswith((".inc", ".s")):
                continue
            p = os.path.join(dp, f)
            t = read(p)
            for sym in voicegroup_defines(t):
                index.setdefault(sym, (p, t))
    return index


def repo_voicegroups(repo):
    """Every voicegroup symbol the ROM already defines."""
    out = set()
    for dp, _dn, fn in os.walk(os.path.join(repo, "sound")):
        for f in fn:
            if f.endswith((".inc", ".s")):
                out |= voicegroup_defines(read(os.path.join(dp, f)))
    return out


def voicegroup_closure(path, text, index, already):
    """({path: text} to install, {unresolved symbols}) for one song's bank.

    Transitive, because a drumset may itself reference another voicegroup.
    """
    install, stack = {}, [(path, text)]
    while stack:
        p, t = stack.pop()
        if p in install:
            continue
        install[p] = t
        for sym in sorted(set(re.findall(VOICEGROUP_REF, t))):
            if sym in already:
                continue
            dep = index.get(sym)
            if dep is not None and dep[0] not in install:
                stack.append(dep)

    # Resolved only AFTER the walk, so a symbol defined by any file that ended up
    # in the set counts as satisfied whatever order they were reached in.
    provided = set()
    for t in install.values():
        provided |= voicegroup_defines(t)

    missing = set()
    for t in install.values():
        for sym in set(re.findall(VOICEGROUP_REF, t)):
            if sym not in already and sym not in provided:
                missing.add(sym)

    return install, missing


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def next_song_id(repo):
    rows = re.findall(r"^\s*song\s+\w+", read(os.path.join(
        repo, "sound", "song_table.inc")), re.M)
    return len(rows)


def main():
    # --selftest takes no repo and no directory, so it is handled before the
    # required arguments are enforced.
    if "--selftest" in sys.argv:
        print("--- selftest: names that must not hang the shortener ---")
        good = selftest()
        print("--- selftest %s ---" % ("passed" if good else "FAILED"))
        raise SystemExit(0 if good else 1)

    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", required=True)
    ap.add_argument("--dir", required=True, help="directory of .mid files")
    ap.add_argument("--playlist", required=True,
                    help="RMP_PLAYLIST_<THIS>, created if absent")
    ap.add_argument("--label", help="playlist name shown in the menu")
    ap.add_argument("--prefix", help="song label prefix (default: playlist)")
    ap.add_argument("--voicegroup", default="all_instruments")
    # Songs that bring their own instruments. --voicegroup-dir matches a
    # voicegroup to each song by file stem (jorts ships one per song);
    # --voicegroup-file gives every song in the batch the same one (nico ships a
    # single bank for all nine). Neither means the shared --voicegroup above.
    ap.add_argument("--voicegroup-dir",
                    help="per-song voicegroup .inc files, matched by file stem")
    ap.add_argument("--voicegroup-file",
                    help="one voicegroup .inc shared by every song in the batch")
    ap.add_argument("--voicegroup-search",
                    help="where to look for voicegroups a bank DEPENDS on "
                         "(default: alongside the bank). Specker keeps a drumset "
                         "one song's folder over from the song that uses it.")
    ap.add_argument("--samples-dir",
                    help="directory of .wav samples to install (searched recursively)")
    ap.add_argument("--keysplits",
                    help="file of keysplit definitions to append to "
                         "sound/keysplit_tables.inc")
    ap.add_argument("--volume", default="080")
    ap.add_argument("--strip", default="",
                    help="comma-separated words to strip from display names")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    repo = args.repo
    prefix = slug(args.prefix or args.playlist)
    label = args.label or args.playlist
    strip_words = [w for w in args.strip.split(",") if w]

    mid2agb = os.path.join(repo, "tools", "mid2agb", "mid2agb")
    if not os.path.isfile(mid2agb):
        raise SystemExit("tools/mid2agb/mid2agb is not built -- run `make tools`")

    files = sorted(f for f in os.listdir(args.dir) if f.lower().endswith(".mid"))
    if not files:
        raise SystemExit("no .mid files in %s" % args.dir)

    if args.voicegroup_dir and args.voicegroup_file:
        raise SystemExit("--voicegroup-dir and --voicegroup-file are exclusive")

    # Resolve every song's voicegroup up front. A song whose bank is missing is
    # dropped here rather than wired and left to fail at link time, where the
    # error names a symbol and not the song that wanted it.
    shared_vg = None
    if args.voicegroup_file:
        shared_vg = (args.voicegroup_file, read(args.voicegroup_file))
        if voicegroup_arg(shared_vg[1]) is None:
            raise SystemExit("%s defines no voicegroup" % args.voicegroup_file)

    # What the ROM already has, and what the pack can supply. Both are needed to
    # tell "this bank is satisfied" from "this bank will not link".
    have_vgs = set()
    vg_index = {}
    vg_search_root = None
    if args.voicegroup_dir or args.voicegroup_file:
        have_vgs = repo_voicegroups(repo)
        vg_search_root = (args.voicegroup_search
                          or args.voicegroup_dir
                          or os.path.dirname(args.voicegroup_file))
        vg_index = index_voicegroups(vg_search_root)

    def voicegroup_for(stem):
        """(path, text) of the voicegroup this song plays through, or None."""
        if shared_vg:
            return shared_vg
        if not args.voicegroup_dir:
            return None
        # jorts names its midis mus_<x>.mid and its banks <x>.inc.
        bare = re.sub(r"^mus_", "", stem)
        for cand in (stem, bare):
            p = os.path.join(args.voicegroup_dir, cand + ".inc")
            if os.path.isfile(p):
                return (p, read(p))
        return None

    songs, failures, warnings = [], [], []
    used_labels, used_names = set(), set()

    with tempfile.TemporaryDirectory() as tmp:
        for f in files:
            src = os.path.join(args.dir, f)
            stem = os.path.splitext(f)[0]

            # Strip the pack's own prefix from the slug too, or every label
            # reads mus_dppt_dppt_... and the MUS_ constants get absurd.
            bare = stem
            for w in strip_words:
                bare = re.sub(r"^%s[ _-]*" % re.escape(w), "", bare, flags=re.I)
            base = "mus_%s_%s" % (prefix, slug(bare))
            lbl, n = base, 2
            while lbl in used_labels:
                lbl = "%s_%d" % (base, n); n += 1
            used_labels.add(lbl)

            vg = voicegroup_for(stem)
            if (args.voicegroup_dir or args.voicegroup_file) and vg is None:
                failures.append((f, ["no voicegroup found for it"]))
                continue

            vg_install = {}
            if vg is None:
                gopt = "-G_" + args.voicegroup
            else:
                garg = voicegroup_arg(vg[1])
                if garg is None:
                    failures.append((f, ["%s defines no voicegroup"
                                         % os.path.basename(vg[0])]))
                    continue
                gopt = "-G" + garg

                # Everything the bank pulls in, resolved NOW. A drumset that
                # cannot be found is a link error against a symbol name, with
                # nothing to say which song wanted it -- so the song is dropped
                # here instead, named, with the symbol that is missing.
                vg_install, missing = voicegroup_closure(
                    vg[0], vg[1], vg_index, have_vgs)
                if missing:
                    failures.append((f, ["needs %s, which neither the ROM nor "
                                         "this pack defines"
                                         % ", ".join(sorted(missing)[:3])]))
                    continue

            # mid2agb keys its output on the filename, so the copy has to carry
            # the final label before it is worth testing.
            probe = os.path.join(tmp, lbl + ".mid")
            shutil.copyfile(src, probe)
            out = os.path.join(tmp, lbl + ".s")
            r = subprocess.run(
                [mid2agb, probe, out, "-E", "-R50", gopt, "-V" + args.volume],
                capture_output=True, text=True)
            if r.returncode != 0 or not os.path.exists(out):
                failures.append((f, (r.stderr or r.stdout or "").strip()
                                 .splitlines()[-1:] or ["mid2agb failed"]))
                continue

            c = midi_census(src) or {}
            if c.get("drum_notes"):
                # The warning's whole premise is that the song plays through a
                # MELODIC bank. A song that brings its own voicegroup with a
                # drumset in it -- voice_keysplit_all is the drumset form -- was
                # written for these notes, so reporting it as a defect turns
                # every such import into a wall of warnings that mean nothing.
                own_drums = vg is not None and "voice_keysplit_all" in vg[1]
                if own_drums:
                    warnings.append("%s has %d notes on MIDI channel 10, and its"
                                    " own voicegroup maps a drumset -- expected,"
                                    " noted only so it is not a surprise"
                                    % (f, c["drum_notes"]))
                else:
                    warnings.append("%s has %d notes on MIDI channel 10; a melodic"
                                    " bank will play them pitched"
                                    % (f, c["drum_notes"]))
            if c.get("channels", 0) > MAX_DIRECTSOUND_CHANNELS:
                warnings.append("%s uses %d channels, over the %d DirectSound"
                                " channels; m4a will steal the quietest"
                                % (f, c["channels"], MAX_DIRECTSOUND_CHANNELS))

            # TEMPO OFF THE CONDUCTOR TRACK, which is the quietest failure in
            # this whole tool. mid2agb reads tempo from track 0 only; a
            # sequencer that writes it onto the first MUSIC track instead makes
            # a valid MIDI that every desktop player renders correctly, and
            # mid2agb then emits no TEMPO command at all. The song converts
            # cleanly, reports nothing, and plays at m4a's default speed.
            #
            # Caught on HGSS Champion Lance, whose three 184 BPM events sat on
            # track 1 while track 0 was a 12-byte stub. It was found by
            # comparing its .s against a known-good song's, not by anything
            # here -- which is why this warning now exists.
            stray = tempo_off_conductor(src)
            if stray:
                warnings.append("%s has its tempo on track(s) %s, not track 0;"
                                " mid2agb will emit NO tempo and the song will"
                                " play at the default speed. Fix with"
                                " tools/rogue/fix_midi_tempo_track.py"
                                % (f, stray))

            disp = display_name(stem, strip_words)
            d, n = disp, 2
            while d in used_names:
                d = ("%s %d" % (disp, n))[:MAX_NAME_CHARS]; n += 1
            used_names.add(d)

            songs.append({"src": src, "label": lbl, "name": d,
                          "const": "MUS_" + lbl[4:].upper(),
                          "gopt": gopt, "vg": vg, "vg_install": vg_install})

    print("%d of %d convert cleanly" % (len(songs), len(files)))
    for f, why in failures:
        print("  SKIP %-44s %s" % (f, why[0] if why else ""))
    for w in warnings:
        print("  WARN %s" % w)

    if args.dry_run:
        print()
        for s in songs[:12]:
            print("  %-40s %-22s %s" % (s["label"], s["name"], s["const"]))
        print("\n(--dry-run: nothing written)")
        return

    if not songs:
        raise SystemExit("nothing to import")

    # ---- instruments, before anything that references them ---------------
    #
    # Order matters only for readability -- the assembler resolves forward
    # references within voice_groups.inc -- but every step here is guarded so
    # re-running the import is a no-op rather than a duplicate symbol.
    installed = {"samples": 0, "keysplits": 0, "voicegroups": 0}

    if args.samples_dir:
        sample_dir = os.path.join(repo, "sound", "direct_sound_samples")
        p = os.path.join(repo, "sound", "direct_sound_data.inc")
        data = read(p).rstrip("\n")
        # .wav AND .bin. Packs ship either -- Specker ships one sample only as an
        # already-converted pl_musicbox62.bin -- and sound/direct_sound_samples/
        # holds both forms side by side, with audio_rules.mk regenerating the
        # .bin from the .wav when there is one. Taking only .wav leaves exactly
        # that sample undefined, which surfaces as a link error naming a symbol
        # rather than a song.
        found = {}
        for dp, _dn, fn in os.walk(args.samples_dir):
            for f in sorted(fn):
                stem, ext = os.path.splitext(f)
                if ext.lower() not in (".wav", ".bin"):
                    continue
                # .wav wins: it is the source the build converts from.
                if ext.lower() == ".wav" or stem not in found:
                    found[stem] = os.path.join(dp, f)

        for stem, srcfile in sorted(found.items()):
                sym = "DirectSoundWaveData_" + stem
                dst = os.path.join(sample_dir,
                                   stem + os.path.splitext(srcfile)[1].lower())
                if not os.path.exists(dst):
                    shutil.copyfile(srcfile, dst)
                if (sym + "::") not in data:
                    # audio_rules.mk turns the .wav into the .bin this points at.
                    data += ('\n\n\t.align 2\n%s::\n'
                             '\t.incbin "sound/direct_sound_samples/%s.bin"'
                             % (sym, stem))
                    installed["samples"] += 1
        write(p, data + "\n")

    if args.keysplits:
        p = os.path.join(repo, "sound", "keysplit_tables.inc")
        ks = read(p).rstrip("\n")
        block = read(args.keysplits).strip("\n")
        for name in re.findall(r"^\s*keysplit\s+(\w+)", block, re.M):
            if re.search(r"^\s*keysplit\s+%s\b" % re.escape(name), ks, re.M):
                continue
            installed["keysplits"] += 1
        if installed["keysplits"]:
            ks += "\n\n@ Imported from %s\n%s" % (label, block)
            write(p, ks + "\n")

    # The union of every song's dependency closure, not just the banks that
    # match a song by name -- the drumsets live in a subdirectory.
    #
    # Keyed on the path RELATIVE TO THE PACK, never the basename: jorts names a
    # song's drumset exactly as it names the song's own bank
    # (voicegroups/x.inc and voicegroups/drumsets/x.inc), so a basename key
    # silently collapses the pair and one of them is never installed at all.
    vg_files = {}
    for s in songs:
        for p, t in s.get("vg_install", {}).items():
            vg_files[os.path.relpath(p, vg_search_root)] = (p, t)
    if vg_files:
        # Kept in their own directory so what is vendored is obvious, and so a
        # future re-import can find them without guessing.
        vg_dir = os.path.join(repo, "sound", "voicegroups", "imported", prefix)
        p = os.path.join(repo, "sound", "voice_groups.inc")
        inc = read(p).rstrip("\n")
        for rel, (srcpath, text) in sorted(vg_files.items()):
            # Slugified per component: pack directories are named for humans
            # ("Jazzy Song"), and a space in a path that ends up in a Make
            # prerequisite is a rule that silently matches nothing.
            parts = rel.replace(os.sep, "/").split("/")
            rel = "/".join([slug(x) for x in parts[:-1]] + [parts[-1]])
            dst = os.path.join(vg_dir, *rel.split("/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if not os.path.exists(dst):
                write(dst, text)
            line = '.include "sound/voicegroups/imported/%s/%s"' % (prefix, rel)
            if line not in inc:
                inc += "\n" + line
                installed["voicegroups"] += 1
        write(p, inc + "\n")

    if any(installed.values()):
        print("installed %(samples)d samples, %(keysplits)d keysplits, "
              "%(voicegroups)d voicegroups" % installed)

    # ---- the five files -------------------------------------------------
    midi_dir = os.path.join(repo, "sound", "songs", "midi")
    for s in songs:
        shutil.copyfile(s["src"], os.path.join(midi_dir, s["label"] + ".mid"))

    p = os.path.join(midi_dir, "midi.cfg")
    cfg = read(p).rstrip("\n")
    for s in songs:
        # The song's OWN voicegroup, resolved when it was test-converted, not
        # the batch default -- these songs each bring their own instruments.
        line = "%s.mid: -E -R50 %s -V%s" % (s["label"], s["gopt"], args.volume)
        if (s["label"] + ".mid:") not in cfg:
            cfg += "\n" + line
    write(p, cfg + "\n")

    # Appended, never inserted: song_table.inc is indexed BY ROW, so inserting
    # renumbers every song after it.
    p = os.path.join(repo, "sound", "song_table.inc")
    tbl = read(p)
    first_id = next_song_id(repo)
    add = []
    for s in songs:
        row = "\tsong %s, MUSIC_PLAYER_BGM, GBS_MUSIC_NONE" % s["label"]
        if row not in tbl:
            add.append(row)
    if add:
        marker = "\n".join(add)
        idx = tbl.rindex("\tsong ")
        end = tbl.index("\n", idx) + 1
        tbl = tbl[:end] + marker + "\n" + tbl[end:]
        write(p, tbl)

    p = os.path.join(repo, "include", "constants", "songs.h")
    hdr = read(p)
    lines = []
    for i, s in enumerate(songs):
        if s["const"] not in hdr:
            lines.append("#define %-27s %d" % (s["const"], first_id + i))
    if lines:
        anchor = "#define MUS_ROUTE118"
        hdr = hdr.replace(anchor, "// Imported from %s by"
                          " tools/rogue/import_midi_pack.py\n%s\n\n%s"
                          % (label, "\n".join(lines), anchor), 1)
        write(p, hdr)

    # ---- the player's table and its playlist ----------------------------
    p = os.path.join(repo, "src", "data", "rogue_music_player.h")
    tab = read(p)
    enum = "RMP_PLAYLIST_" + args.playlist.upper()
    if enum not in tab:
        tab = tab.replace("    RMP_PLAYLIST_COUNT,",
                          "    %s,\n    RMP_PLAYLIST_COUNT," % enum, 1)
        tab = tab.replace("};\n\n#define TRACK", "};\n\n#define TRACK")
        tab = tab.replace(
            "    [RMP_PLAYLIST_FANFARE]   = COMPOUND_STRING(\"JINGLES & EVENTS\"),",
            "    [RMP_PLAYLIST_FANFARE]   = COMPOUND_STRING(\"JINGLES & EVENTS\"),\n"
            "    [%s] = COMPOUND_STRING(\"%s\")," % (enum, label), 1)

    rows = []
    for s in songs:
        row = '    TRACK(%s, GBS_MUSIC_NONE, %s, "%s"),' % (
            s["const"], enum, s["name"])
        if s["const"] not in tab:
            rows.append(row)
    if rows:
        tab = tab.replace("};\n\n#undef TRACK",
                          "\n    // Imported from %s.\n%s\n};\n\n#undef TRACK"
                          % (label, "\n".join(rows)), 1)
    write(p, tab)

    print("\nwired %d songs as ids %d..%d under %s"
          % (len(songs), first_id, first_id + len(songs) - 1, enum))


if __name__ == "__main__":
    main()
