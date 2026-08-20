#include "global.h"
#include "battle_anim.h"
#include "event_object_movement.h"
// For FieldEffectFreeTilesIfUnused / FieldEffectFreePaletteIfUnused, which
// WEATHER_ZUBATS needs and no other weather does - it is the only one whose
// sprites hold a sprite sheet and an OBJ palette it did not allocate itself.
#include "field_effect.h"
#include "constants/event_objects.h"
#include "constants/event_object_movement.h"
#include "constants/species.h"
#include "event_data.h"
#include "fieldmap.h"
#include "field_weather.h"
#include "overworld.h"
#include "random.h"
#include "script.h"
#include "constants/region_map_sections.h"
#include "constants/weather.h"
#include "constants/rogue_dungeon.h"
#include "constants/songs.h"
#include "constants/rgb.h"
#include "sound.h"
#include "sprite.h"
#include "task.h"
#include "trig.h"
#include "gpu_regs.h"
#include "palette.h"

EWRAM_DATA static u8 sCurrentAbnormalWeather = 0;

const u16 gCloudsWeatherPalette[] = INCGFX_U16("graphics/weather/cloud.png", ".gbapal");
const u16 gSandstormWeatherPalette[] = INCGFX_U16("graphics/weather/sandstorm.png", ".gbapal");
const u8 gWeatherFogDiagonalTiles[] = INCGFX_U8("graphics/weather/fog_diagonal.png", ".4bpp");
const u8 gWeatherFogHorizontalTiles[] = INCGFX_U8("graphics/weather/fog_horizontal.png", ".4bpp");
const u8 gWeatherCloudTiles[] = INCGFX_U8("graphics/weather/cloud.png", ".4bpp");
const u8 gWeatherSnow1Tiles[] = INCGFX_U8("graphics/weather/snow0.png", ".4bpp");
const u8 gWeatherSnow2Tiles[] = INCGFX_U8("graphics/weather/snow1.png", ".4bpp");
const u8 gWeatherPetal1Tiles[] = INCGFX_U8("graphics/weather/petal0.png", ".4bpp");
const u8 gWeatherPetal2Tiles[] = INCGFX_U8("graphics/weather/petal1.png", ".4bpp");
// Loaded into PALTAG_WEATHER_2 by Petals_Main, the way clouds and sandstorm
// load theirs. PALTAG_WEATHER is shared by rain, snow, ash and the fog.
const u16 gPetalsWeatherPalette[] = INCGFX_U16("graphics/weather/petals.pal", ".gbapal");
// VANILLA'S BATTLE-ANIM SHEET, READ A SECOND TIME. This is the same PNG that
// ANIM_TAG_LEAF draws Razor Leaf with; the anim table takes it as .4bpp.smol
// and this takes it as plain .4bpp, so there are two encodings of one source
// and no copied art. 16x144 is nine 16x16 frames of a leaf rotating, and the
// conversion puts them at 128-byte strides in OBJ 1D order (TL,TR,BL,BR) -
// verified host-side, because a wrong stride there is a scrambled sprite and
// not a build error. sLeafSpriteImages is what depends on it.
const u8 gWeatherLeafTiles[] = INCGFX_U8("graphics/battle_anims/sprites/leaf.png", ".4bpp");
// The whole recolour, in 32 bytes. Vanilla's ramp lives at palette indices 2-7
// and is green; this is the same indices in amber, so not a pixel of the art
// changes. See tools/rogue/make_leaf_weather.py for what was measured to land
// on it, and why green and a green/amber mix were both rejected.
const u16 gLeavesWeatherPalette[] = INCGFX_U16("graphics/weather/leaves.pal", ".gbapal");
const u8 gWeatherBubbleTiles[] = INCGFX_U8("graphics/weather/bubble.png", ".4bpp");
const u8 gWeatherAshTiles[] = INCGFX_U8("graphics/weather/ash.png", ".4bpp");
const u8 gWeatherRainTiles[] = INCGFX_U8("graphics/weather/rain.png", ".4bpp");
const u8 gWeatherSandstormTiles[] = INCGFX_U8("graphics/weather/sandstorm.png", ".4bpp");

//------------------------------------------------------------------------------
// WEATHER_SUNNY_CLOUDS
//------------------------------------------------------------------------------

static void CreateCloudSprites(void);
static void DestroyCloudSprites(void);
static void UpdateCloudSprite(struct Sprite *);

// The clouds are positioned on the map's grid.
// These coordinates are for the lower half of Route 120.
static const struct Coords16 sCloudSpriteMapCoords[] =
{
    { 0, 66},
    { 5, 73},
    {10, 78},
};

static const struct SpriteSheet sCloudSpriteSheet =
{
    .data = gWeatherCloudTiles,
    .size = sizeof(gWeatherCloudTiles),
    .tag = GFXTAG_CLOUD
};

static const struct OamData sCloudSpriteOamData =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_BLEND,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(64x64),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(64x64),
    .tileNum = 0,
    .priority = 3,
    .paletteNum = 0,
    .affineParam = 0,
};

static const union AnimCmd sCloudSpriteAnimCmd[] =
{
    ANIMCMD_FRAME(0, 16),
    ANIMCMD_END,
};

static const union AnimCmd *const sCloudSpriteAnimCmds[] =
{
    sCloudSpriteAnimCmd,
};

static const struct SpriteTemplate sCloudSpriteTemplate =
{
    .tileTag = GFXTAG_CLOUD,
    .paletteTag = PALTAG_WEATHER_2,
    .oam = &sCloudSpriteOamData,
    .anims = sCloudSpriteAnimCmds,
    .callback = UpdateCloudSprite,
};

void Clouds_InitVars(void)
{
    gWeatherPtr->noShadows = FALSE;
    gWeatherPtr->targetColorMapIndex = 0;
    gWeatherPtr->colorMapStepDelay = 20;
    gWeatherPtr->weatherGfxLoaded = FALSE;
    gWeatherPtr->initStep = 0;
    if (gWeatherPtr->cloudSpritesCreated == FALSE)
        Weather_SetBlendCoeffs(0, 16);
}

void Clouds_InitAll(void)
{
    Clouds_InitVars();
    while (gWeatherPtr->weatherGfxLoaded == FALSE)
        Clouds_Main();
}

void Clouds_Main(void)
{
    switch (gWeatherPtr->initStep)
    {
    case 0:
        CreateCloudSprites();
        gWeatherPtr->initStep++;
        break;
    case 1:
        Weather_SetTargetBlendCoeffs(12, 8, 1);
        gWeatherPtr->initStep++;
        break;
    case 2:
        if (Weather_UpdateBlend())
        {
            gWeatherPtr->weatherGfxLoaded = TRUE;
            gWeatherPtr->initStep++;
        }
        break;
    }
}

bool8 Clouds_Finish(void)
{
    switch (gWeatherPtr->finishStep)
    {
    case 0:
        Weather_SetTargetBlendCoeffs(0, 16, 1);
        gWeatherPtr->finishStep++;
        return TRUE;
    case 1:
        if (Weather_UpdateBlend())
        {
            DestroyCloudSprites();
            gWeatherPtr->finishStep++;
        }
        return TRUE;
    }
    return FALSE;
}

STATIC_ASSERT(OW_SHADOW_INTENSITY >= 0 && OW_SHADOW_INTENSITY <= 16, ObjEventShadowTransparencyNotInRange)

void Sunny_InitVars(void)
{
    gWeatherPtr->targetColorMapIndex = 0;
    gWeatherPtr->colorMapStepDelay = 20;
    Weather_SetBlendCoeffs(8, BASE_SHADOW_INTENSITY); // preserve shadow darkness
    gWeatherPtr->noShadows = FALSE;
}

void Sunny_InitAll(void)
{
    Sunny_InitVars();
}

void Sunny_Main(void)
{
}

bool8 Sunny_Finish(void)
{
    return FALSE;
}

static void CreateCloudSprites(void)
{
    u16 i;
    u8 spriteId;
    struct Sprite *sprite;

    if (gWeatherPtr->cloudSpritesCreated == TRUE)
        return;

    LoadSpriteSheet(&sCloudSpriteSheet);
    LoadCustomWeatherSpritePalette(gCloudsWeatherPalette);
    for (i = 0; i < NUM_CLOUD_SPRITES; i++)
    {
        spriteId = CreateSprite(&sCloudSpriteTemplate, 0, 0, 0xFF);
        if (spriteId != MAX_SPRITES)
        {
            gWeatherPtr->sprites.s1.cloudSprites[i] = &gSprites[spriteId];
            sprite = gWeatherPtr->sprites.s1.cloudSprites[i];
            SetSpritePosToMapCoords(sCloudSpriteMapCoords[i].x + MAP_OFFSET, sCloudSpriteMapCoords[i].y + MAP_OFFSET, &sprite->x, &sprite->y);
            sprite->coordOffsetEnabled = TRUE;
        }
        else
        {
            gWeatherPtr->sprites.s1.cloudSprites[i] = NULL;
        }
    }

    gWeatherPtr->cloudSpritesCreated = TRUE;
}

static void DestroyCloudSprites(void)
{
    u16 i;

    if (!gWeatherPtr->cloudSpritesCreated)
        return;

    for (i = 0; i < NUM_CLOUD_SPRITES; i++)
    {
        if (gWeatherPtr->sprites.s1.cloudSprites[i] != NULL)
            DestroySprite(gWeatherPtr->sprites.s1.cloudSprites[i]);
    }

    FreeSpriteTilesByTag(GFXTAG_CLOUD);
    gWeatherPtr->cloudSpritesCreated = FALSE;
}

static void UpdateCloudSprite(struct Sprite *sprite)
{
    // Move 1 pixel left every 2 frames.
    sprite->data[0] = (sprite->data[0] + 1) & 1;
    if (sprite->data[0])
        sprite->x--;
}

//------------------------------------------------------------------------------
// WEATHER_DROUGHT
//------------------------------------------------------------------------------

static void UpdateDroughtBlend(u8);

void Drought_InitVars(void)
{
    gWeatherPtr->initStep = 0;
    gWeatherPtr->weatherGfxLoaded = FALSE;
    gWeatherPtr->targetColorMapIndex = 0;
    gWeatherPtr->colorMapStepDelay = 0;
    gWeatherPtr->noShadows = FALSE;
}

void Drought_InitAll(void)
{
    Drought_InitVars();
    while (gWeatherPtr->weatherGfxLoaded == FALSE)
        Drought_Main();
}

void Drought_Main(void)
{
    switch (gWeatherPtr->initStep)
    {
    case 0:
        if (gWeatherPtr->palProcessingState != WEATHER_PAL_STATE_CHANGING_WEATHER)
            gWeatherPtr->initStep++;
        break;
    case 1:
        ResetDroughtWeatherPaletteLoading();
        gWeatherPtr->initStep++;
        break;
    case 2:
        if (LoadDroughtWeatherPalettes() == FALSE)
            gWeatherPtr->initStep++;
        break;
    case 3:
        DroughtStateInit();
        gWeatherPtr->initStep++;
        break;
    case 4:
        DroughtStateRun();
        if (gWeatherPtr->droughtBrightnessStage == 6)
        {
            gWeatherPtr->weatherGfxLoaded = TRUE;
            gWeatherPtr->initStep++;
        }
        break;
    default:
        DroughtStateRun();
        break;
    }
}

bool8 Drought_Finish(void)
{
    return FALSE;
}

void StartDroughtWeatherBlend(void)
{
    CreateTask(UpdateDroughtBlend, 80);
}

#define tState      data[0]
#define tBlendY     data[1]
#define tBlendDelay data[2]
#define tWinRange   data[3]

static void UpdateDroughtBlend(u8 taskId)
{
    struct Task *task = &gTasks[taskId];

    switch (task->tState)
    {
    case 0:
        task->tBlendY = 0;
        task->tBlendDelay = 0;
        task->tWinRange = REG_WININ;
        SetGpuReg(REG_OFFSET_WININ, WININ_WIN0_ALL | WININ_WIN1_ALL);
        SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT1_BG1 | BLDCNT_TGT1_BG2 | BLDCNT_TGT1_BG3 | BLDCNT_TGT1_OBJ | BLDCNT_EFFECT_LIGHTEN);
        SetGpuReg(REG_OFFSET_BLDY, 0);
        task->tState++;
        // fall through
    case 1:
        task->tBlendY += 3;
        if (task->tBlendY > 16)
            task->tBlendY = 16;
        SetGpuReg(REG_OFFSET_BLDY, task->tBlendY);
        if (task->tBlendY >= 16)
            task->tState++;
        break;
    case 2:
        task->tBlendDelay++;
        if (task->tBlendDelay > 9)
        {
            task->tBlendDelay = 0;
            task->tBlendY--;
            if (task->tBlendY <= 0)
            {
                task->tBlendY = 0;
                task->tState++;
            }
            SetGpuReg(REG_OFFSET_BLDY, task->tBlendY);
        }
        break;
    case 3:
        SetGpuReg(REG_OFFSET_BLDCNT, 0);
        SetGpuReg(REG_OFFSET_BLDY, 0);
        SetGpuReg(REG_OFFSET_WININ, task->tWinRange);
        task->tState++;
        break;
    case 4:
        ScriptContext_Enable();
        DestroyTask(taskId);
        break;
    }
}

#undef tState
#undef tBlendY
#undef tBlendDelay
#undef tWinRange

//------------------------------------------------------------------------------
// WEATHER_RAIN
//------------------------------------------------------------------------------

static void LoadRainSpriteSheet(void);
static bool8 CreateRainSprite(void);
static void UpdateRainSprite(struct Sprite *sprite);
static bool8 UpdateVisibleRainSprites(void);
static void DestroyRainSprites(void);

static const struct Coords16 sRainSpriteCoords[] =
{
    {  0,   0},
    {  0, 160},
    {  0,  64},
    {144, 224},
    {144, 128},
    { 32,  32},
    { 32, 192},
    { 32,  96},
    { 72, 128},
    { 72,  32},
    { 72, 192},
    {216,  96},
    {216,   0},
    {104, 160},
    {104,  64},
    {104, 224},
    {144,   0},
    {144, 160},
    {144,  64},
    { 32, 224},
    { 32, 128},
    { 72,  32},
    { 72, 192},
    { 48,  96},
};

static const struct OamData sRainSpriteOamData =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(16x32),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(16x32),
    .tileNum = 0,
    .priority = 1,
    .paletteNum = 2,
    .affineParam = 0,
};

static const union AnimCmd sRainSpriteFallAnimCmd[] =
{
    ANIMCMD_FRAME(0, 16),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sRainSpriteSplashAnimCmd[] =
{
    ANIMCMD_FRAME(8, 3),
    ANIMCMD_FRAME(32, 2),
    ANIMCMD_FRAME(40, 2),
    ANIMCMD_END,
};

static const union AnimCmd sRainSpriteHeavySplashAnimCmd[] =
{
    ANIMCMD_FRAME(8, 3),
    ANIMCMD_FRAME(16, 3),
    ANIMCMD_FRAME(24, 4),
    ANIMCMD_END,
};

static const union AnimCmd *const sRainSpriteAnimCmds[] =
{
    sRainSpriteFallAnimCmd,
    sRainSpriteSplashAnimCmd,
    sRainSpriteHeavySplashAnimCmd,
};

static const struct SpriteTemplate sRainSpriteTemplate =
{
    .tileTag = GFXTAG_RAIN,
    .paletteTag = PALTAG_WEATHER,
    .oam = &sRainSpriteOamData,
    .anims = sRainSpriteAnimCmds,
    .callback = UpdateRainSprite,
};

// Q28.4 fixed-point format values
static const s16 sRainSpriteMovement[][2] =
{
    {-0x68,  0xD0},
    {-0xA0, 0x140},
};

// First byte is the number of frames a raindrop falls before it splashes.
// Second byte is the maximum number of frames a raindrop can "wait" before
// it appears and starts falling. (This is only for the initial raindrop spawn.)
static const u16 sRainSpriteFallingDurations[][2] =
{
    {18, 7},
    {12, 10},
};

static const struct SpriteSheet sRainSpriteSheet =
{
    .data = gWeatherRainTiles,
    .size = sizeof(gWeatherRainTiles),
    .tag = GFXTAG_RAIN,
};

void Rain_InitVars(void)
{
    gWeatherPtr->initStep = 0;
    gWeatherPtr->weatherGfxLoaded = FALSE;
    gWeatherPtr->rainSpriteVisibleCounter = 0;
    gWeatherPtr->rainSpriteVisibleDelay = 8;
    gWeatherPtr->isDownpour = FALSE;
    gWeatherPtr->targetRainSpriteCount = 10;
    gWeatherPtr->targetColorMapIndex = 3;
    gWeatherPtr->colorMapStepDelay = 20;
    SetRainStrengthFromSoundEffect(SE_RAIN);
    Weather_SetBlendCoeffs(8, BASE_SHADOW_INTENSITY); // preserve shadow darkness
    gWeatherPtr->noShadows = FALSE;
}

void Rain_InitAll(void)
{
    Rain_InitVars();
    while (!gWeatherPtr->weatherGfxLoaded)
        Rain_Main();
}

void Rain_Main(void)
{
    switch (gWeatherPtr->initStep)
    {
    case 0:
        LoadRainSpriteSheet();
        gWeatherPtr->initStep++;
        break;
    case 1:
        if (!CreateRainSprite())
            gWeatherPtr->initStep++;
        break;
    case 2:
        if (!UpdateVisibleRainSprites())
        {
            gWeatherPtr->weatherGfxLoaded = TRUE;
            gWeatherPtr->initStep++;
        }
        break;
    }
}

bool8 Rain_Finish(void)
{
    switch (gWeatherPtr->finishStep)
    {
    case 0:
        if (IsWeatherRainy(gWeatherPtr->nextWeather))
        {
            gWeatherPtr->finishStep = 0xFF;
            return FALSE;
        }
        else
        {
            gWeatherPtr->targetRainSpriteCount = 0;
            gWeatherPtr->finishStep++;
        }
        // fall through
    case 1:
        if (!UpdateVisibleRainSprites())
        {
            DestroyRainSprites();
            gWeatherPtr->finishStep++;
            return FALSE;
        }
        return TRUE;
    }
    return FALSE;
}

#define tCounter data[0]
#define tRandom  data[1]
#define tPosX    data[2]
#define tPosY    data[3]
#define tState   data[4]
#define tActive  data[5]
#define tWaiting data[6]

static void StartRainSpriteFall(struct Sprite *sprite)
{
    u32 rand;
    u16 numFallingFrames;
    int tileX;
    int tileY;

    if (sprite->tRandom == 0)
        sprite->tRandom = 361;

    rand = ISO_RANDOMIZE2(sprite->tRandom);
    sprite->tRandom = ((rand & 0x7FFF0000) >> 16) % 600;

    numFallingFrames = sRainSpriteFallingDurations[gWeatherPtr->isDownpour][0];

    tileX = sprite->tRandom % 30;
    sprite->tPosX = tileX * 8; // Useless assignment, leftover from before fixed-point values were used

    tileY = sprite->tRandom / 30;
    sprite->tPosY = tileY * 8; // Useless assignment, leftover from before fixed-point values were used

    sprite->tPosX = tileX;
    sprite->tPosX <<= 7; // This is tileX * 8, using a fixed-point value with 4 decimal places

    sprite->tPosY = tileY;
    sprite->tPosY <<= 7; // This is tileX * 8, using a fixed-point value with 4 decimal places

    // "Rewind" the rain sprites, from their ending position.
    sprite->tPosX -= sRainSpriteMovement[gWeatherPtr->isDownpour][0] * numFallingFrames;
    sprite->tPosY -= sRainSpriteMovement[gWeatherPtr->isDownpour][1] * numFallingFrames;

    StartSpriteAnim(sprite, 0);
    sprite->tState = 0;
    sprite->coordOffsetEnabled = FALSE;
    sprite->tCounter = numFallingFrames;
}

static void UpdateRainSprite(struct Sprite *sprite)
{
    if (sprite->tState == 0)
    {
        // Raindrop is in its "falling" motion.
        sprite->tPosX += sRainSpriteMovement[gWeatherPtr->isDownpour][0];
        sprite->tPosY += sRainSpriteMovement[gWeatherPtr->isDownpour][1];
        sprite->x = sprite->tPosX >> 4;
        sprite->y = sprite->tPosY >> 4;

        if (sprite->tActive
         && (sprite->x >= -8 && sprite->x <= DISPLAY_WIDTH + 8)
         && sprite->y >= -16 && sprite->y <= DISPLAY_HEIGHT + 16)
            sprite->invisible = FALSE;
        else
            sprite->invisible = TRUE;

        if (--sprite->tCounter == 0)
        {
            // Make raindrop splash on the ground
            StartSpriteAnim(sprite, gWeatherPtr->isDownpour + 1);
            sprite->tState = 1;
            sprite->x -= gSpriteCoordOffsetX;
            sprite->y -= gSpriteCoordOffsetY;
            sprite->coordOffsetEnabled = TRUE;
        }
    }
    else if (sprite->animEnded)
    {
        // The splashing animation ended.
        sprite->invisible = TRUE;
        StartRainSpriteFall(sprite);
    }
}

static void WaitRainSprite(struct Sprite *sprite)
{
    if (sprite->tCounter == 0)
    {
        StartRainSpriteFall(sprite);
        sprite->callback = UpdateRainSprite;
    }
    else
    {
        sprite->tCounter--;
    }
}

static void InitRainSpriteMovement(struct Sprite *sprite, u16 val)
{
    u16 numFallingFrames = sRainSpriteFallingDurations[gWeatherPtr->isDownpour][0];
    u16 numAdvanceRng = val / (sRainSpriteFallingDurations[gWeatherPtr->isDownpour][1] + numFallingFrames);
    u16 frameVal = val % (sRainSpriteFallingDurations[gWeatherPtr->isDownpour][1] + numFallingFrames);

    while (--numAdvanceRng != 0xFFFF)
        StartRainSpriteFall(sprite);

    if (frameVal < numFallingFrames)
    {
        while (--frameVal != 0xFFFF)
            UpdateRainSprite(sprite);

        sprite->tWaiting = 0;
    }
    else
    {
        sprite->tCounter = frameVal - numFallingFrames;
        sprite->invisible = TRUE;
        sprite->tWaiting = 1;
    }
}

static void LoadRainSpriteSheet(void)
{
    LoadSpriteSheet(&sRainSpriteSheet);
}

static bool8 CreateRainSprite(void)
{
    u8 spriteIndex;
    u8 spriteId;

    if (gWeatherPtr->rainSpriteCount == MAX_RAIN_SPRITES)
        return FALSE;

    spriteIndex = gWeatherPtr->rainSpriteCount;
    spriteId = CreateSpriteAtEnd(&sRainSpriteTemplate,
      sRainSpriteCoords[spriteIndex].x, sRainSpriteCoords[spriteIndex].y, 78);

    if (spriteId != MAX_SPRITES)
    {
        gSprites[spriteId].tActive = FALSE;
        gSprites[spriteId].tRandom = spriteIndex * 145;
        while (gSprites[spriteId].tRandom >= 600)
            gSprites[spriteId].tRandom -= 600;

        StartRainSpriteFall(&gSprites[spriteId]);
        InitRainSpriteMovement(&gSprites[spriteId], spriteIndex * 9);
        gSprites[spriteId].invisible = TRUE;
        gWeatherPtr->sprites.s1.rainSprites[spriteIndex] = &gSprites[spriteId];
    }
    else
    {
        gWeatherPtr->sprites.s1.rainSprites[spriteIndex] = NULL;
    }

    if (++gWeatherPtr->rainSpriteCount == MAX_RAIN_SPRITES)
    {
        u16 i;
        for (i = 0; i < MAX_RAIN_SPRITES; i++)
        {
            if (gWeatherPtr->sprites.s1.rainSprites[i])
            {
                if (!gWeatherPtr->sprites.s1.rainSprites[i]->tWaiting)
                    gWeatherPtr->sprites.s1.rainSprites[i]->callback = UpdateRainSprite;
                else
                    gWeatherPtr->sprites.s1.rainSprites[i]->callback = WaitRainSprite;
            }
        }

        return FALSE;
    }

    return TRUE;
}

static bool8 UpdateVisibleRainSprites(void)
{
    if (gWeatherPtr->curRainSpriteIndex == gWeatherPtr->targetRainSpriteCount)
        return FALSE;

    if (++gWeatherPtr->rainSpriteVisibleCounter > gWeatherPtr->rainSpriteVisibleDelay)
    {
        gWeatherPtr->rainSpriteVisibleCounter = 0;
        if (gWeatherPtr->curRainSpriteIndex < gWeatherPtr->targetRainSpriteCount)
        {
            gWeatherPtr->sprites.s1.rainSprites[gWeatherPtr->curRainSpriteIndex++]->tActive = TRUE;
        }
        else
        {
            gWeatherPtr->curRainSpriteIndex--;
            gWeatherPtr->sprites.s1.rainSprites[gWeatherPtr->curRainSpriteIndex]->tActive = FALSE;
            gWeatherPtr->sprites.s1.rainSprites[gWeatherPtr->curRainSpriteIndex]->invisible = TRUE;
        }
    }
    return TRUE;
}

static void DestroyRainSprites(void)
{
    u16 i;

    for (i = 0; i < gWeatherPtr->rainSpriteCount; i++)
    {
        if (gWeatherPtr->sprites.s1.rainSprites[i] != NULL)
            DestroySprite(gWeatherPtr->sprites.s1.rainSprites[i]);
    }
    gWeatherPtr->rainSpriteCount = 0;
    FreeSpriteTilesByTag(GFXTAG_RAIN);
}

#undef tCounter
#undef tRandom
#undef tPosX
#undef tPosY
#undef tState
#undef tActive
#undef tWaiting

//------------------------------------------------------------------------------
// Snow
//------------------------------------------------------------------------------

static void UpdateSnowflakeSprite(struct Sprite *);
static bool8 UpdateVisibleSnowflakeSprites(void);
static bool8 CreateSnowflakeSprite(void);
static bool8 DestroySnowflakeSprite(void);
static void InitSnowflakeSpriteMovement(struct Sprite *);

void Snow_InitVars(void)
{
    gWeatherPtr->initStep = 0;
    gWeatherPtr->weatherGfxLoaded = FALSE;
    gWeatherPtr->targetColorMapIndex = 0;
    gWeatherPtr->colorMapStepDelay = 20;
    gWeatherPtr->targetSnowflakeSpriteCount = NUM_SNOWFLAKE_SPRITES;
    gWeatherPtr->snowflakeVisibleCounter = 0;
    Weather_SetBlendCoeffs(8, BASE_SHADOW_INTENSITY); // preserve shadow darkness
    gWeatherPtr->noShadows = FALSE;
}

void Snow_InitAll(void)
{
    u16 i;

    Snow_InitVars();
    while (gWeatherPtr->weatherGfxLoaded == FALSE)
    {
        Snow_Main();
        for (i = 0; i < gWeatherPtr->snowflakeSpriteCount; i++)
            UpdateSnowflakeSprite(gWeatherPtr->sprites.s1.snowflakeSprites[i]);
    }
}

void Snow_Main(void)
{
    if (gWeatherPtr->initStep == 0 && !UpdateVisibleSnowflakeSprites())
    {
        gWeatherPtr->weatherGfxLoaded = TRUE;
        gWeatherPtr->initStep++;
    }
}

bool8 Snow_Finish(void)
{
    switch (gWeatherPtr->finishStep)
    {
    case 0:
        gWeatherPtr->targetSnowflakeSpriteCount = 0;
        gWeatherPtr->snowflakeVisibleCounter = 0;
        gWeatherPtr->finishStep++;
        // fall through
    case 1:
        if (!UpdateVisibleSnowflakeSprites())
        {
            gWeatherPtr->finishStep++;
            return FALSE;
        }
        return TRUE;
    }

    return FALSE;
}

static bool8 UpdateVisibleSnowflakeSprites(void)
{
    if (gWeatherPtr->snowflakeSpriteCount == gWeatherPtr->targetSnowflakeSpriteCount)
        return FALSE;

    if (++gWeatherPtr->snowflakeVisibleCounter > 36)
    {
        gWeatherPtr->snowflakeVisibleCounter = 0;
        if (gWeatherPtr->snowflakeSpriteCount < gWeatherPtr->targetSnowflakeSpriteCount)
            CreateSnowflakeSprite();
        else
            DestroySnowflakeSprite();
    }

    return gWeatherPtr->snowflakeSpriteCount != gWeatherPtr->targetSnowflakeSpriteCount;
}

static const struct OamData sSnowflakeSpriteOamData =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(8x8),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(8x8),
    .tileNum = 0,
    .priority = 1,
    .paletteNum = 0,
    .affineParam = 0,
};

