"""Build a GBA voicegroup that mirrors a DS bank, so a rip plays its own samples.

WHAT THIS BUYS. Mapped onto voicegroup_all_instruments, a DPPt rip gets the
instrument FAMILY right and the sound approximately - GBA strings standing in
for DPPt strings. Given its own voicegroup built from the cart's samples, it
plays the actual instruments the DS played. For SEQ_THE_EVENT03 that is 98 KB.

THE PROGRAM NUMBERS THEN BECOME CORRECT ON THEIR OWN, which is the neatest part.
This emits a root voicegroup indexed the same way the DS bank is, so the rip's
untouched program-change events - 3, 47, 48, 56, 60 - select the same
instruments they selected on the DS. Every remap made while guessing should be
REVERTED before using this; they exist only to survive a GM bank.

ADSR IS RESCALED, NOT COPIED. DS envelope fields are 0-127 and GBA's are 0-255,
so each is scaled by 255/127. The curves are not identical and this is an
approximation, but it is the right order of magnitude - and it preserves the one
thing that matters most, whether a voice speaks instantly or swells in.

KEY SPLITS BECOME A TABLE PLUS A SUB-VOICEGROUP, which is exactly how
all_instruments is built. A DS key split carries up to eight regions, each with
an upper key bound; the GBA form is a 128-byte table mapping every key to a
region index, and a sub-voicegroup holding one voice per region.

Run:  python3 tools/rogue/rip_sdat_voicegroup.py ROM.nds --seq SEQ_THE_EVENT03 \\
          --name dp_event03 --programs 1,3,47,48,56,60 --repo .
"""
import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rip_sdat_samples import (          # noqa: E402
    Sdat, find_sdat, swar_offsets, read_swav, write_wav, u8, u16, u32, SEQ,
)


# THE BASE KEY IS THE DS INSTRUMENT'S OWN NOTE, and the sample carries no
# transposition. This took two wrong builds to get right, so both wrong answers
# are recorded here.
#
# FIRST WRONG ANSWER: base key = the DS note AND the sample's pitch scaled to
# middle C from that same note. Both halves transposing gives eight octaves of
# error - noise.
#
# SECOND WRONG ANSWER: base key 60, copying vanilla, with the transposition left
# in the sample. Vanilla can do that because vanilla's samples are RECORDED at
# middle C, so their pitch words sit around 20,000,000 - one sample rate. A DS
# sample recorded at note 24 needs a pitch of 131,260,416 to reach middle C,
# 6.5x outside the range anything else in the ROM uses, and it still played as
# high-pitched noise.
#
# THIRD WRONG ANSWER, AND THE ONE THAT SHIPPED: the sample keeps its own rate
# (midiKey 60, "no transposition") and the VOICE says which note that rate
# corresponds to. This is the one that reads best and is the most wrong, because
# NEITHER HALF CARRIES THE OFFSET - the voice's base_midi_key is a byte the
# engine does not read for a melodic voice. See BASE_NOTE_GOES_IN_FREQ.
#
# RIGHT ANSWER: the offset goes in WaveData.freq, which is what the second
# attempt did. It was rejected on a bad measurement - see below.


