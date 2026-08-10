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

// Wakes the FARTHEST idle trainer on the floor and sends it after the player,
// ignoring the notice radius entirely. This is what an ambush trap springs.
//
// A trainer woken this way STALKS: it does not give up when outrun, because it
// starts at a distance the give-up radius would stand it down at immediately.
// It can still be written off after enough repaths find no path at all, which is
// the only thing that hands the hunter slot back short of catching the player.
//
// Returns one of TRAP_RESULT_* from constants/rogue_dungeon.h, NOT a bool. The
// caller prints all three: a trap has no art, so a silent branch reads exactly
// like a trap that never fired.
u8 RogueHunt_SpringAmbush(void);

// trainer_see has committed to approaching the player. Ends the chase at once,
// because everything after that point locks the player field controls -- which
// is exactly when RogueHunt_Tick stops running and ScriptMovement does not.
// Safe to call for any local id; it ignores all but the active hunter.
void RogueHunt_OnTrainerSpotted(u8 localId);

// A dungeon trainer has been beaten and is about to be removed. Marks it SPENT
// so a trap cannot wake it -- its template is parked at INT16_MAX, which would
// otherwise make it win the farthest-trainer comparison every time.
void RogueHunt_MarkTrainerBeaten(u8 localId);

// Debug only. Ends the current hunt, releases its path, and re-arms every
// trainer that has not already been beaten, so a debug trap can be sprung more
// than once on a floor.
void RogueHunt_DebugStandDown(void);

// Debug only, and the one part of the hunt that works on ANY map. Sends the
// farthest NPC that can actually reach the player at them, and returns its local
// id -- or LOCALID_NONE when no NPC on the map can reach at all, which is a real
// answer rather than an error. Scenery is excluded: berry trees, item balls,
// cuttable trees, boulders and breakable rocks are all object events too.
//
// Ends itself on arrival, on a warp, or after enough failed searches. A vanilla
// map is not a dungeon floor, so nothing else will ever end it for you.
u8 RogueHunt_DebugStartHunt(void);
bool8 RogueHunt_IsDebugHunting(void);
void RogueHunt_StopDebugHunt(void);

// What the hunt is doing right now, for the debug readout. `state` is one of the
// HUNT_* values named in sHuntStateNames (rogue_hunt.c owns that enum), and
// `lastSearch` says which of the two searches last produced movement -- the one
// number that distinguishes "cannot find a path" from "is not being asked".
//
// `spawned` matters more than it looks: an object event outside a 19x17 window
// around the player does not exist, so a hunter can be pursuing perfectly well
// with no ObjectEvent at all. Distance is still valid in that case; it comes
// from the template.
struct RogueHuntDebugInfo
{
    u8 huntable;
    u8 activeSlot;
    u8 state;
    u8 localId;
    u8 lastSearch;
    u8 failedRepaths;
    u8 debugLocalId;
    bool8 spawned;
    s16 distance;
};

void RogueHunt_GetDebugInfo(struct RogueHuntDebugInfo *out);
const u8 *RogueHunt_DebugStateName(u8 state);
const u8 *RogueHunt_DebugSearchName(u8 lastSearch);

#endif // GUARD_ROGUE_HUNT_H
