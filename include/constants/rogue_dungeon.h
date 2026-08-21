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

// Counts objects on a floor so the live sprite ceiling can be read instead of
// counted by hand.
//
// THE NUMBER THAT MATTERS IS NOT WHAT IS ON SCREEN NOW. Object events spawn and
// despawn by proximity as the player walks, so the interesting moment is the
// WORST one, and standing in it is exactly when you are least able to count.
// This records peaks and refusals as they happen and hands them back afterwards,
// so one lap of a floor answers the question.
//
// A refusal is the thing to watch: TrySpawnObjectEventTemplate returns
// OBJECT_EVENTS_COUNT when no slot is free and the object silently does not
// appear, which is invisible from inside the game by construction.
#define ROGUE_DEBUG_OBJECT_CENSUS TRUE

// map.json must declare exactly DUNGEON_MAX_TRAINERS + DUNGEON_MAX_ITEMS object
// events: the engine reads templates from the save block but takes the count
// from ROM. check_dungeon_objects.py enforces that, because the two live in
// different files and nothing else would notice them drifting apart.
// SEVEN, so the FIVE below has somewhere to climb to. A floor has ten rooms,
// and what matters is not the object count but how many rooms hold nothing a
// player would cross a room for - tools/rogue/room_density.py prints the table.
// This started at one trainer and two item balls, which left 7.3 rooms of ten
// with only a tree, a rock or bare ground in them.
#define DUNGEON_MAX_TRAINERS            7
// FIVE FROM THE FIRST FLOOR. Trainers are the densest content a floor has - a
// battle is the thing a room is FOR - so the floor of this number does more for
// how a floor reads than any other constant here.
#define DUNGEON_TRAINER_MIN             5
// 5 to floor 39, 6 to 79, 7 after. Mirrors the item ramp: the ceiling lands
// inside a 115-floor run rather than just outside it.
#define DUNGEON_TRAINER_FLOORS_PER_EXTRA 40
#define DUNGEON_TRAINER_SIGHT_RANGE      4

// Item balls. These cost NO new RAM: gSaveBlock1Ptr->objectEventTemplates is a
// fixed 64 entries whether or not a map uses them, and the floor spends 4 on
// trainers. The live-sprite limit (OBJECT_EVENTS_COUNT) is the real ceiling, and
// it is 24 now rather than vanilla's 16 - the floor's 21 declared objects plus
// the player and a follower fit under it with one to spare.
//
// FOUR AT THE BOTTOM, NOT TWO, for the room-coverage reason on the trainers
// above. The ramp is stretched to match so the ceiling still lands inside a run
// (4 + 115/25 = 8) rather than being reached in the first quarter of it.
// MAX is untouched: raising it would mean more declarations in map.json and the
// floor is already 23 of 24 live slots with the player and a follower.
#define DUNGEON_MAX_ITEMS            8
#define DUNGEON_ITEM_MIN             4
#define DUNGEON_ITEM_FLOORS_PER_EXTRA 25

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
//
// A CEILING, NOT A COUNT - and it was a count until play found it. PlaceBerryTrees
// looped to MAX with no depth term, so floor 1 of a berry theme stood four trees
// and three rocks against one trainer and two item balls: seven pieces of scenery
// to three of content, at the exact moment a run makes its first impression. The
// items and trainers beside it had scaled with depth all along, so this reads as
// the same shape and now behaves like it.
#define DUNGEON_MAX_BERRIES 4
// SET EQUAL TO MAX ON PURPOSE, which makes the scaling above inert for berries:
// four trees on every floor with soil, as before. Early floors were yielding
// under one good tree once the count scaled down, and berries are an early
// resource. The rot fix carries the complaint on its own now - odds of 8 give
// 1.37 duds of 4 rather than 1.75 - so the count did not have to.
// Rocks still scale. To re-enable berry scaling, drop this below MAX.
#define DUNGEON_BERRY_MIN   4
// Inert while MIN == MAX. Kept so the shape is still there to turn back on.
#define DUNGEON_BERRY_FLOORS_PER_EXTRA 55

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
// These three take the floor's declarations to 19. THAT WAS OVER THE LIVE
// CEILING while OBJECT_EVENTS_COUNT was vanilla's 16 and the player and a
// follower came out of the same budget, and the symptom was the documented one:
// proximity decides, and something furthest away silently does not appear.
// OBJECT_EVENTS_COUNT is 24 now and the whole floor fits. The per-map TEMPLATE
// ceiling is a separate 64 and was never the tight one.
#define DUNGEON_MAX_ROCKS 3
// SET EQUAL TO MAX, as the berries are. The scaling was added when the floor was
// over the live sprite ceiling and something had to give; OBJECT_EVENTS_COUNT is
// 24 now, so 21 declared objects plus the player and a follower all fit and the
// density does not have to be bought back. To re-enable scaling, drop below MAX.
#define DUNGEON_ROCK_MIN  3
// Inert while MIN == MAX. Kept so the shape is still there to turn back on.
#define DUNGEON_ROCK_FLOORS_PER_EXTRA 50

