#ifndef GUARD_ROGUE_CHARMS_H
#define GUARD_ROGUE_CHARMS_H

#include "constants/rogue_charms.h"

// The run's charms and afflictions. See constants/rogue_charms.h for the effect
// vocabulary and why it is narrow, and the charm section of roguelike-state.md
// for why this is a SaveBlock3 struct rather than packed into the var pool.
//
// EVERYTHING OUTSIDE THIS FILE GOES THROUGH THESE FUNCTIONS. In particular the
// floor events reach charms only through the callnative wrappers at the bottom
// of rogue_charms.c - charm work and event work are separate concerns, and the
// wrappers are the whole interface between them.

// One row per ROGUE_CHARM_*. See sCharms in rogue_charms.c - adding a charm is
// a row there and nothing else.
struct RogueCharmInfo
{
    const u8 *name;
    const u8 *description;
    u8 effect;           // ROGUE_CHARM_EFFECT_*
    u8 magnitude;        // percent of max HP, or number of stat stages
    u8 param;            // ROGUE_CHARM_STAT_* mask, for STAT_DROP
    u8 defaultDuration;  // battles, or ROGUE_CHARM_DURATION_RUN / _ACT
    bool8 partyWide;     // TRUE for afflictions that hit the whole team
    // TRUE if a stat change should be reapplied to each Pokemon that switches
    // in, not only to whoever starts the battle. Per-charm rather than global:
    // a curse that a single switch shrugs off is not a curse, while a minor
    // debuff that follows the whole team all battle is oppressive.
    bool8 reapplyOnSwitchIn;
    u16 battleStringId;  // STRINGID_ROGUECHARM_*, announced at battle start
};

// Indexed by charm id, for printfromtable in RogueBattleScript_CharmAnnounce.
extern const u16 gRogueCharmStringIds[ROGUE_CHARM_COUNT];

// Returns the next charm to announce this battle and marks it announced, or
// ROGUE_CHARM_NONE when there are none left. Drives FIRST_TURN_EVENTS_ROGUE_CHARMS.
u8 RogueCharm_NextAnnouncement(void);

// Never returns NULL - an id past the end of the table returns the empty
// placeholder row, because script callnatives pass ids the C side cannot vet.
const struct RogueCharmInfo *RogueCharm_Info(u8 id);

// Returns the run's charm state, migrating it first if the save predates the
// current layout. Never returns NULL.
struct RogueRunModifiers *RogueCharm_Data(void);

// Wipes every charm. Called from RogueDungeon_ResetRun.
void RogueCharm_ResetRun(void);

// Re-points charm rows at the party slots their Pokemon now occupy. Called from
// every entry point below rather than hooked onto the party menu and storage
// system, both of which are forked in this build - see the definition.
void RogueCharm_SyncParty(void);

// Applies recoil charms to the party. From BattleStartClearSetData, before
// anything is sent out, so the HP bar draws the reduced value on the way in.
void RogueCharm_OnBattleStart(void);

// Applies stat-drop charms to the player's battlers. From
// FIRST_TURN_EVENTS_START, the first point where gBattlerPartyIndexes is valid.
void RogueCharm_QueueBattleStatDrops(void);

// Reapplies charms flagged reapplyOnSwitchIn to one battler. From the END of
// SwitchInClearSetData, which resets stat stages on its way through.
void RogueCharm_OnSwitchIn(u32 battler);

// Counts one battle off every duration-limited charm. From battle end - see the
// comment on the definition for why it cannot be battle start.
void RogueCharm_OnBattleEnd(void);

// Expires act-scoped charms if this floor is in a different dungeon than the
// last one seen. From ApplyRunConfig, which every path onto a floor goes through.
void RogueCharm_OnFloorLoad(u8 dungeon);

// Clears act-scoped charms. From wherever the run leaves a dungeon.
void RogueCharm_OnDungeonEnd(void);

// The script interface - callnative targets, and the ONLY way the floor events
// touch charms. See the block comment above their definitions for the special
// var contract; in short, VAR_0x8000 is the charm id, VAR_0x8001 the duration
// (0xFFFF for the table default), VAR_0x8002 the party slot, VAR_RESULT the out.
void RogueCharm_ScriptGrantMon(void);
void RogueCharm_ScriptGrantParty(void);
void RogueCharm_ScriptHas(void);
void RogueCharm_ScriptCount(void);
void RogueCharm_ScriptCleanseAll(void);
void RogueCharm_ScriptCleanseOne(void);        // VAR_0x8000 = id; RESULT = count removed
void RogueCharm_ScriptCleanseMon(void);        // VAR_0x8002 = slot; RESULT = count removed
void RogueCharm_ScriptBufferName(void);        // VAR_0x8000 = id -> gStringVar1/2
void RogueCharm_ScriptBufferFirstOnMon(void);  // VAR_0x8002 = slot -> gStringVar1/2, RESULT = id
void RogueCharm_ScriptBufferSummary(void);     // -> gStringVar1 listing, RESULT = entries

// The debug surface. See the block comment above the definitions - it exists
// because the resync and the save migration are otherwise only reachable by
// playing a real run and waiting for an event.
void RogueCharm_GetDebugSummary(u8 *dest);
void RogueCharm_GetDebugTargetName(u8 target, u8 *dest);
void RogueCharm_DebugGrant(u8 id, u8 target);   // target 0 = party-wide, else slot
const u8 *RogueCharm_GetName(u8 id);            // never NULL

// Is this charm a punishment rather than a blessing?
//
// DERIVED FROM THE EFFECT, NOT FROM A LIST. A second hand-kept list of "the bad
// ones" is how a charm added later ends up on the wrong side of it - the same
// argument gRogueCharmStringIds is generated from sCharms for. RECOIL,
// STAT_DROP, MAX_HP and DAMAGE_TAKEN take something away; STAT_BOOST and HEAL
// give. Added for the Shuppet, which eats afflictions and must not be able to
// eat a blessing.
bool32 RogueCharm_IsAffliction(u32 id);

// The two engine-facing lookups. Both are READ-ONLY and match on personality
// alone - they are called from CalculateMonStats and the damage calculation,
// which run constantly and know nothing about parties or migrations. See the
// block comment above their definitions before calling either from anywhere new.
u32 RogueCharm_MaxHpPercentLost(struct Pokemon *mon);
u32 RogueCharm_ExtraDamagePercent(struct Pokemon *mon, bool32 superEffective);

#endif // GUARD_ROGUE_CHARMS_H
