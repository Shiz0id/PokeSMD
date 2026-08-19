#include "global.h"
#include "rogue_charms.h"
#include "rogue_journal.h"
#include "constants/rogue_dungeon.h"
#include "battle.h"
#include "pokemon.h"
#include "string_util.h"
#include "event_data.h"
#include "party_menu.h"
#include "constants/battle.h"
#include "constants/battle_string_ids.h"
#include "constants/characters.h"
#include "constants/pokemon.h"

// Forward declared because every path that CHANGES a charm has to call it, and
// those paths are spread from RogueCharm_ResetRun near the top of this file to
// the debug grant near the bottom.
static void RecalcParty(void);

// Stands in wherever a charm id has no name - the placeholder row, or an id a
// script invented. Never let a NULL reach StringCopy.
static const u8 sText_DebugCharmNone[]   = _("none");

static const u8 sText_CharmUnstable[]    = _("Unstable");
static const u8 sText_CharmUnstableDesc[] = _("Its body crackles. Takes recoil at the\nstart of every battle.");
static const u8 sText_CharmOverexerted[] = _("Overexerted");
static const u8 sText_CharmOverexertedDesc[] = _("Strained by forbidden training. Takes\nrecoil at the start of every battle.");
static const u8 sText_CharmCursed[]      = _("Cursed");
static const u8 sText_CharmCursedDesc[]  = _("An ancient trap clings to the party.\nDefenses fall as battle begins.");
static const u8 sText_CharmSluggish[]    = _("Sluggish");
static const u8 sText_CharmSluggishDesc[] = _("The bitter brew still weighs on it.\nSpeed falls as battle begins.");
static const u8 sText_CharmEmboldened[]  = _("Emboldened");
static const u8 sText_CharmEmboldenedDesc[] = _("A blessing steels its resolve. Attack\nrises as battle begins.");
static const u8 sText_CharmRejuvenating[] = _("Rejuvenating");
static const u8 sText_CharmRejuvenatingDesc[] = _("A gentle warmth lingers. Recovers a\nlittle health each battle.");
static const u8 sText_CharmFrail[]       = _("Frail");
static const u8 sText_CharmFrailDesc[]   = _("Forbidden training left scars. Its\nmaximum HP is reduced.");
static const u8 sText_CharmAlpha[]           = _("Alpha");
static const u8 sText_CharmAlphaDesc[]       = _("Born larger than its kind. Every stat\nstands one stage higher, all run.");
static const u8 sText_CharmBrittle[]         = _("Brittle");
static const u8 sText_CharmBrittleDesc[]     = _("Something was traded away for power.\nDefense falls each battle this run.");
static const u8 sText_CharmOrbAwakened[]     = _("Orb-Awakened");
static const u8 sText_CharmOrbAwakenedDesc[] = _("The orb lends its strength. Attack and\nSp. Atk rise each battle this dungeon.");
static const u8 sText_CharmOrbBurdened[]     = _("Orb-Burdened");
static const u8 sText_CharmOrbBurdenedDesc[] = _("The orb takes its due. The party loses\nHP each battle this dungeon.");
static const u8 sText_CharmHexed[]       = _("Hexed");
static const u8 sText_CharmHexedDesc[]   = _("An ancient hex clings on. Takes more\ndamage from super-effective hits.");

