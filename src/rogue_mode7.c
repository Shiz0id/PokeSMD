#include "global.h"
#include "bg.h"
#include "gpu_regs.h"
#include "main.h"
#include "palette.h"
#include "scanline_effect.h"
#include "sprite.h"
#include "pokemon_icon.h"
#include "decompress.h"
#include "malloc.h"
#include "random.h"
#include "constants/pokemon.h"
#include "constants/species.h"
#include "task.h"
#include "trig.h"
#include "constants/rgb.h"
#include "rogue_mode7.h"

// Mode 7 ground plane.
//
// The whole technique is one idea: BG2 is an affine background, and the affine
// registers are rewritten every scanline so that each row of the screen samples
// the plane at a different scale. Rows near the horizon sample a long way out,
// rows near the bottom sample close by, and the result reads as a floor.
//
// Tonc chapter 21 is the reference. Two things from it matter here:
//
//   lambda = z/D = camera_height / h        (eq 20.2, h = rows below horizon)
//
// and the ordering rule, which is the part that fails silently: BG2X must be
// derived from the ALREADY-ROUNDED BG2PA, because the rounded PA is what the
// hardware accumulates across the scanline. Deriving it from the unrounded
// scale compiles, looks nearly right, and shears every row.
//
// The host-side prototype in tools/mode7/ rendered all of this before any of it
// was written, and measured the two limits that shape the camera:
//
//   * REG_BG2PA is s16 in .8, so it cannot express a scale above 127.996. That
//     is reached on the rows closest to the horizon, and the higher the camera
//     the more rows are affected. Those rows are parked rather than clamped --
//     see BuildScanlineTable.
//   * Camera height 64-96 is the clean window. Below that, lambda quantised
//     coarsely starts repeating between adjacent scanlines.

#define MODE7_PLANE_TILES 64                    // 512x512 px = 64x64 tiles
#define MODE7_CHAR_BASE   0
#define MODE7_SCREEN_BASE 8                     // 2 KB units; the map needs two

// Four words is the whole affine set: PA, PB, PC, PD, X, Y.
// DMA_DEST_RELOAD means "increment across the transfer, then reload", which is
// exactly right for four consecutive registers repeated every scanline.
#define MODE7_DMA_CONTROL                                                      \
    (((DMA_ENABLE | DMA_START_HBLANK | DMA_REPEAT | DMA_SRC_INC                \
       | DMA_DEST_RELOAD | DMA_32BIT) << 16) | 4)

// (1 << 16) / h, so a division per scanline becomes a multiply and a shift.
// Index is rows below the horizon; entry 0 is never read.
static const u32 sInvH[DISPLAY_HEIGHT] =
{
    0, 65536, 32768, 21845, 16384, 13107, 10922, 9362, 8192, 7281, 6553,
    5957, 5461, 5041, 4681, 4369, 4096, 3855, 3640, 3449, 3276, 3120, 2978,
    2849, 2730, 2621, 2520, 2427, 2340, 2259, 2184, 2114, 2048, 1985, 1927,
    1872, 1820, 1771, 1724, 1680, 1638, 1598, 1560, 1524, 1489, 1456, 1424,
    1394, 1365, 1337, 1310, 1285, 1260, 1236, 1213, 1191, 1170, 1149, 1129,
    1110, 1092, 1074, 1057, 1040, 1024, 1008, 992, 978, 963, 949, 936, 923,
    910, 897, 885, 873, 862, 851, 840, 829, 819, 809, 799, 789, 780, 771,
    762, 753, 744, 736, 728, 720, 712, 704, 697, 689, 682, 675, 668, 661,
    655, 648, 642, 636, 630, 624, 618, 612, 606, 601, 595, 590, 585, 579,
    574, 569, 564, 560, 555, 550, 546, 541, 537, 532, 528, 524, 520, 516,
    512, 508, 504, 500, 496, 492, 489, 485, 481, 478, 474, 471, 468, 464,
    461, 458, 455, 451, 448, 445, 442, 439, 436, 434, 431, 428, 425, 422,
    420, 417, 414, 412
};

static EWRAM_DATA ALIGNED(4) struct Mode7Scanline sScanlineTable[DISPLAY_HEIGHT] = {0};
static EWRAM_DATA struct Mode7Camera sCamera = {0};

static void VBlankCB_Mode7(void);
static void MainCB2_Mode7(void);
static void BuildPlane(void);
static void ApplyScanline(const struct Mode7Scanline *line);

// ---------------------------------------------------------------------------

void RogueMode7_BuildScanlineTable(const struct Mode7Camera *cam)
{
    s32 cosYaw = gSineTable[(cam->yaw + 64) & 0xFF];    // Q8.8
    s32 sinYaw = gSineTable[cam->yaw & 0xFF];           // Q8.8
    s32 row;

    for (row = 0; row < DISPLAY_HEIGHT; row++)
    {
        struct Mode7Scanline *out = &sScanlineTable[row];
        s32 h = row - cam->horizon;
        s32 lambda, pa, pc, z, centreX, centreZ;

        // Above the horizon there is no ground. Park the sampler instead of
        // leaving stale values: a stale row shows last frame's ground floating
        // in the sky.
        if (h < 1)
        {
            out->pa = 0;
            out->pb = 0;
            out->pc = 0;
            out->pd = 0;
            out->x = 0;
            out->y = 0;
            continue;
        }

        // lambda = height / h. sInvH is (1 << 16) / h, so the product is
        // Q24.24 and a >> 16 brings it back to Q24.8.
        lambda = (s32)(((u32)cam->height * sInvH[h]) >> 16);

        // The rows nearest the horizon want a scale REG_BG2PA cannot hold.
        // Park them rather than let them clamp: a clamped PA shears the row
        // visibly, while a parked one is a flat band the horizon haze covers.
        // |pa| <= lambda because |cos| <= 1, so testing lambda is sufficient.
        if (lambda > 32767)
        {
            out->pa = 0;
            out->pb = 0;
            out->pc = 0;
            out->pd = 0;
            out->x = 0;
            out->y = 0;
            continue;
        }

        pa = (lambda * cosYaw) >> 8;
        pc = (-lambda * sinYaw) >> 8;
        z = lambda * MODE7_PROJ_D;                      // forward distance, Q24.8

        // Where the centre of this scanline lands on the plane.
        centreX = cam->x + ((z * sinYaw) >> 8);
        centreZ = cam->z + ((z * cosYaw) >> 8);

        // Tonc "Type C". Take the half-screen offset back out using pa and pc
        // as they will actually be written, not using lambda. This single
        // detail is the difference between a clean plane and one that shears.
        out->pa = pa;
        out->pb = 0;
        out->pc = pc;
        out->pd = 0;
        out->x = centreX - (DISPLAY_WIDTH / 2) * pa;
        out->y = centreZ - (DISPLAY_WIDTH / 2) * pc;
    }
}

static void ApplyScanline(const struct Mode7Scanline *line)
{
    SetGpuReg(REG_OFFSET_BG2PA, line->pa);
    SetGpuReg(REG_OFFSET_BG2PB, line->pb);
    SetGpuReg(REG_OFFSET_BG2PC, line->pc);
    SetGpuReg(REG_OFFSET_BG2PD, line->pd);
    SetGpuReg(REG_OFFSET_BG2X_L, line->x & 0xFFFF);
    SetGpuReg(REG_OFFSET_BG2X_H, line->x >> 16);
    SetGpuReg(REG_OFFSET_BG2Y_L, line->y & 0xFFFF);
    SetGpuReg(REG_OFFSET_BG2Y_H, line->y >> 16);
}

void RogueMode7_ArmHBlankDma(void)
{
    DmaStop(0);

    // The first HBlank DMA fires AFTER scanline 0 has been drawn, so it feeds
    // scanline 1. Point the DMA at entry 1 and write entry 0 by hand -- the
    // same off-by-one scanline_effect.c handles the same way.
    DmaSet(0, &sScanlineTable[1], (void *)REG_ADDR_BG2PA, MODE7_DMA_CONTROL);
    ApplyScanline(&sScanlineTable[0]);
}

