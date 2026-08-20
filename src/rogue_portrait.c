#include "global.h"
#include "bg.h"
#include "decompress.h"
#include "field_name_box.h"
#include "field_weather.h"
#include "malloc.h"
#include "menu.h"
#include "palette.h"
#include "pokemon.h"
#include "script.h"
#include "sprite.h"
#include "rogue_portrait.h"
#include "data.h"          // struct FollowerMessagePool, which follower_helper.h needs
#include "follower_helper.h" // FOLLOWER_EMOTION_*, which portrait entries are keyed on
#include "constants/field_effects.h"
#include "random.h"

#include "data/rogue_portraits.h"

// The sprite is always 64x64 regardless of how big the art is, so the sheet is
// always this size and smaller art is padded into its top-left corner.
#define PORTRAIT_SHEET_TILES_W 8
#define PORTRAIT_SHEET_TILES_H 8
#define PORTRAIT_SHEET_SIZE    (PORTRAIT_SHEET_TILES_W * PORTRAIT_SHEET_TILES_H * TILE_SIZE_4BPP)

// THE DECOMPRESSION BUFFER IS NOT THE SHEET SIZE, AND SIZING IT THAT WAY FROZE
// THE GAME. A front pic in this build is graphics/pokemon/<species>/anim_front.png,
// which is 64x128 - MAX_MON_PIC_FRAMES is 2, and DecompressDataWithHeaderWram
// writes as many frames as the compressed header says, not as many as the caller
// wants. So LoadSpecialPokePic writes 4096 bytes into whatever it is handed. A
// 2048-byte buffer therefore overran the next heap block's header on EVERY
// portrait, and the freeze was the following Free walking a corrupted list -
// nowhere near the write, and with a clean build.
//
// Only frame 0 reaches VRAM: LoadSpriteSheet copies PORTRAIT_SHEET_SIZE. The
// rest of the buffer exists purely so the decompressor has somewhere legal to
// put the frames nobody asked for. CreatePicSprite in trainer_pokemon_sprites.c
// sizes its own buffer the same way and is the precedent.
#define PORTRAIT_DECOMP_SIZE (MON_PIC_SIZE * MAX_MON_PIC_FRAMES)

// The face follows the speaker's settled FOLLOWER_EMOTION_* through
// sFollowerEmotionToFace. Set to TRUE to show one of the species' own faces at
// random instead, which is what to do while bringing up art for a NEW speaker
// that has no emotion of its own yet - it exercises every face in the sheet.
#define PORTRAIT_RANDOM_FACE FALSE

// The stand-in until real portrait art exists: the species battle front pic,
// which is exactly 64x64 and already present for every species.
#define PORTRAIT_FALLBACK_TILES_W 8
#define PORTRAIT_FALLBACK_TILES_H 8

// Offsets into the dialogue frame gfx, which LoadMessageBoxAndBorderGfx has
// already put at DLG_WINDOW_BASE_TILE_NUM by the time we draw. Named from the
// literals in menu.c WindowFunc_DrawDialogueFrame - reusing them is what makes
// the box cost zero window-tile budget, which matters because BG0 has none left
// (the dialogue window runs 0x194 to 0x200 and the namebox eats downwards).
#define FRAME_TILE_CORNER_OUTER 1
#define FRAME_TILE_CORNER_INNER 3
#define FRAME_TILE_EDGE_TOP     4
#define FRAME_TILE_INNER_RIGHT  5
#define FRAME_TILE_CORNER_RIGHT 6
#define FRAME_TILE_EDGE_LEFT    7
#define FRAME_TILE_FILL         9
#define FRAME_TILE_EDGE_RIGHT   10

#define DLG_TILE(n)  (DLG_WINDOW_BASE_TILE_NUM + (n))
#define DLG_VFLIP(n) (BG_TILE_V_FLIP(0) + DLG_TILE(n))

struct RoguePortraitState
{
    u16 species;
    u32 personality;
    u8 face;      // enum RoguePortraitFace, settled at SetSpeakerMon time
    bool8 isShiny;
    bool8 pending;   // a speaker is set and the next message should draw it
    bool8 showing;   // the sprite and box are live
    bool8 ownsSpeakerName; // we set gSpeakerName and must be the one to clear it
    u8 spriteId;     // MAX_SPRITES when none
    u8 palIndex;     // 0xFF when none
    u8 boxLeft;
    u8 boxTop;
    u8 boxWidth;
    u8 boxHeight;
};

