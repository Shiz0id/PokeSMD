#include "global.h"
#include "test/battle.h"
#include "decompress.h"
#include "rogue_bw_anim.h"

// The nine emitted species are the two boss parties this was built to test -
// Roxanne's Geodude and Nosepass, Tate & Liza's Claydol, Xatu, Lunatone and
// Solrock - plus the three starters, which animate only when a run MEETS them
// since the player's own side shows back sprites these assets do not contain.

TEST("BW anim: every emitted species resolves and agrees with its container")
{
    static const u16 species[] = {
        SPECIES_GEODUDE, SPECIES_NOSEPASS,
        SPECIES_CLAYDOL, SPECIES_XATU, SPECIES_LUNATONE, SPECIES_SOLROCK,
        SPECIES_TREECKO, SPECIES_TORCHIC, SPECIES_MUDKIP,
    };

    for (u32 i = 0; i < ARRAY_COUNT(species); i++)
    {
        const struct BwAnim *anim = GetBwAnim(species[i]);

        EXPECT(anim != NULL);
        if (anim == NULL)
            continue;

        EXPECT_EQ(anim->species, species[i]);
        EXPECT_EQ(IsSmolFrameContainer(anim->frames), TRUE);

        // Every battle sprite is 64x64. A species emitted at its own bounding
        // box instead would tile into VRAM eight tiles to a row regardless and
        // draw a diagonal smear, which is not a subtle failure but is a silent
        // one until someone looks at the screen.
        EXPECT_EQ(GetSmolFrameSize(anim->frames), MON_PIC_SIZE);
        EXPECT_EQ(BW_FRAME_SIZE(anim), MON_PIC_SIZE);

        // The table and the container have to agree on the frame count: the
        // table is what the runtime bounds its sequence against, the container
        // is what it indexes.
        EXPECT_EQ(GetSmolFrameCount(anim->frames), anim->frameCount);

        for (u32 s = 0; s < anim->seqLength; s++)
        {
            EXPECT(anim->seq[s].frame < anim->frameCount);
            // A hold of zero would underflow the tick, which stores hold - 1
            // into a u8 and would sit on one frame for 255 video frames.
            EXPECT(anim->seq[s].hold >= 1);
        }
    }
}

SINGLE_BATTLE_TEST("BW anim: Roxanne's Pokemon animate through a battle")
{
    enum Species species;
    PARAMETRIZE { species = SPECIES_GEODUDE; }
    PARAMETRIZE { species = SPECIES_NOSEPASS; }
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(species);
    } WHEN {
        TURN { }
        TURN { }
    }
}

// The worst case the design is budgeted against: two opponent sprites decoding,
// which is the most that can ever animate at once. Four species are on the team
// so switch-in resets are exercised too.
DOUBLE_BATTLE_TEST("BW anim: Tate and Liza animate two sprites at once")
{
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_CLAYDOL);
        OPPONENT(SPECIES_XATU);
        OPPONENT(SPECIES_LUNATONE);
        OPPONENT(SPECIES_SOLROCK);
    } WHEN {
        TURN { }
        TURN { }
    }
}

// A species with no entry has to keep its stock sprite rather than fall into
// the animated path with a NULL anim - that is what lets the roster land a few
// species at a time instead of as one commit.
SINGLE_BATTLE_TEST("BW anim: a species with no entry is left alone")
{
    GIVEN {
        ASSUME(GetBwAnim(SPECIES_WOBBUFFET) == NULL);
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { }
    }
}
