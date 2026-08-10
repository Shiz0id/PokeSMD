#ifndef GUARD_PATH_FINDING_H
#define GUARD_PATH_FINDING_H

// Weighted A* path finder, vendored from estellarc/feature/pathfinder.
// docs/PATHFINDER.md is the reference for the scripting macros.

// The variant the hunt uses, and the difference from the macros matters:
//
//  - It is SILENT. MoveObjectEventToCoords plays SE_PIN and walks a fail script
//    that pops an X over the object when there is no path. That is right for a
//    cutscene and wrong for a hunter, which fails to find a path routinely -- a
//    player standing behind a wall is a normal frame, not an error.
//
//  - It TRACKS the generated script, but does not solely own it. Read this
//    before touching any of it, because getting the ownership wrong here was a
//    deterministic double free that read as a random crash near a hunter.
//
//    ReconstructPath Allocs the movement script and
//    ScriptMovement_StartObjectMovementScript only stores the pointer -- it
//    neither copies nor frees. But script_movement.c DOES free it, in the
//    MOVEMENT_ACTION_GENERATED_END branch of ScriptMovement_TakeStep, which the
//    vendor added in the same commit as the path finder itself. Every generated
//    path ends in that action, so a path walked to completion is freed there and
//    nowhere else. Grepping path_finding.c alone says the opposite and is how
//    this was got wrong the first time.
//
//    So the tracked pointer exists for exactly the cases that branch never
//    reaches: a path rejected at install, a chase cancelled at handoff, and a
//    path still in flight when the floor changes. It is cleared by
//    PathFinder_OnGeneratedScriptFreed below the moment script_movement frees
//    one, so the two can never both free the same block.
//
//    Note the buffer is NOT freeable as returned: ReconstructPath does
//    `movementScript++` past a marker byte before returning, so the base
//    pointer is one byte lower. Freeing what the vendor hands back would
//    corrupt the heap. Everything here tracks and frees the BASE.
//
// Returns FALSE and moves nothing when no path exists within maxNodes.
bool32 PathFinder_MoveObjectToCoordsSilent(u8 localId, s16 targetX, s16 targetY,
                                           u8 facingDirection, u32 speed, u32 maxNodes);

// Drops the tracked script without starting a new one. Call when the floor
// changes, so a path still being walked does not outlive the floor it was for.
// Safe to call at any time: it frees only a block nothing else has freed.
void PathFinder_ReleaseTrackedScript(void);

// Stops `localId` walking its current generated path, on the spot, and releases
// it. Called at the hunt's handoff: scripted movement actions are FORCED and do
// not test collision, so a hunter left to finish a path computed for where the
// player used to be walks straight through them and lines up from the far side.
void PathFinder_CancelTrackedMovement(u8 localId);

// Called by script_movement.c when it frees a generated script itself. Clears
// the tracked pointer if it was that one. Without this the next repath frees an
// already-freed block -- and the hunt only ever repaths once the previous path
// has hit GENERATED_END, so that collision is guaranteed rather than unlucky.
void PathFinder_OnGeneratedScriptFreed(const u8 *allocationBase);

#endif // GUARD_PATH_FINDING_H