// THE CHARMS, AS A TABLE. Adding one should be a row here and nothing else -
// same rule the dungeon themes follow. If a new charm is turning into an edit
// to RogueCharm_OnBattleStart, it wants an effect KIND, not a special case, and
// the kind belongs in the switch in ApplyCharmEffect where every charm can
// reach it.
//
// Indexed by ROGUE_CHARM_*, so row 0 is the empty-slot placeholder and exists
// only to keep the id and the index the same number. check_charms.py asserts
// the row count matches ROGUE_CHARM_COUNT, because a table that silently ran
// short would read off the end for the highest id and apply whatever followed.
//
// Every duration here is the DEFAULT. The granting event may override it - the
// Fossil site wants its curse to last 3 battles, but nothing stops a later
// event granting the same charm for longer.
static const struct RogueCharmInfo sCharms[ROGUE_CHARM_COUNT] =
{
    [ROGUE_CHARM_NONE] =
    {
        .name = NULL, .description = NULL,
        .effect = ROGUE_CHARM_EFFECT_NONE,
    },

    // The Volatile Evolution Crystal. 5% is small enough that it is a tax
    // rather than a death sentence over a long run, which is the point - the
    // player took an evolution they had not earned yet.
    [ROGUE_CHARM_UNSTABLE] =
    {
        .name = sText_CharmUnstable,
        .description = sText_CharmUnstableDesc,
        .effect = ROGUE_CHARM_EFFECT_RECOIL,
        .magnitude = 5,
        .defaultDuration = ROGUE_CHARM_DURATION_RUN,
        .partyWide = FALSE,
        .battleStringId = STRINGID_ROGUECHARM_UNSTABLE,
    },

    // The Shady Move Tutor. Steeper than Unstable because the move it buys is
    // permanent and immediately useful, where a forced evolution mostly brings
    // forward something the run would have reached anyway.
    //
    // THIS IS NOT THE DRAFTED EFFECT. The draft asked for a permanent -15% max
    // HP; max HP is derived in CalculateMonStats and recomputed on every level
    // up, so honouring it literally means hooking stat recalculation, which is
    // the v2 line. Recoil keeps the shape - permanent, per-Pokemon, the price
    // of power - inside the v1 vocabulary.
    [ROGUE_CHARM_OVEREXERTED] =
    {
        .name = sText_CharmOverexerted,
        .description = sText_CharmOverexertedDesc,
        .effect = ROGUE_CHARM_EFFECT_RECOIL,
        .magnitude = 8,
        .defaultDuration = ROGUE_CHARM_DURATION_RUN,
        .partyWide = FALSE,
        .battleStringId = STRINGID_ROGUECHARM_OVEREXERTED,
    },

    // The Fossil Excavation Site. The draft asked for +10% super-effective
    // damage taken, which is a damage-formula hook; a defence drop is the same
    // design intent - you are fragile for a while - and it ANIMATES, so the
    // player can see the thing that is happening to them.
    [ROGUE_CHARM_CURSED] =
    {
        .name = sText_CharmCursed,
        .description = sText_CharmCursedDesc,
        .effect = ROGUE_CHARM_EFFECT_STAT_DROP,
        .magnitude = 1,
        .param = ROGUE_CHARM_STAT_DEF | ROGUE_CHARM_STAT_SPDEF,
        .defaultDuration = 3,
        .partyWide = TRUE,
        // The only charm that follows a switch. A party-wide curse the player
        // could shed by rotating one Pokemon would be an inconvenience rather
        // than a curse, and three battles is short enough to bear.
        .reapplyOnSwitchIn = TRUE,
        .battleStringId = STRINGID_ROGUECHARM_CURSED,
    },

    // The Herbalist's Revival Brew. Scoped to the act because the draft was
    // explicit about it, and because a whole-run speed drop on a revived party
    // is a bigger price than a full revive is worth.
    [ROGUE_CHARM_SLUGGISH] =
    {
        .name = sText_CharmSluggish,
        .description = sText_CharmSluggishDesc,
        .effect = ROGUE_CHARM_EFFECT_STAT_DROP,
        .magnitude = 1,
        .param = ROGUE_CHARM_STAT_SPEED,
        .defaultDuration = ROGUE_CHARM_DURATION_ACT,
        .partyWide = FALSE,
        .battleStringId = STRINGID_ROGUECHARM_SLUGGISH,
    },

    // THE FIRST TWO CHARMS THAT HELP. Nothing grants them yet - they exist so
    // the boost and heal paths have a caller, and so the event track has
    // something to offer rather than only something to charge.
    //
    // Act-scoped rather than run-scoped on purpose: a permanent stat boost
    // compounds with the level curve and stops being a decision by the third
    // dungeon, where an act-long one stays a reason to press on now.
    [ROGUE_CHARM_EMBOLDENED] =
    {
        .name = sText_CharmEmboldened,
        .description = sText_CharmEmboldenedDesc,
        .effect = ROGUE_CHARM_EFFECT_STAT_BOOST,
        .magnitude = 1,
        .param = ROGUE_CHARM_STAT_ATK | ROGUE_CHARM_STAT_SPATK,
        .defaultDuration = ROGUE_CHARM_DURATION_ACT,
        .partyWide = FALSE,
        .battleStringId = STRINGID_ROGUECHARM_EMBOLDENED,
    },

    // THE ALPHA a player caught rather than knocked out, and the only charm in
    // this table that is a PROPERTY OF THE POKEMON rather than something that
    // happened to it.
    //
    // It exists because settotemboost cannot survive the battle it is set in.
    // That command writes stat STAGES, which live on struct BattlePokemon and
    // are thrown away with the battle; struct Pokemon has no such field, so a
    // captured Alpha arrived in the party as an ordinary member of its species
    // wearing none of what the player just fought. This charm is where the
    // boost is kept instead, and charms are stored per Pokemon in SaveBlock3.
    //
    // RUN-SCOPED AND reapplyOnSwitchIn, BOTH AGAINST THE HOUSE STYLE, and both
    // deliberate. Emboldened argues right above that a permanent stat boost
    // compounds with the level curve - true of a blessing handed out by an
    // event, which is what that note is about. This is not handed out. It is
    // what the Pokemon IS, and it was already on the field at that strength
    // when the player chose to catch it rather than knock it out.
    //
    // reapplyOnSwitchIn is the half that fails SILENTLY if forgotten:
    // SwitchInClearSetData resets every stat stage, so without it the Alpha is
    // correct on turn one of its first battle and plain for the rest of the
    // run - visible only to a player who switches and then counts.
    [ROGUE_CHARM_ALPHA] =
    {
        .name = sText_CharmAlpha,
        .description = sText_CharmAlphaDesc,
        .effect = ROGUE_CHARM_EFFECT_STAT_BOOST,
        .magnitude = 1,
        .param = ROGUE_CHARM_STAT_ATK | ROGUE_CHARM_STAT_DEF
               | ROGUE_CHARM_STAT_SPEED | ROGUE_CHARM_STAT_SPATK
               | ROGUE_CHARM_STAT_SPDEF,
        .defaultDuration = ROGUE_CHARM_DURATION_RUN,
        .partyWide = FALSE,
        .reapplyOnSwitchIn = TRUE,
        .battleStringId = STRINGID_ROGUECHARM_ALPHA,
    },

    // Deliberately small. 8% a battle is a slow drip that rewards pushing one
    // more floor, not a substitute for the bag - a heal large enough to replace
    // potions would flatten the resource decisions the run is built on.
    [ROGUE_CHARM_REJUVENATING] =
    {
        .name = sText_CharmRejuvenating,
        .description = sText_CharmRejuvenatingDesc,
        .effect = ROGUE_CHARM_EFFECT_HEAL,
        .magnitude = 8,
        .defaultDuration = ROGUE_CHARM_DURATION_RUN,
        .partyWide = FALSE,
        .battleStringId = STRINGID_ROGUECHARM_REJUVENATING,
    },

    // THE TWO DRAFTED EFFECTS, restored now that the hooks exist. They are ADDED
    // ALONGSIDE Overexerted and Cursed rather than replacing them, because the
    // substitutes are not strictly worse - a stat drop ANIMATES and a max-HP cut
    // does not, so the player can see one happen and must be told about the
    // other. The event track picks per event; both pairs are supported.
    //
    // Frail is the Shady Move Tutor's drafted price: a permanent -15% max HP.
    [ROGUE_CHARM_FRAIL] =
    {
        .name = sText_CharmFrail,
        .description = sText_CharmFrailDesc,
        .effect = ROGUE_CHARM_EFFECT_MAX_HP,
        .magnitude = 15,
        .defaultDuration = ROGUE_CHARM_DURATION_RUN,
        .partyWide = FALSE,
        .battleStringId = STRINGID_ROGUECHARM_FRAIL,
    },

    // Hexed is the Fossil site's drafted curse: +10% super-effective damage
    // taken, for three battles, across the party.
    [ROGUE_CHARM_HEXED] =
    {
        .name = sText_CharmHexed,
        .description = sText_CharmHexedDesc,
        .effect = ROGUE_CHARM_EFFECT_DAMAGE_TAKEN,
        .magnitude = 10,
        .defaultDuration = 3,
        .partyWide = TRUE,
        .battleStringId = STRINGID_ROGUECHARM_HEXED,
    },

    // THE ORB AT THE SUMMIT, both halves of it. Mt Pyre's orbs are not gifts -
    // they wake something, and whoever holds one is along for the ride.
    //
    // +1 to both attacking stats, party-wide, for the dungeon. Larger in reach
    // than Emboldened's per-mon boost because the price below is paid every
    // battle rather than once, and because a party-wide charm the player CHOSE
    // should feel like the run turned a corner.
    [ROGUE_CHARM_ORB_AWAKENED] =
    {
        .name = sText_CharmOrbAwakened,
        .description = sText_CharmOrbAwakenedDesc,
        .effect = ROGUE_CHARM_EFFECT_STAT_BOOST,
        .magnitude = 1,
        .param = ROGUE_CHARM_STAT_ATK | ROGUE_CHARM_STAT_SPATK,
        .defaultDuration = ROGUE_CHARM_DURATION_ACT,
        .partyWide = TRUE,
        .battleStringId = STRINGID_ROGUECHARM_ORB_AWAKENED,
    },

    // The cut. 6% a battle, party-wide, and deliberately BELOW Overexerted's 8%
    // per-mon: this lands on everything that fights, for a whole dungeon, so the
    // same number would be a far larger tax than it looks. Small enough to push
    // one more floor with, large enough that a long dungeon on the orb is a
    // different run from a short one.
    [ROGUE_CHARM_ORB_BURDENED] =
    {
        .name = sText_CharmOrbBurdened,
        .description = sText_CharmOrbBurdenedDesc,
        .effect = ROGUE_CHARM_EFFECT_RECOIL,
        .magnitude = 6,
        .defaultDuration = ROGUE_CHARM_DURATION_ACT,
        .partyWide = TRUE,
        .battleStringId = STRINGID_ROGUECHARM_ORB_BURDENED,
    },

    // The Ability Transposer's alternative price. A permanent -1 Defense on the
    // one Pokemon that got the new ability, for the whole run.
    //
    // DEFENSE ONLY, not both defences like Cursed. The transposer's reward is a
    // build-defining upgrade on one mon, so the cost has to be legible on that
    // mon rather than smeared across the party - and a single stage on one stat
    // is something a player can decide is worth it, which is the whole event.
    [ROGUE_CHARM_BRITTLE] =
    {
        .name = sText_CharmBrittle,
        .description = sText_CharmBrittleDesc,
        .effect = ROGUE_CHARM_EFFECT_STAT_DROP,
        .magnitude = 1,
        .param = ROGUE_CHARM_STAT_DEF,
        .defaultDuration = ROGUE_CHARM_DURATION_RUN,
        .partyWide = FALSE,
        .battleStringId = STRINGID_ROGUECHARM_BRITTLE,
    },
};

