#!/usr/bin/env python3
"""A back pic's declared frame count is a promise to three separate things.

THE RULE, once: `TRAINER_BACK_PIC(n, sprite, pal, anim)` declares n frames -
the argument is named yOffset and is used as a frame COUNT, which is worth
knowing before reading any of this. Three things must agree with that number,
and each disagreement fails somewhere else:

  1. THE BUFFER. CopyTrainerBackspriteFramesToDest copies n * TRAINER_PIC_SIZE
     bytes into whatever it is handed, and the handers allocate
     MAX_TRAINER_PIC_FRAMES frames. n larger is a HEAP OVERFLOW - no error, no
     crash where the damage is done, and a console that stops somewhere else
     entirely.

     This is not hypothetical. Red and Leaf declare 5, the constant said 4, and
     upstream left a comment saying `presumably irrelevant in the places this is
     used`. The outfit picker redraws a back pic on every cursor move, so
     resting on the Kanto outfit wrote 2 KB past a heap allocation twice a
     second. It froze with a stray sound effect playing - the heap it had just
     walked over.

  2. THE PIXELS. n frames of 64x64 must actually be behind the symbol. Claiming
     more than the sheet holds reads past the end of it and copies whatever the
     linker put next.

  3. THE ANIMS. No anim in the row's table may name a frame >= n, or the sprite
     draws a frame the copy never brought over.

WHY A SCRIPT AND NOT A STATIC_ASSERT: the frame counts live inside compound
literals in a table of a thousand rows, the pixel counts live in PNG headers,
and nothing in C can compare those at build time. That asymmetry - data the
compiler cannot see, checked by a constant it can - is what made this bug
possible.

Run with --selftest to break each assertion in turn and require it to fire.
A check that has never failed is worth nothing.
"""

import argparse
import re
import struct
import sys
from pathlib import Path

TRAINERS = Path("src/data/graphics/trainers.h")
DATA_H = Path("include/data.h")

TRAINER_PIC_WIDTH = 64
TRAINER_PIC_HEIGHT = 64
FRAME_BYTES = TRAINER_PIC_WIDTH * TRAINER_PIC_HEIGHT // 2


def read(repo, path):
    return (repo / path).read_text(encoding="utf-8")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def png_or_raw_bytes(repo, rel):
    """Pixel bytes behind an asset path, 4bpp, or None."""
    p = repo / rel
    if not p.exists():
        return None
    if rel.endswith(".png"):
        head = p.read_bytes()[:24]
        if head[12:16] != b"IHDR":
            return None
        w, h = struct.unpack(">II", head[16:24])
        return (w * h) // 2
    return p.stat().st_size


def sheet_bytes(repo, symbol):
    """Total pixel bytes behind a gTrainerBackPic_* symbol, or None."""
    text = read(repo, TRAINERS)
    m = re.search(r"\b" + re.escape(symbol) + r"\[\]\s*=\s*(?:INCGFX|INCBIN)_\w+\((.*?)\);", text, re.S)
    if not m:
        return None
    total = 0
    for arg in re.findall(r'"([^"]+)"', m.group(1)):
        if arg.startswith("-") or arg.startswith("."):
            continue  # a converter flag or an output extension
        got = png_or_raw_bytes(repo, arg)
        if got is None:
            return None
        total += got
    return total or None


def anim_frames(repo):
    """sAnim*_ -> the frames it names."""
    text = strip_comments(read(repo, TRAINERS))
    out = {}
    for m in re.finditer(r"\b(sAnim\w+)\[\]\s*=\s*\{(.*?)\};", text, re.S):
        out[m.group(1)] = {int(f) for f in re.findall(r"ANIMCMD_FRAME\(\s*(\d+)", m.group(2))}
    return out


def anim_tables(repo):
    """sBackAnims_* -> the frames every anim in it names."""
    text = strip_comments(read(repo, TRAINERS))
    frames = anim_frames(repo)
    out = {}
    for m in re.finditer(r"\b(sBackAnims_\w+)\[\]\s*=\s*\{(.*?)\};", text, re.S):
        used = set()
        for sym in re.findall(r"sAnim\w+", m.group(2)):
            used |= frames.get(sym, set())
        out[m.group(1)] = used
    return out


