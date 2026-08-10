#include "global.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "fieldmap.h" // MAP_OFFSET
#include "path_finding.h"
#include "rogue_dungeon.h"
#include "rogue_hunt.h"
#include "script.h"
#include "script_movement.h"
#include "sound.h"
#include "constants/event_object_movement.h"
#include "constants/layouts.h"
#include "constants/rogue_dungeon.h"
#include "constants/songs.h"

// ---------------------------------------------------------------------------
// Tuning. These are taste knobs, not derived numbers -- the feel is the point,
// so expect to move them after playing rather than reasoning about them.
// ---------------------------------------------------------------------------

// The screen is 15x10 blocks, so a notice radius of 7 was almost exactly "once
// it is already on screen" -- which is the one range at which a hunter cannot be
// dreadful, because you can see it. 11 means it commits from beyond the edge of
// the screen and you meet it as a sound first.
#define HUNT_NOTICE_RADIUS     11   // Chebyshev tiles. Through walls, on purpose.
// Was 14, barely past the notice ring: a hunter gave up almost as soon as it
// had started. It should cost real distance to shake, and A* is only paid for
// on repath, so a long leash is cheap.
#define HUNT_GIVE_UP_RADIUS    24   // Outrun it and it loses interest.
#define HUNT_HANDOFF_RADIUS     2   // Stop repathing; let trainer_see take the battle.

#define HUNT_REPATH_FRAMES     48   // ~0.8s. Each repath is one A* search.
#define HUNT_STEP_SE_FRAMES    52   // Footstep cue cadence while chasing.

#define HUNT_SPEED              2   // 0 slow, 1 normal, 2 fast, 3 faster. 2 is a run.
#define HUNT_MAX_NODES         64   // ~2 KB of heap per search, against the
                                    // vendor default of 256 (~8 KB). The heap is
                                    // what this build runs out of, not the linker.

#define HUNT_NOTICE_SE     SE_BANG
#define HUNT_STEP_SE       SE_MUD_BALL

enum {
    HUNT_IDLE,      // has not noticed the player
    HUNT_CHASING,   // actively pathing at the player
    HUNT_SPENT,     // handed off to trainer_see; never hunts again this floor
};

#define HUNT_NO_HUNTER 0xFF

EWRAM_DATA static u8 sHuntState[DUNGEON_MAX_TRAINERS] = {0};
// One hunter at a time: heap and CPU budget. Zero-initialised because EWRAM_DATA
// lands in .sbss, which permits nothing else; HuntableTrainerCount() returns 0
// until a dungeon floor is loaded, and it gates every read of this.
EWRAM_DATA static u8 sActiveHunter = 0;
EWRAM_DATA static u8 sRepathTimer = 0;
EWRAM_DATA static u8 sStepSeTimer = 0;

// ---------------------------------------------------------------------------

// There is deliberately NO latched "hunting is on" flag any more, and the bug
// that took it away is worth keeping in view.
//
// RogueHunt_OnFloorLoad is reached only from
// RogueDungeon_LoadObjectEventTemplates, which LoadMapFromWarp calls only when
// the layout is LAYOUT_ROGUE_DUNGEON_FLOOR. But RogueHunt_Tick runs from
// OverworldBasic, on EVERY map in the game. So a flag set on the last dungeon
// floor survived the warp, and the rest stop, the game corner room and all six
// Safari maps went on hunting -- with the shopkeeper, the archivist and the
// nurse standing on the local ids the hunt reads as trainers 1 to 4.
//
// Everything is derived live instead. Both tests below are re-read on the frame
// they matter, so there is no state left behind to go stale across a warp.
static u32 HuntableTrainerCount(void)
{
    // The map is the gate, and this is the same test LoadMapFromWarp uses to
    // decide the floor is ours at all. All seven dungeon maps -- base, the five
    // weather variants and Underwater -- share this one layout, and the rest
    // stop and the Safari have their own.
    if (gMapHeader.mapLayoutId != LAYOUT_ROGUE_DUNGEON_FLOOR)
        return 0;

    // Zero on a boss floor. rogue_dungeon owns that decision, because it is what
    // knows the boss was placed as trainer 0 like any other.
    return RogueDungeon_GetHuntableTrainerCount();
}

static s16 ChebyshevToPlayer(struct ObjectEvent *obj, s16 playerX, s16 playerY)
{
    s16 dx = obj->currentCoords.x - playerX;
    s16 dy = obj->currentCoords.y - playerY;

    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;

    return (dx > dy) ? dx : dy;
}

// Left/right of the player, so a cue you cannot see still tells you where to
// look. This is most of the dread; the chase itself is only the payoff.
static s8 PanFromObject(struct ObjectEvent *obj, s16 playerX)
{
    s32 pan = (obj->currentCoords.x - playerX) * 16;

    if (pan < -64)
        pan = -64;
    if (pan > 63)
        pan = 63;

    return (s8)pan;
}

