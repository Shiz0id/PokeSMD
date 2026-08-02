#include "global.h"
#include "event_data.h"
#include "fieldmap.h"
#include "random.h"
#include "script.h"
#include "pokemon.h"
#include "wild_encounter.h"
#include "constants/items.h"
#include "constants/layouts.h"
#include "constants/moves.h"
#include "constants/vars.h"
#include "constants/wild_encounter.h"
#include "rogue_dungeon.h"

extern const u8 RogueDungeonFloor_EventScript_Stairs[];

// Ordered weakest to strongest. A floor draws only from the prefix its depth
// has unlocked, so early floors stay tame and later ones can roll evolved
// forms. The static table in wild_encounters.json is a placeholder that this
// replaces at runtime.
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
    u8 level = DUNGEON_ENCOUNTER_BASE_LEVEL + floor * DUNGEON_ENCOUNTER_LEVEL_STEP;
    u8 tiers = DUNGEON_ENCOUNTER_STARTING_TIER + floor;
    u32 i;

    if (level > MAX_LEVEL - DUNGEON_ENCOUNTER_LEVEL_SPREAD)
        level = MAX_LEVEL - DUNGEON_ENCOUNTER_LEVEL_SPREAD;
    if (tiers > ARRAY_COUNT(sDungeonSpeciesPool))
        tiers = ARRAY_COUNT(sDungeonSpeciesPool);

    for (i = 0; i < NUM_LAND_MONS_ENCOUNTER_SLOTS; i++)
    {
        sDungeonWildMons[i].species = sDungeonSpeciesPool[DungeonRandom() % tiers];
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

            if (openSouth)
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

void GenerateRogueDungeonFloor(u16 *backupMapData, bool8 setPlayerPosition)
{
    struct DungeonRoom rooms[DUNGEON_MAX_ROOMS];
    u8 roomCount = 0;
    // Placeholder art; ApplyWallAutotiling picks the real metatile at the end.
    // Only the collision bit matters during carving.
    u16 wallBlock = MakeBlock(DUNGEON_METATILE_WALL_INTERIOR_MID, 1, DUNGEON_ELEVATION_WALL);
    s32 x, y, i, attempt;
    u16 seed;

    // setPlayerPosition distinguishes the two callers. FALSE is LoadMapFromWarp
    // - the player is entering, so roll a new floor. TRUE is CB2_ContinueSavedGame
    // - reuse the stored seed so a reload reproduces the floor the player saved
    // on, rather than dropping them inside solid rock.
    if (setPlayerPosition == FALSE)
    {
        seed = Random();
        VarSet(VAR_ROGUE_DUNGEON_SEED, seed);
    }
    else
    {
        seed = VarGet(VAR_ROGUE_DUNGEON_SEED);
    }

    SeedDungeonRng(seed);

    gBackupMapLayout.map = backupMapData;
    gBackupMapLayout.width = DUNGEON_WIDTH + MAP_OFFSET_W;
    gBackupMapLayout.height = DUNGEON_HEIGHT + MAP_OFFSET_H;

    for (y = 0; y < DUNGEON_HEIGHT; y++)
        for (x = 0; x < DUNGEON_WIDTH; x++)
            SetBlock(backupMapData, x, y, wallBlock);

    // Rejection-sample non-overlapping rooms. A fixed attempt budget keeps this
    // bounded; falling short of DUNGEON_MAX_ROOMS is fine.
    for (attempt = 0; attempt < 64 && roomCount < DUNGEON_MAX_ROOMS; attempt++)
    {
        struct DungeonRoom room;
        bool8 clear = TRUE;

        room.w = DUNGEON_ROOM_MIN + (DungeonRandom() % (DUNGEON_ROOM_MAX - DUNGEON_ROOM_MIN + 1));
        room.h = DUNGEON_ROOM_MIN + (DungeonRandom() % (DUNGEON_ROOM_MAX - DUNGEON_ROOM_MIN + 1));
        room.x = 1 + (DungeonRandom() % (DUNGEON_WIDTH  - room.w - 2));
        room.y = 1 + (DungeonRandom() % (DUNGEON_HEIGHT - room.h - 2));

        for (i = 0; i < roomCount; i++)
        {
            if (RoomsOverlap(&room, &rooms[i]))
            {
                clear = FALSE;
                break;
            }
        }

        if (clear)
            rooms[roomCount++] = room;
    }

    for (i = 0; i < roomCount; i++)
    {
        for (y = 0; y < rooms[i].h; y++)
            for (x = 0; x < rooms[i].w; x++)
                CarveFloor(backupMapData, rooms[i].x + x, rooms[i].y + y);
    }

    // Chain the rooms centre-to-centre so the floor is always fully connected.
    for (i = 1; i < roomCount; i++)
    {
        CarveCorridor(backupMapData,
                      rooms[i - 1].x + rooms[i - 1].w / 2,
                      rooms[i - 1].y + rooms[i - 1].h / 2,
                      rooms[i].x + rooms[i].w / 2,
                      rooms[i].y + rooms[i].h / 2);
    }

    // Exactly one exit per floor. Placed in a room the player does not start
    // in, so reaching it means actually traversing the floor. Rooms are always
    // connected, so it is always reachable.
    if (roomCount != 0)
    {
        u8 room = (roomCount > 1) ? 1 + (DungeonRandom() % (roomCount - 1)) : 0;
        u16 stairs = (DungeonRandom() & 1) ? DUNGEON_METATILE_STAIRS_DOWN
                                           : DUNGEON_METATILE_STAIRS_UP;

        SetBlock(backupMapData,
                 rooms[room].x + (DungeonRandom() % rooms[room].w),
                 rooms[room].y + (DungeonRandom() % rooms[room].h),
                 MakeBlock(stairs, 0, DUNGEON_ELEVATION_FLOOR));
    }

    // Runs last, but only rewrites blocks whose collision bit is set, so the
    // stairs tile is left alone.
    ApplyWallAutotiling(backupMapData);

    if (setPlayerPosition == FALSE && roomCount != 0)
    {
        gSaveBlock1Ptr->pos.x = rooms[0].x + rooms[0].w / 2;
        gSaveBlock1Ptr->pos.y = rooms[0].y + rooms[0].h / 2;
    }

    // Last, so they do not shift the RNG sequence the layout depends on -
    // adding these must not change the floors existing seeds produce.
    BuildWildEncounterTable(VarGet(VAR_ROGUE_DUNGEON_FLOOR));

    if (setPlayerPosition == FALSE && !FlagGet(FLAG_ROGUE_STARTER_GIVEN))
    {
        FlagSet(FLAG_ROGUE_STARTER_GIVEN);
        GiveStarterTeam();
    }
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
