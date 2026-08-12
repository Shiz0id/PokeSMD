#ifndef GUARD_CONSTANTS_ROGUE_DUNGEON_H
#define GUARD_CONSTANTS_ROGUE_DUNGEON_H

#include "constants/vars.h"
#include "constants/flags.h"

// Shared between the C generator and the map scripts, so the var aliases are
// defined in exactly one place.
//
// Claiming spare vars rather than adding SaveBlock fields keeps the save
// layout untouched; changing it would invalidate existing saves.
#define VAR_ROGUE_DUNGEON_SEED  VAR_UNUSED_0x40FE
#define VAR_ROGUE_DUNGEON_FLOOR VAR_UNUSED_0x40FF

// Replaces the Birch intro with name entry alone and starts the player in the
// dungeon instead of the moving truck. Set to FALSE to get vanilla back.
#define ROGUE_SLIM_NEW_GAME TRUE

// Kept permanently set. An object event whose flagId is set is not spawned, so
// this hides the placeholder slots map.json has to declare but a given floor
// does not use.
#define FLAG_ROGUE_OBJECT_UNUSED FLAG_UNUSED_0x919

// Set once this floor's boss has paid out. Talking to a trainer the player has
// already beaten runs the post-battle script again - that is how the engine
// reports "no battle to fight here" - so without this the reward sequence
// re-runs on every conversation, and a boss ace can be farmed into a full party
// of clones. Cleared only when a genuinely new floor is rolled, so it survives
// a save and reload on the arena.
#define FLAG_ROGUE_BOSS_REWARD_TAKEN FLAG_UNUSED_0x918

// RogueDungeon_PrepareBossAceOffer results. PARTY_FULL is distinct from NONE so
// the player is told what they walked away from rather than nothing happening.
#define ROGUE_ACE_NONE       0
#define ROGUE_ACE_OFFER      1
#define ROGUE_ACE_PARTY_FULL 2

// map.json must declare exactly DUNGEON_MAX_TRAINERS + DUNGEON_MAX_ITEMS object
// events: the engine reads templates from the save block but takes the count
// from ROM. check_dungeon_objects.py enforces that, because the two live in
// different files and nothing else would notice them drifting apart.
#define DUNGEON_MAX_TRAINERS            4
#define DUNGEON_TRAINER_FLOORS_PER_EXTRA 25
#define DUNGEON_TRAINER_SIGHT_RANGE      4

// Item balls. These cost NO new RAM: gSaveBlock1Ptr->objectEventTemplates is a
// fixed 64 entries whether or not a map uses them, and the floor spends 4 on
// trainers. The live-sprite limit (OBJECT_EVENTS_COUNT, 16) is the real ceiling
// and is nowhere near - objects spawn by proximity, so what matters is how many
// crowd one screen, not how many exist.
#define DUNGEON_MAX_ITEMS            8
#define DUNGEON_ITEM_MIN             2
#define DUNGEON_ITEM_FLOORS_PER_EXTRA 20

// Object event local ids are 1-based and the trainers hold the first block, so
// item ball i is localId DUNGEON_ITEM_FIRST_LOCAL_ID + i.
#define DUNGEON_ITEM_FIRST_LOCAL_ID (DUNGEON_MAX_TRAINERS + 1)

// Hidden items. A DIFFERENT SEAM ENTIRELY: these are bg events, which the
// engine never copies to the save block, so the template door the trainers,
// the Unown and the item balls all go through does not reach them. They are
// generated into EWRAM and gMapHeader.events is repointed at them.
//
// This is the only part of the item system that costs real RAM - 12 bytes per
// placement - and it is what the dowsing machine has been waiting for.
#define DUNGEON_MAX_HIDDEN              16
#define DUNGEON_HIDDEN_MIN               2
#define DUNGEON_HIDDEN_FLOORS_PER_EXTRA 16

