#include "global.h"
#include "bg.h"
#include "gpu_regs.h"
#include "main.h"
#include "palette.h"
#include "scanline_effect.h"
#include "sprite.h"
#include "pokemon_icon.h"
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

// Deliberately not the icon palette: the body goes dark violet and the eye
// stays bright, which is what makes an Unown read at ten pixels. Recoloured
// here rather than in the art, so the art stays the stock icon.
static const u16 sSwarmPalette[16] =
{
    RGB(0, 0, 0),      RGB(1, 1, 2),      RGB(4, 3, 7),      RGB(7, 5, 11),
    RGB(10, 8, 15),    RGB(13, 11, 19),   RGB(16, 14, 23),   RGB(20, 27, 29),
    RGB(29, 31, 31),   RGB(6, 4, 10),     RGB(9, 7, 14),     RGB(12, 10, 18),
    RGB(15, 13, 22),   RGB(18, 16, 26),   RGB(22, 20, 28),   RGB(26, 28, 30),
};

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
    struct SpritePalette pal = { sSwarmPalette, SWARM_PAL_TAG };
    u32 i;

    LoadSpritePalette(&pal);

    // GetMonIconTiles resolves the Unown FORM from the personality, so eight
    // different personalities give eight different letters without naming a
    // single form symbol. Each icon is 32x64 (two animation frames); only the
    // first 32x32 frame is wanted, hence 512 bytes.
    for (i = 0; i < MODE7_SWARM_FORMS; i++)
    {
        struct SpriteSheet sheet;

        sheet.data = GetMonIconTiles(SPECIES_UNOWN, i * 0x1234567 + i);
        sheet.size = 512;
        sheet.tag = SWARM_GFX_TAG + i;
        LoadSpriteSheet(&sheet);
    }
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

// A checkerboard with a bordered tile and a coarse accent grid. Flat colour
// hides shear; borders and a grid do not, which is the entire point of testing
// against this rather than against art.
static void BuildPlane(void)
{
    ALIGNED(4) u8 tile[64];
    ALIGNED(4) u8 row[MODE7_PLANE_TILES];
    u32 i, x, y;

    // VRAM rejects 8-bit writes, so every tile and every map row is assembled
    // in RAM and copied as words. Writing a u8 straight to VRAM here would
    // corrupt the neighbouring byte instead of failing.
    for (i = 0; i < 64; i++)
    {
        bool32 edge = (i < 8) || (i >= 56) || ((i & 7) == 0) || ((i & 7) == 7);
        tile[i] = edge ? 2 : 1;
    }
    CpuCopy32(tile, (void *)(BG_CHAR_ADDR(MODE7_CHAR_BASE) + 1 * 64), 64);

    for (i = 0; i < 64; i++)
    {
        bool32 edge = (i < 8) || (i >= 56) || ((i & 7) == 0) || ((i & 7) == 7);
        tile[i] = edge ? 1 : 3;
    }
    CpuCopy32(tile, (void *)(BG_CHAR_ADDR(MODE7_CHAR_BASE) + 2 * 64), 64);

    for (i = 0; i < 64; i++)
        tile[i] = 4;
    CpuCopy32(tile, (void *)(BG_CHAR_ADDR(MODE7_CHAR_BASE) + 3 * 64), 64);

    // Affine map entries are ONE BYTE each -- no priority, no palette bank, no
    // flip. That byte is why the plane is capped at 256 distinct tiles.
    for (y = 0; y < MODE7_PLANE_TILES; y++)
    {
        for (x = 0; x < MODE7_PLANE_TILES; x++)
        {
            if ((x % 8) == 0 || (y % 8) == 0)
                row[x] = 3;
            else
                row[x] = ((x ^ y) & 1) ? 1 : 2;
        }
        CpuCopy32(row,
                  (void *)(BG_SCREEN_ADDR(MODE7_SCREEN_BASE)
                           + y * MODE7_PLANE_TILES),
                  MODE7_PLANE_TILES);
    }
}

static void SetPlanePalette(void)
{
    static const u16 sPal[] = {
        RGB(0, 0, 0),        // 0 unused by the plane; the backdrop colour
        RGB(24, 20, 14),     // 1 light check
        RGB(14, 11, 8),      // 2 dark check / tile border
        RGB(8, 7, 12),       // 3 accent grid
        RGB(31, 28, 16),     // 4 marker
    };

    LoadPalette(sPal, BG_PLTT_ID(0), sizeof(sPal));
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

    // Altitude is the interesting axis to be able to sweep by hand: it is what
    // decides how many rows near the horizon get parked.
    if (JOY_HELD(R_BUTTON) && sCamera.height < (200 << 8))
        sCamera.height += (1 << 8);
    if (JOY_HELD(L_BUTTON) && sCamera.height > (8 << 8))
        sCamera.height -= (1 << 8);

    if (JOY_NEW(START_BUTTON) && sSwarmCount <= MODE7_SWARM_MAX - 4)
        sSwarmCount += 4;
    if (JOY_NEW(SELECT_BUTTON) && sSwarmCount >= 4)
        sSwarmCount -= 4;

    // Double-size doubles the sprite's on-screen WIDTH, and the per-scanline
    // OBJ cost is charged per pixel of width -- so this toggle is the direct
    // test of whether affine sprites really cost what the docs say.
    if (JOY_NEW(A_BUTTON))
        sSwarmDoubleSize ^= 1;
    if (JOY_NEW(B_BUTTON))
        sSwarmCount = sSwarmCount ? 0 : MODE7_SWARM_MAX;

    sFrame++;
    UpdateSwarm();
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
        LoadSwarmGfx();
        CreateSwarm();
        sSwarmCount = 0;
        sSwarmDoubleSize = 0;
        sFrame = 0;

        sCamera.x = 256 << 8;
        sCamera.z = 0;
        sCamera.height = 88 << 8;   // inside the clean window the proto measured
        sCamera.yaw = 0;
        sCamera.horizon = 48;
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

        SetGpuReg(REG_OFFSET_BG0CNT, BGCNT_PRIORITY(0)
                                   | BGCNT_CHARBASE(METER_CHAR_BASE)
                                   | BGCNT_SCREENBASE(METER_SCREEN_BASE)
                                   | BGCNT_16COLOR
                                   | BGCNT_TXT256x256);

        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_MODE_1
                                    | DISPCNT_OBJ_1D_MAP
                                    | DISPCNT_BG0_ON
                                    | DISPCNT_BG2_ON
                                    | DISPCNT_OBJ_ON);
        EnableInterrupts(INTR_FLAG_VBLANK);
        SetVBlankCallback(VBlankCB_Mode7);
        SetMainCallback2(MainCB2_Mode7);
        gMain.state = 0;
        break;
    }
}
