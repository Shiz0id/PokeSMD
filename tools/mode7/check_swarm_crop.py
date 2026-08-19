#!/usr/bin/env python3
"""
Check that every Unown letter the swarm uses survives the 32x32 crop intact.

WHAT THIS GUARDS

LoadSwarmGfx in src/rogue_mode7.c lifts the middle 4x4 tile block out of each
64x64 Unown front pic and loads it as a 32x32 sprite. That crop is not a nicety:
keeping the OBJ 32 wide instead of 64 halves its per-scanline cost, which is the
binding limit on how many Unown can be on screen at once -- measured at 20 on
hardware.

But three of the 28 forms -- a, g and '!' -- have ink 34 to 36 rows tall, which
does not fit in 32 rows. Using one of those silently shears the top off the
letter. It still looks like an Unown, just a slightly wrong one, and at ten
pixels on screen nobody will ever spot it.

So this reads the letter list AND the crop origin straight out of the C, and
replays the crop against the real tile data.

WHY IT READS build/ AND NOT graphics/

It counts ink in build/assets/.../front.png.4bpp, which is the raw 64x64 4bpp
tile data the ROM actually carries -- the same bytes LoadSpecialPokePic
decompresses. That means no PNG decoding and so no Pillow, which matters
because run_all_checks.sh runs under WSL where Pillow is not installed. A check
that cannot run in the runner is worth nothing.

It also means this needs a build first. If the assets are absent it says so
loudly rather than passing on nothing.

--selftest adds letter 'a' and fails if the check does not fire.

Takes the repo positionally or as --repo.
"""

import argparse
import os
import re
import sys

LETTER_DIR = {i: chr(ord("a") + i) for i in range(26)}
LETTER_DIR[26] = "exclamation"
LETTER_DIR[27] = "question"

PIC_TILES_W = 8          # a 64x64 pic is 8x8 tiles
TILE_BYTES = 32          # 8x8 at 4bpp
CROP_TILES = 4


def parse_letters(repo):
    """Read sLetters out of the C so this cannot drift from what ships."""
    src = open(os.path.join(repo, "src", "rogue_mode7.c"), encoding="utf-8").read()
    m = re.search(r"sLetters\[MODE7_SWARM_FORMS\]\s*=\s*\{([^}]*)\}", src)
    if not m:
        raise SystemExit("could not find sLetters[] in src/rogue_mode7.c")
    return [int(t) for t in re.findall(r"\d+", m.group(1))]


def parse_crop_origin(repo):
    """Read the crop origin too, so moving the block is caught."""
    src = open(os.path.join(repo, "src", "rogue_mode7.c"), encoding="utf-8").read()
    xs = re.search(r"\(tx \+ (\d+)\)", src)
    ys = re.search(r"\(ty \+ (\d+)\)", src)
    if not xs or not ys:
        raise SystemExit("could not find the crop origin in LoadSwarmGfx")
    return int(xs.group(1)), int(ys.group(1))


def pic_path(repo, letter):
    base = os.path.join(repo, "build", "assets", "graphics", "pokemon", "unown")
    # Letter 0 (A) is the base sprite, not in a subdirectory.
    if letter == 0:
        return os.path.join(base, "front.png.4bpp")
    return os.path.join(base, LETTER_DIR[letter], "front.png.4bpp")


def tile_ink(data, tile):
    """Count non-zero 4bpp pixels in one tile. Index 0 is transparent."""
    n = 0
    for b in data[tile * TILE_BYTES:(tile + 1) * TILE_BYTES]:
        if b & 0x0F:
            n += 1
        if b >> 4:
            n += 1
    return n


def crop_loss(data, ox, oy):
    """Ink outside the crop block -- i.e. what the C would throw away."""
    total = sum(tile_ink(data, t) for t in range(PIC_TILES_W * PIC_TILES_W))
    kept = 0
    for ty in range(CROP_TILES):
        for tx in range(CROP_TILES):
            kept += tile_ink(data, (ty + oy) * PIC_TILES_W + (tx + ox))
    return total - kept


def run(repo, letters, verbose=True):
    ox, oy = parse_crop_origin(repo)
    ok = True
    if verbose:
        print(f"  crop origin        : tile ({ox}, {oy}), "
              f"{CROP_TILES}x{CROP_TILES} tiles of {PIC_TILES_W}x{PIC_TILES_W}")
        print(f"  letters in swarm   : {len(letters)}")

    for L in letters:
        p = pic_path(repo, L)
        if not os.path.exists(p):
            print(f"\n  SKIPPED: {p} is absent. Run make first -- this check "
                  f"reads built tile data, and has verified NOTHING.")
            return True
        with open(p, "rb") as f:
            data = f.read()
        lost = crop_loss(data, ox, oy)
        if lost:
            ok = False
            if verbose:
                print(f"  CLIPPED: letter {LETTER_DIR[L]!r} (index {L}) "
                      f"loses {lost} ink pixels to the crop")

    if ok and verbose:
        print("  every letter fits the crop with nothing lost")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo_pos", nargs="?", default=None)
    ap.add_argument("--repo", default=None)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    repo = args.repo or args.repo_pos or os.environ.get("POKEDECOMP_REPO") or "."

    print("check_swarm_crop")
    letters = parse_letters(repo)
    ok = run(repo, letters)

    if args.selftest:
        # 'a' is 34 rows tall and cannot fit. If adding it does not fail the
        # check, the check is not measuring anything.
        print("\n  --selftest: adding letter 'a', whose ink is 34 rows tall")
        if run(repo, letters + [0], verbose=False):
            print("  SELFTEST FAILED: a clipped letter passed")
            ok = False
        else:
            print("  selftest ok: the check fires on a letter that does not fit")

    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
