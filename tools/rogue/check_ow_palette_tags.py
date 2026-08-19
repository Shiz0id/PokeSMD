"""Does every overworld sprite's paletteTag actually resolve to a palette?

An ObjectEventGraphicsInfo names its colours by TAG, and the tag is resolved at
runtime against sObjectEventSpritePalettes in src/event_object_movement.c. The
two live in different files, and NOTHING CONNECTS THEM AT BUILD TIME - the tag
is a u16, so a graphics info may name a tag that the table does not carry and
the build stays clean.

The failure is silent and it is not a crash. ObjectEventSetGraphics does:

    u32 i = FindObjectEventPaletteIndexByTag(graphicsInfo->paletteTag);
    if (i != 0xFF)
        UpdateSpritePalette(...);

so a missing tag means the palette is simply never loaded. The sprite keeps
whatever palette its OAM slot happened to hold from the last thing drawn there,
and renders in those colours instead - which on screen reads as corruption that
CHANGES depending on what else is on the map.

THIS HAS ALREADY SHIPPED ONCE. The ~150 FRLG overworld sprites were unguarded
for trainer variety by replacing IS_FRLG with 1 in four object_events data
files. The palette table is a FIFTH site in a different file and was missed, so
every FRLG sprite drew through a stale palette from the moment something
started selecting them.

--selftest proves the measurement can fail by reconstructing exactly that state:
it re-resolves with the FRLG palette block forced inactive and requires the
check to fire. A check that has never failed is worth nothing.

Usage:  python3 tools/rogue/check_ow_palette_tags.py [--repo PATH] [--selftest]
"""
import argparse
import re
import sys
from pathlib import Path

TABLE = "src/event_object_movement.c"
INFOS = "src/data/object_events/object_event_graphics_info.h"
CONFIG = "include/config/overworld.h"

# Tags that are never looked up in the table. DYNAMIC is branched on by name in
# ObjectEventSetGraphicsInfo and routed to LoadDynamicFollowerPalette; NONE is
# the table terminator and means "this sprite has no palette of its own".
EXEMPT = {"OBJ_EVENT_PAL_TAG_NONE", "OBJ_EVENT_PAL_TAG_DYNAMIC"}

TAG_RE = re.compile(r"OBJ_EVENT_PAL_TAG_[A-Z0-9_]+")
ENTRY_RE = re.compile(r"gObjectEventGraphicsInfo_(\w+)\s*=")
FIELD_RE = re.compile(r"\.(reflectionPaletteTag|paletteTag)\s*=\s*(OBJ_EVENT_PAL_TAG_[A-Z0-9_]+)")


def read(repo, rel):
    return (repo / rel).read_text(encoding="utf-8", errors="replace").splitlines()


def config_bool(repo, name):
    """Resolve a TRUE/FALSE config #define, rather than assuming its value."""
    pat = re.compile(r"^\s*#define\s+" + re.escape(name) + r"\s+(TRUE|FALSE)\b")
    for line in read(repo, CONFIG):
        m = pat.match(line)
        if m:
            return m.group(1) == "TRUE"
    raise SystemExit("FAIL: could not resolve %s in %s" % (name, CONFIG))


def guard_value(cond, repo, force_frlg_off):
    """Evaluate the small set of guards these two files actually use.

    Anything unrecognised is a HARD ERROR rather than a guess. If someone adds a
    new guard around part of the palette table, this check must stop and say so
    - quietly assuming the block is active would make it pass vacuously on the
    exact edit most likely to reintroduce the bug.
    """
    cond = cond.split("//")[0].split("/*")[0].strip()
    if cond == "1":
        # `#if 1 // was IS_FRLG` is the deliberate unguarding. Under --selftest
        # we pretend it was never done, which is the historical broken state.
        return not force_frlg_off
    if cond == "0":
        return False
    if cond == "IS_FRLG":
        return False  # this branch builds Emerald
    if cond == "BUGFIX":
        return False  # not defined by the build
    if cond == "OW_FOLLOWERS_POKEBALLS":
        return config_bool(repo, cond)
    raise SystemExit(
        "FAIL: unrecognised preprocessor guard '%s'.\n"
        "      Teach guard_value() its real value - do not let this check guess."
        % cond
    )


