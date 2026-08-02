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

// A system flag, deliberately not one of the DAILY_FLAGS - those are cleared
// overnight, which would silently re-grant the team.
#define FLAG_ROGUE_STARTER_GIVEN FLAG_UNUSED_0x918

#endif // GUARD_CONSTANTS_ROGUE_DUNGEON_H
