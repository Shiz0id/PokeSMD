#!/usr/bin/env python3
"""Build a parallel sample set from gm.dls, for an A/B against a decompiled one.

The All-Instrument Patch's samples are the Windows GS wavetable already
downsampled once for the GBA. gm.dls is that wavetable at source: 16-bit, at
its own rate, with the publisher's own loop points. Going straight to it means
one conversion instead of two.

MATCHING IS BY INDEX, NOT BY EAR, and only because the decompiled bank turned
out to be General MIDI ordered. Program N there is program N here. If that ever
stops being true for a bank, this tool is the wrong approach for it and the
match has to be made some other way.

NOTHING IS RESAMPLED. An earlier cut of this tool forced every sample to the
length of its decompiled counterpart, which time-stretches the audio and
therefore transposes it -- gm.dls unity notes run from 48 to 85, so that put
most of the bank at the wrong pitch. The samples are stored exactly as gm.dls
holds them and the GBA pitch word is computed from the region's unity note and
fine tune, which is what that word is for. The hardware resamples at playback.

Truncation is at the loop end, because m4a plays to `size` and jumps to
`loopStart`; anything past the loop end is data the ROM would carry and never
play.

EACH SAMPLE IS PEAK-NORMALISED, which reverses an earlier decision here. The
argument for a single shared gain was that per-sample normalisation changes the
balance between instruments and flatters whichever bank ends up louder. That is
true and it is also beside the point: at 8 bits the bit depth IS the dynamic
range, and a shared gain left the piano -- the most used instrument in the bank
-- peaking at 40 of 127, throwing away nearly two bits and putting quantisation
noise right under the melody.

Censused rather than argued: vanilla's own samples peak at 115, 115, 128, 127,
128. The engine expects normalised samples and balances with track volume and
velocity, which is where balance belongs. DLS attenuation is a playback-time
mix parameter, not something to bake into the sample.

The voicegroup keeps the decompiled bank's keys, pans and envelopes exactly --
it is produced by substituting symbol names in that .inc, not by regenerating
it -- so the ear is judging sample audio and nothing else.

Usage:
    extract_gmdls.py --repo PATH                 # write the parallel bank
    extract_gmdls.py --repo PATH --inspect       # report matches, write nothing
    extract_gmdls.py --dls /path/to/gm.dls --repo PATH
"""

import argparse
import hashlib
import json
import os
import re
import struct
import sys

DEFAULT_DLS = "/mnt/c/Windows/System32/drivers/gm.dls"

F_WAVELINK_PHASE_MASTER = 0x0001
DRUM_BANK_BIT = 0x80000000


# --------------------------------------------------------------------------
# RIFF / DLS parsing
# --------------------------------------------------------------------------

def iter_chunks(buf, start, end):
    i = start
    while i + 8 <= end:
        cid = buf[i:i + 4]
        size = struct.unpack_from("<I", buf, i + 4)[0]
        if i + 8 + size > end:
            break
        yield cid, i + 8, size
        i += 8 + size + (size & 1)


def list_type(buf, off):
    return buf[off:off + 4]


def parse_wsmp(buf, off, size):
    unity, fine = struct.unpack_from("<Hh", buf, off + 4)
    attenuation, options, num_loops = struct.unpack_from("<iII", buf, off + 8)
    loop = None
    if num_loops:
        lo = off + 20
        _, ltype, lstart, llen = struct.unpack_from("<IIII", buf, lo)
        loop = (lstart, llen, ltype)
    return {"unity": unity, "fine": fine, "attenuation": attenuation,
            "loop": loop}


