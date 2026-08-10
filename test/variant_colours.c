#include "global.h"
#include "test/test.h"
#include "variant_colours.h"

// These run the real code on the emulator, which is the only honest way to
// exercise the fixed-point OkLCH pipeline: it leans on Sin, Cos, ArcTan2 and
// Sqrt, and a host reimplementation of those is a model of the maths rather
// than the maths.
//
// Two of these tests exist because of bugs that reached a screen, and both
// bugs were invisible to a measurement that looked almost right.

// THE PRN IS SIXTEEN BITS AND EACH FIELD OWNS ITS OWN SLICE: hue in bits 0..6,
// chroma in 7..9, luminance in 10..12, and the three direction bits in 13..15.
//
// Sweeping 0..127 therefore exercises hue AND NOTHING ELSE -- it pins chroma
// and luma to zero however large the entry asks for them to be. A calibration
// run done that way reported that adding chroma and luminance changed the mean
// delta by exactly nothing, which is true of the sweep and false of the code.
// A coprime stride over the full range reaches every field.
#define PRN_STRIDE 37

// A spread of real palette colours rather than a gradient: saturated primaries
// where hue is meaningful, muddy mid-tones where most Pokemon actually live,
// and the near-greys that outlines and eyes are made of and where hue means
// nothing at all.
//
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
    u32 e;

    if (dr < 0) dr = -dr;
    if (dg < 0) dg = -dg;
    if (db < 0) db = -db;

    e = (u32)dr;
    if ((u32)dg > e) e = (u32)dg;
    if ((u32)db > e) e = (u32)db;
    return e;
}

// ---------------------------------------------------------------------------
// Round-trip fidelity, split by colour family.
//
// SPLIT, because measuring it as one aggregate said "worst error 6 of 31" and
// made the conversion look broken. It is not: 6 is one gamut corner, and the
// colours real sprites are made of come back at 1 and 0. Per instance, never
// in aggregate, and never averaged across mixed categories.

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

TEST("variant colours: round trip loss on greys and outlines")
{
    // EXACT, and this is the most important number here. Outlines, eyes and
    // shading are where an error would be most visible, and hue is meaningless
    // at zero chroma so nothing moves them at all.
    EXPECT_EQ(RoundTripWorst(12, 15), 0);
}

TEST("variant colours: round trip loss on muddy mid-tones")
{
    // One level of 31, on the colour family most of every Pokemon is made of.
    EXPECT_LE(RoundTripWorst(7, 11), 1);
}

TEST("variant colours: round trip loss on saturated primaries")
{
    // Six levels, and it is gamut clipping rather than lost precision: chroma
    // is stored doubled in a u8, and a fully saturated RGB555 primary drives
    // that past 255 and clamps. A corner of the colour space that Pokemon
    // palettes almost never contain.
    EXPECT_LE(RoundTripWorst(1, 6), 6);
}

// ---------------------------------------------------------------------------
// Behaviour of the getter.

TEST("variant colours: a shiny keeps the palette it was handed")
{
    const u16 *out = GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, TRUE, FALSE, 0x12345678);

    // Pointer identity, not contents: the point is that no copy is made and the
    // caller keeps its pointer into ROM.
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

    // Churns the ring in between, so this also proves a mon does not depend on
    // which ring slot it lands in.
    GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, FALSE, FALSE, 0x11111111);
    GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, FALSE, FALSE, 0x22222222);
    GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, FALSE, FALSE, 0x33333333);
    GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, FALSE, FALSE, 0x44444444);

    b = GetMonSpritePalVariant(sTestPal, SPECIES_ZUBAT, FALSE, FALSE, 0xABCD1234);

    for (i = 0; i < 16; i++)
        EXPECT_EQ(b[i], first[i]);
}

// ---------------------------------------------------------------------------
// End to end against REAL species palettes, through the functions the game
// actually calls.
//
// GetMonSpritePalFromSpecies is the unhooked getter -- it takes isFemale and
// has no personality -- so it returns the stock palette and is a free control.
// GetMonSpritePalFromSpeciesAndPersonality is the hooked one. The difference
// between the two IS the feature.
//
// ARON IS HERE BECAUSE IT SHIPPED LOOKING WRONG. It is a nearly grey species,
// the variant was hue-only, and hue is meaningless at zero chroma -- so it came
// out looking like stock colours in play while every host-side number said the
// feature worked. A grey species is the worst case and therefore the one worth
// asserting on.

static u32 RealSpeciesMaxDelta(u32 species)
{
    const u16 *stock = GetMonSpritePalFromSpecies(species, FALSE, FALSE);
    u16 stockCopy[16];
    u32 pid, i, worst = 0;

    for (i = 0; i < 16; i++)
        stockCopy[i] = stock[i];

    for (pid = 0; pid < 65536; pid += PRN_STRIDE)
    {
        const u16 *out = GetMonSpritePalFromSpeciesAndPersonality(species, FALSE, pid);

        for (i = 1; i < 16; i++)
        {
            u32 e = ChannelError(out[i], stockCopy[i]);
            if (e > worst)
                worst = e;
        }
    }
    return worst;
}

// How many of the fifteen real colours ever move. A single colour shifting hard
// while the rest sit still is what "it looks stock" is made of -- the body
// dominates what the eye sees, and a max-of-everything hides that completely.
static u32 RealSpeciesColoursThatMove(u32 species)
{
    const u16 *stock = GetMonSpritePalFromSpecies(species, FALSE, FALSE);
    u16 stockCopy[16];
    bool32 movesAtAll[16] = {FALSE};
    u32 pid, i, moved = 0;

    for (i = 0; i < 16; i++)
        stockCopy[i] = stock[i];

    for (pid = 0; pid < 65536; pid += PRN_STRIDE)
    {
        const u16 *out = GetMonSpritePalFromSpeciesAndPersonality(species, FALSE, pid);

        for (i = 1; i < 16; i++)
        {
            if (ChannelError(out[i], stockCopy[i]) != 0)
                movesAtAll[i] = TRUE;
        }
    }

    for (i = 1; i < 16; i++)
    {
        if (movesAtAll[i])
            moved++;
    }
    return moved;
}

TEST("variant colours: a grey species (Aron) moves enough to see")
{
    // 13 of 31 levels at the time of writing, against 6 when the variant was
    // hue-only and Aron looked stock on a screen.
    EXPECT_GE(RealSpeciesMaxDelta(SPECIES_ARON), 12);
}

TEST("variant colours: most of a grey species' colours move")
{
    // 12 of 15, against 9 when it was hue-only.
    EXPECT_GE(RealSpeciesColoursThatMove(SPECIES_ARON), 11);
}

TEST("variant colours: a coloured species (Zubat) moves enough to see")
{
    // 17 of 31. A coloured species gets roughly twice what a grey one does from
    // the same setting, which is exactly why calibrating on a coloured species
    // alone is how a grey one ends up looking untouched.
    EXPECT_GE(RealSpeciesMaxDelta(SPECIES_ZUBAT), 16);
}

TEST("variant colours: all of a coloured species' colours move")
{
    EXPECT_GE(RealSpeciesColoursThatMove(SPECIES_ZUBAT), 14);
}
