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
//  - It OWNS the generated script. ReconstructPath Allocs the movement script
//    and ScriptMovement_StartObjectMovementScript only stores the pointer; it
//    neither copies nor frees. So every call to the vendor's own entry point
//    leaks that buffer. Rare for a script macro, fatal for something repathing
//    once a second. This variant frees the previous script once the next one
//    has replaced it.
//
//    Note the buffer is NOT freeable as returned: ReconstructPath does
//    `movementScript++` past a marker byte before returning, so the base
//    pointer is one byte lower. Freeing what the vendor hands back would
//    corrupt the heap.
//
// Returns FALSE and moves nothing when no path exists within maxNodes.
bool32 PathFinder_MoveObjectToCoordsSilent(u8 localId, s16 targetX, s16 targetY,
                                           u8 facingDirection, u32 speed, u32 maxNodes);

// Drops the tracked script without starting a new one. Call when the chase ends
// or the floor changes, so the last path does not outlive the floor it was for.
void PathFinder_ReleaseTrackedScript(void);

#endif // GUARD_PATH_FINDING_H
