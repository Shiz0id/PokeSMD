#!/usr/bin/env python3
"""A sprite the player can WEAR must answer every anim the player can PLAY.

THE RULE, once: an outfit names a graphics id per avatar state, and the ENGINE
decides which anim ids it will ask that sprite for. Those two facts live in
different files and nothing pairs them. When they disagree, StartSpriteAnim
indexes past the end of the sprite's anim table, reads whatever the linker put
next as a `union AnimCmd *`, and the sprite engine walks it - which is a HANG,
not a wrong picture. The game builds, the outfit menu draws, the player takes
one step and the console stops.

That is not a hypothetical. OUTFIT_UNUSUAL_RED wore OBJ_EVENT_GFX_LINK_RS_BRENDAN
- the Ruby/Sapphire NPC sprite, on sAnimTable_Standard, which defines ids 0-19
and stops. The player on foot plays ANIM_RUN_* (20-23) when running and
ANIM_SPIN_* (24-27) on a warp. It froze on the first step, and every check in
this tree passed the whole time, because every table it looked at was correct.

WHAT THE REQUIRED SET IS, AND WHY IT COMES FROM THE DEFAULT ROW. The anims the
player can play are a property of the STATE, not of the sprite: on foot it is
running and spinning, on the acro bike it is wheelies and hops (ids up to 39),
surfing has its own. Hard-coding that table here would mean maintaining a second
copy of something the engine already states. So the reference is the DEFAULT
outfit's sprite for the same slot - it is the vanilla player's own art, is what
the engine was written against, and is correct by construction. Every other
outfit's sprite for that slot must define a SUPERSET of the ids it defines.

That is why the FRLG sprites pass while looking nothing like the Hoenn ones.
Kanto's surf sprite is 16x32 where Hoenn's is 32x32, and its underwater sprite
is on sAnimTable_Surfing where Hoenn's is on sAnimTable_Standard - different
shape, superset of anims, no way for the engine to ask it for something it does
not have. A check that demanded the shapes MATCH would have flagged twelve of
those and trained everyone to ignore it.

AND A FLOOR FOR THE ON-FOOT STATE, checked directly rather than by comparison:
the NORMAL slot must define ANIM_RUN_* and ANIM_SPIN_*. If the default row ever
lost them, the comparison above would go quiet at exactly the moment it mattered
most - every outfit would still agree with a default that had stopped being
right. The two halves fail differently and both are here.

FRAMES ARE THE OTHER HALF OF THE SAME BUG. An anim that exists but points at a
frame the sheet does not have reads a SpriteFrameImage past the end of the pic
table - a bogus pointer AND a bogus length handed to a copy. The RS sprite had
this too: nine walking frames, while ANIM_RUN_* reads frames 9-17. The running
sheets had been sitting in object_event_graphics.h since the import, referenced
by nothing. So the frame count of every pic table is compared against the
highest frame its own anims ask for.

Run with --selftest to break each assertion in turn and require it to fire.
A check that has never failed is worth nothing.
"""

import argparse
import re
import struct
import sys
from pathlib import Path

TABLE = Path("src/data/outfit_tables.h")
EVENT_OBJECTS = Path("include/constants/event_objects.h")
MOVEMENT = Path("include/constants/event_object_movement.h")
ANIMS = Path("src/data/object_events/object_event_anims.h")
INFOS = Path("src/data/object_events/object_event_graphics_info.h")
POINTERS = Path("src/data/object_events/object_event_graphics_info_pointers.h")
GRAPHICS = Path("src/data/object_events/object_event_graphics.h")
PIC_TABLES = Path("src/data/object_events/object_event_pic_tables.h")

DEFAULT_ROW = "OUTFIT_USUAL_GREEN"

# The on-foot floor. These are the two families sAnimTable_Standard does not
# have, which is precisely what an NPC sheet is.
ON_FOOT_REQUIRED = [
    "ANIM_RUN_SOUTH", "ANIM_RUN_NORTH", "ANIM_RUN_WEST", "ANIM_RUN_EAST",
    "ANIM_SPIN_SOUTH", "ANIM_SPIN_NORTH", "ANIM_SPIN_WEST", "ANIM_SPIN_EAST",
]


