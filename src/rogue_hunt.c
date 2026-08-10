#include "global.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "fieldmap.h" // MAP_OFFSET
#include "malloc.h"   // Alloc/Free, for the off-screen BFS field
#include "path_finding.h"
#include "rogue_dungeon.h"
#include "rogue_hunt.h"
#include "script.h"
#include "script_movement.h"
#include "sound.h"
#include "constants/event_object_movement.h"
#include "constants/event_objects.h"   // LOCALID_*, for the debug hunt's id filter
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

// Minimum frames between searches. Was 48 (~0.8s), which at HUNT_SPEED 2 is six
// tiles of the player moving between re-aims -- most of why the hunter appeared
// to chase a ghost. 24 halves that; the cost is up to twice the A* per second,
// and the searches are only issued when the player has actually moved.
#define HUNT_REPATH_FRAMES     24   // ~0.4s. Each repath is one A* search.

// How far the player must have moved from the tile the current path was aimed at
// before a new search is worth it. 2 rather than 1 so a player shuffling on the
// spot does not order a search every 24 frames.
#define HUNT_REAIM_DISTANCE     2

#define HUNT_STEP_SE_FRAMES    52   // Footstep cue cadence while chasing.

#define HUNT_SPEED              2   // 0 slow, 1 normal, 2 fast, 3 faster. 2 is a run.

// THE NODE BUDGET IS THE HUNT'S RANGE. There is no radius that limits how far a
// hunter can come from -- maxNodes caps how many nodes path_finding.c may CREATE,
// and once ctx->nodeCount reaches it PathNode_CreateNeighbor silently returns,
// the frontier drains, and the search comes back NULL. No warning, no partial
// path: the hunter just stands still, which reads as the feature being broken.
//
// These were 64 and 128, chosen by eye. Measured over 2,996 reachable pairs on
// floors from this generator's own shapes (cave, jungle, ocean), counting the
// nodes weighted A* at PATH_FINDER_WEIGHT 1.5 actually needs:
//
//   budget   heap   1-5   6-10  11-15  16-20  21-30  31-47 tiles apart
//       64    1 KB   99%    86%    59%    30%     7%     1%
//      128    3 KB   99%    91%    85%    77%    59%    31%
//      256    7 KB  100%    96%    95%    92%    87%    80%
//      512   14 KB  100%    99%    99%    99%    98%    97%
//     1024   28 KB  100%   100%   100%   100%   100%   100%
//
// So 64 failed 41% of searches at its OWN notice radius of 11, and a stalker
// woken across a 48x48 floor -- routinely 21 to 47 tiles -- failed its direct
// search more often than not at 128. That is the "range is very low".
//
// Heap is affordable: HEAP_SIZE is 0x1E500, about 122 KB, and a search Allocs
// 28 bytes per node (a 20-byte node, a queue slot, a list slot) and frees it
// before returning. Nothing else large is allocated while this runs -- battles
// and menus lock the field controls, and the tick returns on that.
//
// THE UNMEASURED AXIS IS CPU, not memory. Every node expansion does four
// collision tests and DoesObjectCollideWithObjectAt walks all 16 object events,
// so a 512-node search is real work inside one frame and a failed one costs the
// full budget twice, direct and halfway. Nothing host-side can measure GBA
// cycles; if a repath hitches visibly on screen, this pair is the knob.
#define HUNT_MAX_NODES        256   // proximity chase: notices at 11, leashes at 24
#define HUNT_STALK_MAX_NODES  512   // trap stalker: anywhere on the floor

// How many repaths in a row may find no path at all before a stalker is written
// off as unable to reach the player.
//
// This exists because the give-up test below is deliberately not applied to a
// stalker, and that leaves it with NO way to release the one hunter slot short
// of catching the player. A stalker that can never path -- sealed off, or hidden
// behind more corridor area than the node budget can explore even halfway --
// would otherwise hold the slot for the rest of the floor.
//
// Counted in FAILED SEARCHES rather than in frames on purpose. A stalker
// crossing the whole floor legitimately takes a long time, so a wall clock would
// stand down exactly the case the feature is for; a stalker that is walking
// resets this to zero every repath and can never trip it.
#define HUNT_STALK_MAX_FAILED_REPATHS  8   // ~6.4s of getting nowhere.

// How many NPCs the debug hunt may test for reachability before giving up. See
// RogueHunt_DebugStartHunt: each test is a real search, and they all happen in
// the one frame the menu action runs in.
#define HUNT_DEBUG_MAX_CANDIDATES      4

// Frames per tile while the hunter is OFF SCREEN and being moved by hand.
//
// THE OBJECT EVENT DOES NOT EXIST OFF SCREEN, and that is the single fact this
// whole mechanism exists to work around. RemoveObjectEventIfOutsideView destroys
// anything outside pos.x-2..pos.x+17 by pos.y..pos.y+16 -- a 19x17 window, the
// screen plus a margin -- and TrySpawnObjectEvents rebuilds it from its TEMPLATE
// when the window comes back. So a hunter the player is successfully running
// from stops existing at the exact moment the chase starts to matter.
//
// Deliberately SLOWER than HUNT_SPEED 2 on screen. Nothing forces this -- the
// off-screen route is a true shortest path, so it is not compensating for a
// worse one -- but a hunter that closes off screen at full speed pops into
// view already on top of the player, with none of the approach they were
// supposed to hear coming.
#define HUNT_OFFSCREEN_FRAMES         10

// Whether walking near an idle trainer is enough to start a chase.
//
// FALSE is the design being prototyped: the hunt becomes punctuation rather
// than ambient dread, and the only thing that starts one is a trap the player
// stepped on. Set it TRUE to get the old behaviour back alongside the traps --
// the two triggers compose, and nothing else has to change.
#define HUNT_ARM_ON_PROXIMITY  FALSE

#define HUNT_NOTICE_SE     SE_BANG
#define HUNT_STEP_SE       SE_MUD_BALL
// Debug hunt only. The dungeon hunt has no equivalent because nothing there ends
// without the player seeing why -- a chase ends in a battle, on the stairs, or
// on a give-up the player caused by outrunning it.
#define HUNT_GIVE_UP_SE    SE_FAILURE

enum {
    HUNT_IDLE,        // has not noticed the player
    HUNT_CHASING,     // noticed by proximity; gives up if outrun
    HUNT_STALKING,    // woken by a trap; does NOT give up, and starts far away
    HUNT_SPENT,       // handed off to trainer_see; never hunts again this floor
    HUNT_UNREACHABLE, // stalked, could not path, and will not be woken again
};

#define HUNT_NO_HUNTER 0xFF

// Sentinel for the off-screen BFS field. 0xFF rather than a signed -1 so the
// field is one byte a tile; a 48x48 floor cannot hold a path longer than 254.
#define HUNT_FIELD_UNREACHED 0xFF

