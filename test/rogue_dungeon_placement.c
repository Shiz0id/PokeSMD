#include "global.h"
#include "test/test.h"
#include "rogue_dungeon.h"

// THE SEEDED STREAM, PINNED.
//
// Every object on a floor - stairs, spawn, grass, trainers, item balls, berry
// trees, mining rocks, the event and its prop, buried items - is positioned by
// draws from one seeded RNG, in one fixed order. The rule the whole placement
// system rests on is that NO PLACER MAY CHANGE HOW MANY TIMES IT DRAWS:
// inserting or removing a single DungeonRandom() relocates everything placed
// after it, on every floor, for ever. Nothing fails to build, every check still
// passes, and the game is silently different.
//
// The code pays real costs to hold that line - the event odds roll happens
// before the early returns, the species and ambush rolls happen whether or not
// the event reads them, the weighted pick is one draw plus a walk rather than
// reject-and-reroll, props are placed by arithmetic instead of a draw, and a
// blocked tile means no object rather than a retry. Until this file, all of
// that was enforced by comments alone.
//
// WHY IN THE ROM AND NOT IN PYTHON. tools/rogue/verify_dungeon_gen.py and
// verify_seeding.py are PORTS - a second implementation of the generator - so
// they can only ever check that the port agrees with itself. One of them had
// drifted onto an eight-room floor the game stopped generating and still
// reported a clean pass. A digest taken from the real generator on real
// emulated hardware has nothing to drift from.
//
// HOW TO REPIN. Change the digest below to 0, run the file, and the failure
// message prints the value it got - EXPECT_EQ reports both sides. Paste it in
// only after satisfying yourself the change was intended. A digest that moves
// for a reason nobody can name is the bug this file exists to surface, not a
// number to update.

// Floors chosen to cover every generator a theme actually selects, plus both
// arena paths. Theme is fixed by floor number because the digest pins
// VAR_ROGUE_RUN_ORDER to 0 (no shuffle) - see the note in
// RogueDungeon_Test_HashFloorPlacements.
//
// DUNGEON_GEN_FACILITY is deliberately absent: NO THEME IN sDungeonThemes
// SELECTS IT, so no floor reaches it and there is nothing here to pin. That is
// worth knowing rather than working around - the generator and its own check
// exist, and nothing in a run can currently reach either.
//
// FIVE OF THESE DIGESTS WERE REPINNED when the floor state became one struct
// and the per-floor reset became one memset. That was not a cosmetic change:
// before it, a floor's placement depended on WHAT HAD BEEN PREPARED BEFORE IT,
// because only six of the placement counters were zeroed in PrepareFloor's
// prologue. hiddenCount was not among them and is reset only inside
// PlaceHiddenItems, which the arena path never reaches - so a boss floor
// published the PREVIOUS floor's buried items through
// ApplyDungeonEvents (bgEventCount = sFloor.hiddenCount), at the previous
// floor's coordinates, collectable because RollNewFloorSeed clears their flags.
//
// The test below pins the property rather than the symptom, and it is the one
// that would have caught this.
struct PlacementCase
{
    u16 floor;
    u16 seed;
    u32 digest;
    // Is any of what this floor places decided by a DRAW? False for a gym boss
    // arena: fixed geometry, and a boss picked from the dungeon's identity
    // rather than rolled, so PrepareArenaFloor makes no DungeonRandom() calls
    // at all. Its placement is a function of the floor number alone, and no
    // stray draw anywhere can move it.
    bool8 seedDependent;
    const char *what;
};