def read(repo, path):
    return (repo / path).read_text(encoding="utf-8")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def matching_brace(text, open_index):
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return None


def anim_ids(repo):
    """ANIM_* -> integer, resolving the `(ANIM_STD_COUNT + n)` forms."""
    raw = {}
    for m in re.finditer(r"^#define\s+(ANIM_[A-Z0-9_]+)\s+(.+)$", read(repo, MOVEMENT), re.M):
        raw.setdefault(m.group(1), m.group(2).split("//")[0].strip())

    out = {}

    def resolve(name, depth=0):
        if name in out:
            return out[name]
        if name not in raw or depth > 8:
            return None
        expr = raw[name]
        if expr.isdigit():
            out[name] = int(expr)
            return out[name]
        m = re.match(r"\(?\s*([A-Z0-9_]+)\s*\+\s*(\d+)\s*\)?$", expr)
        if m:
            base = resolve(m.group(1), depth + 1)
            if base is not None:
                out[name] = base + int(m.group(2))
                return out[name]
        if re.match(r"^[A-Z0-9_]+$", expr):
            base = resolve(expr, depth + 1)
            if base is not None:
                out[name] = base
                return out[name]
        return None

    for name in raw:
        resolve(name)
    return out


def anim_frames(repo):
    """sAnim_* -> the set of image frames it asks for."""
    text = strip_comments(read(repo, ANIMS))
    out = {}
    for m in re.finditer(r"(sAnim_\w+)\[\]\s*=\s*\{(.*?)\n\};", text, re.S):
        frames = {int(f) for f in re.findall(r"ANIMCMD_FRAME\(\s*(\d+)", m.group(2))}
        out[m.group(1)] = frames
    return out


def anim_tables(repo, ids):
    """sAnimTable_* -> (set of anim ids defined, set of frames referenced)."""
    text = strip_comments(read(repo, ANIMS))
    frames_of = anim_frames(repo)
    out = {}
    for m in re.finditer(r"(sAnimTable_\w+)\[\]\s*=\s*\{(.*?)\n\};", text, re.S):
        body = m.group(2)
        covered = set()
        frames = set()
        entries = [e.strip() for e in body.split(",") if e.strip()]
        designated = re.findall(r"\[([A-Z0-9_]+)\]\s*=\s*([^,]+)", body)
        if designated:
            for key, value in designated:
                if key in ids:
                    covered.add(ids[key])
                for sym in re.findall(r"sAnim_\w+", value):
                    frames |= frames_of.get(sym, set())
        else:
            # A plain list: the ids it answers are 0..n-1.
            for i, entry in enumerate(entries):
                covered.add(i)
                for sym in re.findall(r"sAnim_\w+", entry):
                    frames |= frames_of.get(sym, set())
        out[m.group(1)] = (covered, frames)
    return out


def graphics_infos(repo):
    """gObjectEventGraphicsInfo_* -> {field: value}."""
    text = strip_comments(read(repo, INFOS))
    out = {}
    for name, body in re.findall(r"gObjectEventGraphicsInfo_(\w+)\s*=\s*\{(.*?)\n\};", text, re.S):
        d = {}
        for field in ("anims", "images", "width", "height", "size"):
            m = re.search(r"\." + field + r"\s*=\s*([^,\n]+)", body)
            if m:
                d[field] = m.group(1).strip()
        out[name] = d
    return out


def info_pointers(repo):
    """OBJ_EVENT_GFX_* -> info symbol."""
    text = strip_comments(read(repo, POINTERS))
    return dict(
        re.findall(r"\[(OBJ_EVENT_GFX_[A-Z0-9_]+)\]\s*=\s*&gObjectEventGraphicsInfo_(\w+)", text)
    )


