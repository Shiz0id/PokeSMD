#ifndef GUARD_VARIANT_COLOURS_H
#define GUARD_VARIANT_COLOURS_H

#include "global.h"
#include "constants/species.h"

// Deterministic per-Pokemon colour variation, shifted in OkLCH rather than
// blended in RGB so a hue rotation keeps its contrast instead of washing
// toward grey. The shift is derived from the personality value, so a given
// mon is the same colour every time it loads, for as long as it exists.
//
// Colour maths: SpaceOtter99/pokeemerald-expansion@a9bf6cc (branch
// colour-variants), on Bjorn Ottosson's OKLab. See docs/COLOUR_VARIANTS.md.

// The whole feature. FALSE restores stock palettes everywhere with no other
// edit -- the hooks in pokemon.c and rogue_bw_anim.c compile to a plain
// return of the palette they were handed.
#define VARIANT_COLOURS TRUE

// Whether a shiny is also shifted. FALSE deliberately: a shiny is the one
// colour signal in a run that is supposed to mean something, and upstream
// shifts it -- it takes a `shiny` argument and never reads it. Shifting the
// trophy is how you stop being able to recognise the trophy.
#define VARIANT_COLOURS_SHINY FALSE

// How many mons may hold a variant palette pointer at once. The getter hands
// back a pointer into this ring rather than into ROM, and one caller --
// pokemon_storage_system's displayMonPalette -- holds that pointer across
// frames. Only one display mon exists at a time (box icons use a different
// palette path entirely), so a ring of 4 is slack rather than a requirement.
#define VARIANT_PAL_RING 4

struct PaletteVariant
{
  u8 start : 4;        // Start index of palette customisation range
  u8 length : 4;       // Length of customisation range
  u8 hue_amount : 3;   // Index into hue table [0,10,20,30,45,60,90,180]
  u8 chr_amount : 2;   // Index into chroma table [0,5,10,25]
  u8 lum_amount : 2;   // Index into luma table [0,5,10,25]
  u8 sv_down_only : 1; // If set, switch from +/- to "down only" for both C & L (C: -2*chr, L: -2*lum)
};

struct SpeciesVariant
{
  struct PaletteVariant pv1;
  struct PaletteVariant pv2;
};

// return variant data or return default if species has no variants.
const struct SpeciesVariant *GetSpeciesVariants(u32 species);

void ApplyPaletteVariantToPaletteBuffer(u16 pal16[16], const struct PaletteVariant *pv, u16 prn16);
void ApplyCustomRestrictionToPaletteBuffer(u8 hMin, u8 hMax, u8 cMin, u8 cMax, u8 lMin, u8 lMax, u16 pal16[16]);
void ApplyMonSpeciesVariantToPaletteBuffer(u32 species, bool8 shiny, u32 PID, u16 pal16[16]);

// The two entry points this project actually calls.
//
// GetMonSpritePalVariant takes the palette the engine was about to use and
// returns either it unchanged, or a shifted copy in the ring. Returning the
// input unchanged is the common case: eggs, shinies, and any species whose
// entry asks for no shift at all.
const u16 *GetMonSpritePalVariant(const u16 *src, u32 species, bool32 isShiny, bool32 isEgg, u32 personality);

// Same decision, but writing into a caller-supplied buffer instead of the
// ring. This exists for the BW animated sprites, which carry their own
// palette in ROM alongside their pixels and overwrite whatever the engine
// loaded -- so the variant has to be applied to THAT palette or it is
// invisible in battle for all 772 animated species.
void ApplyMonSpritePalVariantTo(u16 dst[16], const u16 *src, u32 species, bool32 isShiny, u32 personality);

// Species data helpers

#define HUE_INDEX(h) (            \
    ((h) == 0 ? 0 : (h) <= 10 ? 1 \
                : (h) <= 20   ? 2 \
                : (h) <= 30   ? 3 \
                : (h) <= 45   ? 4 \
                : (h) <= 60   ? 5 \
                : (h) <= 90   ? 6 \
                : /*(h)==180*/ 7))

#define CHR_INDEX(s) (           \
    ((s) == 0 ? 0 : (s) <= 5 ? 1 \
                : (s) <= 10  ? 2 \
                : /*(s)==25*/ 3))

#define LUM_INDEX(v) ( \
    ((v) == 0 ? 0      \
    : (v) <= 5 ? 1     \
    : (v) <= 10  ? 2   \
    : /*(v)==25*/ 3))

#define PAL1(s, l)  \
  .pv1.start = (s), \
  .pv1.length = (l)

#define PAL2(s, l)  \
  .pv2.start = (s), \
  .pv2.length = (l)

#define HCL1(h, s, v, f)          \
  .pv1.hue_amount = HUE_INDEX(h), \
  .pv1.chr_amount = CHR_INDEX(s), \
  .pv1.lum_amount = LUM_INDEX(v), \
  .pv1.sv_down_only = (f)

#define HCL2(h, s, v, f)          \
  .pv2.hue_amount = HUE_INDEX(h), \
  .pv2.chr_amount = CHR_INDEX(s), \
  .pv2.lum_amount = LUM_INDEX(v), \
  .pv2.sv_down_only = (f)

#endif // GUARD_VARIANT_COLOURS_H