static const struct PlacementCase sCases[] =
{
    // floor  seed     digest       what
    { 0,   0x1234,  0x6012315C, TRUE,  "woods, GEN_WOODS" },
    { 10,  0x1234,  0xA0E17B47, TRUE,  "cave, GEN_ORGANIC" },
    { 20,  0x1234,  0xA11CEC19, TRUE,  "new mauville, GEN_CAVE" },
    { 30,  0xBEEF,  0xBDE2749B, TRUE,  "fiery path, GEN_ORGANIC" },
    { 50,  0x1234,  0xC562B790, TRUE,  "jungle, GEN_TRAILS" },
    { 60,  0x1234,  0x85BD9831, TRUE,  "ocean, water elevation, buries nothing" },
    { 70,  0x1234,  0x56AB6FDD, TRUE,  "underwater, own map, buries nothing" },
    { 100, 0x1234,  0x386091B0, TRUE,  "ever grande, GEN_TRAILS" },
    { 105, 0x0001,  0xEFC2AB29, TRUE,  "murky cave, the finale" },
    // Both arena paths. PrepareFloor takes its boss branch and returns before
    // it reaches the placers at all, which is a different failure surface -
    // and the one that shipped a bug, when four placement counters kept the
    // previous floor's values and boss floors spawned last floor's objects.
    { 4,   0x1234,  0x2B233DE5, TRUE,  "mini boss arena, one draw for the pick" },
    { 9,   0x1234,  0xB59B9557, FALSE, "gym boss arena, NOTHING is rolled" },
};

TEST("dungeon placement: the digest is a function of floor and seed alone")
{
    u32 i;

    // Runs FIRST in the file on purpose. If placement depended on anything the
    // digest does not control - a leftover var, party state, a static the last
    // floor left set - every pinned value below would be noise, and this is the
    // test that says so rather than letting them fail one at a time.
    for (i = 0; i < ARRAY_COUNT(sCases); i++)
    {
        u32 a = RogueDungeon_Test_HashFloorPlacements(sCases[i].floor,
                                                      sCases[i].seed);
        u32 b = RogueDungeon_Test_HashFloorPlacements(sCases[i].floor,
                                                      sCases[i].seed);
        EXPECT_EQ(a, b);
    }
}

TEST("dungeon placement: every floor still places exactly what it used to")
{
    u32 i;

    u32 moved = 0;

    // COLLECTED FIRST, ASSERTED ONCE. EXPECT_EQ exits the test at the first
    // failure, so asserting inside the loop would report one moved floor and
    // hide the rest - and "which floors moved" is exactly what tells you
    // whether a change was scoped the way you thought it was.
    for (i = 0; i < ARRAY_COUNT(sCases); i++)
    {
        u32 h = RogueDungeon_Test_HashFloorPlacements(sCases[i].floor,
                                                      sCases[i].seed);

        if (h != sCases[i].digest)
        {
            // Printed as decimal halves: Test_MgbaPrintf has no %x and no
            // width specifiers, and an unknown conversion desynchronises its
            // va_list rather than printing a wrong number.
            Test_MgbaPrintf("MOVED idx %d floor %d hi %d lo %d",
                            i, sCases[i].floor, h >> 16, h & 0xFFFF);
            moved++;
        }
    }

    EXPECT_EQ(moved, 0);
}

TEST("dungeon placement: ONE extra draw moves the digest on every floor")
{
    u32 i;
    u32 clean[ARRAY_COUNT(sCases)];
    u32 skewed[ARRAY_COUNT(sCases)];

    // THE BREAK TEST, and the reason the pinned values above can be trusted at
    // all. A digest that never moves is worth nothing, so this reproduces the
    // exact failure it guards: one stray DungeonRandom() in a placer.
    //
    // The skew is burned AFTER the rooms, stairs, spawn and grass, so the
    // floor's walls come out byte-identical and only the objects move. That is
    // the silent version of this bug and the version that has to be caught -
    // skewing at the seed instead would relayout the whole map and prove only
    // the much weaker claim that the hash notices a different floor.
    for (i = 0; i < ARRAY_COUNT(sCases); i++)
        clean[i] = RogueDungeon_Test_HashFloorPlacements(sCases[i].floor,
                                                         sCases[i].seed);

    RogueDungeon_Test_SetStreamSkew(1);
    for (i = 0; i < ARRAY_COUNT(sCases); i++)
        skewed[i] = RogueDungeon_Test_HashFloorPlacements(sCases[i].floor,
                                                          sCases[i].seed);
    // Cleared BEFORE the assertions, never after: a failing EXPECT exits the
    // test immediately, and a skew left set would silently poison every test
    // that runs after this one.
    RogueDungeon_Test_SetStreamSkew(0);

    // BOTH DIRECTIONS. A floor whose placement is decided by draws must move;
    // a floor that rolls nothing must NOT, and asserting that is what stops
    // someone "fixing" this test by making the gym arena random.
    for (i = 0; i < ARRAY_COUNT(sCases); i++)
    {
        if (sCases[i].seedDependent)
            EXPECT_NE(clean[i], skewed[i]);
        else
            EXPECT_EQ(clean[i], skewed[i]);
    }
}