def check(repo, fail):
    data_h = read(repo, DATA_H)
    m = re.search(r"#define\s+MAX_TRAINER_PIC_FRAMES\s+(\d+)", data_h)
    if not m:
        fail("MAX_TRAINER_PIC_FRAMES not found in %s" % DATA_H)
        return
    max_frames = int(m.group(1))

    text = strip_comments(read(repo, TRAINERS))
    tables = anim_tables(repo)

    rows = re.findall(
        r"\[(TRAINER_PIC_[A-Z0-9_]+)\]\s*=\s*\{(.*?)\n    \},", text, re.S
    )
    if not rows:
        fail("no rows parsed out of %s" % TRAINERS)
        return

    seen = 0
    for name, body in rows:
        m2 = re.search(
            r"TRAINER_BACK_PIC\(\s*(\d+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\)", body
        )
        if not m2:
            continue
        seen += 1
        declared = int(m2.group(1))
        symbol = m2.group(2)
        anims = m2.group(4)

        # 1. THE BUFFER.
        if declared > max_frames:
            fail(
                "%s declares %d back pic frames and MAX_TRAINER_PIC_FRAMES is %d. "
                "CopyTrainerBackspriteFramesToDest copies every declared frame into "
                "a buffer allocated for %d, so this writes %d bytes past the end of "
                "a heap allocation - which is not where the game will stop"
                % (name, declared, max_frames, max_frames,
                   (declared - max_frames) * FRAME_BYTES)
            )

        # 2. THE PIXELS.
        total = sheet_bytes(repo, symbol)
        if total is None:
            fail(
                "%s: cannot measure the pixels behind %s. Teach sheet_bytes() this "
                "shape - an unmeasurable sheet is an unchecked one" % (name, symbol)
            )
        else:
            have = total // FRAME_BYTES
            if declared != have:
                fail(
                    "%s declares %d back pic frames and %s holds %d. %s"
                    % (
                        name, declared, symbol, have,
                        "The copy reads past the end of the sheet."
                        if declared > have
                        else "The extra frames are unreachable, which means one of "
                             "the two numbers is a mistake.",
                    )
                )

        # 3. THE ANIMS.
        used = tables.get(anims)
        if used is None:
            fail("%s names anim table %s, which was not found" % (name, anims))
        elif used and max(used) >= declared:
            fail(
                "%s declares %d back pic frames and %s animates frame %d - the copy "
                "never brought that frame over, so the sprite draws whatever follows "
                "it in the buffer" % (name, declared, anims, max(used))
            )

    if seen < 5:
        fail("only %d back pic rows parsed - the row pattern has drifted" % seen)


BREAKS = [
    (
        "a back pic declaring more frames than the buffer holds",
        DATA_H,
        lambda s: s.replace("#define MAX_TRAINER_PIC_FRAMES 5", "#define MAX_TRAINER_PIC_FRAMES 4", 1),
    ),
    (
        "a row claiming a frame its sheet does not have",
        TRAINERS,
        lambda s: s.replace(
            "TRAINER_BACK_PIC(4, gTrainerBackPic_Brendan,",
            "TRAINER_BACK_PIC(5, gTrainerBackPic_Brendan,",
            1,
        ),
    ),
    (
        "a row leaving a frame of its sheet unreachable",
        TRAINERS,
        lambda s: s.replace(
            "TRAINER_BACK_PIC(5, gTrainerBackPic_Red,",
            "TRAINER_BACK_PIC(3, gTrainerBackPic_Red,",
            1,
        ),
    ),
    (
        "an anim naming a frame past the declared count",
        TRAINERS,
        lambda s: s.replace(
            "TRAINER_BACK_PIC(4, gTrainerBackPic_Brendan, gTrainerPalette_Brendan, sBackAnims_Hoenn)",
            "TRAINER_BACK_PIC(4, gTrainerBackPic_Brendan, gTrainerPalette_Brendan, sBackAnims_Kanto)",
            1,
        ),
    ),
]


def run_check(repo):
    failures = []
    seen = set()

    def fail(msg):
        if msg not in seen:
            seen.add(msg)
            failures.append(msg)

    check(repo, fail)
    return failures


def selftest(repo):
    base = run_check(repo)
    if base:
        print("SELFTEST ABORTED: the tree does not pass to begin with:")
        for f in base:
            print("  " + f)
        return 1

    bad = 0
    for label, path, mutate in BREAKS:
        full = repo / path
        original = full.read_text(encoding="utf-8")
        mutated = mutate(original)
        if mutated == original:
            print("  NO-OP  %s -- the mutation matched nothing, so it proves nothing" % label)
            bad += 1
            continue
        try:
            full.write_text(mutated, encoding="utf-8", newline="\n")
            failures = run_check(repo)
        finally:
            full.write_text(original, encoding="utf-8", newline="\n")
        if failures:
            print("  fires  %s" % label)
        else:
            print("  SILENT %s -- this break is not caught" % label)
            bad += 1

    if bad:
        print("\n%d of %d breaks were not caught." % (bad, len(BREAKS)))
        return 1
    print("\nAll %d breaks fire." % len(BREAKS))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    # Takes the repo BOTH ways, the check_start_menu_pages.py pattern.
    ap.add_argument("repo_pos", nargs="?", default=None, help="path to the decomp repo")
    ap.add_argument("--repo", default=None, help="path to the decomp repo")
    ap.add_argument(
        "--selftest", action="store_true", help="break each assertion and require it to fire"
    )
    args = ap.parse_args()

    repo = Path(args.repo or args.repo_pos or ".").resolve()
    if not (repo / TRAINERS).exists():
        print("FAIL: %s not found under %s" % (TRAINERS, repo))
        return 1

    if args.selftest:
        return selftest(repo)

    failures = run_check(repo)
    if failures:
        for f in failures:
            print("FAIL: " + f)
        return 1
    print("check_trainer_backpic_frames: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