void RogueMode7_Stop(void)
{
    DmaStop(0);
}

// ---------------------------------------------------------------------------
// Unown swarm -- the sprite budget testbed
// ---------------------------------------------------------------------------
//
// The question this exists to answer is one no check can: where does the
// per-scanline OBJ budget actually fall over, with the Mode 7 HBlank DMA
// competing for the same HBlank?
//
// The documented numbers say roughly 1210 cycles of OBJ time per scanline, an
// affine sprite costing about 2 cycles per pixel of width plus 10 overhead --
// so on the order of 16 overlapping 32-px affine sprites per line. That is a
// starting guess, not a measurement, and the DMA makes it worse by an unknown
// amount. Hence a knob and a meter rather than a fixed number.
//
// Two limits are already hard and are NOT what is being measured:
//   MAX_SPRITES      64  -- the engine's pool, half the hardware's 128 OAM slots
//   OAM_MATRIX_COUNT 32  -- affine matrices, shared across every sprite onscreen
//
// The matrix limit is why depth is quantised into buckets below. With more
// Unown than matrices, distinct depths MUST share a scale; that is not a
// shortcut, it is the only thing the hardware allows.

#define MODE7_SWARM_MAX     56          // MAX_SPRITES is 64; leave a margin
#define MODE7_SWARM_FORMS   8
#define MODE7_UNOWN_SIZE    7           // world units; sets the on-screen size
#define MODE7_AHEAD_MIN     38
#define MODE7_AHEAD_SPAN    152
#define MODE7_SPRITE_BOX    32          // the OBJ is 32x32
#define MODE7_MATRIX_MIN_PX 4           // bucket 0 renders the sprite this small

#define SWARM_PAL_TAG  0x4D37
#define SWARM_GFX_TAG  0x4D40           // eight consecutive tags, one per form

struct SwarmMember
{
    u8 spriteId;
    u8 ang;         // position around the helix
    u8 spin;        // how fast it goes around
    u8 rad;         // helix radius
    u8 baseAhead;   // phase along the flight path
    u8 hgt;         // height above the plane
};

static EWRAM_DATA struct SwarmMember sSwarm[MODE7_SWARM_MAX] = {0};
static EWRAM_DATA u8 sSwarmCount = 0;
static EWRAM_DATA u8 sSwarmDoubleSize = 0;
static EWRAM_DATA u16 sFrame = 0;
static EWRAM_DATA u8 sVBlankEndLine = 0;

// Indices here are the FRONT SPRITE's, which are not the icon's: 4-5 are the
// eye, 6 the outline, 7-9 the body. Body goes dark violet and the eye stays
// bright, because at ten pixels the silhouette is mush and the eye is the whole
// read. Recoloured here rather than in the art, so the art stays stock.
static const u16 sSwarmPalette[16] =
{
    [0] = RGB(0, 0, 0),         // transparent
    [4] = RGB(21, 27, 29),      // eye edge
    [5] = RGB(29, 31, 31),      // eye core
    [6] = RGB(1, 1, 2),         // outline
    [7] = RGB(10, 8, 15),       // body light
    [8] = RGB(7, 5, 11),        // body mid
    [9] = RGB(4, 3, 7),         // body dark
};

// GET_UNOWN_LETTER packs the letter across four 2-bit fields, so this is its
// inverse: the smallest personality that yields letter L.
#define UNOWN_PERSONALITY(L) (((L) & 3)                                             | ((((L) >> 2) & 3) << 8)                               | ((((L) >> 4) & 3) << 16)                              | ((((L) >> 6) & 3) << 24))

static const struct OamData sSwarmOam =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_NORMAL,
    .objMode = ST_OAM_OBJ_NORMAL,
    .shape = SPRITE_SHAPE(32x32),
    .x = 0,
    .size = SPRITE_SIZE(32x32),
    .tileNum = 0,
    .priority = 0,
    .paletteNum = 0,
};

static const struct SpriteTemplate sSwarmSpriteTemplate =
{
    .tileTag = SWARM_GFX_TAG,
    .paletteTag = SWARM_PAL_TAG,
    .oam = &sSwarmOam,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy,
};

// One matrix per bucket: bucket b renders the 32x32 texture at
// (MODE7_MATRIX_MIN_PX + b) pixels. The OBJ matrix maps SCREEN to TEXTURE, so
// the entry is the reciprocal of the scale -- a bigger number is a smaller
// sprite, which is the opposite of what it reads like.
static void SetSwarmMatrices(void)
{
    u32 b;

    for (b = 0; b < OAM_MATRIX_COUNT; b++)
    {
        s32 px = MODE7_MATRIX_MIN_PX + b;
        s32 v = (MODE7_SPRITE_BOX << 8) / px;

        gOamMatrices[b].a = v;
        gOamMatrices[b].b = 0;
        gOamMatrices[b].c = 0;
        gOamMatrices[b].d = v;
    }
}

static void LoadSwarmGfx(void)
{
    // Letters chosen because their ink fits the middle 32x32 of the 64x64 pic
    // exactly: b c d e f h i k. The three tallest forms (a, g, !) reach 36 rows
    // and would lose their top two rows to the crop, so they are left out.
    static const u8 sLetters[MODE7_SWARM_FORMS] = { 1, 2, 3, 4, 5, 7, 8, 10 };
    struct SpritePalette pal = { sSwarmPalette, SWARM_PAL_TAG };
    u8 *full = Alloc(MON_PIC_SIZE);
    u8 *crop = Alloc(32 * 32 / 2);
    u32 i, tx, ty;

    LoadSpritePalette(&pal);

    for (i = 0; i < MODE7_SWARM_FORMS; i++)
    {
        struct SpriteSheet sheet;

        // Decompresses the pic AND resolves the Unown letter from personality,
        // the same way the icon path did.
        LoadSpecialPokePic(full, SPECIES_UNOWN,
                           UNOWN_PERSONALITY(sLetters[i]), TRUE);

        // A 64x64 mon pic is 64 tiles in 1D order, so tile (tx, ty) sits at
        // index ty * 8 + tx. Measured across every form, the ink never leaves
        // the middle 32x32, so lift the 4x4 tile block at (2, 2) and repack it
        // as a contiguous 32x32 sprite.
        //
        // This crop is the whole reason the swarm can use front sprites at all:
        // a 64-wide affine sprite costs about 2 cycles per pixel of width plus
        // overhead, so keeping the OBJ 32 wide keeps the per-scanline cost
        // identical to the icons it replaces.
        for (ty = 0; ty < 4; ty++)
        {
            for (tx = 0; tx < 4; tx++)
            {
                CpuCopy32(full + ((ty + 2) * 8 + (tx + 2)) * TILE_SIZE_4BPP,
                          crop + (ty * 4 + tx) * TILE_SIZE_4BPP,
                          TILE_SIZE_4BPP);
            }
        }

        sheet.data = crop;
        sheet.size = 32 * 32 / 2;
        sheet.tag = SWARM_GFX_TAG + i;
        LoadSpriteSheet(&sheet);
    }

    Free(crop);
    Free(full);
}

static void CreateSwarm(void)
{
    u32 i;

    SetSwarmMatrices();

    for (i = 0; i < MODE7_SWARM_MAX; i++)
    {
        struct SwarmMember *m = &sSwarm[i];
        u8 spriteId = CreateSprite(&sSwarmSpriteTemplate, 0, 0, 0);

        m->spriteId = spriteId;
        if (spriteId == MAX_SPRITES)
            continue;

        // Spread deterministically rather than randomly, so two runs of the
        // same test are comparable.
        m->ang = i * 37;
        m->spin = 3 + (i % 5);
        m->rad = 22 + (i * 7) % 44;
        m->baseAhead = (i * 23) % MODE7_AHEAD_SPAN;
        m->hgt = 34 + (i * 11) % 30;

        gSprites[spriteId].oam.tileNum =
            GetSpriteTileStartByTag(SWARM_GFX_TAG + (i % MODE7_SWARM_FORMS));
        gSprites[spriteId].invisible = TRUE;
    }
}

