#include "global.h"
#include "rogue_bw_anim.h"
#include "battle.h"
#include "decompress.h"
#include "malloc.h"
#include "palette.h"
#include "pokemon.h"
#include "sprite.h"
#include "constants/species.h"

#include "data/rogue_bw_anim.h"

// Bisection, not a scan. The table is nine entries today and is meant to reach
// several hundred; a linear scan runs per battler and would be the kind of cost
// that stays invisible until the roster is large enough to make it matter.
//
// emit_bw_anim.py is what guarantees the ordering this depends on, and it
// sorts by the NUMERIC species id read out of the species enum. Sorting by
// name looks equivalent and is not - it puts CLAYDOL before GEODUDE while
// their ids run the other way, and this search would then silently miss them.
const struct BwAnim *GetBwAnim(u16 species, bool32 isBack)
{
    const struct BwAnim *table = isBack ? sBwAnimsBack : sBwAnims;
    u32 lo = 0;
    u32 hi = isBack ? ARRAY_COUNT(sBwAnimsBack) : ARRAY_COUNT(sBwAnims);

    while (lo < hi)
    {
        u32 mid = (lo + hi) / 2;
        u16 got = table[mid].species;

        if (got == species)
            return &table[mid];
        if (got < species)
            lo = mid + 1;
        else
            hi = mid;
    }

    return NULL;
}

// ---------------------------------------------------------------------------
// The battle runtime.
//
// Frames live compressed in ROM and are decoded a CHUNK at a time into a buffer
// held per battler. A chunk is four frames, so three frame changes out of four
// cost only a copy - which is why average CPU is lower than decoding one frame
// at a time would be, not higher.
//
// EITHER SIDE, but usually only the opponent. The 1,253 gif set is front
// sprites with no backs, so a species animates in the player's own slot only if
// a back sprite was sourced separately for it. Where both exist, a double
// battle can have FOUR sprites animating - which the one-decode-per-video-frame
// rule below is what makes affordable, since it caps the cost at one decode
// however many battlers want one.

// EWRAM_DATA, not a plain static: plain statics land in IWRAM, which is the one
// with no headroom - see limits-and-ram.md.
static EWRAM_DATA u8 *sBwChunkBuf[MAX_BATTLERS_COUNT] = {NULL};
static EWRAM_DATA const struct BwAnim *sBwAnim[MAX_BATTLERS_COUNT] = {NULL};

// The sprite id we have already proved is this battler's mon sprite, latched so
// the proof does not have to be repeated against a stamp that does not survive.
// See IsBattlerMonSprite.
static EWRAM_DATA u8 sBwSpriteId[MAX_BATTLERS_COUNT] = {0};

#define BW_NO_CHUNK 0xFF

static void ClearBwState(u32 battler)
{
    struct BattleSpriteInfo *info = &gBattleSpritesDataPtr->battlerData[battler];

    sBwAnim[battler] = NULL;
    sBwSpriteId[battler] = SPRITE_NONE;
    info->bwStep = 0;
    info->bwHold = 0;
    info->bwChunk = BW_NO_CHUNK;
}