// Evolution stones are the one buried thing that is not a held item, so they
// do not live in sLootHeld: that table is graded by tier and checked for a
// ceiling that never drops, and a stone has no place on that ladder. They get
// a slot of their own instead - at most the first buried item on a floor, one
// floor in DUNGEON_STONE_ODDS, from DUNGEON_STONE_FIRST_FLOOR.
//
// They are here because Clefairy and Pikachu evolve by stone and by nothing
// else, so without this those picks are locked at a stage-1 statline for all
// 115 floors. The odds are deliberately generous rather than precious - a
// stone is dead weight to most picks, and a Clefairy run that never finds one
// is the whole bug this closes.
//
// ODDS IS PER FLOOR, NOT PER STONE, and sBuriedStones is picked from
// uniformly - so adding a stone to that table DIVIDES the rate of every stone
// already in it. Two stones at 1-in-7 is what keeps each of them at the
// 1-in-14 the Clefairy fix was measured against; the shop stocks all ten
// because a shelf can be wide without changing anyone's odds of finding the
// one thing that makes their pick work.
#define DUNGEON_STONE_FIRST_FLOOR    4
#define DUNGEON_STONE_ODDS           7

// Crafting materials are buried too, and they are ADDITIVE rather than a
// share of the held-item roll. sLootHeld is a graded ladder whose ceiling is
// checked never to drop, and a Tiny Mushroom has no place on it -- the same
// argument that gave evolution stones a slot of their own. Substituting
// materials into that roll would also quietly halve the rate of the held
// items a run's whole power curve is built on.
//
// So a floor buries its usual 2-9 held items AND this many materials. Worst
// case is floor 115: 2 + 115/16 = 9 held, plus 3, which is 12 against
// DUNGEON_MAX_HIDDEN 16. That headroom is not spare change -- a hidden
// item's id IS its flag, so the cap is a flag range, not an array bound.
// Two rather than three, now that the rocks carry the mineral half.
#define DUNGEON_MATERIALS_PER_FLOOR  2

// The engine derives a hidden item's flag as
// hiddenItemId + FLAG_HIDDEN_ITEMS_START, so the ids we choose ARE flags and
// have to be ones nothing else owns. Vanilla's block runs 0x00..0x6F, and
// 0x1F4 + 0x70 lands exactly on FLAG_UNUSED_0x264 with a long free run after
// it. A collision here would mark a vanilla hidden item collected on a save
// that never visited Hoenn, so rogue_dungeon.c asserts the arithmetic rather
// than trusting this comment.
#define DUNGEON_HIDDEN_FIRST_ID 0x70

// Berry trees, on the themes that have soil. Object events again, so like the
// item balls they come out of the 64 templates already allocated.
#define DUNGEON_MAX_BERRIES 4

// Berry tree ids index gSaveBlock1Ptr->berryTrees[BERRY_TREES_COUNT], which is
// 128 long and of which vanilla names 0..89. Ours start above that. Unlike the
// hidden item flags these are not shared with anything, but they are still
// somebody else's numbering and rogue_dungeon.c asserts the headroom.
#define DUNGEON_BERRY_FIRST_TREE_ID 90

// What a tree gives when picked. berryYield is a 5-BIT field, so 31 is the
// ceiling, and the stock range is roughly 2-6 - this sits at the top of it
// rather than past it, so a picked tree reads as a good tree rather than as a
// broken one.
#define DUNGEON_BERRY_YIELD 6

// Object event local ids again: trainers, then item balls, then these.
#define DUNGEON_BERRY_FIRST_LOCAL_ID \
    (DUNGEON_MAX_TRAINERS + DUNGEON_MAX_ITEMS + 1)

