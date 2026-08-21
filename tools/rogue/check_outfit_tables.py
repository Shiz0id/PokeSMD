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

  9. THE PICKER IS TWO AXES ON TWO CONTROLS, new game only. L and R step the
     LOOK and SELECT steps the IDENTITY, and each arm writes exactly one field
     through one setter.

     CROSSING THEM IS THE SILENT ONE. An arm that steps the other axis builds
     cleanly, moves a value, prints a word, and leaves the save holding a
     player who is not the one on the screen - so the arms are matched to
     their setters here, and the setters to their fields.

     Only the LOOK arm rebuilds the grid, because only the look indexes art:
     every cell draws its outfit at the current look, so redrawing the pics
     alone leaves a row of who the player just stopped being. The identity arm
     redraws text and nothing else.

     AND BOTH HINTS ARE PRINTED, FROM BOTH PRINT PATHS. The look announces
     itself - the sprites change - but identity has no visible consumer on this
     screen at all, so an unprinted hint makes SELECT a button that does
     nothing, on the only screen where that choice exists. The setup path and
     the update path print this window through different functions; drawing it
     in one means the hint arrives after the first cursor move.

     This replaced a curated list of five (identity, look) pairs. The list gave
     the choice of sprite to the androgynous stop and to no other, which is the
     sentence the two fields exist to avoid, and it made the second axis
     visible only in the two stops that spelled both halves with a slash.

 10. IDENTITY AND LOOK STAY SEPARATE, and this is the assertion that matters
     most in this file.

     enum PlayerGender is WHO the player is and drives text. enum PlayerLook is
     WHAT THEY LOOK LIKE and indexes every art table. They are two fields
     because an androgynous player may present as any of the three looks - the
     alternative is telling somebody their identity picks their sprite.

     enum Gender must stay TWO WIDE. Its MALE/FEMALE tokens are also what
     struct Trainer's one-bit `gender` field holds, so a third value there
     truncates silently: a trainer who is neither becomes male, with a clean
     build and nothing to see.

     And no table indexed by a look may be sized GENDER_COUNT or by a literal,
     because a look of PLAYER_LOOK_ANDRO then reads one past the end - not a
     crash, just whatever the next symbol happens to be, read as a palette
     pointer or a colour.

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
GLOBAL_CONSTANTS = Path("include/constants/global.h")

# Every file that indexes a table by the player's LOOK. Each one is a place
# where a two-wide table would read out of bounds the moment the player picks
# the third look.
LOOK_INDEXED = [
    Path("src/mail.c"),
    Path("src/battle_transition.c"),
    Path("src/trainer_card.c"),
    Path("src/cable_car.c"),
    Path("src/field_player_avatar.c"),
]

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
GENDERS = ["MALE", "FEMALE", "PLAYER_LOOK_ANDRO"]

# Slots with a PLAYER_AVATAR_GFX_* macro carrying the IS_FRLG ternary.
# DECORATING deliberately has none - FRLG has no such sprite - so it is the one
# slot the default row may name a raw id for.
MACRO_BACKED = set(AVATAR_STATES) | (set(ANIM_SLOTS) - {"PLAYER_AVATAR_ANIM_DECORATING"})

FALLBACK_COMMENT = re.compile(r"//\s*no\s+\S+\s+art", re.IGNORECASE)

MACRO_DEF = (
    r"#define\s+(PLAYER_AVATAR_GFX_[A-Z0-9_]+)\s+\(IS_FRLG\s*\?\s*"
    r"([A-Za-z0-9_]+)\s*:\s*([A-Za-z0-9_]+)\)"
)


