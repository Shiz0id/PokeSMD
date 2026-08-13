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

// How a container's palette becomes a SHINY palette.
//
// A container carries one palette and it is the gif's, so a shiny animated
// Pokemon showed normal colours - the one thing the sprite feature could not
// express. There is no shiny gif set to emit from, and shipping a second
// palette per container would be colour data the ROM already holds: every
// species carries gSpeciesInfo[species].shinyPalette for its stock sprite.
//
// So what ships is a PERMUTATION, not a palette. For each of the container's
// 16 slots, the index of the stock shiny entry that slot takes. Eight bytes a
// container, two slots to a byte, LOW NIBBLE FIRST, and the runtime rebuilds
// the palette with sixteen indexed loads and no arithmetic.
//
// IT IS FAITHFUL RATHER THAN APPROXIMATE, and that is not luck. This build has
// P_GBA_STYLE_SPECIES_GFX FALSE, so the shipped sprites are the Gen 4/5 art -
// the same generation the BW gifs were ripped from. 80.9% of container slots
// hold a colour appearing VERBATIM in the stock normal palette, and 385 of the
// 772 containers match on every slot they use, so those take the official
// shiny palette exactly rather than something derived from it.
//
// The map is chosen at emit time by tools/rogue/emit_bw_shiny.py as a
// minimum-cost bijection in OkLab. Read that file before changing any of it:
// three other formulations were built and rejected on the rendered result, and
// the reasons are recorded there so they are not tried again.
struct BwShinyMap
{
    u16 species;
    u8 map[8];
};

// NULL when the species has no animation for that side - callers fall back to
// the stock pic. Front and back are separate tables rather than one table with
// a flag, because a species may have both and a single species-sorted table
// would then hold duplicate keys, which is what a binary search cannot resolve.
//
// Most species have a front and no back: the 1,253 gif set this is built from
// is front sprites only. Backs come from elsewhere, one at a time.
const struct BwAnim *GetBwAnim(u16 species, bool32 isBack);

// Write this species' SHINY palette into dst, or return FALSE and leave dst
// untouched. FALSE means the species has no container or no map for that side,
// and the caller keeps the normal palette - which is what every shiny did
// before this existed, so a miss degrades to the old behaviour rather than to
// a wrong one.
//
// Public rather than static because it is the only part of the shiny path a
// headless test can reach. The battle test compares what a battler's OBJ
// palette actually holds against what this returns, which is the one assertion
// that spans the emitted map, the bisect and the load - and none of the three
// is observable on its own.
bool32 RogueBwAnim_BuildShinyPalette(u16 dst[16], u16 species, bool32 isBack);

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