#define DUNGEON_ROCK_FIRST_LOCAL_ID \
    (DUNGEON_MAX_TRAINERS + DUNGEON_MAX_ITEMS + DUNGEON_MAX_BERRIES + 1)

// Floor events: the one thing on a floor that asks the player a question.
//
// ONE SLOT, AND THE NUMBER IS THE DESIGN. Everything else on a floor is a
// quantity - more trainers deeper, more item balls deeper - and an event is not
// that. It is a thing that happens, so a floor has at most one and most floors
// have none. It also keeps the template cost low: with the prop below, the two
// take the floor's declarations to 21, against an OBJECT_EVENTS_COUNT that is 24
// now. Player and follower come out of the same budget, so that is 23 of 24 -
// which is the reason this event was the thing most likely to lose its slot back
// when the ceiling was 16, being frequently the furthest object from the player.
//
// ONE FLOOR IN FOUR, which is ~29 events across a 115-floor run. That is a lot
// of draws for a small table, which is why the table is BANDED by depth rather
// than uniform - the events a run meets in its first forty floors are not the
// ones it meets after eighty, so the repetition is spread across subsets instead
// of being felt against the whole pool.
// Two: the event's NPC, and an optional PROP placed beside it - the berry tree
// the herbalist works from, the rock the hiker is digging at. The prop is a
// second OBJECT EVENT, not a metatile, because a metatile id means a different
// thing under every tileset pair and an event can appear under any of them.
#define DUNGEON_MAX_EVENTS 2
// A PERCENTAGE, not a 1-in-N. It was 1 in 4, which is the only shape a modulo
// can express - 50% and 25% are reachable that way but 75% is not, and the
// difference between "one floor in four" and "most floors" is a design decision
// that should not be constrained by how the roll happens to be written.
//
// Still exactly ONE draw from the seeded stream, which is the part that matters:
// every placer shares it, and changing the number of draws here would move every
// object placed afterwards.
#define DUNGEON_EVENT_PERCENT 50

// Relative frequency, used by PlaceEvents. ZERO MEANS DEFAULT, not "never" - a
// row that forgets to set a weight must behave normally rather than silently
// vanishing from the table, which is the class of failure this project keeps
// paying for. Rarity is therefore expressed as a weight BELOW the default.
#define DUNGEON_EVENT_WEIGHT_DEFAULT 10
#define DUNGEON_EVENT_WEIGHT_RARE     1
// Above default, for the one or two events a run should be BUILT around rather
// than merely offered. Note that a weight is relative to what else is eligible
// at that depth, so this does not buy a fixed frequency - check_floor_events.py
// reports the expected count per run, which is the number to actually read.
#define DUNGEON_EVENT_WEIGHT_COMMON  20

