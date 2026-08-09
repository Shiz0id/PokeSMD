#ifndef GUARD_CRAFT_MENU_UI_H
#define GUARD_CRAFT_MENU_UI_H

#include "global.h"
#include "task.h"

extern const u8 gText_CraftConfirm[];

void CraftMenuUI_Init(void);
void CraftMenuUI_UpdateGrid(void);
void CraftMenuUI_DrawIcons(void);
bool8 CraftMenuUI_HandleDpadInput(void);
void CraftMenuUI_Close(void);
u8 CraftMenuUI_GetCursorPos(void);
void CraftMenuUI_SetCursorPos(u8 pos);
void CraftMenuUI_GetItemIconPos(u8 slot, s16 baseX, s16 baseY, s16 *x, s16 *y);

void CraftMenuUI_DisplayPackUpMessage(u8 taskId, TaskFunc nextTask);
void CraftMenuUI_ShowPackUpYesNo(void);
void CraftMenuUI_ClearPackUpMessage(void);

void CraftMenuUI_DisplayMessage(u8 taskId, const u8 *text, TaskFunc nextTask);
void CraftMenuUI_ShowYesNo(void);
void CraftMenuUI_ClearMessage(void);
void CraftMenuUI_DisplayNoItemsMessage(u8 taskId, TaskFunc nextTask);
void CraftMenuUI_DisplayInvalidMessage(u8 taskId, TaskFunc nextTask);
void CraftMenuUI_DisplayCraftConfirmMessage(u8 taskId, u16 itemId, u16 quantity, TaskFunc nextTask);

// Additional UI features
void CraftMenuUI_ShowActionMenu(void);
void CraftMenuUI_HideActionMenu(void);
s8 CraftMenuUI_ProcessActionMenuInput(void);
void CraftMenuUI_StartSwapMode(void);
void CraftMenuUI_EndSwapMode(void);
bool8 CraftMenuUI_InSwapMode(void);
void CraftMenuUI_RedrawInfo(void);
void CraftMenuUI_PrintInfo(const u8 *text, u8 x, u8 y);
u8 CraftMenuUI_AddQuantityWindow(void);
void CraftMenuUI_PrintQuantity(u16 quantity);
void CraftMenuUI_RemoveQuantityWindow(void);
void CraftMenuUI_DisplayAdjustQtyMessage(u8 taskId, u16 itemId, TaskFunc nextTask);
void CraftMenuUI_ClearAdjustQtyMessage(void);

#endif // GUARD_CRAFT_MENU_UI_H
