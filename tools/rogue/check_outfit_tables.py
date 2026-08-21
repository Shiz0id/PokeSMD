"""Guard the outfit tables: what a row says it has, it has.

THE OWNERSHIP RULE, stated once, here:

    An outfit row names every slot the player can be drawn in. A slot drawing
    the SAME graphics as another outfit's is a fallback, and a fallback is only
    ever to the DEFAULT outfit -- never between two non-default outfits, and
    never silently.

WHY THIS EXISTS. Upstream ships the second outfit with a block of aliases:

    #define OBJ_EVENT_GFX_OUTFIT_RS_BRENDAN_SURFING  OBJ_EVENT_GFX_BRENDAN_SURFING

and then lists OBJ_EVENT_GFX_OUTFIT_RS_BRENDAN_SURFING in the RS row as though
it were RS art. The row reads as complete. What actually happens is that the
player buys an outfit, walks around in it, gets on a bike, and is wearing the
green one again -- with a clean build, every check passing, and nothing in any
table that looks wrong, because to the compiler the alias and the id it expands
to are one number.

That is the failure this file exists to make loud. The fallbacks are still
allowed - there IS no Ruby/Sapphire bike sprite in this tree and inventing one
is not the porter's job - but they must be visible, and they must be fallbacks
to the default rather than two outfits quietly sharing a look.

Same shape as check_jukebox_funnels.py and check_bw_published_palette.py: name
the rule, then assert the pairing that makes it true. No curve here, no
generated data to sample.

WHAT IT ASSERTS.

  1. EVERY SLOT OF EVERY WEARABLE ROW IS FILLED. A missing designated
     initializer is zero, and graphics id 0 is OBJ_EVENT_GFX_BRENDAN_NORMAL --
     so an unfilled surfing slot does not draw nothing, it draws a boy standing
     on the water.

  2. NO TWO NON-DEFAULT OUTFITS DRAW THE SAME THING in one slot and gender.

  3. A NON-DEFAULT ROW DRAWING THE DEFAULT'S ART SAYS SO, with a `no ... art`
     comment on that line. This is the assertion upstream's alias block fails.

  4. THE DEFAULT ROW NAMES NO RAW ID for any slot that has a
     PLAYER_AVATAR_GFX_* macro. Those macros carry the IS_FRLG ternary; a raw
     Emerald id there is correct on this build and draws Brendan into FireRed.

  5. OUTFIT_COUNT FITS THE SAVE. currOutfitId is a byte and the unlock bits are
     ROUND_BITS_TO_BYTES(OUTFIT_COUNT) out of an eight-byte filler.

  6. EVERY ARRAY IS SIZED BY GENDER_COUNT. That is the whole of what keeps a
     third gender a data edit rather than a refactor.

  7. THE NEW GAME PICKER'S FUNNEL, all three parts. The picker runs before the
     naming screen and therefore before NewGameInitData, which is what makes it
     fragile in three separate ways, each silent and each different:

       - ResetOutfitData must unlock by isHidden, or the picker has one row.
       - ResetOutfitData must NOT write currOutfitId, because it runs a second
         time from NewGameInitData - after the pick - and would overwrite the
         player's choice with the default on the way into floor one.
       - CB2_RogueSlimNewGame must call it BEFORE opening the picker, or the
         picker is built from the previous save's unlock bits.
       - The input handler's new game arm must come before the on-foot test.
         There is no avatar yet, so gPlayerAvatar holds whatever the last save
         left there and the ordinary path refuses every pick.

     This is the jukebox-funnel shape: four sites, one rule, and missing any
     one of them breaks the feature at a different moment with a clean build.

  8. THE GRID ICONS ARE BORROWED ALLOCATIONS, and this is the flier rule again.
     CreateObjectGraphicsSprite takes a sheet and one of sixteen OBJ palette
     slots; DestroySprite gives back neither, because its tile-freeing branch
     is the !usingSheet one and OW_GFX_COMPRESS puts these on the other. Read
     the fields before destroying, destroy before freeing, and use the
     scanning *IfUnused variants. tools/rogue/check_flier_lifetime.py states
     the whole rule; this asserts the same pairing at a second site.

     It leaked here from the day it was ported and never showed, because the
     only caller was the scroll path and two outfits do not fill a four-row
     grid. The gender switch rebuilds on every press, which is what turns a
     dormant leak into an empty palette table in about four seconds.

  9. THE GENDER SWITCH IS NEW GAME ONLY, cycles through GENDER_COUNT rather
     than flipping between two named values, and rebuilds the GRID as well as
     the trainer pics - every cell draws its outfit at the current gender, so
     redrawing only the pics leaves a row of who the player just stopped
     being.

COMPARISONS RUN ON VALUES, NOT SPELLINGS, and that was not true when this file
was first written: the alias-shaped break below passed on its first run,
because two different tokens naming one id compared as different. Every id is
resolved through the PLAYER_AVATAR_GFX_* macros before anything is compared.

Run with --selftest to break each assertion in turn and require it to fire.
A check that has never failed is worth nothing.
"""