// themeMask of 0 means "any theme", so a row that omits it is unrestricted - the
// same default-safe reasoning as the weight. Otherwise it is a bitmask over enum
// DungeonThemeId, so one event can be allowed in several themes at once. That
// matters for props that only some tilesets carry.
#define DUNGEON_EVENT_THEME(t) (1 << (t))
#define DUNGEON_EVENT_ANY_THEME 0

// propGfxId of this means no prop. Graphics id 0 is the player's own sprite,
// which is never a usable prop, so zero is both a safe sentinel and the right
// default for a row that does not want one.
#define DUNGEON_EVENT_NO_PROP 0

// Set once a floor's event has been TAKEN, so it cannot be taken twice.
//
// A TEMP VAR, and that is the whole trick. The first 0x10 vars are wiped by
// ClearTempFieldEventData on every map load, and every floor is reached by a
// warp - so this clears itself on arrival with no flag spent, no var spent from
// the eight that are left, and no cleanup code that could be forgotten.
//
// THIS EXISTS BECAUSE THE ORIGINAL REASONING HAD A HOLE. The shared event tail
// argued that an NPC need not be removed after use because a floor is never
// revisited, which is true ACROSS floors and says nothing about talking to the
// same NPC twice ON one. Every event was repeatable: infinite eggs, infinite
// heals, infinite money from the fossil, and a trader that ratchets a Pokemon
// up five levels per conversation. Found by playing, not by any check.
//
// Set on the COMMITTING path only. Declining an offer and walking back is fine;
// it is taking the reward twice that is not.
#define VAR_ROGUE_EVENT_SPENT VAR_TEMP_0

// Scratch for an event that has to remember something ACROSS a msgbox, because
// MSGBOX_YESNO writes its answer into VAR_RESULT and destroys whatever a special
// left there. The injured Pokemon needs exactly that: it asks before it reveals
// whether the thing swaying at you is hurt or waiting.
//
// Temp for the same reason as above - it cannot leak into the next floor.
#define VAR_ROGUE_EVENT_SCRATCH VAR_TEMP_1

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

// THE SUDOWOODO IN THE GROVE. One floor in this many that has berry trees hides
// a Sudowoodo among them, wearing the berry tree's own graphics, movement type
// and a planted berry so it is INDISTINGUISHABLE from the real ones until it is
// touched.
//
// It costs no object event slot and no event slot. The generator already
// rewrites templates[slot].script per slot, so the disguise is one pointer: the
// imposter is a berry tree in every other respect. That is also why it has to be
// a berry tree rather than a floor event wearing tree graphics - a real tree is
// drawn from its BERRY STAGE, and an object event with no planted berry behind
// it renders as nothing.
//
// It is the answer to a floor reading as trees and rocks. Scenery the player has
// learned to walk past becomes a thing worth a second look for the rest of the
// run, and nothing else on the floor had to change to buy that.
//
// HASHED, NOT DRAWN, for exactly the reason the rot below is: this is decided in
// the template loader, which runs on every load of a floor including from a
// save, so it must reproduce without touching the generation stream. Rolling on
// interaction instead would let a player reload until the tree was real.
// THE PHANTOM. A floor event with NO OBJECT EVENT AT ALL - it is a step count
// and a script, which makes it the only thing in the pool that cannot be walked
// past, ignored, or seen coming.
//
// One floor in this many is haunted. On arrival the player is told the floor
// feels wrong; some number of steps later it takes them.
//
// THE THRESHOLD IS HASHED, NOT FIXED, and that is the whole tension. A constant
// 120 steps is a timer the player learns to count, after which the warning means
// "you have 120 steps"; a threshold drawn from the floor seed between MIN and
// MAX means the warning only ever means "soon". Same argument as the berry rot
// guarantee: the mechanic has to be legible without being schedulable.
//
// Counted in VAR_ROGUE_PHANTOM_STEPS rather than an EWRAM static because temp
// vars are SAVED but cleared on map change - which is exactly a floor's
// lifetime, and survives a save and reload mid-floor without granting fresh
// grace. The var pool proper is exhausted; every VAR_UNUSED_0x40F* is claimed.
#define DUNGEON_PHANTOM_ODDS       7
#define DUNGEON_PHANTOM_STEPS_MIN  90
#define DUNGEON_PHANTOM_STEPS_MAX 160
#define DUNGEON_PHANTOM_SALT   0x5E0
// It hunted the player down, so it is above the floor - the same argument and
// the same size of bonus as the Sudowoodo and the injured ambush.
#define DUNGEON_PHANTOM_LEVEL_BONUS 3
// What it costs to send it away with an offering instead of fighting it. A third
// of the run's money, matching the gambler's stake - the two are the only events
// that price anything in money, and pricing them differently would be noise.
#define DUNGEON_PHANTOM_TOLL_DIVISOR 3

