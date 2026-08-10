#include "variant_colours.h"
#include "trig.h"
#include "fpmath.h"

/* ---------- helpers ---------- */

typedef u8 uQ0_8;
typedef u16 uQ8_8;
typedef s16 sQ8_8;
typedef s32 sQ24_8;

// Upstream puts the three lookup tables and Cbrt in IWRAM. Dropped, and the
// reason is the budget rather than taste: IWRAM is 28460/32768 on this branch,
// so 544 bytes of table is an eighth of everything left, and this code runs
// once per palette load -- on a sprite appearing, not per frame. ROM is at
// 86.67% with megabytes free. Trading the scarce region for the abundant one
// to speed up something that is not in a frame loop is the wrong way round.
#define CV_TABLE_DATA

#if FALSE
#define DebugPrintfCV DebugPrintf
#else
#define DebugPrintfCV(...)
#endif

// We already have some functions from syscalls:
// s16 Sin(s16 index, s16 amplitude) as Q8.8
// s16 Cos(s16 index, s16 amplitude) as Q8.8
// u16 Sqrt(u32 num)

#define Q8_8(n) ((s16)((n) >= 0 ? ((n) * 256.0f + 0.5f) \
                                : ((n) * 256.0f - 0.5f)))

static inline uQ8_8 mul_uq8_8(uQ8_8 a, uQ8_8 b)
{
  u32 product = (a * b) + 0x80;
  return product >> 8;
}

static inline sQ8_8 mul_sq8_8(sQ8_8 a, sQ8_8 b)
{
  s32 product = (a * b);
  product = product + (product < 0 ? -0x80 : 0x80);
  return product / 256;
}

static inline u8 ClampU8(u16 x, u8 lo, u8 hi)
{
  if (x < lo)
    return lo;
  if (x > hi)
    return hi;
  return (u8)x;
}

static inline u8 ClampSignedToU8(s16 x, s16 lo, s16 hi)
{
  if (x < lo)
    return (u8)lo;
  if (x > hi)
    return (u8)hi;
  return (u8)x;
}

#define BITS(v, lo, n) ((u32)((v) >> (lo)) & ((1u << (n)) - 1u))

// Scale `value` in [0 .. (2^bits - 1)] to an inclusive u8 range.
static inline u8 ScaleToRange(u32 value, u8 minVal, u8 maxVal, u8 bits)
{
  if (minVal == maxVal)
    return minVal;

  u8 b = (bits > 31) ? 31 : bits;
  u32 mask = (b >= 31) ? 0xFFFFFFFFu : ((1u << b) - 1u);
  u32 v = value & mask;
  const bool8 wrapped = (minVal > maxVal);
  u32 span = wrapped
                 ? (u32)(256u - (u32)minVal) + ((u32)maxVal + 1u) // [min..255] + [0..max]
                 : ((u32)maxVal - (u32)minVal + 1u);              // [min..max]

  u32 scaled = (v * span) >> b;
  if (scaled >= span)
    scaled = span - 1;
  if (!wrapped)
  {
    return (u8)(minVal + (u8)scaled);
  }
  else
  {
    return (u8)(((u32)minVal + scaled) & 0xFFu);
  }
}

static inline void Rgb555Unpack(u16 c, u8 *r5, u8 *g5, u8 *b5)
{
  *r5 = (u8)(c & 0x1Fu);
  *g5 = (u8)((c >> 5) & 0x1Fu);
  *b5 = (u8)((c >> 10) & 0x1Fu);
}

static inline u16 Rgb555Pack(u8 r5, u8 g5, u8 b5)
{
  if (r5 > 31)
    r5 = 31;
  if (g5 > 31)
    g5 = 31;
  if (b5 > 31)
    b5 = 31;
  return (u16)((r5 & 31u) | ((u16)(g5 & 31u) << 5) | ((u16)(b5 & 31u) << 10));
}

