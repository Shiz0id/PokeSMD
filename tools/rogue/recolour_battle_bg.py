"""Re-light an existing battle background with another theme's colours.

THE ENGINE IS BUILT FOR THIS. gBattleEnvironmentInfo separates art from palette
- ENVIRONMENT_BACKGROUND names a tileset and tilemap, .palette names the colours
- and vanilla already leans on it: the whole Elite Four is ONE Stadium
background with five palettes. So a new backdrop can cost a .pal file and a
table row, with no new tiles at all.

WHAT IT MAPS ON IS LUMINANCE, and that is the whole method. A background's
palette is a shading ramp: some colours are lit, some are shadow, and which is
which is what makes the art read as three-dimensional. Re-hue every entry and
that survives; re-order the ramp and the image falls apart into noise. So each
source colour is placed at its own position within the source's luminance range
and read back out of the target ramp at the same position.

THE TARGET RAMP COMES FROM A TILESET, not from taste. A theme's floor and wall
metatiles already name the palette they draw with, so the colours a player
associates with that dungeon are sitting in the tileset. Sampling those means
the battle backdrop and the floor agree without anyone matching them by eye.

Run:
    python3 tools/rogue/recolour_battle_bg.py SRC_DIR TILESET_DIR PAL_N OUT_DIR
                                              [--indices 1-7] [--write]

SRC_DIR is a battle_environment folder; its palette.pal is the source ramp.
TILESET_DIR/palettes/PAL_N.pal supplies the target. --indices picks which
entries of the target are the ramp, since a tileset palette usually carries
special-purpose colours alongside it.
"""
import argparse
import sys
from pathlib import Path

COLOURS = 16
BANKS = 3


def read_pal(path):
    out = []
    for line in path.read_text(errors="ignore").splitlines()[3:]:
        parts = line.split()
        if len(parts) == 3:
            out.append(tuple(int(v) for v in parts))
    return out


def write_pal(path, colours):
    n = len(colours)
    lines = ["JASC-PAL", "0100", str(n)]
    lines += [f"{r} {g} {b}" for r, g, b in colours]
    path.write_text("\r\n".join(lines) + "\r\n", newline="")


def lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def sample(ramp, t):
    """Read the ramp at 0..1, interpolating between neighbours."""
    if len(ramp) == 1:
        return ramp[0]
    pos = max(0.0, min(1.0, t)) * (len(ramp) - 1)
    i = int(pos)
    if i >= len(ramp) - 1:
        return ramp[-1]
    f = pos - i
    a, b = ramp[i], ramp[i + 1]
    return tuple(round(a[k] + (b[k] - a[k]) * f) for k in range(3))


def parse_indices(spec):
    out = []
    for part in spec.split(","):
        if "-" in part:
            lo, hi = part.split("-")
            out.extend(range(int(lo), int(hi) + 1))
        else:
            out.append(int(part))
    return out


def deepen(src_pal, factor, tint, weight):
    """Sink a palette toward depth without flattening what it distinguishes.

    RAMP-MAPPING IS THE WRONG TOOL FOR A MULTI-BANK BACKGROUND. Every bank would
    be read onto the same target ramp, so a background whose three banks
    deliberately hold different MATERIALS - water, creatures, weed - would come
    back as three copies of one ramp, undoing the separation that made it worth
    banking. This scales and tints instead, so every colour moves the same way
    and the differences between them survive.

    `factor` multiplies, `tint` and `weight` blend toward a deep colour. The two
    together are what reads as depth: darkening alone gives muddy grey, and
    tinting alone gives a blue photograph of a bright scene.

    Index 0 of each bank is the transparency key and is left alone.
    """
    out = list(src_pal)
    for bank in range(BANKS):
        base = bank * COLOURS
        for i in range(1, COLOURS):
            if base + i >= len(src_pal):
                break
            c = src_pal[base + i]
            if c == (0, 0, 0):
                continue
            lit = [min(255, round(v * factor)) for v in c]
            out[base + i] = tuple(
                round(lit[k] * (1 - weight) + tint[k] * weight) for k in range(3))
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("tileset", nargs="?")
    ap.add_argument("pal_n", type=int, nargs="?")
    ap.add_argument("out")
    ap.add_argument("--indices", default="1-7")
    ap.add_argument("--deepen", help="FACTOR,R,G,B,WEIGHT - transform instead "
                                     "of remapping, for multi-bank art")
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args(argv)

    if args.deepen:
        parts = [float(x) for x in args.deepen.split(",")]
        factor, tint, weight = parts[0], tuple(parts[1:4]), parts[4]
        src_pal = read_pal(Path(args.src) / "palette.pal")
        out = deepen(src_pal, factor, tint, weight)
        lit = [c for c in src_pal if c != (0, 0, 0)]
        new = [c for c in out if c != (0, 0, 0)]
        print(f"deepened {len(lit)} colours: mean luminance "
              f"{sum(lum(c) for c in lit) / max(len(lit), 1):.0f} -> "
              f"{sum(lum(c) for c in new) / max(len(new), 1):.0f}")
        dest = Path(args.out)
        if args.write:
            dest.mkdir(parents=True, exist_ok=True)
            write_pal(dest / "palette.pal", out)
            print(f"wrote {dest / 'palette.pal'}")
        else:
            print("not written - pass --write")
        return

    src_pal = read_pal(Path(args.src) / "palette.pal")
    target_all = read_pal(Path(args.tileset) / "palettes" / f"{args.pal_n:02d}.pal")
    ramp = [target_all[i] for i in parse_indices(args.indices)]
    ramp.sort(key=lum)
    print(f"target ramp, {len(ramp)} colours, luminance "
          f"{lum(ramp[0]):.0f} to {lum(ramp[-1]):.0f}")

    out = list(src_pal)
    for bank in range(BANKS):
        base = bank * COLOURS
        if base >= len(src_pal):
            break
        # Index 0 of every bank is the transparency key and is never art. Leave
        # it exactly as it was; recolouring it would change nothing on screen
        # and would make the .pal disagree with the source it came from.
        art = [(i, src_pal[base + i]) for i in range(1, COLOURS)
               if base + i < len(src_pal) and src_pal[base + i] != (0, 0, 0)]
        if not art:
            continue
        lo = min(lum(c) for _, c in art)
        hi = max(lum(c) for _, c in art)
        span = (hi - lo) or 1.0
        for i, c in art:
            out[base + i] = sample(ramp, (lum(c) - lo) / span)
        print(f"  bank {bank}: {len(art)} colours, source luminance "
              f"{lo:.0f} to {hi:.0f}")

    dest = Path(args.out)
    if args.write:
        dest.mkdir(parents=True, exist_ok=True)
        write_pal(dest / "palette.pal", out)
        print(f"wrote {dest / 'palette.pal'} ({len(out)} entries)")
    else:
        print("not written - pass --write")


if __name__ == "__main__":
    main(sys.argv[1:])
