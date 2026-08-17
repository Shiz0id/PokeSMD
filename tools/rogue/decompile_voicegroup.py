#!/usr/bin/env python3
"""Decompile an m4a voicegroup out of a ROM image into decomp SOURCE.

Not a rip. The output is sound/voicegroups/*.inc full of voice_* macros, a
keysplit table file, and a sound/direct_sound_samples/*.wav per distinct
sample, which the ordinary build turns back into the same bytes via wav2agb.
Nothing is copied to a fixed offset and nothing is committed as a blob.

IT FOLLOWS KEYSPLITS. A serious General MIDI bank multi-samples each instrument
across the keyboard: the top-level voicegroup is mostly voice_keysplit (0x40)
and voice_keysplit_all (0x80) entries pointing at sub-voicegroups, and the
samples live down there. An earlier version of this tool emitted "@ TODO" for
those and only understood flat DirectSound banks -- which meant that when it was
pointed at a slab of sub-voicegroup data that happened to LOOK like a flat
128-entry bank, it decompiled it happily and produced a bank whose program
numbers meant nothing. Every instrument came out wrong and nothing failed.

DECOMPILING WHAT THE ENGINE READS IS WHAT MAKES THIS CORRECT. m4a indexes a
keysplit table as table[key] and a sub-voicegroup as group[table[key]], both
straight off the pointer in the ToneData. So do we -- which sidesteps the
mks4agb convention where a label deliberately points BEFORE its data (see the
comment at the top of sound/keysplit_tables.inc). We never need to recover the
original starting_note, because we re-emit from note 0 and the bytes the engine
reads at each key are identical.

A sub-voicegroup's LENGTH is derived, not guessed: it is one past the highest
index its keysplit table can produce. That also guarantees every index in the
table is in range by construction.

The round-trip check (check_voicegroup_roundtrip.py) proves each .wav rebuilds
to the bytes read from the ROM. It does NOT prove the right structure was read
-- validate that by decompiling a VANILLA voicegroup and diffing it against the
repo's hand-written source, which is what --verify does.

Usage:
    decompile_voicegroup.py ROM --offset 0x09130C5C --name all_instruments --repo .
    decompile_voicegroup.py ROM --offset 0x09130C5C --dry-run
    decompile_voicegroup.py ROM --verify 0x0867C5A4 --expect sound/voicegroups/abandoned_ship.inc
"""

import argparse
import hashlib
import json
import os
import re
import struct
import sys

ROM_BASE = 0x08000000
MAX_DEPTH = 4

GM_NAMES = [
    "ac_grand_piano", "bright_piano", "el_grand_piano", "honky_tonk",
    "el_piano_1", "el_piano_2", "harpsichord", "clavi",
    "celesta", "glockenspiel", "music_box", "vibraphone",
    "marimba", "xylophone", "tubular_bells", "dulcimer",
    "drawbar_organ", "perc_organ", "rock_organ", "church_organ",
    "reed_organ", "accordion", "harmonica", "tango_accordion",
    "nylon_guitar", "steel_guitar", "jazz_guitar", "clean_guitar",
    "muted_guitar", "overdriven_guitar", "distortion_guitar", "guitar_harmonics",
    "acoustic_bass", "finger_bass", "pick_bass", "fretless_bass",
    "slap_bass_1", "slap_bass_2", "synth_bass_1", "synth_bass_2",
    "violin", "viola", "cello", "contrabass",
    "tremolo_strings", "pizzicato_strings", "orch_harp", "timpani",
    "string_ens_1", "string_ens_2", "synth_strings_1", "synth_strings_2",
    "choir_aahs", "voice_oohs", "synth_voice", "orchestra_hit",
    "trumpet", "trombone", "tuba", "muted_trumpet",
    "french_horn", "brass_section", "synth_brass_1", "synth_brass_2",
    "soprano_sax", "alto_sax", "tenor_sax", "baritone_sax",
    "oboe", "english_horn", "bassoon", "clarinet",
    "piccolo", "flute", "recorder", "pan_flute",
    "blown_bottle", "shakuhachi", "whistle", "ocarina",
    "square_lead", "saw_lead", "calliope_lead", "chiff_lead",
    "charang_lead", "voice_lead", "fifths_lead", "bass_lead",
    "new_age_pad", "warm_pad", "poly_synth_pad", "choir_pad",
    "bowed_pad", "metallic_pad", "halo_pad", "sweep_pad",
    "fx_rain", "fx_soundtrack", "fx_crystal", "fx_atmosphere",
    "fx_brightness", "fx_goblins", "fx_echoes", "fx_sci_fi",
    "sitar", "banjo", "shamisen", "koto",
    "kalimba", "bagpipe", "fiddle", "shanai",
    "tinkle_bell", "agogo", "steel_drums", "woodblock",
    "taiko_drum", "melodic_tom", "synth_drum", "reverse_cymbal",
    "guitar_fret_noise", "breath_noise", "seashore", "bird_tweet",
    "telephone_ring", "helicopter", "applause", "gunshot",
]