// printfromtable in RogueBattleScript_CharmAnnounce indexes this by charm id,
// so it is generated FROM sCharms rather than written out again - two hand-kept
// lists of the same thing is how a new charm ends up announcing the wrong line.
const u16 gRogueCharmStringIds[ROGUE_CHARM_COUNT] =
{
    [ROGUE_CHARM_NONE]        = STRINGID_ROGUECHARM_UNSTABLE, // never printed
    [ROGUE_CHARM_UNSTABLE]    = STRINGID_ROGUECHARM_UNSTABLE,
    [ROGUE_CHARM_OVEREXERTED] = STRINGID_ROGUECHARM_OVEREXERTED,
    [ROGUE_CHARM_CURSED]      = STRINGID_ROGUECHARM_CURSED,
    [ROGUE_CHARM_SLUGGISH]    = STRINGID_ROGUECHARM_SLUGGISH,
    [ROGUE_CHARM_EMBOLDENED]  = STRINGID_ROGUECHARM_EMBOLDENED,
    [ROGUE_CHARM_REJUVENATING] = STRINGID_ROGUECHARM_REJUVENATING,
    [ROGUE_CHARM_FRAIL]       = STRINGID_ROGUECHARM_FRAIL,
    [ROGUE_CHARM_HEXED]       = STRINGID_ROGUECHARM_HEXED,
    [ROGUE_CHARM_ORB_AWAKENED] = STRINGID_ROGUECHARM_ORB_AWAKENED,
    [ROGUE_CHARM_ORB_BURDENED] = STRINGID_ROGUECHARM_ORB_BURDENED,
    [ROGUE_CHARM_BRITTLE]      = STRINGID_ROGUECHARM_BRITTLE,
    [ROGUE_CHARM_ALPHA]        = STRINGID_ROGUECHARM_ALPHA,
};

bool32 RogueCharm_IsAffliction(u32 id)
{
    if (id == ROGUE_CHARM_NONE || id >= ROGUE_CHARM_COUNT)
        return FALSE;

    switch (sCharms[id].effect)
    {
    case ROGUE_CHARM_EFFECT_RECOIL:
    case ROGUE_CHARM_EFFECT_STAT_DROP:
    case ROGUE_CHARM_EFFECT_MAX_HP:
    case ROGUE_CHARM_EFFECT_DAMAGE_TAKEN:
        return TRUE;
    default:
        // STAT_BOOST, HEAL and NONE. Written as a default rather than as a
        // second list so a new EFFECT added later is a blessing until someone
        // decides otherwise - the safe direction, because the Shuppet eating a
        // boon is a broken event and the Shuppet ignoring a new curse is only a
        // missed one.
        return FALSE;
    }
}

// Which charms have already had their line printed this battle. A bit per charm
// id, so a curse held by four party members announces ONCE - the player needs to
// know the run is cursed, not to press A four times.
static u8 sAnnouncedThisBattle;

// Returns the next charm to announce, marking it so the caller can keep calling
// until it returns ROGUE_CHARM_NONE. The battle main loop has to return between
// messages, so this cannot be a loop on our side.
u8 RogueCharm_NextAnnouncement(void)
{
    struct RogueRunModifiers *data = RogueCharm_Data();
    u32 slot, i;

    for (i = 0; i < ROGUE_PARTY_CHARM_SLOTS; i++)
    {
        u8 id = data->party[i].id;
        if (id != ROGUE_CHARM_NONE && !(sAnnouncedThisBattle & (1 << id)))
        {
            sAnnouncedThisBattle |= 1 << id;
            return id;
        }
    }

    for (slot = 0; slot < PARTY_SIZE; slot++)
    {
        for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
        {
            u8 id = data->mon[slot][i].id;
            if (id != ROGUE_CHARM_NONE && !(sAnnouncedThisBattle & (1 << id)))
            {
                sAnnouncedThisBattle |= 1 << id;
                return id;
            }
        }
    }

    return ROGUE_CHARM_NONE;
}

// Out-of-range ids return the placeholder rather than reading off the end.
// Scripts pass charm ids as raw numbers through callnative, so this is reachable
// from data the C side does not control.
const struct RogueCharmInfo *RogueCharm_Info(u8 id)
{
    if (id >= ROGUE_CHARM_COUNT)
        return &sCharms[ROGUE_CHARM_NONE];

    return &sCharms[id];
}

// THE MIGRATION IS LAZY, AND THAT IS DELIBERATE.
//
// load_save.c has no post-load hook to hang a migration on - ClearSav3 runs on
// a new game and nothing runs when an existing save is continued. Checking the
// version on every access instead means every arrival path is covered by
// construction: new game, continue, and the case that actually matters, a save
// written before struct RogueRunModifiers existed.
//
// That last case is not hypothetical. Appending to SaveBlock3 leaves an older
// save reading whatever bytes were already in the sector, so without this the
// first floor of an existing run would come up holding charms nobody granted,
// with garbage durations, applying real effects.
struct RogueRunModifiers *RogueCharm_Data(void)
{
    struct RogueRunModifiers *data = &gSaveBlock3Ptr->rogueCharms;

    if (data->version != ROGUE_CHARMS_SAVE_VERSION)
    {
        CpuFill16(0, data, sizeof(*data));
        data->version = ROGUE_CHARMS_SAVE_VERSION;
    }

    return data;
}

void RogueCharm_ResetRun(void)
{
    struct RogueRunModifiers *data = RogueCharm_Data();

    CpuFill16(0, data, sizeof(*data));
    data->version = ROGUE_CHARMS_SAVE_VERSION;

    RecalcParty();
}

// Defined below, beside InstallCharm which it needs. Declared here because
// RogueCharm_SyncParty is the one caller and sits above both.
static void EnsureAlphaCharms(struct RogueRunModifiers *data);

// Returns slot's charm row, or NULL if the identity there is stale.
//
// THE PERSONALITY CHECK IS THE POINT OF THE WHOLE STORAGE DESIGN. If the player
// reordered the party or swapped a box mon in, the charms recorded against this
// slot belong to a Pokemon that is no longer standing in it. RogueCharm_SyncParty
// keeps that from happening, but every reader still checks, because a charm
// applied to the wrong Pokemon is worse than a charm that quietly does nothing.
static struct RogueCharm *MonCharms(struct RogueRunModifiers *data, u32 slot, struct Pokemon *mon)
{
    if (data->personality[slot] == 0)
        return NULL;

    if (GetMonData(mon, MON_DATA_PERSONALITY) != data->personality[slot])
        return NULL;

    return data->mon[slot];
}

// Re-points every charm row at the party slot its Pokemon now occupies.
//
// THIS IS SYNCED LAZILY, FROM EVERY ENTRY POINT, RATHER THAN HOOKED ONTO THE
// PARTY MENU AND THE STORAGE SYSTEM. Both of those are FORKED in this build -
// montmoguri's SwSh branches replaced party_menu.c and the storage system
// wholesale - so "hook the swap" means finding and re-hosting the hook in a fork
// today and again after the next merge. Syncing on read costs a 6x6 loop and
// cannot be orphaned by a merge, which is the trade worth making.
//
// Charms follow the POKEMON. A mon deposited to a box takes its charms out of
// the run with it and comes back clean, which is the honest reading of "this
// Pokemon is cursed" - and it is also the only reading that cannot be exploited,
// since the alternative would let the player park an affliction in a box.
void RogueCharm_SyncParty(void)
{
    struct RogueRunModifiers *data = RogueCharm_Data();
    u32 personality[PARTY_SIZE];
    struct RogueCharm charms[PARTY_SIZE][ROGUE_CHARMS_PER_MON];
    u32 newSlot, oldSlot, i;

    for (newSlot = 0; newSlot < PARTY_SIZE; newSlot++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][newSlot];
        u32 pid;

        personality[newSlot] = 0;
        for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
        {
            charms[newSlot][i].id = ROGUE_CHARM_NONE;
            charms[newSlot][i].duration = ROGUE_CHARM_DURATION_RUN;
        }

        if (GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE)
            continue;

        // A personality of 0 is this struct's empty marker, so a Pokemon that
        // genuinely rolled 0 simply never holds charms. That is one identity in
        // four billion, and the alternative - a separate occupancy flag per slot
        // - is more state to keep in step for no reachable gain.
        pid = GetMonData(mon, MON_DATA_PERSONALITY);
        if (pid == 0)
            continue;

        personality[newSlot] = pid;

        for (oldSlot = 0; oldSlot < PARTY_SIZE; oldSlot++)
        {
            if (data->personality[oldSlot] != pid)
                continue;

            for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
                charms[newSlot][i] = data->mon[oldSlot][i];
            break;
        }
    }

    for (newSlot = 0; newSlot < PARTY_SIZE; newSlot++)
    {
        data->personality[newSlot] = personality[newSlot];
        for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
            data->mon[newSlot][i] = charms[newSlot][i];
    }

    // AFTER the rows are re-pointed, never before: this writes INTO the slot a
    // Pokemon now occupies, and doing it first would install against the old
    // arrangement and then have the result overwritten by the loop above.
    EnsureAlphaCharms(data);
}

