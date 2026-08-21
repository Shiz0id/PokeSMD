#!/usr/bin/env python3
"""Build an acro bike sheet for a character who only has a mach bike one.

THE PROBLEM, once. sAnimTable_AcroBike defines 40 anim ids and indexes frames
up to 26 - wheelies, bunny hops and moving wheelies on top of the ordinary
nine riding frames. A character imported with only a mach_bike.png has NINE
frames. Point the acro state at it and StartSpriteAnim reads SpriteFrameImage
entries past the end of the pic table: a bogus pointer AND a bogus length
handed to a copy. That is the hang OUTFIT_UNUSUAL_RED shipped.

The alternative everyone reaches for first is falling back to the DEFAULT
outfit's acro bike, which builds, runs, and turns the player into Brendan the
moment they get on the acro bike - on floor one, because this project grants
both bikes immediately.

WHAT THIS DOES INSTEAD. Vanilla's acro sheet already contains every pose, drawn
for Brendan. Two measurements make it reusable:

  1. SEVEN OF NINE acro frames 0-8 are pixel-identical to Brendan's mach frames.
     RSE's two bikes are essentially the same overworld art, so the level-riding
     half needs no work at all - the target's own mach frames ARE those frames.

  2. THE HEAD IN A WHEELIE IS A TRANSLATION, NOT A REDRAW. Correlating
     Brendan's level head against each acro frame finds an offset at which the
     pixels match 100% for the south frames and 73-91% elsewhere (the west ones
     are partly occluded by the leaning arm). So the head can be replaced with
     the target character's own head, moved by that offset.

So each output frame is: Brendan's acro frame with every colour index remapped
to the target's palette by ROLE, then his head erased and the target's real
head pasted at the measured offset. The result is the character's genuine face
and cap on a correctly-posed body in their own colours.

WHAT IT IS NOT. The BODY silhouette is still Brendan's - Dawn's skirt and
Lucas's jacket are not reproduced, only recoloured. At 32x32, under a helmet,
mid-wheelie, that reads fine; do not mistake it for a redraw.

A PLAIN PALETTE SWAP DOES NOT WORK, and it is the obvious thing to try. The
three artists share no index convention: Brendan's skin is at indices 1-3,
Dawn's at 8-9, and the palettes agree on 2 of 16 slots. Applying Dawn's palette
to Brendan's pixels paints his face with her clothing pinks. The remap below is
by role, index to index, which is the step that makes it work.

THE ROLE MAPS ARE HAND-WRITTEN AND THAT IS DELIBERATE. An optimiser that
minimises per-pixel colour distance against the target's own mach frames scores
better and looks worse - it collapses several source indices onto one target,
flattening the shading, because silhouette mismatch dominates the objective.
Sixteen colours is small enough to assign by eye and check by looking.

Usage:
    python3 tools/rogue/compose_acro_bike.py . --character dawn
    python3 tools/rogue/compose_acro_bike.py . --all --preview out.png

Writes graphics/object_events/pics/people/<character>/acro_bike.png.
"""

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip install pillow")

FW = FH = 32
ACRO_FRAMES = 27

SOURCE = Path("graphics/object_events/pics/people/brendan/acro_bike.png")
SOURCE_MACH = Path("graphics/object_events/pics/people/brendan/mach_bike.png")
PEOPLE = Path("graphics/object_events/pics/people")
PALETTES = Path("graphics/object_events/palettes")

# Which direction each acro frame depicts. Derived from sAnimTable_AcroBike:
# every anim in it names a direction, so every frame it references inherits one.
# Regenerate with the snippet in the module docstring of check_player_sprite_anims.
FRAME_DIR = {
    0: "S", 1: "N", 2: "W", 3: "S", 4: "S", 5: "N", 6: "N", 7: "W", 8: "W",
    9: "S", 10: "S", 11: "S", 12: "S", 13: "N", 14: "N", 15: "N", 16: "N",
    17: "W", 18: "W", 19: "W", 20: "W", 21: "S", 22: "S", 23: "N", 24: "N",
    25: "W", 26: "W",
}
# The mach frame whose head is used as the donor for each direction. These are
# the three STILL frames - a still head is unoccluded, which is what makes the
# correlation below trustworthy.
STILL_FRAME = {"S": 0, "N": 1, "W": 2}