static void UpdateSwarm(void)
{
    u32 i;

    for (i = 0; i < MODE7_SWARM_MAX; i++)
    {
        struct SwarmMember *m = &sSwarm[i];
        struct Sprite *s;
        s32 ahead, ang, lateral, wy, sx, sy, size, bucket;

        if (m->spriteId == MAX_SPRITES)
            continue;
        s = &gSprites[m->spriteId];

        if (i >= sSwarmCount)
        {
            s->invisible = TRUE;
            continue;
        }

        // The swarm is anchored to the camera, so it is defined in CAMERA
        // space directly. Going out to world space and projecting back would
        // be the same numbers with two more chances to be wrong.
        ahead = MODE7_AHEAD_MIN
              + ((m->baseAhead + MODE7_AHEAD_SPAN
                  - ((sFrame * 5 / 8) % MODE7_AHEAD_SPAN)) % MODE7_AHEAD_SPAN);
        ang = (m->ang + sFrame * m->spin / 8) & 0xFF;

        lateral = (gSineTable[(ang + 64) & 0xFF] * m->rad) >> 8;
        wy = m->hgt + ((gSineTable[ang] * m->rad) >> 9);

        sx = (DISPLAY_WIDTH / 2) + (lateral * MODE7_PROJ_D) / ahead;
        sy = sCamera.horizon
           + (((sCamera.height >> 8) - wy) * MODE7_PROJ_D) / ahead;

        size = (MODE7_UNOWN_SIZE * MODE7_PROJ_D) / ahead;
        bucket = size - MODE7_MATRIX_MIN_PX;
        if (bucket < 0)
            bucket = 0;
        if (bucket >= OAM_MATRIX_COUNT)
            bucket = OAM_MATRIX_COUNT - 1;

        s->invisible = FALSE;
        s->oam.affineMode = sSwarmDoubleSize ? ST_OAM_AFFINE_DOUBLE
                                             : ST_OAM_AFFINE_NORMAL;
        s->oam.matrixNum = bucket;
        s->x = sx - MODE7_SPRITE_BOX / 2;
        s->y = sy - MODE7_SPRITE_BOX / 2;
        // Nearer Unown must draw over farther ones. Lower subpriority is in
        // front, so subpriority tracks distance directly.
        s->subpriority = ahead > 255 ? 255 : ahead;
    }
}


// ---------------------------------------------------------------------------
// Meters
// ---------------------------------------------------------------------------
//
// No font and no text window: two bars of solid tiles on BG0. That keeps this
// working on hardware and in any emulator, with no dependency on NDEBUG or
// LOG_HANDLER, which is what DebugPrintf would have needed.

#define METER_CHAR_BASE   2
#define METER_SCREEN_BASE 28
#define METER_WIDTH       30

// VBlank has 68 scanlines, 160..227. Sampling REG_VCOUNT at the END of the
// VBlank handler says how much of that budget the handler ate, for free and
// with no timer. If it reads BELOW 160 the handler ran past VBlank entirely and
// is now eating visible scanlines.
#define VBLANK_FIRST_LINE 160
#define VBLANK_LINES      68

static void BuildMeterTiles(void)
{
    ALIGNED(4) u8 px[32];
    u32 i;

    // 4bpp: two pixels per byte.
    for (i = 0; i < 32; i++)
        px[i] = 0x11;
    CpuCopy32(px, (void *)(BG_CHAR_ADDR(METER_CHAR_BASE) + 1 * 32), 32);
    for (i = 0; i < 32; i++)
        px[i] = 0x22;
    CpuCopy32(px, (void *)(BG_CHAR_ADDR(METER_CHAR_BASE) + 2 * 32), 32);
    for (i = 0; i < 32; i++)
        px[i] = 0x33;
    CpuCopy32(px, (void *)(BG_CHAR_ADDR(METER_CHAR_BASE) + 3 * 32), 32);
    for (i = 0; i < 32; i++)
        px[i] = 0x44;
    CpuCopy32(px, (void *)(BG_CHAR_ADDR(METER_CHAR_BASE) + 4 * 32), 32);
}

static void SetMeterPalette(void)
{
    static const u16 sPal[] = {
        RGB(0, 0, 0),        // 0 transparent
        RGB(8, 24, 10),      // 1 bar filled, green
        RGB(6, 6, 8),        // 2 bar empty
        RGB(31, 8, 6),       // 3 overrun, red
        RGB(28, 24, 6),      // 4 swarm count, amber
    };

    LoadPalette(sPal, BG_PLTT_ID(1), sizeof(sPal));
}

static void DrawBar(u32 barRow, u32 filled, u32 fillTile)
{
    ALIGNED(4) u16 row[METER_WIDTH];
    u32 i;

    for (i = 0; i < METER_WIDTH; i++)
        row[i] = (i < filled ? fillTile : 2) | (1 << 12);   // palette bank 1

    // CpuCopy16, not 32: the bar starts at tile column 1, so the destination is
    // only 2-byte aligned and a 32-bit copy would land wrong.
    CpuCopy16(row,
              (void *)(BG_SCREEN_ADDR(METER_SCREEN_BASE)
                       + (barRow * 32 + 1) * 2),
              METER_WIDTH * 2);
}

static void UpdateMeters(void)
{
    u32 used, filled;

    if (sVBlankEndLine < VBLANK_FIRST_LINE)
    {
        // Overran VBlank: the handler is now inside the visible frame.
        DrawBar(1, METER_WIDTH, 3);
    }
    else
    {
        used = sVBlankEndLine - VBLANK_FIRST_LINE;
        filled = (used * METER_WIDTH) / VBLANK_LINES;
        DrawBar(1, filled, 1);
    }

    filled = (sSwarmCount * METER_WIDTH) / MODE7_SWARM_MAX;
    DrawBar(3, filled, 4);
}

// ---------------------------------------------------------------------------
// Testbed
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The dungeon plane
// ---------------------------------------------------------------------------
//
// This is a PORT OF THE HOST PROTOTYPE in tools/mode7/mode7_proto.py, not a
// view of the game's own maps, and that is deliberate. rogue_dungeon.c does
// have a rooms-and-corridors generator -- CarveFloor, CarveCorridor,
// FitPlaneRooms -- and murky cave is a THEME running through it rather than a
// generator of its own. But that code emits u16 METATILES for a 48x48 play map
// via theme tables, and what an affine BG needs is a 64x64 map of 8x8 tiles at
// one byte per entry. The two formats do not meet, and the look wanted here is
// the prototype's regardless.
//
// The whole vocabulary is NINE tiles, against the affine ceiling of 256. That
// is why this theme was chosen over a landscape: an organic plane wanted 2466
// distinct tiles, and fragmented platforms wanted 589.

#define DUN_W           48      // matches DUNGEON_WIDTH in rogue_dungeon.h
#define DUN_H           48
#define DUN_MAX_ROOMS   10      // DUNGEON_ROOMS_DEFAULT
#define DUN_ROOM_MIN    5
#define DUN_ROOM_MAX    10
#define DUN_PLACE_TRIES 400
#define DUN_WALL_RING   2       // rings of rock around the floor, then void
#define DUN_OFFSET      ((MODE7_PLANE_TILES - DUN_W) / 2)

// Grid cell values. The two rock values differ only so the two dilation passes
// cannot cascade into each other; both render as rock.
#define DCELL_VOID    0
#define DCELL_FLOOR   1
#define DCELL_STAIRS  2
#define DCELL_ROCK_A  3
#define DCELL_ROCK_B  4