// ---------------------------------------------------------------------------
// THE TWO EFFECTS THAT ARE NOT BATTLE-START-ONLY.
//
// Everything above applies once, at a known moment, to a known party slot. These
// two are read by the ENGINE, from paths that know nothing about charms and run
// constantly - stat recalculation on every level up, damage calculation on every
// hit including the AI's speculative ones.
//
// That forces three rules on the lookups below, all of them learned from what
// those call sites are:
//
//   1. THEY MUST NOT WRITE. RogueCharm_Data migrates on read, and a migration
//      firing inside CalculateMonStats during save loading would rewrite the
//      block the load is in the middle of filling. They read the struct raw and
//      return zero on a version mismatch instead.
//   2. THEY MUST NOT SYNC. RogueCharm_SyncParty reads the live party, and
//      CalculateMonStats is called FROM BoxMonToMon while a party is being
//      built. They match on personality alone, which needs no party at all.
//   3. THEY MUST BE CHEAP. Six comparisons, no GetMonData beyond personality.
// ---------------------------------------------------------------------------

// Raw read - no migration, no sync. Returns NULL if the block is not ours yet.
static const struct RogueRunModifiers *ReadOnlyData(void)
{
    const struct RogueRunModifiers *data = &gSaveBlock3Ptr->rogueCharms;

    if (data->version != ROGUE_CHARMS_SAVE_VERSION)
        return NULL;

    return data;
}

// Total magnitude of one effect kind held by the Pokemon with this personality,
// counting its own charms and the party-wide ones.
static u32 MagnitudeForPersonality(u32 personality, u8 effect)
{
    const struct RogueRunModifiers *data = ReadOnlyData();
    u32 slot, i, total = 0;

    if (data == NULL || personality == 0)
        return 0;

    for (i = 0; i < ROGUE_PARTY_CHARM_SLOTS; i++)
    {
        const struct RogueCharmInfo *info = RogueCharm_Info(data->party[i].id);
        if (data->party[i].id != ROGUE_CHARM_NONE && info->effect == effect)
            total += info->magnitude;
    }

    for (slot = 0; slot < PARTY_SIZE; slot++)
    {
        if (data->personality[slot] != personality)
            continue;

        for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
        {
            const struct RogueCharmInfo *info = RogueCharm_Info(data->mon[slot][i].id);
            if (data->mon[slot][i].id != ROGUE_CHARM_NONE && info->effect == effect)
                total += info->magnitude;
        }
        break;
    }

    return total;
}

// Percent of max HP to remove, for CalculateMonStats.
//
// CAPPED AT 90%. A stack of max-HP charms that reached 100% would compute a
// maximum of zero, and a Pokemon with zero max HP is not a weak Pokemon, it is
// a divide-by-zero waiting for whichever HP-bar routine gets there first.
u32 RogueCharm_MaxHpPercentLost(struct Pokemon *mon)
{
    u32 percent = MagnitudeForPersonality(GetMonData(mon, MON_DATA_PERSONALITY),
                                          ROGUE_CHARM_EFFECT_MAX_HP);

    return percent > 90 ? 90 : percent;
}

// Extra percent damage the defender takes, for the damage calculation.
//
// superEffective is passed in rather than derived here, because the type
// multiplier lives in the damage context and this file has no business knowing
// how that context is shaped.
u32 RogueCharm_ExtraDamagePercent(struct Pokemon *mon, bool32 superEffective)
{
    if (ROGUE_CHARM_DAMAGE_SUPER_EFFECTIVE_ONLY && !superEffective)
        return 0;

    return MagnitudeForPersonality(GetMonData(mon, MON_DATA_PERSONALITY),
                                   ROGUE_CHARM_EFFECT_DAMAGE_TAKEN);
}

// Recomputes party stats, so a max-HP charm takes effect the moment it is
// granted or removed rather than at the next level up.
//
// NOTHING ELSE RECALCULATES ON A CHARM CHANGE. CalculateMonStats runs on level
// up, evolution and party load - none of which is "the player just agreed to a
// forbidden move". Without this the Frail charm would be installed, announced,
// and have no effect at all until the Pokemon happened to gain a level.
static void RecalcParty(void)
{
    u32 slot;

    for (slot = 0; slot < PARTY_SIZE; slot++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][slot];

        if (GetMonData(mon, MON_DATA_SPECIES) != SPECIES_NONE)
            CalculateMonStats(mon);
    }
}

// Signed percent of max HP this charm moves at battle start: negative for
// recoil, positive for a heal. NETTED RATHER THAN APPLIED IN TURN, so a
// Pokemon carrying both Unstable and Rejuvenating takes one HP change instead
// of two, and can never be knocked to 1 HP by the recoil half of a pair that
// was meant to come out roughly even.
static s32 HpPercentOf(u8 id)
{
    const struct RogueCharmInfo *info = RogueCharm_Info(id);

    if (id == ROGUE_CHARM_NONE)
        return 0;

    if (info->effect == ROGUE_CHARM_EFFECT_RECOIL)
        return -(s32)info->magnitude;
    if (info->effect == ROGUE_CHARM_EFFECT_HEAL)
        return info->magnitude;

    return 0;
}

// Called from BattleStartClearSetData, which runs in BeginBattleIntro BEFORE
// anything is sent out. That ordering is what makes this cheap: the party HP is
// reduced before gBattleMons is built from it, so the HP bar draws the lowered
// value on the way in and there is no bar to animate down afterwards.
//
// RECOIL CANNOT FAINT. A charm that knocked out the last standing Pokemon would
// be a whiteout with no battle in it, and a party wiped by charms between floors
// would be unrecoverable. It floors at 1 HP instead, which also keeps the effect
// legible - the player sees a sliver rather than a black screen.
void RogueCharm_OnBattleStart(void)
{
    struct RogueRunModifiers *data;
    u32 slot;

    RogueCharm_SyncParty();
    data = RogueCharm_Data();

    // Reset here rather than at battle end: this is the earliest point in a
    // battle, so a battle that exits by an unusual path cannot leave the mask
    // set and mute the next battle's announcements.
    sAnnouncedThisBattle = 0;

    for (slot = 0; slot < PARTY_SIZE; slot++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][slot];
        struct RogueCharm *charms;
        s32 percent = 0, delta;
        u32 hp, maxHp, i;

        if (GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE)
            continue;

        hp = GetMonData(mon, MON_DATA_HP);
        if (hp == 0)
            continue;

        charms = MonCharms(data, slot, mon);
        if (charms != NULL)
        {
            for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
                percent += HpPercentOf(charms[i].id);
        }

        for (i = 0; i < ROGUE_PARTY_CHARM_SLOTS; i++)
            percent += HpPercentOf(data->party[i].id);

        if (percent == 0)
            continue;

        maxHp = GetMonData(mon, MON_DATA_MAX_HP);
        delta = ((s32)maxHp * percent) / 100;

        // A charm the player was told about must do something, and integer
        // division on a low-level Pokemon rounds a real percentage to zero.
        if (delta == 0)
            delta = percent < 0 ? -1 : 1;

        if (delta < 0)
        {
            u32 loss = -delta;

            if (loss >= hp)
                loss = hp - 1;
            hp -= loss;
        }
        else
        {
            hp += delta;
            if (hp > maxHp)
                hp = maxHp;
        }

        SetMonData(mon, MON_DATA_HP, &hp);
    }
}