// The step counter for the above. VAR_TEMP rather than a claimed var: temps are
// saved but cleared on map change, and a floor IS a map change.
#define VAR_ROGUE_PHANTOM_STEPS VAR_TEMP_2

// How far a wandering event may stray from where it was placed, in tiles. ONE,
// deliberately: object events are destroyed off-screen and respawn at their
// TEMPLATE position, so a wanderer that drifts out of view snaps back rather
// than carrying on. At a range of 1 that is almost never visible, and the point
// is only that the thing does not read as furniture. Widen it and the snap
// becomes the thing the player notices.
#define DUNGEON_EVENT_WANDER_RANGE 1

// The nest: three waves, each one level above the last, all below the floor's
// own level. The difficulty is that there is no heal between them, not that any
// single wave is hard - a wave at floor level would make this a wall, and the
// whole point is a question about the party's DEPTH rather than its best mon.
#define DUNGEON_NEST_WAVES        3
#define DUNGEON_NEST_LEVEL_MALUS  4

// What the hoard pays, on top of the item rolled from the floor's loot table.
//
// SCALED, because a flat sum is the wrong shape here and the old flat 5000 is
// what made this event feel unpaid. The waves climb with the floor, so a
// constant reward is five times the pedlar's asking price on floor 11 and
// pocket change by floor 115 - a jackpot for the easiest run of it and nothing
// for the hardest. Pegged above the fossil's sale price, which is
// 500 + 60/floor: that one is free and this one costs three fights with no heal
// between them, so it has to be worth more at every depth.
#define DUNGEON_NEST_REWARD_MIN       1500
#define DUNGEON_NEST_REWARD_PER_FLOOR 100

// What the transposer charges in money, for a player who would rather not carry
// Brittle for the rest of the run.
#define DUNGEON_TRANSPOSER_COST 3000

#define DUNGEON_SUDOWOODO_ODDS       6
#define DUNGEON_SUDOWOODO_SALT   0x5D0
// It ambushed you, so it is above the floor rather than level with it - the same
// argument as the injured Pokemon's ambush, and the same size of bonus.
#define DUNGEON_SUDOWOODO_LEVEL_BONUS 3

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
// EIGHT, NOT FOUR. These odds were set before the guarantee below existed and
// were never revisited against it. The two stack - a tree is rotten if it is the
// floor's chosen dud OR its own roll comes up - so the real per-tree rate was
// 1/4 + 3/4 x 1/4 = 43.75%, measured at 43.1-43.9% over all 65536 seeds by
// check_berry_rot.py. Nearly half of every tree in the game was a dud, which is
// not what "one in four" reads as to anyone setting this number.
#define DUNGEON_BERRY_ROTTEN_ODDS  8

// EVERY FLOOR WITH TREES HAS AT LEAST ONE ROTTEN ONE, chosen by hash from the
// floor seed, and the remaining trees still roll independently at the odds
// above. Before this it was purely a per-tree roll, so a run could go a long
// way without meeting one - and because rot was only a smaller NUMBER of
// berries, with no message and no visual, a player could pick a dozen rotten
// trees and never learn the mechanic existed.
#define DUNGEON_BERRY_ROTTEN_GUARANTEED TRUE