import argparse
import re
import sys
from pathlib import Path

TABLE = Path("src/data/outfit_tables.h")
CONSTANTS = Path("include/constants/outfits.h")
STRUCT = Path("include/data.h")
SAVE = Path("include/global.h")
EVENT_OBJECTS = Path("include/constants/event_objects.h")
OUTFIT_C = Path("src/outfit.c")
MAIN_MENU = Path("src/main_menu.c")
MENU_C = Path("src/outfit_menu.c")

AVATAR_STATES = [
    "PLAYER_AVATAR_STATE_NORMAL",
    "PLAYER_AVATAR_STATE_MACH_BIKE",
    "PLAYER_AVATAR_STATE_ACRO_BIKE",
    "PLAYER_AVATAR_STATE_SURFING",
    "PLAYER_AVATAR_STATE_UNDERWATER",
]
ANIM_SLOTS = [
    "PLAYER_AVATAR_ANIM_FIELD_MOVE",
    "PLAYER_AVATAR_ANIM_FISHING",
    "PLAYER_AVATAR_ANIM_WATERING",
    "PLAYER_AVATAR_ANIM_DECORATING",
    "PLAYER_AVATAR_ANIM_VSSEEKER",
]
GENDERS = ["MALE", "FEMALE"]

# Slots with a PLAYER_AVATAR_GFX_* macro carrying the IS_FRLG ternary.
# DECORATING deliberately has none - FRLG has no such sprite - so it is the one
# slot the default row may name a raw id for.
MACRO_BACKED = set(AVATAR_STATES) | (set(ANIM_SLOTS) - {"PLAYER_AVATAR_ANIM_DECORATING"})

FALLBACK_COMMENT = re.compile(r"//\s*no\s+\S+\s+art", re.IGNORECASE)

MACRO_DEF = (
    r"#define\s+(PLAYER_AVATAR_GFX_[A-Z0-9_]+)\s+\(IS_FRLG\s*\?\s*"
    r"([A-Za-z0-9_]+)\s*:\s*([A-Za-z0-9_]+)\)"
)


def read(repo, path):
    return (repo / path).read_text(encoding="utf-8")


def strip_comments(text):
    """Drop // and /* */ comments.

    EVERY ASSERTION ABOUT CODE RUNS THROUGH THIS, and the selftest is why. The
    GENDER_COUNT break replaced the cycling arithmetic with a two-way flip and
    still passed, because the comment directly above the line explains that it
    cycles through GENDER_COUNT - so the token was still in the text being
    searched. An assertion that a well-written comment can satisfy is checking
    the documentation, not the code.
    """
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def macro_map(repo):
    """PLAYER_AVATAR_GFX_* -> the OBJ_EVENT_GFX_* id it means on an Emerald build."""
    out = {}
    for m in re.finditer(MACRO_DEF, read(repo, EVENT_OBJECTS)):
        out[m.group(1)] = m.group(3)  # the Emerald arm of the ternary
    return out