static const struct SpriteFrameImage sSnowflakeSpriteImages[] =
{
    {gWeatherSnow1Tiles, sizeof(gWeatherSnow1Tiles)},
    {gWeatherSnow2Tiles, sizeof(gWeatherSnow2Tiles)},
};

static const union AnimCmd sSnowflakeAnimCmd0[] =
{
    ANIMCMD_FRAME(0, 16),
    ANIMCMD_END,
};

static const union AnimCmd sSnowflakeAnimCmd1[] =
{
    ANIMCMD_FRAME(1, 16),
    ANIMCMD_END,
};

static const union AnimCmd *const sSnowflakeAnimCmds[] =
{
    sSnowflakeAnimCmd0,
    sSnowflakeAnimCmd1,
};

static const struct SpriteTemplate sSnowflakeSpriteTemplate =
{
    .tileTag = TAG_NONE,
    .paletteTag = PALTAG_WEATHER,
    .oam = &sSnowflakeSpriteOamData,
    .anims = sSnowflakeAnimCmds,
    .images = sSnowflakeSpriteImages,
    .callback = UpdateSnowflakeSprite,
};

#define tPosY         data[0]
#define tDeltaY       data[1]
#define tWaveDelta    data[2]
#define tWaveIndex    data[3]
#define tSnowflakeId  data[4]
#define tFallCounter  data[5]
#define tFallDuration data[6]
#define tDeltaY2      data[7]

static bool8 CreateSnowflakeSprite(void)
{
    u8 spriteId = CreateSpriteAtEnd(&sSnowflakeSpriteTemplate, 0, 0, 78);
    if (spriteId == MAX_SPRITES)
        return FALSE;

    gSprites[spriteId].tSnowflakeId = gWeatherPtr->snowflakeSpriteCount;
    InitSnowflakeSpriteMovement(&gSprites[spriteId]);
    gSprites[spriteId].coordOffsetEnabled = TRUE;
    gWeatherPtr->sprites.s1.snowflakeSprites[gWeatherPtr->snowflakeSpriteCount++] = &gSprites[spriteId];
    return TRUE;
}

static bool8 DestroySnowflakeSprite(void)
{
    if (gWeatherPtr->snowflakeSpriteCount)
    {
        DestroySprite(gWeatherPtr->sprites.s1.snowflakeSprites[--gWeatherPtr->snowflakeSpriteCount]);
        return TRUE;
    }

    return FALSE;
}

static void InitSnowflakeSpriteMovement(struct Sprite *sprite)
{
    u16 rand;
    u16 x = ((sprite->tSnowflakeId * 5) & 7) * 30 + (Random() % 30);

    sprite->y = -3 - (gSpriteCoordOffsetY + sprite->centerToCornerVecY);
    sprite->x = x - (gSpriteCoordOffsetX + sprite->centerToCornerVecX);
    // A FRACTION, NOT A POSITION - zero, not y * 128. See UpdateSnowflakeSprite.
    sprite->tPosY = 0;
    sprite->x2 = 0;
    rand = Random();
    sprite->tDeltaY = (rand & 3) * 5 + 64;
    sprite->tDeltaY2 = sprite->tDeltaY;
    StartSpriteAnim(sprite, (rand & 1) ? 0 : 1);
    sprite->tWaveIndex = 0;
    sprite->tWaveDelta = ((rand & 3) == 0) ? 2 : 1;
    sprite->tFallDuration = (rand & 0x1F) + 210;
    sprite->tFallCounter = 0;
}

static void UNUSED WaitSnowflakeSprite(struct Sprite *sprite)
{
    if (++gWeatherPtr->snowflakeTimer > 18)
    {
        sprite->invisible = FALSE;
        sprite->callback = UpdateSnowflakeSprite;
        sprite->y = 250 - (gSpriteCoordOffsetY + sprite->centerToCornerVecY);
        sprite->tPosY = 0;
        gWeatherPtr->snowflakeTimer = 0;
    }
}

static void UpdateSnowflakeSprite(struct Sprite *sprite)
{
    s16 x;

    // WHOLE PIXELS INTO y, FRACTION KEPT IN tPosY. This used to be
    // `tPosY = y * 128` in the init and `y = tPosY >> 7` here, carrying the
    // POSITION in a Q7 s16 - and sprite->y is stored relative to
    // gSpriteCoordOffsetY, which reaches ~600 on a 48x48 dungeon floor. The
    // SEED alone is then -595 * 128 = -76160 and wraps before the first frame,
    // so y came back as garbage that varied with where the camera happened to
    // be. It never looked broken on a vanilla-sized route, where the offset is
    // small enough that y * 128 fits, and it was never noticed here because
    // the 8-bit OAM y wrap kept recycling the flakes anyway - just from the
    // wrong row, shifting phase every time the player walked.
    //
    // Keeping only the remainder bounds tPosY to 0..127 forever and leaves
    // sprite->y as the single authority the wraps below can rewrite. Same
    // shape as the tSubX comment in UpdateBlizzardSprite, which had worked
    // this out for the x axis and stated the y axis did it the other way.
    sprite->tPosY += sprite->tDeltaY;
    sprite->y += sprite->tPosY >> 7;
    sprite->tPosY &= 0x7F;
    sprite->tWaveIndex += sprite->tWaveDelta;
    sprite->tWaveIndex &= 0xFF;
    sprite->x2 = gSineTable[sprite->tWaveIndex] / 64;

    x = (sprite->x + sprite->centerToCornerVecX + gSpriteCoordOffsetX) & 0x1FF;
    if (x & 0x100)
        x |= -0x100;

    if (x < -3)
        sprite->x = 242 - (gSpriteCoordOffsetX + sprite->centerToCornerVecX);
    else if (x > 242)
        sprite->x = -3 - (gSpriteCoordOffsetX + sprite->centerToCornerVecX);

    // OFF THE BOTTOM, BACK TO THE TOP - and vanilla snow had no vertical test
    // at all. It did not need one: tPosY overflowed every few seconds and threw
    // the flake back above the screen, and the 8-bit OAM y wrap caught whatever
    // that missed. Both of those are accidents of the bug fixed above, and with
    // the accumulator carrying only a fraction, sprite->y now grows without
    // bound - so the recycling has to be stated rather than fallen into. This
    // is the petals' and the blizzard's test, unchanged.
    //
    // SCREEN SPACE, not sprite space: sprite->y is relative to
    // gSpriteCoordOffsetY and coordOffsetEnabled adds it back at draw time, so
    // comparing it raw against a screen bound is only correct while the camera
    // has not scrolled. The x wrap directly above already converts before it
    // compares; this is the same conversion.
    if (sprite->y + sprite->centerToCornerVecY + gSpriteCoordOffsetY > 163)
    {
        // A new entry lane, for the reason the petals re-roll theirs:
        // tSnowflakeId picks the 30-pixel column a flake enters from and is
        // otherwise fixed for the sprite's life, so keeping it would file every
        // flake down the same stripe.
        sprite->tSnowflakeId = Random() & 7;
        InitSnowflakeSpriteMovement(sprite);
    }
}

#undef tPosY
#undef tDeltaY
#undef tWaveDelta
#undef tWaveIndex
#undef tSnowflakeId
#undef tFallCounter
#undef tFallDuration
#undef tDeltaY2

//------------------------------------------------------------------------------
// WEATHER_PETALS
//------------------------------------------------------------------------------
//
// Blossom blowing across the flower dungeon. The snow with a different drift.
//
// It reuses the snow's sprite storage and counters outright - only one weather
// runs at a time, so snowflakeSprites[] is free whenever petals are up, and a
// second array would cost the Weather struct 64 bytes for nothing.
//
// PURELY COSMETIC, deliberately. Petals do not appear in the overworld-to-battle
// weather switch in battle_util.c, so gBattleWeather stays clear. That is a
// choice rather than an omission: WEATHER_SNOW is NOT cosmetic, because
// B_OVERWORLD_SNOW is GEN_LATEST and Glacia's floors hand every Ice type 1.5x
// Defense. Wallace's floor stays mechanically plain.

static void UpdatePetalSprite(struct Sprite *);
static bool8 UpdateVisiblePetalSprites(void);
static bool8 CreatePetalSprite(void);
static bool8 DestroyPetalSprite(void);
static void InitPetalSpriteMovement(struct Sprite *);

void Petals_InitVars(void)
{
    gWeatherPtr->initStep = 0;
    gWeatherPtr->weatherGfxLoaded = FALSE;
    gWeatherPtr->targetColorMapIndex = 0;
    gWeatherPtr->colorMapStepDelay = 20;
    // Fewer than the snow's sixteen. A petal covers more ground than a flake
    // because it drifts four times as far sideways, so the same count reads as
    // confetti rather than as a breeze.
    gWeatherPtr->targetSnowflakeSpriteCount = NUM_PETAL_SPRITES;
    gWeatherPtr->snowflakeVisibleCounter = 0;
    Weather_SetBlendCoeffs(8, BASE_SHADOW_INTENSITY); // preserve shadow darkness
    gWeatherPtr->noShadows = FALSE;
}

void Petals_InitAll(void)
{
    u16 i;

    Petals_InitVars();
    while (gWeatherPtr->weatherGfxLoaded == FALSE)
    {
        Petals_Main();
        for (i = 0; i < gWeatherPtr->snowflakeSpriteCount; i++)
            UpdatePetalSprite(gWeatherPtr->sprites.s1.snowflakeSprites[i]);
    }
}

void Petals_Main(void)
{
    if (gWeatherPtr->initStep == 0)
    {
        // PALTAG_WEATHER_2, not PALTAG_WEATHER. The latter holds gFogPalette
        // and is shared by rain, snow, ash, bubbles and the fog itself, so
        // putting petal colours there would repaint every weather in the game.
        // This is the lazily-allocated second slot, and loading into it is
        // exactly how clouds and sandstorm get their own colours. It applies
        // the weather fade itself, so nothing else has to.
        LoadCustomWeatherSpritePalette(gPetalsWeatherPalette);
        if (!UpdateVisiblePetalSprites())
        {
            gWeatherPtr->weatherGfxLoaded = TRUE;
            gWeatherPtr->initStep++;
        }
    }
}

bool8 Petals_Finish(void)
{
    switch (gWeatherPtr->finishStep)
    {
    case 0:
        gWeatherPtr->targetSnowflakeSpriteCount = 0;
        gWeatherPtr->snowflakeVisibleCounter = 0;
        gWeatherPtr->finishStep++;
        // fall through
    case 1:
        if (!UpdateVisiblePetalSprites())
        {
            gWeatherPtr->finishStep++;
            return FALSE;
        }
        return TRUE;
    }

    return FALSE;
}

static bool8 UpdateVisiblePetalSprites(void)
{
    if (gWeatherPtr->snowflakeSpriteCount == gWeatherPtr->targetSnowflakeSpriteCount)
        return FALSE;

    if (++gWeatherPtr->snowflakeVisibleCounter > 36)
    {
        gWeatherPtr->snowflakeVisibleCounter = 0;
        if (gWeatherPtr->snowflakeSpriteCount < gWeatherPtr->targetSnowflakeSpriteCount)
            CreatePetalSprite();
        else
            DestroyPetalSprite();
    }

    return gWeatherPtr->snowflakeSpriteCount != gWeatherPtr->targetSnowflakeSpriteCount;
}

static const struct OamData sPetalSpriteOamData =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(8x8),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(8x8),
    .tileNum = 0,
    .priority = 1,
    .paletteNum = 0,
    .affineParam = 0,
};

static const struct SpriteFrameImage sPetalSpriteImages[] =
{
    {gWeatherPetal1Tiles, sizeof(gWeatherPetal1Tiles)},
    {gWeatherPetal2Tiles, sizeof(gWeatherPetal2Tiles)},
};

static const union AnimCmd sPetalAnimCmd0[] =
{
    ANIMCMD_FRAME(0, 16),
    ANIMCMD_END,
};

static const union AnimCmd sPetalAnimCmd1[] =
{
    ANIMCMD_FRAME(1, 16),
    ANIMCMD_END,
};

static const union AnimCmd *const sPetalAnimCmds[] =
{
    sPetalAnimCmd0,
    sPetalAnimCmd1,
};

static const struct SpriteTemplate sPetalSpriteTemplate =
{
    .tileTag = TAG_NONE,
    .paletteTag = PALTAG_WEATHER_2,
    .oam = &sPetalSpriteOamData,
    .anims = sPetalAnimCmds,
    .images = sPetalSpriteImages,
    .callback = UpdatePetalSprite,
};

#define tPosY       data[0]
#define tDeltaY     data[1]
#define tWaveDelta  data[2]
#define tWaveIndex  data[3]
#define tPetalId    data[4]
#define tRestCount  data[5]
#define tRestUntil  data[6]
#define tFallDeltaY data[7]

static bool8 CreatePetalSprite(void)
{
    u8 spriteId = CreateSpriteAtEnd(&sPetalSpriteTemplate, 0, 0, 78);
    if (spriteId == MAX_SPRITES)
        return FALSE;

    gSprites[spriteId].tPetalId = gWeatherPtr->snowflakeSpriteCount;
    InitPetalSpriteMovement(&gSprites[spriteId]);
    gSprites[spriteId].coordOffsetEnabled = TRUE;
    gWeatherPtr->sprites.s1.snowflakeSprites[gWeatherPtr->snowflakeSpriteCount++] = &gSprites[spriteId];
    return TRUE;
}

static bool8 DestroyPetalSprite(void)
{
    if (gWeatherPtr->snowflakeSpriteCount)
    {
        DestroySprite(gWeatherPtr->sprites.s1.snowflakeSprites[--gWeatherPtr->snowflakeSpriteCount]);
        return TRUE;
    }

    return FALSE;
}

static void InitPetalSpriteMovement(struct Sprite *sprite)
{
    u16 rand;
    u16 x = ((sprite->tPetalId * 5) & 7) * 30 + (Random() % 30);

    sprite->y = -3 - (gSpriteCoordOffsetY + sprite->centerToCornerVecY);
    sprite->x = x - (gSpriteCoordOffsetX + sprite->centerToCornerVecX);
    // A FRACTION, NOT A POSITION - zero, not y * 128. See UpdatePetalSprite.
    sprite->tPosY = 0;
    sprite->x2 = 0;
    rand = Random();
    // Slower and more varied than snow, which falls at 64 to 79. A petal is
    // being carried rather than dropped.
    sprite->tDeltaY = (rand & 7) * 4 + 28;
    sprite->tFallDeltaY = sprite->tDeltaY;
    StartSpriteAnim(sprite, (rand & 1) ? 0 : 1);
    // Not all in phase, unlike the snow, which starts every flake at wave index
    // 0 and so has them all swing the same way at the same time.
    sprite->tWaveIndex = Random() & 0xFF;
    sprite->tWaveDelta = ((rand >> 4) & 3) + 2;
    sprite->tRestUntil = (rand & 0x3F) + 90;
    sprite->tRestCount = 0;
}

static void UpdatePetalSprite(struct Sprite *sprite)
{
    s16 x;

    // THE SNOW'S CUT FEATURE, FINISHED. InitSnowflakeSpriteMovement writes
    // tFallCounter, tFallDuration and tDeltaY2 on every spawn and nothing ever
    // reads them, and WaitSnowflakeSprite is UNUSED - the machinery for a
    // pause-and-resume cycle is all there and inert. Snow does not need it.
    // Petals do: one caught in a breeze stalls, hangs, and then drops again,
    // and that hesitation is most of what separates blossom from confetti
    // falling at a constant rate.
    if (++sprite->tRestCount > sprite->tRestUntil)
    {
        sprite->tDeltaY = sprite->tFallDeltaY / 4;
        if (sprite->tRestCount > sprite->tRestUntil + 40)
        {
            sprite->tDeltaY = sprite->tFallDeltaY;
            sprite->tRestCount = 0;
        }
    }

    // WHOLE PIXELS INTO y, FRACTION KEPT IN tPosY - see UpdateSnowflakeSprite
    // for the full account. Carrying the position here overflowed on the very
    // first frame of a dungeon floor, because the seed is y * 128 and y is
    // relative to a gSpriteCoordOffsetY that reaches ~600.
    sprite->tPosY += sprite->tDeltaY;
    sprite->y += sprite->tPosY >> 7;
    sprite->tPosY &= 0x7F;
    sprite->tWaveIndex += sprite->tWaveDelta;
    sprite->tWaveIndex &= 0xFF;
    // The snow divides by 64, which is +/-4 pixels and reads as a wobble. This
    // is +/-16, so the sideways travel IS the motion rather than a decoration
    // on a fall.
    sprite->x2 = gSineTable[sprite->tWaveIndex] / 16;

    x = (sprite->x + sprite->centerToCornerVecX + gSpriteCoordOffsetX) & 0x1FF;
    if (x & 0x100)
        x |= -0x100;

    if (x < -3)
        sprite->x = 242 - (gSpriteCoordOffsetX + sprite->centerToCornerVecX);
    else if (x > 242)
        sprite->x = -3 - (gSpriteCoordOffsetX + sprite->centerToCornerVecX);

    // Off the bottom: respawn at the top with a fresh drift. The snow relies on
    // its flakes being off screen quickly; a petal that hangs would otherwise
    // sit at the bottom edge for a long time.
    //
    // SCREEN SPACE, not sprite space. sprite->y is stored RELATIVE to
    // gSpriteCoordOffsetY -- InitPetalSpriteMovement seeds it as
    // -3 - (gSpriteCoordOffsetY + centerToCornerVecY), and coordOffsetEnabled
    // adds the offset back at draw time -- so comparing it raw against a screen
    // bound is only correct while that offset is zero. It is zero on map load
    // and stops being zero the moment the camera scrolls, which is why this
    // failed "as soon as you walk": with a negative offset the test fires early
    // and respawns every petal while it is still near the top, so the field
    // never crosses the screen. The x wrap directly above already converts
    // before it compares; this is the same conversion, which it was missing.
    //
    // Vanilla snow has no vertical test at all -- its flakes recycle on the
    // 8-bit OAM y wrap -- which is exactly why WEATHER_SNOW was unaffected while
    // both of the weathers written here were not.
    if (sprite->y + sprite->centerToCornerVecY + gSpriteCoordOffsetY > 163)
    {
        // A NEW LANE, not the one it started in. tPetalId picks the 30-pixel
        // column a petal enters from, and it is otherwise fixed for the sprite's
        // life - so every petal would fall down the same stripe forever, which
        // is invisible at a handful of sprites and turns into obvious vertical
        // lanes once there are enough of them on screen to notice.
        sprite->tPetalId = Random() & 7;
        InitPetalSpriteMovement(sprite);
    }
}

#undef tPosY
#undef tDeltaY
#undef tWaveDelta
#undef tWaveIndex
#undef tPetalId
#undef tRestCount
#undef tRestUntil
#undef tFallDeltaY

