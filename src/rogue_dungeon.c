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
#include "rogue_dungeon.h"

extern const u8 RogueDungeonFloor_EventScript_Stairs[];
extern const u8 RogueDungeonFloor_EventScript_Trainer[];
extern const u8 RogueDungeonFloor_EventScript_TrainerDone[];
extern const u8 RogueDungeonFloor_EventScript_BossDone[];
extern const u8 RogueDungeonFloor_Text_TrainerIntro[];
extern const u8 RogueDungeonFloor_Text_TrainerDefeat[];

static u16 PickTrainerForLevel(u8 target);

// Target level for a floor. The rate slows past the gym stretch because the
// stock game does: eight gyms span levels 15 to 46, but the Elite Four and
// Champion only span 46 to 58. One rate cannot fit both.
static u32 FloorTargetLevel(u16 floor)
{
    u32 level = DUNGEON_ENCOUNTER_BASE_LEVEL;

    if (floor <= DUNGEON_GYM_FLOORS)
        return level + ((u32)floor * DUNGEON_ENCOUNTER_LEVEL_NUM)
                     / DUNGEON_ENCOUNTER_LEVEL_DEN;

    level += ((u32)DUNGEON_GYM_FLOORS * DUNGEON_ENCOUNTER_LEVEL_NUM)
           / DUNGEON_ENCOUNTER_LEVEL_DEN;
    level += ((u32)(floor - DUNGEON_GYM_FLOORS) * DUNGEON_ENCOUNTER_LATE_NUM)
           / DUNGEON_ENCOUNTER_LEVEL_DEN;
    return level;
}

// Ordered weakest to strongest. A floor draws from a window of this list that
// slides with depth, so early floors stay tame, later ones roll evolved forms,
// and the weakest species retire instead of lingering forever. The static table
// in wild_encounters.json is a placeholder that this replaces at runtime.
static const u16 sDungeonSpeciesPool[] =
{
    SPECIES_ZUBAT,   SPECIES_WHISMUR,  SPECIES_GEODUDE,  SPECIES_MAKUHITA,
    SPECIES_ARON,    SPECIES_NOSEPASS, SPECIES_SABLEYE,  SPECIES_MAWILE,
    SPECIES_GOLBAT,  SPECIES_LOUDRED,  SPECIES_GRAVELER, SPECIES_LAIRON,
    SPECIES_ONIX,    SPECIES_HARIYAMA, SPECIES_SHUCKLE,  SPECIES_CLAYDOL,
};

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

// Placeholder starting party so the dungeon is testable before run structure
// exists. Steven's Meteor Falls team, copied from TRAINER_STEVEN in
// src/data/trainers.party.
struct DungeonStarterMon
{
    u16 species;
    u8 level;
    u16 item;
    u16 moves[MAX_MON_MOVES];
};

static const struct DungeonStarterMon sStarterTeam[] =
{
    { SPECIES_SKARMORY,  77, ITEM_NONE,
      { MOVE_TOXIC, MOVE_AERIAL_ACE, MOVE_SPIKES, MOVE_STEEL_WING } },
    { SPECIES_CLAYDOL,   75, ITEM_NONE,
      { MOVE_REFLECT, MOVE_LIGHT_SCREEN, MOVE_ANCIENT_POWER, MOVE_EARTHQUAKE } },
    { SPECIES_AGGRON,    76, ITEM_NONE,
      { MOVE_THUNDER, MOVE_EARTHQUAKE, MOVE_SOLAR_BEAM, MOVE_DRAGON_CLAW } },
    { SPECIES_CRADILY,   76, ITEM_NONE,
      { MOVE_GIGA_DRAIN, MOVE_ANCIENT_POWER, MOVE_INGRAIN, MOVE_CONFUSE_RAY } },
    { SPECIES_ARMALDO,   76, ITEM_NONE,
      { MOVE_WATER_PULSE, MOVE_ANCIENT_POWER, MOVE_AERIAL_ACE, MOVE_SLASH } },
    { SPECIES_METAGROSS, 78, ITEM_SITRUS_BERRY,
      { MOVE_EARTHQUAKE, MOVE_PSYCHIC, MOVE_METEOR_MASH, MOVE_SHADOW_BALL } },
};

