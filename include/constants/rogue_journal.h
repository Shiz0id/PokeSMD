#ifndef GUARD_CONSTANTS_ROGUE_JOURNAL_H
#define GUARD_CONSTANTS_ROGUE_JOURNAL_H

// The run journal: a PERSISTENT, cross-run log of what happened, read on the
// back of the trainer card. See docs/RUN_JOURNAL.md for the whole design.
//
// PERSISTENT IS THE WHOLE POINT, and it is the one place this disagrees with
// everything else the run keeps. RogueDungeon_ResetRun wipes the party, the bag,
// the coins, the money, the charms and the rod flags because those are per-run.
// The journal is the record OF those runs ending, so it must survive exactly
// what they do not - clearing it there looks like tidiness and destroys the
// feature. check_run_journal.py asserts ResetRun does not touch it.

// KINDS. Row 0 is the empty-slot placeholder and exists only to keep the id and
// the table index the same number, the same way ROGUE_CHARM_NONE does.
//
// APPENDED, NEVER RENUMBERED. Kind ids are stored in SaveBlock3, so inserting
// one in the middle silently reinterprets every entry an existing save holds -
// a run that fell on floor 42 becomes a run that cleared the dungeon.
#define ROGUE_JOURNAL_NONE        0
#define ROGUE_JOURNAL_RUN_STARTED 1
#define ROGUE_JOURNAL_RUN_FELL    2
#define ROGUE_JOURNAL_RUN_CLEARED 3
// The events, appended after the three boundaries. Adding these needed NO save
// version bump, which is the record format doing its job: ids 0-3 still mean
// what they meant, so a journal written before this build reads back unchanged.
#define ROGUE_JOURNAL_BOSS_BEATEN     4
#define ROGUE_JOURNAL_MINIBOSS_BEATEN 5
#define ROGUE_JOURNAL_ACE_ADOPTED     6
#define ROGUE_JOURNAL_TM_TAKEN        7
#define ROGUE_JOURNAL_DUNGEON_ENTERED 8
#define ROGUE_JOURNAL_CHARM_MON       9
#define ROGUE_JOURNAL_CHARM_PARTY     10
#define ROGUE_JOURNAL_KIND_COUNT      11

// What a row's param means, so that turning a stored u16 into a word is one
// resolver rather than a switch per kind. ROM-side only - param kinds are never
// stored - so this vocabulary may grow freely as event kinds land, and only the
// KIND ids above are frozen by the save format.
#define ROGUE_JOURNAL_PARAM_NONE    0
#define ROGUE_JOURNAL_PARAM_NUMBER  1
#define ROGUE_JOURNAL_PARAM_SPECIES 2
#define ROGUE_JOURNAL_PARAM_TRAINER 3
#define ROGUE_JOURNAL_PARAM_ITEM    4
// A MAPSEC id, not a dungeon index. The dungeon-to-theme routing lives behind
// static functions in rogue_dungeon.c, so the caller resolves it and the journal
// stores the map section name it landed on - which is also what the death
// summary already prints a dungeon as.
#define ROGUE_JOURNAL_PARAM_MAPSEC  5
#define ROGUE_JOURNAL_PARAM_CHARM   6
#define ROGUE_JOURNAL_PARAM_COUNT   7

// How many entries the ring holds. 128 at six bytes is 768, plus a four-byte
// header, against roughly 1.4 KB free in SaveBlock3 - so this is sized for the
// design rather than for the budget, the same way the charm slot counts are.
//
// MUST FIT IN A u8, because head and count are u8 and the ring is only ever
// "full" when count EQUALS this. At 256 the count would wrap to zero on the
// 256th append, the ring would never mark itself full, head would never advance,
// and the journal would silently read as empty forever. STATIC_ASSERTed in
// rogue_journal.c rather than left as a comment.
#define ROGUE_JOURNAL_ENTRIES 128

// How much room one rendered line needs. Longest current line is
// "Fell on floor 115" at 17, and an event naming two Pokemon will be longer -
// "Nidoran-F became Nidorina" is 25. 40 leaves headroom without the buffer
// mattering: six of these is 240 bytes on the trainer card's ~31 KB heap
// allocation.
#define ROGUE_JOURNAL_LINE_LENGTH 40

// How many entries one page of the trainer card's back shows. Six is the
// vanilla back's own row count - PrintStatOnBackOfCard puts a row every 16px
// from y=33 - so the journal fits the card art that already exists and needs no
// repaint. See docs/RUN_JOURNAL.md.
#define ROGUE_JOURNAL_ROWS_PER_PAGE 6

// Bumping this zeroes every player's journal on next access. Bump it whenever
// the LAYOUT of struct RogueJournal or struct RogueJournalEntry changes, or
// whenever a kind id is renumbered.
//
// THIS IS WHY THE RECORD IS SIX BYTES AND NOT FOUR. Save space is not the
// constraint - 1.4 KB is free - but a layout change costs every player their
// history, so the width that can express two named operands was chosen up front
// rather than discovered after release.
#define ROGUE_JOURNAL_SAVE_VERSION 1

#endif // GUARD_CONSTANTS_ROGUE_JOURNAL_H