def resolve(value, macros):
    return macros.get(value, value)


def matching_brace(text, open_index):
    depth = 0
    for j in range(open_index, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return j
    return None


def split_outfit_blocks(text):
    """{outfit name: block text} for each [OUTFIT_*] = { ... } row."""
    blocks = {}
    for m in re.finditer(r"\[(OUTFIT_[A-Z0-9_]+)\]\s*=\s*\{", text):
        name = m.group(1)
        # gOutfits[OUTFIT_COUNT] = { ... } is the array declaration, not a row.
        # Without this the whole table parses as one outfit called OUTFIT_COUNT
        # and every real row reads as a duplicate of it.
        if name == "OUTFIT_COUNT":
            continue
        end = matching_brace(text, m.end() - 1)
        if end is not None:
            blocks[name] = text[m.end() - 1 : end + 1]
    return blocks


def parse_all_slots(block):
    """{(array, gender, slot): (value, line)} for avatarGfxIds and animGfxIds."""
    out = {}
    for array in ("avatarGfxIds", "animGfxIds"):
        am = re.search(re.escape("." + array) + r"\s*=\s*\{", block)
        if not am:
            continue
        end = matching_brace(block, am.end() - 1)
        if end is None:
            continue
        gender = None
        for line in block[am.end() - 1 : end + 1].splitlines():
            gm = re.search(r"\[(MALE|FEMALE)\]\s*=\s*\{", line)
            if gm:
                gender = gm.group(1)
                continue
            sm = re.search(r"\[([A-Z0-9_]+)\]\s*=\s*([A-Za-z0-9_]+)", line)
            if sm and gender:
                out[(array, gender, sm.group(1))] = (sm.group(2), line)
    return out


def check(repo, fail):
    table = read(repo, TABLE)
    constants = read(repo, CONSTANTS)
    struct = read(repo, STRUCT)
    save = read(repo, SAVE)

    blocks = split_outfit_blocks(table)
    if not blocks:
        fail("no outfit rows found in %s" % TABLE)
        return

    m = re.search(r"#define\s+OUTFIT_COUNT\s+(\d+)", constants)
    if not m:
        fail("OUTFIT_COUNT not found in %s" % CONSTANTS)
        return
    outfit_count = int(m.group(1))

    m = re.search(r"#define\s+DEFAULT_OUTFIT\s+(OUTFIT_[A-Z0-9_]+)", constants)
    if not m:
        fail("DEFAULT_OUTFIT not found in %s" % CONSTANTS)
        return
    default = m.group(1)

    wearable = {n: b for n, b in blocks.items() if n != "OUTFIT_NONE"}
    if default not in wearable:
        fail("DEFAULT_OUTFIT %s has no row" % default)
        return

    macros = macro_map(repo)
    if not macros:
        fail("no PLAYER_AVATAR_GFX_* macros parsed from %s" % EVENT_OBJECTS)
        return

    slots = {n: parse_all_slots(b) for n, b in wearable.items()}

    # 1. every slot of every wearable row is filled
    for name, got in slots.items():
        for array, names in (("avatarGfxIds", AVATAR_STATES), ("animGfxIds", ANIM_SLOTS)):
            for gender in GENDERS:
                for slot in names:
                    if (array, gender, slot) not in got:
                        fail(
                            "%s: %s[%s][%s] is not filled - a missing initializer is "
                            "graphics id 0, which draws Brendan standing there"
                            % (name, array, gender, slot)
                        )

    # 2. no two NON-DEFAULT outfits draw the same thing in one slot
    others = sorted(n for n in wearable if n != default)
    for i, a in enumerate(others):
        for b in others[i + 1 :]:
            for key, (val, _) in slots[a].items():
                if key not in slots[b]:
                    continue
                if resolve(val, macros) == resolve(slots[b][key][0], macros):
                    fail(
                        "%s and %s both draw %s for %s[%s][%s] - two outfits the "
                        "player cannot tell apart in that state"
                        % (a, b, resolve(val, macros), key[0], key[1], key[2])
                    )

    # 3. a non-default row drawing the default's art must say so
    for name in others:
        for key, (val, line) in slots[name].items():
            if key not in slots[default]:
                continue
            if resolve(val, macros) != resolve(slots[default][key][0], macros):
                continue
            if not FALLBACK_COMMENT.search(line):
                fail(
                    "%s: %s[%s][%s] draws %s, the same as the default outfit, with "
                    "no 'no ... art' comment - a fallback that reads as finished "
                    "work is exactly the bug this check exists for"
                    % (name, key[0], key[1], key[2], resolve(val, macros))
                )

    # 4. the default row uses the IS_FRLG-carrying macros
    for key, (val, _) in slots[default].items():
        if key[2] in MACRO_BACKED and val not in macros:
            fail(
                "%s: %s[%s][%s] is %s, a raw id - the default row must use the "
                "PLAYER_AVATAR_GFX_* macros, which carry the IS_FRLG ternary"
                % (default, key[0], key[1], key[2], val)
            )

    # 5. OUTFIT_COUNT fits what SaveBlock2 set aside
    if outfit_count > 255:
        fail("OUTFIT_COUNT %d does not fit currOutfitId, which is a u8" % outfit_count)
    owned_bytes = (outfit_count + 7) // 8
    if owned_bytes > 7:
        fail(
            "OUTFIT_COUNT %d needs %d bytes of unlock bits; filler_90 has 8 and "
            "currOutfitId took one" % (outfit_count, owned_bytes)
        )
    if "u8 currOutfitId;" not in save:
        fail(
            "currOutfitId is not a plain u8 in SaveBlock2 - a bitfield truncates "
            "silently once OUTFIT_COUNT passes what it holds"
        )

    # 7. THE NEW GAME PICKER'S THREE-PART FUNNEL. Each of these fails silently
    #    and differently: an empty picker, a pick that is overwritten on the
    #    way to floor one, or a picker that refuses every choice.
    outfit_c = read(repo, OUTFIT_C)
    main_menu = read(repo, MAIN_MENU)
    menu_c = read(repo, MENU_C)

    outfit_c = strip_comments(outfit_c)
    main_menu = strip_comments(main_menu)
    menu_c = strip_comments(menu_c)

    reset = re.search(r"void ResetOutfitData\(void\)\s*\{(.*?)\n\}", outfit_c, re.S)
    if not reset:
        fail("ResetOutfitData not found in %s" % OUTFIT_C)
    else:
        if "isHidden" not in reset.group(1):
            fail(
                "ResetOutfitData does not consult isHidden - it is what decides "
                "which outfits a new game starts with, and without it the picker "
                "has one row to offer"
            )
        if "currOutfitId" in reset.group(1):
            fail(
                "ResetOutfitData writes currOutfitId. It runs AGAIN from "
                "NewGameInitData, after the picker, so it would overwrite the "
                "outfit the player just chose with the default on the way into "
                "floor one - with nothing on screen having said so"
            )

    slim = re.search(r"void CB2_RogueSlimNewGame\(void\)\s*\{(.*?)\n\}", main_menu, re.S)
    if not slim:
        fail("CB2_RogueSlimNewGame not found in %s" % MAIN_MENU)
    else:
        body = slim.group(1)
        if "ResetOutfitData()" not in body:
            fail(
                "CB2_RogueSlimNewGame does not call ResetOutfitData before opening "
                "the picker - NewGameInitData does not run until after both "
                "screens, so the picker would offer whatever the LAST save had "
                "unlocked"
            )
        if "OpenOutfitMenuForNewGame" not in body:
            fail("CB2_RogueSlimNewGame does not open the outfit picker")
        elif "ResetOutfitData()" not in body:
            pass  # already reported above; nothing to order against
        elif body.index("ResetOutfitData()") > body.index("OpenOutfitMenuForNewGame"):
            fail(
                "CB2_RogueSlimNewGame unlocks outfits AFTER opening the picker, so "
                "the picker is built from the previous save's unlock bits"
            )

    handler = re.search(
        r"static void Task_OutfitMenuHandleInput\(u8 taskId\)\s*\{(.*?)\n\}", menu_c, re.S
    )
    if not handler:
        fail("Task_OutfitMenuHandleInput not found in %s" % MENU_C)
    else:
        body = handler.group(1)
        # THE A-BUTTON ARM SPECIFICALLY, not just the token anywhere in the
        # function. The first version looked for "newGame" and found it in the
        # B-button branch, so replacing the A-button arm with `else if (FALSE)`
        # passed - the break that exists to catch exactly that went SILENT on
        # its first run. Match the construct, not a word that appears near it.
        arm = re.search(r"else\s+if\s*\(\s*sOutfitMenu->newGame\s*\)", body)
        onfoot = body.find("PLAYER_AVATAR_FLAG_ON_FOOT")
        if not arm:
            fail(
                "Task_OutfitMenuHandleInput has no `else if (sOutfitMenu->newGame)` "
                "arm on the A button - the on-foot test reads gPlayerAvatar, which "
                "on a new game holds whatever the last save left in it, so every "
                "pick would be refused"
            )
        elif onfoot != -1 and arm.start() > onfoot:
            fail(
                "the on-foot test comes before the new game arm in "
                "Task_OutfitMenuHandleInput, so the new game arm cannot be reached"
            )

    # 8. THE GRID ICONS ARE BORROWED ALLOCATIONS. CreateObjectGraphicsSprite
    #    takes a sheet and an OBJ palette slot; DestroySprite gives back
    #    neither. The gender switch rebuilds the grid on every press, so a
    #    free path that only destroys empties the palette table in seconds.
    free_fn = re.search(
        r"static void ForAllCB_FreeOutfitOverworlds\(.*?\)\s*\{(.*?)\n\}", menu_c, re.S
    )
    if not free_fn:
        fail("ForAllCB_FreeOutfitOverworlds not found in %s" % MENU_C)
    else:
        body = free_fn.group(1)
        for needed, why in (
            ("FieldEffectFreePaletteIfUnused",
             "the OBJ palette CreateObjectGraphicsSprite loaded is never given back"),
            ("FieldEffectFreeTilesIfUnused",
             "the sheet is never given back - DestroySprite frees tiles only on "
             "the !usingSheet branch, and OW_GFX_COMPRESS puts these on the other one"),
        ):
            if needed not in body:
                fail("ForAllCB_FreeOutfitOverworlds does not call %s: %s" % (needed, why))

        if "DestroySprite" in body and "FieldEffectFreePaletteIfUnused" in body:
            if body.index("DestroySprite") > body.index("FieldEffectFreePaletteIfUnused"):
                fail(
                    "ForAllCB_FreeOutfitOverworlds frees before it destroys - the "
                    "*IfUnused scans count this very sprite until DestroySprite "
                    "clears inUse, so they decline and it leaks anyway"
                )
        # The fields must be read out before the destroy zeroes the struct.
        for field in ("sheetTileStart", "oam.paletteNum"):
            m2 = re.search(re.escape(field), body)
            if not m2:
                fail("ForAllCB_FreeOutfitOverworlds never reads %s" % field)
            elif "DestroySprite" in body and m2.start() > body.index("DestroySprite"):
                fail(
                    "ForAllCB_FreeOutfitOverworlds reads %s after DestroySprite, "
                    "which zeroes the struct - and zero is a real tile start and a "
                    "real palette number, so this frees something else's" % field
                )

    # 9. THE GENDER SWITCH. New game only, cycled through GENDER_COUNT rather
    #    than flipped between two names, and it must rebuild the GRID as well
    #    as the trainer pics - every cell draws its outfit at the current
    #    gender, so pics-only leaves a row of who the player just stopped being.
    if not handler:
        pass  # already reported
    else:
        body = handler.group(1)
        gender_arm = re.search(r"if\s*\(\s*sOutfitMenu->newGame\s*&&\s*JOY_NEW\(L_BUTTON \| R_BUTTON\)\s*\)", body)
        if not gender_arm:
            fail(
                "no `if (sOutfitMenu->newGame && JOY_NEW(L_BUTTON | R_BUTTON))` arm "
                "in Task_OutfitMenuHandleInput - the gender switch must be new game "
                "only, because changing gender later would leave the trainer card, "
                "the player object and the save disagreeing about who this is"
            )
        else:
            arm = body[gender_arm.start() :]
            arm = arm[: arm.index("\n    }") + 6] if "\n    }" in arm else arm
            if "GENDER_COUNT" not in arm:
                fail(
                    "the gender switch does not cycle through GENDER_COUNT - a flip "
                    "between two named values is the one thing that makes adding a "
                    "third gender a code change rather than a data change"
                )
            if "RebuildOutfitGrid" not in arm:
                fail(
                    "the gender switch does not rebuild the grid. Every cell draws "
                    "its outfit's overworld sprite AT THE CURRENT GENDER, so "
                    "redrawing only the trainer pics leaves the grid showing the "
                    "gender the player just stopped being"
                )

    # 6. the struct is sized by GENDER_COUNT throughout
    sm = re.search(r"struct Outfit\s*\{(.*?)\n\};", struct, re.S)
    if not sm:
        fail("struct Outfit not found in %s" % STRUCT)
    else:
        body = sm.group(1)
        for field in ("prices", "trainerPics", "avatarGfxIds", "animGfxIds", "iconsRM"):
            fm = re.search(re.escape(field) + r"\s*\[([^\]]+)\]", body)
            if not fm:
                fail("struct Outfit has no %s" % field)
            elif fm.group(1).strip() != "GENDER_COUNT":
                fail(
                    "struct Outfit.%s is sized [%s], not [GENDER_COUNT] - a literal "
                    "width is what makes a third gender a refactor instead of a "
                    "data edit" % (field, fm.group(1).strip())
                )


def clone_rs_row(text):
    """Append a second non-default outfit that is a copy of the RS one.

    ASSERTION 2 HAS NOTHING TO COMPARE with one non-default outfit in the
    table, so a break that only edits the existing rows cannot exercise it -
    it would report `fires` for the wrong reason or, as it first did, report
    SILENT because the comparison loop never ran a single iteration. That is
    the vacuous-check shape: an assertion nobody has ever seen fail because
    nothing in the tree can reach it. So the mutation manufactures the second
    outfit rather than waiting for one to exist.
    """
    m = re.search(r"\[OUTFIT_UNUSUAL_RED\]\s*=\s*\{", text)
    if not m:
        return text
    end = matching_brace(text, m.end() - 1)
    if end is None:
        return text
    block = text[m.start() : end + 1]
    clone = block.replace("[OUTFIT_UNUSUAL_RED]", "[OUTFIT_TEST_CLONE]", 1)
    close = text.rindex("};")
    return text[:close] + "\n" + clone + ",\n" + text[close:]


BREAKS = [
    (
        "unfilled surfing slot",
        TABLE,
        lambda s: s.replace(
            "                [PLAYER_AVATAR_STATE_SURFING]    = PLAYER_AVATAR_GFX_MALE_SURFING,\n",
            "",
            1,
        ),
    ),
    (
        "unfilled fishing anim slot",
        TABLE,
        lambda s: s.replace(
            "                [PLAYER_AVATAR_ANIM_FISHING]    = PLAYER_AVATAR_GFX_MALE_FISHING,\n",
            "",
            1,
        ),
    ),
    (
        "fallback with its comment stripped",
        TABLE,
        lambda s: s.replace(
            "= PLAYER_AVATAR_GFX_MALE_MACH_BIKE,  // no RS art",
            "= PLAYER_AVATAR_GFX_MALE_MACH_BIKE,",
            1,
        ),
    ),
    (
        "an alias-shaped fallback, upstream's actual bug",
        TABLE,
        lambda s: s.replace(
            "[PLAYER_AVATAR_STATE_SURFING]    = PLAYER_AVATAR_GFX_MALE_SURFING,    // no RS art",
            "[PLAYER_AVATAR_STATE_SURFING]    = OBJ_EVENT_GFX_BRENDAN_SURFING,",
            1,
        ),
    ),
    (
        "two non-default outfits sharing a look",
        TABLE,
        clone_rs_row,
    ),
    (
        "default row naming a raw Emerald id",
        TABLE,
        lambda s: s.replace(
            "[PLAYER_AVATAR_STATE_NORMAL]     = PLAYER_AVATAR_GFX_MALE_NORMAL,",
            "[PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_BRENDAN_NORMAL,",
            1,
        ),
    ),
    (
        "currOutfitId narrowed to a bitfield",
        SAVE,
        lambda s: s.replace("u8 currOutfitId;", "u8 currOutfitId:4;", 1),
    ),
    (
        "OUTFIT_COUNT past what the save filler holds",
        CONSTANTS,
        lambda s: s.replace("#define OUTFIT_COUNT       3", "#define OUTFIT_COUNT       80", 1),
    ),
    (
        "ResetOutfitData choosing an outfit as well as unlocking them",
        OUTFIT_C,
        lambda s: s.replace(
            "        if (!gOutfits[i].isHidden)\n            UnlockOutfit(i);",
            "        if (!gOutfits[i].isHidden)\n            UnlockOutfit(i);\n"
            "    gSaveBlock2Ptr->currOutfitId = DEFAULT_OUTFIT;",
            1,
        ),
    ),
    (
        "ResetOutfitData unlocking everything regardless of isHidden",
        OUTFIT_C,
        lambda s: s.replace("if (!gOutfits[i].isHidden)\n            ", "", 1),
    ),
    (
        "the new game flow unlocking after it opens the picker",
        MAIN_MENU,
        lambda s: s.replace(
            "    ResetOutfitData();\n"
            "    OpenOutfitMenuForNewGame(CB2_RogueSlimNewGame_AfterOutfit, CB2_InitMainMenu);",
            "    OpenOutfitMenuForNewGame(CB2_RogueSlimNewGame_AfterOutfit, CB2_InitMainMenu);\n"
            "    ResetOutfitData();",
            1,
        ),
    ),
    (
        "the new game flow not unlocking at all",
        MAIN_MENU,
        lambda s: s.replace("    ResetOutfitData();\n    OpenOutfitMenuForNewGame", "    OpenOutfitMenuForNewGame", 1),
    ),
    (
        "the picker losing its new game arm to the on-foot test",
        MENU_C,
        lambda s: s.replace("        else if (sOutfitMenu->newGame)", "        else if (FALSE)", 1),
    ),
    (
        "the grid icons destroyed without their palette given back",
        MENU_C,
        lambda s: s.replace("        FieldEffectFreePaletteIfUnused(paletteNum);\n", "", 1),
    ),
    (
        "the grid icons destroyed without their sheet given back",
        MENU_C,
        lambda s: s.replace(
            "        if (usingSheet)\n            FieldEffectFreeTilesIfUnused(tileStart);\n", "", 1
        ),
    ),
    (
        "freeing before destroying, so the scans decline and it leaks anyway",
        MENU_C,
        lambda s: s.replace(
            "        DestroySprite(sprite);\n\n        if (usingSheet)\n"
            "            FieldEffectFreeTilesIfUnused(tileStart);\n"
            "        FieldEffectFreePaletteIfUnused(paletteNum);",
            "        if (usingSheet)\n"
            "            FieldEffectFreeTilesIfUnused(tileStart);\n"
            "        FieldEffectFreePaletteIfUnused(paletteNum);\n\n"
            "        DestroySprite(sprite);",
            1,
        ),
    ),
    (
        "reading the palette number after the struct has been zeroed",
        MENU_C,
        lambda s: s.replace(
            "        tileStart = sprite->sheetTileStart;\n"
            "        paletteNum = sprite->oam.paletteNum;\n"
            "        usingSheet = sprite->usingSheet;\n\n"
            "        DestroySprite(sprite);",
            "        tileStart = sprite->sheetTileStart;\n"
            "        usingSheet = sprite->usingSheet;\n\n"
            "        DestroySprite(sprite);\n"
            "        paletteNum = sprite->oam.paletteNum;",
            1,
        ),
    ),
    (
        "the gender switch reachable outside a new game",
        MENU_C,
        lambda s: s.replace(
            "if (sOutfitMenu->newGame && JOY_NEW(L_BUTTON | R_BUTTON))",
            "if (JOY_NEW(L_BUTTON | R_BUTTON))",
            1,
        ),
    ),
    (
        "the gender switch flipping two names instead of cycling GENDER_COUNT",
        MENU_C,
        lambda s: s.replace(
            "        if (JOY_NEW(R_BUTTON))\n"
            "            gSaveBlock2Ptr->playerGender = (gSaveBlock2Ptr->playerGender + 1) % GENDER_COUNT;\n"
            "        else\n"
            "            gSaveBlock2Ptr->playerGender = (gSaveBlock2Ptr->playerGender + GENDER_COUNT - 1) % GENDER_COUNT;",
            "        gSaveBlock2Ptr->playerGender = (gSaveBlock2Ptr->playerGender == MALE) ? FEMALE : MALE;",
            1,
        ),
    ),
    (
        "the gender switch redrawing the pics but not the grid",
        MENU_C,
        lambda s: s.replace(
            "        PlaySE(SE_SELECT);\n        RebuildOutfitGrid();\n        UpdateOutfitInfo();",
            "        PlaySE(SE_SELECT);\n        UpdateOutfitInfo();",
            1,
        ),
    ),
    (
        "struct sized by a literal instead of GENDER_COUNT",
        STRUCT,
        lambda s: s.replace(
            "u16 avatarGfxIds[GENDER_COUNT][PLAYER_AVATAR_STATE_COUNT];",
            "u16 avatarGfxIds[2][PLAYER_AVATAR_STATE_COUNT];",
            1,
        ),
    ),
]


def run_check(repo):
    failures = []
    check(repo, failures.append)
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
    # Takes the repo BOTH ways, the check_start_menu_pages.py pattern, so a
    # runner passing --repo uniformly cannot put this one on the wrong list.
    ap.add_argument("repo_pos", nargs="?", default=None, help="path to the decomp repo")
    ap.add_argument("--repo", default=None, help="path to the decomp repo")
    ap.add_argument(
        "--selftest", action="store_true", help="break each assertion and require it to fire"
    )
    args = ap.parse_args()

    repo = Path(args.repo or args.repo_pos or ".").resolve()
    if not (repo / TABLE).exists():
        print("FAIL: %s not found under %s" % (TABLE, repo))
        return 1

    if args.selftest:
        return selftest(repo)

    failures = run_check(repo)
    if failures:
        for f in failures:
            print("FAIL: " + f)
        return 1
    print("check_outfit_tables: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