# brendan index -> target index, by what the colour is FOR.
# Brendan: 1-3 skin, 4 dark shadow, 5-8 navy clothing (light->dark),
#          9 off-white cap, 10-11 green cap accent, 12-13 reds, 14 white, 15 black.
ROLE_MAPS = {
    "dawn": {
        0: 0,                      # transparent
        1: 9, 2: 9, 3: 8,          # skin -> Dawn's skin (9 light, 8 mid)
        4: 3,                      # shadow -> dark brown
        # Same trap as the FRLG row below, milder: index 5 also carries the
        # shadow under the bike on the moving-wheelie frames, so sending it to
        # her 6 (56,96,136) drew a teal streak. Her 5 (56,64,80) is the darkest
        # non-black she has, which is what that pixel is for.
        5: 5, 6: 5, 7: 15, 8: 15,
        9: 12,                     # cap white -> light grey (her beanie)
        10: 2, 11: 1,              # green accent -> her pinks
        12: 10, 13: 11,            # reds -> her orange / red
        14: 14, 15: 15,            # white, black outline
    },
    "lucas": {
        0: 0,
        1: 12, 2: 12, 3: 10,       # skin -> Lucas's skin
        4: 2,                      # shadow -> dark brown
        5: 5, 6: 5, 7: 3, 8: 3,    # navy -> his navy / dark slate
        9: 14,                     # cap white -> near white
        10: 1, 11: 4,              # green accent -> his beret reds
        12: 1, 13: 4,
        14: 14, 15: 15,
    },
    # RED AND GREEN SHARE ONE PALETTE - both OBJ_EVENT_PAL_TAG_PLAYER_RED and
    # _GREEN resolve to gObjectEventPal_PlayerFrlg - so they share one map too.
    # Convenient here: FRLG indices 2, 3 and 4 are byte-identical to Brendan's,
    # so the skin ramp needs no thought at all. Only his index 1, a lighter
    # highlight FRLG does not carry, has to fold onto 2.
    "red": {
        0: 0,
        1: 2, 2: 2, 3: 3, 4: 4,    # skin - 2/3/4 are the SAME colours as his
        # NAVY RAMP -> NAVY AND BLACK, and NOT to FRLG's index 7. That is
        # (115,164,197), a LIGHT blue, and Brendan's index 5 is the darkest
        # thing under the bike on the moving-wheelie frames: routing it there
        # drew a glowing pale streak below the wheels. Match by luminance -
        # his 5 (57,74,123) is near-identical to FRLG's 6 (57,57,123).
        5: 6, 6: 6, 7: 15, 8: 15,
        9: 9,                      # cap white -> near white
        10: 10, 11: 6,             # green accent -> grey / navy
        12: 11, 13: 12,            # reds -> FRLG orange / red
        14: 9, 15: 15,
    },
}
ROLE_MAPS["green"] = ROLE_MAPS["red"]

# Where each character's art lives, when it is not simply <name>/mach_bike.png
# and palettes/<name>.pal. FRLG breaks both conventions: the directory for
# Green is `leaf`, the sheet is <who>_bike.png, and the palette is shared.
CHARACTERS = {
    "dawn":  dict(folder="dawn",  sheet="mach_bike.png",     palette="dawn.pal",        out="acro_bike.png"),
    "lucas": dict(folder="lucas", sheet="mach_bike.png",     palette="lucas.pal",       out="acro_bike.png"),
    "red":   dict(folder="red",   sheet="red_bike.png",      palette="player_frlg.pal", out="red_acro_bike.png"),
    "green": dict(folder="leaf",  sheet="green_bike.png",    palette="player_frlg.pal", out="green_acro_bike.png"),
}


def read_jasc(path):
    lines = path.read_text().split("\n")
    count = int(lines[2])
    flat = []
    for i in range(count):
        flat += [int(x) for x in lines[3 + i].split()[:3]]
    return flat + [0] * (768 - len(flat))


def frame_pixels(image, index):
    box = (index * FW, 0, (index + 1) * FW, FH)
    return list(image.crop(box).get_flattened_data())


def head_box(pixels):
    """Bounding box of the top nine occupied rows - the head and shoulders."""
    rows = [r for r in range(FH) if any(pixels[r * FW + x] for x in range(FW))]
    if not rows:
        return None
    band = rows[:9]
    pts = [(r, x) for r in band for x in range(FW) if pixels[r * FW + x]]
    return (min(r for r, _ in pts), max(r for r, _ in pts),
            min(x for _, x in pts), max(x for _, x in pts))


def best_offset(donor, box, target, span=8):
    """Offset at which the donor head best matches the target frame.

    Exhaustive over a +/-8 window, which is cheap and removes any question of a
    local minimum. Returns (match_fraction, dx, dy).
    """
    r0, r1, x0, x1 = box
    best = None
    for dy in range(-span, span + 1):
        for dx in range(-span, span + 1):
            hit = total = 0
            for r in range(r0, r1 + 1):
                for x in range(x0, x1 + 1):
                    v = donor[r * FW + x]
                    if not v:
                        continue
                    total += 1
                    rr, xx = r + dy, x + dx
                    if 0 <= rr < FH and 0 <= xx < FW and target[rr * FW + xx] == v:
                        hit += 1
            if total and (best is None or hit / total > best[0]):
                best = (hit / total, dx, dy)
    return best