// EWRAM_INIT, not EWRAM_DATA, because the two handles need SENTINELS and not
// zero. A zeroed spriteId is 0, which is a real sprite - on the field usually
// the player - and TearDownPortrait would destroy it. Nothing currently reaches
// a teardown before InitStandardTextBoxWindows has run RoguePortrait_ResetState,
// but that is an argument about call order rather than about this struct, and it
// is one edit away from being wrong.
static EWRAM_INIT struct RoguePortraitState sPortrait =
{
    .spriteId = MAX_SPRITES,
    .palIndex = 0xFF,
};

// The nickname is already in gStringVar1 by the time a follower speaks
// (bufferlivemonnickname runs before getfolloweraction), so the namebox needs no
// storage of its own - it expands this every time it draws.
static const u8 sText_SpeakerNickname[] = _("{STR_VAR_1}");

static const struct OamData sOamData_Portrait =
{
    .shape = SPRITE_SHAPE(64x64),
    .size = SPRITE_SIZE(64x64),
    .priority = 0, // above BG0, which is the window layer the frame is drawn on
};

static const struct SpriteTemplate sSpriteTemplate_Portrait =
{
    .tileTag = PORTRAIT_TILE_TAG,
    .paletteTag = FLDEFF_PAL_TAG_ROGUE_PORTRAIT,
    .oam = &sOamData_Portrait,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy,
};

static void DrawPortraitFrame(u32 left, u32 top, u32 width, u32 height)
{
    u32 pal = DLG_WINDOW_PALETTE_NUM;

    // Top row: outer corner, inner corner, edge run, inner corner, outer corner.
    WriteSequenceToBgTilemapBuffer(0, DLG_TILE(FRAME_TILE_CORNER_OUTER), left, top, 1, 1, pal, 0);
    WriteSequenceToBgTilemapBuffer(0, DLG_TILE(FRAME_TILE_CORNER_INNER), left + 1, top, 1, 1, pal, 0);
    WriteSequenceToBgTilemapBuffer(0, DLG_TILE(FRAME_TILE_EDGE_TOP), left + 2, top, width - 4, 1, pal, 0);
    WriteSequenceToBgTilemapBuffer(0, DLG_TILE(FRAME_TILE_INNER_RIGHT), left + width - 2, top, 1, 1, pal, 0);
    WriteSequenceToBgTilemapBuffer(0, DLG_TILE(FRAME_TILE_CORNER_RIGHT), left + width - 1, top, 1, 1, pal, 0);

    // Body: left edge, interior fill the portrait sits on top of, right edge.
    WriteSequenceToBgTilemapBuffer(0, DLG_TILE(FRAME_TILE_EDGE_LEFT), left, top + 1, 1, height - 2, pal, 0);
    WriteSequenceToBgTilemapBuffer(0, DLG_TILE(FRAME_TILE_FILL), left + 1, top + 1, width - 2, height - 2, pal, 0);
    WriteSequenceToBgTilemapBuffer(0, DLG_TILE(FRAME_TILE_EDGE_RIGHT), left + width - 1, top + 1, 1, height - 2, pal, 0);

    // Bottom row is the top row flipped, exactly as the dialogue frame does it.
    WriteSequenceToBgTilemapBuffer(0, DLG_VFLIP(FRAME_TILE_CORNER_OUTER), left, top + height - 1, 1, 1, pal, 0);
    WriteSequenceToBgTilemapBuffer(0, DLG_VFLIP(FRAME_TILE_CORNER_INNER), left + 1, top + height - 1, 1, 1, pal, 0);
    WriteSequenceToBgTilemapBuffer(0, DLG_VFLIP(FRAME_TILE_EDGE_TOP), left + 2, top + height - 1, width - 4, 1, pal, 0);
    WriteSequenceToBgTilemapBuffer(0, DLG_VFLIP(FRAME_TILE_INNER_RIGHT), left + width - 2, top + height - 1, 1, 1, pal, 0);
    WriteSequenceToBgTilemapBuffer(0, DLG_VFLIP(FRAME_TILE_CORNER_RIGHT), left + width - 1, top + height - 1, 1, 1, pal, 0);
}

