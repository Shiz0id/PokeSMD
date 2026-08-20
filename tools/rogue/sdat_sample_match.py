"""Match a DS cart's samples against the GBA bank's, by listening to the audio.

WHY. sdat_instruments.py resolves what a rip's program numbers are by ENVELOPE -
struck versus sustained, instant versus swelling - which is enough to rule things
out but not to choose between, say, Acoustic Grand and Pizzicato Strings. Both
are struck and ring out. The samples themselves settle it.

WHAT IT DOES. Decodes the DS SWAV the instrument actually plays, decodes every
sample in voicegroup_all_instruments, and ranks the GBA samples by spectral
similarity to the DS one. The answer is "GBA sample X is the nearest thing this
bank has to what the DS plays here", which maps straight back to a GM program.

HOW THE COMPARISON WORKS, and its limits. Both sides are decoded to mono, peak
normalised, resampled to a common rate and reduced to a log-spaced magnitude
spectrum, then compared by cosine similarity. That measures TIMBRE - the balance
of harmonics - and deliberately ignores pitch, length and loudness, because the
two banks disagree about all three and none of them matter for "which instrument
is this". It is not a claim about what a listener will prefer.

DS AUDIO IS USUALLY IMA-ADPCM. Type 0 is PCM8, 1 is PCM16, 2 is ADPCM with a
4-byte header carrying the initial predictor and step index. Decoding 2 as raw
PCM produces noise that still correlates with nothing, so the type byte matters.

Run (needs numpy - on this setup that means the Windows interpreter):
  python tools/rogue/sdat_sample_match.py ROM.nds --seq SEQ_THE_EVENT03 \\
      --gba-samples <path to sound/direct_sound_samples>
"""
import argparse
import struct
import sys
import wave
from pathlib import Path

try:
    import numpy as np
except ImportError:
    print("numpy required")
    sys.exit(1)

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
        n = u32(self.d, rec)
        out = []
        for i in range(n):
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
        n = u32(self.d, rec)
        return [u32(self.d, rec + 4 + 4 * i) for i in range(n)]

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


def swar_samples(blob):
    """[(offset, ...)] of each sample entry inside a SWAR."""
    if blob[:4] != b"SWAR":
        return []
    n = u32(blob, 0x38)
    return [u32(blob, 0x3C + 4 * i) for i in range(n)]


