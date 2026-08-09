#ifndef GUARD_CRAFTING_RECIPES_H
#define GUARD_CRAFTING_RECIPES_H

#include "craft_logic.h"
#include "constants/flags.h"

// RECIPES FOR THIS RUN, not for Hoenn.
//
// Upstream ships three recipes keyed on Route 102's tutorial flag, and the
// commit at the tip of that branch deletes the tutorial -- so the table arrived
// describing content this game does not have. These are written from what a run
// actually holds: berries off the trees on three themes, materials out of the
// ground, and the consumables its own loot table drops.
//
// FOUR RULES, and they are the whole design:
//
// 1. DEPTH IS GATED BY THE INGREDIENT, NEVER BY A FLAG. A recipe wanting a Star
//    Piece cannot be made before floor 55 because nothing drops one before then.
//    Every unlockFlag here is 0 as a result. That is not laziness -- a flag is
//    an id out of a pool three projects have already collided in, it needs
//    setting somewhere, clearing in ResetRun, and keeping in sync with the loot
//    bands it is supposed to mirror. The bands do the job on their own.
//
// 2. CRAFTING MAY COME ONLINE BEFORE THE DROP, BUT NEVER FOR FREE. Getting a
//    Full Restore forty floors before the loot table offers one is the feature,
//    not a bug -- what would break the run is getting it without paying. So
//    every consumable the loot table gates past floor 50 must want at least one
//    material from the scarce end of sLootMaterials, and check_craft_recipes.py
//    enforces exactly that rather than a blanket "never earlier", which the
//    cheap tiers should be allowed to break.
//
// 3. IT CONVERTS ABUNDANCE INTO SCARCITY. Berry trees yield six apiece, four
//    trees a floor, on three themes -- so berries are the one thing a run has
//    too many of, and most of this table is a way to spend them.
//
// 4. THE SHARD BLOCK EXISTS TO CLOSE A REAL GAP. Only Moon and Thunder are
//    buried, deliberately: sBuriedStones is picked uniformly and every entry
//    added divides the rate of the ones already there. The other eight are sold
//    for 1,000 coins each at the Game Corner, which is a lot of Voltorb Flip for
//    a Vulpix. Four shards of a colour is a second route to the same stone that
//    costs the buried odds nothing.
//
// Patterns are shape-matched at any offset in the 3x3 grid, so a single ITEM in
// a row is a one-cell recipe and { A, A } is two side by side.

struct CraftRecipe
{
    u16 pattern[CRAFT_ROWS][CRAFT_COLS];
    u16 resultQuantity;
    u16 unlockFlag; // FLAG_NONE for always unlocked
};

struct CraftRecipeList
{
    const struct CraftRecipe *recipes;
    u8 count;
};

#define CRAFT_ONE(result, qty, ...)                 \
    [result] =                                      \
    {                                               \
        .recipes = (const struct CraftRecipe[])     \
        {                                           \
            {                                       \
                .pattern = __VA_ARGS__,             \
                .resultQuantity = qty,              \
                .unlockFlag = 0,                    \
            },                                      \
        },                                          \
        .count = 1,                                 \
    }