// Replaces the party outright. Guarded by FLAG_ROGUE_STARTER_GIVEN so it
// happens once rather than wiping progress on every entry.
static void GiveStarterTeam(void)
{
    u32 i, j;

    ZeroPlayerPartyMons();

    for (i = 0; i < ARRAY_COUNT(sStarterTeam); i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];
        u16 item = sStarterTeam[i].item;

        CreateRandomMonWithIVs(mon, sStarterTeam[i].species, sStarterTeam[i].level,
                               MAX_PER_STAT_IVS);

        for (j = 0; j < MAX_MON_MOVES; j++)
            SetMonMoveSlot(mon, sStarterTeam[i].moves[j], j);

        if (item != ITEM_NONE)
            SetMonData(mon, MON_DATA_HELD_ITEM, &item);

        CalculateMonStats(mon);
    }

    CalculatePlayerPartyCount();
}

// Must be called from generation, after SeedDungeonRng, so the table is
// reproducible for a given floor.
static void BuildWildEncounterTable(u16 floor)
{
    u32 scaled = FloorTargetLevel(floor);
    u32 tiers = DUNGEON_ENCOUNTER_STARTING_TIER + floor / DUNGEON_ENCOUNTER_TIER_FLOORS;
    u32 bottom, width, rotation;
    u8 level;
    u32 i;

    // Clamp before narrowing to u8, or a deep enough floor wraps.
    if (scaled > MAX_LEVEL - DUNGEON_ENCOUNTER_LEVEL_SPREAD)
        scaled = MAX_LEVEL - DUNGEON_ENCOUNTER_LEVEL_SPREAD;
    level = scaled;

    if (tiers > ARRAY_COUNT(sDungeonSpeciesPool))
        tiers = ARRAY_COUNT(sDungeonSpeciesPool);

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
        sDungeonWildMons[i].species = sDungeonSpeciesPool[bottom + (i + rotation) % width];
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

static void CarveFloor(u16 *map, s32 x, s32 y)
{
    SetBlock(map, x, y, MakeBlock(DUNGEON_METATILE_FLOOR, 0, DUNGEON_ELEVATION_FLOOR));
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

// Second pass over the carved grid, choosing each wall's art from its
// neighbours. Must run after all carving is done.
static void ApplyWallAutotiling(u16 *map)
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
                metatile = DUNGEON_METATILE_WALL_SLIVER_ISOLATED;
            }
            else if (openNorth && openSouth)
            {
                if (openWest)
                    metatile = DUNGEON_METATILE_WALL_SLIVER_HORZ_L;
                else if (openEast)
                    metatile = DUNGEON_METATILE_WALL_SLIVER_HORZ_R;
                else
                    metatile = DUNGEON_METATILE_WALL_SLIVER_HORZ;
            }
            else if (openWest && openEast)
            {
                if (openNorth)
                    metatile = DUNGEON_METATILE_WALL_SLIVER_VERT_TOP;
                else if (openSouth)
                    metatile = DUNGEON_METATILE_WALL_SLIVER_VERT_BOT;
                else
                    metatile = DUNGEON_METATILE_WALL_SLIVER_VERT;
            }
            else if (openSouth)
            {
                // Floor below, so this is the wall face the camera sees. Takes
                // priority over every other edge - it is the most visible one.
                if (openWest)
                    metatile = DUNGEON_METATILE_WALL_FACE_LEFT;
                else if (openEast)
                    metatile = DUNGEON_METATILE_WALL_FACE_RIGHT;
                else
                    metatile = DUNGEON_METATILE_WALL_FACE_MID;
            }
            else if (openNorth)
            {
                // Floor above - the bottom boundary of a room.
                if (openWest)
                    metatile = DUNGEON_METATILE_WALL_NORTH_LEFT;
                else if (openEast)
                    metatile = DUNGEON_METATILE_WALL_NORTH_RIGHT;
                else
                    metatile = DUNGEON_METATILE_WALL_NORTH_MID;
            }
            else if (openWest)
            {
                metatile = DUNGEON_METATILE_WALL_INTERIOR_LEFT;
            }
            else if (openEast)
            {
                metatile = DUNGEON_METATILE_WALL_INTERIOR_RIGHT;
            }
            else if (!IsWallAt(map, x + 1, y + 1))
            {
                // Every cardinal is wall, so only a diagonal can be open. These
                // are the outer corners of a room; without them the outline
                // notches at the corners.
                metatile = DUNGEON_METATILE_WALL_CORNER_NW;
            }
            else if (!IsWallAt(map, x - 1, y + 1))
            {
                metatile = DUNGEON_METATILE_WALL_CORNER_NE;
            }
            else if (!IsWallAt(map, x - 1, y - 1) || !IsWallAt(map, x + 1, y - 1))
            {
                metatile = DUNGEON_METATILE_WALL_CORNER_SOUTH;
            }
            else
            {
                metatile = DUNGEON_METATILE_WALL_INTERIOR_MID;
            }

            SetBlock(map, x, y, MakeBlock(metatile, 1, DUNGEON_ELEVATION_WALL));
        }
    }
}