// Copy wTiles x hTiles of art into the top-left of a 64x64 sprite sheet. The two
// have different row strides - the art is wTiles wide, the sheet is always 8 -
// so this cannot be one flat copy.
static void BlitArtIntoSheet(u8 *dest, const u8 *src, u32 wTiles, u32 hTiles)
{
    u32 row;

    for (row = 0; row < hTiles; row++)
    {
        CpuCopy16(src + row * wTiles * TILE_SIZE_4BPP,
                  dest + row * PORTRAIT_SHEET_TILES_W * TILE_SIZE_4BPP,
                  wTiles * TILE_SIZE_4BPP);
    }
}

// Fill sheet and palOut for the current speaker, and report the art size in
// tiles. Returns FALSE if this species has nothing to draw.
static bool32 BuildPortraitSheet(u8 *sheet, const u16 **palOut, u32 *wTiles, u32 *hTiles)
{
    // gRoguePortraits is NUM_SPECIES long and the species is party data, so it is
    // bounded here rather than trusted. LoadSpecialPokePic runs SanitizeSpeciesId
    // for the same reason; an out-of-range id below would read a pointer out of
    // whatever follows the table and then dereference it.
    const struct RoguePortraitEntry *entry = (sPortrait.species < NUM_SPECIES)
                                           ? &gRoguePortraits[sPortrait.species]
                                           : NULL;

    if (entry != NULL && entry->faces != NULL && entry->widthTiles != 0 && entry->heightTiles != 0)
    {
        // A real portrait. An absent face falls back to NORMAL rather than
        // drawing nothing, so a partial sheet is usable exactly as imported.
        const struct RoguePortraitImage *image = NULL;

        if (sPortrait.face < entry->faceCount && entry->faces[sPortrait.face].gfx != NULL)
            image = &entry->faces[sPortrait.face];
        else if (entry->faces[PORTRAIT_FACE_NORMAL].gfx != NULL)
            image = &entry->faces[PORTRAIT_FACE_NORMAL];

        if (image == NULL)
            return FALSE;

        if (entry->widthTiles > PORTRAIT_SHEET_TILES_W || entry->heightTiles > PORTRAIT_SHEET_TILES_H)
            return FALSE; // will not fit the 64x64 sprite; refuse rather than overrun it

        BlitArtIntoSheet(sheet, (const u8 *)image->gfx, entry->widthTiles, entry->heightTiles);
        *palOut = image->pal;
        *wTiles = entry->widthTiles;
        *hTiles = entry->heightTiles;
        return TRUE;
    }

    // Stand-in: the battle front pic, which decompresses straight into a 64x64
    // sprite-ordered buffer and so needs no blit.
    LoadSpecialPokePic(sheet, sPortrait.species, sPortrait.personality, TRUE);
    *palOut = GetMonSpritePalFromSpeciesAndPersonality(sPortrait.species, sPortrait.isShiny, sPortrait.personality);
    *wTiles = PORTRAIT_FALLBACK_TILES_W;
    *hTiles = PORTRAIT_FALLBACK_TILES_H;
    return TRUE;
}

static void TearDownPortrait(void)
{
    if (sPortrait.spriteId != MAX_SPRITES)
    {
        DestroySprite(&gSprites[sPortrait.spriteId]);
        sPortrait.spriteId = MAX_SPRITES;
    }

    if (sPortrait.palIndex != 0xFF)
    {
        // Undo the weather exemption for OUR palette only. The global
        // ResetPreservedPalettesInWeather throws the whole override table away,
        // which would un-preserve a field move pic or the start menu icon that
        // had preserved a different slot.
        ResetPaletteColorMapType(sPortrait.palIndex + 16);
        sPortrait.palIndex = 0xFF;
    }

    FreeSpriteTilesByTag(PORTRAIT_TILE_TAG);
    FreeSpritePaletteByTag(FLDEFF_PAL_TAG_ROGUE_PORTRAIT);

    if (sPortrait.showing)
    {
        FillBgTilemapBufferRect(0, 0, sPortrait.boxLeft, sPortrait.boxTop,
                                sPortrait.boxWidth, sPortrait.boxHeight, 0);
        ScheduleBgCopyTilemapToVram(0);
        sPortrait.showing = FALSE;
    }
}

