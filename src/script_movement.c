#include "global.h"
#include "script_movement.h"
#include "event_object_movement.h"
#include "event_scripts.h"
#include "malloc.h"
#include "path_finding.h"
#include "task.h"
#include "util.h"
#include "constants/event_objects.h"
#include "constants/event_object_movement.h"

static void ScriptMovement_StartMoveObjects(u8 priority);
static u8 GetMoveObjectsTaskId(void);
static bool8 ScriptMovement_TryAddNewMovement(u8 taskId, u8 objEventId, const u8 *movementScript);
static u8 GetMovementScriptIdFromObjectEventId(u8 taskId, u8 objEventId);
static bool8 IsMovementScriptFinished(u8 taskId, u8 moveScrId);
// Both defined below their first use, by ScriptMovement_StopObjectMovement.
static void SetMovementScriptFinished(u8 taskId, u8 moveScrId);
static void SetMovementScript(u8 moveScrId, const u8 *movementScript);
static void ScriptMovement_AddNewMovement(u8 taskId, u8 moveScrId, u8 objEventId, const u8 *movementScript);
static void ScriptMovement_UnfreezeActiveObjects(u8 taskId);
static void ScriptMovement_MoveObjects(u8 taskId);
static void ScriptMovement_TakeStep(u8 taskId, u8 moveScrId, u8 objEventId, const u8 *movementScript);

static EWRAM_DATA const u8 *sMovementScripts[OBJECT_EVENTS_COUNT] = {0};

bool8 ScriptMovement_StartObjectMovementScript(u8 localId, u8 mapNum, u8 mapGroup, const u8 *movementScript)
{
    u8 objEventId;

    if (TryGetObjectEventIdByLocalIdAndMap(localId, mapNum, mapGroup, &objEventId))
        return TRUE;
    if (!FuncIsActiveTask(ScriptMovement_MoveObjects))
        ScriptMovement_StartMoveObjects(50);
    return ScriptMovement_TryAddNewMovement(GetMoveObjectsTaskId(), objEventId, movementScript);
}

bool8 ScriptMovement_IsObjectMovementFinished(u8 localId, u8 mapNum, u8 mapGroup)
{
    u8 objEventId;
    u8 taskId;
    u8 moveScrId;

    if (TryGetObjectEventIdByLocalIdAndMap(localId, mapNum, mapGroup, &objEventId))
        return TRUE;
    taskId = GetMoveObjectsTaskId();
    moveScrId = GetMovementScriptIdFromObjectEventId(taskId, objEventId);
    if (moveScrId == OBJECT_EVENTS_COUNT)
        return TRUE;
    return IsMovementScriptFinished(taskId, moveScrId);
}

bool32 ScriptMovement_IsAllObjectMovementFinished(void)
{
    u8 taskId = GetMoveObjectsTaskId();
    if (taskId != TASK_NONE)
    {
        u32 finishedMovements = gTasks[taskId].data[0];
        const u8 *objEventIds = (u8 *)&gTasks[taskId].data[1];
        for (u32 i = 0; i < OBJECT_EVENTS_COUNT; i++)
        {
            if (objEventIds[i] != 0xFF && !(finishedMovements & (1 << i)))
                return FALSE;
        }
    }
    return TRUE;
}

// Ends one object's scripted walk immediately, without touching the rest.
// ScriptMovement_UnfreezeObjectEvents is the only other way to stop one and it
// destroys the whole task, which would cancel every other object's movement too.
//
// It FREES NOTHING on purpose. A generated path is owned by whoever installed
// it -- path_finding.c -- and it releases the block itself once this returns.
// Freeing here as the GENERATED_END branch does would be wrong, because a
// caller may equally be stopping a plain ROM movement script.
//
// The object finishes the tile it is mid-way through rather than stopping
// between tiles: the held movement is left to complete and only the script is
// ended, so nothing is left standing on a half-step.
bool8 ScriptMovement_StopObjectMovement(u8 localId, u8 mapNum, u8 mapGroup)
{
    static const u8 sStopMovement = MOVEMENT_ACTION_STEP_END;
    u8 objEventId;
    u8 taskId;
    u8 moveScrId;

    // Inverted, like every Try* in this file: TRUE means it was NOT found.
    if (TryGetObjectEventIdByLocalIdAndMap(localId, mapNum, mapGroup, &objEventId))
        return FALSE;

    taskId = GetMoveObjectsTaskId();
    if (taskId == TASK_NONE)
        return FALSE;

    moveScrId = GetMovementScriptIdFromObjectEventId(taskId, objEventId);
    if (moveScrId == OBJECT_EVENTS_COUNT)
        return FALSE;

    SetMovementScriptFinished(taskId, moveScrId);
    SetMovementScript(moveScrId, &sStopMovement);

    // Deliberately NOT FreezeObjectEvent, which is what both completion paths
    // above do. The hunt hands its trainer straight to trainer_see, and that
    // wants an object it can turn and walk itself.
    UnfreezeObjectEvent(&gObjectEvents[objEventId]);
    return TRUE;
}