static const CV_TABLE_DATA u8 cbrt_q0_8_lut[256] = {
      0,  40,  51,  58,  64,  69,  73,  77,  80,  84,  87,  89,  92,  95,  97,  99,
    101, 103, 105, 107, 109, 111, 113, 114, 116, 118, 119, 121, 122, 124, 125, 126,
    128, 129, 130, 132, 133, 134, 135, 136, 138, 139, 140, 141, 142, 143, 144, 145,
    146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 157, 158, 159, 160,
    161, 162, 163, 163, 164, 165, 166, 167, 167, 168, 169, 170, 170, 171, 172, 173,
    173, 174, 175, 175, 176, 177, 177, 178, 179, 180, 180, 181, 182, 182, 183, 183,
    184, 185, 185, 186, 187, 187, 188, 188, 189, 190, 190, 191, 191, 192, 193, 193,
    194, 194, 195, 196, 196, 197, 197, 198, 198, 199, 199, 200, 201, 201, 202, 202,
    203, 203, 204, 204, 205, 205, 206, 206, 207, 207, 208, 208, 209, 209, 210, 210,
    211, 211, 212, 212, 213, 213, 214, 214, 215, 215, 216, 216, 216, 217, 217, 218,
    218, 219, 219, 220, 220, 221, 221, 221, 222, 222, 223, 223, 224, 224, 224, 225,
    225, 226, 226, 227, 227, 227, 228, 228, 229, 229, 230, 230, 230, 231, 231, 232,
    232, 232, 233, 233, 234, 234, 234, 235, 235, 236, 236, 236, 237, 237, 237, 238,
    238, 239, 239, 239, 240, 240, 241, 241, 241, 242, 242, 242, 243, 243, 243, 244,
    244, 245, 245, 245, 246, 246, 246, 247, 247, 247, 248, 248, 249, 249, 249, 250,
    250, 250, 251, 251, 251, 252, 252, 252, 253, 253, 253, 254, 254, 254, 255, 255,
};

// Cube root
static inline u8 Cbrt(u8 x)
{
  return cbrt_q0_8_lut[x];
}

static inline u8 Atan2(s16 y, s16 x)
{
  if ((x | y) == 0)
    return 0;
  // ArcTan2 returns unsigned angle (wrap-safe). Convert to 0..255
  u16 a = (u16)ArcTan2(y, x);
  return (u8)(a >> 8); // 0..255
}

/* -------- Colourspace conversions -------- */

static const CV_TABLE_DATA uQ0_8 rgb5_to_linear_rgb_q_0_8[32] = {
    0,   1,   1,   2,   4,   6,   8,  11,
   14,  18,  22,  26,  32,  38,  44,  51,
   59,  67,  76,  85,  96, 107, 118, 130,
  143, 158, 172, 187, 203, 221, 238, 255
};

static const CV_TABLE_DATA u8 linear_rgb_q0_8_to_rgb5[256] = {
   0,  2,  3,  3,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  9,
   9,  9,  9, 10, 10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12,
  12, 12, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 15, 15,
  15, 15, 15, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17,
  17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20, 20, 20, 20, 20,
  20, 20, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22,
  22, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23,
  23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
  24, 24, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25,
  26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 27, 27,
  27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 28,
  28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 29, 29,
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
  30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
  30, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31,
};

// Based on https://bottosson.github.io/posts/oklab/#converting-from-linear-srgb-to-oklab

