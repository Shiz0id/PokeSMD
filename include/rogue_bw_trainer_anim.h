#ifndef GUARD_ROGUE_BW_TRAINER_ANIM_H
#define GUARD_ROGUE_BW_TRAINER_ANIM_H

#include "rogue_bw_anim.h"

// BW-style animated TRAINER front pics.
//
// The sister feature to the species animations in rogue_bw_anim.h, sharing
// their container format, their streaming decode and their one-decode-per-
// video-frame budget. A TRAINER_PIC with an entry plays a frame sequence
// instead of its single static pic; one without keeps the stock pic, so a
// partial roster is always a valid build.
//
// WHERE THIS IS ACTUALLY SEEN, because it is not the whole battle. An opponent
// trainer is on screen for the intro (from the slide-in until the ball throw),
// for any mid-battle slide-in message, and for the defeat speech at the end.
// On this branch that first window is where every dungeon boss says "H-H-Help
// me!", which is the moment the feature exists for.
//
// IT IS SIMPLER THAN THE SPECIES SIDE IN ONE IMPORTANT WAY. A mon's sprite has
// to be identified by a stamp that does not survive a mon animation, which is
// why rogue_bw_anim.c carries a latch and a page of reasoning about it. A
// trainer sprite has its own dedicated slot in
// gBattleStruct->trainerSlideSpriteIds[battler], written by nothing else, so
// identity here is a slot check plus an anims-table check.

struct BwTrainerAnim
{
    u16 trainerPic;
    const u32 *frames;              // one smol blob of frameCount frames
    const u16 *palette;
    const struct BwAnimStep *seq;
    u8 frameCount;
    u8 seqLength;
    u8 width, height;               // pixels, always whole tiles
};

// NULL when this trainer pic has no animation - callers keep the stock pic.
const struct BwTrainerAnim *GetBwTrainerAnim(u16 trainerPic);

// Called from DecompressTrainerFrontPic, after it has decompressed the stock
// pic into the position's buffer and loaded the palette. Takes over both when
// this trainer pic has an animation, and stops any previous one when it does
// not - so a battler that draws an unanimated trainer clears the animated one
// that stood there before it.
//
// FRONT PICS ONLY, AND THAT IS THE WHOLE SURFACE. The player's own trainer
// sprite is a BACK pic, which comes from a different accessor with its own
// coordinates and its own multi-frame throw animation; nothing here touches it.
void RogueBwTrainerAnim_OnLoadPic(u32 battler, u16 trainerPic);

// Called from RogueBwAnim_Tick, which owns the shared decode budget. Advances
// every animating trainer by one video frame. `decodedThisFrame` is the budget
// token: at most one chunk decode happens per video frame across BOTH features,
// because a decode is ~31% of a frame and the battle's own steady state is 8%.
void RogueBwTrainerAnim_Tick(bool32 *decodedThisFrame);

// The battler's trainer sprite is being destroyed; the latch must die with it.
//
// A SPRITE ID IS A SLOT NUMBER, NOT AN IDENTITY - the lesson rogue_bw_anim.c
// paid for. trainerSlideSpriteIds keeps naming the freed slot after
// DestroySprite, and the slot is handed straight to the mon that the trainer
// just threw. Without this the tick would keep writing 2 KB into whatever
// claimed it.
void RogueBwTrainerAnim_OnSpriteFreed(u32 battler);

// Stop animating this battler's trainer without freeing the buffer.
//
// Called from RogueBwAnim_OnLoadSprite. A POSITION HAS ONE PIXEL BUFFER AND
// THEREFORE ONE ANIMATION: both features publish into
// gMonSpritesGfxPtr->spritesGfx[position], so a mon loading into a battler must
// evict the trainer that was standing there, and RogueBwTrainerAnim_OnLoadPic
// evicts the mon the same way. Two live animations on one buffer is the
// Geodude-over-Roxanne fault with the roles swapped.
void RogueBwTrainerAnim_Stop(u32 battler);

// Called from FreeMonSpritesGfx. Releases the chunk buffers.
void RogueBwTrainerAnim_Free(void);

// Whether a frame published now would reach VRAM for this battler. For tests -
// a frozen sprite is not otherwise observable, because the tick and the buffer
// writes carry on exactly as normal and only the VRAM copy stops.
bool32 RogueBwTrainerAnim_WouldPublish(u32 battler);

#endif // GUARD_ROGUE_BW_TRAINER_ANIM_H