static const struct Coords8 sOffScreenDirs[4] =
{
    { 0, -1}, { 0, 1}, { 1, 0}, {-1, 0},
};

EWRAM_DATA static u8 sHuntState[DUNGEON_MAX_TRAINERS] = {0};
// One hunter at a time: heap and CPU budget. Zero-initialised because EWRAM_DATA
// lands in .sbss, which permits nothing else; HuntableTrainerCount() returns 0
// until a dungeon floor is loaded, and it gates every read of this.
EWRAM_DATA static u8 sActiveHunter = 0;
EWRAM_DATA static u8 sRepathTimer = 0;
EWRAM_DATA static u8 sStepSeTimer = 0;
// Consecutive repaths that installed no movement at all. Only a stalker acts on
// it, but it is reset by every hunter so a chase cannot inherit a stalker's tally.
EWRAM_DATA static u8 sFailedRepaths = 0;

// The hunter's position, in object space (MAP_OFFSET included), and OURS rather
// than the object event's. While the object is spawned this mirrors it; while it
// is not, this is the only record that the hunter is anywhere at all.
EWRAM_DATA static s16 sHunterX = 0;
EWRAM_DATA static s16 sHunterY = 0;
EWRAM_DATA static u8 sOffScreenTimer = 0;

// Where the player was when the current path was computed. The re-aim test
// compares against THIS rather than against the hunter, so it measures staleness
// of the path rather than distance to the target.
EWRAM_DATA static s16 sAimedAtX = 0;
EWRAM_DATA static s16 sAimedAtY = 0;

// Diagnostics only. Nothing branches on these -- they exist because two rounds
// of this feature were debugged by guessing at what the hunt was doing, when it
// could simply have been asked.
enum {
    HUNT_SEARCH_NONE,
    HUNT_SEARCH_DIRECT,     // aimed at the player and got a path
    HUNT_SEARCH_HALFWAY,    // direct failed, the midpoint worked
    HUNT_SEARCH_FAILED,     // both failed; this is what stalls a hunt
    HUNT_SEARCH_OFFSCREEN,  // not searching at all -- moved by hand
};
EWRAM_DATA static u8 sLastSearch = 0;

// DEBUG HUNT. One object event, on any map at all, driven at the player.
//
// A parallel path rather than a flag on the dungeon hunt, and the reason is
// identity. The dungeon hunt knows its trainers as local ids 1..N because the
// generator put them there; on a vanilla map those same ids belong to whatever
// NPC the map author happened to place first, and an index into sHuntState means
// nothing at all. So this tracks one local id explicitly and shares only the
// stepping.
//
// The map is stored WITH the id, and checked live every tick. That is not
// belt-and-braces -- it is the same bug the dungeon hunt already shipped once,
// where a latched "hunting is on" flag survived a warp and set the rest stop's
// shopkeeper chasing the player. A debug hunt left armed across a warp would do
// exactly that again, on an id belonging to somebody else entirely.
EWRAM_DATA static u8 sDebugHuntLocalId = 0;   // 0 (LOCALID_NONE) means off
EWRAM_DATA static u8 sDebugHuntMapNum = 0;
EWRAM_DATA static u8 sDebugHuntMapGroup = 0;

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

static s16 ChebyshevBetween(s16 x, s16 y, s16 playerX, s16 playerY)
{
    s16 dx = x - playerX;
    s16 dy = y - playerY;

    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;

    return (dx > dy) ? dx : dy;
}

// Coord-based, because a despawned hunter has no ObjectEvent to measure from.
static s16 ChebyshevToPlayer(struct ObjectEvent *obj, s16 playerX, s16 playerY)
{
    return ChebyshevBetween(obj->currentCoords.x, obj->currentCoords.y,
                            playerX, playerY);
}

// Left/right of the player, so a cue you cannot see still tells you where to
// look. This is most of the dread; the chase itself is only the payoff.
//
// Takes a coordinate rather than an object, because the thing being announced is
// very often not spawned -- that is the normal case for a trap, which wakes the
// farthest trainer on the floor.
static s8 PanFromX(s16 x, s16 playerX)
{
    s32 pan = (x - playerX) * 16;

    if (pan < -64)
        pan = -64;
    if (pan > 63)
        pan = 63;

    return (s8)pan;
}

static s8 PanFromObject(struct ObjectEvent *obj, s16 playerX)
{
    return PanFromX(obj->currentCoords.x, playerX);
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

// Where a dungeon trainer IS, whether or not it currently exists.
//
// Returns TRUE when the object is spawned, so the caller can tell the difference
// -- but the coordinates are valid either way, and that is the point. A trainer
// outside the 19x17 spawn window has no ObjectEvent at all, and every earlier
// version of this file treated that as "not a candidate", which is why a trap
// could only ever wake something already on screen.
//
// The template is the authority for a despawned object: TrySpawnObjectEvents
// both tests and spawns from template->x/y, so that IS its position.
static bool8 GetHunterCoords(u32 slot, s16 *x, s16 *y)
{
    struct ObjectEvent *obj = GetHunterObject(slot);

    if (obj != NULL)
    {
        *x = obj->currentCoords.x;
        *y = obj->currentCoords.y;
        return TRUE;
    }

    *x = gSaveBlock1Ptr->objectEventTemplates[slot].x + MAP_OFFSET;
    *y = gSaveBlock1Ptr->objectEventTemplates[slot].y + MAP_OFFSET;
    return FALSE;
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
    sFailedRepaths = 0;
    sOffScreenTimer = 0;
    sHunterX = 0;
    sHunterY = 0;
    sLastSearch = HUNT_SEARCH_NONE;

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

    // Spelled out rather than tested as != HUNT_IDLE, because HUNT_UNREACHABLE
    // is the one non-idle state that must NOT suppress the mark: that trainer
    // never got near the player, so it has to spot them like any other. This
    // only ever controls the "!" -- trainer_see runs the battle either way.
    switch (sHuntState[localId - 1])
    {
    case HUNT_CHASING:
    case HUNT_STALKING:
    case HUNT_SPENT:
        return TRUE;
    default:
        return FALSE;
    }
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
        sFailedRepaths = 0;
        PlaySE12WithPanning(HUNT_NOTICE_SE, PanFromObject(obj, playerX));
        return; // one hunter at a time
    }
}