TEST("dungeon placement: a gym boss arena is the same floor whatever the seed")
{
    u32 i;

    // The other half of seedDependent. A gym arena is fixed geometry with a
    // boss chosen by the dungeon's identity, so its placement cannot vary with
    // the seed - and if it ever starts to, the row above is stale and the skew
    // assertion silently weakens.
    for (i = 0; i < ARRAY_COUNT(sCases); i++)
    {
        if (sCases[i].seedDependent)
            continue;
        EXPECT_EQ(RogueDungeon_Test_HashFloorPlacements(sCases[i].floor, 0x0001),
                  RogueDungeon_Test_HashFloorPlacements(sCases[i].floor, 0xFFFE));
    }
}

TEST("dungeon placement: the skew hook is off unless a test asks for it")
{
    // Guards the harness rather than the game: if the skew ever leaked, every
    // pinned digest would fail together and the cause would not be obvious.
    u32 before = RogueDungeon_Test_HashFloorPlacements(0, 0x1234);

    RogueDungeon_Test_SetStreamSkew(0);
    EXPECT_EQ(before, RogueDungeon_Test_HashFloorPlacements(0, 0x1234));
    EXPECT_EQ(before, sCases[0].digest);
}

TEST("dungeon placement: a floor does not depend on the floor prepared before it")
{
    u32 i;
    u32 afterNormal[ARRAY_COUNT(sCases)];
    u32 afterArena[ARRAY_COUNT(sCases)];

    // THE GUARANTEE THE WHOLE DESIGN RESTS ON: a floor is entirely derived from
    // its seed. Nothing else may reach it - not the floor the player was on a
    // moment ago, not how many objects that floor happened to place.
    //
    // This was NOT true before the floor state became one struct with one
    // memset. Only six counters were zeroed before PrepareFloor could branch,
    // so state the arena path never touched survived into the next floor.
    //
    // Primed with an ARENA floor on the second pass on purpose: PrepareFloor
    // takes its boss branch and returns before reaching the placers at all, so
    // it is the case that leaves the most behind and the sharpest thing to
    // prime with.
    for (i = 0; i < ARRAY_COUNT(sCases); i++)
    {
        RogueDungeon_Test_HashFloorPlacements(20, 0x0777);
        afterNormal[i] = RogueDungeon_Test_HashFloorPlacements(sCases[i].floor,
                                                               sCases[i].seed);
    }

    for (i = 0; i < ARRAY_COUNT(sCases); i++)
    {
        RogueDungeon_Test_HashFloorPlacements(9, 0x0777);
        afterArena[i] = RogueDungeon_Test_HashFloorPlacements(sCases[i].floor,
                                                              sCases[i].seed);
    }

    for (i = 0; i < ARRAY_COUNT(sCases); i++)
    {
        if (afterNormal[i] != afterArena[i])
            Test_MgbaPrintf("CARRIED idx %d floor %d", i, sCases[i].floor);
        EXPECT_EQ(afterNormal[i], afterArena[i]);
    }
}

TEST("dungeon placement: different seeds place things differently")
{
    // Cheap, but it is what says the digest is reading the floor at all rather
    // than hashing a set of arrays that are still zeroed.
    EXPECT_NE(RogueDungeon_Test_HashFloorPlacements(0, 0x1234),
              RogueDungeon_Test_HashFloorPlacements(0, 0x1235));
    EXPECT_NE(RogueDungeon_Test_HashFloorPlacements(20, 0x0001),
              RogueDungeon_Test_HashFloorPlacements(20, 0x0002));
}
