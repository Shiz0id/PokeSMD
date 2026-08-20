"""Convert a DS SDAT sequence straight to MIDI, so no third-party rip sits in between.

WHY THIS EXISTS. Every DPPt track in this ROM arrived as somebody's MIDI rip of
the cart. A rip is a transcription: it can renumber channels, flatten a
controller, lose a tempo change, and - the one that has cost real time here -
drop the loop point entirely, because the loop lives in the SSEQ bytecode and
has nowhere to go in a MIDI file. SEQ_THE_EVENT03 is the worked example: the
sequence allocates tracks 0,1,2,4,5,8,9,10 and the rip in this repo has them
compacted onto channels 0,1,2,3,4,5,7,9. Same music, different file.

The sequence is in the ROM. Reading it directly removes the transcription step,
and with it every question of the form "is that the rip or is that us".

THE LOOP POINT COMES OUT FOR FREE, and that is the biggest single win. An SSEQ
loops with a `jump` (0x94) to an earlier address. This walks each track, notes
the tick at which the jump target was FIRST executed, and writes `[` there and
`]` at the jump - the two text meta events mid2agb needs. No lifting from
another rip, no guessing a bar count, and none of the failure in
set_midi_loop.py's docstring where a lifted point lands mid-phrase.

PROGRAM NUMBERS ARE THE DS BANK'S, WHICH IS THE POINT. Pair the output with the
voicegroup rip_sdat_voicegroup.py builds from the same bank and the program
changes select the instruments the DS selected, with no remapping anywhere.

NOTE_WAIT (0xC7) DECIDES WHETHER A NOTE ADVANCES TIME, and getting it backwards
silently turns a song into a chord. It defaults to ON: a note command waits out
its own duration. SEQ_THE_EVENT03 turns it OFF on track 0 and drives that track
from explicit rests, so every note there would stack at tick 0 under the wrong
assumption. Both modes are exercised by --selftest.

TICKS ARE 48 PER QUARTER on both sides, so the division is copied rather than
converted and no rounding enters.

Run:  python3 tools/rogue/rip_sdat_sequence.py ROM.nds --seq SEQ_THE_EVENT03 \\
          --out sound/songs/midi/mus_dppt_natural_disaster.mid
      python3 tools/rogue/rip_sdat_sequence.py ROM.nds --seq SEQ_THE_EVENT03 --list
      python3 tools/rogue/rip_sdat_sequence.py --selftest
"""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rip_sdat_samples import Sdat, find_sdat, SEQ, u8, u16, u32  # noqa: E402

TPQN = 48
SEQ_BASE = 0x1C
STEP_BUDGET = 200000

# Argument shape per opcode. "v" varlen, "1" u8, "2" u16, "3" u24, "s1"/"s2"
# signed. Anything absent takes none. Keeping this complete matters more than
# keeping it interesting: one wrong length desynchronises the whole stream and
# the result is not a wrong note, it is noise that still converts cleanly.
ARGS = {
    0x80: "v", 0x81: "v", 0x93: "13", 0x94: "3", 0x95: "3",
    0xC0: "1", 0xC1: "1", 0xC2: "1", 0xC3: "s1", 0xC4: "s1", 0xC5: "1",
    0xC6: "1", 0xC7: "1", 0xC8: "1", 0xC9: "1", 0xCA: "1", 0xCB: "1",
    0xCC: "1", 0xCD: "1", 0xCE: "1", 0xCF: "1",
    0xD0: "1", 0xD1: "1", 0xD2: "1", 0xD3: "1", 0xD4: "1", 0xD5: "1",
    0xD6: "1",
    0xE0: "2", 0xE1: "2", 0xE3: "s2",
    0xFC: "", 0xFD: "", 0xFE: "2", 0xFF: "",
}
for _op in range(0xB0, 0xBE):
    ARGS[_op] = "12"