// Wakes one trainer and sends it after the player from wherever it is standing.
// Called by a trap the player stepped on; returns FALSE when there was nobody
// left to wake, so the script can decline to print anything.
//
// It picks the FARTHEST idle trainer, not the nearest and not a random one.
// Three reasons, and the first is the whole point of the feature: distance is
// what makes the pathfinder visible, because a trainer three tiles away just
// walks at you and one across the floor has to solve the floor. The second is
// that the nearest trainer is the one proximity would have given you anyway.
// The third is that the walk is the dread -- the sound gets closer, and there
// is time to decide whether to run.
u8 RogueHunt_SpringAmbush(void)
{
    u32 huntable = HuntableTrainerCount();
    s16 playerX, playerY;
    u32 best = HUNT_NO_HUNTER;
    s16 bestDistance = -1;
    s16 bestX = 0;
    u32 i;

    // Zero on a boss floor and off a dungeon floor. PlaceTraps already refuses
    // to put a trap on either, so this is the belt to that braces -- but it is
    // the one that runs at the moment of use rather than at generation time.
    if (huntable == 0)
        return TRAP_RESULT_NOBODY;

    // One at a time, as everywhere else here: the heap and the CPU budget both
    // assume a single A* search in flight.
    //
    // This is the ordinary answer, not an edge case, and it is why the caller
    // gets a reason rather than a bool: a stalker holds this slot until it
    // catches the player, and a player who walks away from one instead has
    // every later trap on the floor land here.
    if (sActiveHunter != HUNT_NO_HUNTER)
        return TRAP_RESULT_ALREADY;

    PlayerGetDestCoords(&playerX, &playerY);

    for (i = 0; i < huntable; i++)
    {
        s16 x, y, distance;

        // IDLE only. That skips SPENT, so a trainer the player has already
        // beaten is not woken to walk at them a second time with nothing to
        // give -- and it skips UNREACHABLE, so a stalker that already proved it
        // cannot path here is not picked again. Without the second one it would
        // be picked EVERY time, since being far away is the selection criterion
        // and being far away is what made it unreachable.
        if (sHuntState[i] != HUNT_IDLE)
            continue;

        // NOT gated on being spawned any more. It used to be, and since the
        // spawn window is only 19x17 that silently reduced "the farthest
        // trainer on the floor" to "the farthest trainer on screen" -- which is
        // the whole of why the ambush had no range.
        GetHunterCoords(i, &x, &y);

        distance = ChebyshevBetween(x, y, playerX, playerY);
        if (distance > bestDistance)
        {
            bestDistance = distance;
            best = i;
            bestX = x;
        }
    }

    if (best == HUNT_NO_HUNTER)
        return TRAP_RESULT_NOBODY;

    sHuntState[best] = HUNT_STALKING;
    sActiveHunter = best;
    sRepathTimer = 0;
    sStepSeTimer = HUNT_STEP_SE_FRAMES;
    sFailedRepaths = 0;
    sOffScreenTimer = 0;
    sLastSearch = HUNT_SEARCH_NONE;
    GetHunterCoords(best, &sHunterX, &sHunterY);

    // Panned from the waker, which is the only thing telling the player which
    // way to look. On a trap this matters more than it does on a proximity
    // notice, because the thing that woke is off screen by construction -- and
    // now genuinely can be, rather than only nearly.
    PlaySE12WithPanning(HUNT_NOTICE_SE, PanFromX(bestX, playerX));
    return TRAP_RESULT_WOKE;
}

// trainer_see has committed to approaching the player. Stop the hunt NOW.
//
// THIS IS THE ONE THAT WAS MISSING, and it is why the handoff radius never
// helped. CheckTrainer begins the approach at the trainer's SIGHT RANGE, several
// tiles beyond HUNT_HANDOFF_RADIUS, and the first thing the approach does is lock
// the player's field controls. RogueHunt_Tick returns early on exactly that --
// but ScriptMovement steps the object from its OWN task, not from the tick, so
// the hunter carries on walking its committed path while the exclamation mark
// and the intro textbox play. Straight through the player, because scripted
// movement actions are forced and do not test collision.
//
// So the cancel cannot live in the tick at all. It has to be driven by the thing
// that actually ends the chase, which is trainer_see.
void RogueHunt_OnTrainerSpotted(u8 localId)
{
    if (localId == LOCALID_NONE || localId > DUNGEON_MAX_TRAINERS)
        return;
    if (localId > HuntableTrainerCount())
        return;

    // Only the one actually hunting. trainer_see calls this for every trainer
    // that spots the player, including ones that never hunted.
    if (sActiveHunter != (u32)(localId - 1))
        return;

    PathFinder_CancelTrackedMovement(localId);
    sHuntState[localId - 1] = HUNT_SPENT;
    sActiveHunter = HUNT_NO_HUNTER;
}

// A trainer has been beaten and its object is about to be removed.
//
// SPENT rather than IDLE, and this is not cosmetic bookkeeping: SpringAmbush
// picks the FARTHEST idle trainer by template position, and a beaten trainer's
// template is parked at INT16_MAX. It would win that comparison every single
// time, and then the trap would wake something that does not exist and can never
// arrive -- a silent dead ambush on every floor after the first kill.
void RogueHunt_MarkTrainerBeaten(u8 localId)
{
    if (localId == LOCALID_NONE || localId > DUNGEON_MAX_TRAINERS)
        return;

    // If it was the one hunting, hand the slot back properly rather than leaving
    // a hunter pointed at a removed object.
    if (sActiveHunter == (u32)(localId - 1))
    {
        PathFinder_CancelTrackedMovement(localId);
        sActiveHunter = HUNT_NO_HUNTER;
    }

    sHuntState[localId - 1] = HUNT_SPENT;
}

// Debug: ends whatever is hunting and re-arms everything that has not already
// been beaten.
//
// Without this a debug trap can only be tested once per floor. The first stalker
// holds the single hunter slot until it catches the player, and someone testing
// the path finder is usually walking away from it to watch it route -- which is
// exactly the case that never releases the slot.
//
// SPENT is preserved. Those trainers have been fought, and returning them to
// IDLE would send a defeated one walking at the player to offer nothing.
void RogueHunt_DebugStandDown(void)
{
    u32 i;

    // Released before the slot is cleared, not after: the same pairing every
    // other stand-down in this file obeys, and the one check_path_ownership.py
    // holds it to. Losing the pointer first leaks the path for the floor.
    if (sActiveHunter != HUNT_NO_HUNTER)
    {
        PathFinder_CancelTrackedMovement(sActiveHunter + 1);
        sActiveHunter = HUNT_NO_HUNTER;
    }

    for (i = 0; i < DUNGEON_MAX_TRAINERS; i++)
    {
        if (sHuntState[i] != HUNT_SPENT)
            sHuntState[i] = HUNT_IDLE;
    }

    sRepathTimer = 0;
    sStepSeTimer = 0;
    sFailedRepaths = 0;
}