static bool8 RoomsOverlap(const struct DungeonRoom *a, const struct DungeonRoom *b)
{
    // one block of padding so rooms never share a wall
    return !(a->x + a->w + 1 < b->x || b->x + b->w + 1 < a->x
          || a->y + a->h + 1 < b->y || b->y + b->h + 1 < a->y);
}

static void CarveCorridor(u16 *map, s32 x0, s32 y0, s32 x1, s32 y1)
{
    s32 x = x0, y = y0;

    while (x != x1)
    {
        CarveFloor(map, x, y);
        x += (x1 > x) ? 1 : -1;
    }
    while (y != y1)
    {
        CarveFloor(map, x, y);
        y += (y1 > y) ? 1 : -1;
    }
    CarveFloor(map, x, y);
}

// One major battle per dungeon, in stock order: the eight gym leaders, then the
// Elite Four and the Champion. The _1 variants are the base gym battles rather
// than the rematch tiers; the Elite Four have no such variants.
static const u16 sDungeonBosses[] =
{
    TRAINER_ROXANNE_1, TRAINER_BRAWLY_1, TRAINER_WATTSON_1, TRAINER_FLANNERY_1,
    TRAINER_NORMAN_1,  TRAINER_WINONA_1, TRAINER_TATE_AND_LIZA_1, TRAINER_JUAN_1,
    TRAINER_SIDNEY, TRAINER_PHOEBE, TRAINER_GLACIA, TRAINER_DRAKE, TRAINER_WALLACE,
};

// Parallel to sDungeonBosses, so the boss looks like who it is.
static const u16 sDungeonBossGfx[] =
{
    OBJ_EVENT_GFX_ROXANNE, OBJ_EVENT_GFX_BRAWLY, OBJ_EVENT_GFX_WATTSON,
    OBJ_EVENT_GFX_FLANNERY, OBJ_EVENT_GFX_NORMAN, OBJ_EVENT_GFX_WINONA,
    OBJ_EVENT_GFX_TATE, OBJ_EVENT_GFX_JUAN,
    OBJ_EVENT_GFX_SIDNEY, OBJ_EVENT_GFX_PHOEBE, OBJ_EVENT_GFX_GLACIA,
    OBJ_EVENT_GFX_DRAKE, OBJ_EVENT_GFX_WALLACE,
};

// Mini bosses. Team Aqua and Magma grunts, chosen from the seed.
static const u16 sMiniBosses[] =
{
    TRAINER_GRUNT_AQUA_HIDEOUT_1, TRAINER_GRUNT_AQUA_HIDEOUT_2,
    TRAINER_GRUNT_AQUA_HIDEOUT_3, TRAINER_GRUNT_AQUA_HIDEOUT_4,
    TRAINER_GRUNT_SEAFLOOR_CAVERN_1, TRAINER_GRUNT_SEAFLOOR_CAVERN_2,
};

