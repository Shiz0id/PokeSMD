"""Does the published BW palette still outlive the battle screen, and only that?

THIS CHECKS A MEMORY LIFETIME, not generated data, so it asserts a PAIRING of
sites rather than a measurement. The rule it guards:

    sBwPublishedPal holds the palette that goes with the animation PIXELS in
    gMonSpritesGfxPtr. Those pixels outlive the battle screen, so the palette
    must too. It is therefore invalidated by ClearBwState -- which runs at the
    top of every RogueBwAnim_OnLoadSprite, the same pass that overwrites the
    pixels -- and deliberately NOT by RogueBwAnim_Free.

That asymmetry is the whole fix and it looks exactly like an oversight. The
caught-mon Pokedex page draws from the battler's sprite GFX because there is not
enough heap to build a new sprite (see Pokedex_CreateCaughtMonSprite), and
CloseMainBattleScreen calls RogueBwAnim_Free BEFORE that page runs. Anyone
"tidying up" by clearing the published species in Free returns NULL at the one
moment the answer is needed, and every caught mon with an animation is scrambled
again -- no container in the set shares the stock sprite's palette slot order.

It builds clean either way and no other check sees it, which is why this one
exists.

--selftest breaks each half of the pairing in turn and requires the check to
fire on both; a check that has never failed is worth nothing.

Usage:  python3 tools/rogue/check_bw_published_palette.py [--repo PATH] [--selftest]
"""
import argparse
import re
import sys
from pathlib import Path

ANIM = "src/rogue_bw_anim.c"
DEX = "src/pokedex.c"

STATE = "sBwPublishedSpecies"
GETTER = "RogueBwAnim_GetPublishedPalette"


def read(repo, rel):
    return (repo / rel).read_text(encoding="utf-8", errors="replace")


def function_body(text, name):
    """Extract one C function body by brace matching from its definition."""
    m = re.search(r"^[A-Za-z_][\w \t\*]*\b" + re.escape(name) + r"\s*\([^;{]*\)\s*\{",
                  text, re.M)
    if not m:
        raise SystemExit("FAIL: could not find the definition of %s()" % name)
    depth = 0
    start = m.end() - 1
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    raise SystemExit("FAIL: unbalanced braces reading %s()" % name)


def strip_comments(body):
    """The rule is stated in prose in these functions; only CODE counts."""
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    return body


def assigns_state(body):
    return re.search(re.escape(STATE) + r"\s*\[[^\]]*\]\s*=", strip_comments(body)) is not None


def evaluate(anim_src, dex_src):
    """Return a list of violated rules, most important first."""
    bad = []

    clear = function_body(anim_src, "ClearBwState")
    free = function_body(anim_src, "RogueBwAnim_Free")
    load = function_body(anim_src, "RogueBwAnim_OnLoadSprite")

    if not assigns_state(clear):
        bad.append(
            "ClearBwState no longer invalidates %s. That is what makes the "
            "value safe to keep across teardown; without it a battler can hand "
            "out a palette for a mon whose pixels are gone." % STATE)

    if assigns_state(free):
        bad.append(
            "RogueBwAnim_Free assigns %s. It must NOT: CloseMainBattleScreen "
            "calls Free and then shows the caught-mon dex page, which needs the "
            "palette for pixels that are still in gMonSpritesGfxPtr." % STATE)

    if not assigns_state(load):
        bad.append(
            "RogueBwAnim_OnLoadSprite never records %s, so the getter can only "
            "ever return NULL and the fix is inert." % STATE)

    if GETTER not in strip_comments(dex_src):
        bad.append(
            "%s does not consult %s, so the caught-mon page is back on the "
            "stock palette over animation pixels." % (DEX, GETTER))

    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    repo = Path(args.repo)

    anim_src = read(repo, ANIM)
    dex_src = read(repo, DEX)

    if args.selftest:
        if evaluate(anim_src, dex_src):
            print("SELFTEST FAIL: the real tree already violates the rule")
            return 1

        # Break half one: drop the invalidation from ClearBwState.
        clear = function_body(anim_src, "ClearBwState")
        broken = anim_src.replace(
            clear, re.sub(re.escape(STATE) + r"\s*\[[^\]]*\]\s*=[^;]*;", "", clear))
        if not evaluate(broken, dex_src):
            print("SELFTEST FAIL: removing the ClearBwState invalidation did "
                  "not fire the check")
            return 1

        # Break half two: "tidy up" by clearing in Free, which is the regression
        # this whole check exists to catch.
        free = function_body(anim_src, "RogueBwAnim_Free")
        tidied = free.replace("sBwAnim[battler] = NULL;",
                              "sBwAnim[battler] = NULL;\n        %s[battler] = SPECIES_NONE;" % STATE)
        if tidied == free:
            print("SELFTEST FAIL: could not construct the 'tidied Free' case; "
                  "the anchor line moved and this selftest is now vacuous")
            return 1
        if not evaluate(anim_src.replace(free, tidied), dex_src):
            print("SELFTEST FAIL: clearing the state in RogueBwAnim_Free did "
                  "not fire the check")
            return 1

        print("SELFTEST PASS: both halves of the pairing fire when broken.")
        return 0

    bad = evaluate(anim_src, dex_src)
    if bad:
        print("FAIL: the published-palette lifetime rule is broken.")
        for b in bad:
            print("      - " + b)
        return 1

    print("PASS: %s is invalidated by ClearBwState, is not touched by "
          "RogueBwAnim_Free, is recorded on load, and %s consults the getter."
          % (STATE, DEX))
    return 0


if __name__ == "__main__":
    sys.exit(main())