// One repath: aim at the player, and if that finds nothing and this is a
// stalker, aim halfway instead. Returns whether either attempt actually
// installed movement.
//
// Shared by the dungeon hunt and the debug hunt, which is the whole reason it is
// a function. The two disagree about WHO hunts -- the dungeon knows its trainers
// as local ids 1..N out of the generator's arrays, the debug one is handed an
// arbitrary id off a vanilla map -- but they agree completely about how a hunter
// moves, and the coordinate-space trap below is not worth writing twice.
static bool8 StepHunterToward(u8 localId, struct ObjectEvent *obj, bool8 stalking,
                              s16 playerX, s16 playerY)
{
    u32 nodes = stalking ? HUNT_STALK_MAX_NODES : HUNT_MAX_NODES;
    s16 targetX, targetY;
    bool8 moved;

    GetChaseTarget(obj, playerX, playerY, &targetX, &targetY);

    // TWO COORDINATE SPACES, and they are one tile-border apart.
    // CreatePathFinderContext takes ctx.start straight from
    // objectEvent->currentCoords, which already includes MAP_OFFSET, but does
    // `ctx.target = target + MAP_OFFSET` because its callers are script macros
    // passing raw map coordinates. playerX/playerY here come from
    // PlayerGetDestCoords, which is object space -- so they must come back DOWN
    // by MAP_OFFSET or the hunter paths seven tiles past the player on both
    // axes, which looks exactly like running away.
    moved = PathFinder_MoveObjectToCoordsSilent(localId,
                                                targetX - MAP_OFFSET, targetY - MAP_OFFSET,
                                                DIR_NONE, HUNT_SPEED, nodes);
    if (moved)
    {
        sLastSearch = HUNT_SEARCH_DIRECT;
        return TRUE;
    }
    if (!stalking)
    {
        sLastSearch = HUNT_SEARCH_FAILED;
        return FALSE;
    }

    // NO PATH INSIDE THE NODE BUDGET, which across a 48x48 floor is the ordinary
    // case rather than the exception -- A* explores area, and the area between
    // two far corners is far more than 128 tiles.
    //
    // Aim HALFWAY instead. The search that failed cost a bounded number of nodes
    // and this one is bounded the same way, so a stalker pays two small searches
    // rather than one enormous one and still closes the distance. Once it is near
    // enough the direct path starts succeeding on its own.
    //
    // The midpoint is not tested for walkability, so this is a second roll rather
    // than a guarantee. That is survivable only because the caller re-arms its
    // repath timer either way, and gives up after
    // HUNT_STALK_MAX_FAILED_REPATHS of them.
    targetX = (obj->currentCoords.x + playerX) / 2;
    targetY = (obj->currentCoords.y + playerY) / 2;
    moved = PathFinder_MoveObjectToCoordsSilent(localId,
                                                targetX - MAP_OFFSET, targetY - MAP_OFFSET,
                                                DIR_NONE, HUNT_SPEED, nodes);
    sLastSearch = moved ? HUNT_SEARCH_HALFWAY : HUNT_SEARCH_FAILED;
    return moved;
}

// Turns a hunter that has just stopped to face the player, and makes it STAY
// turned.
//
// Both halves are needed and the second is the non-obvious one. Cancelling the
// path leaves the object's own movementType to resume, and every dungeon trainer
// is placed MOVEMENT_TYPE_FACE_DOWN -- so its handler re-asserts SOUTH within a
// few frames whatever direction the hunter actually walked in from. Approaching
// from above that happens to point at the player and looks correct; approaching
// from the left or right it does not, which is exactly the "stops facing away"
// this fixes.
//
// And it is not cosmetic. trainer_see spots the player along the trainer's
// FACING direction, so a hunter that arrives facing away runs the whole chase
// and then never opens the battle -- it just stands there.
//
// SetTrainerMovementType with the matching MOVEMENT_TYPE_FACE_* is the engine's
// own idiom for making a facing stick; battle_setup.c does exactly this pair
// after a trainer battle, and going through it also re-points the sprite
// callback, which writing objectEvent->facingDirection by hand does not.
static void FaceHunterAtPlayer(struct ObjectEvent *obj, s16 playerX, s16 playerY)
{
    enum Direction direction = GetDirectionToFace(obj->currentCoords.x,
                                                  obj->currentCoords.y,
                                                  playerX, playerY);

    ObjectEventTurn(obj, direction);
    SetTrainerMovementType(obj, GetTrainerFacingDirectionMovementType(direction));
}