NAMES = {
    0x80: "rest", 0x81: "program", 0x93: "opentrack", 0x94: "jump",
    0x95: "call", 0xC0: "pan", 0xC1: "volume", 0xC2: "mastervol",
    0xC3: "transpose", 0xC4: "bend", 0xC5: "bendrange", 0xC6: "priority",
    0xC7: "notewait", 0xC8: "tie", 0xC9: "portakey", 0xCA: "moddepth",
    0xCB: "modspeed", 0xCC: "modtype", 0xCD: "modrange", 0xCE: "portaonoff",
    0xCF: "portatime", 0xD0: "attack", 0xD1: "decay", 0xD2: "sustain",
    0xD3: "release", 0xD4: "loopstart", 0xD5: "expression", 0xD6: "printvar",
    0xE0: "moddelay", 0xE1: "tempo", 0xE3: "sweeppitch", 0xFC: "loopend",
    0xFD: "return", 0xFE: "alloctracks", 0xFF: "end",
}


def read_varlen(d, p):
    v = 0
    while True:
        b = d[p]
        p += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, p


def read_args(d, p, shape):
    out = []
    i = 0
    while i < len(shape):
        c = shape[i]
        if c == "s":
            i += 1
            c = shape[i]
            if c == "1":
                out.append(struct.unpack_from("<b", d, p)[0])
                p += 1
            else:
                out.append(struct.unpack_from("<h", d, p)[0])
                p += 2
        elif c == "v":
            v, p = read_varlen(d, p)
            out.append(v)
        elif c == "1":
            out.append(d[p])
            p += 1
        elif c == "2":
            out.append(u16(d, p))
            p += 2
        elif c == "3":
            out.append(d[p] | d[p + 1] << 8 | d[p + 2] << 16)
            p += 3
        i += 1
    return out, p


def track_starts(data):
    """{track index: byte offset}. Track 0 is implicit and starts at 0."""
    starts = {0: 0}
    p = 0
    while p < len(data):
        op = data[p]
        if op == 0xFE:
            p += 3
            continue
        if op != 0x93:
            break
        p += 1
        num = data[p]
        off = data[p + 1] | data[p + 2] << 8 | data[p + 3] << 16
        starts[num] = off
        p += 4
    starts[0] = p
    return starts


class Track:
    """One SSEQ track walked once through, to the jump that closes its loop."""

    def __init__(self, data, start):
        self.data = data
        self.start = start
        self.events = []          # (tick, kind, *args)
        self.tempo = []           # (tick, bpm) - conductor data, track 0 only
        self.loop_begin = None
        self.loop_end = None
        self.end_tick = 0

    def run(self):
        d = self.data
        p = self.start
        tick = 0
        note_wait = True
        tie = False
        transpose = 0
        stack = []
        loops = []
        first_seen = {}
        steps = 0
        held = None

        while p < len(d):
            if steps > STEP_BUDGET:
                raise SystemExit("sequence did not terminate; likely a bad offset")
            steps += 1
            first_seen.setdefault(p, tick)
            here = p
            op = d[p]
            p += 1

            if op < 0x80:
                vel = d[p]
                p += 1
                dur, p = read_varlen(d, p)
                key = max(0, min(127, op + transpose))
                if tie:
                    # A tied note holds until the next one on this track. Its
                    # own duration is not the length, so it cannot be emitted
                    # until the successor arrives.
                    if held is not None:
                        self.events.append((held[0], "note", held[1], held[2],
                                            tick - held[0]))
                    held = (tick, key, vel)
                else:
                    self.events.append((tick, "note", key, vel, max(1, dur)))
                if note_wait:
                    tick += dur
                continue

            shape = ARGS.get(op)
            if shape is None:
                raise SystemExit(f"unknown opcode 0x{op:02X} at 0x{here:X}")
            args, p = read_args(d, p, shape)

            if op == 0x80:
                tick += args[0]
            elif op == 0x81:
                self.events.append((tick, "program", args[0] & 0x7F))
            elif op == 0xC0:
                self.events.append((tick, "cc", 10, args[0]))
            elif op == 0xC1:
                self.events.append((tick, "cc", 7, args[0]))
            elif op == 0xD5:
                self.events.append((tick, "cc", 11, args[0]))
            elif op == 0xCA:
                # Modulation depth. DS scales it against mod range (0xCD); CC1
                # is the closest thing m4a has and is what the existing rip of
                # this track carries, so dropping it would lose ground the
                # transcription already held.
                self.events.append((tick, "cc", 1, args[0]))
            elif op == 0xC3:
                transpose = args[0]
            elif op == 0xC4:
                self.events.append((tick, "bend", args[0]))
            elif op == 0xC5:
                self.events.append((tick, "bendrange", args[0]))
            elif op == 0xC7:
                note_wait = bool(args[0])
            elif op == 0xC8:
                tie = bool(args[0])
                if not tie and held is not None:
                    self.events.append((held[0], "note", held[1], held[2],
                                        max(1, tick - held[0])))
                    held = None
            elif op == 0xE1:
                self.tempo.append((tick, args[0]))
            elif op == 0xD4:
                loops.append((p, args[0]))
            elif op == 0xFC:
                if loops:
                    back, count = loops[-1]
                    if count == 0:
                        loops.pop()          # infinite: take it once and move on
                    elif count > 1:
                        loops[-1] = (back, count - 1)
                        p = back
                    else:
                        loops.pop()
            elif op == 0x95:
                stack.append(p)
                p = args[0]
            elif op == 0xFD:
                if not stack:
                    break
                p = stack.pop()
            elif op == 0x94:
                target = args[0]
                if target in first_seen:
                    self.loop_begin = first_seen[target]
                    self.loop_end = tick
                    break
                p = target
            elif op == 0xFF:
                break

        if held is not None:
            self.events.append((held[0], "note", held[1], held[2],
                                max(1, tick - held[0])))
        self.end_tick = tick
        return self


