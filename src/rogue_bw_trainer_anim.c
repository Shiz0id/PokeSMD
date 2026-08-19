#include "global.h"
#include "rogue_bw_trainer_anim.h"
#include "rogue_bw_anim.h"
#include "battle.h"
#include "data.h"
#include "decompress.h"
#include "malloc.h"
#include "palette.h"
#include "sprite.h"
#include "trainer.h"
#include "constants/trainers.h"

#include "data/rogue_bw_trainer_anim.h"

// Bisection, not a scan, matching GetBwAnim. Nine entries scan fine today; the
// table is meant to reach the fourteen dungeon bosses and then the generated
// trainer pool, and a linear scan runs per battler and stays invisible until
// the roster is large enough to make it matter.
//
// emit_bw_trainer_anim.py guarantees the ordering this depends on, and it sorts
// by the NUMERIC id read out of enum TrainerPicID rather than by name.
const struct BwTrainerAnim *GetBwTrainerAnim(u16 trainerPic)
{
    u32 lo = 0, hi = ARRAY_COUNT(sBwTrainerAnims);

    while (lo < hi)
    {
        u32 mid = (lo + hi) / 2;
        u16 got = sBwTrainerAnims[mid].trainerPic;

        if (got == trainerPic)
            return &sBwTrainerAnims[mid];
        if (got < trainerPic)
            lo = mid + 1;
        else
            hi = mid;
    }

    return NULL;
}

// EWRAM_DATA, not plain statics: plain statics land in IWRAM, which is the one
// with no headroom - see limits-and-ram.md.
static EWRAM_DATA u8 *sChunkBuf[MAX_BATTLERS_COUNT] = {NULL};
static EWRAM_DATA const struct BwTrainerAnim *sAnim[MAX_BATTLERS_COUNT] = {NULL};
static EWRAM_DATA u8 sSpriteId[MAX_BATTLERS_COUNT] = {0};

// The playback cursors, held here rather than in
// gBattleSpritesDataPtr->battlerData[battler] where the species side keeps
// them. Those fields belong to the mon standing in this battler's slot, and a
// trainer sharing them would tie two features together for three bytes each -
// the coupling that made two generated tables drift on the shiny work. They
// also have to be readable when gBattleSpritesDataPtr is not yet valid.
static EWRAM_DATA u8 sStep[MAX_BATTLERS_COUNT] = {0};
static EWRAM_DATA u8 sHold[MAX_BATTLERS_COUNT] = {0};
static EWRAM_DATA u8 sChunk[MAX_BATTLERS_COUNT] = {0};

#define BW_NO_CHUNK 0xFF

static void ClearState(u32 battler)
{
    sAnim[battler] = NULL;
    sSpriteId[battler] = SPRITE_NONE;
    sStep[battler] = 0;
    sHold[battler] = 0;
    sChunk[battler] = BW_NO_CHUNK;
}

void RogueBwTrainerAnim_Stop(u32 battler)
{
    if (battler < MAX_BATTLERS_COUNT)
        ClearState(battler);
}

void RogueBwTrainerAnim_OnSpriteFreed(u32 battler)
{
    if (battler < MAX_BATTLERS_COUNT)
        sSpriteId[battler] = SPRITE_NONE;
}

// Is gBattleStruct->trainerSlideSpriteIds[battler] really this battler's
// trainer sprite?
//
// TWO CHECKS, AND THE SECOND IS THE ONE THAT SEPARATES A TRAINER FROM A MON.
// The slot is freed and reused almost immediately - by the very mon the trainer
// throws - while trainerSlideSpriteIds keeps naming it, so `inUse` alone is not
// enough and the explicit OnSpriteFreed hooks are not enough either if a future
// destroy site is added without one.
//
// `anims` is what discriminates. A trainer sprite is created from
// gMultiuseSpriteTemplate with anims = gAnims_Trainer (see
// SetMultiuseSpriteTemplateToTrainerFront); a mon sprite in the same slot,
// built from the same gMonSpritesGfxPtr->templates[position] and sharing its
// frameImages, gets gAnims_MonPic or the species' own frontAnimFrames. That is
// the check rogue_bw_anim.c could NOT make in the other direction - comparing
// `images` does not reject a trainer, because mon and trainer sprites share
// frameImages - and it works here precisely because it is the trainer we are
// trying to confirm rather than the mon.
static bool32 IsTrainerSprite(u32 battler)
{
    u32 spriteId;

    if (gBattleStruct == NULL)
        return FALSE;

    spriteId = gBattleStruct->trainerSlideSpriteIds[battler];
    if (spriteId == SPRITE_NONE || spriteId >= MAX_SPRITES)
        return FALSE;
    if (spriteId != sSpriteId[battler])
        return FALSE;
    if (!gSprites[spriteId].inUse)
    {
        sSpriteId[battler] = SPRITE_NONE;
        return FALSE;
    }
    if (gSprites[spriteId].anims != gAnims_Trainer)
        return FALSE;

    return TRUE;
}