// Tile ids in VRAM, in the order BuildDungeonTiles writes them.
#define DTILE_VOID        0
#define DTILE_FLOOR       1
#define DTILE_FLOOR_LIT   2
#define DTILE_STAIRS      3
#define DTILE_WALL_FACE   4
#define DTILE_WALL_TOP    5
#define DTILE_WALL_HI     6
#define DTILE_WALL_SPECK  7
#define DTILE_WALL_HISPK  8

// Palette indices, matching the prototype. Walls sit far below the floor in
// value on purpose: the rock should read as the dark the floor is carved out
// of, not as a surface of its own.
#define DPAL_HI       1
#define DPAL_TOP      2
#define DPAL_TOP_DK   3
#define DPAL_FACE_LT  4
#define DPAL_FACE     5
#define DPAL_FACE_DK  6
#define DPAL_FLOOR_LT 7
#define DPAL_FLOOR    8
#define DPAL_GRID     9
#define DPAL_STAIRS   10
#define DPAL_ACCENT   11

// Stable per-cell noise: a hash rather than Random(), so the speckle does not
// crawl when the plane is rebuilt for the same floor.
static u32 CellHash(u32 x, u32 y)
{
    return ((x * 73) ^ (y * 151)) & 0xFF;
}

static void FillTile(u8 *t, u8 v)
{
    u32 i;

    for (i = 0; i < 64; i++)
        t[i] = v;
}

static void WriteTile(const u8 *t, u32 index)
{
    // VRAM rejects 8-bit writes, so tiles are assembled in RAM and copied as
    // words. Writing a u8 straight to VRAM corrupts its neighbour rather than
    // failing.
    CpuCopy32(t, (void *)(BG_CHAR_ADDR(MODE7_CHAR_BASE) + index * 64), 64);
}

static void BuildDungeonTiles(void)
{
    ALIGNED(4) u8 t[64];
    u32 i, x, y;

    FillTile(t, 0);
    WriteTile(t, DTILE_VOID);

    // Floor carries a grid line along its top and left edges, so the plane
    // reads as tiled rather than painted. That line is also what makes any
    // shear in the projection visible.
    FillTile(t, DPAL_FLOOR);
    for (i = 0; i < 8; i++)
    {
        t[i] = DPAL_GRID;
        t[i * 8] = DPAL_GRID;
    }
    WriteTile(t, DTILE_FLOOR);

    FillTile(t, DPAL_FLOOR_LT);
    for (i = 0; i < 8; i++)
    {
        t[i] = DPAL_GRID;
        t[i * 8] = DPAL_GRID;
    }
    WriteTile(t, DTILE_FLOOR_LIT);

    FillTile(t, DPAL_FLOOR);
    for (y = 1; y < 7; y++)
        for (x = 1; x < 7; x++)
            t[y * 8 + x] = DPAL_STAIRS;
    for (y = 2; y < 6; y++)
        for (x = 2; x < 6; x++)
            t[y * 8 + x] = DPAL_ACCENT;
    WriteTile(t, DTILE_STAIRS);

    // The face is the side of the rock the camera can see, so the top half is
    // shadow and the bottom half is a lit wall front.
    for (i = 0; i < 64; i++)
        t[i] = (i < 32) ? DPAL_TOP_DK : DPAL_FACE;
    for (i = 56; i < 64; i++)
        t[i] = DPAL_FACE_DK;
    WriteTile(t, DTILE_WALL_FACE);

    FillTile(t, DPAL_TOP);
    WriteTile(t, DTILE_WALL_TOP);

    FillTile(t, DPAL_TOP);
    for (i = 0; i < 8; i++)
        t[i] = DPAL_HI;
    WriteTile(t, DTILE_WALL_HI);

    FillTile(t, DPAL_TOP);
    for (y = 3; y < 5; y++)
        for (x = 3; x < 5; x++)
            t[y * 8 + x] = DPAL_TOP_DK;
    WriteTile(t, DTILE_WALL_SPECK);

    FillTile(t, DPAL_TOP);
    for (i = 0; i < 8; i++)
        t[i] = DPAL_HI;
    for (y = 3; y < 5; y++)
        for (x = 3; x < 5; x++)
            t[y * 8 + x] = DPAL_TOP_DK;
    WriteTile(t, DTILE_WALL_HISPK);
}

static void CarveH(u8 *g, s32 y, s32 x0, s32 x1)
{
    s32 x;

    if (x0 > x1)
    {
        s32 tmp = x0;

        x0 = x1;
        x1 = tmp;
    }
    for (x = x0; x <= x1; x++)
        g[y * DUN_W + x] = DCELL_FLOOR;
}

static void CarveV(u8 *g, s32 x, s32 y0, s32 y1)
{
    s32 y;

    if (y0 > y1)
    {
        s32 tmp = y0;

        y0 = y1;
        y1 = tmp;
    }
    for (y = y0; y <= y1; y++)
        g[y * DUN_W + x] = DCELL_FLOOR;
}

static void CarveL(u8 *g, s32 ax, s32 ay, s32 bx, s32 by, bool32 horizFirst)
{
    if (horizFirst)
    {
        CarveH(g, ay, ax, bx);
        CarveV(g, bx, ay, by);
    }
    else
    {
        CarveV(g, ax, ay, by);
        CarveH(g, by, ax, bx);
    }
}

// Rooms plus L corridors, the shape the run generator produces. A plain
// reimplementation on purpose: what matters here is the SILHOUETTE of a floor,
// not bit-compatibility with rogue_dungeon.c, which could not supply this
// format anyway.
static void GenerateFloorPlan(u8 *g)
{
    s32 rx[DUN_MAX_ROOMS], ry[DUN_MAX_ROOMS];
    s32 rw[DUN_MAX_ROOMS], rh[DUN_MAX_ROOMS];
    s32 order[DUN_MAX_ROOMS];
    u32 count = 0;
    u32 attempt, i, j;

    CpuFill16(0, g, DUN_W * DUN_H);

    for (attempt = 0; attempt < DUN_PLACE_TRIES && count < DUN_MAX_ROOMS; attempt++)
    {
        s32 w = DUN_ROOM_MIN + (Random() % (DUN_ROOM_MAX - DUN_ROOM_MIN + 1));
        s32 h = DUN_ROOM_MIN + (Random() % (DUN_ROOM_MAX - DUN_ROOM_MIN + 1));
        s32 x = 2 + (Random() % (DUN_W - w - 4));
        s32 y = 2 + (Random() % (DUN_H - h - 4));
        bool32 clash = FALSE;

        // Two cells of gap, so rooms never end up sharing a wall.
        for (i = 0; i < count; i++)
        {
            if (x < rx[i] + rw[i] + 2 && rx[i] < x + w + 2
             && y < ry[i] + rh[i] + 2 && ry[i] < y + h + 2)
            {
                clash = TRUE;
                break;
            }
        }
        if (clash)
            continue;

        rx[count] = x;
        ry[count] = y;
        rw[count] = w;
        rh[count] = h;
        for (j = 0; j < (u32)h; j++)
            CarveH(g, y + j, x, x + w - 1);
        count++;
    }

    if (count == 0)
        return;

    // Order by centre x, then chain them, so corridors run across the plan
    // rather than criss-crossing it.
    for (i = 0; i < count; i++)
        order[i] = i;
    for (i = 1; i < count; i++)
    {
        s32 key = order[i];
        s32 k = i;

        while (k > 0 && rx[order[k - 1]] + rw[order[k - 1]] / 2
                      > rx[key] + rw[key] / 2)
        {
            order[k] = order[k - 1];
            k--;
        }
        order[k] = key;
    }

    for (i = 0; i + 1 < count; i++)
    {
        s32 a = order[i], b = order[i + 1];

        CarveL(g, rx[a] + rw[a] / 2, ry[a] + rh[a] / 2,
                  rx[b] + rw[b] / 2, ry[b] + rh[b] / 2, Random() & 1);
    }

    // A couple of extra links, so the plan reads as a network and not a chain.
    if (count > 3)
    {
        for (i = 0; i < 2; i++)
        {
            s32 a = Random() % count;
            s32 b = Random() % count;

            if (a != b)
                CarveL(g, rx[a] + rw[a] / 2, ry[a] + rh[a] / 2,
                          rx[b] + rw[b] / 2, ry[b] + rh[b] / 2, Random() & 1);
        }
    }

    j = order[count - 1];
    g[(ry[j] + rh[j] / 2) * DUN_W + rx[j] + rw[j] / 2] = DCELL_STAIRS;
}