void ScriptMovement_UnfreezeObjectEvents(void)
{
    u8 taskId;

    taskId = GetMoveObjectsTaskId();
    if (taskId != TASK_NONE)
    {
        ScriptMovement_UnfreezeActiveObjects(taskId);
        DestroyTask(taskId);
    }
}

static void ScriptMovement_StartMoveObjects(u8 priority)
{
    u8 taskId;
    u8 i;

    taskId = CreateTask(ScriptMovement_MoveObjects, priority);

    for (i = 1; i < NUM_TASK_DATA; i++)
        gTasks[taskId].data[i] = 0xFFFF;
}

static u8 GetMoveObjectsTaskId(void)
{
    return FindTaskIdByFunc(ScriptMovement_MoveObjects);
}

static bool8 ScriptMovement_TryAddNewMovement(u8 taskId, u8 objEventId, const u8 *movementScript)
{
    u8 moveScrId;

    moveScrId = GetMovementScriptIdFromObjectEventId(taskId, objEventId);
    if (moveScrId != OBJECT_EVENTS_COUNT)
    {
        if (IsMovementScriptFinished(taskId, moveScrId) == 0)
        {
            return TRUE;
        }
        else
        {
            ScriptMovement_AddNewMovement(taskId, moveScrId, objEventId, movementScript);
            return FALSE;
        }
    }
    moveScrId = GetMovementScriptIdFromObjectEventId(taskId, LOCALID_PLAYER);
    if (moveScrId == OBJECT_EVENTS_COUNT)
    {
        return TRUE;
    }
    else
    {
        ScriptMovement_AddNewMovement(taskId, moveScrId, objEventId, movementScript);
        return FALSE;
    }
}

static u8 GetMovementScriptIdFromObjectEventId(u8 taskId, u8 objEventId)
{
    u8 *moveScriptId;
    u8 i;

    moveScriptId = (u8 *)&gTasks[taskId].data[1];
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++, moveScriptId++)
    {
        if (*moveScriptId == objEventId)
            return i;
    }
    return OBJECT_EVENTS_COUNT;
}

static void LoadObjectEventIdPtrFromMovementScript(u8 taskId, u8 moveScrId, u8 **pObjEventId)
{
    u8 i;

    *pObjEventId = (u8 *)&gTasks[taskId].data[1];
    for (i = 0; i < moveScrId; i++, (*pObjEventId)++)
        ;
}

static void SetObjectEventIdAtMovementScript(u8 taskId, u8 moveScrId, u8 objEventId)
{
    u8 *ptr;

    LoadObjectEventIdPtrFromMovementScript(taskId, moveScrId, &ptr);
    *ptr = objEventId;
}

static void LoadObjectEventIdFromMovementScript(u8 taskId, u8 moveScrId, u8 *objEventId)
{
    u8 *ptr;

    LoadObjectEventIdPtrFromMovementScript(taskId, moveScrId, &ptr);
    *objEventId = *ptr;
}

static void ClearMovementScriptFinished(u8 taskId, u8 moveScrId)
{
    u16 mask = ~(1u << moveScrId);

    gTasks[taskId].data[0] &= mask;
}

static void SetMovementScriptFinished(u8 taskId, u8 moveScrId)
{
    gTasks[taskId].data[0] |= (1u << moveScrId);
}

static bool8 IsMovementScriptFinished(u8 taskId, u8 moveScrId)
{
    u16 moveScriptFinished = (u16)gTasks[taskId].data[0] & (1u << moveScrId);

    if (moveScriptFinished != 0)
        return TRUE;
    else
        return FALSE;
}

