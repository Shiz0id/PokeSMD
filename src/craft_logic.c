#include "global.h"
#include "constants/items.h"
#include "config/crafting.h"
#include "craft_logic.h"
#include "data/crafting_recipes.h"
#include "event_data.h"

//---------------------------------------------------------------------------
// Crafting state
//---------------------------------------------------------------------------

EWRAM_DATA struct CraftMenuState gCraftState = {0};

//---------------------------------------------------------------------------
// Initialization and slot management
//---------------------------------------------------------------------------

void CraftLogic_InitSlots(void)
{
    int row, col;
    for (row = 0; row < CRAFT_ROWS; row++)
    {
        for (col = 0; col < CRAFT_COLS; col++)
        {
            gCraftSlots[row][col].itemId = ITEM_NONE;
            gCraftSlots[row][col].quantity = 0;
        }
    }
}

void CraftLogic_SetSlot(u8 slot, u16 itemId, u16 quantity)
{
    if (slot >= CRAFT_SLOT_COUNT)
        return;

    gCraftSlots[CRAFT_SLOT_ROW(slot)][CRAFT_SLOT_COL(slot)].itemId = itemId;
    gCraftSlots[CRAFT_SLOT_ROW(slot)][CRAFT_SLOT_COL(slot)].quantity = quantity;
}

void CraftLogic_SwapSlots(u8 slotA, u8 slotB)
{
    if (slotA >= CRAFT_SLOT_COUNT || slotB >= CRAFT_SLOT_COUNT || slotA == slotB)
        return;

    struct ItemSlot temp = gCraftSlots[CRAFT_SLOT_ROW(slotA)][CRAFT_SLOT_COL(slotA)];
    gCraftSlots[CRAFT_SLOT_ROW(slotA)][CRAFT_SLOT_COL(slotA)] = gCraftSlots[CRAFT_SLOT_ROW(slotB)][CRAFT_SLOT_COL(slotB)];
    gCraftSlots[CRAFT_SLOT_ROW(slotB)][CRAFT_SLOT_COL(slotB)] = temp;
}

//---------------------------------------------------------------------------
// Recipe utilities
//---------------------------------------------------------------------------

static void GetRecipeDimensions(const struct CraftRecipe *recipe, int *rows, int *cols)
{
    int r, c;
    int maxRow = -1;
    int maxCol = -1;

    for (r = 0; r < CRAFT_ROWS; r++)
    {
        for (c = 0; c < CRAFT_COLS; c++)
        {
            if (recipe->pattern[r][c] != ITEM_NONE)
            {
                if (r > maxRow)
                    maxRow = r;
                if (c > maxCol)
                    maxCol = c;
            }
        }
    }

    *rows = maxRow + 1;
    *cols = maxCol + 1;
}

static u16 TryCraftRecipeAt(const struct CraftRecipe *recipe, int baseRow, int baseCol, int patRows, int patCols,
                            struct ItemSlot slots[CRAFT_ROWS][CRAFT_COLS], bool8 consume)
{
    int r, c;
    u16 minQty = 0xFFFF;

    for (r = 0; r < patRows; r++)
    {
        for (c = 0; c < patCols; c++)
        {
            struct ItemSlot *slot = &slots[baseRow + r][baseCol + c];
            u16 itemId = recipe->pattern[r][c];

            if (itemId != ITEM_NONE)
            {
                if (slot->itemId != itemId || slot->quantity == 0)
                    return 0;
                if (slot->quantity < minQty)
                    minQty = slot->quantity;
            }
            else if (slot->itemId != ITEM_NONE)
            {
                // Slot contains an item but the recipe expects it to be empty
                return 0;
            }
        }
    }

    // Ensure all remaining slots on the workbench are empty
    for (r = 0; r < CRAFT_ROWS; r++)
    {
        for (c = 0; c < CRAFT_COLS; c++)
        {
            if (r >= baseRow && r < baseRow + patRows && c >= baseCol && c < baseCol + patCols)
                continue;

            if (slots[r][c].itemId != ITEM_NONE)
                return 0;
        }
    }

    if (minQty == 0 || minQty == 0xFFFF)
        return 0;

    if (consume)
    {
        for (r = 0; r < patRows; r++)
        {
            for (c = 0; c < patCols; c++)
            {
                if (recipe->pattern[r][c] != ITEM_NONE)
                {
                    struct ItemSlot *slot = &slots[baseRow + r][baseCol + c];
                    slot->quantity -= minQty;
                    if (slot->quantity == 0)
                        slot->itemId = ITEM_NONE;
                }
            }
        }
    }

    return minQty;
}

