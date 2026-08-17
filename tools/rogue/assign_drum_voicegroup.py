#!/usr/bin/env python3
"""Give songs whose melodic programs carry DRUM TRACKS a voicegroup with kits there.

THE PROBLEM
-----------
Every song imported from the GBA Music Pack plays on voicegroup_all_instruments,
which is a faithful decompile of "All-Instrument Patch (Emerald).ups" -- verified
by applying the patch and decompiling it: 112 voicegroups, 30 keysplit tables,
405 samples, and slot 13 is a xylophone. That is correct General MIDI. GM keeps
no drum kit among its 128 melodic programs, because GM drums are a separate bank
reached by being on channel 10.

But m4a has no channel-10 concept -- the program byte is only an index into the
song's voicegroup -- and the pack's rips carry no channel 10 at all. Many of them
put a DRUM track on a melodic program using GM drum-map note numbers (36 kick,
38 snare, 42 closed hat), so those notes play as pitched instrument hits.

WHICH PROGRAMS, and how that was established
--------------------------------------------
audit_program_usage.py finds the pattern across all 128 programs.
verify_program_audit.py then cross-checks its MIDI parser against mid2agb's own
.s output, and confirm_findings.py re-derives the findings from that output
independently. Only programs flagged by BOTH readings are listed here:

    13  Xylophone       88%% of its notes on GM drum keys
    53  Voice Oohs      72%%
    64  Soprano Sax     92%%

Programs 26, 37, 55 and 118 flagged in only one reading -- they sit on the
threshold -- and are deliberately NOT included. Program 127 (Gunshot) is flagged
by both but its slot is already voice_keysplit_all, so it already behaves.

WHY NOT ONE SHARED "DRUMS" BANK
-------------------------------
Because the songs disagree about which slots are drums. Some use program 13 as a
genuine xylophone while using 64 as drums, and vice versa. A single bank with
every candidate slot turned into a kit would fix one song by breaking another.
So this generates ONE TABLE PER DISTINCT COMBINATION actually needed, and no
more. Each is 128 * 12 = 1536 bytes and shares every sub-voicegroup, keysplit
table and sample with the original -- nothing is duplicated.

CLASSIFICATION, deliberately conservative
-----------------------------------------
A program is a drum track for a song only when >=70%% of that song's notes on it
fall on the GM percussion map (35-81) AND >=20%% land on the canonical kit keys
(36 kick, 38 snare, 42 closed hat). A melodic part does not concentrate on those
three. Everything else stays melodic, because a song wrongly moved to drums
sounds broken while one left behind merely sounds as it did before.

IDEMPOTENT. Every imported song is reset to the base voicegroup first, so
re-running after an import or a threshold change produces the same result as a
clean run rather than layering on the last one.

    assign_drum_voicegroup.py <repo> --dry-run
    assign_drum_voicegroup.py <repo> --apply
    assign_drum_voicegroup.py <repo> --doc
"""

import argparse
import collections
import glob
import os
import re
import struct
import sys

BASE_VG = "all_instruments"
KIT = "voicegroup_frlg_drumset"

# ONLY PROGRAM 13. Programs 53 (Voice Oohs) and 64 (Soprano Sax) were assigned
# here and then REVERTED -- read this before adding a candidate back.
#
# They were justified by "share of notes on GM percussion keys 35-81", which is
# a much weaker measure than it looks: 35-81 is B1 to A5, most of the musical
# register, so an ordinary melody scores 90%+ on it for no reason at all. Both
# programs cleared the bar on that alone while showing 13-19 distinct pitches and
# only 25-34% of notes on kick/snare/hat. That is the shape of a melody passing
# through those keys, not a kit.
#
# Note that confirm_findings.py "confirming" 53 and 64 from mid2agb's output did
# NOT rescue them: two readings of the same weak metric are not independent
# evidence. Agreement between sources says the parser is right, not that the
# measure means anything.
#
# WHAT ACTUALLY IDENTIFIES A DRUM TRACK is a small pitch set hammering the
# canonical kit keys -- the strongest cases here are ONE distinct pitch struck
# 267 times, and it is note 36. Judge a new candidate on PITCH COUNT and
# CORE_SHARE, never on DRUM_SHARE alone.
CANDIDATES = [13]
GM_NAMES = {13: "Xylophone", 53: "Voice Oohs", 64: "Soprano Sax"}