def parse(blob):
    if blob[:4] != b"SSEQ":
        raise SystemExit("not an SSEQ")
    data = blob[SEQ_BASE:SEQ_BASE + u32(blob, 0x14) - 4]
    starts = track_starts(data)
    return data, starts


# ---------------------------------------------------------------- MIDI output

def varlen(v):
    out = bytearray([v & 0x7F])
    v >>= 7
    while v:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    return bytes(reversed(out))


def chunk(cid, body):
    return cid + struct.pack(">I", len(body)) + body


def build_midi(tracks, loop_begin, loop_end):
    out = [chunk(b"MThd", struct.pack(">HHH", 1, len(tracks), TPQN))]
    for n, (channel, tk) in enumerate(tracks):
        timed = []
        for ev in tk.events:
            tick = ev[0]
            if ev[1] == "note":
                _, _, key, vel, dur = ev
                timed.append((tick, 1, bytes([0x90 | channel, key, vel])))
                timed.append((tick + dur, 0, bytes([0x80 | channel, key, 0])))
            elif ev[1] == "program":
                timed.append((tick, 0, bytes([0xC0 | channel, ev[2]])))
            elif ev[1] == "cc":
                timed.append((tick, 0, bytes([0xB0 | channel, ev[2], min(127, ev[3])])))
            elif ev[1] == "bend":
                v = max(0, min(16383, 8192 + ev[2] * 64))
                timed.append((tick, 0, bytes([0xE0 | channel, v & 0x7F, v >> 7])))
            elif ev[1] == "bendrange":
                for cc, val in ((101, 0), (100, 0), (6, min(127, ev[2]))):
                    timed.append((tick, 0, bytes([0xB0 | channel, cc, val])))
        if n == 0:
            for tick, bpm in tk.tempo:
                us = 60000000 // max(1, bpm)
                timed.append((tick, 0, b"\xFF\x51\x03" + struct.pack(">I", us)[1:]))
            if loop_begin is not None:
                timed.append((loop_begin, 0, b"\xFF\x06\x01["))
                timed.append((loop_end, 2, b"\xFF\x06\x01]"))

        timed.sort(key=lambda e: (e[0], e[1]))
        body = bytearray()
        last = 0
        for tick, _, raw in timed:
            body += varlen(tick - last) + raw
            last = tick
        body += b"\x00\xFF\x2F\x00"
        out.append(chunk(b"MTrk", bytes(body)))
    return b"".join(out)


# ------------------------------------------------------------------- commands