// Is gBattlerSpriteIds[battler] really this battler's mon sprite, showing this
// species?
//
// IT IS OFTEN NOT, and assuming otherwise corrupts whatever sprite it does name.
// Two ways it lies. The trainer slide code assigns
// gBattlerSpriteIds[battler] = gBattleStruct->trainerSlideSpriteIds[battler],
// so the id can be a TRAINER's - that is how a Geodude frame ended up drawn
// over Roxanne. And at the moment the pic is loaded the mon sprite does not
// exist yet: BtlController_HandleDrawTrainerPic calls BattleLoadMonSpriteGfx
// and only then CreateSprite, so the id still holds whatever was there before,
// and writing 2 KB to its tiles is what put a band of garbage through the
// player's healthbox.
//
// The engine stamps data[0] and data[2] on a battler's mon sprite the moment it
// creates one, so checking both is the discriminator, and it costs two loads.
// THE STAMP DOES NOT SURVIVE A MON ANIMATION, so it is proof of identity only
// the first time and a latch carries it afterwards. Task_HandleMonAnimation
// zeroes data[0] and data[2..7] while an animation runs and restores them from
// what it saved - but it saves data[0] as oam.paletteNum, and it saves data[2]
// from a sprite whose data another animation may already have cleared. After a
// KO animation, which is the two-frame victory animation played by the winner,
// the opponent's data[2] comes back as 1 rather than its species.
//
// That is a permanent freeze rather than a glitch: the stamp never becomes
// right again on its own, so every later frame is written into the buffer and
// none of them is copied to VRAM. Anything that recreates the sprite fixes it,
// which is why going to the Bag or the party menu appeared to restart the
// animation.
//
// Latching keeps every protection the stamp was there to provide. At load time
// the mon sprite does not exist yet and the latch is cleared, so a stale id is
// still rejected. If gBattlerSpriteIds[battler] is re-pointed at a TRAINER
// sprite - which the slide code really does - it no longer equals the latch and
// no longer matches the stamp, so that is still rejected too. Only the case
// this bug is about, the same sprite we already verified with its scratch data
// since trampled, now passes.
static bool32 IsBattlerMonSprite(u32 battler, u16 species)
{
    u32 spriteId = gBattlerSpriteIds[battler];

    if (spriteId == SPRITE_NONE || spriteId >= MAX_SPRITES)
        return FALSE;

    if (gSprites[spriteId].data[0] == (s16)battler
     && gSprites[spriteId].data[2] == (s16)species)
    {
        sBwSpriteId[battler] = spriteId;
        return TRUE;
    }

    return spriteId == sBwSpriteId[battler];
}

// Put a frame where the engine will draw it.
//
// It goes into BOTH resident slots, and that is not redundancy. A species'
// frontAnimFrames cycles image frames 0 and 1 - ANIMCMD_FRAME(0,30),
// ANIMCMD_FRAME(1,30) - and every one of those issues its own VRAM copy out of
// whichever slot it names. Writing only the slot we request would leave the
// other holding a stale frame for the anim system to draw the moment a mon
// plays its two-frame animation. Filling both means it does not matter which
// one wins, and it costs two 2 KB copies against a 48,000 cycle decode.
//
// The VRAM copy is skipped unless the sprite is verified. Filling the buffer is
// always safe and is all that is needed at load time anyway - the sprite is
// created immediately afterwards and reads it.
static void PublishBwFrame(u32 battler, u16 species, const u8 *frame, u32 size)
{
    enum BattlerPosition position = GetBattlerPosition(battler);
    u8 *dest = gMonSpritesGfxPtr->spritesGfx[position];

    if (dest == NULL)
        return;

    CpuFastCopy(frame, dest, size);
    CpuFastCopy(frame, dest + MON_PIC_SIZE, size);

    if (IsBattlerMonSprite(battler, species))
        RequestSpriteFrameImageCopy(0, gSprites[gBattlerSpriteIds[battler]].oam.tileNum,
                                    gMonSpritesGfxPtr->frameImages[position]);
}

void RogueBwAnim_OnLoadSprite(u32 battler, u16 species)
{
    const struct BwAnim *anim;
    struct BattleSpriteInfo *info;
    u32 chunk, frame;

    ClearBwState(battler);

    // Which side the battler is on decides which table to read, because it
    // decides which sprite the engine will draw. A species with a front entry
    // and no back animates as an opponent and stays static in the player's own
    // slot, which is the usual case.
    anim = GetBwAnim(species, IsOnPlayerSide(battler));
    if (anim == NULL)
        return;

    // AllocUnchecked, NOT Alloc. Alloc calls fatalf when the heap cannot
    // satisfy it and never returns NULL, so a NULL check after it is dead code
    // and a tight battle is a crash rather than a degraded one. This is the
    // allocation most likely to fail: it is the largest thing a battle asks for
    // after the 16 KB sprite buffer, and a double battle can want four.
    if (sBwChunkBuf[battler] == NULL)
        sBwChunkBuf[battler] = AllocUnchecked(GetSmolChunkSize(anim->frames));

    // Out of heap is not worth a crash. Leaving the buffer NULL means the mon
    // keeps the stock two-frame sprite the caller has already loaded, which is
    // exactly the fallback a species with no entry gets.
    if (sBwChunkBuf[battler] == NULL)
        return;

    sBwAnim[battler] = anim;
    info = &gBattleSpritesDataPtr->battlerData[battler];
    info->bwStep = 0;
    info->bwHold = anim->seq[0].hold - 1;

    frame = anim->seq[0].frame;
    chunk = GetSmolFrameChunk(anim->frames, frame);
    DecompressSmolChunk(anim->frames, sBwChunkBuf[battler], chunk);
    info->bwChunk = chunk;

    PublishBwFrame(battler, anim->species,
                   sBwChunkBuf[battler] + GetSmolFrameOffsetInChunk(anim->frames, frame),
                   GetSmolFrameSize(anim->frames));

    // The gif carries its own 15 colours, which are not the ones the shipped
    // sprite uses - so the palette has to be taken over along with the pixels
    // or the mon arrives in the right shape and the wrong colours. This
    // overwrites the LoadPalette the caller just did.
    LoadPalette(anim->palette, OBJ_PLTT_ID(battler), PLTT_SIZE_4BPP);
    LoadPalette(anim->palette, BG_PLTT_ID(8) + BG_PLTT_ID(battler), PLTT_SIZE_4BPP);
}