// BUT NOT ON A FLOOR WITH FEWER TREES THAN THIS, and the guard is not cosmetic.
// The guarantee picks its victim with `DecorHash(...) % sBerryCount == tree`, so
// on a ONE tree floor the modulo is always 0 and that floor's only tree is
// always a dud - a berry theme that hands the player a single tree and
// guarantees it is worthless. Nothing caught this while the count was pinned at
// four; it became reachable the moment the count started scaling with depth, and
// it is the kind of thing that would have shipped as "berries are pointless
// early" rather than as a bug.
#define DUNGEON_BERRY_ROTTEN_MIN_TREES 2

// A DUD, not a small harvest. Zero, plus its own message - see
// RogueDungeonFloor_EventScript_BerryTree. A yield of 1 against 6 is invisible
// unless the player is counting, which is the same thing as not existing.
#define DUNGEON_BERRY_ROTTEN_YIELD 0

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

// THE DUNGEON ORDER SHUFFLE. A first playthrough walks vanilla's own
// progression - Petalburg Woods to Roxanne, through to Steven - and the first
// clear is what turns the shuffle on.
//
// THAT IS A DEFAULT AND NOT A GATE, and the difference shipped as a bug.
// RollDungeonOrder used to test FLAG_ROGUE_RUN_COMPLETED as well as the toggle,
// which made the options entry a one-way switch - it could turn the shuffle off
// but never on, so a player before their first clear saw a control reading ON
// that did nothing, indistinguishable from a roll that came up identity. The
// starting position now lives in ApplyNewGameUnlocks and the first clear moves
// it once; the roll reads the toggle and nothing else.
//
// The default is not only flavour. A shuffle is allowed to be lumpy precisely
// because the player has already won once, which is what lets this ship with no
// boss scaling and no re-authored species pools - so turning it on early is a
// choice the player is allowed to make and not one to make for them.
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
// THAT ARITHMETIC IS WHY THE PERMUTATION WAS RETIRED RATHER THAN WIDENED. A
// band of two was the only width the level curve tolerated, so the whole feature
// amounted to four coin flips on adjacent pairs; the region roll delivers the
// variety it was reaching for and costs the curve nothing, because every region
// row is authored against its own slot. DUNGEON_SHUFFLE_BAND is gone with it.

// RETIRED, AND NOT YET RECLAIMED. This held the position permutation; nothing
// reads it now. RollDungeonOrder still zeroes it on every roll so a save carried
// over from the shuffling build cannot leave a live-looking word behind.
//
// DO NOT HAND 0x40FD TO SOMETHING ELSE WITHOUT READING THIS. A save that has not
// reached a run reset under the current build still holds its old order word, so
// the next claimant would read someone else's shuffle as its own value - which
// is the collision the var pool note warns about, arriving from the one
// direction nobody watches, a var that used to be ours.
#define VAR_ROGUE_RUN_ORDER VAR_UNUSED_0x40FD

// SET means the player turned the shuffle OFF, so clear - the value every save
// already holds - is on. Stored inverted for that reason alone: the toggle
// defaults to enabled once unlocked, and a flag cannot default to set. Same
// trick optionsAutoRun already plays in option_menu.c.
#define FLAG_ROGUE_VANILLA_ORDER FLAG_UNUSED_0x91F