def avatar_macros(repo):
    """PLAYER_AVATAR_GFX_* -> the id it means on an EMERALD build."""
    text = read(repo, EVENT_OBJECTS)
    out = {}
    for m in re.finditer(
        r"#define\s+(PLAYER_AVATAR_GFX_[A-Z0-9_]+)\s+\(IS_FRLG\s*\?\s*"
        r"([A-Za-z0-9_]+)\s*:\s*([A-Za-z0-9_]+)\)",
        text,
    ):
        out[m.group(1)] = m.group(3)
    for m in re.finditer(r"#define\s+(PLAYER_AVATAR_GFX_[A-Z0-9_]+)\s+([A-Za-z0-9_]+)\s*$", text, re.M):
        out.setdefault(m.group(1), m.group(2))
    return out


def sheet_frames(repo, symbol, frame_bytes):
    """How many frames the pixel data behind a pic symbol holds, or None."""
    text = read(repo, GRAPHICS)
    m = re.search(r"\b" + re.escape(symbol) + r"\[\]\s*=\s*(INCGFX_\w+|INCBIN_\w+)\((.*?)\);", text, re.S)
    if not m:
        return None
    args = re.findall(r'"([^"]+)"', m.group(2))
    total = 0
    for arg in args:
        if arg.startswith("-") or arg.startswith("."):
            continue  # a converter flag or an extension, not a file
        p = repo / arg
        if not p.exists():
            return None
        if arg.endswith(".png"):
            head = p.read_bytes()[:24]
            if head[12:16] != b"IHDR":
                return None
            w, h = struct.unpack(">II", head[16:24])
            total += (w * h) // 2  # 4bpp
        else:
            total += p.stat().st_size
    if total == 0 or frame_bytes == 0:
        return None
    return total // frame_bytes