static bool32 IsWalkable(const u8 *g, s32 x, s32 y)
{
    if (x < 0 || y < 0 || x >= DUN_W || y >= DUN_H)
        return FALSE;
    return g[y * DUN_W + x] == DCELL_FLOOR || g[y * DUN_W + x] == DCELL_STAIRS;
}

static bool32 IsSolid(const u8 *g, s32 x, s32 y)
{
    if (x < 0 || y < 0 || x >= DUN_W || y >= DUN_H)
        return FALSE;
    return g[y * DUN_W + x] != DCELL_VOID;
}

// Pass 2 of GrowRock must test what existed BEFORE pass 2 started. Testing
// "anything not void" would include DCELL_ROCK_B, which is what pass 2 is
// writing -- so a cell written early in the scan seeds the cell to its right,
// which seeds the next, and the rock floods the entire grid. That is the bug
// that made the border a big square instead of hugging the rooms, and writing
// pass 2 to a distinct value did NOT prevent it: the guard has to be on the
// predicate, not on the value written.
static bool32 IsFloorOrInnerRock(const u8 *g, s32 x, s32 y)
{
    u8 c;

    if (x < 0 || y < 0 || x >= DUN_W || y >= DUN_H)
        return FALSE;
    c = g[y * DUN_W + x];
    return c == DCELL_FLOOR || c == DCELL_STAIRS || c == DCELL_ROCK_A;
}

// DUN_WALL_RING rings of rock around the floor, then nothing. The nothing is
// palette index 0, which on an affine BG is TRANSPARENT -- so the void beyond
// the rock is the backdrop showing through and costs no pixels at all. That is
// what makes the plan hang in the dark instead of sitting in a slab, and it
// only works if the rock stops where it is told to.
static void GrowRock(u8 *g)
{
    s32 x, y;

    for (y = 0; y < DUN_H; y++)
    {
        for (x = 0; x < DUN_W; x++)
        {
            if (g[y * DUN_W + x] == DCELL_VOID
             && (IsWalkable(g, x - 1, y) || IsWalkable(g, x + 1, y)
              || IsWalkable(g, x, y - 1) || IsWalkable(g, x, y + 1)
              || IsWalkable(g, x - 1, y - 1) || IsWalkable(g, x + 1, y - 1)
              || IsWalkable(g, x - 1, y + 1) || IsWalkable(g, x + 1, y + 1)))
                g[y * DUN_W + x] = DCELL_ROCK_A;
        }
    }

    for (y = 0; y < DUN_H; y++)
    {
        for (x = 0; x < DUN_W; x++)
        {
            if (g[y * DUN_W + x] == DCELL_VOID
             && (IsFloorOrInnerRock(g, x - 1, y) || IsFloorOrInnerRock(g, x + 1, y)
              || IsFloorOrInnerRock(g, x, y - 1) || IsFloorOrInnerRock(g, x, y + 1)
              || IsFloorOrInnerRock(g, x - 1, y - 1) || IsFloorOrInnerRock(g, x + 1, y - 1)
              || IsFloorOrInnerRock(g, x - 1, y + 1) || IsFloorOrInnerRock(g, x + 1, y + 1)))
                g[y * DUN_W + x] = DCELL_ROCK_B;
        }
    }
}

static u8 TileForCell(const u8 *g, s32 x, s32 y)
{
    u8 c = g[y * DUN_W + x];
    u32 hash = CellHash(x, y);

    if (c == DCELL_STAIRS)
        return DTILE_STAIRS;
    if (c == DCELL_FLOOR)
        return (hash > 184) ? DTILE_FLOOR_LIT : DTILE_FLOOR;
    if (c == DCELL_VOID)
        return DTILE_VOID;

    // Rock with floor directly SOUTH of it is the face -- the side of the wall
    // the camera can see. Everything else is the top of the rock.
    if (IsWalkable(g, x, y + 1))
        return DTILE_WALL_FACE;

    {
        bool32 hi = !IsSolid(g, x, y - 1);
        bool32 speck = hash > 217;

        if (hi && speck)
            return DTILE_WALL_HISPK;
        if (hi)
            return DTILE_WALL_HI;
        if (speck)
            return DTILE_WALL_SPECK;
        return DTILE_WALL_TOP;
    }
}

static void RasteriseFloorPlan(const u8 *g)
{
    ALIGNED(4) u8 row[MODE7_PLANE_TILES];
    s32 x, y;

    for (y = 0; y < MODE7_PLANE_TILES; y++)
    {
        s32 dy = y - DUN_OFFSET;

        for (x = 0; x < MODE7_PLANE_TILES; x++)
        {
            s32 dx = x - DUN_OFFSET;

            if (dx < 0 || dy < 0 || dx >= DUN_W || dy >= DUN_H)
                row[x] = DTILE_VOID;
            else
                row[x] = TileForCell(g, dx, dy);
        }
        // Affine map entries are ONE BYTE -- no palette bank, no priority, no
        // flip. That byte is the whole reason the plane is capped at 256 tiles.
        CpuCopy32(row,
                  (void *)(BG_SCREEN_ADDR(MODE7_SCREEN_BASE)
                           + y * MODE7_PLANE_TILES),
                  MODE7_PLANE_TILES);
    }
}

static void BuildPlane(void)
{
    u8 *grid = Alloc(DUN_W * DUN_H);

    BuildDungeonTiles();
    GenerateFloorPlan(grid);
    GrowRock(grid);
    RasteriseFloorPlan(grid);
    Free(grid);
}

static void SetPlanePalette(void)
{
    static const u16 sPal[] = {
        [0]  = RGB(0, 0, 0),        // transparent: the void beyond the rock
        [1]  = RGB(9, 10, 11),      // wall top highlight
        [2]  = RGB(6, 6, 7),        // wall top
        [3]  = RGB(4, 4, 5),        // wall top shadow
        [4]  = RGB(3, 3, 4),        // wall face light
        [5]  = RGB(2, 2, 3),        // wall face
        [6]  = RGB(1, 1, 2),        // wall face dark
        [7]  = RGB(21, 18, 13),     // floor light
        [8]  = RGB(16, 14, 10),     // floor
        [9]  = RGB(11, 9, 7),       // floor grid
        [10] = RGB(31, 28, 16),     // stairs glow
        [11] = RGB(15, 12, 21),     // accent
    };

    LoadPalette(sPal, BG_PLTT_ID(0), sizeof(sPal));
}