// Moves the hunter while its object event does not exist.
//
// Greedy rather than A*, and that is a deliberate trade rather than laziness:
// PathFinder_MoveObjectToCoordsSilent needs a live ObjectEvent to path from, and
// there isn't one. Nobody can see this walk, and the moment the hunter re-enters
// the spawn window the real search takes over and corrects whatever this got
// wrong. Only metatile collision is tested -- other objects are despawned too,
// so there is nothing to collide with.
//
// The TEMPLATE is what gets written, not just our own copy, and that is the
// whole trick. TrySpawnObjectEvents both TESTS and SPAWNS from template->x/y,
// so advancing it is simultaneously how the hunter moves and how it comes back
// where it has got to instead of teleporting home.
// One tile along a true shortest path to the player, for a hunter with no object
// event to path with. Returns FALSE when there is no path at all.
//
// A GREEDY WALK WAS TRIED FIRST AND MEASURED, and this is why it is not here:
// over 2,005 pursuits on generated floors it closed from only 75% of starting
// positions. The other 25% never arrived -- not boxed in, but oscillating
// forever in a local minimum, stepping into a concave wall, sliding along it and
// stepping back. Off screen that is completely invisible, and from the player's
// side it is indistinguishable from the despawn bug this whole mechanism exists
// to fix. tools/rogue/measure_hunt_offscreen.py is that measurement.
//
// So: breadth-first flood from the PLAYER, stopped the moment it reaches the
// hunter, then step to any neighbour one closer. Gradient descent on a BFS field
// cannot have a local minimum -- if a path exists this finds one, and it is a
// shortest one. Costs one flood of a 48x48 grid per step rather than per frame.
//
// Dungeon floors only. The field is sized DUNGEON_WIDTH x DUNGEON_HEIGHT, and a
// vanilla map can be far larger than that; the debug hunt simply stands down
// when its NPC despawns instead.
static bool8 StepOffScreenTowardPlayer(s16 playerX, s16 playerY, s16 *outX, s16 *outY)
{
    u8 *dist;
    u16 *queue;
    u32 head = 0, tail = 0;
    u32 i;
    bool8 stepped = FALSE;
    s16 hx = sHunterX - MAP_OFFSET;
    s16 hy = sHunterY - MAP_OFFSET;
    s16 px = playerX - MAP_OFFSET;
    s16 py = playerY - MAP_OFFSET;

    if (hx < 0 || hy < 0 || hx >= DUNGEON_WIDTH || hy >= DUNGEON_HEIGHT
     || px < 0 || py < 0 || px >= DUNGEON_WIDTH || py >= DUNGEON_HEIGHT)
        return FALSE;

    dist = Alloc(DUNGEON_WIDTH * DUNGEON_HEIGHT);
    if (dist == NULL)
        return FALSE;
    queue = Alloc(DUNGEON_WIDTH * DUNGEON_HEIGHT * sizeof(u16));
    if (queue == NULL)
    {
        Free(dist);
        return FALSE;
    }

    for (i = 0; i < DUNGEON_WIDTH * DUNGEON_HEIGHT; i++)
        dist[i] = HUNT_FIELD_UNREACHED;

    dist[py * DUNGEON_WIDTH + px] = 0;
    queue[tail++] = py * DUNGEON_WIDTH + px;

    while (head != tail)
    {
        u32 cell = queue[head++];
        s16 cx = cell % DUNGEON_WIDTH;
        s16 cy = cell / DUNGEON_WIDTH;
        u8 next = dist[cell] + 1;

        if (cx == hx && cy == hy)
            break;                      // the hunter is reached; the rest is waste
        if (next == HUNT_FIELD_UNREACHED)
            continue;                   // 254 tiles away; nothing is on this floor

        for (i = 0; i < ARRAY_COUNT(sOffScreenDirs); i++)
        {
            s16 nx = cx + sOffScreenDirs[i].x;
            s16 ny = cy + sOffScreenDirs[i].y;
            u32 nCell;

            if (nx < 0 || ny < 0 || nx >= DUNGEON_WIDTH || ny >= DUNGEON_HEIGHT)
                continue;

            nCell = ny * DUNGEON_WIDTH + nx;
            if (dist[nCell] != HUNT_FIELD_UNREACHED)
                continue;
            // Object space, which is what MapGridGetCollisionAt indexes with.
            if (MapGridGetCollisionAt(nx + MAP_OFFSET, ny + MAP_OFFSET) != 0)
                continue;

            dist[nCell] = next;
            queue[tail++] = nCell;
        }
    }

    // Descend one step. Any neighbour exactly one closer is on a shortest path,
    // so the first found is as good as the best.
    if (dist[hy * DUNGEON_WIDTH + hx] != HUNT_FIELD_UNREACHED)
    {
        u8 here = dist[hy * DUNGEON_WIDTH + hx];

        for (i = 0; i < ARRAY_COUNT(sOffScreenDirs) && here != 0; i++)
        {
            s16 nx = hx + sOffScreenDirs[i].x;
            s16 ny = hy + sOffScreenDirs[i].y;

            if (nx < 0 || ny < 0 || nx >= DUNGEON_WIDTH || ny >= DUNGEON_HEIGHT)
                continue;
            // Never onto the player's own tile. On screen the path finder refuses
            // it because the player is a colliding object event; off screen
            // nothing would, and a hunter standing on the player can never hand
            // off. dist 0 is the player, so this is the same test.
            if (dist[ny * DUNGEON_WIDTH + nx] != here - 1 || here - 1 == 0)
                continue;

            *outX = nx + MAP_OFFSET;
            *outY = ny + MAP_OFFSET;
            stepped = TRUE;
            break;
        }
    }

    Free(queue);
    Free(dist);
    return stepped;
}

static void StepHunterOffScreen(u32 slot, s16 playerX, s16 playerY)
{
    struct ObjectEventTemplate *templates = gSaveBlock1Ptr->objectEventTemplates;
    s16 nx, ny;

    sLastSearch = HUNT_SEARCH_OFFSCREEN;

    // A proximity chase still has a leash. Despawn happens at about 17 tiles and
    // HUNT_GIVE_UP_RADIUS is 24, so without this test here the leash could never
    // be reached -- the hunter would be off screen for the whole of the distance
    // that was supposed to shake it.
    if (sHuntState[slot] == HUNT_CHASING
     && ChebyshevBetween(sHunterX, sHunterY, playerX, playerY) > HUNT_GIVE_UP_RADIUS)
    {
        PathFinder_ReleaseTrackedScript();
        sHuntState[slot] = HUNT_IDLE;
        sActiveHunter = HUNT_NO_HUNTER;
        return;
    }

    if (sOffScreenTimer != 0)
    {
        sOffScreenTimer--;
        return;
    }
    sOffScreenTimer = HUNT_OFFSCREEN_FRAMES;

    // The footstep cue keeps playing while it is off screen, panned from where
    // it actually is. This is the entire point of the feature -- you are meant
    // to hear it closing before you see it -- and it is also the only thing that
    // tells a player being pursued that anything is happening at all. Without
    // it, working pursuit and a dead hunt sound identical.
    if (sStepSeTimer == 0)
    {
        PlaySE12WithPanning(HUNT_STEP_SE, PanFromX(sHunterX, playerX));
        sStepSeTimer = HUNT_STEP_SE_FRAMES;
    }

    if (StepOffScreenTowardPlayer(playerX, playerY, &nx, &ny))
    {
        sHunterX = nx;
        sHunterY = ny;
        templates[slot].x = nx - MAP_OFFSET;
        templates[slot].y = ny - MAP_OFFSET;

        // AND TELL THE ENGINE, because nothing else will.
        //
        // Spawning is driven entirely by camera movement: CameraUpdate calls
        // UpdateObjectEventsForCameraUpdate only inside
        // `if (deltaX != 0 || deltaY != 0)`, so a player standing still runs no
        // spawn pass at all. Moving the template is then invisible to the
        // engine -- the hunter walks into the spawn window and stays
        // non-existent until the player happens to take a step, by which point
        // it is already on top of them, so the trainer and its exclamation mark
        // appear together out of thin air.
        //
        // (0, 0) is the right camera delta for a camera that has not moved,
        // which is the whole case this exists for; overworld.c passes the same
        // on map load. Safe to call every step: it re-runs the engine's own
        // window test rather than a copy of it, so it is a no-op while the
        // hunter is still outside, and GetAvailableObjectEventId returns
        // OBJECT_EVENTS_COUNT for an already-loaded object, so it cannot spawn
        // anything twice.
        TrySpawnObjectEvents(0, 0);
    }
}

