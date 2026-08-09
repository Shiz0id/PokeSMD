#ifndef GUARD_CRAFT_LOGIC_H
#define GUARD_CRAFT_LOGIC_H

#include "item.h"

#define CRAFT_ROWS 3
#define CRAFT_COLS 3
#define CRAFT_SLOT_COUNT (CRAFT_ROWS * CRAFT_COLS)

#define CRAFT_SLOT_ROW(slot) ((slot) / CRAFT_COLS)
#define CRAFT_SLOT_COL(slot) ((slot) % CRAFT_COLS)

struct CraftMenuState
{
    struct ItemSlot slots[CRAFT_ROWS][CRAFT_COLS];
    u8 activeSlot;
};

extern struct CraftMenuState gCraftState;

#define gCraftSlots      (gCraftState.slots)
#define gCraftActiveSlot (gCraftState.activeSlot)

void CraftLogic_InitSlots(void);
void CraftLogic_SetSlot(u8 slot, u16 itemId, u16 quantity);
void CraftLogic_SwapSlots(u8 slotA, u8 slotB);

struct CraftRecipeList;
struct CraftRecipe;

const struct CraftRecipe *CraftLogic_GetMatchingRecipe(const struct CraftRecipeList *recipes, u16 recipeCount, u16 *resultItemId);
u16 CraftLogic_GetCraftableQuantity(const struct CraftRecipe *recipe);

u16 CraftLogic_Craft(const struct CraftRecipeList *recipes, u16 recipeCount);
bool8 CraftLogic_CanCraft(const struct CraftRecipeList *recipes, u16 recipeCount);
bool8 CraftLogic_IsAutoCraftEnabled(void);

#endif // GUARD_CRAFT_LOGIC_H