def compose(repo, character, verbose=True):
    cfg = CHARACTERS[character]
    acro = Image.open(repo / SOURCE)
    bmach = Image.open(repo / SOURCE_MACH)
    tmach_path = repo / PEOPLE / cfg["folder"] / cfg["sheet"]
    if not tmach_path.exists():
        sys.exit("%s has no %s to build from" % (character, cfg["sheet"]))
    tmach = Image.open(tmach_path)
    palette = read_jasc(repo / PALETTES / cfg["palette"])
    role = ROLE_MAPS[character]

    out = [0] * (FW * ACRO_FRAMES * FH)
    report = []
    for fi in range(ACRO_FRAMES):
        direction = FRAME_DIR[fi]
        still = STILL_FRAME[direction]
        src = frame_pixels(acro, fi)
        donor = frame_pixels(bmach, still)
        head = frame_pixels(tmach, still)

        box = head_box(donor)
        match, dx, dy = best_offset(donor, box, src)

        # THE PASTE SPANS THE TARGET'S HEAD, NOT THE DONOR'S BOX, and getting
        # this wrong CUTS THE HAT OFF. The offset is found by correlating
        # Brendan's head, so his bounding box is the right thing to search
        # with - but it is the wrong thing to copy with. Red's and Green's caps
        # occupy rows 11-19 where his occupy 9-17, and reach one column further
        # left; iterating his box silently drops their brim and the left edge
        # of the hat. Both heads live in the same coordinate frame - they are
        # still frames of the same pose at the same size - so the SAME offset
        # applies to a different, larger rectangle.
        tbox = head_box(head)
        cur = [role.get(v, 0) for v in src]
        r0, r1, x0, x1 = box
        t_r0, t_r1, t_x0, t_x1 = tbox
        # Erase Brendan's head where it sits in THIS frame, then paste theirs
        # into the same place. Two passes, because the two heads are different
        # shapes and a single pass would leave his outline poking out.
        #
        # ERASE THE DONOR'S PIXELS, PLUS EVERYTHING ABOVE THEM IN THE SAME
        # COLUMNS. The exact-pixel erase alone leaves Brendan's cap SPIKE
        # standing: on the moving-wheelie frames his head is drawn taller than
        # the still head the box was measured from, so the tip pokes out above
        # the box and no offset covers it. Recoloured by the role map it became
        # a pale speckle floating over the rider's hat.
        #
        # Clearing the columns from row 0 down to the box's bottom is safe
        # because that band is above the shoulders by construction - it is where
        # a head is. Two things it is NOT: dilating the erase sideways, which
        # eats the neck and detaches the head; and clearing the whole bounding
        # box, which reaches the handlebars on the leaning frames and holes the
        # bike. Both were tried and looked worse.
        # The cleared band spans the UNION of the two heads, for the same reason
        # the paste does - clearing only his columns leaves his outline beside
        # a wider hat.
        span_lo, span_hi = min(x0, t_x0) + dx, max(x1, t_x1) + dx
        cut_to = max(r1, t_r1) + dy
        for rr in range(0, min(cut_to + 1, FH)):
            for xx in range(max(0, span_lo), min(span_hi + 1, FW)):
                cur[rr * FW + xx] = 0
        for r in range(r0, r1 + 1):
            for x in range(x0, x1 + 1):
                if not donor[r * FW + x]:
                    continue
                rr, xx = r + dy, x + dx
                if 0 <= rr < FH and 0 <= xx < FW:
                    cur[rr * FW + xx] = 0
        for r in range(t_r0, t_r1 + 1):
            for x in range(t_x0, t_x1 + 1):
                v = head[r * FW + x]
                if not v:
                    continue
                rr, xx = r + dy, x + dx
                if 0 <= rr < FH and 0 <= xx < FW:
                    cur[rr * FW + xx] = v

        for r in range(FH):
            base = r * FW * ACRO_FRAMES + fi * FW
            out[base:base + FW] = cur[r * FW:(r + 1) * FW]
        report.append((fi, direction, dx, dy, match))

    sheet = Image.new("P", (FW * ACRO_FRAMES, FH))
    sheet.putdata(out)
    sheet.putpalette(palette)
    dest = repo / PEOPLE / cfg["folder"] / cfg["out"]
    sheet.save(dest)

    if verbose:
        weak = [r for r in report if r[4] < 0.70]
        print("%s: wrote %s (%d frames)" % (character, dest, ACRO_FRAMES))
        print("   head offsets, worst match %.0f%%"
              % (100 * min(r[4] for r in report)))
        if weak:
            print("   LOW-CONFIDENCE FRAMES (head correlation under 70%%): %s"
                  % ", ".join("f%d" % r[0] for r in weak))
            print("   Look at those before trusting the sheet - a bad offset")
            print("   puts the head somewhere the body is not.")
    return report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo", nargs="?", default=".")
    ap.add_argument("--character", action="append", default=[])
    ap.add_argument("--all", action="store_true")
    args = ap.parse_args()

    repo = Path(args.repo)
    who = list(CHARACTERS) if args.all else args.character
    if not who:
        sys.exit("name --character, or pass --all")
    for c in who:
        if c not in CHARACTERS:
            sys.exit("no role map for %s - add one to ROLE_MAPS, by eye" % c)
        compose(repo, c)


if __name__ == "__main__":
    main()
