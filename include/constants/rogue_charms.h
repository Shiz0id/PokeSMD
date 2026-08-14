#ifndef GUARD_CONSTANTS_ROGUE_CHARMS_H
#define GUARD_CONSTANTS_ROGUE_CHARMS_H

// Charms and afflictions: the run's persistent per-Pokemon and party-wide
// modifiers. See the charm section of roguelike-state.md for why this lives in
// SaveBlock3 rather than in the var pool - the short version is that charms are
// keyed on PERSONALITY, which is 4 bytes per party member, and the eight free
// vars are 16 bytes in total.
//
// THE V1 EFFECT VOCABULARY IS DELIBERATELY NARROW: a charm may only do what is
// expressible as "at the start of a battle, do X". That covers every drafted
// affliction except the Shady Move Tutor's -15% max HP, which is why the tutor
// ships with Overexerted instead. Persistent stat recalculation is the cliff -
// max HP is derived in CalculateMonStats and recomputed on every level up, so a
// real max-HP modifier is a different and much larger piece of work.

#define ROGUE_CHARM_NONE         0
#define ROGUE_CHARM_UNSTABLE     1   // Volatile Evolution Crystal
#define ROGUE_CHARM_OVEREXERTED  2   // Shady Move Tutor
#define ROGUE_CHARM_CURSED       3   // Fossil Excavation Site
#define ROGUE_CHARM_SLUGGISH     4   // Herbalist's Revival Brew
#define ROGUE_CHARM_EMBOLDENED   5   // the first charm that helps
#define ROGUE_CHARM_REJUVENATING 6
#define ROGUE_CHARM_FRAIL        7   // the DRAFTED Shady Move Tutor cost
#define ROGUE_CHARM_HEXED        8   // the DRAFTED Fossil curse
// The orb at the summit. TWO CHARMS FOR ONE EVENT, because a charm carries
// exactly one effect and the orb is a bargain: it makes the party stronger AND
// it takes its cut at the start of every battle. Splitting them is not a
// workaround - each is announced on its own line, so the player watches the boon
// and the price land separately and learns that the orb is doing both.
//
// PARTY-WIDE AND DURATION_ACT, unlike anything before them. Act length is what
// makes this a decision rather than a permanent upgrade: the orb is worth taking
// into the dungeon you are standing in, and you choose again next time. It is
// also the first use of DURATION_ACT for a boon rather than for an affliction.
//
// APPENDED, NEVER RENUMBERED. Charm ids are stored per Pokemon in SaveBlock3, so
// inserting one in the middle would silently reinterpret every charm an existing
// save is holding. Adding at the end leaves ids 0-8 meaning what they meant.
#define ROGUE_CHARM_ORB_AWAKENED 9
#define ROGUE_CHARM_ORB_BURDENED 10
// The Ability Transposer's price, for a player who would rather pay in flesh
// than in money. PER-MON and DURATION_RUN, which no existing charm was: Cursed
// is party-wide and lasts three battles, Sluggish is per-mon but act-scoped and
// hits Speed. The mon that got the new ability is the one that carries the cost.
#define ROGUE_CHARM_BRITTLE      11
#define ROGUE_CHARM_COUNT        12

// Effect kinds. Every one of these must be applyable at battle start and
// nowhere else - that is the invariant the whole scope rests on, and
// check_charms.py asserts the table never names a kind the applier lacks.
//
// THE FOUR COME IN TWO MIRRORED PAIRS. v1 shipped only the two that hurt, which
// made "charms and afflictions" a system that could only tax the player - an
// event could take but never give, so every bargain had to be paid for in items
// or money. Boost and heal cost almost nothing on top of drop and recoil,
// because both of those already sum contributions across sources and needed
// only a sign.
#define ROGUE_CHARM_EFFECT_NONE       0
#define ROGUE_CHARM_EFFECT_RECOIL     1  // magnitude = percent of max HP lost
#define ROGUE_CHARM_EFFECT_STAT_DROP  2  // magnitude = stages, param = STAT mask
#define ROGUE_CHARM_EFFECT_HEAL       3  // magnitude = percent of max HP restored
#define ROGUE_CHARM_EFFECT_STAT_BOOST 4  // magnitude = stages, param = STAT mask
// The two that are NOT battle-start-only, and the reason the "at the start of a
// battle" rule is now a rule with two named exceptions rather than an invariant.
// Both were held back deliberately; see the charm section of roguelike-state.md.
#define ROGUE_CHARM_EFFECT_MAX_HP     5  // magnitude = percent of max HP removed
#define ROGUE_CHARM_EFFECT_DAMAGE_TAKEN 6  // magnitude = extra percent taken
#define ROGUE_CHARM_EFFECT_COUNT      7

// ROGUE_CHARM_EFFECT_DAMAGE_TAKEN applies only to SUPER-EFFECTIVE hits, which
// is what the Fossil draft asked for and is also what keeps it from being a
// flat damage tax the player cannot play around.
#define ROGUE_CHARM_DAMAGE_SUPER_EFFECTIVE_ONLY TRUE

// param mask for the two stat kinds. Deliberately NOT the engine's
// STAT_* ids - those are indices, and a mask lets one charm drop several stats
// (Cursed drops both defences) without needing a second table row.
#define ROGUE_CHARM_STAT_ATK   (1 << 0)
#define ROGUE_CHARM_STAT_DEF   (1 << 1)
#define ROGUE_CHARM_STAT_SPEED (1 << 2)
#define ROGUE_CHARM_STAT_SPATK (1 << 3)
#define ROGUE_CHARM_STAT_SPDEF (1 << 4)

// Duration, stored in struct RogueCharm.duration.
//
// 0 means "the rest of the run" rather than "expired" because an EMPTY SLOT is
// already unambiguously id == ROGUE_CHARM_NONE. Overloading 0 to mean expired
// would make a permanent charm indistinguishable from an empty slot, which is
// exactly the kind of silent wrong-state this project keeps paying for.
#define ROGUE_CHARM_DURATION_RUN 0
#define ROGUE_CHARM_DURATION_ACT 255  // until the current dungeon ends

// Slot counts. Three per Pokemon and six party-wide is 48 bytes of the ~1612
// free in SaveBlock3, so these are sized for the design rather than the budget.
#define ROGUE_CHARMS_PER_MON   3
#define ROGUE_PARTY_CHARM_SLOTS 6

// Bumping this zeroes every player's charm state on next access. Bump it
// whenever the LAYOUT of struct RogueRunModifiers changes - appending to
// SaveBlock3 leaves old saves reading whatever was in the sector, and charms
// that arrive holding garbage would apply garbage effects.
#define ROGUE_CHARMS_SAVE_VERSION 1

#endif // GUARD_CONSTANTS_ROGUE_CHARMS_H
