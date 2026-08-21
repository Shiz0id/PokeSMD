#include "global.h"
#include "data.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "outfit.h"

// See include/outfit.h for why this is not part of outfit_menu.c.

u8 SanitizeOutfitId(u8 outfitId)
{
    if (outfitId == OUTFIT_NONE || outfitId >= OUTFIT_COUNT)
        return DEFAULT_OUTFIT;

    return outfitId;
}

u8 GetCurrentOutfitId(void)
{
    return SanitizeOutfitId(gSaveBlock2Ptr->currOutfitId);
}

u16 GetPlayerTrainerPicIdByOutfitAndGender(u8 outfitId, u8 gender)
{
    return gOutfits[SanitizeOutfitId(outfitId)].trainerPics[gender];
}

u16 GetPlayerTrainerPicId(void)
{
    return GetPlayerTrainerPicIdByOutfitAndGender(gSaveBlock2Ptr->currOutfitId, gSaveBlock2Ptr->playerGender);
}

u16 GetPlayerDecoratingGfxId(void)
{
    return GetPlayerAnimGraphicsIdByOutfitStateIdAndGender(gSaveBlock2Ptr->currOutfitId,
                                                           PLAYER_AVATAR_ANIM_DECORATING,
                                                           gSaveBlock2Ptr->playerGender);
}

const u16 *GetPlayerHeadGfx(void)
{
    return gOutfits[GetCurrentOutfitId()].iconsRM[gSaveBlock2Ptr->playerGender].gfx;
}

const u16 *GetPlayerHeadPal(void)
{
    return gOutfits[GetCurrentOutfitId()].iconsRM[gSaveBlock2Ptr->playerGender].pal;
}

// OUTFIT_COUNT bits, one per outfit, in gSaveBlock2Ptr->outfits.
bool32 IsOutfitUnlocked(u16 outfitId)
{
    if (outfitId >= OUTFIT_COUNT)
        return FALSE;

    return (gSaveBlock2Ptr->outfits[outfitId / 8] >> (outfitId % 8)) & 1;
}

void UnlockOutfit(u16 outfitId)
{
    if (outfitId >= OUTFIT_COUNT)
        return;

    gSaveBlock2Ptr->outfits[outfitId / 8] |= (1 << (outfitId % 8));
}

void LockOutfit(u16 outfitId)
{
    if (outfitId >= OUTFIT_COUNT)
        return;

    gSaveBlock2Ptr->outfits[outfitId / 8] &= ~(1 << (outfitId % 8));
}

void ResetOutfitData(void)
{
    memset(gSaveBlock2Ptr->outfits, 0, sizeof(gSaveBlock2Ptr->outfits));
    UnlockOutfit(DEFAULT_OUTFIT);
    gSaveBlock2Ptr->currOutfitId = DEFAULT_OUTFIT;
}