// ---------------------------------------------------------------------------
// Atmosphere: horizon fog, vignette, stars
// ---------------------------------------------------------------------------
//
// All three live on ONE text BG in front of the plane, drawn as DITHER and then
// softened by the blend unit. That shape is forced by the DMA budget, not
// chosen for elegance:
//
//   DMA0  the affine scanline transfer
//   DMA1  sound, FIFO A          -- m4a owns it
//   DMA2  sound, FIFO B          -- m4a owns it
//   DMA3  every DmaCopy and DmaFill in the game
//
// There is no free channel, so a second per-scanline register write is not
// available. A per-scanline BLDALPHA gradient -- the obvious way to fade the
// plane into the sky -- would need an HBlank IRQ firing 160 times a frame, and
// that competes with the OBJ renderer already measured out at 20 sprites.
//
// Dither costs none of that. Density carries the gradient and ONE global
// BLDALPHA takes the harshness off. The blend unit is used once, statically,
// and no scanline work happens at all.
//
// TWO THINGS THE FIRST ATTEMPT GOT WRONG, both visible in a host render:
//
//   * Five density levels banded badly. A tile has ONE dither pattern, so
//     density is quantised to 8 px spatially no matter what -- the fix is not
//     fewer steps but MORE of them spread over MORE tiles, so each step is
//     small. Sixteen levels off a Bayer 8x8 threshold, over seven tiles.
//   * Stars never appeared. A map entry is one tile id, so a tile cannot be
//     both fog and star, and every sky tile already had some density. Stars now
//     have their own tiles AT each low density.
//
// The layer does not scroll: the horizon travels only 8 px across the shot, so
// the fog band is made tall enough to cover that, which keeps the vignette
// screen-fixed as it must be.

#define FOG_CHAR_BASE   3
#define FOG_SCREEN_BASE 30
#define FOG_PAL_BANK    2

#define FOG_TILES_W     30      // the visible area, 240x160
#define FOG_TILES_H     20

#define FOG_LEVELS      16      // dither steps, tiles 1..16
// Stars have to exist at most densities, not just clear sky. The vignette
// reaches FOG_VIGNETTE_TILES in from every edge, and the screen is only 20
// tiles tall, so every sky tile already carries some density -- capping star
// tiles at low densities meant no star ever placed.
#define FOG_STAR_LEVELS 8       // stars exist at density 0..7
#define FTILE_EMPTY     0
#define FTILE_STAR0     (FOG_LEVELS + 1)

#define FOG_PAL_DARK    1
#define FOG_PAL_STAR0   2       // entries 2..5 rotate for the twinkle

// Blend weights for the whole atmosphere layer. EVA is the fog's own weight, so
// it sets how hard each dither dot reads -- lowering IT is what takes the crunch
// out, not lowering the density. The two sum to 16 on purpose: above that the
// blend brightens as well as mixing, which the first attempt at 11 and 9 did,
// putting a little glare on top of the crunch.
#define FOG_BLEND_EVA   6
#define FOG_BLEND_EVB   10

// Vignette reach, in tiles. Wider and finer reads smoother than strong and
// narrow, because the banding is spatial and cannot be removed, only made small.
#define FOG_VIGNETTE_TILES 6

// Fog density by tile row, out of FOG_LEVELS. The horizon sits at rows 6-7
// across the shot, so the band covers both and falls away downward into clear
// air. It fades the ground into the BACKDROP, which is why the fog colour is
// near-black: the sky here is the backdrop, and the backdrop is black.
static const u8 sFogByRow[FOG_TILES_H] =
{
    3, 3, 2, 2, 3, 7, 12, 10, 7, 5, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0,
};

// Ordered dither. Threshold per pixel; a pixel is set when its entry is below
// density * 4, which gives sixteen even steps.
static const u8 sBayer8[64] =
{
     0, 32,  8, 40,  2, 34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38,
    60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37,
    63, 31, 55, 23, 61, 29, 53, 21,
};

static EWRAM_DATA u8 sTwinklePhase = 0;

// Four brightnesses rotated among four palette entries, so groups of stars
// twinkle out of phase. Four palette writes every twelfth frame, and nothing
// per star.
// Compressed upward from 31/21/13/7. The stars go through the same blend as the
// fog, so at FOG_BLEND_EVA of 6 they keep only 6/16 of their value over a black
// backdrop -- the old bottom step landed near 2 of 31 and simply vanished, which
// stalls the twinkle on a quarter of the stars.
static const u16 sStarBrightness[4] =
{
    RGB(31, 31, 31), RGB(26, 26, 29), RGB(20, 20, 25), RGB(14, 14, 20),
};

static void PackTile(const u8 *px, u32 index)
{
    ALIGNED(4) u8 tile[32];
    u32 i;

    // 4bpp packs two pixels per byte, low nibble first.
    for (i = 0; i < 32; i++)
        tile[i] = px[i * 2] | (px[i * 2 + 1] << 4);
    CpuCopy32(tile, (void *)(BG_CHAR_ADDR(FOG_CHAR_BASE) + index * 32), 32);
}

static void BuildFogTiles(void)
{
    ALIGNED(4) u8 px[64];
    u32 i, d, g;

    for (i = 0; i < 64; i++)
        px[i] = 0;
    PackTile(px, FTILE_EMPTY);

    for (d = 1; d <= FOG_LEVELS; d++)
    {
        for (i = 0; i < 64; i++)
            px[i] = (sBayer8[i] < d * 4) ? FOG_PAL_DARK : 0;
        PackTile(px, d);
    }

    // A star at each low density, so a star tile can still carry fog. Offsets
    // differ per group so a run of stars does not line up on a grid.
    for (g = 0; g < 4; g++)
    {
        for (d = 0; d < FOG_STAR_LEVELS; d++)
        {
            for (i = 0; i < 64; i++)
                px[i] = (d && sBayer8[i] < d * 4) ? FOG_PAL_DARK : 0;
            px[((2 + g * 3) & 7) * 8 + ((1 + g * 2) & 7)] = FOG_PAL_STAR0 + g;
            PackTile(px, FTILE_STAR0 + g * FOG_STAR_LEVELS + d);
        }
    }
}

static void SetFogPalette(void)
{
    u16 pal[16];
    u32 i;

    for (i = 0; i < 16; i++)
        pal[i] = RGB(0, 0, 0);
    // Near-black, matching the backdrop the ground has to fade into.
    pal[FOG_PAL_DARK] = RGB(1, 1, 3);
    for (i = 0; i < 4; i++)
        pal[FOG_PAL_STAR0 + i] = sStarBrightness[i];

    LoadPalette(pal, BG_PLTT_ID(FOG_PAL_BANK), sizeof(pal));
}

// Rotating which brightness sits in which entry is the whole twinkle.
static void UpdateTwinkle(void)
{
    u16 pal[4];
    u32 i;

    if ((sFrame % 12) != 0)
        return;

    // LoadPalette writes gPlttBufferFaded as well as unfaded, so writing star
    // colours during a fade would snap them back to full brightness while
    // everything else goes dark. Sit the fade out.
    if (gPaletteFade.active)
        return;

    sTwinklePhase++;
    for (i = 0; i < 4; i++)
        pal[i] = sStarBrightness[(i + sTwinklePhase) & 3];
    LoadPalette(pal, BG_PLTT_ID(FOG_PAL_BANK) + FOG_PAL_STAR0, sizeof(pal));
}

static void BuildFogMap(void)
{
    ALIGNED(4) u16 row[32];
    s32 tx, ty;

    for (ty = 0; ty < 32; ty++)
    {
        for (tx = 0; tx < 32; tx++)
            row[tx] = FTILE_EMPTY | (FOG_PAL_BANK << 12);

        if (ty < FOG_TILES_H)
        {
            for (tx = 0; tx < FOG_TILES_W; tx++)
            {
                s32 edge, density, fog;
                u32 hash;

                // Vignette: distance to the nearest screen edge, in tiles.
                edge = tx;
                if (FOG_TILES_W - 1 - tx < edge)
                    edge = FOG_TILES_W - 1 - tx;
                if (ty < edge)
                    edge = ty;
                if (FOG_TILES_H - 1 - ty < edge)
                    edge = FOG_TILES_H - 1 - ty;

                density = FOG_VIGNETTE_TILES - edge;
                if (density < 0)
                    density = 0;

                fog = sFogByRow[ty];
                if (fog > density)
                    density = fog;
                if (density > FOG_LEVELS)
                    density = FOG_LEVELS;

                hash = CellHash(tx * 3 + 1, ty * 7 + 5);
                if (ty < 7 && density < FOG_STAR_LEVELS && hash > 208)
                {
                    row[tx] = (FTILE_STAR0 + (hash & 3) * FOG_STAR_LEVELS + density)
                            | (FOG_PAL_BANK << 12);
                }
                else if (density > 0)
                {
                    row[tx] = density | (FOG_PAL_BANK << 12);
                }
            }
        }

        CpuCopy32(row, (void *)(BG_SCREEN_ADDR(FOG_SCREEN_BASE) + ty * 64), 64);
    }
}