// Put a frame where the engine will draw it.
//
// ONLY IMAGE SLOT 0, unlike the species path which fills both. A mon's
// frontAnimFrames cycles image frames 0 and 1, so writing one slot would leave
// the other stale for the two-frame animation to draw; gAnims_Trainer is
// { sAnim_GeneralFrame0, sAnim_GeneralFrame0 } and every entry of it is
// ANIMCMD_FRAME(0, 0), so a trainer sprite can only ever name frame 0. Writing
// the second slot would be a 2 KB CpuFastCopy per frame change that nothing
// reads. check_bw_trainer_anim.py asserts that property of gAnims_Trainer, so
// this stops being true loudly rather than as a stale sprite.
//
// The VRAM copy is skipped unless the sprite is verified. Filling the buffer is
// always safe and is all that is needed at load time anyway - the sprite is
// created immediately afterwards and reads it.
static void PublishFrame(u32 battler, const u8 *frame, u32 size)
{
    enum BattlerPosition position = GetBattlerPosition(battler);
    u8 *dest = gMonSpritesGfxPtr->spritesGfx[position];

    if (dest == NULL)
        return;

    CpuFastCopy(frame, dest, size);

    if (IsTrainerSprite(battler))
        RequestSpriteFrameImageCopy(0, gSprites[gBattleStruct->trainerSlideSpriteIds[battler]].oam.tileNum,
                                    gMonSpritesGfxPtr->frameImages[position]);
}

void RogueBwTrainerAnim_OnLoadPic(u32 battler, u16 trainerPic)
{
    const struct BwTrainerAnim *anim;
    u32 chunk, frame, palSlot;

    if (battler >= MAX_BATTLERS_COUNT || gMonSpritesGfxPtr == NULL)
        return;

    ClearState(battler);

    // A POSITION HAS ONE PIXEL BUFFER. Evict any mon animation on this battler
    // before the buffer they share is written - see the header.
    //
    // UNCONDITIONAL, AND BEFORE THE GetBwTrainerAnim MISS BELOW, exactly as
    // RogueBwAnim_OnLoadSprite calls RogueBwTrainerAnim_Stop before its own
    // miss and for the same reason: a trainer with no animation of their own
    // still overwrites the buffer with their STOCK pic. The caller has already
    // decompressed it into gMonSpritesGfxPtr->spritesGfx[position].
    //
    // THIS SAT BELOW THE MISS AND SHIPPED AS A CORRUPTED SCREEN. Only thirteen
    // trainers have an animation, so for every other one - which is all four
    // regions' bosses but Hoenn's - nothing stopped the mon, and RogueBwAnim's
    // tick carried on copying its frames straight over the trainer pic.
    //
    // It stayed invisible because an ordinary win faints the opponent's team
    // first: FreeMonSprite runs, the sprite latch dies, and nothing is
    // animating by the time the trainer slides in for the defeat text. The
    // battle debug's Instant Win skips every faint, so the animations are still
    // live - and a DOUBLE is the worst case, with a live animation in both
    // opponent positions.
    RogueBwAnim_StopForBattler(battler);

    anim = GetBwTrainerAnim(trainerPic);
    if (anim == NULL)
        return;

    // AllocUnchecked, NOT Alloc. Alloc calls fatalf when the heap cannot
    // satisfy it and never returns NULL, so a NULL check after it is dead code
    // and a tight battle is a crash rather than a degraded one.
    if (sChunkBuf[battler] == NULL)
        sChunkBuf[battler] = AllocUnchecked(GetSmolChunkSize(anim->frames));

    // Out of heap is not worth a crash. Leaving the buffer NULL means the
    // trainer keeps the stock pic the caller has already decompressed, which is
    // exactly the fallback a trainer with no entry gets.
    if (sChunkBuf[battler] == NULL)
        return;

    sAnim[battler] = anim;
    sStep[battler] = 0;
    sHold[battler] = anim->seq[0].hold - 1;

    frame = anim->seq[0].frame;
    chunk = GetSmolFrameChunk(anim->frames, frame);
    DecompressSmolChunk(anim->frames, sChunkBuf[battler], chunk);
    sChunk[battler] = chunk;

    PublishFrame(battler,
                 sChunkBuf[battler] + GetSmolFrameOffsetInChunk(anim->frames, frame),
                 GetSmolFrameSize(anim->frames));

    // The gif carries its own 15 colours, which are not the ones the shipped
    // pic uses, so the palette has to be taken over along with the pixels or the
    // trainer arrives in the right shape and the wrong colours.
    //
    // INTO THE TAG'S SLOT, NOT OBJ_PLTT_ID(battler). A trainer pic's palette is
    // loaded by tag through the sprite palette allocator, and the caller reads
    // that allocation back with IndexOfSpritePaletteTag to set oam.paletteNum -
    // so the slot is whichever one the allocator handed out, and writing the
    // battler's own OBJ slot would recolour a mon instead. This runs after
    // DecompressTrainerFrontPic's LoadSpritePaletteWithTag, so the tag resolves.
    palSlot = IndexOfSpritePaletteTag(GetTrainerPicTag(trainerPic, TRUE));
    if (palSlot != 0xFF)
        LoadPalette(anim->palette, OBJ_PLTT_ID(palSlot), PLTT_SIZE_4BPP);
}