def parse_dls(data):
    if data[:4] != b"RIFF" or data[8:12] != b"DLS ":
        raise SystemExit("not a DLS file")

    lins = ptbl = wvpl = None
    for cid, off, size in iter_chunks(data, 12, len(data)):
        if cid == b"LIST":
            t = list_type(data, off)
            if t == b"lins":
                lins = (off + 4, off + size)
            elif t == b"wvpl":
                wvpl = (off + 4, off + size)
        elif cid == b"ptbl":
            ptbl = (off, size)

    if not (lins and ptbl and wvpl):
        raise SystemExit("gm.dls is missing lins, ptbl or wvpl")

    # Pool table: cue offsets relative to the start of wvpl's payload.
    cb, ccues = struct.unpack_from("<II", data, ptbl[0])
    cues = list(struct.unpack_from("<%dI" % ccues, data, ptbl[0] + cb))
    wave_offsets = [wvpl[0] + c for c in cues]

    instruments = {}
    for cid, off, size in iter_chunks(data, *lins):
        if cid != b"LIST" or list_type(data, off) != b"ins ":
            continue
        ins_start, ins_end = off + 4, off + size

        header = None
        regions = []
        for c2, o2, s2 in iter_chunks(data, ins_start, ins_end):
            if c2 == b"insh":
                _, bank, program = struct.unpack_from("<III", data, o2)
                header = (bank, program)
            elif c2 == b"LIST" and list_type(data, o2) == b"lrgn":
                for c3, o3, s3 in iter_chunks(data, o2 + 4, o2 + s2):
                    if c3 != b"LIST" or list_type(data, o3) not in (b"rgn ", b"rgn2"):
                        continue
                    reg = {"key_low": 0, "key_high": 127, "wsmp": None,
                           "table_index": None}
                    for c4, o4, s4 in iter_chunks(data, o3 + 4, o3 + s3):
                        if c4 == b"rgnh":
                            klo, khi = struct.unpack_from("<HH", data, o4)
                            reg["key_low"], reg["key_high"] = klo, khi
                        elif c4 == b"wsmp":
                            reg["wsmp"] = parse_wsmp(data, o4, s4)
                        elif c4 == b"wlnk":
                            # fusOptions:u16 usPhaseGroup:u16 ulChannel:u32
                            # ulTableIndex:u32 -- the index is at offset 8.
                            reg["table_index"] = struct.unpack_from(
                                "<I", data, o4 + 8)[0]
                    if reg["table_index"] is not None:
                        regions.append(reg)

        if header:
            instruments.setdefault(header, []).extend(regions)

    return instruments, wave_offsets


