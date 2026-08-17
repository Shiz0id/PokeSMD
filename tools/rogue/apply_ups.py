#!/usr/bin/env python3
"""Apply a UPS patch, verifying both CRC32s.

Step one of decompiling a binary-hack patch: a UPS is an XOR delta, so the
patch's contents do not exist as bytes until it is applied to the exact ROM it
was built against. This is input acquisition, not the decompile -- what turns
the result into source is decompile_voicegroup.py, which reads structures out of
the patched image and emits .inc and .wav.

VERIFIES RATHER THAN TRUSTS. The format carries a CRC32 of the input, the
output and the patch itself, and this checks all three. A UPS applied to the
wrong base does not fail loudly: it XORs the delta into whatever is there and
produces a plausible-looking ROM full of garbage at exactly the offsets you
care about. Skipping the input check is how you spend a day decompiling noise.

Usage:
    apply_ups.py BASE.gba PATCH.ups OUT.gba
    apply_ups.py BASE.gba PATCH.ups OUT.gba --force   # ignore a CRC mismatch
"""

import argparse
import sys
import zlib


def read_vli(data, pos):
    """UPS variable-length integer. Not LEB128 -- the continuation arithmetic
    differs, and decoding it as LEB128 gives offsets that are almost right."""
    value = 0
    shift = 1
    while True:
        byte = data[pos]
        pos += 1
        value += (byte & 0x7F) * shift
        if byte & 0x80:
            break
        shift <<= 7
        value += shift
    return value, pos


def apply_ups(base, patch, force=False):
    problems = []

    if patch[:4] != b"UPS1":
        raise SystemExit("not a UPS1 patch (magic is %r)" % patch[:4])

    # The trailer is three little-endian CRC32s: input, output, patch.
    if len(patch) < 4 + 12:
        raise SystemExit("patch is too short to hold a trailer")

    want_in = int.from_bytes(patch[-12:-8], "little")
    want_out = int.from_bytes(patch[-8:-4], "little")
    want_patch = int.from_bytes(patch[-4:], "little")

    got_patch = zlib.crc32(patch[:-4])
    if got_patch != want_patch:
        problems.append(
            "patch CRC32 is %08x, its own trailer says %08x -- the .ups file is"
            " corrupt" % (got_patch, want_patch)
        )

    pos = 4
    in_size, pos = read_vli(patch, pos)
    out_size, pos = read_vli(patch, pos)

    if len(base) != in_size:
        problems.append(
            "base ROM is %d bytes, patch expects %d" % (len(base), in_size)
        )

    got_in = zlib.crc32(base)
    if got_in != want_in:
        problems.append(
            "base ROM CRC32 is %08x, patch expects %08x -- this is the wrong ROM"
            % (got_in, want_in)
        )

    if problems and not force:
        for p in problems:
            print("FAIL: " + p, file=sys.stderr)
        raise SystemExit(1)

    out = bytearray(out_size)
    out[: min(len(base), out_size)] = base[:out_size]

    outpos = 0
    end = len(patch) - 12
    while pos < end:
        rel, pos = read_vli(patch, pos)
        outpos += rel
        while pos < end and patch[pos] != 0:
            if outpos < out_size:
                out[outpos] ^= patch[pos]
            outpos += 1
            pos += 1
        pos += 1     # the 0x00 terminating this block
        outpos += 1  # which is itself an implied XOR-with-zero

    got_out = zlib.crc32(out)
    if got_out != want_out:
        msg = ("output CRC32 is %08x, patch expects %08x" % (got_out, want_out))
        if not force:
            print("FAIL: " + msg, file=sys.stderr)
            raise SystemExit(1)
        print("WARNING: " + msg, file=sys.stderr)
    else:
        print("OK: output CRC32 %08x matches the patch's trailer." % got_out)

    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("base")
    ap.add_argument("patch")
    ap.add_argument("out")
    ap.add_argument("--force", action="store_true",
                    help="apply even if a CRC32 does not match")
    args = ap.parse_args()

    with open(args.base, "rb") as f:
        base = f.read()
    with open(args.patch, "rb") as f:
        patch = f.read()

    out = apply_ups(base, patch, args.force)

    with open(args.out, "wb") as f:
        f.write(out)

    print("Wrote %s (%d bytes)." % (args.out, len(out)))


if __name__ == "__main__":
    main()
