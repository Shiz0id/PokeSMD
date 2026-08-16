#include "global.h"
#include "coins.h"
#include "money.h"                  // SetMoney, so a wipe reaches it too
#include "script_pokemon_util.h"     // CreateScriptedWildMon, for the ambush event
#include "event_data.h"
#include "fieldmap.h"
#include "random.h"
#include "script.h"
#include "string_util.h"
#include "pokemon.h"
#include "move.h"                   // GetMoveType/GetMoveCategory, for the starter fallback
#include "wild_encounter.h"
#include "battle_setup.h"
#include "event_object_movement.h"
#include "field_camera.h"
#include "overworld.h"
#include "field_screen_effect.h"   // DoWarp, for the script-side floor warp
#include "item.h"
#include "data.h"
#include "trainer_see.h"
#include "region_map.h"           // gRegionMapEntries, for the death summary's dungeon name
#include "constants/field_effects.h"
#include "constants/items.h"
#include "constants/layouts.h"
#include "constants/moves.h"
#include "constants/songs.h"
#include "constants/vars.h"
#include "constants/wild_encounter.h"
#include "berry.h"
#include "event_scripts.h"   // BerryTreeScript, the engine's own tree handler
#include "constants/berry.h"
#include "constants/event_bg.h"
#include "constants/event_objects.h"
#include "constants/trainer_types.h"
#include "constants/battle_setup.h"
#include "constants/event_object_movement.h"
#include "constants/rogue_dungeon_trainers.h"
#include "constants/rogue_dungeon_starters.h"
#include "constants/rogue_evolution_levels.h"
#include "constants/rogue_safari_pool.h"
#include "rogue_dungeon.h"
#include "rogue_charms.h"
#include "battle.h"

extern const u8 RogueDungeonFloor_EventScript_Stairs[];
extern const u8 RogueDungeonFloor_EventScript_Trainer[];
extern const u8 RogueDungeonFloor_EventScript_TrainerDone[];
extern const u8 RogueDungeonFloor_EventScript_ItemBall[];
extern const u8 RogueDungeonFloor_EventScript_MiningRock[];
extern const u8 RogueDungeonFloor_EventScript_BerryTree[];
// The imposter among them. Not a floor event - it is a berry tree wearing a
// different script. See DUNGEON_SUDOWOODO_ODDS.
extern const u8 RogueDungeonFloor_EventScript_Sudowoodo[];
extern const u8 RogueDungeonFloor_EventScript_BossDone[];
// Floor events, one per entry in sFloorEvents.
extern const u8 RogueDungeonFloor_EventScript_EventSpring[];
extern const u8 RogueDungeonFloor_EventScript_EventGambler[];
extern const u8 RogueDungeonFloor_EventScript_EventPedlar[];
extern const u8 RogueDungeonFloor_EventScript_EventTrader[];
extern const u8 RogueDungeonFloor_EventScript_EventEgg[];
extern const u8 RogueDungeonFloor_EventScript_EventInjured[];
extern const u8 RogueDungeonFloor_EventScript_EventScout[];
extern const u8 RogueDungeonFloor_EventScript_EventShrine[];
extern const u8 RogueDungeonFloor_EventScript_EventHerbalist[];
extern const u8 RogueDungeonFloor_EventScript_EventFossil[];
extern const u8 RogueDungeonFloor_EventScript_EventTutor[];
extern const u8 RogueDungeonFloor_EventScript_EventCrystal[];
extern const u8 RogueDungeonFloor_EventScript_EventDittoBall[];
extern const u8 RogueDungeonFloor_EventScript_EventTotem[];
extern const u8 RogueDungeonFloor_EventScript_EventOrb[];
extern const u8 RogueDungeonFloor_EventScript_EventShuppet[];
extern const u8 RogueDungeonFloor_EventScript_EventClefairy[];
extern const u8 RogueDungeonFloor_EventScript_EventTransposer[];
extern const u8 RogueDungeonFloor_EventScript_EventPokerus[];
extern const u8 RogueDungeonFloor_EventScript_EventNest[];
extern const u8 RogueDungeonFloor_Text_TrainerIntro[];
extern const u8 RogueDungeonFloor_Text_TrainerDefeat[];
extern const u8 RogueDungeonFloor_Text_BossIntro[];

static u16 PickTrainerForLevel(u8 target, const struct RogueDungeonTheme *theme,
                               u16 *gfxOut);

// How long each dungeon is. The Elite Four's five are half length, which is what
// makes them come every five floors with no mini boss between - see the run
// shape in rogue_dungeon.h. A floor past the last one clamps to the final
// dungeon rather than wrapping: the run ends at DUNGEON_TOTAL_FLOORS, so a floor
// beyond it means something upstream failed to reset the run, and pretending we
// are back at Roxanne would hide that.
static u32 DungeonLengthOf(u32 dungeon)
{
    if (dungeon < DUNGEON_GYM_DUNGEONS)
        return DUNGEON_LONG_FLOORS;
    if (dungeon < DUNGEON_GYM_DUNGEONS + DUNGEON_E4_DUNGEONS)
        return DUNGEON_SHORT_FLOORS;
    return DUNGEON_LONG_FLOORS;
}

static u32 DungeonIndexOf(u16 floor)
{
    if (floor < DUNGEON_GYM_FLOORS)
        return floor / DUNGEON_LONG_FLOORS;
    if (floor < DUNGEON_E4_END_FLOOR)
        return DUNGEON_GYM_DUNGEONS
             + (floor - DUNGEON_GYM_FLOORS) / DUNGEON_SHORT_FLOORS;
    return DUNGEON_COUNT - 1;
}

static u32 DungeonFloorWithin(u16 floor)
{
    if (floor < DUNGEON_GYM_FLOORS)
        return floor % DUNGEON_LONG_FLOORS;
    if (floor < DUNGEON_E4_END_FLOOR)
        return (floor - DUNGEON_GYM_FLOORS) % DUNGEON_SHORT_FLOORS;
    return floor - DUNGEON_E4_END_FLOOR;
}

// The twenty-four permutations of the four shuffled Elite Four slots, row 0
// identity. A table rather than a shuffle because it is four elements: a
// Fisher-Yates here would need a random stream, and the only one live at the
// moment a theme is looked up belongs to floor generation. 96 bytes of ROM buys
// a pure function of the order word.
static const u8 sE4Orders[][DUNGEON_E4_SHUFFLED] =
{
    {0,1,2,3}, {0,1,3,2}, {0,2,1,3}, {0,2,3,1}, {0,3,1,2}, {0,3,2,1},
    {1,0,2,3}, {1,0,3,2}, {1,2,0,3}, {1,2,3,0}, {1,3,0,2}, {1,3,2,0},
    {2,0,1,3}, {2,0,3,1}, {2,1,0,3}, {2,1,3,0}, {2,3,0,1}, {2,3,1,0},
    {3,0,1,2}, {3,0,2,1}, {3,1,0,2}, {3,1,2,0}, {3,2,0,1}, {3,2,1,0},
};

// SLOT IN, IDENTITY OUT. A slot is a position in the run - which decides the
// floors it spans, whether it has a mini boss, and how far down the curve it
// sits. An identity is which dungeon is standing there - its theme, its boss,
// that boss's sprite and TM. Before the shuffle the two were the same number and
// every caller used one variable for both; they are now different questions and
// the call sites have to pick deliberately.
//
// Things that stay on the SLOT: all the floor arithmetic above, the finale test
// in PrepareArenaFloor, the rod unlocks in ApplyRunConfig, and the dungeon number
// the debug readout prints. Things that take the IDENTITY: ThemeForFloor,
// sDungeonBosses, sDungeonBossGfx and sDungeonBossTMs.
//
// The gym half is one bit per band because the band is two: swapping within a
// pair is the low bit of the slot. The assert is what stops a wider band reading
// as a working shuffle - DUNGEON_SHUFFLE_BAND at 3 or 4 would still compile here
// and would still permute, just not within the bands it claims to.
STATIC_ASSERT(DUNGEON_SHUFFLE_BAND == 2, GymBandIsAPairOrThisCodeIsWrong);
STATIC_ASSERT(DUNGEON_GYM_DUNGEONS % DUNGEON_SHUFFLE_BAND == 0, GymBandsDivideEvenly);
STATIC_ASSERT(DUNGEON_GYM_BANDS <= 8, GymBandBitsFitTheOrderWord);

static u32 DungeonForSlot(u32 slot)
{
    u32 order = VarGet(VAR_ROGUE_RUN_ORDER);

    // Vanilla order: the gate is closed, the toggle is off, or the roll came up
    // identity. All three are the same run, so they are the same branch.
    if (order == 0)
        return slot;

    if (slot < DUNGEON_GYM_DUNGEONS)
        return ((order >> (slot / DUNGEON_SHUFFLE_BAND)) & 1) ? (slot ^ 1) : slot;

    // Wallace is the fifth Elite Four slot and does not move, so the range test
    // is against DUNGEON_E4_SHUFFLED rather than DUNGEON_E4_DUNGEONS. The finale
    // falls through the same way.
    if (slot < DUNGEON_GYM_DUNGEONS + DUNGEON_E4_SHUFFLED)
    {
        u32 pick = (order >> DUNGEON_GYM_BANDS) % ARRAY_COUNT(sE4Orders);

        return DUNGEON_GYM_DUNGEONS + sE4Orders[pick][slot - DUNGEON_GYM_DUNGEONS];
    }

    return slot;
}

// The boss stands on the last floor of its dungeon, whichever length that is.
static bool8 IsDungeonBossFloor(u16 floor)
{
    return DungeonFloorWithin(floor)
        == DungeonLengthOf(DungeonIndexOf(floor)) - 1;
}

// Only a full-length dungeon has a mini boss. In a five-floor Elite Four
// dungeon the fifth floor is the boss's own, so the length test is what stops
// Sidney sharing his arena with a grunt.
static bool8 IsMiniBossFloor(u16 floor)
{
    return DungeonLengthOf(DungeonIndexOf(floor)) == DUNGEON_LONG_FLOORS
        && DungeonFloorWithin(floor) == DUNGEON_MINIBOSS_FLOOR;
}

// Target level for a floor, in three stages because the stock bosses climb in
// three stages. The reasoning, and why the last one is so steep, is in
// rogue_dungeon.h next to the constants.
static u32 FloorTargetLevel(u16 floor)
{
    u32 level = DUNGEON_ENCOUNTER_BASE_LEVEL;

    if (floor <= DUNGEON_GYM_FLOORS)
        return level + ((u32)floor * DUNGEON_ENCOUNTER_LEVEL_NUM)
                     / DUNGEON_ENCOUNTER_LEVEL_DEN;

    level += ((u32)DUNGEON_GYM_FLOORS * DUNGEON_ENCOUNTER_LEVEL_NUM)
           / DUNGEON_ENCOUNTER_LEVEL_DEN;

    if (floor < DUNGEON_E4_END_FLOOR)
        return level + ((u32)(floor - DUNGEON_GYM_FLOORS)
                        * DUNGEON_ENCOUNTER_E4_NUM)
                     / DUNGEON_ENCOUNTER_LEVEL_DEN;

    level += ((u32)DUNGEON_E4_FLOORS * DUNGEON_ENCOUNTER_E4_NUM)
           / DUNGEON_ENCOUNTER_LEVEL_DEN;
    level += ((u32)(floor - DUNGEON_E4_END_FLOOR) * DUNGEON_ENCOUNTER_FINAL_NUM)
           / DUNGEON_ENCOUNTER_LEVEL_DEN;
    return level;
}

// Per-theme species pools, ordered weakest to strongest. A floor draws from a
// window of its theme pool that slides with depth, so early floors stay tame,
// later ones roll evolved forms, and the weakest species retire instead of
// lingering forever. The static table in wild_encounters.json is a placeholder
// that this replaces at runtime.
static const u16 sCaveSpecies[] =
{
    SPECIES_ZUBAT, SPECIES_WHISMUR, SPECIES_GEODUDE, SPECIES_MAKUHITA,
    SPECIES_ARON, SPECIES_NOSEPASS, SPECIES_DUGTRIO, SPECIES_SABLEYE,
    SPECIES_MAWILE, SPECIES_GOLBAT, SPECIES_LOUDRED, SPECIES_GRAVELER,
    // SUDOWOODO, CLAYDOL and URSARING were here and none of them lives in a
    // cave. Sudowoodo mimics a TREE and stands on routes; Ursaring is a forest
    // bear; and Claydol was also in sMirageTowerSpecies, where desert ruins are
    // its actual home, so the cave was the duplicate. Replaced with Granite
    // Cave's own vocabulary: Medicham for Meditite's cave line, Solrock from
    // Meteor Falls, and Aggron to finish the Aron at 4 and Lairon at 13.
    //
    // AGGRON RATHER THAN STEELIX, and the difference is the evolution method
    // rather than the flavour. Onix to Steelix is a TRADE and carries no level,
    // so DevolveForLevel has nothing to key on and a Steelix would stand on
    // floor 19 at level 16 exactly as Graveler used to. Aron to Lairon is 32
    // and Lairon to Aggron is 42, so the same slot self-corrects with depth.
    SPECIES_MEDICHAM, SPECIES_LAIRON, SPECIES_ONIX, SPECIES_HARIYAMA,
    SPECIES_GOLEM, SPECIES_SHUCKLE, SPECIES_SOLROCK, SPECIES_CROBAT,
    SPECIES_AGGRON,
};

static const u16 sWoodsSpecies[] =
{
    SPECIES_WURMPLE, SPECIES_ZIGZAGOON, SPECIES_LEDIAN, SPECIES_POOCHYENA,
    SPECIES_SEEDOT, SPECIES_LOTAD, SPECIES_TAILLOW, SPECIES_SILCOON,
    SPECIES_CASCOON, SPECIES_NINJASK, SPECIES_SHROOMISH, SPECIES_NINCADA,
    SPECIES_BEAUTIFLY, SPECIES_DUSTOX, SPECIES_NUZLEAF, SPECIES_LOMBRE,
    // AMBIPOM moved to the jungle, where Aipom actually lives. Scyther takes
    // the top slot: same 500 BST as the Heracross beside it, and a forest
    // ambusher is what this pool wanted there.
    SPECIES_LINOONE, SPECIES_SWELLOW, SPECIES_BRELOOM, SPECIES_HERACROSS,
    SPECIES_SCYTHER,
};

// The woods' whole cosmetic vocabulary, and the first thing this theme has ever
// had beyond grass, tall grass and trees - the generator used to return from
// WriteFloorBlocks before the decor pass could see it.
//
// Only the plain floor is a base. Tall grass is deliberately left alone: it is
// the encounter surface, so a variant scattered through it would make the
// player read where battles fire off a texture that has nothing to do with it,
// and this tileset has no MB_TALL_GRASS variant to use anyway.
static const struct RogueDecor sWoodsDecor[] =
{
    { WOODS_METATILE_GRASS, WOODS_METATILE_FLOWER_BUSH },
};

// New Mauville runs on generators, so the pool is electric with the steel and
// magnet types the facility already houses.
static const u16 sNewMauvilleSpecies[] =
{
    // CHINCHOU and LANTURN were here and are DEEP SEA ANGLERFISH. Electric
    // typing is not enough: this theme's wildArea is LAND, so they were walking
    // around a generator hall, and both were already in sUnderwaterSpecies
    // where they belong. Replaced with the Shinx line, which gives the pool a
    // family ladder into the Luxray it already ends on.
    SPECIES_MAGNEMITE, SPECIES_VOLTORB, SPECIES_PIKACHU, SPECIES_ELECTRIKE,
    SPECIES_PLUSLE, SPECIES_MINUN, SPECIES_SHINX, SPECIES_PACHIRISU,
    // PORYGON rather than PORYGON2: the upgrade is a TRADE and carries no
    // level, so a Porygon2 sat at index 11 arrived on floor 20 at level 17 with
    // nothing able to walk it back. Porygon is the base form and the pool
    // already climbs past it.
    SPECIES_MAREEP, SPECIES_MAGNETON, SPECIES_ELECTRODE, SPECIES_PORYGON,
    SPECIES_FLAAFFY, SPECIES_LUXIO, SPECIES_MANECTRIC, SPECIES_ROTOM,
    SPECIES_RAICHU, SPECIES_ELECTABUZZ, SPECIES_AMPHAROS,
    SPECIES_ELECTIVIRE, SPECIES_LUXRAY,
};

// Drop-in swaps for the New Mauville wall band. Vanilla interleaves these along
// a run, which is why the wall mining came back undecisive: the runners up were
// never noise, they were this set.
// Which floor tile each wall metatile bleeds into. Every entry is measured
// from NewMauville_Inside, split by neighbour (see rogue_dungeon.h).
static const struct RogueSkirt sNewMauvilleSkirts[] =
{
    { NEWMAUVILLE_METATILE_WALL_BAND,    NEWMAUVILLE_METATILE_SKIRT_BAND_S,   0 },
    { NEWMAUVILLE_METATILE_WALL_FACE_L,  NEWMAUVILLE_METATILE_SKIRT_FACE_L_S, 0 },
    { NEWMAUVILLE_METATILE_WALL_FACE_R,  NEWMAUVILLE_METATILE_SKIRT_BAND_S,
                                         NEWMAUVILLE_METATILE_SKIRT_E },
    { NEWMAUVILLE_METATILE_WALL_EAST,    0, NEWMAUVILLE_METATILE_SKIRT_E },
    { NEWMAUVILLE_METATILE_WALL_NORTH_R, 0, NEWMAUVILLE_METATILE_SKIRT_E },
    // Under a one-wide column vanilla puts 0x27F, 3/3 - the same tile it puts
    // under face-left, not the plain band shadow.
    { NEWMAUVILLE_METATILE_WALL_PILLAR_BASE,
                                         NEWMAUVILLE_METATILE_SKIRT_FACE_L_S, 0 },
};

static const struct RogueSkirt sFieryPathSkirts[] =
{
    { FIERYPATH_METATILE_WALL_NORTH_MID, FIERYPATH_METATILE_RIDGE_SKIRT_S, 0 },
};

// Three rows each, ported from tools/rogue/newmauville/nm_v4.py after that
// prototype was validated on screen. The single-row table this replaced painted
// every one of these headless and footless.
//
// The supercomputer is NOT here even though its lower two rows look like a
// decoration with a cap: it is a 2x3 solid set piece, and registering the
// bottom two thirds drew a headless machine flush into the wall.
// Densities are censused, not chosen. Over both vanilla layouts: 182 visible
// wall faces carry 42 decoration units, and the mix is near-uniform - crate
// unit 8, bookcase 7, vent 7, counter 7, console 6, box shelf 4, crate shelf 3.
// So one chance shared by the six wall pieces is right, and the vent needs its
// own: vanilla puts one on 7 of its 53 one-thick partitions, 13%, where a
// shared gate makes it 100% because nothing else fits there.
#define NM_STAMP_CHANCE 27
#define NM_VENT_CHANCE  13
static const struct RogueWallStamp sNewMauvilleStamps[] =
{
    { 2, FALSE, NM_STAMP_CHANCE,
      { NEWMAUVILLE_METATILE_WALL_BOOKCASE_CL, NEWMAUVILLE_METATILE_WALL_BOOKCASE_CR },
      { NEWMAUVILLE_METATILE_WALL_BOOKCASE_L,  NEWMAUVILLE_METATILE_WALL_BOOKCASE_R },
      { NEWMAUVILLE_METATILE_WALL_BOOKCASE_FL, NEWMAUVILLE_METATILE_WALL_BOOKCASE_FR } },
    // No cap of its own; the plain cap row above it is correct.
    { 2, FALSE, NM_STAMP_CHANCE,
      { 0, 0 },
      { NEWMAUVILLE_METATILE_WALL_CONSOLE_L,  NEWMAUVILLE_METATILE_WALL_CONSOLE_R },
      { NEWMAUVILLE_METATILE_WALL_CONSOLE_FL, NEWMAUVILLE_METATILE_WALL_CONSOLE_FR } },
    // Its cap covers the left half only, which is how vanilla draws it.
    { 2, FALSE, NM_STAMP_CHANCE,
      { NEWMAUVILLE_METATILE_WALL_SHELF_CAP, 0 },
      { NEWMAUVILLE_METATILE_WALL_SHELF_L,  NEWMAUVILLE_METATILE_WALL_SHELF_R },
      { NEWMAUVILLE_METATILE_WALL_SHELF_FL, NEWMAUVILLE_METATILE_WALL_SHELF_FR } },
    { 1, FALSE, NM_STAMP_CHANCE, { 0 },
      { NEWMAUVILLE_METATILE_WALL_BOX },     { NEWMAUVILLE_METATILE_WALL_BOX_F } },
    { 1, FALSE, NM_STAMP_CHANCE, { 0 },
      { NEWMAUVILLE_METATILE_WALL_COUNTER }, { NEWMAUVILLE_METATILE_WALL_COUNTER_F } },
    { 1, FALSE, NM_STAMP_CHANCE, { 0 },
      { NEWMAUVILLE_METATILE_WALL_CRATE },   { NEWMAUVILLE_METATILE_WALL_CRATE_F } },
    // The vent, and the only entry that wants floor above rather than a cap.
    { 1, TRUE,  NM_VENT_CHANCE,  { 0 },
      { NEWMAUVILLE_METATILE_WALL_VENT },    { 0 } },
};

// Two objects in this tileset are not decorations at all: their top row sits in
// the wall and their body projects out over the floor, solid in every cell.
// Registering the supercomputer's bottom two thirds as a wall decoration with a
// cap - which is what the first pass at this did - drew a headless machine
// flush into the wall, its top row never placed and its body never projecting.
static const u16 sNewMauvilleGenerator[] =
{
    0x2D0, 0x2D1, 0x2D2, 0x2D3,
    0x2D8, 0x2D9, 0x2DA, 0x2DB,
    0x2E0, 0x2E1, 0x2E2, 0x2E3,
    0x2E8, 0x2E9, 0x2EA, 0x2EB,
};

static const u16 sNewMauvilleSupercomputer[] =
{
    0x2D4, 0x2D5,
    0x2DC, 0x2DD,
    0x2E4, 0x2E5,
};

static const struct RogueSetPiece sNewMauvillePieces[] =
{
    { 4, 4, 1, TRUE, sNewMauvilleGenerator },
    { 2, 3, 2, TRUE, sNewMauvilleSupercomputer },
};

// Free-standing crate stacks, shapes taken from vanilla's own clusters of
// 0x2C0..0x2C3. Every other object we place is welded to a wall; vanilla
// scatters these in open floor.
static const u16 sNewMauvilleCrate1[] = { 0x2C0 };
static const u16 sNewMauvilleCrate2[] = { 0x2C0, 0x2C0 };
static const u16 sNewMauvilleCrate3[] = { 0x2C1, 0x2C0 };
static const u16 sNewMauvilleCrate4[] = { 0x2C1, 0x2C0,
                                          0x2C0, 0x2C0 };
static const u16 sNewMauvilleCrate5[] = { 0x2C3,
                                          0x2C2 };

static const struct RogueSetPiece sNewMauvilleClutter[] =
{
    { 1, 1, 0, FALSE, sNewMauvilleCrate1 },
    { 2, 1, 0, FALSE, sNewMauvilleCrate2 },
    { 2, 1, 0, FALSE, sNewMauvilleCrate3 },
    { 2, 2, 0, FALSE, sNewMauvilleCrate4 },
    { 1, 2, 0, FALSE, sNewMauvilleCrate5 },
};

// 0x270 and 0x272 are the interior columns; the corner slots share their art,
// so a corner joins the run it continues and is terminated with it.
static const struct RogueColumnEnd sNewMauvilleColumnEnds[] =
{
    { NEWMAUVILLE_METATILE_WALL_EAST, NEWMAUVILLE_METATILE_COLUMN_HEAD_E,
                                      NEWMAUVILLE_METATILE_COLUMN_FOOT_E },
    { NEWMAUVILLE_METATILE_WALL_WEST, NEWMAUVILLE_METATILE_COLUMN_HEAD_W,
                                      NEWMAUVILLE_METATILE_COLUMN_FOOT_W },
};

// Glacia's snow, take two. The first version was hers on a cave recolour and
// was deleted rather than repointed when she moved to Lapis, because the ids
// meant crystal wall under the new tileset. These are the real thing: Mt.
// Freeze's own Ground Alt 1 and Alt 2, imported beside the ground they vary and
// quantised into the same palette, so they are the snow rather than something
// resting on it.
//
// FLOOR decor rather than wall decor, which the pass has always supported - it
// keys on the painted metatile, and a floor base is as valid a base as a wall
// one. Both are 1 wide, so unlike the old drift there is no pairing constraint
// and they can land anywhere the floor does.
static const struct RogueDecor sGlaciaDecor[] =
{
    { LAPIS_METATILE_FLOOR, LAPIS_METATILE_DECOR_CLUMP },
    { LAPIS_METATILE_FLOOR, LAPIS_METATILE_DECOR_DRIFT },
};

// Fiery Path's own residents plus their evolutions, with a couple of fire
// types that fit the tunnel. Ordered weakest to strongest like the others.
static const u16 sFieryPathSpecies[] =
{
    SPECIES_SLUGMA, SPECIES_KOFFING, SPECIES_NUMEL, SPECIES_GRIMER,
    SPECIES_MACHOP, SPECIES_GROWLITHE, SPECIES_VULPIX, SPECIES_HOUNDOUR,
    SPECIES_TORKOAL, SPECIES_MAGCARGO, SPECIES_WEEZING, SPECIES_CAMERUPT,
    // FLAREON out: Eevee is a STARTER PICK here, so meeting its evolutions in
    // the wild undercuts the one choice the run opens on, and a lone
    // eeveelution in a volcanic pool never read as anything but a stray.
    // Rapidash is the fire horse the slot wanted, at the same 500 BST.
    SPECIES_MACHOKE, SPECIES_ARCANINE, SPECIES_MAGMAR, SPECIES_MUK,
    SPECIES_RAPIDASH, SPECIES_HOUNDOOM, SPECIES_NINETALES, SPECIES_MAGMORTAR,
    SPECIES_MACHAMP,
};

// The Route 111 desert's own residents, plus the two fossils that Mirage Tower
// is famous for handing out - Lileep and Anorith come from this tower in the
// stock game, so meeting them here is the point. Ordered weakest to strongest.
// Deliberately shares nothing with the cave pool despite the shared tileset.
static const u16 sMirageTowerSpecies[] =
{
    SPECIES_SANDSHREW, SPECIES_TRAPINCH, SPECIES_BALTOY, SPECIES_CACNEA,
    SPECIES_LILEEP, SPECIES_ANORITH, SPECIES_PHANPY, SPECIES_KABUTOPS,
    SPECIES_RHYHORN, SPECIES_OMASTAR, SPECIES_SANDSLASH, SPECIES_VIBRAVA,
    SPECIES_CLAYDOL, SPECIES_CACTURNE, SPECIES_DONPHAN, SPECIES_HIPPOWDON,
    SPECIES_CRADILY, SPECIES_ARMALDO, SPECIES_FLYGON, SPECIES_RHYPERIOR,
    SPECIES_GLISCOR,
};

// Rocks embedded in the sand mass, keyed on the wall interior the same way
// Fiery Path keys on its own. No floor decor: the tileset's only floor variety
// is the sand drift, which is a 3x3 region autotile and cannot be expressed as
// a single- or double-wide swap.
static const struct RogueDecor sMirageTowerDecor[] =
{
    { MIRAGETOWER_METATILE_WALL_INTERIOR, MIRAGETOWER_METATILE_ROCKS_A },
    { MIRAGETOWER_METATILE_WALL_INTERIOR, MIRAGETOWER_METATILE_ROCKS_B },
    { MIRAGETOWER_METATILE_WALL_INTERIOR, MIRAGETOWER_METATILE_BOULDER },
};

static const struct RogueDecor sFieryPathDecor[] =
{
    { FIERYPATH_METATILE_FLOOR, FIERYPATH_METATILE_FLOOR_SPARKLE_A },
    { FIERYPATH_METATILE_FLOOR, FIERYPATH_METATILE_FLOOR_SPARKLE_B },
    { FIERYPATH_METATILE_WALL_INTERIOR, FIERYPATH_METATILE_WALL_ROCKS_A },
    { FIERYPATH_METATILE_WALL_INTERIOR, FIERYPATH_METATILE_WALL_ROCKS_B },
    { FIERYPATH_METATILE_WALL_INTERIOR, FIERYPATH_METATILE_WALL_BOULDER },
};

// Pale sand pooling on the cave floor. Vanilla draws exactly this in Shoal
// Cave, the Desert Underpass and Altering Cave; Granite Cave itself has no
// floor variety at all, which is why our cave floors read so flat.
static const struct RoguePatchLayer sCaveSandPatch[] =
{
    {
        .tile =
        {
            [PATCH_NW]      = DUNGEON_METATILE_SAND_NW,
            [PATCH_N]       = DUNGEON_METATILE_SAND_N,
            [PATCH_NE]      = DUNGEON_METATILE_SAND_NE,
            [PATCH_W]       = DUNGEON_METATILE_SAND_W,
            [PATCH_MID]     = DUNGEON_METATILE_SAND_MID,
            [PATCH_E]       = DUNGEON_METATILE_SAND_E,
            [PATCH_SW]      = DUNGEON_METATILE_SAND_SW,
            [PATCH_S]       = DUNGEON_METATILE_SAND_S,
            [PATCH_SE]      = DUNGEON_METATILE_SAND_SE,
            [PATCH_NW_WALL] = DUNGEON_METATILE_SAND_NW_WALL,
            [PATCH_N_WALL]  = DUNGEON_METATILE_SAND_N_WALL,
            [PATCH_NE_WALL] = DUNGEON_METATILE_SAND_NE_WALL,
        },
        .blobs = 8, .radius = 4,
    },
};

// The same twelve ids in sandstone. Denser and larger than the cave's, because
// in vanilla Mirage Tower the drift is the dominant floor treatment rather than
// an occasional pool.
static const struct RoguePatchLayer sMirageTowerDriftPatch[] =
{
    {
        .tile =
        {
            [PATCH_NW]      = MIRAGETOWER_METATILE_DRIFT_NW,
            [PATCH_N]       = MIRAGETOWER_METATILE_DRIFT_N,
            [PATCH_NE]      = MIRAGETOWER_METATILE_DRIFT_NE,
            [PATCH_W]       = MIRAGETOWER_METATILE_DRIFT_W,
            [PATCH_MID]     = MIRAGETOWER_METATILE_DRIFT_MID,
            [PATCH_E]       = MIRAGETOWER_METATILE_DRIFT_E,
            [PATCH_SW]      = MIRAGETOWER_METATILE_DRIFT_SW,
            [PATCH_S]       = MIRAGETOWER_METATILE_DRIFT_S,
            [PATCH_SE]      = MIRAGETOWER_METATILE_DRIFT_SE,
            [PATCH_NW_WALL] = MIRAGETOWER_METATILE_DRIFT_NW_WALL,
            [PATCH_N_WALL]  = MIRAGETOWER_METATILE_DRIFT_N_WALL,
            [PATCH_NE_WALL] = MIRAGETOWER_METATILE_DRIFT_NE_WALL,
        },
        .blobs = 11, .radius = 5,
    },
};

// Route 119 and 120 residents, which is where the rain and the long grass are.
// Nothing here overlaps the woods pool - the two are both green, so the wild
// list is a large part of what tells them apart.
static const u16 sJungleSpecies[] =
{
    SPECIES_ODDISH, SPECIES_SURSKIT, SPECIES_MARILL, SPECIES_VOLBEAT,
    SPECIES_ILLUMISE, SPECIES_ROSELIA, SPECIES_CARNIVINE, SPECIES_SLAKOTH,
    SPECIES_KECLEON, SPECIES_GLOOM, SPECIES_MASQUERAIN, SPECIES_LUDICOLO,
    SPECIES_AZUMARILL, SPECIES_VIGOROTH, SPECIES_VESPIQUEN, SPECIES_TROPIUS,
    // ABSOL out, AMBIPOM in. Absol is a MOUNTAIN Pokemon and was already in
    // sSidneySpecies, where a Dark-type gauntlet is its real home; Aipom is
    // tropical and was sitting in the woods. A straight swap rather than an
    // addition, because a gym pool has exactly window + dungeonLength - 1 = 21
    // reachable entries and a twenty-second could never be dealt.
    SPECIES_ZANGOOSE, SPECIES_SEVIPER, SPECIES_AMBIPOM, SPECIES_YANMEGA,
    SPECIES_TOXICROAK,
};

// The open sea. Everything here is what actually swims on Routes 124 to 126,
// which is the stretch the player is crossing. Encounters fire on every block
// rather than only in a grass layer, because MB_OCEAN_WATER carries the flag
// itself - the same deal the cave gets.
static const u16 sOceanSpecies[] =
{
    SPECIES_TENTACOOL, SPECIES_MAGIKARP, SPECIES_WINGULL, SPECIES_CARVANHA,
    SPECIES_POLIWHIRL, SPECIES_GOLDEEN, SPECIES_HORSEA, SPECIES_SPHEAL,
    SPECIES_WAILMER, SPECIES_TENTACRUEL, SPECIES_PELIPPER, SPECIES_FLOATZEL,
    SPECIES_SEAKING, SPECIES_SEADRA, SPECIES_LUMINEON, SPECIES_SEALEO,
    SPECIES_SHARPEDO, SPECIES_WAILORD, SPECIES_KINGDRA, SPECIES_MILOTIC,
    SPECIES_GYARADOS,
};

// Two layers, painted in order. Long grass goes down first and covers whole
// clearings; puddles are punched through it afterwards, so water sits in the
// grass rather than being hidden under it.
//
// Long grass has only one piece of edge art in the whole game - the south
// fringe - so every other slot is the grass itself. Putting the fringe on the
// region's own bottom row rather than the row below it costs one row of
// encounters and saves a slot for "outside the region to the south".
static const struct RoguePatchLayer sJunglePatches[] =
{
    {
        .tile =
        {
            [PATCH_NW]      = JUNGLE_METATILE_LONG_GRASS,
            [PATCH_N]       = JUNGLE_METATILE_LONG_GRASS,
            [PATCH_NE]      = JUNGLE_METATILE_LONG_GRASS,
            [PATCH_W]       = JUNGLE_METATILE_LONG_GRASS,
            [PATCH_MID]     = JUNGLE_METATILE_LONG_GRASS,
            [PATCH_E]       = JUNGLE_METATILE_LONG_GRASS,
            // The fringe is back, and it is the theme's own - vanilla's pixels
            // recoloured so the band under the blades is dirt rather than the
            // route grass it was drawn against. See the header.
            [PATCH_SW]      = JUNGLE_METATILE_LONG_GRASS_S,
            [PATCH_S]       = JUNGLE_METATILE_LONG_GRASS_S,
            [PATCH_SE]      = JUNGLE_METATILE_LONG_GRASS_S,
            [PATCH_NW_WALL] = JUNGLE_METATILE_LONG_GRASS,
            [PATCH_N_WALL]  = JUNGLE_METATILE_LONG_GRASS,
            [PATCH_NE_WALL] = JUNGLE_METATILE_LONG_GRASS,
        },
        // Few and large, so a clearing tends to be swallowed whole rather than
        // speckled. This is the theme's only encounter surface.
        .blobs = 6, .radius = 8,
    },
    // The water, and it is the SHEET's rather than gTileset_General's.
    //
    // The vanilla puddles that used to sit here were pulled, and the reason is
    // worth keeping: 0x0C8-0x0DA is a 3x3 region autotile in the PRIMARY
    // tileset, so it survived the tileset swap and still resolved perfectly -
    // but its EIGHT edge pieces are a shore drawn against green route grass,
    // and only its centre is purely water. On dirt every puddle came out ringed
    // in a pale mint halo. An autotile's EDGES encode what the art expects to
    // sit in, and swapping what it sits in invalidates all of them while
    // leaving the ids valid; the mock is what showed it, not the build.
    //
    // These edges were drawn against this sheet's own ground, so they fit.
    //
    // MB_PUDDLE, so the player reflects in it and splashes through it, and so
    // it stays off the water encounter branch entirely. See the header.
    {
        .tile =
        {
            [PATCH_NW]      = JUNGLE_METATILE_WATER_NW,
            [PATCH_N]       = JUNGLE_METATILE_WATER_N,
            [PATCH_NE]      = JUNGLE_METATILE_WATER_NE,
            [PATCH_W]       = JUNGLE_METATILE_WATER_W,
            [PATCH_MID]     = JUNGLE_METATILE_WATER_MID,
            [PATCH_E]       = JUNGLE_METATILE_WATER_E,
            [PATCH_SW]      = JUNGLE_METATILE_WATER_SW,
            [PATCH_S]       = JUNGLE_METATILE_WATER_S,
            [PATCH_SE]      = JUNGLE_METATILE_WATER_SE,
            // No wall-adjacent variant exists, so the ordinary top edge serves.
            [PATCH_NW_WALL] = JUNGLE_METATILE_WATER_NW,
            [PATCH_N_WALL]  = JUNGLE_METATILE_WATER_N,
            [PATCH_NE_WALL] = JUNGLE_METATILE_WATER_NE,
        },
        // Many and small: pools, not lakes.
        .blobs = 10, .radius = 3,
    },
};

// Was two entries scattering vanilla canopy variants at rarity 2, a near 50/50
// mix. The imported sheet has more to work with: five wall variants, each one
// varying a SPECIFIC wall case rather than the mass as a whole, plus two
// variants of the dirt floor.
//
// The pairing is not a judgement call - import_tile_sheet.py reads it out of
// the sheet, because an Alt cell sits at the same legend position as the case
// it varies. Getting it wrong by hand would put a north-edge variant on an
// interior block, which reads as a hole in the foliage.
static const struct RogueDecor sJungleDecor[] =
{
    { JUNGLE_METATILE_WALL_NORTH_M,    JUNGLE_METATILE_WALL_ALT_NORTH_M },
    { JUNGLE_METATILE_WALL_INTERIOR_L, JUNGLE_METATILE_WALL_ALT_INT_L },
    { JUNGLE_METATILE_WALL_INTERIOR_M, JUNGLE_METATILE_WALL_ALT_INT_M },
    { JUNGLE_METATILE_WALL_INTERIOR_R, JUNGLE_METATILE_WALL_ALT_INT_R },
    { JUNGLE_METATILE_WALL_FACE_M,     JUNGLE_METATILE_WALL_ALT_FACE_M },
    { JUNGLE_METATILE_FLOOR,           JUNGLE_METATILE_FLOOR_ALT_1 },
    { JUNGLE_METATILE_FLOOR,           JUNGLE_METATILE_FLOOR_ALT_2 },
};

// The seafloor. Vanilla puts only three species underwater - Clamperl,
// Relicanth and Chinchou - which is far too thin for ten floors, so this is
// built out to the sixteen every other theme has, from what the deep sea and
// Sootopolis hold rather than from what Routes 124-134 happen to list.
//
// Deliberately DISJOINT from sOceanSpecies. Underwater is the dungeon directly
// after the ocean, and six shared species would have made the two read as one
// long water stretch. It also foreshadows Juan for free: Luvdisc, Whiscash and
// Crawdaunt are his, and Milotic closes the list because Sootopolis is where
// the stock game puts it.
// The seafloor's own opponents, so the figure on the floor and the pic in the
// battle are the same person. Sorted by level like the stock table, because the
// picker widens a window around the floor's target and expects that order.
//
// Levels 40-46 straddle the dungeon on purpose: Underwater is floors 71-80,
// whose FloorTargetLevel runs 41-45, and the first window is +/-3. Reaching past
// both ends is what stops the deepest floors falling through to a wider window.
//
// Their parties are in src/data/trainers.party. They were rewritten once
// already, and the reason is worth keeping: the first set was chosen while Gen
// 5 and 6 were compiled in, and cutting those generations left ELEVEN of the
// fourteen slots naming species that are not in the ROM - Frillish, Clauncher,
// Tirtouga, Skrelp, Binacle, Alomomola, Jellicent x2, Carracosta, Clawitzer,
// Barbaracle. Only Lanturn, Mantine and Relicanth survived.
//
// Nothing said so. A disabled family still compiles to a zeroed species_info
// row, so the build was clean and the divers just led with blanks. The rule
// this earns: a species named anywhere - a pool, a party, a gift - has to be
// checked against pokeemerald.map, not against species_info, because
// species_info still has the row.
//
// The replacements are Gen 1-4 and graded to the old power ramp (per-diver
// party BST 315, 350, 408, 475, 490, 490, 505 against the old 332, 337, 383,
// 475, 490, 492, 490). The shape is deliberate and matches what was there: the
// shallow divers carry juveniles and the deep ones carry the evolutions -
// Shellder to Cloyster, Omanyte to Omastar, Kabuto to Kabutops, Shellos to
// Gastrodon, Clamperl to Gorebyss.
//
// Typing was read out of src/data/pokemon/species_info rather than recalled,
// and that check earned its place on the first pass: DRAGALGE is Poison/DRAGON
// - Skrelp loses the Water type when it evolves, exactly the Seadra/Kingdra
// trap. SPECIES_BASCULIN does not exist under that name either.
//
// SHELLOS and GASTRODON are form species: the bare name is an enum entry equal
// to the WEST form, not a valueless alias, so trainers.party may name them
// plainly. Castform already ships that way and is the precedent.
static const struct RogueThemeTrainer sUnderwaterTrainers[] =
{
    { TRAINER_ROGUE_DIVER_1, OBJ_EVENT_GFX_ROGUE_DIVER_M, 40 },
    { TRAINER_ROGUE_DIVER_2, OBJ_EVENT_GFX_ROGUE_DIVER_F, 41 },
    { TRAINER_ROGUE_DIVER_3, OBJ_EVENT_GFX_ROGUE_DIVER_M, 42 },
    { TRAINER_ROGUE_DIVER_4, OBJ_EVENT_GFX_ROGUE_DIVER_F, 43 },
    { TRAINER_ROGUE_DIVER_5, OBJ_EVENT_GFX_ROGUE_DIVER_M, 44 },
    { TRAINER_ROGUE_DIVER_6, OBJ_EVENT_GFX_ROGUE_DIVER_F, 45 },
    { TRAINER_ROGUE_DIVER_7, OBJ_EVENT_GFX_ROGUE_DIVER_M, 46 },
};

static const u16 sUnderwaterSpecies[] =
{
    SPECIES_CHINCHOU, SPECIES_CLAMPERL, SPECIES_CORPHISH, SPECIES_BARBOACH,
    SPECIES_LUVDISC, SPECIES_QUAGSIRE, SPECIES_CORSOLA, SPECIES_REMORAID,
    SPECIES_STARYU, SPECIES_RELICANTH, SPECIES_KINGLER, SPECIES_WHISCASH,
    SPECIES_LANTURN, SPECIES_OCTILLERY, SPECIES_GOLDUCK, SPECIES_CRAWDAUNT,
    SPECIES_STARMIE, SPECIES_HUNTAIL, SPECIES_GOREBYSS, SPECIES_SLOWKING,
    SPECIES_POLIWRATH,
};

// The Elite Four's pools, each the type its member specialises in.
//
// ELEVEN entries, and the number is arithmetic rather than taste. The ramp is
// per dungeon now, so an Elite Four dungeon of DUNGEON_SHORT_FLOORS runs tiers
// STARTING_TIER..STARTING_TIER + SHORT - 1, which is 7..11 - so eleven is
// exactly what is reachable. Fewer leaves the window of eight unfilled and a
// floor repeats species across its slots; more leaves a tail nothing can roll.
// check_species_pools.py fails on either, so this number follows the run
// structure rather than having to be remembered.
//
// It used to be EIGHT, for a reason that was correct then and is not now: the
// ramp keyed off the ABSOLUTE floor, these dungeons start at floor 81, so
// `tiers` was pinned at the pool length and only the top eight could ever show.
// Steven's is sixteen rather than eleven because his dungeon is a full
// DUNGEON_LONG_FLOORS.
//
// Still sorted weakest to strongest, because the window is a window: the
// ordering is what the tier machinery reads, and it stays correct if these
// pools are ever pointed at a shallower dungeon.
//
// STILL OFF THE PSEUDO LEGENDARIES, which is the part of the original note that
// survives unchanged. Tyranitar, Dragonite, Salamence, Garchomp and Metagross
// are all type-appropriate and all sit at 600 BST; inside a window of eight they
// would be a fifth of the encounters on their floor rather than a rare, and the
// member's own ace would stop meaning anything - Metagross IS Steven's ace.
// Their pre-evolutions are absent for the same reason in reverse: there is no
// level 53 Bagon. Haxorus is in because 540 is not that tier, and Zweilous
// because it is a MIDDLE stage, so Hydreigon stays out.
//
// Gen 4 and 5 are drawn on now, and that is what makes eleven reachable without
// reaching outside the type. Gen 1-3 alone could not fill some of these: Phoebe
// was stuck at seven because Gen 1-3 has ten Ghosts and three are too weak for
// level 49, and Drake at six because Dragon was deliberately rare before Gen 4.

// Sidney, Dark. Mightyena through Houndoom is his own party plus the rest of
// Hoenn's Dark types; Sneasel and Houndoom come from Gen 2, which is where the
// type's depth is, and Murkrow, Honchkrow and Weavile from Gen 4 - Honchkrow and
// Weavile being the evolutions of two entries already here.
static const u16 sSidneySpecies[] =
{
    SPECIES_MURKROW, SPECIES_SNEASEL, SPECIES_MIGHTYENA,
    SPECIES_CACTURNE, SPECIES_CRAWDAUNT, SPECIES_SHIFTRY,
    SPECIES_SHARPEDO, SPECIES_ABSOL, SPECIES_HONCHKROW,
    SPECIES_HOUNDOOM, SPECIES_WEAVILE,
};

// Phoebe, Ghost. Gen 1-3 has ten Ghosts and three - Gastly, Shuppet, Duskull -
// are too weak to belong at level 49, which is why this was the shortest pool
// in the run at seven. Gen 4 and 5 close it without leaving the type: Drifblim,
// Mismagius, Dusknoir and Chandelure. Shedinja stays at the bottom as a free
// catch; it is one of the most distinctive things in Gen 3 and exactly what a
// Ghost floor is for.
static const u16 sPhoebeSpecies[] =
{
    SPECIES_SHEDINJA, SPECIES_MISDREAVUS, SPECIES_SABLEYE, SPECIES_HAUNTER,
    SPECIES_DRIFBLIM, SPECIES_BANETTE, SPECIES_MISMAGIUS, SPECIES_SPIRITOMB,
    SPECIES_DUSCLOPS, SPECIES_GENGAR, SPECIES_DUSKNOIR,
};

// Glacia, Ice. Sealeo, Glalie and Walrein are her own three. This is the pool
// that actually interacts with the floor: MAP_ROGUE_DUNGEON_SNOW puts
// B_WEATHER_SNOW up in every battle, so every one of these gets 1.5x Defense -
// which is worth remembering before adding an Ice type that is already bulky.
static const u16 sGlaciaSpecies[] =
{
    SPECIES_SEALEO, SPECIES_DEWGONG, SPECIES_JYNX, SPECIES_PILOSWINE,
    SPECIES_GLALIE, SPECIES_CLOYSTER, SPECIES_FROSLASS, SPECIES_ABOMASNOW,
    SPECIES_WALREIN, SPECIES_MAMOSWINE, SPECIES_LAPRAS,
};

// Drake, Dragon. This was the thinnest pool in the run at six, and that was the
// whole of what Gen 1-3 offers once the pseudo legendaries are out - Dragon was
// deliberately rare until Gen 4, which is exactly the gap Gen 4 and 5 fill.
//
// Everything in the neighbourhood that only LOOKS like it belongs is still out,
// and it was checked rather than assumed because all three read as dragons and
// none of them are: Seadra is pure Water and becomes Water/Dragon only as
// Kingdra, Swablu is Normal/Flying until it evolves, Trapinch is pure Ground.
static const u16 sDrakeSpecies[] =
{
    SPECIES_VIBRAVA, SPECIES_DRAGONAIR, SPECIES_GABITE, SPECIES_SHELGON,
    SPECIES_ALTARIA, SPECIES_FLYGON, SPECIES_KINGDRA,
};

// Seaweed, laid in blobs the way vanilla lays it. Every slot is the same id
// because the region autotile has nothing to autotile: seaweed is one uniform
// 2x2 metatile with no edge art at all - 0x201 and 0x281 are the same four
// tiles under different palettes - so a patch is just a filled area.
//
// 0x281 rather than 0x201 because it is MB_SEAWEED_NO_SURFACING. Both carry
// encounters; only this one also refuses to let the player surface, which on a
// floor with no paired surface map is the difference between a sealed dungeon
// and an unanswered dive warp.
static const struct RoguePatchLayer sUnderwaterPatch[] =
{
    {
        .tile =
        {
            [PATCH_NW] = UNDERWATER_METATILE_SEAWEED,
            [PATCH_N]  = UNDERWATER_METATILE_SEAWEED,
            [PATCH_NE] = UNDERWATER_METATILE_SEAWEED,
            [PATCH_W]  = UNDERWATER_METATILE_SEAWEED,
            [PATCH_MID]= UNDERWATER_METATILE_SEAWEED,
            [PATCH_E]  = UNDERWATER_METATILE_SEAWEED,
            [PATCH_SW] = UNDERWATER_METATILE_SEAWEED,
            [PATCH_S]  = UNDERWATER_METATILE_SEAWEED,
            [PATCH_SE] = UNDERWATER_METATILE_SEAWEED,
            [PATCH_NW_WALL] = UNDERWATER_METATILE_SEAWEED,
            [PATCH_N_WALL]  = UNDERWATER_METATILE_SEAWEED,
            [PATCH_NE_WALL] = UNDERWATER_METATILE_SEAWEED,
        },
        .blobs = 10,
        .radius = 5,
    },
};

// Wallace, the flower meadow. SIX, sorted weakest to strongest, and the first
// Elite Four pool NOT type-matched to its member: the dungeon is deliberately
// not water themed, so these are chosen for the field rather than for him.
//
// A pitcher plant, a sunflower, a moth that scatters powder over flowers,
// dandelion seed, and the two things Gloom becomes. Six divides the twelve
// encounter slots exactly, as Drake's does, so this floor spreads flat.
//
// Two rejections mattered more than they look. Beautifly and Dustox are the TOP
// of the WOODS pool, at floors 1 to 10 - putting them back as Wallace's weakest
// ninety floors later would undo the retirement the tier window exists for.
// Masquerain, Volbeat, Illumise and Tropius are all the JUNGLE's, the other
// lush-green land dungeon, and reusing them would blur two themes that already
// share a palette family. Butterfree is unused and is the swap-in if a second
// Bug is ever wanted; it costs Bellossom, since keeping both Gloom branches is
// what fills the sixth slot.
static const u16 sEverGrandeSpecies[] =
{
    SPECIES_SUNFLORA, SPECIES_BELLOSSOM, SPECIES_CHERRIM, SPECIES_JUMPLUFF,
    SPECIES_VILEPLUME, SPECIES_VICTREEBEL, SPECIES_VENOMOTH,
    SPECIES_CRADILY, SPECIES_TANGROWTH, SPECIES_EXEGGUTOR, SPECIES_ROSERADE,
};

// Steven, Steel, and the deepest floors in the run. EIGHT, like the Elite Four
// pools, and here that is not a window - dungeon 13 is floors 106-115, where
// `tiers` has long since clamped to speciesCount, so ALL EIGHT appear on every
// floor of it. Nothing retires, so every one of these has to belong at the end
// of a run rather than merely being on the ladder to it.
//
// That is most of why the list looks like this. Gen 1-3 has only nine Steel
// types once the legendaries and Metagross are set aside, so this is nearly all
// of them and there was no room to drop the two mid-stages: Metang and Lairon
// are here because the type has nothing else, the same squeeze Phoebe's Ghosts
// ran into. Mawile is the one cut, at 380 BST against the next weakest's 420.
//
// METAGROSS IS ABSENT and it is his ace. Same call as Drake's: 600 BST at
// one-in-eight would be a fifth of the encounters on his own floor rather than
// a rare, and beating the champion's signature on the way to him takes the
// meaning out of meeting it.
static const u16 sStevenSpecies[] =
{
    SPECIES_MAGNETON, SPECIES_LAIRON, SPECIES_METANG, SPECIES_FORRETRESS,
    SPECIES_PROBOPASS, SPECIES_BRONZONG, SPECIES_SKARMORY,
    SPECIES_BASTIODON, SPECIES_STEELIX, SPECIES_SCIZOR, SPECIES_MAGNEZONE,
    SPECIES_AGGRON, SPECIES_LUCARIO,
};

// Six wall variants and two floor ones, all paired out of the sheet's legend.
// The wall set is the largest of any theme - this sheet has both Alt columns
// filled, and neither costs a colour the walls do not already carry.
static const struct RogueDecor sMurkyDecor[] =
{
    { MURKY_METATILE_WALL_NORTH_M,    MURKY_METATILE_WALL_ALT_NORTH_M },
    { MURKY_METATILE_WALL_INTERIOR_L, MURKY_METATILE_WALL_ALT_INT_L },
    { MURKY_METATILE_WALL_INTERIOR_M, MURKY_METATILE_WALL_ALT_INT_M },
    { MURKY_METATILE_WALL_INTERIOR_R, MURKY_METATILE_WALL_ALT_INT_R },
    { MURKY_METATILE_WALL_FACE_M,     MURKY_METATILE_WALL_ALT_FACE_M },
    { MURKY_METATILE_WALL_INTERIOR_M, MURKY_METATILE_WALL_ALT_INT_M2 },
    { MURKY_METATILE_FLOOR,           MURKY_METATILE_FLOOR_ALT_1 },
    { MURKY_METATILE_FLOOR,           MURKY_METATILE_FLOOR_ALT_2 },
};

// Pools, the same shape the jungle lays. MB_PUDDLE, so they reflect.
static const struct RoguePatchLayer sMurkyPatches[] =
{
    {
        .tile =
        {
            [PATCH_NW]      = MURKY_METATILE_WATER_NW,
            [PATCH_N]       = MURKY_METATILE_WATER_N,
            [PATCH_NE]      = MURKY_METATILE_WATER_NE,
            [PATCH_W]       = MURKY_METATILE_WATER_W,
            [PATCH_MID]     = MURKY_METATILE_WATER_MID,
            [PATCH_E]       = MURKY_METATILE_WATER_E,
            [PATCH_SW]      = MURKY_METATILE_WATER_SW,
            [PATCH_S]       = MURKY_METATILE_WATER_S,
            [PATCH_SE]      = MURKY_METATILE_WATER_SE,
            [PATCH_NW_WALL] = MURKY_METATILE_WATER_NW,
            [PATCH_N_WALL]  = MURKY_METATILE_WATER_N,
            [PATCH_NE_WALL] = MURKY_METATILE_WATER_NE,
        },
        .blobs = 10, .radius = 3,
    },
};

// Three layers, painted in order so a later one wins.
//
// Layer 0 is the short flower beds, MB_UNUSED_05 - encounters and nothing else,
// no overlay and no clip, the way Ever Grande City treats them. PHASED: the
// eight ids from the base are eight steps of a diagonal banding rather than
// eight variants, so the beds come out in alternating diagonal rows exactly as
// vanilla lays them. Every slot is the same base because the set has no edge
// art at all - the region autotile degenerates to a fill, as seaweed does.
//
// Layer 1 is the flowery long grass, MB_LONG_GRASS, which is what the
// wade-through curtain belongs to. Fewer and smaller blobs than the beds: it is
// the deeper surface and it should read as stands of tall planting standing in
// a meadow, not as the meadow. It wins where it overlaps the beds, which is
// the right way round - grass grows up through a bed, not the reverse.
//
// Layer 2 is brick, and it is a path rather than a region: no north or south
// edge art exists, so a blob's top and bottom are hard cuts. Small and sparse
// on purpose, and last so it reads as laid ON the planting.
// Thirty-two wall and floor variants out of the sheet's two Wall Alt columns,
// which is the reason this import was worth taking - a wall mass gets a face per
// block instead of one picture repeated.
//
// SIXTEEN OF THESE CAN NEVER FIRE TODAY, and that is worth knowing before
// counting them as thirty-two. The sheet keys its cells on all eight neighbours
// and draws 47; ApplyWallAutotiling reads four and the theme names 20, so every
// pair whose BASE is one of the other 27 is unreachable. They are kept because
// they cost a few bytes, they are correct, and a 47-case autotile would light
// them all at once. The live ones are the pairs based on 0x203/0x204/0x205/
// 0x207/0x21D/0x21E/0x21F/0x220 and the two on the floor.
static const struct RogueDecor sMeadowDecor[] =
{
    { 0x203, 0x22F }, { 0x204, 0x230 }, { 0x205, 0x231 }, { 0x207, 0x232 },
    { 0x215, 0x233 }, { 0x217, 0x234 }, { 0x219, 0x235 }, { 0x21A, 0x236 },
    { 0x21D, 0x237 }, { 0x21E, 0x238 }, { 0x21F, 0x239 }, { 0x220, 0x23A },
    { 0x222, 0x23B }, { 0x224, 0x23C }, { 0x226, 0x23D }, { 0x228, 0x23E },
    { 0x229, 0x23F }, { 0x22C, 0x240 }, { 0x22E, 0x241 },
    { 0x204, 0x242 }, { 0x207, 0x243 }, { 0x219, 0x244 }, { 0x21B, 0x245 },
    { 0x21C, 0x246 }, { 0x21D, 0x247 }, { 0x21E, 0x248 }, { 0x21F, 0x249 },
    { 0x220, 0x24A }, { 0x227, 0x24B }, { 0x229, 0x24C },
    { MEADOW_METATILE_FLOOR, 0x27C }, { MEADOW_METATILE_FLOOR, 0x27D },
};

// Three layers, in order, so a later one wins where they overlap. Same shape as
// the tileset it replaces, with two differences that matter.
static const struct RoguePatchLayer sMeadowPatch[] =
{
    // The SHORT encounter surface. Eight phases of a diagonal banding, which is
    // what `phase` paints - vanilla satisfies variant = (y - x + k) mod 8 across
    // 85% of Ever Grande City's own flower blocks, so this is the real rule and
    // not a scatter.
    {
        .tile =
        {
            [PATCH_NW] = MEADOW_METATILE_BEDPINK,
            [PATCH_N]  = MEADOW_METATILE_BEDPINK,
            [PATCH_NE] = MEADOW_METATILE_BEDPINK,
            [PATCH_W]  = MEADOW_METATILE_BEDPINK,
            [PATCH_MID]= MEADOW_METATILE_BEDPINK,
            [PATCH_E]  = MEADOW_METATILE_BEDPINK,
            [PATCH_SW] = MEADOW_METATILE_BEDPINK,
            [PATCH_S]  = MEADOW_METATILE_BEDPINK,
            [PATCH_SE] = MEADOW_METATILE_BEDPINK,
            [PATCH_NW_WALL] = MEADOW_METATILE_BEDPINK,
            [PATCH_N_WALL]  = MEADOW_METATILE_BEDPINK,
            [PATCH_NE_WALL] = MEADOW_METATILE_BEDPINK,
        },
        .blobs = 11,
        .radius = 6,
        .phase = MEADOW_FLOWER_PHASE,
    },
    // The TALL one. DIFFERENT FROM WHAT IT REPLACES: the grafted long grass is
    // one metatile with a real south fringe rather than eight phases, so the S
    // row takes the fringe and there is no phase. The fringe carries
    // MB_LONG_GRASS_SOUTH_EDGE and therefore no encounters, which is vanilla's
    // own arrangement - the bottom row of a grass patch is where the blades are
    // cut off, not more grass.
    {
        .tile =
        {
            [PATCH_NW] = MEADOW_METATILE_GRASS,
            [PATCH_N]  = MEADOW_METATILE_GRASS,
            [PATCH_NE] = MEADOW_METATILE_GRASS,
            [PATCH_W]  = MEADOW_METATILE_GRASS,
            [PATCH_MID]= MEADOW_METATILE_GRASS,
            [PATCH_E]  = MEADOW_METATILE_GRASS,
            [PATCH_SW] = MEADOW_METATILE_FRINGE,
            [PATCH_S]  = MEADOW_METATILE_FRINGE,
            [PATCH_SE] = MEADOW_METATILE_FRINGE,
            [PATCH_NW_WALL] = MEADOW_METATILE_GRASS,
            [PATCH_N_WALL]  = MEADOW_METATILE_GRASS,
            [PATCH_NE_WALL] = MEADOW_METATILE_GRASS,
        },
        .blobs = 6,
        .radius = 4,
    },
    // THE COBBLE, and it is rare and small on purpose: three 2x2 squares a floor,
    // the only sign anything was ever built out here. A 2x2 of a nine-sliced
    // region is exactly its four CORNERS, so it draws as a small closed square
    // with no repeating middle - see RoguePatchLayer.square.
    //
    // The nine-slice is still worth having even though a 2x2 never uses five of
    // its slots: it is what closes gap 4, because the terrace this replaces had
    // no north or south edge art at all and could not be laid as a region of ANY
    // size without hard cuts.
    {
        .tile =
        {
            [PATCH_NW] = MEADOW_METATILE_PLAZA_NW,
            [PATCH_N]  = MEADOW_METATILE_PLAZA_N,
            [PATCH_NE] = MEADOW_METATILE_PLAZA_NE,
            [PATCH_W]  = MEADOW_METATILE_PLAZA_W,
            [PATCH_MID]= MEADOW_METATILE_PLAZA_MID,
            [PATCH_E]  = MEADOW_METATILE_PLAZA_E,
            [PATCH_SW] = MEADOW_METATILE_PLAZA_SW,
            [PATCH_S]  = MEADOW_METATILE_PLAZA_S,
            [PATCH_SE] = MEADOW_METATILE_PLAZA_SE,
            [PATCH_NW_WALL] = MEADOW_METATILE_PLAZA_NW,
            [PATCH_N_WALL]  = MEADOW_METATILE_PLAZA_N,
            [PATCH_NE_WALL] = MEADOW_METATILE_PLAZA_NE,
        },
        .blobs = 3,
        .square = 2,
    },
};

static const struct RoguePatchLayer sEverGrandePatch[] =
{
    {
        .tile =
        {
            [PATCH_NW] = EVERGRANDE_METATILE_FLOWERS_PINK,
            [PATCH_N]  = EVERGRANDE_METATILE_FLOWERS_PINK,
            [PATCH_NE] = EVERGRANDE_METATILE_FLOWERS_PINK,
            [PATCH_W]  = EVERGRANDE_METATILE_FLOWERS_PINK,
            [PATCH_MID]= EVERGRANDE_METATILE_FLOWERS_PINK,
            [PATCH_E]  = EVERGRANDE_METATILE_FLOWERS_PINK,
            [PATCH_SW] = EVERGRANDE_METATILE_FLOWERS_PINK,
            [PATCH_S]  = EVERGRANDE_METATILE_FLOWERS_PINK,
            [PATCH_SE] = EVERGRANDE_METATILE_FLOWERS_PINK,
            [PATCH_NW_WALL] = EVERGRANDE_METATILE_FLOWERS_PINK,
            [PATCH_N_WALL]  = EVERGRANDE_METATILE_FLOWERS_PINK,
            [PATCH_NE_WALL] = EVERGRANDE_METATILE_FLOWERS_PINK,
        },
        .blobs = 11,
        .radius = 6,
        .phase = EVERGRANDE_FLOWER_PHASE,
    },
    {
        .tile =
        {
            [PATCH_NW] = EVERGRANDE_METATILE_LONG_GRASS,
            [PATCH_N]  = EVERGRANDE_METATILE_LONG_GRASS,
            [PATCH_NE] = EVERGRANDE_METATILE_LONG_GRASS,
            [PATCH_W]  = EVERGRANDE_METATILE_LONG_GRASS,
            [PATCH_MID]= EVERGRANDE_METATILE_LONG_GRASS,
            [PATCH_E]  = EVERGRANDE_METATILE_LONG_GRASS,
            [PATCH_SW] = EVERGRANDE_METATILE_LONG_GRASS,
            [PATCH_S]  = EVERGRANDE_METATILE_LONG_GRASS,
            [PATCH_SE] = EVERGRANDE_METATILE_LONG_GRASS,
            [PATCH_NW_WALL] = EVERGRANDE_METATILE_LONG_GRASS,
            [PATCH_N_WALL]  = EVERGRANDE_METATILE_LONG_GRASS,
            [PATCH_NE_WALL] = EVERGRANDE_METATILE_LONG_GRASS,
        },
        .blobs = 6,
        .radius = 4,
        .phase = EVERGRANDE_LONG_GRASS_PHASE,
    },
    {
        .tile =
        {
            [PATCH_NW] = EVERGRANDE_METATILE_PATH_W,
            [PATCH_N]  = EVERGRANDE_METATILE_PATH_MID,
            [PATCH_NE] = EVERGRANDE_METATILE_PATH_E,
            [PATCH_W]  = EVERGRANDE_METATILE_PATH_W,
            [PATCH_MID]= EVERGRANDE_METATILE_PATH_MID,
            [PATCH_E]  = EVERGRANDE_METATILE_PATH_E,
            [PATCH_SW] = EVERGRANDE_METATILE_PATH_W,
            [PATCH_S]  = EVERGRANDE_METATILE_PATH_MID,
            [PATCH_SE] = EVERGRANDE_METATILE_PATH_E,
            [PATCH_NW_WALL] = EVERGRANDE_METATILE_PATH_W,
            [PATCH_N_WALL]  = EVERGRANDE_METATILE_PATH_MID,
            [PATCH_NE_WALL] = EVERGRANDE_METATILE_PATH_E,
        },
        .blobs = 5,
        .radius = 4,
    },
};

enum DungeonThemeId
{
    DUNGEON_THEME_WOODS,
    DUNGEON_THEME_CAVE,
    DUNGEON_THEME_NEWMAUVILLE,
    DUNGEON_THEME_FIERYPATH,
    DUNGEON_THEME_MIRAGETOWER,
    DUNGEON_THEME_JUNGLE,
    DUNGEON_THEME_OCEAN,
    DUNGEON_THEME_UNDERWATER,
    // Victory Road, one per Elite Four member. Their ORDER matters: ThemeForFloor
    // is dungeon % DUNGEON_THEME_COUNT, and the Elite Four are dungeons 8 to 12,
    // so sitting at indices 8, 9, 10 and 11 is what lands Sidney's theme on
    // Sidney's dungeon with no change to ThemeForFloor at all.
    DUNGEON_THEME_VICTORYROAD_SIDNEY,
    DUNGEON_THEME_VICTORYROAD_PHOEBE,
    DUNGEON_THEME_VICTORYROAD_GLACIA,
    DUNGEON_THEME_VICTORYROAD_DRAKE,
    // Wallace is dungeon 12, so index 12 is his by the same modulo that gives
    // the Elite Four theirs. This is what stops him wrapping back to the woods.
    DUNGEON_THEME_EVERGRANDE,
    // Steven is dungeon 13, the finale, so index 13 is his by the same modulo.
    // With this the enum is as long as the run is: fourteen themes against
    // fourteen dungeons, and NOTHING WRAPS any more. ThemeForFloor's modulo is
    // now only a bounds guard rather than a routing decision.
    DUNGEON_THEME_MURKYCAVE,
    DUNGEON_THEME_COUNT
};

static const struct RogueDungeonTheme sDungeonThemes[DUNGEON_THEME_COUNT] =
{
    [DUNGEON_THEME_WOODS] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_WOODS,
        // Leaves blowing across on a breeze. This is DUNGEON 1, so for most
        // runs it is the first weather - and the first anything - the player
        // sees, which is why the effect is tuned calm rather than showy.
        //
        // MAP_ROGUE_DUNGEON_LEAVES carries MUS_PETALBURG_WOODS, not the other
        // weather maps' MUS_ABNORMAL_WEATHER, because this theme used to be on
        // MAP_ROGUE_DUNGEON_FLOOR and that is the track it had. Moving a theme
        // onto a weather map moves its WHOLE header, music included, and taking
        // the woods' music away as a side effect of adding weather to it would
        // have been a silent regression - the same shape of mistake as a new
        // weather dropping out of battle_util.c's switch.
        .mapId = MAP_ROGUE_DUNGEON_LEAVES,
        // it IS Petalburg Woods, and the only theme this track was ever right for
        .music = MUS_PETALBURG_WOODS,
        .mapSecId = MAPSEC_ROGUE_WOODS,
        .berries = TRUE,   // open sky and soil
        .generator = DUNGEON_GEN_WOODS,
        .elevationFloor = DUNGEON_ELEVATION_FLOOR,
        .elevationWall = DUNGEON_ELEVATION_WALL,
        .floor = WOODS_METATILE_GRASS,
        .tallGrass = WOODS_METATILE_TALL_GRASS,
        // No long grass. Vanilla Petalburg Woods has none - it is the Route 119
        // kind - and gTileset_General has no MB_LONG_GRASS_SOUTH_EDGE metatile
        // at all, so under Rustboro there is nothing correct to end a patch
        // with. It belongs to the jungle, which has 0x208.
        .longGrass = 0,
        // NO CROWN ROW, deliberately, and this is a loss taken on purpose.
        // Vanilla pokes a tree's crown up into the cell above so a mass reads
        // as overlapping canopy; doing that for the autumn trees needs a
        // crown-over-grass AND a crown-over-tall-grass composite for every
        // variant - eight more metatiles of art - to buy a flourish. Left at 0,
        // which StampCell's `leftTop != 0` guard already treats as "skip".
        .aboveTreeFloorL = 0,
        .aboveTreeFloorR = 0,
        .aboveTreeGrassL = 0,
        .aboveTreeGrassR = 0,
        .stairsDown = WOODS_METATILE_STAIRS,
        .stairsUp = WOODS_METATILE_STAIRS,
        .stamp =
        {
            [STAMP_R0C0] = WOODS_METATILE_TREE_R0C0,
            [STAMP_R0C1] = WOODS_METATILE_TREE_R0C1,
            [STAMP_R0C2] = WOODS_METATILE_TREE_R0C2,
            [STAMP_R1C0] = WOODS_METATILE_TREE_R1C0,
            [STAMP_R1C1] = WOODS_METATILE_TREE_R1C1,
            [STAMP_R1C2] = WOODS_METATILE_TREE_R1C2,
            [STAMP_R2C0] = WOODS_METATILE_TREE_R2C0,
            [STAMP_R2C1] = WOODS_METATILE_TREE_R2C1,
            [STAMP_R2C2] = WOODS_METATILE_TREE_R2C2,
            // THE SAME METATILES AS ROW 2, not a separate ground-contact row.
            // These trees each draw their own trunk and shadow, so one standing
            // below another should still show it; vanilla swaps the row only
            // because its trees tile into a featureless mass.
            [STAMP_BASE_0] = WOODS_METATILE_TREE_R2C0,
            [STAMP_BASE_1] = WOODS_METATILE_TREE_R2C1,
            [STAMP_BASE_2] = WOODS_METATILE_TREE_R2C2,
        },
        .stampVariants = WOODS_METATILE_TREE_VARIANTS,

        // Sparser than any other theme's - one variant against the cave's two
        // or three, and a woods floor is far more open than a cave's, so the
        // same rarity would put the same bush everywhere and read as wallpaper
        // rather than as scatter.
        .decor = sWoodsDecor,
        .decorCount = ARRAY_COUNT(sWoodsDecor),
        .decorRarity = 28,

        .species = sWoodsSpecies,
        .speciesCount = ARRAY_COUNT(sWoodsSpecies),
        .encounterWindow = 12,

        // The woods paints tall grass, so the engine already gave this theme
        // GRASS on its encounter surface and CAVE everywhere else - two
        // backdrops on one floor depending on where the player was standing.
        // Naming one makes the floor consistent with itself.
        .battleEnvironment = BATTLE_ENVIRONMENT_DEEP_WOODS,
    },
    [DUNGEON_THEME_CAVE] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_FLOOR,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
        // vanilla uses it for Shoal Cave -- a plain deep cave
        .music = MUS_MT_PYRE,
        .mapSecId = MAPSEC_ROGUE_CAVE,
        .generator = DUNGEON_GEN_ORGANIC,
        .caveFill = 48,
        .elevationFloor = DUNGEON_ELEVATION_FLOOR,
        .elevationWall = DUNGEON_ELEVATION_WALL,
        .floor = DUNGEON_METATILE_FLOOR,
        .tallGrass = 0,   // caves have no grass; encounters fire anywhere
        .longGrass = 0,
        .stairsDown = DUNGEON_METATILE_STAIRS_DOWN,
        .stairsUp = DUNGEON_METATILE_STAIRS_UP,
        .wall =
        {
            [WALL_INTERIOR_LEFT]  = DUNGEON_METATILE_WALL_INTERIOR_LEFT,
            [WALL_INTERIOR_MID]   = DUNGEON_METATILE_WALL_INTERIOR_MID,
            [WALL_INTERIOR_RIGHT] = DUNGEON_METATILE_WALL_INTERIOR_RIGHT,
            [WALL_FACE_LEFT]      = DUNGEON_METATILE_WALL_FACE_LEFT,
            [WALL_FACE_MID]       = DUNGEON_METATILE_WALL_FACE_MID,
            [WALL_FACE_RIGHT]     = DUNGEON_METATILE_WALL_FACE_RIGHT,
            [WALL_NORTH_LEFT]     = DUNGEON_METATILE_WALL_NORTH_LEFT,
            [WALL_NORTH_MID]      = DUNGEON_METATILE_WALL_NORTH_MID,
            [WALL_NORTH_RIGHT]    = DUNGEON_METATILE_WALL_NORTH_RIGHT,
            [WALL_CORNER_OPEN_SE] = DUNGEON_METATILE_WALL_CORNER_NW,
            [WALL_CORNER_OPEN_SW] = DUNGEON_METATILE_WALL_CORNER_NE,
            [WALL_CORNER_OPEN_NW] = DUNGEON_METATILE_WALL_CORNER_SOUTH,
            [WALL_CORNER_OPEN_NE] = DUNGEON_METATILE_WALL_CORNER_SOUTH,
            [WALL_SLIVER_VERT]    = DUNGEON_METATILE_WALL_SLIVER_VERT,
            [WALL_SLIVER_HORZ]    = DUNGEON_METATILE_WALL_SLIVER_HORZ,
            [WALL_SLIVER_VERT_TOP]= DUNGEON_METATILE_WALL_SLIVER_VERT_TOP,
            [WALL_SLIVER_VERT_BOT]= DUNGEON_METATILE_WALL_SLIVER_VERT_BOT,
            [WALL_SLIVER_HORZ_L]  = DUNGEON_METATILE_WALL_SLIVER_HORZ_L,
            [WALL_SLIVER_HORZ_R]  = DUNGEON_METATILE_WALL_SLIVER_HORZ_R,
            [WALL_SLIVER_ISOLATED]= DUNGEON_METATILE_WALL_SLIVER_ISOLATED,
        },
        .patches = sCaveSandPatch,
        .patchCount = ARRAY_COUNT(sCaveSandPatch),

        .species = sCaveSpecies,
        .speciesCount = ARRAY_COUNT(sCaveSpecies),
        .encounterWindow = 12,

        // A straight upgrade rather than a change of idea: this theme already
        // resolved to vanilla's CAVE through the metatile, and this is the same
        // cave drawn better.
        .battleEnvironment = BATTLE_ENVIRONMENT_ROGUE_CAVE,
    },
    [DUNGEON_THEME_NEWMAUVILLE] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_NEWMAUVILLE,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
        // New Mauville is an electrical facility, not a cave; the hideout track is the industrial one
        .music = MUS_AQUA_MAGMA_HIDEOUT,
        .mapSecId = MAPSEC_ROGUE_NEWMAUVILLE,
        // The room-and-corridor carve, not the facility floorplan.
        //
        // The facility's partitions are two blocks thick vertically and one
        // horizontally, which leaves nothing for the furniture to hang off: a
        // wall-mounted piece needs a run of capped wall face with clear floor
        // under it, and measured over 60 facility floors the 4x4 generator had
        // ZERO candidate spots per floor and the supercomputer 1.10. The cave
        // carve builds thick irregular masses, which is what every one of these
        // pieces was censused against in vanilla.
        .generator = DUNGEON_GEN_CAVE,
        .elevationFloor = DUNGEON_ELEVATION_FLOOR,
        .elevationWall = DUNGEON_ELEVATION_WALL,
        .floor = NEWMAUVILLE_METATILE_FLOOR,
        .tallGrass = 0,   // a facility has no grass; encounters fire anywhere
        .longGrass = 0,
        .stairsDown = NEWMAUVILLE_METATILE_STAIRS,
        .stairsUp = NEWMAUVILLE_METATILE_STAIRS,
        .wall =
        {
            // The band serves both faces: a flat partition looks the same from
            // either side, which is how vanilla uses it.
            [WALL_FACE_MID]       = NEWMAUVILLE_METATILE_WALL_BAND,
            [WALL_NORTH_MID]      = NEWMAUVILLE_METATILE_WALL_BAND,
            [WALL_SLIVER_HORZ]    = NEWMAUVILLE_METATILE_WALL_BAND,
            [WALL_SLIVER_HORZ_L]  = NEWMAUVILLE_METATILE_WALL_BAND,
            [WALL_SLIVER_HORZ_R]  = NEWMAUVILLE_METATILE_WALL_BAND,

            [WALL_INTERIOR_LEFT]  = NEWMAUVILLE_METATILE_WALL_WEST,
            [WALL_INTERIOR_RIGHT] = NEWMAUVILLE_METATILE_WALL_EAST,
            [WALL_FACE_LEFT]      = NEWMAUVILLE_METATILE_WALL_FACE_L,
            [WALL_FACE_RIGHT]     = NEWMAUVILLE_METATILE_WALL_FACE_R,
            [WALL_NORTH_LEFT]     = NEWMAUVILLE_METATILE_WALL_NORTH_L,
            [WALL_NORTH_RIGHT]    = NEWMAUVILLE_METATILE_WALL_NORTH_R,

            // The column stands on a base two metatiles tall, so BOT is the
            // flat brown block on the floor and the shoulder goes above it.
            [WALL_SLIVER_VERT]    = NEWMAUVILLE_METATILE_WALL_PILLAR,
            [WALL_SLIVER_VERT_TOP]= NEWMAUVILLE_METATILE_WALL_PILLAR_TOP,
            [WALL_SLIVER_VERT_BOT]= NEWMAUVILLE_METATILE_WALL_PILLAR_BASE,
            [WALL_SLIVER_VERT_BOT_UPPER] = NEWMAUVILLE_METATILE_WALL_PILLAR_BOT,
            [WALL_SLIVER_ISOLATED]= NEWMAUVILLE_METATILE_WALL_PILLAR,

            // The row above every face. This is the only theme whose wall is
            // two metatiles tall, so it is the only one that sets these.
            [WALL_CAP_LEFT]       = NEWMAUVILLE_METATILE_WALL_CAP_L,
            [WALL_CAP_MID]        = NEWMAUVILLE_METATILE_WALL_CAP,
            [WALL_CAP_RIGHT]      = NEWMAUVILLE_METATILE_WALL_CAP_R,

            // Deep interior is genuinely void - censused at 79% over cells with
            // all eight neighbours wall. The CORNERS are not: vanilla continues
            // the column through them (47-73%), and void in those four slots is
            // what notched every room outline.
            [WALL_INTERIOR_MID]   = NEWMAUVILLE_METATILE_VOID,
            [WALL_CORNER_OPEN_SE] = NEWMAUVILLE_METATILE_WALL_EAST,
            [WALL_CORNER_OPEN_SW] = NEWMAUVILLE_METATILE_WALL_WEST,
            [WALL_CORNER_OPEN_NW] = NEWMAUVILLE_METATILE_CORNER_NW,
            [WALL_CORNER_OPEN_NE] = NEWMAUVILLE_METATILE_CORNER_NE,
        },
        .skirts = sNewMauvilleSkirts,
        .skirtCount = ARRAY_COUNT(sNewMauvilleSkirts),
        .shadowCorner = NEWMAUVILLE_METATILE_SKIRT_CORNER,
        .columnEnds = sNewMauvilleColumnEnds,
        .columnEndCount = ARRAY_COUNT(sNewMauvilleColumnEnds),

        // Density is per stamp and censused; see sNewMauvilleStamps.
        .stamps = sNewMauvilleStamps,
        .stampCount = ARRAY_COUNT(sNewMauvilleStamps),
        .pieces = sNewMauvillePieces,
        .pieceCount = ARRAY_COUNT(sNewMauvillePieces),
        .clutter = sNewMauvilleClutter,
        .clutterCount = ARRAY_COUNT(sNewMauvilleClutter),
        .clutterMax = 6,

        .species = sNewMauvilleSpecies,
        .speciesCount = ARRAY_COUNT(sNewMauvilleSpecies),
        .encounterWindow = 12,
    },
    [DUNGEON_THEME_FIERYPATH] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_FIERYPATH,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
        // the volcano track, for a lava cave
        .music = MUS_MT_CHIMNEY,
        .mapSecId = MAPSEC_ROGUE_FIERYPATH,
        .generator = DUNGEON_GEN_ORGANIC,
        .caveFill = 48,
        .elevationFloor = DUNGEON_ELEVATION_FLOOR,
        .elevationWall = DUNGEON_ELEVATION_WALL,
        .floor = FIERYPATH_METATILE_FLOOR,
        .tallGrass = 0,
        .longGrass = 0,
        .stairsDown = FIERYPATH_METATILE_STAIRS,
        .stairsUp = FIERYPATH_METATILE_STAIRS,
        .wall =
        {
            [WALL_INTERIOR_LEFT]  = FIERYPATH_METATILE_WALL_WEST,
            [WALL_INTERIOR_MID]   = FIERYPATH_METATILE_WALL_INTERIOR,
            [WALL_INTERIOR_RIGHT] = FIERYPATH_METATILE_WALL_EAST,
            [WALL_FACE_LEFT]      = FIERYPATH_METATILE_WALL_FACE_L,
            [WALL_FACE_MID]       = FIERYPATH_METATILE_WALL_FACE_MID,
            [WALL_FACE_RIGHT]     = FIERYPATH_METATILE_WALL_FACE_R,
            [WALL_NORTH_LEFT]     = FIERYPATH_METATILE_WALL_NORTH_L,
            [WALL_NORTH_MID]      = FIERYPATH_METATILE_WALL_NORTH_MID,
            [WALL_NORTH_RIGHT]    = FIERYPATH_METATILE_WALL_NORTH_R,
            [WALL_CORNER_OPEN_SE] = FIERYPATH_METATILE_WALL_CORNER_SE,
            [WALL_CORNER_OPEN_SW] = FIERYPATH_METATILE_WALL_CORNER_SW,
            [WALL_CORNER_OPEN_NW] = FIERYPATH_METATILE_WALL_CORNER_NW,
            [WALL_CORNER_OPEN_NE] = FIERYPATH_METATILE_WALL_CORNER_NW,
            [WALL_SLIVER_VERT]    = FIERYPATH_METATILE_SLIVER_VERT,
            [WALL_SLIVER_HORZ]    = FIERYPATH_METATILE_WALL_NORTH_MID,
            [WALL_SLIVER_VERT_TOP]= FIERYPATH_METATILE_SLIVER_VERT_TOP,
            [WALL_SLIVER_VERT_BOT]= FIERYPATH_METATILE_SLIVER_VERT_BOT,
            [WALL_SLIVER_HORZ_L]  = FIERYPATH_METATILE_SLIVER_HORZ_L,
            [WALL_SLIVER_HORZ_R]  = FIERYPATH_METATILE_SLIVER_HORZ_R,
            [WALL_SLIVER_ISOLATED]= FIERYPATH_METATILE_SLIVER_ISOLATED,
        },

        .skirts = sFieryPathSkirts,
        .skirtCount = ARRAY_COUNT(sFieryPathSkirts),
        .shadowCorner = 0,

        .decor = sFieryPathDecor,
        .decorCount = ARRAY_COUNT(sFieryPathDecor),
        .decorRarity = 12,

        .species = sFieryPathSpecies,
        .speciesCount = ARRAY_COUNT(sFieryPathSpecies),
        .encounterWindow = 12,

        // The first theme to name its own backdrop. Without it Fiery Path is a
        // lava cave that fights in front of a grey one, because the engine only
        // ever sees MAP_TYPE_UNDERGROUND and a non-encounter metatile.
        .battleEnvironment = BATTLE_ENVIRONMENT_SCALDING_CAVE,
    },
    [DUNGEON_THEME_MIRAGETOWER] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_MIRAGETOWER,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
        // Mirage Tower stands in the desert and this is the desert track
        .music = MUS_DESERT,
        .mapSecId = MAPSEC_ROGUE_MIRAGETOWER,
        .generator = DUNGEON_GEN_CAVE,
        .elevationFloor = DUNGEON_ELEVATION_FLOOR,
        .elevationWall = DUNGEON_ELEVATION_WALL,
        .floor = MIRAGETOWER_METATILE_FLOOR,
        .tallGrass = 0,   // sandstone tower; encounters fire anywhere
        .longGrass = 0,
        .stairsDown = MIRAGETOWER_METATILE_STAIRS,
        .stairsUp = MIRAGETOWER_METATILE_STAIRS,
        .wall =
        {
            [WALL_INTERIOR_LEFT]  = MIRAGETOWER_METATILE_WALL_WEST,
            [WALL_INTERIOR_MID]   = MIRAGETOWER_METATILE_WALL_INTERIOR,
            [WALL_INTERIOR_RIGHT] = MIRAGETOWER_METATILE_WALL_EAST,
            [WALL_FACE_LEFT]      = MIRAGETOWER_METATILE_WALL_FACE_L,
            [WALL_FACE_MID]       = MIRAGETOWER_METATILE_WALL_FACE_MID,
            [WALL_FACE_RIGHT]     = MIRAGETOWER_METATILE_WALL_FACE_R,
            [WALL_NORTH_LEFT]     = MIRAGETOWER_METATILE_WALL_NORTH_L,
            [WALL_NORTH_MID]      = MIRAGETOWER_METATILE_WALL_NORTH_MID,
            [WALL_NORTH_RIGHT]    = MIRAGETOWER_METATILE_WALL_NORTH_R,
            [WALL_CORNER_OPEN_SE] = MIRAGETOWER_METATILE_WALL_CORNER_NW,
            [WALL_CORNER_OPEN_SW] = MIRAGETOWER_METATILE_WALL_CORNER_NE,
            [WALL_CORNER_OPEN_NW] = MIRAGETOWER_METATILE_WALL_CORNER_S,
            [WALL_CORNER_OPEN_NE] = MIRAGETOWER_METATILE_WALL_CORNER_S,
            [WALL_SLIVER_VERT]    = MIRAGETOWER_METATILE_SLIVER_VERT,
            [WALL_SLIVER_HORZ]    = MIRAGETOWER_METATILE_SLIVER_HORZ,
            [WALL_SLIVER_VERT_TOP]= MIRAGETOWER_METATILE_SLIVER_VERT_TOP,
            [WALL_SLIVER_VERT_BOT]= MIRAGETOWER_METATILE_SLIVER_VERT_BOT,
            [WALL_SLIVER_HORZ_L]  = MIRAGETOWER_METATILE_SLIVER_HORZ_L,
            [WALL_SLIVER_HORZ_R]  = MIRAGETOWER_METATILE_SLIVER_HORZ_R,
            [WALL_SLIVER_ISOLATED]= MIRAGETOWER_METATILE_SLIVER_ISOLATED,
        },

        .patches = sMirageTowerDriftPatch,
        .patchCount = ARRAY_COUNT(sMirageTowerDriftPatch),

        // No skirts: this tileset's wall edges are opaque art rather than an
        // overlay that bleeds, exactly as in the cave, which has none either.
        .decor = sMirageTowerDecor,
        .decorCount = ARRAY_COUNT(sMirageTowerDecor),
        .decorRarity = 14,

        .species = sMirageTowerSpecies,
        .speciesCount = ARRAY_COUNT(sMirageTowerSpecies),
        .encounterWindow = 12,

        // The cave backdrop under this theme's own sand ramp. Costs a palette
        // and no tiles - see BATTLE_ENVIRONMENT_MIRAGE_SANDS.
        .battleEnvironment = BATTLE_ENVIRONMENT_MIRAGE_SANDS,
    },
    [DUNGEON_THEME_JUNGLE] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_JUNGLE,

        // The fourth weather map, and the first that is not an Elite Four's.
        // Rain is what a jungle is missing once the art is right, and weather
        // cannot ride the tileset swap - see RogueDungeonRain/scripts.inc.
        //
        // It carries WEATHER_MONSOON, which is ours: vanilla's three rains are
        // one light one and two that throw lightning, and a ten-floor dungeon
        // wants the heavy rain without the bolts.
        //
        // IT IS MECHANICAL. Every rain sets B_WEATHER_RAIN_NORMAL, so all ten
        // floors are fought in rain. Chosen rather than inherited - a new
        // weather reaches battle only if it is added to that switch, and
        // WEATHER_PETALS is the one deliberately left out.
        .mapId = MAP_ROGUE_DUNGEON_RAIN,
        // unchanged -- Route 119 is the jungle route, and it already had it
        .music = MUS_ROUTE119,
        .mapSecId = MAPSEC_ROGUE_JUNGLE,

        .berries = TRUE,   // open sky and soil
        .generator = DUNGEON_GEN_TRAILS,   // the canopy tiles 1x1, unlike the woods
        .elevationFloor = DUNGEON_ELEVATION_FLOOR,
        .elevationWall = DUNGEON_ELEVATION_WALL,

        // Far more open than the cave: 12 rooms of 7-13 with 3-wide corridors
        // measures 42.7% floor against the cave's 24.0%, and still averages
        // 7.8 rooms so trainers and the exit stay well spread.
        .roomCount = 12,
        .roomMin = 7,
        .roomMax = 13,
        .corridorWidth = 3,

        .floor = JUNGLE_METATILE_FLOOR,
        // Encounters come from the long grass layer, not the ground, the same
        // way they do in the woods. The dirt is MB_NORMAL and safe to cross -
        // unchanged from when it was plain green grass, and deliberately so.
        // Swapping the art is not licence to move where wild battles fire.
        .tallGrass = 0,
        .longGrass = JUNGLE_METATILE_LONG_GRASS,
        .stairsDown = JUNGLE_METATILE_STAIRS,
        .stairsUp = JUNGLE_METATILE_STAIRS,
        // Twenty slots where there were two. The sheet's legend supplied every
        // one at full score, so a wall mass has faces, corners and slivers
        // instead of being an undifferentiated block of leaves.
        .wall =
        {
            [WALL_INTERIOR_LEFT]  = JUNGLE_METATILE_WALL_INTERIOR_L,
            [WALL_INTERIOR_MID]   = JUNGLE_METATILE_WALL_INTERIOR_M,
            [WALL_INTERIOR_RIGHT] = JUNGLE_METATILE_WALL_INTERIOR_R,
            [WALL_FACE_LEFT]      = JUNGLE_METATILE_WALL_FACE_L,
            [WALL_FACE_MID]       = JUNGLE_METATILE_WALL_FACE_M,
            [WALL_FACE_RIGHT]     = JUNGLE_METATILE_WALL_FACE_R,
            [WALL_NORTH_LEFT]     = JUNGLE_METATILE_WALL_NORTH_L,
            [WALL_NORTH_MID]      = JUNGLE_METATILE_WALL_NORTH_M,
            [WALL_NORTH_RIGHT]    = JUNGLE_METATILE_WALL_NORTH_R,
            [WALL_CORNER_OPEN_SE] = JUNGLE_METATILE_WALL_CORNER_SE,
            [WALL_CORNER_OPEN_SW] = JUNGLE_METATILE_WALL_CORNER_SW,
            [WALL_CORNER_OPEN_NW] = JUNGLE_METATILE_WALL_CORNER_NW,
            [WALL_CORNER_OPEN_NE] = JUNGLE_METATILE_WALL_CORNER_NE,
            [WALL_SLIVER_VERT]    = JUNGLE_METATILE_SLIVER_VERT,
            [WALL_SLIVER_HORZ]    = JUNGLE_METATILE_SLIVER_HORZ,
            [WALL_SLIVER_VERT_TOP]= JUNGLE_METATILE_SLIVER_VERT_TOP,
            [WALL_SLIVER_VERT_BOT]= JUNGLE_METATILE_SLIVER_VERT_BOT,
            [WALL_SLIVER_HORZ_L]  = JUNGLE_METATILE_SLIVER_HORZ_L,
            [WALL_SLIVER_HORZ_R]  = JUNGLE_METATILE_SLIVER_HORZ_R,
            [WALL_SLIVER_ISOLATED]= JUNGLE_METATILE_SLIVER_ISOLATED,
        },

        .patches = sJunglePatches,
        .patchCount = ARRAY_COUNT(sJunglePatches),

        // Rarity 4 rather than the old 2. The old pair were interchangeable
        // halves of a 50/50 mix, so half of everything was meant to flip; these
        // are variants of a fill that already reads, and seven entries at 1-in-2
        // would leave almost nothing plain.
        .decor = sJungleDecor,
        .decorCount = ARRAY_COUNT(sJungleDecor),
        .decorRarity = 4,

        .species = sJungleSpecies,
        .speciesCount = ARRAY_COUNT(sJungleSpecies),
        .encounterWindow = 12,

        // The theme paints long grass, so like the woods it was showing GRASS
        // on the grass and CAVE on the dirt - a jungle whose battles half
        // happened in a cave.
        .battleEnvironment = BATTLE_ENVIRONMENT_JUNGLE_CANOPY,
    },
    [DUNGEON_THEME_OCEAN] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_OCEAN,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
        // vanilla uses it for Faraway and Southern Islands
        .music = MUS_ABANDONED_SHIP,
        .mapSecId = MAPSEC_ROGUE_OCEAN,
        .generator = DUNGEON_GEN_CAVE,   // the rock nine slice tiles 1x1
        // NOT DUNGEON_ELEVATION_FLOOR. Water is elevation 1 - see the note by
        // the constant. The walls are ordinary rock and stay at 0.
        .elevationFloor = DUNGEON_ELEVATION_WATER,
        .elevationWall = DUNGEON_ELEVATION_WALL,

        // The most open shape in the game - 49.3% water against the jungle's
        // 42.8% - because it is the open sea, and at the jungle's 3-wide it
        // read as a sand field with channels cut through it.
        //
        // The rooms are the jungle's size deliberately. Widening them to 9-15
        // measured WORSE on both axes, 44.6% coverage with the room count
        // collapsing from 7.9 to 5.7, which is the size-is-the-wrong-lever
        // finding all over again. Corridor width alone buys the openness, and
        // it leaves the room count alone - which matters, because trainers and
        // the exit are placed per room.
        .roomCount = 12,
        .roomMin = 7,
        .roomMax = 13,
        .corridorWidth = 5,

        // Surfable, and that is what puts the player on a surf blob when the
        // floor loads. Encounters fire everywhere, so there is no grass layer.
        .floor = OCEAN_METATILE_WATER,
        .tallGrass = 0,
        .longGrass = 0,
        .stairsDown = OCEAN_METATILE_WHIRLPOOL,
        .stairsUp = OCEAN_METATILE_WHIRLPOOL,
        .wall =
        {
            [WALL_NORTH_LEFT]     = OCEAN_METATILE_ROCK_NW,
            [WALL_NORTH_MID]      = OCEAN_METATILE_ROCK_N,
            [WALL_NORTH_RIGHT]    = OCEAN_METATILE_ROCK_NE,
            [WALL_INTERIOR_LEFT]  = OCEAN_METATILE_ROCK_W,
            [WALL_INTERIOR_MID]   = OCEAN_METATILE_ROCK_MID,
            [WALL_INTERIOR_RIGHT] = OCEAN_METATILE_ROCK_E,
            [WALL_FACE_LEFT]      = OCEAN_METATILE_ROCK_SW,
            [WALL_FACE_MID]       = OCEAN_METATILE_ROCK_S,
            [WALL_FACE_RIGHT]     = OCEAN_METATILE_ROCK_SE,

            // These four were plain rock, and the wall's dark edge stopped dead
            // at every corner of every room. They are vanilla's own cliff inside
            // corners under Mossdeep's palette - the answer the corner-case scan
            // was already giving, and the same answer Granite Cave takes.
            [WALL_CORNER_OPEN_SE] = OCEAN_METATILE_CORNER_OPEN_SE,
            [WALL_CORNER_OPEN_SW] = OCEAN_METATILE_CORNER_OPEN_SW,
            [WALL_CORNER_OPEN_NW] = OCEAN_METATILE_CORNER_OPEN_NW,
            [WALL_CORNER_OPEN_NE] = OCEAN_METATILE_CORNER_OPEN_NE,

            [WALL_SLIVER_VERT]    = OCEAN_METATILE_SLIVER_VERT,
            [WALL_SLIVER_HORZ]    = OCEAN_METATILE_SLIVER_HORZ,
            [WALL_SLIVER_VERT_TOP]= OCEAN_METATILE_SLIVER_VERT_TOP,
            [WALL_SLIVER_VERT_BOT]= OCEAN_METATILE_SLIVER_VERT_BOT,
            [WALL_SLIVER_HORZ_L]  = OCEAN_METATILE_SLIVER_HORZ_L,
            [WALL_SLIVER_HORZ_R]  = OCEAN_METATILE_SLIVER_HORZ_R,
            [WALL_SLIVER_ISOLATED]= OCEAN_METATILE_SLIVER_ISOLATED,
        },

        // Swimmers, because a trainer here is standing on open water. They swim
        // toward the player when they spot one, which on Route 124 is exactly
        // what a swimmer does.
        .trainerGfx = OBJ_EVENT_GFX_SWIMMER_M,
        .trainerGfxAlt = OBJ_EVENT_GFX_SWIMMER_F,

        // Tate and Liza wait on a rock rather than in the water.
        .arenaPlatform = TRUE,

        .species = sOceanSpecies,
        .speciesCount = ARRAY_COUNT(sOceanSpecies),
        .encounterWindow = 12,

        // MB_OCEAN_WATER is a WATER encounter, not a land one - five slots
        // rather than twelve. See BuildWildEncounterTable.
        .wildArea = WILD_AREA_WATER,

        // The ocean surface resolved to POND, because
        // MetatileBehavior_IsSurfableWaterOrUnderwater is the only water test
        // the environment picker makes and a pond is what it answers with. An
        // open sea with islands on the horizon is the honest one.
        .battleEnvironment = BATTLE_ENVIRONMENT_OPEN_OCEAN,
    },
    [DUNGEON_THEME_UNDERWATER] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_UNDERWATER,

        // The one theme that does NOT live on the shared dungeon map. Diving is
        // a map-header property and the header is read out of ROM, so it cannot
        // be faked the way the tileset swap is. See theme->mapId.
        .mapId = MAP_ROGUE_DUNGEON_UNDERWATER,
        // unchanged, and already correct
        .music = MUS_UNDERWATER,
        .mapSecId = MAPSEC_ROGUE_UNDERWATER,

        .generator = DUNGEON_GEN_CAVE,

        // Elevation 3, the ordinary walking one, NOT the ocean's 1. The player
        // is on the seafloor rather than on the surface, and vanilla puts 1467
        // of 1468 passable underwater blocks at 3.
        .elevationFloor = DUNGEON_ELEVATION_FLOOR,
        .elevationWall = DUNGEON_ELEVATION_WALL,

        // As open as the ocean. Vanilla underwater is a wide basin rather than
        // a corridor network, and wide corridors are also what keeps the sliver
        // slots from firing often, which matters because this tileset has no
        // one-block-thick wall art at all.
        .roomCount = 12,
        .roomMin = 7,
        .roomMax = 13,
        .corridorWidth = 5,

        .floor = UNDERWATER_METATILE_FLOOR,

        // Encounters come from the seaweed patches below rather than from the
        // floor, which is MB_NORMAL and carries none. That is what vanilla
        // does - roughly 40% of its passable underwater area is seaweed - and
        // it is the woods' arrangement rather than the ocean's.
        //
        // longGrass names the seaweed and tallGrass stays 0, which is the
        // jungle's arrangement and not an abuse of the field: tallGrass is what
        // switches on the grass-BLOB placement, and this theme is painted by a
        // patch layer instead, so setting it would place the seaweed twice.
        // longGrass is left as the declaration of where encounters fire, which
        // is what check_encounter_flags.py reads. Everything that would paint
        // from it - the base row under exposed blades, GrassAt - lives in
        // StampCell, and StampCell is DUNGEON_GEN_WOODS only.
        .tallGrass = 0,
        .longGrass = UNDERWATER_METATILE_SEAWEED,

        // The same whirlpool the ocean uses, appended to this tileset too. Both
        // water dungeons descending through one shape is the point: the exit is
        // what the player is hunting for, and two different ones would be two
        // things to learn instead of one.
        .stairsDown = UNDERWATER_METATILE_STAIRS,
        .stairsUp = UNDERWATER_METATILE_STAIRS,
        .wall =
        {
            [WALL_NORTH_LEFT]     = UNDERWATER_METATILE_WALL_NW,
            [WALL_NORTH_MID]      = UNDERWATER_METATILE_WALL_N,
            [WALL_NORTH_RIGHT]    = UNDERWATER_METATILE_WALL_NE,
            [WALL_INTERIOR_LEFT]  = UNDERWATER_METATILE_WALL_W,
            [WALL_INTERIOR_MID]   = UNDERWATER_METATILE_WALL_MID,
            [WALL_INTERIOR_RIGHT] = UNDERWATER_METATILE_WALL_E,
            [WALL_FACE_LEFT]      = UNDERWATER_METATILE_WALL_SW,
            [WALL_FACE_MID]       = UNDERWATER_METATILE_WALL_S,
            [WALL_FACE_RIGHT]     = UNDERWATER_METATILE_WALL_SE,

            [WALL_CORNER_OPEN_SE] = UNDERWATER_METATILE_CORNER_OPEN_SE,
            [WALL_CORNER_OPEN_SW] = UNDERWATER_METATILE_CORNER_OPEN_SW,
            [WALL_CORNER_OPEN_NW] = UNDERWATER_METATILE_CORNER_OPEN_NW,
            [WALL_CORNER_OPEN_NE] = UNDERWATER_METATILE_CORNER_OPEN_NE,

            // No thin-wall art exists: the case never occurs once across all
            // twelve vanilla Underwater layouts. Rather than the interior, which
            // paints an unedged bar, each falls back on the edge that WOULD be
            // visible - the south face for a horizontal, a side for a vertical.
            // The mock still hits the horizontal three ways about 11 times a
            // floor, so this is not hypothetical; composed art would be better.
            [WALL_SLIVER_HORZ]    = UNDERWATER_METATILE_WALL_S,
            [WALL_SLIVER_HORZ_L]  = UNDERWATER_METATILE_WALL_S,
            [WALL_SLIVER_HORZ_R]  = UNDERWATER_METATILE_WALL_S,
            [WALL_SLIVER_ISOLATED]= UNDERWATER_METATILE_WALL_S,
            [WALL_SLIVER_VERT]    = UNDERWATER_METATILE_WALL_W,
            [WALL_SLIVER_VERT_TOP]= UNDERWATER_METATILE_WALL_W,
            [WALL_SLIVER_VERT_BOT]= UNDERWATER_METATILE_WALL_W,
        },

        .patches = sUnderwaterPatch,
        .patchCount = ARRAY_COUNT(sUnderwaterPatch),

        // The seafloor brings its own trainers, so both halves of a diver
        // agree: the entry names the overworld sprite AND the trainer whose
        // battle pic will follow it. trainerGfx below is the fallback for a
        // floor that somehow places a trainer without going through the table.
        .trainers = sUnderwaterTrainers,
        .trainerCount = ARRAY_COUNT(sUnderwaterTrainers),
        .trainerGfx = OBJ_EVENT_GFX_ROGUE_DIVER_M,
        .trainerGfxAlt = OBJ_EVENT_GFX_ROGUE_DIVER_F,

        // No platform, unlike the ocean: the seafloor is ordinary walkable
        // ground, so Juan stands on it the way every land boss does.
        .arenaPlatform = FALSE,

        .species = sUnderwaterSpecies,
        .speciesCount = ARRAY_COUNT(sUnderwaterSpecies),
        .encounterWindow = 12,

        // The seaweed is MB_SEAWEED_NO_SURFACING, which carries TILE_FLAG_
        // SURFABLE, so it is a WATER encounter even though the player is
        // walking. Vanilla agrees: every UNDERWATER map registers water_mons.
        .wildArea = WILD_AREA_WATER,

        // The one theme that already resolved CORRECTLY on its own - it has a
        // map of its own and MAP_TYPE_UNDERWATER short-circuits the picker - so
        // this is an upgrade rather than a fix. Vanilla's underwater backdrop
        // is fine; this one is the seafloor the theme actually paints.
        .battleEnvironment = BATTLE_ENVIRONMENT_ABYSSAL_DEPTHS,
    },

    // Victory Road, the Elite Four's four dungeons. One theme each, differing
    // ONLY in the palette their tileset ships - see the VICTORYROAD_METATILE_*
    // block in the header and make_victory_road_palettes.py.
    //
    // Vanilla Emerald's Victory Road runs on gTileset_General + gTileset_Cave,
    // the identical pair to Granite Cave. So the honest version of this theme
    // is not a new wall table, it is a recolour: the entire cave wall table,
    // both stairs and all seven composed slivers draw from palette 6 and only
    // palette 6, and palette 6 is the first SECONDARY slot, so a tileset that
    // shares Cave's tiles and metatiles and ships its own palettes recolours
    // one hundred percent of what the generator paints.
    //
    // Deliberately NO .patches and NO decor, and that is not laziness. The
    // cave's sand pool region draws 45% from primary palette 5 and its decor
    // 50% from primary palette 3, both owned by gTileset_General and shared
    // with every other theme, so they cannot be recoloured and would sit in
    // Granite Cave's browns on a violet floor. A theme with no region set is
    // an ordinary thing here - Fiery Path and New Mauville have none either.
    //
    // Written out four times rather than behind a macro because
    // check_encounter_flags.py parses `.layoutId = <token>` out of this table
    // literally; a pasted token would leave these four themes silently
    // unverified, which is the exact failure the lint exists to catch.
    [DUNGEON_THEME_VICTORYROAD_SIDNEY] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_VRSIDNEY,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
        // it is Victory Road
        .music = MUS_VICTORY_ROAD,
        .mapSecId = MAPSEC_ROGUE_VR_SIDNEY,
        .generator = DUNGEON_GEN_CAVE,
        .elevationFloor = DUNGEON_ELEVATION_FLOOR,
        .elevationWall = DUNGEON_ELEVATION_WALL,
        .floor = VICTORYROAD_METATILE_FLOOR,
        .tallGrass = 0,   // a cave has no grass; encounters fire anywhere
        .longGrass = 0,
        .stairsDown = VICTORYROAD_METATILE_STAIRS_DOWN,
        .stairsUp = VICTORYROAD_METATILE_STAIRS_UP,
        .wall =
        {
            [WALL_INTERIOR_LEFT]  = VICTORYROAD_METATILE_WALL_INTERIOR_L,
            [WALL_INTERIOR_MID]   = VICTORYROAD_METATILE_WALL_INTERIOR_M,
            [WALL_INTERIOR_RIGHT] = VICTORYROAD_METATILE_WALL_INTERIOR_R,
            [WALL_FACE_LEFT]      = VICTORYROAD_METATILE_WALL_FACE_L,
            [WALL_FACE_MID]       = VICTORYROAD_METATILE_WALL_FACE_M,
            [WALL_FACE_RIGHT]     = VICTORYROAD_METATILE_WALL_FACE_R,
            [WALL_NORTH_LEFT]     = VICTORYROAD_METATILE_WALL_NORTH_L,
            [WALL_NORTH_MID]      = VICTORYROAD_METATILE_WALL_NORTH_M,
            [WALL_NORTH_RIGHT]    = VICTORYROAD_METATILE_WALL_NORTH_R,
            [WALL_CORNER_OPEN_SE] = VICTORYROAD_METATILE_WALL_CORNER_NW,
            [WALL_CORNER_OPEN_SW] = VICTORYROAD_METATILE_WALL_CORNER_NE,
            [WALL_CORNER_OPEN_NW] = VICTORYROAD_METATILE_WALL_CORNER_S,
            [WALL_CORNER_OPEN_NE] = VICTORYROAD_METATILE_WALL_CORNER_S,
            [WALL_SLIVER_VERT]    = VICTORYROAD_METATILE_SLIVER_VERT,
            [WALL_SLIVER_HORZ]    = VICTORYROAD_METATILE_SLIVER_HORZ,
            [WALL_SLIVER_VERT_TOP]= VICTORYROAD_METATILE_SLIVER_VERT_TOP,
            [WALL_SLIVER_VERT_BOT]= VICTORYROAD_METATILE_SLIVER_VERT_BOT,
            [WALL_SLIVER_HORZ_L]  = VICTORYROAD_METATILE_SLIVER_HORZ_L,
            [WALL_SLIVER_HORZ_R]  = VICTORYROAD_METATILE_SLIVER_HORZ_R,
            [WALL_SLIVER_ISOLATED]= VICTORYROAD_METATILE_SLIVER_ISOLATED,
        },
        .species = sSidneySpecies,
        .speciesCount = ARRAY_COUNT(sSidneySpecies),

        // The cave art under this theme's own palette, sampled from the very
        // tileset palette its walls draw with. Ramp 2-7 of 1-7, chosen by
        // looking - the full ramp lights the floor lilac.
        .battleEnvironment = BATTLE_ENVIRONMENT_VR_SIDNEY,
    },

    // Phoebe is the only one with a mapId of its own. Weather lives in the map
    // HEADER, which is read out of ROM by warp group and id, so the RAM patch
    // that swaps tilesets per theme cannot reach it - the same wall underwater
    // ran into. MAP_ROGUE_DUNGEON_FOG shares LAYOUT_ROGUE_DUNGEON_FLOOR, so
    // mapLayoutId is identical and every dispatch keying on it is untouched.
    //
    // The fog is why this palette can be as dark as it is: it is the lowest
    // contrast of the four by some way, and fog over a near-black floor reads
    // as depth rather than as an unlit room.
    [DUNGEON_THEME_VICTORYROAD_PHOEBE] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_VRPHOEBE,
        .mapId = MAP_ROGUE_DUNGEON_FOG,
        // Phoebe is the Mt Pyre ghost trainer; the exterior track keeps her distinct from CAVE
        .music = MUS_MT_PYRE_EXTERIOR,
        .mapSecId = MAPSEC_ROGUE_VR_PHOEBE,
        .generator = DUNGEON_GEN_CAVE,
        .elevationFloor = DUNGEON_ELEVATION_FLOOR,
        .elevationWall = DUNGEON_ELEVATION_WALL,
        .floor = VICTORYROAD_METATILE_FLOOR,
        .tallGrass = 0,
        .longGrass = 0,
        .stairsDown = VICTORYROAD_METATILE_STAIRS_DOWN,
        .stairsUp = VICTORYROAD_METATILE_STAIRS_UP,
        .wall =
        {
            [WALL_INTERIOR_LEFT]  = VICTORYROAD_METATILE_WALL_INTERIOR_L,
            [WALL_INTERIOR_MID]   = VICTORYROAD_METATILE_WALL_INTERIOR_M,
            [WALL_INTERIOR_RIGHT] = VICTORYROAD_METATILE_WALL_INTERIOR_R,
            [WALL_FACE_LEFT]      = VICTORYROAD_METATILE_WALL_FACE_L,
            [WALL_FACE_MID]       = VICTORYROAD_METATILE_WALL_FACE_M,
            [WALL_FACE_RIGHT]     = VICTORYROAD_METATILE_WALL_FACE_R,
            [WALL_NORTH_LEFT]     = VICTORYROAD_METATILE_WALL_NORTH_L,
            [WALL_NORTH_MID]      = VICTORYROAD_METATILE_WALL_NORTH_M,
            [WALL_NORTH_RIGHT]    = VICTORYROAD_METATILE_WALL_NORTH_R,
            [WALL_CORNER_OPEN_SE] = VICTORYROAD_METATILE_WALL_CORNER_NW,
            [WALL_CORNER_OPEN_SW] = VICTORYROAD_METATILE_WALL_CORNER_NE,
            [WALL_CORNER_OPEN_NW] = VICTORYROAD_METATILE_WALL_CORNER_S,
            [WALL_CORNER_OPEN_NE] = VICTORYROAD_METATILE_WALL_CORNER_S,
            [WALL_SLIVER_VERT]    = VICTORYROAD_METATILE_SLIVER_VERT,
            [WALL_SLIVER_HORZ]    = VICTORYROAD_METATILE_SLIVER_HORZ,
            [WALL_SLIVER_VERT_TOP]= VICTORYROAD_METATILE_SLIVER_VERT_TOP,
            [WALL_SLIVER_VERT_BOT]= VICTORYROAD_METATILE_SLIVER_VERT_BOT,
            [WALL_SLIVER_HORZ_L]  = VICTORYROAD_METATILE_SLIVER_HORZ_L,
            [WALL_SLIVER_HORZ_R]  = VICTORYROAD_METATILE_SLIVER_HORZ_R,
            [WALL_SLIVER_ISOLATED]= VICTORYROAD_METATILE_SLIVER_ISOLATED,
        },
        .species = sPhoebeSpecies,
        .speciesCount = ARRAY_COUNT(sPhoebeSpecies),

        // Ramp 5-7. This palette is the darkest of the four - it tops out at
        // luminance 97 where the source art reaches 155 - so every ramp comes
        // out dark and the choice is only how much violet survives.
        .battleEnvironment = BATTLE_ENVIRONMENT_VR_PHOEBE,
    },
    // The second theme with a map of its own, for the same reason Phoebe has
    // one - weather is in the map header, out of reach of the tileset patch.
    //
    // Unlike the fog, this one is NOT only cosmetic, and that is intended.
    // B_OVERWORLD_SNOW is GEN_LATEST, so ArriveInBattle sets B_WEATHER_SNOW
    // here: every Ice type on the floor gets 1.5x Defense, Blizzard cannot
    // miss, Weather Ball turns Ice. Gen 9 snow does no chip damage the way hail
    // would, so it costs the player's team nothing just for being here - but
    // Glacia's own party is Ice and she fights her boss battle with the buff up.
    // Glacia is the one Elite Four member NOT on the shared Victory Road
    // tileset. She has Lapis Cave, imported from a Mystery Dungeon sheet - see
    // the metatile block in the header for what that bought and what it cost.
    // The theme id keeps its VICTORYROAD_ name because ThemeForFloor is a
    // modulo over this enum and the INDEX is what lands her theme on her
    // dungeon; renaming it would be churn with a real chance of an off-by-one.
    [DUNGEON_THEME_VICTORYROAD_GLACIA] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_LAPIS,
        .mapId = MAP_ROGUE_DUNGEON_SNOW,
        // unchanged -- it is what the snow map already carried
        .music = MUS_ABNORMAL_WEATHER,
        .mapSecId = MAPSEC_ROGUE_VR_GLACIA,
        .generator = DUNGEON_GEN_CAVE,
        .elevationFloor = DUNGEON_ELEVATION_FLOOR,
        .elevationWall = DUNGEON_ELEVATION_WALL,
        .floor = LAPIS_METATILE_FLOOR,
        .tallGrass = 0,
        .longGrass = 0,
        .stairsDown = LAPIS_METATILE_STAIRS,
        .stairsUp = LAPIS_METATILE_STAIRS,
        // The first table in the project where all four inside corners differ,
        // and the first with no composed slivers - both because the sheet's own
        // autotile legend covers cases vanilla Emerald never drew.
        .wall =
        {
            [WALL_INTERIOR_LEFT]  = LAPIS_METATILE_WALL_INTERIOR_L,
            [WALL_INTERIOR_MID]   = LAPIS_METATILE_WALL_INTERIOR_M,
            [WALL_INTERIOR_RIGHT] = LAPIS_METATILE_WALL_INTERIOR_R,
            [WALL_FACE_LEFT]      = LAPIS_METATILE_WALL_FACE_L,
            [WALL_FACE_MID]       = LAPIS_METATILE_WALL_FACE_M,
            [WALL_FACE_RIGHT]     = LAPIS_METATILE_WALL_FACE_R,
            [WALL_NORTH_LEFT]     = LAPIS_METATILE_WALL_NORTH_L,
            [WALL_NORTH_MID]      = LAPIS_METATILE_WALL_NORTH_M,
            [WALL_NORTH_RIGHT]    = LAPIS_METATILE_WALL_NORTH_R,
            [WALL_CORNER_OPEN_SE] = LAPIS_METATILE_WALL_CORNER_SE,
            [WALL_CORNER_OPEN_SW] = LAPIS_METATILE_WALL_CORNER_SW,
            [WALL_CORNER_OPEN_NW] = LAPIS_METATILE_WALL_CORNER_NW,
            [WALL_CORNER_OPEN_NE] = LAPIS_METATILE_WALL_CORNER_NE,
            [WALL_SLIVER_VERT]    = LAPIS_METATILE_SLIVER_VERT,
            [WALL_SLIVER_HORZ]    = LAPIS_METATILE_SLIVER_HORZ,
            [WALL_SLIVER_VERT_TOP]= LAPIS_METATILE_SLIVER_VERT_TOP,
            [WALL_SLIVER_VERT_BOT]= LAPIS_METATILE_SLIVER_VERT_BOT,
            [WALL_SLIVER_HORZ_L]  = LAPIS_METATILE_SLIVER_HORZ_L,
            [WALL_SLIVER_HORZ_R]  = LAPIS_METATILE_SLIVER_HORZ_R,
            [WALL_SLIVER_ISOLATED]= LAPIS_METATILE_SLIVER_ISOLATED,
        },
        // A clump and a swept drift, scattered over the snow. Both are the
        // sheet's own art in the floor's own palette, so this is texture
        // breaking up a repeated tile rather than ornament placed on it - 1 in
        // 8 floor blocks, which is denser than the ornamental themes run at
        // because there is nothing here to notice individually.
        .decor = sGlaciaDecor,
        .decorCount = ARRAY_COUNT(sGlaciaDecor),
        .decorRarity = 8,

        .species = sGlaciaSpecies,
        .speciesCount = ARRAY_COUNT(sGlaciaSpecies),

        // The snow dungeon, and the one place a theme and a BOSS agree: Glacia
        // herself is overridden to this too, in sDungeonBossEnvironment, so her
        // arena does not switch to a stadium after nine floors of ice.
        .battleEnvironment = BATTLE_ENVIRONMENT_FROZEN_DEPTHS,
    },
    [DUNGEON_THEME_VICTORYROAD_DRAKE] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_VRDRAKE,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
        // vanilla uses it for Meteor Falls, which is the dragon cave
        .music = MUS_CAVE_OF_ORIGIN,
        .mapSecId = MAPSEC_ROGUE_VR_DRAKE,
        .generator = DUNGEON_GEN_CAVE,
        .elevationFloor = DUNGEON_ELEVATION_FLOOR,
        .elevationWall = DUNGEON_ELEVATION_WALL,
        .floor = VICTORYROAD_METATILE_FLOOR,
        .tallGrass = 0,
        .longGrass = 0,
        .stairsDown = VICTORYROAD_METATILE_STAIRS_DOWN,
        .stairsUp = VICTORYROAD_METATILE_STAIRS_UP,
        .wall =
        {
            [WALL_INTERIOR_LEFT]  = VICTORYROAD_METATILE_WALL_INTERIOR_L,
            [WALL_INTERIOR_MID]   = VICTORYROAD_METATILE_WALL_INTERIOR_M,
            [WALL_INTERIOR_RIGHT] = VICTORYROAD_METATILE_WALL_INTERIOR_R,
            [WALL_FACE_LEFT]      = VICTORYROAD_METATILE_WALL_FACE_L,
            [WALL_FACE_MID]       = VICTORYROAD_METATILE_WALL_FACE_M,
            [WALL_FACE_RIGHT]     = VICTORYROAD_METATILE_WALL_FACE_R,
            [WALL_NORTH_LEFT]     = VICTORYROAD_METATILE_WALL_NORTH_L,
            [WALL_NORTH_MID]      = VICTORYROAD_METATILE_WALL_NORTH_M,
            [WALL_NORTH_RIGHT]    = VICTORYROAD_METATILE_WALL_NORTH_R,
            [WALL_CORNER_OPEN_SE] = VICTORYROAD_METATILE_WALL_CORNER_NW,
            [WALL_CORNER_OPEN_SW] = VICTORYROAD_METATILE_WALL_CORNER_NE,
            [WALL_CORNER_OPEN_NW] = VICTORYROAD_METATILE_WALL_CORNER_S,
            [WALL_CORNER_OPEN_NE] = VICTORYROAD_METATILE_WALL_CORNER_S,
            [WALL_SLIVER_VERT]    = VICTORYROAD_METATILE_SLIVER_VERT,
            [WALL_SLIVER_HORZ]    = VICTORYROAD_METATILE_SLIVER_HORZ,
            [WALL_SLIVER_VERT_TOP]= VICTORYROAD_METATILE_SLIVER_VERT_TOP,
            [WALL_SLIVER_VERT_BOT]= VICTORYROAD_METATILE_SLIVER_VERT_BOT,
            [WALL_SLIVER_HORZ_L]  = VICTORYROAD_METATILE_SLIVER_HORZ_L,
            [WALL_SLIVER_HORZ_R]  = VICTORYROAD_METATILE_SLIVER_HORZ_R,
            [WALL_SLIVER_ISOLATED]= VICTORYROAD_METATILE_SLIVER_ISOLATED,
        },
        .species = sDrakeSpecies,
        .speciesCount = ARRAY_COUNT(sDrakeSpecies),
        // SEVEN, and it is the only theme that needs its own. Gen 1-4 holds
        // exactly seven Dragons once the legendaries, the 600 BST pseudos and
        // the pre-evolutions are out, so this is the whole type rather than a
        // choice. The default of 8 would leave the window permanently one short
        // and the floor repeating a species across its twelve slots.
        .encounterWindow = ARRAY_COUNT(sDrakeSpecies),

        // Ramp 3-7 rather than the full one, and the reason is Fiery Path.
        // Drake's palette at 1-7 lights the floor hot red on the SAME
        // silhouette Fiery Path already uses, so the two dungeons would have
        // read as the same room. The narrow end keeps it maroon stone.
        .battleEnvironment = BATTLE_ENVIRONMENT_VR_DRAKE,
    },
    // Ever Grande, Wallace's, and the first theme whose ENCOUNTER SURFACE is
    // something other than grass, cave floor or water.
    //
    // Corridors are 5 wide like the ocean's, and that is a structural choice
    // rather than a taste one: at 3 wide the vertical sliver fires and this
    // tileset has no art for it, so widening removes the only composed art the
    // theme would have needed. It also suits a meadow.
    //
    // tallGrass stays 0 and longGrass names the tall flowery grass, which is
    // the jungle's and underwater's arrangement. tallGrass is not merely
    // unused here, it is unsafe: PrepareFloor places grass blobs whenever
    // tallGrass is nonzero and consumes RNG doing it, while only StampCell -
    // DUNGEON_GEN_WOODS - ever paints from them. Setting it on a cave-generator
    // theme would shift the RNG stream and silently relay out every floor for
    // blobs that never get drawn.
    //
    // longGrass is left as the declaration of where encounters fire, which is
    // what check_encounter_flags.py reads. The short beds are the floor's OTHER
    // encounter surface and no field names them - they are painted by patch
    // layer 0 and carry MB_UNUSED_05. The lint only has to find one surface per
    // theme so it passes either way, which means it cannot catch the beds
    // silently losing their encounter flag; make_evergrande_tiles.py asserts
    // that instead, on the attributes it has just written.
    [DUNGEON_THEME_EVERGRANDE] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_EVERGRANDE,
        .berries = TRUE,   // open sky and soil

        // Blossom on the wind. Weather lives in the map header, which is read
        // out of ROM by warp group and id, so it needs a map of its own the way
        // the fog and the snow do - and like those, the map is named for the
        // WEATHER rather than for this theme, so a later one can point here too.
        .mapId = MAP_ROGUE_DUNGEON_PETALS,
        // it is Ever Grande
        .music = MUS_EVER_GRANDE,
        .mapSecId = MAPSEC_ROGUE_EVERGRANDE,

        // An overgrown prairie thicket: many small bedded-down clearings joined
        // by narrow paths the Pokemon wore themselves. Sixteen clearings at a
        // lobe of three rather than the jungle's twelve at five, which is what
        // makes them small enough to read as nests instead of rooms.
        //
        // roomCount/roomMin/roomMax and corridorWidth are GONE, not zeroed:
        // TRAILS reads none of them. corridorWidth was 5 for one reason - to
        // stop the vertical sliver firing on a tileset that had no art for one -
        // and this tileset has all seven, so the reason is gone with it.
        .generator = DUNGEON_GEN_TRAILS,
        // Sixteen clearings on a 4x4 jittered grid, four small lobes each,
        // ANCHORED. Settled by rendering every candidate against the real tileset
        // and picking one, not by optimising a number - which matters here,
        // because the number and the eye disagreed. Averaged over 40 seeds:
        //
        //   clr lobe lobes drift  distinct  open   narrow  biggest clearing
        //    12    5     4     0       4.4  27.1%   10.7%   304   (jungle's)
        //     9    5     3     2       5.8  22.9%   16.3%   142
        //    16    5     2     2       6.0  28.5%   11.0%   249
        //    16    3     4     0       7.3  25.7%   13.9%   178   <- this
        //    16    3     3     2       7.3  24.5%   20.7%   191
        //    16    3     3     0       8.6  22.8%   20.0%   145
        //    16    3     2     2       9.2  21.2%   21.7%   104
        //    16    3     2     0       9.8  20.7%   24.7%    94
        //
        // NOTE THAT THIS IS NOT THE TOP OF THAT TABLE ON ANY COLUMN. Fewer lobes
        // score better on every metric I built - more clearings stay distinct,
        // the biggest gets smaller, more of the floor is narrow - and two lobes
        // was set on that basis and then rejected on sight. Two rectangles make
        // an angular pocket, and a floor of angular pockets reads worse than the
        // metrics say it should. Four lobes is rounder and more connected.
        //
        // So: do not "improve" this by chasing distinct-clearing count. The
        // metric measures separation, and separation past a point is just a
        // sparser floor.
        //
        // Nine clearings on a 3x3 grid clump into the middle and leave the map
        // edges empty; sixteen on a 4x4 fills it. Lobe 5 is out at any count -
        // it wants to merge, 249 to 304 block clearings.
        //
        // Anchored rather than drifting: drift was built for this theme, tried at
        // 2, and rejected - it reads as detached chunks. trailLobeDrift is 0 and
        // left unwritten. The FIELD stays, because check_trail_floor.py's drift-4
        // stress row is the only test of the stub, and that stub is the only
        // thing making unanchored lobes safe at all.
        .trailClearings = 16,
        .trailLobe = 3,
        .trailLobes = 4,
        .elevationFloor = DUNGEON_ELEVATION_FLOOR,
        .elevationWall = DUNGEON_ELEVATION_WALL,
        .floor = MEADOW_METATILE_FLOOR,
        .tallGrass = 0,
        .longGrass = MEADOW_METATILE_GRASS,
        .stairsDown = MEADOW_METATILE_STAIRS,
        .stairsUp = MEADOW_METATILE_STAIRS,
        .wall =
        {
            [WALL_NORTH_LEFT]     = MEADOW_METATILE_NORTH_LEFT,
            [WALL_NORTH_MID]      = MEADOW_METATILE_NORTH_MID,
            [WALL_NORTH_RIGHT]    = MEADOW_METATILE_NORTH_RIGHT,
            [WALL_INTERIOR_LEFT]  = MEADOW_METATILE_INTERIOR_LEFT,
            [WALL_INTERIOR_MID]   = MEADOW_METATILE_INTERIOR_MID,
            [WALL_INTERIOR_RIGHT] = MEADOW_METATILE_INTERIOR_RIGHT,
            [WALL_FACE_LEFT]      = MEADOW_METATILE_FACE_LEFT,
            [WALL_FACE_MID]       = MEADOW_METATILE_FACE_MID,
            [WALL_FACE_RIGHT]     = MEADOW_METATILE_FACE_RIGHT,
            [WALL_CORNER_OPEN_SE] = MEADOW_METATILE_CORNER_OPEN_SE,
            [WALL_CORNER_OPEN_SW] = MEADOW_METATILE_CORNER_OPEN_SW,
            [WALL_CORNER_OPEN_NW] = MEADOW_METATILE_CORNER_OPEN_NW,
            [WALL_CORNER_OPEN_NE] = MEADOW_METATILE_CORNER_OPEN_NE,
            // ALL SEVEN SLIVERS ARE REAL ART NOW. The old table pointed the
            // vertical three at WALL_INTERIOR_M because the tileset had nothing
            // to draw, and corridorWidth 5 existed to stop them ever firing.
            // Both of those workarounds are now unnecessary - see the note on
            // corridorWidth below.
            [WALL_SLIVER_HORZ]    = MEADOW_METATILE_SLIVER_HORZ,
            [WALL_SLIVER_HORZ_L]  = MEADOW_METATILE_SLIVER_HORZ_L,
            [WALL_SLIVER_HORZ_R]  = MEADOW_METATILE_SLIVER_HORZ_R,
            [WALL_SLIVER_VERT]    = MEADOW_METATILE_SLIVER_VERT,
            [WALL_SLIVER_VERT_TOP]= MEADOW_METATILE_SLIVER_VERT_TOP,
            [WALL_SLIVER_VERT_BOT]= MEADOW_METATILE_SLIVER_VERT_BOT,
            // Wallace's dais - arenaPlatform paints one wall block ringed by
            // floor, which resolves here. It used to be COBBLE because that was
            // the only sensible thing in the old tileset; it is now the real
            // isolated sliver, so the dais is a lone flowering shrub. Defensible
            // in a meadow and solid either way, but it is a visible change and
            // wants looking at on floor 105.
            [WALL_SLIVER_ISOLATED]= MEADOW_METATILE_SLIVER_ISOLATED,
        },
        .decor = sMeadowDecor,
        .decorCount = ARRAY_COUNT(sMeadowDecor),
        .decorRarity = 10,
        .patches = sMeadowPatch,
        .patchCount = ARRAY_COUNT(sMeadowPatch),
        .arenaPlatform = TRUE,
        .species = sEverGrandeSpecies,
        .speciesCount = ARRAY_COUNT(sEverGrandeSpecies),

        // PLACEHOLDER, pending meadow art. It also closes the same split the
        // woods had: this theme paints flower beds and long grass, so the
        // engine gave it LONG_GRASS on those and CAVE on the cobble - two
        // backdrops on one floor depending where the player was standing.
        .battleEnvironment = BATTLE_ENVIRONMENT_OPEN_PLAIN,
    },
    // Steven's, dungeon 13, and the last theme the run needed. Everything
    // before this either had a dungeon of its own or was borrowing one; with
    // this the enum is as long as the run and nothing wraps.
    [DUNGEON_THEME_MURKYCAVE] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_MURKYCAVE,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
        // vanilla uses it for Ancient Tomb, Desert Ruins and Island Cave -- the eerie one
        .music = MUS_SEALED_CHAMBER,
        .mapSecId = MAPSEC_ROGUE_MURKYCAVE,
        .generator = DUNGEON_GEN_ORGANIC,
        .caveFill = 48,
        .elevationFloor = DUNGEON_ELEVATION_FLOOR,
        .elevationWall = DUNGEON_ELEVATION_WALL,

        // The cave's own shape, not the jungle's openness. The finale is ten
        // floors of carved corridor rather than a plateau, and a tighter carve
        // is what makes the pillared wall art readable - a wall mass that is
        // mostly interior shows none of its edges.
        .floor = MURKY_METATILE_FLOOR,
        // A cave, so encounters come from the floor itself. There is no grass
        // layer here to take that job the way the woods and jungle have.
        .tallGrass = 0,
        .longGrass = 0,
        .stairsDown = MURKY_METATILE_STAIRS,
        .stairsUp = MURKY_METATILE_STAIRS,
        .wall =
        {
            [WALL_INTERIOR_LEFT]  = MURKY_METATILE_WALL_INTERIOR_L,
            [WALL_INTERIOR_MID]   = MURKY_METATILE_WALL_INTERIOR_M,
            [WALL_INTERIOR_RIGHT] = MURKY_METATILE_WALL_INTERIOR_R,
            [WALL_FACE_LEFT]      = MURKY_METATILE_WALL_FACE_L,
            [WALL_FACE_MID]       = MURKY_METATILE_WALL_FACE_M,
            [WALL_FACE_RIGHT]     = MURKY_METATILE_WALL_FACE_R,
            [WALL_NORTH_LEFT]     = MURKY_METATILE_WALL_NORTH_L,
            [WALL_NORTH_MID]      = MURKY_METATILE_WALL_NORTH_M,
            [WALL_NORTH_RIGHT]    = MURKY_METATILE_WALL_NORTH_R,
            [WALL_CORNER_OPEN_SE] = MURKY_METATILE_WALL_CORNER_SE,
            [WALL_CORNER_OPEN_SW] = MURKY_METATILE_WALL_CORNER_SW,
            [WALL_CORNER_OPEN_NW] = MURKY_METATILE_WALL_CORNER_NW,
            [WALL_CORNER_OPEN_NE] = MURKY_METATILE_WALL_CORNER_NE,
            [WALL_SLIVER_VERT]    = MURKY_METATILE_SLIVER_VERT,
            [WALL_SLIVER_HORZ]    = MURKY_METATILE_SLIVER_HORZ,
            [WALL_SLIVER_VERT_TOP]= MURKY_METATILE_SLIVER_VERT_TOP,
            [WALL_SLIVER_VERT_BOT]= MURKY_METATILE_SLIVER_VERT_BOT,
            [WALL_SLIVER_HORZ_L]  = MURKY_METATILE_SLIVER_HORZ_L,
            [WALL_SLIVER_HORZ_R]  = MURKY_METATILE_SLIVER_HORZ_R,
            [WALL_SLIVER_ISOLATED]= MURKY_METATILE_SLIVER_ISOLATED,
        },

        .patches = sMurkyPatches,
        .patchCount = ARRAY_COUNT(sMurkyPatches),

        // Eight entries, the most of any theme, so a lower rate than the
        // jungle's: at 1-in-4 with eight variants almost nothing would be left
        // plain, and the pillars are meant to be occasional rather than the
        // rule.
        .decor = sMurkyDecor,
        .decorCount = ARRAY_COUNT(sMurkyDecor),
        .decorRarity = 7,

        .species = sStevenSpecies,
        .speciesCount = ARRAY_COUNT(sStevenSpecies),

        // Steven's dungeon, and the run's last floors before the finale. Mossy
        // grey rock, which is what the theme already looks like on the floor.
        .battleEnvironment = BATTLE_ENVIRONMENT_MURKY_DEPTHS,
    },
};

// Which theme each dungeon uses, following the stock game: Petalburg Woods then
// Roxanne, Granite Cave then Brawly, New Mauville then Wattson, Fiery Path then
// Flannery, Mirage Tower then Norman - the Go-Goggles and the Route 111 desert
// are what sit between Lavaridge and Petalburg - and the jungle then Winona,
// Route 119 and 120 being the rainy overgrown approach to Fortree. Then the
// open ocean and Tate and Liza, because Routes 124 to 126 out of Lilycove are
// how the stock game reaches Mossdeep. Then the seafloor and Juan, since diving
// is what Mossdeep hands the player and Sootopolis is reachable no other way.
//
// That is all eight gym dungeons with a theme of their own and no repeats.
//
// The Elite Four then get Victory Road, which is where the stock game puts it -
// it is the road TO them - as four recolours of the one tileset, tinted to each
// member's type: Sidney violet for Dark, Phoebe near-black and fogged for
// Ghost, Glacia pale blue for Ice, Drake crimson for Dragon. Four dungeons that
// read as one place seen four ways is the right shape for a gauntlet, and it is
// what makes them affordable: they share every metatile and differ only in a
// palette set each.
//
// The modulo does the routing on its own. The Elite Four are dungeons 8 to 11
// and these are theme indices 8 to 11, so nothing here needed changing.
//
// NOTHING WRAPS ANY MORE. There are fourteen themes against fourteen dungeons,
// Wallace has Ever Grande at 12 and Steven has Murky Cave at 13, so the modulo
// below is a bounds guard rather than a routing decision - every dungeon lands
// on the theme written for it. The first arrangement in the project where that
// is true, and the reason the "wraps to another theme's art" gap is closed.
static const struct RogueDungeonTheme *ThemeForFloor(u16 floor)
{
    // IDENTITY, not slot - which dungeon is standing at this depth. Everything a
    // theme owns travels with it: its art, its encounter surface, its species
    // ladder and its weather. See DungeonForSlot.
    u32 dungeon = DungeonForSlot(DungeonIndexOf(floor));

    return &sDungeonThemes[dungeon % DUNGEON_THEME_COUNT];
}

// Rebuilt whenever a floor is generated, from the same seeded RNG, so a given
// floor always has the same encounter table.
//
// EWRAM_DATA explicitly: plain statics land in IWRAM, and IWRAM is by far the
// scarcer region here (~4 KB free against ~35 KB of EWRAM).
// Sized for the LAND slot count because it is the larger of the two; a water
// floor fills only the first NUM_WATER_MONS_ENCOUNTER_SLOTS of it.
EWRAM_DATA static struct WildPokemon sDungeonWildMons[NUM_LAND_MONS_ENCOUNTER_SLOTS] = {0};

// The rod gets a table of its own rather than sharing the one above, for two
// reasons that both end in silence rather than a crash.
//
// TEN SLOTS AGAINST WATER'S FIVE, and the rod reads them in bands - Old Rod
// draws slots 0-1, Good Rod 2-4, Super Rod 5-9. Sharing the buffer would hand a
// Super Rod slots 5-9 of whatever the last surf roll happened to leave there,
// which on a fresh floor is zeroes: species 0, level 0.
//
// AND THE ROTATION STRIDE IS THE SLOT COUNT. Making the shared stride 10 to
// match would put gcd(10, 12) = 2 between stride and window, and the rotation
// would then reach only half its ladder - the exact failure check_safari_pool.py
// exists to catch. Its own rotation keeps the two independent.
EWRAM_DATA static struct WildPokemon sDungeonFishingMons[NUM_FISHING_MONS_ENCOUNTER_SLOTS] = {0};
EWRAM_DATA static struct WildPokemonInfo sDungeonFishingInfo = {0};
EWRAM_DATA static u8 sFishingRotation = 0;

// What the floor's deal was made from, kept so the water branch can re-deal it.
//
// WHY WATER RE-DEALS AND LAND DOES NOT. The engine gives water five encounter
// slots against land's twelve, and the slots are the ceiling on how many species
// can be present AT ONCE - not on how many a floor may offer. TryGenerateWildMon
// asks for the table at the moment of each roll and uses it inside that one
// call, so re-dealing there is atomic: the species, its level and the
// ability-influenced scans (Magnet Pull, Static) all read one consistent deal.
// Rotating between rolls therefore lifts a water floor from five species to the
// whole window, with no extra slots and nothing for the player to notice.
//
// It also fixes the uneven-weight problem rather than working around it. Slot
// weights run 20/20/10/10/..., so a species dealt into slot 0 dominates a floor
// whose table never changes; rotating gives every species in the window its turn
// in the common slots.
//
// WHETHER IT RE-DEALS IS DERIVED, NOT KEYED ON THE AREA: it re-deals exactly
// when `width > slots`, which is the condition under which re-dealing buys
// anything at all. That is behaviour-identical to the old `area == WATER` test
// for every dungeon theme - the ocean and the seafloor run a window of 12 into
// 5 water slots and still re-deal; the six land gyms run 12 into 12 and still
// do not; Drake's 7 and the Elite Four's 8 are narrower than 12 and still do
// not - and it turns the Safari Zone on by itself, because its land window is
// 47 against the same 12 slots.
//
// So "land is left alone" was never about land. It was about 12 == 12.
EWRAM_DATA static const u16 *sWildSpecies = NULL;
EWRAM_DATA static u8 sWildBottom = 0;
EWRAM_DATA static u8 sWildWidth = 0;
EWRAM_DATA static u8 sWildRotation = 0;
EWRAM_DATA static u8 sWildLevel = 0;
EWRAM_DATA static u8 sWildSlots = 0;
EWRAM_DATA static struct WildPokemonInfo sDungeonWildInfo = {0};

// Which branch the table above was built for. Recorded rather than re-derived
// in the hook, so the answer cannot disagree with the table actually in RAM.
EWRAM_DATA static u8 sDungeonWildArea = WILD_AREA_LAND;

// The Safari Zone's table shares every sWild* variable above. It needs no
// "whose table is this" flag of its own: sWildSpecies already names the ladder
// in RAM, so comparing against the ladder the Safari WANTS is self-
// invalidating - a dungeon floor rebuilding puts a theme's pool there, which
// matches neither Safari ladder, and the Safari rebuilds on its next roll.
// Only the floor has to be remembered.
EWRAM_DATA static u16 sSafariBuiltFloor = 0;

// sWildBottom and sWildWidth are u8, and the deepest floor indexes
// sSafariLandSpecies at bottom + width - 1. A ladder past 255 would wrap them
// silently and deal species from the wrong end of the pool.
STATIC_ASSERT(ARRAY_COUNT(sSafariLandSpecies) <= 255, SafariLandLadderFitsU8);
STATIC_ASSERT(ARRAY_COUNT(sSafariWaterSpecies) <= 255, SafariWaterLadderFitsU8);

// A window WIDER than its slot count is what makes the re-deal in
// RogueDungeon_GetWildMonInfo do anything, and it is the whole design of this
// area: 47 live species dealt twelve at a time, re-dealt every roll. Narrow
// either of these to its slot count and the Safari quietly becomes twelve
// species a visit.
STATIC_ASSERT(DUNGEON_SAFARI_LAND_WINDOW > NUM_LAND_MONS_ENCOUNTER_SLOTS,
              SafariLandWindowWiderThanSlots);
STATIC_ASSERT(DUNGEON_SAFARI_WATER_WINDOW > NUM_WATER_MONS_ENCOUNTER_SLOTS,
              SafariWaterWindowWiderThanSlots);

// A pool must reach its own end: bottom tops out at count - window, so a window
// wider than the ladder leaves the deepest floors reading a truncated window.
STATIC_ASSERT(ARRAY_COUNT(sSafariLandSpecies) >= DUNGEON_SAFARI_LAND_WINDOW,
              SafariLandLadderFillsItsWindow);
STATIC_ASSERT(ARRAY_COUNT(sSafariWaterSpecies) >= DUNGEON_SAFARI_WATER_WINDOW,
              SafariWaterLadderFillsItsWindow);

// Runtime dungeon floor generator.
//
// Follows the same contract as GenerateBattlePyramidFloorLayout: the caller has
// already filled sBackupMapData with MAPGRID_UNDEFINED, and we own setting up
// gBackupMapLayout and writing every block of the playfield.
//
// This is deliberately a plain rooms-and-corridors carver rather than anything
// clever - the point of the first pass is to prove the engine integration, so
// bugs are unambiguously in the wiring rather than in the generation.

struct DungeonRoom
{
    u8 x, y, w, h;
};

// The prepared floor. Held rather than recomputed because object-event
// templates are loaded before the map is generated, so trainer placement and
// block painting are two passes over the same seeded layout.
//
// Not saved: it is entirely derived from VAR_ROGUE_DUNGEON_SEED.
EWRAM_DATA static struct DungeonRoom sRooms[DUNGEON_MAX_ROOMS] = {0};
EWRAM_DATA static u8 sRoomCount = 0;
EWRAM_DATA static u8 sStairsX = 0;
EWRAM_DATA static u8 sStairsY = 0;
EWRAM_DATA static u16 sStairsMetatile = 0;
EWRAM_DATA static u8 sSpawnX = 0;
EWRAM_DATA static u8 sSpawnY = 0;
EWRAM_DATA static bool8 sFloorPrepared = FALSE;

// Grass blobs, in cell coordinates. Stored as centres and radii rather than a
// grid, which is a handful of bytes instead of a 576-cell map.
//
// This budget has to clear the room cap, because the patch loop runs ONCE PER
// ROOM and stops at the ceiling rather than spreading what it has. At twelve
// patches against ten rooms drawing 0-2 each it saturated on 23% of floors,
// and a saturated floor is not merely thinner - the loop walks rooms in index
// order, so every missing patch comes off the LAST rooms and the grass quietly
// migrates to the start of the chain. Sixteen clears ten rooms outright.
// Costs 16 bytes of EWRAM across the four arrays below.
#define DUNGEON_MAX_GRASS_PATCHES 16
EWRAM_DATA static u8 sGrassPatchX[DUNGEON_MAX_GRASS_PATCHES] = {0};
EWRAM_DATA static u8 sGrassPatchY[DUNGEON_MAX_GRASS_PATCHES] = {0};
EWRAM_DATA static u8 sGrassPatchRadius[DUNGEON_MAX_GRASS_PATCHES] = {0};
EWRAM_DATA static bool8 sGrassPatchLong[DUNGEON_MAX_GRASS_PATCHES] = {0};
EWRAM_DATA static u8 sGrassPatchCount = 0;

// Cell openness for the woods painter. Held rather than stack-allocated because
// 576 bytes is a lot of GBA stack.
EWRAM_DATA static u8 sWoodsOpen[DUNGEON_CELLS_H][DUNGEON_CELLS_W] = {0};

// Trainers for this floor. Indexed by object event localId - 1, mirroring how
// the Battle Pyramid maps a talked-to object back to its opponent.
EWRAM_DATA static u16 sTrainerIds[DUNGEON_MAX_TRAINERS] = {0};
EWRAM_DATA static u8 sTrainerX[DUNGEON_MAX_TRAINERS] = {0};
EWRAM_DATA static u8 sTrainerY[DUNGEON_MAX_TRAINERS] = {0};
EWRAM_DATA static u16 sTrainerGfx[DUNGEON_MAX_TRAINERS] = {0};
EWRAM_DATA static u8 sTrainerCount = 0;

// Item balls for this floor, indexed the same way. Contents are rolled at
// PREPARE time and held rather than rolled when the ball is opened: preparing
// happens on every load of a floor, including load-from-save with the stored
// seed, so the same ball yields the same item without the pickup path having to
// reseed anything.
//
// 40 bytes. The templates the balls occupy cost nothing on top - see
// DUNGEON_MAX_ITEMS.
EWRAM_DATA static u16 sItemIds[DUNGEON_MAX_ITEMS] = {0};
EWRAM_DATA static u8 sItemQty[DUNGEON_MAX_ITEMS] = {0};
EWRAM_DATA static u8 sItemX[DUNGEON_MAX_ITEMS] = {0};
EWRAM_DATA static u8 sItemY[DUNGEON_MAX_ITEMS] = {0};
EWRAM_DATA static u8 sItemCount = 0;

// Hidden items, and the events header that points at them.
//
// These are held in their FINAL FORM - real struct BgEvents, ready for the
// engine to read - rather than as coordinates a builder turns into events
// later, because the engine reads them straight out of the array through
// gMapHeader.events and there is no later.
//
// sDungeonEvents is a copy of the map's ROM header with only the bg fields
// swapped, so warps, coord events and the object event count all keep saying
// what map.json said.
// Berry trees for this floor. The berry is held rather than the tree planted at
// prepare time, because planting writes the SAVE BLOCK and preparing happens on
// every load - see PlantFloorBerryTrees.
EWRAM_DATA static u16 sBerryItems[DUNGEON_MAX_BERRIES] = {0};
EWRAM_DATA static u8 sBerryX[DUNGEON_MAX_BERRIES] = {0};
EWRAM_DATA static u8 sRockX[DUNGEON_MAX_ROCKS] = {0};
EWRAM_DATA static u8 sRockY[DUNGEON_MAX_ROCKS] = {0};
EWRAM_DATA static u8 sRockCount = 0;
// The floor's event, if it rolled one. sEventIndex is an index into
// sFloorEvents rather than a copy of the entry, so the table stays the only
// place that knows what an event IS. 4 bytes total.
EWRAM_DATA static u8 sEventX = 0;
EWRAM_DATA static u8 sEventY = 0;
EWRAM_DATA static u8 sEventCount = 0;
EWRAM_DATA static u8 sEventIndex = 0;
// The prop's tile, derived from the event's rather than rolled - see the note in
// PlaceEvents on why it takes no draw of its own. 2 bytes.
EWRAM_DATA static u8 sEventPropX = 0;
EWRAM_DATA static u8 sEventPropY = 0;
// Rolled at prepare time for every event, whether or not the one picked reads
// them - so the number of draws taken from the seeded stream does not depend on
// WHICH event came up. sEventSpecies is also what the injured Pokemon's overworld
// sprite is built from, so it has to be decided before the templates are written,
// not when the player talks to it.
EWRAM_DATA static u16 sEventSpecies = SPECIES_NONE;
EWRAM_DATA static u8 sEventAmbush = 0;
EWRAM_DATA static u8 sBerryY[DUNGEON_MAX_BERRIES] = {0};
EWRAM_DATA static u8 sBerryCount = 0;

EWRAM_DATA static struct BgEvent sHiddenItems[DUNGEON_MAX_HIDDEN] = {0};
EWRAM_DATA static struct MapEvents sDungeonEvents = {0};
EWRAM_DATA static u8 sHiddenCount = 0;

// A wrong id here would clear or read a VANILLA hidden item's flag. Checked at
// compile time because the two constants live in different headers and the
// relationship between them is arithmetic nobody would re-derive by eye.
STATIC_ASSERT(FLAG_HIDDEN_ITEMS_START + DUNGEON_HIDDEN_FIRST_ID == FLAG_UNUSED_0x264,
              RogueHiddenItemIdsMustStartAtTheFirstFreeFlag);

// A bg event stores its item in an ELEVEN BIT field, so an item id past 2047
// would be silently truncated into a different item rather than failing. There
// is plenty of room today - the highest id is in the 700s - but the bag grows
// every expansion release and nothing else would notice the day it does not.
STATIC_ASSERT(ITEMS_COUNT <= (1 << 11), RogueHiddenItemIdsMustFitElevenBits);

// Berry tree ids index a fixed 128-entry save block array that vanilla already
// names 0..89 of. Running off the end would silently write over another map's
// tree - or past the array - so the headroom is checked rather than counted by
// hand.
STATIC_ASSERT(DUNGEON_BERRY_FIRST_TREE_ID + DUNGEON_MAX_BERRIES <= BERRY_TREES_COUNT,
              RogueBerryTreeIdsMustFitTheSaveBlockArray);

// berryYield is a five-bit field, so a yield past 31 would wrap into a small
// number and read as a stingy tree rather than as a mistake.
STATIC_ASSERT(DUNGEON_BERRY_YIELD <= 31, RogueBerryYieldMustFitFiveBits);

// Local PRNG. Generation must not consume or perturb the global RNG - if it
// did, the floor would depend on how many steps the player had taken, and the
// same seed would stop reproducing the same floor.
EWRAM_DATA static u32 sDungeonRngState = 0;

static void SeedDungeonRng(u16 seed)
{
    sDungeonRngState = ISO_RANDOMIZE1(seed);
}

// High bits only; the low bits of an LCG have short periods.
static u16 DungeonRandom(void)
{
    sDungeonRngState = ISO_RANDOMIZE1(sDungeonRngState);
    return sDungeonRngState >> 16;
}

// How many things each room already holds, this floor. Reset in PrepareFloor.
EWRAM_DATA static u8 sRoomUse[DUNGEON_MAX_ROOMS] = {0};

// Picks a room to put something in, PREFERRING ONE THAT IS STILL EMPTY.
//
// Placement used to be a flat `DungeonRandom() % sRoomCount`, which is uniform
// WITH REPLACEMENT - so objects landed on rooms that already had one and the
// floor saturated long before it was full. Ten rooms and thirteen objects still
// left 2.5 rooms holding nothing at all, and 5.3 holding nothing a player would
// cross a room for. Reported from play as rooms feeling sparse "besides rocks
// and berries", and tools/rogue/room_density.py prints the arithmetic.
//
// EXACTLY ONE DRAW, like the modulo it replaces. That matters more than it
// looks: every placer shares the seeded stream, so a helper that sometimes drew
// twice - the obvious retry loop - would make the number of draws depend on how
// full the floor happened to be, and everything placed afterwards would move
// with it. Choosing uniformly among the least-used rooms gets the same effect
// with a fixed cost and stays reproducible.
//
// ONE COUNTER FOR EVERY KIND of object, deliberately. A room holding only a rock
// still reads as empty to a player, but it is a worse place to put the next
// thing than a room holding nothing, and shared counting is what expresses that.
// Placement order then does the rest: trainers and item balls run first, so they
// take empty rooms, and the scenery fills what is left.
// firstRoom exists for the TRAINERS, which have always skipped room 0 - that is
// the room the player spawns in, and a trainer there fights them before they can
// move. Everything else passes 0.
static u32 PickRoomLeastUsedFrom(u32 firstRoom)
{
    u32 i, fewest = 0xFF, candidates = 0, pick;

    if (sRoomCount <= firstRoom)
        return firstRoom < sRoomCount ? firstRoom : 0;

    for (i = firstRoom; i < sRoomCount; i++)
    {
        if (sRoomUse[i] < fewest)
            fewest = sRoomUse[i];
    }

    for (i = firstRoom; i < sRoomCount; i++)
    {
        if (sRoomUse[i] == fewest)
            candidates++;
    }

    pick = DungeonRandom() % candidates;

    for (i = firstRoom; i < sRoomCount; i++)
    {
        if (sRoomUse[i] == fewest && pick-- == 0)
            return i;
    }

    return firstRoom;
}

// Called when a placement actually lands. NOT called when one is skipped for
// landing on the stairs or on another object, so a room does not get credit for
// something that was never put in it.
static void NoteRoomUsed(u32 room)
{
    if (room < DUNGEON_MAX_ROOMS && sRoomUse[room] != 0xFF)
        sRoomUse[room]++;
}

static const u8 sText_DungeonFloorPrefix[] = _("DUNGEON B");
static const u8 sText_DungeonFloorSuffix[] = _("F");

// Fills the map name popup. The floor counter is 0-based; the player sees 1F
// on the first floor.
void RogueDungeon_GetFloorName(u8 *dest)
{
    u8 *ptr = StringCopy(dest, sText_DungeonFloorPrefix);

    ptr = ConvertIntToDecimalStringN(ptr, VarGet(VAR_ROGUE_DUNGEON_FLOOR) + 1,
                                     STR_CONV_MODE_LEFT_ALIGN, 4);
    StringCopy(ptr, sText_DungeonFloorSuffix);
}

#if ROGUE_DEBUG_OBJECT_CENSUS

// Peaks rather than instantaneous values - see ROGUE_DEBUG_OBJECT_CENSUS.
EWRAM_DATA static u8 sCensusLivePeak = 0;
EWRAM_DATA static u8 sCensusWantedPeak = 0;
EWRAM_DATA static u16 sCensusRefusals = 0;

void RogueDungeon_Debug_ResetObjectCensus(void)
{
    sCensusLivePeak = 0;
    sCensusWantedPeak = 0;
    sCensusRefusals = 0;
}

// Called where the engine declined to spawn a template. Counts it ONLY if the
// object is not already live.
//
// THE FIRST VERSION OF THIS COUNTED EVERY DECLINE AND WAS USELESS, reporting
// refusals in the hundreds on floors that had dropped nothing at all.
// GetAvailableObjectEventId returns the same value for "no slot free" and for
// "already loaded", and since TrySpawnObjectEvents re-attempts every in-range
// template on every camera update, the second case fires once per already-live
// object per pass. The number it produced was a measure of how long the player
// had been walking.
//
// Re-deriving the answer here rather than trusting the sentinel is the fix: if
// this template already owns a slot, nothing was dropped.
void RogueDungeon_Debug_NoteObjectSpawnOutcome(u16 localId, u8 mapNum, u8 mapGroup)
{
    u32 i;

    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        if (gObjectEvents[i].active
         && gObjectEvents[i].localId == localId
         && gObjectEvents[i].mapNum == mapNum
         && gObjectEvents[i].mapGroup == mapGroup)
            return;
    }

    if (sCensusRefusals != 0xFFFF)
        sCensusRefusals++;
}

// The other way an object silently does not appear: the SPRITE table is full
// rather than the object event table. Distinct from the above and worth its own
// count - it is the one that bites on maps that are not caves, where
// OW_OBJECT_VANILLA_SHADOWS gives every object a second sprite.
void RogueDungeon_Debug_NoteSpriteExhausted(void)
{
    if (sCensusRefusals != 0xFFFF)
        sCensusRefusals++;
}

// Called once per spawn pass, with the number of templates the engine WANTED to
// have live. Counting live objects here rather than on a timer means the sample
// lands at the moment the count changes, which is the moment worth catching.
void RogueDungeon_Debug_NoteSpawnPass(u32 wanted)
{
    u32 i, live = 0;

    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        if (gObjectEvents[i].active)
            live++;
    }

    if (live > sCensusLivePeak)
        sCensusLivePeak = live;
    if (wanted > sCensusWantedPeak)
        sCensusWantedPeak = wanted;
}

static const u8 sText_CensusPlaced[]  = _("placed T{STR_VAR_1}");
static const u8 sText_CensusItems[]   = _(" I");
static const u8 sText_CensusBerries[] = _(" B");
static const u8 sText_CensusRocks[]   = _(" R");
static const u8 sText_CensusEvents[]  = _(" E");
static const u8 sText_CensusHidden[]  = _(" h");
static const u8 sText_CensusPeak[]    = _("\npeak live ");
static const u8 sText_CensusOf[]      = _(" / ");
static const u8 sText_CensusWanted[]  = _("  wanted ");
static const u8 sText_CensusRefused[] = _("\nREFUSED ");
static const u8 sText_CensusOk[]      = _("\nrefused 0 - nothing was dropped");

void RogueDungeon_GetDebugObjectCensus(u8 *dest)
{
    u8 *ptr;

    ptr = StringCopy(dest, sText_CensusPlaced);
    ptr = ConvertIntToDecimalStringN(ptr, sTrainerCount, STR_CONV_MODE_LEFT_ALIGN, 2);
    ptr = StringCopy(ptr, sText_CensusItems);
    ptr = ConvertIntToDecimalStringN(ptr, sItemCount, STR_CONV_MODE_LEFT_ALIGN, 2);
    ptr = StringCopy(ptr, sText_CensusBerries);
    ptr = ConvertIntToDecimalStringN(ptr, sBerryCount, STR_CONV_MODE_LEFT_ALIGN, 2);
    ptr = StringCopy(ptr, sText_CensusRocks);
    ptr = ConvertIntToDecimalStringN(ptr, sRockCount, STR_CONV_MODE_LEFT_ALIGN, 2);
    ptr = StringCopy(ptr, sText_CensusEvents);
    ptr = ConvertIntToDecimalStringN(ptr, sEventCount, STR_CONV_MODE_LEFT_ALIGN, 2);
    ptr = StringCopy(ptr, sText_CensusHidden);
    ptr = ConvertIntToDecimalStringN(ptr, sHiddenCount, STR_CONV_MODE_LEFT_ALIGN, 2);

    // Against OBJECT_EVENTS_COUNT, so the headroom is on screen rather than
    // being something the reader has to remember.
    ptr = StringCopy(ptr, sText_CensusPeak);
    ptr = ConvertIntToDecimalStringN(ptr, sCensusLivePeak, STR_CONV_MODE_LEFT_ALIGN, 2);
    ptr = StringCopy(ptr, sText_CensusOf);
    ptr = ConvertIntToDecimalStringN(ptr, OBJECT_EVENTS_COUNT, STR_CONV_MODE_LEFT_ALIGN, 2);
    ptr = StringCopy(ptr, sText_CensusWanted);
    ptr = ConvertIntToDecimalStringN(ptr, sCensusWantedPeak, STR_CONV_MODE_LEFT_ALIGN, 2);

    // THE LINE TO READ. A refusal is an object that silently did not appear.
    if (sCensusRefusals != 0)
    {
        ptr = StringCopy(ptr, sText_CensusRefused);
        ConvertIntToDecimalStringN(ptr, sCensusRefusals, STR_CONV_MODE_LEFT_ALIGN, 5);
    }
    else
    {
        StringCopy(ptr, sText_CensusOk);
    }
}

#else

void RogueDungeon_Debug_ResetObjectCensus(void) {}
void RogueDungeon_Debug_NoteObjectSpawnOutcome(u16 localId, u8 mapNum, u8 mapGroup) {}
void RogueDungeon_Debug_NoteSpriteExhausted(void) {}
void RogueDungeon_Debug_NoteSpawnPass(u32 wanted) {}

static const u8 sText_CensusOff[] = _("ROGUE_DEBUG_OBJECT_CENSUS is FALSE");

void RogueDungeon_GetDebugObjectCensus(u8 *dest)
{
    StringCopy(dest, sText_CensusOff);
}

#endif // ROGUE_DEBUG_OBJECT_CENSUS

static const u8 sText_DebugDungeon[] = _("D");
static const u8 sText_DebugFloorIn[] = _("F");
static const u8 sText_DebugLevel[]   = _(" Lv");
static const u8 sText_DebugBoss[]    = _(" BOSS");
static const u8 sText_DebugMini[]    = _(" mini");

// One line describing what a floor holds, for the debug warp tool: which dungeon
// and which floor of it, the encounter level, and whether it is an arena.
//
// The dungeon/floor pair is the useful part. It is the whole reason to have this
// rather than a bare number, because it is what shows the segmentation lining up
// - floor 85 reading D9 F5 BOSS says the Elite Four's half-length dungeons
// landed where they should without anyone doing the arithmetic by hand.
void RogueDungeon_GetDebugFloorInfo(u16 floor, u8 *dest)
{
    u8 *ptr = StringCopy(dest, sText_DebugDungeon);

    ptr = ConvertIntToDecimalStringN(ptr, DungeonIndexOf(floor) + 1,
                                     STR_CONV_MODE_LEFT_ALIGN, 2);
    ptr = StringCopy(ptr, sText_DebugFloorIn);
    ptr = ConvertIntToDecimalStringN(ptr, DungeonFloorWithin(floor) + 1,
                                     STR_CONV_MODE_LEFT_ALIGN, 2);
    ptr = StringCopy(ptr, sText_DebugLevel);
    ptr = ConvertIntToDecimalStringN(ptr, FloorTargetLevel(floor),
                                     STR_CONV_MODE_LEFT_ALIGN, 3);

    if (IsDungeonBossFloor(floor))
        StringCopy(ptr, sText_DebugBoss);
    else if (IsMiniBossFloor(floor))
        StringCopy(ptr, sText_DebugMini);
}

// Called at the end of NewGameInitData, which must come after InitEventData -
// that clears every flag, so setting these earlier would be undone.
//
// A run starts with the progression gates already open: there is no overworld
// to earn them in, and without badges high-level Pokemon can disobey.
void RogueDungeon_ApplyNewGameUnlocks(void)
{
    u32 flag;

    // Contiguous in the system flag block.
    for (flag = FLAG_BADGE01_GET; flag <= FLAG_BADGE08_GET; flag++)
        FlagSet(flag);

    FlagSet(FLAG_SYS_POKEMON_GET);  // party entry in the start menu
    FlagSet(FLAG_SYS_POKEDEX_GET);  // dex entry in the start menu

    // Not just FLAG_SYS_NATIONAL_DEX - the dex also checks a magic value and
    // VAR_NATIONAL_DEX, and IsNationalPokedexEnabled requires all three.
    EnableNationalPokedex();

    FlagSet(FLAG_SYS_B_DASH);             // running actually works
    FlagSet(FLAG_RECEIVED_RUNNING_SHOES); // event bookkeeping to match
}

// A run begins with two starters the player picks, not a handed-out team. The
// multichoice returns an index into sRogueDungeonStarters.
void RogueDungeon_GiveChosenStarter(void)
{
    u32 index = gSpecialVar_Result;
    u32 slot = CalculatePlayerPartyCount();
    struct Pokemon *mon;
    u32 i, fallback, freeSlot, statusSlot;
    bool32 known, hasOwnTypeAttack;
    enum Type type1, type2;

    if (index >= ARRAY_COUNT(sRogueDungeonStarters) || slot >= PARTY_SIZE)
        return;

    mon = &gParties[B_TRAINER_PLAYER][slot];
    CreateRandomMonWithIVs(mon, sRogueDungeonStarters[index].species,
                           DUNGEON_STARTER_LEVEL, MAX_PER_STAT_IVS);

    // The elemental attack is a FALLBACK, not a guarantee to be forced in. With
    // Gen 9 learnsets at level 10, 23 of the 30 picks already know it, and
    // adding it again both wasted a slot and - once all four were full, which
    // an old clamp papered over - overwrote a real move with a duplicate.
    // Squirtle lost Rapid Spin for a second Water Gun.
    //
    // So, in order: skip it if the species already knows it; otherwise take a
    // free slot; otherwise displace a status move, but ONLY for a pick that
    // would be left with no damaging move of its own type at all.
    //
    // That last case is not hypothetical and is why the rule is not simply
    // "never displace". Clefairy learns Disarming Voice at level 1, but enough
    // other level-1 moves follow it that the initial-moveset window keeps
    // Charm, Copycat, Stored Power and Encore instead - three status moves and
    // a 20 BP Psychic one that scales off boosts a level 10 does not have.
    // With no free slot the fallback could never land, so the one Fairy-type
    // that fights with a Fairy move arrived unable to use it. Every other pick
    // in the table reaches level 10 holding a damaging move of its own type
    // and so never reaches this branch. check_starter_moves.py grades all of
    // it; run it after touching this table, DUNGEON_STARTER_LEVEL or
    // P_LVL_UP_LEARNSETS.
    fallback = sRogueDungeonStarters[index].move;
    freeSlot = statusSlot = MAX_MON_MOVES;
    known = hasOwnTypeAttack = FALSE;
    type1 = GetSpeciesType(sRogueDungeonStarters[index].species, 0);
    type2 = GetSpeciesType(sRogueDungeonStarters[index].species, 1);

    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        u32 move = GetMonData(mon, MON_DATA_MOVE1 + i, NULL);

        if (move == MOVE_NONE)
        {
            if (freeSlot == MAX_MON_MOVES)
                freeSlot = i;
        }
        else if (move == fallback)
        {
            known = TRUE;
        }
        else if (GetMoveCategory(move) == DAMAGE_CATEGORY_STATUS)
        {
            if (statusSlot == MAX_MON_MOVES)
                statusSlot = i;
        }
        else if (GetMoveType(move) == type1 || GetMoveType(move) == type2)
        {
            hasOwnTypeAttack = TRUE;
        }
    }

    if (!known)
    {
        if (freeSlot != MAX_MON_MOVES)
            SetMonMoveSlot(mon, fallback, freeSlot);
        else if (!hasOwnTypeAttack && statusSlot != MAX_MON_MOVES)
            SetMonMoveSlot(mon, fallback, statusSlot);
    }

    CalculateMonStats(mon);
    CalculatePlayerPartyCount();
}

// The boss ace on offer. Held between the two specials below rather than
// recomputed, so the name shown in the prompt and the mon actually granted
// cannot disagree.
EWRAM_DATA static u16 sBossAceSpecies = SPECIES_NONE;
EWRAM_DATA static u8 sBossAceLevel = 0;
EWRAM_DATA static u8 sBossAceSlot = 0;

// The boss's own object event. An arena places exactly one trainer -
// PrepareArenaFloor sets sTrainerCount to 1 and fills sTrainerIds[0] - and
// trainer i takes local id i + 1, so the boss is always this one.
#define DUNGEON_BOSS_LOCAL_ID 1

// specialvar target. Takes the boss off the floor and stands their ace where
// they were, in place and on the same tile. Returns FALSE where there is no
// boss to take, which is every mini boss floor: those share this whole
// post-battle script, and an anonymous grunt cannot vanish because nothing is
// left behind to make the vanishing mean anything.
//
// THE OBJECT THIS LEAVES BEHIND IS THE ONLY EXIT FROM THE FLOOR, and that is
// the constraint the whole function is built around. RogueDungeon_OnBossDefeated
// returns early on exactly these floors and draws no stairs, so a boss arena is
// left by talking, not by walking. Removing the boss and putting nothing back
// seals the run permanently - which is why the ace inherits the boss's own
// script rather than getting one of its own.
//
// Rewrites the TEMPLATE and not just the live object, the same seam
// RogueDungeon_HideTakenFloorItem and RogueDungeon_HideMinedRock go through: a
// reload rebuilds the floor from the save block, so a change made only to the
// sprite on screen would put the boss back the first time the player saved on a
// cleared arena and loaded.
u16 RogueDungeon_AbandonBossAce(void)
{
    struct ObjectEventTemplate *templates = gSaveBlock1Ptr->objectEventTemplates;
    struct ObjectEventTemplate *boss = &templates[DUNGEON_BOSS_LOCAL_ID - 1];
    u16 trainerId = sTrainerIds[0];
    u8 size = GetTrainerPartySizeFromId(trainerId);
    const struct TrainerMon *party = GetTrainerPartyFromId(trainerId);

    if (!IsDungeonBossFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR)))
        return FALSE;

    if (sTrainerCount == 0 || size == 0 || party == NULL)
        return FALSE;

    // The vanish message's only variable. Deliberately buffered here rather than
    // in the script, so the one function that knows which trainer stood here is
    // the one that names them.
    StringCopy(gStringVar1, GetTrainerNameFromId(trainerId));

    // The ace is the last party member - see PrepareBossAceOffer, which picks
    // the same slot for the same reason. Read straight from the party rather
    // than from sBossAceSpecies, because that one is only set once the OFFER is
    // prepared and the Pokemon has to be standing there even on the paths where
    // no offer is made: a full party, and Steven.
    //
    // OBJ_EVENT_MON is a BIT (1 << 14) added to a species id, not a table index
    // - the same construction the rest stop's Unown and the injured-Pokemon
    // event use. OW_POKEMON_OBJECT_EVENTS is already TRUE so the sprites are in
    // the build already.
    boss->graphicsId = OBJ_EVENT_MON + party[size - 1].species;

    // TRAINER_TYPE_NONE and no sight range, or the abandoned Pokemon would
    // spot the player and try to start the fight that just ended.
    boss->movementType = MOVEMENT_TYPE_FACE_DOWN;
    boss->trainerType = TRAINER_TYPE_NONE;
    boss->trainerRange_berryTreeId = 0;
    boss->script = RogueDungeonFloor_EventScript_BossDone;

    RemoveObjectEventByLocalIdAndMap(DUNGEON_BOSS_LOCAL_ID,
                                     gSaveBlock1Ptr->location.mapNum,
                                     gSaveBlock1Ptr->location.mapGroup);
    TrySpawnObjectEventTemplate(boss,
                                gSaveBlock1Ptr->location.mapNum,
                                gSaveBlock1Ptr->location.mapGroup, 0, 0);
    return TRUE;
}

// specialvar target. Returns one of the ROGUE_ACE_* results and buffers the
// TRAINER name into gStringVar1 and the species name into gStringVar2, for both
// the offer and the party-full refusal.
//
// BOTH are re-buffered here even though RogueDungeon_AbandonBossAce already put
// the trainer name in gStringVar1: RogueDungeon_GiveBossTM runs between the two
// and its CopyItemName overwrites that slot with the item name.
//
// The ace is the last party member: the stock data orders a trainer's team
// weakest to strongest, so the signature Pokemon is always last.
u16 RogueDungeon_PrepareBossAceOffer(void)
{
    u16 trainerId = sTrainerIds[0];
    u8 size = GetTrainerPartySizeFromId(trainerId);
    const struct TrainerMon *party = GetTrainerPartyFromId(trainerId);

    sBossAceSpecies = SPECIES_NONE;

    // Only a gym leader's ace is on offer. Mini bosses run the same post-battle
    // script - RogueDungeon_IsBossFloor covers both - and an Aqua grunt handing
    // over its Poochyena was never the intent.
    if (!IsDungeonBossFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR)))
        return ROGUE_ACE_NONE;

    // Nor Steven's. The run resets the moment the player walks away from him, so
    // handing over a Metagross here would announce a prize and take it back in
    // the same breath.
    if (RogueDungeon_IsRunCompleteFloor())
        return ROGUE_ACE_NONE;

    if (sTrainerCount == 0 || size == 0 || party == NULL)
        return ROGUE_ACE_NONE;

    sBossAceSlot = size - 1;
    sBossAceLevel = party[sBossAceSlot].lvl;

    // Buffered before the party check, so the refusal can name what was missed.
    // Leaving it until after meant the full-party path showed whatever species
    // name a previous message happened to leave in the buffer.
    StringCopy(gStringVar1, GetTrainerNameFromId(trainerId));
    StringCopy(gStringVar2, GetSpeciesName(party[sBossAceSlot].species));

    // sBossAceSpecies stays SPECIES_NONE, so RogueDungeon_GiveBossAce refuses
    // even if something did reach it.
    if (CalculatePlayerPartyCount() >= PARTY_SIZE)
        return ROGUE_ACE_PARTY_FULL;

    sBossAceSpecies = party[sBossAceSlot].species;
    return ROGUE_ACE_OFFER;
}

// Stamps the boss onto the adopted ace as its original trainer, so the summary
// screen says who it belonged to rather than crediting the player with a
// Pokemon they watched someone else raise.
//
// THE OT ID IS INVENTED, AND IT HAS TO BE. struct Trainer carries a name, a
// class, a pic and a gender, and NO id number - the stock game never needs one
// because no stock Pokemon is ever handed over by a trainer. So the only
// property worth designing for is stability: Roxanne's Nosepass must read as
// coming from the same Roxanne in every run, which means a pure function of the
// trainer id and nothing drawn from any random stream.
//
// SAFE ONLY BECAUSE THE RUN GRANTS ALL EIGHT BADGES. A foreign OT id is what
// makes a Pokemon disobey, and RogueDungeon_ApplyNewGameUnlocks sets
// FLAG_BADGE01_GET through FLAG_BADGE08_GET at new game for exactly this
// reason - see the comment there. Drop that and every adopted ace starts
// ignoring orders. Nothing in the experience path reads OT id, so there is no
// traded-experience interaction to weigh.
// THE OT ID MUST BE CHOSEN BEFORE THE POKEMON IS BUILT, NOT WRITTEN AFTERWARDS.
//
// This shipped as a BAD EGG. The substructs of a box mon are encrypted with
// personality ^ otId, and MON_DATA_OT_ID is one of the fields SetBoxMonData
// handles in its UNENCRYPTED branch - it writes boxMon->otId and does not
// decrypt or re-encrypt anything. So changing the id after creation leaves every
// substruct encrypted under the old key while every later read uses the new one:
// the next GetMonData decrypts garbage, the checksum fails, and the engine marks
// the Pokemon isBadEgg. CalculateMonStats at the end of the gift did exactly
// that, one line later.
//
// Nothing about it looked wrong. Every write went through SetMonData, the
// shininess was carefully preserved, and the mon was corrupt before the script
// printed its congratulations.
static u32 BossAceOriginalTrainerId(u16 trainerId)
{
    // A golden-ratio mix, so trainer ids that sit next to each other in the
    // table do not produce OT ids that look related.
    u32 otId = trainerId * 0x9E3779B1u;
    // The player's own id, assembled from the four bytes the save keeps it in.
    u32 playerId = gSaveBlock2Ptr->playerTrainerId[0]
                 | (gSaveBlock2Ptr->playerTrainerId[1] << 8)
                 | (gSaveBlock2Ptr->playerTrainerId[2] << 16)
                 | (gSaveBlock2Ptr->playerTrainerId[3] << 24);

    otId ^= otId >> 16;

    // A collision would make the ace read as the player's own, silently undoing
    // the whole point. One in four billion, and one comparison to rule out.
    if (otId == playerId)
        otId++;

    return otId;
}

// The NAME and GENDER are safe to write after creation - neither is part of the
// encryption key, which is only personality and otId.
static void SetBossAceTrainerName(struct Pokemon *mon, u16 trainerId)
{
    u8 gender = GetTrainerStructFromId(trainerId)->gender;

    SetMonData(mon, MON_DATA_OT_NAME, GetTrainerNameFromId(trainerId));
    SetMonData(mon, MON_DATA_OT_GENDER, &gender);
}

// Grants the ace at the level the boss ran it, with the same moveset, so it
// arrives as the thing that just beat you rather than a blank slate.
void RogueDungeon_GiveBossAce(void)
{
    const struct TrainerMon *party = GetTrainerPartyFromId(sTrainerIds[0]);
    u32 slot = CalculatePlayerPartyCount();
    struct Pokemon *mon;
    u32 i;

    if (sBossAceSpecies == SPECIES_NONE || slot >= PARTY_SIZE || party == NULL)
        return;

    mon = &gParties[B_TRAINER_PLAYER][slot];

    // CreateMonWithIVs rather than CreateRandomMonWithIVs, purely so the OT id
    // can be handed in at creation - see BossAceOriginalTrainerId. The moveset
    // is set below from the boss's own party, so the initial moveset that
    // CreateRandomMonWithIVs would have added is not wanted either.
    CreateMonWithIVs(mon, sBossAceSpecies, sBossAceLevel, Random32(),
                     OTID_STRUCT_PRESET(BossAceOriginalTrainerId(sTrainerIds[0])),
                     MAX_PER_STAT_IVS);
    GiveMonInitialMoveset(mon);

    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        if (party[sBossAceSlot].moves[i] != MOVE_NONE)
            SetMonMoveSlot(mon, party[sBossAceSlot].moves[i], i);
    }

    if (party[sBossAceSlot].heldItem != ITEM_NONE)
    {
        u16 item = party[sBossAceSlot].heldItem;

        SetMonData(mon, MON_DATA_HELD_ITEM, &item);
    }

    SetBossAceTrainerName(mon, sTrainerIds[0]);

    CalculateMonStats(mon);
    CalculatePlayerPartyCount();
    sBossAceSpecies = SPECIES_NONE;
}

// A loss ends the run. Rather than the vanilla respawn at the last Pokemon
// Center, the floor counter and party are wiped and the run state is set back
// to needing starters, so the next dungeon entry starts over from the top.
// Wipes the run back to its starting state without moving the player. Shared by
// the whiteout path and by finishing the run, which differ only in how they say
// so and in how they get back to floor one - a loss is handled from C and warps
// itself, a win is handled from the boss script and uses the warp command.
// Credits a finished run. Called from the boss script's run-complete branch
// only, and pointedly NOT from inside ResetRun below - that is shared with the
// whiteout path, and a loss crediting a win is exactly the bug this counter
// exists to make visible.
//
// Runs before ResetRun, because ResetRun puts the floor counter back to 0 and
// IsRunCompleteFloor would stop being true.
void RogueDungeon_OnRunCompleted(void)
{
    u32 completed = VarGet(VAR_ROGUE_RUNS_COMPLETED);

    FlagSet(FLAG_ROGUE_RUN_COMPLETED);

    // A var is 16 bits and there is no sensible behaviour past the top, so it
    // saturates rather than wrapping to zero - a player with 65535 wins should
    // not be told they have none.
    if (completed < 0xFFFF)
        VarSet(VAR_ROGUE_RUNS_COMPLETED, completed + 1);

    // The other writer of the best floor, and the only one that is not a loss.
    // Winning never passes through the whiteout path, so without this a player
    // who clears the run and never dies that deep again is shown a record short
    // of the game they actually finished.
    if (DUNGEON_TOTAL_FLOORS > VarGet(VAR_ROGUE_BEST_FLOOR))
        VarSet(VAR_ROGUE_BEST_FLOOR, DUNGEON_TOTAL_FLOORS);
}

// Rolls the dungeon order for the run that is about to start.
//
// CALLED FROM ResetRun, and that placement is the whole design. ResetRun runs
// before the warp on the whiteout path and before the warp on the run-complete
// path, so floor one is always GENERATED with the order it will be played under.
// Rolling it from the starter script instead would be too late: the player warps
// in, the template loader calls PrepareFloor, PrepareFloor calls ThemeForFloor -
// and only then does the frame table get a turn. The floor would be painted as
// one theme and described as another, and every wild encounter on it would read
// the wrong ladder.
//
// It also means an in-progress save keeps vanilla order for the rest of its run
// and shuffles from the next one, which is the graceful way for this to arrive.
static void RollDungeonOrder(void)
{
    u32 order;

    // Vanilla until the run has been cleared once, and after that only while the
    // player leaves the toggle on. Note ResetRun is called AFTER
    // RogueDungeon_OnRunCompleted on the winning path, so the run that clears the
    // game is also the one that unlocks the shuffle for its successor.
    if (!FlagGet(FLAG_ROGUE_RUN_COMPLETED) || FlagGet(FLAG_ROGUE_VANILLA_ORDER))
    {
        VarSet(VAR_ROGUE_RUN_ORDER, 0);
        return;
    }

    // THE GLOBAL RNG, deliberately, and this is the one place in the file that
    // wants it. Floor generation uses a local LCG so that a layout cannot depend
    // on the player's step count; a run's dungeon order is rolled exactly once
    // and being unpredictable is the entire point of it.
    order = Random() & ((1 << DUNGEON_GYM_BANDS) - 1);
    order |= (Random() % ARRAY_COUNT(sE4Orders)) << DUNGEON_GYM_BANDS;

    // Zero is a legitimate roll - every band unswapped and Elite Four order 0 -
    // and it lands on the same branch as "not shuffling" in DungeonForSlot,
    // because it describes the same run. 1 in 384.
    VarSet(VAR_ROGUE_RUN_ORDER, order);
}

void RogueDungeon_ResetRun(void)
{
    VarSet(VAR_ROGUE_DUNGEON_FLOOR, 0);
    RollDungeonOrder();
    VarSet(VAR_ROGUE_RUN_STATE, ROGUE_RUN_NEEDS_STARTERS);
    ZeroPlayerPartyMons();
    CalculatePlayerPartyCount();

    // Charms are per-run and must not outlive one. AFTER ZeroPlayerPartyMons,
    // because the charm rows are keyed on the personalities of the party that
    // just ended - clearing them before the party is gone would leave
    // RogueCharm_SyncParty a window in which it could re-point a live charm at
    // whatever ZeroPlayerPartyMons left behind.
    RogueCharm_ResetRun();

    // Otherwise the run-start item grant stacks with whatever survived the last
    // run, and a few losses leave the player with hundreds of balls.
    ClearBag();

    // Coins live in the save block rather than the bag, so ClearBag does not
    // reach them and a wipe would otherwise leave the rest stop's game room
    // bankrolled by every previous run. The Coin Case is an item and goes with
    // the bag; the clerk hands out another one.
    SetCoins(0);

    // AND MONEY, WHICH LIVES IN THE SAME PLACE THE COINS DO and was missed when
    // they were fixed. Nothing else in this file touches money at all, so before
    // this it survived every wipe and accumulated across runs forever - by the
    // third run the rest stop's mart is a formality and the only real currency
    // in the game has no scarcity left in it.
    //
    // That is the same bug the comment above describes, in the same save block,
    // one field over. Worth noticing that the reasoning was written down
    // correctly and still only applied to one of the two.
    SetMoney(&gSaveBlock1Ptr->money, 0);

    // Same reasoning as the bag, and a flag needs saying out loud because
    // resetting the floor counter does not touch one: ApplyRunConfig sets these
    // by depth, so leaving them set hands the next run a Super technique on
    // floor 1. The rod itself goes with ClearBag and is granted again at the
    // starter pick.
    FlagClear(FLAG_ROGUE_ROD_GOOD_TECHNIQUE);
    FlagClear(FLAG_ROGUE_ROD_SUPER_TECHNIQUE);

    // And the remembered technique, which is not a flag and would otherwise
    // leave a fresh Old-Rod-only run fishing at whatever it last chose. OLD_ROD
    // is 0, so this is also the value a save that never opened the menu holds.
    VarSet(VAR_ROGUE_ROD_TECHNIQUE, OLD_ROD);
}

// The floor's remaining wild-experience allowance, and one knockout taken off
// it. See ROGUE_GRIND_FREE_KOS for why this exists at all.
//
// READS AND WRITES IN ONE CALL, deliberately. Splitting it into a getter and a
// stepper gives the single caller two things it has to do in the right order,
// and the failure mode of getting that wrong is silent - a taper that never
// advances looks exactly like a taper that is working, right up until someone
// measures it. One accessor, one call site, nothing to sequence.
//
// The count is packed with the floor it belongs to, so a floor change resets it
// implicitly and a save-and-reload does not. See VAR_ROGUE_FLOOR_WILD_KOS.
STATIC_ASSERT(DUNGEON_TOTAL_FLOORS <= 255, DungeonFloorFitsPackedByte);

u16 RogueDungeon_TakeWildExpPercent(void)
{
    u32 floor, packed, kos, spent;

    // Every dungeon map declares LAYOUT_ROGUE_DUNGEON_FLOOR, including
    // underwater and the four that exist only to carry a weather setting, so
    // this one test means "the player is on a generated floor". Anywhere else -
    // the rest stop, the Safari, a debug map - pays the flat rate.
    if (gMapHeader.mapLayoutId != LAYOUT_ROGUE_DUNGEON_FLOOR)
        return ROGUE_WILD_EXP_PERCENT;

    floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR) & 0xFF;
    packed = VarGet(VAR_ROGUE_FLOOR_WILD_KOS);
    kos = ((packed >> 8) == floor) ? (packed & 0xFF) : 0;

    // Saturates rather than wrapping. Wrapping would hand a player who ground
    // 256 knockouts on one floor a fresh full-rate allowance, which is the one
    // outcome this whole function exists to prevent.
    if (kos < 0xFF)
        VarSet(VAR_ROGUE_FLOOR_WILD_KOS, (floor << 8) | (kos + 1));

    // kos is the count BEFORE this knockout, so the first ROGUE_GRIND_FREE_KOS
    // of them - indices 0 through FREE-1 - are the ones that pay in full.
    if (kos < ROGUE_GRIND_FREE_KOS)
        return ROGUE_WILD_EXP_PERCENT;

    spent = (kos - ROGUE_GRIND_FREE_KOS + 1) * ROGUE_GRIND_STEP;
    if (spent >= ROGUE_WILD_EXP_PERCENT - ROGUE_GRIND_MIN_PERCENT)
        return ROGUE_GRIND_MIN_PERCENT;

    return ROGUE_WILD_EXP_PERCENT - spent;
}

// Fills the three string vars the death summary message reads. Called from the
// starter script on the other side of a whiteout, not from the whiteout itself
// - see RogueDungeon_TryHandleWhiteOut for why the summary is stored rather
// than shown.
//
// The dungeon is DERIVED from the floor rather than stored beside it. Routing a
// floor to its theme is what ThemeForFloor is for, and a second copy of that
// answer in a var is a second thing to keep true.
void RogueDungeon_BufferRunSummary(void)
{
    u16 shown = VarGet(VAR_ROGUE_LAST_RUN_FLOOR);
    const struct RogueDungeonTheme *theme;

    // Both vars hold the DISPLAYED floor, so the internal counter is one less.
    // Guarded because a script could reach this with nothing pending, and
    // ThemeForFloor on an underflowed floor would index the theme table out of
    // bounds - which does not crash, it just names the wrong dungeon.
    theme = ThemeForFloor(shown ? shown - 1 : 0);

    ConvertIntToDecimalStringN(gStringVar1, shown, STR_CONV_MODE_LEFT_ALIGN, 3);
    StringCopy(gStringVar2, gRegionMapEntries[theme->mapSecId].name);
    ConvertIntToDecimalStringN(gStringVar3, VarGet(VAR_ROGUE_BEST_FLOOR),
                               STR_CONV_MODE_LEFT_ALIGN, 3);
}

// Maps that replace a theme's own for the last few floors of its dungeon. See
// struct RogueFloorMapOverride for why this is a table.
//
// Glacia's arena and its approach are a blizzard rather than the steady snow of
// the three floors before them, so her weather closes in as the player reaches
// her instead of being the room they have been walking through since floor 91.
// Two floors is deliberately short: the point is a change the player notices,
// and a change is only visible against the thing it changed from.
static const struct RogueFloorMapOverride sFloorMapOverrides[] =
{
    { DUNGEON_THEME_VICTORYROAD_GLACIA, 2, MAP_ROGUE_DUNGEON_BLIZZARD },
};

// Which map a floor lives on. Almost always MAP_ROGUE_DUNGEON_FLOOR; see
// theme->mapId for why underwater cannot share it.
//
// EVERY path that puts the player on a dungeon floor has to go through this.
// Warping to the wrong one of the two maps is not a visual glitch: the map type
// is what decides whether the player arrives diving or walking, so a floor
// reached by the wrong route would be underwater art walked over on foot.
//
// Which is also why the override is applied HERE rather than at the warp sites.
// There are five paths onto a floor and they all come through this accessor;
// that is what made the underwater problem a one-line fix once, and it is what
// makes a per-floor map cost nothing to reach every one of them.
static u16 MapForFloor(u16 floor)
{
    const struct RogueDungeonTheme *theme = ThemeForFloor(floor);
    u32 dungeon = DungeonIndexOf(floor);
    u32 within = DungeonFloorWithin(floor);
    u32 i;

    for (i = 0; i < ARRAY_COUNT(sFloorMapOverrides); i++)
    {
        // Compared through ThemeForFloor rather than against DungeonIndexOf, so
        // this holds no second copy of the floor-to-dungeon routing. The Elite
        // Four's theme indices equal their dungeon indices by construction (see
        // enum DungeonThemeId), and that is a relationship to READ, not restate.
        if (theme != &sDungeonThemes[sFloorMapOverrides[i].themeId])
            continue;

        // Counted back from the dungeon's end, the way IsDungeonBossFloor is, so
        // a change to DUNGEON_SHORT_FLOORS moves this with it.
        if (within + sFloorMapOverrides[i].lastFloors >= DungeonLengthOf(dungeon))
            return sFloorMapOverrides[i].mapId;
    }

    return theme->mapId;
}

// Field music for a dungeon floor. Returns 0 for anything that is not one of
// our floors, which leaves the engine on the map header exactly as before.
//
// GUARDED ON THE MAP, NOT THE FLOOR COUNTER, and that is the important part.
// VAR_ROGUE_DUNGEON_FLOOR keeps its value at the rest stop, through the Safari
// and in the game room -- the same trap the theme lookup in
// RogueDungeon_GetBattleEnvironment had to avoid -- so asking "which theme is
// at this depth" is only meaningful once the map has confirmed we are actually
// standing on a generated floor. The maps here are used by nothing else.
//
// Takes the map being asked about rather than reading the player's location,
// because GetLocationMusic is called for a warp DESTINATION as well as for the
// current spot: the music has to be right before the floor is stood on.
u16 RogueDungeon_GetLocationMusic(u8 mapGroup, u8 mapNum)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    // MapForFloor already resolves theme->mapId and every late-floor override,
    // so this holds no second copy of that routing. Same comparison
    // IsOnDungeonFloor makes, against the map being ASKED about rather than the
    // one being stood on.
    u16 map = MapForFloor(floor);

    if (mapGroup != MAP_GROUP(map) || mapNum != MAP_NUM(map))
        return 0;

    return ThemeForFloor(floor)->music;   // 0 = the map header decides, as before
}

static void SetWarpDestinationToFloor(u16 floor)
{
    u16 map = MapForFloor(floor);

    SetWarpDestination(MAP_GROUP(map), MAP_NUM(map), WARP_ID_NONE, -1, -1);
}

// Destination only, for the callers that follow it with WarpIntoMap rather than
// DoWarp - a new game and a whiteout both move the player without a fade.
void RogueDungeon_SetWarpToCurrentFloor(void)
{
    SetWarpDestinationToFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR));
}

// The script-side descent, replacing a hard-coded `warp MAP_ROGUE_DUNGEON_FLOOR`.
// Mirrors ScrCmd_warp exactly - destination, DoWarp, reset the stored avatar
// state - so the calling script still just follows it with waitstate.
//
// Callers must have advanced VAR_ROGUE_DUNGEON_FLOOR already, because this
// reads the floor it is warping TO in order to pick the map.
void RogueDungeon_WarpToCurrentFloor(void)
{
    RogueDungeon_SetWarpToCurrentFloor();
    DoWarp();
    ResetInitialPlayerAvatarState();
}

// The rest stop's exits are MAP_DYNAMIC, because which map they lead to depends
// on the next dungeon's theme and a warp event cannot be conditional. Called
// from the rest stop's ON_LOAD so it is recomputed however the player got there,
// including loading a save made inside it.
void RogueDungeon_SetRestStopExit(void)
{
    u16 map = MapForFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR));

    SetDynamicWarp(0, MAP_GROUP(map), MAP_NUM(map), WARP_ID_NONE);
}

// The way-station's floor cells that touch a wall, minus the ones already
// spoken for: the keeper (8,3), the merchant (4,6), the archivist (12,6), the
// descent (8,9), the alcove through to the game room (1,6) and the arrival
// tile (8,6), which is where a bare `warp` drops the player.
//
// make_rest_stop.py parses this table out of here and checks all of that
// against the floor plan, so it is allowed to be hand-picked.
static const u8 sUnownSpots[][2] =
{
    { 4, 2}, { 6, 2}, {10, 2}, {12, 2},
    { 3, 3}, {13, 3},
    { 2, 4}, {14, 4},
    { 2, 7}, {14, 7},
    { 5, 9}, {11, 9},
};

// Unown form A is SPECIES_UNOWN at 201 and the other twenty-seven are one
// contiguous run from SPECIES_UNOWN_B, which is why this is not just an add.
static u16 UnownForm(u8 n)
{
    return n == 0 ? SPECIES_UNOWN : SPECIES_UNOWN_B + n - 1;
}

// The Unown are the only thing in the run that acknowledges the player, and the
// only place the illusion shows a seam. They stand along the way-station's edge
// and watch, and there are more of them the deeper the run has gone.
//
// Called from the rest stop's ON_TRANSITION, which runs after the map's
// templates are copied out of ROM and before anything spawns from them - the
// same window `setobjectxyperm` writes in. Only the Unown slots are touched, so
// the three staff keep whatever map.json gave them.
//
// SEEDED FROM THE FLOOR, so one rest stop looks the same every time it is
// entered - reload a save inside one and nothing shifts - while no two rest
// stops in a run are arranged alike. Reseeding the shared RNG here is safe
// because floor generation seeds it again before it uses it.
void RogueDungeon_SeedRestStopUnown(void)
{
    struct ObjectEventTemplate *templates = gSaveBlock1Ptr->objectEventTemplates;
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    u8 order[ARRAY_COUNT(sUnownSpots)];
    u8 i, count;

    FlagSet(FLAG_ROGUE_OBJECT_UNUSED);
    SeedDungeonRng(floor * 3 + 1);

    // Shuffle the whole spot list and take a prefix, rather than rolling a spot
    // per Unown - two Unown on one tile would stack invisibly.
    for (i = 0; i < ARRAY_COUNT(sUnownSpots); i++)
        order[i] = i;
    for (i = ARRAY_COUNT(sUnownSpots) - 1; i > 0; i--)
    {
        u8 j = DungeonRandom() % (i + 1);
        u8 swap = order[i];

        order[i] = order[j];
        order[j] = swap;
    }

    count = REST_STOP_UNOWN_MIN + floor / REST_STOP_UNOWN_FLOORS_PER_EXTRA;
    if (count > REST_STOP_UNOWN_SLOTS)
        count = REST_STOP_UNOWN_SLOTS;

    for (i = 0; i < REST_STOP_UNOWN_SLOTS; i++)
    {
        struct ObjectEventTemplate *t = &templates[REST_STOP_UNOWN_FIRST_SLOT + i];

        if (i >= count)
        {
            // An object event whose flagId is set is not spawned.
            t->flagId = FLAG_ROGUE_OBJECT_UNUSED;
            continue;
        }

        t->graphicsId = OBJ_EVENT_MON + UnownForm(DungeonRandom() % 28);
        t->x = sUnownSpots[order[i]][0];
        t->y = sUnownSpots[order[i]][1];
        t->flagId = 0;
    }
}

bool8 RogueDungeon_TryHandleWhiteOut(void)
{
    if (gMapHeader.mapLayoutId != LAYOUT_ROGUE_DUNGEON_FLOOR)
        return FALSE;

    // STASHED, NOT SHOWN, and read BEFORE ResetRun puts the floor counter back
    // to zero. This runs from CB2_WhiteOut with no message window open and warps
    // out of the map immediately afterwards, so there is nowhere here to say
    // anything; the frame script that asks for starters on the other side shows
    // it instead. A non-zero value is what marks a summary as waiting, which is
    // why this stores the displayed floor rather than the counter - dying on the
    // first floor is the commonest way a run ends and it must not read as "no
    // summary".
    //
    // A loss is the ONLY writer of the best floor. Putting it here rather than
    // in ApplyRunConfig keeps the debug floor warp out of it - that warp sets
    // the counter without a run behind it, so a single warp to 115 would retire
    // the record for good.
    VarSet(VAR_ROGUE_LAST_RUN_FLOOR, VarGet(VAR_ROGUE_DUNGEON_FLOOR) + 1);
    if (VarGet(VAR_ROGUE_LAST_RUN_FLOOR) > VarGet(VAR_ROGUE_BEST_FLOOR))
        VarSet(VAR_ROGUE_BEST_FLOOR, VarGet(VAR_ROGUE_LAST_RUN_FLOOR));

    RogueDungeon_ResetRun();

    // Back to the first floor, where the frame table will ask for starters.
    // ResetRun has already put the floor counter back to 0, so this picks that
    // floor's map rather than the one being whited out of.
    RogueDungeon_SetWarpToCurrentFloor();
    WarpIntoMap();
    return TRUE;
}

// Deals `slots` entries from a species ladder, starting the round robin at
// `rotation`. Split out of BuildWildEncounterTable because a table whose window
// is wider than its slots re-deals per roll - see RogueDungeon_GetWildMonInfo.
//
// Takes the LADDER rather than the theme, because the Safari Zone has pools and
// no theme. Nothing here ever wanted anything else off the theme.
//
// Takes the DESTINATION too, because fishing keeps a table of its own - see
// sDungeonFishingMons.
// The lowest level a species could legitimately have reached, or NULL for one
// with no pre-evolution.
//
// Bisects sRogueEvoSteps, emitted in species-enum order by
// tools/rogue/gen_evolution_levels.py. The ordering IS the contract, and it is
// the same one the BW animation table depends on: emit_bw_anim.py sorts by
// NUMERIC id because sorting by name puts CLAYDOL before GEODUDE while their
// ids run the other way, and a bisection would then silently miss them.
static const struct RogueEvoStep *FindEvoStep(u16 species)
{
    u32 lo = 0, hi = ARRAY_COUNT(sRogueEvoSteps);

    while (lo < hi)
    {
        u32 mid = (lo + hi) / 2;
        u16 got = sRogueEvoSteps[mid].species;

        if (got == species)
            return &sRogueEvoSteps[mid];
        if (got < species)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

// Walk a species DOWN its chain until the floor's level could have produced it.
//
// THE POOLS CANNOT SOLVE THIS BY ORDERING, which is why it lives here and not
// in the data. A gym theme reads a window TWELVE wide, and bottom is tiers
// minus window, so the two cancel on a dungeon's first floor and the opening
// floor already exposes the first twelve entries of a twenty-one entry pool. A
// gym dungeon meanwhile moves about five levels end to end. So whatever sits at
// index 11 arrives at the dungeon's STARTING level however carefully the ladder
// is sorted - which is how the cave came to offer Graveler at level 10 against
// a Geodude that cannot evolve until 25, and Dugtrio at 10 against 26.
//
// Keyed on the floor's BASE level rather than base + spread, so every roll in
// the encounter's range is legitimate rather than only the luckiest one. The
// cost is that a mon at the top of the range is sometimes one stage below what
// it could have been, which is what wild Pokemon look like anyway.
//
// It does NOT fix a stone, trade or friendship evolution. Those carry no level
// at all, so an Arcanine is legal at any level and only the pool can decide it
// is out of place - the STAGED findings in check_pool_evolutions.py are a
// curation problem and not this one.
u16 RogueDungeon_DevolveForLevel(u16 species, u8 level)
{
    const struct RogueEvoStep *step;
    u32 guard;

    // The longest chain in Gen 1-4 is three stages, so two steps is the real
    // bound. The guard is against a CYCLE in the generated table rather than a
    // long chain: this reads emitted data, and a table pointing a species at
    // itself would hang the floor load with no other symptom.
    for (guard = 0; guard < 4; guard++)
    {
        step = FindEvoStep(species);
        if (step == NULL || step->minLevel <= level)
            break;
        if (step->preEvo == species)
            break;
        species = step->preEvo;
    }
    return species;
}

static void DealWildSlots(struct WildPokemon *dst, const u16 *species, u32 slots,
                          u32 bottom, u32 width, u32 rotation, u8 level)
{
    u32 i;

    // Dealt round-robin, not drawn independently per slot. Encounter slot
    // weights are steeply uneven (20/20/10/10/...), so independent draws let one
    // species take both 20% slots and dominate the floor. The rotation varies
    // which species lands in the common slots.
    for (i = 0; i < slots; i++)
    {
        // Devolved HERE rather than in the pools, so the one hook covers land,
        // water, fishing and the Safari Zone - every caller comes through this
        // function, which is why it takes the ladder and the destination rather
        // than the theme.
        dst[i].species = RogueDungeon_DevolveForLevel(species[bottom + (i + rotation) % width],
                                         level);
        dst[i].minLevel = level;
        dst[i].maxLevel = level + DUNGEON_ENCOUNTER_LEVEL_SPREAD;
    }
}

// Must be called from generation, after SeedDungeonRng, so a given floor always
// opens on the same table. The WATER branch then re-deals it per roll, which is
// deliberate and is explained at sWildRotation.
static void BuildWildEncounterTable(u16 floor)
{
    const struct RogueDungeonTheme *theme = ThemeForFloor(floor);
    u32 scaled = FloorTargetLevel(floor);

    // THE RAMP IS RELATIVE TO THE DUNGEON, NOT THE RUN, and that is a
    // correctness point rather than a tuning one. Every theme has its OWN pool,
    // written for its own depth - so keying the tier on the absolute floor made
    // a theme read its pool from wherever its dungeon happened to sit. The
    // underwater pool starts at floor 71, so only its top five entries were ever
    // reachable and eleven curated species could not appear at all. Counting
    // from the floor within the dungeon makes every theme read its pool the same
    // way, which is also what lets a pool be lengthened without touching this.
    //
    // Difficulty does not come from here. FloorTargetLevel is still absolute, so
    // a deep dungeon still sends high-level Pokemon; this decides only WHICH.
    // The ramp STARTS at the window, which is what makes index 0 reachable on a
    // dungeon's first floor: bottom is tiers - window, and on floor 0 those are
    // equal. Any other starting tier either buries the bottom of the pool or
    // leaves the window unfilled.
    u32 window = (theme->encounterWindow != 0)
               ? theme->encounterWindow : DUNGEON_ENCOUNTER_WINDOW;
    u32 tiers = window + DungeonFloorWithin(floor) / DUNGEON_ENCOUNTER_TIER_FLOORS;
    u32 bottom, width, rotation;
    u8 level;

    // The engine gives the two branches different slot counts - twelve for
    // land, five for water - and the theme feeds whichever one its encounter
    // surface belongs to.
    u32 slots = (theme->wildArea == WILD_AREA_WATER)
              ? NUM_WATER_MONS_ENCOUNTER_SLOTS
              : NUM_LAND_MONS_ENCOUNTER_SLOTS;

    // Clamp before narrowing to u8, or a deep enough floor wraps.
    if (scaled > MAX_LEVEL - DUNGEON_ENCOUNTER_LEVEL_SPREAD)
        scaled = MAX_LEVEL - DUNGEON_ENCOUNTER_LEVEL_SPREAD;
    level = scaled;

    if (tiers > theme->speciesCount)
        tiers = theme->speciesCount;

    // Window rather than prefix, so the weakest species retire with depth.
    bottom = (tiers > window) ? tiers - window : 0;
    width = tiers - bottom;

    // THIS DRAW MUST HAPPEN HERE whatever the water branch does with it later.
    // BuildWildEncounterTable runs before PlaceTrainers, PlaceItems,
    // PlaceBerryTrees and the hidden item, all of which draw from this same
    // seeded stream - so not consuming a value here would shift every one of
    // them. The layout would be byte-identical and every object on the floor
    // would move, which is not a failure anything checks for.
    rotation = DungeonRandom() % width;

    sWildBottom = bottom;
    sWildWidth = width;
    sWildRotation = rotation;
    sWildLevel = level;
    sWildSlots = slots;
    sWildSpecies = theme->species;

    DealWildSlots(sDungeonWildMons, theme->species, slots, bottom, width, rotation, level);

    // encounterRate is read straight off the static table, not from here, so
    // this value is only a sane fallback.
    sDungeonWildInfo.encounterRate = 10;
    sDungeonWildInfo.wildPokemon = sDungeonWildMons;
    sDungeonWildArea = theme->wildArea;
}

// Which ladder the Safari's given surface reads. Also the cache key - see
// sSafariBuiltFloor.
static const u16 *SafariLadder(enum WildPokemonArea area)
{
    return (area == WILD_AREA_WATER) ? sSafariWaterSpecies : sSafariLandSpecies;
}

// Is the player in the run's Safari Zone?
//
// Group AND section, because neither alone is enough: the Rogue map group also
// holds the dungeon floors and the way-station, and MAPSEC_SAFARI_ZONE is
// vanilla's own, shared with the six Hoenn maps these were copied from. The
// pair is exact and costs one comparison more than a six-way map id list would,
// while not needing to name a single map - so a seventh zone needs no edit
// here.
static bool8 IsSafariMap(void)
{
    return gSaveBlock1Ptr->location.mapGroup == MAP_GROUP(MAP_ROGUE_SAFARI_SOUTH)
        && gMapHeader.regionMapSectionId == MAPSEC_SAFARI_ZONE;
}

// The Safari Zone's table.
//
// Depth decides WHICH species, exactly as it does on a dungeon floor, and the
// level follows FloorTargetLevel so a Safari catch arrives at parity with the
// curve rather than as a free gift or a fossil.
//
// THE TIER COUNTS FROM THE ABSOLUTE RUN FLOOR, which is the opposite of what
// BuildWildEncounterTable does and right for the opposite reason. A theme pool
// is written for one dungeon's depth, so keying it on the absolute floor made
// every theme read its pool from wherever its dungeon happened to sit. This
// pool spans the whole run and has no dungeon to be relative to. Do not
// "correct" this to DungeonFloorWithin.
static void BuildSafariEncounterTable(u16 floor, enum WildPokemonArea area)
{
    const u16 *ladder;
    u32 count, window, slots, tiers, bottom, width;
    u32 scaled = FloorTargetLevel(floor);
    u8 level;

    ladder = SafariLadder(area);
    if (area == WILD_AREA_WATER)
    {
        count = ARRAY_COUNT(sSafariWaterSpecies);
        window = DUNGEON_SAFARI_WATER_WINDOW;
        slots = NUM_WATER_MONS_ENCOUNTER_SLOTS;
    }
    else
    {
        count = ARRAY_COUNT(sSafariLandSpecies);
        window = DUNGEON_SAFARI_LAND_WINDOW;
        slots = NUM_LAND_MONS_ENCOUNTER_SLOTS;
    }

    // Clamp before narrowing to u8, or a deep enough floor wraps.
    if (scaled > MAX_LEVEL - DUNGEON_ENCOUNTER_LEVEL_SPREAD)
        scaled = MAX_LEVEL - DUNGEON_ENCOUNTER_LEVEL_SPREAD;
    level = scaled;

    tiers = window + floor / DUNGEON_ENCOUNTER_TIER_FLOORS;
    if (tiers > count)
        tiers = count;
    bottom = (tiers > window) ? tiers - window : 0;
    width = tiers - bottom;

    sWildBottom = bottom;
    sWildWidth = width;
    // NOT DungeonRandom(). That stream belongs to floor generation and is not
    // live here - the Safari is entered from the way-station, outside any
    // floor's generation - so the opening rotation comes off the floor number
    // and the per-roll advance does the rest.
    sWildRotation = width ? floor % width : 0;
    sWildLevel = level;
    sWildSlots = slots;
    sWildSpecies = ladder;

    DealWildSlots(sDungeonWildMons, ladder, slots, bottom, width, sWildRotation, level);

    sDungeonWildInfo.encounterRate = 10;
    sDungeonWildInfo.wildPokemon = sDungeonWildMons;
    sDungeonWildArea = area;
}

// Deals the rod's ten slots from whatever window the water branch has already
// worked out for this floor. Called only after that window is current.
//
// STRIDE 1, and that does not contradict the stride-`slots` finding at
// sWildRotation. That one was about a table as wide as its window, where
// advancing by one leaves eleven of twelve species in place and consecutive
// tables read as identical. Ten slots into a twelve-wide window share eight
// species at ANY stride, so the thing left worth getting right is which rung
// lands in the Old Rod's two slots - and 1 is the only stride coprime with
// every width, so every rung reaches them equally often.
static const struct WildPokemonInfo *FishingInfoForCurrentWindow(void)
{
    // No window means no floor has been built for this surface yet, which is
    // the one case where the static placeholder is the better answer.
    if (sWildSpecies == NULL || sWildWidth == 0)
        return NULL;

    sFishingRotation = (sFishingRotation + 1) % sWildWidth;
    DealWildSlots(sDungeonFishingMons, sWildSpecies, NUM_FISHING_MONS_ENCOUNTER_SLOTS,
                  sWildBottom, sWildWidth, sFishingRotation, sWildLevel);

    sDungeonFishingInfo.encounterRate = 10;
    sDungeonFishingInfo.wildPokemon = sDungeonFishingMons;
    return &sDungeonFishingInfo;
}

// Hooked into TryGenerateWildMon. Returning NULL leaves the caller on the
// ordinary static table.
//
// Every dungeon map declares LAYOUT_ROGUE_DUNGEON_FLOOR, including the four
// that exist only to carry a weather setting, so the layout check still means
// "the player is on a generated floor" even though the LAYOUT the generator
// installed is the theme's own. mapLayoutId is the id the map was built with;
// only gMapHeader.mapLayout is swapped.
//
// Matching on the area the table was BUILT for, not on WILD_AREA_LAND, is what
// lets the ocean and the seafloor use this at all - they take the engine's
// water branch, and before this they fell through to the static placeholder.
const struct WildPokemonInfo *RogueDungeon_GetWildMonInfo(enum WildPokemonArea area)
{
    // FROM HERE DOWN, FISHING IS THE WATER BRANCH. The rod fishes the water the
    // player would otherwise surf, so it wants the same ladder, the same window
    // and the same levels; folding it in here rather than adding a third case
    // means the Safari cache key, the theme's wildArea test and the level curve
    // all keep working untouched. Only the final deal differs, and that has a
    // table of its own.
    //
    // Which also settles where the rod works: on a floor this passes only when
    // the theme's own wildArea is WATER, so the ocean and the seafloor - the two
    // themes that have any water at all - and nowhere else.
    bool8 fishing = (area == WILD_AREA_FISHING);

    if (fishing)
        area = WILD_AREA_WATER;

    if (IsSafariMap())
    {
        u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);

        // Built LAZILY rather than from a map script, so no entry path can miss
        // it: there is no ON_TRANSITION on a load-from-save, and the Safari has
        // six maps the player walks between by CONNECTION rather than by warp,
        // which runs no map script at all.
        //
        // The cache exists so the rotation below can advance. Rebuilding every
        // roll would reset it and undo the whole point.
        if (sSafariBuiltFloor != floor || sWildSpecies != SafariLadder(area))
        {
            BuildSafariEncounterTable(floor, area);
            sSafariBuiltFloor = floor;
        }
    }
    else
    {
        if (gMapHeader.mapLayoutId != LAYOUT_ROGUE_DUNGEON_FLOOR)
            return NULL;
        if (area != sDungeonWildArea)
            return NULL;
    }

    // The rod leaves here, ABOVE the re-deal below, so that a cast never
    // advances the surf rotation. The two surfaces share a window, not a
    // sequence.
    if (fishing)
        return FishingInfoForCurrentWindow();

    // RE-DEAL WHEN THE WINDOW IS WIDER THAN THE SLOTS, which is exactly when
    // re-dealing buys anything: the window then means "how many species can
    // appear here at all" instead of "how many are present at once". This used
    // to test `area == WILD_AREA_WATER`, which was the same set of tables by
    // coincidence - water's 12-wide window over 5 slots. Land was never the
    // exception; 12 == 12 was.
    //
    // ADVANCED BY A WHOLE TABLE'S WORTH, not by one. Advancing by one shifts
    // the window a single rung, so consecutive tables share ELEVEN OF TWELVE
    // species and any one species sits in the table for twelve consecutive
    // rolls - passing through both 20% slots on its way down. That reads as
    // "the same table every hit" with one Pokemon over-represented, which is
    // exactly what it did in play. Measured over 20 encounters: 13 distinct
    // species at stride 1 against 18 at stride `slots`, where consecutive
    // tables share NOTHING.
    //
    // Long-run fairness is unchanged. The stride is coprime with the width in
    // every configuration this build ships, so the rotation still visits every
    // value once per lap and every rung reaches the common slots equally often
    // - check_safari_pool.py asserts that coprimality, because a width that
    // became a multiple of the stride would strand most of the ladder in
    // silence.
    //
    // Deliberately NOT DungeonRandom(): that stream belongs to floor
    // generation and is not live at roll time, and drawing from it here would
    // make encounters depend on generation order.
    //
    // Land runs six ability-influenced scans over the table where water runs
    // fewer, so on land an ability's pull now samples a freshly dealt twelve
    // each roll. That is still atomic - TryGenerateWildMon takes this pointer
    // once and uses it for the whole call - but it is a real change in what
    // Magnet Pull and friends are choosing from, not an oversight.
    if (sWildWidth > sWildSlots && sWildSpecies != NULL)
    {
        sWildRotation = (sWildRotation + sWildSlots) % sWildWidth;
        DealWildSlots(sDungeonWildMons, sWildSpecies, sWildSlots, sWildBottom,
                      sWildWidth, sWildRotation, sWildLevel);
    }

    return &sDungeonWildInfo;
}

static u16 MakeBlock(u16 metatile, u8 collision, u8 elevation)
{
    return (metatile & MAPGRID_METATILE_ID_MASK)
         | ((collision << 10) & MAPGRID_COLLISION_MASK)
         | ((elevation << 12) & MAPGRID_ELEVATION_MASK);
}

// Playfield coords -> backup map index. The layout sits MAP_OFFSET blocks in
// from the edge of the backup buffer on both axes.
static void SetBlock(u16 *map, s32 x, s32 y, u16 block)
{
    if (x < 0 || y < 0 || x >= DUNGEON_WIDTH || y >= DUNGEON_HEIGHT)
        return;
    map[(y + MAP_OFFSET) * gBackupMapLayout.width + (x + MAP_OFFSET)] = block;
}

// Takes the theme rather than the cave's own metatile. This used to be
// hard-coded to DUNGEON_METATILE_FLOOR, which is right only for the cave: a
// metatile id means something else under every other tileset pair, so New
// Mauville's floor came out as a counter fragment and Fiery Path's as a green
// bush - and the bush is MB_NORMAL, which carries no encounter flag, so ten
// floors had no wild Pokemon at all. theme->floor existed the whole time but
// was only ever read on the woods path.
static void CarveFloor(u16 *map, const struct RogueDungeonTheme *theme,
                       s32 x, s32 y)
{
    SetBlock(map, x, y, MakeBlock(theme->floor, 0, theme->elevationFloor));
}

// Anything outside the playfield counts as wall, so the map edge tiles the same
// way the interior does instead of being treated as an exposed face.
static bool8 IsWallAt(const u16 *map, s32 x, s32 y)
{
    if (x < 0 || y < 0 || x >= DUNGEON_WIDTH || y >= DUNGEON_HEIGHT)
        return TRUE;
    return (map[(y + MAP_OFFSET) * gBackupMapLayout.width + (x + MAP_OFFSET)]
            & MAPGRID_COLLISION_MASK) != 0;
}

static u16 GetBlockMetatile(const u16 *map, s32 x, s32 y)
{
    if (x < 0 || y < 0 || x >= DUNGEON_WIDTH || y >= DUNGEON_HEIGHT)
        return 0;
    return map[(y + MAP_OFFSET) * gBackupMapLayout.width + (x + MAP_OFFSET)]
           & MAPGRID_METATILE_ID_MASK;
}

// Cosmetic passes are keyed on position and seed rather than drawn from the
// dungeon RNG. They run in WriteFloorBlocks, which can repaint a floor without
// PrepareFloor having run again; consuming RNG there would leave the state
// dependent on how the player arrived, and a floor would redecorate itself.
static u16 DecorHash(u16 seed, s32 x, s32 y)
{
    u32 h = (u32)seed * 2654435761u;

    h ^= (u32)(x + 1) * 40503u;
    h ^= (u32)(y + 1) * 24593u;
    h ^= h >> 13;
    h *= 2246822519u;
    h ^= h >> 15;
    return (u16)h;
}

static u16 SkirtFor(const struct RogueDungeonTheme *theme, u16 wall,
                    bool8 east)
{
    u32 i;

    for (i = 0; i < theme->skirtCount; i++)
    {
        if (theme->skirts[i].wall == wall)
            return east ? theme->skirts[i].east : theme->skirts[i].south;
    }
    return 0;
}

// Wall skirts: a wall's own bottom or side edge, drawn into the floor tile
// next to it. Keyed to the SPECIFIC wall metatile, and deterministic - vanilla
// applies these at effectively 100% per wall type. (Averaged across all wall
// types they look probabilistic, which produced two wrong versions of this
// pass: a solid band under every wall, then a randomly thinned one.)
static void ApplySkirts(u16 *map, const struct RogueDungeonTheme *theme)
{
    s32 x, y;

    if (theme->skirtCount == 0)
        return;

    for (y = 0; y < DUNGEON_HEIGHT; y++)
    {
        for (x = 0; x < DUNGEON_WIDTH; x++)
        {
            u16 south = 0, east = 0, metatile;

            if (IsWallAt(map, x, y))
                continue;

            if (IsWallAt(map, x, y - 1))
                south = SkirtFor(theme, GetBlockMetatile(map, x, y - 1), FALSE);
            if (IsWallAt(map, x - 1, y))
                east = SkirtFor(theme, GetBlockMetatile(map, x - 1, y), TRUE);

            if (south && east)
                metatile = theme->shadowCorner ? theme->shadowCorner : south;
            else if (south)
                metatile = south;
            else if (east)
                metatile = east;
            else if (theme->shadowCorner && !IsWallAt(map, x, y - 1)
                  && !IsWallAt(map, x - 1, y) && IsWallAt(map, x - 1, y - 1)
                  && SkirtFor(theme, GetBlockMetatile(map, x - 1, y - 1), FALSE))
                metatile = theme->shadowCorner;   // only the corner catches it
            else
                continue;

            SetBlock(map, x, y, MakeBlock(metatile, 0, theme->elevationFloor));
        }
    }
}

// One soft region stamped on the floor. Ellipses rather than per-cell noise:
// scattered single cells read as static, and the region art is drawn with
// rounded corners, so it wants shapes that are actually round.
struct PatchBlob
{
    s16 cx, cy;
    u8 rx, ry;
    bool8 square;   // rx/ry are a half-extent in blocks, not an ellipse radius
};

// Derived from the seed alone, never from the dungeon RNG, for the same reason
// the decor hash is: WriteFloorBlocks can repaint a floor without PrepareFloor
// having run, so consuming RNG here would make a floor redraw itself
// differently depending on how the player arrived.
static u16 BlobHash(u16 seed, u32 index, u32 salt)
{
    u32 h = (u32)seed * 2654435761u;

    h ^= (index + 1) * 2654435769u;
    h ^= (salt + 1) * 2246822519u;
    h ^= h >> 13;
    h *= 2654435761u;
    h ^= h >> 15;
    return (u16)h;
}

// The layer index is folded into the hash salt so two layers of a theme never
// stamp the same blobs on top of each other.
static u32 BuildPatchBlobs(const struct RoguePatchLayer *layer, u32 which,
                           u16 seed, struct PatchBlob *blobs)
{
    u32 count = layer->blobs;
    u32 i;

    if (count > DUNGEON_MAX_PATCH_BLOBS)
        count = DUNGEON_MAX_PATCH_BLOBS;

    for (i = 0; i < count; i++)
    {
        u16 a = BlobHash(seed, i, which * 2);
        u16 b = BlobHash(seed, i, which * 2 + 1);

        blobs[i].cx = a % DUNGEON_WIDTH;
        blobs[i].cy = b % DUNGEON_HEIGHT;
        blobs[i].square = (layer->square != 0);
        if (layer->square)
        {
            // Deliberate, so NO variation: same size every time, which is the
            // whole point of it reading as built rather than grown.
            blobs[i].rx = layer->square - 1;
            blobs[i].ry = layer->square - 1;
        }
        else
        {
            // Radius varies either side of the nominal so blobs do not all read
            // as the same stamp repeated.
            blobs[i].rx = layer->radius + ((a >> 8) % 3) - 1;
            blobs[i].ry = layer->radius + ((b >> 8) % 3) - 1;
        }
    }
    return count;
}

// Raw membership: geometry AND not-wall, deliberately nothing else. It must not
// depend on what this pass has already written, or a cell painted earlier would
// stop reading as part of the region and its neighbour would draw an edge
// against the middle of the blob.
static bool8 InPatchBlob(const u16 *map, const struct PatchBlob *blobs,
                         u32 count, s32 x, s32 y)
{
    u32 i;

    if (x < 0 || y < 0 || x >= DUNGEON_WIDTH || y >= DUNGEON_HEIGHT)
        return FALSE;
    if (IsWallAt(map, x, y))
        return FALSE;

    for (i = 0; i < count; i++)
    {
        s32 dx = x - blobs[i].cx;
        s32 dy = y - blobs[i].cy;
        u32 rx = blobs[i].rx;
        u32 ry = blobs[i].ry;

        // Cheap rejects before the multiplies - this runs for every floor cell
        // and its four neighbours.
        if (dx < 0)
            dx = -dx;
        if (dy < 0)
            dy = -dy;
        if ((u32)dx > rx || (u32)dy > ry)
            continue;

        // A square stamp is already fully described by the reject above - every
        // cell inside the extent is in. Falling through to the ellipse test
        // would round its corners off, which is the one thing it must not do.
        if (blobs[i].square)
            return TRUE;

        if ((u32)(dx * dx) * ry * ry + (u32)(dy * dy) * rx * rx <= rx * rx * ry * ry)
            return TRUE;
    }
    return FALSE;
}

// Membership proper, which is the raw blob eroded by one. Where a blob only
// clips the corner of a room, or crosses a corridor at a single cell, the raw
// shape leaves one- and two-block specks that read as a stray tile of the wrong
// colour rather than as a drift. Requiring two neighbours removes those and
// leaves the shape of anything larger untouched.
//
// Still a pure function of geometry, so it gives the same answer for a cell
// whether it is asked about itself or as somebody's neighbour. That is the
// whole reason the erosion tests the RAW shape rather than the eroded one.
static bool8 IsPatchCell(const u16 *map, const struct PatchBlob *blobs,
                         u32 count, s32 x, s32 y)
{
    u32 neighbours;

    if (!InPatchBlob(map, blobs, count, x, y))
        return FALSE;

    neighbours = InPatchBlob(map, blobs, count, x, y - 1)
               + InPatchBlob(map, blobs, count, x, y + 1)
               + InPatchBlob(map, blobs, count, x - 1, y)
               + InPatchBlob(map, blobs, count, x + 1, y);
    return neighbours >= 2;
}

// Lays the theme's floor region over the carved floor and autotiles its edges.
// Runs after the walls are painted, so the wall/floor split is final, and
// before the skirts, so a wall's own edge art still wins for a theme that has
// both. Collision and elevation come from the floor, so this cannot change what
// is reachable.
static void ApplyPatchLayer(u16 *map, const struct RogueDungeonTheme *theme,
                            const struct RoguePatchLayer *layer, u32 which,
                            u16 seed)
{
    struct PatchBlob blobs[DUNGEON_MAX_PATCH_BLOBS];
    u32 count;
    s32 x, y;

    if (layer->blobs == 0 || layer->tile[PATCH_MID] == 0)
        return;

    count = BuildPatchBlobs(layer, which, seed, blobs);

    for (y = 0; y < DUNGEON_HEIGHT; y++)
    {
        for (x = 0; x < DUNGEON_WIDTH; x++)
        {
            bool8 openNorth, openSouth, openWest, openEast;
            enum DungeonPatchSlot slot;

            // Most of a floor is wall, so reject those before any blob maths.
            if (IsWallAt(map, x, y))
                continue;
            if (!IsPatchCell(map, blobs, count, x, y))
                continue;

            openNorth = !IsPatchCell(map, blobs, count, x, y - 1);
            openSouth = !IsPatchCell(map, blobs, count, x, y + 1);
            openWest  = !IsPatchCell(map, blobs, count, x - 1, y);
            openEast  = !IsPatchCell(map, blobs, count, x + 1, y);

            if (openNorth)
            {
                // A wall above means the wall's base is baked into the art, so
                // these are a different row rather than the same one shaded.
                if (IsWallAt(map, x, y - 1))
                    slot = openWest ? PATCH_NW_WALL
                         : openEast ? PATCH_NE_WALL : PATCH_N_WALL;
                else
                    slot = openWest ? PATCH_NW : openEast ? PATCH_NE : PATCH_N;
            }
            else if (openSouth)
            {
                slot = openWest ? PATCH_SW : openEast ? PATCH_SE : PATCH_S;
            }
            else if (openWest)
            {
                slot = PATCH_W;
            }
            else if (openEast)
            {
                slot = PATCH_E;
            }
            else
            {
                slot = PATCH_MID;
            }

            if (layer->tile[slot] != 0)
            {
                u16 metatile = layer->tile[slot];

                // A phased layer's slot id is the base of `phase` consecutive
                // metatiles that are phases of a diagonal banding rather than
                // interchangeable variants, so the offset is a diagonal and not
                // a hash. Folded back into range by hand because y - x is
                // signed and negative over half the floor, and C's % keeps the
                // sign of the dividend.
                if (layer->phase != 0)
                    metatile += ((y - x) % layer->phase + layer->phase)
                                % layer->phase;

                SetBlock(map, x, y,
                         MakeBlock(metatile, 0, theme->elevationFloor));
            }
        }
    }
}

static void ApplyFloorPatches(u16 *map, const struct RogueDungeonTheme *theme,
                              u16 seed)
{
    u32 i;

    // In order, so a later layer wins: the jungle lays long grass and then
    // punches puddles through it.
    for (i = 0; i < theme->patchCount; i++)
        ApplyPatchLayer(map, theme, &theme->patches[i], i, seed);
}

// Swaps the occasional block for a decorated variant of the same metatile.
// The block's collision and elevation are copied over unchanged, so a wall
// variant stays wall and a floor variant (Fiery Path's ember sparkles) stays
// floor - decoration can never change what is reachable.
static void ApplyDecor(u16 *map, const struct RogueDungeonTheme *theme,
                       u16 seed)
{
    s32 x, y;
    u32 i;

    if (theme->decorCount == 0 || theme->decorRarity == 0)
        return;

    for (y = 0; y < DUNGEON_HEIGHT; y++)
    {
        for (x = 0; x < DUNGEON_WIDTH; x++)
        {
            u16 hash, metatile, block;
            u32 matches = 0, pick;

            hash = DecorHash(seed, x, y);
            if (hash % theme->decorRarity != 0)
                continue;

            block = map[(y + MAP_OFFSET) * gBackupMapLayout.width
                        + (x + MAP_OFFSET)];
            metatile = block & MAPGRID_METATILE_ID_MASK;

            // A 2-wide unit needs the block east to still be undecorated base
            // as well. Because this scans west to east, a placed unit turns
            // both its blocks into non-base metatiles, so later rolls cannot
            // land a second decoration on either half.
            for (i = 0; i < theme->decorCount; i++)
            {
                if (theme->decor[i].base != metatile)
                    continue;
                if (theme->decor[i].variantEast != 0
                 && GetBlockMetatile(map, x + 1, y) != metatile)
                    continue;
                matches++;
            }
            if (matches == 0)
                continue;

            // A second draw off the same hash, so which variant lands does not
            // correlate with whether one lands at all.
            pick = (hash >> 8) % matches;
            for (i = 0; i < theme->decorCount; i++)
            {
                if (theme->decor[i].base != metatile)
                    continue;
                if (theme->decor[i].variantEast != 0
                 && GetBlockMetatile(map, x + 1, y) != metatile)
                    continue;
                if (pick-- == 0)
                {
                    u16 keep = block & ~MAPGRID_METATILE_ID_MASK;

                    SetBlock(map, x, y, theme->decor[i].variant | keep);
                    if (theme->decor[i].variantEast != 0)
                        SetBlock(map, x + 1, y,
                                 theme->decor[i].variantEast | keep);
                    break;
                }
            }
        }
    }
}

// Is this cell the bottom-most block of a one-wide vertical wall run - the one
// WALL_SLIVER_VERT_BOT is painted on? Used to identify the block ABOVE it,
// which some tilesets draw differently because the column stands on a base two
// metatiles tall.
static bool8 IsSliverVertBottom(const u16 *map, s32 x, s32 y)
{
    return IsWallAt(map, x, y)
        && !IsWallAt(map, x - 1, y) && !IsWallAt(map, x + 1, y)
        && !IsWallAt(map, x, y + 1);
}

// A tileset whose wall is two metatiles tall needs a cap - the top surface -
// on the row above every face. Returns 0 for a theme that declares none, and
// for any cell that is not one.
//
// This cannot be answered from the cell's own eight neighbours: they are all
// wall, exactly as they are for deep interior. What identifies a cap is
// distance to floor going south - this cell and the one below are wall, the
// cell two below is floor - so it is tested geometrically rather than through
// the mask chain. Getting that wrong is why derive_wall_table.py reports void
// at 62.8% for the mask and the cap row hides inside the other 37%.
static u16 WallCapFor(const u16 *map, const struct RogueDungeonTheme *theme,
                      s32 x, s32 y)
{
    if (theme->wall[WALL_CAP_MID] == 0 && theme->wall[WALL_CAP_LEFT] == 0
     && theme->wall[WALL_CAP_RIGHT] == 0)
        return 0;

    if (!IsWallAt(map, x, y + 1) || IsWallAt(map, x, y + 2))
        return 0;

    // Open on both sides makes the cell below a sliver, which carries its own
    // end art and is not a face. Capping one would put a lid on a column.
    if (!IsWallAt(map, x - 1, y + 1) && !IsWallAt(map, x + 1, y + 1))
        return 0;

    // Match the cap to the face beneath it, chosen the same way the face was.
    if (!IsWallAt(map, x - 1, y + 1))
        return theme->wall[WALL_CAP_LEFT];
    if (!IsWallAt(map, x + 1, y + 1))
        return theme->wall[WALL_CAP_RIGHT];
    return theme->wall[WALL_CAP_MID];
}

// Defined beside the cave plane helpers it borrows, far below. Forward
// declared because the pass that needs it runs from up here.
static bool8 DungeonOpenIsOnePiece(const u16 *map);

static u16 GetBlockRaw(const u16 *map, s32 x, s32 y)
{
    if (x < 0 || y < 0 || x >= DUNGEON_WIDTH || y >= DUNGEON_HEIGHT)
        return 0;
    return map[(y + MAP_OFFSET) * gBackupMapLayout.width + (x + MAP_OFFSET)];
}

// Would this set piece sit somewhere the floor already needs?
//
// The prototype knew nothing about any of this, because it painted a grid and
// the grid is all there is. In the game the same cells carry the exit, the
// arrival point, and every object event the floor placed - all of which are
// chosen by PrepareFloor BEFORE the blocks are written. A solid object over an
// item ball leaves it inside a wall; over the stairs it ends the run.
static bool8 PieceClearOfObjects(const struct RogueSetPiece *piece, s32 x, s32 y)
{
    s32 i, j;
    u32 k;

    for (j = 0; j < piece->height; j++)
    {
        for (i = 0; i < piece->width; i++)
        {
            s32 cx = x + i, cy = y + j;

            if ((cx == sStairsX && cy == sStairsY)
             || (cx == sSpawnX && cy == sSpawnY))
                return FALSE;
            for (k = 0; k < sTrainerCount; k++)
                if (cx == sTrainerX[k] && cy == sTrainerY[k])
                    return FALSE;
            for (k = 0; k < sItemCount; k++)
                if (cx == sItemX[k] && cy == sItemY[k])
                    return FALSE;
            for (k = 0; k < sRockCount; k++)
                if (cx == sRockX[k] && cy == sRockY[k])
                    return FALSE;
            for (k = 0; k < sBerryCount; k++)
                if (cx == sBerryX[k] && cy == sBerryY[k])
                    return FALSE;
            if (sEventCount && cx == sEventX && cy == sEventY)
                return FALSE;
            // Buried items are the one class with nothing visible on the map,
            // so a set piece over one is silent twice: the player never learns
            // it was there, and the Dowsing Machine points at solid wall.
            for (k = 0; k < sHiddenCount; k++)
                if (cx == sHiddenItems[k].x && cy == sHiddenItems[k].y)
                    return FALSE;
        }
    }
    return TRUE;
}

static bool8 PieceFits(const u16 *map, const struct RogueDungeonTheme *theme,
                       const struct RogueSetPiece *piece, s32 x, s32 y)
{
    s32 i, j;

    if (x < 0 || y < 1 || x + piece->width > DUNGEON_WIDTH
     || y + piece->height >= DUNGEON_HEIGHT)
        return FALSE;
    if (!PieceClearOfObjects(piece, x, y))
        return FALSE;

    if (piece->wallMounted)
    {
        // The top row replaces a run of capped wall face, so the piece grows
        // out of the wall instead of being stuck onto it.
        for (i = 0; i < piece->width; i++)
        {
            if (GetBlockMetatile(map, x + i, y) != theme->wall[WALL_FACE_MID]
             || GetBlockMetatile(map, x + i, y - 1) != theme->wall[WALL_CAP_MID])
                return FALSE;
        }
        for (j = 1; j < piece->height; j++)
            for (i = 0; i < piece->width; i++)
                if (IsWallAt(map, x + i, y + j))
                    return FALSE;
        // One clear row past the body, or it stands flush against the far wall
        // of a shallow room and reads as a bulge in it.
        for (i = 0; i < piece->width; i++)
            if (IsWallAt(map, x + i, y + piece->height))
                return FALSE;
        return TRUE;
    }

    // Free-standing: the stack and a one-block ring must all be open floor, or
    // the crates land against a wall and read as part of it.
    for (j = -1; j <= piece->height; j++)
        for (i = -1; i <= piece->width; i++)
            if (IsWallAt(map, x + i, y + j))
                return FALSE;
    return TRUE;
}

// One pass, no candidate list, and every fitting spot equally likely however
// many there turn out to be. The prototype collected candidates into a Python
// list; 2304 cells of those would be real EWRAM here for no benefit.
static bool8 FindPieceSpot(const u16 *map, const struct RogueDungeonTheme *theme,
                           const struct RogueSetPiece *piece, u16 seed,
                           u32 salt, s32 *outX, s32 *outY)
{
    s32 x, y;
    u32 n = 0;

    for (y = 0; y < DUNGEON_HEIGHT; y++)
    {
        for (x = 0; x < DUNGEON_WIDTH; x++)
        {
            if (!PieceFits(map, theme, piece, x, y))
                continue;
            n++;
            if (DecorHash(seed, salt, n) % n == 0)
            {
                *outX = x;
                *outY = y;
            }
        }
    }
    return n != 0;
}

static bool8 PlaceOneSetPiece(u16 *map, const struct RogueDungeonTheme *theme,
                              const struct RogueSetPiece *piece, u16 seed,
                              u32 salt)
{
    u16 saved[DUNGEON_SET_PIECE_MAX_CELLS];
    s32 x = 0, y = 0, i, j;
    u32 attempt;

    if (piece->width * piece->height > DUNGEON_SET_PIECE_MAX_CELLS)
        return FALSE;

    for (attempt = 0; attempt < 8; attempt++)
    {
        if (!FindPieceSpot(map, theme, piece, seed, salt + attempt, &x, &y))
            return FALSE;

        for (j = 0; j < piece->height; j++)
            for (i = 0; i < piece->width; i++)
                saved[j * piece->width + i] = GetBlockRaw(map, x + i, y + j);

        for (j = 0; j < piece->height; j++)
        {
            for (i = 0; i < piece->width; i++)
            {
                // The top row of a wall-mounted piece was wall and keeps the
                // wall elevation; everything else was floor and stays at the
                // floor's, so the player walks around it on the level it
                // stands on.
                u8 elev = (piece->wallMounted && j == 0) ? theme->elevationWall
                                                         : theme->elevationFloor;

                SetBlock(map, x + i, y + j,
                         MakeBlock(piece->rows[j * piece->width + i], 1, elev));
            }
        }

        // Turning floor into collision can cut the walkable area in two, with
        // the stairs on the far side. It did on 3 of 200 seeds in the
        // prototype, once splitting 526 cells into 263 + 251, and nothing
        // downstream reports it - so the placement is rolled back and retried.
        if (DungeonOpenIsOnePiece(map))
            return TRUE;

        for (j = 0; j < piece->height; j++)
            for (i = 0; i < piece->width; i++)
                SetBlock(map, x + i, y + j, saved[j * piece->width + i]);
    }
    return FALSE;
}

static void ApplySetPieces(u16 *map, const struct RogueDungeonTheme *theme,
                           u16 seed)
{
    u32 i, k, placed = 0;

    for (i = 0; i < theme->pieceCount; i++)
    {
        for (k = 0; k < theme->pieces[i].count; k++)
        {
            if (!PlaceOneSetPiece(map, theme, &theme->pieces[i], seed,
                                  7 + i * 31 + k * 13))
                break;
        }
    }

    if (theme->clutterCount == 0)
        return;

    // Tries are capped rather than looped until satisfied: a floor with nowhere
    // to stand a crate must not spin.
    for (k = 0; k < (u32)theme->clutterMax * 6 && placed < theme->clutterMax; k++)
    {
        u16 hash = DecorHash(seed, 31 + k, 17 + k);

        if (PlaceOneSetPiece(map, theme,
                             &theme->clutter[hash % theme->clutterCount],
                             seed, 4096 + k * 7))
            placed++;
    }
}

// Terminates every maximal vertical run of column art, top and bottom.
//
// Not part of the autotile chain, because the cells it paints are not columns:
// they are the fill above and below one, and their own neighbour mask says
// "deep interior" like every other buried block. Run after the autotiler.
//
// Only ever writes over WALL_INTERIOR_MID. A run that ends against a cap, a
// face or a corner already terminates against something, and overwriting that
// would put a wall end in the middle of joined art.
static void ApplyColumnEnds(u16 *map, const struct RogueDungeonTheme *theme)
{
    u16 fill = theme->wall[WALL_INTERIOR_MID];
    s32 x, y;
    u32 i;

    if (theme->columnEndCount == 0)
        return;

    for (x = 0; x < DUNGEON_WIDTH; x++)
    {
        y = 0;
        while (y < DUNGEON_HEIGHT)
        {
            u16 column = GetBlockMetatile(map, x, y);
            s32 top = y;

            for (i = 0; i < theme->columnEndCount; i++)
            {
                if (theme->columnEnds[i].column == column)
                    break;
            }
            if (i == theme->columnEndCount)
            {
                y++;
                continue;
            }

            while (y + 1 < DUNGEON_HEIGHT
                && GetBlockMetatile(map, x, y + 1) == column)
                y++;

            if (theme->columnEnds[i].head != 0
             && GetBlockMetatile(map, x, top - 1) == fill
             && IsWallAt(map, x, top - 1))
                SetBlock(map, x, top - 1,
                         MakeBlock(theme->columnEnds[i].head, 1,
                                   theme->elevationWall));

            if (theme->columnEnds[i].foot != 0
             && GetBlockMetatile(map, x, y + 1) == fill
             && IsWallAt(map, x, y + 1))
                SetBlock(map, x, y + 1,
                         MakeBlock(theme->columnEnds[i].foot, 1,
                                   theme->elevationWall));
            y++;
        }
    }
}

// Wall furniture stamped as the three-row assembly vanilla actually draws: a
// cap row, the wall face, and the floor block below it.
//
// No `taken` set is needed to stop two stamps overlapping. The pass scans west
// to east and a placed stamp replaces the face metatile it landed on, so the
// "is this cell still a plain face" test that admits a stamp is the same test
// that rejects one over its own left half. The cap test reads the live map for
// the same reason - a stamp that already wrote its own cap will not take a
// second one.
static void ApplyWallStamps(u16 *map, const struct RogueDungeonTheme *theme,
                            u16 seed)
{
    u16 face = theme->wall[WALL_FACE_MID];
    u16 cap = theme->wall[WALL_CAP_MID];
    s32 x, y;
    u32 i, k;

    if (theme->stampCount == 0)
        return;

    for (y = 0; y < DUNGEON_HEIGHT; y++)
    {
        for (x = 0; x < DUNGEON_WIDTH; x++)
        {
            u8 fits[DUNGEON_MAX_WALL_STAMPS];
            u32 nfits = 0;
            u16 hash = DecorHash(seed, x, y);
            const struct RogueWallStamp *stamp;

            for (i = 0; i < theme->stampCount && nfits < ARRAY_COUNT(fits); i++)
            {
                bool8 ok = TRUE;

                stamp = &theme->stamps[i];
                for (k = 0; k < stamp->width; k++)
                {
                    s32 cx = x + k;

                    // A visible wall face with open floor under it, so the
                    // piece is actually seen and has somewhere to stand.
                    if (cx >= DUNGEON_WIDTH
                     || GetBlockMetatile(map, cx, y) != face
                     || IsWallAt(map, cx, y + 1))
                    {
                        ok = FALSE;
                        break;
                    }
                    if (stamp->needsFloorAbove ? IsWallAt(map, cx, y - 1)
                                               : GetBlockMetatile(map, cx, y - 1) != cap)
                    {
                        ok = FALSE;
                        break;
                    }
                }
                if (ok)
                    fits[nfits++] = i;
            }
            if (nfits == 0)
                continue;

            // Choose first, THEN gate on that piece's own density. Gating
            // first would hand every thin partition to the vent, since it is
            // the only piece that fits one - which is exactly what a single
            // shared gate did.
            stamp = &theme->stamps[fits[(hash >> 8) % nfits]];
            if ((hash >> 3) % 100 >= stamp->chance)
                continue;

            for (k = 0; k < stamp->width; k++)
            {
                s32 cx = x + k;

                if (stamp->cap[k] != 0)
                    SetBlock(map, cx, y - 1,
                             MakeBlock(stamp->cap[k], 1, theme->elevationWall));
                SetBlock(map, cx, y,
                         MakeBlock(stamp->face[k], 1, theme->elevationWall));
                // Passable, always: the floor row is the piece standing on the
                // ground, not the ground becoming solid.
                if (stamp->below[k] != 0)
                    SetBlock(map, cx, y + 1,
                             MakeBlock(stamp->below[k], 0, theme->elevationFloor));
            }
        }
    }
}

// Second pass over the carved grid, choosing each wall's art from its
// neighbours. Must run after all carving is done.
static void ApplyWallAutotiling(u16 *map, const struct RogueDungeonTheme *theme)
{
    s32 x, y;

    for (y = 0; y < DUNGEON_HEIGHT; y++)
    {
        for (x = 0; x < DUNGEON_WIDTH; x++)
        {
            bool8 openNorth, openSouth, openWest, openEast;
            u16 metatile, cap;

            if (!IsWallAt(map, x, y))
                continue;

            openNorth = !IsWallAt(map, x, y - 1);
            openSouth = !IsWallAt(map, x, y + 1);
            openWest  = !IsWallAt(map, x - 1, y);
            openEast  = !IsWallAt(map, x + 1, y);

            // A wall one block thick has floor on opposite sides, and vanilla
            // has no art for it. These must be tested before the face and north
            // cases, which would match on a single open side and shade only
            // that edge. An end of a run is a sliver with a third side open.
            if (openNorth && openSouth && openWest && openEast)
            {
                metatile = theme->wall[WALL_SLIVER_ISOLATED];
            }
            else if (openNorth && openSouth)
            {
                if (openWest)
                    metatile = theme->wall[WALL_SLIVER_HORZ_L];
                else if (openEast)
                    metatile = theme->wall[WALL_SLIVER_HORZ_R];
                else
                    metatile = theme->wall[WALL_SLIVER_HORZ];
            }
            else if (openWest && openEast)
            {
                if (openNorth)
                    metatile = theme->wall[WALL_SLIVER_VERT_TOP];
                else if (openSouth)
                    metatile = theme->wall[WALL_SLIVER_VERT_BOT];
                else if (theme->wall[WALL_SLIVER_VERT_BOT_UPPER] != 0
                      && IsSliverVertBottom(map, x, y + 1))
                    metatile = theme->wall[WALL_SLIVER_VERT_BOT_UPPER];
                else
                    metatile = theme->wall[WALL_SLIVER_VERT];
            }
            else if (openSouth)
            {
                // Floor below, so this is the wall face the camera sees. Takes
                // priority over every other edge - it is the most visible one.
                if (openWest)
                    metatile = theme->wall[WALL_FACE_LEFT];
                else if (openEast)
                    metatile = theme->wall[WALL_FACE_RIGHT];
                else
                    metatile = theme->wall[WALL_FACE_MID];
            }
            else if (openNorth)
            {
                // Floor above - the bottom boundary of a room.
                if (openWest)
                    metatile = theme->wall[WALL_NORTH_LEFT];
                else if (openEast)
                    metatile = theme->wall[WALL_NORTH_RIGHT];
                else
                    metatile = theme->wall[WALL_NORTH_MID];
            }
            else if ((cap = WallCapFor(map, theme, x, y)) != 0)
            {
                // Above a face, so this is the wall's top surface rather than
                // its side. Tested before the interior cases, which would
                // otherwise run the column art straight down into the face and
                // leave the wall one metatile tall where vanilla is two.
                //
                // Deliberately AFTER openNorth: vanilla never caps a wall with
                // floor above it (0x21F sits under void or the map edge 89% of
                // the time, and under floor never), so a room's south boundary
                // keeps its own art.
                metatile = cap;
            }
            else if (openWest)
            {
                metatile = theme->wall[WALL_INTERIOR_LEFT];
            }
            else if (openEast)
            {
                metatile = theme->wall[WALL_INTERIOR_RIGHT];
            }
            else if (!IsWallAt(map, x + 1, y + 1))
            {
                // Every cardinal is wall, so only a diagonal can be open. These
                // are the outer corners of a room; without them the outline
                // notches at the corners. Slots are named for the OPEN diagonal.
                metatile = theme->wall[WALL_CORNER_OPEN_SE];
            }
            else if (!IsWallAt(map, x - 1, y + 1))
            {
                metatile = theme->wall[WALL_CORNER_OPEN_SW];
            }
            else if (!IsWallAt(map, x - 1, y - 1))
            {
                metatile = theme->wall[WALL_CORNER_OPEN_NW];
            }
            else if (!IsWallAt(map, x + 1, y - 1))
            {
                // Split from the case above. They shared a slot, which is fine
                // while a tileset draws both the same and wrong the moment one
                // has a distinct north-east corner.
                metatile = theme->wall[WALL_CORNER_OPEN_NE];
            }
            else
            {
                metatile = theme->wall[WALL_INTERIOR_MID];
            }

            SetBlock(map, x, y, MakeBlock(metatile, 1, theme->elevationWall));
        }
    }
}

// The three cosmetic passes, in the one order they may run in.
//
// These used to sit at the bottom of ApplyWallAutotiling, on the reasoning that
// it was the single point every cave-generator path passed through. That was
// true and still left the WOODS with none of them: DUNGEON_GEN_WOODS stamps
// whole 2x2 cells, needs no autotiling, and returned from WriteFloorBlocks
// before any of this - so for one theme out of fourteen, patches, skirts and
// decor silently did not exist. A pass hosted inside the function that happens
// to precede it is only reachable by the callers of THAT function.
//
// Every caller must still run this BEFORE placing the stairs, which is what
// stops a decoration painting over the exit. That is now a rule the call sites
// keep rather than one the nesting enforced for them.
//
// Order matters. Patches lay the floor region down first, then a wall's own
// skirt paints over it where a theme has both, then decor swaps individual
// blocks. Decor keys on the painted metatile, so it sees patch tiles as their
// own thing and will not put a floor variant on a sand drift.
static void ApplyCosmeticPasses(u16 *map, const struct RogueDungeonTheme *theme)
{
    // A wall pass, not a cosmetic one, but it is hosted here for the reason
    // above: this is the function every generator path is required to call
    // after autotiling and before the stairs, and ApplyWallAutotiling is not.
    ApplyColumnEnds(map, theme);
    ApplyFloorPatches(map, theme, VarGet(VAR_ROGUE_DUNGEON_SEED));
    ApplySkirts(map, theme);
    // Before the stamps, as the prototype has it: a set piece consumes the
    // capped wall face it grows out of, and the stamp pass must not have put a
    // bookcase there first.
    ApplySetPieces(map, theme, VarGet(VAR_ROGUE_DUNGEON_SEED));
    // One or the other, never both - a theme whose furniture is three rows tall
    // would otherwise get a second, differently-aligned set of the same objects
    // from the single-block pass.
    ApplyDecor(map, theme, VarGet(VAR_ROGUE_DUNGEON_SEED));
    ApplyWallStamps(map, theme, VarGet(VAR_ROGUE_DUNGEON_SEED));
}


static bool8 RoomsOverlap(const struct DungeonRoom *a, const struct DungeonRoom *b)
{
    // one block of padding so rooms never share a wall
    return !(a->x + a->w + 1 < b->x || b->x + b->w + 1 < a->x
          || a->y + a->h + 1 < b->y || b->y + b->h + 1 < a->y);
}

// A corridor is `corridorWidth` blocks across, centred on the path. Widening
// it is the cheapest way to make a theme read as open: room size alone tops out
// near 35% coverage because bigger rooms simply stop fitting, whereas 3-wide
// corridors take the same rooms from 24% to 43%.
//
// SetBlock drops out-of-range writes, so a wide corridor near the edge is
// clipped rather than wrapping, and IsWallAt still treats off-map as wall so
// the outline closes.
static void CarveCorridorBlock(u16 *map, const struct RogueDungeonTheme *theme,
                               s32 x, s32 y, s32 width)
{
    s32 dx, dy, half = width / 2;

    for (dy = -half; dy <= half; dy++)
        for (dx = -half; dx <= half; dx++)
            CarveFloor(map, theme, x + dx, y + dy);
}

static void CarveCorridor(u16 *map, const struct RogueDungeonTheme *theme,
                          s32 x0, s32 y0, s32 x1, s32 y1)
{
    s32 width = theme->corridorWidth ? theme->corridorWidth : 1;
    s32 x = x0, y = y0;

    while (x != x1)
    {
        CarveCorridorBlock(map, theme, x, y, width);
        x += (x1 > x) ? 1 : -1;
    }
    while (y != y1)
    {
        CarveCorridorBlock(map, theme, x, y, width);
        y += (y1 > y) ? 1 : -1;
    }
    CarveCorridorBlock(map, theme, x, y, width);
}

// One major battle per dungeon, in stock order: the eight gym leaders, the Elite
// Four, the Champion, and then Steven to finish the run. The _1 variants are the
// base gym battles rather than the rematch tiers; the rest have no such variants.
//
// TRAINER_STEVEN is Emerald's Meteor Falls superboss at levels 75-78, twenty
// above Wallace. That gap is deliberate - it is the whole point of the last ten
// floors - and the encounter curve climbs to meet it. See the curve constants.
static const u16 sDungeonBosses[] =
{
    TRAINER_ROXANNE_1, TRAINER_BRAWLY_1, TRAINER_WATTSON_1, TRAINER_FLANNERY_1,
    TRAINER_NORMAN_1,  TRAINER_WINONA_1, TRAINER_TATE_AND_LIZA_1, TRAINER_JUAN_1,
    TRAINER_SIDNEY, TRAINER_PHOEBE, TRAINER_GLACIA, TRAINER_DRAKE, TRAINER_WALLACE,
    TRAINER_STEVEN,
};

// Parallel to sDungeonBosses, so the boss looks like who it is.
static const u16 sDungeonBossGfx[] =
{
    OBJ_EVENT_GFX_ROXANNE, OBJ_EVENT_GFX_BRAWLY, OBJ_EVENT_GFX_WATTSON,
    OBJ_EVENT_GFX_FLANNERY, OBJ_EVENT_GFX_NORMAN, OBJ_EVENT_GFX_WINONA,
    OBJ_EVENT_GFX_TATE, OBJ_EVENT_GFX_JUAN,
    OBJ_EVENT_GFX_SIDNEY, OBJ_EVENT_GFX_PHOEBE, OBJ_EVENT_GFX_GLACIA,
    OBJ_EVENT_GFX_DRAKE, OBJ_EVENT_GFX_WALLACE,
    OBJ_EVENT_GFX_STEVEN,
};

// Parallel to sDungeonBosses. The eight gym entries are exactly what each leader
// hands over in the stock game.
//
// The Elite Four and Champion give no TM in the stock game, so the last five are
// invented: the signature type where the stock 50 has a TM for it, and Hyper
// Beam for the Champion because Water Pulse is already Juan's. Sidney is the
// awkward one - every Dark TM in Gen 3 is a status move - so he gives Taunt,
// which at least suits him. Set any of these to ITEM_NONE to give nothing.
static const u16 sDungeonBossTMs[] =
{
    ITEM_TM_ROCK_TOMB, ITEM_TM_BULK_UP,     ITEM_TM_SHOCK_WAVE, ITEM_TM_OVERHEAT,
    ITEM_TM_FACADE,    ITEM_TM_AERIAL_ACE,  ITEM_TM_CALM_MIND,  ITEM_TM_WATER_PULSE,
    ITEM_TM_TAUNT,     ITEM_TM_SHADOW_BALL, ITEM_TM_BLIZZARD,   ITEM_TM_DRAGON_CLAW,
    ITEM_TM_HYPER_BEAM,
    ITEM_NONE,  // Steven: the run ends on his floor and the bag is wiped with it
};

// Parallel to sDungeonBosses. ZERO MEANS "let the engine choose", which is the
// right answer for thirteen of the fourteen: GetBattleBGM in src/pokemon.c
// switches on the trainer's CLASS, and because this project fights the real
// stock trainers rather than copies, Leader already resolves to
// MUS_VS_GYM_LEADER, Elite Four to MUS_VS_ELITE_FOUR and Wallace's Champion to
// MUS_VS_CHAMPION with nothing to fix.
//
// STEVEN IS THE EXCEPTION, AND HE IS MISFILED IN THE STOCK DATA. His class is
// Rival, not Champion - see src/data/trainers.party - so the switch gives him
// MUS_VS_RIVAL, which is also what TRAINER_ROGUE_RIVAL gets on floor 110. The
// run's final boss shared a theme with the mini boss five floors above him.
//
// MUS_VS_FRONTIER_BRAIN is Emerald's own "this is the hardest fight in the
// game" track, and it is unreachable in this build because there is no Battle
// Frontier - so it costs nothing and is not already spoken for by anything the
// player will have heard. Steven is Emerald's superboss at levels 75-78 and the
// last ten floors exist to reach him, which is the same claim the music makes.
//
// A table rather than a switch, so re-scoring any boss is a row edit.
static const u16 sDungeonBossMusic[] =
{
    0, 0, 0, 0,  // Roxanne, Brawly, Wattson, Flannery
    0, 0, 0, 0,  // Norman, Winona, Tate & Liza, Juan
    0, 0, 0, 0,  // Sidney, Phoebe, Glacia, Drake
    0,           // Wallace
    MUS_VS_FRONTIER_BRAIN,  // Steven
};

STATIC_ASSERT(ARRAY_COUNT(sDungeonBossMusic) == ARRAY_COUNT(sDungeonBosses),
              BossMusicMustBeParallelToBosses);

// Called from GetBattleBGM. Returns 0 for anything that is not one of our
// bosses, which is what leaves every other battle on the engine's own choice.
//
// Keyed on the TRAINER ID rather than on the floor, deliberately. A floor test
// would have to trust VAR_ROGUE_DUNGEON_FLOOR to be meaningful at the moment
// the music is picked, and that var keeps its value at the rest stop and
// through the Safari; a trainer id cannot be ambiguous, and these fourteen are
// reserved for boss floors anyway - gen_trainer_table.py excludes every one of
// them from the random opponent pool.
u16 RogueDungeon_GetBossBGM(u16 trainerId)
{
    u32 i;

    for (i = 0; i < ARRAY_COUNT(sDungeonBosses); i++)
    {
        if (sDungeonBosses[i] == trainerId)
            return sDungeonBossMusic[i];
    }

    return 0;
}

// Parallel to sDungeonBosses again, and this one costs NO NEW ART AT ALL.
//
// BattleSetup_GetEnvironmentId derives the backdrop from the metatile under the
// player and gMapHeader.mapType, and every dungeon floor is MAP_TYPE_UNDERGROUND
// - so the whole run resolves to CAVE, or GRASS/LONG_GRASS on an encounter
// surface and POND on the water themes. Fourteen themes and fourteen bosses
// collapse onto about four backdrops, and a gym leader fights in front of the
// same cave wall as the trainer two floors above.
//
// Vanilla already ships the right ones and this build simply never asked for
// them. The Elite Four share ENVIRONMENT_BACKGROUND(Stadium) and differ only by
// PALETTE, and all of it is in the ROM today - verified against PokeSMD.map,
// not against the enum, because an enum entry proves nothing about what linked.
//
// Steven takes CHAMPION rather than a stadium of his own: there is no STEVEN
// environment, and the run ends on his floor, so the champion stadium is the
// closest thing to a final-battle backdrop that costs nothing.
//
// BATTLE_ENVIRONMENT_COUNT is the "no override" sentinel rather than 0, because
// 0 is BATTLE_ENVIRONMENT_GRASS and a real answer.
#define DUNGEON_ENV_DEFAULT BATTLE_ENVIRONMENT_COUNT

static const u8 sDungeonBossEnvironment[] =
{
    // The eight gym leaders, on vanilla's own gym-leader interior.
    BATTLE_ENVIRONMENT_LEADER, BATTLE_ENVIRONMENT_LEADER,
    BATTLE_ENVIRONMENT_LEADER, BATTLE_ENVIRONMENT_LEADER,
    BATTLE_ENVIRONMENT_LEADER, BATTLE_ENVIRONMENT_LEADER,
    BATTLE_ENVIRONMENT_LEADER, BATTLE_ENVIRONMENT_LEADER,
    // The Elite Four, each with their own stadium palette - except Glacia.
    //
    // SHE IS THE ONE DELIBERATE EXCEPTION. Her dungeon is nine floors of ice
    // and her arena is the tenth, so BATTLE_ENVIRONMENT_GLACIA - a recoloured
    // stadium - throws away everything the descent has been building. The
    // frozen cave is the more honest backdrop for her and it is the same one
    // her floors already use, so the run does not change scenery for its own
    // boss. Her stadium palette is still in the ROM if this reads wrong.
    BATTLE_ENVIRONMENT_SIDNEY, BATTLE_ENVIRONMENT_PHOEBE,
    BATTLE_ENVIRONMENT_FROZEN_DEPTHS, BATTLE_ENVIRONMENT_DRAKE,
    // Wallace, then Steven.
    BATTLE_ENVIRONMENT_CHAMPION,
    BATTLE_ENVIRONMENT_CHAMPION,
};

STATIC_ASSERT(ARRAY_COUNT(sDungeonBossEnvironment) == ARRAY_COUNT(sDungeonBosses),
              BossEnvironmentMustBeParallelToBosses);

// Called from BattleMainCB2. Returns DUNGEON_ENV_DEFAULT for anything that is
// not one of our bosses, which leaves every other battle on the engine's own
// metatile-derived answer.
//
// Keyed on the trainer id for the same reason the music is: a floor test would
// have to trust VAR_ROGUE_DUNGEON_FLOOR to mean something at the moment the
// backdrop is picked, and these fourteen ids are reserved for boss floors.
u8 RogueDungeon_GetBossEnvironment(u16 trainerId)
{
    u32 i;

    for (i = 0; i < ARRAY_COUNT(sDungeonBosses); i++)
    {
        if (sDungeonBosses[i] == trainerId)
            return sDungeonBossEnvironment[i];
    }

    return DUNGEON_ENV_DEFAULT;
}

// Is the player standing on the floor the run thinks they are on? Compares the
// live map against the one MapForFloor picks, so the rest stop, the Safari and
// the game room all answer no even though VAR_ROGUE_DUNGEON_FLOOR still holds a
// perfectly good floor number in all three.
//
// THE THEME OVERRIDE NEEDS THIS AND THE BOSS OVERRIDE DID NOT, which is the
// whole reason it exists. A boss lookup is keyed on a trainer id that only ever
// appears on a boss floor and so cannot fire anywhere else; a theme lookup is
// keyed on the floor counter alone, and without this a Safari battle would take
// whichever theme the counter happened to be sitting in.
static bool8 IsOnDungeonFloor(u16 floor)
{
    u16 map = MapForFloor(floor);

    return gSaveBlock1Ptr->location.mapGroup == MAP_GROUP(map)
        && gSaveBlock1Ptr->location.mapNum == MAP_NUM(map);
}

// Called from BattleMainCB2. The one place that decides whether this project
// has an opinion about the backdrop, and the order is the design:
//
//   1. A BOSS wins. Flannery should stand in vanilla's gym-leader interior even
//      though her floor is a lava cave - she is the more specific fact. Flip
//      these two if that reads wrong on screen; it is one line.
//   2. Then the THEME, for every other battle on that floor, wild or trainer.
//   3. Otherwise the sentinel, and the engine's metatile answer stands.
//
// isTrainerBattle is passed rather than read here because
// TRAINER_BATTLE_PARAM.opponentA IS NOT CLEARED between battles. On a wild
// encounter it still holds the last trainer fought, so a boss lookup against it
// would give a wild Numel the champion stadium as soon as the player had beaten
// a champion.
u8 RogueDungeon_GetBattleEnvironment(bool8 isTrainerBattle, u16 trainerId)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    const struct RogueDungeonTheme *theme;

    if (isTrainerBattle)
    {
        u8 boss = RogueDungeon_GetBossEnvironment(trainerId);

        if (boss != DUNGEON_ENV_DEFAULT)
            return boss;
    }

    if (!IsOnDungeonFloor(floor))
        return DUNGEON_ENV_DEFAULT;

    theme = ThemeForFloor(floor);

    // Zero is "unset" here, not BATTLE_ENVIRONMENT_GRASS - see the field's note
    // in rogue_dungeon.h for why this sentinel differs from the boss table's.
    if (theme != NULL && theme->battleEnvironment != 0)
        return theme->battleEnvironment;

    return DUNGEON_ENV_DEFAULT;
}

// Mini bosses are picked by level from sRogueDungeonMiniBosses, not from a
// fixed list. A fixed list meant the floor-5 mini boss was whatever grunt
// happened to be in it - which was an Aqua Hideout one, so a level 31 Zubat
// against a player still around level 10.
//
// Slightly above the floor target, since it is still a boss.
#define DUNGEON_MINIBOSS_LEVEL_BONUS 3

static u16 PickMiniBossForLevel(u8 target, u16 *gfxId)
{
    u32 i, first = 0, last = 0;
    u32 window;

    for (window = 3; window < 64; window += 4)
    {
        bool8 found = FALSE;

        for (i = 0; i < ARRAY_COUNT(sRogueDungeonMiniBosses); i++)
        {
            u32 level = sRogueDungeonMiniBosses[i].avgLevel;

            if (level + window >= target && level <= target + window)
            {
                if (!found)
                {
                    first = i;
                    found = TRUE;
                }
                last = i;
            }
        }

        if (found)
        {
            u32 pick = first + (DungeonRandom() % (last - first + 1));

            *gfxId = sRogueDungeonMiniBosses[pick].gfxId;
            return sRogueDungeonMiniBosses[pick].trainerId;
        }
    }

    *gfxId = sRogueDungeonMiniBosses[0].gfxId;
    return sRogueDungeonMiniBosses[0].trainerId;
}

bool8 RogueDungeon_IsBossFloor(u16 floor)
{
    return IsMiniBossFloor(floor) || IsDungeonBossFloor(floor);
}

// The Wave Charm. A run grants no field moves at all, so the Safari Zone's
// water surface used to be reachable only on a Mudkip run - Mudkip learns Surf
// at 30 and Swampert has it at 1, and no other starter pick learns it by level.
// Sixteen generated, check-guarded species sat behind one of thirty picks.
//
// This does not make anything else surfable. The ocean theme already crosses
// water because its floor metatile is water and the player arrives on a surf
// blob; the seafloor is a property of the MAP, not a move; and the jungle's
// water is MB_PUDDLE precisely so it is not a second encounter surface. So the
// only thing this opens is the Safari's water, which is the point.
bool32 RogueDungeon_HasSurfTool(void)
{
    return CheckBagHasItem(ITEM_ROGUE_SURF_TOOL, 1);
}

// Boss floors skip rooms and corridors entirely: a single centred arena, the
// boss standing in the open, and no exit at all until it is beaten.
static void PrepareArenaFloor(u16 floor)
{
    u32 dungeon = DungeonIndexOf(floor);
    const struct RogueDungeonTheme *theme = ThemeForFloor(floor);
    u8 w = DUNGEON_ARENA_WIDTH;
    u8 h = DUNGEON_ARENA_HEIGHT;
    u8 x0, y0;

    // A stamped theme paints in whole 2x2 cells, so its arena has to be even
    // sized and even aligned or every stamp lands half off the room.
    if (theme->generator == DUNGEON_GEN_WOODS)
    {
        // MODULO, NOT `&= ~1`. That mask only rounds down to a multiple of a
        // POWER OF TWO. At a 3-cell unit it still compiles, still runs, and
        // silently leaves every arena misaligned so each stamp lands a third of
        // a tree off its room.
        w -= w % DUNGEON_WOODS_CELL;
        h -= h % DUNGEON_WOODS_CELL;
    }

    x0 = (DUNGEON_WIDTH - w) / 2;
    y0 = (DUNGEON_HEIGHT - h) / 2;
    if (theme->generator == DUNGEON_GEN_WOODS)
    {
        x0 -= x0 % DUNGEON_WOODS_CELL;
        y0 -= y0 % DUNGEON_WOODS_CELL;
    }

    sRoomCount = 1;
    sRooms[0].x = x0;
    sRooms[0].y = y0;
    sRooms[0].w = w;
    sRooms[0].h = h;

    // Player at the south end, boss at the north, so the two face off across
    // the arena. The boss is deliberately NOT in a chokepoint - a defeated
    // trainer object still blocks movement, and would wall the exit off.
    sSpawnX = x0 + w / 2;
    sSpawnY = y0 + h - 2;

    sStairsX = x0 + w / 2;
    sStairsY = y0 + 1;
    sStairsMetatile = theme->stairsDown;

    sTrainerCount = 1;
    sTrainerX[0] = x0 + w / 2;
    sTrainerY[0] = y0 + 3;

    if (IsDungeonBossFloor(floor))
    {
        // IDENTITY here, and SLOT in the branch below. The boss and its sprite
        // belong to whichever dungeon is standing at this depth; "is this the
        // last dungeon in the run" is a question about the position, and the
        // finale never permutes anyway.
        u32 identity = DungeonForSlot(dungeon);

        sTrainerIds[0] = sDungeonBosses[identity % ARRAY_COUNT(sDungeonBosses)];
        sTrainerGfx[0] = sDungeonBossGfx[identity % ARRAY_COUNT(sDungeonBossGfx)];
    }
    else if (dungeon == DUNGEON_COUNT - 1)
    {
        // The last dungeon's mini boss is the rival, fixed rather than picked by
        // level: no stock trainer comes close to floor 110, and the run should
        // not spend its second-to-last arena on an anonymous hiker.
        sTrainerIds[0] = TRAINER_ROGUE_RIVAL;
        sTrainerGfx[0] = OBJ_EVENT_GFX_MAY_NORMAL;
    }
    else
    {
        // Only gym dungeons reach here: the Elite Four's are five floors long,
        // so their fifth floor is the boss's own and they have no mini boss slot
        // at all. The table carries its own sprite, so a female grunt looks
        // female and a leader looks like the leader rather than one of their
        // grunts.
        sTrainerIds[0] = PickMiniBossForLevel(
            FloorTargetLevel(floor) + DUNGEON_MINIBOSS_LEVEL_BONUS,
            &sTrainerGfx[0]);
    }
}

// specialvar target. The last floor of a dungeon sends the player to the rest
// stop to heal rather than straight down another set of stairs.
//
// Returning this rather than writing gSpecialVar_Result is load-bearing: see the
// note in rogue_dungeon.h. Written the other way it answered with the low half
// of a return address, the warp branch was never taken, and beating a gym leader
// left the player sealed in an arena that by design has no stairs.
u16 RogueDungeon_IsDungeonEndFloor(void)
{
    return IsDungeonBossFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR));
}

// specialvar target. Steven is the end of the run, so his arena neither opens an
// exit nor warps to a rest stop - there is nothing after it.
//
// The script asks this BEFORE RogueDungeon_IsDungeonEndFloor, which also answers
// TRUE here: the last floor of the last dungeon is both.
u16 RogueDungeon_IsRunCompleteFloor(void)
{
    return VarGet(VAR_ROGUE_DUNGEON_FLOOR) >= DUNGEON_TOTAL_FLOORS - 1;
}

// specialvar target. Hands over the gym leader's TM, the way the stock game
// does. Buffers the item name into gStringVar1 and returns TRUE only if the item
// actually reached the bag, so the script never claims a TM the player has not
// got.
u16 RogueDungeon_GiveBossTM(void)
{
    // IDENTITY: the TM is the boss's, so it travels with the boss. Under the
    // shuffle that means a band-of-two swap can hand over Bulk Up before Rock
    // Tomb, which is one dungeon of drift and deliberately not corrected.
    u32 dungeon = DungeonForSlot(DungeonIndexOf(VarGet(VAR_ROGUE_DUNGEON_FLOOR)));
    u16 item;

    if (!IsDungeonBossFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR)))
        return FALSE;

    item = sDungeonBossTMs[dungeon % ARRAY_COUNT(sDungeonBossTMs)];
    if (item == ITEM_NONE)
        return FALSE;

    if (!AddBagItem(item, 1))
        return FALSE;

    CopyItemName(item, gStringVar1);
    return TRUE;
}

// Called from the boss post-battle script. The exit does not exist until now,
// which is what forces the fight - there is no other way off an arena floor.
void RogueDungeon_OnBossDefeated(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);

    // A dungeon's last floor warps to the rest stop instead of opening an exit,
    // so drawing one here would give the player a way to skip the heal. Steven's
    // floor ends the run outright and needs an exit even less.
    if (IsDungeonBossFloor(floor))
        return;

    // theme->elevationFloor, NOT DUNGEON_ELEVATION_FLOOR. The ocean walks at
    // DUNGEON_ELEVATION_WATER, and IsElevationMismatchAt blocks a move between
    // two DIFFERENT non-zero elevations - so writing the exit at 3 on a floor
    // the player crosses at 1 puts the whirlpool somewhere they cannot step.
    // An arena floor has no other way out, so that stranded the run outright.
    //
    // Both generation paths already read it from the theme; this one did not.
    MapGridSetMetatileEntryAt(sStairsX + MAP_OFFSET, sStairsY + MAP_OFFSET,
                              MakeBlock(sStairsMetatile, 0,
                                        ThemeForFloor(floor)->elevationFloor));
    DrawWholeMapView();
}

// The table is sorted by average party level, so candidates for a target level
// form a contiguous run. Widens the window until something matches rather than
// failing - the low end of the table is thin, as the stock game has few
// trainers below level 10.

// Returns the trainer, and writes its overworld sprite through gfxOut when the
// theme brought its own. gfxOut is left alone for the stock table, whose
// entries have no sprite and leave the caller alternating theme->trainerGfx.
// THE OVERWORLD SPRITE COMES FROM THE TRAINER'S CLASS, and this table is why a
// floor is no longer four hikers.
//
// Before this, a generated trainer took theme->trainerGfx or trainerGfxAlt by
// slot parity, so a floor showed at most TWO figures and a theme that set
// neither showed OBJ_EVENT_GFX_HIKER for every single battle. The class is
// already known - it rides on the trainer id, which is what picks the BATTLE
// pic - so deriving the overworld sprite from it costs one lookup and makes the
// two halves agree. Walking up to a Lass and fighting a Lass is the point; the
// variety is the side effect.
//
// This is the same fault the seafloor's own trainer table was invented to fix,
// generalised. A theme table still wins where one exists, because a diver needs
// a diver sprite and no stock class describes that.
//
// GENDERED, because struct Trainer carries a gender bit and half these classes
// come in pairs. A Swimmer drawn male in the overworld opening a battle as a
// woman is exactly the mismatch the divers hit.
//
// THE ALT IS A SECOND SPRITE FOR THE SAME CLASS, taken from the FRLG set that
// is now compiled in, chosen by a bit of the trainer id so it is stable for a
// given trainer rather than varying by where they stand. Ten classes have one,
// which roughly doubles the apparent variety on the floors those classes are
// common on. Zero means no alt and the primary is always used.
//
// Zero for a class means NO OPINION, and the caller falls back to the theme's
// own sprites exactly as before - so the bosses, the Frontier brains and the
// rivals, none of which the generated pool draws from, are simply absent here.
struct RogueClassGfx
{
    u16 male, female;
    u16 maleAlt, femaleAlt;
};

static const struct RogueClassGfx sTrainerClassGfx[TRAINER_CLASS_COUNT] =
{
    [TRAINER_CLASS_HIKER]        = { OBJ_EVENT_GFX_HIKER, OBJ_EVENT_GFX_HIKER,
                                     OBJ_EVENT_GFX_HIKER_FRLG, OBJ_EVENT_GFX_HIKER_FRLG },
    [TRAINER_CLASS_TEAM_AQUA]    = { OBJ_EVENT_GFX_AQUA_MEMBER_M, OBJ_EVENT_GFX_AQUA_MEMBER_F },
    [TRAINER_CLASS_AQUA_ADMIN]   = { OBJ_EVENT_GFX_AQUA_MEMBER_M, OBJ_EVENT_GFX_AQUA_MEMBER_F },
    [TRAINER_CLASS_TEAM_MAGMA]   = { OBJ_EVENT_GFX_MAGMA_MEMBER_M, OBJ_EVENT_GFX_MAGMA_MEMBER_F },
    [TRAINER_CLASS_MAGMA_ADMIN]  = { OBJ_EVENT_GFX_MAGMA_MEMBER_M, OBJ_EVENT_GFX_MAGMA_MEMBER_F },
    [TRAINER_CLASS_PKMN_BREEDER] = { OBJ_EVENT_GFX_POKEFAN_M, OBJ_EVENT_GFX_POKEFAN_F },
    [TRAINER_CLASS_POKEFAN]      = { OBJ_EVENT_GFX_POKEFAN_M, OBJ_EVENT_GFX_POKEFAN_F },
    [TRAINER_CLASS_COOLTRAINER]  = { OBJ_EVENT_GFX_COOLTRAINER_M, OBJ_EVENT_GFX_COOLTRAINER_F },
    [TRAINER_CLASS_COOLTRAINER_2]= { OBJ_EVENT_GFX_COOLTRAINER_M, OBJ_EVENT_GFX_COOLTRAINER_F },
    [TRAINER_CLASS_DRAGON_TAMER] = { OBJ_EVENT_GFX_COOLTRAINER_M, OBJ_EVENT_GFX_COOLTRAINER_F },
    [TRAINER_CLASS_EXPERT]       = { OBJ_EVENT_GFX_EXPERT_M, OBJ_EVENT_GFX_EXPERT_F },
    [TRAINER_CLASS_BLACK_BELT]   = { OBJ_EVENT_GFX_BLACK_BELT, OBJ_EVENT_GFX_CRUSH_GIRL,
                                     OBJ_EVENT_GFX_BLACK_BELT_FRLG, 0 },
    [TRAINER_CLASS_BATTLE_GIRL]  = { OBJ_EVENT_GFX_CRUSH_GIRL, OBJ_EVENT_GFX_CRUSH_GIRL },
    [TRAINER_CLASS_HEX_MANIAC]   = { OBJ_EVENT_GFX_HEX_MANIAC, OBJ_EVENT_GFX_HEX_MANIAC },
    [TRAINER_CLASS_PSYCHIC]      = { OBJ_EVENT_GFX_PSYCHIC_M, OBJ_EVENT_GFX_HEX_MANIAC },
    [TRAINER_CLASS_AROMA_LADY]   = { OBJ_EVENT_GFX_PICNICKER, OBJ_EVENT_GFX_PICNICKER },
    [TRAINER_CLASS_RUIN_MANIAC]  = { OBJ_EVENT_GFX_MANIAC, OBJ_EVENT_GFX_MANIAC },
    [TRAINER_CLASS_POKEMANIAC]   = { OBJ_EVENT_GFX_MANIAC, OBJ_EVENT_GFX_MANIAC },
    [TRAINER_CLASS_BUG_MANIAC]   = { OBJ_EVENT_GFX_BUG_CATCHER, OBJ_EVENT_GFX_BUG_CATCHER },
    [TRAINER_CLASS_BUG_CATCHER]  = { OBJ_EVENT_GFX_BUG_CATCHER, OBJ_EVENT_GFX_BUG_CATCHER,
                                     OBJ_EVENT_GFX_BUG_CATCHER_FRLG, OBJ_EVENT_GFX_BUG_CATCHER_FRLG },
    [TRAINER_CLASS_INTERVIEWER]  = { OBJ_EVENT_GFX_REPORTER_M, OBJ_EVENT_GFX_REPORTER_F },
    [TRAINER_CLASS_TUBER_M]      = { OBJ_EVENT_GFX_TUBER_M, OBJ_EVENT_GFX_TUBER_M },
    [TRAINER_CLASS_TUBER_F]      = { OBJ_EVENT_GFX_TUBER_F, OBJ_EVENT_GFX_TUBER_F,
                                     OBJ_EVENT_GFX_TUBER_F_FRLG, OBJ_EVENT_GFX_TUBER_F_FRLG },
    [TRAINER_CLASS_LADY]         = { OBJ_EVENT_GFX_BEAUTY, OBJ_EVENT_GFX_BEAUTY },
    [TRAINER_CLASS_BEAUTY]       = { OBJ_EVENT_GFX_BEAUTY, OBJ_EVENT_GFX_BEAUTY,
                                     OBJ_EVENT_GFX_BEAUTY_FRLG, OBJ_EVENT_GFX_BEAUTY_FRLG },
    [TRAINER_CLASS_PARASOL_LADY] = { OBJ_EVENT_GFX_WOMAN_2, OBJ_EVENT_GFX_WOMAN_2 },
    [TRAINER_CLASS_RICH_BOY]     = { OBJ_EVENT_GFX_RICH_BOY, OBJ_EVENT_GFX_RICH_BOY },
    [TRAINER_CLASS_COLLECTOR]    = { OBJ_EVENT_GFX_RICH_BOY, OBJ_EVENT_GFX_RICH_BOY },
    [TRAINER_CLASS_GENTLEMAN]    = { OBJ_EVENT_GFX_GENTLEMAN, OBJ_EVENT_GFX_GENTLEMAN,
                                     OBJ_EVENT_GFX_GENTLEMAN_FRLG, OBJ_EVENT_GFX_GENTLEMAN_FRLG },
    [TRAINER_CLASS_GUITARIST]    = { OBJ_EVENT_GFX_BIKER, OBJ_EVENT_GFX_BIKER },
    [TRAINER_CLASS_KINDLER]      = { OBJ_EVENT_GFX_CAMPER, OBJ_EVENT_GFX_CAMPER },
    [TRAINER_CLASS_CAMPER]       = { OBJ_EVENT_GFX_CAMPER, OBJ_EVENT_GFX_CAMPER,
                                     OBJ_EVENT_GFX_CAMPER_FRLG, OBJ_EVENT_GFX_CAMPER_FRLG },
    [TRAINER_CLASS_PICNICKER]    = { OBJ_EVENT_GFX_PICNICKER, OBJ_EVENT_GFX_PICNICKER,
                                     OBJ_EVENT_GFX_PICNICKER_FRLG, OBJ_EVENT_GFX_PICNICKER_FRLG },
    [TRAINER_CLASS_PKMN_RANGER]  = { OBJ_EVENT_GFX_CAMPER, OBJ_EVENT_GFX_PICNICKER },
    [TRAINER_CLASS_BIRD_KEEPER]  = { OBJ_EVENT_GFX_CAMPER, OBJ_EVENT_GFX_PICNICKER },
    [TRAINER_CLASS_SCHOOL_KID]   = { OBJ_EVENT_GFX_SCHOOL_KID_M, OBJ_EVENT_GFX_LASS },
    [TRAINER_CLASS_YOUNGSTER]    = { OBJ_EVENT_GFX_YOUNGSTER, OBJ_EVENT_GFX_YOUNGSTER,
                                     OBJ_EVENT_GFX_YOUNGSTER_FRLG, OBJ_EVENT_GFX_YOUNGSTER_FRLG },
    [TRAINER_CLASS_LASS]         = { OBJ_EVENT_GFX_LASS, OBJ_EVENT_GFX_LASS,
                                     OBJ_EVENT_GFX_LASS_FRLG, OBJ_EVENT_GFX_LASS_FRLG },
    [TRAINER_CLASS_NINJA_BOY]    = { OBJ_EVENT_GFX_NINJA_BOY, OBJ_EVENT_GFX_NINJA_BOY },
    [TRAINER_CLASS_FISHERMAN]    = { OBJ_EVENT_GFX_FISHERMAN, OBJ_EVENT_GFX_FISHERMAN },
    [TRAINER_CLASS_SAILOR]       = { OBJ_EVENT_GFX_SAILOR, OBJ_EVENT_GFX_SAILOR,
                                     OBJ_EVENT_GFX_SAILOR_FRLG, OBJ_EVENT_GFX_SAILOR_FRLG },
    // The LAND variants deliberately. The plain SWIMMER_M/F are the in-water
    // sprites and a swimmer standing on cave floor in them reads as a bug.
    [TRAINER_CLASS_SWIMMER_M]    = { OBJ_EVENT_GFX_SWIMMER_M_LAND, OBJ_EVENT_GFX_SWIMMER_M_LAND },
    [TRAINER_CLASS_SWIMMER_F]    = { OBJ_EVENT_GFX_SWIMMER_F_LAND, OBJ_EVENT_GFX_SWIMMER_F_LAND },
    [TRAINER_CLASS_TRIATHLETE]   = { OBJ_EVENT_GFX_RUNNING_TRIATHLETE_M, OBJ_EVENT_GFX_RUNNING_TRIATHLETE_F },
    [TRAINER_CLASS_TWINS]        = { OBJ_EVENT_GFX_TWIN, OBJ_EVENT_GFX_TWIN },
    [TRAINER_CLASS_SR_AND_JR]    = { OBJ_EVENT_GFX_TWIN, OBJ_EVENT_GFX_TWIN },
    [TRAINER_CLASS_SIS_AND_BRO]  = { OBJ_EVENT_GFX_TWIN, OBJ_EVENT_GFX_TWIN },
    [TRAINER_CLASS_YOUNG_COUPLE] = { OBJ_EVENT_GFX_MAN_1, OBJ_EVENT_GFX_WOMAN_1 },
    [TRAINER_CLASS_WINSTRATE]    = { OBJ_EVENT_GFX_MAN_1, OBJ_EVENT_GFX_WOMAN_1 },
    [TRAINER_CLASS_OLD_COUPLE]   = { OBJ_EVENT_GFX_OLD_MAN, OBJ_EVENT_GFX_OLD_WOMAN },
    // The last six of the pool, found by check_trainer_sprites.py rather than by
    // reading the class list - gen_trainer_table.py excludes gym leaders and the
    // Elite Four but NOT the team leaders, so Maxie and Archie are ordinary
    // dungeon opponents and were falling through to a hiker.
    [TRAINER_CLASS_MAGMA_LEADER] = { OBJ_EVENT_GFX_MAXIE, OBJ_EVENT_GFX_MAXIE },
    [TRAINER_CLASS_AQUA_LEADER]  = { OBJ_EVENT_GFX_ARCHIE, OBJ_EVENT_GFX_ARCHIE },
    [TRAINER_CLASS_RS_PROTAG]    = { OBJ_EVENT_GFX_RUBY, OBJ_EVENT_GFX_SAPPHIRE },
};

// -> the overworld sprite for this trainer, or 0 when the class has no entry.
//
// The alt is chosen from BIT 1 of the trainer id rather than bit 0, because the
// stock table is sorted by average party level and adjacent ids are therefore
// adjacent in difficulty; bit 0 would make the two sprites alternate in lockstep
// with the level ramp on any floor that picks a contiguous run.
static u16 TrainerClassGfx(u16 trainerId)
{
    const struct Trainer *trainer = GetTrainerStructFromId(trainerId);
    enum TrainerClassID class = trainer->trainerClass;
    const struct RogueClassGfx *row;
    u16 gfx, alt;

    if (class >= TRAINER_CLASS_COUNT)
        return 0;

    row = &sTrainerClassGfx[class];
    gfx = trainer->gender ? row->female : row->male;
    alt = trainer->gender ? row->femaleAlt : row->maleAlt;

    if (alt != 0 && (trainerId & 2))
        return alt;

    return gfx;
}

static u16 PickTrainerForLevel(u8 target, const struct RogueDungeonTheme *theme,
                               u16 *gfxOut)
{
    const struct RogueThemeTrainer *themed = theme ? theme->trainers : NULL;
    u32 count = themed ? theme->trainerCount
                       : ARRAY_COUNT(sRogueDungeonTrainers);
    u32 i, first = 0, last = 0;
    u32 window;

    for (window = 3; window < 64; window += 4)
    {
        bool8 found = FALSE;

        for (i = 0; i < count; i++)
        {
            u32 level = themed ? themed[i].avgLevel
                               : sRogueDungeonTrainers[i].avgLevel;

            if (level + window >= target && level <= target + window)
            {
                if (!found)
                {
                    first = i;
                    found = TRUE;
                }
                last = i;
            }
        }

        if (found)
        {
            u32 pick = first + (DungeonRandom() % (last - first + 1));

            if (themed)
            {
                *gfxOut = themed[pick].gfxId;
                return themed[pick].trainerId;
            }
            return sRogueDungeonTrainers[pick].trainerId;
        }
    }

    if (themed)
    {
        *gfxOut = themed[0].gfxId;
        return themed[0].trainerId;
    }
    return sRogueDungeonTrainers[0].trainerId;
}

// Trainers stand in rooms the player does not start in, so the first room stays
// a safe landing spot.
// What an item ball can hold, as a floor BAND rather than a floor minimum.
//
// A minimum alone is the mistake the species pool already made and had to undo:
// entries unlocked by a prefix never retire, so Potions would still be the most
// common find on floor 100 because they are cheap and were there first. A band
// retires them, exactly the way the encounter window retires Zubat.
//
// Healing dominates the table on purpose, and gets stronger the deeper the run
// goes - that is the whole point of the feature. The count of balls per floor
// climbs too (see PlaceItems), so it is both better healing and more of it.
//
// Weights are relative among whichever entries are in band on a given floor, so
// they do not need to sum to anything.
struct RogueLootEntry
{
    u16 item;
    u8 weight;
    u8 minFloor;
    u8 maxFloor;   // exclusive; 255 means to the end of the run
    u8 quantity;
};

static const struct RogueLootEntry sLootConsumables[] =
{
    // The healing ladder. Bands overlap so a floor near a boundary can roll
    // either side of it and the change reads as a drift rather than a switch.
    //
    // WEIGHTS ROUGHLY HALVED, from 22/22/22/20/14. Healing is still meant to
    // dominate this table, and does - but at the old numbers a Potion was 50% of
    // every item ball on floors 1-11 and the potion family was 64% at floor 45,
    // which is not dominance, it is the same pickup over and over. Reported from
    // play as "it is all I have picked up this run".
    //
    // verify_loot_table.py now prints the per-item SHARE, because every check it
    // already had passed on the old numbers: it counted how many KINDS were in
    // band and measured HP per ball, and neither of those can see one entry
    // eating half the table.
    { ITEM_POTION,        12,   0,  30, 2 },
    { ITEM_SUPER_POTION,  12,  12,  60, 2 },
    { ITEM_HYPER_POTION,  12,  40, 100, 2 },
    // Both in twos, and that is not generosity - it is what stops the deepest
    // floors healing for LESS than the ones above them. A Hyper Potion arrives
    // in twos, so a single Max Potion is a downgrade in raw HP and a single
    // Full Restore is a downgrade again. verify_loot_table.py caught both.
    { ITEM_MAX_POTION,    11,  75, 255, 2 },
    { ITEM_FULL_RESTORE,   9,  95, 255, 2 },

    // Fainting is the thing that ends a run, so revives never retire once they
    // arrive - only the strength of them moves.
    //
    // WEIGHTS CUT ROUGHLY IN HALF, and the shrine is why. A revive in the loot
    // table at the old rate made the Memorial Shrine's sacrifice branch strictly
    // worse than a bag slot - trading 30% of the lead's maximum for one
    // half-health revival is only a decision if a revive is a thing you might
    // not have. Cutting the rate is what turned that branch from dominated into
    // the hardest choice on the floor.
    //
    // The pair moves TOGETHER and stays in proportion, because Max Revive is the
    // late-run answer and leaving it alone would have made the deep floors more
    // forgiving than the shallow ones - the inversion verify_loot_table.py exists
    // to catch.
    { ITEM_REVIVE,         6,  12, 255, 1 },
    { ITEM_MAX_REVIVE,     4,  80, 255, 1 },

    // Status. The single-status cures are an early-run answer and are gone by
    // the time Full Heal is common, which is the same shape as the potions.
    { ITEM_ANTIDOTE,       8,   0,  22, 2 },
    { ITEM_PARALYZE_HEAL,  8,   0,  22, 2 },
    { ITEM_AWAKENING,      6,   0,  22, 2 },
    { ITEM_FULL_HEAL,     10,   6, 255, 1 },

    // PP is the quiet way a long run dies - a full team with no moves left is
    // still a loss. Arrives late because early floors are short.
    { ITEM_ETHER,          8,  10, 255, 1 },
    { ITEM_MAX_ETHER,      6,  70, 255, 1 },
    { ITEM_ELIXIR,         5,  55, 255, 1 },
};

// What is BURIED, as opposed to what is left in a ball. Held items rather than
// consumables, so the dowsing machine is worth the fact that wearing it stops
// the player running.
//
// Banded like the consumables, but the bands mean something different. A potion
// retires because a better one replaces it; a held item retires because it stops
// being worth a slot. So the cheap ones close and the ones that stay good never
// do - Leftovers and the Choice items have no upper bound because there is no
// floor deep enough for them to be a bad find.
static const struct RogueLootEntry sLootHeld[] =
{
    // Tier 1 - things that help a level-10 starter survive to floor 10.
    { ITEM_QUICK_CLAW,    14,   0,  45, 1 },
    { ITEM_SHELL_BELL,    12,   0,  45, 1 },
    { ITEM_SITRUS_BERRY,  14,   0,  60, 1 },

    // Tier 2 - the first items that reward a plan rather than patching a
    // weakness. Eviolite closes early because a run's unevolved mon do not stay
    // unevolved, and it is dead weight once they have.
    { ITEM_EVIOLITE,      10,   5,  70, 1 },
    { ITEM_MUSCLE_BAND,   12,  10,  70, 1 },
    { ITEM_WISE_GLASSES,  12,  10,  70, 1 },
    { ITEM_SCOPE_LENS,    10,  25,  80, 1 },

    // Tier 3 - competitively real, and none of them ever retire.
    { ITEM_FOCUS_SASH,    10,  20, 255, 1 },
    { ITEM_ROCKY_HELMET,  10,  30, 255, 1 },
    { ITEM_EXPERT_BELT,   10,  35, 255, 1 },
    { ITEM_AIR_BALLOON,    8,  35, 255, 1 },
    { ITEM_CHOICE_SCARF,   9,  45, 255, 1 },
    { ITEM_CHOICE_BAND,    9,  50, 255, 1 },
    { ITEM_CHOICE_SPECS,   9,  50, 255, 1 },
    { ITEM_ASSAULT_VEST,   9,  60, 255, 1 },
    { ITEM_LIFE_ORB,       8,  65, 255, 1 },
    { ITEM_LEFTOVERS,     10,  70, 255, 1 },
};

// What can be buried that is not a held item. Deliberately just the two the
// starter roster needs - Clefairy and Pikachu both evolve by stone and by
// nothing else - because this list is picked from UNIFORMLY and every entry
// added divides the rate of the ones already here. The rest stop's counter
// stocks all ten stones instead, where a wide shelf costs nobody anything.
//
// No bands and no weights: a stone does not get better with depth and does not
// retire, so the banded struct the three loot tables share would be four
// fields of pretending. See DUNGEON_STONE_ODDS.
static const u16 sBuriedStones[] =
{
    ITEM_MOON_STONE,        // Clefairy -> Clefable
    ITEM_THUNDER_STONE,     // Pikachu -> Raichu, and Eevee -> Jolteon
};

// The rest stop's stone counter has no table of its own: it derives the item
// from the menu row, ITEM_FIRE_STONE + index, so its eleven-entry
// dynmultichoice costs one line instead of ten near-identical buy scripts.
// That only holds while the ten stones stay contiguous and in this order.
//
// A .inc cannot check itself and nothing else reads it, so the arithmetic is
// asserted here rather than trusted to the comment beside it - the same reason
// DUNGEON_HIDDEN_FIRST_ID is asserted above. Reordering items.h would
// otherwise silently sell the wrong stone, which is the kind of bug that
// survives a playthrough.
STATIC_ASSERT(ITEM_WATER_STONE   == ITEM_FIRE_STONE + 1, RogueStoneOrderWater);
STATIC_ASSERT(ITEM_THUNDER_STONE == ITEM_FIRE_STONE + 2, RogueStoneOrderThunder);
STATIC_ASSERT(ITEM_LEAF_STONE    == ITEM_FIRE_STONE + 3, RogueStoneOrderLeaf);
STATIC_ASSERT(ITEM_ICE_STONE     == ITEM_FIRE_STONE + 4, RogueStoneOrderIce);
STATIC_ASSERT(ITEM_SUN_STONE     == ITEM_FIRE_STONE + 5, RogueStoneOrderSun);
STATIC_ASSERT(ITEM_MOON_STONE    == ITEM_FIRE_STONE + 6, RogueStoneOrderMoon);
STATIC_ASSERT(ITEM_SHINY_STONE   == ITEM_FIRE_STONE + 7, RogueStoneOrderShiny);
STATIC_ASSERT(ITEM_DUSK_STONE    == ITEM_FIRE_STONE + 8, RogueStoneOrderDusk);
STATIC_ASSERT(ITEM_DAWN_STONE    == ITEM_FIRE_STONE + 9, RogueStoneOrderDawn);

// What grows on the themes that have soil. Same banded shape as the other two,
// and named by ITEM rather than by berry id so it reads like them and so
// verify_loot_table.py can check the names against constants/items.h;
// ItemIdToBerryType converts at planting time.
//
// Quantity is not read here - a tree hands over DUNGEON_BERRY_YIELD of whatever
// it grew, which is the point of the feature - so it stays 1 to keep the shared
// struct honest rather than pretending to mean something.
// WHAT A RUN CAN CRAFT WITH. Every one of these is a vanilla item whose only
// vanilla purpose is to be SOLD, and a run has no shop that buys -- so before
// crafting they would have been the worst thing a dowsing machine could find.
// That is exactly what makes them the right materials: they cost the loot
// ladder nothing, because they were never on it.
//
// DEPTH IS GATED BY THE INGREDIENT, NOT BY A FLAG. A recipe needing a Star
// Piece cannot be made before floor 55 because nothing drops one before then,
// and that needs no unlockFlag, no id from the unused pool, and nothing to
// keep in sync. The bands below ARE the recipe tree's progression.
//
// The four shards are deliberately the commonest thing here and never retire.
// They are what turns the eight evolution stones the Game Corner sells for
// 1,000 coins into something a run can also earn by digging -- the same gap
// sBuriedStones only half closes by burying two of the ten.
static const struct RogueLootEntry sLootMaterials[] =
{
    // NO SHARDS, NO HEART SCALE, NO STAR PIECE. Those are the mining
    // minigame's, and the first draft of this table buried them as well --
    // two sources for one thing, and the worse of the two, since a dowsing
    // hit is a press of A and mining is a decision with a stress meter
    // running. What is left here is the half mining does NOT give.

    // Water is the base of every healing recipe and has to be findable from
    // floor 1, in twos, or the berry half of the tree cannot start.
    { ITEM_FRESH_WATER,    20,   0, 255, 2 },

    // The early filler. Both retire: a Tiny Mushroom recipe is an answer to
    // floor 10 and an insult by floor 80.
    { ITEM_TINY_MUSHROOM,  14,   0,  50, 2 },
    { ITEM_PEARL,          12,   0,  60, 1 },
    { ITEM_STARDUST,       12,   0,  70, 1 },

    // The mid and late materials, and the reason the top of the recipe tree is
    // unreachable early.
    { ITEM_BIG_MUSHROOM,   10,  35, 255, 1 },
    { ITEM_BIG_PEARL,      10,  45, 255, 1 },
};

static const struct RogueLootEntry sLootBerries[] =
{
    // In-battle healing and status cover, which is what a berry is for in a
    // run. Oran retires the way Potions do; the rest are useful throughout.
    { ITEM_ORAN_BERRY,     20,   0,  40, 1 },
    { ITEM_PECHA_BERRY,    10,   0,  50, 1 },
    { ITEM_CHERI_BERRY,    10,   0,  50, 1 },
    { ITEM_CHESTO_BERRY,   10,   0,  50, 1 },
    { ITEM_LEPPA_BERRY,    12,  10, 255, 1 },
    { ITEM_SITRUS_BERRY,   20,  25, 255, 1 },
    { ITEM_LUM_BERRY,      16,  30, 255, 1 },

    // The pinch berries, which are the ones worth holding on a deep floor.
    { ITEM_SALAC_BERRY,     8,  60, 255, 1 },
    { ITEM_LIECHI_BERRY,    8,  65, 255, 1 },
    { ITEM_PETAYA_BERRY,    8,  65, 255, 1 },
};

// Weighted pick from a banded table. Shared by both loot tables, because they
// are the same shape and a second copy of this is a second place for the
// running-total arithmetic to be wrong.
static void RollFromTable(const struct RogueLootEntry *table, u32 count,
                          u16 floor, u16 *item, u8 *quantity)
{
    u32 total = 0;
    u32 roll;
    u32 i;

    for (i = 0; i < count; i++)
    {
        if (floor >= table[i].minFloor && floor < table[i].maxFloor)
            total += table[i].weight;
    }

    // Cannot happen with either table as written, and verify_loot_table.py
    // fails if an edit ever makes it possible - but a gap should hand over
    // something rather than divide by zero.
    if (total == 0)
    {
        *item = ITEM_POTION;
        *quantity = 1;
        return;
    }

    roll = DungeonRandom() % total;

    for (i = 0; i < count; i++)
    {
        if (floor < table[i].minFloor || floor >= table[i].maxFloor)
            continue;
        if (roll < table[i].weight)
        {
            *item = table[i].item;
            *quantity = table[i].quantity;
            return;
        }
        roll -= table[i].weight;
    }

    *item = ITEM_POTION;
    *quantity = 1;
}

// Buries hidden items through the floor's rooms.
//
// Runs after PlaceItems so it can avoid a tile that already has an item ball on
// it - not because the two would collide, they are different systems entirely,
// but because a hidden item under a ball is one the player can never be told
// about: the ball takes the interaction.
static void PlaceHiddenItems(u16 floor)
{
    u32 heldCount = DUNGEON_HIDDEN_MIN + floor / DUNGEON_HIDDEN_FLOORS_PER_EXTRA;
    u32 count = heldCount + DUNGEON_MATERIALS_PER_FLOOR;
    const struct RogueDungeonTheme *theme = ThemeForFloor(floor);
    u32 i, j;
    bool32 buryStone;
    u16 stone;

    sHiddenCount = 0;

    // Both draws happen before the early returns and before the loop, so they
    // are two fixed steps of the floor's stream rather than steps that depend
    // on how many placements collided. The floor test comes second in the
    // condition for the same reason: && would short-circuit past the draw on
    // floors 1-3 and shift every later roll on those floors.
    buryStone = ((DungeonRandom() % DUNGEON_STONE_ODDS) == 0)
             && (floor >= DUNGEON_STONE_FIRST_FLOOR);
    stone = sBuriedStones[DungeonRandom() % ARRAY_COUNT(sBuriedStones)];

    if (sRoomCount == 0)
        return;

    // NOTHING BURIED WHERE IT CANNOT BE FOUND. item_use.c refuses to start the
    // dowsing machine while surfing or underwater, so on the ocean and the
    // seafloor a buried item would be invisible in the strict sense - reachable
    // only by pressing A on the right tile with no way to know it was there.
    //
    // These two tests are what "surfing or diving" means in theme terms: the
    // ocean is the only theme that walks at water elevation, and underwater is
    // the only one with a map of its own.
    if (theme->elevationFloor == DUNGEON_ELEVATION_WATER
     || theme->mapId == MAP_ROGUE_DUNGEON_UNDERWATER)
        return;

    if (count > DUNGEON_MAX_HIDDEN)
        count = DUNGEON_MAX_HIDDEN;

    for (i = 0; i < count; i++)
    {
        u16 item;
        u8 quantity;
        u8 x = 0, y = 0;
        u32 room = DungeonRandom() % sRoomCount;

        x = sRooms[room].x + (DungeonRandom() % sRooms[room].w);
        y = sRooms[room].y + (DungeonRandom() % sRooms[room].h);

        // The exit, an item ball, a trainer or another buried item - anything
        // that owns the interaction on that tile, or that the player cannot
        // stand on to search it.
        if (x == sStairsX && y == sStairsY)
            continue;

        for (j = 0; j < sItemCount; j++)
        {
            if (sItemX[j] == x && sItemY[j] == y)
                break;
        }
        if (j != sItemCount)
            continue;

        for (j = 0; j < sTrainerCount; j++)
        {
            if (sTrainerX[j] == x && sTrainerY[j] == y)
                break;
        }
        if (j != sTrainerCount)
            continue;

        for (j = 0; j < sBerryCount; j++)
        {
            if (sBerryX[j] == x && sBerryY[j] == y)
                break;
        }
        if (j != sBerryCount)
            continue;

        // THE ROCKS WERE MISSING FROM THIS LIST, and they are placed before this
        // runs, so a buried item could land under one - unreachable, and the
        // Dowsing Machine pointing at it forever. The same bug the comment at the
        // head of this function describes for balls and trees, one class short.
        for (j = 0; j < sRockCount; j++)
        {
            if (sRockX[j] == x && sRockY[j] == y)
                break;
        }
        if (j != sRockCount)
            continue;

        if (sEventCount && sEventX == x && sEventY == y)
            continue;

        for (j = 0; j < sHiddenCount; j++)
        {
            if (sHiddenItems[j].x == x && sHiddenItems[j].y == y)
                break;
        }
        if (j != sHiddenCount)
            continue;

        // sHiddenCount rather than i, so a floor whose first few candidate
        // positions collided still buries the stone rather than losing it.
        if (buryStone && sHiddenCount == 0)
        {
            item = stone;
            quantity = 1;
        }
        else if (sHiddenCount >= heldCount)
        {
            // The tail of the floor's buried items is materials. Keyed on the
            // INDEX rather than on a fresh roll, so this adds no draw to the
            // floor's stream and cannot shift any placement that follows --
            // the property the two draws above are arranged to preserve.
            RollFromTable(sLootMaterials, ARRAY_COUNT(sLootMaterials), floor, &item, &quantity);
        }
        else
        {
            RollFromTable(sLootHeld, ARRAY_COUNT(sLootHeld), floor, &item, &quantity);
        }

        sHiddenItems[sHiddenCount].x = x;
        sHiddenItems[sHiddenCount].y = y;
        // ELEVATION_TRANSITION, not the theme's floor elevation.
        // GetBackgroundEventAtPosition matches a bg event when its elevation
        // equals the PLAYER'S or is ELEVATION_TRANSITION, so transition matches
        // whatever the player is standing at and cannot be wrong. A per-theme
        // elevation hard-coded here is the recurring bug in this project, and
        // it already bit the trainers once on the ocean.
        sHiddenItems[sHiddenCount].elevation = ELEVATION_TRANSITION;
        sHiddenItems[sHiddenCount].kind = BG_EVENT_HIDDEN_ITEM;
        sHiddenItems[sHiddenCount].bgUnion.hiddenItem.item = item;
        sHiddenItems[sHiddenCount].bgUnion.hiddenItem.quantity = quantity;
        // Interacted with rather than stepped on. Emerald's step-on path for
        // buried items is not wired up - underfoot is read in exactly one place
        // and only to REJECT - so a TRUE here would bury the item forever.
        sHiddenItems[sHiddenCount].bgUnion.hiddenItem.underfoot = FALSE;
        sHiddenItems[sHiddenCount].bgUnion.hiddenItem.hiddenItemId =
            DUNGEON_HIDDEN_FIRST_ID + sHiddenCount;
        sHiddenCount++;
    }
}

// Chooses where berry trees stand and what grows on them. Positions and berries
// only - nothing is planted here, because this runs on every load of a floor
// and planting writes the save block.
static void PlaceBerryTrees(u16 floor)
{
    const struct RogueDungeonTheme *theme = ThemeForFloor(floor);
    // Depth-scaled, the same shape PlaceItems and PlaceTrainers use. This loop
    // ran to DUNGEON_MAX_BERRIES flat, which made a constant named MAX into the
    // count and stood four trees on floor one.
    u32 count = DUNGEON_BERRY_MIN + floor / DUNGEON_BERRY_FLOORS_PER_EXTRA;
    u32 i, j;

    sBerryCount = 0;

    if (sRoomCount == 0 || !theme->berries)
        return;
    if (count > DUNGEON_MAX_BERRIES)
        count = DUNGEON_MAX_BERRIES;

    for (i = 0; i < count; i++)
    {
        u8 quantity;
        u32 room = PickRoomLeastUsedFrom(0);
        u8 x = sRooms[room].x + (DungeonRandom() % sRooms[room].w);
        u8 y = sRooms[room].y + (DungeonRandom() % sRooms[room].h);

        // A berry tree is solid, so the same rule the item balls have: never on
        // the exit, or the floor cannot be left.
        if (x == sStairsX && y == sStairsY)
            continue;

        for (j = 0; j < sItemCount; j++)
        {
            if (sItemX[j] == x && sItemY[j] == y)
                break;
        }
        if (j != sItemCount)
            continue;

        for (j = 0; j < sTrainerCount; j++)
        {
            if (sTrainerX[j] == x && sTrainerY[j] == y)
                break;
        }
        if (j != sTrainerCount)
            continue;

        for (j = 0; j < sBerryCount; j++)
        {
            if (sBerryX[j] == x && sBerryY[j] == y)
                break;
        }
        if (j != sBerryCount)
            continue;

        NoteRoomUsed(room);
        sBerryX[sBerryCount] = x;
        sBerryY[sBerryCount] = y;
        RollFromTable(sLootBerries, ARRAY_COUNT(sLootBerries), floor,
                      &sBerryItems[sBerryCount], &quantity);
        sBerryCount++;
    }
}

// Mining rocks. Solid objects like berry trees, so the same placement rules:
// never on the stairs, never on top of anything already placed.
//
// Every theme gets them, unlike berries. A rock is rubble rather than soil,
// so there is no theme this reads wrong on -- and the ocean and the seafloor
// are exactly the two themes that bury NOTHING, because dowsing cannot be
// started while surfing or diving. Without rocks those two would have no
// material source at all.
// The floor events, banded by depth. See struct RogueFloorEvent.
//
// Every one of these is a QUESTION with a cost on both branches, which is the
// whole reason the table exists - a floor already hands out items and fights, and
// what it had none of was a decision. An event that is simply a free reward would
// be an item ball with more text.
//
// All three need only small ARITHMETIC specials - a fraction of the player's
// money, a loot roll, a coin flip - and no new UI, no new item ids and no new
// engine paths, which is what decided this first slate. Events that hand over or
// fight a floor-appropriate POKEMON are the obvious next ones and are a step up
// in cost: givemon and setwildbattle both take their level as a byte LITERAL, so
// a scaling one needs the party or the battle built in C, and the give path also
// needs the full-party case handled the way Text_AceNoRoom does it.
// NOT sDungeonEvents - that name is already taken, by the struct MapEvents copy
// the hidden items repoint gMapHeader.events at.
static const struct RogueFloorEvent sFloorEvents[] =
{
    // A free full heal, and it never retires because the thing it competes with
    // is walking on with a hurt party - which is a real choice at every depth
    // once there is a reason not to burn the bag on it.
    { OBJ_EVENT_GFX_OLD_WOMAN,   RogueDungeonFloor_EventScript_EventSpring,
      0,  DUNGEON_TOTAL_FLOORS },

    // Double or nothing on a third of the player's money. Live from the start,
    // and only meaningful at all because money is now wiped with the run - see
    // the note in RogueDungeon_ResetRun.
    { OBJ_EVENT_GFX_GENTLEMAN,   RogueDungeonFloor_EventScript_EventGambler,
      0,  DUNGEON_TOTAL_FLOORS, DUNGEON_EVENT_WEIGHT_COMMON },

    // Sells one item sight unseen. Arrives at floor 21 rather than floor 1
    // because before the first rest stop the player has no money to gamble with
    // and the offer would read as broken rather than as a bad deal.
    { OBJ_EVENT_GFX_MART_EMPLOYEE, RogueDungeonFloor_EventScript_EventPedlar,
      20, DUNGEON_TOTAL_FLOORS },

    // A Pokemon for a Pokemon, five levels up, species unseen. Self-balancing -
    // see DUNGEON_TRADE_LEVEL_BONUS - and refused outright on a party of one,
    // because trading the last Pokemon away is a whiteout with extra steps.
    { OBJ_EVENT_GFX_HIKER,       RogueDungeonFloor_EventScript_EventTrader,
      0,  DUNGEON_TOTAL_FLOORS, DUNGEON_EVENT_WEIGHT_COMMON },

    // An egg. Free, and the price is the party slot - which this build already
    // treats as a real wager, since the boss ace offer is refused on a full
    // party and the archivist is the only way to make room.
    { OBJ_EVENT_GFX_WOMAN_2,     RogueDungeonFloor_EventScript_EventEgg,
      0,  DUNGEON_TOTAL_FLOORS, DUNGEON_EVENT_WEIGHT_COMMON },

    // An injured Pokemon, wearing its OWN overworld sprite - the reason the gfx
    // sentinel exists. OW_POKEMON_OBJECT_EVENTS is already TRUE and the 1.12 MB
    // of per-species overworld sprites is already in the build, reachable by
    // almost nothing; this is the second thing to use it after the follower.
    { DUNGEON_EVENT_GFX_ROLLED,  RogueDungeonFloor_EventScript_EventInjured,
      0,  DUNGEON_TOTAL_FLOORS },

    // ---- the second slate ----
    //
    // The three fields after the floor band are weight, theme mask and prop, all
    // default-safe: a row that omits them is a normal-weight, any-theme, propless
    // event, which is exactly what the six rows above still are.

    // The scout, and THE RUN'S FIRST SHINY. Arrives at floor 11 rather than 1
    // because a shiny met before the player owns a spare Poke Ball is only a
    // memory of the one that got away.
    { OBJ_EVENT_GFX_HIKER,       RogueDungeonFloor_EventScript_EventScout,
      10, DUNGEON_TOTAL_FLOORS },

    // The memorial shrine, and the grave she tends.
    //
    // NOT THEME-RESTRICTED, and the reason is worth writing down because it was
    // got wrong once. The grave was first taken to be tileset art, which would
    // have meant restricting her to themes carrying it - but no theme carries a
    // grave, and props are OBJECT EVENTS, which have nothing to do with
    // tilesets. tools/rogue/make_grave_sprite.py lifts the Mt Pyre gravestone
    // out of its metatile into a sprite, and a sprite works in every theme. The
    // restriction bought nothing and was removed.
    // COMMON, and not only for pacing: this is the run's ONLY source of charm
    // cleansing, so how often she appears is how long an affliction can stick.
    { OBJ_EVENT_GFX_OLD_WOMAN,   RogueDungeonFloor_EventScript_EventShrine,
      0,  DUNGEON_TOTAL_FLOORS, DUNGEON_EVENT_WEIGHT_COMMON,
      DUNGEON_EVENT_ANY_THEME, OBJ_EVENT_GFX_ROGUE_GRAVE },

    // The herbalist, beside the berry tree she is stripping.
    { OBJ_EVENT_GFX_WOMAN_3,     RogueDungeonFloor_EventScript_EventHerbalist,
      0,  DUNGEON_TOTAL_FLOORS, DUNGEON_EVENT_WEIGHT_COMMON,
      DUNGEON_EVENT_ANY_THEME, OBJ_EVENT_GFX_BERRY_TREE },

    // The fossil dig, beside the rock it is set into.
    { OBJ_EVENT_GFX_HIKER,       RogueDungeonFloor_EventScript_EventFossil,
      10, DUNGEON_TOTAL_FLOORS, DUNGEON_EVENT_WEIGHT_RARE,
      DUNGEON_EVENT_ANY_THEME, OBJ_EVENT_GFX_BREAKABLE_ROCK },

    // The shady move tutor. THE ONE EVENT WEIGHTED ABOVE DEFAULT, and it arrives
    // at floor 11 rather than 21.
    //
    // An illegal move is the cheapest thing in the whole table that makes one run
    // feel unlike the last, so it wants to be a thing runs are BUILT around
    // rather than a thing they occasionally meet. It was first banded with the
    // pedlar on the argument that there is no team worth reshaping before the
    // first rest stop - but that is backwards: the earlier a Pokemon gets a move
    // it could never learn, the longer the run is shaped by it, and a team that
    // is still half-formed is exactly the one a forbidden move transforms.
    //
    // The band change alone would have made it RARER in absolute terms, not
    // commoner - it now competes on floors 11-20 as well, but against a table
    // that is already crowded there. The weight is what actually moves it.
    { OBJ_EVENT_GFX_MAN_4,       RogueDungeonFloor_EventScript_EventTutor,
      10, DUNGEON_TOTAL_FLOORS, DUNGEON_EVENT_WEIGHT_COMMON },

    // The volatile evolution crystal. ULTRA RARE - a tenth the weight of
    // everything else, so a run may well never see one. A palette variant of the
    // breakable rock, which is what made "a giant crackling geode" cost no art.
    { OBJ_EVENT_GFX_ROGUE_CRYSTAL, RogueDungeonFloor_EventScript_EventCrystal,
      0,  DUNGEON_TOTAL_FLOORS, DUNGEON_EVENT_WEIGHT_RARE },

    // The Ditto item ball. Wears the item ball sprite because the whole trap is
    // that it cannot be told apart from the floor's real ones.
    { OBJ_EVENT_GFX_ITEM_BALL,   RogueDungeonFloor_EventScript_EventDittoBall,
      10, DUNGEON_TOTAL_FLOORS },

    // The enraged totem. Wears the rolled species' own sprite like the injured
    // Pokemon - the second use of DUNGEON_EVENT_GFX_ROLLED, and the thing that
    // made the sentinel worth having.
    { DUNGEON_EVENT_GFX_ROLLED,  RogueDungeonFloor_EventScript_EventTotem,
      20, DUNGEON_TOTAL_FLOORS },

    // The orb at the summit. NO PROP AND NO NPC - the orb IS the object, wearing
    // OBJ_EVENT_GFX_METEORITE, which is a sphere sitting on the ground and was
    // already compiled in with the FRLG overworld sprites while nothing selected
    // it. An attendant standing next to it would make it a shop.
    //
    // From floor 1, because it is a decision rather than a reward and the first
    // dungeon is exactly where a player has the least to lose by taking it.
    { OBJ_EVENT_GFX_METEORITE,   RogueDungeonFloor_EventScript_EventOrb,
      0,  DUNGEON_TOTAL_FLOORS },

    // The Shuppet that eats curses. IT WANDERS - the only event that does, and
    // the reason the movementType field exists. A grief-eater that stands
    // politely facing you is furniture; one that drifts around its own patch is
    // waiting for something. Range 1, for the off-screen respawn reason in the
    // struct note.
    //
    // THE FIRST EVENT THAT TAKES A CHARM AWAY. Everything else in this table
    // only ever adds one, and RogueCharm_ScriptCleanseOne has been sitting
    // unused since the shrine was built.
    { OBJ_EVENT_MON + SPECIES_SHUPPET, RogueDungeonFloor_EventScript_EventShuppet,
      0,  DUNGEON_TOTAL_FLOORS, DUNGEON_EVENT_WEIGHT_DEFAULT,
      DUNGEON_EVENT_ANY_THEME, DUNGEON_EVENT_NO_PROP,
      MOVEMENT_TYPE_WANDER_AROUND },

    // The moonlit Clefairy, and the stone they dance around. The prop is the
    // METEORITE, which is what a Moon Stone is - Mt Moon's fell out of the sky,
    // and the sprite is a rock on the ground either way.
    //
    // It answers a problem the project already wrote down: DUNGEON_STONE_ODDS
    // exists because Clefairy and Pikachu evolve by stone and by nothing else,
    // and a Clefairy run that never digs one up is stuck at a stage-1 statline
    // for 115 floors. This makes a stone something you can be OFFERED rather
    // than only something you can be lucky about.
    { OBJ_EVENT_MON + SPECIES_CLEFAIRY, RogueDungeonFloor_EventScript_EventClefairy,
      0,  DUNGEON_TOTAL_FLOORS, DUNGEON_EVENT_WEIGHT_DEFAULT,
      DUNGEON_EVENT_ANY_THEME, OBJ_EVENT_GFX_METEORITE },

    // The ability transposer. From floor 11 rather than 1: a hidden ability is a
    // build decision, and on floor 1 the player has had two Pokemon for ten
    // minutes and no idea which of them the run is going to be about.
    { OBJ_EVENT_GFX_SCIENTIST_1, RogueDungeonFloor_EventScript_EventTransposer,
      10, DUNGEON_TOTAL_FLOORS },

    // The Pokerus injector. A TERMINAL, NOT A SECOND SCIENTIST - the transposer
    // above already is one, and two events wearing the same sprite read as the
    // same event until the player has talked to both. The PC is lifted out of
    // the Pokemon Center by tools/rogue/make_pc_sprite.py.
    //
    // Also floor 11: doubled EV gain is worth most when there is a run left to
    // earn EVs in, and 1 HP is a death sentence on floor 1 with two starters.
    { OBJ_EVENT_GFX_ROGUE_PC, RogueDungeonFloor_EventScript_EventPokerus,
      10, DUNGEON_TOTAL_FLOORS },

    // The nest. THREE WILD BATTLES BACK TO BACK with no heal between, which is
    // the only thing in the pool that asks about the whole party rather than the
    // lead - everything else is answered by one strong Pokemon.
    //
    // GFX_ROLLED, THE SAME SENTINEL THE INJURED POKEMON USES, and that shared
    // sprite is the point rather than a saving. A lone Pokemon standing on a
    // floor is now genuinely ambiguous: it might be hurt and want to join, or it
    // might be the visible corner of a nest. The player cannot tell until they
    // talk to it, and both events already existed - the ambiguity costs nothing
    // and did not have to be built.
    { DUNGEON_EVENT_GFX_ROLLED, RogueDungeonFloor_EventScript_EventNest,
      10, DUNGEON_TOTAL_FLOORS },
};

// THE ROLL AND THE DEVOLVE HAPPEN AT DIFFERENT TIMES, and they have to.
//
// The species is drawn from the seeded stream, so it can only be drawn at prepare
// time - DungeonRandom is not live once the player is standing on the floor. But
// DevolveForLevel needs a LEVEL, and the trader's level is the traded Pokemon's
// plus five, which nothing knows until the player picks one.
//
// So sEventSpecies holds the RAW roll, and each event devolves it when it can:
// the trader at interaction time against the level it just learned, the injured
// Pokemon at prepare time against the floor's level - because that one WEARS the
// species as its overworld sprite, and a Dragonite sprite that hands over a
// Dratini is the bug this split exists to avoid.

// THE FLOOR'S SPECIES, DEVOLVED FOR WHATEVER LEVEL IS ABOUT TO USE IT.
//
// EVERY event that fights, gifts or names the rolled species goes through this,
// and reading sEventSpecies raw for any of those is a bug. It shipped as one: the
// devolve at prepare time is gated on gfxId == DUNGEON_EVENT_GFX_ROLLED, which is
// a COSMETIC condition - does the NPC wear the species as its sprite - being used
// to answer a BALANCE question. The scout wears a hiker, so its shiny was never
// devolved and a floor 11 encounter could be a fully evolved Aggron. Not a hard
// fight; a run ending.
//
// Idempotent, so events whose sprite already forced a devolve at prepare time can
// call it again for free - DevolveForLevel only ever walks downward.
//
// The TRADER is the one caller that must not go through a floor level: its level
// is the traded Pokemon's plus five, which nothing knows until the player picks
// one, and that is the whole reason sEventSpecies holds the raw roll.
static u16 EventSpeciesForLevel(u8 level)
{
    return RogueDungeon_DevolveForLevel(sEventSpecies, level);
}

// Rolls the floor's event, and places it the way everything else on a floor is
// placed - pick a room, pick a point, refuse anything already standing there.
//
// LAST IN THE PREPARE SEQUENCE, and that is not a preference. Every placer draws
// from the same seeded stream, so inserting a draw anywhere but the end shifts
// the position of every object placed after it - identical layout, everything
// moved, and nothing checks for that. See the note on the rotation draw in
// BuildWildEncounterTable.
// A weight of zero is the DEFAULT, not "never". See the note in
// constants/rogue_dungeon.h - a row that omits the field has to behave normally.
static u32 EventWeight(const struct RogueFloorEvent *event)
{
    return event->weight != 0 ? event->weight : DUNGEON_EVENT_WEIGHT_DEFAULT;
}

// Is this tile free of everything already placed on the floor?
static bool32 EventTileFree(u32 x, u32 y)
{
    u32 j;

    if (x == sStairsX && y == sStairsY)
        return FALSE;
    if (x == sSpawnX && y == sSpawnY)
        return FALSE;

    for (j = 0; j < sTrainerCount; j++)
        if (sTrainerX[j] == x && sTrainerY[j] == y)
            return FALSE;
    for (j = 0; j < sItemCount; j++)
        if (sItemX[j] == x && sItemY[j] == y)
            return FALSE;
    for (j = 0; j < sBerryCount; j++)
        if (sBerryX[j] == x && sBerryY[j] == y)
            return FALSE;
    for (j = 0; j < sRockCount; j++)
        if (sRockX[j] == x && sRockY[j] == y)
            return FALSE;

    return TRUE;
}

static void PlaceEvents(u16 floor)
{
    u32 i, j, x, y, room, choices = 0;
    u8 eligible[ARRAY_COUNT(sFloorEvents)];

    sEventCount = 0;

    if (sRoomCount == 0)
        return;

    // The odds roll happens FIRST and unconditionally, so it costs the same
    // number of draws whether or not a floor gets an event. A roll skipped on
    // the cheap path would desynchronise nothing today - this is the last placer
    // - but it would the moment anything is added after it.
    if (DungeonRandom() % 100 >= DUNGEON_EVENT_PERCENT)
        return;

    // An arena is the boss and the player facing off across an empty room. There
    // is no room in that for a merchant.
    if (IsDungeonBossFloor(floor) || IsMiniBossFloor(floor))
        return;

    {
        u32 themeBit = 1 << (ThemeForFloor(floor) - sDungeonThemes);
        u32 total = 0, roll;

        for (i = 0; i < ARRAY_COUNT(sFloorEvents); i++)
        {
            const struct RogueFloorEvent *e = &sFloorEvents[i];

            if (floor < e->minFloor || floor > e->maxFloor)
                continue;
            // A mask of zero is "any theme", so an unrestricted row passes here
            // without naming every theme it is allowed in.
            if (e->themeMask != DUNGEON_EVENT_ANY_THEME && !(e->themeMask & themeBit))
                continue;

            eligible[choices++] = i;
            total += EventWeight(e);
        }

        if (choices == 0)
            return;

        // ONE DRAW, WHATEVER THE WEIGHTS ARE. Every placer shares the seeded
        // stream, so the number of times this function calls DungeonRandom must
        // not depend on the table - a weighted pick that rejected and re-rolled
        // would move every object placed after it whenever the table changed.
        roll = DungeonRandom() % total;
        for (i = 0; i < choices; i++)
        {
            u32 w = EventWeight(&sFloorEvents[eligible[i]]);

            if (roll < w)
                break;
            roll -= w;
        }

        // Cannot happen while total is the sum of the same weights, but an index
        // one past the end would read a script pointer out of a neighbouring
        // table, and that is not a failure worth being clever about.
        if (i >= choices)
            i = choices - 1;

        sEventIndex = eligible[i];
    }

    // Rolled for EVERY event, not only the ones that read them, so the number of
    // draws taken here does not depend on which event came up. PlaceHiddenItems
    // runs after this and shares the stream.
    sEventSpecies = sSafariLandSpecies[DungeonRandom()
                                      % ARRAY_COUNT(sSafariLandSpecies)];
    sEventAmbush = (DungeonRandom() % DUNGEON_INJURED_AMBUSH_ODDS) == 0;

    // See the roll/devolve note under sFloorEvents: the event that WEARS the
    // species has to hold the final one, and the floor's level is known here.
    if (sFloorEvents[sEventIndex].gfxId == DUNGEON_EVENT_GFX_ROLLED)
        sEventSpecies = RogueDungeon_DevolveForLevel(sEventSpecies,
                                                     FloorTargetLevel(floor));

    room = PickRoomLeastUsedFrom(0);
    x = sRooms[room].x + (DungeonRandom() % sRooms[room].w);
    y = sRooms[room].y + (DungeonRandom() % sRooms[room].h);

    // One attempt, not a retry loop. A floor that happens to roll an occupied
    // tile simply has no event, which is indistinguishable from the three floors
    // in four that rolled none - and a retry loop would draw a variable number of
    // times from the shared stream, which is the one thing a placer must not do.
    if (!EventTileFree(x, y))
        return;

    NoteRoomUsed(room);
    sEventX = x;
    sEventY = y;
    sEventCount = 1;

    // The prop, if this event wants one. PLACED DETERMINISTICALLY, one tile east
    // and falling back to one tile west - NOT with a draw of its own. Every
    // placer shares the seeded stream, so a prop that rolled its own position
    // would move every object placed after it on floors that happened to roll an
    // event with a prop, and nothing checks for that.
    //
    // A prop that will not fit is simply absent. The event still works; the
    // hiker is just standing next to nothing, which reads as scenery rather than
    // as a bug.
    if (sFloorEvents[sEventIndex].propGfxId != DUNGEON_EVENT_NO_PROP)
    {
        u32 room = 0;

        // Kept inside the room the event landed in, so a prop cannot end up
        // embedded in a wall or out in a corridor on its own.
        for (j = 0; j < sRoomCount; j++)
        {
            if (x >= sRooms[j].x && x < sRooms[j].x + sRooms[j].w
                && y >= sRooms[j].y && y < sRooms[j].y + sRooms[j].h)
            {
                room = j;
                break;
            }
        }

        if (x + 1 < sRooms[room].x + sRooms[room].w && EventTileFree(x + 1, y))
        {
            sEventPropX = x + 1;
            sEventPropY = y;
            sEventCount = 2;
        }
        else if (x > sRooms[room].x && EventTileFree(x - 1, y))
        {
            sEventPropX = x - 1;
            sEventPropY = y;
            sEventCount = 2;
        }
    }
}

// ---------------------------------------------------------------- event specials
//
// The four below are the arithmetic the scripts cannot do. Each pair is
// deliberately split into a BUFFER call that only reads and describes, and a
// RESOLVE/BUY call that commits - so the player is shown the real number before
// the yes/no and the commit cannot disagree with what was offered. The figure is
// carried between them in a static rather than recomputed, which is the whole
// point: recomputing after a yes would let the offer and the charge differ.
//
// They use the GLOBAL Random(), not DungeonRandom(). That stream belongs to floor
// generation and is not live once the player is standing on the floor - the same
// rule the water re-deal follows.

// A third, so the wager scales with the run rather than being unrefusable early
// and pocket change late. Floored to a round hundred so the message reads like a
// price and not like a calculation.
#define DUNGEON_EVENT_STAKE_DIVISOR 3
#define DUNGEON_EVENT_STAKE_MIN     100

EWRAM_DATA static u32 sEventStake = 0;
EWRAM_DATA static u16 sEventWareItem = ITEM_NONE;
EWRAM_DATA static u32 sEventWarePrice = 0;

// Result is the stake, or 0 when the player cannot cover the minimum.
void RogueDungeon_BufferEventStake(void)
{
    u32 money = GetMoney(&gSaveBlock1Ptr->money);

    sEventStake = (money / DUNGEON_EVENT_STAKE_DIVISOR / 100) * 100;

    if (sEventStake < DUNGEON_EVENT_STAKE_MIN)
    {
        sEventStake = 0;
        gSpecialVar_Result = 0;
        return;
    }

    ConvertIntToDecimalStringN(gStringVar1, sEventStake,
                               STR_CONV_MODE_LEFT_ALIGN, MAX_MONEY_DIGITS);
    gSpecialVar_Result = 1;
}

// Result is 1 on a win, 0 on a loss. Buffers the amount either way, because a
// wager the player is not told the outcome of in figures is just a message.
void RogueDungeon_ResolveEventStake(void)
{
    bool32 won = (Random() & 1);

    // Re-checked rather than assumed. Nothing between the offer and here can
    // spend money today, but a stake larger than the balance would silently
    // clamp inside RemoveMoney and the player would be told they lost more than
    // they had.
    if (!IsEnoughMoney(&gSaveBlock1Ptr->money, sEventStake))
    {
        gSpecialVar_Result = 0;
        return;
    }

    if (won)
        AddMoney(&gSaveBlock1Ptr->money, sEventStake);
    else
        RemoveMoney(&gSaveBlock1Ptr->money, sEventStake);

    ConvertIntToDecimalStringN(gStringVar1, sEventStake,
                               STR_CONV_MODE_LEFT_ALIGN, MAX_MONEY_DIGITS);
    gSpecialVar_Result = won ? 1 : 0;
}

// What the pedlar is selling, and for how much. Result is 0 when the player
// cannot afford it - the price is still buffered so the refusal can name it.
void RogueDungeon_BufferEventWares(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    u8 quantity;   // RollFromTable's out-param is a u8; a u16 here is a type error

    // The floor's own consumable table, so the wares are always depth
    // appropriate and this holds no second copy of the loot ladder. Random()
    // rather than the seeded stream: what is on the blanket is not part of the
    // floor's layout and re-rolling it on a reload is harmless.
    RollFromTable(sLootConsumables, ARRAY_COUNT(sLootConsumables), floor,
                  &sEventWareItem, &quantity);

    // Above what the item is worth, because the thing being sold is the gamble.
    // A pedlar who was reliably good value would be the mart with extra steps.
    sEventWarePrice = ((u32)FloorTargetLevel(floor)) * 60;

    ConvertIntToDecimalStringN(gStringVar1, sEventWarePrice,
                               STR_CONV_MODE_LEFT_ALIGN, MAX_MONEY_DIGITS);

    gSpecialVar_Result = IsEnoughMoney(&gSaveBlock1Ptr->money, sEventWarePrice);
}

// Result is 0 if the bag had no room, in which case NOTHING is charged. Ordered
// that way on purpose: AddBagItem first, and only take the money once the item
// is definitely in.
void RogueDungeon_BuyEventWares(void)
{
    if (sEventWareItem == ITEM_NONE
        || !IsEnoughMoney(&gSaveBlock1Ptr->money, sEventWarePrice))
    {
        gSpecialVar_Result = 0;
        return;
    }

    if (!AddBagItem(sEventWareItem, 1))
    {
        gSpecialVar_Result = 0;
        return;
    }

    RemoveMoney(&gSaveBlock1Ptr->money, sEventWarePrice);
    CopyItemName(sEventWareItem, gStringVar2);
    gSpecialVar_Result = 1;
}

// Adds a Pokemon to the first free party slot. Result is 0 if there was none.
//
// CreateRandomMonWithIVs is the idiom this file already uses for the boss ace,
// and MAX_PER_STAT_IVS with it: a gift the player fought for should not arrive
// with worse stats than something they caught.
static bool32 GiveEventMon(u16 species, u8 level)
{
    u32 slot = CalculatePlayerPartyCount();

    if (species == SPECIES_NONE || slot >= PARTY_SIZE)
        return FALSE;

    CreateRandomMonWithIVs(&gParties[B_TRAINER_PLAYER][slot], species, level,
                           MAX_PER_STAT_IVS);
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][slot]);
    CalculatePlayerPartyCount();
    return TRUE;
}

// The trader. Result 0 refuses the whole event, because a party of one has
// nothing it can afford to give - trading the last Pokemon away is a whiteout
// with extra steps, and the engine would be left with an empty party mid-floor.
void RogueDungeon_EventTraderCanTrade(void)
{
    gSpecialVar_Result = (CalculatePlayerPartyCount() > 1);
}

// Describes the deal for the yes/no, having been handed a party slot in
// VAR_0x8004 by ChoosePartyMon. Buffers what is being given up and at what level
// the replacement arrives - NOT what it is, which is the entire point.
//
// The level is the traded Pokemon's plus five, which is the whole balance of this
// event. See DUNGEON_TRADE_LEVEL_BONUS.
void RogueDungeon_EventTraderOffer(void)
{
    u32 slot = gSpecialVar_0x8004;
    struct Pokemon *mon;
    u32 level;

    if (slot >= CalculatePlayerPartyCount())
    {
        gSpecialVar_Result = 0;
        return;
    }

    mon = &gParties[B_TRAINER_PLAYER][slot];
    level = GetMonData(mon, MON_DATA_LEVEL) + DUNGEON_TRADE_LEVEL_BONUS;
    if (level > MAX_LEVEL)
        level = MAX_LEVEL;

    StringCopy(gStringVar1, GetSpeciesName(GetMonData(mon, MON_DATA_SPECIES)));
    ConvertIntToDecimalStringN(gStringVar2, level, STR_CONV_MODE_LEFT_ALIGN, 3);
    gSpecialVar_Result = 1;
}

// Commits it. OVERWRITES THE SLOT IN PLACE rather than removing and appending,
// which is what a trade is anyway - and it sidesteps party compaction entirely.
// Removing slot 2 of 4 means shuffling 3 and 4 down, and every path that holds a
// party index across that is a bug waiting to be written.
void RogueDungeon_EventTraderDo(void)
{
    u32 slot = gSpecialVar_0x8004;
    struct Pokemon *mon;
    u32 level;
    u16 species;

    if (slot >= CalculatePlayerPartyCount() || CalculatePlayerPartyCount() <= 1)
    {
        gSpecialVar_Result = 0;
        return;
    }

    mon = &gParties[B_TRAINER_PLAYER][slot];
    level = GetMonData(mon, MON_DATA_LEVEL) + DUNGEON_TRADE_LEVEL_BONUS;
    if (level > MAX_LEVEL)
        level = MAX_LEVEL;

    // Devolved HERE, against the level only now known. See the roll/devolve note
    // under sFloorEvents.
    species = RogueDungeon_DevolveForLevel(sEventSpecies, level);

    CreateRandomMonWithIVs(mon, species, level, MAX_PER_STAT_IVS);
    CalculateMonStats(mon);

    StringCopy(gStringVar2, GetSpeciesName(species));
    gSpecialVar_Result = 1;
}

// The egg. Free, and the cost is the party slot - which this build already treats
// as a wager, since the boss ace is refused on a full party and the archivist at
// the rest stop is the only way to make room. Result 0 means no room.
void RogueDungeon_EventEggTake(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    u8 level = FloorTargetLevel(floor);
    u16 species = RogueDungeon_DevolveForLevel(sEventSpecies, level);

    if (!GiveEventMon(species, level))
    {
        gSpecialVar_Result = 0;
        return;
    }

    StringCopy(gStringVar1, GetSpeciesName(species));
    gSpecialVar_Result = 1;
}

// The injured Pokemon. Result 1 if it joins, 0 if it was feigning - both decided
// at prepare time from the floor's seed, so reloading cannot reroll it. See
// DUNGEON_INJURED_AMBUSH_ODDS.
//
// Buffers the species either way, because the player can see its sprite and being
// told the wrong name would be worse than being told none.
void RogueDungeon_EventInjuredApproach(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);

    StringCopy(gStringVar1,
               GetSpeciesName(EventSpeciesForLevel(FloorTargetLevel(floor))));
    gSpecialVar_Result = !sEventAmbush;
}

// Result 0 means the party was full, in which case it is still standing there -
// the script says so rather than silently doing nothing.
void RogueDungeon_EventInjuredJoin(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);

    gSpecialVar_Result = GiveEventMon(EventSpeciesForLevel(FloorTargetLevel(floor)),
                                      FloorTargetLevel(floor));
}

// Sets up the ambush for the script's dowildbattle. CreateScriptedWildMon is what
// the setwildbattle command itself uses; going through it rather than the command
// is what lets the level be computed instead of assembled as a byte literal.
void RogueDungeon_EventInjuredAmbush(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    u32 level = FloorTargetLevel(floor) + DUNGEON_INJURED_AMBUSH_BONUS;

    if (level > MAX_LEVEL)
        level = MAX_LEVEL;

    CreateScriptedWildMon(EventSpeciesForLevel(level), level, ITEM_NONE);
}

// ------------------------------------------------- the second slate of events
//
// Every one of these was drafted assuming custom rooms and custom art. NONE of
// them needed either: each is a stock overworld sprite, a msgbox, and the small
// piece of arithmetic below. The props they stand next to are object events, not
// metatiles, for the reason in the PlaceEvents note.
//
// They all use the GLOBAL Random() where they need chance, never DungeonRandom -
// that stream belongs to floor generation and is not live once the player is
// standing on the floor.

// ---- The scout. A rare Pokemon nearby, and THE RUN'S FIRST SHINY.
//
// Forced shiny rather than lucky shiny. MON_DATA_IS_SHINY writes shinyModifier,
// which is a stored flag rather than a reroll, so this cannot fail to take and
// the caught Pokemon stays shiny forever after.
//
// The species comes from the same safari pool the injured Pokemon uses and is
// devolved to the floor's level, because a shiny the player cannot beat or catch
// is a worse memory than no shiny at all.
void RogueDungeon_EventScoutApproach(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);

    StringCopy(gStringVar1,
               GetSpeciesName(EventSpeciesForLevel(FloorTargetLevel(floor))));
}

void RogueDungeon_EventScoutBattle(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    u32 isShiny = TRUE;

    CreateScriptedWildMon(EventSpeciesForLevel(FloorTargetLevel(floor)),
                          FloorTargetLevel(floor), ITEM_NONE);
    SetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_IS_SHINY, &isShiny);
}

// ---- The memorial shrine.
//
// The sacrifice is only a choice because Revives are scarce - see
// DUNGEON_LOOT_REVIVE_WEIGHT. If revives ever become common again this branch
// should go, because it would be strictly worse than a bag slot.
#define DUNGEON_SHRINE_HP_COST_PERCENT 30
#define DUNGEON_SHRINE_REVIVE_PERCENT  50

// Result 0 when there is nothing to revive OR the lead cannot pay. Checked
// BEFORE the offer, so the shrine never charges for something it cannot deliver.
void RogueDungeon_EventShrineCanRevive(void)
{
    struct Pokemon *lead = &gParties[B_TRAINER_PLAYER][0];
    u32 count = CalculatePlayerPartyCount();
    u32 cost, i;

    gSpecialVar_Result = 0;

    if (GetMonData(lead, MON_DATA_HP) == 0)
        return;

    cost = (GetMonData(lead, MON_DATA_MAX_HP) * DUNGEON_SHRINE_HP_COST_PERCENT) / 100;
    if (cost == 0)
        cost = 1;
    // Strictly greater: paying down to exactly zero is a faint, and a shrine
    // that knocks out the lead to revive someone else is a trade nobody asked
    // for.
    if (GetMonData(lead, MON_DATA_HP) <= cost)
        return;

    for (i = 1; i < count; i++)
    {
        if (GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_SPECIES) != SPECIES_NONE
            && GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_HP) == 0)
        {
            gSpecialVar_Result = 1;
            return;
        }
    }
}

// Revives the FIRST fainted member, which keeps the event a single yes/no rather
// than a party menu on top of one.
void RogueDungeon_EventShrineRevive(void)
{
    struct Pokemon *lead = &gParties[B_TRAINER_PLAYER][0];
    u32 count = CalculatePlayerPartyCount();
    u32 hp, cost, i;

    gSpecialVar_Result = 0;

    cost = (GetMonData(lead, MON_DATA_MAX_HP) * DUNGEON_SHRINE_HP_COST_PERCENT) / 100;
    if (cost == 0)
        cost = 1;

    for (i = 1; i < count; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];

        if (GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE
            || GetMonData(mon, MON_DATA_HP) != 0)
            continue;

        hp = (GetMonData(mon, MON_DATA_MAX_HP) * DUNGEON_SHRINE_REVIVE_PERCENT) / 100;
        if (hp == 0)
            hp = 1;
        SetMonData(mon, MON_DATA_HP, &hp);
        StringCopy(gStringVar1, GetSpeciesName(GetMonData(mon, MON_DATA_SPECIES)));

        hp = GetMonData(lead, MON_DATA_HP) - cost;
        SetMonData(lead, MON_DATA_HP, &hp);

        gSpecialVar_Result = 1;
        return;
    }
}

// The offering. PRICED PER CHARM HELD, and it TAKES the money - the first cut of
// this event cleansed for free, which made the Shrine strictly better than never
// having been cursed and left the affliction system with no teeth at all.
//
// Buffer-then-resolve like the gambler: the figure is shown before the yes/no
// and carried in a static, so the charge cannot disagree with the offer.
#define DUNGEON_SHRINE_CLEANSE_PER_CHARM 400
#define DUNGEON_SHRINE_CLEANSE_PER_FLOOR 20

EWRAM_DATA static u32 sShrinePrice = 0;

// VAR_RESULT: 0 nothing to cleanse, 1 affordable, 2 cursed but cannot pay.
void RogueDungeon_EventShrineOffer(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    u32 charms;

    RogueCharm_ScriptCount();
    charms = gSpecialVar_Result;

    sShrinePrice = 0;
    if (charms == 0)
    {
        gSpecialVar_Result = 0;
        return;
    }

    sShrinePrice = charms * (DUNGEON_SHRINE_CLEANSE_PER_CHARM
                             + floor * DUNGEON_SHRINE_CLEANSE_PER_FLOOR);

    ConvertIntToDecimalStringN(gStringVar1, sShrinePrice, STR_CONV_MODE_LEFT_ALIGN, 6);
    ConvertIntToDecimalStringN(gStringVar2, charms, STR_CONV_MODE_LEFT_ALIGN, 2);

    gSpecialVar_Result = IsEnoughMoney(&gSaveBlock1Ptr->money, sShrinePrice) ? 1 : 2;
}

void RogueDungeon_EventShrineCleanse(void)
{
    if (sShrinePrice == 0 || !IsEnoughMoney(&gSaveBlock1Ptr->money, sShrinePrice))
    {
        gSpecialVar_Result = 0;
        return;
    }

    RemoveMoney(&gSaveBlock1Ptr->money, sShrinePrice);
    RogueCharm_ScriptCleanseAll();
    gSpecialVar_Result = 1;
}

// Disturbing the flames. Ghost-type, above the floor's level, catchable - the
// same shape as the injured Pokemon's ambush.
#define DUNGEON_SHRINE_GHOST_BONUS 4

static const u16 sShrineGhosts[] =
{
    SPECIES_GASTLY, SPECIES_HAUNTER, SPECIES_MISDREAVUS, SPECIES_SHUPPET,
    SPECIES_DUSKULL, SPECIES_SABLEYE,
};

void RogueDungeon_EventShrineGhost(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    u32 level = FloorTargetLevel(floor) + DUNGEON_SHRINE_GHOST_BONUS;
    u16 species = sShrineGhosts[Random() % ARRAY_COUNT(sShrineGhosts)];

    if (level > MAX_LEVEL)
        level = MAX_LEVEL;

    species = RogueDungeon_DevolveForLevel(species, level);
    CreateScriptedWildMon(species, level, ITEM_NONE);
}

// ---- The herbalist.
//
// The brew is HealPlayerParty, which already revives - so the whole of the
// drafted "fully revive all fainted Pokemon" is one existing special. The price
// is the charm, and Sluggish is act-scoped in the charm table exactly because
// the draft asked for "the remainder of the act".
// THE ORB AT THE SUMMIT. Mt Pyre's orbs are not gifts - they wake something, and
// whoever picks one up is along for the ride.
//
// TWO CHARMS, GRANTED TOGETHER, because a charm carries exactly one effect and
// this is a bargain rather than a boon. They are announced on separate lines at
// the start of the next battle, which is the point: the player watches the power
// arrive and the price arrive, and learns that the orb is doing both.
//
// BOTH ARE DURATION_ACT, so what the orb is worth depends on WHERE IN A DUNGEON
// it is found. Taken on the first floor of one it carries a whole act; taken on
// the last it is nearly nothing. That is a real decision rather than an
// oversight, and it is legible - the description says "this dungeon".
//
// Nothing is refused and nothing can fail: the orb is a choice the player makes
// at the yes/no, not a transaction that can come up short. That is why this
// returns nothing for the script to branch on.
void RogueDungeon_EventOrbTake(void)
{
    gSpecialVar_0x8000 = ROGUE_CHARM_ORB_AWAKENED;
    gSpecialVar_0x8001 = 0xFFFF;   // the table's own duration
    RogueCharm_ScriptGrantParty();

    gSpecialVar_0x8000 = ROGUE_CHARM_ORB_BURDENED;
    gSpecialVar_0x8001 = 0xFFFF;
    RogueCharm_ScriptGrantParty();
}

// The Shuppet eats one affliction. RESULT is the charm it took, or
// ROGUE_CHARM_NONE if the party was carrying nothing worth eating.
//
// AFFLICTIONS ONLY, and the loop below is what enforces it - a grief-eater that
// happily swallowed Emboldened would be a trap rather than a bargain, and the
// player has no way to steer it. Scanning in table order means it takes the
// first affliction it finds rather than the worst one; that is deliberate, since
// choosing for the player would need a menu this event does not want.
void RogueDungeon_EventShuppetEat(void)
{
    u32 id;

    for (id = ROGUE_CHARM_NONE + 1; id < ROGUE_CHARM_COUNT; id++)
    {
        if (!RogueCharm_IsAffliction(id))
            continue;

        gSpecialVar_0x8000 = id;
        RogueCharm_ScriptCleanseOne();

        if (gSpecialVar_Result != 0)
        {
            RogueCharm_ScriptBufferName();   // gStringVar1 = what it took
            gSpecialVar_Result = id;
            return;
        }
    }

    gSpecialVar_Result = ROGUE_CHARM_NONE;
}

// ---------------------------------------------------------------- the phantom
//
// Is this floor haunted, and after how many steps? Both hashed from the floor
// seed, so they reproduce on reload and cost nothing to store. See
// DUNGEON_PHANTOM_ODDS.
static bool8 FloorIsHaunted(u16 floor)
{
    u16 seed = VarGet(VAR_ROGUE_DUNGEON_SEED);

    // Never on a boss floor. An arena is a fight the player walked into
    // deliberately, and an ambush on top of it is not tension, it is a mugging.
    if (IsDungeonBossFloor(floor) || IsMiniBossFloor(floor))
        return FALSE;

    return DecorHash(seed, DUNGEON_PHANTOM_SALT, 0) % DUNGEON_PHANTOM_ODDS == 0;
}

static u32 PhantomStepThreshold(void)
{
    u16 seed = VarGet(VAR_ROGUE_DUNGEON_SEED);
    u32 span = DUNGEON_PHANTOM_STEPS_MAX - DUNGEON_PHANTOM_STEPS_MIN + 1;

    return DUNGEON_PHANTOM_STEPS_MIN
         + DecorHash(seed, DUNGEON_PHANTOM_SALT, 1) % span;
}

// The arrival warning, fired on the FIRST step of a haunted floor rather than on
// load. The map's frame table keys on a var and every var is claimed, so hanging
// it off the step hook costs nothing new - and a warning that lands on the first
// step reads better anyway: the player has taken one step into the room before
// the floor tells them something is wrong.
bool8 RogueDungeon_PhantomShouldWarn(void)
{
    if (!FloorIsHaunted(VarGet(VAR_ROGUE_DUNGEON_FLOOR)))
        return FALSE;

    if (VarGet(VAR_ROGUE_PHANTOM_STEPS) != 0)
        return FALSE;

    // This step counts. Setting it here is what stops the warning repeating and
    // starts the clock in the same move.
    VarSet(VAR_ROGUE_PHANTOM_STEPS, 1);
    return TRUE;
}

// Called once per step from TryStartStepCountScript, which is where vanilla
// hangs egg hatching, poison and the Regice puzzle. Returns TRUE on the step the
// phantom strikes, and only once - the counter is left above the threshold.
bool8 RogueDungeon_PhantomShouldStrike(void)
{
    u32 steps;

    if (!FloorIsHaunted(VarGet(VAR_ROGUE_DUNGEON_FLOOR)))
        return FALSE;

    steps = VarGet(VAR_ROGUE_PHANTOM_STEPS);

    // Already struck on this floor. The counter is the guard as well as the
    // clock, so nothing else has to remember.
    if (steps == 0xFFFF)
        return FALSE;

    steps++;
    if (steps < PhantomStepThreshold())
    {
        VarSet(VAR_ROGUE_PHANTOM_STEPS, steps);
        return FALSE;
    }

    VarSet(VAR_ROGUE_PHANTOM_STEPS, 0xFFFF);
    return TRUE;
}

void RogueDungeon_PhantomBattle(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    u32 level = FloorTargetLevel(floor) + DUNGEON_PHANTOM_LEVEL_BONUS;

    if (level > MAX_LEVEL)
        level = MAX_LEVEL;

    CreateScriptedWildMon(SPECIES_GASTLY, level, ITEM_NONE);
}

// The offering. RESULT is 0 when the player cannot pay, 1 when they have.
void RogueDungeon_PhantomToll(void)
{
    u32 cost = GetMoney(&gSaveBlock1Ptr->money) / DUNGEON_PHANTOM_TOLL_DIVISOR;

    if (cost == 0 || !IsEnoughMoney(&gSaveBlock1Ptr->money, cost))
    {
        gSpecialVar_Result = 0;
        return;
    }

    RemoveMoney(&gSaveBlock1Ptr->money, cost);
    ConvertIntToDecimalStringN(gStringVar1, cost, STR_CONV_MODE_LEFT_ALIGN, 6);
    gSpecialVar_Result = 1;
}

// The Clefairy's stone. RESULT is 0 if the bag had no room, 1 otherwise.
//
// A MOON STONE SPECIFICALLY, not a rolled one. The event exists because Clefairy
// and Pikachu are stone-locked and DUNGEON_STONE_ODDS is the only other source;
// handing out a Fire Stone here would be a different event that happens to have
// Clefairy in it.
//
// THE CURSE IS GRANTED HERE, NOT IN THE SCRIPT, because the charm interface is a
// C one - RogueCharm_ScriptGrant* read the special vars and are not registered
// as script specials. Every other event that grants a charm does it this way;
// see RogueDungeon_EventHerbBrew.
//
// And only on SUCCESS. A bag with no room means the player never got the stone,
// and cursing them for an offer that failed would be punishing them for their
// inventory.
void RogueDungeon_EventClefairyStone(void)
{
    if (!AddBagItem(ITEM_MOON_STONE, 1))
    {
        gSpecialVar_Result = 0;
        return;
    }

    gSpecialVar_0x8000 = ROGUE_CHARM_CURSED;
    gSpecialVar_0x8001 = 0xFFFF;   // the table's own duration
    RogueCharm_ScriptGrantParty();

    gSpecialVar_Result = 1;
}

// ------------------------------------------------------ the ability transposer
//
// Can the lead actually take a hidden ability? RESULT is 0 when it cannot, and
// the event has to ask BEFORE it offers, because charging for nothing is worse
// than not being offered anything.
//
// TWO WAYS TO FAIL AND BOTH ARE COMMON. A species may have no hidden ability at
// all (abilities[2] is ABILITY_NONE), or its hidden ability may be the SAME one
// it already has - which happens across whole families and would make the offer
// a con. Checking only the first is the mistake that ships.
static bool32 LeadCanTranspose(struct Pokemon *mon)
{
    u32 species = GetMonData(mon, MON_DATA_SPECIES);
    u32 hidden = gSpeciesInfo[species].abilities[NUM_NORMAL_ABILITY_SLOTS];
    u32 current = GetMonAbility(mon);

    if (hidden == ABILITY_NONE)
        return FALSE;

    return hidden != current;
}

void RogueDungeon_EventTransposerCheck(void)
{
    struct Pokemon *mon = &gPlayerParty[0];

    if (!LeadCanTranspose(mon))
    {
        gSpecialVar_Result = 0;
        return;
    }

    StringCopy(gStringVar1, GetSpeciesName(GetMonData(mon, MON_DATA_SPECIES)));
    StringCopy(gStringVar2, gAbilitiesInfo[
        gSpeciesInfo[GetMonData(mon, MON_DATA_SPECIES)]
            .abilities[NUM_NORMAL_ABILITY_SLOTS]].name);
    gSpecialVar_Result = 1;
}

// Does the switch. The PRICE is chosen by the script and applied there or here:
// VAR_0x8004 is 0 for the money price and 1 for the charm.
void RogueDungeon_EventTransposerApply(void)
{
    struct Pokemon *mon = &gPlayerParty[0];

    if (!LeadCanTranspose(mon))
    {
        gSpecialVar_Result = 0;
        return;
    }

    // The hidden slot index, not a magic 2 - NUM_NORMAL_ABILITY_SLOTS is what
    // says where the normal ones stop.
    SetMonData(mon, MON_DATA_ABILITY_NUM, &(u8){NUM_NORMAL_ABILITY_SLOTS});

    if (gSpecialVar_0x8004 != 0)
    {
        gSpecialVar_0x8000 = ROGUE_CHARM_BRITTLE;
        gSpecialVar_0x8001 = 0xFFFF;   // the table's own duration
        gSpecialVar_0x8002 = 0;        // the lead, which is the mon that changed
        RogueCharm_ScriptGrantMon();
    }

    gSpecialVar_Result = 1;
}

// --------------------------------------------------------- the pokerus injector
//
// Grants Pokerus, then takes the lead to 1 HP and poisons it.
//
// POKERUS IS OTHERWISE INVISIBLE IN A RUN. Nothing grants it, nothing sells it,
// and the EV allocator that makes it worth having was merged separately - so the
// doubled EV gain is a real reward that the game has never once handed out.
//
// The price is deliberately front-loaded and survivable-but-not-safe: 1 HP with
// poison means the next battle is a decision and the walk to it is a risk.
void RogueDungeon_EventPokerusInject(void)
{
    struct Pokemon *mon = &gPlayerParty[0];
    u32 hp = 1;
    u32 status = STATUS1_POISON;
    // The strain value, not a boolean. The low nibble is the strain and the high
    // nibble the days remaining; anything non-zero in the low nibble reads as
    // infected, and CheckPartyPokerus looks at exactly that.
    u8 pokerus = 0x40 | 0x1;

    SetMonData(mon, MON_DATA_POKERUS, &pokerus);
    SetMonData(mon, MON_DATA_HP, &hp);
    SetMonData(mon, MON_DATA_STATUS, &status);

    StringCopy(gStringVar1, GetSpeciesName(GetMonData(mon, MON_DATA_SPECIES)));
}

// Dancing with them instead. Costs nothing and cannot fail, which is the whole
// contrast with the stone.
void RogueDungeon_EventClefairyDance(void)
{
    gSpecialVar_0x8000 = ROGUE_CHARM_EMBOLDENED;
    gSpecialVar_0x8001 = 0xFFFF;
    RogueCharm_ScriptGrantParty();
}

void RogueDungeon_EventHerbBrew(void)
{
    HealPlayerParty();

    gSpecialVar_0x8000 = ROGUE_CHARM_SLUGGISH;
    gSpecialVar_0x8001 = 0xFFFF;   // the table's own duration
    RogueCharm_ScriptGrantParty();
}

// Foraging. Status-curing berries, which are the ones worth having in a run
// where a sleeping lead is how a floor goes wrong.
static const u16 sHerbBerries[] =
{
    ITEM_LUM_BERRY, ITEM_CHESTO_BERRY, ITEM_CHERI_BERRY,
    ITEM_PECHA_BERRY, ITEM_RAWST_BERRY, ITEM_ASPEAR_BERRY,
};

#define DUNGEON_HERB_FORAGE_MIN 2
#define DUNGEON_HERB_FORAGE_MAX 3

void RogueDungeon_EventHerbForage(void)
{
    u32 count = DUNGEON_HERB_FORAGE_MIN
              + (Random() % (DUNGEON_HERB_FORAGE_MAX - DUNGEON_HERB_FORAGE_MIN + 1));
    u16 item = sHerbBerries[Random() % ARRAY_COUNT(sHerbBerries)];

    // One KIND of berry, several of it - a handful of one thing reads as
    // foraging, where one each of three reads as a shop.
    gSpecialVar_Result = AddBagItem(item, count);
    StringCopy(gStringVar1, GetItemName(item));
    ConvertIntToDecimalStringN(gStringVar2, count, STR_CONV_MODE_LEFT_ALIGN, 1);
}

// ---- The fossil dig.
//
// FOSSILS ARE NOT UNIQUE and this event does not pretend they are - Aerodactyl,
// Cranidos, Kabuto, Omanyte and Shieldon are already in sSafariLandSpecies and
// three more are in theme pools. It is a good reward, not an exclusive one.
static const u16 sFossilSpecies[] =
{
    SPECIES_OMANYTE, SPECIES_KABUTO, SPECIES_AERODACTYL,
    SPECIES_LILEEP,  SPECIES_ANORITH, SPECIES_CRANIDOS, SPECIES_SHIELDON,
};

#define DUNGEON_FOSSIL_SELL_PER_FLOOR 60
#define DUNGEON_FOSSIL_SELL_MIN       500
#define DUNGEON_FOSSIL_EV_GRANT       12

EWRAM_DATA static u16 sFossilSpeciesRolled = 0;

// Buffer-then-resolve, the same split the gambler and pedlar use: the player is
// shown the real figure before the yes/no, and the commit cannot disagree with
// what was offered.
void RogueDungeon_EventFossilOffer(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    u32 price = DUNGEON_FOSSIL_SELL_MIN + floor * DUNGEON_FOSSIL_SELL_PER_FLOOR;

    sFossilSpeciesRolled = sFossilSpecies[Random() % ARRAY_COUNT(sFossilSpecies)];

    ConvertIntToDecimalStringN(gStringVar1, price, STR_CONV_MODE_LEFT_ALIGN, 6);
    gSpecialVar_Result = price;
}

// The fossil, and the hex that comes with it. Result 0 means a full party, and
// the script says so rather than taking the choice and giving nothing.
void RogueDungeon_EventFossilTake(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);

    if (!GiveEventMon(sFossilSpeciesRolled, FloorTargetLevel(floor)))
    {
        gSpecialVar_Result = 0;
        return;
    }

    StringCopy(gStringVar1, GetSpeciesName(sFossilSpeciesRolled));

    gSpecialVar_0x8000 = ROGUE_CHARM_HEXED;
    gSpecialVar_0x8001 = 0xFFFF;
    RogueCharm_ScriptGrantParty();

    gSpecialVar_Result = 1;
}

void RogueDungeon_EventFossilSell(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);

    AddMoney(&gSaveBlock1Ptr->money,
             DUNGEON_FOSSIL_SELL_MIN + floor * DUNGEON_FOSSIL_SELL_PER_FLOOR);
    gSpecialVar_Result = 1;
}

// Studying the carvings. EVs rather than EXP, because EXP is what the anti-grind
// clock exists to meter and handing it out here would work against it.
//
// ONE STAT PER POKEMON, ROLLED, rather than a flat spread: a spread of twelve
// across six stats is beneath notice, where twelve in one is a real nudge, and
// the roll is what makes the choice worth taking twice in a run.
void RogueDungeon_EventFossilStudy(void)
{
    u32 count = CalculatePlayerPartyCount();
    u32 i;

    for (i = 0; i < count; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];
        u32 stat, ev, total = 0, j;

        if (GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE)
            continue;

        for (j = 0; j < NUM_STATS; j++)
            total += GetMonData(mon, MON_DATA_HP_EV + j);

        if (total + DUNGEON_FOSSIL_EV_GRANT > MAX_TOTAL_EVS)
            continue;

        stat = Random() % NUM_STATS;
        ev = GetMonData(mon, MON_DATA_HP_EV + stat) + DUNGEON_FOSSIL_EV_GRANT;
        if (ev > MAX_PER_STAT_EVS)
            ev = MAX_PER_STAT_EVS;

        SetMonData(mon, MON_DATA_HP_EV + stat, &ev);
        CalculateMonStats(mon);
    }

    gSpecialVar_Result = 1;
}

// ---- The shady move tutor.
//
// THE MOVES ARE OFF-TYPE AND HIGH-TIER ON PURPOSE - a forbidden move is only
// interesting if the Pokemon could never have had it, and this is the single
// cheapest thing in the whole slate that makes one run feel unlike the last.
static const u16 sForbiddenMoves[] =
{
    MOVE_DRACO_METEOR, MOVE_CLOSE_COMBAT, MOVE_EARTHQUAKE, MOVE_ICE_BEAM,
    MOVE_THUNDERBOLT, MOVE_FLAMETHROWER, MOVE_SHADOW_BALL, MOVE_SURF,
    MOVE_PSYCHIC, MOVE_CRUNCH, MOVE_AERIAL_ACE, MOVE_ROCK_SLIDE,
};

EWRAM_DATA static u16 sTutorMove = 0;
EWRAM_DATA static u8 sTutorSlot = 0;

// The mon comes from ChoosePartyMon, which is UNFILTERED - the whole point is
// teaching a move the Pokemon could never learn, so the move tutor's own
// selection flow is exactly the wrong one to reuse.
//
// Buffers the move being taught AND the move being lost, before the yes/no.
// Overwriting a move without saying which would be the single most unfair thing
// in the run, and the buffer-then-resolve split is already this file's idiom.
void RogueDungeon_EventTutorOffer(void)
{
    struct Pokemon *mon;
    u32 slot = gSpecialVar_0x8004;
    u32 i;

    gSpecialVar_Result = 0;

    if (slot >= CalculatePlayerPartyCount())
        return;

    mon = &gParties[B_TRAINER_PLAYER][slot];
    if (GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE)
        return;

    sTutorMove = sForbiddenMoves[Random() % ARRAY_COUNT(sForbiddenMoves)];
    StringCopy(gStringVar1, GetMoveName(sTutorMove));
    StringCopy(gStringVar2, GetSpeciesName(GetMonData(mon, MON_DATA_SPECIES)));

    // A free slot if there is one; otherwise the LAST, and the script names what
    // it is about to lose. Result 1 means a free slot, 2 means something goes.
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        if (GetMonData(mon, MON_DATA_MOVE1 + i) == MOVE_NONE)
        {
            sTutorSlot = i;
            gSpecialVar_Result = 1;
            return;
        }
        // Refusing a move it already knows, which would otherwise read as the
        // tutor stealing money for nothing.
        if (GetMonData(mon, MON_DATA_MOVE1 + i) == sTutorMove)
        {
            gSpecialVar_Result = 0;
            return;
        }
    }

    sTutorSlot = MAX_MON_MOVES - 1;
    StringCopy(gStringVar3, GetMoveName(GetMonData(mon, MON_DATA_MOVE1 + sTutorSlot)));
    gSpecialVar_Result = 2;
}

void RogueDungeon_EventTutorTeach(void)
{
    struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][gSpecialVar_0x8004];

    SetMonMoveSlot(mon, sTutorMove, sTutorSlot);

    // The price. Frail rather than Overexerted because the tutor is the one
    // event whose reward is permanent and immediately usable - see the charm
    // table's note on the two.
    gSpecialVar_0x8000 = ROGUE_CHARM_FRAIL;
    gSpecialVar_0x8001 = 0xFFFF;
    gSpecialVar_0x8002 = gSpecialVar_0x8004;
    RogueCharm_ScriptGrantMon();

    gSpecialVar_Result = 1;
}

// ---- The volatile evolution crystal. Ultra rare - see its weight in
// sFloorEvents - and a palette variant of the breakable rock rather than new art.
void RogueDungeon_EventCrystalOffer(void)
{
    struct Pokemon *mon;
    u32 slot = gSpecialVar_0x8004;
    enum Species target;

    gSpecialVar_Result = 0;

    if (slot >= CalculatePlayerPartyCount())
        return;

    mon = &gParties[B_TRAINER_PLAYER][slot];
    if (GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE)
        return;

    // SCRIPT_TRIGGER rather than NORMAL: the whole offer is skipping the level
    // requirement, and NORMAL would refuse everything under-levelled - which is
    // every Pokemon the player would actually want to bring here.
    target = GetEvolutionTargetSpecies(mon, EVO_MODE_SCRIPT_TRIGGER, ITEM_NONE,
                                       NULL, NULL, CHECK_EVO);
    if (target == SPECIES_NONE)
        return;

    StringCopy(gStringVar1, GetSpeciesName(GetMonData(mon, MON_DATA_SPECIES)));
    StringCopy(gStringVar2, GetSpeciesName(target));
    gSpecialVar_Result = 1;
}

void RogueDungeon_EventCrystalEvolve(void)
{
    struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][gSpecialVar_0x8004];
    enum Species target = GetEvolutionTargetSpecies(mon, EVO_MODE_SCRIPT_TRIGGER,
                                                    ITEM_NONE, NULL, NULL,
                                                    DO_EVO);

    if (target == SPECIES_NONE)
    {
        gSpecialVar_Result = 0;
        return;
    }

    SetMonData(mon, MON_DATA_SPECIES, &target);
    CalculateMonStats(mon);

    gSpecialVar_0x8000 = ROGUE_CHARM_UNSTABLE;
    gSpecialVar_0x8001 = 0xFFFF;
    gSpecialVar_0x8002 = gSpecialVar_0x8004;
    RogueCharm_ScriptGrantMon();

    gSpecialVar_Result = 1;
}

// ---- The Ditto item ball. A trap that looks exactly like a reward.
//
// A DOUBLE wild battle, which is where the joke lands: Ditto's own Transform
// copies the player's lead, so the trap fights back with the team that walked
// into it and no special code is needed to make that happen.
#define DUNGEON_DITTO_LEVEL_BONUS 2

void RogueDungeon_EventDittoAmbush(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    u32 level = FloorTargetLevel(floor) + DUNGEON_DITTO_LEVEL_BONUS;

    if (level > MAX_LEVEL)
        level = MAX_LEVEL;

    CreateScriptedDoubleWildMon(SPECIES_DITTO, level, ITEM_NONE,
                                SPECIES_DITTO, level, ITEM_NONE);
}

// ---- The enraged totem.
//
// NO LEGENDARIES. sSafariLandSpecies is the pool the rest of the floor draws
// from and carries none, so this needs no exclusion list of its own - but if that
// pool ever gains one, this is the event that would hand the player a legendary
// for winning a coin flip.
#define DUNGEON_TOTEM_LEVEL_BONUS 5
#define DUNGEON_TOTEM_SNEAK_PERCENT 25

void RogueDungeon_EventTotemApproach(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);

    StringCopy(gStringVar1,
               GetSpeciesName(EventSpeciesForLevel(FloorTargetLevel(floor))));
}

// The stat boost itself is the SCRIPT's settotemboost, not ours - the engine
// already has the whole totem-battle feature including its animation and its
// message, and reimplementing it here would be a second copy that drifts.
void RogueDungeon_EventTotemBattle(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    u32 level = FloorTargetLevel(floor) + DUNGEON_TOTEM_LEVEL_BONUS;

    if (level > MAX_LEVEL)
        level = MAX_LEVEL;

    // Devolved against the FLOOR's level, not the totem's boosted one. It is
    // already five levels up with every stat raised a stage; letting it be a
    // whole evolution higher as well is the difference between a hard fight
    // and an unwinnable one.
    CreateScriptedWildMon(EventSpeciesForLevel(FloorTargetLevel(floor)),
                          level, ITEM_NONE);
}

// Laying down food. Costs one healing item from the bag and turns the fight into
// an ordinary catchable encounter at the floor's own level.
void RogueDungeon_EventTotemFeed(void)
{
    static const u16 sFood[] =
    {
        ITEM_POTION, ITEM_SUPER_POTION, ITEM_HYPER_POTION, ITEM_FULL_RESTORE,
        ITEM_ORAN_BERRY, ITEM_SITRUS_BERRY,
    };
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    u32 i;

    gSpecialVar_Result = 0;

    // Cheapest first, so appeasing it does not quietly spend the Full Restore
    // the player was saving for the boss.
    for (i = 0; i < ARRAY_COUNT(sFood); i++)
    {
        if (!CheckBagHasItem(sFood[i], 1))
            continue;

        RemoveBagItem(sFood[i], 1);
        StringCopy(gStringVar2, GetItemName(sFood[i]));
        CreateScriptedWildMon(EventSpeciesForLevel(FloorTargetLevel(floor)),
                              FloorTargetLevel(floor), ITEM_NONE);
        gSpecialVar_Result = 1;
        return;
    }
}

// ---- what the totem is guarding.
//
// The event's own text has always said "something glitters behind it" and the
// challenge branch handed over nothing at all. This is the something.
//
// A MEGA STONE FOR A POKEMON THE PLAYER ACTUALLY HAS. A stone is the most
// run-defining item in the build and also the most useless one: Charizardite
// found by a team with no Charizard is a bag slot. So the party is searched
// first and the reward is drawn from what it can actually use, which turns the
// totem from a loot roll into the moment a run's plan comes together.
//
// The lookup walks the species' own form change table rather than keeping a
// second species-to-stone list. Those tables are where the pairing is DEFINED -
// {FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM, SPECIES_x_MEGA, ITEM_xITE} - and a
// copy here would drift the moment P_MEGA_EVOLUTIONS or the species roster moved.
//
// Z-CRYSTALS ARE THE OTHER HALF, and they are the fallback as well as the rare
// roll. A crystal is keyed on a TYPE rather than a species, so it is the one
// reward that is never dead weight - which makes it exactly right for a party
// with nothing that megas, and worth a rare roll even for one that does.
#define DUNGEON_TOTEM_Z_ODDS 8

// Type-indexed, matching the item ids' own order. Read straight off the run of
// ITEM_NORMALIUM_Z onward in constants/items.h.
static const u16 sTotemZCrystals[] =
{
    [TYPE_NORMAL]   = ITEM_NORMALIUM_Z,
    [TYPE_FIRE]     = ITEM_FIRIUM_Z,
    [TYPE_WATER]    = ITEM_WATERIUM_Z,
    [TYPE_ELECTRIC] = ITEM_ELECTRIUM_Z,
    [TYPE_GRASS]    = ITEM_GRASSIUM_Z,
    [TYPE_ICE]      = ITEM_ICIUM_Z,
    [TYPE_FIGHTING] = ITEM_FIGHTINIUM_Z,
    [TYPE_POISON]   = ITEM_POISONIUM_Z,
    [TYPE_GROUND]   = ITEM_GROUNDIUM_Z,
    [TYPE_FLYING]   = ITEM_FLYINIUM_Z,
    [TYPE_PSYCHIC]  = ITEM_PSYCHIUM_Z,
    [TYPE_BUG]      = ITEM_BUGINIUM_Z,
    [TYPE_ROCK]     = ITEM_ROCKIUM_Z,
    [TYPE_GHOST]    = ITEM_GHOSTIUM_Z,
    [TYPE_DRAGON]   = ITEM_DRAGONIUM_Z,
    [TYPE_DARK]     = ITEM_DARKINIUM_Z,
    [TYPE_STEEL]    = ITEM_STEELIUM_Z,
    [TYPE_FAIRY]    = ITEM_FAIRIUM_Z,
};

// The stone this species megas with, or ITEM_NONE. Charizard and Mewtwo have
// two; the first is taken, because a run cannot use both and picking between
// them is a decision the player has no information to make here.
static u16 MegaStoneFor(u16 species)
{
    const struct FormChange *changes = GetSpeciesFormChanges(species);
    u32 i;

    if (changes == NULL)
        return ITEM_NONE;

    for (i = 0; changes[i].method != FORM_CHANGE_TERMINATOR; i++)
    {
        if (changes[i].method == FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM)
            return changes[i].param1;
    }

    return ITEM_NONE;
}

// A crystal for a type somebody on the team actually attacks with, chosen from a
// party member at random rather than always the lead - the lead is whoever is
// healthiest, which is not the same as whoever the run is built around.
static u16 TotemZCrystal(void)
{
    u32 count = CalculatePlayerPartyCount();
    struct Pokemon *mon;
    u32 type;

    if (count == 0)
        return sTotemZCrystals[TYPE_NORMAL];

    mon = &gParties[B_TRAINER_PLAYER][Random() % count];
    type = GetSpeciesType(GetMonData(mon, MON_DATA_SPECIES), Random() % 2);

    if (type >= ARRAY_COUNT(sTotemZCrystals) || sTotemZCrystals[type] == ITEM_NONE)
        type = TYPE_NORMAL;

    return sTotemZCrystals[type];
}

// VAR_RESULT: 0 the bag had no room, 1 it was handed over.
void RogueDungeon_EventTotemReward(void)
{
    u32 count = CalculatePlayerPartyCount();
    u16 stones[PARTY_SIZE];
    u32 found = 0, i;
    u16 item;

    gSpecialVar_Result = 0;

    for (i = 0; i < count; i++)
    {
        u16 stone = MegaStoneFor(GetMonData(&gParties[B_TRAINER_PLAYER][i],
                                            MON_DATA_SPECIES));

        // Skipped if the player already carries it: a second Charizardite is a
        // bag slot, and this is the one event whose whole promise is that the
        // prize was worth the fight.
        if (stone != ITEM_NONE && !CheckBagHasItem(stone, 1))
            stones[found++] = stone;
    }

    if (found != 0 && (Random() % DUNGEON_TOTEM_Z_ODDS) != 0)
        item = stones[Random() % found];
    else
        item = TotemZCrystal();

    if (!AddBagItem(item, 1))
        return;

    StringCopy(gStringVar1, GetItemName(item));
    gSpecialVar_Result = 1;
}

// Did the player actually beat it? dowildbattle leaves the outcome in
// gBattleOutcome and nothing puts it where a script can compare it.
//
// CAUGHT COUNTS. A totem that is caught rather than knocked out was still
// overcome, and withholding the prize for the better outcome would be perverse.
void RogueDungeon_EventBattleWon(void)
{
    gSpecialVar_Result = (gBattleOutcome == B_OUTCOME_WON
                          || gBattleOutcome == B_OUTCOME_CAUGHT);
}

void RogueDungeon_EventTotemSneak(void)
{
    u32 cost = GetMoney(&gSaveBlock1Ptr->money) * DUNGEON_TOTEM_SNEAK_PERCENT / 100;

    if (cost == 0 || !IsEnoughMoney(&gSaveBlock1Ptr->money, cost))
    {
        gSpecialVar_Result = 0;
        return;
    }

    RemoveMoney(&gSaveBlock1Ptr->money, cost);
    ConvertIntToDecimalStringN(gStringVar2, cost, STR_CONV_MODE_LEFT_ALIGN, 6);
    gSpecialVar_Result = 1;
}

static void PlaceRocks(u16 floor)
{
    // Depth-scaled, as the berries and item balls are. This ran to
    // DUNGEON_MAX_ROCKS flat on every floor in the game.
    u32 count = DUNGEON_ROCK_MIN + floor / DUNGEON_ROCK_FLOORS_PER_EXTRA;
    u32 i, j;

    sRockCount = 0;

    if (sRoomCount == 0)
        return;
    if (count > DUNGEON_MAX_ROCKS)
        count = DUNGEON_MAX_ROCKS;

    for (i = 0; i < count; i++)
    {
        u32 room = PickRoomLeastUsedFrom(0);
        u8 x = sRooms[room].x + (DungeonRandom() % sRooms[room].w);
        u8 y = sRooms[room].y + (DungeonRandom() % sRooms[room].h);

        if (x == sStairsX && y == sStairsY)
            continue;

        for (j = 0; j < sItemCount; j++)
            if (sItemX[j] == x && sItemY[j] == y)
                break;
        if (j != sItemCount)
            continue;

        for (j = 0; j < sTrainerCount; j++)
            if (sTrainerX[j] == x && sTrainerY[j] == y)
                break;
        if (j != sTrainerCount)
            continue;

        for (j = 0; j < sBerryCount; j++)
            if (sBerryX[j] == x && sBerryY[j] == y)
                break;
        if (j != sBerryCount)
            continue;

        for (j = 0; j < sRockCount; j++)
            if (sRockX[j] == x && sRockY[j] == y)
                break;
        if (j != sRockCount)
            continue;

        NoteRoomUsed(room);
        sRockX[sRockCount] = x;
        sRockY[sRockCount] = y;
        sRockCount++;
    }
}

// Plants what PlaceBerryTrees chose, into the save block.
//
// SEPARATE FROM PLACEMENT, AND CALLED FROM A DIFFERENT PLACE, because
// gSaveBlock1Ptr->berryTrees is saved state and the two run on different
// schedules. Preparing a floor happens on every load, including load-from-save;
// planting must happen only when a genuinely new floor is rolled, or reloading
// a save would regrow every tree the player had already picked.
//
// The template loader is that place - it does not run on the load-from-save
// path, which is the same property the trainer defeat flags rely on.
//
// Planted straight to BERRY_STAGE_BERRIES with growth stopped, because a run is
// not long enough to wait for anything and the growth clock is real time. The
// yield is overwritten rather than calculated: CalcBerryYield gives the stock
// 2-6 spread off watering that never happened here.
// Script side of the above: is the tree the player is standing at a dud?
//
// Reads berryYield rather than re-deriving the hash, because the yield is what
// was actually planted - if the two ever disagreed, believing the hash would
// tell the player one thing and hand them another.
void RogueDungeon_EventBerryIsRotten(void)
{
    u8 id = GetObjectEventBerryTreeId(gSelectedObjectEvent);

    gSpecialVar_Result = (GetBerryTreeInfo(id)->berryYield
                          == DUNGEON_BERRY_ROTTEN_YIELD);
}

// Is tree i on this floor a rotten dud?
//
// Two ways in: the floor's guaranteed rotten tree, and the independent per-tree
// roll that was here before. Both read the floor seed through DecorHash rather
// than the generation stream, because this runs from the TEMPLATE LOADER and
// not from PrepareFloor - the stream has already been spent on the layout, and
// the loader does not run on every path onto a floor.
//
// The guaranteed index uses salt 0x5B0 on the y axis, which no tree index can
// produce, so the two hashes cannot alias each other.
#define DUNGEON_BERRY_ROTTEN_SALT 0x5B0

bool8 RogueDungeon_IsBerryRotten(u32 tree)
{
    u16 seed = VarGet(VAR_ROGUE_DUNGEON_SEED);

    // sBerryCount >= MIN_TREES, not != 0: the modulo below is what picks the
    // victim, so on a one tree floor it always picks that tree and the floor's
    // only berry is always a dud. See DUNGEON_BERRY_ROTTEN_MIN_TREES.
    if (DUNGEON_BERRY_ROTTEN_GUARANTEED && sBerryCount >= DUNGEON_BERRY_ROTTEN_MIN_TREES
        && DecorHash(seed, DUNGEON_BERRY_ROTTEN_SALT, 0) % sBerryCount == tree)
        return TRUE;

    return DecorHash(seed, tree, 0) % DUNGEON_BERRY_ROTTEN_ODDS == 0;
}

// Is this tree the imposter? See DUNGEON_SUDOWOODO_ODDS.
//
// TWO HASHES, NOT ONE, and the second is what keeps it fair. The first decides
// whether this FLOOR hides one at all; only then does the second pick which
// tree. Folding them into a single roll per tree would make the number of
// Sudowoodos on a floor binomial - a floor with three of them is a joke rather
// than an ambush, and a run would eventually produce one.
//
// Its own salt, distinct from the rot salt, for the reason that one has a salt:
// a tree index can never collide with it, so the two questions cannot answer
// each other.
bool8 RogueDungeon_IsSudowoodoTree(u32 tree)
{
    u16 seed = VarGet(VAR_ROGUE_DUNGEON_SEED);

    if (sBerryCount == 0)
        return FALSE;

    if (DecorHash(seed, DUNGEON_SUDOWOODO_SALT, 0) % DUNGEON_SUDOWOODO_ODDS != 0)
        return FALSE;

    return DecorHash(seed, DUNGEON_SUDOWOODO_SALT, 1) % sBerryCount == tree;
}

// The imposter, at the floor's level plus the ambush bonus.
//
// NOT ROUTED THROUGH EventSpeciesForLevel: this is Sudowoodo or it is nothing.
// The species IS the joke, and a rolled species standing in a grove pretending
// to be a tree would be a different and much worse event.
void RogueDungeon_EventSudowoodoAmbush(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    u32 level = FloorTargetLevel(floor) + DUNGEON_SUDOWOODO_LEVEL_BONUS;

    if (level > MAX_LEVEL)
        level = MAX_LEVEL;

    CreateScriptedWildMon(SPECIES_SUDOWOODO, level, ITEM_NONE);
}

// One wave of the nest. VAR_0x8004 is which wave, 0-based, and the level climbs
// with it - the third is the one that decides whether the player should have
// walked away, so it cannot be the same fight as the first.
//
// BELOW the floor's level rather than above it. Three fights with no heal
// between is the difficulty; making each one individually hard as well would
// make this a wall rather than a gamble on the party's depth.
void RogueDungeon_EventNestWave(void)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    u32 level = FloorTargetLevel(floor);
    u32 wave = gSpecialVar_0x8004;

    if (level > DUNGEON_NEST_LEVEL_MALUS)
        level -= DUNGEON_NEST_LEVEL_MALUS;
    else
        level = 1;

    level += wave;
    if (level > MAX_LEVEL)
        level = MAX_LEVEL;

    // EVERY WAVE IS THE SPECIES ON THE FLOOR, which is what makes it a nest
    // rather than three unrelated encounters - and what makes the shared sprite
    // honest. The thing the player walked up to is the thing that attacks, three
    // times. EventSpeciesForLevel devolves the raw roll for the level, so a
    // shallow floor gets the first stage rather than something it cannot beat.
    CreateScriptedWildMon(EventSpeciesForLevel(level), level, ITEM_NONE);
}

static void PlantFloorBerryTrees(void)
{
    u32 i;

    for (i = 0; i < DUNGEON_MAX_BERRIES; i++)
    {
        u8 id = DUNGEON_BERRY_FIRST_TREE_ID + i;

        if (i >= sBerryCount)
        {
            // Blank it rather than leaving last floor's tree standing - the
            // template is hidden, but a live tree in the array would be picked
            // up by anything that scans them.
            RemoveBerryTree(id);
            continue;
        }

        PlantBerryTree(id, ItemIdToBerryType(sBerryItems[i]),
                       BERRY_STAGE_BERRIES, FALSE);

        // ROTTEN TREES YIELD LESS, and the roll is a HASH rather than a draw
        // from the generation stream. This function runs from the template loader,
        // not from PrepareFloor, so DungeonRandom is the wrong thing to reach for
        // here twice over: the stream has already been spent on the layout, and
        // the loader does not run on every path onto a floor. DecorHash keyed on
        // the floor seed and the tree index reproduces on reload and moves nothing
        // else. Same reason the cosmetic passes use it.
        //
        // Keyed on the tree INDEX rather than its position, so two trees on one
        // floor can disagree while the same tree always agrees with itself.
        //
        // ONE TREE PER FLOOR IS ROTTEN BY CONSTRUCTION, and the rest still roll.
        // A pure per-tree roll left rot rare enough to go unmet for a long
        // stretch of a run, which - combined with rot being invisible before it
        // became a dud - meant a player could pick rotten trees repeatedly and
        // never learn the mechanic was there.
        //
        // The guaranteed one is chosen from the SAME hash family as the rolls,
        // keyed on a salt no tree index can collide with, so it reproduces on
        // reload like everything else on the floor.
        GetBerryTreeInfo(id)->berryYield =
            (RogueDungeon_IsBerryRotten(i))
                ? DUNGEON_BERRY_ROTTEN_YIELD
                : DUNGEON_BERRY_YIELD;
    }
}

// Points gMapHeader at a bg event list of our own.
//
// gMapHeader is a RAM copy assigned wholesale from ROM on every map load
// (overworld.c, in both of the dispatch branches this project already knows
// about), so this has to run on every load and undoes itself for free the
// moment the player warps anywhere else.
//
// Everything except the bg fields is copied from what the ROM header said, so
// warps still work and the object event COUNT - which the engine reads from
// here while reading the templates from the save block - is untouched.
static void ApplyDungeonEvents(void)
{
    if (gMapHeader.events == NULL)
        return;

    sDungeonEvents = *gMapHeader.events;
    sDungeonEvents.bgEvents = sHiddenItems;
    sDungeonEvents.bgEventCount = sHiddenCount;
    gMapHeader.events = &sDungeonEvents;
}

// Scatters item balls through the floor's rooms. Called from PrepareFloor, so
// it can see the rooms and runs before the map is painted.
static void PlaceItems(u16 floor)
{
    u32 count = DUNGEON_ITEM_MIN + floor / DUNGEON_ITEM_FLOORS_PER_EXTRA;
    u32 i, j;

    sItemCount = 0;

    if (sRoomCount == 0)
        return;
    if (count > DUNGEON_MAX_ITEMS)
        count = DUNGEON_MAX_ITEMS;

    for (i = 0; i < count; i++)
    {
        u32 room = PickRoomLeastUsedFrom(0);
        u8 x = sRooms[room].x + (DungeonRandom() % sRooms[room].w);
        u8 y = sRooms[room].y + (DungeonRandom() % sRooms[room].h);

        // Never on the exit: an item ball is solid, so one sitting on the
        // stairs would make the floor impossible to leave.
        if (x == sStairsX && y == sStairsY)
            continue;

        // Nor on a trainer, for the same reason in reverse - two object events
        // on one tile stack invisibly and only the top one can be interacted
        // with, so the floor would look like it had lost an item.
        for (j = 0; j < sTrainerCount; j++)
        {
            if (sTrainerX[j] == x && sTrainerY[j] == y)
                break;
        }
        if (j != sTrainerCount)
            continue;

        for (j = 0; j < sItemCount; j++)
        {
            if (sItemX[j] == x && sItemY[j] == y)
                break;
        }
        if (j != sItemCount)
            continue;

        NoteRoomUsed(room);
        sItemX[sItemCount] = x;
        sItemY[sItemCount] = y;
        RollFromTable(sLootConsumables, ARRAY_COUNT(sLootConsumables), floor,
                      &sItemIds[sItemCount], &sItemQty[sItemCount]);
        sItemCount++;
    }
}

static void PlaceTrainers(u16 floor)
{
    const struct RogueDungeonTheme *theme = ThemeForFloor(floor);
    u32 target = FloorTargetLevel(floor);
    u32 count = DUNGEON_TRAINER_MIN + floor / DUNGEON_TRAINER_FLOORS_PER_EXTRA;
    u16 trainerId, gfx, themedGfx;
    u32 j;
    u32 i;

    sTrainerCount = 0;

    if (sRoomCount < 2)
        return;
    if (count > DUNGEON_MAX_TRAINERS)
        count = DUNGEON_MAX_TRAINERS;

    for (i = 0; i < count; i++)
    {
        // From room 1: room 0 is the spawn, and a trainer there fights the player
        // before they can move.
        u32 room = PickRoomLeastUsedFrom(1);
        u8 x = sRooms[room].x + (DungeonRandom() % sRooms[room].w);
        u8 y = sRooms[room].y + (DungeonRandom() % sRooms[room].h);

        // Never on the exit, or the player cannot reach it without fighting.
        if (x == sStairsX && y == sStairsY)
            continue;

        NoteRoomUsed(room);
        sTrainerX[sTrainerCount] = x;
        sTrainerY[sTrainerCount] = y;
        // Distinct ids only. The defeat flag is derived from the trainer id, so
        // two slots sharing one would both be marked beaten by a single fight,
        // leaving a trainer standing that refuses to battle.
        // Themed is left at 0 and only written when the theme brought its own
        // trainers, so the alternation below stays the behaviour for every
        // theme that did not.
        themedGfx = 0;
        trainerId = PickTrainerForLevel(target, theme, &themedGfx);

        for (j = 0; j < sTrainerCount; j++)
        {
            if (sTrainerIds[j] == trainerId)
                break;
        }
        if (j != sTrainerCount)
            continue;

        sTrainerIds[sTrainerCount] = trainerId;

        // Four ways to answer, most specific first.
        //
        //   1. A THEMED trainer names its own sprite, because parity and level
        //      are picked independently and a diver drawn female must not open
        //      the battle as a man.
        //   2. The trainer's own CLASS, which is the answer for almost every
        //      trainer in the run and the reason a floor is no longer four
        //      hikers. It also makes the overworld figure agree with the battle
        //      pic, which is the same fault the diver table was built to fix.
        //   3. The theme's two sprites, alternating, for any class the table
        //      has no entry for.
        //   4. A hiker, which used to be step two and was therefore most of
        //      what the player ever saw.
        gfx = themedGfx;
        if (gfx == 0)
            gfx = TrainerClassGfx(trainerId);
        if (gfx == 0)
            gfx = ((sTrainerCount & 1) && theme->trainerGfxAlt) ? theme->trainerGfxAlt
                                                                : theme->trainerGfx;
        sTrainerGfx[sTrainerCount] = gfx ? gfx : OBJ_EVENT_GFX_HIKER;
        sTrainerCount++;
    }
}

// ---------------------------------------------------------------------------
// DUNGEON_GEN_ORGANIC - a cellular-automata cave.
//
// Shares the cave's 1x1 carve, its wall autotile and its cosmetic passes. The
// only thing replaced is the part that decides which blocks are open.
//
// HELD AS BITS BECAUSE THE RESULT HAS TO CROSS THE TWO-PHASE SPLIT. PrepareFloor
// runs at object-event-template load time and must touch no map memory;
// WriteFloorBlocks paints later from what it decided. So the cave cannot live
// in the map buffer, and 48x48 as bytes would be 2304 against 288 as bits.
#define CAVE_PLANE_BYTES ((DUNGEON_WIDTH * DUNGEON_HEIGHT + 7) / 8)

#define DUNGEON_CAVE_FILL_DEFAULT  48
#define DUNGEON_CAVE_ITERS_DEFAULT  4

// The floor plan, and one scratch plane.
//
// SHARED BY DUNGEON_GEN_ORGANIC AND DUNGEON_GEN_FACILITY, because a floor runs
// exactly one generator and the two never overlap in time. That is why the
// facility cost no RAM at all: the plane it needs to cross the two-phase split
// was already bought for the cave.
//
// sCaveWork is the automaton's next-generation buffer while it iterates and the
// flood fill's label plane afterwards - again, never both at once.
EWRAM_DATA static u8 sCaveBits[CAVE_PLANE_BYTES] = {0};
EWRAM_DATA static u8 sCaveWork[CAVE_PLANE_BYTES] = {0};

// Off-map reads as WALL, matching IsWallAt, so the automaton closes the cave
// against the border instead of treating the edge as open space.
static bool8 CaveWallAt(const u8 *plane, s32 x, s32 y)
{
    u32 i;

    if (x < 0 || y < 0 || x >= DUNGEON_WIDTH || y >= DUNGEON_HEIGHT)
        return TRUE;
    i = y * DUNGEON_WIDTH + x;
    return (plane[i >> 3] >> (i & 7)) & 1;
}

// The same plane read with the opposite out-of-bounds answer, for the flood
// fill's labels - off-map is "not reached", never "reached".
static bool8 CaveBitAt(const u8 *plane, s32 x, s32 y)
{
    u32 i;

    if (x < 0 || y < 0 || x >= DUNGEON_WIDTH || y >= DUNGEON_HEIGHT)
        return FALSE;
    i = y * DUNGEON_WIDTH + x;
    return (plane[i >> 3] >> (i & 7)) & 1;
}

static void CaveSet(u8 *plane, s32 x, s32 y, bool8 set)
{
    u32 i;

    if (x < 0 || y < 0 || x >= DUNGEON_WIDTH || y >= DUNGEON_HEIGHT)
        return;
    i = y * DUNGEON_WIDTH + x;
    if (set)
        plane[i >> 3] |= 1 << (i & 7);
    else
        plane[i >> 3] &= ~(1 << (i & 7));
}

static void CaveClear(u8 *plane)
{
    s32 i;

    for (i = 0; i < CAVE_PLANE_BYTES; i++)
        plane[i] = 0;
}

// Is every open block still reachable from every other?
//
// Borrows sCaveWork as the label plane. The generators have finished by the
// time this runs and the plane is idle, so a check that would otherwise need
// 288 bytes of its own costs nothing - the same argument that made the facility
// generator free.
//
// Reads walls from the MAP, not from sCaveBits: by this point the map is the
// truth, because a set piece has turned floor into collision and the plane was
// never told.
static bool8 DungeonOpenIsOnePiece(const u16 *map)
{
    s32 x, y, sx = -1, sy = -1;
    u32 open = 0, seen = 1, added;

    for (y = 0; y < DUNGEON_HEIGHT; y++)
    {
        for (x = 0; x < DUNGEON_WIDTH; x++)
        {
            if (IsWallAt(map, x, y))
                continue;
            open++;
            if (sx < 0)
            {
                sx = x;
                sy = y;
            }
        }
    }
    if (open == 0)
        return TRUE;

    CaveClear(sCaveWork);
    CaveSet(sCaveWork, sx, sy, TRUE);

    // The same queueless alternating-direction fill as CaveFloodFrom, for the
    // same reason: a BFS queue over 2304 cells would be up to 4608 bytes.
    do
    {
        added = 0;

        for (y = 0; y < DUNGEON_HEIGHT; y++)
        {
            for (x = 0; x < DUNGEON_WIDTH; x++)
            {
                if (IsWallAt(map, x, y) || CaveBitAt(sCaveWork, x, y))
                    continue;
                if (CaveBitAt(sCaveWork, x - 1, y) || CaveBitAt(sCaveWork, x, y - 1))
                {
                    CaveSet(sCaveWork, x, y, TRUE);
                    added++;
                }
            }
        }

        for (y = DUNGEON_HEIGHT - 1; y >= 0; y--)
        {
            for (x = DUNGEON_WIDTH - 1; x >= 0; x--)
            {
                if (IsWallAt(map, x, y) || CaveBitAt(sCaveWork, x, y))
                    continue;
                if (CaveBitAt(sCaveWork, x + 1, y) || CaveBitAt(sCaveWork, x, y + 1))
                {
                    CaveSet(sCaveWork, x, y, TRUE);
                    added++;
                }
            }
        }

        seen += added;
    } while (added != 0);

    return seen == open;
}

// Queueless flood fill into sCaveWork, returning how many cells IT added.
//
// A BFS queue over 2304 cells would be up to 4608 bytes and this build has
// about 22 KB of EWRAM left. Repeated relabel passes need no queue at all, and
// ALTERNATING THE SCAN DIRECTION is what makes that cheap: a forward pass only
// propagates down and right, so a passage doubling back would need one pass per
// bend, while forward-then-backward closes both directions each round.
// Measured over 300 seeds: 4.2 rounds on average, 7 at worst.
//
// Counting only newly labelled cells is what lets the caller measure every
// region using this one plane - regions are disconnected, so a later flood
// cannot reach an earlier one's labels and cannot double-count them.
static u32 CaveFloodFrom(s32 sx, s32 sy)
{
    u32 total = 1, added;
    s32 x, y;

    CaveSet(sCaveWork, sx, sy, TRUE);

    do
    {
        added = 0;

        for (y = 0; y < DUNGEON_HEIGHT; y++)
        {
            for (x = 0; x < DUNGEON_WIDTH; x++)
            {
                if (CaveWallAt(sCaveBits, x, y) || CaveBitAt(sCaveWork, x, y))
                    continue;
                if (CaveBitAt(sCaveWork, x - 1, y) || CaveBitAt(sCaveWork, x, y - 1))
                {
                    CaveSet(sCaveWork, x, y, TRUE);
                    added++;
                }
            }
        }

        for (y = DUNGEON_HEIGHT - 1; y >= 0; y--)
        {
            for (x = DUNGEON_WIDTH - 1; x >= 0; x--)
            {
                if (CaveWallAt(sCaveBits, x, y) || CaveBitAt(sCaveWork, x, y))
                    continue;
                if (CaveBitAt(sCaveWork, x + 1, y) || CaveBitAt(sCaveWork, x, y + 1))
                {
                    CaveSet(sCaveWork, x, y, TRUE);
                    added++;
                }
            }
        }

        total += added;
    } while (added != 0);

    return total;
}

static void GenerateOrganicCave(const struct RogueDungeonTheme *theme)
{
    u32 fill = theme->caveFill ? theme->caveFill : DUNGEON_CAVE_FILL_DEFAULT;
    u32 iters = theme->caveIters ? theme->caveIters : DUNGEON_CAVE_ITERS_DEFAULT;
    u32 pass, best;
    s32 x, y, i, bestX, bestY;

    // Seed the whole plane as wall, then open the interior at random. The
    // border is never touched, so the cave cannot open onto the map edge.
    for (i = 0; i < CAVE_PLANE_BYTES; i++)
        sCaveBits[i] = 0xFF;
    for (y = 1; y < DUNGEON_HEIGHT - 1; y++)
        for (x = 1; x < DUNGEON_WIDTH - 1; x++)
            CaveSet(sCaveBits, x, y, (DungeonRandom() % 100) < fill);

    // The 4-5 rule. THE TWO THRESHOLDS ARE NOT THE SAME NUMBER: a wall stays a
    // wall on four wall neighbours, but a floor only becomes one on five. Using
    // a single threshold of 5 for both erodes walls badly - measured at 84%
    // walkable from a 45% seed, which is a field with rocks in it.
    for (pass = 0; pass < iters; pass++)
    {
        for (i = 0; i < CAVE_PLANE_BYTES; i++)
            sCaveWork[i] = sCaveBits[i];

        for (y = 1; y < DUNGEON_HEIGHT - 1; y++)
        {
            for (x = 1; x < DUNGEON_WIDTH - 1; x++)
            {
                s32 dx, dy, n = 0;

                for (dy = -1; dy <= 1; dy++)
                    for (dx = -1; dx <= 1; dx++)
                        if ((dx != 0 || dy != 0)
                            && CaveWallAt(sCaveBits, x + dx, y + dy))
                            n++;

                CaveSet(sCaveWork, x, y,
                        CaveWallAt(sCaveBits, x, y) ? (n >= 4) : (n >= 5));
            }
        }

        for (i = 0; i < CAVE_PLANE_BYTES; i++)
            sCaveBits[i] = sCaveWork[i];
    }

    // CONNECTIVITY IS NOT A PROPERTY A CELLULAR AUTOMATON HAS. It has to be
    // imposed, and keeping the largest region is not optional either: measured
    // over 300 seeds the biggest region holds 78.6% of the open cells on
    // average but only 27.6% at worst, so flooding from an arbitrary start
    // would sometimes keep a quarter of the cave and wall off the rest.
    //
    // Two passes so that one label plane is enough. The first measures every
    // region and remembers only where the best one STARTED - four bytes, not a
    // plane per candidate, and a floor carries up to fifteen regions.
    CaveClear(sCaveWork);
    best = 0;
    bestX = -1;
    bestY = -1;
    for (y = 0; y < DUNGEON_HEIGHT; y++)
    {
        for (x = 0; x < DUNGEON_WIDTH; x++)
        {
            u32 n;

            if (CaveWallAt(sCaveBits, x, y) || CaveBitAt(sCaveWork, x, y))
                continue;
            n = CaveFloodFrom(x, y);
            if (n > best)
            {
                best = n;
                bestX = x;
                bestY = y;
            }
        }
    }

    // A fill so dense it opened nothing. Leave the plane solid; FitPlaneRooms
    // then finds no rooms and PrepareFloor's existing sRoomCount == 0 guards
    // take over, exactly as they do for a room sampler that placed nothing.
    if (bestX < 0)
        return;

    CaveClear(sCaveWork);
    CaveFloodFrom(bestX, bestY);
    for (y = 0; y < DUNGEON_HEIGHT; y++)
        for (x = 0; x < DUNGEON_WIDTH; x++)
            if (!CaveBitAt(sCaveWork, x, y))
                CaveSet(sCaveBits, x, y, TRUE);
}

// Neither the cave nor the trails have rooms, but everything downstream is
// written against them:
// PlaceTrainers, PlaceItems, PlaceBerryTrees, PlaceRocks, PlaceHiddenItems and
// the grass pass all pick a room and then a point inside it, and every one of
// them relies on the whole room being floor. The spawn and the stairs are
// derived from rooms too.
//
// So rather than teach six callers a second world model, the cave synthesises
// the interface they already speak - small squares that are entirely open -
// and NOTHING downstream changes. Measured over 120 seeds this places 9.8 of
// the ten it asks for on a cave, and 10.0 on a trail floor.
#define CAVE_ROOM_SIZE 3

static void FitPlaneRooms(void)
{
    u32 cap = DUNGEON_ROOMS_DEFAULT;
    s32 attempt, i, x, y, dx, dy;

    if (cap > DUNGEON_MAX_ROOMS)
        cap = DUNGEON_MAX_ROOMS;

    for (attempt = 0; attempt < 400 && sRoomCount < (s32)cap; attempt++)
    {
        struct DungeonRoom room;
        bool8 clear = TRUE;

        x = 1 + (DungeonRandom() % (DUNGEON_WIDTH - CAVE_ROOM_SIZE - 2));
        y = 1 + (DungeonRandom() % (DUNGEON_HEIGHT - CAVE_ROOM_SIZE - 2));

        for (dy = 0; dy < CAVE_ROOM_SIZE && clear; dy++)
            for (dx = 0; dx < CAVE_ROOM_SIZE; dx++)
                if (CaveWallAt(sCaveBits, x + dx, y + dy))
                {
                    clear = FALSE;
                    break;
                }

        if (!clear)
            continue;

        room.x = x;
        room.y = y;
        room.w = CAVE_ROOM_SIZE;
        room.h = CAVE_ROOM_SIZE;

        for (i = 0; i < sRoomCount; i++)
        {
            if (RoomsOverlap(&room, &sRooms[i]))
            {
                clear = FALSE;
                break;
            }
        }

        if (clear)
            sRooms[sRoomCount++] = room;
    }
}

// ---------------------------------------------------------------------------
// DUNGEON_GEN_FACILITY - a floorplan.
//
// Partitions the interior into rectangles that TILE IT EXACTLY, turns each into
// a room by giving back a wall on two sides, and joins neighbours with doors
// punched through the shared wall. Paints through the same plane the organic
// cave uses, so it costs no RAM of its own.

#define FACILITY_LEAVES_DEFAULT    14
#define FACILITY_MINLEAF_DEFAULT    8
#define FACILITY_EXTRA_DOORS_DEFAULT 3
#define FACILITY_DOOR_WIDTH         2

// A vertical partition is TWO blocks thick and a horizontal one is ONE, and
// that asymmetry is read off New Mauville's wall table rather than chosen:
//
//   - one block thick with floor above and below is WALL_SLIVER_HORZ, which
//     that theme sets to the band. Its own comment says the band serves both
//     faces because a flat partition looks the same from either side. Correct.
//   - one block thick with floor left and right would be WALL_SLIVER_VERT,
//     which is the PILLAR, and a whole column of pillars reads as a colonnade
//     rather than a wall. Two blocks instead gives the left column floor only
//     to its west (WALL_INTERIOR_LEFT) and the right column floor only to its
//     east (WALL_INTERIOR_RIGHT) - a wall seen properly from both sides.
//
// Measured over 200 floors, this puts 12% of walls on the band and 40% on the
// west/east faces, and drops pillars and isolated blocks to ZERO. Before the
// asymmetry it was 33-37% slivers with pillars everywhere.
#define FACILITY_VTHICK 2

// Where two rooms meet across a partition, if they do. `axis` is 0 when the
// partition is vertical (the rooms are side by side) and 1 when horizontal.
// `lo`/`hi` bound the door slots along it.
static bool8 FacilitySharedWall(const struct DungeonRoom *a,
                                const struct DungeonRoom *b,
                                s32 *axis, s32 *coord, s32 *thick,
                                s32 *lo, s32 *hi)
{
    s32 t;

    for (t = 1; t <= FACILITY_VTHICK; t++)
    {
        if (a->x + a->w + t == b->x || b->x + b->w + t == a->x)
        {
            s32 l = max(a->y, b->y);
            s32 hgh = min(a->y + a->h, b->y + b->h);

            if (hgh - l < 2)
                continue;
            *axis = 0;
            *coord = (a->x + a->w + t == b->x) ? a->x + a->w : b->x + b->w;
            *thick = t;
            *lo = l;
            *hi = hgh;
            return TRUE;
        }
        if (a->y + a->h + t == b->y || b->y + b->h + t == a->y)
        {
            s32 l = max(a->x, b->x);
            s32 hgh = min(a->x + a->w, b->x + b->w);

            if (hgh - l < 2)
                continue;
            *axis = 1;
            *coord = (a->y + a->h + t == b->y) ? a->y + a->h : b->y + b->h;
            *thick = t;
            *lo = l;
            *hi = hgh;
            return TRUE;
        }
    }
    return FALSE;
}

// Opens every cell across the partition at `count` consecutive slots. Opening
// only part of the thickness would be a doorway into the inside of a wall - it
// looks like a door and leads nowhere.
static void FacilityPunchDoor(s32 axis, s32 coord, s32 thick, s32 lo, s32 hi)
{
    s32 slots = hi - lo;
    s32 width = FACILITY_DOOR_WIDTH;
    s32 start, i, k;

    if (slots < width)
        width = slots;
    if (width <= 0)
        return;

    start = lo + (DungeonRandom() % (slots - width + 1));
    for (i = 0; i < width; i++)
        for (k = 0; k < thick; k++)
            if (axis == 0)
                CaveSet(sCaveBits, coord + k, start + i, FALSE);
            else
                CaveSet(sCaveBits, start + i, coord + k, FALSE);
}

static void GenerateFacilityFloor(const struct RogueDungeonTheme *theme)
{
    u32 leaves = theme->facilityLeaves ? theme->facilityLeaves
                                       : FACILITY_LEAVES_DEFAULT;
    u32 minLeaf = theme->facilityMinLeaf ? theme->facilityMinLeaf
                                         : FACILITY_MINLEAF_DEFAULT;
    u32 extra = theme->facilityExtraDoors ? theme->facilityExtraDoors
                                          : FACILITY_EXTRA_DOORS_DEFAULT;
    bool8 joined[DUNGEON_MAX_ROOMS];
    s32 i, j, x, y, count;

    if (leaves > DUNGEON_MAX_ROOMS)
        leaves = DUNGEON_MAX_ROOMS;

    // Partition the INTERIOR, with no margin, so the rects tile it exactly and
    // every room ends up touching its neighbours. Built directly in sRooms -
    // rects and rooms are the same four bytes, so no second array is needed.
    sRooms[0].x = 1;
    sRooms[0].y = 1;
    sRooms[0].w = DUNGEON_WIDTH - 2;
    sRooms[0].h = DUNGEON_HEIGHT - 2;
    count = 1;

    while (count < (s32)leaves)
    {
        s32 best = 0, bestArea = 0, cut;
        bool8 horiz;

        // Always split the largest, which keeps the partition balanced without
        // needing a recursion stack to walk a tree.
        for (i = 0; i < count; i++)
        {
            s32 area = sRooms[i].w * sRooms[i].h;

            if (area > bestArea)
            {
                bestArea = area;
                best = i;
            }
        }

        // Cut across the long axis, so rooms tend to squareness. Near-square
        // partitions pick a direction at random instead, which is what stops
        // every floor subdividing in the same order.
        if (sRooms[best].w > sRooms[best].h + 2)
            horiz = FALSE;
        else if (sRooms[best].h > sRooms[best].w + 2)
            horiz = TRUE;
        else
            horiz = (DungeonRandom() & 1) != 0;

        if (horiz)
        {
            if (sRooms[best].h < (s32)minLeaf * 2)
                break;
            cut = minLeaf + (DungeonRandom() % (sRooms[best].h - minLeaf * 2 + 1));
            sRooms[count].x = sRooms[best].x;
            sRooms[count].y = sRooms[best].y + cut;
            sRooms[count].w = sRooms[best].w;
            sRooms[count].h = sRooms[best].h - cut;
            sRooms[best].h = cut;
        }
        else
        {
            if (sRooms[best].w < (s32)minLeaf * 2)
                break;
            cut = minLeaf + (DungeonRandom() % (sRooms[best].w - minLeaf * 2 + 1));
            sRooms[count].x = sRooms[best].x + cut;
            sRooms[count].y = sRooms[best].y;
            sRooms[count].w = sRooms[best].w - cut;
            sRooms[count].h = sRooms[best].h;
            sRooms[best].w = cut;
        }
        count++;
    }

    // Give back the partition walls: FACILITY_VTHICK columns on the right and
    // one row at the bottom. What is left is the room, and what was given back
    // is the wall it shares with the neighbour on that side.
    sRoomCount = 0;
    for (i = 0; i < count; i++)
    {
        if (sRooms[i].w - FACILITY_VTHICK < 3 || sRooms[i].h - 1 < 3)
            continue;
        sRooms[sRoomCount].x = sRooms[i].x;
        sRooms[sRoomCount].y = sRooms[i].y;
        sRooms[sRoomCount].w = sRooms[i].w - FACILITY_VTHICK;
        sRooms[sRoomCount].h = sRooms[i].h - 1;
        sRoomCount++;
    }

    if (sRoomCount == 0)
        return;

    // Everything solid, then open the rooms.
    for (i = 0; i < CAVE_PLANE_BYTES; i++)
        sCaveBits[i] = 0xFF;
    for (i = 0; i < sRoomCount; i++)
        for (y = 0; y < sRooms[i].h; y++)
            for (x = 0; x < sRooms[i].w; x++)
                CaveSet(sCaveBits, sRooms[i].x + x, sRooms[i].y + y, FALSE);

    // A spanning tree over the adjacency graph, so the floor connects through
    // DOORS rather than through corridors bored across the map. Each round
    // takes a random edge with exactly one end already on the network.
    for (i = 0; i < DUNGEON_MAX_ROOMS; i++)
        joined[i] = FALSE;
    joined[0] = TRUE;

    for (;;)
    {
        s32 cand = 0, pick, axis, coord, thick, lo, hi;

        for (i = 0; i < sRoomCount; i++)
            for (j = 0; j < sRoomCount; j++)
                if (joined[i] && !joined[j]
                    && FacilitySharedWall(&sRooms[i], &sRooms[j],
                                          &axis, &coord, &thick, &lo, &hi))
                    cand++;

        if (cand == 0)
            break;

        pick = DungeonRandom() % cand;
        for (i = 0; i < sRoomCount; i++)
        {
            for (j = 0; j < sRoomCount; j++)
            {
                if (!joined[i] || joined[j]
                    || !FacilitySharedWall(&sRooms[i], &sRooms[j],
                                           &axis, &coord, &thick, &lo, &hi))
                    continue;
                if (pick-- != 0)
                    continue;
                FacilityPunchDoor(axis, coord, thick, lo, hi);
                joined[j] = TRUE;
                i = sRoomCount;   // break both loops
                break;
            }
        }
    }

    // Extra doors close loops. Unlike the tree above these are not needed for
    // correctness, so a pair that shares no wall is simply skipped.
    while (extra--)
    {
        s32 axis, coord, thick, lo, hi;

        i = DungeonRandom() % sRoomCount;
        j = DungeonRandom() % sRoomCount;
        if (i == j)
            continue;
        if (FacilitySharedWall(&sRooms[i], &sRooms[j],
                               &axis, &coord, &thick, &lo, &hi))
            FacilityPunchDoor(axis, coord, thick, lo, hi);
    }

    // A room the spanning tree could not reach - possible if its only shared
    // wall was too short to hold a door - is walled off and dropped, so nothing
    // is ever placed somewhere the player cannot go.
    CaveClear(sCaveWork);
    CaveFloodFrom(sRooms[0].x + sRooms[0].w / 2, sRooms[0].y + sRooms[0].h / 2);
    for (i = sRoomCount - 1; i >= 0; i--)
    {
        if (CaveBitAt(sCaveWork, sRooms[i].x, sRooms[i].y))
            continue;
        for (y = 0; y < sRooms[i].h; y++)
            for (x = 0; x < sRooms[i].w; x++)
                CaveSet(sCaveBits, sRooms[i].x + x, sRooms[i].y + y, TRUE);
        for (j = i; j < sRoomCount - 1; j++)
            sRooms[j] = sRooms[j + 1];
        sRoomCount--;
    }
}

// ---------------------------------------------------------------------------
// DUNGEON_GEN_TRAILS - irregular clearings joined by meandering trails.
//
// Paints through sCaveBits like the other two plane generators, so it costs no
// RAM of its own.

#define TRAIL_CLEARINGS_DEFAULT 12
#define TRAIL_LOBE_DEFAULT       5
#define TRAIL_WANDER_DEFAULT    35
#define TRAIL_LOBES_DEFAULT      4
#define TRAIL_MARGIN             3

static void TrailCarve(s32 x, s32 y)
{
    if (x >= TRAIL_MARGIN && x < DUNGEON_WIDTH - TRAIL_MARGIN
     && y >= TRAIL_MARGIN && y < DUNGEON_HEIGHT - TRAIL_MARGIN)
        CaveSet(sCaveBits, x, y, FALSE);
}

// A walk that drifts but always finishes. The drift is what makes it read as a
// trail; the straight finish is what makes the floor connected BY CONSTRUCTION,
// so this generator needs no repair pass and no largest-region flood the way
// the cellular automaton does.
//
// THE STRAIGHT TAIL IS DEAD CODE AT THE DEFAULT WANDER AND ESSENTIAL ABOVE IT.
// Measured over 4,788 walks, the budget is exhausted 0% of the time at wander
// 50, 0.40% at 60, 7.73% at 70 and 42.61% at 80. So at the shipped 35 the drift
// always arrives on its own and deleting the tail changes nothing - but
// trailWander is a field a theme may set, and past about 60 the tail is the
// only thing connecting the floor. Do not remove it because it never fires;
// check_trail_floor.py carries a wander-80 stress row precisely so that the
// assertion guarding it is not vacuous.
static void TrailWalk(s32 x, s32 y, s32 tx, s32 ty, u32 wander)
{
    s32 budget = (abs(tx - x) + abs(ty - y)) * 4 + 40;

    while ((x != tx || y != ty) && budget-- > 0)
    {
        TrailCarve(x, y);

        if ((DungeonRandom() % 100) < wander)
        {
            s32 d = DungeonRandom() % 4;
            s32 nx = x + (d == 0 ? 1 : d == 1 ? -1 : 0);
            s32 ny = y + (d == 2 ? 1 : d == 3 ? -1 : 0);

            if (nx >= TRAIL_MARGIN && nx < DUNGEON_WIDTH - TRAIL_MARGIN
             && ny >= TRAIL_MARGIN && ny < DUNGEON_HEIGHT - TRAIL_MARGIN)
            {
                x = nx;
                y = ny;
            }
            continue;
        }

        if (abs(tx - x) > abs(ty - y))
            x += (tx > x) ? 1 : -1;
        else if (ty != y)
            y += (ty > y) ? 1 : -1;
        else
            x += (tx > x) ? 1 : -1;
    }

    while (x != tx)
    {
        TrailCarve(x, y);
        x += (tx > x) ? 1 : -1;
    }
    while (y != ty)
    {
        TrailCarve(x, y);
        y += (ty > y) ? 1 : -1;
    }
    TrailCarve(x, y);
}

static void GenerateTrailFloor(const struct RogueDungeonTheme *theme)
{
    u32 clearings = theme->trailClearings ? theme->trailClearings
                                          : TRAIL_CLEARINGS_DEFAULT;
    u32 lobe = theme->trailLobe ? theme->trailLobe : TRAIL_LOBE_DEFAULT;
    u32 wander = theme->trailWander ? theme->trailWander : TRAIL_WANDER_DEFAULT;
    u32 lobes = theme->trailLobes ? theme->trailLobes : TRAIL_LOBES_DEFAULT;
    u32 drift = theme->trailLobeDrift;
    u8 cx[DUNGEON_MAX_ROOMS], cy[DUNGEON_MAX_ROOMS];
    u8 order[DUNGEON_MAX_ROOMS];
    bool8 used[DUNGEON_MAX_ROOMS];
    u32 cols, rows, cw, ch, n;
    s32 i, j, cur;

    if (clearings > DUNGEON_MAX_ROOMS)
        clearings = DUNGEON_MAX_ROOMS;

    for (i = 0; i < CAVE_PLANE_BYTES; i++)
        sCaveBits[i] = 0xFF;

    // Jittered grid. Shuffling which cells get used, rather than taking the
    // first N, is what stops every floor filling the same corner first.
    cols = (clearings <= 9) ? 3 : 4;
    rows = (clearings + cols - 1) / cols;
    cw = (DUNGEON_WIDTH - 2 * TRAIL_MARGIN - 8) / cols;
    ch = (DUNGEON_HEIGHT - 2 * TRAIL_MARGIN - 8) / rows;
    if (cw == 0)
        cw = 1;
    if (ch == 0)
        ch = 1;

    n = cols * rows;
    if (n > DUNGEON_MAX_ROOMS)
        n = DUNGEON_MAX_ROOMS;
    for (i = 0; i < (s32)n; i++)
        order[i] = i;
    for (i = n - 1; i > 0; i--)
    {
        u32 k = DungeonRandom() % (i + 1);
        u8 t = order[i];

        order[i] = order[k];
        order[k] = t;
    }
    if (clearings > n)
        clearings = n;

    for (i = 0; i < (s32)clearings; i++)
    {
        u32 gx = order[i] % cols;
        u32 gy = order[i] / cols;
        s32 px = TRAIL_MARGIN + 4 + gx * cw + (DungeonRandom() % cw);
        s32 py = TRAIL_MARGIN + 4 + gy * ch + (DungeonRandom() % ch);

        cx[i] = px;
        cy[i] = py;

        // A clump of overlapping rectangles, so the outline is irregular. One
        // rectangle is exactly the silhouette this generator exists to avoid.
        //
        // EVERY LOBE MUST REACH THE CLEARING CENTRE, one way or the other, and
        // that is a correctness requirement rather than a shape preference. The
        // trail into a clearing arrives at its centre, so a lobe that cannot be
        // reached from the centre is unreachable from the whole floor.
        //
        // ANCHORED (drift 0) does it by placing the offset in
        // [px - lw + 1, px], so the lobe contains the centre arithmetically.
        //
        // UNANCHORED (drift n) lets the lobe wander +/- n and then carves a STUB
        // from the lobe's own centre back to the clearing centre. Same
        // guarantee, ragged outline. The old code did the wandering WITHOUT the
        // stub and that is the bug check_trail_floor.py found on seed 173 - nine
        // of 625 open blocks unreachable, because a lobe had drifted clear.
        for (j = 0; j < (s32)lobes; j++)
        {
            s32 lw = 3 + (DungeonRandom() % lobe);
            s32 lh = 3 + (DungeonRandom() % lobe);
            s32 ox, oy, dx, dy;

            if (drift != 0)
            {
                ox = px - lw / 2 + (DungeonRandom() % (drift * 2 + 1)) - (s32)drift;
                oy = py - lh / 2 + (DungeonRandom() % (drift * 2 + 1)) - (s32)drift;
            }
            else
            {
                ox = px - (s32)(DungeonRandom() % lw);
                oy = py - (s32)(DungeonRandom() % lh);
            }

            for (dy = 0; dy < lh; dy++)
                for (dx = 0; dx < lw; dx++)
                    TrailCarve(ox + dx, oy + dy);

            // The stub. Straight, from a point guaranteed inside the lobe to the
            // clearing centre - so the lobe is joined whatever it did.
            if (drift != 0)
            {
                s32 lx = ox + lw / 2, ly = oy + lh / 2;

                while (lx != px)
                {
                    TrailCarve(lx, ly);
                    lx += (px > lx) ? 1 : -1;
                }
                while (ly != py)
                {
                    TrailCarve(lx, ly);
                    ly += (py > ly) ? 1 : -1;
                }
                TrailCarve(lx, ly);
            }
        }
    }

    // Chain nearest-unvisited, so a trail joins neighbouring clearings rather
    // than striking across the map to whichever was sampled next.
    for (i = 0; i < (s32)clearings; i++)
        used[i] = FALSE;
    used[0] = TRUE;
    cur = 0;
    for (;;)
    {
        s32 best = -1, bestD = 0;

        for (i = 0; i < (s32)clearings; i++)
        {
            s32 d;

            if (used[i])
                continue;
            d = abs((s32)cx[i] - (s32)cx[cur]) + abs((s32)cy[i] - (s32)cy[cur]);
            if (best < 0 || d < bestD)
            {
                best = i;
                bestD = d;
            }
        }
        if (best < 0)
            break;

        TrailWalk(cx[cur], cy[cur], cx[best], cy[best], wander);
        used[best] = TRUE;
        cur = best;
    }
}

// Everything a floor is, derived from its seed. Touches no map memory, so it
// can run at object-event-template load time - which happens before the map is
// generated, and is where trainers have to be placed.
static void PrepareFloor(u16 seed)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    const struct RogueDungeonTheme *theme = ThemeForFloor(floor);
    u32 unit = (theme->generator == DUNGEON_GEN_WOODS) ? DUNGEON_WOODS_CELL : 1;
    u32 gridW = DUNGEON_WIDTH / unit;
    u32 gridH = DUNGEON_HEIGHT / unit;
    // RETUNED WITH THE CELL, and not cosmetically. These are in CELLS, so at
    // a 3-cell unit the old 3-5 means 9-15 metatiles - 2.25x the area on a
    // floor the same size. Measured over 400 floors: room count fell from a
    // solid 10 to 7.1 and swung 4-10, and the room-and-corridor structure
    // collapsed into one blob. 2-3 puts it back at 10 every time, with 35.4%
    // walkable against the 35.5% this shipped with.
    //
    // `unit != 1` rather than `unit == 2`, so the test asks whether the theme
    // is stamped rather than what its cell size happens to be.
    u32 rmin = (unit != 1) ? 2 : DUNGEON_ROOM_MIN;
    u32 rmax = (unit != 1) ? 3 : DUNGEON_ROOM_MAX;
    u32 roomCap = theme->roomCount ? theme->roomCount : DUNGEON_ROOMS_DEFAULT;
    u32 attempts;
    s32 i, attempt;

    // A theme may ask to be more open than the cave. Woods keeps its own
    // half-resolution sizing, which is already in cells rather than blocks.
    if (unit == 1 && theme->roomMin != 0 && theme->roomMax != 0)
    {
        rmin = theme->roomMin;
        rmax = theme->roomMax;
    }
    if (roomCap > DUNGEON_MAX_ROOMS)
        roomCap = DUNGEON_MAX_ROOMS;
    // Bigger rooms are rejected more often, so the budget scales with the cap
    // rather than staying at the cave's 64.
    attempts = roomCap * 8 + 32;

    // The themed layout is a tileset donor. gMapHeader is a RAM copy and
    // CopyMapTilesetsToVram reads mapLayout, and this runs before the map view
    // initialises, so this is what swaps the dungeon between cave and woods.
    // mapLayoutId is deliberately left alone - every dispatch keys on it.
    gMapHeader.mapLayout = GetMapLayout(theme->layoutId);

    // And the name on the banner, for the same reason and by the same trick.
    // The section is a property of the MAP in map.json, but the themes share a
    // map, so declaring it there named every non-override floor GRANITE CAVE -
    // the woods, New Mauville, Fiery Path, Mirage Tower, the jungle and Ever
    // Grande included. Patching the RAM header per floor is what lets fourteen
    // themes have fourteen names without fourteen maps.
    gMapHeader.regionMapSectionId = theme->mapSecId;

    SeedDungeonRng(seed);

    // EVERY PLACEMENT COUNTER, ZEROED BEFORE ANY PATH BRANCHES.
    //
    // The arena path below returns without ever reaching the placers, so a
    // counter left standing here keeps the PREVIOUS floor's value - and the
    // object template builder reads it. That shipped: boss and mini boss floors
    // spawned the last floor's event NPC, item balls, berry trees and mining
    // rocks, at the last floor's coordinates, inside a room that no longer had
    // anything at those coordinates. Found by playing.
    //
    // Zeroed here rather than in PrepareArenaFloor so the invariant is "a floor
    // starts with nothing placed" regardless of which generator runs, which is
    // one rule instead of one per path.
    sRoomCount = 0;
    sTrainerCount = 0;
    sItemCount = 0;
    sBerryCount = 0;
    sRockCount = 0;
    sEventCount = 0;

    if (RogueDungeon_IsBossFloor(floor))
    {
        PrepareArenaFloor(floor);
        BuildWildEncounterTable(floor);
        sFloorPrepared = TRUE;
        return;
    }

    // The organic cave decides its own open blocks and then synthesises the
    // room list the rest of this function expects, so the sampler below has
    // nothing left to do. Zeroing the budget is how it is switched off - the
    // loop is left reachable rather than wrapped in an else, because every line
    // after it (stairs, spawn, grass, trainers, items, berries, rocks, hidden
    // items) is shared and must keep running for both generators.
    if (theme->generator == DUNGEON_GEN_ORGANIC)
    {
        GenerateOrganicCave(theme);
        FitPlaneRooms();
        attempts = 0;
    }
    else if (theme->generator == DUNGEON_GEN_TRAILS)
    {
        GenerateTrailFloor(theme);
        FitPlaneRooms();
        attempts = 0;
    }
    else if (theme->generator == DUNGEON_GEN_FACILITY)
    {
        // Builds its rooms by subdividing rather than by sampling, so it fills
        // sRooms itself and the sampler below has nothing to do either.
        GenerateFacilityFloor(theme);
        attempts = 0;
    }

    // Rejection-sample non-overlapping rooms. A fixed attempt budget keeps this
    // bounded; falling short of DUNGEON_MAX_ROOMS is fine.
    for (attempt = 0; attempt < (s32)attempts && sRoomCount < roomCap; attempt++)
    {
        struct DungeonRoom room;
        bool8 clear = TRUE;

        // Woods generates in whole 2x2 cells, so rooms come out even-aligned
        // and a tree stamp is never split. Everything downstream still works in
        // metatile space.
        room.w = (rmin + (DungeonRandom() % (rmax - rmin + 1))) * unit;
        room.h = (rmin + (DungeonRandom() % (rmax - rmin + 1))) * unit;
        room.x = (1 + (DungeonRandom() % (gridW - room.w / unit - 2))) * unit;
        room.y = (1 + (DungeonRandom() % (gridH - room.h / unit - 2))) * unit;

        for (i = 0; i < sRoomCount; i++)
        {
            if (RoomsOverlap(&room, &sRooms[i]))
            {
                clear = FALSE;
                break;
            }
        }

        if (clear)
            sRooms[sRoomCount++] = room;
    }

    // Exactly one exit per floor, in a room the player does not start in, so
    // reaching it means traversing the floor. Rooms are always connected.
    //
    // Each draw is its own statement: as function arguments the order of
    // evaluation would be unspecified, and the layout would depend on it.
    if (sRoomCount != 0)
    {
        u8 room = (sRoomCount > 1) ? 1 + (DungeonRandom() % (sRoomCount - 1)) : 0;

        sStairsMetatile = (DungeonRandom() & 1) ? theme->stairsDown : theme->stairsUp;
        sStairsX = sRooms[room].x + (DungeonRandom() % sRooms[room].w);
        sStairsY = sRooms[room].y + (DungeonRandom() % sRooms[room].h);
    }

    if (sRoomCount != 0)
    {
        sSpawnX = sRooms[0].x + sRooms[0].w / 2;
        sSpawnY = sRooms[0].y + sRooms[0].h / 2;
    }

    // Grass blobs, only for themes that have grass. Placed inside rooms so they
    // never land in a tree.
    sGrassPatchCount = 0;
    if (theme->tallGrass != 0)
    {
        for (i = 0; i < sRoomCount && sGrassPatchCount < DUNGEON_MAX_GRASS_PATCHES; i++)
        {
            u32 patches = DungeonRandom() % 3;   // 0-2 per clearing

            while (patches-- && sGrassPatchCount < DUNGEON_MAX_GRASS_PATCHES)
            {
                // The draw happens whether or not the theme has long grass, so
                // that a theme losing it does not shift the RNG stream and
                // silently relay out every floor. && would short-circuit.
                bool8 wantLong = (DungeonRandom() % 4 == 0);

                sGrassPatchLong[sGrassPatchCount] =
                    (theme->longGrass != 0) && wantLong;
                sGrassPatchX[sGrassPatchCount] =
                    (sRooms[i].x + (DungeonRandom() % sRooms[i].w)) / 2;
                sGrassPatchY[sGrassPatchCount] =
                    (sRooms[i].y + (DungeonRandom() % sRooms[i].h)) / 2;
                sGrassPatchRadius[sGrassPatchCount] = 1 + (DungeonRandom() % 2);
                sGrassPatchCount++;
            }
        }
    }

    // Peaks are per floor, so they have to be dropped as one is built or the
    // worst moment of the whole run would be reported for every floor in it.
    RogueDungeon_Debug_ResetObjectCensus();

    // Room occupancy is per floor too, and it drives WHERE everything below
    // goes - carrying last floor's counts over would push this floor's objects
    // away from rooms that are empty on it.
    for (i = 0; i < DUNGEON_MAX_ROOMS; i++)
        sRoomUse[i] = 0;

    BuildWildEncounterTable(floor);
    PlaceTrainers(floor);
    // After the trainers, because it refuses to stack a ball on one of them.
    PlaceItems(floor);
    PlaceBerryTrees(floor);
    PlaceRocks(floor);
    PlaceEvents(floor);
    // Last, so it can avoid every solid thing already placed - a hidden item
    // under a ball or a tree is one the player can never be told about.
    PlaceHiddenItems(floor);
    sFloorPrepared = TRUE;
}

// Stamps one cell. Woods trees are whole DUNGEON_WOODS_CELL blocks on aligned
// coordinates, so the generator works in whole cells and never needs an
// autotile pass.
static void StampCell(u16 *map, s32 cx, s32 cy, const struct RogueDungeonTheme *theme,
                      bool8 open, u16 floorMetatile, bool8 openBelow,
                      bool8 treeBelow)
{
    s32 x = cx * DUNGEON_WOODS_CELL, y = cy * DUNGEON_WOODS_CELL;

    if (open)
    {
        u16 lower = floorMetatile;
        s32 r, c;

        // THE WHOLE CELL, FIRST. The two special cases below are 2-wide - they
        // predate the cell being 3 - and would leave the third column of every
        // cell they touched unwritten, which is a hole in the map rather than a
        // cosmetic slip. Painting plain floor across the cell up front means the
        // worst either can now do is fail to decorate.
        //
        // Neither fires today: both are guarded on theme fields the only
        // stamped theme sets to 0 (aboveTree* and longGrass). Widen them to
        // DUNGEON_WOODS_CELL before giving a stamped theme either.
        for (r = 0; r < DUNGEON_WOODS_CELL; r++)
            for (c = 0; c < DUNGEON_WOODS_CELL; c++)
                SetBlock(map, x + c, y + r,
                         MakeBlock(floorMetatile, 0, theme->elevationFloor));

        // A tree's crown pokes up into the block above its canopy. Which
        // variant depends on what that block already is, and long grass has no
        // vanilla variant, so it falls through and is left alone.
        if (treeBelow)
        {
            u16 leftTop = 0, rightTop = 0;

            if (floorMetatile == theme->tallGrass && theme->tallGrass != 0)
            {
                leftTop = theme->aboveTreeGrassL;
                rightTop = theme->aboveTreeGrassR;
            }
            else if (floorMetatile == theme->floor)
            {
                leftTop = theme->aboveTreeFloorL;
                rightTop = theme->aboveTreeFloorR;
            }

            if (leftTop != 0 && rightTop != 0)
            {
                SetBlock(map, x,     y,     MakeBlock(floorMetatile, 0, theme->elevationFloor));
                SetBlock(map, x + 1, y,     MakeBlock(floorMetatile, 0, theme->elevationFloor));
                SetBlock(map, x,     y + 1, MakeBlock(leftTop, 0, theme->elevationFloor));
                SetBlock(map, x + 1, y + 1, MakeBlock(rightTop, 0, theme->elevationFloor));
                return;
            }
        }

        // Long grass needs its base row where it meets open ground, or the
        // blades are cut off flat.
        if (floorMetatile == theme->longGrass && theme->longGrass != 0 && openBelow)
        {
            SetBlock(map, x,     y,     MakeBlock(floorMetatile, 0, theme->elevationFloor));
            SetBlock(map, x + 1, y,     MakeBlock(floorMetatile, 0, theme->elevationFloor));
            SetBlock(map, x,     y + 1, MakeBlock(theme->longGrassBaseL, 0, theme->elevationFloor));
            SetBlock(map, x + 1, y + 1, MakeBlock(theme->longGrassBaseR, 0, theme->elevationFloor));
            return;
        }

        // `lower` differs from floorMetatile only for the cases handled above,
        // so the up-front fill has already painted this; kept as the explicit
        // bottom row so the intent survives if those cases change.
        for (c = 0; c < DUNGEON_WOODS_CELL; c++)
            SetBlock(map, x + c, y + DUNGEON_WOODS_CELL - 1,
                     MakeBlock(lower, 0, theme->elevationFloor));
    }
    else
    {
        // WHICH TREE. Drawn from DungeonRandom rather than from cx/cy, so the
        // scatter is part of the floor's seed and a reload reproduces it - the
        // same reason every other placement here uses it. A hash of the
        // coordinates would also be deterministic but would tile: the same
        // variant would land on the same cell of every floor.
        u32 variants = theme->stampVariants ? theme->stampVariants : 1;
        u32 pick = (variants > 1 ? DungeonRandom() % variants : 0)
                   * DUNGEON_STAMP_METATILES;
        s32 r, c;

        for (r = 0; r < DUNGEON_WOODS_CELL; r++)
        {
            for (c = 0; c < DUNGEON_WOODS_CELL; c++)
            {
                // Where a tree mass ends, the bottom row becomes the
                // ground-contact row instead of the trunk row. The woods now
                // points both at the same metatiles - see the stamp enum - but
                // the branch stays, because it is the theme's choice to make
                // and a later stamped theme may want vanilla's behaviour.
                u32 slot = (openBelow && r == DUNGEON_WOODS_CELL - 1)
                           ? STAMP_BASE_0 + c
                           : STAMP_R0C0 + r * DUNGEON_WOODS_CELL + c;

                SetBlock(map, x + c, y + r,
                         MakeBlock(theme->stamp[slot] + pick, 1, theme->elevationWall));
            }
        }
    }
}

// Grass only grows where the floor is already open, and in blobs rather than
// per-cell noise - scattered single cells read as static, blobs read as
// undergrowth.
static u16 GrassAt(s32 cx, s32 cy, const struct RogueDungeonTheme *theme)
{
    u32 i;

    for (i = 0; i < sGrassPatchCount; i++)
    {
        s32 dx = cx - sGrassPatchX[i];
        s32 dy = cy - sGrassPatchY[i];

        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;

        if (dx + dy <= sGrassPatchRadius[i])
            return sGrassPatchLong[i] ? theme->longGrass : theme->tallGrass;
    }

    return theme->floor;
}

static void OpenCell(s32 cx, s32 cy)
{
    if (cx >= 0 && cy >= 0 && cx < DUNGEON_CELLS_W && cy < DUNGEON_CELLS_H)
        sWoodsOpen[cy][cx] = TRUE;
}

// Two passes: work out which cells are open, then paint. The second pass needs
// to know whether the cell below is open, which decides whether a tree mass
// ends here and whether long grass needs its base row.
static void WriteWoodsBlocks(u16 *map, const struct RogueDungeonTheme *theme)
{
    s32 cx, cy, i;

    for (cy = 0; cy < DUNGEON_CELLS_H; cy++)
        for (cx = 0; cx < DUNGEON_CELLS_W; cx++)
            sWoodsOpen[cy][cx] = FALSE;

    for (i = 0; i < sRoomCount; i++)
        for (cy = 0; cy < sRooms[i].h / DUNGEON_WOODS_CELL; cy++)
            for (cx = 0; cx < sRooms[i].w / DUNGEON_WOODS_CELL; cx++)
                OpenCell(sRooms[i].x / DUNGEON_WOODS_CELL + cx,
                         sRooms[i].y / DUNGEON_WOODS_CELL + cy);

    // Corridors, walked in cell space so they stay a whole stamp wide. Same
    // centre-to-centre chaining as the cave, which keeps every room connected.
    for (i = 1; i < sRoomCount; i++)
    {
        // The inner /2 is a room CENTRE and stays. The outer one converted
        // metatiles to cells and is the one that had to follow the cell size.
        s32 x0 = (sRooms[i - 1].x + sRooms[i - 1].w / 2) / DUNGEON_WOODS_CELL;
        s32 y0 = (sRooms[i - 1].y + sRooms[i - 1].h / 2) / DUNGEON_WOODS_CELL;
        s32 x1 = (sRooms[i].x + sRooms[i].w / 2) / DUNGEON_WOODS_CELL;
        s32 y1 = (sRooms[i].y + sRooms[i].h / 2) / DUNGEON_WOODS_CELL;

        while (x0 != x1)
        {
            OpenCell(x0, y0);
            x0 += (x1 > x0) ? 1 : -1;
        }
        while (y0 != y1)
        {
            OpenCell(x0, y0);
            y0 += (y1 > y0) ? 1 : -1;
        }
        OpenCell(x0, y0);
    }

    for (cy = 0; cy < DUNGEON_CELLS_H; cy++)
    {
        for (cx = 0; cx < DUNGEON_CELLS_W; cx++)
        {
            bool8 open = sWoodsOpen[cy][cx];
            // Off the bottom of the map counts as closed, so the edge does not
            // sprout tree bases against nothing.
            bool8 openBelow = (cy + 1 < DUNGEON_CELLS_H) ? sWoodsOpen[cy + 1][cx] : FALSE;
            // Distinct from !openBelow, which is also true off the bottom edge:
            // there is no tree down there to draw the crown of.
            bool8 treeBelow = (cy + 1 < DUNGEON_CELLS_H) && !sWoodsOpen[cy + 1][cx];

            StampCell(map, cx, cy, theme, open,
                      open ? GrassAt(cx, cy, theme) : theme->floor,
                      openBelow, treeBelow);
        }
    }
}

// Paints the prepared floor into the map buffer. PrepareFloor must have run.
static void WriteFloorBlocks(u16 *backupMapData)
{
    const struct RogueDungeonTheme *theme = ThemeForFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR));
    // Placeholder art; ApplyWallAutotiling picks the real metatile at the end.
    // Only the collision bit matters during carving.
    u16 wallBlock = MakeBlock(theme->wall[WALL_INTERIOR_MID], 1, theme->elevationWall);
    s32 x, y, i;

    gBackupMapLayout.map = backupMapData;
    gBackupMapLayout.width = DUNGEON_WIDTH + MAP_OFFSET_W;
    gBackupMapLayout.height = DUNGEON_HEIGHT + MAP_OFFSET_H;

    // Woods stamps whole 2x2 cells and needs no autotile pass at all. It still
    // wants the cosmetic ones, which is why they are no longer inside it: the
    // decor swap works a block at a time and is indifferent to the fact that
    // the ground under it was laid two blocks square.
    if (theme->generator == DUNGEON_GEN_WOODS)
    {
        WriteWoodsBlocks(backupMapData, theme);
        ApplyCosmeticPasses(backupMapData, theme);
        if (sRoomCount != 0 && !RogueDungeon_IsBossFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR)))
            SetBlock(backupMapData, sStairsX, sStairsY,
                     MakeBlock(sStairsMetatile, 0, theme->elevationFloor));
        return;
    }

    // The organic cave paints from the plane PrepareFloor computed rather than
    // from rooms and corridors. Boss floors are deliberately NOT included: they
    // are a single centred arena on every theme, and PrepareArenaFloor has
    // already set sRooms up for that, so the shared path below is correct for
    // them whatever the theme's generator says.
    if ((theme->generator == DUNGEON_GEN_ORGANIC
         || theme->generator == DUNGEON_GEN_FACILITY
         || theme->generator == DUNGEON_GEN_TRAILS)
        && !RogueDungeon_IsBossFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR)))
    {
        for (y = 0; y < DUNGEON_HEIGHT; y++)
        {
            for (x = 0; x < DUNGEON_WIDTH; x++)
            {
                if (CaveWallAt(sCaveBits, x, y))
                    SetBlock(backupMapData, x, y, wallBlock);
                else
                    CarveFloor(backupMapData, theme, x, y);
            }
        }

        ApplyWallAutotiling(backupMapData, theme);
        ApplyCosmeticPasses(backupMapData, theme);

        if (sRoomCount != 0)
            SetBlock(backupMapData, sStairsX, sStairsY,
                     MakeBlock(sStairsMetatile, 0, theme->elevationFloor));
        return;
    }

    for (y = 0; y < DUNGEON_HEIGHT; y++)
        for (x = 0; x < DUNGEON_WIDTH; x++)
            SetBlock(backupMapData, x, y, wallBlock);

    for (i = 0; i < sRoomCount; i++)
    {
        for (y = 0; y < sRooms[i].h; y++)
            for (x = 0; x < sRooms[i].w; x++)
                CarveFloor(backupMapData, theme,
                           sRooms[i].x + x, sRooms[i].y + y);
    }

    // An arena floor is the single room, and its exit is not drawn until the
    // boss is beaten - see RogueDungeon_OnBossDefeated.
    if (RogueDungeon_IsBossFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR)))
    {
        // Before the autotile pass, not after: one wall block ringed by floor
        // is exactly what WALL_SLIVER_ISOLATED draws, so the platform costs no
        // art. The player never boards it - it is solid - so it cannot strand
        // anyone by dismounting them on a theme they have to swim across.
        if (theme->arenaPlatform && sTrainerCount != 0)
            SetBlock(backupMapData, sTrainerX[0], sTrainerY[0], wallBlock);

        ApplyWallAutotiling(backupMapData, theme);
        ApplyCosmeticPasses(backupMapData, theme);
        return;
    }

    // Chain the rooms centre-to-centre so the floor is always fully connected.
    for (i = 1; i < sRoomCount; i++)
    {
        CarveCorridor(backupMapData, theme,
                      sRooms[i - 1].x + sRooms[i - 1].w / 2,
                      sRooms[i - 1].y + sRooms[i - 1].h / 2,
                      sRooms[i].x + sRooms[i].w / 2,
                      sRooms[i].y + sRooms[i].h / 2);
    }

    // Runs last, but only rewrites blocks whose collision bit is set, so the
    // stairs tile placed below is left alone.
    ApplyWallAutotiling(backupMapData, theme);
    ApplyCosmeticPasses(backupMapData, theme);

    if (sRoomCount != 0)
        SetBlock(backupMapData, sStairsX, sStairsY,
                 MakeBlock(sStairsMetatile, 0, theme->elevationFloor));
}

// Everything that marks the start of a genuinely new floor, as opposed to
// repainting one the player is already standing on. Both callers below roll a
// fresh seed, and both must clear the boss reward - but the reload path must
// not, or saving on a cleared arena and loading would make the reward takeable
// again. Kept in one place so the two cannot drift apart.
static u16 RollNewFloorSeed(void)
{
    u16 seed = Random();
    u32 i;

    VarSet(VAR_ROGUE_DUNGEON_SEED, seed);
    FlagClear(FLAG_ROGUE_BOSS_REWARD_TAKEN);

    // Buried items are marked collected by FLAG, and the ids repeat every
    // floor, so last floor's finds would read as already taken on this one.
    //
    // Cleared HERE, where a genuinely NEW floor is rolled, and deliberately not
    // on every load: doing it per load would resurrect everything the player
    // had already dug up as soon as they saved and reloaded on the same floor.
    // Same reasoning as FLAG_ROGUE_BOSS_REWARD_TAKEN above it.
    for (i = 0; i < DUNGEON_MAX_HIDDEN; i++)
        FlagClear(FLAG_HIDDEN_ITEMS_START + DUNGEON_HIDDEN_FIRST_ID + i);

    return seed;
}

// Called from the object-event template loader, which runs before the map is
// generated. Rolls the floor early so trainer placement can see the rooms.
void RogueDungeon_PrepareNewFloor(void)
{
    PrepareFloor(RollNewFloorSeed());
}

// Replaces LoadObjEventTemplatesFromHeader for dungeon floors, the same way the
// Battle Pyramid and Trainer Hill do. The engine reads templates for the
// current map from the save block but takes the COUNT from ROM, so map.json
// declares DUNGEON_MAX_TRAINERS placeholders and we overwrite them here.
void RogueDungeon_LoadObjectEventTemplates(void)
{
    struct ObjectEventTemplate *templates = gSaveBlock1Ptr->objectEventTemplates;
    u32 i;
    // Trainers stand on the floor, so they take the floor's elevation rather
    // than a fixed one. The ocean walks at elevation 1 where every other theme
    // walks at 3, and mismatched elevations do not collide - the player would
    // have walked straight through every trainer on a water floor instead of
    // being able to talk to one.
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    const struct RogueDungeonTheme *theme = ThemeForFloor(floor);
    u8 elevation = theme->elevationFloor;
    bool8 inArena = RogueDungeon_IsBossFloor(floor);
    bool8 onPlatform = theme->arenaPlatform && inArena;

    // This runs before the map is generated, so roll the floor now - trainer
    // placement needs to see the rooms.
    RogueDungeon_PrepareNewFloor();

    // An object event whose flagId is set is not spawned. Kept set permanently
    // so leftover placeholder slots stay invisible.
    FlagSet(FLAG_ROGUE_OBJECT_UNUSED);

    // Stock trainer defeat flags are permanent, and we reuse stock trainers
    // every floor, so a trainer beaten earlier in the run would otherwise be
    // skipped as already won.
    //
    // Cleared HERE, once per floor, and deliberately not at battle setup: the
    // battle script checks this flag immediately after running that special, so
    // clearing it there means a defeated trainer is never recognised as such
    // and rematches forever.
    for (i = 0; i < sTrainerCount; i++)
        FlagClear(TRAINER_FLAGS_START + sTrainerIds[i]);

    CpuFill32(0, templates, sizeof(gSaveBlock1Ptr->objectEventTemplates));

    for (i = 0; i < DUNGEON_MAX_TRAINERS; i++)
    {
        templates[i].localId = i + 1;
        templates[i].kind = OBJ_KIND_NORMAL;
        templates[i].elevation = elevation;

        if (i < sTrainerCount)
        {
            templates[i].graphicsId = sTrainerGfx[i];
            templates[i].x = sTrainerX[i];
            templates[i].y = sTrainerY[i];
            templates[i].movementType = MOVEMENT_TYPE_FACE_DOWN;
            // EVERY ARENA TRAINER IS TALK-ONLY, not just the ones on a platform.
            // The platform case came first and its reasoning generalises: sight
            // makes the trainer WALK, and an arena trainer that walks is wrong
            // three separate ways.
            //
            // It was cosmetic on a platform theme only by accident - there the
            // walk ended on water and the trainer was stranded for good, which
            // is merely the most visible version of the same fault.
            //
            // 1. The arena is built as a face-off. PrepareArenaFloor puts the
            //    player at the south end and the boss at the north "so the two
            //    face off across the arena"; a boss that trots over to the
            //    player before the fight throws that staging away.
            // 2. A boss pleading for help does not ALSO charge the player with
            //    an exclamation mark over its head. The approach is the stock
            //    game's aggression cue and it contradicts the line.
            // 3. IT MOVES THE BOSS OFF ITS PLACED TILE, and
            //    RogueDungeon_AbandonBossAce respawns the abandoned ace from the
            //    TEMPLATE - which still holds where the boss was placed, not
            //    where it walked to. A boss that approached would vanish in
            //    front of the player and leave its Pokemon standing across the
            //    room. This is the one that is a bug rather than a staging
            //    choice, and it is why the range goes to zero as well as the
            //    type: sight range is what triggers the walk.
            templates[i].trainerType = (onPlatform || inArena) ? TRAINER_TYPE_NONE
                                                               : TRAINER_TYPE_NORMAL;
            templates[i].trainerRange_berryTreeId = inArena
                                                  ? 0
                                                  : DUNGEON_TRAINER_SIGHT_RANGE;
            templates[i].script = RogueDungeonFloor_EventScript_Trainer;
            templates[i].flagId = 0;
        }
        else
        {
            templates[i].flagId = FLAG_ROGUE_OBJECT_UNUSED;
        }
    }

    // Item balls. They carry no item id in the template on purpose: item_ball.c
    // reads its contents out of gMapHeader.events->objectEvents, which is the
    // ROM array and not the save block copy written here, so a generated item
    // written into a template would be ignored and every ball would hand over
    // whatever map.json happened to declare. The engine's own accessor
    // (GetObjectEventTemplateByLocalIdAndMap) reads the save block correctly;
    // item_ball.c is the one place that does not.
    //
    // Sidestepped rather than fixed: the SCRIPT pointer does come from the save
    // block, so pointing these at our own script means that path is never
    // entered and there is nothing upstream to patch.
    for (i = 0; i < DUNGEON_MAX_ITEMS; i++)
    {
        u32 slot = DUNGEON_MAX_TRAINERS + i;

        templates[slot].localId = slot + 1;
        templates[slot].kind = OBJ_KIND_NORMAL;
        templates[slot].elevation = elevation;

        if (i < sItemCount)
        {
            templates[slot].graphicsId = OBJ_EVENT_GFX_ITEM_BALL;
            templates[slot].x = sItemX[i];
            templates[slot].y = sItemY[i];
            templates[slot].movementType = MOVEMENT_TYPE_LOOK_AROUND;
            templates[slot].script = RogueDungeonFloor_EventScript_ItemBall;
            templates[slot].flagId = 0;
        }
        else
        {
            templates[slot].flagId = FLAG_ROGUE_OBJECT_UNUSED;
        }
    }

    // Berry trees. The save block half has to happen here rather than at
    // prepare time - see PlantFloorBerryTrees for why this function is the
    // right place and PrepareFloor is not.
    PlantFloorBerryTrees();

    for (i = 0; i < DUNGEON_MAX_BERRIES; i++)
    {
        u32 slot = DUNGEON_MAX_TRAINERS + DUNGEON_MAX_ITEMS + i;

        templates[slot].localId = slot + 1;
        templates[slot].kind = OBJ_KIND_NORMAL;
        templates[slot].elevation = elevation;

        if (i < sBerryCount)
        {
            templates[slot].graphicsId = OBJ_EVENT_GFX_BERRY_TREE;
            templates[slot].x = sBerryX[i];
            templates[slot].y = sBerryY[i];
            // Both of these are load-bearing rather than decorative. The
            // movement type is how the engine recognises a tree at all - it
            // scans for MOVEMENT_TYPE_BERRY_TREE_GROWTH - and the berry tree id
            // shares the field the trainers use for sight range.
            templates[slot].movementType = MOVEMENT_TYPE_BERRY_TREE_GROWTH;
            templates[slot].trainerRange_berryTreeId =
                DUNGEON_BERRY_FIRST_TREE_ID + i;
            // THE SCRIPT IS THE ONLY THING THAT DIFFERS about the imposter.
            // Graphics, movement type and the planted berry behind it are
            // identical, because the disguise has to survive being looked at -
            // a tree that renders differently is not hiding. See
            // DUNGEON_SUDOWOODO_ODDS.
            if (RogueDungeon_IsSudowoodoTree(i))
                templates[slot].script = RogueDungeonFloor_EventScript_Sudowoodo;
            else
                templates[slot].script = RogueDungeonFloor_EventScript_BerryTree;
            templates[slot].flagId = 0;
        }
        else
        {
            templates[slot].flagId = FLAG_ROGUE_OBJECT_UNUSED;
        }
    }

    for (i = 0; i < DUNGEON_MAX_ROCKS; i++)
    {
        u32 slot = DUNGEON_MAX_TRAINERS + DUNGEON_MAX_ITEMS
                 + DUNGEON_MAX_BERRIES + i;

        templates[slot].localId = slot + 1;
        templates[slot].kind = OBJ_KIND_NORMAL;
        templates[slot].elevation = elevation;

        if (i < sRockCount)
        {
            templates[slot].graphicsId = OBJ_EVENT_GFX_BREAKABLE_ROCK;
            templates[slot].x = sRockX[i];
            templates[slot].y = sRockY[i];
            templates[slot].movementType = MOVEMENT_TYPE_NONE;
            templates[slot].trainerRange_berryTreeId = 0;
            templates[slot].script = RogueDungeonFloor_EventScript_MiningRock;
            templates[slot].flagId = 0;
        }
        else
        {
            templates[slot].flagId = FLAG_ROGUE_OBJECT_UNUSED;
        }
    }

    // The floor's event. Both the sprite and the script come out of
    // sFloorEvents, so this loop knows nothing about what any event does - and
    // the script pointer being read from the SAVE BLOCK is what makes that work.
    // See the item ball note above for the one engine path that does not.
    for (i = 0; i < DUNGEON_MAX_EVENTS; i++)
    {
        u32 slot = DUNGEON_MAX_TRAINERS + DUNGEON_MAX_ITEMS
                 + DUNGEON_MAX_BERRIES + DUNGEON_MAX_ROCKS + i;

        templates[slot].localId = slot + 1;
        templates[slot].kind = OBJ_KIND_NORMAL;
        templates[slot].elevation = elevation;

        if (i == 1 && sEventCount > 1)
        {
            // The prop. NO SCRIPT AND NO TRAINER TYPE - it is scenery the event's
            // text refers to, and a second talkable object beside the NPC would
            // read as a second event that does nothing.
            templates[slot].graphicsId = sFloorEvents[sEventIndex].propGfxId;
            templates[slot].x = sEventPropX;
            templates[slot].y = sEventPropY;
            templates[slot].movementType = MOVEMENT_TYPE_NONE;
            templates[slot].trainerType = TRAINER_TYPE_NONE;
            templates[slot].trainerRange_berryTreeId = 0;
            templates[slot].script = NULL;
            templates[slot].flagId = 0;
        }
        else if (i == 0 && sEventCount > 0)
        {
            const struct RogueFloorEvent *event = &sFloorEvents[sEventIndex];

            // OBJ_EVENT_MON is a BIT (1 << 14) added to a species id, not a
            // table index - the same construction the rest stop's Unown use.
            // OW_POKEMON_OBJECT_EVENTS is already TRUE so the sprites are
            // already in the build.
            templates[slot].graphicsId =
                (event->gfxId == DUNGEON_EVENT_GFX_ROLLED)
                    ? OBJ_EVENT_MON + sEventSpecies
                    : event->gfxId;
            templates[slot].x = sEventX;
            templates[slot].y = sEventY;
            // FACE_DOWN rather than LOOK_AROUND: an event NPC is a fixture the
            // player walks up to and talks to, and TRAINER_TYPE_NONE keeps it
            // from ever initiating anything itself. A row may override it - see
            // the movementType note on struct RogueFloorEvent.
            templates[slot].movementType = event->movementType != 0
                                         ? event->movementType
                                         : MOVEMENT_TYPE_FACE_DOWN;
            templates[slot].trainerType = TRAINER_TYPE_NONE;
            // The wander range rides the SAME FIELD as a trainer's sight range,
            // which is why it can only be set once trainerType is NONE - the
            // engine reads it as one or the other depending on that. Left at 0
            // for a fixture, because a range on something that never moves is
            // meaningless rather than harmless.
            templates[slot].trainerRange_berryTreeId =
                event->movementType != 0 ? DUNGEON_EVENT_WANDER_RANGE : 0;
            templates[slot].script = event->script;
            templates[slot].flagId = 0;
        }
        else
        {
            templates[slot].flagId = FLAG_ROGUE_OBJECT_UNUSED;
        }
    }
}

// A rock is spent once it has been mined. Same shape as
// RogueDungeon_HideTakenFloorItem and for the same reason: the template is
// what a reload rebuilds from, so moving it off the map is what makes the
// rock stay gone until the next floor.
void RogueDungeon_HideMinedRock(void)
{
    struct ObjectEventTemplate *templates = gSaveBlock1Ptr->objectEventTemplates;
    u32 first = DUNGEON_ROCK_FIRST_LOCAL_ID;
    u32 slot;

    if (gSpecialVar_LastTalked < first
     || gSpecialVar_LastTalked >= first + DUNGEON_MAX_ROCKS)
        return;

    slot = gSpecialVar_LastTalked - 1;
    templates[slot].x = INT16_MAX;
    templates[slot].y = INT16_MAX;
    RemoveObjectEventByLocalIdAndMap(gSpecialVar_LastTalked,
                                     gSaveBlock1Ptr->location.mapNum,
                                     gSaveBlock1Ptr->location.mapGroup);
}

// Maps a talked-to object event back to its item ball slot, the same way
// RogueDungeon_HasTrainerBeenBeaten maps one back to its trainer. Returns
// DUNGEON_MAX_ITEMS when the object is not an item ball.
static u32 FloorItemSlotOf(u16 localId)
{
    u32 slot;

    if (localId < DUNGEON_ITEM_FIRST_LOCAL_ID)
        return DUNGEON_MAX_ITEMS;

    slot = localId - DUNGEON_ITEM_FIRST_LOCAL_ID;
    return (slot < DUNGEON_MAX_ITEMS) ? slot : DUNGEON_MAX_ITEMS;
}

// specialvar target. Puts the ball's contents where the finditem macro expects
// them - VAR_RESULT for the item, VAR_0x8009 for how many - matching the shape
// of the engine's own Common_EventScript_FindItem.
u16 RogueDungeon_PrepareFloorItem(void)
{
    u32 slot = FloorItemSlotOf(gSpecialVar_LastTalked);

    if (slot >= sItemCount)
    {
        // Nothing sensible to hand over. A Potion rather than ITEM_NONE, which
        // finditem would announce as an empty pickup.
        gSpecialVar_0x8009 = 1;
        return ITEM_POTION;
    }

    gSpecialVar_0x8009 = sItemQty[slot];
    return sItemIds[slot];
}

// Called after the pickup. The ball has to stay gone for the rest of the floor,
// and removeobject alone does not manage that - the object respawns whenever
// the player walks far enough away and back, because the template it spawns
// from still says it is there.
//
// So the template moves off the map, which is what the Battle Pyramid does with
// its own items and for the same reason. It survives a save and reload because
// RogueDungeon_LoadObjectEventTemplates does not run on the load-from-save
// path - the save block copy it wrote is what comes back.
//
// Keyed on whether the object is still active rather than on the item having
// reached the bag, because those are the same question asked of the thing that
// actually happened: Std_FindItem only removes the object on success, so a full
// bag leaves the ball standing and this correctly does nothing.
void RogueDungeon_HideTakenFloorItem(void)
{
    struct ObjectEventTemplate *templates = gSaveBlock1Ptr->objectEventTemplates;
    u32 slot = FloorItemSlotOf(gSpecialVar_LastTalked);
    u8 objectEventId;

    if (slot >= DUNGEON_MAX_ITEMS)
        return;

    // TryGet... returns TRUE when it did NOT find one, so this is "still there".
    if (!TryGetObjectEventIdByLocalIdAndMap(gSpecialVar_LastTalked,
                                            gSaveBlock1Ptr->location.mapNum,
                                            gSaveBlock1Ptr->location.mapGroup,
                                            &objectEventId))
        return;

    templates[DUNGEON_MAX_TRAINERS + slot].x = INT16_MAX;
    templates[DUNGEON_MAX_TRAINERS + slot].y = INT16_MAX;
}

// Called by the shared trainer script. The opponent is chosen per floor rather
// than baked into a script, so the ordinary trainerbattle command cannot be
// used - this configures the battle by hand and jumps to the shared tail.
// TRUE while standing on a generated floor, where trainer scripts carry no
// inline trainerbattle data.
bool8 RogueDungeon_IsGeneratedTrainer(void)
{
    return gMapHeader.mapLayoutId == LAYOUT_ROGUE_DUNGEON_FLOOR;
}

// Has this trainer already been beaten on this floor?
//
// The engine normally answers this by reading the opponent id straight out of
// the trainer script, which a generated trainer does not carry. Looked up by
// object event instead, the same way the Battle Pyramid and Trainer Hill do it.
bool8 RogueDungeon_HasTrainerBeenBeaten(u8 objectEventId)
{
    u32 slot = gObjectEvents[objectEventId].localId - 1;

    if (slot >= sTrainerCount)
        return FALSE;

    return FlagGet(TRAINER_FLAGS_START + sTrainerIds[slot]);
}

void RogueDungeon_SetUpTrainerBattle(void)
{
    u32 slot = gSpecialVar_LastTalked - 1;
    u16 trainerId;

    if (slot >= sTrainerCount)
        slot = 0;
    trainerId = sTrainerIds[slot];

    // Winning a battle ends on gotobeatenscript, which reads
    // battleScriptRetAddrA - NOT sTrainerBattleEndScript, which only serves
    // gotopostbattlescript on the already-defeated path. Leaving it NULL makes
    // BattleSetup_GetTrainerPostBattleScript fall through to
    // EventScript_TryGetTrainerScript, which loops straight back into
    // gotobeatenscript and hangs the script context forever.
    const u8 *endScript = RogueDungeon_IsBossFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR))
                        ? RogueDungeonFloor_EventScript_BossDone
                        : RogueDungeonFloor_EventScript_TrainerDone;

    // IsDungeonBossFloor, not RogueDungeon_IsBossFloor: the plea belongs to the
    // fourteen named bosses and not to the mini bosses, which share the
    // post-battle script but are anonymous trainers picked by level. A grunt
    // begging for help would make the line ambient rather than a signal, and
    // nothing is abandoned on a mini boss floor for it to pay off against.
    const u8 *introText = IsDungeonBossFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR))
                        ? RogueDungeonFloor_Text_BossIntro
                        : RogueDungeonFloor_Text_TrainerIntro;

    // Mirrors BattleSetup_ConfigureFacilityTrainerBattle: when two trainers
    // spot the player at once this runs twice, and the second call must fill
    // slot B without wiping slot A.
    if (gApproachingTrainerId != 0)
    {
        TRAINER_BATTLE_PARAM.playMusicB = TRUE;
        TRAINER_BATTLE_PARAM.objEventLocalIdB = gSpecialVar_LastTalked;
        TRAINER_BATTLE_PARAM.opponentB = trainerId;
        TRAINER_BATTLE_PARAM.introTextB = (u8 *)introText;
        TRAINER_BATTLE_PARAM.defeatTextB = (u8 *)RogueDungeonFloor_Text_TrainerDefeat;
        TRAINER_BATTLE_PARAM.battleScriptRetAddrB = (u8 *)endScript;
        return;
    }

    InitTrainerBattleParameter();
    TRAINER_BATTLE_PARAM.mode = TRAINER_BATTLE_SINGLE;
    TRAINER_BATTLE_PARAM.playMusicA = TRUE;
    TRAINER_BATTLE_PARAM.objEventLocalIdA = gSpecialVar_LastTalked;
    TRAINER_BATTLE_PARAM.opponentA = trainerId;
    TRAINER_BATTLE_PARAM.introTextA = (u8 *)introText;
    TRAINER_BATTLE_PARAM.defeatTextA = (u8 *)RogueDungeonFloor_Text_TrainerDefeat;
    TRAINER_BATTLE_PARAM.battleScriptRetAddrA = (u8 *)endScript;

    SetMapVarsToTrainerA();

    // A generated trainer never reaches BattleSetup_ConfigureTrainerBattle --
    // ConfigureTrainerBattle and ConfigureTwoTrainersBattle branch around it
    // because these scripts carry no inline trainerbattle data -- so the
    // player's battle mode option has to be applied here or the majority of the
    // battles in a run would quietly ignore it.
    //
    // Safe in the two-trainer case as well. This runs on the slot A call, which
    // happens first either way; forcing singles leaves the mode at the
    // TRAINER_BATTLE_SINGLE set just above, and forcing doubles matches what a
    // two-opponent battle already is.
    ApplyBattleModePreference();

    // The other exit: talking to an already-beaten trainer skips the battle and
    // leaves via gotopostbattlescript, which reads this one instead.
    SetTrainerBattleEndScript(endScript);
}

// Run-wide settings the engine reads from flags rather than from config. Called
// on every floor load rather than once when a run begins, because a flag set
// only by the run-start script would never reach a save made before that script
// gained the line - and because there is no cost to setting a set flag.
static void ApplyRunConfig(void)
{
    u32 dungeon = DungeonIndexOf(VarGet(VAR_ROGUE_DUNGEON_FLOOR));

    // Act-scoped charms expire when the run crosses into a new dungeon. Hung off
    // this function for the same reason the rod techniques are: every path onto
    // a floor comes through here, including the debug warp.
    RogueCharm_OnFloorLoad(dungeon);

    // Party-wide Exp Share, permanently on. See FLAG_ROGUE_EXP_SHARE.
    FlagSet(FLAG_ROGUE_EXP_SHARE);

    // The variable rod's two techniques, which is the whole progression the rod
    // has. Set here rather than from a script for the reason above: this runs on
    // every floor load, so a run already past the threshold when this shipped
    // still gets them, and there is no entry path that can miss one.
    //
    // KEYED ON THE DUNGEON, not the floor. Dungeons are not the same length -
    // ten floors for a gym, five for an Elite Four member - so a raw floor
    // number means a different place in the run the moment anything is
    // restructured, and nothing would report it.
    //
    // Cleared by RogueDungeon_ResetRun, not here. Putting the floor counter
    // back to 0 does NOT narrow the menu again by itself - a flag survives it -
    // so without that a whiteout would leave the next run holding the Super
    // technique on floor 1.
    if (dungeon >= DUNGEON_ROD_GOOD_DUNGEON)
        FlagSet(FLAG_ROGUE_ROD_GOOD_TECHNIQUE);
    if (dungeon >= DUNGEON_ROD_SUPER_DUNGEON)
        FlagSet(FLAG_ROGUE_ROD_SUPER_TECHNIQUE);
}

void GenerateRogueDungeonFloor(u16 *backupMapData, bool8 setPlayerPosition)
{
    ApplyRunConfig();

    // Normally RogueDungeon_PrepareNewFloor has already run from the template
    // loader. That loader can be skipped, and never runs on load-from-save, so
    // fall back to preparing here.
    //
    // setPlayerPosition distinguishes the callers. FALSE is LoadMapFromWarp -
    // the player is entering, so roll a new floor. TRUE is CB2_ContinueSavedGame
    // - reuse the stored seed so a reload reproduces the floor the player saved
    // on, rather than dropping them inside solid rock.
    if (!sFloorPrepared)
    {
        u16 seed;

        if (setPlayerPosition == FALSE)
        {
            seed = RollNewFloorSeed();
        }
        else
        {
            seed = VarGet(VAR_ROGUE_DUNGEON_SEED);
        }

        PrepareFloor(seed);
    }

    // After PrepareFloor, which is what decides the hidden items, and on every
    // load rather than only on new floors - gMapHeader is re-copied from ROM
    // each time, so the pointer has to be put back each time too.
    ApplyDungeonEvents();

    WriteFloorBlocks(backupMapData);

    if (setPlayerPosition == FALSE && sRoomCount != 0)
    {
        gSaveBlock1Ptr->pos.x = sSpawnX;
        gSaveBlock1Ptr->pos.y = sSpawnY;
    }

    // Consumed. The next map load must prepare afresh, or it would repaint this
    // floor instead of generating the next one.
    sFloorPrepared = FALSE;
}

// Which graphic FLDEFF_LONG_GRASS should wear on this floor.
//
// MB_LONG_GRASS is what makes Ever Grande's flowers an encounter surface, and
// it brings FLDEFF_LONG_GRASS with it - whose art is a curtain of green blades.
// Over pink and orange blooms that is not broken, it just tells the player they
// are standing in grass. The flower curtain is the same sprite recoloured onto
// the flower metatiles' own leaf ramp with blossoms among the blades.
//
// This picks the GRAPHIC only. The effect stays FLDEFF_LONG_GRASS, because
// every ground-effect flag, the OAM clip that hides the player's lower half and
// UpdateLongGrassFieldEffect's own FieldEffectStop all key on that id. The
// jungle, whose long grass is genuinely grass, is untouched.
//
// Keyed on the floor's theme rather than on the metatile under the player,
// because the caller has the object event's position and not necessarily the
// block, and a floor only ever has one long-grass surface.
u8 RogueDungeon_LongGrassFieldEffectObj(void)
{
    if (gMapHeader.mapLayoutId != LAYOUT_ROGUE_DUNGEON_FLOOR)
        return FLDEFFOBJ_LONG_GRASS;

    if (ThemeForFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR))
        == &sDungeonThemes[DUNGEON_THEME_EVERGRANDE])
        return FLDEFFOBJ_ROGUE_FLOWERS;

    return FLDEFFOBJ_LONG_GRASS;
}

// The same trick one effect over, for the TALL grass rustle - the sprite drawn
// on whatever tile the player is standing in.
//
// It needed its own object for a duller reason than the flowers did: the art is
// identical and only the PALETTE differs. FLDEFFOBJ_TALL_GRASS resolves
// FLDEFF_PAL_TAG_GENERAL_1, which sixteen field effects share, so the woods
// could not be recoloured without tinting grass rustle, jump grass and ripples
// on every map in the game. With the floor autumn and this still mint, the one
// square of old colour left on the map was the one under the player, and it
// followed them around.
u8 RogueDungeon_TallGrassFieldEffectObj(void)
{
    if (gMapHeader.mapLayoutId != LAYOUT_ROGUE_DUNGEON_FLOOR)
        return FLDEFFOBJ_TALL_GRASS;

    if (ThemeForFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR))
        == &sDungeonThemes[DUNGEON_THEME_WOODS])
        return FLDEFFOBJ_ROGUE_WOODS_GRASS;

    return FLDEFFOBJ_TALL_GRASS;
}

// Hooked into TryStartStepBasedScript. Returning TRUE means we consumed the
// step, so nothing else gets a chance to run a script for it.
bool8 RogueDungeon_TryStartStairsScript(struct MapPosition *position)
{
    const struct RogueDungeonTheme *theme;
    u16 metatile;

    if (gMapHeader.mapLayoutId != LAYOUT_ROGUE_DUNGEON_FLOOR)
        return FALSE;

    theme = ThemeForFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR));
    metatile = MapGridGetMetatileIdAt(position->x, position->y);

    // Per theme: a metatile id means different things under different secondary
    // tilesets, so the cave stairs id cannot be reused for the woods.
    if (metatile != theme->stairsDown && metatile != theme->stairsUp)
        return FALSE;

    ScriptContext_SetupScript(RogueDungeonFloor_EventScript_Stairs);
    return TRUE;
}
