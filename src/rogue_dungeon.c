#include "global.h"
#include "event_data.h"
#include "fieldmap.h"
#include "random.h"
#include "script.h"
#include "string_util.h"
#include "pokemon.h"
#include "wild_encounter.h"
#include "battle_setup.h"
#include "event_object_movement.h"
#include "field_camera.h"
#include "overworld.h"
#include "field_screen_effect.h"   // DoWarp, for the script-side floor warp
#include "item.h"
#include "data.h"
#include "trainer_see.h"
#include "constants/items.h"
#include "constants/layouts.h"
#include "constants/moves.h"
#include "constants/vars.h"
#include "constants/wild_encounter.h"
#include "constants/event_objects.h"
#include "constants/trainer_types.h"
#include "constants/battle_setup.h"
#include "constants/event_object_movement.h"
#include "constants/rogue_dungeon_trainers.h"
#include "constants/rogue_dungeon_starters.h"
#include "rogue_dungeon.h"

extern const u8 RogueDungeonFloor_EventScript_Stairs[];
extern const u8 RogueDungeonFloor_EventScript_Trainer[];
extern const u8 RogueDungeonFloor_EventScript_TrainerDone[];
extern const u8 RogueDungeonFloor_EventScript_BossDone[];
extern const u8 RogueDungeonFloor_Text_TrainerIntro[];
extern const u8 RogueDungeonFloor_Text_TrainerDefeat[];

static u16 PickTrainerForLevel(u8 target);

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
    SPECIES_ZUBAT,   SPECIES_WHISMUR,  SPECIES_GEODUDE,  SPECIES_MAKUHITA,
    SPECIES_ARON,    SPECIES_NOSEPASS, SPECIES_SABLEYE,  SPECIES_MAWILE,
    SPECIES_GOLBAT,  SPECIES_LOUDRED,  SPECIES_GRAVELER, SPECIES_LAIRON,
    SPECIES_ONIX,    SPECIES_HARIYAMA, SPECIES_SHUCKLE,  SPECIES_CLAYDOL,
};

static const u16 sWoodsSpecies[] =
{
    SPECIES_WURMPLE, SPECIES_ZIGZAGOON, SPECIES_POOCHYENA, SPECIES_SEEDOT,
    SPECIES_LOTAD,   SPECIES_TAILLOW,   SPECIES_SILCOON,   SPECIES_CASCOON,
    SPECIES_SHROOMISH, SPECIES_NINCADA, SPECIES_BEAUTIFLY, SPECIES_DUSTOX,
    SPECIES_NUZLEAF, SPECIES_LOMBRE,    SPECIES_SWELLOW,   SPECIES_BRELOOM,
};

// New Mauville runs on generators, so the pool is electric with the steel and
// magnet types the facility already houses.
static const u16 sNewMauvilleSpecies[] =
{
    SPECIES_MAGNEMITE, SPECIES_VOLTORB,  SPECIES_PIKACHU,   SPECIES_ELECTRIKE,
    SPECIES_PLUSLE,    SPECIES_MINUN,    SPECIES_CHINCHOU,  SPECIES_MAREEP,
    SPECIES_MAGNETON,  SPECIES_ELECTRODE, SPECIES_FLAAFFY,  SPECIES_LANTURN,
    SPECIES_MANECTRIC, SPECIES_RAICHU,   SPECIES_ELECTABUZZ, SPECIES_AMPHAROS,
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
};

static const struct RogueSkirt sFieryPathSkirts[] =
{
    { FIERYPATH_METATILE_WALL_NORTH_MID, FIERYPATH_METATILE_RIDGE_SKIRT_S, 0 },
};

static const struct RogueDecor sNewMauvilleDecor[] =
{
    { NEWMAUVILLE_METATILE_WALL_BAND, NEWMAUVILLE_METATILE_WALL_VENT },
    { NEWMAUVILLE_METATILE_WALL_BAND, NEWMAUVILLE_METATILE_WALL_COUNTER },
    { NEWMAUVILLE_METATILE_WALL_BAND, NEWMAUVILLE_METATILE_WALL_BOOKCASE_L,
                                      NEWMAUVILLE_METATILE_WALL_BOOKCASE_R },
};

// Fiery Path's own residents plus their evolutions, with a couple of fire
// types that fit the tunnel. Ordered weakest to strongest like the others.
static const u16 sFieryPathSpecies[] =
{
    SPECIES_SLUGMA,   SPECIES_KOFFING,  SPECIES_NUMEL,    SPECIES_GRIMER,
    SPECIES_MACHOP,   SPECIES_VULPIX,   SPECIES_HOUNDOUR, SPECIES_TORKOAL,
    SPECIES_MAGCARGO, SPECIES_WEEZING,  SPECIES_CAMERUPT, SPECIES_MACHOKE,
    SPECIES_MAGMAR,   SPECIES_MUK,      SPECIES_HOUNDOOM, SPECIES_NINETALES,
};

