"""Guard the scrambler's region half: which boss stands at a dungeon.

THE OWNERSHIP RULE, stated once, here:

    The five boss tables are ONE array of DUNGEON_COUNT * DUNGEON_REGION_COUNT
    rows, and the only expression allowed to index any of them is the row
    BossRowForSlot() returns. A slot is a position in the run; an identity is
    which dungeon stands there; a row is that identity in the region it rolled.
    Nothing may index a boss table with a slot, or with an identity.

WHY THIS EXISTS, and why it is not a data check. verify_run_structure.py already
proves the level arithmetic - that every boss in either region lands near the
curve at every slot the shuffle can put it at. It reads the tables out of the C
and the parties out of trainers.party and it says nothing whatever about which
expression the C uses to index them.

That is the whole failure mode here. Indexing sDungeonBossGfx with the identity
while indexing sDungeonBosses with the row builds cleanly, passes every level
check, and puts BROCK'S SPRITE ON ROXANNE - or worse, indexes with the slot and
reads a lineup that the floor generator does not agree with. Nothing is out of
range, nothing is out of tolerance, and no table is wrong. It is the same shape
as check_jukebox_funnels.py: a lifetime and a pairing, not a number.

The parallel-length half is a STATIC_ASSERT in the C and is deliberately not
duplicated here. What a STATIC_ASSERT cannot see is which variable a subscript
was written with.

WHAT IT ASSERTS.

  1. All five tables - sDungeonBosses, sDungeonBossGfx, sDungeonBossTMs,
     sDungeonBossMusic, sDungeonBossEnvironment - have exactly
     DUNGEON_COUNT * DUNGEON_REGION_COUNT rows, counted from the source.

  2. Every subscript of those five tables is the row, never a slot or an
     identity. The two ID-keyed searches (RogueDungeon_GetBossBGM,
     RogueDungeon_GetBossEnvironment) are exempt: they walk the whole array by
     loop counter, which is correct and is how they cover both region blocks.

  3. BossRowForSlot composes the two halves in the right ORDER - the identity
     comes out of DungeonForSlot first, and the region bit is then read for that
     identity. Reading the region for the SLOT instead compiles, permutes, and
     builds a fixed per-position roster that the shuffle merely reorders, which
     is a different feature that looks like this one.

  4. The region word is rolled behind the same gate as the order word, and is
     CLEARED on the closed-gate branch. Both are vars and survive a run, so a
     gate that only skips the roll leaves the last run's regions standing -
     the toggle would put the dungeons back in vanilla order and leave Red on
     floor 115.

  5. The placement-test hash pins the region word as well as the order word.
     Without it every pinned digest in the test file depends on whatever the
     save block happens to hold.

Run:  python3 tools/rogue/check_boss_regions.py [repo]
      python3 tools/rogue/check_boss_regions.py --selftest
"""
import re
import sys
from pathlib import Path

TABLES = [
    "sDungeonBosses",
    "sDungeonBossGfx",
    "sDungeonBossTMs",
    "sDungeonBossMusic",
    "sDungeonBossEnvironment",
]

# The two functions that walk a boss table by loop counter rather than by row.
# Both search for a trainer id across BOTH region blocks, which is exactly what
# the loop counter is for, so their subscripts are not row expressions.
ID_KEYED = ["RogueDungeon_GetBossBGM", "RogueDungeon_GetBossEnvironment"]


def fail(msgs, m):
    msgs.append(m)


def strip_comments(text):
    """Comments out, so a commented-out call does not read as a live one.

    Not hygiene - the selftest caught this. Commenting out RollDungeonRegion()
    is the likeliest way for it to actually go missing, and a substring test
    over the raw source finds the call inside the comment and passes.
    """
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def body_of(text, name):
    """The source of one function, from its signature to the closing brace."""
    m = re.search(rf"^\w[\w \*]*\b{name}\(", text, re.M)
    if not m:
        return ""
    i = text.index("{", m.start())
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return strip_comments(text[i:j + 1])
    return ""


def table_rows(text, name):
    """Count initialiser rows in a table, ignoring comments."""
    m = re.search(rf"{name}\[\]\s*=\s*\{{(.*?)\n\}};", text, re.S)
    if not m:
        return None
    body = re.sub(r"//[^\n]*", "", m.group(1))
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return len([t for t in body.split(",") if t.strip()])