static inline void Rgb5ToOklch(u8 r5, u8 g5, u8 b5, uQ0_8 *L, uQ0_8 *C, uQ0_8 *H)
{
  DebugPrintfCV("rgb5: %u %u %u", r5, g5, b5);

  // Step 1: Convert RGB555 to linear RGB Q0.8
  uQ8_8 r = rgb5_to_linear_rgb_q_0_8[r5];
  uQ8_8 g = rgb5_to_linear_rgb_q_0_8[g5];
  uQ8_8 b = rgb5_to_linear_rgb_q_0_8[b5];
  DebugPrintfCV("rgb : %u %u %u", r, g, b);

  // Step 2: Calculate L, M & S
  uQ8_8 l = (mul_uq8_8(Q8_8(0.4122214708f), r)) + (mul_uq8_8(Q8_8(0.5363325363f), g)) + (mul_uq8_8(Q8_8(0.0514459929f), b));
  uQ8_8 m = (mul_uq8_8(Q8_8(0.2119034982f), r)) + (mul_uq8_8(Q8_8(0.6806995451f), g)) + (mul_uq8_8(Q8_8(0.1073969566f), b));
  uQ8_8 s = (mul_uq8_8(Q8_8(0.0883024619f), r)) + (mul_uq8_8(Q8_8(0.2817188376f), g)) + (mul_uq8_8(Q8_8(0.6299787005f), b));
  DebugPrintfCV("lms : %u %u %u", l, m, s);

  // Step 3: Cube root
  uQ8_8 l_ = Cbrt(l & 0xFF);
  uQ8_8 m_ = Cbrt(m & 0xFF);
  uQ8_8 s_ = Cbrt(s & 0xFF);
  DebugPrintfCV("lms_: %u %u %u", l_, m_, s_);

  // Step 4: Calculate unbounded LAB values in sQ8.8
  sQ8_8 Lu = ((s32)(mul_uq8_8(Q8_8(0.2104542553f), l_)) + (s32)(mul_uq8_8(Q8_8(0.7936177850f), m_)) - (s32)(mul_uq8_8(Q8_8(0.0040720468f), s_)));
  sQ8_8 Au = ((s32)(mul_uq8_8(Q8_8(1.9779984951f), l_)) - (s32)(mul_uq8_8(Q8_8(2.4285922050f), m_)) + (s32)(mul_uq8_8(Q8_8(0.4505937099f), s_)));
  sQ8_8 Bu = ((s32)(mul_uq8_8(Q8_8(0.0259040371f), l_)) + (s32)(mul_uq8_8(Q8_8(0.7827717662f), m_)) - (s32)(mul_uq8_8(Q8_8(0.8086757660f), s_)));
  DebugPrintfCV("LAB : %d %d %d", Lu, Au, Bu);

  // Step 5: Calculate bounded CH values in Q8
  *L = ClampSignedToU8(Lu, 0, 255);
  *C = ClampU8(Sqrt((u32)(Au * Au + Bu * Bu)) * 2, 0, 255);
  if (*C == 0)
    *H = 0;
  else
    *H = Atan2(Bu, Au);
  DebugPrintfCV("LCH : %u %u %u", *L, *C, *H);
}

static inline void OklchToRgb5(uQ0_8 L, uQ0_8 C, uQ0_8 H, u8 *r, u8 *g, u8 *b)
{
  DebugPrintfCV("LCH : %u %u %u", L, C, H);

  // Step 1: CH to AB
  s32 A = Sin(H, (C / 2)); // Default Cos & Sin return Q8_8
  s32 B = Cos(H, (C / 2));
  DebugPrintfCV("AB  : %d %d ", A, B);

  // Step 2: Calculate l_ m_ s_ values
  sQ8_8 l_ = L + mul_sq8_8(Q8_8(0.3963377774f), A) + mul_sq8_8(Q8_8(0.2158037573f), B);
  sQ8_8 m_ = L - mul_sq8_8(Q8_8(0.1055613458f), A) - mul_sq8_8(Q8_8(0.0638541728f), B);
  sQ8_8 s_ = L - mul_sq8_8(Q8_8(0.0894841775f), A) - mul_sq8_8(Q8_8(1.2914855480f), B);
  DebugPrintfCV("lms_: %d %d %d", l_, m_, s_);

  // Step 3: Cube back to l m s values
  uQ8_8 l = mul_sq8_8(mul_sq8_8(l_, l_), l_);
  sQ8_8 m = mul_sq8_8(mul_sq8_8(m_, m_), m_);
  sQ8_8 s = mul_sq8_8(mul_sq8_8(s_, s_), s_);
  DebugPrintfCV("lms : %u %u %u", l, m, s);

  // Step 4: Calculate RGB values (unbounded)
  sQ8_8 ru = mul_sq8_8(Q8_8(4.0767416621f), l) - mul_sq8_8(Q8_8(3.3077115913f), m) + mul_sq8_8(Q8_8(0.2309699292f), s);
  sQ8_8 gu = -mul_sq8_8(Q8_8(1.2684380046f), l) + mul_sq8_8(Q8_8(2.6097574011f), m) - mul_sq8_8(Q8_8(0.3413193965f), s);
  sQ8_8 bu = -mul_sq8_8(Q8_8(0.0041960863f), l) - mul_sq8_8(Q8_8(0.7034186147f), m) + mul_sq8_8(Q8_8(1.7076147010f), s);
  DebugPrintfCV("rgbu: %u %u %u", ru, gu, bu);

  *r = linear_rgb_q0_8_to_rgb5[ClampSignedToU8(ru, 0, 255)];
  *g = linear_rgb_q0_8_to_rgb5[ClampSignedToU8(gu, 0, 255)];
  *b = linear_rgb_q0_8_to_rgb5[ClampSignedToU8(bu, 0, 255)];
  DebugPrintfCV("rgb : %u %u %u\n", *r, *g, *b);
}