// Mining rocks: the source of crafting materials, and the reason
// sLootMaterials no longer buries a shard.
//
// The mining minigame was vendored and wired to nothing -- reachable only
// from the debug menu -- and its own reward table is already exactly the
// crafting tree's mineral half: all four shards, Heart Scale, Star Piece,
// and six evolution stones outright. Burying those as well would have been
// two sources for one thing, and the worse of the two: a dowsing hit is a
// press of A, and the minigame is a decision about where to dig with a
// stress meter running.
//
// THREE, AND THE NUMBER IS A TEMPLATE BUDGET RATHER THAN A TASTE. map.json
// declares one object event per possible placement and the engine takes the
// COUNT from ROM, so every rock is a slot whether or not a floor uses it.
// The floor already declares 16 against an OBJECT_EVENTS_COUNT of 16 that
// includes the player and now a follower, so these three take it to 19 and
// spawning degrades the way the game corner's 28 do: proximity decides, and
// something furthest away silently does not appear. The per-map ceiling is
// 64, so this is legal; it is the live sprite limit that is tight.
#define DUNGEON_MAX_ROCKS 3

#define DUNGEON_ROCK_FIRST_LOCAL_ID \
    (DUNGEON_MAX_TRAINERS + DUNGEON_MAX_ITEMS + DUNGEON_MAX_BERRIES + 1)

// Floor events: the one thing on a floor that asks the player a question.
//
// ONE SLOT, AND THE NUMBER IS THE DESIGN. Everything else on a floor is a
// quantity - more trainers deeper, more item balls deeper - and an event is not
// that. It is a thing that happens, so a floor has at most one and most floors
// have none. It also keeps the template cost to exactly one: the floor already
// declares 19 against an OBJECT_EVENTS_COUNT of 16 that includes the player and
// a follower, so this takes it to 20 and spawning degrades the documented way,
// by proximity, with something furthest away silently not appearing. The per-map
// ceiling is 64.
//
// ONE FLOOR IN FOUR, which is ~29 events across a 115-floor run. That is a lot
// of draws for a small table, which is why the table is BANDED by depth rather
// than uniform - the events a run meets in its first forty floors are not the
// ones it meets after eighty, so the repetition is spread across subsets instead
// of being felt against the whole pool.
#define DUNGEON_MAX_EVENTS 1
#define DUNGEON_EVENT_ODDS 4

// A gfxId of this in sFloorEvents means "the sprite is the species the floor
// rolled", which is how the injured Pokemon wears its own overworld sprite.
// 0xFFFF rather than 0, because 0 is a real graphics id.
#define DUNGEON_EVENT_GFX_ROLLED 0xFFFF

// THE TRADE IS SELF-BALANCING, and that is why it needs no other rule. The
// stranger's Pokemon arrives at the level of the one handed over plus this, so
// trading the weakest thing on the bench at floor 80 returns a level-15 random
// against a curve of 45 - useless. Getting something that can fight means giving
// up something that could. The cost scales itself.
//
// The species comes from sSafariLandSpecies: 161 entries, sorted weakest to
// strongest, every one checked present in the ROM, and every one deliberately a
// species the run cannot obtain any other way. A uniform roll over that is the
// payoff-or-bust the trade is for - and it is also why the Safari ladder is the
// pool for all three of these events rather than the floor's own. A cave mon
// would be more thematic and strictly less valuable.
#define DUNGEON_TRADE_LEVEL_BONUS 5

// The injured Pokemon. One in three is feigning, and it ambushes that many
// levels above the floor - so approaching is the gamble and the odds are good
// enough that approaching is usually right.
//
// BOTH ARE ROLLED AT PREPARE TIME, from the floor's seed, which is what makes
// them un-scummable: saving and reloading regenerates the same floor and so the
// same outcome. Rolling on interaction would let a player reload until it joins.
#define DUNGEON_INJURED_AMBUSH_ODDS  3
#define DUNGEON_INJURED_AMBUSH_BONUS 5

// Rotten berries. One tree in four yields this instead of DUNGEON_BERRY_YIELD.
//
// DERIVED FROM A HASH, NOT FROM DungeonRandom. PlantFloorBerryTrees runs from
// the template loader rather than from PrepareFloor, so the generation stream is
// not the right thing to draw from there - it has already been spent, and the
// loader does not run on every path. DecorHash keyed on the floor seed and the
// tree index is deterministic, reproduces on reload, and moves nothing else on
// the floor. Same reason the cosmetic passes use it.
//
// One rather than zero. A tree that gives NOTHING is a dud, not a gamble - there
// is no decision in walking up to it - and reducing the yield keeps the tree
// worth picking while making the good ones feel like the good ones.
#define DUNGEON_BERRY_ROTTEN_ODDS  4
#define DUNGEON_BERRY_ROTTEN_YIELD 1