//------------------------------------------------------------------------------
// WEATHER_LEAVES
//------------------------------------------------------------------------------
//
// The woods dungeon's, and the first weather most runs ever show: a calm breezy
// day, leaves blowing lazily ACROSS the screen on a shallow diagonal.
//
// IT TRAVELS, AND THAT IS WHY IT IS NOT THE PETALS WITH SLOWER NUMBERS. This
// was written that way first and it was wrong. Every falling weather in vanilla
// moves sideways by writing sprite->x2 from gSineTable, and an oscillation has
// NO NET TRAVEL however wide it is made - a petal at /16 sways sixteen pixels
// and arrives directly below where it started. Leaves that blow across the
// screen have to move sprite->x itself, which is the blizzard's mechanism, so
// this borrows that and not the petals'.
//
// SO IT IS BOTH, AND THAT COMBINATION IS THE WEATHER. The blizzard has travel
// and no sway; the petals have sway and no travel; a leaf on a breeze has a net
// direction AND bobs on the way. Between 1.36 and 6.09 parts sideways to one
// part down, plus a +/-10 pixel sine on top. Simulated before it was built: a
// leaf is on screen for 11 to 25 seconds and crosses it three times on average,
// and the field stays spread over all four vertical quarters after five minutes
// rather than silting into the bottom band.
//
// SLOWED ~30% AGAINST POKEMON BLACK, whose falling leaves are the reference.
// All four speeds were cut together - fall, travel, rotation, gust peak - and
// the measurements say why that mattered: the sideways-to-down ratio came out
// unchanged at 1.36-6.09:1, and a gust is still exactly x3.0 the slowest leaf
// and x2.0 the fastest. Scaling one number alone would have tilted the angle or
// left the leaf spinning at a rate its travel no longer earns. The field drifts
// at 42 px/s now against 60 before.
//
// The sway rides on x2 and the travel on x, ON PURPOSE - see UpdateLeafSprite
// for why the wrap test must keep reading x alone.
//
// The other half of "lazy" is the stall the petals introduced, reused here with
// one change: a resting leaf slows VERTICALLY but keeps its sideways speed, so
// it planes off on the breeze instead of stopping dead. That is the moment the
// effect is built around.
//
// AND THEN THE WIND ITSELF GUSTS. Six seconds of calm, then a four-second swell
// that takes the field from 42 px/s to 82 px/s, eased in and out off a half
// sine so it never starts or stops abruptly. See UpdateLeafGust. This is what
// makes it a breezy DAY rather than a constant sideways rate, and it is worth
// noting how little it cost: four bytes of EWRAM and one addition in the sprite
// update, with no art, no palette entry, no extra sprite and no VRAM. The
// alternative considered was drawing wind streaks with vanilla's
// whirlwind_lines.png; the leaves reacting turned out to be the effect, and the
// streaks only the decoration on it.
//
// THE ROTATION IS WHY THIS IS 16x16 AND NOT 8x8. The petals fake turning over
// with a sine drift because at 8x8 there is no room to draw an orientation; the
// vanilla leaf sheet has nine real ones. A leaf also simply IS bigger than a
// blossom, and reading larger is most of what distinguishes the two on sight.
//
// It shares snowflakeSprites[] and its counters with the snow, the petals and
// the blizzard, exactly as those three share it with each other - one weather
// runs at a time, so the array is free whenever leaves are up.

static void UpdateLeafSprite(struct Sprite *);
static bool8 UpdateVisibleLeafSprites(void);
static bool8 CreateLeafSprite(void);
static bool8 DestroyLeafSprite(void);
static void InitLeafSpriteMovement(struct Sprite *);
static void UpdateLeafGust(void);

// THE GUST, AND IT IS SHARED RATHER THAN PER-SPRITE. A gust is one event
// crossing the whole screen, so every leaf reads the same extra speed out of
// one variable that Leaves_Main updates once a frame - not fourteen sprites
// each rolling their own, which would be turbulence again.
//
// Four bytes of EWRAM for the entire feature. There is no art, no palette
// entry, no sprite and no VRAM in it: the leaves already travel, and a gust is
// just that speed going up and coming back down.
EWRAM_DATA static u16 sLeafGustTimer = 0;
EWRAM_DATA static s16 sLeafGust = 0;

void Leaves_InitVars(void)
{
    gWeatherPtr->initStep = 0;
    gWeatherPtr->weatherGfxLoaded = FALSE;
    gWeatherPtr->targetColorMapIndex = 0;
    // Slower than the petals' 20. Nothing about this floor should arrive
    // briskly - it is the calm the rest of the run is measured against.
    gWeatherPtr->colorMapStepDelay = 24;
    gWeatherPtr->targetSnowflakeSpriteCount = NUM_LEAF_SPRITES;
    gWeatherPtr->snowflakeVisibleCounter = 0;
    // A floor OPENS CALM. The gust clock starts here and, because Leaves_Main
    // only ticks it after the field is up, InitAll's spin cannot burn through
    // it - so the player always arrives in the quiet part of the cycle and the
    // first gust is something that happens to them rather than something they
    // walked into.
    sLeafGustTimer = 0;
    sLeafGust = 0;
    Weather_SetBlendCoeffs(8, BASE_SHADOW_INTENSITY); // preserve shadow darkness
    gWeatherPtr->noShadows = FALSE;
}

void Leaves_InitAll(void)
{
    u16 i;

    Leaves_InitVars();
    while (gWeatherPtr->weatherGfxLoaded == FALSE)
    {
        Leaves_Main();
        for (i = 0; i < gWeatherPtr->snowflakeSpriteCount; i++)
            UpdateLeafSprite(gWeatherPtr->sprites.s1.snowflakeSprites[i]);
    }
}

// Frames from one gust to the next, and how long the swell lasts. The
// difference between them is the calm - 360 frames, six seconds - and the calm
// is the point: a gust only reads as a gust against stillness.
#define LEAF_GUST_PERIOD 600
#define LEAF_GUST_LENGTH 240
// Extra Q7 horizontal speed at the top of the swell, on top of a leaf's own
// 68-134. So the field roughly triples to 202-268, which is still well under
// the blizzard's 384-576 base - a push, not a gale. Cut from 192 by the same
// ~30% as everything else, so a gust stays the same MULTIPLE of the calm as it
// was before the slowdown rather than becoming a relatively bigger event.
#define LEAF_GUST_PEAK   134

void Leaves_Main(void)
{
    if (gWeatherPtr->initStep == 0)
    {
        // Its own Main rather than Petals_Main purely because the palette is
        // named here. PALTAG_WEATHER_2 has ONE slot, which is fine - petals and
        // leaves never run together - but it does mean the two cannot share a
        // Main the way the monsoon shares the rain's.
        LoadCustomWeatherSpritePalette(gLeavesWeatherPalette);
        if (!UpdateVisibleLeafSprites())
        {
            gWeatherPtr->weatherGfxLoaded = TRUE;
            gWeatherPtr->initStep++;
        }
        // RETURN, so the gust clock does not run during setup. Leaves_InitAll
        // spins this function about five hundred times to spread the field
        // before the floor is drawn, and a clock ticking through that would put
        // the player on the floor at an arbitrary point in the cycle - usually
        // mid-gust, since the swell is 40% of it.
        return;
    }

    UpdateLeafGust();
}

// One frame of the gust cycle. Called once per frame from Leaves_Main, and its
// whole output is sLeafGust, which every leaf adds to its own speed.
static void UpdateLeafGust(void)
{
    u32 phase;

    if (++sLeafGustTimer >= LEAF_GUST_PERIOD)
        sLeafGustTimer = 0;

    if (sLeafGustTimer >= LEAF_GUST_LENGTH)
    {
        sLeafGust = 0;
        return;
    }

    // HALF A SINE, not a linear ramp: index 0 to 128 of gSineTable runs 0 to
    // 256 and back to 0, so one expression gives a swell that eases in, peaks,
    // and eases out. A linear ramp starts and stops the wind abruptly at both
    // ends, which is exactly the thing that would stop reading as a breeze.
    phase = (u32)sLeafGustTimer * 128 / LEAF_GUST_LENGTH;
    sLeafGust = (LEAF_GUST_PEAK * gSineTable[phase]) >> 8;
}

bool8 Leaves_Finish(void)
{
    switch (gWeatherPtr->finishStep)
    {
    case 0:
        gWeatherPtr->targetSnowflakeSpriteCount = 0;
        gWeatherPtr->snowflakeVisibleCounter = 0;
        gWeatherPtr->finishStep++;
        // fall through
    case 1:
        if (!UpdateVisibleLeafSprites())
        {
            gWeatherPtr->finishStep++;
            return FALSE;
        }
        return TRUE;
    }

    return FALSE;
}

static bool8 UpdateVisibleLeafSprites(void)
{
    if (gWeatherPtr->snowflakeSpriteCount == gWeatherPtr->targetSnowflakeSpriteCount)
        return FALSE;

    if (++gWeatherPtr->snowflakeVisibleCounter > 36)
    {
        gWeatherPtr->snowflakeVisibleCounter = 0;
        if (gWeatherPtr->snowflakeSpriteCount < gWeatherPtr->targetSnowflakeSpriteCount)
            CreateLeafSprite();
        else
            DestroyLeafSprite();
    }

    return gWeatherPtr->snowflakeSpriteCount != gWeatherPtr->targetSnowflakeSpriteCount;
}

static const struct OamData sLeafSpriteOamData =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(16x16),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(16x16),
    .tileNum = 0,
    .priority = 1,
    .paletteNum = 0,
    .affineParam = 0,
};

// ONE ENTRY FOR NINE FRAMES, because the engine already does this. A
// SpriteFrameImage whose images[0].relativeFrames is set makes
// RequestSpriteFrameImageCopy compute `images[0].data + images[0].size * index`
// itself instead of indexing the array, which is exactly the base-plus-stride
// this needs - and overworld_ascending_frames is the macro for it, taking the
// frame size in 8x8 tiles (2x2 here, so 128 bytes).
//
// Written out as nine hand-computed offsets first. This is the same thing with
// the arithmetic in the engine rather than in a table that has to stay in step
// with the PNG, and it is the only form under which images[] is read solely at
// [0] - so the one-element array is not a latent out-of-bounds.
//
// It still depends on the nine frames being CONTIGUOUS and in OBJ order in the
// converted .4bpp, which was verified host-side; a wrong stride is a scrambled
// leaf, not a build error. The macro's name says overworld, but it is generic
// and lives in sprite.h; nothing about it is overworld-specific.
static const struct SpriteFrameImage sLeafSpriteImages[] =
{
    overworld_ascending_frames(gWeatherLeafTiles, 2, 2),
};

// How many frames the vanilla sheet holds. It CANNOT be ARRAY_COUNT of the
// array above any more - that is 1 now, by design - and the anim list below is
// the only other place the number appears. Getting it wrong is silent: too low
// and part of the rotation is never drawn, too high and the sprite reads past
// the end of the sheet into gLeavesWeatherPalette.
#define LEAF_FRAME_COUNT 9

// 17 ticks a frame is a full turn in 153, about two and a half seconds. Fast
// enough to be motion rather than a flicker between stills, slow enough that
// the leaf looks heavy.
//
// SCALED WITH THE TRAVEL, not chosen independently. It was 12 when the leaf
// moved 30% faster; a sprite that keeps spinning at the old rate while drifting
// slower stops looking like a leaf turning over as it goes and starts looking
// like a leaf being spun. Rotation has to stay tied to travel.
#define LEAF_ANIM_DELAY 17

