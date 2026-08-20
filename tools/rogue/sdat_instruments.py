"""Resolve what a DS cart rip's program numbers ACTUALLY are, from the game's SDAT.

WHY THIS EXISTS. A MIDI ripped from a DS game carries the game's own SBNK
instrument indices in its program-change events. voicegroup_all_instruments is
GENERAL MIDI ordered, so those numbers land on whatever GM happens to have at
that index - sometimes plausibly, sometimes absurdly. DPPt's Natural Disaster
has 102 bass notes on program 3, which in GM is Honky-tonk Piano; whether that
is right depends entirely on what instrument 3 is in ITS bank, and nothing in
the MIDI says.

Three rounds of guessing that from pitch range and GM semantics produced three
different wrong answers. This reads the actual answer out of the ROM.

WHAT IT CAN AND CANNOT TELL YOU. The SDAT gives, per program: the sample it
plays, which wave archive that sample lives in, and the full ADSR envelope. It
does NOT give instrument names - Nintendo shipped none. So this does not print
"French Horn"; it prints the evidence from which the right GM equivalent can be
chosen. A long sample with a slow attack and full sustain is a pad; a short one
with fast decay and no sustain is a pluck or a hit.

ENVELOPE IS THE PART WORTH READING. It is what made the earlier guesses fail -
the instrument family was arguable, but attack 128 against 255 is not, and it
decides whether a part is audible at all.

Run:  python3 tools/rogue/sdat_instruments.py SDAT --list
      python3 tools/rogue/sdat_instruments.py SDAT --seq SEQ_THE_EVENT03
      python3 tools/rogue/sdat_instruments.py SDAT --seq NAME --programs 3,48,56
"""
import argparse
import struct
import sys

SEQ, SEQARC, BANK, WAVEARC, PLAYER, GROUP, PLAYER2, STRM = range(8)

INST_TYPES = {
    0: "empty", 1: "PCM", 2: "PSG square", 3: "PSG noise",
    16: "drum set", 17: "key split",
}


def u8(d, o):
    return d[o]


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


class Sdat:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        d = self.d
        if d[:4] != b"SDAT":
            raise SystemExit("not an SDAT file")
        self.symb_off = u32(d, 0x10)
        self.info_off = u32(d, 0x18)
        self.fat_off = u32(d, 0x20)
        self.file_off = u32(d, 0x28)

    def names(self, slot):
        if not self.symb_off:
            return []
        base = self.symb_off
        rec = base + u32(self.d, base + 8 + 4 * slot)
        count = u32(self.d, rec)
        out = []
        for i in range(count):
            off = u32(self.d, rec + 4 + 4 * i)
            if not off:
                out.append(None)
                continue
            end = self.d.index(b"\0", base + off)
            out.append(self.d[base + off:end].decode("ascii", "replace"))
        return out

    def info_entries(self, slot):
        base = self.info_off
        rec = base + u32(self.d, base + 8 + 4 * slot)
        count = u32(self.d, rec)
        return [u32(self.d, rec + 4 + 4 * i) for i in range(count)]

    def seq_info(self, index):
        off = self.info_entries(SEQ)[index]
        if not off:
            return None
        o = self.info_off + off
        return {"fileId": u16(self.d, o), "bank": u16(self.d, o + 4),
                "vol": u8(self.d, o + 6)}

    def bank_info(self, index):
        off = self.info_entries(BANK)[index]
        if not off:
            return None
        o = self.info_off + off
        return {"fileId": u16(self.d, o),
                "wa": [u16(self.d, o + 4 + 2 * i) for i in range(4)]}

    def file(self, file_id):
        o = self.fat_off + 12 + file_id * 16
        # (start, SIZE), not (start, end). Read as start/end, the second field
        # is a length and every file comes back EMPTY - which then parses as
        # "this bank defines 0 instruments" rather than as an error, so the
        # tool reports a clean, confident, wrong answer.
        start, size = u32(self.d, o), u32(self.d, o + 4)
        return self.d[start:start + size]


def parse_sbnk(blob):
    if blob[:4] != b"SBNK":
        return []
    count = u32(blob, 0x38)
    out = []
    for i in range(count):
        rec = 0x3C + 4 * i
        typ = blob[rec]
        off = u16(blob, rec + 1)
        if typ == 0:
            continue
        out.append((i, typ, off))
    return out


def pcm_detail(blob, off):
    return {
        "swav": u16(blob, off), "swar": u16(blob, off + 2),
        "note": u8(blob, off + 4), "attack": u8(blob, off + 5),
        "decay": u8(blob, off + 6), "sustain": u8(blob, off + 7),
        "release": u8(blob, off + 8), "pan": u8(blob, off + 9),
    }



