"""Extract a DS sequence's actual samples as .wav the GBA toolchain can build.

WHY RIP RATHER THAN APPROXIMATE. Mapping a cart rip's programs onto
voicegroup_all_instruments gets the instrument FAMILY right and the sound only
roughly - the GBA bank's strings are not DPPt's strings. For a track where that
matters, the samples are right there in the ROM and cost almost nothing:
SEQ_THE_EVENT03's thirteen samples are 98 KB as 8-bit PCM, against about 5.5 MB
of free ROM.

This is the same move the project already made twice - all_instruments is a
decompile of a patch's samples, and extract_gmdls.py built a voicegroup from
Windows' gm.dls. Per-song voicegroups are also already normal here: the jorts
imports each bring their own.

THE SAMPLE CARRIES ITS RAW RATE; THE VOICE CARRIES THE BASE NOTE. Getting this
backwards produced a build of pure high-pitched noise, twice, so it is worth
stating exactly.

wav2agb computes

    pitch = sampleRate * 2^((60 - midiKey) / 12 + tuning / 1200)

from a `smpl` chunk. Writing the DS instrument's real base note there - 24 for a
low string - asks for a pitch EIGHT OCTAVES up: 16 kHz becomes 128 kHz and the
pitch word becomes 131,260,416. Vanilla's own pitch words are around 20,000,000,
one per sample rate, because vanilla's samples are recorded AT middle C and need
no transposition. A value 6.5x outside that range is not something the mixer
handles gracefully.

So midiKey is written as 60 - meaning "no transposition, this is the raw rate" -
and the DS base note goes on the voice instead, as `voice_directsound <note>`.
The two halves must never both carry it.

The `smpl` chunk is still written, for the LOOP POINTS.

Loop points travel the same way: DS stores loopOffset and nonLoopLen in WORDS,
so both are multiplied by two to reach samples.

DS AUDIO IS USUALLY IMA-ADPCM (type 2), with a 4-byte header carrying the
initial predictor and step index. Types 0 and 1 are PCM8 and PCM16.

Run:  python3 tools/rogue/rip_sdat_samples.py ROM.nds --seq SEQ_THE_EVENT03 \\
          --out sound/direct_sound_samples --prefix dp_event03
      python3 tools/rogue/rip_sdat_samples.py --selftest
"""
import argparse
import struct
import sys
from pathlib import Path

SEQ, SEQARC, BANK, WAVEARC = 0, 1, 2, 3

IMA_STEP = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
    796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767,
]
IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def u8(d, o):
    return d[o]


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def find_sdat(rom):
    i = 0
    while True:
        i = rom.find(b"SDAT", i)
        if i < 0:
            raise SystemExit("no SDAT in this ROM")
        if u32(rom, i + 4) == 0x0100FEFF and u16(rom, i + 12) == 0x40:
            return rom[i:i + u32(rom, i + 8)]
        i += 4


class Sdat:
    def __init__(self, blob):
        self.d = blob
        self.symb_off = u32(blob, 0x10)
        self.info_off = u32(blob, 0x18)
        self.fat_off = u32(blob, 0x20)

    def names(self, slot):
        base = self.symb_off
        rec = base + u32(self.d, base + 8 + 4 * slot)
        out = []
        for i in range(u32(self.d, rec)):
            off = u32(self.d, rec + 4 + 4 * i)
            if not off:
                out.append(None)
                continue
            end = self.d.index(b"\0", base + off)
            out.append(self.d[base + off:end].decode("ascii", "replace"))
        return out

    def entries(self, slot):
        base = self.info_off
        rec = base + u32(self.d, base + 8 + 4 * slot)
        return [u32(self.d, rec + 4 + 4 * i) for i in range(u32(self.d, rec))]

    def seq_info(self, i):
        o = self.info_off + self.entries(SEQ)[i]
        return {"fileId": u16(self.d, o), "bank": u16(self.d, o + 4)}

    def bank_info(self, i):
        o = self.info_off + self.entries(BANK)[i]
        return {"fileId": u16(self.d, o),
                "wa": [u16(self.d, o + 4 + 2 * j) for j in range(4)]}

    def wavearc_info(self, i):
        o = self.info_off + self.entries(WAVEARC)[i]
        return {"fileId": u16(self.d, o)}

    def file(self, fid):
        o = self.fat_off + 12 + fid * 16
        start, size = u32(self.d, o), u32(self.d, o + 4)
        return self.d[start:start + size]


