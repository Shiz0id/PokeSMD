#include "global.h"
#include "test/test.h"
#include "variant_colours.h"

// These run the real code on the emulator, which is the only way to exercise
// the fixed-point OkLCH pipeline honestly: it leans on Sin, Cos, ArcTan2 and
// Sqrt, and a host reimplementation of those is a model of the maths rather
// than the maths.
//
// The question these exist to answer is not "does it compile". It is whether
// the conversion is lossless enough to be invisible when it is asked to do
// nothing -- because EVERY species without a table entry takes the default
// variant and goes through the full round trip, so conversion loss is not a
// per-species problem, it is a whole-dex recolour that nobody asked for.

// A spread of real palette colours rather than a gradient: saturated primaries
// where hue is meaningful, muddy mid-tones where most Pokemon actually live,
// and the near-greys that outlines and eyes are made of and where hue means
// nothing at all.
// Raw RGB555 literals rather than RGB(): that macro is not a constant
// expression in this tree, so it cannot initialise a static.
static const u16 sTestPal[16] = {
    0x0000,           // transparent slot
    0x001F,           // 31, 0, 0    saturated red
    0x03E0,           // 0, 31, 0    saturated green
    0x7C00,           // 0, 0, 31    saturated blue
    0x03FF,           // 31,31, 0    yellow
    0x7C1F,           // 31, 0,31    magenta
    0x7FE0,           // 0, 31,31    cyan
    0x1994,           // 20,12, 6    brown, where a lot of Pokemon live
    0x520C,           // 12,16,20    slate blue
    0x3912,           // 18, 8,14    dull purple
    0x3299,           // 25,20,12    tan
    0x29C8,           // 8, 14,10    dark green
    0x7FFF,           // 31,31,31    white
    0x4210,           // 16,16,16    mid grey
    0x1084,           // 4, 4, 4     near black, an outline
    0x635C,           // 28,26,24    off-white highlight
};

// Asks for a hue range, but with a PRN of 0 every derived shift is exactly
// zero: hueShift8, chrAmtPct and lumAmtPct all scale from bit fields that are
// all clear. So what comes back is the pure round trip with nothing done in
// between, and any difference is conversion loss.
static const struct PaletteVariant sNullShift = {
    .start = 0,
    .length = 15,
    .hue_amount = 1,
    .chr_amount = 0,
    .lum_amount = 0,
    .sv_down_only = FALSE,
};

static u32 ChannelError(u16 a, u16 b)
{
    s32 dr = (s32)(a & 0x1F) - (s32)(b & 0x1F);
    s32 dg = (s32)((a >> 5) & 0x1F) - (s32)((b >> 5) & 0x1F);
    s32 db = (s32)((a >> 10) & 0x1F) - (s32)((b >> 10) & 0x1F);
    u32 e = 0;

    if (dr < 0) dr = -dr;
    if (dg < 0) dg = -dg;
    if (db < 0) db = -db;

    e = (u32)dr;
    if ((u32)dg > e) e = (u32)dg;
    if ((u32)db > e) e = (u32)db;
    return e;
}

// Split by colour family, one test each, because the runner prints only the
// first failing assertion per test and these three numbers are the whole
// diagnosis: which kinds of colour the conversion cannot return unchanged.
// Measuring them as one aggregate hid the answer -- a single worst-of-15 said
// "error 6" and made the conversion look broken, when 6 is one gamut corner
// and the colours real sprites are made of come back at 1 and 0.
static u32 RoundTripWorst(u32 lo, u32 hi)
{
    u32 i, worst = 0;
    u16 pal[16];

    for (i = 0; i < 16; i++)
        pal[i] = sTestPal[i];

    ApplyPaletteVariantToPaletteBuffer(pal, &sNullShift, 0);

    for (i = lo; i <= hi; i++)
    {
        u32 e = ChannelError(pal[i], sTestPal[i]);
        if (e > worst)
            worst = e;
    }
    return worst;
}

TEST("variant colours: round trip loss on saturated primaries")
{
    // SIX levels of 31, and this is the only place the conversion is anything
    // but faithful. It is gamut clipping rather than lost precision: chroma is
    // stored doubled in a u8 and a fully saturated RGB555 primary drives that
    // past 255, so it clamps and comes back less saturated than it went in.
    // Tolerated because a fully saturated primary is a corner of the colour
    // space that Pokemon palettes almost never contain -- and because the two
    // families that DO make up real sprites are measured below at 1 and 0.
    EXPECT_LE(RoundTripWorst(1, 6), 6);
}

