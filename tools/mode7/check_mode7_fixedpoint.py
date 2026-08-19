#!/usr/bin/env python3
"""
Check the integer scanline maths in src/rogue_mode7.c against a floating point
reference, and check that nothing overflows the registers it is written into.

WHAT THIS GUARDS

RogueMode7_BuildScanlineTable is pure fixed-point arithmetic feeding registers
with hard limits, and the ways it goes wrong are silent:

  * REG_BG2PA/PC are s16 and REG_BG2X/Y are s32. A value that does not fit does
    not fail, it wraps, and the row shears.
  * The intermediate `height * sInvH[h]` is computed as u32. Above a camera
    height of 256.0 that product overflows and lambda comes out small, so the
    ground silently flattens instead of the camera rising.
  * A sign error or a missing shift produces a plane that is wrong in a way
    that looks deliberate.

MEASURED IN SCREEN PIXELS, NOT TEXELS

The first version of this check compared absolute texture coordinates and fired
at 58 texels of deviation. That was the metric being wrong, not the C: the row
it fired on was one scanline below the horizon, where lambda is 120 texels per
screen pixel and the view distance is 19,200 texels. gSineTable is Q8.8 and is
0.7% off at that index, and 0.7% of 19,200 is 55 texels -- which is 0.46 SCREEN
pixels, i.e. invisible. Absolute texel error is meaningless where one pixel
covers a hundred texels, so everything here is divided by lambda first.

WHAT THIS DOES NOT GUARD

The Tonc "Type C" ordering rule -- BG2X derived from the rounded pa rather than
from lambda -- is BELOW THE NOISE FLOOR at this precision. Breaking it moves the
sampled point by at most 120 * 255/256 in .8, which is under half a texel. The
--selftest run demonstrates this rather than hiding it: the ordering break is
listed as EXPECTED-PASS. The rule is still worth following in the C, and the
host prototype measured its real cost, but do not believe this script is
watching it.

--selftest breaks three things that ARE caught and fails if any still passes.

Takes the repo positionally or as --repo, so it cannot end up on the wrong side
of run_all_checks.sh's hand-maintained list.
"""

import argparse
import math
import os
import re
import sys

S16_MIN, S16_MAX = -32768, 32767
S32_MIN, S32_MAX = -(1 << 31), (1 << 31) - 1
U32_MAX = (1 << 32) - 1

DISPLAY_WIDTH = 240
DISPLAY_HEIGHT = 160
PROJ_D = 160

# Screen pixels. This is deliberately LOOSE, because it is here to catch gross
# errors -- a wrong sign, a missing shift, reordered struct fields -- and not to
# police the last texel.
#
# The floor on it is lambda's own quantisation: lambda is Q24.8, so it carries
# an absolute error of 1/256 texels-per-pixel, which at the bottom of the screen
# at low altitude (lambda ~0.35) is a 1.1% relative scale error, and 1.1% across
# 119 pixels is several screen pixels. Measured worst across the camera sweep is
# ~6. That error is SMOOTH from row to row, so it reads as the plane being a
# hair off scale rather than as anything visible; the artifact that IS visible
# is rows disagreeing with each other, and MONOTONIC_RAMP below is what guards
# that.
MAX_SCREEN_ERR = 8.0


def parse_sine_table(repo):
    """gSineTable from src/trig.c: sin(x * pi/128) as Q8.8."""
    src = open(os.path.join(repo, "src", "trig.c"), encoding="utf-8").read()
    body = src.split("const s16 gSineTable[]", 1)[1].split("{", 1)[1].split("};", 1)[0]
    vals = [int(round(float(m) * 256)) for m in re.findall(r"Q_8_8\(([-\d.]+)\)", body)]
    if len(vals) < 256:
        raise SystemExit(f"gSineTable parsed as {len(vals)} entries, expected >= 256")
    return vals