/* -------- Variants data -------- */

// Upstream keeps these three tables in the HEADER as `static const`, which
// gives every translation unit that includes it a private copy. That was
// invisible upstream because exactly one file included it; here the header is
// pulled in by pokemon.c and rogue_bw_anim.c as well, and gSpeciesVariants is
// NUM_SPECIES * 4 bytes, so it would have been three copies of several KB.
// The table is private to this file and reached through GetSpeciesVariants,
// which is the accessor upstream already provided and then bypassed.

// Precomputed hue-amount table
// Code uses hue in [0..255] instead of [0..360]
// {0,10,20,30,45,60,90,180} -> {0,7,14,21,32,43,64,128}
static const u8 sHueTable[8] = {0, 7, 14, 21, 32, 43, 64, 128};
static const u8 sCLTable[4] = {0, 5, 10, 25};

// What a species with no entry of its own gets. A hue swing of +/-10 degrees
// and nothing else: enough that two Zubats in the same corridor are not the
// same Zubat, small enough that nobody misreads it as a shiny.
#define DEFAULT_SPECIES_VARIANT \
  {                             \
      PAL1(1, 15),              \
      HCL1(10, 0, 0, FALSE),    \
  }

static const struct SpeciesVariant sDefaultSpeciesVariant = DEFAULT_SPECIES_VARIANT;

static const struct SpeciesVariant gSpeciesVariants[NUM_SPECIES] = {
    [SPECIES_POOCHYENA] = {
        PAL1(1, 5),
        HCL1(0, 25, 5, FALSE),
    },
    [SPECIES_MIGHTYENA] = {
        PAL1(1, 5),
        HCL1(0, 25, 5, FALSE),
    },
    [SPECIES_ZIGZAGOON] = {
        PAL1(5, 8),
        HCL1(10, 25, 5, FALSE),
    },
    [SPECIES_LINOONE] = {
        PAL1(1, 3),
        HCL1(10, 25, 5, FALSE),
    },
    [SPECIES_WURMPLE] = {
        PAL1(1, 4),
        HCL1(30, 5, 0, TRUE),
    },
    [SPECIES_TYRANITAR] = {
        PAL1(11, 3),
        HCL1(30, 25, 0, TRUE),
        PAL2(1, 5),
        HCL2(0, 0, 10, FALSE),
    },
};

const struct SpeciesVariant *GetSpeciesVariants(u32 species)
{
  const struct SpeciesVariant *l;

  // A species id past the end of the table indexes ROM that is not the table.
  // Every caller here comes through SanitizeSpeciesId already, so this is the
  // belt to that braces -- but the table is indexed by a value the caller
  // supplies, and that is exactly the shape this project keeps getting wrong.
  if (species >= NUM_SPECIES)
    return &sDefaultSpeciesVariant;

  l = &gSpeciesVariants[species];

  // Treat an all-zero entry as "no variant" and return default.
  if (l->pv1.length == 0 && l->pv2.length == 0 &&
      l->pv1.hue_amount == 0 && l->pv1.chr_amount == 0 && l->pv1.lum_amount == 0 &&
      l->pv2.hue_amount == 0 && l->pv2.chr_amount == 0 && l->pv2.lum_amount == 0)
  {
    // Upstream calls DebugPrintf here rather than its own gated DebugPrintfCV.
    // It is a no-op in a default build, but this runs on every palette load of
    // every mon, so turning mgba logging on would flood the log and cost real
    // time per sprite. Gated like the rest of the file.
    DebugPrintfCV("%d - Default variant", species);
    return &sDefaultSpeciesVariant;
  }
  else
  {
    DebugPrintfCV("%d - Custom variant", species);
  }
  return l;
}