def scan_guarded(lines, repo, force_frlg_off, start, end):
    """Yield (lineno, text) for lines whose guard nesting is all-true."""
    stack = []
    for i in range(start, end):
        line = lines[i]
        s = line.strip()
        if s.startswith("#if"):
            if s.startswith("#ifdef"):
                cond = s[len("#ifdef"):]
            elif s.startswith("#ifndef"):
                # None present today; refuse rather than invert blindly.
                raise SystemExit("FAIL: #ifndef in a scanned region is unhandled")
            else:
                cond = s[len("#if"):]
            stack.append(guard_value(cond, repo, force_frlg_off))
            continue
        if s.startswith("#else"):
            if stack:
                stack[-1] = not stack[-1]
            continue
        if s.startswith("#endif"):
            if stack:
                stack.pop()
            continue
        if all(stack):
            yield i + 1, line


def table_region(lines):
    start = None
    for i, line in enumerate(lines):
        if "sObjectEventSpritePalettes[] = {" in line:
            start = i + 1
            break
    if start is None:
        raise SystemExit("FAIL: could not find sObjectEventSpritePalettes in " + TABLE)
    for i in range(start, len(lines)):
        if lines[i].startswith("};"):
            return start, i
    raise SystemExit("FAIL: could not find the end of sObjectEventSpritePalettes")


def registered_tags(repo, force_frlg_off):
    lines = read(repo, TABLE)
    start, end = table_region(lines)
    tags = set()
    for _, line in scan_guarded(lines, repo, force_frlg_off, start, end):
        for tag in TAG_RE.findall(line):
            tags.add(tag)
    return tags


ROW_RE = re.compile(r"\{\s*(\w+)\s*,\s*(OBJ_EVENT_PAL_TAG_[A-Z0-9_]+)\s*\}")
BERRY_TABLES = "src/data/object_events/berry_tree_graphics_tables.h"


def registered_rows(repo, force_frlg_off):
    """The table's rows IN ORDER, which is what the berry trees depend on."""
    lines = read(repo, TABLE)
    start, end = table_region(lines)
    rows = []
    for _, line in scan_guarded(lines, repo, force_frlg_off, start, end):
        m = ROW_RE.search(line)
        if m:
            rows.append(m.group(2))
    return rows


def berry_slots(repo):
    """Every distinct slot number the berry tree palette tables name."""
    src = "\n".join(read(repo, BERRY_TABLES))
    slots = set()
    for body in re.findall(r"gBerryTreePaletteSlotTable_\w+\[\]\s*=\s*\{([0-9,\s]*)\}", src):
        for n in re.findall(r"\d+", body):
            slots.add(int(n))
    return sorted(slots)


def berry_position_errors(repo, force_frlg_off):
    """THE POSITIONAL CONTRACT, which tag resolution cannot see.

    SetBerryTreeGraphicsById is the one consumer that indexes this table by
    POSITION rather than by tag:

        UpdateSpritePalette(&sObjectEventSpritePalettes[
            gBerries[berryId].berryTreePaletteSlotTable[berryStage] - 2], sprite);

    The slot numbers are 2..5, so it reads rows 0..3, and those rows must be the
    four NPC palettes the berry sheets were drawn against. Insert anything at the
    top of the table and every berry tree in the game silently repaints - which
    is exactly what staging 27 Johto/Sinnoh sprites did, while every TAG still
    resolved and this check went on passing. It is the same lesson the FRLG bug
    taught in the other direction: the thing that broke was never a tag.
    """
    rows = registered_rows(repo, force_frlg_off)
    bad = []
    for slot in berry_slots(repo):
        idx = slot - 2
        want = "OBJ_EVENT_PAL_TAG_NPC_%d" % (slot - 1)
        got = rows[idx] if idx < len(rows) else "<past the end of the table>"
        if got != want:
            bad.append((slot, idx, want, got))
    return bad


def referenced_tags(repo, force_frlg_off):
    """Every (sprite, field, tag) an active graphics info entry names."""
    lines = read(repo, INFOS)
    refs = []
    current = "?"
    for lineno, line in scan_guarded(lines, repo, force_frlg_off, 0, len(lines)):
        m = ENTRY_RE.search(line)
        if m:
            current = m.group(1)
        m = FIELD_RE.search(line)
        if m:
            refs.append((current, m.group(1), m.group(2), lineno))
    return refs