// Which face a settled FOLLOWER_EMOTION_* wears. GetFollowerAction picks one of
// these eleven per line and this is the only place the engine's emotion list and
// the Sprite Repository's face list meet.
//
// EVERY EMOTION MUST APPEAR HERE. A missing row is not a build error - the
// designated initialiser leaves it 0, which is PORTRAIT_FACE_NORMAL - so an
// unmapped emotion silently wears a blank expression forever.
// check_portrait_lifetime.py derives the emotion list from follower_helper.h and
// requires each name, so an emotion added upstream fails rather than defaulting.
//
// TWO EMOTIONS SHARE JOYOUS ON PURPOSE. The face list has no "in love" entry, so
// LOVE and MUSIC - both wholehearted delight - land on the broadest smile there
// is. Seven faces (CRYING, SHOUTING, TEARY_EYED, DETERMINED, DIZZY, STUNNED,
// SPECIAL1) have no emotion pointing at them and are unreachable until dialogue
// carries its own tags. That is a gap in the engine's emotion list, not here.
static const u8 sFollowerEmotionToFace[FOLLOWER_EMOTION_LENGTH] =
{
    [FOLLOWER_EMOTION_HAPPY]    = PORTRAIT_FACE_HAPPY,
    [FOLLOWER_EMOTION_NEUTRAL]  = PORTRAIT_FACE_NORMAL,
    [FOLLOWER_EMOTION_SAD]      = PORTRAIT_FACE_SAD,
    [FOLLOWER_EMOTION_UPSET]    = PORTRAIT_FACE_WORRIED,
    [FOLLOWER_EMOTION_ANGRY]    = PORTRAIT_FACE_ANGRY,
    [FOLLOWER_EMOTION_PENSIVE]  = PORTRAIT_FACE_SIGH,
    [FOLLOWER_EMOTION_LOVE]     = PORTRAIT_FACE_JOYOUS,
    [FOLLOWER_EMOTION_SURPRISE] = PORTRAIT_FACE_SURPRISED,
    [FOLLOWER_EMOTION_CURIOUS]  = PORTRAIT_FACE_INSPIRED,
    [FOLLOWER_EMOTION_MUSIC]    = PORTRAIT_FACE_JOYOUS,
    [FOLLOWER_EMOTION_POISONED] = PORTRAIT_FACE_PAIN,
};

// Pick a face this species actually has, uniformly. Reservoir sampling over the
// non-NULL slots, so it needs no second pass to count them first and a sheet
// with holes in it (Bulbasaur has no SPECIAL0) cannot roll an empty slot.
static u32 PickRandomFace(const struct RoguePortraitEntry *entry)
{
    u32 i, seen = 0, chosen = PORTRAIT_FACE_NORMAL;

    for (i = 0; i < entry->faceCount; i++)
    {
        if (entry->faces[i].gfx == NULL)
            continue;
        seen++;
        if (Random() % seen == 0)
            chosen = i;
    }

    return chosen;
}

void RoguePortrait_SetSpeakerMon(struct Pokemon *mon, u32 emotion)
{
    const struct RoguePortraitEntry *entry;

    if (mon == NULL)
        return;

    sPortrait.species = GetMonData(mon, MON_DATA_SPECIES);
    sPortrait.personality = GetMonData(mon, MON_DATA_PERSONALITY);
    sPortrait.isShiny = IsMonShiny(mon);
    sPortrait.pending = TRUE;

    // THE FACE IS CHOSEN ONCE PER CONVERSATION, HERE, not per draw. Picking in
    // RoguePortrait_Draw would reroll on any redraw, and a portrait that changes
    // expression between pages of one line reads as a bug.
    entry = (sPortrait.species < NUM_SPECIES) ? &gRoguePortraits[sPortrait.species] : NULL;

    if (PORTRAIT_RANDOM_FACE && entry != NULL && entry->faces != NULL)
        sPortrait.face = PickRandomFace(entry);
    else if (emotion < FOLLOWER_EMOTION_LENGTH)
        sPortrait.face = sFollowerEmotionToFace[emotion];
    else
        sPortrait.face = PORTRAIT_FACE_NORMAL;

    // Put the nickname in the namebox, which is the "Sedna:" half of the look.
    gSpeakerName = sText_SpeakerNickname;
    sPortrait.ownsSpeakerName = TRUE;
}

