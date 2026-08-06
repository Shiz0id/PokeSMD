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

// Bytes one decoded frame occupies, for sizing the ring buffer.
#define BW_FRAME_SIZE(anim) ((anim)->width * (anim)->height / 2)

#endif // GUARD_ROGUE_BW_ANIM_H
