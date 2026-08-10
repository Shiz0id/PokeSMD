#include "global.h"
#include "test/test.h"
#include "rogue_dungeon.h"

// RogueDungeon_DevolveForLevel, on the emulator, against the generated table.
//
// The failure this is really guarding is not a wrong answer but NO answer:
// FindEvoStep bisects sRogueEvoSteps, which tools/rogue/gen_evolution_levels.py
// emits in species-enum order. A table out of order does not crash and does not
// warn -- the bisection misses, returns NULL, and the species is handed back
// untouched. That is pixel-identical to the bug this whole change exists to
// fix, so "it built and the encounter looked plausible" proves nothing.

TEST("dungeon encounters: a Graveler on an early cave floor becomes a Geodude")
{
    // The reported case. Cave floor 0 is level 10; Geodude evolves at 25.
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_GRAVELER, 10), SPECIES_GEODUDE);
}

TEST("dungeon encounters: a Golem walks down TWO steps, not one")
{
    // Golem needs 25 by way of Graveler even though Graveler to Golem is a
    // trade carrying no level of its own. One step would leave a Graveler,
    // which is still wrong at level 10 and is what a non-chain-aware minLevel
    // would produce.
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_GOLEM, 10), SPECIES_GEODUDE);
}

TEST("dungeon encounters: a species is left alone once the level allows it")
{
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_GRAVELER, 25), SPECIES_GRAVELER);
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_GRAVELER, 40), SPECIES_GRAVELER);
}

TEST("dungeon encounters: a basic species is never touched")
{
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_ZUBAT, 5), SPECIES_ZUBAT);
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_TAUROS, 5), SPECIES_TAUROS);
}

TEST("dungeon encounters: a stone evolution is left alone at any level")
{
    // Growlithe to Arcanine is a Fire Stone and carries no level, so this is
    // legal at level 5 and the runtime has no basis to touch it. Whether an
    // Arcanine BELONGS on an early floor is a pool question, and it is the
    // STAGED half of check_pool_evolutions.py rather than this.
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_ARCANINE, 5), SPECIES_ARCANINE);
}

TEST("dungeon encounters: the worst offenders all come down")
{
    // Claydol was the widest gap found: level 15 against a Baltoy that cannot
    // evolve until 36.
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_CLAYDOL, 15), SPECIES_BALTOY);
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_LAIRON, 13), SPECIES_ARON);
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_DUGTRIO, 12), SPECIES_DIGLETT);
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_GOLBAT, 12), SPECIES_ZUBAT);
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_LOUDRED, 12), SPECIES_WHISMUR);
}

TEST("dungeon encounters: a three stage chain resolves to its base")
{
    // Beautifly is Wurmple at 7 then Silcoon at 10, so a floor at level 5 must
    // land on Wurmple rather than stopping at Silcoon.
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_BEAUTIFLY, 5), SPECIES_WURMPLE);
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_BEAUTIFLY, 8), SPECIES_SILCOON);
    EXPECT_EQ(RogueDungeon_DevolveForLevel(SPECIES_BEAUTIFLY, 10), SPECIES_BEAUTIFLY);
}

TEST("dungeon encounters: the table is searchable across its whole range")
{
    // A bisection over a mis-sorted table fails SILENTLY by missing, so these
    // probe the ends and the middle of the species enum rather than trusting
    // that one hit anywhere proves the ordering. Each of these has a known
    // pre-evolution, so a NULL from FindEvoStep shows up as no change.
    EXPECT_NE(RogueDungeon_DevolveForLevel(SPECIES_IVYSAUR, 5), SPECIES_IVYSAUR);
    EXPECT_NE(RogueDungeon_DevolveForLevel(SPECIES_CROBAT, 5), SPECIES_CROBAT);
    EXPECT_NE(RogueDungeon_DevolveForLevel(SPECIES_AMPHAROS, 5), SPECIES_AMPHAROS);
    EXPECT_NE(RogueDungeon_DevolveForLevel(SPECIES_METAGROSS, 5), SPECIES_METAGROSS);
    EXPECT_NE(RogueDungeon_DevolveForLevel(SPECIES_LUXRAY, 5), SPECIES_LUXRAY);
}
