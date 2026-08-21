#ifndef GUARD_OUTFIT_H
#define GUARD_OUTFIT_H

#include "constants/outfits.h"

// The outfit DATA layer: what the player is wearing and what art goes with it.
//
// DELIBERATELY SEPARATE FROM outfit_menu.h. A dozen files need to ask which
// trainer pic or head icon to draw - battle controllers, the region map, the
// frontier pass, the trainer card, decoration - and none of them has any
// business pulling in a menu's task ids and window templates to do it.
// Upstream puts both in one header and every consumer includes the menu.

// One 16x16 4bpp head, one sprite per gender.
#define REGION_MAP_HEAD_SIZE 0x80

// Clamps to DEFAULT_OUTFIT. PURE - it does not write the save. Upstream's
// equivalent repairs gSaveBlock2Ptr->currOutfitId from inside a graphics
// getter, which makes a screen redraw a save write.
u8 SanitizeOutfitId(u8 outfitId);

// The current outfit, already sanitized.
u8 GetCurrentOutfitId(void);

u16 GetPlayerTrainerPicIdByOutfitAndGender(u8 outfitId, u8 gender);
u16 GetPlayerTrainerPicId(void);

// The secret-base decorating sprite. Not a player avatar state - decoration.c
// creates a standalone sprite rather than re-graphicsing the avatar - so it
// gets a helper here instead of a SetPlayerAvatar* in field_player_avatar.c.
u16 GetPlayerDecoratingGfxId(void);

// The region map head for the outfit and gender currently worn. Both halves
// come out of the table - see the note on struct Outfit's iconsRM for why the
// palette is not derived from the overworld sprite. The frontier pass head is
// NOT outfit-aware yet; that is recorded on the struct too.
const u16 *GetPlayerHeadGfx(void);
const u16 *GetPlayerHeadPal(void);

bool32 IsOutfitUnlocked(u16 outfitId);
void UnlockOutfit(u16 outfitId);
void LockOutfit(u16 outfitId);
void ToggleOutfit(u16 outfitId);
bool32 IsPlayerWearingOutfit(u16 outfitId);
u32 GetOutfitPrice(u16 outfitId);
void ResetOutfitData(void);

#endif // GUARD_OUTFIT_H
