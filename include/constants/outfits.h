#ifndef GUARD_CONSTANTS_OUTFITS_H
#define GUARD_CONSTANTS_OUTFITS_H

// An outfit is the player's whole visual identity in one table row: overworld
// avatar, field-move animations, battle trainer pics, and the head icons the
// region map and frontier pass draw. Adding one is a table entry in
// src/data/outfit_tables.h, never a code edit.
//
// OUTFIT_USUAL_GREEN IS THE VANILLA LOOK, and its row is built out of the
// PLAYER_AVATAR_GFX_* macros rather than raw OBJ_EVENT_GFX_* ids. Those macros
// already carry the IS_FRLG ternary, so the default outfit stays correct on
// both builds for free and a regression in the default look is not reachable
// without editing the macros themselves.

// ScrCmd_getoutfitstatus
#define OUTFIT_CHECK_FLAG 0
#define OUTFIT_CHECK_USED 1
// ScrCmd_toggleoutfit
#define OUTFIT_TOGGLE_UNLOCK 0
#define OUTFIT_TOGGLE_LOCK 1
// BufferOutfitStrings
#define OUTFIT_BUFFER_NAME 0
#define OUTFIT_BUFFER_DESC 1

// Note: upstream carries a GFX/PAL pair of macros here, selecting which half of
// a { graphics, palette } pair one getter should return. They are gone in two
// ways: the getter is split into GetPlayerHeadGfx and GetPlayerHeadPal, so
// there is no flag to name - and three-letter macros called GFX and PAL, in a
// header reachable from nearly every translation unit in the tree, are a
// collision waiting for whoever next writes a local called gfx.

#define OUTFIT_NONE          0
#define OUTFIT_USUAL_GREEN   1
#define OUTFIT_UNUSUAL_RED   2
#define OUTFIT_KANTO_CLASSIC 3
#define OUTFIT_JOHTO         4
#define OUTFIT_SINNOH        5
#define OUTFIT_COUNT         6

// Inclusive. OUTFIT_NONE is not a wearable outfit - it is the zeroed row that
// an out-of-range id lands on - so menu iteration starts at OUTFIT_BEGIN.
#define OUTFIT_BEGIN OUTFIT_USUAL_GREEN
#define OUTFIT_END   OUTFIT_SINNOH

// THE UNLOCK BITS ARE ROUND_BITS_TO_BYTES(OUTFIT_COUNT) OF AN EIGHT-BYTE
// FILLER, and filler_92 is sized against that, so growing this past a byte
// boundary is a negative array size rather than a silent shift of every
// SaveBlock2 field after it. Five outfits is still one byte; the ninth is
// where that sizing stops being free.
#define DEFAULT_OUTFIT OUTFIT_USUAL_GREEN

#endif //! GUARD_CONSTANTS_OUTFITS_H