static u16 ApplyRecipe(u16 resultItemId, const struct CraftRecipe *recipe)
{
    int patRows, patCols;
    u16 crafted = 0;

    GetRecipeDimensions(recipe, &patRows, &patCols);

    if (patRows == 0 || patCols == 0)
        return 0;

    bool8 progress;
    do
    {
        int baseRow, baseCol;
        progress = FALSE;

        for (baseRow = 0; baseRow <= CRAFT_ROWS - patRows && !progress; baseRow++)
        {
            for (baseCol = 0; baseCol <= CRAFT_COLS - patCols && !progress; baseCol++)
            {
                u16 sets = TryCraftRecipeAt(recipe, baseRow, baseCol, patRows, patCols, gCraftSlots, TRUE);
                if (sets)
                {
                    crafted += sets;
                    progress = TRUE;
                }
            }
        }
    } while (progress);

    if (crafted)
        AddBagItem(resultItemId, crafted * recipe->resultQuantity);

    return crafted * recipe->resultQuantity;
}

//---------------------------------------------------------------------------
// Public crafting API
//---------------------------------------------------------------------------

const struct CraftRecipe *CraftLogic_GetMatchingRecipe(const struct CraftRecipeList *recipes, u16 recipeCount, u16 *resultItemId)
{
    u16 itemId;

    for (itemId = 0; itemId < recipeCount; itemId++)
    {
        const struct CraftRecipeList *list = &recipes[itemId];
        u8 r;
        for (r = 0; r < list->count; r++)
        {
            const struct CraftRecipe *recipe = &list->recipes[r];
            if (recipe->unlockFlag != 0 && !FlagGet(recipe->unlockFlag))
                continue;
            int patRows, patCols;

            GetRecipeDimensions(recipe, &patRows, &patCols);

            if (patRows == 0 || patCols == 0)
                continue;

            int baseRow, baseCol;
            for (baseRow = 0; baseRow <= CRAFT_ROWS - patRows; baseRow++)
            {
                for (baseCol = 0; baseCol <= CRAFT_COLS - patCols; baseCol++)
                {
                    if (TryCraftRecipeAt(recipe, baseRow, baseCol, patRows, patCols, gCraftSlots, FALSE))
                    {
                        if (resultItemId)
                            *resultItemId = itemId;
                        return recipe;
                    }
                }
            }
        }
    }

    return NULL;
}

u16 CraftLogic_GetCraftableQuantity(const struct CraftRecipe *recipe)
{
    struct ItemSlot temp[CRAFT_ROWS][CRAFT_COLS];
    int patRows, patCols;
    u16 crafted = 0;

    if (recipe->unlockFlag != 0 && !FlagGet(recipe->unlockFlag))
        return 0;

    memcpy(temp, gCraftSlots, sizeof(temp));
    GetRecipeDimensions(recipe, &patRows, &patCols);

    if (patRows == 0 || patCols == 0)
        return 0;

    bool8 progress;
    do
    {
        int baseRow, baseCol;
        progress = FALSE;

        for (baseRow = 0; baseRow <= CRAFT_ROWS - patRows && !progress; baseRow++)
        {
            for (baseCol = 0; baseCol <= CRAFT_COLS - patCols && !progress; baseCol++)
            {
                u16 sets = TryCraftRecipeAt(recipe, baseRow, baseCol, patRows, patCols, temp, TRUE);
                if (sets)
                {
                    crafted += sets;
                    progress = TRUE;
                }
            }
        }
    } while (progress);

    return crafted * recipe->resultQuantity;
}

u16 CraftLogic_Craft(const struct CraftRecipeList *recipes, u16 recipeCount)
{
    u16 itemId;
    const struct CraftRecipe *recipe = CraftLogic_GetMatchingRecipe(recipes, recipeCount, &itemId);

    if (recipe)
        return ApplyRecipe(itemId, recipe);

    return 0;
}

bool8 CraftLogic_CanCraft(const struct CraftRecipeList *recipes, u16 recipeCount)
{
    u16 itemId;
    const struct CraftRecipe *recipe = CraftLogic_GetMatchingRecipe(recipes, recipeCount, &itemId);

    return (recipe != NULL);
}

bool8 CraftLogic_IsAutoCraftEnabled(void)
{
    if (CRAFT_AUTO_CRAFT_MODE == CRAFT_AUTO_CRAFT_ALWAYS)
        return TRUE;
    if (CRAFT_AUTO_CRAFT_MODE == CRAFT_AUTO_CRAFT_AFTER_FLAG)
        return (CRAFT_AUTO_CRAFT_FLAG != 0 && FlagGet(CRAFT_AUTO_CRAFT_FLAG));

    return FALSE;
}
