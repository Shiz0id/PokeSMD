#ifndef GUARD_ROGUE_DUNGEON_H
#define GUARD_ROGUE_DUNGEON_H

// Metatile ids for the General + Cave tileset pair, derived by mining all 62
// vanilla General+Cave layouts rather than picked by eye.
//
// 0x201 is the MB_CAVE floor, so wild encounters work on it.
//
// Walls form a 3x2 autotile block. The "face" row is wall with floor directly
// below it - the part the camera actually sees - and the "interior" row is the
// bulk behind it. Columns vary by whether the neighbouring cell is open:
//
//                left    middle   right
//   interior     0x210   0x211    0x212
//   face         0x218   0x219    0x21A
//
// Evidence: 0x219 appears with walls on both sides 94% of the time (middle),
// while 0x210/0x218 favour an open west neighbour and 0x212/0x21A an open east.
#define DUNGEON_METATILE_FLOOR 0x201
#define DUNGEON_METATILE_VOID  0x200

#define DUNGEON_METATILE_WALL_INTERIOR_LEFT   0x210
#define DUNGEON_METATILE_WALL_INTERIOR_MID    0x211
#define DUNGEON_METATILE_WALL_INTERIOR_RIGHT  0x212
#define DUNGEON_METATILE_WALL_FACE_LEFT       0x218
#define DUNGEON_METATILE_WALL_FACE_MID        0x219
#define DUNGEON_METATILE_WALL_FACE_RIGHT      0x21A

// Caves only ever use elevations 0 and 3 in vanilla.
#define DUNGEON_ELEVATION_FLOOR 3
#define DUNGEON_ELEVATION_WALL  0

// The floor is regenerated from this seed rather than stored, so a whole floor
// costs 2 bytes of save data. Claiming a spare var keeps the SaveBlock layout
// untouched, which matters because changing it invalidates existing saves.
#define VAR_ROGUE_DUNGEON_SEED VAR_UNUSED_0x40FE

#define DUNGEON_WIDTH  48
#define DUNGEON_HEIGHT 48

#define DUNGEON_MAX_ROOMS  8
#define DUNGEON_ROOM_MIN   5
#define DUNGEON_ROOM_MAX  10

void GenerateRogueDungeonFloor(u16 *backupMapData, bool8 setPlayerPosition);

#endif // GUARD_ROGUE_DUNGEON_H
