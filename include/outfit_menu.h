#ifndef GUARD_OUTFIT_MENU_H
#define GUARD_OUTFIT_MENU_H

// The outfit menu UI only. Everything about what the player is WEARING - the
// trainer pic, the avatar graphics, the lock bits - is in outfit.h, which the
// rest of the tree includes instead of this.

void OpenOutfitMenu(MainCallback retCB);
void Task_OpenOutfitMenu(u8 taskId);

void BufferOutfitStrings(u8 *dest, u8 outfitId, u8 dataType);
void SetCurrentOutfitGfxIntoVar(struct ScriptContext *ctx);

#endif //! GUARD_OUTFIT_MENU_H