# type byte -> (macro, kind). The inverse of asm/macros/music_voice.inc, which
# is the authority because the macro is what emits the type byte.
VOICE_TYPES = {
    0x00: ("voice_directsound", "directsound"),
    0x08: ("voice_directsound_no_resample", "directsound"),
    0x10: ("voice_directsound_alt", "directsound"),
    0x01: ("voice_square_1", "square_1"),
    0x09: ("voice_square_1_alt", "square_1"),
    0x02: ("voice_square_2", "square_2"),
    0x0A: ("voice_square_2_alt", "square_2"),
    0x03: ("voice_programmable_wave", "wave"),
    0x0B: ("voice_programmable_wave_alt", "wave"),
    0x04: ("voice_noise", "noise"),
    0x0C: ("voice_noise_alt", "noise"),
    0x40: ("voice_keysplit", "keysplit"),
    0x80: ("voice_keysplit_all", "keysplit_all"),
}

LOOP_FLAG = 0x4000


class Rom:
    def __init__(self, data):
        self.data = data

    def off(self, addr):
        o = addr - ROM_BASE if addr >= ROM_BASE else addr
        if not (0 <= o < len(self.data)):
            raise ValueError("address %08x is outside the ROM" % addr)
        return o

    def u8(self, o):
        return self.data[o]

    def u32(self, o):
        return struct.unpack_from("<I", self.data, o)[0]


def sanitise(name):
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_") or "x"


# --------------------------------------------------------------------------
# Reading
# --------------------------------------------------------------------------

def read_wave(rom, addr):
    o = rom.off(addr)
    typ = struct.unpack_from("<H", rom.data, o)[0]
    status = struct.unpack_from("<H", rom.data, o + 2)[0]
    freq, loop_start, size = struct.unpack_from("<III", rom.data, o + 4)

    if size == 0 or size > 4_000_000:
        raise ValueError("wave %08x has implausible size %d" % (addr, size))
    if loop_start > size:
        raise ValueError("wave %08x loops at %d past %d samples"
                         % (addr, loop_start, size))
    end = o + 16 + size
    if end > len(rom.data):
        raise ValueError("wave %08x runs past the end of the ROM" % addr)

    return {
        "addr": addr, "type": typ, "status": status, "freq": freq,
        "loop_start": loop_start, "size": size,
        "looped": bool(status & LOOP_FLAG),
        # EXACTLY size samples: convert_uncompressed_bin writes
        # `for i < loopEnd` and stops. The extra loop sample belongs to the .s
        # output path, not the binary one.
        "pcm": rom.data[o + 16:end],
    }


def read_keysplit(rom, addr):
    """The 128 bytes m4a can index as table[key]."""
    o = rom.off(addr)
    if o + 128 > len(rom.data):
        raise ValueError("keysplit table %08x runs past the ROM" % addr)
    return list(rom.data[o:o + 128])


