#ifndef GUARD_OUTFIT_H
#define GUARD_OUTFIT_H

#include "constants/outfits.h"
#include "constants/global.h"

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

// WHAT THE NEW GAME PICKER STEPS: TWO AXES, TWO CONTROLS. L and R step the
// LOOK, SELECT steps the IDENTITY, and neither control can see the other
// field, so all nine pairings the model permits are reachable.
//
// THIS REPLACED A CURATED LIST OF (identity, look) PAIRS, and the list is
// worth knowing about because its shape was the trap. Five stops - BOY, GIRL,
// ENBY, ENBY/BOY, ENBY/GIRL - gave the choice of sprite to the androgynous
// stop and to no other, which is the sentence these two fields exist to avoid:
// it told a masculine player that their identity picks their sprite while
// telling an androgynous one that it does not. The two axes were also only
// legible in the two stops that spelled both halves out with a slash; the
// other three read as one word and hid the fact that there was a second thing
// being chosen at all. And it multiplied - a fourth look or a fourth identity
// is a row per combination on a single button, not a row in a table.
//
// THE TABLES BELOW ARE LABELS AND NOTHING ELSE. No art is indexed through
// them and no pairing is legal or illegal because of them; they are the words
// the picker prints.
extern const u8 *const gPlayerLookNames[PLAYER_LOOK_COUNT];
extern const u8 *const gPlayerIdentityNames[PLAYER_GENDER_COUNT];

// The word for what the save holds right now. Both clamp, because a save
// written before this feature can hold anything in either byte.
const u8 *GetPlayerLookName(void);
const u8 *GetPlayerIdentityName(void);

// Step one axis by +1 or -1, wrapping over that axis's own count.
//
// EACH ONE WRITES EXACTLY ONE FIELD. StepPlayerLook may not touch
// playerGenderIdentity and StepPlayerIdentity may not touch playerGender:
// crossing them builds cleanly and looks right on the picker, and the save
// then disagrees with the sprite the player is looking at.
void StepPlayerLook(s32 delta);
void StepPlayerIdentity(s32 delta);

bool32 IsOutfitUnlocked(u16 outfitId);
void UnlockOutfit(u16 outfitId);
void LockOutfit(u16 outfitId);
void ToggleOutfit(u16 outfitId);
bool32 IsPlayerWearingOutfit(u16 outfitId);
u32 GetOutfitPrice(u16 outfitId);
void ResetOutfitData(void);

#endif // GUARD_OUTFIT_H