static void SetMovementScript(u8 moveScrId, const u8 *movementScript)
{
    sMovementScripts[moveScrId] = movementScript;
}

static const u8 *GetMovementScript(u8 moveScrId)
{
    return sMovementScripts[moveScrId];
}

static void ScriptMovement_AddNewMovement(u8 taskId, u8 moveScrId, u8 objEventId, const u8 *movementScript)
{
    ClearMovementScriptFinished(taskId, moveScrId);
    SetMovementScript(moveScrId, movementScript);
    SetObjectEventIdAtMovementScript(taskId, moveScrId, objEventId);
}

static void ScriptMovement_UnfreezeActiveObjects(u8 taskId)
{
    u8 *pObjEventId;
    u8 i;

    pObjEventId = (u8 *)&gTasks[taskId].data[1];
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++, pObjEventId++)
    {
        if (*pObjEventId != 0xFF)
            UnfreezeObjectEvent(&gObjectEvents[*pObjEventId]);
    }
}

static void ScriptMovement_MoveObjects(u8 taskId)
{
    u8 i;
    u8 objEventId;

    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        LoadObjectEventIdFromMovementScript(taskId, i, &objEventId);
        if (objEventId != 0xFF)
            ScriptMovement_TakeStep(taskId, i, objEventId, GetMovementScript(i));
    }
}

// from event_object_movement
#define sTypeFuncId data[1]
#define sTimer      data[5]

static void ScriptMovement_TakeStep(u8 taskId, u8 moveScrId, u8 objEventId, const u8 *movementScript)
{
    u8 nextMoveActionId;
    struct ObjectEvent *obj = &gObjectEvents[objEventId];

    if (ObjectEventIsHeldMovementActive(obj) && !ObjectEventClearHeldMovementIfFinished(obj))
    {
        // If, while undergoing scripted movement,
        // a non-player object collides with an active follower Pokémon,
        // put that follower into a pokeball
        // (sTimer helps limit this expensive check to once per step)
        if (OW_FOLLOWERS_SCRIPT_MOVEMENT && gSprites[obj->spriteId].sTimer == 1
         && (objEventId = GetObjectObjectCollidesWith(obj, 0, 0, obj->currentElevation, TRUE)) < OBJECT_EVENTS_COUNT
            // switch `obj` to follower
         && ((obj = &gObjectEvents[objEventId])->movementType == MOVEMENT_TYPE_FOLLOW_PLAYER)
         && gSprites[obj->spriteId].sTypeFuncId != 0)
        {
            ClearObjectEventMovement(obj, &gSprites[obj->spriteId]);
            ScriptMovement_StartObjectMovementScript(obj->localId, obj->mapNum, obj->mapGroup, EnterPokeballMovement);
        }
        return;
    }

    nextMoveActionId = *movementScript;
    if (nextMoveActionId == MOVEMENT_ACTION_STEP_END)
    {
        SetMovementScriptFinished(taskId, moveScrId);
        FreezeObjectEvent(&gObjectEvents[objEventId]);
    }
    else if (nextMoveActionId == MOVEMENT_ACTION_GENERATED_END)
    {
        SetMovementScriptFinished(taskId, moveScrId);
        FreezeObjectEvent(&gObjectEvents[objEventId]);

        u8 *startPtr = (u8*)movementScript;
        while (*startPtr != MOVEMENT_ACTION_GENERATED_BEGIN)
            startPtr--;

        Free(startPtr);
        // This is the ONLY place a completed generated path is freed, and
        // path_finding.c is still tracking that same block so it can release the
        // paths that never reach here (rejected at install, cancelled at the
        // hunt's handoff, or in flight when the floor changes). Tell it, or its
        // next release frees this a second time -- and because the hunt only
        // repaths once the previous path has hit this branch, that double free
        // is guaranteed rather than unlucky.
        PathFinder_OnGeneratedScriptFreed(startPtr);
        static const u8 dummyMovement = MOVEMENT_ACTION_STEP_END;
        SetMovementScript(moveScrId, &dummyMovement);
    }
    else
    {
        if (!ObjectEventSetHeldMovement(&gObjectEvents[objEventId], nextMoveActionId))
        {
            movementScript++;
            SetMovementScript(moveScrId, movementScript);
        }
    }
}

#undef sTypeFuncId
#undef sTimer