#define DUNGEON_EVENT_FIRST_LOCAL_ID                                   \
    (DUNGEON_MAX_TRAINERS + DUNGEON_MAX_ITEMS + DUNGEON_MAX_BERRIES    \
     + DUNGEON_MAX_ROCKS + 1)

// Runs finished, and whether any ever has. The counter is what the run-complete
// message reads back; the flag is the door for post-first-run content, which is
// why it is a flag rather than a comparison on the counter - what unlocks
// should not have to know the number.
//
// Both are vars/flags rather than SaveBlock fields, so the save layout is
// untouched, and both survive RogueDungeon_ResetRun - which wipes the party,
// the bag and the coins, and deliberately not these.
#define VAR_ROGUE_RUNS_COMPLETED VAR_UNUSED_0x40F8
#define FLAG_ROGUE_RUN_COMPLETED FLAG_UNUSED_0x91C

// The death summary, and the two vars behind it.
//
// VAR_ROGUE_BEST_FLOOR is the deepest floor any run has reached and
// VAR_ROGUE_LAST_RUN_FLOOR is the floor the run that just ended died on. Both
// hold the DISPLAYED floor - VAR_ROGUE_DUNGEON_FLOOR + 1, the number
// RogueDungeon_GetFloorName prints - rather than the internal counter, because
// zero has to mean "no summary is waiting" and dying on the very first floor
// is not only possible, it is the commonest way a run ends.
//
// Like the two above they survive RogueDungeon_ResetRun, which is the whole
// point: a loss is the only time either is written and a wipe is what follows
// it. Claimed from the tail of the unused pool - checked against this file
// rather than vars.h, since every id the project holds is an alias and the pool
// still calls it unused.
#define VAR_ROGUE_BEST_FLOOR     VAR_UNUSED_0x40FB
#define VAR_ROGUE_LAST_RUN_FLOOR VAR_UNUSED_0x40FC

// THE DUNGEON ORDER SHUFFLE, gated behind FLAG_ROGUE_RUN_COMPLETED so a first
// playthrough walks vanilla's own progression - Petalburg Woods to Roxanne,
// through to Steven - and the shuffle is what a clear unlocks.
//
// The gate is not only flavour. A shuffle is allowed to be lumpy precisely
// because the player has already won once, which is what lets this ship with no
// boss scaling and no re-authored species pools.
//
// TWO GROUPS, PERMUTED SEPARATELY AND NEVER ACROSS. DungeonIndexOf,
// DungeonLengthOf and DungeonFloorWithin are pure functions of the dungeon SLOT,
// so a permutation confined to the eight gyms or to the Elite Four leaves every
// floor boundary in the run byte-identical. Moving a five-floor Elite Four
// dungeon into a ten-floor gym slot is not a tuning problem, it shifts every
// boundary after it.
//
// The gyms move in BANDS OF TWO, which is what keeps this free. Measured against
// the real curve and the real stock parties, band-of-two permutations hold every
// boss within -6.5 to +8.0 levels of its floor, against a shipped tolerance of
// -4.6 (Winona) to +5.3 (Steven). Bands of four reach -16.5 to +16.8, and a free
// permutation of all eight is ruinous in BOTH directions - Juan on floor 10 at
// +33.8, a level-13 two-mon Roxanne on floor 80 at -32.0 - which is why band
// width is the difficulty knob and not a preference.
//
// verify_run_structure.py enumerates every permitted permutation and asserts
// that tolerance, so widening a band fails the build rather than the run.
#define DUNGEON_SHUFFLE_BAND 2

