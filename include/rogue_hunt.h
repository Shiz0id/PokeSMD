#ifndef GUARD_ROGUE_HUNT_H
#define GUARD_ROGUE_HUNT_H

// Dungeon trainers that come after the player.
//
// Vanilla spotting is a tripwire: GetTrainerApproachDistance{South,North,West,
// East} only sees straight down a row or column, and the approach is a straight
// line with an exclamation mark over it. This makes a trainer notice by
// PROXIMITY instead -- through walls, around corners -- and walk a real path to
// the player, fast, with a sound cue instead of the "!".
//
// It only MOVES the trainer. The battle still happens the way it always did:
// once the hunter lines up inside DUNGEON_TRAINER_SIGHT_RANGE, vanilla
// trainer_see spots it and runs RogueDungeonFloor_EventScript_Trainer. Nothing
// here starts a battle, so none of that machinery had to be duplicated.

// Per floor. Resets the chase state and releases any path still in flight from
// the floor being left.
//
// It takes no "enabled" argument, and that is deliberate. Whether hunting may
// happen at all is derived live inside RogueHunt_Tick, from the current map's
// layout and from RogueDungeon_GetHuntableTrainerCount -- because this function
// is reached only on a dungeon floor while the tick runs on every map, so
// anything latched here stays latched across the warp out to the rest stop.
void RogueHunt_OnFloorLoad(void);

// Once per overworld frame, from OverworldBasic -- on EVERY map, including the
// rest stop, the game corner and the Safari. It gates itself, and a caller-side
// gate is exactly the bug it was written to remove; do not add one back.
void RogueHunt_Tick(void);

// TRUE while this local id is actively chasing. trainer_see asks so it can skip
// the exclamation mark -- the announcement is the opposite of the intent.
bool8 RogueHunt_IsHunting(u8 localId);

#endif // GUARD_ROGUE_HUNT_H