class Bank:
    """One decompiled voicegroup, plus everything it reaches."""

    def __init__(self, rom):
        self.rom = rom
        self.groups = {}      # (addr, count) -> {"name":..., "entries":[...]}
        self.tables = {}      # addr -> {"name":..., "bytes":[...]}
        self.waves = {}       # addr -> wave dict
        self.wave_use = {}    # addr -> (program, key_lo, key_hi) first use
        self.pwaves = {}      # addr -> 16 bytes of programmable-wave data

    def entry_ok(self, addr, i):
        """Does entry i of the group at addr look like a real ToneData?

        Needed because neither a keysplit_all group nor a keysplit table
        carries its own length. m4a gets away with that -- it only ever indexes
        with keys a song actually plays -- but a decompiler walking the array
        has to stop somewhere, and the honest place is the first entry that
        cannot be a voice.
        """
        try:
            o = self.rom.off(addr) + i * 12
            if o + 12 > len(self.rom.data):
                return False
            typ = self.rom.u8(o)
            if typ not in VOICE_TYPES:
                return False
            kind = VOICE_TYPES[typ][1]
            ptr = self.rom.u32(o + 4)
            if kind == "directsound":
                read_wave(self.rom, ptr)
            elif kind in ("keysplit", "keysplit_all"):
                self.rom.off(ptr)
                if kind == "keysplit":
                    self.rom.off(self.rom.u32(o + 8))
            return True
        except (ValueError, struct.error):
            return False

    def probe_length(self, addr, limit=128):
        n = 0
        while n < limit and self.entry_ok(addr, n):
            n += 1
        return n

    def group(self, addr, count, name_hint, depth=0, program=None,
              key_ranges=None):
        """key_ranges[i] is the (lo, hi) span of MIDI keys that reach entry i.

        Carried down so each sample knows which part of the keyboard it serves,
        which is what lets a replacement bank pick the matching multi-sampled
        region rather than one sample for the whole range.
        """
        key = (addr, count)
        if key in self.groups:
            return self.groups[key]["name"]
        if depth > MAX_DEPTH:
            raise ValueError("keysplit nesting deeper than %d at %08x"
                             % (MAX_DEPTH, addr))

        name = self._unique(name_hint, {g["name"] for g in self.groups.values()})
        rec = {"name": name, "entries": []}
        # Register before recursing so a cycle terminates instead of hanging.
        self.groups[key] = rec

        base = self.rom.off(addr)
        for i in range(count):
            o = base + i * 12
            typ = self.rom.u8(o)
            if typ not in VOICE_TYPES:
                raise ValueError(
                    "%s entry %d has voice type %02x, which no macro emits --"
                    " the offset or the length is wrong" % (name, i, typ))
            macro, kind = VOICE_TYPES[typ]
            pan_byte = self.rom.u8(o + 3)
            e = {
                "macro": macro, "kind": kind,
                "key": self.rom.u8(o + 1),
                "length": self.rom.u8(o + 2),
                "pan": (pan_byte & 0x7F) if (pan_byte & 0x80) else 0,
                "ptr": self.rom.u32(o + 4),
                "tail": self.rom.u32(o + 8),
                "attack": self.rom.u8(o + 8), "decay": self.rom.u8(o + 9),
                "sustain": self.rom.u8(o + 10), "release": self.rom.u8(o + 11),
            }

            lo, hi = (key_ranges[i] if key_ranges and i < len(key_ranges)
                      else (0, 127))
            prog = i if program is None else program
            hint = GM_NAMES[prog] if (program is None and prog < len(GM_NAMES)) \
                else "%s_%02d" % (name_hint, i)

            if kind == "directsound":
                e["wave"] = self._wave(e["ptr"], prog, lo, hi, hint)
            elif kind == "wave":
                # Programmable wave: 16 bytes of 4-bit samples for the GBA's
                # wave-RAM channel, not a DirectSound sample.
                e["pwave"] = self._pwave(e["ptr"], hint)
            elif kind == "keysplit":
                # The table is the UPPER BOUND on the sub-group's length --
                # that is what it is for -- and validation trims it from there.
                # Probing without that bound over-reads into the next
                # voicegroup and then recurses into ITS keysplits, which is a
                # runaway that only stops at the depth limit.
                raw = read_keysplit(self.rom, e["tail"])
                sub_len = self.probe_length(e["ptr"], min(max(raw) + 1, 128))
                if sub_len == 0:
                    raise ValueError("keysplit target %08x holds no valid"
                                     " voices" % e["ptr"])
                tbl = self._table(e["tail"], hint, sub_len)
                e["table"] = tbl
                spans = {}
                for k, idx in enumerate(self.tables[e["tail"]]["bytes"]):
                    a, b = spans.get(idx, (k, k))
                    spans[idx] = (min(a, k), max(b, k))
                sub_ranges = [spans.get(j, (0, 127)) for j in range(sub_len)]
                e["group"] = self.group(e["ptr"], sub_len, hint,
                                        depth + 1, prog, sub_ranges)
            elif kind == "keysplit_all":
                # Indexed directly by key, but the array is not guaranteed to
                # be 128 long -- the original relies on songs never asking for
                # a key past its end, and reading 128 walks into other data.
                sub_len = self.probe_length(e["ptr"])
                if sub_len == 0:
                    raise ValueError("keysplit_all target %08x holds no valid"
                                     " voices" % e["ptr"])
                e["group"] = self.group(e["ptr"], sub_len, hint,
                                        depth + 1, prog,
                                        [(j, j) for j in range(sub_len)])

            rec["entries"].append(e)

        return name

    def _wave(self, addr, program, lo, hi, hint):
        if addr not in self.waves:
            self.waves[addr] = read_wave(self.rom, addr)
            self.wave_use[addr] = (program, lo, hi, hint)
        return addr

    def _pwave(self, addr, hint):
        if addr not in self.pwaves:
            name = self._unique(hint, {p["name"] for p in self.pwaves.values()})
            o = self.rom.off(addr)
            self.pwaves[addr] = {"name": name,
                                 "bytes": list(self.rom.data[o:o + 16])}
        return self.pwaves[addr]["name"]

    def _table(self, addr, hint, sub_len):
        if addr not in self.tables:
            name = self._unique(hint, {t["name"] for t in self.tables.values()})
            raw = read_keysplit(self.rom, addr)
            # Clamp into the sub-group. Any index at or past its length came
            # from bytes beyond the table's real extent -- keys the original
            # never mapped -- and left alone they would index a neighbouring
            # voicegroup, which is the one thing this must not reproduce.
            clamped = sum(1 for b in raw if b >= sub_len)
            self.tables[addr] = {
                "name": name,
                "bytes": [min(b, sub_len - 1) for b in raw],
                "clamped": clamped,
            }
        return self.tables[addr]["name"]

    @staticmethod
    def _unique(hint, taken):
        base = sanitise(hint)
        if base not in taken:
            return base
        n = 2
        while "%s_%d" % (base, n) in taken:
            n += 1
        return "%s_%d" % (base, n)