// The run's order, rolled once when the starters are picked and constant for the
// rest of the run. Low DUNGEON_GYM_BANDS bits are one swap-or-not per gym band;
// the bits above them index the Elite Four permutation table.
//
// IT NEEDS A VAR OF ITS OWN. VAR_ROGUE_DUNGEON_SEED cannot carry this: the
// stairs script warps, which re-enters LoadMapFromWarp and rolls a fresh seed
// every single floor. An order derived from it would reshuffle the dungeons
// under the player as they descended.
//
// Zero means vanilla order, which covers all three ways that happens - the gate
// is closed, the player turned the toggle off, or the roll genuinely came up
// identity. The three are indistinguishable because they produce the same run.
#define VAR_ROGUE_RUN_ORDER VAR_UNUSED_0x40FD

// SET means the player turned the shuffle OFF, so clear - the value every save
// already holds - is on. Stored inverted for that reason alone: the toggle
// defaults to enabled once unlocked, and a flag cannot default to set. Same
// trick optionsAutoRun already plays in option_menu.c.
#define FLAG_ROGUE_VANILLA_ORDER FLAG_UNUSED_0x91F

// Run lifecycle. A run needs starters on a new game and again after a whiteout,
// which is what makes a loss send the player back to the beginning.
#define VAR_ROGUE_RUN_STATE      VAR_UNUSED_0x40F7
#define ROGUE_RUN_NEEDS_STARTERS 0
#define ROGUE_RUN_ACTIVE         1

#define DUNGEON_STARTER_LEVEL 10
#define DUNGEON_STARTER_COUNT 2

// Handed out at the start of every run until shops exist.
#define ROGUE_RUN_STARTING_BALLS 100

// Exp Share, party-wide and permanent. The expansion already implements this -
// I_EXP_SHARE_FLAG in config/item.h means "every party mon gains experience
// while this flag is set" - so the whole feature is a flag, not code.
//
// It costs participants NOTHING, which is worth knowing before tuning anything
// else. B_SPLIT_EXP is GEN_LATEST, so the branch that runs gives the sent-in
// mon the full award and the rest half on top; it is additive, not a split.
// That is the Gen 7 behaviour rather than the Gen 5 one.
//
// Set on every dungeon floor load rather than once at run start, because a save
// made before this existed would never pass through the run-start script again.
#define FLAG_ROGUE_EXP_SHARE FLAG_UNUSED_0x91A

// The ORAS dowsing machine's ACTIVE state - it is a mode the player toggles by
// using the Itemfinder, not a compile-time enable. The engine ships the whole
// mechanic (src/oras_dowse.c); assigning it a flag is what turns it on.
//
// Note what being in that mode costs: field_player_avatar.c refuses to run
// while it is set, and item_use.c refuses to start it at all while surfing or
// underwater - so it is unavailable on the ocean and seafloor themes.
#define FLAG_ROGUE_DOWSING_ACTIVE FLAG_UNUSED_0x91B

// The variable rod's two technique unlocks, and the var remembering which
// technique was last chosen. Read by the bag's context menu and by
// ItemUseOnFieldCB_VariableRod through the OW_* config macros in
// include/config/overworld.h, which is where upstream's implementation expects
// to find them.
//
// The var needs no initialisation and deliberately has none: OLD_ROD is 0, so a
// save that has never opened the menu already reads as the Old Rod technique.
//
// CHECKED AGAINST THE ROGUELIKE'S OWN CLAIMS FIRST, which is not the same as
// checking vars.h - every id this project takes is an ALIAS of a VAR_UNUSED_/
// FLAG_UNUSED_ name, so the pool still calls them unused. 0x40F9 and
// 0x91D-0x91E are genuinely free; vars_frlg.h names 0x40F9 as VAR_0x40F9 but
// nothing in the tree references it.
#define VAR_ROGUE_ROD_TECHNIQUE        VAR_UNUSED_0x40F9
#define FLAG_ROGUE_ROD_GOOD_TECHNIQUE  FLAG_UNUSED_0x91D
#define FLAG_ROGUE_ROD_SUPER_TECHNIQUE FLAG_UNUSED_0x91E

