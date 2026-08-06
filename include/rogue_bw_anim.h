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

// NULL when the species has no animation - callers fall back to the stock pic.
const struct BwAnim *GetBwAnim(u16 species);

// Bytes one decoded frame occupies. Always MON_PIC_SIZE - every battle sprite
// is 64x64 and the frames are padded to it - but derived rather than assumed so
// a species emitted at the wrong size fails visibly instead of tiling askew.
#define BW_FRAME_SIZE(anim) ((anim)->width * (anim)->height / 2)

// Called from BattleLoadMonSpriteGfx, after it has loaded the stock pic and
// palette. Takes over both when the species has an animation, and clears any
// previous one when it does not - which is what makes it correct on switch-in
// AND on transform, since that function is the hook for both.
void RogueBwAnim_OnLoadSprite(u32 battler, u16 species);

// Called from BattleMainCB2. Advances every animating battler by one video
// frame. Safe to call when nothing is animating, and before sprites exist.
void RogueBwAnim_Tick(void);

// Called from FreeMonSpritesGfx. Releases the chunk buffers.
void RogueBwAnim_Free(void);

#endif // GUARD_ROGUE_BW_ANIM_H
