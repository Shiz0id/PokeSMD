#ifndef GUARD_CRAFT_MENU_H
#define GUARD_CRAFT_MENU_H

#include "global.h"

extern bool8 (*gMenuCallback)(void);
extern const u8 gText_CraftSuccess[];
void CraftMenu_SetCraftSuccessVars(u16 resultItemId, u16 resultQty);
void CB2_OpenCraftMenu(void);
void CB2_ReturnToCraftMenu(void);

#endif // GUARD_CRAFT_MENU_H