// The Route 111 desert's own residents, plus the two fossils that Mirage Tower
// is famous for handing out - Lileep and Anorith come from this tower in the
// stock game, so meeting them here is the point. Ordered weakest to strongest.
// Deliberately shares nothing with the cave pool despite the shared tileset.
static const u16 sMirageTowerSpecies[] =
{
    SPECIES_SANDSHREW, SPECIES_TRAPINCH, SPECIES_BALTOY,   SPECIES_CACNEA,
    SPECIES_LILEEP,    SPECIES_ANORITH,  SPECIES_PHANPY,   SPECIES_RHYHORN,
    SPECIES_SANDSLASH, SPECIES_VIBRAVA,  SPECIES_CLAYDOL,  SPECIES_CACTURNE,
    SPECIES_DONPHAN,   SPECIES_CRADILY,  SPECIES_ARMALDO,  SPECIES_FLYGON,
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
    SPECIES_ODDISH,   SPECIES_SURSKIT,    SPECIES_MARILL,     SPECIES_VOLBEAT,
    SPECIES_ILLUMISE, SPECIES_ROSELIA,    SPECIES_SLAKOTH,    SPECIES_KECLEON,
    SPECIES_GLOOM,    SPECIES_MASQUERAIN, SPECIES_AZUMARILL,  SPECIES_VIGOROTH,
    SPECIES_TROPIUS,  SPECIES_ZANGOOSE,   SPECIES_SEVIPER,    SPECIES_ABSOL,
};

// The open sea. Everything here is what actually swims on Routes 124 to 126,
// which is the stretch the player is crossing. Encounters fire on every block
// rather than only in a grass layer, because MB_OCEAN_WATER carries the flag
// itself - the same deal the cave gets.
static const u16 sOceanSpecies[] =
{
    SPECIES_TENTACOOL,  SPECIES_MAGIKARP, SPECIES_WINGULL,  SPECIES_CARVANHA,
    SPECIES_GOLDEEN,    SPECIES_HORSEA,   SPECIES_SPHEAL,   SPECIES_WAILMER,
    SPECIES_TENTACRUEL, SPECIES_PELIPPER, SPECIES_SEAKING,  SPECIES_SEADRA,
    SPECIES_SEALEO,     SPECIES_SHARPEDO, SPECIES_WAILORD,  SPECIES_KINGDRA,
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
    {
        .tile =
        {
            [PATCH_NW]      = JUNGLE_METATILE_PUDDLE_NW,
            [PATCH_N]       = JUNGLE_METATILE_PUDDLE_N,
            [PATCH_NE]      = JUNGLE_METATILE_PUDDLE_NE,
            [PATCH_W]       = JUNGLE_METATILE_PUDDLE_W,
            [PATCH_MID]     = JUNGLE_METATILE_PUDDLE_MID,
            [PATCH_E]       = JUNGLE_METATILE_PUDDLE_E,
            [PATCH_SW]      = JUNGLE_METATILE_PUDDLE_SW,
            [PATCH_S]       = JUNGLE_METATILE_PUDDLE_S,
            [PATCH_SE]      = JUNGLE_METATILE_PUDDLE_SE,
            // No wall-adjacent variant exists, so the ordinary top edge serves.
            [PATCH_NW_WALL] = JUNGLE_METATILE_PUDDLE_NW,
            [PATCH_N_WALL]  = JUNGLE_METATILE_PUDDLE_N,
            [PATCH_NE_WALL] = JUNGLE_METATILE_PUDDLE_NE,
        },
        // Many and small: puddles, not lakes.
        .blobs = 10, .radius = 3,
    },
};