// -------- Core palette application --------

// PRN bit layout (16 bits total):
// [0..6]=hue, [7..9]=chroma, [10..12]=luma, [13]=downH, [14]=downC, [15]=downL
void ApplyPaletteVariantToPaletteBuffer(u16 pal16[16], const struct PaletteVariant *pv, u16 prn16)
{
  u8 start = pv->start;
  u8 len = (u8)(pv->length);
  if (len == 0)
    return;

  const u8 hmax = sHueTable[pv->hue_amount & 7u];
  const u8 cmax = sCLTable[pv->chr_amount & 3u];
  const u8 lmax = sCLTable[pv->lum_amount & 3u];

  // Nothing to do if variant asks for no change at all
  if ((hmax | cmax | lmax) == 0)
    return;

  // End is exclusive (the loop below is `i < iEnd`), so it clamps to 16 and
  // not to 15. Upstream clamps to 15, which means colour index 15 can never be
  // shifted by any entry: the widest range expressible in a 4-bit start and a
  // 4-bit length is PAL(1,15), and that reaches 1..14. A sprite using index 15
  // would keep one original colour while the other fourteen moved, which reads
  // as a stray wrong pixel rather than as a variant.
  u8 iStart = ClampU8(start, 0, 15);
  u8 iEnd = ClampU8((u16)start + (u16)len, 0, 16);

  // Derive shifts/directions from the 16-bit PRN
  u32 rnd = (u32)prn16;
  u8 hueShift8 = ScaleToRange(BITS(rnd, 0, 7), 0, hmax, 7);
  u8 chrAmtPct = ScaleToRange(BITS(rnd, 7, 3), 0, cmax, 3);
  u8 lumAmtPct = ScaleToRange(BITS(rnd, 10, 3), 0, lmax, 3);
  u8 downH = (u8)BITS(rnd, 13, 1);
  u8 downC = (u8)BITS(rnd, 14, 1);
  u8 downL = (u8)BITS(rnd, 15, 1);

  // Compute signed deltas for C and L; amplify when down-only
  u8 cDelta = chrAmtPct;
  u8 lDelta = lumAmtPct;
  if (pv->sv_down_only)
  {
    cDelta = (u8)(2 * cDelta);
    lDelta = (u8)(2 * lDelta);
  }

  for (u8 i = iStart; i < iEnd; ++i)
  {
    u8 r5, g5, b5, h, c, l;
    Rgb555Unpack(pal16[i], &r5, &g5, &b5);
    Rgb5ToOklch(r5, g5, b5, &l, &c, &h);

    h = downH ? (u8)(h - hueShift8) : (u8)(h + hueShift8);

    if (pv->sv_down_only || downC)
      c = cDelta > c ? 0 : (u8)(c - cDelta);
    else
      c = (u8)(c + cDelta);

    if (pv->sv_down_only || downL)
      l = lDelta > l ? 0 : (u8)(l - lDelta);
    else
      l = (u8)((255 - lDelta < l) ? 255 : (l + lDelta));

    OklchToRgb5(l, c, h, &r5, &g5, &b5);
    pal16[i] = Rgb555Pack(r5, g5, b5);
  }
}

void ApplyCustomRestrictionToPaletteBuffer(u8 hMin, u8 hMax, u8 cMin, u8 cMax, u8 lMin, u8 lMax, u16 pal16[16])
{

  for (u8 i = 1; i <= 15; ++i)
  {
    u8 r5, g5, b5, h, c, l;
    Rgb555Unpack(pal16[i], &r5, &g5, &b5);
    Rgb5ToOklch(r5, g5, b5, &l, &c, &h);

    h = ScaleToRange(h, hMin, hMax, 8);
    c = ScaleToRange(c, cMin, cMax, 8);
    l = ScaleToRange(l, lMin, lMax, 8);

    OklchToRgb5(l, c, h, &r5, &g5, &b5);
    pal16[i] = Rgb555Pack(r5, g5, b5);
  }
}

