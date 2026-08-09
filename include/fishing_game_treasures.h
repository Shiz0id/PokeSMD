#ifndef GUARD_FISHING_GAME_TREASURES_H
#define GUARD_FISHING_GAME_TREASURES_H

#include "item.h"
#include "constants/items.h"

static const u16 sTreasureItems[] =
{
    // Common
    ITEM_POTION,
    ITEM_POKE_BALL,
    ITEM_ANTIDOTE,
    ITEM_X_ATTACK,
    ITEM_X_DEFEND,
    ITEM_REPEL,
    // NOT ITEM_ESCAPE_ROPE, for the reason the rest stop's mart gives at
    // length: RogueDungeonFloor sets allow_escaping and nothing ever calls
    // SetEscapeWarp, so a rope warps the player to a stale destination and out
    // of the run. Substituted rather than deleted - SetFishingTreasureItem
    // reads this table as a sliding window keyed on the rod, so a shorter table
    // moves every tier boundary.
    ITEM_PARALYZE_HEAL,
    ITEM_POKE_DOLL,
    ITEM_FULL_HEAL,
    ITEM_STARDUST,

    // Uncommon
    ITEM_SUPER_POTION,
    ITEM_GREAT_BALL,
    ITEM_REVIVE,
    ITEM_SUPER_REPEL,
    ITEM_ETHER,
    ITEM_PROTEIN,
    ITEM_HP_UP,
    ITEM_MAX_ELIXIR,
    ITEM_NUGGET,
    ITEM_HEART_SCALE,

    // Rare
    ITEM_HYPER_POTION,
    ITEM_ULTRA_BALL,
    ITEM_FULL_RESTORE,
    ITEM_MAX_REPEL,
    ITEM_MAX_REVIVE,
    ITEM_ELIXIR,
    ITEM_RARE_CANDY,
    ITEM_KINGS_ROCK,
    ITEM_LEFTOVERS,
    ITEM_TM_EARTHQUAKE,
};

#endif // GUARD_FISHING_GAME_TREASURES_H