def pic_table_frames(repo, symbol):
    """How many frames a sPicTable_* can serve, or None if it cannot be read."""
    text = strip_comments(read(repo, PIC_TABLES))
    m = re.search(r"\b" + re.escape(symbol) + r"\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        return None
    body = m.group(1)
    # Three shapes exist. Explicit frames and single-frame tiles are counted;
    # an ascending sheet is measured from the pixel data behind it.
    explicit = re.findall(r"overworld_frame\(|obj_frame_tiles\(", body)
    if explicit:
        return len(explicit)
    asc = re.search(r"overworld_ascending_frames\(\s*(\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)", body)
    if asc:
        frame_bytes = (int(asc.group(2)) * int(asc.group(3)) * 64) // 2
        return sheet_frames(repo, asc.group(1), frame_bytes)
    return None


def outfit_slots(repo):
    """{(outfit, array, look, slot): id token}."""
    text = strip_comments(read(repo, TABLE))
    out = {}
    for m in re.finditer(r"\[(OUTFIT_[A-Z0-9_]+)\]\s*=\s*\{", text):
        name = m.group(1)
        if name == "OUTFIT_COUNT":
            continue
        end = matching_brace(text, m.end() - 1)
        if end is None:
            continue
        block = text[m.end() - 1 : end + 1]
        for array in ("avatarGfxIds", "animGfxIds"):
            am = re.search(r"\." + array + r"\s*=\s*\{", block)
            if not am:
                continue
            aend = matching_brace(block, am.end() - 1)
            abody = block[am.end() - 1 : aend + 1]
            for gm in re.finditer(r"\[(MALE|FEMALE|PLAYER_LOOK_[A-Z]+)\]\s*=\s*\{", abody):
                look = {"MALE": "PLAYER_LOOK_MASC", "FEMALE": "PLAYER_LOOK_FEM"}.get(
                    gm.group(1), gm.group(1)
                )
                gend = matching_brace(abody, gm.end() - 1)
                for sm in re.finditer(
                    r"\[(PLAYER_AVATAR_[A-Z0-9_]+)\]\s*=\s*([A-Za-z0-9_]+)",
                    abody[gm.end() - 1 : gend + 1],
                ):
                    out[(name, array, look, sm.group(1))] = sm.group(2)
    return out


def check(repo, fail):
    ids = anim_ids(repo)
    tables = anim_tables(repo, ids)
    infos = graphics_infos(repo)
    pointers = info_pointers(repo)
    macros = avatar_macros(repo)
    slots = outfit_slots(repo)

    if not slots:
        fail("no outfit slots parsed from %s" % TABLE)
        return
    for name in ON_FOOT_REQUIRED:
        if name not in ids:
            fail("%s is not defined in %s" % (name, MOVEMENT))
            return

    def resolve(token):
        """id token -> (id, info name, anim id set, anim frame set)."""
        gid = macros.get(token, token)
        info = pointers.get(gid)
        if info is None or info not in infos:
            return gid, None, None, None
        anims = infos[info].get("anims")
        if anims not in tables:
            return gid, info, None, None
        covered, frames = tables[anims]
        return gid, info, covered, frames

    # 1. EVERY SLOT'S SPRITE COVERS WHAT THE DEFAULT'S DOES. The default row is
    #    the vanilla player's own art, so it is the statement of what the engine
    #    asks for in that state.
    for key in sorted(slots):
        outfit, array, look, slot = key
        token = slots[key]
        gid, info, covered, frames = resolve(token)
        if info is None:
            fail(
                "%s %s[%s][%s] names %s, which has no graphics info - "
                "GetObjectEventGraphicsInfo would read past the pointer table"
                % (outfit, array, look, slot, gid)
            )
            continue
        if covered is None:
            fail("%s: the anim table of %s could not be read" % (outfit, info))
            continue

        ref_token = slots.get((DEFAULT_ROW, array, look, slot))
        if ref_token is None:
            fail("the default outfit has no %s[%s][%s] to compare against" % (array, look, slot))
            continue
        _, ref_info, ref_covered, _ = resolve(ref_token)
        if ref_covered is None:
            continue  # reported on its own row
        missing = sorted(ref_covered - covered)
        if missing:
            fail(
                "%s %s[%s][%s] wears %s (%s), whose anim table does not define "
                "anim id%s %s - the default outfit's %s does, so the engine asks "
                "for %s in this state and this sprite indexes past the end of its "
                "table. That is a hang, not a wrong picture"
                % (
                    outfit, array, look, slot, gid, info,
                    "" if len(missing) == 1 else "s",
                    ", ".join(str(i) for i in missing[:8]) + (" ..." if len(missing) > 8 else ""),
                    ref_info, "them" if len(missing) > 1 else "it",
                )
            )

        # 3. FRAMES. An anim that exists but points past the sheet hands a copy
        #    a pointer AND a length read from past the end of the pic table.
        images = infos[info].get("images")
        if images and frames:
            have = pic_table_frames(repo, images)
            if have is None:
                fail(
                    "%s: cannot determine how many frames %s holds. Teach "
                    "pic_table_frames() this shape or write the table out as "
                    "explicit overworld_frame entries - an unreadable frame count "
                    "is an unchecked one" % (info, images)
                )
            elif max(frames) >= have:
                fail(
                    "%s %s[%s][%s] wears %s (%s): its anims ask for frame %d and "
                    "%s holds %d. The frame past the end is a SpriteFrameImage "
                    "read out of bounds - a bogus pointer and a bogus length"
                    % (outfit, array, look, slot, gid, info, max(frames), images, have)
                )

    # 2. THE ON-FOOT FLOOR, checked directly. If the default row lost these, the
    #    comparison above would go quiet at the moment it mattered most.
    for look in ("PLAYER_LOOK_MASC", "PLAYER_LOOK_FEM"):
        token = slots.get((DEFAULT_ROW, "avatarGfxIds", look, "PLAYER_AVATAR_STATE_NORMAL"))
        if token is None:
            fail("the default outfit has no on-foot sprite for %s" % look)
            continue
        gid, info, covered, _ = resolve(token)
        if covered is None:
            continue
        for name in ON_FOOT_REQUIRED:
            if ids[name] not in covered:
                fail(
                    "the default outfit's on-foot sprite for %s (%s, %s) does not "
                    "define %s. The player plays it while running and while warping, "
                    "and every other outfit is checked against THIS row - losing it "
                    "here silently lowers the bar for all of them"
                    % (look, gid, info, name)
                )


BREAKS = [
    (
        "the RS outfit back on the NPC sprite, the bug this check exists for",
        TABLE,
        lambda s: s.replace(
            "[PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_RS_BRENDAN_NORMAL,",
            "[PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_LINK_RS_BRENDAN,",
            1,
        ),
    ),
    (
        "the Kanto acro bike back on a sprite with no wheelie anims",
        TABLE,
        lambda s: s.replace(
            "[PLAYER_AVATAR_STATE_ACRO_BIKE]  = PLAYER_AVATAR_GFX_MALE_ACRO_BIKE, // no FRLG art",
            "[PLAYER_AVATAR_STATE_ACRO_BIKE]  = OBJ_EVENT_GFX_RED_BIKE,",
            1,
        ),
    ),
    (
        "an outfit naming an id that has no graphics info",
        TABLE,
        lambda s: s.replace(
            "[PLAYER_AVATAR_STATE_SURFING]    = OBJ_EVENT_GFX_RED_SURF,",
            "[PLAYER_AVATAR_STATE_SURFING]    = OBJ_EVENT_GFX_NOT_A_REAL_SPRITE,",
            1,
        ),
    ),
    (
        "the Kanto watering slot back on the field move sprite",
        TABLE,
        lambda s: s.replace(
            "[PLAYER_AVATAR_ANIM_WATERING]   = PLAYER_AVATAR_GFX_MALE_WATERING, // no FRLG art",
            "[PLAYER_AVATAR_ANIM_WATERING]   = OBJ_EVENT_GFX_RED_FIELD_MOVE,",
            1,
        ),
    ),
    (
        "the surfing slot pointed at a one-anim field move sprite",
        TABLE,
        lambda s: s.replace(
            "[PLAYER_AVATAR_STATE_SURFING]    = OBJ_EVENT_GFX_RED_SURF,",
            "[PLAYER_AVATAR_STATE_SURFING]    = OBJ_EVENT_GFX_RED_FIELD_MOVE,",
            1,
        ),
    ),
    (
        "the new RS player info downgraded to an NPC anim table",
        INFOS,
        lambda s: s.replace(
            "    .anims = sAnimTable_BrendanMayNormal,\n    .images = sPicTable_RSBrendanPlayer,",
            "    .anims = sAnimTable_Standard,\n    .images = sPicTable_RSBrendanPlayer,",
            1,
        ),
    ),
    (
        "the RS player sheet cut back to its walking half",
        PIC_TABLES,
        lambda s: re.sub(
            r"(static const struct SpriteFrameImage sPicTable_RSBrendanPlayer\[\] = \{\n"
            r"(?:.*?overworld_frame\(gObjectEventPic_RubySapphireBrendanNormal[^\n]*\n)+)"
            r"(?:\s*overworld_frame\(gObjectEventPic_RubySapphireBrendanRunning[^\n]*\n)+",
            r"\1",
            s,
            count=1,
        ),
    ),
    (
        "the default row's on-foot sprite losing its spin anims",
        ANIMS,
        lambda s: s.replace("    [ANIM_SPIN_EAST] = sAnim_SpinEast,\n", "", 1),
    ),
    (
        "the default row's on-foot sprite losing its run anims",
        ANIMS,
        lambda s: s.replace(
            "    [ANIM_RUN_SOUTH] = (IS_FRLG ? sAnim_RunSouthFrlg : sAnim_RunSouth),\n", "", 1
        ),
    ),
]


def run_check(repo):
    # DEDUPED. A frame count belongs to a pic table, not to a slot, so three
    # looks wearing one sprite reported it three times - and a check that
    # repeats itself reads as three problems when it has found one.
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
    print("check_player_sprite_anims: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