static void UpdateActiveHunter(s16 playerX, s16 playerY)
{
    u32 slot = sActiveHunter;
    struct ObjectEvent *obj = GetHunterObject(slot);
    s16 distance;

    // DESPAWNED IS NOT DEAD, and treating it as dead is what made running away
    // end the hunt. RemoveObjectEventIfOutsideView destroys every object outside
    // a 19x17 window around the player, so the hunter stops existing at exactly
    // the moment the player succeeds in outrunning it -- and this branch used to
    // mark it HUNT_SPENT, which is permanent, leaving it standing wherever it
    // had got to and never hunting again.
    //
    // The path is released rather than cancelled: cancelling addresses a local id
    // whose object is gone. ScriptMovement's task went with it.
    if (obj == NULL)
    {
        PathFinder_ReleaseTrackedScript();
        StepHunterOffScreen(slot, playerX, playerY);
        return;
    }

    // Just came back into the spawn window. sOffScreenTimer is only ever nonzero
    // while the off-screen walk is running, so it doubles as the transition flag.
    //
    // The cancel is not tidiness. ScriptMovement keys its scripts on OBJECT EVENT
    // ID, not on local id, and a respawned object can land on the id its own
    // stale entry still occupies -- in which case IsObjectMovementFinished
    // answers FALSE forever and the hunter never repaths again. Clearing it and
    // zeroing the timer forces a fresh search on this very frame.
    if (sOffScreenTimer != 0)
    {
        PathFinder_CancelTrackedMovement(slot + 1);
        sOffScreenTimer = 0;
        sRepathTimer = 0;
        sFailedRepaths = 0;
    }

    // Spawned, so the object is authoritative again. Re-syncing every frame is
    // what makes the handover in both directions seamless: the object's position
    // came from the template we were writing, and the moment it despawns again
    // this is where the off-screen walk resumes from.
    sHunterX = obj->currentCoords.x;
    sHunterY = obj->currentCoords.y;

    // STANDING ON THE PLAYER. There is no engine-level fix for this and it is
    // worth being precise about why: the path finder emits plain
    // MOVEMENT_ACTION_WALK_* out of sMovementsBySpeed, and every walk action in
    // this engine is FORCED. Collision is always tested by whatever decides the
    // step -- a movement type handler, or the path search -- never by the action.
    // So there is no "collision on" flag to set; a committed step lands wherever
    // it was aimed even if the player has since moved into it.
    //
    // ObjectEvent coords jump to the DESTINATION when a step begins, so this
    // catches the overlap on the frame it starts rather than after the sprite has
    // slid through. Clearing the held movement and putting it back on the tile it
    // came from is what actually stops it: cancelling the script alone leaves the
    // action in progress. Then hand off, because being on top of the player is
    // as caught as caught gets.
    if (obj->currentCoords.x == playerX && obj->currentCoords.y == playerY)
    {
        PathFinder_CancelTrackedMovement(slot + 1);
        ObjectEventClearHeldMovementIfActive(obj);
        MoveObjectEventToMapCoords(obj,
                                   obj->previousCoords.x - MAP_OFFSET,
                                   obj->previousCoords.y - MAP_OFFSET);
        FaceHunterAtPlayer(obj, playerX, playerY);
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
        // Before the handoff, not after: trainer_see is what runs next and it
        // reads this facing to decide whether it can see the player at all.
        FaceHunterAtPlayer(obj, playerX, playerY);
        sHuntState[slot] = HUNT_SPENT;
        sActiveHunter = HUNT_NO_HUNTER;
        return;
    }

    // Outrun. It stops where it is and may notice again later, so a floor the
    // player has crossed reads as one that has been disturbed.
    //
    // A STALKER NEVER GIVES UP, and that is not flavour -- it is load-bearing.
    // A trap wakes something from anywhere on a 48x48 floor, which is routinely
    // further than HUNT_GIVE_UP_RADIUS of 24. Applying the give-up test to it
    // would stand it down on the very first tick, before it had taken a step,
    // and the trap would appear to do nothing at all.
    if (sHuntState[slot] == HUNT_CHASING && distance > HUNT_GIVE_UP_RADIUS)
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

    // RE-AIM, rather than waiting for the whole path to be walked.
    //
    // This used to require ScriptMovement_IsObjectMovementFinished, on the
    // reasoning that ScriptMovement rejects a new script while the old one is
    // unfinished so asking sooner wastes a search. True, but the conclusion was
    // wrong: it means the hunter COMMITS to the entire path, and at 512 nodes a
    // path can be thirty tiles long. It walks to where the player WAS, which is
    // exactly what it looked like.
    //
    // The rejection is avoidable -- cancel first and the new script is accepted.
    // So the gate is now: the player has moved far enough from where we last
    // aimed to be worth a new search, or the path is done anyway. Standing still
    // costs nothing, because sAimedAt stops moving too.
    if (sRepathTimer == 0
     && (ChebyshevBetween(sAimedAtX, sAimedAtY, playerX, playerY) >= HUNT_REAIM_DISTANCE
      || ScriptMovement_IsObjectMovementFinished(slot + 1,
                                                 gSaveBlock1Ptr->location.mapNum,
                                                 gSaveBlock1Ptr->location.mapGroup)))
    {
        bool8 stalking = (sHuntState[slot] == HUNT_STALKING);
        bool8 moved;

        // Cancel before re-aiming, or ScriptMovement rejects the new script and
        // the hunter keeps walking the stale one. Harmless when nothing is in
        // flight, and it is what keeps the path's single owner rule: the release
        // happens here, the new Alloc is tracked by the search below.
        PathFinder_CancelTrackedMovement(slot + 1);

        sAimedAtX = playerX;
        sAimedAtY = playerY;
        moved = StepHunterToward(slot + 1, obj, stalking, playerX, playerY);

        if (moved)
        {
            sFailedRepaths = 0;
        }
        else if (stalking && ++sFailedRepaths >= HUNT_STALK_MAX_FAILED_REPATHS)
        {
            // Written off. A stalker has no give-up radius, so this is the ONLY
            // thing that can hand the hunter slot back short of catching the
            // player -- and without it a stalker that cannot path holds the slot
            // for the rest of the floor. That is invisible while
            // HUNT_ARM_ON_PROXIMITY is FALSE, because nothing else wants the
            // slot; turn proximity back on and it silently kills every notice on
            // the floor instead.
            //
            // UNREACHABLE rather than IDLE, so the next trap does not pick the
            // same trainer straight back out: SpringAmbush selects on distance,
            // and distance is exactly what defeated this one.
            PathFinder_CancelTrackedMovement(slot + 1);
            sHuntState[slot] = HUNT_UNREACHABLE;
            sActiveHunter = HUNT_NO_HUNTER;
            sFailedRepaths = 0;
            return;
        }

        sRepathTimer = HUNT_REPATH_FRAMES;
    }
}

// Ends the debug hunt and releases whatever it was walking. Safe to call when no
// debug hunt is running.
static void StopDebugHunt(void)
{
    if (sDebugHuntLocalId != LOCALID_NONE)
        PathFinder_CancelTrackedMovement(sDebugHuntLocalId);

    sDebugHuntLocalId = LOCALID_NONE;
}