// The canopy's two body variants and two base variants are interchangeable, so
// decor is what scatters them. Rarity 2 gets close to vanilla's even 50/50 mix.
static const struct RogueDecor sJungleDecor[] =
{
    { JUNGLE_METATILE_CANOPY,      JUNGLE_METATILE_CANOPY_ALT },
    { JUNGLE_METATILE_CANOPY_BASE, JUNGLE_METATILE_CANOPY_BASE_ALT },
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
static const u16 sUnderwaterSpecies[] =
{
    SPECIES_CHINCHOU,  SPECIES_CLAMPERL, SPECIES_CORPHISH, SPECIES_BARBOACH,
    SPECIES_LUVDISC,   SPECIES_CORSOLA,  SPECIES_REMORAID, SPECIES_STARYU,
    SPECIES_RELICANTH, SPECIES_WHISCASH, SPECIES_LANTURN,  SPECIES_OCTILLERY,
    SPECIES_CRAWDAUNT, SPECIES_STARMIE,  SPECIES_HUNTAIL,  SPECIES_GOREBYSS,
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
    DUNGEON_THEME_COUNT
};

static const struct RogueDungeonTheme sDungeonThemes[DUNGEON_THEME_COUNT] =
{
    [DUNGEON_THEME_WOODS] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_WOODS,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
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
        .aboveTreeFloorL = WOODS_METATILE_ABOVE_TREE_L,
        .aboveTreeFloorR = WOODS_METATILE_ABOVE_TREE_R,
        .aboveTreeGrassL = WOODS_METATILE_ABOVE_TREE_TALL_L,
        .aboveTreeGrassR = WOODS_METATILE_ABOVE_TREE_TALL_R,
        .stairsDown = WOODS_METATILE_STAIRS,
        .stairsUp = WOODS_METATILE_STAIRS,
        .stamp =
        {
            [STAMP_TL] = WOODS_METATILE_TREE_TL, [STAMP_TR] = WOODS_METATILE_TREE_TR,
            [STAMP_BL] = WOODS_METATILE_TREE_BL, [STAMP_BR] = WOODS_METATILE_TREE_BR,
            [STAMP_BASE_L] = WOODS_METATILE_TREE_BASE_L,
            [STAMP_BASE_R] = WOODS_METATILE_TREE_BASE_R,
        },
        .species = sWoodsSpecies,
        .speciesCount = ARRAY_COUNT(sWoodsSpecies),
    },
    [DUNGEON_THEME_CAVE] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_FLOOR,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
        .generator = DUNGEON_GEN_CAVE,
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
    },
    [DUNGEON_THEME_NEWMAUVILLE] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_NEWMAUVILLE,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
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

            [WALL_SLIVER_VERT]    = NEWMAUVILLE_METATILE_WALL_PILLAR,
            [WALL_SLIVER_VERT_TOP]= NEWMAUVILLE_METATILE_WALL_PILLAR_TOP,
            [WALL_SLIVER_VERT_BOT]= NEWMAUVILLE_METATILE_WALL_PILLAR_BOT,
            [WALL_SLIVER_ISOLATED]= NEWMAUVILLE_METATILE_WALL_PILLAR,

            // Every cardinal is wall in these cases, so nothing of the wall art
            // is visible - only the void reads correctly. Filling them with a
            // wall body puts a lit edge in the middle of a dark mass.
            [WALL_INTERIOR_MID]   = NEWMAUVILLE_METATILE_VOID,
            [WALL_CORNER_OPEN_SE] = NEWMAUVILLE_METATILE_VOID,
            [WALL_CORNER_OPEN_SW] = NEWMAUVILLE_METATILE_VOID,
            [WALL_CORNER_OPEN_NW] = NEWMAUVILLE_METATILE_VOID,
            [WALL_CORNER_OPEN_NE] = NEWMAUVILLE_METATILE_VOID,
        },
        .skirts = sNewMauvilleSkirts,
        .skirtCount = ARRAY_COUNT(sNewMauvilleSkirts),
        .shadowCorner = NEWMAUVILLE_METATILE_SKIRT_CORNER,

        // Sparse on purpose. Vanilla is dense because it is a designed
        // facility; a generated floor read for stairs and trainers wants the
        // wall mostly plain.
        .decor = sNewMauvilleDecor,
        .decorCount = ARRAY_COUNT(sNewMauvilleDecor),
        .decorRarity = 12,

        .species = sNewMauvilleSpecies,
        .speciesCount = ARRAY_COUNT(sNewMauvilleSpecies),
    },
    [DUNGEON_THEME_FIERYPATH] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_FIERYPATH,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
        .generator = DUNGEON_GEN_CAVE,
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
    },
    [DUNGEON_THEME_MIRAGETOWER] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_MIRAGETOWER,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
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
    },
    [DUNGEON_THEME_JUNGLE] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_JUNGLE,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
        .generator = DUNGEON_GEN_CAVE,   // the canopy tiles 1x1, unlike the woods
        .elevationFloor = DUNGEON_ELEVATION_FLOOR,
        .elevationWall = DUNGEON_ELEVATION_WALL,

        // Far more open than the cave: 12 rooms of 7-13 with 3-wide corridors
        // measures 42.7% floor against the cave's 24.0%, and still averages
        // 7.8 rooms so trainers and the exit stay well spread.
        .roomCount = 12,
        .roomMin = 7,
        .roomMax = 13,
        .corridorWidth = 3,

        .floor = JUNGLE_METATILE_GRASS,
        // Encounters come from the long grass layer, not the ground, the same
        // way they do in the woods. Plain grass is MB_NORMAL and safe to cross.
        .tallGrass = 0,
        .longGrass = JUNGLE_METATILE_LONG_GRASS,
        .stairsDown = JUNGLE_METATILE_STAIRS,
        .stairsUp = JUNGLE_METATILE_STAIRS,
        .wall =
        {
            // One edge case only. Every slot whose south neighbour is floor
            // gets the base row; all the rest are just more canopy.
            [WALL_FACE_LEFT]      = JUNGLE_METATILE_CANOPY_BASE,
            [WALL_FACE_MID]       = JUNGLE_METATILE_CANOPY_BASE,
            [WALL_FACE_RIGHT]     = JUNGLE_METATILE_CANOPY_BASE,
            [WALL_SLIVER_HORZ]    = JUNGLE_METATILE_CANOPY_BASE,
            [WALL_SLIVER_HORZ_L]  = JUNGLE_METATILE_CANOPY_BASE,
            [WALL_SLIVER_HORZ_R]  = JUNGLE_METATILE_CANOPY_BASE,
            [WALL_SLIVER_VERT_BOT]= JUNGLE_METATILE_CANOPY_BASE,
            [WALL_SLIVER_ISOLATED]= JUNGLE_METATILE_CANOPY_BASE,

            [WALL_INTERIOR_LEFT]  = JUNGLE_METATILE_CANOPY,
            [WALL_INTERIOR_MID]   = JUNGLE_METATILE_CANOPY,
            [WALL_INTERIOR_RIGHT] = JUNGLE_METATILE_CANOPY,
            [WALL_NORTH_LEFT]     = JUNGLE_METATILE_CANOPY,
            [WALL_NORTH_MID]      = JUNGLE_METATILE_CANOPY,
            [WALL_NORTH_RIGHT]    = JUNGLE_METATILE_CANOPY,
            [WALL_CORNER_OPEN_SE] = JUNGLE_METATILE_CANOPY,
            [WALL_CORNER_OPEN_SW] = JUNGLE_METATILE_CANOPY,
            [WALL_CORNER_OPEN_NW] = JUNGLE_METATILE_CANOPY,
            [WALL_CORNER_OPEN_NE] = JUNGLE_METATILE_CANOPY,
            [WALL_SLIVER_VERT]    = JUNGLE_METATILE_CANOPY,
            [WALL_SLIVER_VERT_TOP]= JUNGLE_METATILE_CANOPY,
        },

        .patches = sJunglePatches,
        .patchCount = ARRAY_COUNT(sJunglePatches),

        .decor = sJungleDecor,
        .decorCount = ARRAY_COUNT(sJungleDecor),
        .decorRarity = 2,   // texture scatter, not ornament

        .species = sJungleSpecies,
        .speciesCount = ARRAY_COUNT(sJungleSpecies),
    },
    [DUNGEON_THEME_OCEAN] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_OCEAN,
        .mapId = MAP_ROGUE_DUNGEON_FLOOR,
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
    },
    [DUNGEON_THEME_UNDERWATER] =
    {
        .layoutId = LAYOUT_ROGUE_DUNGEON_UNDERWATER,

        // The one theme that does NOT live on the shared dungeon map. Diving is
        // a map-header property and the header is read out of ROM, so it cannot
        // be faked the way the tileset swap is. See theme->mapId.
        .mapId = MAP_ROGUE_DUNGEON_UNDERWATER,

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

        // Swimmers stand in until divers exist. They are wrong - a swimmer is
        // drawn treading the surface in swimwear, and this is the seafloor -
        // but the only diving sprites in the game are the player's own
        // BRENDAN_UNDERWATER / MAY_UNDERWATER, and dressing every trainer as
        // the player is worse.
        .trainerGfx = OBJ_EVENT_GFX_SWIMMER_M,
        .trainerGfxAlt = OBJ_EVENT_GFX_SWIMMER_F,

        // No platform, unlike the ocean: the seafloor is ordinary walkable
        // ground, so Juan stands on it the way every land boss does.
        .arenaPlatform = FALSE,

        .species = sUnderwaterSpecies,
        .speciesCount = ARRAY_COUNT(sUnderwaterSpecies),
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
// That is all eight gym dungeons with a theme of their own and no repeats. The
// cycling starts at the Elite Four, where the dungeons are half as long.
static const struct RogueDungeonTheme *ThemeForFloor(u16 floor)
{
    u32 dungeon = DungeonIndexOf(floor);

    return &sDungeonThemes[dungeon % DUNGEON_THEME_COUNT];
}

// Rebuilt whenever a floor is generated, from the same seeded RNG, so a given
// floor always has the same encounter table.
//
// EWRAM_DATA explicitly: plain statics land in IWRAM, and IWRAM is by far the
// scarcer region here (~4 KB free against ~35 KB of EWRAM).
EWRAM_DATA static struct WildPokemon sDungeonWildMons[NUM_LAND_MONS_ENCOUNTER_SLOTS] = {0};
EWRAM_DATA static struct WildPokemonInfo sDungeonWildInfo = {0};

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
#define DUNGEON_MAX_GRASS_PATCHES 12
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
    u32 i;

    if (index >= ARRAY_COUNT(sRogueDungeonStarters) || slot >= PARTY_SIZE)
        return;

    mon = &gParties[B_TRAINER_PLAYER][slot];
    CreateRandomMonWithIVs(mon, sRogueDungeonStarters[index].species,
                           DUNGEON_STARTER_LEVEL, MAX_PER_STAT_IVS);

    // The elemental attack is a FALLBACK, not a guarantee to be forced in. With
    // Gen 9 learnsets at level 10, 21 of the 27 starters already know it, and
    // adding it again both wasted a slot and - once all four were full, which
    // the clamp below used to paper over - overwrote a real move with a
    // duplicate. Squirtle lost Rapid Spin for a second Water Gun.
    //
    // So: skip it if the species already knows it, and never displace an
    // existing move to make room. Every starter reaches level 10 with a
    // damaging move of its own type under these learnsets, so nothing is left
    // unable to fight.
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        if (GetMonData(mon, MON_DATA_MOVE1 + i, NULL)
            == sRogueDungeonStarters[index].move)
            break;
    }
    if (i == MAX_MON_MOVES)
    {
        for (i = 0; i < MAX_MON_MOVES; i++)
        {
            if (GetMonData(mon, MON_DATA_MOVE1 + i, NULL) == MOVE_NONE)
            {
                SetMonMoveSlot(mon, sRogueDungeonStarters[index].move, i);
                break;
            }
        }
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

// specialvar target. Returns one of the ROGUE_ACE_* results and buffers the
// species name into gStringVar1, for both the offer and the party-full refusal.
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
    StringCopy(gStringVar1, GetSpeciesName(party[sBossAceSlot].species));

    // sBossAceSpecies stays SPECIES_NONE, so RogueDungeon_GiveBossAce refuses
    // even if something did reach it.
    if (CalculatePlayerPartyCount() >= PARTY_SIZE)
        return ROGUE_ACE_PARTY_FULL;

    sBossAceSpecies = party[sBossAceSlot].species;
    return ROGUE_ACE_OFFER;
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
    CreateRandomMonWithIVs(mon, sBossAceSpecies, sBossAceLevel, MAX_PER_STAT_IVS);

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
void RogueDungeon_ResetRun(void)
{
    VarSet(VAR_ROGUE_DUNGEON_FLOOR, 0);
    VarSet(VAR_ROGUE_RUN_STATE, ROGUE_RUN_NEEDS_STARTERS);
    ZeroPlayerPartyMons();
    CalculatePlayerPartyCount();

    // Otherwise the run-start item grant stacks with whatever survived the last
    // run, and a few losses leave the player with hundreds of balls.
    ClearBag();
}

// Which map a floor's theme lives on. Almost always MAP_ROGUE_DUNGEON_FLOOR;
// see theme->mapId for why underwater cannot share it.
//
// EVERY path that puts the player on a dungeon floor has to go through this.
// Warping to the wrong one of the two maps is not a visual glitch: the map type
// is what decides whether the player arrives diving or walking, so a floor
// reached by the wrong route would be underwater art walked over on foot.
static u16 MapForFloor(u16 floor)
{
    return ThemeForFloor(floor)->mapId;
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

bool8 RogueDungeon_TryHandleWhiteOut(void)
{
    if (gMapHeader.mapLayoutId != LAYOUT_ROGUE_DUNGEON_FLOOR)
        return FALSE;

    RogueDungeon_ResetRun();

    // Back to the first floor, where the frame table will ask for starters.
    // ResetRun has already put the floor counter back to 0, so this picks that
    // floor's map rather than the one being whited out of.
    RogueDungeon_SetWarpToCurrentFloor();
    WarpIntoMap();
    return TRUE;
}

// Must be called from generation, after SeedDungeonRng, so the table is
// reproducible for a given floor.
static void BuildWildEncounterTable(u16 floor)
{
    const struct RogueDungeonTheme *theme = ThemeForFloor(floor);
    u32 scaled = FloorTargetLevel(floor);
    u32 tiers = DUNGEON_ENCOUNTER_STARTING_TIER + floor / DUNGEON_ENCOUNTER_TIER_FLOORS;
    u32 bottom, width, rotation;
    u8 level;
    u32 i;

    // Clamp before narrowing to u8, or a deep enough floor wraps.
    if (scaled > MAX_LEVEL - DUNGEON_ENCOUNTER_LEVEL_SPREAD)
        scaled = MAX_LEVEL - DUNGEON_ENCOUNTER_LEVEL_SPREAD;
    level = scaled;

    if (tiers > theme->speciesCount)
        tiers = theme->speciesCount;

    // Window rather than prefix, so the weakest species retire with depth.
    bottom = (tiers > DUNGEON_ENCOUNTER_WINDOW) ? tiers - DUNGEON_ENCOUNTER_WINDOW : 0;
    width = tiers - bottom;
    rotation = DungeonRandom() % width;

    // Dealt round-robin, not drawn independently per slot. Encounter slot
    // weights are steeply uneven (20/20/10/10/...), so independent draws let one
    // species take both 20% slots and dominate the floor. The rotation varies
    // which species lands in the common slots from floor to floor.
    for (i = 0; i < NUM_LAND_MONS_ENCOUNTER_SLOTS; i++)
    {
        sDungeonWildMons[i].species = theme->species[bottom + (i + rotation) % width];
        sDungeonWildMons[i].minLevel = level;
        sDungeonWildMons[i].maxLevel = level + DUNGEON_ENCOUNTER_LEVEL_SPREAD;
    }

    // encounterRate is read straight off the static table, not from here, so
    // this value is only a sane fallback.
    sDungeonWildInfo.encounterRate = 10;
    sDungeonWildInfo.wildPokemon = sDungeonWildMons;
}

// Hooked into TryGenerateWildMon. Returning NULL leaves the caller on the
// ordinary static table.
const struct WildPokemonInfo *RogueDungeon_GetWildMonInfo(enum WildPokemonArea area)
{
    if (gMapHeader.mapLayoutId != LAYOUT_ROGUE_DUNGEON_FLOOR)
        return NULL;
    if (area != WILD_AREA_LAND)
        return NULL;

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
        // Radius varies either side of the nominal so blobs do not all read as
        // the same stamp repeated.
        blobs[i].rx = layer->radius + ((a >> 8) % 3) - 1;
        blobs[i].ry = layer->radius + ((b >> 8) % 3) - 1;
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
                SetBlock(map, x, y,
                         MakeBlock(layer->tile[slot], 0, theme->elevationFloor));
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
            u16 metatile;

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

    // All three are cosmetic and belong here rather than at the call sites:
    // arena floors return early from WriteFloorBlocks, and this is the one
    // point every cave-generator path passes through. Running before the stairs
    // are placed also means none of them can paint over the exit.
    //
    // Order matters. Patches lay the floor region down first, then a wall's own
    // skirt paints over it where a theme has both, then decor swaps individual
    // blocks. Decor keys on the painted metatile, so it sees patch tiles as
    // their own thing and will not put a floor variant on a sand drift.
    ApplyFloorPatches(map, theme, VarGet(VAR_ROGUE_DUNGEON_SEED));
    ApplySkirts(map, theme);
    ApplyDecor(map, theme, VarGet(VAR_ROGUE_DUNGEON_SEED));
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
        w &= ~1;
        h &= ~1;
    }

    x0 = (DUNGEON_WIDTH - w) / 2;
    y0 = (DUNGEON_HEIGHT - h) / 2;
    if (theme->generator == DUNGEON_GEN_WOODS)
    {
        x0 &= ~1;
        y0 &= ~1;
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
        sTrainerIds[0] = sDungeonBosses[dungeon % ARRAY_COUNT(sDungeonBosses)];
        sTrainerGfx[0] = sDungeonBossGfx[dungeon % ARRAY_COUNT(sDungeonBossGfx)];
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
    u32 dungeon = DungeonIndexOf(VarGet(VAR_ROGUE_DUNGEON_FLOOR));
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
static u16 PickTrainerForLevel(u8 target)
{
    u32 i, first = 0, last = 0;
    u32 window;

    for (window = 3; window < 64; window += 4)
    {
        bool8 found = FALSE;

        for (i = 0; i < ARRAY_COUNT(sRogueDungeonTrainers); i++)
        {
            u32 level = sRogueDungeonTrainers[i].avgLevel;

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
            return sRogueDungeonTrainers[first + (DungeonRandom() % (last - first + 1))].trainerId;
    }

    return sRogueDungeonTrainers[0].trainerId;
}

// Trainers stand in rooms the player does not start in, so the first room stays
// a safe landing spot.
static void PlaceTrainers(u16 floor)
{
    const struct RogueDungeonTheme *theme = ThemeForFloor(floor);
    u32 target = FloorTargetLevel(floor);
    u32 count = 1 + floor / DUNGEON_TRAINER_FLOORS_PER_EXTRA;
    u16 trainerId, gfx;
    u32 j;
    u32 i;

    sTrainerCount = 0;

    if (sRoomCount < 2)
        return;
    if (count > DUNGEON_MAX_TRAINERS)
        count = DUNGEON_MAX_TRAINERS;

    for (i = 0; i < count; i++)
    {
        u32 room = 1 + (DungeonRandom() % (sRoomCount - 1));
        u8 x = sRooms[room].x + (DungeonRandom() % sRooms[room].w);
        u8 y = sRooms[room].y + (DungeonRandom() % sRooms[room].h);

        // Never on the exit, or the player cannot reach it without fighting.
        if (x == sStairsX && y == sStairsY)
            continue;

        sTrainerX[sTrainerCount] = x;
        sTrainerY[sTrainerCount] = y;
        // Distinct ids only. The defeat flag is derived from the trainer id, so
        // two slots sharing one would both be marked beaten by a single fight,
        // leaving a trainer standing that refuses to battle.
        trainerId = PickTrainerForLevel(target);

        for (j = 0; j < sTrainerCount; j++)
        {
            if (sTrainerIds[j] == trainerId)
                break;
        }
        if (j != sTrainerCount)
            continue;

        sTrainerIds[sTrainerCount] = trainerId;

        // Alternate the two so a floor is not populated by one repeated figure.
        gfx = (sTrainerCount & 1) && theme->trainerGfxAlt ? theme->trainerGfxAlt
                                                          : theme->trainerGfx;
        sTrainerGfx[sTrainerCount] = gfx ? gfx : OBJ_EVENT_GFX_HIKER;
        sTrainerCount++;
    }
}

// Everything a floor is, derived from its seed. Touches no map memory, so it
// can run at object-event-template load time - which happens before the map is
// generated, and is where trainers have to be placed.
static void PrepareFloor(u16 seed)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    const struct RogueDungeonTheme *theme = ThemeForFloor(floor);
    u32 unit = (theme->generator == DUNGEON_GEN_WOODS) ? 2 : 1;
    u32 gridW = DUNGEON_WIDTH / unit;
    u32 gridH = DUNGEON_HEIGHT / unit;
    u32 rmin = (unit == 2) ? 3 : DUNGEON_ROOM_MIN;
    u32 rmax = (unit == 2) ? 5 : DUNGEON_ROOM_MAX;
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

    SeedDungeonRng(seed);
    sRoomCount = 0;
    sTrainerCount = 0;

    if (RogueDungeon_IsBossFloor(floor))
    {
        PrepareArenaFloor(floor);
        BuildWildEncounterTable(floor);
        sFloorPrepared = TRUE;
        return;
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

    BuildWildEncounterTable(floor);
    PlaceTrainers(floor);
    sFloorPrepared = TRUE;
}

// Stamps one 2x2 cell. Woods trees are 2x2 blocks on even coordinates, so the
// generator works in whole cells and never needs an autotile pass.
static void StampCell(u16 *map, s32 cx, s32 cy, const struct RogueDungeonTheme *theme,
                      bool8 open, u16 floorMetatile, bool8 openBelow,
                      bool8 treeBelow)
{
    s32 x = cx * 2, y = cy * 2;

    if (open)
    {
        u16 lower = floorMetatile;

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

        SetBlock(map, x,     y,     MakeBlock(floorMetatile, 0, theme->elevationFloor));
        SetBlock(map, x + 1, y,     MakeBlock(floorMetatile, 0, theme->elevationFloor));
        SetBlock(map, x,     y + 1, MakeBlock(lower, 0, theme->elevationFloor));
        SetBlock(map, x + 1, y + 1, MakeBlock(lower, 0, theme->elevationFloor));
    }
    else
    {
        // Where a tree mass ends, its bottom row becomes the ground-contact row
        // instead of the trunk row, which is what vanilla does everywhere.
        u16 bl = openBelow ? theme->stamp[STAMP_BASE_L] : theme->stamp[STAMP_BL];
        u16 br = openBelow ? theme->stamp[STAMP_BASE_R] : theme->stamp[STAMP_BR];

        SetBlock(map, x,     y,     MakeBlock(theme->stamp[STAMP_TL], 1, theme->elevationWall));
        SetBlock(map, x + 1, y,     MakeBlock(theme->stamp[STAMP_TR], 1, theme->elevationWall));
        SetBlock(map, x,     y + 1, MakeBlock(bl, 1, theme->elevationWall));
        SetBlock(map, x + 1, y + 1, MakeBlock(br, 1, theme->elevationWall));
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
        for (cy = 0; cy < sRooms[i].h / 2; cy++)
            for (cx = 0; cx < sRooms[i].w / 2; cx++)
                OpenCell(sRooms[i].x / 2 + cx, sRooms[i].y / 2 + cy);

    // Corridors, walked in cell space so they stay a whole stamp wide. Same
    // centre-to-centre chaining as the cave, which keeps every room connected.
    for (i = 1; i < sRoomCount; i++)
    {
        s32 x0 = (sRooms[i - 1].x + sRooms[i - 1].w / 2) / 2;
        s32 y0 = (sRooms[i - 1].y + sRooms[i - 1].h / 2) / 2;
        s32 x1 = (sRooms[i].x + sRooms[i].w / 2) / 2;
        s32 y1 = (sRooms[i].y + sRooms[i].h / 2) / 2;

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

    // Woods stamps whole 2x2 cells and needs no autotile pass at all.
    if (theme->generator == DUNGEON_GEN_WOODS)
    {
        WriteWoodsBlocks(backupMapData, theme);
        if (sRoomCount != 0 && !RogueDungeon_IsBossFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR)))
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

    VarSet(VAR_ROGUE_DUNGEON_SEED, seed);
    FlagClear(FLAG_ROGUE_BOSS_REWARD_TAKEN);
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
    bool8 onPlatform = theme->arenaPlatform && RogueDungeon_IsBossFloor(floor);

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
            // A trainer standing on its own platform is talk-only: sight would
            // walk it off the rock and leave it on the water for good.
            templates[i].trainerType = onPlatform ? TRAINER_TYPE_NONE
                                                  : TRAINER_TYPE_NORMAL;
            templates[i].trainerRange_berryTreeId = DUNGEON_TRAINER_SIGHT_RANGE;
            templates[i].script = RogueDungeonFloor_EventScript_Trainer;
            templates[i].flagId = 0;
        }
        else
        {
            templates[i].flagId = FLAG_ROGUE_OBJECT_UNUSED;
        }
    }
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

    // Mirrors BattleSetup_ConfigureFacilityTrainerBattle: when two trainers
    // spot the player at once this runs twice, and the second call must fill
    // slot B without wiping slot A.
    if (gApproachingTrainerId != 0)
    {
        TRAINER_BATTLE_PARAM.playMusicB = TRUE;
        TRAINER_BATTLE_PARAM.objEventLocalIdB = gSpecialVar_LastTalked;
        TRAINER_BATTLE_PARAM.opponentB = trainerId;
        TRAINER_BATTLE_PARAM.introTextB = (u8 *)RogueDungeonFloor_Text_TrainerIntro;
        TRAINER_BATTLE_PARAM.defeatTextB = (u8 *)RogueDungeonFloor_Text_TrainerDefeat;
        TRAINER_BATTLE_PARAM.battleScriptRetAddrB = (u8 *)endScript;
        return;
    }

    InitTrainerBattleParameter();
    TRAINER_BATTLE_PARAM.mode = TRAINER_BATTLE_SINGLE;
    TRAINER_BATTLE_PARAM.playMusicA = TRUE;
    TRAINER_BATTLE_PARAM.objEventLocalIdA = gSpecialVar_LastTalked;
    TRAINER_BATTLE_PARAM.opponentA = trainerId;
    TRAINER_BATTLE_PARAM.introTextA = (u8 *)RogueDungeonFloor_Text_TrainerIntro;
    TRAINER_BATTLE_PARAM.defeatTextA = (u8 *)RogueDungeonFloor_Text_TrainerDefeat;
    TRAINER_BATTLE_PARAM.battleScriptRetAddrA = (u8 *)endScript;

    SetMapVarsToTrainerA();

    // The other exit: talking to an already-beaten trainer skips the battle and
    // leaves via gotopostbattlescript, which reads this one instead.
    SetTrainerBattleEndScript(endScript);
}

void GenerateRogueDungeonFloor(u16 *backupMapData, bool8 setPlayerPosition)
{
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
