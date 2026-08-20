#ifndef GUARD_ROGUE_PORTRAIT_H
#define GUARD_ROGUE_PORTRAIT_H

#include "constants/species.h"

// A Mystery Dungeon style speaker portrait: a framed box sitting above the left
// end of the field message box, holding the face of whoever is talking.
//
// THE BOX IS SIZED BY THE ART, NOT THE OTHER WAY AROUND. An entry names its size
// in tiles and the frame is built around it, so a 40x40 PMD portrait (5x5 tiles)
// and the 64x64 front pic stand-in (8x8 tiles) both work with no geometry edit.
// The box always bottoms out at PORTRAIT_BOX_BOTTOM_ROW, growing upwards.
//
// WHY ROW 12 IS THE FLOOR: rows 13-14 are the namebox (tilemapTop 13, height 2 in
// field_name_box.c) and rows 14-19 are the dialogue frame (tilemapTop 15, height 4
// plus a border row each side, in menu.c sStandardTextBox_WindowTemplates). A box
// reaching row 13 would be overwritten by whichever of those drew last, which is
// order-dependent and therefore intermittent.
//
// THE ART IS ALWAYS BLITTED INTO A 64x64 SPRITE. There is no OBJ size 40, so a
// smaller portrait is padded into the top-left of a 64x64 sheet rather than given
// its own shape. One OAM shape, one VRAM footprint, one teardown path.

#define PORTRAIT_BOX_BOTTOM_ROW 12
#define PORTRAIT_BOX_LEFT_COL   0

// Sprite tile tag. Tile tags are a separate namespace from palette tags; the
// palette tag lives in constants/field_effects.h with the other field ones so
// every field palette tag is visible in one file.
#define PORTRAIT_TILE_TAG 0x1017

// THE FACE ORDER IS THE SPRITE REPOSITORY'S, NOT THE FOLLOWER SYSTEM'S. A PMD
// portrait sheet is a 5-wide grid in this fixed order, so importing one is a
// straight cut with no remapping, and a sheet that gains a face later drops into
// the slot it already belongs in. The engine's own FOLLOWER_EMOTION_* is a
// different, shorter list; sFollowerEmotionToFace in rogue_portrait.c is the
// bridge between them and is deliberately the only place the two meet.
enum RoguePortraitFace
{
    PORTRAIT_FACE_NORMAL = 0,
    PORTRAIT_FACE_HAPPY,
    PORTRAIT_FACE_PAIN,
    PORTRAIT_FACE_ANGRY,
    PORTRAIT_FACE_WORRIED,
    PORTRAIT_FACE_SAD,
    PORTRAIT_FACE_CRYING,
    PORTRAIT_FACE_SHOUTING,
    PORTRAIT_FACE_TEARY_EYED,
    PORTRAIT_FACE_DETERMINED,
    PORTRAIT_FACE_JOYOUS,
    PORTRAIT_FACE_INSPIRED,
    PORTRAIT_FACE_SURPRISED,
    PORTRAIT_FACE_DIZZY,
    PORTRAIT_FACE_SPECIAL0,
    PORTRAIT_FACE_SPECIAL1,
    PORTRAIT_FACE_SIGH,
    PORTRAIT_FACE_STUNNED,
    PORTRAIT_FACE_SPECIAL2,
    PORTRAIT_FACE_SPECIAL3,
    PORTRAIT_FACE_COUNT,
};

// A portrait is 4bpp art plus its own 16-colour palette.
//
// EVERY FACE CARRIES ITS OWN PALETTE, and that is not a convenience. Across one
// species' sheet the union of colours runs past eighty, so there is no shared
// 16-colour palette to be had. Each face is exactly 15 colours, which is what
// leaves index 0 free — a GBA sprite reads index 0 as transparent, and these
// portraits are fully opaque because the background is part of the art.
struct RoguePortraitImage
{
    const u32 *gfx; // widthTiles * heightTiles * TILE_SIZE_4BPP bytes, tile order
    const u16 *pal; // 16 colours, index 0 unused
};

struct RoguePortraitEntry
{
    u8 widthTiles;
    u8 heightTiles;
    // Indexed by enum RoguePortraitFace. A NULL slot is a face this species does
    // not have; selection skips them, so a partial sheet is usable as-is.
    const struct RoguePortraitImage *faces;
    u8 faceCount;
};

// Sparse, species-indexed. A zeroed entry means "no portrait art", and the
// stand-in front pic is used instead.
extern const struct RoguePortraitEntry gRoguePortraits[NUM_SPECIES];

// Name the mon that is about to speak. Takes effect on the next field message.
void RoguePortrait_SetSpeakerMon(struct Pokemon *mon, u32 emotion);

// Draw the box and portrait for the speaker already set. Called from the field
// message box task once the frame gfx are loaded. Idempotent: re-drawing the
// same speaker does nothing, so a multi-message conversation does not rebuild
// the sprite between pages.
void RoguePortrait_Draw(void);

// Tear the portrait down and forget the speaker. Called from HideFieldMessageBox,
// which every script path reaches through release or closemessage.
void RoguePortrait_Hide(void);

// Forget any speaker and any live handles without touching hardware. Called from
// InitStandardTextBoxWindows, which runs after the sprite system has already been
// reset, so there is nothing left to free by then.
void RoguePortrait_ResetState(void);

bool32 RoguePortrait_IsShowing(void);

#endif // GUARD_ROGUE_PORTRAIT_H