def read_wave(data, off):
    """One LIST 'wave' out of the pool -> rate, 16-bit mono samples, wsmp."""
    if data[off:off + 4] != b"LIST" or list_type(data, off + 8) != b"wave":
        raise ValueError("no wave at %08x" % off)
    size = struct.unpack_from("<I", data, off + 4)[0]
    start, end = off + 12, off + 8 + size

    fmt = pcm = None
    wsmp = None
    for cid, o, s in iter_chunks(data, start, end):
        if cid == b"fmt ":
            tag, channels, rate, _, align, bits = struct.unpack_from(
                "<HHIIHH", data, o)
            fmt = (tag, channels, rate, bits)
        elif cid == b"data":
            pcm = data[o:o + s]
        elif cid == b"wsmp":
            wsmp = parse_wsmp(data, o, s)

    if fmt is None or pcm is None:
        raise ValueError("wave at %08x has no fmt or data" % off)

    tag, channels, rate, bits = fmt
    if tag != 1 or bits not in (8, 16):
        raise ValueError("wave at %08x is format %d/%d-bit, unsupported"
                         % (off, tag, bits))

    if bits == 16:
        samples = list(struct.unpack("<%dh" % (len(pcm) // 2), pcm[:len(pcm) // 2 * 2]))
    else:
        samples = [(b - 128) * 256 for b in pcm]

    if channels == 2:
        samples = [(samples[i] + samples[i + 1]) // 2
                   for i in range(0, len(samples) - 1, 2)]

    return {"rate": rate, "samples": samples, "wsmp": wsmp}


# --------------------------------------------------------------------------
# Conversion
# --------------------------------------------------------------------------

def agb_pitch(src_rate, unity, fine_cents):
    """The GBA pitch word: the playback rate that makes this sample middle C.

    A sample recorded at src_rate sounds like note `unity`. Middle C is
    (60 - unity) semitones away, so it must be played that much slower or
    faster; fine tune is a further correction in cents, subtracted because a
    sample marked sharp has to be played flatter to come out right.
    """
    semitones = (60 - unity) - (fine_cents / 100.0)
    return int(round(src_rate * (2.0 ** (semitones / 12.0)) * 1024))


def dls_gain(attenuation):
    """DLS lAttenuation -> linear gain.

    gm.dls stores this in 1/655360 dB, not the 1/65536 dB a literal reading of
    the DLS spec suggests: at 1/65536 the piano would be attenuated 101 dB,
    which is silence. At 1/655360 it is -10.1 dB, which is a plausible mix
    level, and the whole bank lands in a sane range.
    """
    if attenuation is None:
        return 1.0
    db = attenuation / 655360.0
    return 10.0 ** (db / 20.0)


def chunk(cid, payload):
    out = cid + struct.pack("<I", len(payload)) + payload
    if len(payload) % 2:
        out += b"\x00"
    return out


def build_wav(pcm_s8, freq, loop_start, size, looped, midi_key=60):
    rate = freq // 1024 or 1
    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate, 1, 8)
    smpl = struct.pack("<IIIIIIIII", 0, 0, 1_000_000_000 // rate, midi_key,
                       0, 0, 0, 1 if looped else 0, 0)
    if looped:
        smpl += struct.pack("<IIIIII", 0, 0, loop_start, size - 1, 0, 0)

    body = b"WAVE"
    body += chunk(b"fmt ", fmt)
    body += chunk(b"smpl", smpl)
    body += chunk(b"agbp", struct.pack("<I", freq))
    body += chunk(b"agbl", struct.pack("<I", size))
    body += chunk(b"data", bytes((s + 128) & 0xFF for s in pcm_s8))
    return b"RIFF" + struct.pack("<I", len(body)) + body


def pick_region(regions, key=60):
    for r in regions:
        if r["key_low"] <= key <= r["key_high"]:
            return r
    return regions[0] if regions else None


def pick_region(regions, key):
    for r in regions:
        if r["key_low"] <= key <= r["key_high"]:
            return r
    # No region covers it. Take the nearest by range midpoint rather than the
    # first: gm.dls instruments are ordered by key and "first" is the bass end.
    return min(regions,
               key=lambda r: abs((r["key_low"] + r["key_high"]) // 2 - key)) \
        if regions else None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dls", default=DEFAULT_DLS)
    ap.add_argument("--repo", required=True)
    ap.add_argument("--manifest", default="all_instruments")
    ap.add_argument("--suffix", default="_gmdls")
    ap.add_argument("--label-prefix", default="ai_")
    ap.add_argument("--twin-prefix", default="aidls_")
    ap.add_argument("--inspect", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.dls):
        raise SystemExit("no gm.dls at %s" % args.dls)
    with open(args.dls, "rb") as f:
        dls = f.read()

    instruments, wave_offsets = parse_dls(dls)
    melodic = {prog: regs for (bank, prog), regs in instruments.items()
               if not (bank & DRUM_BANK_BIT)}
    print("gm.dls: %d melodic programs, %d waves in the pool"
          % (len(melodic), len(wave_offsets)))

    man_path = os.path.join(args.repo, "tools", "rogue",
                            "%s_manifest.json" % args.manifest)
    with open(man_path) as f:
        man = json.load(f)
    targets = sorted(man["samples"].items(),
                     key=lambda kv: (kv[1]["program"], kv[1]["key_lo"]))

    # MATCH PER KEY RANGE. The decompiled bank multi-samples each instrument,
    # and so does gm.dls, so the right counterpart to one of our samples is the
    # gm.dls REGION covering the same part of the keyboard -- not one sample per
    # program, which would throw the multi-sampling away and put a bass sample
    # under the top octave.
    decoded = {}   # our name -> record
    canon = {}     # gm.dls region identity -> first our-name that used it
    missing = []

    for name, meta in targets:
        prog = meta["program"]
        regs = melodic.get(prog)
        if not regs:
            missing.append((name, prog, "no melodic program %d" % prog))
            continue

        mid = (meta.get("key_lo", 0) + meta.get("key_hi", 127)) // 2
        reg = pick_region(regs, mid)
        if reg is None:
            missing.append((name, prog, "no regions"))
            continue

        ident = (reg["table_index"], id(reg["wsmp"]) if reg["wsmp"] else 0)
        if ident in canon:
            # Same gm.dls region as an earlier sample: share the file rather
            # than emitting the identical PCM twice.
            decoded[name] = {"alias": canon[ident]}
            continue

        try:
            wave = read_wave(dls, wave_offsets[reg["table_index"]])
        except (ValueError, IndexError) as e:
            missing.append((name, prog, str(e)))
            continue

        wsmp = reg["wsmp"] or wave["wsmp"]
        if wsmp is None:
            missing.append((name, prog, "no wsmp, so no unity note"))
            continue

        samples = wave["samples"]
        if wsmp["loop"]:
            lstart, llen, _ = wsmp["loop"]
            lend = min(lstart + llen, len(samples))
            looped = lstart < lend
        else:
            looped, lstart, lend = False, 0, len(samples)
        if not looped:
            lstart, lend = 0, len(samples)
        samples = samples[:lend]
        if not samples:
            missing.append((name, prog, "empty after truncating to loop end"))
            continue

        canon[ident] = name
        decoded[name] = {
            "pcm": samples,
            "freq": agb_pitch(wave["rate"], wsmp["unity"], wsmp["fine"]),
            "loop_start": lstart if looped else 0,
            "size": len(samples),
            "looped": looped,
            "program": prog, "mid": mid,
            "unity": wsmp["unity"], "src_rate": wave["rate"],
        }

    real = {k: v for k, v in decoded.items() if "alias" not in v}
    total = sum(16 + v["size"] + (-(16 + v["size"]) % 4) for v in real.values())
    print("matched %d of %d samples (%d distinct after sharing regions)"
          % (len(decoded), len(targets), len(real)))
    print("ROM: %d bytes (%.2f MB)" % (total, total / 1048576))
    if missing:
        print("unmatched: %d" % len(missing))
        for n, pr, why in missing[:6]:
            print("  program %3d %-28s %s" % (pr, n, why))

    if args.inspect:
        for name, v in list(real.items())[:12]:
            print("  prog %3d key~%3d %-30s unity %3d  %5.0f Hz at middle C,"
                  " %6d samples" % (v["program"], v["mid"], name, v["unity"],
                                    v["freq"] / 1024.0, v["size"]))
        print("\n(--inspect: nothing written)")
        return

    smp_dir = os.path.join(args.repo, "sound", "direct_sound_samples")
    twin = {}      # our name -> twin symbol name
    for name, v in decoded.items():
        src = v.get("alias", name)
        twin[name] = args.twin_prefix + src[len(args.label_prefix):] \
            if src.startswith(args.label_prefix) else args.twin_prefix + src

    for name, v in real.items():
        peak = max((abs(x) for x in v["pcm"]), default=1) or 1
        g = 127.0 / peak
        pcm = [max(-128, min(127, int(round(x * g)))) for x in v["pcm"]]
        with open(os.path.join(smp_dir, twin[name] + ".wav"), "wb") as f:
            f.write(build_wav(pcm, v["freq"], v["loop_start"], v["size"],
                              v["looped"]))
    print("wrote %d .wav files" % len(real))

    # The twin voicegroup: the decompiled .inc with its voicegroup and sample
    # symbols renamed. Keysplit tables are NOT renamed -- they are pure index
    # maps and both banks have identical structure, so they are shared.
    src_inc = os.path.join(args.repo, "sound", "voicegroups",
                           "%s.inc" % args.manifest)
    with open(src_inc) as f:
        inc = f.read()

    inc = inc.replace("voice_group %s" % args.manifest,
                      "voice_group %s%s" % (args.manifest, args.suffix), 1)
    inc = re.sub(r"voice_group " + re.escape(args.label_prefix),
                 "voice_group " + args.twin_prefix, inc)
    inc = re.sub(r"voicegroup_" + re.escape(args.label_prefix),
                 "voicegroup_" + args.twin_prefix, inc)
    for name in sorted(decoded, key=len, reverse=True):
        inc = inc.replace("DirectSoundWaveData_" + name,
                          "DirectSoundWaveData_" + twin[name])

    out_inc = os.path.join(args.repo, "sound", "voicegroups",
                           "%s%s.inc" % (args.manifest, args.suffix))
    with open(out_inc, "w", newline="\n") as f:
        f.write("@ gm.dls-sourced twin of %s, by tools/rogue/extract_gmdls.py.\n"
                "@ Same structure, keys, pans, envelopes and keysplit tables --\n"
                "@ only the sample audio differs.\n\n" % args.manifest)
        f.write(inc)
    print("wrote %s" % out_inc)

    lines = []
    for name in sorted(set(twin[n] for n in real)):
        lines += ["\t.align 2", "DirectSoundWaveData_%s::" % name,
                  '\t.incbin "sound/direct_sound_samples/%s.bin"' % name, ""]
    dp = os.path.join(args.repo, "sound",
                      "direct_sound_data_%s%s.inc" % (args.manifest, args.suffix))
    with open(dp, "w", newline="\n") as f:
        f.write("@ Emitted by tools/rogue/extract_gmdls.py. Regenerate, do not edit.\n\n")
        f.write("\n".join(lines))
    print("wrote %s" % dp)


if __name__ == "__main__":
    main()