static void TickBattler(u32 battler, bool32 *decodedThisFrame)
{
    const struct BwAnim *anim = sBwAnim[battler];
    struct BattleSpriteInfo *info = &gBattleSpritesDataPtr->battlerData[battler];
    u32 spriteId = gBattlerSpriteIds[battler];
    u32 nextStep, frame, chunk;

    if (anim == NULL || sBwChunkBuf[battler] == NULL)
        return;
    if (info->invisible || info->behindSubstitute)
        return;
    if (spriteId == SPRITE_NONE || spriteId >= MAX_SPRITES || gSprites[spriteId].invisible)
        return;

    if (info->bwHold > 0)
    {
        info->bwHold--;
        return;
    }

    nextStep = info->bwStep + 1;
    if (nextStep >= anim->seqLength)
        nextStep = 0;

    frame = anim->seq[nextStep].frame;
    chunk = GetSmolFrameChunk(anim->frames, frame);

    if (chunk != info->bwChunk)
    {
        // At most one chunk decode per video frame. A decode is ~48% of a frame
        // and the budget only has room for one on top of a battle's own 8%, so
        // in a double battle the second battler waits. bwHold stays 0, so this
        // retries next frame rather than skipping the step - and a one frame
        // delay is invisible against holds that run 4 to 8 frames.
        if (*decodedThisFrame)
            return;

        DecompressSmolChunk(anim->frames, sBwChunkBuf[battler], chunk);
        info->bwChunk = chunk;
        *decodedThisFrame = TRUE;
    }

    info->bwStep = nextStep;
    info->bwHold = anim->seq[nextStep].hold - 1;
    PublishBwFrame(battler, anim->species,
                   sBwChunkBuf[battler] + GetSmolFrameOffsetInChunk(anim->frames, frame),
                   GetSmolFrameSize(anim->frames));
}

void RogueBwAnim_Tick(void)
{
    bool32 decodedThisFrame = FALSE;

    // Runs from BattleMainCB2, which is the steady state callback - setup goes
    // through CB2_InitBattleInternal, where these pointers are not valid yet.
    if (gBattleSpritesDataPtr == NULL || gBattleSpritesDataPtr->battlerData == NULL
     || gMonSpritesGfxPtr == NULL)
        return;

    for (u32 battler = 0; battler < gBattlersCount; battler++)
        TickBattler(battler, &decodedThisFrame);
}

void RogueBwAnim_Free(void)
{
    for (u32 battler = 0; battler < MAX_BATTLERS_COUNT; battler++)
    {
        if (sBwChunkBuf[battler] != NULL)
        {
            Free(sBwChunkBuf[battler]);
            sBwChunkBuf[battler] = NULL;
        }
        sBwAnim[battler] = NULL;
        sBwSpriteId[battler] = SPRITE_NONE;
    }
}

// Whether a frame published now would actually reach VRAM. Exists so the freeze
// this guards against is testable: the tick keeps running and the buffer keeps
// updating either way, so nothing else about a frozen sprite is observable from
// a test.
bool32 RogueBwAnim_WouldPublish(u32 battler)
{
    if (battler >= MAX_BATTLERS_COUNT || sBwAnim[battler] == NULL)
        return FALSE;

    return IsBattlerMonSprite(battler, sBwAnim[battler]->species);
}