// ---------------------------------------------------------------------------
// The shot
// ---------------------------------------------------------------------------
//
// The camera move from the host prototype, frame for frame: cruise forward low
// and steady, lift and bleed off speed, then settle. Four seconds at 60fps.
//
// Altitudes are picked from the window the prototype measured. 88 is inside the
// clean 64-96 band; the climb tops out at 128 rather than the 150 the landscape
// version used, because at 150 REG_BG2PA starts clamping on the rows nearest
// the horizon.
//
// Everything is Q8: t and the easing curves run 0..256 for 0..1, which keeps
// the whole thing to shifts and one divide per phase.

#define SHOT_FRAMES   240       // 4 seconds
#define SHOT_PHASE_A  108       // 0.45 -- cruise ends
#define SHOT_PHASE_B  192       // 0.80 -- climb ends, settle begins

#define SHOT_H0       (88 << 8)
#define SHOT_H1       (120 << 8)
#define SHOT_H2       (128 << 8)

#define SHOT_SPEED0   666       // 2.6 world px per frame, Q8
#define SHOT_SPEED1   205       // 0.8
#define SHOT_SPEED2   64        // 0.25

#define SHOT_HORIZON0 48
#define SHOT_HORIZON1 56

// The turn that closes the loop: drift back down, swing right, wind the speed
// back up into the cruise. Then the whole thing starts again.
//
// Position does NOT have to match across the cut. The plane wraps every 512
// world px in both axes, so every position is equivalent to some other one and
// there is nothing to line up. Height, horizon and speed DO have to match, and
// this phase returns all three to exactly the values SHOT_PHASE_A opens with.
//
// Yaw is a u8, so 256 is a full circle and a quarter turn is 64. It wraps for
// free, and four cycles bring it back to where it started exactly.
#define SHOT_TURN_FRAMES 90     // 1.5s, so the whole cycle is 5.5s
#define SHOT_TURN_YAW    64     // quarter turn right
#define SHOT_CYCLE       (SHOT_FRAMES + SHOT_TURN_FRAMES)

// Laying a fresh floor swaps 4 KB of map in one frame, and that POPS however
// well it is timed -- the dungeon simply becomes a different dungeon. No camera
// position hides it, because the plane wraps and copies are visible in every
// direction, so the only fix is to do it in the dark.
//
// The turn already drifts downward, so a dip to black through the middle of it
// reads as descending into the void and rising over somewhere else. That also
// covers the tearing from writing VRAM outside VBlank, which would otherwise
// need a 4 KB staging buffer to avoid.
//
// All of it is behind MODE7_REGEN_EACH_LOOP, which is currently off. Lower
// SHOT_FADE_DEPTH toward 10 for a dip rather than a blackout.
#define SHOT_FADE_OUT_AT 18
#define SHOT_REGEN_AT    40     // well inside the black
#define SHOT_FADE_IN_AT  46
#define SHOT_FADE_DEPTH  16

#define MANUAL_KEYS (DPAD_UP | DPAD_DOWN | DPAD_LEFT | DPAD_RIGHT \
                   | L_BUTTON | R_BUTTON)

static EWRAM_DATA u16 sShotFrame = 0;
static EWRAM_DATA bool8 sAutoCamera = FALSE;
static EWRAM_DATA u8 sTurnStartYaw = 0;

// Smoothstep, t and result both Q8 over 0..1. t*t*(3 - 2t).
static s32 EaseInOut(s32 t)
{
    s32 t2;

    if (t < 0)
        t = 0;
    if (t > 256)
        t = 256;
    t2 = (t * t) >> 8;
    return (t2 * (768 - 2 * t)) >> 8;
}

// 1 - (1 - t)^3. Decelerates into the hold at the end of the shot.
static s32 EaseOut(s32 t)
{
    s32 u, u2;

    if (t < 0)
        t = 0;
    if (t > 256)
        t = 256;
    u = 256 - t;
    u2 = (u * u) >> 8;
    return 256 - ((u2 * u) >> 8);
}

static s32 Lerp8(s32 a, s32 b, s32 k)
{
    return a + (((b - a) * k) >> 8);
}

static void StartShot(void)
{
    sShotFrame = 0;
    sAutoCamera = TRUE;
    sCamera.x = 256 << 8;
    sCamera.z = 0;
    sCamera.yaw = 0;        // the turn phase owns yaw from here on
    sCamera.height = SHOT_H0;
    sCamera.horizon = SHOT_HORIZON0;
    // The prototype ramped the ground in over the first half second; a palette
    // fade is the same read and costs nothing here.
    BeginNormalPaletteFade(PALETTES_ALL, 1, 16, 0, RGB_BLACK);
}

static void AdvanceShot(void)
{
    s32 k, speed, t;

    sShotFrame++;
    if (sShotFrame >= SHOT_CYCLE)
        sShotFrame = 0;

    if (sShotFrame < SHOT_PHASE_A)
    {
        sCamera.height = SHOT_H0;
        sCamera.horizon = SHOT_HORIZON0;
        speed = SHOT_SPEED0;
    }
    else if (sShotFrame < SHOT_PHASE_B)
    {
        k = EaseInOut(((sShotFrame - SHOT_PHASE_A) * 256)
                      / (SHOT_PHASE_B - SHOT_PHASE_A));
        sCamera.height = Lerp8(SHOT_H0, SHOT_H1, k);
        speed = Lerp8(SHOT_SPEED0, SHOT_SPEED1, k);

        // The horizon drifts down across the whole back half of the shot, which
        // reads as the camera levelling out of the cruise.
        k = EaseInOut(((sShotFrame - SHOT_PHASE_A) * 256)
                      / (SHOT_FRAMES - SHOT_PHASE_A));
        sCamera.horizon = Lerp8(SHOT_HORIZON0, SHOT_HORIZON1, k);
    }
    else if (sShotFrame < SHOT_FRAMES)
    {
        k = EaseOut(((sShotFrame - SHOT_PHASE_B) * 256)
                    / (SHOT_FRAMES - SHOT_PHASE_B));
        sCamera.height = Lerp8(SHOT_H1, SHOT_H2, k);
        speed = Lerp8(SHOT_SPEED1, SHOT_SPEED2, k);

        k = EaseInOut(((sShotFrame - SHOT_PHASE_A) * 256)
                      / (SHOT_FRAMES - SHOT_PHASE_A));
        sCamera.horizon = Lerp8(SHOT_HORIZON0, SHOT_HORIZON1, k);
    }
    else
    {
        // The turn. Everything runs backwards to where PHASE_A opens, and the
        // camera swings a quarter turn right on the way.
        t = sShotFrame - SHOT_FRAMES;
        if (t == 0)
            sTurnStartYaw = sCamera.yaw;

        // Divided by FRAMES - 1, not FRAMES, so the last frame of the turn
        // reaches k = 256 exactly. Dividing by FRAMES tops out at 255, which
        // lands the quarter turn on +63 instead of +64 -- a rounding error that
        // does not cancel, so yaw walks a step per cycle and the loop slowly
        // stops closing. Height and horizon land short in the same way.
        k = EaseInOut((t * 256) / (SHOT_TURN_FRAMES - 1));
        sCamera.height = Lerp8(SHOT_H2, SHOT_H0, k);
        sCamera.horizon = Lerp8(SHOT_HORIZON1, SHOT_HORIZON0, k);
        speed = Lerp8(SHOT_SPEED2, SHOT_SPEED0, k);
        sCamera.yaw = sTurnStartYaw + ((SHOT_TURN_YAW * k) >> 8);

        // With the regen off there is nothing to hide, so the dip does not
        // happen either -- an unmotivated fade would be worse than the repeat.
        if (MODE7_REGEN_EACH_LOOP)
        {
            if (t == SHOT_FADE_OUT_AT)
                BeginNormalPaletteFade(PALETTES_ALL, 1, 0, SHOT_FADE_DEPTH, RGB_BLACK);
            else if (t == SHOT_REGEN_AT)
                BuildPlane();
            else if (t == SHOT_FADE_IN_AT)
                BeginNormalPaletteFade(PALETTES_ALL, 1, SHOT_FADE_DEPTH, 0, RGB_BLACK);
        }
    }

    sCamera.x += (speed * gSineTable[sCamera.yaw & 0xFF]) >> 8;
    sCamera.z += (speed * gSineTable[(sCamera.yaw + 64) & 0xFF]) >> 8;
}