# BASE_NOTE_GOES_IN_FREQ. m4a resolves a note's pitch key three ways
# (src/m4a_1.s:996-1039), and only one of them looks at the voice:
#
#     plain voice_directsound   key = track.key, the played note
#     voice_keysplit    (SPL)   picks a sub-voice, key STAYS the played note
#     voice_keysplit_all (RHY)  key = the sub-voice's own key field (m4a_1.s:1035)
#
# That key goes to MidiKeyToFreq at m4a_1.s:1175, which returns
# wav->freq * scale(key). So for anything that is not a keysplit_all,
# `voice_directsound <note>` is DEAD DATA and the whole transposition must be
# in freq. WaveData.freq means "the rate that would sound middle C", times 1024.
#
# Getting this wrong is silent and total: 910 of 1156 voices played between
# three octaves flat and three octaves sharp, with clean samples, a correct
# sequence and correct envelopes. Every DPPt track on these banks was unusable
# and nothing else in the ROM was affected.
#
# WHY THE SECOND ATTEMPT WAS REJECTED, AND WHY THAT WAS WRONG. It was thrown out
# because a note-24 sample needs freq 131,260,416, called "6.5x outside the range
# anything else in the ROM uses". That compared against the wrong thing. In
# voicegroup_all_instruments, in this same ROM:
#
#     ai_pick_bass_00     128,022,697
#     ai_contrabass_00    134,232,400
#     ai_cello_00         142,296,444
#
# 131 million is not out of range, it is smaller than two values already
# shipping beside it. A low-register sample is SUPPOSED to carry a large freq -
# that is what "the rate that would sound middle C" means. Corrected, this rip
# spans 2,049,024 to 143,368,983, inside all_instruments' own 1,339,712 to
# 142,296,444, and nothing overflows u32.
#
# Confirmed against CyanSMP64/pokeemerald music_expansion_v3, where 84% of
# voices are base key 60 and the pitch is in freq throughout.


# DECAY AND RELEASE POINT THE OPPOSITE WAY ON THE TWO MACHINES, and rescaling
# one onto the other by 255/127 does not merely approximate - it inverts.
#
# GBA, from the mixer this repo actually builds. src/m4a_hq_mixer.s,
# C_adsr_decay_check and the release handler both do
#
#     MULS R5, R5, <field>  /  LSRS R5, #8
#
# so the field is a MULTIPLIER OUT OF 256 applied once per frame. 255 means
# x0.996 per frame - twenty-three seconds to fall silent - and 0 means instant.
# BIGGER IS SLOWER.
#
# DS: the field is a RATE SUBTRACTED from a logarithmic envelope, so 127 is
# instant and 0 is effectively never. BIGGER IS FASTER.
#
# THE BANK'S OWN DATA SETTLES IT WITHOUT TRUSTING EITHER DESCRIPTION. Across the
# four ripped banks, 1018 of 1038 instrument regions carry a release of 110 or
# more and NOT ONE carries 20 or less. Under "bigger is slower" every instrument
# in Diamond would ring for several seconds after note-off, which no sample bank
# does. Under "bigger is faster" they all stop promptly, which is what a bank
# looks like. The distribution only has one reading.
#
# What the old mapping did, on the values that actually occur:
#
#     DS 127 (instant)  -> GBA 255  = 23.6 s     3208x too long, 423 regions
#     DS 126 (0.03 s)   -> GBA 253  =  7.8 s      250x too long
#     DS 125 (0.06 s)   -> GBA 251  =  4.7 s       75x too long
#     DS  70 (3.5 s)    -> GBA 141  =  0.15 s      23x too SHORT
#
# The curve is not shifted, it is reflected: too long at the fast end and too
# short at the slow end. With twelve DirectSound channels and every note ringing
# for seconds, the mixer saturates and later notes are dropped - which presents
# as "quiet and muddy" rather than as an envelope fault.
DS_ENV_STEPS = 92544       # full envelope span in the DS's log units
DS_ENV_HZ = 192            # the DS envelope steps 192 times a second
GBA_ENV_HZ = 60            # m4a steps once per frame


def scale(v):
    """DS 0-127 envelope LEVEL onto GBA's 0-255.

    Correct for attack, whose polarity does match (bigger is faster on both,
    and 127 -> 255 is instant on both). NOT for decay or release: use fall().
    """
    return max(0, min(255, round(v * 255 / 127)))


def ds_rate(v):
    """The DS's own rate table for a decay/release field."""
    if v >= 127:
        return 0xFFFF
    if v == 126:
        return 0x3C00
    if v < 50:
        return v * 2 + 1
    return 0x1E00 // (126 - v)


def ds_env_seconds(v):
    """How long a DS decay/release field takes to run the envelope down."""
    return (DS_ENV_STEPS / ds_rate(v)) / DS_ENV_HZ