static const union AnimCmd sLeafAnimCmd[] =
{
    ANIMCMD_FRAME(0, LEAF_ANIM_DELAY),
    ANIMCMD_FRAME(1, LEAF_ANIM_DELAY),
    ANIMCMD_FRAME(2, LEAF_ANIM_DELAY),
    ANIMCMD_FRAME(3, LEAF_ANIM_DELAY),
    ANIMCMD_FRAME(4, LEAF_ANIM_DELAY),
    ANIMCMD_FRAME(5, LEAF_ANIM_DELAY),
    ANIMCMD_FRAME(6, LEAF_ANIM_DELAY),
    ANIMCMD_FRAME(7, LEAF_ANIM_DELAY),
    ANIMCMD_FRAME(8, LEAF_ANIM_DELAY),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd *const sLeafAnimCmds[] =
{
    sLeafAnimCmd,
};

static const struct SpriteTemplate sLeafSpriteTemplate =
{
    .tileTag = TAG_NONE,
    .paletteTag = PALTAG_WEATHER_2,
    .oam = &sLeafSpriteOamData,
    .anims = sLeafAnimCmds,
    .images = sLeafSpriteImages,
    .callback = UpdateLeafSprite,
};

// EXACTLY EIGHT, WHICH IS ALL THERE IS - struct Sprite has s16 data[8] and the
// petals already used every one. Travel needs two more fields than sway does
// (tDeltaX and its sub-pixel remainder), so two of the petals' had to go:
//
//   tPetalId, the 30-pixel entry lane, is gone because it existed to stop
//   sprites clustering in columns and horizontal travel already prevents that -
//   the field spreads itself within a second. Entry x is just random now.
//
//   tRestUntil, the per-sprite stall threshold, is now the constant
//   LEAF_REST_AFTER, with tRestCount SEEDED randomly instead. Same variety in
//   when a leaf stalls, one field instead of two.
#define tPosY       data[0]
#define tDeltaY     data[1]
#define tFallDeltaY data[2]
#define tSubX       data[3]
#define tDeltaX     data[4]
#define tWaveIndex  data[5]
#define tWaveDelta  data[6]
#define tRestCount  data[7]

// Frames of falling before a leaf catches the air, and how long it planes for.
// Per-sprite variety comes from seeding tRestCount, not from varying these.
#define LEAF_REST_AFTER 70
#define LEAF_REST_FOR   60

static bool8 CreateLeafSprite(void)
{
    u8 spriteId = CreateSpriteAtEnd(&sLeafSpriteTemplate, 0, 0, 78);
    if (spriteId == MAX_SPRITES)
        return FALSE;

    InitLeafSpriteMovement(&gSprites[spriteId]);
    // ONE ANIM, ENTERED AT A RANDOM POINT, rather than nine anims that each
    // start on a different frame. Fourteen leaves all beginning at frame 0
    // would turn over in lockstep, which reads as a mechanism rather than as
    // weather - the petals get their phase variety from tWaveIndex and this is
    // the same problem one axis over.
    //
    // Safe to do at creation despite animBeginning being set by CreateSprite,
    // because SeekSpriteAnim clears that flag itself; otherwise the first
    // AnimateSprite would call BeginAnim and put every leaf back to frame 0.
    SeekSpriteAnim(&gSprites[spriteId], Random() % LEAF_FRAME_COUNT);
    gSprites[spriteId].coordOffsetEnabled = TRUE;
    gWeatherPtr->sprites.s1.snowflakeSprites[gWeatherPtr->snowflakeSpriteCount++] = &gSprites[spriteId];
    return TRUE;
}

static bool8 DestroyLeafSprite(void)
{
    if (gWeatherPtr->snowflakeSpriteCount)
    {
        DestroySprite(gWeatherPtr->sprites.s1.snowflakeSprites[--gWeatherPtr->snowflakeSpriteCount]);
        return TRUE;
    }

    return FALSE;
}

static void InitLeafSpriteMovement(struct Sprite *sprite)
{
    u16 rand = Random();

    sprite->y = -3 - (gSpriteCoordOffsetY + sprite->centerToCornerVecY);
    // Anywhere along the top. The petals and the blizzard both pick a 30-pixel
    // entry lane to stop sprites stacking in columns; leaves need no such thing
    // because they immediately start moving sideways at different speeds.
    sprite->x = (Random() % 240) - (gSpriteCoordOffsetX + sprite->centerToCornerVecX);
    // A FRACTION, NOT A POSITION - zero, not y * 128. See UpdateLeafSprite.
    sprite->tPosY = 0;
    // The BOB, and it is the only thing x2 carries. Travel is on x below.
    sprite->x2 = 0;

    // 22 to 50 in Q7, so a sixth to a bit under half a pixel a frame. The
    // slowest fall in the game by a wide margin - the petals are 28-56 and
    // vanilla snow is 64-79, so this is under HALF the speed of vanilla snow.
    //
    // Was 32-74, cut by ~30% against Pokemon Black's falling leaves as the
    // reference. Every other speed here was cut with it - see tDeltaX below,
    // LEAF_ANIM_DELAY and LEAF_GUST_PEAK - because scaling one in isolation
    // changes the ANGLE or leaves the leaf spinning at a rate its travel no
    // longer justifies. The ratios are what read; the absolute numbers are what
    // was asked for.
    sprite->tDeltaY = (rand & 7) * 4 + 22;
    sprite->tFallDeltaY = sprite->tDeltaY;

    // 68 to 134, about half to one pixel a frame, against the blizzard's
    // 384-576. THIS IS THE NUMBER THAT MAKES IT A BREEZE RATHER THAN A GALE,
    // and the first one to reach for if it reads wrong.
    //
    // Against tDeltaY above that is 1.36:1 to 6.09:1 sideways to down -
    // measured by porting this function to Python and running it, not
    // estimated. Cut from 96-192 by the same ~30% as the fall, which is why the
    // angle is unchanged: scaling both by the same factor slows the leaf
    // without tilting it, and the angle is the thing that says "blowing
    // across" rather than "dropping".
    //
    // The floor matters more than the ceiling. It was 64 originally, which put
    // the slowest-blowing fastest-falling corner at 0.86:1 - a leaf coming down
    // at 45 degrees. Keeping the floor high enough is what guarantees EVERY
    // leaf reads as crossing.
    //
    // Both speeds are rolled from the same u16 but off different bits, so a
    // fast-falling leaf is not also a fast-blowing one and the field crosses at
    // a spread of angles instead of as parallel lines.
    //
    // ALL ONE DIRECTION, never negative: a breeze has a direction, and leaves
    // blowing both ways at once reads as turbulence.
    sprite->tDeltaX = ((rand >> 3) & 3) * 22 + 68;
    sprite->tSubX = 0;

    sprite->tWaveIndex = Random() & 0xFF;
    sprite->tWaveDelta = ((rand >> 5) & 3) + 2;
    // SEEDED, NOT ZEROED. With the stall threshold now a shared constant this
    // is the only thing keeping fourteen leaves from all catching the air on
    // the same frame, which would read as one gust rather than as weather.
    sprite->tRestCount = Random() % LEAF_REST_AFTER;
}

static void UpdateLeafSprite(struct Sprite *sprite)
{
    s16 x;

    // The petals' stall, with the one change that matters here: only tDeltaY is
    // cut. A resting petal slows in the only axis it has and hangs; a resting
    // leaf keeps every bit of its sideways speed and PLANES OFF on the breeze,
    // which is what a leaf actually does when it catches air, and is the moment
    // this whole effect exists to produce.
    if (++sprite->tRestCount > LEAF_REST_AFTER)
    {
        sprite->tDeltaY = sprite->tFallDeltaY / 4;
        if (sprite->tRestCount > LEAF_REST_AFTER + LEAF_REST_FOR)
        {
            sprite->tDeltaY = sprite->tFallDeltaY;
            sprite->tRestCount = 0;
        }
    }

    // WHOLE PIXELS INTO y, FRACTION KEPT IN tPosY - the same shape as tSubX
    // below, and NOT what vanilla's snow, the petals or the blizzard do. Those
    // all seed tPosY with sprite->y * 128 and carry the POSITION in it.
    //
    // THAT OVERFLOWS, AND THIS IS THE BUG THE LEAVES SURFACED. sprite->y is
    // stored relative to gSpriteCoordOffsetY, which on a 48x48 dungeon reaches
    // about 600, so near the bottom of a floor y is around 613 - and 613 * 128
    // is 78464, which wraps a signed 16-bit data[] slot to 12928. sprite->y
    // comes back as 101, about -515 in screen terms: far above the top. The
    // respawn test at the end of this function then never fires, because it is
    // reading the wrapped value, so the sprite is not recycled - it creeps down
    // from off-screen at its fall speed and looks stuck at the top.
    //
    // LATENT IN ALL FOUR WEATHERS, not just this one. Time stranded is roughly
    // 65536 / tDeltaY frames, so the blizzard rides it out in about 9 seconds
    // and the petals in 27, which is why neither obviously misbehaves. The
    // leaves were deliberately made the slowest weather in the game and take
    // nearly 50, which is long enough to read as broken.
    //
    // The tSubX comment below had already worked this out for the x axis and
    // says in as many words that the y axis does it the other way. Nobody
    // carried it across.
    sprite->tPosY += sprite->tDeltaY;
    sprite->y += sprite->tPosY >> 7;
    sprite->tPosY &= 0x7F;

    // WHOLE PIXELS INTO x, FRACTION KEPT IN tSubX - the blizzard's accumulator,
    // and deliberately not the Q7 one the y axis uses. sprite->x is stored
    // relative to gSpriteCoordOffsetX, which grows without bound as the camera
    // scrolls, so an x * 128 accumulator in an s16 would overflow on a large
    // map. Keeping only the remainder bounds tSubX to 0..127 forever and leaves
    // sprite->x as the single authority the wrap below can rewrite.
    // tDeltaX is this leaf's own speed; sLeafGust is what the wind is doing to
    // everything at once. ADDED, not scaled, and that is the better read: an
    // absolute push is a larger PROPORTIONAL change to a slow leaf than a fast
    // one, so a gust gathers the stragglers up and the field briefly moves
    // together before spreading out again as it dies.
    sprite->tSubX += sprite->tDeltaX + sLeafGust;
    sprite->x += sprite->tSubX >> 7;
    sprite->tSubX &= 0x7F;

    // The bob, riding on top of the travel. +/-10 pixels, gentler than the
    // petals' +/-16, because there it was the entire sideways motion and here
    // it is a decoration on real movement - at the petals' width it stops
    // looking like a leaf on a breeze and starts looking like a wobble.
    sprite->tWaveIndex += sprite->tWaveDelta;
    sprite->tWaveIndex &= 0xFF;
    sprite->x2 = gSineTable[sprite->tWaveIndex] / 24;

    // x ALONE, WITHOUT x2, and that is not an oversight. x2 is a draw-time
    // offset; folding it in here would make the wrap point wander by +/-10
    // pixels with the bob, and worse, a sprite whose stored x is inside the
    // band could be pushed outside it by the sine and get rewritten mid-flight.
    // x is the position; x2 is how it is drawn. The blizzard keeps x2 at zero
    // and so never had to make this distinction; this weather does.
    x = (sprite->x + sprite->centerToCornerVecX + gSpriteCoordOffsetX) & 0x1FF;
    if (x & 0x100)
        x |= -0x100;

    // Blown off the right edge, back on at the left. This is the main event
    // now, several times per leaf per descent, where for the snow it only ever
    // fires when the camera moves.
    if (x < -3)
        sprite->x = 242 - (gSpriteCoordOffsetX + sprite->centerToCornerVecX);
    else if (x > 242)
        sprite->x = -3 - (gSpriteCoordOffsetX + sprite->centerToCornerVecX);

    // SCREEN SPACE, not sprite space - sprite->y is stored relative to
    // gSpriteCoordOffsetY and coordOffsetEnabled adds it back at draw time, so
    // the raw field is only comparable to a screen bound while the camera has
    // not moved. Getting this wrong is the bug that made both of the weathers
    // written before this one respawn every sprite the moment the player
    // walked; see UpdatePetalSprite for the full account.
    //
    // Needed for the same reason the blizzard needs it: wrapping sideways keeps
    // a leaf alive indefinitely, so without this they all silt up in the bottom
    // band and the top of the screen empties out.
    if (sprite->y + sprite->centerToCornerVecY + gSpriteCoordOffsetY > 163)
        InitLeafSpriteMovement(sprite);
}

#undef tPosY
#undef tDeltaY
#undef tFallDeltaY
#undef tSubX
#undef tDeltaX
#undef tWaveIndex
#undef tWaveDelta
#undef tRestCount
#undef LEAF_REST_AFTER
#undef LEAF_REST_FOR
#undef LEAF_FRAME_COUNT
#undef LEAF_ANIM_DELAY
#undef LEAF_GUST_PERIOD
#undef LEAF_GUST_LENGTH
#undef LEAF_GUST_PEAK

//------------------------------------------------------------------------------
// FLIERS: overworld Pokemon sprites driven as weather
//------------------------------------------------------------------------------
//
// A FOLLOWER POKEMON'S OVERWORLD SPRITE, DRIVEN AS WEATHER. Every other effect
// in this file owns a sprite sheet, declares an OamData, an anim table and a
// SpriteTemplate, and ships art. These ship none of that: they ask
// CreateObjectGraphicsSprite for an OBJ_EVENT_GFX_SPECIES(...) and drive the
// result with a callback of their own.
//
// TWO WEATHERS RUN ON THIS ONE IMPLEMENTATION and the difference between them
// is a TABLE, not code - WEATHER_ZUBATS in the cave and WEATHER_SEABIRDS over
// the ocean. That is deliberate and it is the second time round: the zubats
// were written standalone, and the moment a second flying weather existed the
// choice was to generalise or to copy 250 lines. This file already records what
// copying a weather costs - the Q7 accumulator bug reached three weathers that
// way, because each was written by copying the one next to it.
//
// WHY A WEATHER AND NOT OBJECT EVENTS, which is the obvious way to put a bird
// on a floor and is the wrong one three times over:
//
//   1. THE ENGINE DESTROYS OBJECT EVENTS OFF-SCREEN. A flier whose whole point
//      is to leave the screen would be torn down mid-exit and never return.
//      Plain sprites are not managed that way; these live exactly as long as
//      the weather does.
//   2. An object event is placed on a metatile and walks the grid. A bird
//      should cross the screen in the air, ignoring both.
//   3. Object events are a fixed, declared set per map. Fliers arrive and leave
//      continuously, which is not a thing that set can express.
//
// WORLD SPACE, VIA coordOffsetEnabled = TRUE, AND THIS WAS WRONG ONCE. These
// were first written with it left FALSE - which is what
// CreateObjectGraphicsSprite hands back, and what the RAIN does - on the
// reasoning that a weather belongs to the view rather than to a tile. On screen
// that reads as the birds being glued to the player: a screen-space sprite holds
// its screen position while the camera scrolls, so it travels with them.
//
// Every other weather sprite in this file is world-anchored. The clouds, the
// snow, the petals, the leaves and the ash all set coordOffsetEnabled = TRUE;
// the rain is the single exception, and it gets away with it because a drop is
// small, fast and on screen for a handful of frames. A bird is large, slow and
// visible for ten seconds, which is ample time for the camera to drag it about.
// COPYING THE ONE OUTLIER IS WHAT WENT WRONG - count which way the rest of a
// family goes before following one member of it.
//
// The consequence is that sprite->x and sprite->y are no longer screen
// coordinates: coordOffsetEnabled adds gSpriteCoordOffset back at DRAW time, so
// every bound test below converts before it compares. That is the same
// correction the petals and the blizzard each needed for their respawn tests,
// and their comments spell out the same trap.
//
// IT DOES NOT REINTRODUCE THE tPosY OVERFLOW that UpdateSnowflakeSprite
// documents, and that is worth being explicit about because it is the obvious
// worry. That bug was seeding a Q7 accumulator with the POSITION; these
// accumulators hold only the sub-pixel FRACTION and are masked back to 0..127
// every frame, so sprite->y growing to several hundred costs nothing.
//
// WHAT THIS COSTS. No art, no palette file, no EWRAM: the sprite pointers live
// in the tail of snowflakeSprites[], which is 101 entries and whose largest
// other user needs 16, and the counters are the snow's - one weather runs at a
// time, so both are free whenever fliers are up. Per SPECIES aloft it is one
// shared sheet and one OBJ palette, plus one more OBJ palette if that species
// reflects.
//
// THE LIFETIME IS THE WHOLE RISK, so it is stated once here and asserted in
// tools/rogue/check_flier_lifetime.py. CreateObjectGraphicsSprite allocates two
// things this file does not otherwise own:
//
//   a sprite SHEET, tagged COMP_OW_TILE_TAG_BASE + graphicsId, because
//   OW_GFX_COMPRESS is TRUE and compressed overworld graphics go through
//   LoadSheetGraphicsInfo; and
//
//   an OBJ PALETTE, because a species' graphicsInfo carries
//   OBJ_EVENT_PAL_TAG_DYNAMIC.
//
// A REFLECTION ADDS A THIRD: its own tinted palette. It adds no tiles, because
// it draws the flier's own - see CreateFlierReflection.
//
// DestroySprite frees NONE of them. It frees tiles only for a sprite with
// usingSheet == FALSE, and ours is TRUE, so destroying a flier and stopping
// there leaks a sheet and one or two palette slots every time the weather ends
// - sixteen OBJ palettes is not many, and a floor is left every ninety seconds.
//
// They are released with FieldEffectFree*IfUnused, which SCAN the live sprites
// and free only if nothing else holds them. That is what makes this safe next
// to a real follower: if the player is walking a Wingull of their own, it
// shares this exact sheet and palette, and a blind FreeSpriteTilesByTag would
// pull the graphics out from under it. The scans see the follower and decline.
//
// ORDER MATTERS AND IT IS THE OPPOSITE OF THE OBVIOUS ONE: destroy first, then
// free. DestroySprite clears inUse, which is what takes the flier itself out of
// the scan; freeing first would find the sprite still live and never free
// anything at all. FollowerSetGraphics makes the same scan work by clearing
// inUse by hand around the call, which is the same trick from the other side.

// Slots reserved in snowflakeSprites[] for fliers. Their REFLECTIONS live in
// the next FLIER_MAX slots, paired by index - sprite i's reflection is at
// FLIER_MAX + i - so a reflection needs no back-pointer and no data[] slot, and
// the two cannot get out of step. snowflakeSprites is 101 entries; this uses 16.
#define FLIER_MAX             8
#define FLIER_REFLECT_BASE    FLIER_MAX

// Screen bounds, in sprite-centre coordinates. centerToCornerVecX is -16 for a
// 32x32 object event sprite, so the sprite is fully off the left edge at -16
// and fully off the right at 256. Spawn and despawn sit outside those, so a
// flier is never seen appearing or vanishing.
#define FLIER_SPAWN_LEFT      -28
#define FLIER_SPAWN_RIGHT     268
#define FLIER_GONE_LEFT       -44
#define FLIER_GONE_RIGHT      284
// Once inside this band the flier has finished entering.
#define FLIER_ONSCREEN_LEFT    28
#define FLIER_ONSCREEN_RIGHT  212

// How often, during the wander, a flier may change its mind.
#define FLIER_TURN_INTERVAL    45

// Q7 change in horizontal speed per frame. A wander reversal is twice the
// wander speed, so a turn takes tens of frames and reads as banking round.
// Assigning the new speed outright reverses velocity inside ONE frame, which
// no flying thing does - this weather shipped that way once and it was the
// second of the two reasons it looked like it was bouncing.
#define FLIER_ACCEL             4

// Q7 vertical drift and the cap on it, plus how much one adjustment may change
// that SPEED. The random walk is on the VELOCITY and never on the position:
// jogging a speed is invisible because position is its integral, while
// assigning a position is a step discontinuity and reads as a hop.
#define FLIER_DRIFT_MAX        20
#define FLIER_DRIFT_STEP        7

// Drawn in front of the field's object events. A flier is in the air and
// everything else on a dungeon floor is on the ground.
#define FLIER_SUBPRIORITY       0
// The reflection goes behind everything, like a cloud - see CreateFlierReflection.
#define FLIER_REFLECT_SUBPRIORITY 0xFF

// How far below a flier its reflection sits when the flier is at the BOTTOM of
// its band, before the altitude term is added. 30 is vanilla's height - 2 for a
// 32x32 sprite, which is where SetUpReflection puts the reflection of something
// standing on water.
#define FLIER_REFLECT_OFFSET      30

// What one flying species does. ADDING A SPECIES IS A ROW HERE, NOT CODE.
struct RogueFlierKind
{
    u16 graphicsId;
    u8 maxAloft;            // how many of this species in the air at once
    u16 spawnDelay;         // frames between arrivals of this species
    s16 speedEnter;         // Q7 px/frame
    s16 speedWander;
    s16 speedLeave;
    s16 bobAmplitude;       // pixels, drawn on y2
    s16 bobSpeed;           // gSineTable steps per frame
    s16 yMin;               // the flight band, in screen rows
    s16 yMax;
    u16 wanderFrames;
    u16 reflectPalTag;      // TAG_NONE for a species that does not reflect
};

struct RogueFlierFlock
{
    const struct RogueFlierKind *kinds;
    u8 kindCount;
    bool8 clouds;           // does this weather also paint drifting clouds
};

// ---- the cave: zubats ------------------------------------------------------
// Unchanged numbers from when this was WEATHER_ZUBATS' own implementation. The
// bob is +/-4 over 256/3 = 85 frames; it was +/-10 to +/-17 over 26-37 frames
// once, which is a 34 pixel swing on a 160 pixel screen twice a second, and it
// read as bouncing on sight. gSineTable is Q8.8, so (gSineTable[i] * amp) >> 8
// is EXACTLY +/-amp - it is easy to write an amplitude without picturing it.
static const struct RogueFlierKind sZubatKinds[] =
{
    {
        .graphicsId = OBJ_EVENT_GFX_SPECIES(ZUBAT),
        .maxAloft = 3, .spawnDelay = 170,
        .speedEnter = 176, .speedWander = 56, .speedLeave = 272,
        .bobAmplitude = 4, .bobSpeed = 3,
        .yMin = 26, .yMax = 104,
        .wanderFrames = 180,
        .reflectPalTag = TAG_NONE,   // a cave has nothing to reflect in
    },
};

// ---- the ocean: gulls ------------------------------------------------------
// A FLOCK AND A LONER, which is the whole reading. Wingull arrive four at a
// time and often, cross fast, and wander briefly; a single Pelipper comes
// through about every twelve seconds, slower and lower, and does not really
// wander at all - it cruises. The size difference between the two sprites does
// most of the work for free.
static const struct RogueFlierKind sSeabirdKinds[] =
{
    {
        .graphicsId = OBJ_EVENT_GFX_SPECIES(WINGULL),
        .maxAloft = 4, .spawnDelay = 95,
        .speedEnter = 208, .speedWander = 72, .speedLeave = 288,
        .bobAmplitude = 5, .bobSpeed = 4,
        // High, and a narrow band: a flock holds a rough altitude together.
        .yMin = 20, .yMax = 68,
        .wanderFrames = 150,
        .reflectPalTag = PALTAG_FLIER_REFLECTION_1,
    },
    {
        .graphicsId = OBJ_EVENT_GFX_SPECIES(PELIPPER),
        .maxAloft = 1, .spawnDelay = 720,
        // Slower everywhere, and its "wander" is barely slower than its cruise,
        // so it crosses the screen in more or less a straight line.
        .speedEnter = 152, .speedWander = 112, .speedLeave = 176,
        // A big bird beats slowly. Larger amplitude, lower rate.
        .bobAmplitude = 7, .bobSpeed = 2,
        // LOWER THAN THE GULLS, deliberately - it passes beneath the flock, and
        // its reflection is therefore tighter beneath it. See ReflectionOffset.
        .yMin = 62, .yMax = 104,
        .wanderFrames = 90,
        .reflectPalTag = PALTAG_FLIER_REFLECTION_2,
    },
};

static const struct RogueFlierFlock sZubatFlock   = { sZubatKinds,   ARRAY_COUNT(sZubatKinds),   FALSE };
static const struct RogueFlierFlock sSeabirdFlock = { sSeabirdKinds, ARRAY_COUNT(sSeabirdKinds), TRUE  };

// Which flock is aloft. Set by the weather's InitVars and read by Main, Finish
// and every sprite update - one weather runs at a time, so a single pointer is
// the whole of the dispatch.
static const struct RogueFlierFlock *sFlierFlock = NULL;

static void UpdateFlierSprite(struct Sprite *);
static bool8 CreateFlierSprite(u32 kindIndex);
static void DestroyFlierSprite(u32 index);
static void InitFlierFlight(struct Sprite *, const struct RogueFlierKind *, bool32 fromLeft);
static void SetFlierFacing(struct Sprite *);
static void CreateFlierReflection(struct Sprite *, const struct RogueFlierKind *, u32 index);
static void LoadFlierReflectionPalette(struct Sprite *, struct Sprite *,
                                       const struct RogueFlierKind *);
static void UpdateFlierReflection(struct Sprite *flier, struct Sprite *reflection,
                                  const struct RogueFlierKind *kind);
static void Fliers_InitVars(const struct RogueFlierFlock *flock);
static void Fliers_Main(void);
static bool8 Fliers_Finish(void);

#define tSubX        data[0]   // Q7 remainder of horizontal travel
#define tDeltaX      data[1]   // Q7 pixels per frame, SIGNED - the current speed
#define tTargetDX    data[2]   // Q7, SIGNED - the speed tDeltaX is easing toward
#define tSubY        data[3]   // Q7 remainder of vertical drift
#define tDeltaY      data[4]   // Q7 pixels per frame, SIGNED - the slow climb
#define tWaveIndex   data[5]   // phase of the drawn bob
#define tPhaseKind   data[6]   // phase in the low nibble, kind index in the high
#define tTimer       data[7]

// PACKED, because eight data[] slots is all there is and the motion needs seven
// of them. Phase is 0-2 and kind is 0-3, so a nibble each is generous. The
// accessors exist so that no site ever open-codes the shift - which is the way
// packing like this normally goes wrong.
#define FLIER_PHASE(s)          ((s)->tPhaseKind & 0xF)
#define FLIER_KIND_INDEX(s)     ((s)->tPhaseKind >> 4)
#define FLIER_SET_PHASE(s, p)   ((s)->tPhaseKind = ((s)->tPhaseKind & 0xF0) | (p))
#define FLIER_SET_KIND(s, k)    ((s)->tPhaseKind = ((s)->tPhaseKind & 0x0F) | ((k) << 4))

// Where a flier is in its crossing. It ENTERS from one edge, WANDERS in view
// for a few seconds, then LEAVES - "flies around a little bit then leaves the
// screen" is three states, and writing it as three is what stops the wander
// from either never ending or never starting.
#define FLIER_PHASE_ENTER     0
#define FLIER_PHASE_WANDER    1
#define FLIER_PHASE_LEAVE     2

static const struct RogueFlierKind *FlierKind(struct Sprite *sprite)
{
    return &sFlierFlock->kinds[FLIER_KIND_INDEX(sprite)];
}

// The flier's index in snowflakeSprites[], or FLIER_MAX if it is not there.
// Searched rather than stored because the array is COMPACTED when a flier
// leaves, so an index kept in data[] goes stale the first time any other flier
// exits. Eight comparisons a frame is nothing.
static u32 FlierIndexOf(struct Sprite *sprite)
{
    u32 i;

    for (i = 0; i < gWeatherPtr->snowflakeSpriteCount; i++)
    {
        if (gWeatherPtr->sprites.s1.snowflakeSprites[i] == sprite)
            return i;
    }
    return FLIER_MAX;
}

static u32 CountAloft(u32 kindIndex)
{
    u32 i, n = 0;

    for (i = 0; i < gWeatherPtr->snowflakeSpriteCount; i++)
    {
        if (FLIER_KIND_INDEX(gWeatherPtr->sprites.s1.snowflakeSprites[i]) == kindIndex)
            n++;
    }
    return n;
}

static void Fliers_InitVars(const struct RogueFlierFlock *flock)
{
    sFlierFlock = flock;
    gWeatherPtr->initStep = 0;
    gWeatherPtr->weatherGfxLoaded = FALSE;
    gWeatherPtr->colorMapStepDelay = 20;
    gWeatherPtr->snowflakeSpriteCount = 0;
    gWeatherPtr->snowflakeTimer = 0;
    gWeatherPtr->noShadows = FALSE;

    if (flock->clouds)
    {
        // The CLOUDS' blend, not the usual one, and on a dungeon floor that is
        // free. Every other weather here sets (8, BASE_SHADOW_INTENSITY) with
        // the comment "preserve shadow darkness", because the second coefficient
        // IS the darkness of an object event's shadow - there is one global
        // BLDALPHA and the weather owns it. But CurrentMapHasShadows() returns
        // mapType != MAP_TYPE_UNDERGROUND, and every dungeon floor is
        // MAP_TYPE_UNDERGROUND, so no object on any of these maps casts one and
        // there is nothing for these coefficients to spoil. Checked rather than
        // assumed; on a surface map it would be a real trade.
        gWeatherPtr->targetColorMapIndex = 0;
        if (gWeatherPtr->cloudSpritesCreated == FALSE)
            Weather_SetBlendCoeffs(0, 16);
    }
    else
    {
        gWeatherPtr->targetColorMapIndex = 0;
        Weather_SetBlendCoeffs(8, BASE_SHADOW_INTENSITY); // preserve shadow darkness
    }
}

// NOTE THE ABSENCE OF THE SPIN that every falling weather's InitAll does. Those
// loop hundreds of times to spread their field before the floor is drawn,
// because arriving to a screen with every flake in a line across the top is
// worse than arriving to no flakes at all. Fliers are the opposite case: they
// are events rather than a field, and the floor SHOULD open with none in view
// and the first one arriving a second or two later. So this terminates on the
// first pass and the spawn clock does the rest.
//
// The clouds are the exception and are created immediately - they are a field,
// and a sea with no clouds that fades in is a different sea.
static void Fliers_Main(void)
{
    u32 i;

    if (gWeatherPtr->initStep == 0)
    {
        if (sFlierFlock->clouds)
            CreateCloudSprites();
        gWeatherPtr->initStep++;
        return;
    }
    if (gWeatherPtr->initStep == 1)
    {
        if (sFlierFlock->clouds)
        {
            // The clouds' own target: 12/16 source over 8/16 destination, which
            // is what makes a cloud read as painted onto what is under it rather
            // than floating over it. The reflections ride the same blend.
            Weather_SetTargetBlendCoeffs(12, 8, 1);
            gWeatherPtr->initStep++;
            return;
        }
        gWeatherPtr->weatherGfxLoaded = TRUE;
        gWeatherPtr->initStep++;
        return;
    }
    if (gWeatherPtr->initStep == 2 && sFlierFlock->clouds)
    {
        if (!Weather_UpdateBlend())
            return;
        gWeatherPtr->weatherGfxLoaded = TRUE;
        gWeatherPtr->initStep++;
        return;
    }

    // ONE CLOCK FOR EVERY SPECIES, each testing its own period against it,
    // rather than a timer per kind. struct Weather has two spare counters and
    // this would have fitted two species exactly - which is precisely the kind
    // of ceiling that turns the third species into a refactor.
    gWeatherPtr->snowflakeTimer++;
    for (i = 0; i < sFlierFlock->kindCount; i++)
    {
        const struct RogueFlierKind *kind = &sFlierFlock->kinds[i];

        if (gWeatherPtr->snowflakeTimer % kind->spawnDelay)
            continue;
        if (CountAloft(i) >= kind->maxAloft)
            continue;
        CreateFlierSprite(i);
    }
}

static bool8 Fliers_Finish(void)
{
    // Torn down in ONE step rather than fading the field out the way the leaves
    // and the petals do. Their sprites are anonymous and can be removed a few
    // at a time without anyone noticing; three birds disappearing one per second
    // would be watched. The weather change already fades the screen over this.
    while (gWeatherPtr->snowflakeSpriteCount)
        DestroyFlierSprite(gWeatherPtr->snowflakeSpriteCount - 1);

    if (sFlierFlock != NULL && sFlierFlock->clouds)
        DestroyCloudSprites();

    return FALSE;
}

// Everything this function allocates is released in DestroyFlierSprite, and the
// pairing is the subject of tools/rogue/check_flier_lifetime.py.
static bool8 CreateFlierSprite(u32 kindIndex)
{
    const struct RogueFlierKind *kind = &sFlierFlock->kinds[kindIndex];
    u32 index = gWeatherPtr->snowflakeSpriteCount;
    u8 spriteId;
    struct Sprite *sprite;
    bool32 fromLeft = (Random() & 1);

    if (index >= FLIER_MAX)
        return FALSE;

    // The one line this whole system is about. This resolves the species'
    // overworldData, loads the compressed sheet under
    // COMP_OW_TILE_TAG_BASE + graphicsId, allocates the dynamic palette, and
    // hands back a plain sprite running our callback - no object event, no
    // slot, no metatile.
    spriteId = CreateObjectGraphicsSprite(kind->graphicsId, UpdateFlierSprite,
                                          fromLeft ? FLIER_SPAWN_LEFT : FLIER_SPAWN_RIGHT,
                                          kind->yMin, FLIER_SUBPRIORITY);
    // MAX_SPRITES means the OAM is full, which on a dungeon floor with two
    // dozen object events and a follower is a real possibility rather than a
    // theoretical one. Returning quietly is right: a missing bird is invisible,
    // and the next spawn tick will try again.
    if (spriteId == MAX_SPRITES)
        return FALSE;

    sprite = &gSprites[spriteId];
    // BEFORE InitFlierFlight, which positions in world coordinates, and before
    // CreateFlierReflection, which copies the whole struct and would otherwise
    // inherit a screen-space reflection of a world-space bird.
    sprite->coordOffsetEnabled = TRUE;
    FLIER_SET_KIND(sprite, kindIndex);
    InitFlierFlight(sprite, kind, fromLeft);
    gWeatherPtr->sprites.s1.snowflakeSprites[index] = sprite;
    gWeatherPtr->sprites.s1.snowflakeSprites[FLIER_REFLECT_BASE + index] = NULL;
    gWeatherPtr->snowflakeSpriteCount++;

    // AFTER the flier is in the array, because the reflection is stored by the
    // flier's index and CreateFlierReflection is allowed to fail quietly.
    if (kind->reflectPalTag != TAG_NONE)
        CreateFlierReflection(sprite, kind, index);

    return TRUE;
}

// A BLUE TINT, the same shape as vanilla's ApplyPondFilter - which is static in
// field_effect_helpers.c, so it is reproduced here rather than exposed: it is
// nine lines, and exporting it would put a weather-only concern into a shared
// header. Blue is pushed up and red and green are left alone, which is what
// makes a reflection read as being IN water rather than merely dimmed.
static void ApplyFlierPondFilter(u8 paletteNum, u16 *dest)
{
    u32 i;
    s32 r, g, b;
    const u16 *src = gPlttBufferUnfaded + OBJ_PLTT_ID(paletteNum);

    *dest++ = *src++;   // copy transparency untouched
    for (i = 0; i < 16 - 1; i++)
    {
        u32 color = *src++;

        r = (color << 27) >> 27;
        g = (color << 22) >> 27;
        b = (color << 17) >> 27;
        b += 10;
        if (b > 31)
            b = 31;
        *dest++ = RGB2(r, g, b);
    }
}

// ONE TINTED PALETTE PER SPECIES, not per sprite: the tag comes from the kind,
// so four Wingull reflections share one slot. That matters, because sixteen OBJ
// palettes is the whole budget and the birds already hold one each.
//
// Reads the flier's palette out of gPlttBufferUnfaded, so the tint is taken from
// the species' true colours rather than from whatever the current weather fade
// has done to them - and UpdateSpritePaletteWithWeather then puts the new
// palette under the same fade as everything else.
static void LoadFlierReflectionPalette(struct Sprite *reflection, struct Sprite *flier,
                                       const struct RogueFlierKind *kind)
{
    u16 filtered[16];
    struct SpritePalette pal = { .tag = kind->reflectPalTag, .data = filtered };
    u32 paletteNum = IndexOfSpritePaletteTag(kind->reflectPalTag);

    if (paletteNum == 0xFF)
    {
        ApplyFlierPondFilter(flier->oam.paletteNum, filtered);
        paletteNum = LoadSpritePalette(&pal);
        // Out of OBJ palette slots. The reflection then draws in the bird's own
        // colours, which is wrong but not broken - an untinted reflection is a
        // great deal less noticeable than a missing one, and this releases no
        // resource it did not take.
        if (paletteNum == 0xFF)
            return;
        UpdateSpritePaletteWithWeather(paletteNum, TRUE);
    }
    reflection->oam.paletteNum = paletteNum;
}

// A REFLECTION IS THE SAME SPRITE, DRAWN AGAIN. CreateCopySpriteAt is a whole
// struct copy, so the reflection inherits the flier's sheet, its images and its
// OAM - and its ANIMS, which is the one part that has to be undone; see below - and because it keeps sheetTileStart and usingSheet, it
// draws the flier's own tiles. IT COSTS NO VRAM AT ALL. This is vanilla's
// SetUpReflection stripped of its object event: the parts that matter are the
// vertical flip, the tinted palette and the priority, and none of those need an
// ObjectEvent to exist.
//
// PAINTED ON, LIKE A CLOUD. objMode = ST_OAM_OBJ_BLEND with priority 3 is
// exactly what sCloudSpriteOamData carries, and it is what makes a cloud read as
// lying on the sea rather than floating above it. A reflection wants the same
// two things for the same reason, so it takes them from the same place.
static void CreateFlierReflection(struct Sprite *flier, const struct RogueFlierKind *kind,
                                  u32 index)
{
    u8 spriteId = CreateCopySpriteAt(flier, flier->x, flier->y, FLIER_REFLECT_SUBPRIORITY);
    struct Sprite *reflection;

    if (spriteId == MAX_SPRITES)
        return;   // no reflection this time; the bird is still fine without one

    reflection = &gSprites[spriteId];
    // The copy brought UpdateFlierSprite with it, and a reflection that ran the
    // flight logic would fly off on its own. It is driven from its flier's
    // update instead - see UpdateFlierReflection - so it needs no callback and,
    // more usefully, no back-pointer to find its flier with.
    reflection->callback = SpriteCallbackDummy;
    // ITS OWN ANIMATION HAS TO BE NEUTERED, and this is the line the first
    // version of this function dropped from vanilla's SetUpReflection. Without
    // it the reflection faced whichever way the bird happened to be flying when
    // it was born, for ever, and its wings beat out of step with the bird's.
    //
    // AnimateSprites runs `sprite->callback(sprite)` and THEN
    // `AnimateSprite(sprite)`. UpdateFlierSprite copies the flier's
    // oam.tileNum onto the reflection from the FLIER'S slot - but the
    // reflection is a whole-struct copy and still carries the flier's real anim
    // table, so when the loop reaches the reflection's own slot AnimateSprite
    // overwrites that tileNum from the reflection's own animation. Nothing ever
    // advances that animation toward a new facing, because the callback above is
    // a dummy: it just runs the walk cycle it was created mid-way through.
    //
    // gDummySpriteAnimTable is { ANIM_END } - an animation that finishes
    // immediately - so AnimateSprite has nothing to write and the copied frame
    // survives to the OAM. THE RULE IS GENERAL: a sprite whose frames are driven
    // from outside must not also be animating itself.
    reflection->anims = gDummySpriteAnimTable;
    StartSpriteAnim(reflection, 0);
    reflection->affineAnims = gDummySpriteAffineAnimTable;
    reflection->affineAnimBeginning = TRUE;
    reflection->oam.objMode = ST_OAM_OBJ_BLEND;
    reflection->oam.priority = 3;
    reflection->subspriteMode = SUBSPRITES_IGNORE_PRIORITY;
    reflection->subspriteTableNum = 0;
    reflection->usingSheet = TRUE;

    LoadFlierReflectionPalette(reflection, flier, kind);
    gWeatherPtr->sprites.s1.snowflakeSprites[FLIER_REFLECT_BASE + index] = reflection;
}

// DESTROY FIRST, THEN FREE. See the header comment: DestroySprite clears inUse,
// and that is what takes these sprites out of the scans below. Freeing first
// would find them still live and free nothing.
static void DestroyFlierSprite(u32 index)
{
    struct Sprite *sprite = gWeatherPtr->sprites.s1.snowflakeSprites[index];
    struct Sprite *reflection = gWeatherPtr->sprites.s1.snowflakeSprites[FLIER_REFLECT_BASE + index];
    u16 tileStart;
    u8 paletteNum;
    bool32 usingSheet;

    if (sprite == NULL)
        return;

    // THE REFLECTION FIRST, and it owns exactly one thing: its tinted palette.
    // It holds no tiles of its own - it was drawing the flier's - so there is
    // nothing here to free but the palette, and freeing tiles for it would be
    // freeing the flier's out from under the flier.
    if (reflection != NULL)
    {
        u8 reflectionPalette = reflection->oam.paletteNum;

        DestroySprite(reflection);
        FieldEffectFreePaletteIfUnused(reflectionPalette);
        gWeatherPtr->sprites.s1.snowflakeSprites[FLIER_REFLECT_BASE + index] = NULL;
    }

    tileStart = sprite->sheetTileStart;
    paletteNum = sprite->oam.paletteNum;
    usingSheet = sprite->usingSheet;

    DestroySprite(sprite);

    // usingSheet is TRUE whenever OW_GFX_COMPRESS is on, and then DestroySprite
    // frees nothing at all - its tile-freeing branch is the !usingSheet one.
    // Guarded rather than assumed because the flag is a build config.
    if (usingSheet)
        FieldEffectFreeTilesIfUnused(tileStart);
    FieldEffectFreePaletteIfUnused(paletteNum);

    // Compact, so snowflakeSpriteCount stays the live count and every index
    // below it is a live flier. The reflection half moves with its flier, which
    // is the whole reason the pairing is positional.
    gWeatherPtr->snowflakeSpriteCount--;
    gWeatherPtr->sprites.s1.snowflakeSprites[index] =
        gWeatherPtr->sprites.s1.snowflakeSprites[gWeatherPtr->snowflakeSpriteCount];
    gWeatherPtr->sprites.s1.snowflakeSprites[FLIER_REFLECT_BASE + index] =
        gWeatherPtr->sprites.s1.snowflakeSprites[FLIER_REFLECT_BASE + gWeatherPtr->snowflakeSpriteCount];
    gWeatherPtr->sprites.s1.snowflakeSprites[gWeatherPtr->snowflakeSpriteCount] = NULL;
    gWeatherPtr->sprites.s1.snowflakeSprites[FLIER_REFLECT_BASE + gWeatherPtr->snowflakeSpriteCount] = NULL;
}

static void InitFlierFlight(struct Sprite *sprite, const struct RogueFlierKind *kind,
                            bool32 fromLeft)
{
    u16 rand = Random();

    // WORLD COORDINATES: the screen position wanted, less the camera offset that
    // coordOffsetEnabled adds back at draw time. Written this way round rather
    // than as a raw number so the arithmetic states what it is doing.
    sprite->x = (fromLeft ? FLIER_SPAWN_LEFT : FLIER_SPAWN_RIGHT) - gSpriteCoordOffsetX;
    sprite->y = kind->yMin + (rand % (kind->yMax - kind->yMin)) - gSpriteCoordOffsetY;
    sprite->y2 = 0;
    sprite->tSubX = 0;
    sprite->tSubY = 0;
    // ARRIVES AT SPEED rather than accelerating from rest: tDeltaX is seeded
    // equal to its target, so the easing has nothing to do until the first
    // turn. Starting at zero would leave the bird crawling onto the screen over
    // the forty frames it takes to reach cruise, off-screen for most of them.
    sprite->tTargetDX = fromLeft ? kind->speedEnter : -kind->speedEnter;
    sprite->tDeltaX = sprite->tTargetDX;
    // A gentle climb or descent to begin with, so no two hold the same line
    // even before the wander starts jogging it.
    sprite->tDeltaY = ((rand >> 5) % (2 * FLIER_DRIFT_MAX + 1)) - FLIER_DRIFT_MAX;
    // Out of phase with each other, so two birds on screen never bob together.
    sprite->tWaveIndex = Random() & 0xFF;
    FLIER_SET_PHASE(sprite, FLIER_PHASE_ENTER);
    sprite->tTimer = 0;
    SetFlierFacing(sprite);
}

// Face the way it is travelling. ANIM_STD_GO_WEST and GO_EAST are the two-frame
// walking animations out of sAnimTable_Following, which on a bird is the wing
// beat - the follower sheet has no idle-in-flight anim, and the walk is the
// flap.
//
// StartSpriteAnimIfDifferent, NOT StartSpriteAnim, which is what lets this be
// called every frame: the plain version restarts the animation from frame 0, so
// calling it unconditionally would hold the wings on one frame forever. That
// matters because tDeltaX EASES through zero on a turn rather than jumping
// sign, so the frame the facing changes on is not something the caller knows.
static void SetFlierFacing(struct Sprite *sprite)
{
    StartSpriteAnimIfDifferent(sprite,
                               sprite->tDeltaX < 0 ? ANIM_STD_GO_WEST : ANIM_STD_GO_EAST);
}

// ALTITUDE DRIVES THE REFLECTION'S DISTANCE, and that is the whole trick for
// selling height on a flat map. Vanilla's reflection sits a fixed height - 2
// below its object, because that object is STANDING on the water. A bird is not:
// the higher it flies, the further away its reflection should be, so the offset
// grows as the bird climbs above the bottom of its band. A Pelipper cruising low
// gets a tight reflection under it and a Wingull up near the top of the screen
// gets a distant one, from one subtraction.
static void UpdateFlierReflection(struct Sprite *flier, struct Sprite *reflection,
                                  const struct RogueFlierKind *kind)
{
    // How far above the bottom of its band the bird currently is. SCREEN space,
    // because the band is a screen band - flier->y is world space now and raw it
    // would make the reflection distance wander with the camera.
    s16 altitude = kind->yMax - (flier->y + gSpriteCoordOffsetY);

    if (altitude < 0)
        altitude = 0;

    reflection->oam.shape = flier->oam.shape;
    reflection->oam.size = flier->oam.size;
    reflection->oam.tileNum = flier->oam.tileNum;   // the flier's own tiles
    // ST_OAM_VFLIP through matrixNum, which is where the flip lives for a
    // non-affine sprite. Copied from vanilla's UpdateObjectReflectionSprite.
    reflection->oam.matrixNum = flier->oam.matrixNum | ST_OAM_VFLIP;
    reflection->subspriteTables = flier->subspriteTables;
    reflection->invisible = flier->invisible;
    reflection->x = flier->x;
    // HALF the altitude, and the halving is load-bearing rather than taste.
    // A true mirror puts the reflection at 2*surface - y, which means the
    // offset grows at TWICE the rate the bird climbs; that is physically right
    // and unusable, because at the top of Pelipper's band it lands at row 176
    // on a 160-row screen. At half, the relationship survives - a bird high up
    // has a visibly more distant reflection than one skimming the water - and
    // both species stay on screen across their whole band.
    //
    // Adding the altitude at the FULL rate is the trap next door: flier->y then
    // cancels out of the sum entirely and the reflection pins itself to one
    // screen row for ever, which looks like the feature simply not working.
    reflection->y = flier->y + FLIER_REFLECT_OFFSET + (altitude >> 1);
    reflection->centerToCornerVecX = flier->centerToCornerVecX;
    reflection->centerToCornerVecY = flier->centerToCornerVecY;
    reflection->x2 = flier->x2;
    // THE BOB INVERTS, and it is free. Because the wingbeat rides on y2 rather
    // than on y, negating it here makes the reflected wings beat the opposite
    // way - which is what a reflection does, and which would have needed real
    // work if the bob had been folded into the position.
    reflection->y2 = -flier->y2;
}

static void UpdateFlierSprite(struct Sprite *sprite)
{
    const struct RogueFlierKind *kind = FlierKind(sprite);
    s16 screenX, screenY;
    u32 index;

    // EASE THE SPEED TOWARD ITS TARGET rather than assigning it. Turning used
    // to be tDeltaX = -tDeltaX, which reverses the velocity inside a single
    // frame - the bird stopped dead and went back the way it came between one
    // frame and the next.
    if (sprite->tDeltaX < sprite->tTargetDX)
    {
        sprite->tDeltaX += FLIER_ACCEL;
        if (sprite->tDeltaX > sprite->tTargetDX)
            sprite->tDeltaX = sprite->tTargetDX;
    }
    else if (sprite->tDeltaX > sprite->tTargetDX)
    {
        sprite->tDeltaX -= FLIER_ACCEL;
        if (sprite->tDeltaX < sprite->tTargetDX)
            sprite->tDeltaX = sprite->tTargetDX;
    }

    // WHOLE PIXELS INTO x AND y, FRACTIONS KEPT IN tSubX AND tSubY, the same
    // rule the blizzard and the leaves follow. The identity holds for NEGATIVE
    // deltas, which is the case the other weathers never exercise: >> 7 floors
    // toward negative infinity and & 0x7F yields the positive remainder, so
    // (v >> 7) * 128 + (v & 0x7F) == v either way. A bird flies in both
    // directions on both axes; a leaf only ever falls.
    sprite->tSubX += sprite->tDeltaX;
    sprite->x += sprite->tSubX >> 7;
    sprite->tSubX &= 0x7F;

    sprite->tSubY += sprite->tDeltaY;
    sprite->y += sprite->tSubY >> 7;
    sprite->tSubY &= 0x7F;

    // CONVERT ONCE, COMPARE EVERYWHERE. sprite->x and sprite->y are world
    // coordinates; coordOffsetEnabled adds the camera offset back at draw time,
    // so every bound below is only meaningful against the converted value. The
    // petals and the blizzard each shipped this comparison unconverted and each
    // failed the same way - correct on a map that had not scrolled yet, wrong
    // the moment the player walked.
    screenX = sprite->x + gSpriteCoordOffsetX;
    screenY = sprite->y + gSpriteCoordOffsetY;

    // Stay in the flight band by STEERING, not by clamping the position. A
    // clamp pins the sprite to the boundary and holds it there while the drift
    // keeps pushing; turning the velocity round instead makes the bird level off
    // and come back. Only reverse when it is actually heading further out, or a
    // bird sitting on the edge flips every frame.
    //
    // The band is a SCREEN band on purpose: it is what keeps the birds up in the
    // visible sky rather than at the player's feet, and a world band would have
    // no meaning on a floor the camera roams over.
    if (screenY < kind->yMin && sprite->tDeltaY < 0)
        sprite->tDeltaY = -sprite->tDeltaY;
    else if (screenY > kind->yMax && sprite->tDeltaY > 0)
        sprite->tDeltaY = -sprite->tDeltaY;

    // THE BOB IS DRAWN, NOT TRAVELLED - y2, and not y. Same split the leaves
    // make between x and x2: y is where the bird IS and what the drift, the band
    // test and the reflection's altitude all own; y2 is a wingbeat drawn on top.
    sprite->tWaveIndex = (sprite->tWaveIndex + kind->bobSpeed) & 0xFF;
    sprite->y2 = (gSineTable[sprite->tWaveIndex] * kind->bobAmplitude) >> 8;

    // Idempotent, so it can run every frame - see SetFlierFacing.
    SetFlierFacing(sprite);

    // OUT OF RANGE IN ANY PHASE, not just while leaving. This used to sit inside
    // the LEAVE case, which was sufficient while the birds were screen-anchored
    // and could only exit under their own power. World-anchored, the PLAYER can
    // now carry the screen away from a bird that is still entering or wandering,
    // and one stranded in those phases would never reach LEAVE - it would fly
    // straight on for ever, off-screen, holding a sprite slot and a palette.
    if (screenX < FLIER_GONE_LEFT || screenX > FLIER_GONE_RIGHT)
    {
        index = FlierIndexOf(sprite);
        if (index < FLIER_MAX)
            DestroyFlierSprite(index);
        return;
    }

    switch (FLIER_PHASE(sprite))
    {
    case FLIER_PHASE_ENTER:
        // Fully in view: settle into the wander. Tested against the band rather
        // than against a frame count so a bird that entered slowly still
        // arrives before it starts wandering.
        if (screenX > FLIER_ONSCREEN_LEFT && screenX < FLIER_ONSCREEN_RIGHT)
        {
            FLIER_SET_PHASE(sprite, FLIER_PHASE_WANDER);
            sprite->tTimer = kind->wanderFrames + (Random() & 0x7F);
            // The TARGET, not the speed. Slowing from the entry cruise to the
            // wander drift is itself a change the easing should smooth out.
            sprite->tTargetDX = sprite->tDeltaX < 0 ? -kind->speedWander : kind->speedWander;
        }
        break;

    case FLIER_PHASE_WANDER:
        if ((sprite->tTimer % FLIER_TURN_INTERVAL) == 0)
        {
            // Retarget and let the easing do the turn. SetFlierFacing is not
            // called from here: tDeltaX has not changed yet, so this is exactly
            // the frame on which it would be wrong.
            if (Random() & 1)
                sprite->tTargetDX = -sprite->tTargetDX;

            // A RANDOM WALK ON THE VELOCITY, NOT ON THE POSITION, and that is
            // the whole difference between a drift and a hop. Jogging the SPEED
            // is invisible because position is its integral; assigning the
            // position teleports the bird a few pixels and reads as bouncing.
            sprite->tDeltaY += (Random() % (2 * FLIER_DRIFT_STEP + 1)) - FLIER_DRIFT_STEP;
            if (sprite->tDeltaY > FLIER_DRIFT_MAX)
                sprite->tDeltaY = FLIER_DRIFT_MAX;
            else if (sprite->tDeltaY < -FLIER_DRIFT_MAX)
                sprite->tDeltaY = -FLIER_DRIFT_MAX;
        }

        // Turned back out of the screen mid-wander: cut it short rather than
        // letting the bird hover just off the edge where nobody can see it.
        if (screenX < FLIER_SPAWN_LEFT || screenX > FLIER_SPAWN_RIGHT)
            sprite->tTimer = 0;

        if (sprite->tTimer)
            sprite->tTimer--;
        else
        {
            FLIER_SET_PHASE(sprite, FLIER_PHASE_LEAVE);
            // Leave by the NEARER edge, so the exit is short and reads as the
            // bird going somewhere rather than crossing the whole screen again.
            sprite->tTargetDX = (screenX < DISPLAY_WIDTH / 2)
                              ? -kind->speedLeave : kind->speedLeave;
        }
        break;

    case FLIER_PHASE_LEAVE:
        // Nothing to do: the range test above the switch reclaims it, in this
        // phase and in every other.
        break;
    }

    // LAST, so the reflection mirrors this frame's position rather than last
    // frame's. Driven from here rather than from a callback of its own, which is
    // what lets the reflection carry no state at all.
    if (kind->reflectPalTag != TAG_NONE)
    {
        index = FlierIndexOf(sprite);
        if (index < FLIER_MAX)
        {
            struct Sprite *reflection =
                gWeatherPtr->sprites.s1.snowflakeSprites[FLIER_REFLECT_BASE + index];

            if (reflection != NULL)
                UpdateFlierReflection(sprite, reflection, kind);
        }
    }
}

// ---- the two weathers, which are now four lines each ------------------------

void Zubats_InitVars(void)
{
    Fliers_InitVars(&sZubatFlock);
}

void Zubats_InitAll(void)
{
    Zubats_InitVars();
    while (gWeatherPtr->weatherGfxLoaded == FALSE)
        Zubats_Main();
}

void Zubats_Main(void)
{
    Fliers_Main();
}

bool8 Zubats_Finish(void)
{
    return Fliers_Finish();
}

void Seabirds_InitVars(void)
{
    Fliers_InitVars(&sSeabirdFlock);
}

void Seabirds_InitAll(void)
{
    Seabirds_InitVars();
    while (gWeatherPtr->weatherGfxLoaded == FALSE)
        Seabirds_Main();
}

void Seabirds_Main(void)
{
    Fliers_Main();
}

bool8 Seabirds_Finish(void)
{
    return Fliers_Finish();
}

#undef tSubX
#undef tDeltaX
#undef tTargetDX
#undef tSubY
#undef tDeltaY
#undef tWaveIndex
#undef tPhaseKind
#undef tTimer
#undef FLIER_PHASE
#undef FLIER_KIND_INDEX
#undef FLIER_SET_PHASE
#undef FLIER_SET_KIND
#undef FLIER_PHASE_ENTER
#undef FLIER_PHASE_WANDER
#undef FLIER_PHASE_LEAVE
#undef FLIER_MAX
#undef FLIER_REFLECT_BASE
#undef FLIER_SPAWN_LEFT
#undef FLIER_SPAWN_RIGHT
#undef FLIER_GONE_LEFT
#undef FLIER_GONE_RIGHT
#undef FLIER_ONSCREEN_LEFT
#undef FLIER_ONSCREEN_RIGHT
#undef FLIER_TURN_INTERVAL
#undef FLIER_ACCEL
#undef FLIER_DRIFT_MAX
#undef FLIER_DRIFT_STEP
#undef FLIER_SUBPRIORITY
#undef FLIER_REFLECT_SUBPRIORITY
#undef FLIER_REFLECT_OFFSET

//------------------------------------------------------------------------------
// WEATHER_BLIZZARD
//------------------------------------------------------------------------------
//
// Driving snow across the last two floors of Glacia's dungeon. The snow's own
// sprites, art, storage and counters, given genuine horizontal VELOCITY.
//
// That is the whole difference and it is not a bigger version of the wobble.
// Every falling weather in this game moves sideways by writing sprite->x2 from
// gSineTable - snow at /64 is a 4-pixel shiver, petals at /16 a 16-pixel sway -
// and an oscillation has no net travel however wide you make it. A blizzard has
// to actually CROSS the screen, so it moves sprite->x itself.
//
// Cheaper than the petals were: they needed PALTAG_WEATHER_2 and their own
// palette, and this is snow, so PALTAG_WEATHER is already the right one and
// gWeatherSnow1Tiles is already the right art. No new asset of any kind.
//
// MECHANICAL, and here that is not a free choice. WEATHER_SNOW is already in
// battle_util.c's switch, so Glacia's floors hand every Ice type 1.5x Defence -
// and floors 93 and 94 are hers. A blizzard left out of that switch would
// quietly REMOVE her weather advantage on her own arena. A new weather is
// cosmetic until it is put in that switch; when it replaces a mechanical one,
// the default is a regression rather than a neutral omission.

static void UpdateBlizzardSprite(struct Sprite *);
static bool8 UpdateVisibleBlizzardSprites(void);
static bool8 CreateBlizzardSprite(void);
static bool8 DestroyBlizzardSprite(void);
static void InitBlizzardSpriteMovement(struct Sprite *);

void Blizzard_InitVars(void)
{
    gWeatherPtr->initStep = 0;
    gWeatherPtr->weatherGfxLoaded = FALSE;
    gWeatherPtr->targetColorMapIndex = 0;
    gWeatherPtr->colorMapStepDelay = 20;
    gWeatherPtr->targetSnowflakeSpriteCount = NUM_BLIZZARD_SPRITES;
    gWeatherPtr->snowflakeVisibleCounter = 0;
    Weather_SetBlendCoeffs(8, BASE_SHADOW_INTENSITY); // preserve shadow darkness
    gWeatherPtr->noShadows = FALSE;
}

void Blizzard_InitAll(void)
{
    u16 i;

    Blizzard_InitVars();
    // The spin is what stops all thirty arriving in a band across the top.
    // UpdateVisibleBlizzardSprites creates one per 36 counter ticks and this
    // loop ticks once per iteration, so the first flake is updated a thousand
    // times before the last is created and the field is already spread by the
    // time the floor is drawn. Straight from Snow_InitAll, and load-bearing for
    // the same reason.
    while (gWeatherPtr->weatherGfxLoaded == FALSE)
    {
        Blizzard_Main();
        for (i = 0; i < gWeatherPtr->snowflakeSpriteCount; i++)
            UpdateBlizzardSprite(gWeatherPtr->sprites.s1.snowflakeSprites[i]);
    }
}

void Blizzard_Main(void)
{
    if (gWeatherPtr->initStep == 0 && !UpdateVisibleBlizzardSprites())
    {
        gWeatherPtr->weatherGfxLoaded = TRUE;
        gWeatherPtr->initStep++;
    }
}

bool8 Blizzard_Finish(void)
{
    switch (gWeatherPtr->finishStep)
    {
    case 0:
        gWeatherPtr->targetSnowflakeSpriteCount = 0;
        gWeatherPtr->snowflakeVisibleCounter = 0;
        gWeatherPtr->finishStep++;
        // fall through
    case 1:
        if (!UpdateVisibleBlizzardSprites())
        {
            gWeatherPtr->finishStep++;
            return FALSE;
        }
        return TRUE;
    }

    return FALSE;
}

static bool8 UpdateVisibleBlizzardSprites(void)
{
    if (gWeatherPtr->snowflakeSpriteCount == gWeatherPtr->targetSnowflakeSpriteCount)
        return FALSE;

    if (++gWeatherPtr->snowflakeVisibleCounter > 36)
    {
        gWeatherPtr->snowflakeVisibleCounter = 0;
        if (gWeatherPtr->snowflakeSpriteCount < gWeatherPtr->targetSnowflakeSpriteCount)
            CreateBlizzardSprite();
        else
            DestroyBlizzardSprite();
    }

    return gWeatherPtr->snowflakeSpriteCount != gWeatherPtr->targetSnowflakeSpriteCount;
}

// The snow's template with this weather's callback on it. Everything else -
// PALTAG_WEATHER, the two 8x8 frames, the priority - is the snow's and correct.
static const struct SpriteTemplate sBlizzardSpriteTemplate =
{
    .tileTag = TAG_NONE,
    .paletteTag = PALTAG_WEATHER,
    .oam = &sSnowflakeSpriteOamData,
    .anims = sSnowflakeAnimCmds,
    .images = sSnowflakeSpriteImages,
    .callback = UpdateBlizzardSprite,
};

#define tPosY        data[0]
#define tDeltaY      data[1]
#define tSubX        data[2]
#define tDeltaX      data[3]
#define tBlizzardId  data[4]

static bool8 CreateBlizzardSprite(void)
{
    u8 spriteId = CreateSpriteAtEnd(&sBlizzardSpriteTemplate, 0, 0, 78);
    if (spriteId == MAX_SPRITES)
        return FALSE;

    gSprites[spriteId].tBlizzardId = gWeatherPtr->snowflakeSpriteCount;
    InitBlizzardSpriteMovement(&gSprites[spriteId]);
    gSprites[spriteId].coordOffsetEnabled = TRUE;
    gWeatherPtr->sprites.s1.snowflakeSprites[gWeatherPtr->snowflakeSpriteCount++] = &gSprites[spriteId];
    return TRUE;
}

static bool8 DestroyBlizzardSprite(void)
{
    if (gWeatherPtr->snowflakeSpriteCount)
    {
        DestroySprite(gWeatherPtr->sprites.s1.snowflakeSprites[--gWeatherPtr->snowflakeSpriteCount]);
        return TRUE;
    }

    return FALSE;
}

static void InitBlizzardSpriteMovement(struct Sprite *sprite)
{
    u16 rand;
    u16 x = ((sprite->tBlizzardId * 5) & 7) * 30 + (Random() % 30);

    sprite->y = -3 - (gSpriteCoordOffsetY + sprite->centerToCornerVecY);
    sprite->x = x - (gSpriteCoordOffsetX + sprite->centerToCornerVecX);
    // A FRACTION, NOT A POSITION - zero, not y * 128. See UpdateBlizzardSprite;
    // it is the same rule the tSubX comment below states for the x axis, which
    // this line spent a long time contradicting eight lines above it.
    sprite->tPosY = 0;
    // Zeroed and never written again. x2 is how every other falling weather
    // moves sideways, and a blizzard must not use it: it is an offset applied at
    // draw time and takes no part in the wrap test below, so a flake carried out
    // of frame on x2 would never be brought back.
    sprite->x2 = 0;
    rand = Random();

    // Falls about twice as fast as snow's 64-79 and travels sideways six times
    // faster than that again - 3 to 4.5 pixels a frame, against the snow's four
    // pixels of shiver TOTAL. The angle is what makes it read as driven rather
    // than heavy, so the horizontal figure is the one to tune first.
    sprite->tDeltaY = (rand & 3) * 16 + 96;
    // Drawn independently of the fall speed, so the field has flakes crossing
    // at different angles rather than a sheet of parallel lines.
    sprite->tDeltaX = ((rand >> 2) & 3) * 64 + 384;
    sprite->tSubX = 0;
    StartSpriteAnim(sprite, (rand & 1) ? 0 : 1);
}

static void UpdateBlizzardSprite(struct Sprite *sprite)
{
    s16 x;

    // WHOLE PIXELS INTO y, FRACTION KEPT IN tPosY - the same rule as tSubX
    // below, which this axis did not follow. See UpdateSnowflakeSprite.
    sprite->tPosY += sprite->tDeltaY;
    sprite->y += sprite->tPosY >> 7;
    sprite->tPosY &= 0x7F;

    // WHOLE PIXELS INTO x, FRACTION KEPT IN tSubX - deliberately not the Q7
    // accumulator the y axis uses. sprite->x is stored relative to
    // gSpriteCoordOffsetX, which grows without bound as the camera scrolls, so
    // an x * 128 accumulator in an s16 would overflow on a large map. Keeping
    // only the sub-pixel remainder bounds tSubX to 0..127 forever, and leaves
    // sprite->x as the single authority the wrap below can rewrite freely.
    sprite->tSubX += sprite->tDeltaX;
    sprite->x += sprite->tSubX >> 7;
    sprite->tSubX &= 0x7F;

    x = (sprite->x + sprite->centerToCornerVecX + gSpriteCoordOffsetX) & 0x1FF;
    if (x & 0x100)
        x |= -0x100;

    // The snow's wrap, unchanged, and it is why the blizzard needs no respawn
    // of its own: a flake blown off the right edge re-enters on the left and
    // keeps the field full. The snow only ever exercises this when the CAMERA
    // moves; here it is the main event, several times per flake per crossing.
    if (x < -3)
        sprite->x = 242 - (gSpriteCoordOffsetX + sprite->centerToCornerVecX);
    else if (x > 242)
        sprite->x = -3 - (gSpriteCoordOffsetX + sprite->centerToCornerVecX);

    // Off the bottom, back to the top with a fresh angle. Wrapping sideways
    // keeps a flake alive indefinitely, so without this every flake would end up
    // in the bottom band and the upper screen would empty out - which the snow
    // never has to think about, its flakes leaving downward almost immediately.
    //
    // SCREEN SPACE, not sprite space, for the reason spelled out on the petals'
    // copy of this test: sprite->y is relative to gSpriteCoordOffsetY, so raw it
    // is only right while the camera has not scrolled. The x wrap eight lines up
    // converts before comparing and this did not, in the same function.
    if (sprite->y + sprite->centerToCornerVecY + gSpriteCoordOffsetY > 163)
    {
        // A new entry lane too, for the reason the petals re-roll theirs:
        // tBlizzardId picks the 30-pixel column and is otherwise fixed for the
        // sprite's life, so keeping it would file every flake down one stripe.
        sprite->tBlizzardId = Random() & 7;
        InitBlizzardSpriteMovement(sprite);
    }
}

#undef tPosY
#undef tDeltaY
#undef tSubX
#undef tDeltaX
#undef tBlizzardId

//------------------------------------------------------------------------------
// WEATHER_RAIN_THUNDERSTORM
//------------------------------------------------------------------------------

enum {
    // This block of states is run only once
    // when first setting up the thunderstorm
    THUNDER_STATE_LOAD_RAIN,
    THUNDER_STATE_CREATE_RAIN,
    THUNDER_STATE_INIT_RAIN,
    THUNDER_STATE_WAIT_CHANGE,

    // The thunderstorm loops through these states,
    // not necessarily in order.
    THUNDER_STATE_NEW_CYCLE,
    THUNDER_STATE_NEW_CYCLE_WAIT,
    THUNDER_STATE_INIT_CYCLE_1,
    THUNDER_STATE_INIT_CYCLE_2,
    THUNDER_STATE_SHORT_BOLT,
    THUNDER_STATE_TRY_NEW_BOLT,
    THUNDER_STATE_WAIT_BOLT_SHORT,
    THUNDER_STATE_INIT_BOLT_LONG,
    THUNDER_STATE_WAIT_BOLT_LONG,
    THUNDER_STATE_FADE_BOLT_LONG,
    THUNDER_STATE_END_BOLT_LONG,
};

void Thunderstorm_InitVars(void)
{
    gWeatherPtr->initStep = THUNDER_STATE_LOAD_RAIN;
    gWeatherPtr->weatherGfxLoaded = FALSE;
    gWeatherPtr->rainSpriteVisibleCounter = 0;
    gWeatherPtr->rainSpriteVisibleDelay = 4;
    gWeatherPtr->isDownpour = FALSE;
    gWeatherPtr->targetRainSpriteCount = 16;
    gWeatherPtr->targetColorMapIndex = 3;
    gWeatherPtr->colorMapStepDelay = 20;
    gWeatherPtr->weatherGfxLoaded = FALSE;  // duplicate assignment
    gWeatherPtr->thunderEnqueued = FALSE;
    SetRainStrengthFromSoundEffect(SE_THUNDERSTORM);
    Weather_SetBlendCoeffs(8, BASE_SHADOW_INTENSITY); // preserve shadow darkness
    gWeatherPtr->noShadows = FALSE;
}

void Thunderstorm_InitAll(void)
{
    Thunderstorm_InitVars();
    while (gWeatherPtr->weatherGfxLoaded == FALSE)
        Thunderstorm_Main();
}

//------------------------------------------------------------------------------
// WEATHER_DOWNPOUR
//------------------------------------------------------------------------------

static void UpdateThunderSound(void);
static void EnqueueThunder(u16);

void Downpour_InitVars(void)
{
    gWeatherPtr->initStep = THUNDER_STATE_LOAD_RAIN;
    gWeatherPtr->weatherGfxLoaded = FALSE;
    gWeatherPtr->rainSpriteVisibleCounter = 0;
    gWeatherPtr->rainSpriteVisibleDelay = 4;
    gWeatherPtr->isDownpour = TRUE;
    gWeatherPtr->targetRainSpriteCount = 24;
    gWeatherPtr->targetColorMapIndex = 3;
    gWeatherPtr->colorMapStepDelay = 20;
    gWeatherPtr->weatherGfxLoaded = FALSE;  // duplicate assignment
    SetRainStrengthFromSoundEffect(SE_DOWNPOUR);
    Weather_SetBlendCoeffs(8, BASE_SHADOW_INTENSITY); // preserve shadow darkness
    gWeatherPtr->noShadows = FALSE;
}

void Downpour_InitAll(void)
{
    Downpour_InitVars();
    while (gWeatherPtr->weatherGfxLoaded == FALSE)
        Thunderstorm_Main();
}

//------------------------------------------------------------------------------
// WEATHER_MONSOON
//------------------------------------------------------------------------------

// The downpour, minus the lightning. Everything visible about the rain is the
// downpour's - 24 sprites falling fast and steep, SE_DOWNPOUR under them - and
// the only difference is which Main drives it.
//
// THAT SUBSTITUTION IS THE WHOLE WEATHER, and it works because Rain_Main's
// states 0, 1 and 2 are numerically THUNDER_STATE_LOAD_RAIN, _CREATE_RAIN and
// _INIT_RAIN. The two loops agree exactly for as long as there is rain to set
// up, and part at state 3: Thunderstorm_Main goes on to THUNDER_STATE_WAIT_
// CHANGE and the bolt cycle, while Rain_Main falls off the end of its switch
// and does nothing further. A monsoon is a downpour that stops after the setup
// the two share.
//
// It leans on that agreement rather than restating it, because a restatement is
// a second copy to keep true. Inserting a state at the front of either enum
// breaks this loudly - the rain never loads at all - rather than quietly, which
// is the right failure mode for a coincidence being relied on.
void Monsoon_InitVars(void)
{
    Downpour_InitVars();
    // Redundant today, Downpour_InitVars having set THUNDER_STATE_LOAD_RAIN and
    // that being 0. Written anyway: Rain_Main is what reads this field now, so
    // it should say the number Rain_Main means by it.
    gWeatherPtr->initStep = 0;
}

void Monsoon_InitAll(void)
{
    Monsoon_InitVars();
    while (gWeatherPtr->weatherGfxLoaded == FALSE)
        Rain_Main();
}

// In a given cycle, there will be some shorter bolts of lightning, potentially
// followed by a longer bolt. As a "regex", the pattern is:
//   (SHORT_BOLT){1,2}(LONG_BOLT)?
//
// Thunder only plays on the final bolt of the cycle.
void Thunderstorm_Main(void)
{
    UpdateThunderSound();
    switch (gWeatherPtr->initStep)
    {
    case THUNDER_STATE_LOAD_RAIN:
        LoadRainSpriteSheet();
        gWeatherPtr->initStep++;
        break;
    case THUNDER_STATE_CREATE_RAIN:
        if (!CreateRainSprite())
            gWeatherPtr->initStep++;
        break;
    case THUNDER_STATE_INIT_RAIN:
        if (!UpdateVisibleRainSprites())
        {
            gWeatherPtr->weatherGfxLoaded = TRUE;
            gWeatherPtr->initStep++;
        }
        break;
    case THUNDER_STATE_WAIT_CHANGE:
        if (gWeatherPtr->palProcessingState != WEATHER_PAL_STATE_CHANGING_WEATHER)
            gWeatherPtr->initStep = THUNDER_STATE_INIT_CYCLE_1;
        break;
    case THUNDER_STATE_NEW_CYCLE:
        gWeatherPtr->thunderAllowEnd = TRUE;
        gWeatherPtr->thunderTimer = (Random() % 360) + 360;
        gWeatherPtr->initStep++;
        // fall through
    case THUNDER_STATE_NEW_CYCLE_WAIT:
        // Wait between 360-720 frames before starting a new cycle.
        if (--gWeatherPtr->thunderTimer == 0)
            gWeatherPtr->initStep++;
        break;
    case THUNDER_STATE_INIT_CYCLE_1:
        gWeatherPtr->thunderAllowEnd = TRUE;
        gWeatherPtr->thunderLongBolt = Random() % 2;
        gWeatherPtr->initStep++;
        break;
    case THUNDER_STATE_INIT_CYCLE_2:
        gWeatherPtr->thunderShortBolts = (Random() & 1) + 1;
        gWeatherPtr->initStep++;
        // fall through
    case THUNDER_STATE_SHORT_BOLT:
        // Short bolt of lightning strikes.
        ApplyWeatherColorMapIfIdle(19);
        // If final lightning bolt, enqueue thunder.
        if (!gWeatherPtr->thunderLongBolt && gWeatherPtr->thunderShortBolts == 1)
            EnqueueThunder(20);

        gWeatherPtr->thunderTimer = (Random() % 3) + 6;
        gWeatherPtr->initStep++;
        break;
    case THUNDER_STATE_TRY_NEW_BOLT:
        if (--gWeatherPtr->thunderTimer == 0)
        {
            // Short bolt of lightning ends.
            ApplyWeatherColorMapIfIdle(3);
            gWeatherPtr->thunderAllowEnd = TRUE;
            if (--gWeatherPtr->thunderShortBolts != 0)
            {
                // Wait a little, then do another short bolt.
                gWeatherPtr->thunderTimer = (Random() % 16) + 60;
                gWeatherPtr->initStep = THUNDER_STATE_WAIT_BOLT_SHORT;
            }
            else if (!gWeatherPtr->thunderLongBolt)
            {
                // No more bolts, restart loop.
                gWeatherPtr->initStep = THUNDER_STATE_NEW_CYCLE;
            }
            else
            {
                // Set up long bolt.
                gWeatherPtr->initStep = THUNDER_STATE_INIT_BOLT_LONG;
            }
        }
        break;
    case THUNDER_STATE_WAIT_BOLT_SHORT:
        if (--gWeatherPtr->thunderTimer == 0)
            gWeatherPtr->initStep = THUNDER_STATE_SHORT_BOLT;
        break;
    case THUNDER_STATE_INIT_BOLT_LONG:
        gWeatherPtr->thunderTimer = (Random() % 16) + 60;
        gWeatherPtr->initStep++;
        break;
    case THUNDER_STATE_WAIT_BOLT_LONG:
        if (--gWeatherPtr->thunderTimer == 0)
        {
            // Do long bolt. Enqueue thunder with a potentially longer delay.
            EnqueueThunder(100);
            ApplyWeatherColorMapIfIdle(19);
            gWeatherPtr->thunderTimer = (Random() & 0xF) + 30;
            gWeatherPtr->initStep++;
        }
        break;
    case THUNDER_STATE_FADE_BOLT_LONG:
        if (--gWeatherPtr->thunderTimer == 0)
        {
            // Fade long bolt out over time.
            ApplyWeatherColorMapIfIdle_Gradual(19, 3, 5);
            gWeatherPtr->initStep++;
        }
        break;
    case THUNDER_STATE_END_BOLT_LONG:
        if (gWeatherPtr->palProcessingState == WEATHER_PAL_STATE_IDLE)
        {
            gWeatherPtr->thunderAllowEnd = TRUE;
            gWeatherPtr->initStep = THUNDER_STATE_NEW_CYCLE;
        }
        break;
    }
}

bool8 Thunderstorm_Finish(void)
{
    switch (gWeatherPtr->finishStep)
    {
    case 0:
        gWeatherPtr->thunderAllowEnd = FALSE;
        gWeatherPtr->finishStep++;
        // fall through
    case 1:
        Thunderstorm_Main();
        if (gWeatherPtr->thunderAllowEnd)
        {
            if (IsWeatherRainy(gWeatherPtr->nextWeather))
                return FALSE;

            gWeatherPtr->targetRainSpriteCount = 0;
            gWeatherPtr->finishStep++;
        }
        break;
    case 2:
        if (!UpdateVisibleRainSprites())
        {
            DestroyRainSprites();
            gWeatherPtr->thunderEnqueued = FALSE;
            gWeatherPtr->finishStep++;
            return FALSE;
        }
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

// Enqueue a thunder sound effect for at most `waitFrames` frames from now.
static void EnqueueThunder(u16 waitFrames)
{
    if (!gWeatherPtr->thunderEnqueued)
    {
        gWeatherPtr->thunderSETimer = Random() % waitFrames;
        gWeatherPtr->thunderEnqueued = TRUE;
    }
}

static void UpdateThunderSound(void)
{
    if (gWeatherPtr->thunderEnqueued == TRUE)
    {
        if (gWeatherPtr->thunderSETimer == 0)
        {
            if (IsSEPlaying())
                return;

            if (Random() & 1)
                PlaySE(SE_THUNDER);
            else
                PlaySE(SE_THUNDER2);

            gWeatherPtr->thunderEnqueued = FALSE;
        }
        else
        {
            gWeatherPtr->thunderSETimer--;
        }
    }
}

//------------------------------------------------------------------------------
// WEATHER_FOG_HORIZONTAL and WEATHER_UNDERWATER
//------------------------------------------------------------------------------

static const u16 sUnusedData[] = {0, 6, 6, 12, 18, 42, 300, 300};

static const struct OamData sOamData_FogH =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_BLEND,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(64x64),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(64x64),
    .tileNum = 0,
    .priority = 2,
    .paletteNum = 0,
    .affineParam = 0,
};

static const union AnimCmd sAnim_FogH_0[] =
{
    ANIMCMD_FRAME(0, 16),
    ANIMCMD_END,
};

static const union AnimCmd sAnim_FogH_1[] =
{
    ANIMCMD_FRAME(32, 16),
    ANIMCMD_END,
};

static const union AnimCmd sAnim_FogH_2[] =
{
    ANIMCMD_FRAME(64, 16),
    ANIMCMD_END,
};

static const union AnimCmd sAnim_FogH_3[] =
{
    ANIMCMD_FRAME(96, 16),
    ANIMCMD_END,
};

static const union AnimCmd sAnim_FogH_4[] =
{
    ANIMCMD_FRAME(128, 16),
    ANIMCMD_END,
};

static const union AnimCmd sAnim_FogH_5[] =
{
    ANIMCMD_FRAME(160, 16),
    ANIMCMD_END,
};

static const union AnimCmd *const sAnims_FogH[] =
{
    sAnim_FogH_0,
    sAnim_FogH_1,
    sAnim_FogH_2,
    sAnim_FogH_3,
    sAnim_FogH_4,
    sAnim_FogH_5,
};

static const union AffineAnimCmd sAffineAnim_FogH[] =
{
    AFFINEANIMCMD_FRAME(0x200, 0x200, 0, 0),
    AFFINEANIMCMD_END,
};

static const union AffineAnimCmd *const sAffineAnims_FogH[] =
{
    sAffineAnim_FogH,
};

static void FogHorizontalSpriteCallback(struct Sprite *);
static const struct SpriteTemplate sFogHorizontalSpriteTemplate =
{
    .tileTag = GFXTAG_FOG_H,
    .paletteTag = PALTAG_WEATHER,
    .oam = &sOamData_FogH,
    .anims = sAnims_FogH,
    .affineAnims = sAffineAnims_FogH,
    .callback = FogHorizontalSpriteCallback,
};

void FogHorizontal_Main(void);
static void CreateFogHorizontalSprites(void);
static void DestroyFogHorizontalSprites(void);

// Updates just the color of shadows to match special weather blending
u8 UpdateShadowColor(u16 color)
{
    u8 paletteNum = IndexOfSpritePaletteTag(TAG_WEATHER_START);
    u16 ALIGNED(4) tempBuffer[16];
    u16 blendedColor;
    if (paletteNum < 16)
    {
        u16 index = OBJ_PLTT_ID(paletteNum) + SHADOW_COLOR_INDEX;
        gPlttBufferUnfaded[index] = gPlttBufferFaded[index] = color;
        // Copy to temporary buffer, blend, and keep just the shadow color index
        CpuFastCopy(&gPlttBufferFaded[index - SHADOW_COLOR_INDEX], tempBuffer, PLTT_SIZE_4BPP);
        UpdateSpritePaletteWithTime(paletteNum);
        blendedColor = gPlttBufferFaded[index];
        CpuFastCopy(tempBuffer, &gPlttBufferFaded[index - SHADOW_COLOR_INDEX], PLTT_SIZE_4BPP);
        gPlttBufferFaded[index] = blendedColor;
    }
    return paletteNum;
}

void FogHorizontal_InitVars(void)
{
    gWeatherPtr->initStep = 0;
    gWeatherPtr->weatherGfxLoaded = FALSE;
    gWeatherPtr->targetColorMapIndex = 0;
    gWeatherPtr->colorMapStepDelay = 20;
    if (gWeatherPtr->fogHSpritesCreated == 0)
    {
        gWeatherPtr->fogHScrollCounter = 0;
        gWeatherPtr->fogHScrollOffset = 0;
        gWeatherPtr->fogHScrollPosX = 0;
        Weather_SetBlendCoeffs(0, 16);
    }
    gWeatherPtr->noShadows = FALSE;
}

void FogHorizontal_InitAll(void)
{
    FogHorizontal_InitVars();
    while (gWeatherPtr->weatherGfxLoaded == FALSE)
        FogHorizontal_Main();
}

void FogHorizontal_Main(void)
{
    gWeatherPtr->fogHScrollPosX = (gSpriteCoordOffsetX - gWeatherPtr->fogHScrollOffset) & 0xFF;
    if (++gWeatherPtr->fogHScrollCounter > 3)
    {
        gWeatherPtr->fogHScrollCounter = 0;
        gWeatherPtr->fogHScrollOffset++;
    }
    switch (gWeatherPtr->initStep)
    {
    case 0:
        CreateFogHorizontalSprites();
        if (gWeatherPtr->currWeather == WEATHER_FOG_HORIZONTAL)
        {
            Weather_SetTargetBlendCoeffs(12, 8, 3);
            UpdateShadowColor(RGB_GRAY);
        }
        else
        {
            Weather_SetTargetBlendCoeffs(4, 16, 0);
        }
        gWeatherPtr->initStep++;
        break;
    case 1:
        if (Weather_UpdateBlend())
        {
            gWeatherPtr->weatherGfxLoaded = TRUE;
            gWeatherPtr->initStep++;
        }
        break;
    }
}

bool8 FogHorizontal_Finish(void)
{
    gWeatherPtr->fogHScrollPosX = (gSpriteCoordOffsetX - gWeatherPtr->fogHScrollOffset) & 0xFF;
    if (++gWeatherPtr->fogHScrollCounter > 3)
    {
        gWeatherPtr->fogHScrollCounter = 0;
        gWeatherPtr->fogHScrollOffset++;
    }

    switch (gWeatherPtr->finishStep)
    {
    case 0:
        Weather_SetTargetBlendCoeffs(0, 16, 3);
        gWeatherPtr->finishStep++;
        break;
    case 1:
        if (Weather_UpdateBlend())
            gWeatherPtr->finishStep++;
        break;
    case 2:
        DestroyFogHorizontalSprites();
        gWeatherPtr->finishStep++;
        break;
    default:
        UpdateShadowColor(RGB_BLACK);
        return FALSE;
    }
    return TRUE;
}

#define tSpriteColumn data[0]

static void FogHorizontalSpriteCallback(struct Sprite *sprite)
{
    sprite->y2 = (u8)gSpriteCoordOffsetY;
    sprite->x = gWeatherPtr->fogHScrollPosX + 32 + sprite->tSpriteColumn * 64;
    if (sprite->x >= DISPLAY_WIDTH + 32)
    {
        sprite->x = (DISPLAY_WIDTH * 2) + gWeatherPtr->fogHScrollPosX - (4 - sprite->tSpriteColumn) * 64;
        sprite->x &= 0x1FF;
    }
}

static void CreateFogHorizontalSprites(void)
{
    u16 i;
    u8 spriteId;
    struct Sprite *sprite;

    if (!gWeatherPtr->fogHSpritesCreated)
    {
        struct SpriteSheet fogHorizontalSpriteSheet = {
            .data = gWeatherFogHorizontalTiles,
            .size = sizeof(gWeatherFogHorizontalTiles),
            .tag = GFXTAG_FOG_H,
        };
        LoadSpriteSheet(&fogHorizontalSpriteSheet);
        for (i = 0; i < NUM_FOG_HORIZONTAL_SPRITES; i++)
        {
            spriteId = CreateSpriteAtEnd(&sFogHorizontalSpriteTemplate, 0, 0, 0xFF);
            if (spriteId != MAX_SPRITES)
            {
                sprite = &gSprites[spriteId];
                sprite->tSpriteColumn = i % 5;
                sprite->x = (i % 5) * 64 + 32;
                sprite->y = (i / 5) * 64 + 32;
                gWeatherPtr->sprites.s2.fogHSprites[i] = sprite;
            }
            else
            {
                gWeatherPtr->sprites.s2.fogHSprites[i] = NULL;
            }
        }

        gWeatherPtr->fogHSpritesCreated = TRUE;
    }
}

static void DestroyFogHorizontalSprites(void)
{
    u16 i;

    if (gWeatherPtr->fogHSpritesCreated)
    {
        for (i = 0; i < NUM_FOG_HORIZONTAL_SPRITES; i++)
        {
            if (gWeatherPtr->sprites.s2.fogHSprites[i] != NULL)
                DestroySprite(gWeatherPtr->sprites.s2.fogHSprites[i]);
        }

        FreeSpriteTilesByTag(GFXTAG_FOG_H);
        gWeatherPtr->fogHSpritesCreated = 0;
    }
}

#undef tSpriteColumn

//------------------------------------------------------------------------------
// WEATHER_VOLCANIC_ASH
//------------------------------------------------------------------------------

static void LoadAshSpriteSheet(void);
static void CreateAshSprites(void);
static void DestroyAshSprites(void);
static void UpdateAshSprite(struct Sprite *);

void Ash_InitVars(void)
{
    gWeatherPtr->initStep = 0;
    gWeatherPtr->weatherGfxLoaded = FALSE;
    gWeatherPtr->targetColorMapIndex = 0;
    gWeatherPtr->colorMapStepDelay = 20;
    gWeatherPtr->ashUnused = 20; // Never read
    if (!gWeatherPtr->ashSpritesCreated)
    {
        Weather_SetBlendCoeffs(0, BASE_SHADOW_INTENSITY);
    }
    gWeatherPtr->noShadows = FALSE;
}

void Ash_InitAll(void)
{
    Ash_InitVars();
    while (gWeatherPtr->weatherGfxLoaded == FALSE)
        Ash_Main();
}

void Ash_Main(void)
{
    gWeatherPtr->ashBaseSpritesX = gSpriteCoordOffsetX & 0x1FF;
    while (gWeatherPtr->ashBaseSpritesX >= DISPLAY_WIDTH)
        gWeatherPtr->ashBaseSpritesX -= DISPLAY_WIDTH;

    switch (gWeatherPtr->initStep)
    {
    case 0:
        LoadAshSpriteSheet();
        gWeatherPtr->initStep++;
        break;
    case 1:
        if (!gWeatherPtr->ashSpritesCreated)
            CreateAshSprites();

        Weather_SetTargetBlendCoeffs(10, 12, 1);
        gWeatherPtr->initStep++;
        break;
    case 2:
        if (Weather_UpdateBlend())
        {
            gWeatherPtr->weatherGfxLoaded = TRUE;
            gWeatherPtr->initStep++;
        }
        break;
    default:
        Weather_UpdateBlend();
        break;
    }
}

bool8 Ash_Finish(void)
{
    switch (gWeatherPtr->finishStep)
    {
    case 0:
        Weather_SetTargetBlendCoeffs(0, 12, 1);
        gWeatherPtr->finishStep++;
        break;
    case 1:
        if (Weather_UpdateBlend())
        {
            DestroyAshSprites();
            gWeatherPtr->finishStep++;
        }
        break;
    case 2:
        SetGpuReg(REG_OFFSET_BLDALPHA, 0);
        gWeatherPtr->finishStep++;
        return FALSE;
    default:
        return FALSE;
    }
    return TRUE;
}

static const struct SpriteSheet sAshSpriteSheet =
{
    .data = gWeatherAshTiles,
    .size = sizeof(gWeatherAshTiles),
    .tag = GFXTAG_ASH,
};

static void LoadAshSpriteSheet(void)
{
    LoadSpriteSheet(&sAshSpriteSheet);
}

static const struct OamData sAshSpriteOamData =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_BLEND,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(64x64),
    .x = 0,
    .size = SPRITE_SIZE(64x64),
    .tileNum = 0,
    .priority = 1,
    .paletteNum = 15,
};

static const union AnimCmd sAshSpriteAnimCmd0[] =
{
    ANIMCMD_FRAME(0, 60),
    ANIMCMD_FRAME(64, 60),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd *const sAshSpriteAnimCmds[] =
{
    sAshSpriteAnimCmd0,
};

static const struct SpriteTemplate sAshSpriteTemplate =
{
    .tileTag = GFXTAG_ASH,
    .paletteTag = PALTAG_WEATHER,
    .oam = &sAshSpriteOamData,
    .anims = sAshSpriteAnimCmds,
    .callback = UpdateAshSprite,
};

#define tOffsetY      data[0]
#define tCounterY     data[1]
#define tSpriteColumn data[2]
#define tSpriteRow    data[3]

static void CreateAshSprites(void)
{
    u8 i;
    u8 spriteId;
    struct Sprite *sprite;

    if (!gWeatherPtr->ashSpritesCreated)
    {
        for (i = 0; i < NUM_ASH_SPRITES; i++)
        {
            spriteId = CreateSpriteAtEnd(&sAshSpriteTemplate, 0, 0, 0x4E);
            if (spriteId != MAX_SPRITES)
            {
                sprite = &gSprites[spriteId];
                sprite->tCounterY = 0;
                sprite->tSpriteColumn = (u8)(i % 5);
                sprite->tSpriteRow = (u8)(i / 5);
                sprite->tOffsetY = sprite->tSpriteRow * 64 + 32;
                gWeatherPtr->sprites.s2.ashSprites[i] = sprite;
            }
            else
            {
                gWeatherPtr->sprites.s2.ashSprites[i] = NULL;
            }
        }

        gWeatherPtr->ashSpritesCreated = TRUE;
    }
}

static void DestroyAshSprites(void)
{
    u16 i;

    if (gWeatherPtr->ashSpritesCreated)
    {
        for (i = 0; i < NUM_ASH_SPRITES; i++)
        {
            if (gWeatherPtr->sprites.s2.ashSprites[i] != NULL)
                DestroySprite(gWeatherPtr->sprites.s2.ashSprites[i]);
        }

        FreeSpriteTilesByTag(GFXTAG_ASH);
        gWeatherPtr->ashSpritesCreated = FALSE;
    }
}

static void UpdateAshSprite(struct Sprite *sprite)
{
    if (++sprite->tCounterY > 5)
    {
        sprite->tCounterY = 0;
        sprite->tOffsetY++;
    }

    sprite->y = gSpriteCoordOffsetY + sprite->tOffsetY;
    sprite->x = gWeatherPtr->ashBaseSpritesX + 32 + sprite->tSpriteColumn * 64;
    if (sprite->x >= DISPLAY_WIDTH + 32)
    {
        sprite->x = gWeatherPtr->ashBaseSpritesX + (DISPLAY_WIDTH * 2) - (4 - sprite->tSpriteColumn) * 64;
        sprite->x &= 0x1FF;
    }
}

#undef tOffsetY
#undef tCounterY
#undef tSpriteColumn
#undef tSpriteRow

//------------------------------------------------------------------------------
// WEATHER_FOG_DIAGONAL
//------------------------------------------------------------------------------

static void UpdateFogDiagonalMovement(void);
static void CreateFogDiagonalSprites(void);
static void DestroyFogDiagonalSprites(void);
static void UpdateFogDiagonalSprite(struct Sprite *);

void FogDiagonal_InitVars(void)
{
    gWeatherPtr->initStep = 0;
    gWeatherPtr->weatherGfxLoaded = 0;
    gWeatherPtr->targetColorMapIndex = 0;
    gWeatherPtr->colorMapStepDelay = 20;
    gWeatherPtr->fogHScrollCounter = 0;
    gWeatherPtr->fogHScrollOffset = 1;
    if (!gWeatherPtr->fogDSpritesCreated)
    {
        gWeatherPtr->fogDScrollXCounter = 0;
        gWeatherPtr->fogDScrollYCounter = 0;
        gWeatherPtr->fogDXOffset = 0;
        gWeatherPtr->fogDYOffset = 0;
        gWeatherPtr->fogDBaseSpritesX = 0;
        gWeatherPtr->fogDPosY = 0;
        Weather_SetBlendCoeffs(0, 16);
    }
    gWeatherPtr->noShadows = TRUE;
}

void FogDiagonal_InitAll(void)
{
    FogDiagonal_InitVars();
    while (gWeatherPtr->weatherGfxLoaded == FALSE)
        FogDiagonal_Main();
}

void FogDiagonal_Main(void)
{
    UpdateFogDiagonalMovement();
    switch (gWeatherPtr->initStep)
    {
    case 0:
        CreateFogDiagonalSprites();
        gWeatherPtr->initStep++;
        break;
    case 1:
        Weather_SetTargetBlendCoeffs(12, 8, 8);
        gWeatherPtr->initStep++;
        break;
    case 2:
        if (!Weather_UpdateBlend())
            break;
        gWeatherPtr->weatherGfxLoaded = TRUE;
        gWeatherPtr->initStep++;
        break;
    }
}

bool8 FogDiagonal_Finish(void)
{
    UpdateFogDiagonalMovement();
    switch (gWeatherPtr->finishStep)
    {
    case 0:
        Weather_SetTargetBlendCoeffs(0, 16, 1);
        gWeatherPtr->finishStep++;
        break;
    case 1:
        if (!Weather_UpdateBlend())
            break;
        gWeatherPtr->finishStep++;
        break;
    case 2:
        DestroyFogDiagonalSprites();
        gWeatherPtr->finishStep++;
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

static void UpdateFogDiagonalMovement(void)
{
    if (++gWeatherPtr->fogDScrollXCounter > 2)
    {
        gWeatherPtr->fogDXOffset++;
        gWeatherPtr->fogDScrollXCounter = 0;
    }

    if (++gWeatherPtr->fogDScrollYCounter > 4)
    {
        gWeatherPtr->fogDYOffset++;
        gWeatherPtr->fogDScrollYCounter = 0;
    }

    gWeatherPtr->fogDBaseSpritesX = (gSpriteCoordOffsetX - gWeatherPtr->fogDXOffset) & 0xFF;
    gWeatherPtr->fogDPosY = gSpriteCoordOffsetY + gWeatherPtr->fogDYOffset;
}

static const struct SpriteSheet sFogDiagonalSpriteSheet =
{
    .data = gWeatherFogDiagonalTiles,
    .size = sizeof(gWeatherFogDiagonalTiles),
    .tag = GFXTAG_FOG_D,
};

static const struct OamData sFogDiagonalSpriteOamData =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_BLEND,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(64x64),
    .x = 0,
    .size = SPRITE_SIZE(64x64),
    .tileNum = 0,
    .priority = 2,
    .paletteNum = 0,
};

static const union AnimCmd sFogDiagonalSpriteAnimCmd0[] =
{
    ANIMCMD_FRAME(0, 16),
    ANIMCMD_END,
};

static const union AnimCmd *const sFogDiagonalSpriteAnimCmds[] =
{
    sFogDiagonalSpriteAnimCmd0,
};

static const struct SpriteTemplate sFogDiagonalSpriteTemplate =
{
    .tileTag = GFXTAG_FOG_D,
    .paletteTag = PALTAG_WEATHER,
    .oam = &sFogDiagonalSpriteOamData,
    .anims = sFogDiagonalSpriteAnimCmds,
    .callback = UpdateFogDiagonalSprite,
};

#define tSpriteColumn data[0]
#define tSpriteRow    data[1]

static void CreateFogDiagonalSprites(void)
{
    u16 i;
    struct SpriteSheet fogDiagonalSpriteSheet;
    u8 spriteId;
    struct Sprite *sprite;

    if (!gWeatherPtr->fogDSpritesCreated)
    {
        fogDiagonalSpriteSheet = sFogDiagonalSpriteSheet;
        LoadSpriteSheet(&fogDiagonalSpriteSheet);
        for (i = 0; i < NUM_FOG_DIAGONAL_SPRITES; i++)
        {
            spriteId = CreateSpriteAtEnd(&sFogDiagonalSpriteTemplate, 0, (i / 5) * 64, 0xFF);
            if (spriteId != MAX_SPRITES)
            {
                sprite = &gSprites[spriteId];
                sprite->tSpriteColumn = i % 5;
                sprite->tSpriteRow = i / 5;
                gWeatherPtr->sprites.s2.fogDSprites[i] = sprite;
            }
            else
            {
                gWeatherPtr->sprites.s2.fogDSprites[i] = NULL;
            }
        }

        gWeatherPtr->fogDSpritesCreated = TRUE;
    }
}

static void DestroyFogDiagonalSprites(void)
{
    u16 i;

    if (gWeatherPtr->fogDSpritesCreated)
    {
        for (i = 0; i < NUM_FOG_DIAGONAL_SPRITES; i++)
        {
            if (gWeatherPtr->sprites.s2.fogDSprites[i])
                DestroySprite(gWeatherPtr->sprites.s2.fogDSprites[i]);
        }

        FreeSpriteTilesByTag(GFXTAG_FOG_D);
        gWeatherPtr->fogDSpritesCreated = FALSE;
    }
}

static void UpdateFogDiagonalSprite(struct Sprite *sprite)
{
    sprite->y2 = gWeatherPtr->fogDPosY;
    sprite->x = gWeatherPtr->fogDBaseSpritesX + 32 + sprite->tSpriteColumn * 64;
    if (sprite->x >= DISPLAY_WIDTH + 32)
    {
        sprite->x = gWeatherPtr->fogDBaseSpritesX + (DISPLAY_WIDTH * 2) - (4 - sprite->tSpriteColumn) * 64;
        sprite->x &= 0x1FF;
    }
}

#undef tSpriteColumn
#undef tSpriteRow

//------------------------------------------------------------------------------
// WEATHER_SANDSTORM
//------------------------------------------------------------------------------

static void UpdateSandstormWaveIndex(void);
static void UpdateSandstormMovement(void);
static void CreateSandstormSprites(void);
static void CreateSwirlSandstormSprites(void);
static void DestroySandstormSprites(void);
static void UpdateSandstormSprite(struct Sprite *);
static void WaitSandSwirlSpriteEntrance(struct Sprite *);
static void UpdateSandstormSwirlSprite(struct Sprite *);

#define MIN_SANDSTORM_WAVE_INDEX 0x20

void Sandstorm_InitVars(void)
{
    gWeatherPtr->initStep = 0;
    gWeatherPtr->weatherGfxLoaded = 0;
    gWeatherPtr->targetColorMapIndex = 0;
    gWeatherPtr->colorMapStepDelay = 20;
    if (!gWeatherPtr->sandstormSpritesCreated)
    {
        gWeatherPtr->sandstormXOffset = gWeatherPtr->sandstormYOffset = 0;
        gWeatherPtr->sandstormWaveIndex = 8;
        gWeatherPtr->sandstormWaveCounter = 0;
        // Dead code. How does the compiler not optimize this out?
        if (gWeatherPtr->sandstormWaveIndex >= 0x80 - MIN_SANDSTORM_WAVE_INDEX)
            gWeatherPtr->sandstormWaveIndex = 0x80 - gWeatherPtr->sandstormWaveIndex;

        Weather_SetBlendCoeffs(0, 16);
    }
    gWeatherPtr->noShadows = FALSE;
}

void Sandstorm_InitAll(void)
{
    Sandstorm_InitVars();
    while (!gWeatherPtr->weatherGfxLoaded)
        Sandstorm_Main();
}

void Sandstorm_Main(void)
{
    UpdateSandstormMovement();
    UpdateSandstormWaveIndex();
    if (gWeatherPtr->sandstormWaveIndex >= 0x80 - MIN_SANDSTORM_WAVE_INDEX)
        gWeatherPtr->sandstormWaveIndex = MIN_SANDSTORM_WAVE_INDEX;

    switch (gWeatherPtr->initStep)
    {
    case 0:
        CreateSandstormSprites();
        CreateSwirlSandstormSprites();
        gWeatherPtr->initStep++;
        break;
    case 1:
        Weather_SetTargetBlendCoeffs(16, 2, 0);
        UpdateShadowColor(RGB_GRAY);
        gWeatherPtr->initStep++;
        break;
    case 2:
        if (Weather_UpdateBlend())
        {
            gWeatherPtr->weatherGfxLoaded = TRUE;
            gWeatherPtr->initStep++;
        }
        break;
    }
}

bool8 Sandstorm_Finish(void)
{
    UpdateSandstormMovement();
    UpdateSandstormWaveIndex();
    switch (gWeatherPtr->finishStep)
    {
    case 0:
        Weather_SetTargetBlendCoeffs(0, 16, 0);
        gWeatherPtr->finishStep++;
        break;
    case 1:
        if (Weather_UpdateBlend())
            gWeatherPtr->finishStep++;
        if (gWeatherPtr->currBlendEVB == 12)
          UpdateShadowColor(RGB_BLACK);
        break;
    case 2:
        DestroySandstormSprites();
        UpdateShadowColor(RGB_BLACK);
        gWeatherPtr->finishStep++;
        break;
    default:
        return FALSE;
    }

    return TRUE;
}

static void UpdateSandstormWaveIndex(void)
{
    if (gWeatherPtr->sandstormWaveCounter++ > 4)
    {
        gWeatherPtr->sandstormWaveIndex++;
        gWeatherPtr->sandstormWaveCounter = 0;
    }
}

static void UpdateSandstormMovement(void)
{
    gWeatherPtr->sandstormXOffset -= gSineTable[gWeatherPtr->sandstormWaveIndex] * 4;
    gWeatherPtr->sandstormYOffset -= gSineTable[gWeatherPtr->sandstormWaveIndex];
    gWeatherPtr->sandstormBaseSpritesX = (gSpriteCoordOffsetX + (gWeatherPtr->sandstormXOffset >> 8)) & 0xFF;
    gWeatherPtr->sandstormPosY = gSpriteCoordOffsetY + (gWeatherPtr->sandstormYOffset >> 8);
}

static void DestroySandstormSprites(void)
{
    u16 i;

    if (gWeatherPtr->sandstormSpritesCreated)
    {
        for (i = 0; i < NUM_SANDSTORM_SPRITES; i++)
        {
            if (gWeatherPtr->sprites.s2.sandstormSprites1[i])
                DestroySprite(gWeatherPtr->sprites.s2.sandstormSprites1[i]);
        }

        gWeatherPtr->sandstormSpritesCreated = FALSE;
        FreeSpriteTilesByTag(GFXTAG_SANDSTORM);
    }

    if (gWeatherPtr->sandstormSwirlSpritesCreated)
    {
        for (i = 0; i < NUM_SWIRL_SANDSTORM_SPRITES; i++)
        {
            if (gWeatherPtr->sprites.s2.sandstormSprites2[i] != NULL)
                DestroySprite(gWeatherPtr->sprites.s2.sandstormSprites2[i]);
        }

        gWeatherPtr->sandstormSwirlSpritesCreated = FALSE;
    }
}

static const struct OamData sSandstormSpriteOamData =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_BLEND,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(64x64),
    .x = 0,
    .size = SPRITE_SIZE(64x64),
    .tileNum = 0,
    .priority = 1,
    .paletteNum = 0,
};

static const union AnimCmd sSandstormSpriteAnimCmd0[] =
{
    ANIMCMD_FRAME(0, 3),
    ANIMCMD_END,
};

static const union AnimCmd sSandstormSpriteAnimCmd1[] =
{
    ANIMCMD_FRAME(64, 3),
    ANIMCMD_END,
};

static const union AnimCmd *const sSandstormSpriteAnimCmds[] =
{
    sSandstormSpriteAnimCmd0,
    sSandstormSpriteAnimCmd1,
};

static const struct SpriteTemplate sSandstormSpriteTemplate =
{
    .tileTag = GFXTAG_SANDSTORM,
    .paletteTag = PALTAG_WEATHER_2,
    .oam = &sSandstormSpriteOamData,
    .anims = sSandstormSpriteAnimCmds,
    .callback = UpdateSandstormSprite,
};

static const struct SpriteSheet sSandstormSpriteSheet =
{
    .data = gWeatherSandstormTiles,
    .size = sizeof(gWeatherSandstormTiles),
    .tag = GFXTAG_SANDSTORM,
};

// Regular sandstorm sprites
#define tSpriteColumn  data[0]
#define tSpriteRow     data[1]

// Swirly sandstorm sprites
#define tRadius        data[0]
#define tWaveIndex     data[1]
#define tRadiusCounter data[2]
#define tEntranceDelay data[3]

static void CreateSandstormSprites(void)
{
    u16 i;
    u8 spriteId;

    if (!gWeatherPtr->sandstormSpritesCreated)
    {
        LoadSpriteSheet(&sSandstormSpriteSheet);
        LoadCustomWeatherSpritePalette(gSandstormWeatherPalette);
        for (i = 0; i < NUM_SANDSTORM_SPRITES; i++)
        {
            spriteId = CreateSpriteAtEnd(&sSandstormSpriteTemplate, 0, (i / 5) * 64, 1);
            if (spriteId != MAX_SPRITES)
            {
                gWeatherPtr->sprites.s2.sandstormSprites1[i] = &gSprites[spriteId];
                gWeatherPtr->sprites.s2.sandstormSprites1[i]->tSpriteColumn = i % 5;
                gWeatherPtr->sprites.s2.sandstormSprites1[i]->tSpriteRow = i / 5;
            }
            else
            {
                gWeatherPtr->sprites.s2.sandstormSprites1[i] = NULL;
            }
        }

        gWeatherPtr->sandstormSpritesCreated = TRUE;
    }
}

static const u16 sSwirlEntranceDelays[] = {0, 120, 80, 160, 40, 0};

static void CreateSwirlSandstormSprites(void)
{
    u16 i;
    u8 spriteId;

    if (!gWeatherPtr->sandstormSwirlSpritesCreated)
    {
        for (i = 0; i < NUM_SWIRL_SANDSTORM_SPRITES; i++)
        {
            spriteId = CreateSpriteAtEnd(&sSandstormSpriteTemplate, i * 48 + 24, 208, 1);
            if (spriteId != MAX_SPRITES)
            {
                gWeatherPtr->sprites.s2.sandstormSprites2[i] = &gSprites[spriteId];
                gWeatherPtr->sprites.s2.sandstormSprites2[i]->oam.size = ST_OAM_SIZE_2;
                gWeatherPtr->sprites.s2.sandstormSprites2[i]->tSpriteRow = i * 51;
                gWeatherPtr->sprites.s2.sandstormSprites2[i]->tRadius = 8;
                gWeatherPtr->sprites.s2.sandstormSprites2[i]->tRadiusCounter = 0;
                gWeatherPtr->sprites.s2.sandstormSprites2[i]->data[4] = 0x6730; // unused value
                gWeatherPtr->sprites.s2.sandstormSprites2[i]->tEntranceDelay = sSwirlEntranceDelays[i];
                StartSpriteAnim(gWeatherPtr->sprites.s2.sandstormSprites2[i], 1);
                CalcCenterToCornerVec(gWeatherPtr->sprites.s2.sandstormSprites2[i], SPRITE_SHAPE(32x32), SPRITE_SIZE(32x32), ST_OAM_AFFINE_OFF);
                gWeatherPtr->sprites.s2.sandstormSprites2[i]->callback = WaitSandSwirlSpriteEntrance;
            }
            else
            {
                gWeatherPtr->sprites.s2.sandstormSprites2[i] = NULL;
            }

            gWeatherPtr->sandstormSwirlSpritesCreated = TRUE;
        }
    }
}

static void UpdateSandstormSprite(struct Sprite *sprite)
{
    sprite->y2 = gWeatherPtr->sandstormPosY;
    sprite->x = gWeatherPtr->sandstormBaseSpritesX + 32 + sprite->tSpriteColumn * 64;
    if (sprite->x >= DISPLAY_WIDTH + 32)
    {
        sprite->x = gWeatherPtr->sandstormBaseSpritesX + (DISPLAY_WIDTH * 2) - (4 - sprite->tSpriteColumn) * 64;
        sprite->x &= 0x1FF;
    }
}

static void WaitSandSwirlSpriteEntrance(struct Sprite *sprite)
{
    if (--sprite->tEntranceDelay == -1)
        sprite->callback = UpdateSandstormSwirlSprite;
}

static void UpdateSandstormSwirlSprite(struct Sprite *sprite)
{
    u32 x, y;

    if (--sprite->y < -48)
    {
        sprite->y = DISPLAY_HEIGHT + 48;
        sprite->tRadius = 4;
    }

    x = sprite->tRadius * gSineTable[sprite->tWaveIndex];
    y = sprite->tRadius * gSineTable[sprite->tWaveIndex + 0x40];
    sprite->x2 = x >> 8;
    sprite->y2 = y >> 8;
    sprite->tWaveIndex = (sprite->tWaveIndex + 10) & 0xFF;
    if (++sprite->tRadiusCounter > 8)
    {
        sprite->tRadiusCounter = 0;
        sprite->tRadius++;
    }
}

#undef tSpriteColumn
#undef tSpriteRow

#undef tRadius
#undef tWaveIndex
#undef tRadiusCounter
#undef tEntranceDelay

//------------------------------------------------------------------------------
// WEATHER_SHADE
//------------------------------------------------------------------------------

void Shade_InitVars(void)
{
    gWeatherPtr->initStep = 0;
    gWeatherPtr->targetColorMapIndex = 3;
    gWeatherPtr->colorMapStepDelay = 20;
    Weather_SetBlendCoeffs(8, BASE_SHADOW_INTENSITY); // preserve shadow darkness
    gWeatherPtr->noShadows = FALSE;
}

void Shade_InitAll(void)
{
    Shade_InitVars();
}

void Shade_Main(void)
{
}

bool8 Shade_Finish(void)
{
    return FALSE;
}

//------------------------------------------------------------------------------
// WEATHER_UNDERWATER_BUBBLES
//------------------------------------------------------------------------------

static void CreateBubbleSprite(u16);
static void DestroyBubbleSprites(void);
static void UpdateBubbleSprite(struct Sprite *);

static const u8 sBubbleStartDelays[] = {40, 90, 60, 90, 2, 60, 40, 30};

static const struct SpriteSheet sWeatherBubbleSpriteSheet =
{
    .data = gWeatherBubbleTiles,
    .size = sizeof(gWeatherBubbleTiles),
    .tag = GFXTAG_BUBBLE,
};

static const s16 sBubbleStartCoords[][2] =
{
    {120, 160},
    {376, 160},
    { 40, 140},
    {296, 140},
    {180, 130},
    {436, 130},
    { 60, 160},
    {436, 160},
    {220, 180},
    {476, 180},
    { 10,  90},
    {266,  90},
    {256, 160},
};

void Bubbles_InitVars(void)
{
    FogHorizontal_InitVars();
    if (!gWeatherPtr->bubblesSpritesCreated)
    {
        LoadSpriteSheet(&sWeatherBubbleSpriteSheet);
        gWeatherPtr->bubblesDelayIndex = 0;
        gWeatherPtr->bubblesDelayCounter = sBubbleStartDelays[0];
        gWeatherPtr->bubblesCoordsIndex = 0;
        gWeatherPtr->bubblesSpriteCount = 0;
    }
    gWeatherPtr->noShadows = TRUE;
}

void Bubbles_InitAll(void)
{
    Bubbles_InitVars();
    while (!gWeatherPtr->weatherGfxLoaded)
        Bubbles_Main();
}

void Bubbles_Main(void)
{
    FogHorizontal_Main();
    if (++gWeatherPtr->bubblesDelayCounter > sBubbleStartDelays[gWeatherPtr->bubblesDelayIndex])
    {
        gWeatherPtr->bubblesDelayCounter = 0;
        if (++gWeatherPtr->bubblesDelayIndex > ARRAY_COUNT(sBubbleStartDelays) - 1)
            gWeatherPtr->bubblesDelayIndex = 0;

        CreateBubbleSprite(gWeatherPtr->bubblesCoordsIndex);
        if (++gWeatherPtr->bubblesCoordsIndex > ARRAY_COUNT(sBubbleStartCoords) - 1)
            gWeatherPtr->bubblesCoordsIndex = 0;
    }
}

bool8 Bubbles_Finish(void)
{
    if (!FogHorizontal_Finish())
    {
        DestroyBubbleSprites();
        return FALSE;
    }

    return TRUE;
}

static const union AnimCmd sBubbleSpriteAnimCmd0[] =
{
    ANIMCMD_FRAME(0, 16),
    ANIMCMD_FRAME(1, 16),
    ANIMCMD_END,
};

static const union AnimCmd *const sBubbleSpriteAnimCmds[] =
{
    sBubbleSpriteAnimCmd0,
};

static const struct SpriteTemplate sBubbleSpriteTemplate =
{
    .tileTag = GFXTAG_BUBBLE,
    .paletteTag = PALTAG_WEATHER,
    .oam = &gOamData_AffineOff_ObjNormal_8x8,
    .anims = sBubbleSpriteAnimCmds,
    .callback = UpdateBubbleSprite,
};

#define tScrollXCounter data[0]
#define tScrollXDir     data[1]
#define tCounter        data[2]

static void CreateBubbleSprite(u16 coordsIndex)
{
    s16 x = sBubbleStartCoords[coordsIndex][0];
    s16 y = sBubbleStartCoords[coordsIndex][1] - gSpriteCoordOffsetY;
    u8 spriteId = CreateSpriteAtEnd(&sBubbleSpriteTemplate, x, y, 0);
    if (spriteId != MAX_SPRITES)
    {
        gSprites[spriteId].oam.priority = 1;
        gSprites[spriteId].coordOffsetEnabled = TRUE;
        gSprites[spriteId].tScrollXCounter = 0;
        gSprites[spriteId].tScrollXDir = 0;
        gSprites[spriteId].tCounter = 0;
        gWeatherPtr->bubblesSpriteCount++;
    }
}

static void DestroyBubbleSprites(void)
{
    u16 i;

    if (gWeatherPtr->bubblesSpriteCount)
    {
        for (i = 0; i < MAX_SPRITES; i++)
        {
            if (gSprites[i].template == &sBubbleSpriteTemplate)
                DestroySprite(&gSprites[i]);
        }

        FreeSpriteTilesByTag(GFXTAG_BUBBLE);
        gWeatherPtr->bubblesSpriteCount = 0;
    }
}

static void UpdateBubbleSprite(struct Sprite *sprite)
{
    ++sprite->tScrollXCounter;
    if (++sprite->tScrollXCounter > 8) // double increment
    {
        sprite->tScrollXCounter = 0;
        if (sprite->tScrollXDir == 0)
        {
            if (++sprite->x2 > 4)
                sprite->tScrollXDir = 1;
        }
        else
        {
            if (--sprite->x2 <= 0)
                sprite->tScrollXDir = 0;
        }
    }

    sprite->y -= 3;
    if (++sprite->tCounter >= 120)
        DestroySprite(sprite);
}

#undef tScrollXCounter
#undef tScrollXDir
#undef tCounter

//------------------------------------------------------------------------------

#define tState         data[0]
#define tWeatherA      data[1]
#define tWeatherB      data[2]
#define tDelay         data[15]

static void Task_DoAbnormalWeather(u8 taskId)
{
    s16 *data = gTasks[taskId].data;

    switch (tState)
    {
    case 0:
        if (tDelay-- <= 0)
        {
            SetNextWeather(tWeatherA);
            sCurrentAbnormalWeather = tWeatherA;
            tDelay = 600;
            tState++;
        }
        break;
    case 1:
        if (tDelay-- <= 0)
        {
            SetNextWeather(tWeatherB);
            sCurrentAbnormalWeather = tWeatherB;
            tDelay = 600;
            tState = 0;
        }
        break;
    }
}

static void CreateAbnormalWeatherTask(void)
{
    u8 taskId = CreateTask(Task_DoAbnormalWeather, 0);
    s16 *data = gTasks[taskId].data;

    tDelay = 600;
    if (sCurrentAbnormalWeather == WEATHER_DOWNPOUR)
    {
        // Currently Downpour, next will be Drought
        tWeatherA = WEATHER_DROUGHT;
        tWeatherB = WEATHER_DOWNPOUR;
    }
    else if (sCurrentAbnormalWeather == WEATHER_DROUGHT)
    {
        // Currently Drought, next will be Downpour
        tWeatherA = WEATHER_DOWNPOUR;
        tWeatherB = WEATHER_DROUGHT;
    }
    else
    {
        // Default to starting with Downpour
        sCurrentAbnormalWeather = WEATHER_DOWNPOUR;
        tWeatherA = WEATHER_DROUGHT;
        tWeatherB = WEATHER_DOWNPOUR;
    }
}

#undef tState
#undef tWeatherA
#undef tWeatherB
#undef tDelay

static u8 TranslateWeatherNum(u8);
static void UpdateRainCounter(u8, u8);
static u8 GetDynamicWeather(void);

void SetSavedWeather(u32 weather)
{
    u8 oldWeather = gSaveBlock1Ptr->weather;
    gSaveBlock1Ptr->weather = TranslateWeatherNum(weather);
    UpdateRainCounter(gSaveBlock1Ptr->weather, oldWeather);
}

u8 GetSavedWeather(void)
{
    return gSaveBlock1Ptr->weather;
}

void SetSavedWeatherFromCurrMapHeader(void)
{
    u8 oldWeather = gSaveBlock1Ptr->weather;
    gSaveBlock1Ptr->weather = TranslateWeatherNum(gMapHeader.weather);
    UpdateRainCounter(gSaveBlock1Ptr->weather, oldWeather);
}

void SetWeather(u32 weather)
{
    SetSavedWeather(weather);
    SetNextWeather(GetSavedWeather());
}

void SetWeather_Unused(u32 weather)
{
    SetSavedWeather(weather);
    SetCurrentAndNextWeather(GetSavedWeather());
}

void DoCurrentWeather(void)
{
    u8 weather = GetSavedWeather();

    if (weather == WEATHER_ABNORMAL)
    {
        if (!FuncIsActiveTask(Task_DoAbnormalWeather))
            CreateAbnormalWeatherTask();
        weather = sCurrentAbnormalWeather;
    }
    else
    {
        if (FuncIsActiveTask(Task_DoAbnormalWeather))
            DestroyTask(FindTaskIdByFunc(Task_DoAbnormalWeather));
        sCurrentAbnormalWeather = WEATHER_DOWNPOUR;
    }
    SetNextWeather(weather);
}

void ResumePausedWeather(void)
{
    u8 weather = GetSavedWeather();

    if (weather == WEATHER_ABNORMAL)
    {
        if (!FuncIsActiveTask(Task_DoAbnormalWeather))
            CreateAbnormalWeatherTask();
        weather = sCurrentAbnormalWeather;
    }
    else
    {
        if (FuncIsActiveTask(Task_DoAbnormalWeather))
            DestroyTask(FindTaskIdByFunc(Task_DoAbnormalWeather));
        sCurrentAbnormalWeather = WEATHER_DOWNPOUR;
    }
    SetCurrentAndNextWeather(weather);
}

#define WEATHER_CYCLE_LENGTH  4

static const u8 sWeatherCycleRoute119[WEATHER_CYCLE_LENGTH] =
{
    WEATHER_SUNNY,
    WEATHER_RAIN,
    WEATHER_RAIN_THUNDERSTORM,
    WEATHER_RAIN,
};
static const u8 sWeatherCycleRoute123[WEATHER_CYCLE_LENGTH] =
{
    WEATHER_SUNNY,
    WEATHER_SUNNY,
    WEATHER_RAIN,
    WEATHER_SUNNY,
};

#define DYNAMIC_WEATHER_POOL(pool) pool, ARRAY_COUNT(pool)

struct DynamicWeatherPool
{
    mapsec_u16_t mapSec;
    const u8 *weathers;
    u8 count;
};

static const u8 sDefaultDynamicWeathers[] =
{
    WEATHER_SUNNY,
    WEATHER_RAIN,
    WEATHER_SNOW,
    WEATHER_SANDSTORM,
    WEATHER_VOLCANIC_ASH,
    WEATHER_RAIN_THUNDERSTORM,
    WEATHER_DROUGHT,
};

/*static const u8 sDynamicWeathers_DewfordTown[] =
{
    WEATHER_SUNNY,
    WEATHER_RAIN,
    WEATHER_RAIN_THUNDERSTORM,
};*/

// THE JUNGLE. Every entry is something a rainforest actually does: rain in two
// strengths, mist rising off the floor, canopy overcast, and humid sun breaking
// through. No ash, no sandstorm, no snow - a pool is a statement about a PLACE,
// not a list of the weathers that happen to be implemented.
//
// MOSTLY THIS CHANGES NOTHING IN BATTLE, and that is deliberate. The jungle has
// carried WEATHER_MONSOON since it was built, so every floor already set
// B_WEATHER_RAIN_NORMAL; WEATHER_RAIN sets the identical flag, so a floor that
// rolls one instead of the other plays exactly the same. The two that are not
// free are marked.
static const u8 sDynamicWeathers_Jungle[] =
{
    WEATHER_MONSOON,          // mechanical, and what every jungle floor was
    WEATHER_RAIN,             // mechanical, same B_WEATHER_RAIN_NORMAL as above
    // NOT FREE. B_OVERWORLD_FOG is GEN_LATEST, so this takes the >= GEN_8 branch
    // at battle_util.c:2751 and sets MISTY TERRAIN - halved Dragon damage and no
    // status on grounded battlers - rather than the fog condition its case label
    // suggests. Phoebe's floors already do this; the jungle did not until now.
    // Delete this line to make the jungle pool purely a rain pool again.
    WEATHER_FOG_HORIZONTAL,
    WEATHER_SUNNY_CLOUDS,     // cosmetic
    WEATHER_SHADE,            // cosmetic
};

// THE MURKY CAVE. Dark and wet, so: mist at two angles, gloom, and a bat flock.
// Nothing here is sunlit and nothing falls from a sky the player cannot see.
//
// WEATHER_ZUBATS costs nothing to reuse - it is ours, it is already built for
// DUNGEON_THEME_CAVE, and it draws follower Pokemon overworld sprites rather
// than a weather sheet, so a second theme using it adds no art and no VRAM. It
// appears on roughly one floor in four rather than every floor, which is the
// pool doing the work a fixed header could not.
static const u8 sDynamicWeathers_MurkyCave[] =
{
    WEATHER_FOG_HORIZONTAL,   // mechanical: Misty Terrain, see the jungle above
    WEATHER_FOG_DIAGONAL,     // mechanical: the same pair of cases
    WEATHER_SHADE,            // cosmetic
    WEATHER_ZUBATS,           // cosmetic
};

// KEYED ON mapSec, WHICH THIS PROJECT ALREADY WRITES PER THEME.
// RogueDungeon's generator puts theme->mapSecId into gMapHeader.regionMapSectionId
// on every generate, so two themes sharing MAP_ROGUE_DUNGEON_DYNAMIC still get
// completely different weather and neither needed a map of its own.
//
// A theme with no row here gets WEATHER_NONE - see GetDynamicWeatherPool.
static const struct DynamicWeatherPool sDynamicWeatherPools[] =
{
    { MAPSEC_ROGUE_JUNGLE,    DYNAMIC_WEATHER_POOL(sDynamicWeathers_Jungle) },
    { MAPSEC_ROGUE_MURKYCAVE, DYNAMIC_WEATHER_POOL(sDynamicWeathers_MurkyCave) },
};

static const u8 *GetDynamicWeatherPool(u8 *count)
{
    u16 mapSec = gMapHeader.regionMapSectionId;

    for (u32 i = 0; i < ARRAY_COUNT(sDynamicWeatherPools); i++)
    {
        if (sDynamicWeatherPools[i].mapSec == mapSec && sDynamicWeatherPools[i].count != 0)
        {
            *count = sDynamicWeatherPools[i].count;
            return sDynamicWeatherPools[i].weathers;
        }
    }

    // DELIBERATE DIVERGENCE FROM UPSTREAM: no row means NO weather.
    //
    // Upstream falls back to sDefaultDynamicWeathers here, which is SUNNY, RAIN,
    // SNOW, SANDSTORM, VOLCANIC_ASH, RAIN_THUNDERSTORM and DROUGHT - five of the
    // seven are cases in battle_util.c. On this branch that fallback is a
    // landmine: MAP_ROGUE_DUNGEON_DYNAMIC is shared by theme, so a theme pointed
    // at it without a pool row would silently get random sandstorm chip damage
    // and random drought sun on top of whatever its dungeon was balanced for.
    //
    // Returning 0 makes GetDynamicWeather answer WEATHER_NONE, so the feature is
    // OPT-IN: a theme gets exactly the weather its pool row lists, or none.
    // Nothing in the tree used WEATHER_DYNAMIC before this, so no behaviour
    // regressed when this changed.
    *count = 0;
    return NULL;
}

static u8 GetDynamicWeather(void)
{
    u8 count;
    const u8 *weathers = GetDynamicWeatherPool(&count);
    rng_value_t localRngState;
    // THE UPSTREAM FIVE ARE ALL CONSTANT WITHIN ONE DUNGEON ON THIS BRANCH, so
    // on their own they would make "dynamic" weather a fixed value.
    //
    // Every theme shares a map, so mapGroup and mapNum do not move; the dungeon
    // map declares one layout, so mapLayoutId does not move; and mapSecId is per
    // THEME, so it does not move between the floors of one dungeon either. That
    // leaves dailySeed, which clock.c sets once per real-world DAY - so all five
    // floors of a dungeon would share one weather, and so would every run played
    // that day. The feature would look broken in exactly the way that is hardest
    // to tell from "the roll came up the same".
    //
    // Adding the floor makes it vary down a dungeon; adding the run seed makes
    // two runs of the same dungeon on the same day differ. Both are ordinary
    // vars, so this costs nothing.
    const u32 hashPieces[] =
    {
        gSaveBlock1Ptr->dailySeed,
        gSaveBlock1Ptr->location.mapGroup,
        gSaveBlock1Ptr->location.mapNum,
        gMapHeader.mapLayoutId,
        gMapHeader.regionMapSectionId,
        VarGet(VAR_ROGUE_DUNGEON_SEED),
        VarGet(VAR_ROGUE_DUNGEON_FLOOR),
    };

    if (count == 0)
        return WEATHER_NONE;

    localRngState = LocalRandomSeed(Crc32B((const u8 *)hashPieces, sizeof(hashPieces)));
    return weathers[LocalRandom32(&localRngState) % count];
}

static u8 TranslateWeatherNum(u8 weather)
{
    switch (weather)
    {
    case WEATHER_NONE:               return WEATHER_NONE;
    case WEATHER_SUNNY_CLOUDS:       return WEATHER_SUNNY_CLOUDS;
    case WEATHER_SUNNY:              return WEATHER_SUNNY;
    case WEATHER_RAIN:               return WEATHER_RAIN;
    case WEATHER_SNOW:               return WEATHER_SNOW;
    case WEATHER_RAIN_THUNDERSTORM:  return WEATHER_RAIN_THUNDERSTORM;
    case WEATHER_FOG_HORIZONTAL:     return WEATHER_FOG_HORIZONTAL;
    case WEATHER_VOLCANIC_ASH:       return WEATHER_VOLCANIC_ASH;
    case WEATHER_SANDSTORM:          return WEATHER_SANDSTORM;
    case WEATHER_FOG_DIAGONAL:       return WEATHER_FOG_DIAGONAL;
    case WEATHER_UNDERWATER:         return WEATHER_UNDERWATER;
    case WEATHER_SHADE:              return WEATHER_SHADE;
    case WEATHER_DROUGHT:            return WEATHER_DROUGHT;
    case WEATHER_DOWNPOUR:           return WEATHER_DOWNPOUR;
    case WEATHER_UNDERWATER_BUBBLES: return WEATHER_UNDERWATER_BUBBLES;
    case WEATHER_ABNORMAL:           return WEATHER_ABNORMAL;
    case WEATHER_PETALS:             return WEATHER_PETALS;
    case WEATHER_MONSOON:            return WEATHER_MONSOON;
    case WEATHER_BLIZZARD:           return WEATHER_BLIZZARD;
    case WEATHER_LEAVES:             return WEATHER_LEAVES;
    case WEATHER_ZUBATS:             return WEATHER_ZUBATS;
    case WEATHER_SEABIRDS:           return WEATHER_SEABIRDS;
    case WEATHER_ROUTE119_CYCLE:     return sWeatherCycleRoute119[gSaveBlock1Ptr->weatherCycleStage];
    case WEATHER_ROUTE123_CYCLE:     return sWeatherCycleRoute123[gSaveBlock1Ptr->weatherCycleStage];
    case WEATHER_DYNAMIC:            return GetDynamicWeather();
    default:                         return WEATHER_NONE;
    }
}

void UpdateWeatherPerDay(u16 increment)
{
    u16 weatherStage = gSaveBlock1Ptr->weatherCycleStage + increment;
    weatherStage %= WEATHER_CYCLE_LENGTH;
    gSaveBlock1Ptr->weatherCycleStage = weatherStage;
}

static void UpdateRainCounter(u8 newWeather, u8 oldWeather)
{
    if (newWeather != oldWeather
     && (newWeather == WEATHER_RAIN || newWeather == WEATHER_RAIN_THUNDERSTORM))
        IncrementGameStat(GAME_STAT_GOT_RAINED_ON);
}