// Which dungeon each technique arrives in, as a DUNGEON INDEX rather than a
// floor number. Floors per dungeon are not uniform - the Elite Four run five
// where the gyms run ten - so a raw floor threshold silently moves relative to
// the run's shape the moment anything is restructured. See DungeonIndexOf.
//
// Good at dungeon 3 puts it a little before halfway through the gyms; Super at
// dungeon 8 is the first Elite Four dungeon, so the Super technique and the
// gauntlet arrive together.
#define DUNGEON_ROD_GOOD_DUNGEON  3
#define DUNGEON_ROD_SUPER_DUNGEON 8

// Wild encounter experience, as a percentage. Trainers are deliberately NOT
// boosted: the level curve is fitted to the stock bosses' unscaled parties, and
// the trainers on a floor are scaled to the curve, so multiplying those would
// move both sides of a comparison the curve depends on.
//
// 150 rather than 200 because the Exp Share above is itself a large increase to
// PARTY experience, and the two compound. This is the knob to turn once play
// says which way it is wrong.
#define ROGUE_WILD_EXP_PERCENT 150

// THE ANTI-GRIND CLOCK. Nothing else in a run depletes - the rest stop heals
// for free, revives never retire from the loot table, and a floor's grass can
// be walked forever - so without this the level curve is a suggestion and the
// only real cost of any fight is the player's patience. A roguelike needs the
// run to push back; this is the cheapest thing that makes it.
//
// The shape is a taper, not a cliff. The first ROGUE_GRIND_FREE_KOS wild
// knockouts on a floor pay ROGUE_WILD_EXP_PERCENT in full, and every one after
// that pays ROGUE_GRIND_STEP less than the last until it bottoms out at
// ROGUE_GRIND_MIN_PERCENT. A player crossing a floor normally never sees it; a
// player standing in one grass patch feels it within a couple of minutes and
// can still catch, still earn, and still leave whenever they like.
//
// TWELVE is measured against a traverse, not chosen. Grass is ~41% of walkable
// floor and a floor is crossed once, so a full clear lands well inside it - the
// allowance has to cover the floor the player is actually playing or it is a
// punishment rather than a clock.
//
// Wild knockouts only. Trainers are finite by construction and are the side the
// level curve is DEFINED against, which is the same reason ROGUE_WILD_EXP_PERCENT
// does not touch them.
#define ROGUE_GRIND_FREE_KOS    12
#define ROGUE_GRIND_STEP        15
#define ROGUE_GRIND_MIN_PERCENT 25

// The clock itself, packed as (floor << 8) | knockouts.
//
// PACKED WITH ITS FLOOR ON PURPOSE, so it needs no reset hook anywhere and
// cannot be save-scummed. A count that lived on its own would have to be
// cleared when the floor changes, and the only hook that catches every entry
// path - ApplyRunConfig - also runs on load-from-save, so saving and reloading
// on the spot would hand back a fresh allowance. Storing the floor the count
// belongs to makes the reset implicit: a mismatch reads as zero, and a reload
// restores a count that still matches its floor.
#define VAR_ROGUE_FLOOR_WILD_KOS VAR_UNUSED_0x40FA

// The Unown watching the way-station. map.json declares this many slots after
// the three staff, and the seeder decides how many actually spawn and where -
// the engine takes the object COUNT from ROM but reads the TEMPLATES out of the
// save block, which is the same door the trainer placer goes through.
#define REST_STOP_UNOWN_SLOTS            6
#define REST_STOP_UNOWN_FIRST_SLOT       3   // template index, so local id 4

// Two watching near the top of the run, six by the end. 24 is picked against
// the floors a rest stop is actually reached on - 11, 21, ... 101, 106 - so the
// count steps up every other visit or so rather than all at once.
#define REST_STOP_UNOWN_MIN              2
#define REST_STOP_UNOWN_FLOORS_PER_EXTRA 24

#endif // GUARD_CONSTANTS_ROGUE_DUNGEON_H