# Kept because it is a cheap pre-filter, NOT because it is good evidence. See
# the note above: on its own it separates almost nothing.
GM_DRUM_LO, GM_DRUM_HI = 35, 81
KIT_KEYS = {36, 38, 42}
DRUM_SHARE = 0.70
CORE_SHARE = 0.20

PACK_PREFIXES = ["modern", "bw", "hgss", "ranger", "xy", "guest", "nico",
                 "jorts", "aqua", "dppt", "pmd", "gm"]

VG_DIR = "sound/voicegroups"
VG_FILE = os.path.join(VG_DIR, BASE_VG + ".inc")
INCLUDE_FILE = "sound/voice_groups.inc"
MIDI_CFG = "sound/songs/midi/midi.cfg"
DOC_PATH = "docs/PROGRAM_13_SPLIT.md"

"""Guard: the base table must still be the decompile, with no kits added.

Checked as a PROPERTY rather than against hardcoded strings. Hardcoding them was
tried and was wrong -- decompile_voicegroup.py deduplicates identical keysplit
tables, so slot 53 is `voicegroup_ai_voice_oohs, keysplit_ai_synth_bass_2` and
79 of the 128 slots pair a voicegroup with a differently-named keysplit. Writing
the expected strings by hand just encodes a guess about someone else's
generator. What actually matters is narrower and stable: none of the candidate
slots may already be a drum kit, or this tool would generate its tables from an
edited base and bake an override in permanently.
"""


def base_is_unedited(slots):
    bad = [p for p in CANDIDATES if slots[p].startswith("voice_keysplit_all")]
    return bad


def vg_name(slots):
    return BASE_VG + "_kit_" + "_".join(str(s) for s in sorted(slots))