bool8 RogueDungeon_IsBossFloor(u16 floor)
{
    u32 within = DungeonFloorWithin(floor);

    return within == DUNGEON_MINIBOSS_FLOOR || within == DUNGEON_BOSS_FLOOR;
}

// Boss floors skip rooms and corridors entirely: a single centred arena, the
// boss standing in the open, and no exit at all until it is beaten.
static void PrepareArenaFloor(u16 floor)
{
    u32 within = DungeonFloorWithin(floor);
    u32 dungeon = DungeonIndexOf(floor);
    u8 x0 = (DUNGEON_WIDTH - DUNGEON_ARENA_WIDTH) / 2;
    u8 y0 = (DUNGEON_HEIGHT - DUNGEON_ARENA_HEIGHT) / 2;

    sRoomCount = 1;
    sRooms[0].x = x0;
    sRooms[0].y = y0;
    sRooms[0].w = DUNGEON_ARENA_WIDTH;
    sRooms[0].h = DUNGEON_ARENA_HEIGHT;

    // Player at the south end, boss at the north, so the two face off across
    // the arena. The boss is deliberately NOT in a chokepoint - a defeated
    // trainer object still blocks movement, and would wall the exit off.
    sSpawnX = x0 + DUNGEON_ARENA_WIDTH / 2;
    sSpawnY = y0 + DUNGEON_ARENA_HEIGHT - 2;

    sStairsX = x0 + DUNGEON_ARENA_WIDTH / 2;
    sStairsY = y0 + 1;
    sStairsMetatile = DUNGEON_METATILE_STAIRS_DOWN;

    sTrainerCount = 1;
    sTrainerX[0] = x0 + DUNGEON_ARENA_WIDTH / 2;
    sTrainerY[0] = y0 + 3;

    if (within == DUNGEON_BOSS_FLOOR)
    {
        sTrainerIds[0] = sDungeonBosses[dungeon % ARRAY_COUNT(sDungeonBosses)];
        sTrainerGfx[0] = sDungeonBossGfx[dungeon % ARRAY_COUNT(sDungeonBossGfx)];
    }
    else if (dungeon < DUNGEON_GYM_DUNGEONS)
    {
        sTrainerIds[0] = sMiniBosses[DungeonRandom() % ARRAY_COUNT(sMiniBosses)];
        sTrainerGfx[0] = OBJ_EVENT_GFX_AQUA_MEMBER_M;
    }
    else
    {
        // Past the gyms a team grunt would be twenty levels underlevelled, so
        // the mini boss becomes a strong ordinary trainer instead.
        sTrainerIds[0] = PickTrainerForLevel(FloorTargetLevel(floor) + 2);
        sTrainerGfx[0] = OBJ_EVENT_GFX_HIKER;
    }
}

// specialvar target. The last floor of a dungeon sends the player to the rest
// stop to heal rather than straight down another set of stairs.
void RogueDungeon_IsDungeonEndFloor(void)
{
    gSpecialVar_Result =
        DungeonFloorWithin(VarGet(VAR_ROGUE_DUNGEON_FLOOR)) == DUNGEON_BOSS_FLOOR;
}

// Called from the boss post-battle script. The exit does not exist until now,
// which is what forces the fight - there is no other way off an arena floor.
void RogueDungeon_OnBossDefeated(void)
{
    // Gym floors warp to the rest stop instead of opening an exit, so drawing
    // one here would give the player a way to skip the heal.
    if (DungeonFloorWithin(VarGet(VAR_ROGUE_DUNGEON_FLOOR)) == DUNGEON_BOSS_FLOOR)
        return;

    MapGridSetMetatileEntryAt(sStairsX + MAP_OFFSET, sStairsY + MAP_OFFSET,
                              MakeBlock(sStairsMetatile, 0, DUNGEON_ELEVATION_FLOOR));
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
    u32 target = FloorTargetLevel(floor);
    u32 count = 1 + floor / DUNGEON_TRAINER_FLOORS_PER_EXTRA;
    u16 trainerId;
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
        sTrainerGfx[sTrainerCount] = OBJ_EVENT_GFX_HIKER;
        sTrainerCount++;
    }
}