static void TickBattler(u32 battler, bool32 *decodedThisFrame)
{
    const struct BwTrainerAnim *anim = sAnim[battler];
    u32 spriteId, nextStep, frame, chunk;

    if (anim == NULL || sChunkBuf[battler] == NULL || gBattleStruct == NULL)
        return;

    spriteId = gBattleStruct->trainerSlideSpriteIds[battler];
    if (spriteId == SPRITE_NONE || spriteId >= MAX_SPRITES || gSprites[spriteId].invisible)
        return;

    // Latch on first sight. The sprite does not exist yet when OnLoadPic runs -
    // DecompressTrainerFrontPic is called BEFORE CreateSprite, the same
    // ordering that put a band of garbage through a healthbox on the species
    // side - so the id cannot be recorded there and is picked up here instead,
    // once trainerSlideSpriteIds names a live sprite carrying gAnims_Trainer.
    if (sSpriteId[battler] != spriteId)
    {
        if (!gSprites[spriteId].inUse || gSprites[spriteId].anims != gAnims_Trainer)
            return;
        sSpriteId[battler] = spriteId;
    }

    if (sHold[battler] > 0)
    {
        sHold[battler]--;
        return;
    }

    nextStep = sStep[battler] + 1;
    if (nextStep >= anim->seqLength)
        nextStep = 0;

    frame = anim->seq[nextStep].frame;
    chunk = GetSmolFrameChunk(anim->frames, frame);

    if (chunk != sChunk[battler])
    {
        // At most one chunk decode per video frame, shared with the species
        // animations. bwHold stays 0, so this retries next frame rather than
        // skipping the step - and a one frame delay is invisible against holds
        // that run 4 to 8 frames.
        if (*decodedThisFrame)
            return;

        DecompressSmolChunk(anim->frames, sChunkBuf[battler], chunk);
        sChunk[battler] = chunk;
        *decodedThisFrame = TRUE;
    }

    sStep[battler] = nextStep;
    sHold[battler] = anim->seq[nextStep].hold - 1;
    PublishFrame(battler,
                 sChunkBuf[battler] + GetSmolFrameOffsetInChunk(anim->frames, frame),
                 GetSmolFrameSize(anim->frames));
}

void RogueBwTrainerAnim_Tick(bool32 *decodedThisFrame)
{
    if (gMonSpritesGfxPtr == NULL || gBattleStruct == NULL)
        return;

    for (u32 battler = 0; battler < MAX_BATTLERS_COUNT; battler++)
        TickBattler(battler, decodedThisFrame);
}

void RogueBwTrainerAnim_Free(void)
{
    for (u32 battler = 0; battler < MAX_BATTLERS_COUNT; battler++)
    {
        if (sChunkBuf[battler] != NULL)
        {
            Free(sChunkBuf[battler]);
            sChunkBuf[battler] = NULL;
        }
        ClearState(battler);
    }
}

// Whether a frame published now would actually reach VRAM. Exists so a frozen
// sprite is testable: the tick keeps running and the buffer keeps updating
// either way, so nothing else about it is observable from a test.
bool32 RogueBwTrainerAnim_WouldPublish(u32 battler)
{
    if (battler >= MAX_BATTLERS_COUNT || sAnim[battler] == NULL)
        return FALSE;

    return IsTrainerSprite(battler);
}