// The debug hunt's whole tick. Always a stalker: it is started from across the
// map on purpose, so a give-up radius would stand it down before it moved.
static void UpdateDebugHunter(s16 playerX, s16 playerY)
{
    struct ObjectEvent *obj;
    u8 objectEventId;

    // Derived live, never latched. A warp changes this and nothing else would
    // notice -- the id would then belong to a different NPC on a different map.
    if (gSaveBlock1Ptr->location.mapNum != sDebugHuntMapNum
     || gSaveBlock1Ptr->location.mapGroup != sDebugHuntMapGroup)
    {
        // RELEASE, not cancel. Cancelling addresses a local id on the CURRENT
        // map, and the object being walked is on the one just left -- so the
        // cancel would either miss or hit an unrelated NPC. Releasing the
        // tracked buffer is what the dungeon tick does in the same situation,
        // and it is safe for the same reason: ResetTasks has already destroyed
        // anything that could still be walking it, and the release NULLs what it
        // frees so a repeat is harmless.
        PathFinder_ReleaseTrackedScript();
        sDebugHuntLocalId = LOCALID_NONE;
        return;
    }

    objectEventId = GetObjectEventIdByLocalId(sDebugHuntLocalId);
    if (objectEventId >= OBJECT_EVENTS_COUNT || !gObjectEvents[objectEventId].active)
    {
        StopDebugHunt();
        return;
    }

    obj = &gObjectEvents[objectEventId];

    // Stops ON ARRIVAL rather than handing off, because there is no dungeon
    // trainer script behind this one. Whatever the NPC normally does when talked
    // to is left entirely alone.
    //
    // It is still turned to face the player. On a vanilla map that may make a
    // real trainer spot you, which is the honest outcome -- and an NPC that
    // crossed the map to reach you and then stared at a wall is the clearest
    // possible sign the tool is broken when it is not.
    if (ChebyshevToPlayer(obj, playerX, playerY) <= HUNT_HANDOFF_RADIUS)
    {
        StopDebugHunt();
        FaceHunterAtPlayer(obj, playerX, playerY);
        return;
    }

    if (sStepSeTimer == 0)
    {
        PlaySE12WithPanning(HUNT_STEP_SE, PanFromObject(obj, playerX));
        sStepSeTimer = HUNT_STEP_SE_FRAMES;
    }

    if (sRepathTimer == 0
     && ScriptMovement_IsObjectMovementFinished(sDebugHuntLocalId,
                                                gSaveBlock1Ptr->location.mapNum,
                                                gSaveBlock1Ptr->location.mapGroup))
    {
        if (StepHunterToward(sDebugHuntLocalId, obj, TRUE, playerX, playerY))
        {
            sFailedRepaths = 0;
        }
        else if (++sFailedRepaths >= HUNT_STALK_MAX_FAILED_REPATHS)
        {
            // Selection already proved this one could reach, so getting here
            // means the player has since put something uncrossable between them
            // -- water, a ledge, a door. Cued, because the tick has no script
            // context to print from and an unannounced stop is the same silence
            // that made the second trap on a floor look broken.
            PlaySE(HUNT_GIVE_UP_SE);
            StopDebugHunt();
        }

        sRepathTimer = HUNT_REPATH_FRAMES;
    }
}

// Is this object event something that can plausibly walk at the player?
//
// The load-bearing test is `inanimate`, and it is the engine's own answer:
// GetObjectEventGraphicsInfo sets it and it is copied onto the object at spawn.
// Berry trees, item balls, cuttable trees, breakable rocks and pushable boulders
// all carry it.
//
// It is also the ONLY test that works. Movement type cannot do it -- a census of
// every vanilla map.json has item balls, boulders, breakable rocks and cuttable
// trees using exactly the MOVEMENT_TYPE_LOOK_AROUND and MOVEMENT_TYPE_FACE_DOWN
// that ordinary NPCs use, so there is no threshold to put between them. Nor can
// the graphics id be matched by name: OBJ_EVENT_GFX_ITEM_BALL resolves to
// gObjectEventGraphicsInfo_PokeBall, and there are FRLG variants of three of the
// five. Without this the farthest object on most routes is a berry tree, which
// is what shipped.
static bool8 IsHuntableNpc(u32 objectEventId)
{
    struct ObjectEvent *obj = &gObjectEvents[objectEventId];

    if (!obj->active || objectEventId == gPlayerAvatar.objectEventId)
        return FALSE;

    if (obj->inanimate)
        return FALSE;

    // Local ids from LOCALID_CAMERA (127) up are all reserved -- the camera
    // dummy, the five link players, the overworld-encounter spawns, the NPC
    // follower, the following Pokemon and the player. Only 1..126 belong to
    // something a map actually placed, and no vanilla map comes near that many.
    if (obj->localId == LOCALID_NONE || obj->localId >= LOCALID_CAMERA)
        return FALSE;

    return TRUE;
}

// Debug: sends an NPC on the current map at the player, on any map in the game.
// Returns its local id, or LOCALID_NONE if nobody on the map could reach.
//
// FARTHEST FIRST, for the same reason the trap picks farthest: a short path is
// walked, a long one has to be solved, and the solving is the thing being
// looked at.
//
// But farthest-that-can-actually-reach, which is the part that needs saying.
// The farthest object on a vanilla map is very often across water, behind a
// locked door, or on the other side of a map connection -- and a hunt that picks
// one of those stands still, which is indistinguishable from a hunt that is
// broken. So selection PATHS rather than assuming: a failed search installs
// nothing and leaves no state, so a rejected candidate costs one bounded A* and
// the first success is both the hunter and the proof it can arrive.
u8 RogueHunt_DebugStartHunt(void)
{
    s16 playerX, playerY;
    u32 tried = 0;   // bitmask over gObjectEvents; OBJECT_EVENTS_COUNT is 16
    u32 attempts = 0;
    u32 i;

    // Ends the dungeon hunt properly rather than leaving it frozen. The tick
    // hands every frame to the debug hunt while one is running, so an active
    // stalker would otherwise stop mid-path and hold its buffer for the floor.
    RogueHunt_DebugStandDown();
    StopDebugHunt();

    PlayerGetDestCoords(&playerX, &playerY);

    for (;;)
    {
        s16 bestDistance = -1;
        u32 best = OBJECT_EVENTS_COUNT;

        for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
        {
            s16 distance;

            if ((tried & (1 << i)) || !IsHuntableNpc(i))
                continue;

            distance = ChebyshevToPlayer(&gObjectEvents[i], playerX, playerY);

            // Already close enough that UpdateDebugHunter would stand it down on
            // the next frame. Announcing one of these and then having nothing
            // happen is the same false negative as picking a berry tree, so it
            // is refused at selection instead.
            if (distance <= HUNT_HANDOFF_RADIUS)
                continue;

            if (distance > bestDistance)
            {
                bestDistance = distance;
                best = i;
            }
        }

        if (best == OBJECT_EVENTS_COUNT)
            return LOCALID_NONE;   // nobody left who could reach

        tried |= 1 << best;

        // Every rejected candidate costs a full HUNT_STALK_MAX_NODES search and
        // then a halfway one, all inside the single frame this menu action runs
        // in. Sixteen candidates would be 16k nodes of work at once. Four is
        // enough that it effectively never binds -- candidates are taken
        // farthest-first and most maps have only a handful of NPCs -- while
        // keeping the worst case to something a debug menu can absorb.
        if (++attempts > HUNT_DEBUG_MAX_CANDIDATES)
            return LOCALID_NONE;

        if (StepHunterToward(gObjectEvents[best].localId, &gObjectEvents[best],
                             TRUE, playerX, playerY))
        {
            sDebugHuntLocalId = gObjectEvents[best].localId;
            sDebugHuntMapNum = gSaveBlock1Ptr->location.mapNum;
            sDebugHuntMapGroup = gSaveBlock1Ptr->location.mapGroup;
            // Already stepped, so the timer starts full rather than at zero --
            // repathing on the very next frame would throw the path away.
            sRepathTimer = HUNT_REPATH_FRAMES;
            sStepSeTimer = HUNT_STEP_SE_FRAMES;
            sFailedRepaths = 0;
            PlaySE12WithPanning(HUNT_NOTICE_SE,
                                PanFromObject(&gObjectEvents[best], playerX));
            return sDebugHuntLocalId;
        }
    }
}

