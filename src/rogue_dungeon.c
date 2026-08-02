#include "global.h"
#include "fieldmap.h"
#include "random.h"
#include "rogue_dungeon.h"

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
            bool8 isFace;
            u16 metatile;

            if (!IsWallAt(map, x, y))
                continue;

            // a wall with floor directly below it is the face the player sees
            isFace = !IsWallAt(map, x, y + 1);

            if (!IsWallAt(map, x - 1, y))
                metatile = isFace ? DUNGEON_METATILE_WALL_FACE_LEFT
                                  : DUNGEON_METATILE_WALL_INTERIOR_LEFT;
            else if (!IsWallAt(map, x + 1, y))
                metatile = isFace ? DUNGEON_METATILE_WALL_FACE_RIGHT
                                  : DUNGEON_METATILE_WALL_INTERIOR_RIGHT;
            else
                metatile = isFace ? DUNGEON_METATILE_WALL_FACE_MID
                                  : DUNGEON_METATILE_WALL_INTERIOR_MID;

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

        room.w = DUNGEON_ROOM_MIN + (Random() % (DUNGEON_ROOM_MAX - DUNGEON_ROOM_MIN + 1));
        room.h = DUNGEON_ROOM_MIN + (Random() % (DUNGEON_ROOM_MAX - DUNGEON_ROOM_MIN + 1));
        room.x = 1 + (Random() % (DUNGEON_WIDTH  - room.w - 2));
        room.y = 1 + (Random() % (DUNGEON_HEIGHT - room.h - 2));

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

    ApplyWallAutotiling(backupMapData);

    if (setPlayerPosition == FALSE && roomCount != 0)
    {
        gSaveBlock1Ptr->pos.x = rooms[0].x + rooms[0].w / 2;
        gSaveBlock1Ptr->pos.y = rooms[0].y + rooms[0].h / 2;
    }
}
