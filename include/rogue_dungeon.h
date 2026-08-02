#ifndef GUARD_ROGUE_DUNGEON_H
#define GUARD_ROGUE_DUNGEON_H

// Self-contained: the prototypes below name enum WildPokemonArea, so this must
// not rely on the includer having pulled it in first.
#include "wild_encounter.h"

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

// North-facing edge - wall with floor ABOVE it, i.e. the bottom boundary of a
// room. Without these the outline breaks and rooms stop reading as enclosed,
// because the interior fill is near-identical to the floor.
#define DUNGEON_METATILE_WALL_NORTH_LEFT      0x220
#define DUNGEON_METATILE_WALL_NORTH_MID       0x209
#define DUNGEON_METATILE_WALL_NORTH_RIGHT     0x222

// Outer corners: every cardinal neighbour is wall but a diagonal is floor.
// Without these a room's top corners fall through to interior fill and the
// outline shows a notch. Named for the room corner they sit at, so
// _CORNER_NW is the block diagonally up-left of a room's top-left floor tile
// and therefore has its SOUTH-EAST diagonal open.
#define DUNGEON_METATILE_WALL_CORNER_NW       0x21B  // open SE diagonal
#define DUNGEON_METATILE_WALL_CORNER_NE       0x21C  // open SW diagonal
#define DUNGEON_METATILE_WALL_CORNER_SOUTH    0x223  // open NW or NE diagonal

// One-block-thick walls, which vanilla has no art for - its cave walls are
// always at least two thick. Appended to the Cave secondary tileset rather
// than drawn: a metatile is only 8 references to existing 8x8 tiles, so these
// splice the west-facing half of 0x210 to the east-facing half of 0x212, and
// the rock top of 0x209 to the wall face of 0x219. See
// scratchpad/compose_metatiles.py.
#define DUNGEON_METATILE_WALL_SLIVER_VERT     0x39E  // floor to both west and east
#define DUNGEON_METATILE_WALL_SLIVER_HORZ     0x39F  // floor to both north and south

// Caves only ever use elevations 0 and 3 in vanilla.
#define DUNGEON_ELEVATION_FLOOR 3
#define DUNGEON_ELEVATION_WALL  0

// Each floor has exactly one exit, placed at a seed-derived position. Because
// generation is deterministic the stairs position needs no save data at all -
// it is recomputed whenever the floor is.
//
// 0x214 is MB_LADDER, the dark opening in the cave floor. 0x23E is a wooden
// ladder. Both simply advance a floor; the direction is cosmetic variety.
//
// The hook matches on metatile id rather than behaviour, so it cannot be
// confused by an unrelated tile that happens to share a behaviour.
#define DUNGEON_METATILE_STAIRS_DOWN 0x214
#define DUNGEON_METATILE_STAIRS_UP   0x23E

// The floor is regenerated from its seed rather than stored, so a whole floor
// costs 2 bytes of save data. Var aliases live in the constants header because
// the map scripts need them too.
#include "constants/rogue_dungeon.h"

#define DUNGEON_WIDTH  48
#define DUNGEON_HEIGHT 48

#define DUNGEON_MAX_ROOMS  8
#define DUNGEON_ROOM_MIN   5
#define DUNGEON_ROOM_MAX  10

// Deeper floors unlock stronger species and raise levels. Tuning knobs kept
// here so the curve is adjustable without reading the generator.
#define DUNGEON_ENCOUNTER_BASE_LEVEL   4
#define DUNGEON_ENCOUNTER_LEVEL_STEP   2
#define DUNGEON_ENCOUNTER_LEVEL_SPREAD 2
#define DUNGEON_ENCOUNTER_STARTING_TIER 4

void GenerateRogueDungeonFloor(u16 *backupMapData, bool8 setPlayerPosition);
bool8 RogueDungeon_TryStartStairsScript(struct MapPosition *position);
const struct WildPokemonInfo *RogueDungeon_GetWildMonInfo(enum WildPokemonArea area);
void RogueDungeon_ApplyNewGameUnlocks(void);
void RogueDungeon_GetFloorName(u8 *dest);

#endif // GUARD_ROGUE_DUNGEON_H