def load(rom_path, seq_name):
    s = Sdat(find_sdat(Path(rom_path).read_bytes()))
    names = s.names(SEQ)
    if seq_name not in names:
        raise SystemExit(f"{seq_name} not found")
    info = s.seq_info(names.index(seq_name))
    return s, info, s.file(info["fileId"])


def do_list(data, starts):
    for num in sorted(starts):
        tk = Track(data, starts[num]).run()
        notes = [e for e in tk.events if e[1] == "note"]
        progs = sorted({e[2] for e in tk.events if e[1] == "program"})
        keys = [e[2] for e in notes]
        rng = f"{min(keys)}-{max(keys)}" if keys else "-"
        print(f"  track {num:2d}  programs {progs}  {len(notes):5d} notes  "
              f"keys {rng}  ends {tk.end_tick} ticks  "
              f"loop {tk.loop_begin}->{tk.loop_end}")


def silent_loop(walked, begin, end):
    """True if no track sounds a note inside [begin, end).

    A SSEQ closes a one-shot cue by jumping onto a bare rest, so the sequence
    "loops" a few beats of silence forever. That is correct on the DS, where
    the game stops the cue - and useless as dungeon music, which has to come
    back round. SEQ_THE_EVENT03 is exactly this shape: 64 beats of music and a
    4-beat silent vamp. Taken faithfully it plays once and then goes quiet,
    which is a bug that only appears 45 seconds in.
    """
    return not any(begin <= ev[0] < end
                   for _, t in walked for ev in t.events if ev[1] == "note")


def convert(data, starts, out_path, force_whole=False):
    walked = [(num, Track(data, starts[num]).run()) for num in sorted(starts)]
    zero = dict(walked)[0]
    begin, end = zero.loop_begin, zero.loop_end
    why = "sequence jump on track 0"
    if begin is None:
        # No jump on track 0: fall back to the longest track that does loop,
        # and failing that to a whole-song loop, which is never wrong the way a
        # mid-phrase point is.
        looping = [t for _, t in walked if t.loop_begin is not None]
        if looping:
            best = max(looping, key=lambda t: t.loop_end)
            begin, end = best.loop_begin, best.loop_end
            why = "sequence jump on the longest looping track"
        else:
            begin, end = 0, max(t.end_tick for _, t in walked)
            why = "no jump anywhere; whole song"
    whole = (0, max(t.end_tick for _, t in walked))
    if force_whole:
        begin, end = whole
        why = "--whole-song"
    elif silent_loop(walked, begin, end):
        begin, end = whole
        why = ("the sequence's own loop sounds no notes - a one-shot cue's "
               "silent vamp; using the whole song instead")
    blob = build_midi(walked, begin, end)
    Path(out_path).write_bytes(blob)
    return walked, begin, end, why


