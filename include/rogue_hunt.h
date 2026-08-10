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

// Per floor. `enabled` is FALSE on floors whose trainers stand on their own
// platforms -- those are TRAINER_TYPE_NONE precisely because walking them off
// the rock strands them on water, and a hunt would do exactly that.
void RogueHunt_OnFloorLoad(bool8 enabled);

// Once per overworld frame, from OverworldBasic.
void RogueHunt_Tick(void);

// TRUE while this local id is actively chasing. trainer_see asks so it can skip
// the exclamation mark -- the announcement is the opposite of the intent.
bool8 RogueHunt_IsHunting(u8 localId);

#endif // GUARD_ROGUE_HUNT_H