def gba_falloff(seconds):
    """The m4a multiplier that falls silent in `seconds`.

    level *= r/256 each frame, so falling from 255 to 1 takes
    ln(255) / ln(256/r) frames. Inverted for r.
    """
    frames = seconds * GBA_ENV_HZ
    if frames <= 0.05:
        return 0
    return max(0, min(255, round(256 * math.exp(-math.log(255.0) / frames))))


def fall(v):
    """A DS decay/release field as the GBA multiplier of the same duration."""
    return gba_falloff(ds_env_seconds(v))


def attack_of(v):
    """A DS attack field as a GBA attack summand, never 0.

    C_adsr_attack does `ADDS R5, attack` with no floor, so an attack of 0 never
    raises the level off zero and the voice is silent for as long as it is held.
    The banks here bottom out at 3, so this floor never fires today - it is here
    because the failure is total silence rather than a wrong sound.
    """
    return max(1, scale(v))


def bank_types(blob):
    """{program: raw SBNK instrument type}, including the ones we cannot emit."""
    return {p: blob[0x3C + 4 * p] for p in range(u32(blob, 0x38))}


def bank_detail(blob):
    """{program: (type, [(swav, swar, key, A, D, S, R, upper_bound)])}"""
    out = {}
    for prog in range(u32(blob, 0x38)):
        rec = 0x3C + 4 * prog
        typ, off = blob[rec], u16(blob, rec + 1)
        if typ == 0:
            continue
        regions = []
        if typ == 1:
            regions.append((u16(blob, off), u16(blob, off + 2), u8(blob, off + 4),
                            u8(blob, off + 5), u8(blob, off + 6),
                            u8(blob, off + 7), u8(blob, off + 8), 127))
        elif typ in (16, 17):
            if typ == 16:
                lo, hi = blob[off], blob[off + 1]
                bounds = list(range(lo, hi + 1))
                cur = off + 2
            else:
                raw = [blob[off + i] for i in range(8)]
                bounds = [b for b in raw if b]
                cur = off + 8
            for b in bounds:
                regions.append((u16(blob, cur + 2), u16(blob, cur + 4),
                                u8(blob, cur + 6), u8(blob, cur + 7),
                                u8(blob, cur + 8), u8(blob, cur + 9),
                                u8(blob, cur + 10), b))
                cur += 12
        else:
            continue
        out[prog] = (typ, regions)
    return out