// Everything a floor is, derived from its seed. Touches no map memory, so it
// can run at object-event-template load time - which happens before the map is
// generated, and is where trainers have to be placed.
static void PrepareFloor(u16 seed)
{
    u16 floor = VarGet(VAR_ROGUE_DUNGEON_FLOOR);
    s32 i, attempt;

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
    for (attempt = 0; attempt < 64 && sRoomCount < DUNGEON_MAX_ROOMS; attempt++)
    {
        struct DungeonRoom room;
        bool8 clear = TRUE;

        room.w = DUNGEON_ROOM_MIN + (DungeonRandom() % (DUNGEON_ROOM_MAX - DUNGEON_ROOM_MIN + 1));
        room.h = DUNGEON_ROOM_MIN + (DungeonRandom() % (DUNGEON_ROOM_MAX - DUNGEON_ROOM_MIN + 1));
        room.x = 1 + (DungeonRandom() % (DUNGEON_WIDTH  - room.w - 2));
        room.y = 1 + (DungeonRandom() % (DUNGEON_HEIGHT - room.h - 2));

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

        sStairsMetatile = (DungeonRandom() & 1) ? DUNGEON_METATILE_STAIRS_DOWN
                                                : DUNGEON_METATILE_STAIRS_UP;
        sStairsX = sRooms[room].x + (DungeonRandom() % sRooms[room].w);
        sStairsY = sRooms[room].y + (DungeonRandom() % sRooms[room].h);
    }

    if (sRoomCount != 0)
    {
        sSpawnX = sRooms[0].x + sRooms[0].w / 2;
        sSpawnY = sRooms[0].y + sRooms[0].h / 2;
    }

    BuildWildEncounterTable(floor);
    PlaceTrainers(floor);
    sFloorPrepared = TRUE;
}

// Paints the prepared floor into the map buffer. PrepareFloor must have run.
static void WriteFloorBlocks(u16 *backupMapData)
{
    // Placeholder art; ApplyWallAutotiling picks the real metatile at the end.
    // Only the collision bit matters during carving.
    u16 wallBlock = MakeBlock(DUNGEON_METATILE_WALL_INTERIOR_MID, 1, DUNGEON_ELEVATION_WALL);
    s32 x, y, i;

    gBackupMapLayout.map = backupMapData;
    gBackupMapLayout.width = DUNGEON_WIDTH + MAP_OFFSET_W;
    gBackupMapLayout.height = DUNGEON_HEIGHT + MAP_OFFSET_H;

    for (y = 0; y < DUNGEON_HEIGHT; y++)
        for (x = 0; x < DUNGEON_WIDTH; x++)
            SetBlock(backupMapData, x, y, wallBlock);

    for (i = 0; i < sRoomCount; i++)
    {
        for (y = 0; y < sRooms[i].h; y++)
            for (x = 0; x < sRooms[i].w; x++)
                CarveFloor(backupMapData, sRooms[i].x + x, sRooms[i].y + y);
    }

    // An arena floor is the single room, and its exit is not drawn until the
    // boss is beaten - see RogueDungeon_OnBossDefeated.
    if (RogueDungeon_IsBossFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR)))
    {
        ApplyWallAutotiling(backupMapData);
        return;
    }

    // Chain the rooms centre-to-centre so the floor is always fully connected.
    for (i = 1; i < sRoomCount; i++)
    {
        CarveCorridor(backupMapData,
                      sRooms[i - 1].x + sRooms[i - 1].w / 2,
                      sRooms[i - 1].y + sRooms[i - 1].h / 2,
                      sRooms[i].x + sRooms[i].w / 2,
                      sRooms[i].y + sRooms[i].h / 2);
    }

    if (sRoomCount != 0)
        SetBlock(backupMapData, sStairsX, sStairsY,
                 MakeBlock(sStairsMetatile, 0, DUNGEON_ELEVATION_FLOOR));
}

// Called from the object-event template loader, which runs before the map is
// generated. Rolls the floor early so trainer placement can see the rooms.
void RogueDungeon_PrepareNewFloor(void)
{
    u16 seed = Random();

    VarSet(VAR_ROGUE_DUNGEON_SEED, seed);
    PrepareFloor(seed);
}

