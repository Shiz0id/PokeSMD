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

// Set once this floor's boss has paid out. Talking to a trainer the player has
// already beaten runs the post-battle script again - that is how the engine
// reports "no battle to fight here" - so without this the reward sequence
// re-runs on every conversation, and a boss ace can be farmed into a full party
// of clones. Cleared only when a genuinely new floor is rolled, so it survives
// a save and reload on the arena.
#define FLAG_ROGUE_BOSS_REWARD_TAKEN FLAG_UNUSED_0x918

// RogueDungeon_PrepareBossAceOffer results. PARTY_FULL is distinct from NONE so
// the player is told what they walked away from rather than nothing happening.
#define ROGUE_ACE_NONE       0
#define ROGUE_ACE_OFFER      1
#define ROGUE_ACE_PARTY_FULL 2

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

// Handed out at the start of every run until shops exist.
#define ROGUE_RUN_STARTING_BALLS 100

// The Unown watching the way-station. map.json declares this many slots after
// the three staff, and the seeder decides how many actually spawn and where -
// the engine takes the object COUNT from ROM but reads the TEMPLATES out of the
// save block, which is the same door the trainer placer goes through.
#define REST_STOP_UNOWN_SLOTS            6
#define REST_STOP_UNOWN_FIRST_SLOT       3   // template index, so local id 4

// Two watching near the top of the run, six by the end. 24 is picked against
// the floors a rest stop is actually reached on - 11, 21, ... 101, 106 - so the
// count steps up every other visit or so rather than all at once.
#define REST_STOP_UNOWN_MIN              2
#define REST_STOP_UNOWN_FLOORS_PER_EXTRA 24

#endif // GUARD_CONSTANTS_ROGUE_DUNGEON_H