def selftest():
    """The polarity assertions, because the wrong answer built and played."""
    fails = []

    def check(name, ok):
        print(("PASS  " if ok else "FAIL  ") + name)
        if not ok:
            fails.append(name)

    # The bug itself: the fields must run OPPOSITE ways, so the mapping has to
    # be decreasing. The old code was `scale`, which is increasing.
    check("fall() is decreasing - a faster DS rate is a smaller GBA multiplier",
          fall(127) < fall(120) < fall(90) < fall(50) < fall(0))
    check("scale() is increasing, which is why it was wrong for decay/release",
          scale(0) < scale(50) < scale(90) < scale(127))
    check("the old mapping and the new one disagree at the common values",
          all(scale(v) != fall(v) for v in (127, 126, 125, 120, 116)))

    # Endpoints. An instant DS decay must be instant on the GBA, not eternal.
    check("DS 127 (instant) becomes GBA 0 (instant), not 255 (23 s)",
          fall(127) == 0)
    check("a slow DS rate becomes a large GBA multiplier", fall(0) == 255)

    # Durations round-trip to within a frame or so, which is the whole claim.
    for v in (126, 125, 120, 116, 110, 100, 80):
        want = ds_env_seconds(v)
        got = math.log(255.0) / math.log(256.0 / fall(v)) / GBA_ENV_HZ
        check(f"DS {v} keeps its duration ({want:.3f}s -> {got:.3f}s)",
              abs(got - want) < max(0.05, want * 0.12))

    # THE PITCH ASSERTIONS. The failure these guard was a correction written
    # into a field nothing reads, so testing the emitted numbers is the only
    # thing that would have caught it.
    import struct as _struct
    import tempfile as _tempfile

    def wav_midikey(path):
        raw = Path(path).read_bytes()
        i = raw.find(b"smpl")
        return _struct.unpack_from("<I", raw, i + 8 + 12)[0] if i > 0 else None

    with _tempfile.TemporaryDirectory() as td:
        pcm = [0, 1000, -1000, 0]
        for note in (24, 60, 96):
            p = Path(td) / f"n{note}.wav"
            write_wav(p, pcm, 16000, note)
            check(f"a note-{note} sample records midiKey {note}, not 60",
                  wav_midikey(p) == note)
        # wav2agb turns that into pitch = rate * 2^((60-key)/12). Check the
        # arithmetic we are relying on, at the two extremes that matter.
        for note, want in ((24, 16000 * 8), (60, 16000), (96, 16000 / 8)):
            got = 16000 * (2 ** ((60 - note) / 12.0))
            check(f"note {note} implies a middle-C rate of {want:.0f} Hz",
                  abs(got - want) < 1)

    check("a low sample's freq lands inside all_instruments' range "
          "(ai_cello_00 is 142,296,444)",
          16000 * 8 * 1024 < 142296444 * 1.01)

    # Attack keeps the polarity it always had, and never reaches 0 - which the
    # mixer would hold silent forever.
    check("attack stays increasing", attack_of(0) < attack_of(64) < attack_of(127))
    check("attack 127 is instant", attack_of(127) == 255)
    check("attack is floored at 1, never 0", attack_of(0) >= 1)

    print()
    print(f"{len(fails)} failed" if fails else "all passed")
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser()
    if "--selftest" in sys.argv:
        return selftest()
    ap.add_argument("rom")
    ap.add_argument("--seq", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--programs")
    ap.add_argument("--repo", default=".")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    repo = Path(args.repo)
    s = Sdat(find_sdat(open(args.rom, "rb").read()))
    names = s.names(SEQ)
    if args.seq not in names:
        raise SystemExit(f"{args.seq} not found")
    bi = s.bank_info(s.seq_info(names.index(args.seq))["bank"])
    detail = bank_detail(s.file(bi["fileId"]))

    # slot -> (blob, offsets, wave archive ID). THE ID MATTERS, not the slot.
    # Every DPPt music bank has wavearc 1000 in slot 0 but a different archive
    # in slot 1, so naming samples by slot makes bank 1001's slot-1 samples
    # collide with bank 1002's - same name, different audio. Naming by archive
    # id instead makes the 163 samples every bank shares literally the same
    # files, which is what turns a second bank from 950 KB into 60 KB.
    arcs = {}
    for slot, wa in enumerate(bi["wa"]):
        if wa != 0xFFFF:
            blob = s.file(s.wavearc_info(wa)["fileId"])
            arcs[slot] = (blob, swar_offsets(blob), wa)

    wanted = ({int(x) for x in args.programs.replace(" ", "").split(",")}
              if args.programs else set(detail))
    wanted &= set(detail)
    if not wanted:
        raise SystemExit("no matching programs in this bank")

    sample_dir = repo / "sound/direct_sound_samples"
    written = {}
    shared = set()
    total = 0
    for prog in sorted(wanted):
        for (swav, swar, key, a, d, sus, r, _b) in detail[prog][1]:
            if (swav, swar, key) in written or swar not in arcs:
                continue
            blob, offs, waid = arcs[swar]
            if swav >= len(offs):
                continue
            w = read_swav(blob, offs[swav])
            if not w or not w["pcm"]:
                continue
            # THE ARCHIVE INDEX IS PART OF THE NAME, and leaving it out cost a
            # silent overwrite: a bank can reference the same swav index in two
            # different wave archives, so 172 samples collapsed onto 163 files
            # and nine instruments quietly got the wrong audio. Same shape as
            # the drumset/song basename collision in import_midi_pack.py.
            #
            # THE BASE NOTE IS PART OF THE NAME TOO, because it is baked into
            # the file. The transposition has to live in WaveData.freq (see
            # BASE_NOTE_GOES_IN_FREQ below), freq is per-SAMPLE, and 24 of these
            # samples are used at more than one note - dp_wa1000_127 at five of
            # them. Sharing one file between two notes would silently give one
            # of them the other's pitch, so identity is (archive, swav, note).
            nm = f"dp_wa{waid}_{swav:03d}_k{key}"
            written[(swav, swar, key)] = nm
            if (sample_dir / f"{nm}.wav").exists():
                shared.add(nm)
            else:
                total += len(w["pcm"])
            if not args.dry_run and nm not in shared:
                # midi_key = the DS note, NOT 60. wav2agb then computes
                # pitch = rate * 2^((60 - key)/12), which is the middle-C rate
                # the engine expects.
                write_wav(sample_dir / f"{nm}.wav", w["pcm"], w["rate"], key,
                          w["loop_start"] if w["loop"] else None,
                          len(w["pcm"]) if w["loop"] else None)

    # --- the voicegroup source -------------------------------------------
    lines = [
        f"@ Generated by tools/rogue/rip_sdat_voicegroup.py from {args.seq}.",
        "@ Regenerate rather than edit.",
        "@",
        "@ Mirrors the DS bank slot for slot, so the rip's own program-change",
        "@ events select the right instruments with no remapping.",
        "",
    ]
    subs = []
    highest = max(wanted)

    for prog in sorted(wanted):
        typ, regions = detail[prog]
        regions = [g for g in regions if (g[0], g[1], g[2]) in written]
        if not regions:
            continue
        if len(regions) == 1:
            continue                      # emitted inline in the root below
        table = [0] * 128
        low = 0
        for idx, g in enumerate(regions):
            hi = g[7]
            for k in range(low, min(hi, 127) + 1):
                table[k] = idx
            low = hi + 1
        for k in range(low, 128):
            table[k] = len(regions) - 1
        subs.append(f"keysplit_{args.name}_p{prog}::")
        for i in range(0, 128, 16):
            subs.append("\t.byte " + ", ".join(str(v) for v in table[i:i + 16]))
        subs.append("")
        subs.append(f"voice_group {args.name}_p{prog}")
        for g in regions:
            swav, swar, key, a, d, sus, r, _b = g
            subs.append(f"\tvoice_directsound 60, 0, "
                        f"DirectSoundWaveData_{written[(swav, swar, key)]}, "
                        f"{attack_of(a)}, {fall(d)}, {scale(sus)}, {fall(r)}"
                        f" @ ds note {key}")
        subs.append("")

    # AN EMPTY DS SLOT AND ONE WE DROPPED ARE NOT THE SAME THING, and the filler
    # voice looks identical either way. A type-0 program is empty on the DS too,
    # so a rip selecting it is faithful and plays nothing on both machines. A
    # type we cannot emit - PSG square (2) and noise (3), ten per bank - is a
    # GAP: the DS plays a sound there and we do not. Only the second is a bug,
    # so the comment says which, and check_ds_voicegroups.py keys on it.
    raw_types = bank_types(s.file(bi["fileId"]))
    TYPE_NAME = {2: "psg square", 3: "psg noise", 4: "direct"}

    def filler(prog):
        t = raw_types.get(prog, 0)
        why = "unused" if t == 0 else f"DROPPED {TYPE_NAME.get(t, 'type %d' % t)}"
        return ("\tvoice_directsound 60, 0, DirectSoundWaveData_"
                f"drum_and_percussion_kick, 0, 0, 0, 0 @ {why}")

    lines += subs
    lines.append(f"voice_group {args.name}")
    for prog in range(highest + 1):
        if prog not in wanted or prog not in detail:
            lines.append(filler(prog))
            continue
        typ, regions = detail[prog]
        regions = [g for g in regions if (g[0], g[1], g[2]) in written]
        if not regions:
            lines.append(filler(prog))
        elif len(regions) == 1:
            swav, swar, key, a, d, sus, r, _b = regions[0]
            lines.append(f"\tvoice_directsound 60, 0, "
                         f"DirectSoundWaveData_{written[(swav, swar, key)]}, "
                         f"{attack_of(a)}, {fall(d)}, {scale(sus)}, "
                         f"{fall(r)} @ {prog}, ds note {key}")
        else:
            lines.append(f"\tvoice_keysplit voicegroup_{args.name}_p{prog}, "
                         f"keysplit_{args.name}_p{prog} @ {prog}")
    text = "\n".join(lines) + "\n"

    vg_path = repo / f"sound/voicegroups/imported/ds/{args.name}.inc"
    # `.align 2` IS NOT DECORATION, IT IS THE WHOLE THING. struct WaveData is
    # { u16 type; u16 status; u32 freq; u32 loopStart; u32 size; } and the mixer
    # loads the three u32s with 32-bit ARM loads. An unaligned LDR on ARM7 does
    # NOT fault - it rotates the word it fetched. A blob starting two bytes off
    # therefore hands the mixer a rotated rate, a rotated loop point and a
    # rotated length:
    #
    #     dp_wa1000_001_k36   freq 65,646,592  ->  read as 1,073,786,880
    #
    # which is a 1.05 MHz playback rate: the mixer steps ~78 samples per output
    # sample, leaves the buffer in milliseconds and reads on into whatever ROM
    # follows. That is the "pulled cart" noise, and it made every track on these
    # banks unusable while every other song in the ROM was fine.
    #
    # Sample lengths are arbitrary, so without this EVERY entry after the first
    # odd-length one is off. It was 229 of 229. The rest of
    # direct_sound_data.inc carries 1363 of these directives; this generator
    # carried none, and nothing could see it: the data at each symbol is
    # correct, so reading a header host-side in Python - which ignores
    # alignment - shows perfect values. Only the engine's load is wrong.
    decl = "\n".join(
        f'\t.align 2\n'
        f'DirectSoundWaveData_{nm}::\n'
        f'\t.incbin "sound/direct_sound_samples/{nm}.bin"'
        for nm in sorted(written.values())) + "\n"

    print(f"{len(written)} samples referenced, {len(shared)} already ripped "
          f"by another bank; {len(written) - len(shared)} new "
          f"({total/1024:.1f} KB as 8-bit)")
    print(f"voicegroup {args.name}: {highest + 1} slots, "
          f"{sum(1 for p in wanted if len(detail[p][1]) > 1)} key splits")
    if args.dry_run:
        print("\n(dry run) voicegroup would be:", vg_path)
        print(text[:1200])
        return 0

    vg_path.parent.mkdir(parents=True, exist_ok=True)
    vg_path.write_text(text, newline="\n")
    print(f"wrote {vg_path}")

    dsd = repo / "sound/direct_sound_data.inc"
    cur = dsd.read_text(errors="replace")
    # Filter by ENTRY, not by line. The old line-wise filter asked whether each
    # line already appeared anywhere in the file, which is true of every
    # `.align 2` - there are 1363 of them - so it silently ate the alignment
    # directive off every entry it emitted and kept the label. That is how 229
    # of 229 samples ended up unaligned.
    add = []
    for nm in sorted(written.values()):
        if f"DirectSoundWaveData_{nm}::" in cur:
            continue
        add.append(f'\t.align 2\n'
                   f'DirectSoundWaveData_{nm}::\n'
                   f'\t.incbin "sound/direct_sound_samples/{nm}.bin"')
    if add:
        dsd.write_text(cur.rstrip("\n") + "\n\n" + "\n".join(add) + "\n",
                       newline="\n")
        print(f"declared {len(add)} samples in direct_sound_data.inc")

    vgi = repo / "sound/voice_groups.inc"
    inc = f'.include "sound/voicegroups/imported/ds/{args.name}.inc"'
    cur = vgi.read_text(errors="replace")
    if inc not in cur:
        vgi.write_text(cur.rstrip("\n") + "\n" + inc + "\n", newline="\n")
        print("added the include to voice_groups.inc")
    return 0


if __name__ == "__main__":
    sys.exit(main())