def decode_swav(blob, off):
    """(samples as float32 in -1..1, sample rate) for one SWAR entry."""
    wtype = u8(blob, off)
    rate = u16(blob, off + 2)
    nonloop = u32(blob, off + 8)
    loop = u16(blob, off + 6)
    data = blob[off + 12: off + 12 + (loop + nonloop) * 4]
    if wtype == 0:
        a = np.frombuffer(data, dtype=np.int8).astype(np.float32) / 128.0
    elif wtype == 1:
        a = np.frombuffer(data[:len(data) // 2 * 2],
                          dtype="<i2").astype(np.float32) / 32768.0
    elif wtype == 2:
        a = decode_ima(data) / 32768.0
    else:
        return None, rate
    return a, rate


def decode_ima(data):
    if len(data) < 4:
        return np.zeros(0, dtype=np.float32)
    pred = struct.unpack_from("<h", data, 0)[0]
    index = data[2]
    out = []
    for byte in data[4:]:
        for nib in (byte & 0xF, byte >> 4):
            step = IMA_STEP[max(0, min(88, index))]
            diff = step >> 3
            if nib & 1:
                diff += step >> 2
            if nib & 2:
                diff += step >> 1
            if nib & 4:
                diff += step
            pred = pred - diff if nib & 8 else pred + diff
            pred = max(-32768, min(32767, pred))
            index = max(0, min(88, index + IMA_INDEX[nib]))
            out.append(pred)
    return np.asarray(out, dtype=np.float32)


def load_wav(path):
    with wave.open(str(path), "rb") as w:
        width, rate = w.getsampwidth(), w.getframerate()
        raw = w.readframes(w.getnframes())
    if width == 1:
        a = (np.frombuffer(raw, dtype=np.uint8).astype(np.float32) - 128) / 128.0
    elif width == 2:
        a = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    else:
        return None, rate
    return a, rate


BANDS = 48
TARGET_RATE = 16000


def fingerprint(a, rate):
    """Log-spaced magnitude spectrum, normalised. Timbre only."""
    if a is None or len(a) < 256:
        return None
    peak = np.max(np.abs(a))
    if peak <= 0:
        return None
    a = a / peak
    # Resample crudely to a common rate; exact interpolation is irrelevant to a
    # band-averaged spectrum and a proper resampler is a dependency we do not
    # have here.
    if rate != TARGET_RATE:
        n = int(len(a) * TARGET_RATE / rate)
        if n < 256:
            return None
        a = np.interp(np.linspace(0, len(a) - 1, n), np.arange(len(a)), a)
    a = a[:TARGET_RATE // 2]          # first half second is the timbre
    if len(a) < 256:
        return None
    spec = np.abs(np.fft.rfft(a * np.hanning(len(a))))
    edges = np.geomspace(1, len(spec) - 1, BANDS + 1).astype(int)
    bands = np.array([spec[edges[i]:max(edges[i] + 1, edges[i + 1])].mean()
                      for i in range(BANDS)])
    bands = np.log1p(bands)
    n = np.linalg.norm(bands)
    return bands / n if n else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("--seq", required=True)
    ap.add_argument("--gba-samples", required=True)
    ap.add_argument("--top", type=int, default=5)
    args = ap.parse_args()

    s = Sdat(find_sdat(open(args.rom, "rb").read()))
    names = s.names(SEQ)
    if args.seq not in names:
        raise SystemExit(f"{args.seq} not found")
    info = s.seq_info(names.index(args.seq))
    bi = s.bank_info(info["bank"])
    bank_blob = s.file(bi["fileId"])

    # Which programs the bank defines, and the swav each region uses.
    used = {}
    count = u32(bank_blob, 0x38)
    for prog in range(count):
        rec = 0x3C + 4 * prog
        typ, off = bank_blob[rec], u16(bank_blob, rec + 1)
        if typ == 1:
            used.setdefault(prog, []).append((u16(bank_blob, off),
                                              u16(bank_blob, off + 2)))
        elif typ in (16, 17):
            if typ == 16:
                lo, hi = bank_blob[off], bank_blob[off + 1]
                cur, n = off + 2, hi - lo + 1
            else:
                ranges = [bank_blob[off + i] for i in range(8)]
                n = sum(1 for r in ranges if r)
                cur = off + 8
            for _ in range(n):
                used.setdefault(prog, []).append((u16(bank_blob, cur + 2),
                                                  u16(bank_blob, cur + 4)))
                cur += 12

    # GBA side.
    gba = {}
    for f in sorted(Path(args.gba_samples).glob("*.wav")):
        a, rate = load_wav(f)
        fp = fingerprint(a, rate)
        if fp is not None:
            gba[f.stem] = fp
    print(f"{len(gba)} GBA samples fingerprinted")

    # DS side, per wave archive referenced by the bank.
    arcs = {}
    for slot, wa in enumerate(bi["wa"]):
        if wa == 0xFFFF:
            continue
        blob = s.file(s.wavearc_info(wa)["fileId"])
        arcs[slot] = (blob, swar_samples(blob))
    print(f"wave archives: {[w for w in bi['wa'] if w != 0xFFFF]}\n")

    for prog in sorted(used):
        seen = set()
        for swav, swar in used[prog]:
            if (swav, swar) in seen or swar not in arcs:
                continue
            seen.add((swav, swar))
            blob, offs = arcs[swar]
            if swav >= len(offs):
                continue
            a, rate = decode_swav(blob, offs[swav])
            fp = fingerprint(a, rate)
            if fp is None:
                continue
            scores = sorted(((float(np.dot(fp, g)), n) for n, g in gba.items()),
                            reverse=True)[:args.top]
            best = ", ".join(f"{n} {v:.3f}" for v, n in scores)
            print(f"prog {prog:>3} swav {swav:>3} (arc {swar}, {rate} Hz, "
                  f"{len(a)} samples)\n    {best}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
