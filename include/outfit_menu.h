#ifndef GUARD_OUTFIT_MENU_H
#define GUARD_OUTFIT_MENU_H

// The outfit menu UI only. Everything about what the player is WEARING - the
// trainer pic, the avatar graphics, the lock bits - is in outfit.h, which the
// rest of the tree includes instead of this.

// Self-sufficient on purpose. This compiled before only because every consumer
// happened to include main.h first, which is a thing that stays true right up
// until somebody includes it somewhere new.
#include "main.h" // MainCallback

struct ScriptContext;

void OpenOutfitMenu(MainCallback retCB);
void OpenOutfitMenuForNewGame(MainCallback pickedCB, MainCallback backCB);
void Task_OpenOutfitMenu(u8 taskId);

void BufferOutfitStrings(u8 *dest, u8 outfitId, u8 dataType);
void SetCurrentOutfitGfxIntoVar(struct ScriptContext *ctx);

#endif //! GUARD_OUTFIT_MENU_H