// Names live beside the enums they name, so adding a state cannot leave the
// readout quietly printing the wrong word for it.
const u8 *RogueHunt_DebugStateName(u8 state)
{
    switch (state)
    {
    case HUNT_CHASING:     return COMPOUND_STRING("CHASE");
    case HUNT_STALKING:    return COMPOUND_STRING("STALK");
    case HUNT_SPENT:       return COMPOUND_STRING("SPENT");
    case HUNT_UNREACHABLE: return COMPOUND_STRING("UNREACH");
    default:               return COMPOUND_STRING("idle");
    }
}

const u8 *RogueHunt_DebugSearchName(u8 lastSearch)
{
    switch (lastSearch)
    {
    case HUNT_SEARCH_DIRECT:    return COMPOUND_STRING("direct");
    case HUNT_SEARCH_HALFWAY:   return COMPOUND_STRING("halfway");
    case HUNT_SEARCH_FAILED:    return COMPOUND_STRING("FAILED");
    case HUNT_SEARCH_OFFSCREEN: return COMPOUND_STRING("offscr");
    default:                    return COMPOUND_STRING("none");
    }
}

// Diagnostics. Answers "what is the hunt actually doing right now", because the
// two rounds of bugs before this were both debugged by inference from what could
// be seen on screen -- and what could be seen on screen was, in both cases,
// nothing at all.
//
// Reports the position from GetHunterCoords rather than the object, so it still
// says something useful about a hunter that has despawned. `spawned` being FALSE
// with a distance that keeps falling is the off-screen pursuit working; FALSE
// with a distance that does not move is it not.
void RogueHunt_GetDebugInfo(struct RogueHuntDebugInfo *out)
{
    s16 playerX, playerY, x, y;

    out->huntable = HuntableTrainerCount();
    out->activeSlot = sActiveHunter;
    out->lastSearch = sLastSearch;
    out->failedRepaths = sFailedRepaths;
    out->debugLocalId = sDebugHuntLocalId;
    out->state = HUNT_IDLE;
    out->localId = LOCALID_NONE;
    out->spawned = FALSE;
    out->distance = -1;

    PlayerGetDestCoords(&playerX, &playerY);

    if (sDebugHuntLocalId != LOCALID_NONE)
    {
        u8 objectEventId = GetObjectEventIdByLocalId(sDebugHuntLocalId);

        out->localId = sDebugHuntLocalId;
        out->state = HUNT_STALKING;
        out->spawned = (objectEventId < OBJECT_EVENTS_COUNT
                        && gObjectEvents[objectEventId].active);
        if (out->spawned)
            out->distance = ChebyshevToPlayer(&gObjectEvents[objectEventId],
                                              playerX, playerY);
        return;
    }

    if (sActiveHunter == HUNT_NO_HUNTER || sActiveHunter >= out->huntable)
        return;

    out->state = sHuntState[sActiveHunter];
    out->localId = sActiveHunter + 1;
    out->spawned = GetHunterCoords(sActiveHunter, &x, &y);
    out->distance = ChebyshevBetween(x, y, playerX, playerY);
}

bool8 RogueHunt_IsDebugHunting(void)
{
    return sDebugHuntLocalId != LOCALID_NONE;
}

void RogueHunt_StopDebugHunt(void)
{
    StopDebugHunt();
}

void RogueHunt_Tick(void)
{
    // Zero on every map that is not a dungeon floor, and on every boss floor.
    // This runs from OverworldBasic, so it is reached on the rest stop, in the
    // game corner and across the Safari as well -- there is no caller-side gate
    // and there must not be one, because a gate that is set on entry is a gate
    // that is still set after the warp out.
    u32 huntable = HuntableTrainerCount();
    bool8 debugHunting = (sDebugHuntLocalId != LOCALID_NONE);
    s16 playerX, playerY;

    // The teardown for leaving a dungeon floor by any route that is not another
    // dungeon floor -- the rest stop, the game corner, a Safari gate.
    // RogueHunt_OnFloorLoad does not run on those maps, so without this a chase
    // interrupted by the stairs keeps its path buffer for the rest of the run.
    // Safe to repeat: the release NULLs what it frees, and ResetTasks has
    // already destroyed anything that could still walk it.
    //
    // Skipped entirely while a debug hunt is running, and that exemption is
    // load-bearing rather than tidy: a debug hunt lives on maps where huntable
    // is zero BY DEFINITION, so without it this would free the debug hunt's path
    // out from under it on its very first frame.
    if (huntable == 0 && !debugHunting)
    {
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

    // The debug hunt takes the frame outright when one is running. Only ever one
    // hunt at a time -- the heap and the CPU budget both assume a single A* in
    // flight -- and RogueHunt_DebugStartHunt stands the dungeon hunt down before
    // taking over, so nothing is left frozen mid-path behind this.
    if (debugHunting)
    {
        UpdateDebugHunter(playerX, playerY);
        return;
    }

    // The active hunter is re-bounds-checked rather than trusted: huntable can
    // shrink under it -- a floor with fewer trainers, or an arena -- between the
    // frame it started chasing and this one.
    if (sActiveHunter == HUNT_NO_HUNTER || sActiveHunter >= huntable)
    {
        // Only when proximity is a trigger at all. With HUNT_ARM_ON_PROXIMITY
        // FALSE the floor stays quiet until the player steps on something, and
        // RogueHunt_SpringAmbush is the only way a chase begins.
        if (HUNT_ARM_ON_PROXIMITY)
            TryNoticePlayer(huntable, playerX, playerY);
    }
    else
    {
        UpdateActiveHunter(playerX, playerY);
    }
}
