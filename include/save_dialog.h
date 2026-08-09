#ifndef GUARD_SAVE_DIALOG_H
#define GUARD_SAVE_DIALOG_H

#include "global.h"

void SaveDialog_InitSave(void);
void SaveDialog_InitBattlePyramidRetire(void);
void Task_SaveDialogHandleBattlePyramidRetire(u8 taskId);
void Task_SaveDialogHandleSave(u8 taskId);


#endif  // GUARD_SAVE_DIALOG_H
