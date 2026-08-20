// Mystery Dungeon speaker portraits, keyed on species.
//
// ADDING SPECIES IS ONE COMMAND AND TWO LINES EACH. Drop Sprite Repository
// sheets named portrait-<natdex>.png in a directory and run
//
//   python3 tools/rogue/import_portraits.py --batch DIR --repo .
//
// It resolves each dex number against enum NationalDexOrder in the repo, cuts
// the grids into indexed PNGs under graphics/portraits/<species>/, writes
// src/data/portraits/<species>.h, and prints the includes and rows to paste
// below. The build's catch-all gbagfx rules turn the PNGs into .4bpp and
// .gbapal, so nothing binary is committed and the art stays editable.
//
// Pillow is required, and it is NOT installed in the WSL toolchain - run the
// importer from the Windows side with --repo pointing at the checkout.
//
// ONLY THE FIRST 20 SLOTS OF A SHEET ARE FACES. The next 20 are the same faces
// flipped, for a speaker drawn on the right; this project's portrait is always
// top-left, so the importer drops them. They are hand-adjusted rather than exact
// mirrors, so nothing but the slot index can identify them.
//
// A SPECIES WITH NO ENTRY FALLS BACK TO ITS BATTLE FRONT PIC, which is why this
// table can stay small. The fallback is a whole-body 64x64 sprite rather than a
// face, so it reads as a stand-in and not as finished art - that is deliberate.
//
// The 5, 5 is the art size in TILES: PMD portraits are 40x40. The frame is built
// around whatever that says, so a sheet at a different size needs no code edit.

#include "portraits/bulbasaur.h"
#include "portraits/charmander.h"
#include "portraits/squirtle.h"
#include "portraits/pikachu.h"
#include "portraits/clefairy.h"
#include "portraits/eevee.h"
#include "portraits/chikorita.h"
#include "portraits/cyndaquil.h"
#include "portraits/totodile.h"
#include "portraits/treecko.h"
#include "portraits/torchic.h"
#include "portraits/mudkip.h"
#include "portraits/turtwig.h"
#include "portraits/chimchar.h"
#include "portraits/piplup.h"

const struct RoguePortraitEntry gRoguePortraits[NUM_SPECIES] =
{
    [SPECIES_BULBASAUR]  = { 5, 5, sPortraitFaces_Bulbasaur,  ARRAY_COUNT(sPortraitFaces_Bulbasaur) },
    [SPECIES_CHARMANDER] = { 5, 5, sPortraitFaces_Charmander, ARRAY_COUNT(sPortraitFaces_Charmander) },
    [SPECIES_SQUIRTLE]   = { 5, 5, sPortraitFaces_Squirtle,   ARRAY_COUNT(sPortraitFaces_Squirtle) },
    [SPECIES_PIKACHU]    = { 5, 5, sPortraitFaces_Pikachu,    ARRAY_COUNT(sPortraitFaces_Pikachu) },
    [SPECIES_CLEFAIRY]   = { 5, 5, sPortraitFaces_Clefairy,   ARRAY_COUNT(sPortraitFaces_Clefairy) },
    [SPECIES_EEVEE]      = { 5, 5, sPortraitFaces_Eevee,      ARRAY_COUNT(sPortraitFaces_Eevee) },
    [SPECIES_CHIKORITA]  = { 5, 5, sPortraitFaces_Chikorita,  ARRAY_COUNT(sPortraitFaces_Chikorita) },
    [SPECIES_CYNDAQUIL]  = { 5, 5, sPortraitFaces_Cyndaquil,  ARRAY_COUNT(sPortraitFaces_Cyndaquil) },
    [SPECIES_TOTODILE]   = { 5, 5, sPortraitFaces_Totodile,   ARRAY_COUNT(sPortraitFaces_Totodile) },
    [SPECIES_TREECKO]    = { 5, 5, sPortraitFaces_Treecko,    ARRAY_COUNT(sPortraitFaces_Treecko) },
    [SPECIES_TORCHIC]    = { 5, 5, sPortraitFaces_Torchic,    ARRAY_COUNT(sPortraitFaces_Torchic) },
    [SPECIES_MUDKIP]     = { 5, 5, sPortraitFaces_Mudkip,     ARRAY_COUNT(sPortraitFaces_Mudkip) },
    [SPECIES_TURTWIG]    = { 5, 5, sPortraitFaces_Turtwig,    ARRAY_COUNT(sPortraitFaces_Turtwig) },
    [SPECIES_CHIMCHAR]   = { 5, 5, sPortraitFaces_Chimchar,   ARRAY_COUNT(sPortraitFaces_Chimchar) },
    [SPECIES_PIPLUP]     = { 5, 5, sPortraitFaces_Piplup,     ARRAY_COUNT(sPortraitFaces_Piplup) },
};