# --------------------------------------------------------------------------
# Emitting
# --------------------------------------------------------------------------

def emit_entry(e, prefix, sample_names, group_prefix, table_prefix):
    m = e["macro"]
    if e["kind"] == "directsound":
        return "\t%s %d, %d, %s%s, %d, %d, %d, %d" % (
            m, e["key"], e["pan"], prefix, sample_names[e["wave"]],
            e["attack"], e["decay"], e["sustain"], e["release"])
    if e["kind"] == "keysplit":
        return "\t%s %s%s, %s%s" % (m, group_prefix, e["group"],
                                    table_prefix, e["table"])
    if e["kind"] == "keysplit_all":
        return "\t%s %s%s" % (m, group_prefix, e["group"])
    if e["kind"] == "square_1":
        return "\t%s %d, %d, %d, %d, %d, %d, %d, %d" % (
            m, e["key"], e["pan"], e["length"], e["ptr"] & 3,
            e["attack"] & 7, e["decay"] & 7, e["sustain"] & 15, e["release"] & 7)
    if e["kind"] == "square_2":
        return "\t%s %d, %d, %d, %d, %d, %d, %d" % (
            m, e["key"], e["pan"], e["ptr"] & 3,
            e["attack"] & 7, e["decay"] & 7, e["sustain"] & 15, e["release"] & 7)
    if e["kind"] == "noise":
        return "\t%s %d, %d, %d, %d, %d, %d, %d" % (
            m, e["key"], e["pan"], e["ptr"] & 1,
            e["attack"] & 7, e["decay"] & 7, e["sustain"] & 15, e["release"] & 7)
    if e["kind"] == "wave":
        return "\t%s %d, %d, ProgrammableWaveData_%s, %d, %d, %d, %d" % (
            m, e["key"], e["pan"], e["pwave"],
            e["attack"] & 7, e["decay"] & 7, e["sustain"] & 15, e["release"] & 7)
    return "\t@ UNSUPPORTED %s -> %08x" % (m, e["ptr"])