// ROGUE_CHARM_STAT_* mask bit -> engine Stat. A table rather than a shift,
// because the mask bits are ours and the Stat ids are the engine's, and tying
// one to the other by arithmetic is how a reordered enum silently starts
// dropping the wrong stat.
static const u8 sCharmStatToEngineStat[][2] =
{
    { ROGUE_CHARM_STAT_ATK,   STAT_ATK   },
    { ROGUE_CHARM_STAT_DEF,   STAT_DEF   },
    { ROGUE_CHARM_STAT_SPEED, STAT_SPEED },
    { ROGUE_CHARM_STAT_SPATK, STAT_SPATK },
    { ROGUE_CHARM_STAT_SPDEF, STAT_SPDEF },
};

// Signed stages this charm moves the given stat: negative for a drop, positive
// for a boost. Netted for the same reason HpPercentOf is - a Pokemon holding
// both Cursed and a defence blessing should end where the two leave it, not
// wherever the loop happened to look last.
static s32 StatStagesOf(u8 id, u32 mask, bool8 switchInOnly)
{
    const struct RogueCharmInfo *info = RogueCharm_Info(id);

    if (id == ROGUE_CHARM_NONE || !(info->param & mask))
        return 0;
    if (switchInOnly && !info->reapplyOnSwitchIn)
        return 0;

    if (info->effect == ROGUE_CHARM_EFFECT_STAT_DROP)
        return -(s32)info->magnitude;
    if (info->effect == ROGUE_CHARM_EFFECT_STAT_BOOST)
        return info->magnitude;

    return 0;
}

// switchInOnly narrows this to the charms flagged reapplyOnSwitchIn. The rest
// of the function is identical either way, which is the point - a switch-in
// applying a subtly different set of rules than a battle start is exactly the
// kind of divergence nobody would notice until it mattered.
static void ApplyStatChanges(u32 battler, struct RogueCharm *charms,
                             struct RogueRunModifiers *data, bool8 switchInOnly)
{
    u32 i, j, k;

    for (i = 0; i < ARRAY_COUNT(sCharmStatToEngineStat); i++)
    {
        u32 mask = sCharmStatToEngineStat[i][0];
        u32 stat = sCharmStatToEngineStat[i][1];
        s32 stages = 0;

        if (charms != NULL)
        {
            for (j = 0; j < ROGUE_CHARMS_PER_MON; j++)
                stages += StatStagesOf(charms[j].id, mask, switchInOnly);
        }

        for (k = 0; k < ROGUE_PARTY_CHARM_SLOTS; k++)
            stages += StatStagesOf(data->party[k].id, mask, switchInOnly);

        if (stages == 0)
            continue;

        gBattleMons[battler].statStages[stat] += stages;
        if (gBattleMons[battler].statStages[stat] < MIN_STAT_STAGE)
            gBattleMons[battler].statStages[stat] = MIN_STAT_STAGE;
        else if (gBattleMons[battler].statStages[stat] > MAX_STAT_STAGE)
            gBattleMons[battler].statStages[stat] = MAX_STAT_STAGE;
    }
}

// Called from FIRST_TURN_EVENTS_START, which is the first point where
// gBattlerPartyIndexes maps a battler back to a party slot - without that there
// is no way to know whose charms these are.
//
// DELIBERATELY NOT REAPPLIED ON SWITCH-IN. The v1 vocabulary is "at the start of
// a battle", and the charm text says as much ("as battle begins"). Switching
// therefore clears the drop, which is a coherent thing for the player to play
// around rather than an oversight - but it is a real design decision and the
// place to revisit if afflictions start feeling toothless.
//
// It writes statStages directly rather than going through gQueuedStatBoosts.
// That queue is the totem pipeline and it ends in BattleScript_TotemBoost, which
// prints STRINGID_AURAFLAREDTOLIFE - an aura flaring to life is the wrong thing
// to tell someone whose party was just cursed.
void RogueCharm_QueueBattleStatDrops(void)
{
    struct RogueRunModifiers *data = RogueCharm_Data();
    u32 battler;

    for (battler = 0; battler < gBattlersCount; battler++)
    {
        u32 slot;
        struct Pokemon *mon;

        if (!IsOnPlayerSide(battler))
            continue;

        slot = gBattlerPartyIndexes[battler];
        if (slot >= PARTY_SIZE)
            continue;

        mon = &gParties[B_TRAINER_PLAYER][slot];
        ApplyStatChanges(battler, MonCharms(data, slot, mon), data, FALSE);
    }
}

// Reapplies only the charms flagged reapplyOnSwitchIn, to one battler.
//
// From SwitchInClearSetData, which resets every stat stage to default - so this
// has to run AFTER it, and it does, because it is called at the end.
void RogueCharm_OnSwitchIn(u32 battler)
{
    struct RogueRunModifiers *data;
    u32 slot;

    if (!IsOnPlayerSide(battler))
        return;

    slot = gBattlerPartyIndexes[battler];
    if (slot >= PARTY_SIZE)
        return;

    // Deliberately no SyncParty here. A switch mid-battle does not change which
    // Pokemon own which charms, and syncing inside the battle loop would be
    // reading the party at the one moment the engine is rearranging it.
    data = RogueCharm_Data();
    ApplyStatChanges(battler, MonCharms(data, slot, &gParties[B_TRAINER_PLAYER][slot]),
                     data, TRUE);
}

// Counts one battle off a charm, expiring it at zero.
//
// ROGUE_CHARM_DURATION_RUN and _ACT are both skipped, and neither can be
// confused with an expiring charm: RUN is 0, which a counting charm never
// reaches while still installed, and ACT is 255, which it would have to count
// down through. Act-scoped charms are cleared by RogueCharm_OnDungeonEnd.
static void TickCharm(struct RogueCharm *charm)
{
    if (charm->id == ROGUE_CHARM_NONE)
        return;
    if (charm->duration == ROGUE_CHARM_DURATION_RUN || charm->duration == ROGUE_CHARM_DURATION_ACT)
        return;

    if (--charm->duration == 0)
        charm->id = ROGUE_CHARM_NONE;
}

// DURATIONS TICK AT BATTLE END, NOT BATTLE START, and that is not a style
// choice. Recoil is applied in BeginBattleIntro and stat drops in the first-turn
// events, which are two different moments; a counter decremented at the first of
// them would have already expired a one-battle charm before the second one read
// it, and the charm would silently do half of what it said.
void RogueCharm_OnBattleEnd(void)
{
    struct RogueRunModifiers *data = RogueCharm_Data();
    u32 slot, i;

    for (slot = 0; slot < PARTY_SIZE; slot++)
    {
        for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
            TickCharm(&data->mon[slot][i]);
    }

    for (i = 0; i < ROGUE_PARTY_CHARM_SLOTS; i++)
        TickCharm(&data->party[i]);

    RecalcParty();
}

