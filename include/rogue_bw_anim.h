#ifndef GUARD_ROGUE_BW_ANIM_H
#define GUARD_ROGUE_BW_ANIM_H

// BW-style animated battle sprites.
//
// A species with an entry here plays a frame sequence instead of the stock two
// frames. One without keeps the stock sprite, so a partial roster is always a
// valid build - which is what lets this land species by species rather than as
// one 400-species commit.
//
// The frames are held COMPRESSED in ROM and decoded one at a time, not held
// decompressed. That is the whole design and it follows from one measurement:
// resident frames cost 2048 x frames x 4 battlers out of a 113 KB heap, so
// eight frames is 56% of it and the median species needs 26. Streaming holds
// EWRAM at roughly today's figure whatever the frame count, and spends CPU
// instead - a decode is ~48,150 cycles, 17.1% of a video frame, against a
// measured battle steady state of 8%.

// Build-time kill switch for the BACK animations only, and FALSE is the
// shipping setting: animated back sprites are off for now. The player side
// keeps the stock two-frame back pic while the opponent still animates. It
// also isolates a fault to the back assets or the runtime, since TRUE restores
// the player side without touching the front roster.
//
// FALSE REMOVES THE BACK DATA FROM THE BUILD - it is not merely a lookup that
// misses. Every back declaration, sequence and the sBwAnimsBack table itself
// are inside #if ROGUE_BW_ANIM_BACK in data/rogue_bw_anim.h, so the 386 back
// containers are never compiled.
//
// It has to work that way, because --gc-sections cannot reach them however
// carefully they are left unreferenced: -ffunction-sections -fdata-sections
// are only added under LTO (the Makefile has LTO ?= 0), and without them every
// const in rogue_bw_anim.c shares one .rodata section that sBwAnims keeps
// alive. An earlier note here claimed the linker collected 87,432 B on this
// switch; it cannot, and it did not.
//
// What does NOT go away is build time. tools/preproc expands INCGFX before the
// C preprocessor runs and does not evaluate #if, so the back pixels are still
// inlined into the intermediate and only then discarded. Re-run
// emit_bw_anim.py without the back sprites if the build, rather than the ROM,
// is what you are trying to move.
#ifndef ROGUE_BW_ANIM_BACK
#define ROGUE_BW_ANIM_BACK FALSE
#endif

struct BwAnimStep
{
    u8 frame;   // index into the species' frame pool
    u8 hold;    // VIDEO FRAMES to hold it, converted at emit time from the
                // source gif's milliseconds so nothing divides at runtime
};

struct BwAnim
{
    u16 species;
    const u32 *frames;              // one smol blob of frameCount frames
    const u16 *palette;
    const struct BwAnimStep *seq;
    u8 frameCount;
    u8 seqLength;
    u8 width, height;               // pixels, always whole tiles
};

// NULL when the species has no animation for that side - callers fall back to
// the stock pic. Front and back are separate tables rather than one table with
// a flag, because a species may have both and a single species-sorted table
// would then hold duplicate keys, which is what a binary search cannot resolve.
//
// Most species have a front and no back: the 1,253 gif set this is built from
// is front sprites only. Backs come from elsewhere, one at a time.
const struct BwAnim *GetBwAnim(u16 species, bool32 isBack);

// Bytes one decoded frame occupies. Always MON_PIC_SIZE - every battle sprite
// is 64x64 and the frames are padded to it - but derived rather than assumed so
// a species emitted at the wrong size fails visibly instead of tiling askew.
#define BW_FRAME_SIZE(anim) ((anim)->width * (anim)->height / 2)

// Called from BattleLoadMonSpriteGfx, after it has loaded the stock pic and
// palette. Takes over both when the species has an animation, and clears any
// previous one when it does not - which is what makes it correct on switch-in
// AND on transform, since that function is the hook for both.
// isShiny and personality are carried only to pick the colour variant, which
// has to be applied to the animation's OWN palette -- see the call site.
void RogueBwAnim_OnLoadSprite(u32 battler, u16 species, bool32 isShiny, u32 personality);

// Called from BattleMainCB2. Advances every animating battler by one video
// frame. Safe to call when nothing is animating, and before sprites exist.
void RogueBwAnim_Tick(void);

// Called from FreeMonSprite when a battler's mon sprite is destroyed. Drops the
// latched sprite id, because a sprite id is a SLOT NUMBER and the slot is about
// to be handed to something else - the next mon, or the trainer sprite at the
// end of a battle. Without this the latch keeps naming a slot it no longer owns.
void RogueBwAnim_OnSpriteFreed(u32 battler);

// Called from FreeMonSpritesGfx. Releases the chunk buffers.
void RogueBwAnim_Free(void);

// Whether a frame published now would reach VRAM for this battler. For tests -
// a frozen sprite is not otherwise observable, because the tick and the buffer
// writes carry on exactly as normal and only the VRAM copy stops.
bool32 RogueBwAnim_WouldPublish(u32 battler);

#endif // GUARD_ROGUE_BW_ANIM_H