def emit_tables(bank, label_prefix):
    out = ["@ Decompiled keysplit tables. Emitted from note 0 with no",
           "@ starting_note: m4a reads table[key], and re-emitting the full",
           "@ range reproduces the byte at every key the engine can ask for.",
           ""]
    for addr in sorted(bank.tables):
        t = bank.tables[addr]
        out.append("keysplit %s%s" % (label_prefix, t["name"]))
        b = t["bytes"]
        run_start = 0
        for k in range(1, 129):
            if k == 128 or b[k] != b[run_start]:
                out.append("\tsplit %d, %d" % (b[run_start], k))
                run_start = k
        out.append("")
    return "\n".join(out)


def emit_groups(bank, root_name, sample_names, prefix, label_prefix):
    """Root keeps its own name; every sub-voicegroup gets label_prefix.

    `voice_group ai_foo` defines the symbol `voicegroup_ai_foo`, and
    `keysplit ai_foo` defines `keysplit_ai_foo`, so the label written and the
    symbol referenced differ by a fixed prefix each. Keeping those straight is
    the whole reason they are separate arguments here.
    """
    group_ref = "voicegroup_" + label_prefix
    table_ref = "keysplit_" + label_prefix

    out = ["@ Decompiled by tools/rogue/decompile_voicegroup.py.",
           "@ Regenerate rather than edit.", ""]

    # Root last, so every symbol it references is already defined above it.
    ordered = sorted(bank.groups.items(),
                     key=lambda kv: (kv[1]["name"] == root_name,
                                     kv[1]["name"]))
    for (_addr, _count), g in ordered:
        is_root = g["name"] == root_name
        out.append("voice_group %s" % (root_name if is_root
                                       else label_prefix + g["name"]))
        for e in g["entries"]:
            out.append(emit_entry(e, prefix, sample_names,
                                  group_ref, table_ref))
        out.append("")
    return "\n".join(out)


def chunk(cid, payload):
    out = cid + struct.pack("<I", len(payload)) + payload
    if len(payload) % 2:
        out += b"\x00"
    return out


def build_wav(w, midi_key=60):
    rate = w["freq"] // 1024 or 1
    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate, 1, 8)
    smpl = struct.pack("<IIIIIIIII", 0, 0, 1_000_000_000 // rate, midi_key,
                       0, 0, 0, 1 if w["looped"] else 0, 0)
    if w["looped"]:
        smpl += struct.pack("<IIIIII", 0, 0, w["loop_start"], w["size"] - 1, 0, 0)
    body = b"WAVE"
    body += chunk(b"fmt ", fmt)
    body += chunk(b"smpl", smpl)
    # Exactness: agbp carries the pitch word verbatim, agbl the loop end.
    body += chunk(b"agbp", struct.pack("<I", w["freq"]))
    body += chunk(b"agbl", struct.pack("<I", w["size"]))
    body += chunk(b"data", bytes((s + 128) & 0xFF for s in
                                 struct.unpack("<%db" % len(w["pcm"]), w["pcm"])))
    return b"RIFF" + struct.pack("<I", len(body)) + body


# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("--offset", help="GBA pointer or file offset of the voicegroup")
    ap.add_argument("--count", type=int, default=128)
    ap.add_argument("--name", default="all_instruments")
    ap.add_argument("--repo")
    ap.add_argument("--symbol-prefix", default="DirectSoundWaveData_")
    ap.add_argument("--sample-prefix", default="ai_")
    ap.add_argument("--label-prefix", default="ai_",
                    help="prepended to every sub-voicegroup and keysplit label")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", help="decompile this address and print it, for"
                                     " diffing against known-good source")
    args = ap.parse_args()

    with open(args.rom, "rb") as f:
        rom = Rom(f.read())

    target = args.verify or args.offset
    if not target:
        ap.error("need --offset or --verify")

    bank = Bank(rom)
    root = bank.group(int(target, 0), args.count, args.name)

    # Name samples after the first program and key range that reaches them.
    sample_names = {}
    taken = set()
    for addr in bank.waves:
        program, lo, hi, hint = bank.wave_use[addr]
        base = sanitise(hint)
        n, cand = 1, base
        while cand in taken:
            n += 1
            cand = "%s_%d" % (base, n)
        taken.add(cand)
        sample_names[addr] = args.sample_prefix + cand

    total = sum(16 + len(w["pcm"]) + (-(16 + len(w["pcm"])) % 4)
                for w in bank.waves.values())
    print("%d voicegroups, %d keysplit tables, %d distinct samples,"
          " %d bytes (%.2f MB)"
          % (len(bank.groups), len(bank.tables), len(bank.waves),
             total, total / 1048576))

    inc = emit_groups(bank, root, sample_names, args.symbol_prefix,
                      args.label_prefix)

    if args.verify:
        # The root group only -- that is what gets diffed against known-good
        # hand-written source. Sub-voicegroups are named by this tool and will
        # never match the repo's names.
        lines = inc.splitlines()
        start = lines.index("voice_group %s" % root)
        end = start + 1
        while end < len(lines) and lines[end].startswith("\t"):
            end += 1
        print()
        print("\n".join(lines[start:end]))
        return

    if args.dry_run or not args.repo:
        print()
        print("\n".join(inc.splitlines()[:24]))
        if not args.repo:
            print("\n(nothing written)")
        return

    vg = os.path.join(args.repo, "sound", "voicegroups", args.name + ".inc")
    with open(vg, "w", newline="\n") as f:
        f.write(inc)
    print("wrote %s" % vg)

    kt = os.path.join(args.repo, "sound",
                      "keysplit_tables_%s.inc" % args.name)
    with open(kt, "w", newline="\n") as f:
        f.write(emit_tables(bank, args.label_prefix))
    print("wrote %s" % kt)

    smp_dir = os.path.join(args.repo, "sound", "direct_sound_samples")
    manifest = {"voicegroup": args.name, "source_offset": int(target, 0),
                "samples": {}}
    for addr, w in bank.waves.items():
        name = sample_names[addr]
        with open(os.path.join(smp_dir, name + ".wav"), "wb") as f:
            f.write(build_wav(w))
        expected = struct.pack("<IIII", w["type"] | (w["status"] << 16),
                               w["freq"], w["loop_start"], w["size"]) + w["pcm"]
        expected += b"\x00" * (-len(expected) % 4)
        program, lo, hi, _ = bank.wave_use[addr]
        manifest["samples"][name] = {
            "program": program, "key_lo": lo, "key_hi": hi,
            "addr": addr, "freq": w["freq"], "loop_start": w["loop_start"],
            "size": w["size"], "looped": w["looped"],
            "pcm_bytes": len(w["pcm"]),
            "bin_sha256": hashlib.sha256(expected).hexdigest(),
            "bin_bytes": len(expected),
        }
    print("wrote %d .wav files" % len(bank.waves))

    data_lines = []
    for addr in sorted(bank.waves, key=lambda a: sample_names[a]):
        n = sample_names[addr]
        data_lines += ["\t.align 2", "%s%s::" % (args.symbol_prefix, n),
                       '\t.incbin "sound/direct_sound_samples/%s.bin"' % n, ""]
    dp = os.path.join(args.repo, "sound",
                      "direct_sound_data_%s.inc" % args.name)
    with open(dp, "w", newline="\n") as f:
        f.write("@ Decompiled. Regenerate rather than edit.\n\n")
        f.write("\n".join(data_lines))
    print("wrote %s" % dp)

    mp = os.path.join(args.repo, "tools", "rogue",
                      "%s_manifest.json" % args.name)
    with open(mp, "w", newline="\n") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
        f.write("\n")
    print("wrote %s" % mp)


if __name__ == "__main__":
    main()