static struct ObjectEvent *GetHunterObject(u32 slot)
{
    u8 objectEventId = GetObjectEventIdByLocalId(slot + 1); // templates use localId = i + 1

    // GetObjectEventIdByLocalId returns OBJECT_EVENTS_COUNT when it finds
    // nothing, so indexing gObjectEvents with it unchecked reads one past the
    // end. That is not hypothetical here: this map declares 28 object event
    // templates against an OBJECT_EVENTS_COUNT of 16, so a trainer in a crowded
    // corner really does fail to spawn.
    if (objectEventId >= OBJECT_EVENTS_COUNT)
        return NULL;

    if (!gObjectEvents[objectEventId].active)
        return NULL;

    return &gObjectEvents[objectEventId];
}

// The player's own tile is not a reachable destination: the collision test the
// path finder uses runs DoesObjectCollideWithObjectAt, and the player is an
// object event standing on it. Aiming there makes every single search fail.
// Aim one tile short instead, on the side the hunter is coming from, which is
// also where it wants to end up to be spotted.
static void GetChaseTarget(struct ObjectEvent *obj, s16 playerX, s16 playerY,
                           s16 *targetX, s16 *targetY)
{
    s16 dx = obj->currentCoords.x - playerX;
    s16 dy = obj->currentCoords.y - playerY;
    s16 adx = (dx < 0) ? -dx : dx;
    s16 ady = (dy < 0) ? -dy : dy;

    *targetX = playerX;
    *targetY = playerY;

    if (adx >= ady)
        *targetX += (dx > 0) ? 1 : -1;
    else
        *targetY += (dy > 0) ? 1 : -1;
}

void RogueHunt_OnFloorLoad(void)
{
    u32 i;

    for (i = 0; i < DUNGEON_MAX_TRAINERS; i++)
        sHuntState[i] = HUNT_IDLE;

    sActiveHunter = HUNT_NO_HUNTER;
    sRepathTimer = 0;
    sStepSeTimer = 0;

    // The last floor's path must not outlive the floor it was computed for.
    // This is the one release that has no new path replacing it, and it frees
    // only a buffer script_movement.c has not already freed -- see path_finding.h.
    PathFinder_ReleaseTrackedScript();
}

bool8 RogueHunt_IsHunting(u8 localId)
{
    // Deliberately CHASING *or* SPENT, and not just the currently active
    // hunter. The handoff happens at HUNT_HANDOFF_RADIUS, two tiles out, and
    // that is precisely the moment trainer_see fires -- so testing only the
    // active hunter would pop the "!" back up at the worst possible time, at
    // the end of the chase it exists to replace.
    if (localId == 0 || localId > HuntableTrainerCount())
        return FALSE;

    return sHuntState[localId - 1] != HUNT_IDLE;
}

static void TryNoticePlayer(u32 huntable, s16 playerX, s16 playerY)
{
    u32 i;

    // Bounded by the trainers actually placed on THIS floor, not by the size of
    // the array. Iterating to DUNGEON_MAX_TRAINERS reaches local ids 1 to 4
    // whatever happens to be standing on them.
    for (i = 0; i < huntable; i++)
    {
        struct ObjectEvent *obj;

        if (sHuntState[i] != HUNT_IDLE)
            continue;

        obj = GetHunterObject(i);
        if (obj == NULL)
            continue;

        if (ChebyshevToPlayer(obj, playerX, playerY) > HUNT_NOTICE_RADIUS)
            continue;

        sHuntState[i] = HUNT_CHASING;
        sActiveHunter = i;
        sRepathTimer = 0;
        sStepSeTimer = HUNT_STEP_SE_FRAMES;
        PlaySE12WithPanning(HUNT_NOTICE_SE, PanFromObject(obj, playerX));
        return; // one hunter at a time
    }
}

