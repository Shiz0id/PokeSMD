#ifndef GUARD_CONSTANTS_ROGUE_DUNGEON_H
#define GUARD_CONSTANTS_ROGUE_DUNGEON_H

#include "constants/vars.h"
#include "constants/flags.h"

// Shared between the C generator and the map scripts, so the var aliases are
// defined in exactly one place.
//
// Claiming spare vars rather than adding SaveBlock fields keeps the save
// layout untouched; changing it would invalidate existing saves.
#define VAR_ROGUE_DUNGEON_SEED  VAR_UNUSED_0x40FE
#define VAR_ROGUE_DUNGEON_FLOOR VAR_UNUSED_0x40FF

// Replaces the Birch intro with name entry alone and starts the player in the
// dungeon instead of the moving truck. Set to FALSE to get vanilla back.
#define ROGUE_SLIM_NEW_GAME TRUE

// Kept permanently set. An object event whose flagId is set is not spawned, so
// this hides the placeholder slots map.json has to declare but a given floor
// does not use.
#define FLAG_ROGUE_OBJECT_UNUSED FLAG_UNUSED_0x919

// map.json must declare exactly this many object events: the engine reads
// templates from the save block but takes the count from ROM.
#define DUNGEON_MAX_TRAINERS            4
#define DUNGEON_TRAINER_FLOORS_PER_EXTRA 25
#define DUNGEON_TRAINER_SIGHT_RANGE      4

// Run lifecycle. A run needs starters on a new game and again after a whiteout,
// which is what makes a loss send the player back to the beginning.
#define VAR_ROGUE_RUN_STATE      VAR_UNUSED_0x40F7
#define ROGUE_RUN_NEEDS_STARTERS 0
#define ROGUE_RUN_ACTIVE         1

#define DUNGEON_STARTER_LEVEL 10
#define DUNGEON_STARTER_COUNT 2

#endif // GUARD_CONSTANTS_ROGUE_DUNGEON_H