def selftest():
    def sseq(body):
        data = b"\x1c\x00\x00\x00" + body
        return (b"SSEQ\xff\xfe\x00\x01" + struct.pack("<I", 0x10 + 8 + len(data))
                + struct.pack("<HH", 0x10, 1)
                + b"DATA" + struct.pack("<I", 8 + len(data)) + data)

    fails = []

    def check(name, ok):
        print(("PASS  " if ok else "FAIL  ") + name)
        if not ok:
            fails.append(name)

    # note_wait defaults ON, so two notes are sequential, not a chord.
    d, st = parse(sseq(b"\x3C\x64\x18\x3E\x64\x18\xFF"))
    tk = Track(d, st[0]).run()
    ticks = [e[0] for e in tk.events if e[1] == "note"]
    check("note_wait defaults on (notes are sequential)", ticks == [0, 24])

    # note_wait OFF stacks them, and a rest is what moves time.
    d, st = parse(sseq(b"\xC7\x00\x3C\x64\x18\x3E\x64\x18\x80\x18\x40\x64\x18\xFF"))
    tk = Track(d, st[0]).run()
    ticks = [e[0] for e in tk.events if e[1] == "note"]
    check("note_wait off stacks notes until a rest", ticks == [0, 0, 24])

    # A jump backwards names the loop, in ticks, at the tick the target first ran.
    body = b"\x3C\x64\x18\x3E\x64\x18\x94\x03\x00\x00"
    d, st = parse(sseq(body))
    tk = Track(d, st[0]).run()
    check("jump target becomes the loop point",
          (tk.loop_begin, tk.loop_end) == (24, 48))

    # Loop start/end with a count repeats the body that many times.
    d, st = parse(sseq(b"\xD4\x03\x3C\x64\x18\xFC\xFF"))
    tk = Track(d, st[0]).run()
    check("loopstart/loopend repeats by its count",
          len([e for e in tk.events if e[1] == "note"]) == 3)

    # Call and return.
    d, st = parse(sseq(b"\x95\x08\x00\x00\xFF\x00\x00\x00\x3C\x64\x18\xFD"))
    tk = Track(d, st[0]).run()
    check("call/return runs the subroutine once",
          len([e for e in tk.events if e[1] == "note"]) == 1)

    # Transpose shifts the key that reaches the MIDI, not the one in the stream.
    d, st = parse(sseq(b"\xC3\x0C\x3C\x64\x18\xFF"))
    tk = Track(d, st[0]).run()
    check("transpose applies to emitted keys",
          [e[2] for e in tk.events if e[1] == "note"] == [60 + 12])

    # Tempo is BPM in the sequence and microseconds in the MIDI.
    d, st = parse(sseq(b"\xE1\x5E\x00\x3C\x64\x18\xFF"))
    tk = Track(d, st[0]).run()
    check("tempo is read as BPM", tk.tempo == [(0, 94)])

    # Track table: an opentrack names an offset, track 0 starts after them all.
    d, st = parse(sseq(b"\xFE\x03\x00\x93\x01\x20\x00\x00\x3C\x64\x18\xFF"))
    check("track starts are read from opentrack", st == {0: 8, 1: 0x20})

    # The emitted MIDI carries both markers on track 0 and nowhere else.
    d, st = parse(sseq(b"\x3C\x64\x18\x3E\x64\x18\x94\x03\x00\x00"))
    walked = [(0, Track(d, st[0]).run())]
    blob = build_midi(walked, 24, 48)
    check("markers are written as text meta events",
          blob.count(b"\xFF\x06\x01[") == 1 and blob.count(b"\xFF\x06\x01]") == 1)

    # A loop that sounds nothing is a one-shot cue's silent vamp, and taking it
    # gives a song that plays once and goes quiet 45 seconds in.
    d, st = parse(sseq(b"\x3C\x64\x18\x80\x18\x80\xC0\x00\x94\x05\x00\x00"))
    walked = [(0, Track(d, st[0]).run())]
    tk = walked[0][1]
    check("a silent sequence loop is detected",
          silent_loop(walked, tk.loop_begin, tk.loop_end))
    check("a loop with notes in it is not called silent",
          not silent_loop(walked, 0, tk.loop_end))

    # An unknown opcode must stop rather than desynchronise the stream.
    try:
        parse(sseq(b"\x8A\xFF"))
        d, st = parse(sseq(b"\x8A\xFF"))
        Track(d, st[0]).run()
        check("an unknown opcode is refused, not skipped", False)
    except SystemExit:
        check("an unknown opcode is refused, not skipped", True)

    print()
    print(f"{len(fails)} failed" if fails else "all passed")
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", nargs="?")
    ap.add_argument("--seq")
    ap.add_argument("--out")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--whole-song", action="store_true",
                    help="loop the whole song rather than the sequence's own point")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if not (args.rom and args.seq):
        print(__doc__)
        return 2

    s, info, blob = load(args.rom, args.seq)
    data, starts = parse(blob)
    print(f"{args.seq}: bank {info['bank']}, {len(starts)} tracks, "
          f"{len(data)} bytes of sequence")

    if args.list or not args.out:
        do_list(data, starts)
        return 0

    walked, begin, end, why = convert(data, starts, args.out, args.whole_song)
    total = sum(len([e for e in t.events if e[1] == "note"]) for _, t in walked)
    print(f"wrote {args.out}: {len(walked)} tracks, {total} notes, "
          f"loop [{begin} -> ]{end} ticks "
          f"({begin / TPQN:.1f} -> {end / TPQN:.1f} beats)")
    print(f"  loop point: {why}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
