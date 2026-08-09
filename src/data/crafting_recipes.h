#ifndef GUARD_CRAFTING_RECIPES_H
#define GUARD_CRAFTING_RECIPES_H

#include "craft_logic.h"
#include "constants/flags.h"

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

static const struct CraftRecipeList gCraftRecipes[ITEMS_COUNT] =
{
    [ITEM_ANTIDOTE] =
    {
        .recipes = (const struct CraftRecipe[])
        {
            {
                .pattern =
                {
                    { ITEM_POTION },
                    { ITEM_PECHA_BERRY },
                },
                .resultQuantity = 3,
                .unlockFlag = 0,
            },
        },
        .count = 1,
    },
    [ITEM_POTION] =
    {
        .recipes = (const struct CraftRecipe[])
        {
            {
                .pattern =
                {
                    { ITEM_ORAN_BERRY, },
                    { ITEM_FRESH_WATER, },
                },
                .resultQuantity = 1,
                .unlockFlag = 0,
            },
        },
        .count = 1,
    },
    [ITEM_SUPER_POTION] =
    {
        .recipes = (const struct CraftRecipe[])
        {
            {
                .pattern =
                {
                    { ITEM_POTION, ITEM_POTION, ITEM_POTION },
                },
                .resultQuantity = 2,
                .unlockFlag = FLAG_ITEM_ROUTE_102_POTION,
            },
            {
                .pattern =
                {
                    { ITEM_ORAN_BERRY, ITEM_ORAN_BERRY },
                    { ITEM_FRESH_WATER },
                },
                .resultQuantity = 2,
                .unlockFlag = 0,
            },
        },
        .count = 2,
    },
};

static const u16 gCraftRecipeCount = ARRAY_COUNT(gCraftRecipes);

#endif // GUARD_CRAFTING_RECIPES_H