static void VBlankCB_Mode7(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();

    // Built here rather than in the main loop so nothing writes the table
    // while the DMA is walking it. The table is single-buffered to keep it to
    // 2,560 bytes of EWRAM; if this ever overruns VBlank the fix is to double
    // buffer and build outside it, at 2,560 bytes more.
    RogueMode7_BuildScanlineTable(&sCamera);
    RogueMode7_ArmHBlankDma();

    // Sampled last, so it measures everything this handler did.
    sVBlankEndLine = REG_VCOUNT;
}

static void MainCB2_Mode7(void)
{
    // Touching the stick or the shoulders takes the camera off the rails, so
    // the shot can be interrupted to go and look at something. B puts it back.
    if (JOY_HELD(MANUAL_KEYS))
        sAutoCamera = FALSE;

    if (sAutoCamera)
    {
        AdvanceShot();
    }
    else
    {
        s32 cosYaw = gSineTable[(sCamera.yaw + 64) & 0xFF];
        s32 sinYaw = gSineTable[sCamera.yaw & 0xFF];
        s32 speed = 0;

        if (JOY_HELD(DPAD_UP))
            speed = 3 << 8;
        else if (JOY_HELD(DPAD_DOWN))
            speed = -(3 << 8);

        if (speed != 0)
        {
            sCamera.x += (speed * sinYaw) >> 8;
            sCamera.z += (speed * cosYaw) >> 8;
        }

        if (JOY_HELD(DPAD_LEFT))
            sCamera.yaw--;
        if (JOY_HELD(DPAD_RIGHT))
            sCamera.yaw++;

        // Altitude is the interesting axis to sweep by hand: it decides how
        // many rows near the horizon get parked.
        if (JOY_HELD(R_BUTTON) && sCamera.height < (200 << 8))
            sCamera.height += (1 << 8);
        if (JOY_HELD(L_BUTTON) && sCamera.height > (8 << 8))
            sCamera.height -= (1 << 8);
    }

    if (JOY_NEW(START_BUTTON) && sSwarmCount <= MODE7_SWARM_MAX - 4)
        sSwarmCount += 4;
    if (JOY_NEW(SELECT_BUTTON) && sSwarmCount >= 4)
        sSwarmCount -= 4;

    // Double-size doubles the sprite's on-screen WIDTH, and the per-scanline
    // OBJ cost is charged per pixel of width -- so this toggle is the direct
    // test of whether affine sprites really cost what the docs say.
    if (JOY_NEW(A_BUTTON))
        sSwarmDoubleSize ^= 1;
    // A fresh floor and a fresh run of the shot. The plane is 4 KB of map and
    // nine tiles built in code, which is what makes generating one per boot
    // realistic rather than a stretch goal.
    if (JOY_NEW(B_BUTTON))
    {
        BuildPlane();
        StartShot();
    }

    sFrame++;
    UpdateSwarm();
    UpdateTwinkle();
    UpdateMeters();

    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

void CB2_RogueMode7Test(void)
{
    switch (gMain.state)
    {
    default:
    case 0:
        SetVBlankCallback(NULL);
        RogueMode7_Stop();
        ScanlineEffect_Stop();

        SetGpuReg(REG_OFFSET_DISPCNT, 0);
        SetGpuReg(REG_OFFSET_BG0CNT, 0);
        SetGpuReg(REG_OFFSET_BG1CNT, 0);
        SetGpuReg(REG_OFFSET_BG2CNT, 0);
        SetGpuReg(REG_OFFSET_BG3CNT, 0);
        SetGpuReg(REG_OFFSET_BLDCNT, 0);
        DmaFill16(3, 0, (void *)VRAM, VRAM_SIZE);
        DmaFill32(3, 0, (void *)OAM, OAM_SIZE);
        DmaFill16(3, 0, (void *)PLTT, PLTT_SIZE);
        ResetPaletteFade();
        ResetTasks();
        ResetSpriteData();
        gMain.state = 1;
        break;
    case 1:
        BuildPlane();
        SetPlanePalette();
        BuildMeterTiles();
        SetMeterPalette();
        BuildFogTiles();
        SetFogPalette();
        BuildFogMap();
        LoadSwarmGfx();
        CreateSwarm();
        sSwarmCount = 0;
        sSwarmDoubleSize = 0;
        sFrame = 0;

        StartShot();
        gMain.state = 2;
        break;
    case 2:
        // BGCNT_WRAP is what makes the flight endless: without it the plane is
        // transparent outside its 512x512, instead of tiling.
        SetGpuReg(REG_OFFSET_BG2CNT, BGCNT_PRIORITY(1)
                                   | BGCNT_CHARBASE(MODE7_CHAR_BASE)
                                   | BGCNT_SCREENBASE(MODE7_SCREEN_BASE)
                                   | BGCNT_256COLOR
                                   | BGCNT_AFF512x512
                                   | BGCNT_WRAP);

        RogueMode7_BuildScanlineTable(&sCamera);
        RogueMode7_ArmHBlankDma();

        // In front of the plane, so the fog and vignette sit over the ground
        // rather than under it. Priority ties break toward the lower BG, so the
        // meters on BG0 still draw on top of all of it.
        SetGpuReg(REG_OFFSET_BG1CNT, BGCNT_PRIORITY(0)
                                   | BGCNT_CHARBASE(FOG_CHAR_BASE)
                                   | BGCNT_SCREENBASE(FOG_SCREEN_BASE)
                                   | BGCNT_16COLOR
                                   | BGCNT_TXT256x256);
        SetGpuReg(REG_OFFSET_BG1HOFS, 0);
        SetGpuReg(REG_OFFSET_BG1VOFS, 0);

        // The one place the blend unit is used, and it is static: BG1's dither
        // blended over everything behind it. That is what turns a visible
        // checkerboard into fog.
        SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT1_BG1 | BLDCNT_EFFECT_BLEND
                                   | BLDCNT_TGT2_BG2 | BLDCNT_TGT2_OBJ
                                   | BLDCNT_TGT2_BD);
        SetGpuReg(REG_OFFSET_BLDALPHA, BLDALPHA_BLEND(FOG_BLEND_EVA, FOG_BLEND_EVB));

        SetGpuReg(REG_OFFSET_BG0CNT, BGCNT_PRIORITY(0)
                                   | BGCNT_CHARBASE(METER_CHAR_BASE)
                                   | BGCNT_SCREENBASE(METER_SCREEN_BASE)
                                   | BGCNT_16COLOR
                                   | BGCNT_TXT256x256);

        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_MODE_1
                                    | DISPCNT_OBJ_1D_MAP
                                    | DISPCNT_BG0_ON
                                    | DISPCNT_BG1_ON
                                    | DISPCNT_BG2_ON
                                    | DISPCNT_OBJ_ON);
        EnableInterrupts(INTR_FLAG_VBLANK);
        SetVBlankCallback(VBlankCB_Mode7);
        SetMainCallback2(MainCB2_Mode7);
        gMain.state = 0;
        break;
    }
}
