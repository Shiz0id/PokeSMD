#ifndef GUARD_SCRIPT_MOVEMENT_H
#define GUARD_SCRIPT_MOVEMENT_H

bool8 ScriptMovement_StartObjectMovementScript(u8 localId, u8 mapNum, u8 mapGroup, const u8 *movementScript);
bool8 ScriptMovement_IsObjectMovementFinished(u8 localId, u8 mapNum, u8 mapGroup);
bool32 ScriptMovement_IsAllObjectMovementFinished(void);

// Ends ONE object's scripted movement, leaving every other object alone --
// ScriptMovement_UnfreezeObjectEvents destroys the shared task and so cancels
// them all. Frees nothing: a generated path belongs to path_finding.c, which
// releases it after calling this. Returns FALSE if the object is not moving.
bool8 ScriptMovement_StopObjectMovement(u8 localId, u8 mapNum, u8 mapGroup);

void ScriptMovement_UnfreezeObjectEvents(void);

#endif // GUARD_SCRIPT_MOVEMENT_H