// Replaces LoadObjEventTemplatesFromHeader for dungeon floors, the same way the
// Battle Pyramid and Trainer Hill do. The engine reads templates for the
// current map from the save block but takes the COUNT from ROM, so map.json
// declares DUNGEON_MAX_TRAINERS placeholders and we overwrite them here.
void RogueDungeon_LoadObjectEventTemplates(void)
{
    struct ObjectEventTemplate *templates = gSaveBlock1Ptr->objectEventTemplates;
    u32 i;

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
        templates[i].elevation = DUNGEON_ELEVATION_FLOOR;

        if (i < sTrainerCount)
        {
            templates[i].graphicsId = sTrainerGfx[i];
            templates[i].x = sTrainerX[i];
            templates[i].y = sTrainerY[i];
            templates[i].movementType = MOVEMENT_TYPE_FACE_DOWN;
            templates[i].trainerType = TRAINER_TYPE_NORMAL;
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
void RogueDungeon_SetUpTrainerBattle(void)
{
    u32 slot = gSpecialVar_LastTalked - 1;
    u16 trainerId;

    if (slot >= sTrainerCount)
        slot = 0;
    trainerId = sTrainerIds[slot];

    InitTrainerBattleParameter();
    TRAINER_BATTLE_PARAM.mode = TRAINER_BATTLE_SINGLE;
    TRAINER_BATTLE_PARAM.playMusicA = TRUE;
    TRAINER_BATTLE_PARAM.objEventLocalIdA = gSpecialVar_LastTalked;
    TRAINER_BATTLE_PARAM.opponentA = trainerId;
    TRAINER_BATTLE_PARAM.introTextA = (u8 *)RogueDungeonFloor_Text_TrainerIntro;
    TRAINER_BATTLE_PARAM.defeatTextA = (u8 *)RogueDungeonFloor_Text_TrainerDefeat;

    SetMapVarsToTrainerA();
    if (RogueDungeon_IsBossFloor(VarGet(VAR_ROGUE_DUNGEON_FLOOR)))
        SetTrainerBattleEndScript(RogueDungeonFloor_EventScript_BossDone);
    else
        SetTrainerBattleEndScript(RogueDungeonFloor_EventScript_TrainerDone);
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
            seed = Random();
            VarSet(VAR_ROGUE_DUNGEON_SEED, seed);
        }
        else
        {
            seed = VarGet(VAR_ROGUE_DUNGEON_SEED);
        }

        PrepareFloor(seed);
    }

    WriteFloorBlocks(backupMapData);

    // Runs last, but only rewrites blocks whose collision bit is set, so the
    // stairs tile is left alone.
    ApplyWallAutotiling(backupMapData);

    if (setPlayerPosition == FALSE && sRoomCount != 0)
    {
        gSaveBlock1Ptr->pos.x = sSpawnX;
        gSaveBlock1Ptr->pos.y = sSpawnY;
    }

    if (setPlayerPosition == FALSE && !FlagGet(FLAG_ROGUE_STARTER_GIVEN))
    {
        FlagSet(FLAG_ROGUE_STARTER_GIVEN);
        GiveStarterTeam();
    }

    // Consumed. The next map load must prepare afresh, or it would repaint this
    // floor instead of generating the next one.
    sFloorPrepared = FALSE;
}

// Hooked into TryStartStepBasedScript. Returning TRUE means we consumed the
// step, so nothing else gets a chance to run a script for it.
bool8 RogueDungeon_TryStartStairsScript(struct MapPosition *position)
{
    u16 metatile;

    if (gMapHeader.mapLayoutId != LAYOUT_ROGUE_DUNGEON_FLOOR)
        return FALSE;

    metatile = MapGridGetMetatileIdAt(position->x, position->y);

    if (metatile != DUNGEON_METATILE_STAIRS_DOWN
     && metatile != DUNGEON_METATILE_STAIRS_UP)
        return FALSE;

    ScriptContext_SetupScript(RogueDungeonFloor_EventScript_Stairs);
    return TRUE;
}