static const struct CraftRecipeList gCraftRecipes[ITEMS_COUNT] =
{
    // ---------------------------------------------------------------- healing
    //
    // Water is the base of the whole line and drops in twos from floor 1, which
    // is what lets this start immediately. Oran retires from the berry table at
    // floor 40 and from the loot table at 30, so the Potion recipe fades on its
    // own exactly as the dropped Potion does.
    CRAFT_ONE(ITEM_POTION, 2, {
        { ITEM_ORAN_BERRY },
        { ITEM_FRESH_WATER },
    }),
    CRAFT_ONE(ITEM_SUPER_POTION, 2, {
        { ITEM_ORAN_BERRY, ITEM_ORAN_BERRY },
        { ITEM_FRESH_WATER },
    }),
    // Sitrus arrives at 25 and never retires, which is the right shape: this is
    // the recipe a mid run leans on.
    CRAFT_ONE(ITEM_HYPER_POTION, 2, {
        { ITEM_SITRUS_BERRY, ITEM_SITRUS_BERRY },
        { ITEM_FRESH_WATER },
    }),
    // Big Pearl gates this to floor 45, five floors past where the loot table
    // starts dropping Hyper Potions.
    CRAFT_ONE(ITEM_MAX_POTION, 2, {
        { ITEM_SITRUS_BERRY, ITEM_SITRUS_BERRY, ITEM_SITRUS_BERRY },
        { ITEM_FRESH_WATER,  ITEM_BIG_PEARL },
    }),
    // Star Piece gates this to 55; the loot table's own Full Restores start at
    // 95, so for forty floors this is the only route to one and it costs the
    // scarcest material in the game plus a Lum.
    CRAFT_ONE(ITEM_FULL_RESTORE, 1, {
        { ITEM_SITRUS_BERRY, ITEM_LUM_BERRY },
        { ITEM_FRESH_WATER,  ITEM_STAR_PIECE },
    }),

    // ---------------------------------------------------------------- status
    //
    // Two of a berry for the cure it matches. These are the cheapest recipes in
    // the table on purpose: the single-status cures retire from the loot table
    // at floor 22, and after that the berry is the only source.
    CRAFT_ONE(ITEM_ANTIDOTE, 2, {
        { ITEM_PECHA_BERRY, ITEM_PECHA_BERRY },
    }),
    CRAFT_ONE(ITEM_PARALYZE_HEAL, 2, {
        { ITEM_CHERI_BERRY, ITEM_CHERI_BERRY },
    }),
    CRAFT_ONE(ITEM_AWAKENING, 2, {
        { ITEM_CHESTO_BERRY, ITEM_CHESTO_BERRY },
    }),
    // One of each of the three, in a row. Lum does the same job as a held berry
    // and is the reason this is a row of three rather than a pair.
    CRAFT_ONE(ITEM_FULL_HEAL, 2, {
        { ITEM_PECHA_BERRY, ITEM_CHERI_BERRY, ITEM_CHESTO_BERRY },
    }),

    // ------------------------------------------------------------------- PP
    //
    // Leppa is the only PP berry and arrives at floor 10. PP is the quiet way a
    // long run dies, and the loot table does not start dropping Ethers until 25.
    CRAFT_ONE(ITEM_ETHER, 2, {
        { ITEM_LEPPA_BERRY, ITEM_LEPPA_BERRY },
    }),
    // Leppa x3 alone is floor 10 against a drop floor of 70, which the check
    // called and which is plainly too strong; the mushroom puts it at 35.
    CRAFT_ONE(ITEM_MAX_ETHER, 1, {
        { ITEM_LEPPA_BERRY,  ITEM_LEPPA_BERRY, ITEM_LEPPA_BERRY },
        { ITEM_BIG_MUSHROOM },
    }),
    CRAFT_ONE(ITEM_ELIXIR, 1, {
        { ITEM_LEPPA_BERRY, ITEM_LEPPA_BERRY },
        { ITEM_FRESH_WATER, ITEM_BIG_MUSHROOM },
    }),

    // --------------------------------------------------------------- revives
    //
    // Heart Scale is the scarcest material (weight 6, floor 30) and both revive
    // recipes want it, because fainting is the thing that ends a run and a
    // craftable revive is the single most dangerous thing this table could make
    // cheap. Two scales for a Max Revive is deliberately a lot of digging.
    CRAFT_ONE(ITEM_REVIVE, 1, {
        { ITEM_HEART_SCALE, ITEM_SITRUS_BERRY },
        { ITEM_FRESH_WATER },
    }),
    // The Big Pearl is a DEPTH GATE, not a cost, and it is here because moving
    // materials to mining took the depth out of the mineral half: rocks are on
    // every floor from the first, so Heart Scale and Star Piece have no band and
    // this was craftable on floor 1 against a drop floor of 80. The pearl is the
    // only buried thing in the recipe and it puts it at 45.
    CRAFT_ONE(ITEM_MAX_REVIVE, 1, {
        { ITEM_HEART_SCALE, ITEM_HEART_SCALE },
        { ITEM_STAR_PIECE,  ITEM_BIG_PEARL },
    }),

    // ------------------------------------------------------- evolution stones
    //
    // Four shards of a colour, in a square. The colours follow the vanilla
    // shard-for-stone trade where one exists, so a player who knows FRLG or ORAS
    // already knows this table.
    CRAFT_ONE(ITEM_FIRE_STONE, 1, {
        { ITEM_RED_SHARD, ITEM_RED_SHARD },
        { ITEM_RED_SHARD, ITEM_RED_SHARD },
    }),
    CRAFT_ONE(ITEM_WATER_STONE, 1, {
        { ITEM_BLUE_SHARD, ITEM_BLUE_SHARD },
        { ITEM_BLUE_SHARD, ITEM_BLUE_SHARD },
    }),
    CRAFT_ONE(ITEM_THUNDER_STONE, 1, {
        { ITEM_YELLOW_SHARD, ITEM_YELLOW_SHARD },
        { ITEM_YELLOW_SHARD, ITEM_YELLOW_SHARD },
    }),
    CRAFT_ONE(ITEM_LEAF_STONE, 1, {
        { ITEM_GREEN_SHARD, ITEM_GREEN_SHARD },
        { ITEM_GREEN_SHARD, ITEM_GREEN_SHARD },
    }),
    // The six stones with no vanilla shard colour take one of each plus the
    // material that fits them, which is also what keeps them behind the four
    // single-colour ones: a mixed square needs the floor to have given you all
    // four, and the extra ingredient sets the depth.
    //
    // Moon Stone is the exception that is NOT gated late, and that is the point:
    // Clefairy is a starter pick whose whole line is built around finding one,
    // and the buried odds put the median first find at floor 14. A four-shard
    // recipe is a way to stop being unlucky, so it wants nothing scarce.
    CRAFT_ONE(ITEM_MOON_STONE, 1, {
        { ITEM_RED_SHARD,    ITEM_BLUE_SHARD },
        { ITEM_YELLOW_SHARD, ITEM_GREEN_SHARD },
    }),
    CRAFT_ONE(ITEM_SUN_STONE, 1, {
        { ITEM_RED_SHARD,    ITEM_YELLOW_SHARD },
        { ITEM_STARDUST,     ITEM_STARDUST },
    }),
    CRAFT_ONE(ITEM_ICE_STONE, 1, {
        { ITEM_BLUE_SHARD, ITEM_BLUE_SHARD },
        { ITEM_PEARL,      ITEM_PEARL },
    }),
    CRAFT_ONE(ITEM_SHINY_STONE, 1, {
        { ITEM_YELLOW_SHARD, ITEM_GREEN_SHARD },
        { ITEM_STAR_PIECE },
    }),
    CRAFT_ONE(ITEM_DUSK_STONE, 1, {
        { ITEM_RED_SHARD,     ITEM_GREEN_SHARD },
        { ITEM_BIG_MUSHROOM },
    }),
    CRAFT_ONE(ITEM_DAWN_STONE, 1, {
        { ITEM_BLUE_SHARD,  ITEM_YELLOW_SHARD },
        { ITEM_BIG_PEARL },
    }),

    // NO MATERIAL-UPGRADE RECIPES, and this is the one design decision here
    // that was made by a check rather than by taste.
    //
    // The obvious sink for early materials is Tiny Mushroom x3 -> Big Mushroom,
    // Pearl x3 -> Big Pearl, Stardust x3 -> Star Piece. Those three shipped in
    // the first draft of this table, and check_craft_recipes.py failed nine
    // recipes on them: every late material became reachable on floor 0, so
    // Full Restore was craftable at 30 against a drop floor of 95 and Max Revive
    // at 30 against 80. The depth gating this whole table rests on was gone, and
    // reading it did not show that -- the ladder is three deep and the shortcut
    // only appears when you compose it.
    //
    // Early materials get their sink from the stone block instead: Stardust
    // feeds the Sun Stone, Pearl the Ice Stone, Big Mushroom the Dusk Stone.
};

static const u16 gCraftRecipeCount = ARRAY_COUNT(gCraftRecipes);

#endif // GUARD_CRAFTING_RECIPES_H
