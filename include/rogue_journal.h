#ifndef GUARD_ROGUE_JOURNAL_H
#define GUARD_ROGUE_JOURNAL_H

#include "constants/rogue_journal.h"

// The run journal. See constants/rogue_journal.h for the kind vocabulary and
// docs/RUN_JOURNAL.md for the design.
//
// EVERYTHING OUTSIDE THIS FILE GOES THROUGH THESE FUNCTIONS, and one of them in
// particular: RogueJournal_Append is the ONLY writer of head and count. A second
// site advancing count without advancing head drops the ring's full-ness
// handling on the floor - every append past the end then overwrites the newest
// entry with the oldest, and the journal keeps rendering perfectly while losing
// everything. check_run_journal.py asserts the single writer.

// One row per ROGUE_JOURNAL_*. See sJournalKinds in rogue_journal.c - adding an
// event is a row there, an enumerator, and a call at the site, and nothing else.
struct RogueJournalKind
{
    // The line, with {STR_VAR_1} / {STR_VAR_2} where the params go.
    //
    // THE PLACEHOLDERS AND THE PARAM KINDS MUST AGREE, in both directions, and
    // that is a check rule rather than a convention. A {STR_VAR_2} in a row
    // declaring ROGUE_JOURNAL_PARAM_NONE prints whatever gStringVar2 was left
    // holding by something else - not a blank, a real word from an unrelated
    // system, dropped into a sentence that reads as though it meant it.
    const u8 *text;
    u8 param1Kind;      // ROGUE_JOURNAL_PARAM_*
    u8 param2Kind;
    // TRUE for the run-boundary rows. The reader partitions the ring on these,
    // and the card draws them in the stat colour rather than the text one.
    bool8 isSeparator;
};

// The journal, migrating it first if the stored version is not current.
//
// THE MIGRATION IS LAZY, for the same reason RogueCharm_Data's is: load_save.c
// has no post-load hook to hang one on - ClearSav3 runs on a new game and
// nothing runs when an existing save is continued - so checking the version on
// every access is what covers all three arrival paths by construction, and the
// one that matters is a save written before this struct existed, which reads
// whatever bytes were already in the sector.
struct RogueJournal *RogueJournal_Data(void);

// The row for a kind, or the placeholder row for an id no table covers. Never
// returns NULL, and never returns a row with a NULL text.
const struct RogueJournalKind *RogueJournal_KindInfo(u8 kind);

// Records one thing that happened. floor is the DISPLAYED floor (1-based, as
// VAR_ROGUE_BEST_FLOOR and RogueDungeon_GetFloorName use), or 0 for an entry
// that is not floor-scoped. Silently ignores a kind outside the table, because
// the alternative is a script typo writing rows the reader cannot name.
void RogueJournal_Append(u8 kind, u8 floor, u16 param1, u16 param2);

// Opens a run: steps the run counter and writes the RUN_STARTED separator.
// Called as a SPECIAL from the starter prompt in RogueDungeonFloor's script,
// which is the only thing that happens exactly once per run. See the note on the
// definition for why RogueDungeon_ResetRun is not the site it looks like.
void RogueJournal_OnRunStarted(void);

// How many entries are readable, 0 to ROGUE_JOURNAL_ENTRIES.
u32 RogueJournal_Count(void);

// Entry 0 is the NEWEST, which is the order the card reads them in. Returns
// NULL past the end, so a caller cannot walk off the ring by trusting a count
// it cached before something appended.
const struct RogueJournalEntry *RogueJournal_EntryFromNewest(u32 index);

// Renders one entry into dest, resolving its params. dest must hold at least
// ROGUE_JOURNAL_LINE_LENGTH bytes; a NULL entry yields an empty string.
//
// USES gStringVar1 AND gStringVar2, so callers must buffer every line they want
// BEFORE printing any of them rather than expanding inside a per-frame print
// path - the trainer card's own front side uses gStringVar1 for the money row.
void RogueJournal_BufferLine(const struct RogueJournalEntry *entry, u8 *dest);

// TEST ONLY - see test/rogue_journal.c. Empties the ring. NOTHING IN THE GAME
// MAY CALL THIS: the journal is persistent, and the one function that looks like
// it should clear it, RogueDungeon_ResetRun, is asserted not to.
void RogueJournal_Test_Reset(void);

#endif // GUARD_ROGUE_JOURNAL_H