void RoguePortrait_Draw(void)
{
    u8 *sheet;
    const u16 *pal;
    u32 wTiles = 0, hTiles = 0;
    u32 boxWidth, boxHeight, artX, artY;
    u16 tileStart;
    u8 spriteId, palIndex;

    if (!sPortrait.pending)
        return;

    // A signpost message loads different frame gfx into the same base tiles, so
    // the box would be built out of the wrong art. Nothing that uses a portrait
    // uses a signpost, so decline rather than draw a mismatched frame.
    if (gMsgIsSignPost)
        return;

    if (sPortrait.showing)
        return; // already up for this speaker; later pages must not rebuild it

    sheet = AllocZeroed(PORTRAIT_DECOMP_SIZE);
    if (sheet == NULL)
        return;

    if (!BuildPortraitSheet(sheet, &pal, &wTiles, &hTiles))
    {
        Free(sheet);
        return;
    }

    boxWidth = wTiles + 2;
    boxHeight = hTiles + 2;

    // The box grows upwards from its fixed floor, so tall art must not push the
    // top off the screen.
    if (boxHeight > PORTRAIT_BOX_BOTTOM_ROW + 1)
    {
        Free(sheet);
        return;
    }

    tileStart = LoadSpriteSheet(&(struct SpriteSheet){sheet, PORTRAIT_SHEET_SIZE, PORTRAIT_TILE_TAG});
    Free(sheet); // LoadSpriteSheet copies to VRAM immediately, so this is safe here

    // LoadSpriteSheet returns 0 when AllocSpriteTiles fails. Tile 0 is reserved on
    // the field, so 0 here always means the allocation failed rather than
    // succeeding at the start of OBJ VRAM.
    if (tileStart == 0)
        return;

    palIndex = LoadSpritePaletteWithTag(pal, FLDEFF_PAL_TAG_ROGUE_PORTRAIT);
    if (palIndex == 0xFF)
    {
        // A palette miss is silent and would draw the portrait through whatever
        // colours the OAM slot already held. Bail with nothing on screen instead.
        FreeSpriteTilesByTag(PORTRAIT_TILE_TAG);
        return;
    }

    sPortrait.boxLeft = PORTRAIT_BOX_LEFT_COL;
    sPortrait.boxTop = PORTRAIT_BOX_BOTTOM_ROW + 1 - boxHeight;
    sPortrait.boxWidth = boxWidth;
    sPortrait.boxHeight = boxHeight;

    // Art sits one tile inside the frame. The sprite origin is its centre, and
    // the art is always in the top-left of a 64x64 sprite, so the centre is 32
    // pixels in from that corner whatever size the art actually is.
    artX = (sPortrait.boxLeft + 1) * 8;
    artY = (sPortrait.boxTop + 1) * 8;

    spriteId = CreateSprite(&sSpriteTemplate_Portrait, artX + 32, artY + 32, 0);
    if (spriteId == MAX_SPRITES)
    {
        FreeSpriteTilesByTag(PORTRAIT_TILE_TAG);
        FreeSpritePaletteByTag(FLDEFF_PAL_TAG_ROGUE_PORTRAIT);
        return;
    }

    sPortrait.spriteId = spriteId;
    sPortrait.palIndex = palIndex;

    // Weather tints every sprite palette through a colour map. Without this the
    // portrait darkens with the fog or the rain while the frame around it does
    // not, because BG palettes and OBJ palettes are mapped separately.
    PreservePaletteInWeather(palIndex + 16);

    DrawPortraitFrame(sPortrait.boxLeft, sPortrait.boxTop, boxWidth, boxHeight);
    ScheduleBgCopyTilemapToVram(0);

    sPortrait.showing = TRUE;
    sPortrait.pending = FALSE;
}

void RoguePortrait_Hide(void)
{
    TearDownPortrait();

    if (sPortrait.ownsSpeakerName)
    {
        // gSpeakerName is sticky - IsSpeakerBuffered spawns a namebox for ANY
        // non-NULL value, so leaving ours set would put the follower's nickname
        // over every message that came after it.
        gSpeakerName = NULL;
        sPortrait.ownsSpeakerName = FALSE;
    }

    sPortrait.pending = FALSE;
    sPortrait.species = SPECIES_NONE;
}

void RoguePortrait_ResetState(void)
{
    memset(&sPortrait, 0, sizeof(sPortrait));
    sPortrait.spriteId = MAX_SPRITES;
    sPortrait.palIndex = 0xFF;
}

bool32 RoguePortrait_IsShowing(void)
{
    return sPortrait.showing;
}