def run(repo, force_frlg_off):
    """Return the list of unresolvable references."""
    have = registered_tags(repo, force_frlg_off)
    refs = referenced_tags(repo, force_frlg_off)
    if not refs:
        raise SystemExit("FAIL: parsed zero paletteTag references - the parse broke")
    return [r for r in refs if r[2] not in EXEMPT and r[2] not in have], have, refs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    repo = Path(args.repo)

    if args.selftest:
        # Reconstruct the shipped bug: sprites unguarded, palettes still behind
        # IS_FRLG. The check MUST fire, or it is measuring nothing.
        broken, _, _ = run(repo, force_frlg_off=False)
        if broken:
            print("SELFTEST FAIL: the real tree is already broken; fix it first")
            return 1
        # Force the FRLG palette block off while leaving the sprites active.
        lines = read(repo, TABLE)
        start, end = table_region(lines)
        frlg_only = registered_tags(repo, False) - set(
            t for _, line in scan_guarded(lines, repo, True, start, end)
            for t in TAG_RE.findall(line)
        )
        if not frlg_only:
            print("SELFTEST FAIL: forcing the guard off removed no tags at all")
            return 1
        refs = referenced_tags(repo, force_frlg_off=False)
        would_break = [r for r in refs if r[2] in frlg_only]
        if not would_break:
            print("SELFTEST FAIL: no sprite references the tags the guard hides")
            return 1
        print("SELFTEST PASS: restoring the IS_FRLG guard breaks %d references "
              "across %d sprites; the check would fire."
              % (len(would_break), len(set(r[0] for r in would_break))))

        # SECOND BREAK: the positional contract, reconstructed the way it really
        # shipped - one row prepended to the table. Tag resolution is untouched
        # by this, which is the whole point: the first half of this selftest
        # cannot see it, and did not, for the entire time berry trees were drawing
        # in a Sinnoh rival's colours.
        if berry_position_errors(repo, force_frlg_off=False):
            print("SELFTEST FAIL: the berry slots are already wrong; fix first")
            return 1

        lines = read(repo, TABLE)
        start, _ = table_region(lines)
        shifted = lines[:start] + ["    {gObjectEventPal_Npc1, OBJ_EVENT_PAL_TAG_INTRUDER},"] \
                  + lines[start:]

        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / TABLE).parent.mkdir(parents=True, exist_ok=True)
            (tmp / TABLE).write_text("\n".join(shifted), encoding="utf-8", newline="\n")
            for rel in (INFOS, CONFIG, BERRY_TABLES):
                (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
                (tmp / rel).write_text(
                    (repo / rel).read_text(encoding="utf-8", errors="replace"),
                    encoding="utf-8", newline="\n")
            if not berry_position_errors(tmp, force_frlg_off=False):
                print("SELFTEST FAIL: prepending a row did NOT move the berry "
                      "slots - the positional rule is measuring nothing")
                return 1

        print("SELFTEST PASS: prepending one palette row moves all %d berry "
              "slots off the NPC palettes; the check would fire."
              % len(berry_slots(repo)))
        return 0

    broken, have, refs = run(repo, force_frlg_off=False)
    if broken:
        print("FAIL: %d paletteTag references do not resolve in "
              "sObjectEventSpritePalettes." % len(broken))
        print("      These sprites keep a stale palette and draw in wrong colours.")
        for sprite, field, tag, lineno in broken[:20]:
            print("      %s:%d  %s.%s = %s" % (INFOS, lineno, sprite, field, tag))
        if len(broken) > 20:
            print("      ... and %d more" % (len(broken) - 20))
        return 1

    misplaced = berry_position_errors(repo, force_frlg_off=False)
    if misplaced:
        print("FAIL: the berry trees' positional palette slots have moved.")
        print("      SetBerryTreeGraphicsById indexes sObjectEventSpritePalettes")
        print("      by POSITION, not by tag, so anything inserted at the top of")
        print("      that table repaints every berry tree in the game.")
        for slot, idx, want, got in misplaced:
            print("      slot %d -> row %d: expected %s, found %s"
                  % (slot, idx, want, got))
        print("      Fix: move new rows to the END of the table, above the")
        print("      OBJ_EVENT_PAL_TAG_NONE sentinel.")
        return 1

    print("PASS: %d paletteTag references across %d sprites all resolve "
          "against %d registered palettes; berry slots %s still land on the "
          "NPC palettes."
          % (len(refs), len(set(r[0] for r in refs)), len(have),
             ",".join(str(s) for s in berry_slots(repo))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
