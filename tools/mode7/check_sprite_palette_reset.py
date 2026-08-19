#!/usr/bin/env python3
"""
Check that the Mode 7 scene frees the sprite palettes it is about to allocate
into, and that every palette tag it loads is one a sprite template asks for.

THE RULE

ResetSpriteData() does NOT touch sprite palettes. It resets OAM, the sprite
structs and the sprite TILE ranges, and stops -- the palette tag table and
gReservedSpritePaletteCount both survive it, still holding whatever the previous
scene left. So any screen that allocates sprite palettes must pair
ResetSpriteData() with FreeAllSpritePalettes() and an explicit reset of
gReservedSpritePaletteCount. title_screen.c does exactly that; this screen did
not, and the Unown came out wearing another scene's colours.

WHY IT NEEDS A CHECK RATHER THAN CARE

Nothing about the failure is loud. AllocSpritePalette searches from wherever the
reserved count was left, through slots still tagged by the previous scene, and
when it finds nothing free it returns 0xFF. LoadSpritePalette then quietly does
nothing at all. The build is clean, no assert fires, and the sprites simply draw
with the wrong palette -- which reads as an art bug, not a lifetime bug, so it
gets chased in the wrong place.

It also only appeared when a SECOND palette was added for the title banner. One
allocation happened to survive a polluted table; two did not. That is the
signature of a latent lifetime bug, and it is why the assertion is on the
PAIRING rather than on any particular symptom.

WHAT IS ASSERTED

  * ResetSpriteData, FreeAllSpritePalettes and a gReservedSpritePaletteCount
    assignment all appear in the scene entry function
  * FreeAllSpritePalettes comes AFTER ResetSpriteData, since freeing first and
    then resetting would leave the table repopulated
  * every tag handed to LoadSpritePalette is also named by some paletteTag, so a
    tag typo cannot silently orphan a palette
  * no more distinct sprite palette tags than the hardware has OBJ slots

--selftest removes the FreeAllSpritePalettes call and confirms this fires.

Pure Python. Takes the repo positionally or as --repo.
"""

import argparse
import os
import re
import sys

OBJ_PALETTE_SLOTS = 16
SCENE_FN = "CB2_RogueMode7Test"


def scene_body(src):
    """The text of the scene entry function."""
    i = src.index(f"void {SCENE_FN}(void)")
    depth = 0
    start = src.index("{", i)
    for j in range(start, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[start:j + 1]
    raise SystemExit(f"could not find the end of {SCENE_FN}")


def run(repo, drop_free=False, verbose=True):
    path = os.path.join(repo, "src", "rogue_mode7.c")
    src = open(path, encoding="utf-8").read()
    if drop_free:
        src = src.replace("FreeAllSpritePalettes();", "// removed by selftest")

    body = scene_body(src)
    problems = []

    reset = body.find("ResetSpriteData()")
    free = body.find("FreeAllSpritePalettes()")
    reserved = re.search(r"gReservedSpritePaletteCount\s*=", body)

    if reset < 0:
        problems.append("ResetSpriteData() is not called at all")
    if free < 0:
        problems.append("FreeAllSpritePalettes() is missing -- the palette tag "
                        "table survives ResetSpriteData and carries over")
    if not reserved:
        problems.append("gReservedSpritePaletteCount is never reset -- allocation "
                        "starts wherever the previous scene left it")
    if reset >= 0 and free >= 0 and free < reset:
        problems.append("FreeAllSpritePalettes() runs BEFORE ResetSpriteData() -- "
                        "freeing first achieves nothing")

    loaded = set(re.findall(r"LoadSpritePalette\(&?(\w+)", src))
    tags_loaded = set()
    for name in loaded:
        # Each loader is a struct SpritePalette literal or array; pull the tag
        # constant out of whichever declaration defines it.
        m = re.search(rf"{name}\s*(\[\])?\s*=\s*\{{(.+?)\}};", src, re.S)
        if m:
            tags_loaded |= set(re.findall(r"\b([A-Z][A-Z0-9_]*_TAG)\b", m.group(2)))
    tags_used = set(re.findall(r"\.paletteTag\s*=\s*(\w+)", src))

    orphaned = sorted(tags_loaded - tags_used)
    if orphaned:
        problems.append(f"palette tags loaded but named by no template: {orphaned}")
    if len(tags_loaded) > OBJ_PALETTE_SLOTS:
        problems.append(f"{len(tags_loaded)} sprite palettes for "
                        f"{OBJ_PALETTE_SLOTS} OBJ slots")

    ok = not problems
    if verbose:
        print(f"  scene entry        : {SCENE_FN}")
        print(f"  ResetSpriteData    : {'yes' if reset >= 0 else 'NO'}")
        print(f"  FreeAllSpritePalettes : "
              f"{'yes, after it' if free > reset >= 0 else 'NO'}")
        print(f"  reserved count reset  : {'yes' if reserved else 'NO'}")
        print(f"  sprite palette tags   : {sorted(tags_loaded)} "
              f"({len(tags_loaded)} of {OBJ_PALETTE_SLOTS} slots)")
        for p in problems:
            print(f"  PROBLEM: {p}")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo_pos", nargs="?", default=None)
    ap.add_argument("--repo", default=None)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    repo = args.repo or args.repo_pos or os.environ.get("POKEDECOMP_REPO") or "."

    print("check_sprite_palette_reset")
    ok = run(repo)

    if args.selftest:
        print("\n  --selftest: removing FreeAllSpritePalettes, the actual bug")
        if run(repo, drop_free=True, verbose=False):
            print("  SELFTEST FAILED: a scene that never frees its palettes passed")
            ok = False
        else:
            print("  selftest ok: the check fires when the palettes are not freed")

    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
