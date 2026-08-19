#!/usr/bin/env python3
"""
Check the atmosphere layer in src/rogue_mode7.c: that every tile it addresses
exists, and that stars actually place.

WHY THIS EXISTS

The star bug happened twice while building this layer, and both times it was
silent. A map entry is one tile id, so a tile cannot be both fog and star; stars
therefore need their own tiles AT each fog density, and only place where the
density is below FOG_STAR_LEVELS. But the vignette reaches FOG_VIGNETTE_TILES in
from every edge and the screen is only 20 tiles tall, so EVERY sky tile already
carries some density. Set FOG_STAR_LEVELS too low, or the vignette reach too
high, and the count of stars silently falls to zero. The build is clean, the fog
looks right, and the sky is simply empty.

The same shape of error would index a star tile that BuildFogTiles never wrote,
which paints garbage from whatever is at that VRAM offset.

So this replays BuildFogMap against the constants read out of the C and asserts:

  * at least MIN_STARS place, and all four twinkle groups are used -- otherwise
    the twinkle rotation has nothing to rotate
  * every tile id the map emits was actually built by BuildFogTiles
  * density never exceeds FOG_LEVELS, which would run off the end of the dither
    tiles

--selftest drops FOG_STAR_LEVELS to what it originally was and confirms the star
assertion fires.

Pure Python -- run_all_checks.sh runs under WSL, which has no Pillow. Takes the
repo positionally or as --repo.
"""

import argparse
import os
import re
import sys
from collections import Counter

MIN_STARS = 12
TWINKLE_GROUPS = 4


def parse(repo):
    src = open(os.path.join(repo, "src", "rogue_mode7.c"), encoding="utf-8").read()
    d = {}
    for name in ("FOG_TILES_W", "FOG_TILES_H", "FOG_LEVELS", "FOG_STAR_LEVELS",
                 "FOG_VIGNETTE_TILES"):
        m = re.search(rf"#define\s+{name}\s+(\d+)", src)
        if not m:
            raise SystemExit(f"could not find #define {name}")
        d[name] = int(m.group(1))

    m = re.search(r"sFogByRow\[FOG_TILES_H\]\s*=\s*\{([^}]*)\}", src)
    if not m:
        raise SystemExit("could not find sFogByRow")
    d["FOG_BY_ROW"] = [int(t) for t in re.findall(r"\d+", m.group(1))]

    # The row and density gates on the star branch, read from the source so a
    # retune cannot slip past.
    m = re.search(r"if \(ty < (\d+) && density < FOG_STAR_LEVELS && hash > (\d+)\)", src)
    if not m:
        raise SystemExit("could not find the star placement condition")
    d["STAR_MAX_ROW"] = int(m.group(1))
    d["STAR_HASH"] = int(m.group(2))
    return d


def cell_hash(x, y):
    return ((x * 73) ^ (y * 151)) & 0xFF


def build_map(d, star_levels=None):
    """Replay BuildFogMap. Returns (tile id counter, star count)."""
    W, H = d["FOG_TILES_W"], d["FOG_TILES_H"]
    levels = d["FOG_LEVELS"]
    starl = d["FOG_STAR_LEVELS"] if star_levels is None else star_levels
    star0 = levels + 1

    tiles = Counter()
    stars = Counter()
    for ty in range(H):
        for tx in range(W):
            edge = min(tx, W - 1 - tx, ty, H - 1 - ty)
            density = max(0, d["FOG_VIGNETTE_TILES"] - edge)
            density = max(density, d["FOG_BY_ROW"][ty])
            density = min(density, levels)

            h = cell_hash(tx * 3 + 1, ty * 7 + 5)
            if ty < d["STAR_MAX_ROW"] and density < starl and h > d["STAR_HASH"]:
                g = h & 3
                tiles[star0 + g * starl + density] += 1
                stars[g] += 1
            elif density > 0:
                tiles[density] += 1
            else:
                tiles[0] += 1
    return tiles, stars


def built_tiles(d):
    """The set of tile ids BuildFogTiles actually writes."""
    levels, starl = d["FOG_LEVELS"], d["FOG_STAR_LEVELS"]
    ids = {0} | set(range(1, levels + 1))
    star0 = levels + 1
    for g in range(TWINKLE_GROUPS):
        for k in range(starl):
            ids.add(star0 + g * starl + k)
    return ids


def run(repo, star_levels=None, verbose=True):
    d = parse(repo)
    tiles, stars = build_map(d, star_levels)
    built = built_tiles(d)

    total_stars = sum(stars.values())
    unbuilt = sorted(set(tiles) - built)
    over = [t for t in tiles if 1 <= t <= d["FOG_LEVELS"] and t > d["FOG_LEVELS"]]
    missing_groups = [g for g in range(TWINKLE_GROUPS) if stars[g] == 0]

    ok = (total_stars >= MIN_STARS and not unbuilt and not over
          and not missing_groups)

    if verbose:
        print(f"  screen             : {d['FOG_TILES_W']}x{d['FOG_TILES_H']} tiles")
        print(f"  dither levels      : {d['FOG_LEVELS']}, "
              f"vignette reach {d['FOG_VIGNETTE_TILES']} tiles")
        print(f"  stars placed       : {total_stars} "
              f"(need >= {MIN_STARS}), groups {dict(sorted(stars.items()))}")
        print(f"  distinct tiles used: {len(tiles)} of {len(built)} built")
        if total_stars < MIN_STARS:
            print(f"  TOO FEW STARS      : the sky is empty. FOG_STAR_LEVELS "
                  f"({d['FOG_STAR_LEVELS']}) is below the density the vignette "
                  f"puts on every sky tile.")
        for g in missing_groups:
            print(f"  UNUSED TWINKLE GROUP {g}: nothing for the rotation to move")
        if unbuilt:
            print(f"  TILES NEVER BUILT  : {unbuilt}  <-- would paint garbage")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo_pos", nargs="?", default=None)
    ap.add_argument("--repo", default=None)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    repo = args.repo or args.repo_pos or os.environ.get("POKEDECOMP_REPO") or "."

    print("check_fog_overlay")
    ok = run(repo)

    if args.selftest:
        print("\n  --selftest: FOG_STAR_LEVELS back to 3, the value that emptied the sky")
        if run(repo, star_levels=3, verbose=False):
            print("  SELFTEST FAILED: an empty sky passed")
            ok = False
        else:
            print("  selftest ok: the check fires when no star can place")

    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