TEST("variant colours: round trip loss on muddy mid-tones")
{
    // One level of 31, on the colour family most of every Pokemon is made of.
    // Imperceptible, and the reason the whole-dex recolour this test was
    // written to look for is not happening.
    EXPECT_LE(RoundTripWorst(7, 11), 1);
}

TEST("variant colours: round trip loss on greys and outlines")
{
    // EXACT, which is the single most important number here. Outlines, eyes
    // and shading are where an error would be most visible, and hue is
    // meaningless at zero chroma so nothing moves them at all.
    EXPECT_EQ(RoundTripWorst(12, 15), 0);
}

TEST("variant colours: a shiny keeps the palette it was handed")
{
    const u16 *out = GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, TRUE, FALSE, 0x12345678);

    // Pointer identity, not contents: the whole point is that no copy is made
    // and the caller keeps its pointer into ROM.
    EXPECT_EQ(out, sTestPal);
}

TEST("variant colours: an egg keeps the palette it was handed")
{
    const u16 *out = GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, FALSE, TRUE, 0x12345678);

    EXPECT_EQ(out, sTestPal);
}

TEST("variant colours: the same mon is the same colour every time")
{
    const u16 *a = GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, FALSE, FALSE, 0xABCD1234);
    u16 first[16];
    const u16 *b;
    u32 i;

    for (i = 0; i < 16; i++)
        first[i] = a[i];

    // Deliberately churns the ring in between, so this also proves a mon does
    // not depend on which ring slot it lands in.
    GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, FALSE, FALSE, 0x11111111);
    GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, FALSE, FALSE, 0x22222222);
    GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, FALSE, FALSE, 0x33333333);
    GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, FALSE, FALSE, 0x44444444);

    b = GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, FALSE, FALSE, 0xABCD1234);

    for (i = 0; i < 16; i++)
        EXPECT_EQ(b[i], first[i]);
}

TEST("variant colours: the default variant actually changes the palette")
{
    const u16 *out = GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, FALSE, FALSE, 0x0000007F);
    u32 i, changed = 0;

    for (i = 1; i < 16; i++)
    {
        if (out[i] != sTestPal[i])
            changed++;
    }

    // If this is zero the feature does nothing at all, which is the failure
    // mode that would otherwise only show up as "I cannot see any difference".
    EXPECT_GT(changed, 0);
}

// How many visibly different colours the variant actually produces in one
// palette slot, sweeping every PID the shift can distinguish.
//
// The hue shift is drawn from 7 bits of the PID scaled into 0..hmax, so 128
// PIDs is the WHOLE space rather than a sample. hmax is 14 at the default
// hue_amount of 20 degrees, giving 15 possible shifts -- and the answer is
// always lower than that, because RGB555 has 32 levels per channel and a small
// hue rotation lands several shifts on the same quantised colour.
static u32 DistinctColoursAt(u32 index)
{
    u16 seen[64];
    u32 nSeen = 0;
    u32 pid, i;

    for (pid = 0; pid < 128; pid++)
    {
        const u16 *out = GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, FALSE, FALSE, pid);
        u16 c = out[index];
        bool32 dup = FALSE;

        for (i = 0; i < nSeen; i++)
        {
            if (seen[i] == c)
            {
                dup = TRUE;
                break;
            }
        }
        if (!dup && nSeen < ARRAY_COUNT(seen))
            seen[nSeen++] = c;
    }
    return nSeen;
}

TEST("variant colours: distinct colours produced on a saturated primary")
{
    // 10 of a possible 15 at the default 20 degrees. Was 6 at 10 degrees,
    // which is what the widening bought.
    EXPECT_GE(DistinctColoursAt(1), 10);
}

TEST("variant colours: distinct colours produced on a muddy mid-tone")
{
    // The one that matters. Saturated primaries are rare in Pokemon palettes
    // and are also where the round trip clips; this is the colour family real
    // sprites are mostly made of.
    EXPECT_GE(DistinctColoursAt(7), 7);
}