def swar_offsets(blob):
    if blob[:4] != b"SWAR":
        return []
    return [u32(blob, 0x3C + 4 * i) for i in range(u32(blob, 0x38))]


def decode_ima(data):
    pred = struct.unpack_from("<h", data, 0)[0]
    index = data[2]
    out = []
    for byte in data[4:]:
        for nib in (byte & 0xF, byte >> 4):
            step = IMA_STEP[index]
            diff = step >> 3
            if nib & 1:
                diff += step >> 2
            if nib & 2:
                diff += step >> 1
            if nib & 4:
                diff += step
            pred = max(-32768, min(32767, pred - diff if nib & 8 else pred + diff))
            index = max(0, min(88, index + IMA_INDEX[nib]))
            out.append(pred)
    return out


def read_swav(blob, off):
    """(pcm16 list, rate, base note, loop start, loop end) for one SWAR entry."""
    wtype = u8(blob, off)
    loop_flag = u8(blob, off + 1)
    rate = u16(blob, off + 2)
    loop_words = u16(blob, off + 6)
    nonloop_words = u32(blob, off + 8)
    raw = blob[off + 12: off + 12 + (loop_words + nonloop_words) * 4]

    if wtype == 0:
        pcm = [c - 256 if c > 127 else c for c in raw]
        pcm = [v << 8 for v in pcm]
        loop_start = loop_words * 4
    elif wtype == 1:
        pcm = list(struct.unpack(f"<{len(raw) // 2}h", raw[:len(raw) // 2 * 2]))
        loop_start = loop_words * 2
    elif wtype == 2:
        pcm = decode_ima(raw)
        # ADPCM packs two samples per byte, and the 4-byte header is not audio,
        # so a loop expressed in words lands at words*8 - 8 samples.
        loop_start = max(0, loop_words * 8 - 8)
    else:
        return None
    return {"pcm": pcm, "rate": rate, "loop": bool(loop_flag),
            "loop_start": loop_start, "type": wtype}


def write_wav(path, pcm, rate, midi_key=60, loop_start=None, loop_end=None):
    """16-bit mono .wav with a `smpl` chunk carrying the loop points.

    midi_key defaults to 60 ON PURPOSE - see the module docstring. At 60 wav2agb
    derives pitch = sampleRate, which is the sample's own rate and the range
    vanilla samples occupy. The instrument's real base note belongs on the
    voice, not here.
    """
    data = struct.pack(f"<{len(pcm)}h", *pcm)
    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate * 2, 2, 16)

    smpl = struct.pack("<IIIIIIIII",
                       0, 0, int(1e9 / rate), midi_key, 0, 0, 0,
                       1 if loop_end else 0, 0)
    if loop_end:
        smpl += struct.pack("<IIIIII", 0, 0, loop_start, loop_end, 0, 0)

    chunks = (b"fmt " + struct.pack("<I", len(fmt)) + fmt
              + b"data" + struct.pack("<I", len(data)) + data
              + (b"" if len(data) % 2 == 0 else b"\0")
              + b"smpl" + struct.pack("<I", len(smpl)) + smpl)
    path.write_bytes(b"RIFF" + struct.pack("<I", 4 + len(chunks))
                     + b"WAVE" + chunks)


def bank_programs(blob):
    """{program: [(swav, swar, base note)]} for every instrument in an SBNK."""
    out = {}
    for prog in range(u32(blob, 0x38)):
        rec = 0x3C + 4 * prog
        typ, off = blob[rec], u16(blob, rec + 1)
        if typ == 0:
            continue
        if typ == 1:
            out.setdefault(prog, []).append(
                (u16(blob, off), u16(blob, off + 2), u8(blob, off + 4)))
            continue
        if typ == 16:
            lo, hi = blob[off], blob[off + 1]
            cur, n = off + 2, hi - lo + 1
        elif typ == 17:
            ranges = [blob[off + i] for i in range(8)]
            n = sum(1 for r in ranges if r)
            cur = off + 8
        else:
            continue
        for _ in range(n):
            out.setdefault(prog, []).append(
                (u16(blob, cur + 2), u16(blob, cur + 4), u8(blob, cur + 6)))
            cur += 12
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", nargs="?")
    ap.add_argument("--seq")
    ap.add_argument("--programs", help="only these programs, e.g. 3,47,48")
    ap.add_argument("--out")
    ap.add_argument("--prefix", default="ds")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not (args.rom and args.seq and args.out):
        print(__doc__)
        return 2

    s = Sdat(find_sdat(open(args.rom, "rb").read()))
    names = s.names(SEQ)
    if args.seq not in names:
        raise SystemExit(f"{args.seq} not found")
    bi = s.bank_info(s.seq_info(names.index(args.seq))["bank"])
    progs = bank_programs(s.file(bi["fileId"]))

    arcs = {}
    for slot, wa in enumerate(bi["wa"]):
        if wa != 0xFFFF:
            blob = s.file(s.wavearc_info(wa)["fileId"])
            arcs[slot] = (blob, swar_offsets(blob))

    wanted = ({int(x) for x in args.programs.replace(" ", "").split(",")}
              if args.programs else set(progs))
    out = Path(args.out)
    total = 0
    seen = set()
    print(f'{"program":>8}{"swav":>6}{"rate":>7}{"key":>5}{"samples":>9}  file')
    for prog in sorted(wanted & set(progs)):
        for swav, swar, key in progs[prog]:
            if (swav, swar) in seen or swar not in arcs:
                continue
            seen.add((swav, swar))
            blob, offs = arcs[swar]
            if swav >= len(offs):
                continue
            w = read_swav(blob, offs[swav])
            if not w or not w["pcm"]:
                continue
            # Archive index included - see rip_sdat_voicegroup.py; the same
            # swav index can appear in two archives and silently overwrite.
            name = f"{args.prefix}_a{swar}_{swav:03d}"
            n = len(w["pcm"])
            total += n
            if not args.dry_run:
                write_wav(out / f"{name}.wav", w["pcm"], w["rate"], 60,
                          w["loop_start"] if w["loop"] else None,
                          n if w["loop"] else None)
            print(f'{prog:>8}{swav:>6}{w["rate"]:>7}{key:>5}{n:>9}  {name}.wav')
    print(f"\n{len(seen)} samples, {total:,} bytes as 8-bit PCM "
          f"({total/1024:.1f} KB)"
          + (" (dry run)" if args.dry_run else ""))
    return 0


def selftest():
    import tempfile
    import wave as wavemod

    ok = 0
    with tempfile.TemporaryDirectory() as td:
        p = Path(td) / "t.wav"

        # 1. The wav is readable by the stdlib and round-trips its samples.
        pcm = [0, 1000, -1000, 32767, -32768, 0]
        write_wav(p, pcm, 16000, 60)
        with wavemod.open(str(p), "rb") as w:
            got = struct.unpack(f"<{w.getnframes()}h", w.readframes(w.getnframes()))
            rate, width = w.getframerate(), w.getsampwidth()
        fired = list(got) == pcm and rate == 16000 and width == 2
        print(f'  {"ok     " if fired else "*** FAILED ***"}  wav round-trips its samples')
        ok += fired

        # 2. The smpl chunk is present and carries the base note - without it
        #    wav2agb falls back to the bare sample rate and every note is
        #    transposed.
        raw = p.read_bytes()
        i = raw.find(b"smpl")
        key = struct.unpack_from("<I", raw, i + 8 + 12)[0] if i > 0 else -1
        fired = i > 0 and key == 60
        print(f'  {"ok     " if fired else "*** FAILED ***"}  smpl chunk carries midiKey (got {key})')
        ok += fired

        # 3. A different base note lands in the chunk, so pitch actually varies.
        write_wav(p, pcm, 16000, 36)
        raw = p.read_bytes()
        i = raw.find(b"smpl")
        key = struct.unpack_from("<I", raw, i + 8 + 12)[0]
        fired = key == 36
        print(f'  {"ok     " if fired else "*** FAILED ***"}  midiKey follows the instrument (got {key})')
        ok += fired

        # 4. IMA decoding produces the right number of samples: two per byte
        #    after a 4-byte header.
        got = decode_ima(bytes([0, 0, 0, 0]) + bytes([0x11, 0x22, 0x33]))
        fired = len(got) == 6
        print(f'  {"ok     " if fired else "*** FAILED ***"}  IMA yields 2 samples per byte (got {len(got)})')
        ok += fired

    print(f'\n{ok}/4 selftest cases behave')
    return 0 if ok == 4 else 1


if __name__ == "__main__":
    sys.exit(main())