def check(repo):
    msgs = []
    src = (repo / "src/rogue_dungeon.c").read_text(errors="replace")
    hdr = (repo / "include/rogue_dungeon.h").read_text(errors="replace")
    cst = (repo / "include/constants/rogue_dungeon.h").read_text(errors="replace")

    gym = int(re.search(r"#define DUNGEON_GYM_DUNGEONS\s+(\d+)", hdr).group(1))
    e4 = int(re.search(r"#define DUNGEON_E4_DUNGEONS\s+(\d+)", hdr).group(1))
    # WIRED, NOT COUNT, and the two mean different things. COUNT is how many
    # regions the encoding can express - 4, because the field is two bits.
    # WIRED is how many actually have a block in the boss tables. They differ
    # whenever a region has sprites but no parties yet, and indexing by COUNT
    # would demand rows that do not exist while indexing by nothing at all would
    # let the roll run off the end of every table.
    count = int(re.search(r"#define DUNGEON_REGION_COUNT\s+(\d+)", cst).group(1))
    regions = int(re.search(r"#define DUNGEON_REGION_WIRED\s+(\d+)", cst).group(1))
    expect = (gym + e4 + 1) * regions

    if regions > count:
        fail(msgs, f"DUNGEON_REGION_WIRED {regions} exceeds DUNGEON_REGION_COUNT "
                   f"{count}; there are more table blocks than the region field "
                   f"can name, so the tail of them is unreachable")

    bits = int(re.search(r"#define DUNGEON_REGION_BITS\s+(\d+)", cst).group(1))
    if count > (1 << bits):
        fail(msgs, f"DUNGEON_REGION_COUNT {count} does not fit in "
                   f"{bits} bits; a roll would wrap into another identity's "
                   f"field rather than failing")

    # 1. every table is one block per region.
    for name in TABLES:
        rows = table_rows(src, name)
        if rows is None:
            fail(msgs, f"{name}: table not found in src/rogue_dungeon.c")
        elif rows != expect:
            fail(msgs, f"{name}: {rows} rows, expected {expect} "
                       f"({gym + e4 + 1} identities x {regions} WIRED regions)")

    # EVERY BOSS MUSIC ID MUST BE A REAL SONG. sDungeonBossMusic holds song
    # constants, and song_table.inc is indexed BY ROW - so a constant naming a
    # row that is `mus_dummy` resolves, builds, and plays SILENCE for that
    # boss. There are dead rows in that table by design (613 and 614, see the
    # gm.dls note in the state file), so this is reachable, and a silent boss
    # fight is exactly the kind of thing nobody notices until they play it.
    #
    # Zero is exempt: it is the "let the engine choose" sentinel, not a song.
    songs = (repo / "include/constants/songs.h").read_text(errors="replace")
    ids = {m.group(1): int(m.group(2))
           for m in re.finditer(r"#define\s+(MUS_\w+)\s+(\d+)", songs)}
    table = re.search(r"sDungeonBossMusic\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if table:
        rows = [ln.strip() for ln in
                (repo / "sound/song_table.inc").read_text(errors="replace").splitlines()
                if re.match(r"\s*song\s", ln)]
        for name in re.findall(r"\b(MUS_\w+)\b", strip_comments(table.group(1))):
            idx = ids.get(name)
            if idx is None:
                fail(msgs, f"sDungeonBossMusic names {name}, which is not a "
                           f"song constant")
            elif idx >= len(rows):
                fail(msgs, f"sDungeonBossMusic names {name} (id {idx}), past "
                           f"the end of song_table.inc's {len(rows)} rows")
            elif "mus_dummy" in rows[idx]:
                fail(msgs, f"sDungeonBossMusic names {name} (id {idx}), which "
                           f"is a mus_dummy row - that boss fight would play "
                           f"SILENCE, and nothing else would report it")

    # The roll must be bounded by what is wired, not by what is named. A masked
    # two-bit field produces a 3 one time in four, and with three blocks wired
    # that indexes past the end of every table - silently, because the tables
    # are adjacent in ROM and the read succeeds.
    roll_fn = body_of(src, "RollDungeonRegion")
    if roll_fn and "DUNGEON_REGION_WIRED" not in roll_fn:
        fail(msgs, "RollDungeonRegion does not bound its roll by "
                   "DUNGEON_REGION_WIRED; a region with no table block would be "
                   "rolled and would index past the end of all five tables")

    # 2. every subscript is the row.
    exempt = "".join(body_of(src, fn) for fn in ID_KEYED)
    for name in TABLES:
        for m in re.finditer(rf"{name}\[\s*([A-Za-z_]\w*)", src):
            expr = m.group(1)
            # The declaration itself, and the ARRAY_COUNT bound in a modulo.
            if expr in ("", "ARRAY_COUNT"):
                continue
            if f"{name}[{expr}" in exempt or f"{name}[ {expr}" in exempt:
                continue
            if expr != "row":
                line = src[:m.start()].count("\n") + 1
                fail(msgs, f"{name} indexed with '{expr}' at "
                           f"src/rogue_dungeon.c:{line}; only the row "
                           f"BossRowForSlot returns may index a boss table")

    # 3. BossRowForSlot composes identity-then-region, in that order.
    row_fn = body_of(src, "BossRowForSlot")
    if not row_fn:
        fail(msgs, "BossRowForSlot not found")
    else:
        if "DungeonForSlot(slot)" not in row_fn:
            fail(msgs, "BossRowForSlot does not take its identity from "
                       "DungeonForSlot(slot)")
        if "DungeonRegionOf(identity)" not in row_fn:
            fail(msgs, "BossRowForSlot reads the region for something other "
                       "than the identity; a per-slot roster is a different "
                       "feature that looks like this one")
        if "DUNGEON_COUNT" not in row_fn:
            fail(msgs, "BossRowForSlot does not stride by DUNGEON_COUNT")

    # 4. the region word is rolled behind the gate, and cleared by it.
    roll = body_of(src, "RollDungeonOrder")
    if not roll:
        fail(msgs, "RollDungeonOrder not found")
    else:
        # THE ROLL READS THE TOGGLE AND NOTHING ELSE, and the second half of
        # this is the half with history. RollDungeonOrder used to test
        # FLAG_ROGUE_RUN_COMPLETED as well, which made the options entry a
        # one-way switch: it could turn the shuffle off but never on, so before
        # a first clear the control read ON and did nothing. The clear is a
        # DEFAULT now - set at new game, moved once by the first clear - and
        # putting the latch back into the roll would restore the bug exactly.
        if "FLAG_ROGUE_VANILLA_ORDER" not in roll:
            fail(msgs, "RollDungeonOrder does not read "
                       "FLAG_ROGUE_VANILLA_ORDER; the options toggle is the "
                       "only thing that decides whether a run is shuffled")
        if "FLAG_ROGUE_RUN_COMPLETED" in roll:
            fail(msgs, "RollDungeonOrder tests FLAG_ROGUE_RUN_COMPLETED again; "
                       "that makes the options entry a one-way switch that can "
                       "turn the shuffle off but never on")
        if "RollDungeonRegion()" not in roll:
            fail(msgs, "RollDungeonOrder does not roll the region word; the "
                       "two halves of the scrambler must be rolled together "
                       "or a run gets one and not the other")
        # The closed-gate branch is everything before the early return.
        closed = roll.split("return;")[0]
        # BOTH WORDS. The region is 2 bits per identity across a low and a high
        # var, and identities 7-13 live in the high one - so clearing only the
        # low word leaves half the run's bosses regionalised behind a closed
        # gate, which is the bug this rule exists for, just quieter.
        for w in ("LO", "HI"):
            if f"VarSet(VAR_ROGUE_RUN_REGION_{w}, 0)" not in closed:
                fail(msgs, f"the closed-gate branch of RollDungeonOrder does not "
                           f"clear VAR_ROGUE_RUN_REGION_{w}; it is a var and "
                           f"survives a run, so the last run's regions would "
                           f"stand behind a closed gate")

    # 4b. THE DEFAULT LIVES IN TWO PLACES AND BOTH ARE LOAD BEARING. New game
    # sets the toggle off, so a first playthrough walks vanilla order; the FIRST
    # clear turns it on. Miss the first and run 1 is shuffled; miss the second
    # and clearing the game unlocks nothing, which is the old bug wearing a
    # different hat.
    newgame = body_of(src, "RogueDungeon_ApplyNewGameUnlocks")
    if not newgame:
        fail(msgs, "RogueDungeon_ApplyNewGameUnlocks not found")
    elif "FlagSet(FLAG_ROGUE_VANILLA_ORDER)" not in newgame:
        fail(msgs, "RogueDungeon_ApplyNewGameUnlocks does not set "
                   "FLAG_ROGUE_VANILLA_ORDER; nothing else makes a first "
                   "playthrough walk vanilla order")

    completed = body_of(src, "RogueDungeon_OnRunCompleted")
    if not completed:
        fail(msgs, "RogueDungeon_OnRunCompleted not found")
    else:
        clear_at = completed.find("FlagClear(FLAG_ROGUE_VANILLA_ORDER)")
        set_at = completed.find("FlagSet(FLAG_ROGUE_RUN_COMPLETED)")
        if clear_at < 0:
            fail(msgs, "RogueDungeon_OnRunCompleted does not clear "
                       "FLAG_ROGUE_VANILLA_ORDER; a clear would then unlock "
                       "nothing")
        elif "if (!FlagGet(FLAG_ROGUE_RUN_COMPLETED))" not in completed:
            fail(msgs, "RogueDungeon_OnRunCompleted clears "
                       "FLAG_ROGUE_VANILLA_ORDER unconditionally; only the "
                       "FIRST clear may move it, or every later clear "
                       "re-enables a shuffle the player switched off")
        # THE ORDER IS THE WHOLE RULE. Tested after the latch is set, the
        # condition is false on the very first clear and the shuffle is never
        # unlocked at all - and it reads perfectly well either way round.
        elif 0 <= set_at < clear_at:
            fail(msgs, "RogueDungeon_OnRunCompleted sets "
                       "FLAG_ROGUE_RUN_COMPLETED before testing it; the first "
                       "clear then looks like a later one and unlocks nothing")

    if "RollDungeonRegion" in src and "static void RollDungeonRegion" not in src:
        fail(msgs, "RollDungeonRegion must stay static; a caller outside this "
                   "file could reroll mid-descent and repaint the dungeons "
                   "under the player")

    # 5. the placement hash pins the region as well as the order.
    hashfn = body_of(src, "RogueDungeon_Test_HashFloorPlacements")
    if hashfn:
        for w in ("LO", "HI"):
            if f"VarSet(VAR_ROGUE_RUN_REGION_{w}, 0)" not in hashfn:
                fail(msgs, f"RogueDungeon_Test_HashFloorPlacements does not pin "
                           f"VAR_ROGUE_RUN_REGION_{w}; every pinned digest would "
                           f"depend on the save block's region word")
        # BOTH HALVES, not the name. Testing for "savedRegion" passes as long as
        # either the save or the restore survives, and a restore from a variable
        # that no longer exists does not compile - so the case the check has to
        # catch is the save going missing while the restore stays, which the
        # bare name test cannot see. The selftest is what established that.
        saves = all(f"u16 savedRegion{w} = VarGet(VAR_ROGUE_RUN_REGION_{w.upper()})"
                    in hashfn for w in ("Lo", "Hi"))
        restores = all(f"VarSet(VAR_ROGUE_RUN_REGION_{w.upper()}, savedRegion{w})"
                       in hashfn for w in ("Lo", "Hi"))
        if not (saves and restores):
            fail(msgs, "RogueDungeon_Test_HashFloorPlacements must both save "
                       "and restore BOTH region words; pinning them without "
                       "putting it back breaks the NEXT test in the file "
                       f"rather than this one (saves={saves} restores={restores})")

    return msgs


SELFTESTS = [
    ("a boss table indexed with the identity again",
     "src/rogue_dungeon.c",
     "sDungeonBosses[row % ARRAY_COUNT(sDungeonBosses)]",
     "sDungeonBosses[identity % ARRAY_COUNT(sDungeonBosses)]"),
    ("the sprite table left on the identity while the id moved",
     "src/rogue_dungeon.c",
     "sDungeonBossGfx[row % ARRAY_COUNT(sDungeonBossGfx)]",
     "sDungeonBossGfx[slot % ARRAY_COUNT(sDungeonBossGfx)]"),
    ("the TM table indexed with the raw dungeon",
     "src/rogue_dungeon.c",
     "sDungeonBossTMs[row % ARRAY_COUNT(sDungeonBossTMs)]",
     "sDungeonBossTMs[dungeon % ARRAY_COUNT(sDungeonBossTMs)]"),
    ("a boss's music pointed at a dead mus_dummy row",
     "src/rogue_dungeon.c",
     "MUS_RG_VS_CHAMPION,                          // Blue",
     "MUS_GM_ECRUTEAK_DLS,                         // Blue"),
    ("a boss's music naming something that is not a song",
     "src/rogue_dungeon.c",
     "MUS_DPPT_BATTLE_CHAMPION,                          // Cynthia",
     "MUS_NOT_A_REAL_SONG,                               // Cynthia"),
    ("the region read for the slot rather than the identity",
     "src/rogue_dungeon.c",
     "DungeonRegionOf(identity) * DUNGEON_COUNT",
     "DungeonRegionOf(slot) * DUNGEON_COUNT"),
    ("the clear latch put back into the roll (the one-way switch)",
     "src/rogue_dungeon.c",
     "    if (FlagGet(FLAG_ROGUE_VANILLA_ORDER))\n    {\n"
     "        VarSet(VAR_ROGUE_RUN_ORDER, 0);",
     "    if (!FlagGet(FLAG_ROGUE_RUN_COMPLETED)"
     " || FlagGet(FLAG_ROGUE_VANILLA_ORDER))\n    {\n"
     "        VarSet(VAR_ROGUE_RUN_ORDER, 0);"),
    ("new game stops defaulting the shuffle off",
     "src/rogue_dungeon.c",
     "    FlagSet(FLAG_ROGUE_VANILLA_ORDER);\n}",
     "}"),
    ("the first clear stops unlocking the shuffle",
     "src/rogue_dungeon.c",
     "    if (!FlagGet(FLAG_ROGUE_RUN_COMPLETED))\n"
     "        FlagClear(FLAG_ROGUE_VANILLA_ORDER);\n\n"
     "    FlagSet(FLAG_ROGUE_RUN_COMPLETED);",
     "    FlagSet(FLAG_ROGUE_RUN_COMPLETED);"),
    ("the latch set before it is tested",
     "src/rogue_dungeon.c",
     "    if (!FlagGet(FLAG_ROGUE_RUN_COMPLETED))\n"
     "        FlagClear(FLAG_ROGUE_VANILLA_ORDER);\n\n"
     "    FlagSet(FLAG_ROGUE_RUN_COMPLETED);",
     "    FlagSet(FLAG_ROGUE_RUN_COMPLETED);\n\n"
     "    if (!FlagGet(FLAG_ROGUE_RUN_COMPLETED))\n"
     "        FlagClear(FLAG_ROGUE_VANILLA_ORDER);"),
    ("the region roll dropped from RollDungeonOrder",
     "src/rogue_dungeon.c",
     "    RollDungeonRegion();",
     "    /* RollDungeonRegion(); */"),
    ("the closed-gate branch stops clearing the region word",
     "src/rogue_dungeon.c",
     "        VarSet(VAR_ROGUE_RUN_REGION_LO, 0);\n"
     "        VarSet(VAR_ROGUE_RUN_REGION_HI, 0);\n        return;",
     "        return;"),
    ("RollDungeonRegion made non-static",
     "src/rogue_dungeon.c",
     "static void RollDungeonRegion(void)",
     "void RollDungeonRegion(void)"),
    ("the placement hash stops pinning the region",
     "src/rogue_dungeon.c",
     "    VarSet(VAR_ROGUE_RUN_REGION_LO, 0);\n"
     "    VarSet(VAR_ROGUE_RUN_REGION_HI, 0);\n"
     "    VarSet(VAR_ROGUE_DUNGEON_FLOOR, floor);",
     "    VarSet(VAR_ROGUE_DUNGEON_FLOOR, floor);"),
    ("the placement hash pins the region without restoring it",
     "src/rogue_dungeon.c",
     "    u16 savedRegionLo = VarGet(VAR_ROGUE_RUN_REGION_LO);",
     ""),
    # THE HIGH WORD ALONE, and it is the case worth having: identities 0-6 are
    # in the low word, so pinning only that one leaves HALF the run's bosses
    # reading the save block while every early floor looks perfectly stable.
    ("only the HIGH region word left unpinned, so identities 7-13 drift",
     "src/rogue_dungeon.c",
     "    VarSet(VAR_ROGUE_RUN_REGION_HI, 0);\n"
     "    VarSet(VAR_ROGUE_DUNGEON_FLOOR, floor);",
     "    VarSet(VAR_ROGUE_DUNGEON_FLOOR, floor);"),
    # The two "truncate a table" cases are NOT here - they are computed in
    # selftest() by find_last_row(), because naming a row makes them expire
    # every time a region is appended. That happened twice, for Johto and then
    # for Sinnoh, and both times the harness reported BROKEN CASE rather than a
    # pass - which is the harness working, and also noise that will recur for
    # every future region. Computing the anchor ends it.
]


def find_last_row(text, table):
    """(old, new) that deletes the final initialiser row of `table`.

    Anchors on POSITION rather than on a row's contents, so appending a region
    block does not silently retire the case. Returns None if the table cannot
    be located, which the harness reports as a broken case rather than a pass.
    """
    m = re.search(rf"{table}\[\]\s*=\s*\{{(.*?)\n\}};", text, re.S)
    if not m:
        return None
    body = m.group(1)
    rows = [ln for ln in body.split("\n")
            if ln.strip() and not ln.strip().startswith("//")]
    if not rows:
        return None
    last = rows[-1]
    return (last + "\n};", "};")


def selftest(repo):
    """Break each guarded property on purpose and require the check to fire."""
    baseline = check(repo)
    if baseline:
        print("SELFTEST ABORTED - the tree already fails:")
        for m in baseline:
            print("  " + m)
        return 1

    # The two computed cases, appended to the literal ones. Both truncate the
    # last row of a table, which must always leave the blocks uneven whatever
    # the last row currently is.
    src = (repo / "src/rogue_dungeon.c").read_text(errors="replace")
    cases = list(SELFTESTS)
    for table, label in (("sDungeonBosses", "the boss table's last row deleted"),
                         ("sDungeonBossEnvironment",
                          "the environment table's last row deleted")):
        anchor = find_last_row(src, table)
        if anchor is None:
            print(f"  BROKEN CASE  {label}: {table} not found")
            continue
        cases.append((label, "src/rogue_dungeon.c", anchor[0], anchor[1]))

    originals = {}
    bad = 0
    for name, relpath, old, new in cases:
        path = repo / relpath
        if relpath not in originals:
            originals[relpath] = path.read_text(errors="replace")
        text = originals[relpath]
        if old not in text:
            print(f"  BROKEN CASE  {name}: anchor not found, so this case has "
                  f"stopped testing anything")
            bad += 1
            continue
        path.write_text(text.replace(old, new, 1), newline="\n")
        fired = bool(check(repo))
        path.write_text(text, newline="\n")
        print(f"  {'fires ' if fired else '*** SILENT ***'}  {name}")
        if not fired:
            bad += 1

    for relpath, text in originals.items():
        (repo / relpath).write_text(text, newline="\n")

    print(f"\n{len(cases) - bad}/{len(cases)} selftest breaks fire")
    return 1 if bad else 0


def main():
    args = [a for a in sys.argv[1:] if a != "--selftest"]
    # Takes the repo positionally OR as --repo, following
    # check_start_menu_pages.py - a check that accepts both can never end up on
    # the wrong side of run_all_checks.sh's hand-maintained list.
    if args and args[0] == "--repo":
        args = args[1:]
    repo = Path(args[0]) if args else Path(__file__).resolve().parents[2]

    if "--selftest" in sys.argv[1:]:
        return selftest(repo)

    msgs = check(repo)
    if msgs:
        print("FAILED:")
        for m in msgs:
            print("  " + m)
        return 1
    print("boss region tables: rows, subscripts, roll gate and test pinning all OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