// ---------------------------------------------------------------------------
// THE SCRIPT INTERFACE.
//
// This is the ONLY door the floor events come through, and keeping it that way
// is what makes charm work and event work separable. An event that reaches into
// struct RogueRunModifiers directly has coupled the two, and the next change to
// charm storage breaks a script that nothing type-checks.
//
// Arguments arrive in the special vars rather than as inline callnative args,
// because a script that gets an inline arg width wrong desynchronises the script
// pointer and misexecutes everything after it - a failure that looks nothing
// like its cause. Special vars are checkable with a debug print.
//
//   VAR_0x8000  charm id            (ROGUE_CHARM_*)
//   VAR_0x8001  duration            (0xFFFF = use the table default)
//   VAR_0x8002  party slot          (mon-scoped grants only)
//   VAR_RESULT  out
// ---------------------------------------------------------------------------

#define ROGUE_CHARM_USE_DEFAULT_DURATION 0xFFFF

static u8 ResolveDuration(u8 id, u16 requested)
{
    if (requested == ROGUE_CHARM_USE_DEFAULT_DURATION)
        return RogueCharm_Info(id)->defaultDuration;

    return requested & 0xFF;
}

// Puts a charm in the first free slot of the row, or refreshes it if the row
// already holds it. REFRESH RATHER THAN STACK, because two copies of Unstable
// would silently double the recoil and nothing in the UI would explain why.
static bool8 InstallCharm(struct RogueCharm *row, u32 count, u8 id, u8 duration)
{
    u32 i;

    for (i = 0; i < count; i++)
    {
        if (row[i].id == id)
        {
            // The longer of the two wins, so a fresh curse cannot shorten one
            // the player is already serving.
            if (duration == ROGUE_CHARM_DURATION_RUN || row[i].duration == ROGUE_CHARM_DURATION_RUN)
                row[i].duration = ROGUE_CHARM_DURATION_RUN;
            else if (duration > row[i].duration)
                row[i].duration = duration;
            return TRUE;
        }
    }

    for (i = 0; i < count; i++)
    {
        if (row[i].id == ROGUE_CHARM_NONE)
        {
            row[i].id = id;
            row[i].duration = duration;
            return TRUE;
        }
    }

    return FALSE;
}

// Re-installs ROGUE_CHARM_ALPHA on every registered Pokemon standing in the
// party. Called from the tail of every sync.
//
// NOT CONSUMED ON FIRST USE, and that is the whole point of the registry.
// Deposit an Alpha and withdraw it again and it is still an Alpha, because
// being one is a property OF THE POKEMON rather than something that happened
// to it. That is deliberately the opposite of how the afflictions behave - the
// note on RogueCharm_SyncParty explains why a boxed mon comes back clean, and
// that reasoning is about curses a player would otherwise park in a box. It
// does not apply to a blessing the Pokemon was born with.
//
// IDEMPOTENT, because InstallCharm refreshes a row it already holds rather
// than stacking a second copy. So running this on every sync costs one row
// scan per occupied slot and can never double the boost.
static void EnsureAlphaCharms(struct RogueRunModifiers *data)
{
    u32 slot, i;

    for (slot = 0; slot < PARTY_SIZE; slot++)
    {
        if (data->personality[slot] == 0)
            continue;

        for (i = 0; i < ROGUE_ALPHA_SLOTS; i++)
        {
            if (data->alphaPersonality[i] != data->personality[slot])
                continue;

            InstallCharm(data->mon[slot], ROGUE_CHARMS_PER_MON,
                         ROGUE_CHARM_ALPHA,
                         RogueCharm_Info(ROGUE_CHARM_ALPHA)->defaultDuration);
            break;
        }
    }
}

// How many Alphas the run has beaten or caught, and the counter that says so.
//
// A PAIR OF ACCESSORS RATHER THAN A REACH INTO THE STRUCT, because
// rogue_charms.h states that everything outside this file goes through these
// functions - and a run counter sharing the struct with the charms is not a
// reason to make an exception, it is a reason the rule is worth having.
//
// SATURATING, not wrapping. Nothing reads an exact count above one, and a u8
// that rolled over to zero would quietly put the Shiny Charm back out of reach
// on the 256th Alpha of a run that will never happen - but the check is one
// comparison and the alternative is a footnote nobody would ever confirm.
u32 RogueCharm_AlphasBeaten(void)
{
    return RogueCharm_Data()->alphasBeaten;
}

void RogueCharm_NoteAlphaBeaten(void)
{
    struct RogueRunModifiers *data = RogueCharm_Data();

    if (data->alphasBeaten < 255)
        data->alphasBeaten++;
}

// Records that this Pokemon is an Alpha, WHEREVER IT CURRENTLY IS - party, box,
// or still being handed over. TRUE if it is registered when this returns,
// including when it already was; FALSE only if the registry is full.
//
// The sync at the end is what makes a catch into the party take effect
// immediately rather than at the next entry point.
bool32 RogueCharm_RegisterAlpha(u32 personality)
{
    struct RogueRunModifiers *data = RogueCharm_Data();
    u32 i;

    // Zero is this struct's empty marker, the same way it is for personality[].
    if (personality == 0)
        return FALSE;

    for (i = 0; i < ROGUE_ALPHA_SLOTS; i++)
        if (data->alphaPersonality[i] == personality)
            return TRUE;

    for (i = 0; i < ROGUE_ALPHA_SLOTS; i++)
    {
        if (data->alphaPersonality[i] == 0)
        {
            data->alphaPersonality[i] = personality;
            RogueCharm_SyncParty();
            return TRUE;
        }
    }

    return FALSE;
}

// VAR_RESULT = TRUE if the charm was installed. FALSE means every slot was
// full, and the CALLER decides what that means - an event that has already
// taken payment must not silently hand over nothing.
void RogueCharm_ScriptGrantMon(void)
{
    struct RogueRunModifiers *data;
    u8 id = gSpecialVar_0x8000 & 0xFF;
    u32 slot = gSpecialVar_0x8002;

    RogueCharm_SyncParty();
    data = RogueCharm_Data();

    gSpecialVar_Result = FALSE;

    if (id == ROGUE_CHARM_NONE || id >= ROGUE_CHARM_COUNT)
        return;
    if (slot >= PARTY_SIZE || data->personality[slot] == 0)
        return;

    gSpecialVar_Result = InstallCharm(data->mon[slot], ROGUE_CHARMS_PER_MON,
                                      id, ResolveDuration(id, gSpecialVar_0x8001));

    // ONLY WHEN IT TOOK. InstallCharm declines a full row, and a journal line
    // for a charm the Pokemon does not carry is worse than no line at all - the
    // player would go looking for it in the charm list and find nothing.
    //
    // HERE RATHER THAN IN InstallCharm, which knows the row but not whose it is,
    // and deliberately not in RogueCharm_DebugGrant: the debug menu must not be
    // able to write the player's history.
    if (gSpecialVar_Result)
        RogueJournal_Append(ROGUE_JOURNAL_CHARM_MON,
                            VarGet(VAR_ROGUE_DUNGEON_FLOOR) + 1,
                            GetMonData(&gParties[B_TRAINER_PLAYER][slot], MON_DATA_SPECIES),
                            id);

    RecalcParty();
}

void RogueCharm_ScriptGrantParty(void)
{
    struct RogueRunModifiers *data;
    u8 id = gSpecialVar_0x8000 & 0xFF;

    RogueCharm_SyncParty();
    data = RogueCharm_Data();

    gSpecialVar_Result = FALSE;

    if (id == ROGUE_CHARM_NONE || id >= ROGUE_CHARM_COUNT)
        return;

    gSpecialVar_Result = InstallCharm(data->party, ROGUE_PARTY_CHARM_SLOTS,
                                      id, ResolveDuration(id, gSpecialVar_0x8001));

    if (gSpecialVar_Result)
        RogueJournal_Append(ROGUE_JOURNAL_CHARM_PARTY,
                            VarGet(VAR_ROGUE_DUNGEON_FLOOR) + 1, id, 0);

    RecalcParty();
}

