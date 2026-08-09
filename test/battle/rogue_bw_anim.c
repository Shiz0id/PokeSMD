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
        const struct BwAnim *anim = GetBwAnim(species[i], FALSE);

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
//
// THIS SPECIES HAS TO BE RE-POINTED EACH TIME THE ROSTER GROWS. It was
// Wobbuffet until gen 2 was emitted, at which point the ASSUME below started
// failing - correctly, and loudly, which is the whole reason it is an ASSUME
// and not an assertion inside the body. Note that an ASSUMPTION_FAIL SKIPS the
// test rather than failing the run, so leaving it pointed at an animated
// species quietly costs the coverage. Patrat is gen 5, which is the last
// generation this build enables and so the last one the roster can reach.
SINGLE_BATTLE_TEST("BW anim: a species with no entry is left alone")
{
    GIVEN {
        ASSUME(GetBwAnim(SPECIES_PATRAT, FALSE) == NULL);
        ASSUME(GetBwAnim(SPECIES_PATRAT, TRUE) == NULL);
        PLAYER(SPECIES_PATRAT);
        OPPONENT(SPECIES_PATRAT);
    } WHEN {
        TURN { }
    }
}

// Back sprites, which are the exception: the gif set has none, so these are
// sourced one at a time. Mewtwo and Celebi are the test pair.
TEST("BW anim: back sprites resolve on the back table only")
{
    static const u16 species[] = { SPECIES_MEWTWO, SPECIES_CELEBI };

    for (u32 i = 0; i < ARRAY_COUNT(species); i++)
    {
        const struct BwAnim *back = GetBwAnim(species[i], TRUE);

        EXPECT(back != NULL);
        if (back == NULL)
            continue;

        EXPECT_EQ(back->species, species[i]);
        EXPECT_EQ(GetSmolFrameSize(back->frames), MON_PIC_SIZE);
        EXPECT_EQ(GetSmolFrameCount(back->frames), back->frameCount);

        // The two tables must not be aliased. This used to assert the front
        // lookup returned NULL, which held only while Mewtwo and Celebi were
        // the only species with backs and no fronts - a roster change made it
        // false without anything being wrong. What actually matters is that a
        // side gets ITS OWN entry, so compare them instead.
        const struct BwAnim *front = GetBwAnim(species[i], FALSE);
        if (front != NULL)
        {
            EXPECT(front != back);
            EXPECT(front->frames != back->frames);
            EXPECT_EQ(front->species, back->species);
        }

        for (u32 s = 0; s < back->seqLength; s++)
        {
            EXPECT(back->seq[s].frame < back->frameCount);
            EXPECT(back->seq[s].hold >= 1);
        }
    }
}

// The true worst case, and one the front-only design said could not happen:
// four sprites animating at once. Mewtwo and Celebi on the player side against
// two of Tate & Liza's four, every one of them decoding out of its own 8 KB
// chunk buffer. What keeps this affordable is that the tick decodes at most one
// chunk per video frame however many battlers want one.
DOUBLE_BATTLE_TEST("BW anim: four sprites animate at once")
{
    GIVEN {
        PLAYER(SPECIES_MEWTWO);
        PLAYER(SPECIES_CELEBI);
        OPPONENT(SPECIES_CLAYDOL);
        OPPONENT(SPECIES_XATU);
        OPPONENT(SPECIES_LUNATONE);
        OPPONENT(SPECIES_SOLROCK);
    } WHEN {
        TURN { }
        TURN { }
    }
}

// A KO animation trashes the winner's sprite scratch data and never restores
// it: Task_HandleMonAnimation saves data[2] from a sprite another animation may
// already have cleared, so the opponent comes back stamped 1 instead of its
// species. That used to freeze the sprite for the rest of the battle - the tick
// carried on and every frame landed in the buffer, but none of them reached
// VRAM. It looked like it recovered on opening the Bag or the party menu,
// because those recreate the sprite and restamp it.
//
// FORCE_MOVE_ANIM is load bearing. Headless swaps sMonAnimFunctions out for
// WaitAnimEnd, which restores cleanly, so without it this test passes against
// the bug it exists to catch.
SINGLE_BATTLE_TEST("BW anim: a KO animation does not freeze the winner")
{
    GIVEN {
        FORCE_MOVE_ANIM(TRUE);
        PLAYER(SPECIES_WOBBUFFET) { HP(1); }
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_GEODUDE);
    } WHEN {
        TURN { MOVE(opponent, MOVE_TACKLE); SEND_OUT(player, 1); }
        TURN { }
    } THEN {
        EXPECT_EQ(RogueBwAnim_WouldPublish(B_POSITION_OPPONENT_LEFT), TRUE);
    }
}

// The other half of the KO case, and the one that shipped TWICE.
//
// A fainted battler's mon sprite is DESTROYED and its slot handed straight to
// whatever comes next - the replacement mon, or the trainer sprite at the end
// of a battle. gBattlerSpriteIds keeps naming that slot and so does the latch,
// so both still agree it is ours long after it stops being. The first time
// this wrote a mon frame into the player's healthbox; the second, after a fix
// that correctly rejected a healthbox but not a trainer, it drew a Geodude
// over Roxanne.
//
// Nothing about sprite CONTENT tells a mon from a trainer - they come from the
// same gMultiuseSpriteTemplate and share frameImages after
// AllocateMonSpritesGfx - so the guard cannot be another test on the sprite.
// It is the semantic one: a fainted battler has no mon sprite to animate.
//
// The companion above asserts the WINNER still publishes. Both are needed:
// stopping too much looks identical to a fix and silently reintroduces the
// freeze, and that half IS discriminating.
//
// THIS HALF IS NOT, AND SAYING SO IS THE POINT. It was broken on purpose -
// the faint guard neutralised - and it still passed. In the test the freed
// slot is never reused, so IsBattlerMonSprite rejects on !inUse whatever the
// guard does; in the GAME the trainer sprite takes that slot, inUse is true,
// and only the faint guard stops the write. The case that actually corrupts
// anything needs a sprite to claim the slot, and nothing headless creates one.
//
// So this is a regression guard for a property worth keeping, not evidence the
// Geodude-over-Roxanne bug is fixed. That evidence only exists on a screen -
// which is what engine-traps.md means by nothing headless knowing which sprite
// the tiles reached.
SINGLE_BATTLE_TEST("BW anim: a fainted battler publishes nothing")
{
    GIVEN {
        FORCE_MOVE_ANIM(TRUE);
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_GEODUDE) { HP(1); }
    } WHEN {
        TURN { MOVE(player, MOVE_TACKLE); }
    } THEN {
        EXPECT_EQ(RogueBwAnim_WouldPublish(B_POSITION_OPPONENT_LEFT), FALSE);
    }
}