static void UpdateActiveHunter(s16 playerX, s16 playerY)
{
    u32 slot = sActiveHunter;
    struct ObjectEvent *obj = GetHunterObject(slot);
    s16 distance;

    // Despawned mid-chase. GetObjectEventIdByLocalId can start failing at any
    // time here: the floor declares 28 templates against an OBJECT_EVENTS_COUNT
    // of 16, so a trainer in a crowded corner is genuinely not always resident.
    // Release the path as well as the slot -- otherwise the buffer stays owned
    // with nothing walking it, and the next hunter's first repath is what
    // finally frees it, a floor's worth of heap later.
    if (obj == NULL)
    {
        PathFinder_ReleaseTrackedScript();
        sHuntState[slot] = HUNT_SPENT;
        sActiveHunter = HUNT_NO_HUNTER;
        return;
    }

    distance = ChebyshevToPlayer(obj, playerX, playerY);

    // Close enough that vanilla trainer_see will line it up and start the
    // battle. Never hunt with this one again -- otherwise a defeated trainer
    // would go straight back to chasing.
    //
    // The path is CANCELLED here, not merely left unsteered. Scripted movement
    // actions are forced and do not test collision, so a hunter allowed to walk
    // out a path computed for where the player was two seconds ago walks
    // straight through them and then turns around to start the battle from the
    // far side. Stopping it where it stands is also the better read: it caught
    // you, and the battle opens from the tile it caught you on.
    if (distance <= HUNT_HANDOFF_RADIUS)
    {
        PathFinder_CancelTrackedMovement(slot + 1);
        sHuntState[slot] = HUNT_SPENT;
        sActiveHunter = HUNT_NO_HUNTER;
        return;
    }

    // Outrun. It stops where it is and may notice again later, so a floor the
    // player has crossed reads as one that has been disturbed.
    if (distance > HUNT_GIVE_UP_RADIUS)
    {
        PathFinder_CancelTrackedMovement(slot + 1);
        sHuntState[slot] = HUNT_IDLE;
        sActiveHunter = HUNT_NO_HUNTER;
        return;
    }

    if (sStepSeTimer == 0)
    {
        PlaySE12WithPanning(HUNT_STEP_SE, PanFromObject(obj, playerX));
        sStepSeTimer = HUNT_STEP_SE_FRAMES;
    }

    // Repath only once the current path has been walked. ScriptMovement rejects
    // a new script while the old one is unfinished, so asking sooner burns an
    // A* search for nothing -- and the timer alone would have done exactly that
    // every 48 frames. This also means the hunter COMMITS to a path: it walks to
    // where you were, which is both cheaper and better to play against than
    // something that re-aims every step.
    if (sRepathTimer == 0
     && ScriptMovement_IsObjectMovementFinished(slot + 1,
                                                gSaveBlock1Ptr->location.mapNum,
                                                gSaveBlock1Ptr->location.mapGroup))
    {
        s16 targetX, targetY;

        GetChaseTarget(obj, playerX, playerY, &targetX, &targetY);

        // TWO COORDINATE SPACES, and they are one tile-border apart.
        // CreatePathFinderContext takes ctx.start straight from
        // objectEvent->currentCoords, which already includes MAP_OFFSET, but
        // does `ctx.target = target + MAP_OFFSET` because its callers are script
        // macros passing raw map coordinates. playerX/playerY here come from
        // PlayerGetDestCoords, which is object space -- so they must come back
        // DOWN by MAP_OFFSET or the hunter paths seven tiles past the player on
        // both axes, which looks exactly like running away.
        PathFinder_MoveObjectToCoordsSilent(slot + 1,
                                            targetX - MAP_OFFSET, targetY - MAP_OFFSET,
                                            DIR_NONE, HUNT_SPEED, HUNT_MAX_NODES);
        sRepathTimer = HUNT_REPATH_FRAMES;
    }
}

void RogueHunt_Tick(void)
{
    u32 huntable = HuntableTrainerCount();
    s16 playerX, playerY;

    // Zero on every map that is not a dungeon floor, and on every boss floor.
    // This runs from OverworldBasic, so it is reached on the rest stop, in the
    // game corner and across the Safari as well -- there is no caller-side gate
    // and there must not be one, because a gate that is set on entry is a gate
    // that is still set after the warp out.
    if (huntable == 0)
    {
        // Also the teardown for leaving a dungeon floor by any route that is
        // not another dungeon floor -- the rest stop, the game corner, a Safari
        // gate. RogueHunt_OnFloorLoad does not run on those maps, so without
        // this a chase interrupted by the stairs keeps its path buffer for the
        // rest of the run. Safe to repeat: the release NULLs what it frees, and
        // ResetTasks has already destroyed anything that could still walk it.
        PathFinder_ReleaseTrackedScript();
        sActiveHunter = HUNT_NO_HUNTER;
        return;
    }

    // Covers scripts, battles, warps and menus in one test. Without it the hunt
    // would keep repathing underneath a trainer battle it just caused.
    if (ArePlayerFieldControlsLocked())
        return;

    if (sRepathTimer != 0)
        sRepathTimer--;
    if (sStepSeTimer != 0)
        sStepSeTimer--;

    PlayerGetDestCoords(&playerX, &playerY);

    // The active hunter is re-bounds-checked rather than trusted: huntable can
    // shrink under it -- a floor with fewer trainers, or an arena -- between the
    // frame it started chasing and this one.
    if (sActiveHunter == HUNT_NO_HUNTER || sActiveHunter >= huntable)
        TryNoticePlayer(huntable, playerX, playerY);
    else
        UpdateActiveHunter(playerX, playerY);
}