// VAR_RESULT = TRUE if the charm in VAR_0x8000 is held anywhere - party-wide or
// on any party member. The Shrine needs this to know whether it has anything to
// offer to cleanse before it charges for it.
void RogueCharm_ScriptHas(void)
{
    struct RogueRunModifiers *data;
    u8 id = gSpecialVar_0x8000 & 0xFF;
    u32 slot, i;

    RogueCharm_SyncParty();
    data = RogueCharm_Data();

    gSpecialVar_Result = FALSE;

    for (i = 0; i < ROGUE_PARTY_CHARM_SLOTS; i++)
    {
        if (data->party[i].id == id)
            gSpecialVar_Result = TRUE;
    }

    for (slot = 0; slot < PARTY_SIZE; slot++)
    {
        for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
        {
            if (data->mon[slot][i].id == id)
                gSpecialVar_Result = TRUE;
        }
    }
}

// VAR_RESULT = how many charms the run is carrying, counting each affected
// Pokemon separately. An event that wants to price a cleanse reads this.
void RogueCharm_ScriptCount(void)
{
    struct RogueRunModifiers *data;
    u32 slot, i, count = 0;

    RogueCharm_SyncParty();
    data = RogueCharm_Data();

    for (i = 0; i < ROGUE_PARTY_CHARM_SLOTS; i++)
    {
        if (data->party[i].id != ROGUE_CHARM_NONE)
            count++;
    }

    for (slot = 0; slot < PARTY_SIZE; slot++)
    {
        for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
        {
            if (data->mon[slot][i].id != ROGUE_CHARM_NONE)
                count++;
        }
    }

    gSpecialVar_Result = count;
}

// Removes every charm, returning the number removed in VAR_RESULT so the
// Shrine can refuse to charge for nothing.
void RogueCharm_ScriptCleanseAll(void)
{
    struct RogueRunModifiers *data;
    u32 slot, i, count = 0;

    RogueCharm_SyncParty();
    data = RogueCharm_Data();

    for (i = 0; i < ROGUE_PARTY_CHARM_SLOTS; i++)
    {
        if (data->party[i].id != ROGUE_CHARM_NONE)
        {
            data->party[i].id = ROGUE_CHARM_NONE;
            count++;
        }
    }

    for (slot = 0; slot < PARTY_SIZE; slot++)
    {
        for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
        {
            if (data->mon[slot][i].id != ROGUE_CHARM_NONE)
            {
                data->mon[slot][i].id = ROGUE_CHARM_NONE;
                count++;
            }
        }
    }

    gSpecialVar_Result = count;

    RecalcParty();
}

// Buffers the charm in VAR_0x8000 into gStringVar1 (name) and gStringVar2
// (description), so event dialogue can NAME what it just did.
//
// Without this an event can grant a charm but cannot say which one, and every
// affliction reads as "something feels wrong…". That is not a small loss: the
// visibility rule is not only about a listing screen, it is about the player
// being told, in the moment, what they just agreed to.
void RogueCharm_ScriptBufferName(void)
{
    const struct RogueCharmInfo *info = RogueCharm_Info(gSpecialVar_0x8000 & 0xFF);

    StringCopy(gStringVar1, info->name != NULL ? info->name : sText_DebugCharmNone);
    StringCopy(gStringVar2, info->description != NULL ? info->description : sText_DebugCharmNone);
}

// Buffers the first charm held by the party slot in VAR_0x8002, and puts its id
// in VAR_RESULT (ROGUE_CHARM_NONE if it has none). Lets an event ask "what is
// wrong with this one" without the script needing to enumerate ids.
void RogueCharm_ScriptBufferFirstOnMon(void)
{
    struct RogueRunModifiers *data;
    u32 slot = gSpecialVar_0x8002;
    u32 i;

    RogueCharm_SyncParty();
    data = RogueCharm_Data();

    gSpecialVar_Result = ROGUE_CHARM_NONE;

    if (slot >= PARTY_SIZE)
        return;

    for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
    {
        if (data->mon[slot][i].id == ROGUE_CHARM_NONE)
            continue;

        gSpecialVar_Result = data->mon[slot][i].id;
        gSpecialVar_0x8000 = data->mon[slot][i].id;
        RogueCharm_ScriptBufferName();
        return;
    }
}

// Removes one charm id everywhere it is held. VAR_RESULT = how many copies went.
//
// SEPARATE FROM CleanseAll BECAUSE THE SHRINE ASKED FOR IT: the draft offers to
// remove "a Curse", singular, and an offering that also swept away a blessing
// the player had paid for elsewhere would be a bug they would report as one.
void RogueCharm_ScriptCleanseOne(void)
{
    struct RogueRunModifiers *data;
    u8 id = gSpecialVar_0x8000 & 0xFF;
    u32 slot, i, count = 0;

    RogueCharm_SyncParty();
    data = RogueCharm_Data();

    gSpecialVar_Result = 0;

    if (id == ROGUE_CHARM_NONE || id >= ROGUE_CHARM_COUNT)
        return;

    for (i = 0; i < ROGUE_PARTY_CHARM_SLOTS; i++)
    {
        if (data->party[i].id == id)
        {
            data->party[i].id = ROGUE_CHARM_NONE;
            count++;
        }
    }

    for (slot = 0; slot < PARTY_SIZE; slot++)
    {
        for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
        {
            if (data->mon[slot][i].id == id)
            {
                data->mon[slot][i].id = ROGUE_CHARM_NONE;
                count++;
            }
        }
    }

    gSpecialVar_Result = count;

    RecalcParty();
}

// Clears every charm on the party slot in VAR_0x8002. VAR_RESULT = how many.
// Leaves party-wide charms alone - they are not this Pokemon's to shed.
void RogueCharm_ScriptCleanseMon(void)
{
    struct RogueRunModifiers *data;
    u32 slot = gSpecialVar_0x8002;
    u32 i, count = 0;

    RogueCharm_SyncParty();
    data = RogueCharm_Data();

    gSpecialVar_Result = 0;

    if (slot >= PARTY_SIZE)
        return;

    for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
    {
        if (data->mon[slot][i].id != ROGUE_CHARM_NONE)
        {
            data->mon[slot][i].id = ROGUE_CHARM_NONE;
            count++;
        }
    }

    gSpecialVar_Result = count;

    RecalcParty();
}

// ---------------------------------------------------------------------------
// THE PLAYER-FACING LISTING.
//
// This closes the gap the first pass left open: charms announced themselves in
// battle and were invisible everywhere else, so a player between floors had no
// way to answer "what is wrong with my team". The rule in roguelike-state.md is
// that a modifier the player cannot see is a bug they will report as one, and an
// announcement they have already clicked past does not satisfy it.
//
// A MESSAGE BOX RATHER THAN A SCREEN, and deliberately. The two surfaces that
// would be better - the summary screen and the party menu - are both montmoguri
// forks, and putting the first player-facing charm UI inside a fork means
// re-hosting it after every upstream merge. A start menu entry and a msgbox use
// only vanilla machinery.
// ---------------------------------------------------------------------------

static const u8 sText_CharmListNone[]  = _("Your team carries no charms.");
static const u8 sText_CharmListParty[] = _("Whole team");
static const u8 sText_CharmListJoin[]  = _(": ");

// How many entries fit before the message needs another page. Two lines per
// box is what the standard field message window shows.
#define ROGUE_CHARM_LIST_PER_PAGE 2

