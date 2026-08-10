#include "global.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "path_finding.h"
#include "rogue_hunt.h"
#include "script.h"
#include "sound.h"
#include "constants/event_object_movement.h"
#include "constants/rogue_dungeon.h"
#include "constants/songs.h"

// ---------------------------------------------------------------------------
// Tuning. These are taste knobs, not derived numbers -- the feel is the point,
// so expect to move them after playing rather than reasoning about them.
// ---------------------------------------------------------------------------

#define HUNT_NOTICE_RADIUS      7   // Chebyshev tiles. Through walls, on purpose.
#define HUNT_GIVE_UP_RADIUS    14   // Outrun it and it loses interest.
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
// lands in .sbss, which permits nothing else; RogueHunt_OnFloorLoad establishes
// HUNT_NO_HUNTER before sEnabled can be TRUE, and sEnabled gates every read.
EWRAM_DATA static u8 sActiveHunter = 0;
EWRAM_DATA static u8 sRepathTimer = 0;
EWRAM_DATA static u8 sStepSeTimer = 0;
EWRAM_DATA static bool8 sEnabled = FALSE;

// ---------------------------------------------------------------------------

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

void RogueHunt_OnFloorLoad(bool8 enabled)
{
    u32 i;

    for (i = 0; i < DUNGEON_MAX_TRAINERS; i++)
        sHuntState[i] = HUNT_IDLE;

    sActiveHunter = HUNT_NO_HUNTER;
    sRepathTimer = 0;
    sStepSeTimer = 0;
    sEnabled = enabled;

    // The last floor's path must not outlive the floor it was computed for.
    PathFinder_ReleaseTrackedScript();
}

bool8 RogueHunt_IsHunting(u8 localId)
{
    u32 slot;

    if (!sEnabled || sActiveHunter == HUNT_NO_HUNTER)
        return FALSE;

    slot = sActiveHunter;
    return (localId == slot + 1) && (sHuntState[slot] == HUNT_CHASING);
}

static void TryNoticePlayer(s16 playerX, s16 playerY)
{
    u32 i;

    for (i = 0; i < DUNGEON_MAX_TRAINERS; i++)
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

    if (obj == NULL)
    {
        sHuntState[slot] = HUNT_SPENT;
        sActiveHunter = HUNT_NO_HUNTER;
        return;
    }

    distance = ChebyshevToPlayer(obj, playerX, playerY);

    // Close enough that vanilla trainer_see will line it up and start the
    // battle. Stop steering, and never hunt with this one again -- otherwise a
    // defeated trainer would go straight back to chasing.
    if (distance <= HUNT_HANDOFF_RADIUS)
    {
        sHuntState[slot] = HUNT_SPENT;
        sActiveHunter = HUNT_NO_HUNTER;
        return;
    }

    if (distance > HUNT_GIVE_UP_RADIUS)
    {
        sHuntState[slot] = HUNT_IDLE; // may notice again later
        sActiveHunter = HUNT_NO_HUNTER;
        return;
    }

    if (sStepSeTimer == 0)
    {
        PlaySE12WithPanning(HUNT_STEP_SE, PanFromObject(obj, playerX));
        sStepSeTimer = HUNT_STEP_SE_FRAMES;
    }

    if (sRepathTimer == 0)
    {
        PathFinder_MoveObjectToCoordsSilent(slot + 1, playerX, playerY,
                                            DIR_NONE, HUNT_SPEED, HUNT_MAX_NODES);
        sRepathTimer = HUNT_REPATH_FRAMES;
    }
}

void RogueHunt_Tick(void)
{
    s16 playerX, playerY;

    if (!sEnabled)
        return;

    // Covers scripts, battles, warps and menus in one test. Without it the hunt
    // would keep repathing underneath a trainer battle it just caused.
    if (ArePlayerFieldControlsLocked())
        return;

    if (sRepathTimer != 0)
        sRepathTimer--;
    if (sStepSeTimer != 0)
        sStepSeTimer--;

    PlayerGetDestCoords(&playerX, &playerY);

    if (sActiveHunter == HUNT_NO_HUNTER)
        TryNoticePlayer(playerX, playerY);
    else
        UpdateActiveHunter(playerX, playerY);
}