// THE REGION ROLL, the second half of the scrambler and the one that changes
// WHO a dungeon's boss is rather than WHERE it stands.
//
// TWO BITS per dungeon IDENTITY, so four regions. Rolled beside the order word,
// from the same function, behind the same gate - a first playthrough is the
// eight Hoenn leaders, the Hoenn Elite Four, Wallace and Steven, and a clear is
// what unlocks both halves at once.
//
// IT WAS ONE BIT IN ONE VAR AND THAT HELD EXACTLY TWO REGIONS. Fourteen
// identities at two bits is 28 bits and a var is 16, so this is TWO vars, low
// then high, seven identities each. The alternative - a single var holding an
// index into a table of whole rosters - was rejected because it throws away the
// per-identity independence that makes the roll interesting: the point is that
// one run can be Roxanne, Misty, Whitney, Erika.
//
// FOUR IS THE CEILING AND IT IS NOT AN ACCIDENT. Two bits is exactly the four
// regions this project has sprites for. A fifth needs three bits, which is 42
// bits, which is a third var - do that deliberately rather than discovering it
// when a roll silently wraps.
//
// SLOT AND IDENTITY ARE THE SAME NUMBER NOW, so this is keyed on both and the
// distinction has stopped mattering. The region field says "this dungeon is its
// Kanto counterpart" - Norman becomes Koga, Wallace becomes Blue, Steven becomes
// Red - and with the position permutation retired that is the whole scrambler.
//
// A fixed per-position roster is exactly what this now is, and it is what was
// wanted: dungeon 1 is always the woods, and its boss is one of Roxanne, Brock,
// Falkner or Roark. Every one of those four is authored against floor 10, so no
// roll can put a boss written for floor 70 on it.
//
// THE PAIRING IS 1:1 AND THE FOURTEENTH IS AUTHORED. Kanto ships eight leaders,
// four Elite Four and one Champion - thirteen, against Hoenn's fourteen, because
// Steven has no Kanto counterpart. TRAINER_ROGUE_KANTO_RED fills it; see the
// note on him in constants/opponents.h.
//
// TWO VARS OF THEIR OWN, and neither is free the way VAR_ROGUE_RUN_ORDER was:
// the 0x40F* tail is fully claimed, so both come from the earlier part of the
// unused pool. Checked against this file first rather than against vars.h,
// because vars.h still calls every id this project holds "unused".
//
// LOW carries identities 0-6, HIGH carries 7-13. Split by identity rather than
// by bit position so that DungeonRegionOf can pick a var and shift within it,
// instead of a field straddling the boundary - identity 8 at bits 16-17 would
// otherwise need reassembling from two reads.
#define VAR_ROGUE_RUN_REGION_LO VAR_UNUSED_0x40E5
#define VAR_ROGUE_RUN_REGION_HI VAR_UNUSED_0x40DC

// Which region a dungeon identity's boss comes from. Named rather than written
// as 0..3 because the boss tables are indexed by
// `identity + region * DUNGEON_COUNT` and a bare 2 there reads as an
// off-by-something.
//
// THE ORDER IS THE ORDER OF THE TABLE BLOCKS and nothing else may reorder it.
// Hoenn is 0 because 0 must be the vanilla arrangement: a cleared region word
// means a first playthrough, and every gate in RollDungeonOrder zeroes it.
#define DUNGEON_REGION_HOENN  0
#define DUNGEON_REGION_KANTO  1
#define DUNGEON_REGION_JOHTO  2
#define DUNGEON_REGION_SINNOH 3
#define DUNGEON_REGION_COUNT  4

// How many regions actually have a block in the boss tables. All four are wired
// now, so this equals DUNGEON_REGION_COUNT - but it is a SEPARATE CONSTANT and
// must stay one. It existed because Sinnoh had sprites and no parties, and the
// next region added will be in exactly that state again; collapsing the two the
// moment they agree is how the roll ends up producing a region with no rows and
// indexing past the end of all five tables, silently, because the tables are
// adjacent in ROM and the read succeeds.
#define DUNGEON_REGION_WIRED 4

// Bits per identity, and how many identities fit in one var. Derived rather
// than written as 2 and 7, so that a fifth region raising the bit count moves
// the split with it instead of silently overflowing the low word.
#define DUNGEON_REGION_BITS      2
#define DUNGEON_REGION_PER_VAR   (16 / DUNGEON_REGION_BITS)
#define DUNGEON_REGION_MASK      ((1 << DUNGEON_REGION_BITS) - 1)

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