def expand(blob, off, typ):
    """[(low key, high key, detail)] for a drum set or key split.

    THE TWO LAYOUTS DIFFER AND BOTH ARE EASY TO GET WRONG. A drum set opens
    with its low and high note and then carries one record PER NOTE in that
    range. A key split opens with EIGHT key-range bytes - the upper bound of
    each region, zero meaning unused - and then carries one record per used
    region. Each record is a u16 type followed by the same 10-byte PCM body a
    plain instrument uses.
    """
    out = []
    if typ == 16:
        lo, hi = blob[off], blob[off + 1]
        cur = off + 2
        for note in range(lo, hi + 1):
            det = pcm_detail(blob, cur + 2)
            out.append((note, note, det))
            cur += 12
    else:
        ranges = [blob[off + i] for i in range(8)]
        cur = off + 8
        low = 0
        for hi in ranges:
            if hi == 0:
                break
            det = pcm_detail(blob, cur + 2)
            out.append((low, hi, det))
            low = hi + 1
            cur += 12
    return out

def describe(det):
    """Plain reading of a DS envelope.

    THE DS SCALE IS 0-127, NOT 0-255. This mattered: read with GBA thresholds,
    every instrument in the bank came back as a "slow swell" including the
    timpani, and the whole point of consulting the ROM is the envelope. DS
    attack 127 is INSTANT, the same thing GBA calls 255.

    Sustain is the other half. A struck instrument that rings out - piano,
    pizzicato, timpani - reads decay fast, sustain ~0, release long. A bowed or
    blown one reads sustain high.
    """
    a, s, r = det["attack"], det["sustain"], det["release"]
    bits = ["instant attack" if a >= 120 else
            ("slow swell" if a < 90 else "soft attack")]
    bits.append("sustained" if s >= 60 else
                ("struck, rings out" if s <= 5 else "part sustain"))
    if r >= 100:
        bits.append("long release")
    return ", ".join(bits)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sdat")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--seq")
    ap.add_argument("--programs")
    args = ap.parse_args()

    s = Sdat(args.sdat)
    seq_names = s.names(SEQ)
    bank_names = s.names(BANK)

    if args.list or not args.seq:
        print(f"{len(seq_names)} sequences")
        for i, n in enumerate(seq_names):
            if n:
                print(f"  {i:>4}  {n}")
        return 0

    if args.seq not in seq_names:
        near = [n for n in seq_names if n and args.seq.upper() in n.upper()]
        print(f"{args.seq} not found." + (f" Close: {near[:8]}" if near else ""))
        return 1

    idx = seq_names.index(args.seq)
    info = s.seq_info(idx)
    bank_id = info["bank"]
    bi = s.bank_info(bank_id)
    bname = bank_names[bank_id] if bank_id < len(bank_names) else "?"
    print(f"{args.seq}  (sequence {idx}, file {info['fileId']}, "
          f"seq volume {info['vol']})")
    print(f"bank {bank_id} = {bname}   "
          f"wavearcs {[w for w in bi['wa'] if w != 0xFFFF]}")

    blob = s.file(bi["fileId"])
    insts = parse_sbnk(blob)
    wanted = None
    if args.programs:
        wanted = {int(x) for x in args.programs.replace(" ", "").split(",")}

    print(f"\n{len(insts)} instruments defined in this bank")
    print(f'{"prog":>5}  {"type":<11}{"swav":>5}{"swar":>5}{"note":>5}'
          f'{"A":>4}{"D":>4}{"S":>4}{"R":>4}   character')
    for prog, typ, off in insts:
        if wanted is not None and prog not in wanted:
            continue
        name = INST_TYPES.get(typ, f"type {typ}")
        if typ == 1:
            det = pcm_detail(blob, off)
            print(f'{prog:>5}  {name:<11}{det["swav"]:>5}{det["swar"]:>5}'
                  f'{det["note"]:>5}{det["attack"]:>4}{det["decay"]:>4}'
                  f'{det["sustain"]:>4}{det["release"]:>4}   {describe(det)}')
        elif typ in (16, 17):
            for lo, hi, det in expand(blob, off, typ):
                span = f'{lo}-{hi}'
                print(f'{prog:>5}  {name:<11}{det["swav"]:>5}{det["swar"]:>5}'
                      f'{det["note"]:>5}{det["attack"]:>4}{det["decay"]:>4}'
                      f'{det["sustain"]:>4}{det["release"]:>4}   '
                      f'keys {span:<8} {describe(det)}')
        else:
            print(f'{prog:>5}  {name:<11}{"":>19}   (unhandled type {typ})')
    return 0


if __name__ == "__main__":
    sys.exit(main())