PLAIN_MACRO_DEF = r"#define\s+(PLAYER_AVATAR_GFX_[A-Z0-9_]+)\s+([A-Za-z0-9_]+)\s*$"


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
    text = read(repo, EVENT_OBJECTS)
    out = {}
    for m in re.finditer(MACRO_DEF, text):
        out[m.group(1)] = m.group(3)  # the Emerald arm of the ternary

    # THE PLAIN FORM TOO. The androgynous look points straight at Kris ids with
    # no ternary, because there is no FireRed Kris for one to choose between -
    # so a macro without an IS_FRLG is correct there, and a resolver that only
    # knew the ternary form reported every one of them as a raw id.
    for m in re.finditer(PLAIN_MACRO_DEF, text, re.M):
        out.setdefault(m.group(1), m.group(2))
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
            gm = re.search(r"\[(MALE|FEMALE|PLAYER_LOOK_MASC|PLAYER_LOOK_FEM|PLAYER_LOOK_ANDRO)\]\s*=\s*\{", line)
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

    # 2. NO TWO NON-DEFAULT OUTFITS DRAW THE SAME THING IN ONE SLOT - unless
    #    both of them say, on the line, that they have no art for it.
    #
    #    THE EXEMPTION IS THE WHOLE POINT OF THE ASSERTION, not a hole in it.
    #    What this catches is two outfits that READ AS FINISHED and are the same
    #    picture. Two rows that both carry a `no ... art` comment are not that:
    #    they are two acknowledged holes that happen to fall back on the same
    #    thing, which is what a fallback IS. The Kanto and RS outfits are
    #    identical at the third look for exactly this reason - neither game ever
    #    drew a Kris - and the alternative to allowing it is either inventing art
    #    or making one of them silently borrow somebody else's person.
    #
    #    A duplicate where only ONE side is marked still fires, and that is the
    #    case worth having: it means one row believes it has art and the other
    #    knows it does not, so one of the two is wrong about itself.
    others = sorted(n for n in wearable if n != default)
    for i, a in enumerate(others):
        for b in others[i + 1 :]:
            for key, (val, line_a) in slots[a].items():
                if key not in slots[b]:
                    continue
                other_val, line_b = slots[b][key]
                if resolve(val, macros) != resolve(other_val, macros):
                    continue
                if FALLBACK_COMMENT.search(line_a) and FALLBACK_COMMENT.search(line_b):
                    continue
                fail(
                    "%s and %s both draw %s for %s[%s][%s] - two outfits the "
                    "player cannot tell apart in that state%s"
                    % (
                        a, b, resolve(val, macros), key[0], key[1], key[2],
                        ", and only one of them admits it is a fallback"
                        if FALLBACK_COMMENT.search(line_a) or FALLBACK_COMMENT.search(line_b)
                        else "",
                    )
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

    # 3b. NO DESCRIPTION MAY WRAP. The name prints at y=0 and the description
    #     at y=16, both FONT_NORMAL, so a \n puts a second line at y=32 - the
    #     row the new game picker draws its two gender hints on. The text does
    #     not clip or reflow; it overprints, and only on a new game, so it is
    #     invisible from the outfit menu the rest of the game opens.
    for name, block in wearable.items():
        m2 = re.search(r"\.desc\s*=\s*COMPOUND_STRING\(\"(.*?)\"\)", block, re.S)
        if not m2:
            fail("%s has no description" % name)
        elif "\\n" in m2.group(1):
            fail(
                "%s: the description wraps onto a second line, which lands at y=32 "
                "and overprints the new game picker's gender hints. One line only "
                "- WIN_INFO_HINT_Y in src/outfit_menu.c is that row" % name
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

    # 9. THE PICKER'S TWO AXES. L/R steps the look, SELECT steps the identity,
    #    each arm writes one field through one setter, and only the look arm
    #    rebuilds the grid. Crossing the two is silent: it builds, it moves a
    #    value, and the save stops agreeing with the sprite on the screen.
    if not handler:
        pass  # already reported
    else:
        body = handler.group(1)
        arms = {}
        for axis, cond, buttons in (
            ("look", r"if\s*\(\s*sOutfitMenu->newGame\s*&&\s*JOY_NEW\(L_BUTTON \| R_BUTTON\)\s*\)", "L_BUTTON | R_BUTTON"),
            ("identity", r"if\s*\(\s*sOutfitMenu->newGame\s*&&\s*JOY_NEW\(SELECT_BUTTON\)\s*\)", "SELECT_BUTTON"),
        ):
            m2 = re.search(cond, body)
            if not m2:
                fail(
                    "no `if (sOutfitMenu->newGame && JOY_NEW(%s))` arm in "
                    "Task_OutfitMenuHandleInput - the %s axis must have its own "
                    "control and must be new game only, because changing it later "
                    "would leave the trainer card, the player object and the save "
                    "disagreeing about who this is" % (buttons, axis)
                )
                continue
            open_brace = body.find("{", m2.end())
            close = matching_brace(body, open_brace) if open_brace != -1 else None
            if close is None:
                fail("the %s arm of Task_OutfitMenuHandleInput has no body" % axis)
                continue
            arms[axis] = body[open_brace : close + 1]

        for axis, mine, theirs in (
            ("look", "StepPlayerLook", "StepPlayerIdentity"),
            ("identity", "StepPlayerIdentity", "StepPlayerLook"),
        ):
            arm = arms.get(axis)
            if arm is None:
                continue
            if mine + "(" not in arm:
                fail(
                    "the %s arm does not call %s - stepping an axis has to go "
                    "through its setter, which is the only place that knows which "
                    "save field the axis lives in and what it wraps on"
                    % (axis, mine)
                )
            if theirs in arm:
                fail(
                    "the %s arm calls %s. THE TWO ARMS ARE CROSSED: this builds "
                    "cleanly, the picker looks like it works, and the player ends "
                    "up as somebody the sprite on the screen is not"
                    % (axis, theirs)
                )
            if "gSaveBlock2Ptr->playerGender" in arm:
                fail(
                    "the %s arm writes gSaveBlock2Ptr->playerGender* directly "
                    "instead of stepping through its setter" % axis
                )

        if "look" in arms and "RebuildOutfitGrid" not in arms["look"]:
            fail(
                "the look arm does not rebuild the grid. Every cell draws its "
                "outfit's overworld sprite AT THE CURRENT LOOK, so redrawing only "
                "the trainer pics leaves the grid showing the look the player just "
                "stopped wearing"
            )

    # 9b. THE SETTERS. One axis, one field, and the two-wide GENDER_COUNT is
    #     never what either wraps on - that count sizes trainer tables, and
    #     wrapping a look on it makes PLAYER_LOOK_ANDRO unreachable from the
    #     picker while every table that holds it stays perfectly correct.
    for fn, field, other_field, count, other_count in (
        ("StepPlayerLook", r"->playerGender\b", r"->playerGenderIdentity\b",
         "PLAYER_LOOK_COUNT", "PLAYER_GENDER_COUNT"),
        ("StepPlayerIdentity", r"->playerGenderIdentity\b", r"->playerGender\b",
         "PLAYER_GENDER_COUNT", "PLAYER_LOOK_COUNT"),
    ):
        m2 = re.search(r"void " + fn + r"\(s32 delta\)\s*\{(.*?)\n\}", outfit_c, re.S)
        if not m2:
            fail("%s not found in %s" % (fn, OUTFIT_C))
            continue
        fn_body = m2.group(1)
        if not re.search(field, fn_body):
            fail("%s never writes %s" % (fn, field.replace(r"\b", "")))
        if re.search(other_field, fn_body):
            fail(
                "%s touches %s - the two axes are separate fields precisely so one "
                "control cannot move the other, and a setter that writes both is "
                "that separation undone in the one place nothing else guards"
                % (fn, other_field.replace(r"\b", ""))
            )
        if count not in fn_body:
            fail("%s does not wrap on %s" % (fn, count))
        if other_count in fn_body:
            fail("%s wraps on %s, the other axis's count" % (fn, other_count))
        if re.search(r"\bGENDER_COUNT\b", fn_body):
            fail(
                "%s wraps on GENDER_COUNT, which is TWO WIDE and sizes trainer "
                "tables. The third value is then unreachable from the picker while "
                "every table that holds it stays correct - nothing fails, the "
                "option simply is not there" % fn
            )

    # 9c. EVERY VALUE OF EITHER AXIS HAS A LABEL. A missing row is a NULL that
    #     the hint prints straight through.
    for table_name, count, members in (
        ("gPlayerLookNames", "PLAYER_LOOK_COUNT",
         ("PLAYER_LOOK_MASC", "PLAYER_LOOK_FEM", "PLAYER_LOOK_ANDRO")),
        ("gPlayerIdentityNames", "PLAYER_GENDER_COUNT",
         ("GENDER_MASCULINE", "GENDER_FEMININE", "GENDER_ANDROGYNOUS")),
    ):
        m2 = re.search(
            r"const u8 \*const " + table_name + r"\[([A-Za-z0-9_]+)\]\s*=\s*\{(.*?)\n\};",
            outfit_c,
            re.S,
        )
        if not m2:
            fail("%s not found in %s" % (table_name, OUTFIT_C))
            continue
        if m2.group(1) != count:
            fail(
                "%s is sized [%s], not [%s] - the picker steps over the count, so a "
                "table shorter than it reads past the end and prints whatever "
                "follows as a string" % (table_name, m2.group(1), count)
            )
        for member in members:
            if member not in m2.group(2):
                fail(
                    "%s has no row for %s, so that stop prints a NULL label"
                    % (table_name, member)
                )

    # 9d. BOTH HINTS, FROM BOTH PRINT PATHS.
    hint = re.search(r"static void PrintGenderHints\(void\)\s*\{(.*?)\n\}", menu_c, re.S)
    if not hint:
        fail("PrintGenderHints not found in %s" % MENU_C)
    else:
        hint_body = hint.group(1)
        for axis, string, getter in (
            ("look", "sText_OutfitLookHint", "GetPlayerLookName"),
            ("identity", "sText_OutfitIdentityHint", "GetPlayerIdentityName"),
        ):
            if string not in hint_body or getter not in hint_body:
                fail(
                    "PrintGenderHints does not print the %s hint. The look at least "
                    "announces itself by redrawing the sprites; identity changes "
                    "NOTHING on this screen, so an unprinted hint makes SELECT a "
                    "button that appears to do nothing on the only screen in the "
                    "game where that choice is offered" % axis
                )

    for fn in ("SetupOutfitMenu_PrintStr", "UpdateOutfitInfo"):
        m2 = re.search(
            r"(?:static )?(?:inline )?void " + fn + r"\(void\)\s*\{(.*?)\n\}", menu_c, re.S
        )
        if not m2:
            fail("%s not found in %s" % (fn, MENU_C))
        elif "PrintGenderHints()" not in m2.group(1):
            fail(
                "%s does not print the gender hints. The setup path and the update "
                "path draw this window through different functions, so printing "
                "them in one means the hint appears after the first cursor move - "
                "after the moment it existed to explain" % fn
            )

    m2 = re.search(r"sText_OutfitLookHint\[\] = _\(\"([^\"]*)\"\)", menu_c)
    if not m2 or "{L_BUTTON}{R_BUTTON}" not in m2.group(1):
        fail("sText_OutfitLookHint does not name the L and R buttons")
    m2 = re.search(r"sText_OutfitIdentityHint\[\] = _\(\"([^\"]*)\"\)", menu_c)
    if not m2 or "{SELECT_BUTTON}" not in m2.group(1):
        fail("sText_OutfitIdentityHint does not name the SELECT button")

    # 10. identity and look stay separate, and enum Gender stays two wide
    consts = read(repo, GLOBAL_CONSTANTS)

    gender_enum = re.search(r"enum Gender\s*\{(.*?)\}", consts, re.S)
    if not gender_enum:
        fail("enum Gender not found in %s" % GLOBAL_CONSTANTS)
    elif "GENDER_ANDROGYNOUS" in gender_enum.group(1) or gender_enum.group(1).count(",") > 3:
        fail(
            "enum Gender has grown past MALE/FEMALE/GENDER_COUNT. Its tokens are "
            "also what struct Trainer's one-bit gender field holds, so a third "
            "value truncates silently there - which is why enum PlayerGender "
            "exists separately"
        )

    for name, members in (
        ("PlayerGender", ("GENDER_MASCULINE", "GENDER_FEMININE", "GENDER_ANDROGYNOUS")),
        ("PlayerLook", ("PLAYER_LOOK_MASC", "PLAYER_LOOK_FEM", "PLAYER_LOOK_ANDRO")),
    ):
        m2 = re.search(r"enum " + name + r"\s*\{(.*?)\}", consts, re.S)
        if not m2:
            fail("enum %s not found in %s" % (name, GLOBAL_CONSTANTS))
            continue
        for member in members:
            if member not in m2.group(1):
                fail("enum %s has no %s" % (name, member))

    # The look enum has to keep the values the save already holds.
    look_enum = re.search(r"enum PlayerLook\s*\{(.*?)\}", consts, re.S)
    if look_enum:
        body = look_enum.group(1)
        if "PLAYER_LOOK_MASC = MALE" not in body or "PLAYER_LOOK_FEM = FEMALE" not in body:
            fail(
                "enum PlayerLook does not pin its first two members to MALE and "
                "FEMALE. It is stored in gSaveBlock2Ptr->playerGender, so every "
                "existing save and every [MALE]/[FEMALE] designator in a player art "
                "table depends on those values not moving"
            )

    # Identity must have a reader that is not the picker itself, or it is a
    # field the game stores and never consults.
    identity_readers = []
    for path in sorted((repo / "src").glob("*.c")):
        if path.name in ("outfit.c", "outfit_menu.c"):
            continue
        if "playerGenderIdentity" in path.read_text(encoding="utf-8"):
            identity_readers.append(path.name)
    if not identity_readers:
        fail(
            "nothing outside the picker reads playerGenderIdentity - a stored "
            "identity that no screen consults is a save field pretending to be a "
            "feature"
        )

    # NO LOOK-INDEXED TABLE MAY BE TWO WIDE.
    #
    # FIND THE INDEXERS FIRST, then check only those declarations. The first
    # version of this flagged every [2] in the files it scanned - stickers,
    # monSpecies, hikerCoords, gFieldEffectArguments - none of which has
    # anything to do with the player's look. A check that fires on things that
    # are obviously fine trains everyone to ignore it, so it asks the narrower
    # question: which arrays are indexed by playerGender, and are THOSE wide
    # enough.
    for path in LOOK_INDEXED:
        text = strip_comments(read(repo, path))
        indexed = set(re.findall(r"(\w+)\s*\[[^\]]*playerGender[^\]]*\]", text))
        for name in sorted(indexed):
            decl = re.search(
                re.escape(name) + r"\s*\[\s*([A-Za-z0-9_]+)\s*\]", text
            )
            if not decl:
                continue
            width = decl.group(1)
            if width in ("GENDER_COUNT", "2"):
                fail(
                    "%s: %s is indexed by playerGender but declared [%s] - a look "
                    "of PLAYER_LOOK_ANDRO reads one past the end. Size it "
                    "[PLAYER_LOOK_COUNT]" % (path, name, width)
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
            elif fm.group(1).strip() != "PLAYER_LOOK_COUNT":
                fail(
                    "struct Outfit.%s is sized [%s], not [PLAYER_LOOK_COUNT] - this "
                    "table is indexed by the player's LOOK, and a literal or a "
                    "two-wide count reads out of bounds at PLAYER_LOOK_ANDRO"
                    % (field, fm.group(1).strip())
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
        "a duplicate only one side admits is a fallback",
        TABLE,
        lambda s: s.replace(
            "                [PLAYER_AVATAR_STATE_NORMAL]     = PLAYER_AVATAR_GFX_ANDRO_NORMAL,     // no FRLG art",
            "                [PLAYER_AVATAR_STATE_NORMAL]     = PLAYER_AVATAR_GFX_ANDRO_NORMAL,",
            1,
        ),
    ),
    (
        "a description that wraps onto the picker's hint row",
        TABLE,
        lambda s: s.replace(
            'COMPOUND_STRING("From FIRE RED and LEAF GREEN.")',
            'COMPOUND_STRING("From FIRE RED and\\nLEAF GREEN.")',
            1,
        ),
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
        # REGEX, NOT THE LITERAL LINE. This break NO-OPed the moment a third
        # outfit changed the count and its alignment - the mutation matched
        # nothing and the runner said so, which is the only reason anyone
        # noticed. A break pinned to a value that is expected to change is a
        # break with an expiry date on it.
        lambda s: re.sub(r"#define OUTFIT_COUNT(\s+)\d+", r"#define OUTFIT_COUNT\g<1>80", s, count=1),
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
        "the look switch reachable outside a new game",
        MENU_C,
        lambda s: s.replace(
            "if (sOutfitMenu->newGame && JOY_NEW(L_BUTTON | R_BUTTON))",
            "if (JOY_NEW(L_BUTTON | R_BUTTON))",
            1,
        ),
    ),
    (
        "the look arm redrawing the pics but not the grid",
        MENU_C,
        lambda s: s.replace(
            "        PlaySE(SE_SELECT);\n        RebuildOutfitGrid();\n        UpdateOutfitInfo();",
            "        PlaySE(SE_SELECT);\n        UpdateOutfitInfo();",
            1,
        ),
    ),
    (
        "struct sized by a literal instead of the look count",
        STRUCT,
        lambda s: s.replace(
            "u16 avatarGfxIds[PLAYER_LOOK_COUNT][PLAYER_AVATAR_STATE_COUNT];",
            "u16 avatarGfxIds[2][PLAYER_AVATAR_STATE_COUNT];",
            1,
        ),
    ),
    (
        "a third value pushed into enum Gender, where trainers keep theirs",
        GLOBAL_CONSTANTS,
        lambda s: s.replace(
            "enum Gender\n{\n    MALE,\n    FEMALE,\n    GENDER_COUNT,\n};",
            "enum Gender\n{\n    MALE,\n    FEMALE,\n    GENDER_ANDROGYNOUS,\n    GENDER_COUNT,\n};",
            1,
        ),
    ),
    (
        "the look enum unpinned from the values the save holds",
        GLOBAL_CONSTANTS,
        lambda s: s.replace("    PLAYER_LOOK_MASC = MALE,", "    PLAYER_LOOK_MASC,", 1),
    ),
    (
        "a look-indexed table left two wide",
        Path("src/mail.c"),
        lambda s: s.replace(
            "static const u16 sBgColors[PLAYER_LOOK_COUNT][2] = {",
            "static const u16 sBgColors[GENDER_COUNT][2] = {",
            1,
        ),
    ),
    (
        "identity stored but read by nothing outside the picker",
        Path("src/start_menu.c"),
        lambda s: s.replace("gSaveBlock2Ptr->playerGenderIdentity", "gSaveBlock2Ptr->playerGender", 1),
    ),
    (
        "the picker setting the field itself instead of stepping the axis",
        MENU_C,
        lambda s: s.replace(
            "StepPlayerLook(JOY_NEW(R_BUTTON) ? 1 : -1);",
            "gSaveBlock2Ptr->playerGender = PLAYER_LOOK_FEM;",
            1,
        ),
    ),
    (
        "the look arm stepping the identity, so L and R change nothing visible",
        MENU_C,
        lambda s: s.replace(
            "StepPlayerLook(JOY_NEW(R_BUTTON) ? 1 : -1);",
            "StepPlayerIdentity(JOY_NEW(R_BUTTON) ? 1 : -1);",
            1,
        ),
    ),
    (
        "the identity arm stepping the look, so SELECT redresses the player",
        MENU_C,
        lambda s: s.replace("StepPlayerIdentity(1);", "StepPlayerLook(1);", 1),
    ),
    (
        "SELECT losing its arm, so identity is unreachable and defaults forever",
        MENU_C,
        lambda s: s.replace(
            "if (sOutfitMenu->newGame && JOY_NEW(SELECT_BUTTON))", "if (FALSE)", 1
        ),
    ),
    (
        "the identity switch reachable outside a new game",
        MENU_C,
        lambda s: s.replace(
            "if (sOutfitMenu->newGame && JOY_NEW(SELECT_BUTTON))",
            "if (JOY_NEW(SELECT_BUTTON))",
            1,
        ),
    ),
    (
        "the identity hint never printed, so SELECT looks like a dead button",
        MENU_C,
        lambda s: s.replace(
            "    end = StringCopy(str, sText_OutfitIdentityHint);",
            "    end = StringCopy(str, sText_OutfitLookHint);",
            1,
        ),
    ),
    (
        "the hints drawn from the update path only",
        MENU_C,
        lambda s: s.replace(
            "    PrintGenderHints();\n"
            "    PrintTexts(WIN_INFO, FONT_NORMAL, 2, 0, COLORID_NORMAL, gOutfits[outfitId].name);",
            "    PrintTexts(WIN_INFO, FONT_NORMAL, 2, 0, COLORID_NORMAL, gOutfits[outfitId].name);",
            1,
        ),
    ),
    (
        "StepPlayerLook writing the identity field",
        OUTFIT_C,
        lambda s: s.replace(
            "gSaveBlock2Ptr->playerGender = (look",
            "gSaveBlock2Ptr->playerGenderIdentity = (look",
            1,
        ),
    ),
    (
        "StepPlayerIdentity writing the look field",
        OUTFIT_C,
        lambda s: s.replace(
            "gSaveBlock2Ptr->playerGenderIdentity = (identity",
            "gSaveBlock2Ptr->playerGender = (identity",
            1,
        ),
    ),
    (
        "the look wrapping on the two-wide count, so ENBY is unreachable",
        OUTFIT_C,
        lambda s: s.replace(
            "(look + PLAYER_LOOK_COUNT + delta) % PLAYER_LOOK_COUNT",
            "(look + GENDER_COUNT + delta) % GENDER_COUNT",
            1,
        ),
    ),
    (
        "a look with no label, printed as a NULL",
        OUTFIT_C,
        lambda s: s.replace('    [PLAYER_LOOK_ANDRO] = COMPOUND_STRING("ENBY"),\n', "", 1),
    ),
    (
        "an identity with no label, printed as a NULL",
        OUTFIT_C,
        lambda s: s.replace('    [GENDER_ANDROGYNOUS] = COMPOUND_STRING("ENBY"),\n', "", 1),
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