def read_varlen(d, i):
    v = 0
    while True:
        b = d[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not (b & 0x80):
            return v, i


def song_programs(path):
    """program -> Counter(pitch)."""
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


def is_imported(label):
    rest = label[4:] if label.startswith("mus_") else ""
    return any(rest.startswith(p + "_") for p in PACK_PREFIXES)


def drum_shaped(counter):
    n = sum(counter.values())
    if not n:
        return False, 0, 0.0, 0.0
    drum = sum(v for k, v in counter.items() if GM_DRUM_LO <= k <= GM_DRUM_HI) / n
    core = sum(v for k, v in counter.items() if k in KIT_KEYS) / n
    return (drum >= DRUM_SHARE and core >= CORE_SHARE), n, drum, core


def classify(repo):
    """label -> {program: (is_drum, notes, drum_share, core_share)}."""
    out = {}
    for p in sorted(glob.glob(os.path.join(repo, "sound/songs/midi/mus_*.mid"))):
        label = os.path.basename(p)[:-4]
        if not is_imported(label):
            continue
        progs = song_programs(p)
        out[label] = {c: drum_shaped(progs.get(c, collections.Counter()))
                      for c in CANDIDATES}
    return out


def base_table(text):
    m = re.search(r"^voice_group %s\b[^\n]*\n(.*)" % BASE_VG, text, re.M | re.S)
    if not m:
        return None
    slots = []
    for line in m.group(1).splitlines():
        s = line.strip()
        if s.startswith("@") or not s:
            continue
        if s.startswith("voice_group "):
            break
        if s.startswith("voice_"):
            slots.append(s)
        if len(slots) == 128:
            break
    return slots if len(slots) == 128 else None


def build_inc(slots, kit_slots):
    name = vg_name(kit_slots)
    body = []
    for i, s in enumerate(slots):
        body.append("\tvoice_keysplit_all %s" % KIT if i in kit_slots else "\t" + s)
    which = ", ".join("%d (%s)" % (s, GM_NAMES.get(s, "?")) for s in sorted(kit_slots))
    return (
        "@ Generated by tools/rogue/assign_drum_voicegroup.py.\n"
        "@ Regenerate rather than edit.\n"
        "@\n"
        "@ voicegroup_%s with a DRUM KIT at slot(s): %s.\n"
        "@ Everything else is identical to voicegroup_%s, and every\n"
        "@ sub-voicegroup, keysplit table and sample below is SHARED with it --\n"
        "@ this table costs 128 * 12 = 1536 bytes and nothing else.\n"
        "@\n"
        "@ The pack's rips carry no MIDI channel 10 and put drum tracks on\n"
        "@ melodic programs with GM drum-map notes. Songs that do that on\n"
        "@ exactly these slots are pointed here by midi.cfg. See\n"
        "@ docs/PROGRAM_13_SPLIT.md.\n"
        "\n"
        "voice_group %s\n%s\n"
        % (BASE_VG, which, BASE_VG, name, "\n".join(body))
    )


def normalise_cfg(cfg):
    """Reset every generated assignment back to the base voicegroup."""
    return re.sub(r"-G_%s(?:_kit_[0-9_]+|_drums)\b" % BASE_VG,
                  "-G_" + BASE_VG, cfg)


def current_vg(cfg, label):
    m = re.search(r"^%s\.mid:\s+(.*)$" % re.escape(label), cfg, re.M)
    if not m:
        return None
    g = re.search(r"-G(\S+)", m.group(1))
    return g.group(1) if g else None


def plan(repo):
    """(verdicts, label -> frozenset(kit slots)) for songs on the base bank."""
    cfg = normalise_cfg(open(os.path.join(repo, MIDI_CFG)).read())
    verdicts = classify(repo)
    assign = {}
    for label, per in verdicts.items():
        if current_vg(cfg, label) != "_" + BASE_VG:
            continue                      # ships its own voicegroup; not ours
        kits = frozenset(p for p, v in per.items() if v[0])
        if kits:
            assign[label] = kits
    return verdicts, assign, cfg


def write_doc(repo):
    verdicts, assign, _ = plan(repo)
    cfg = open(os.path.join(repo, MIDI_CFG)).read()

    ours = {l: v for l, v in verdicts.items()
            if (current_vg(cfg, l) or "").startswith("_" + BASE_VG)}
    combos = collections.Counter(assign.values())

    def table(rows):
        out = ["| song | program | notes | on GM keys 35-81 | on kick/snare/hat |",
               "|---|---:|---:|---:|---:|"]
        for label, prog, n, d, c in rows:
            out.append("| `%s` | %d %s | %d | %.0f%% | %.0f%% |"
                       % (label, prog, GM_NAMES.get(prog, ""), n,
                          100 * d, 100 * c))
        return "\n".join(out)

    moved = []
    for label in sorted(assign):
        for prog in sorted(assign[label]):
            _, n, d, c = verdicts[label][prog]
            moved.append((label, prog, n, d, c))

    near = []
    for label in sorted(ours):
        if label in assign:
            continue
        for prog in CANDIDATES:
            ok, n, d, c = verdicts[label][prog]
            if n and d >= 0.35:
                near.append((label, prog, n, d, c))
    near.sort(key=lambda r: -r[3])

    md = """# Drum tracks on melodic programs

Which imported songs get a drum kit at which GM program, and why one shared
voicegroup cannot serve them all.

**Regenerate this file** — do not hand-edit it:

```bash
python3 tools/rogue/assign_drum_voicegroup.py . --doc
```

## The short version

`voicegroup_all_instruments` is a faithful decompile of *All-Instrument Patch
(Emerald).ups*. **Verified, not assumed**: applying the patch and decompiling it
reproduces the file exactly — 112 voicegroups, 30 keysplit tables, 405 samples —
and slot 13 there really is `voice_keysplit voicegroup_ai_xylophone`. That is
correct General MIDI. GM keeps no drum kit among its 128 melodic programs,
because GM drums are a separate bank reached by being on **channel 10**.

m4a has no channel-10 concept. The program byte is only an index into the song's
voicegroup, and the pack's rips carry **no channel 10** — they put drum tracks on
melodic programs using GM drum-map notes (36 kick, 38 snare, 42 closed hat). So
those notes play as pitched instrument hits.

## Which programs, and how that was established

`audit_program_usage.py` sweeps all 128 programs.
`verify_program_audit.py` cross-checks its MIDI parser against **mid2agb's own
`.s` output** — the actual toolchain — and `confirm_findings.py` re-derives the
findings from that output independently. **Only programs flagged by both
readings are acted on:**

| program | GM name | .mid reading | mid2agb reading | |
|---:|---|---|---|---|
| 13 | Xylophone | 87%% / 37%% | 88%% / 38%% | acted on |
| 53 | Voice Oohs | 74%% / 34%% | 72%% / 32%% | acted on |
| 64 | Soprano Sax | 93%% / 39%% | 92%% / 27%% | acted on |
| 127 | Gunshot | 98%% / 83%% | 98%% / 73%% | **no action** — slot 127 is already `voice_keysplit_all` |
| 26 | El. Guitar jazz | 100%% / 21%% | — | **no action** — one source only |
| 55 | Orchestra Hit | 96%% / 27%% | — | **no action** — one source only |
| 37 | Slap Bass 2 | — | 78%% / 21%% | **no action** — one source only |
| 118 | Synth Drum | 86%% / 47%% | — | **no action** — one source only, and GM 118 *is* a drum |

Percentages are share of that program's notes on GM keys 35–81 / on kick, snare
and hat. The four "one source only" cases sit on the 20%% threshold and flip
depending on how notes are counted; they need listening, not a rule.

## One table per combination

Songs disagree about which slots are drums — some use 13 as a real xylophone
while using 64 as drums. A single bank with every candidate turned into a kit
would fix one song by breaking another. So there is one table per combination
actually needed:

| voicegroup | kit at | songs |
|---|---|---:|
%s

Each is **1,536 bytes** (128 × 12) and shares every sub-voicegroup, keysplit
table and sample with the original. `frlg_drumset` is `voice_group
frlg_drumset, 36`, so under `voice_keysplit_all` note 36 → kick, 38 → snare,
39 → clap, 41 → tom: the GM drum map. It has 54 voices covering 36–89, the widest
kit in the tree, and 52 vanilla voicegroups already reference it.

## How a song is classified

A program is a drum track for a song only when **≥70%%** of that song's notes on
it fall on GM keys 35–81 **and ≥20%%** land on kick/snare/hat. Deliberately
conservative: a song wrongly moved to drums sounds broken, while one left behind
merely sounds as it did before.

**Known gap:** some drum-map notes fall below `frlg_drumset`'s base of 36 (note
35 is GM acoustic bass drum) and are not covered. No kit in the tree spans 35–89.

## Moved to a kit (%d assignments across %d songs)

%s

## Left melodic, but worth a listen (%d)

Program is used with ≥35%% of its notes on GM drum keys but did not meet the bar.
Sorted most drum-like first.

%s

## Unaffected

- **%d songs** ship their **own per-song voicegroup** (jorts, nico, aqua imports)
  and were never on `all_instruments`.
""" % ("\n".join("| `voicegroup_%s` | %s | %d |"
                 % (vg_name(k), ", ".join(str(s) for s in sorted(k)), v)
                 for k, v in sorted(combos.items(), key=lambda kv: -kv[1])),
       len(moved), len(assign), table(moved),
       len(near), table(near),
       len(verdicts) - len(ours))

    out = os.path.join(repo, DOC_PATH)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", newline="\n") as f:
        f.write(md)
    print("wrote %s" % DOC_PATH)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("repo_pos", nargs="?",
                    default=os.environ.get("POKEDECOMP_REPO", "."))
    ap.add_argument("--repo", dest="repo_kw")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--doc", action="store_true")
    args = ap.parse_args()
    repo = args.repo_kw or args.repo_pos

    if args.doc:
        return write_doc(repo)

    vg_text = open(os.path.join(repo, VG_FILE)).read()
    slots = base_table(vg_text)
    if slots is None:
        print("could not read 128 slots from %s" % VG_FILE)
        return 1
    bad = base_is_unedited(slots)
    if bad:
        for p in bad:
            print("slot %d of %s is already a drum kit:\n    %s"
                  % (p, BASE_VG, slots[p]))
        print("Refusing to generate from an edited base -- revert %s to the"
              " decompile first, or the override becomes permanent." % VG_FILE)
        return 1

    verdicts, assign, cfg = plan(repo)
    combos = collections.Counter(assign.values())

    print("%d imported songs, %d get a kit\n" % (len(verdicts), len(assign)))
    for k, v in sorted(combos.items(), key=lambda kv: -kv[1]):
        print("  voicegroup_%-34s kit at %-12s %3d songs"
              % (vg_name(k), ",".join(str(s) for s in sorted(k)), v))
    for prog in CANDIDATES:
        ls = sorted(l for l, k in assign.items() if prog in k)
        print("\n  program %d (%s): %d songs" % (prog, GM_NAMES[prog], len(ls)))
        for l in ls[:6]:
            _, n, d, c = verdicts[l][prog]
            print("      %-44s %5d notes %3.0f%%/%3.0f%%"
                  % (l, n, 100 * d, 100 * c))
        if len(ls) > 6:
            print("      ... and %d more" % (len(ls) - 6))

    if not args.apply:
        print("\n(dry run -- pass --apply to write)")
        return 0

    # 1. remove stale generated tables, write the needed ones
    for old in glob.glob(os.path.join(repo, VG_DIR, BASE_VG + "_kit_*.inc")) + \
               glob.glob(os.path.join(repo, VG_DIR, BASE_VG + "_drums.inc")):
        os.remove(old)
    for k in combos:
        with open(os.path.join(repo, VG_DIR, vg_name(k) + ".inc"), "w",
                  newline="\n") as f:
            f.write(build_inc(slots, k))

    # 2. rewrite the include list
    inc_path = os.path.join(repo, INCLUDE_FILE)
    inc = open(inc_path).read()
    inc = re.sub(r'^\.include "%s/%s(?:_kit_[0-9_]+|_drums)\.inc"\n'
                 % (re.escape(VG_DIR), BASE_VG), "", inc, flags=re.M)
    anchor = '.include "%s"\n' % VG_FILE
    if anchor not in inc:
        print("could not find %s in %s" % (VG_FILE, INCLUDE_FILE))
        return 1
    added = "".join('.include "%s/%s.inc"\n' % (VG_DIR, vg_name(k))
                    for k in sorted(combos, key=lambda k: sorted(k)))
    inc = inc.replace(anchor, anchor + added, 1)
    with open(inc_path, "w", newline="\n") as f:
        f.write(inc)

    # 3. point the songs at them
    cfg_path = os.path.join(repo, MIDI_CFG)
    cfg = normalise_cfg(open(cfg_path).read())
    changed = 0
    for label, kits in assign.items():
        pat = re.compile(r"^(%s\.mid:\s+.*?)-G_%s\b"
                         % (re.escape(label), BASE_VG), re.M)
        cfg, n = pat.subn(lambda m: m.group(1) + "-G_" + vg_name(kits), cfg,
                          count=1)
        changed += n
    with open(cfg_path, "w", newline="\n") as f:
        f.write(cfg)

    print("\nwrote %d voicegroups and pointed %d songs at them."
          % (len(combos), changed))
    if changed != len(assign):
        print("WARNING: %d songs to assign but %d rows changed"
              % (len(assign), changed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