// Builds the listing into gStringVar1, paged. Named for the Pokemon rather than
// the party slot, because "Slot 3" means nothing to someone who has reordered
// their team twice.
void RogueCharm_ScriptBufferSummary(void)
{
    struct RogueRunModifiers *data;
    u8 *ptr = gStringVar1;
    u32 slot, i, shown = 0;

    RogueCharm_SyncParty();
    data = RogueCharm_Data();

    for (i = 0; i < ROGUE_PARTY_CHARM_SLOTS; i++)
    {
        if (data->party[i].id == ROGUE_CHARM_NONE)
            continue;

        if (shown != 0)
            *ptr++ = (shown % ROGUE_CHARM_LIST_PER_PAGE) ? CHAR_NEWLINE : CHAR_PROMPT_CLEAR;

        ptr = StringCopy(ptr, sText_CharmListParty);
        ptr = StringCopy(ptr, sText_CharmListJoin);
        ptr = StringCopy(ptr, RogueCharm_GetName(data->party[i].id));
        shown++;
    }

    for (slot = 0; slot < PARTY_SIZE; slot++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][slot];

        for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
        {
            if (data->mon[slot][i].id == ROGUE_CHARM_NONE)
                continue;

            if (shown != 0)
                *ptr++ = (shown % ROGUE_CHARM_LIST_PER_PAGE) ? CHAR_NEWLINE : CHAR_PROMPT_CLEAR;

            ptr = StringCopy_Nickname(ptr, GetMonNickname(mon, gStringVar2));
            ptr = StringCopy(ptr, sText_CharmListJoin);
            ptr = StringCopy(ptr, RogueCharm_GetName(data->mon[slot][i].id));
            shown++;
        }
    }

    if (shown == 0)
        StringCopy(gStringVar1, sText_CharmListNone);
    else
        *ptr = EOS;

    // So a caller can branch on whether there was anything to show.
    gSpecialVar_Result = shown;
}

// ---------------------------------------------------------------------------
// THE DEBUG SURFACE.
//
// This exists because v1 shipped with its two riskiest parts - the personality
// resync and the save migration - reachable only by playing a real run and
// waiting for an event that grants a charm. Neither existed yet. Being able to
// hand a Pokemon a charm and then go and reorder the party turns the test that
// matters from a session into a minute.
// ---------------------------------------------------------------------------

static const u8 sText_DebugCharmParty[] = _("P:");
static const u8 sText_DebugCharmColon[] = _(":");
static const u8 sText_DebugCharmOpen[]  = _("(");
static const u8 sText_DebugCharmClose[] = _(") ");
static const u8 sText_DebugTargetParty[] = _("PARTY");
static const u8 sText_DebugTargetSlot[]  = _("Slot ");

// Compact listing of everything the run holds, as "P:Cursed(3) 2:Unstable(0)".
// Duration is shown because expiry is the thing hardest to eyeball otherwise.
//
// CAPPED AT SIX ENTRIES. The struct can hold twenty-four, and twenty-four names
// would run off the end of a gStringVar - a debug tool that corrupts memory
// while you are hunting a memory bug is worse than no tool.
#define ROGUE_CHARM_DEBUG_MAX_ENTRIES 6

static u8 *AppendCharmEntry(u8 *ptr, u8 id, u8 duration)
{
    ptr = StringCopy(ptr, RogueCharm_Info(id)->name);
    ptr = StringCopy(ptr, sText_DebugCharmOpen);
    ptr = ConvertIntToDecimalStringN(ptr, duration, STR_CONV_MODE_LEFT_ALIGN, 3);
    return StringCopy(ptr, sText_DebugCharmClose);
}

void RogueCharm_GetDebugSummary(u8 *dest)
{
    struct RogueRunModifiers *data;
    u8 *ptr = dest;
    u32 slot, i, shown = 0;

    RogueCharm_SyncParty();
    data = RogueCharm_Data();

    for (i = 0; i < ROGUE_PARTY_CHARM_SLOTS && shown < ROGUE_CHARM_DEBUG_MAX_ENTRIES; i++)
    {
        if (data->party[i].id == ROGUE_CHARM_NONE)
            continue;

        ptr = StringCopy(ptr, sText_DebugCharmParty);
        ptr = AppendCharmEntry(ptr, data->party[i].id, data->party[i].duration);
        shown++;
    }

    for (slot = 0; slot < PARTY_SIZE && shown < ROGUE_CHARM_DEBUG_MAX_ENTRIES; slot++)
    {
        for (i = 0; i < ROGUE_CHARMS_PER_MON && shown < ROGUE_CHARM_DEBUG_MAX_ENTRIES; i++)
        {
            if (data->mon[slot][i].id == ROGUE_CHARM_NONE)
                continue;

            ptr = ConvertIntToDecimalStringN(ptr, slot + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
            ptr = StringCopy(ptr, sText_DebugCharmColon);
            ptr = AppendCharmEntry(ptr, data->mon[slot][i].id, data->mon[slot][i].duration);
            shown++;
        }
    }

    if (shown == 0)
        StringCopy(dest, sText_DebugCharmNone);
}

// target 0 is the party-wide row; 1..PARTY_SIZE is that party slot.
void RogueCharm_DebugGrant(u8 id, u8 target)
{
    struct RogueRunModifiers *data;

    RogueCharm_SyncParty();
    data = RogueCharm_Data();

    if (id == ROGUE_CHARM_NONE || id >= ROGUE_CHARM_COUNT)
        return;

    if (target == 0)
    {
        InstallCharm(data->party, ROGUE_PARTY_CHARM_SLOTS, id,
                     RogueCharm_Info(id)->defaultDuration);
    }
    else if (target <= PARTY_SIZE && data->personality[target - 1] != 0)
    {
        InstallCharm(data->mon[target - 1], ROGUE_CHARMS_PER_MON, id,
                     RogueCharm_Info(id)->defaultDuration);
    }

    RecalcParty();
}

// Names the charm and the target for the debug readout.
void RogueCharm_GetDebugTargetName(u8 target, u8 *dest)
{
    if (target == 0)
        StringCopy(dest, sText_DebugTargetParty);
    else
        ConvertIntToDecimalStringN(StringCopy(dest, sText_DebugTargetSlot),
                                   target, STR_CONV_MODE_LEFT_ALIGN, 1);
}

const u8 *RogueCharm_GetName(u8 id)
{
    const u8 *name = RogueCharm_Info(id)->name;

    return name != NULL ? name : sText_DebugCharmNone;
}

// Expires act-scoped charms when the run crosses into a new dungeon.
//
// DRIVEN BY A REMEMBERED DUNGEON INDEX RATHER THAN BY A BOSS-DEFEAT HOOK. There
// is no single place a run "finishes a dungeon" - it can be left by clearing the
// boss, and the floor counter is also moved by the debug warp - but every path
// onto a floor goes through ApplyRunConfig, which already derives the dungeon.
// Comparing against the last one seen catches all of them, including the ones
// nobody has written yet.
//
// lastDungeon lives in the spare byte the struct already had, so this costs no
// save space and no version bump.
void RogueCharm_OnFloorLoad(u8 dungeon)
{
    struct RogueRunModifiers *data = RogueCharm_Data();

    if (data->lastDungeon == dungeon)
        return;

    data->lastDungeon = dungeon;
    RogueCharm_OnDungeonEnd();
}

// Clears every charm scoped to the act. Called when the run leaves a dungeon.
void RogueCharm_OnDungeonEnd(void)
{
    struct RogueRunModifiers *data = RogueCharm_Data();
    u32 slot, i;

    for (slot = 0; slot < PARTY_SIZE; slot++)
    {
        for (i = 0; i < ROGUE_CHARMS_PER_MON; i++)
        {
            if (data->mon[slot][i].duration == ROGUE_CHARM_DURATION_ACT)
                data->mon[slot][i].id = ROGUE_CHARM_NONE;
        }
    }

    for (i = 0; i < ROGUE_PARTY_CHARM_SLOTS; i++)
    {
        if (data->party[i].duration == ROGUE_CHARM_DURATION_ACT)
            data->party[i].id = ROGUE_CHARM_NONE;
    }

    RecalcParty();
}