def parse_inv_h(repo):
    """sInvH from src/rogue_mode7.c -- read it, do not recompute it, or this
    checks a second guess at the C instead of the C."""
    src = open(os.path.join(repo, "src", "rogue_mode7.c"), encoding="utf-8").read()
    body = src.split("static const u32 sInvH[DISPLAY_HEIGHT]", 1)[1]
    body = body.split("{", 1)[1].split("};", 1)[0]
    vals = [int(t) for t in re.findall(r"\d+", body)]
    if len(vals) != DISPLAY_HEIGHT:
        raise SystemExit(f"sInvH parsed as {len(vals)} entries, expected {DISPLAY_HEIGHT}")
    return vals


def build_table_c(cam, sine, inv_h, break_mode=None):
    """
    Transcription of RogueMode7_BuildScanlineTable. Python's >> on negative ints
    floors, which is the arithmetic shift GCC emits, so the operators line up.

    Returns (entries, u32_overflowed).
    """
    x, z, height, yaw, horizon = cam
    cos_yaw = sine[(yaw + 64) & 0xFF]
    sin_yaw = sine[yaw & 0xFF]

    out = []
    overflowed = False
    for row in range(DISPLAY_HEIGHT):
        h = row - horizon
        if h < 1:
            out.append(None)
            continue

        prod = height * inv_h[h]
        if prod > U32_MAX:
            overflowed = True
        lam = (prod & U32_MAX) >> 16 if break_mode != "nowrap" else prod >> 16

        if break_mode == "jitter":
            lam += (row & 1) * 3
        if lam > S16_MAX and break_mode != "park":
            out.append(None)
            continue

        pa = (lam * cos_yaw) >> 8
        pc = (-lam * sin_yaw) >> 8
        if break_mode == "pcsign":
            pc = -pc
        if break_mode == "noshift":
            pa = lam * cos_yaw

        zf = lam * PROJ_D
        centre_x = x + ((zf * sin_yaw) >> 8)
        centre_z = z + ((zf * cos_yaw) >> 8)

        if break_mode == "typec":
            bx = centre_x - ((DISPLAY_WIDTH // 2) * lam * cos_yaw) // 256
            by = centre_z - ((DISPLAY_WIDTH // 2) * -lam * sin_yaw) // 256
        else:
            bx = centre_x - (DISPLAY_WIDTH // 2) * pa
            by = centre_z - (DISPLAY_WIDTH // 2) * pc

        out.append((pa, pc, bx, by, h, lam))
    return out, overflowed


def reference(cam, row, col):
    x, z, height, yaw, horizon = cam
    h = row - horizon
    ang = yaw * 2.0 * math.pi / 256.0
    ca, sa = math.cos(ang), math.sin(ang)
    lam = (height / 256.0) / h
    zf = lam * PROJ_D
    return (x / 256.0 + zf * sa + (col - DISPLAY_WIDTH / 2) * lam * ca,
            z / 256.0 + zf * ca - (col - DISPLAY_WIDTH / 2) * lam * sa,
            lam)


CAMERAS = [
    # x, z, height (Q24.8), yaw (0-255), horizon
    (256 << 8, 0, 88 << 8, 0, 48),
    (256 << 8, 0, 88 << 8, 32, 48),
    (256 << 8, 0, 88 << 8, 200, 48),
    (1000 << 8, -700 << 8, 64 << 8, 96, 44),
    (0, 0, 120 << 8, 17, 56),
    (5000 << 8, 5000 << 8, 40 << 8, 250, 40),
    (256 << 8, 0, 127 << 8, 128, 48),
    (256 << 8, 0, 160 << 8, 64, 48),   # high enough to exercise the lambda park
    (256 << 8, 0, 200 << 8, 3, 40),    # the altitude cap MainCB2_Mode7 allows
]

COLUMNS = (0, 1, 60, 119, 120, 180, 239)


def run(repo, sine, inv_h, break_mode=None, verbose=True):
    worst = 0.0
    worst_at = None
    problems = []
    parked = 0
    compared = 0

    dupes = 0
    for cam in CAMERAS:
        table, overflowed = build_table_c(cam, sine, inv_h, break_mode)
        if overflowed:
            problems.append(f"cam h={cam[2] / 256:.0f}: height * sInvH overflowed u32")

        # Lambda must never INCREASE as you go down the screen. h grows with the
        # row, so height/h must shrink; if it ever grows, the depth ramp has
        # folded and the ground turns inside out on that row. This needs no
        # reference and no tolerance, and it is the failure that is actually
        # visible, so it is the assertion that matters most here.
        lams = [e[5] for e in table if e is not None]
        for i in range(len(lams) - 1):
            if lams[i + 1] > lams[i]:
                problems.append(
                    f"h={cam[2] / 256:.0f}: depth ramp folds, "
                    f"lambda {lams[i]} -> {lams[i + 1]}")
                break
        dupes += sum(1 for i in range(len(lams) - 1) if lams[i + 1] == lams[i])

        for row, ent in enumerate(table):
            if ent is None:
                if row - cam[4] >= 1:
                    parked += 1
                continue
            pa, pc, bx, by, h, lam = ent
            compared += 1

            if not (S16_MIN <= pa <= S16_MAX):
                problems.append(f"h={cam[2] / 256:.0f} row{row}: pa {pa} outside s16")
            if not (S16_MIN <= pc <= S16_MAX):
                problems.append(f"h={cam[2] / 256:.0f} row{row}: pc {pc} outside s16")
            if not (S32_MIN <= bx <= S32_MAX) or not (S32_MIN <= by <= S32_MAX):
                problems.append(f"h={cam[2] / 256:.0f} row{row}: bg2x/y outside s32")

            for col in COLUMNS:
                # Exactly what the hardware does: start at BG2X, add pa per
                # pixel in .8, drop the fraction.
                tex_x = (bx + col * pa) >> 8
                tex_y = (by + col * pc) >> 8
                ref_x, ref_y, lam_f = reference(cam, row, col)
                # Divide by lambda: a texel is worth 1/lambda of a screen pixel.
                err = max(abs(tex_x - ref_x), abs(tex_y - ref_y)) / lam_f
                if err > worst:
                    worst, worst_at = err, (cam, row, col, lam_f)

    ok = not problems and worst <= MAX_SCREEN_ERR

    if verbose:
        print(f"  cameras            : {len(CAMERAS)}")
        print(f"  scanlines compared : {compared}  ({parked} parked: lambda > s16)")
        print(f"  rows sharing a lambda with the row above: {dupes}"
              f"  (blockiness, not an error)")
        print(f"  worst deviation    : {worst:.3f} screen px "
              f"(tolerance {MAX_SCREEN_ERR})")
        if worst_at:
            cam, row, col, lam_f = worst_at
            print(f"    at row {row} col {col}, height {cam[2] / 256:.0f}, "
                  f"yaw {cam[3]}, lambda {lam_f:.1f} texels/px")
        for p in problems[:8]:
            print(f"  PROBLEM: {p}")
        if len(problems) > 8:
            print(f"  ... and {len(problems) - 8} more")
    return ok


# name, description, whether the check is expected to catch it
BREAKS = [
    ("park", "remove the lambda > s16 park", True),
    ("pcsign", "flip the sign of pc", True),
    ("noshift", "drop the >> 8 when forming pa", True),
    ("jitter", "perturb lambda per row, folding the depth ramp", True),
    ("typec", "derive BG2X from lambda, not the rounded pa", False),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo_pos", nargs="?", default=None,
                    help="repo root (positional; --repo also accepted)")
    ap.add_argument("--repo", default=None)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    repo = args.repo or args.repo_pos or os.environ.get("POKEDECOMP_REPO") or "."
    sine = parse_sine_table(repo)
    inv_h = parse_inv_h(repo)

    print("check_mode7_fixedpoint")
    ok = run(repo, sine, inv_h)

    if args.selftest:
        print("\n  --selftest")
        for name, desc, should_catch in BREAKS:
            passed = run(repo, sine, inv_h, break_mode=name, verbose=False)
            if should_catch:
                verdict = "ok, fires" if not passed else "SELFTEST FAILED, not caught"
                if passed:
                    ok = False
            else:
                verdict = ("ok, expected-pass (below the noise floor)"
                           if passed else "unexpected: this break IS caught now")
            print(f"    {name:8s} {desc:46s} {verdict}")

    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