void ApplyMonSpeciesVariantToPaletteBuffer(u32 species, bool8 shiny, u32 originalPID, u16 pal16[16])
{
  const struct SpeciesVariant *sv = GetSpeciesVariants(species);
  if (sv == NULL)
    return;

  // ---- Variant 1 (use low 16 bits as PRN) ----
  if (sv->pv1.hue_amount || sv->pv1.chr_amount || sv->pv1.lum_amount)
  {
    u16 prn1 = (u16)BITS(originalPID, 0, 16);
    ApplyPaletteVariantToPaletteBuffer(pal16, &sv->pv1, prn1);
  }

  // ---- Variant 2 (use high 16 bits as PRN) ----
  if (sv->pv2.hue_amount || sv->pv2.chr_amount || sv->pv2.lum_amount)
  {
    u16 prn2 = (u16)BITS(originalPID, 16, 16);
    ApplyPaletteVariantToPaletteBuffer(pal16, &sv->pv2, prn2);
  }
}

// -------- Entry points --------
//
// Upstream's README says to rewrite GetMonSpritePalFromSpecies and invent a
// personality-taking wrapper. Both already exist here, and the personality is
// already threaded to them, so the hook is a filter on the way out instead.

// Whether this mon gets shifted at all. Split out because two call sites ask
// the same question and one of them (the BW animation) is nowhere near the
// other.
static bool32 MonWantsVariant(u32 species, bool32 isShiny, bool32 isEgg)
{
#if !VARIANT_COLOURS
  return FALSE;
#else
  const struct SpeciesVariant *sv;

  // An egg's palette is the egg's, not the species'. Shifting it would leak
  // what is inside -- every egg would be a slightly different colour, and the
  // colour would be a function of the mon it hatches into.
  if (isEgg)
    return FALSE;

#if !VARIANT_COLOURS_SHINY
  if (isShiny)
    return FALSE;
#endif

  sv = GetSpeciesVariants(species);
  if (sv == NULL)
    return FALSE;

  // An entry that asks for no shift on either variant is not worth a copy.
  return (sv->pv1.hue_amount || sv->pv1.chr_amount || sv->pv1.lum_amount
       || sv->pv2.hue_amount || sv->pv2.chr_amount || sv->pv2.lum_amount);
#endif
}

#if VARIANT_COLOURS
// See VARIANT_PAL_RING in the header for why this is a ring and not one buffer.
static EWRAM_DATA u16 sVariantPal[VARIANT_PAL_RING][16] = {0};
static EWRAM_DATA u8 sVariantPalNext = 0;
#endif

const u16 *GetMonSpritePalVariant(const u16 *src, u32 species, bool32 isShiny, bool32 isEgg, u32 personality)
{
#if !VARIANT_COLOURS
  return src;
#else
  u16 *dst;

  if (src == NULL || !MonWantsVariant(species, isShiny, isEgg))
    return src;

  dst = sVariantPal[sVariantPalNext];
  sVariantPalNext = (sVariantPalNext + 1) % VARIANT_PAL_RING;

  CpuCopy16(src, dst, 16 * sizeof(u16));
  ApplyMonSpeciesVariantToPaletteBuffer(species, isShiny, personality, dst);
  return dst;
#endif
}

void ApplyMonSpritePalVariantTo(u16 dst[16], const u16 *src, u32 species, bool32 isShiny, u32 personality)
{
  CpuCopy16(src, dst, 16 * sizeof(u16));

  // isEgg is FALSE unconditionally: this path is a battle sprite, and an egg
  // is never a battler.
  if (!MonWantsVariant(species, isShiny, FALSE))
    return;

  ApplyMonSpeciesVariantToPaletteBuffer(species, isShiny, personality, dst);
}