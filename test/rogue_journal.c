#include "global.h"
#include "test/test.h"
#include "rogue_journal.h"
#include "string_util.h"
#include "constants/characters.h"

// THE RING, RUN ON REAL EMULATED HARDWARE.
//
// check_run_journal.py asserts the SHAPE of this code - one writer, a version
// guard, a row per kind, placeholders matching declared params. It cannot
// execute any of it, and every step of the journal so far has been verified by
// reading. These cases are the first time an append runs at all.
//
// WHAT THEY ARE FOR, specifically. The ring's wrap is arithmetic that is wrong
// silently: a count that grows without head advancing, or an off-by-one in the
// newest-first walk, produces a journal that renders perfectly and has quietly
// lost or duplicated entries. Nothing on screen would say so, and no static
// check can see it.
//
// They also pin the two hazards the resolver was written around: that a param
// reaches its line at all, and that a row declaring no params does not print
// whatever the row before it left in gStringVar1.

TEST("run journal: entries read back newest first")
{
    u32 i;

    RogueJournal_Test_Reset();
    EXPECT_EQ(RogueJournal_Count(), 0);

    // Floors 1..5 as the param, so the entries are distinguishable by value
    // rather than by kind - all three kinds that exist today are separators.
    for (i = 1; i <= 5; i++)
        RogueJournal_Append(ROGUE_JOURNAL_RUN_FELL, 0, i, 0);

    EXPECT_EQ(RogueJournal_Count(), 5);

    for (i = 0; i < 5; i++)
    {
        const struct RogueJournalEntry *entry = RogueJournal_EntryFromNewest(i);

        EXPECT_EQ(entry->kind, ROGUE_JOURNAL_RUN_FELL);
        EXPECT_EQ(entry->param1, 5 - i);
    }
}

TEST("run journal: a full ring drops the oldest and never the newest")
{
    u32 i;
    u32 total = ROGUE_JOURNAL_ENTRIES + 10;

    RogueJournal_Test_Reset();

    for (i = 0; i < total; i++)
        RogueJournal_Append(ROGUE_JOURNAL_RUN_FELL, 0, i, 0);

    // Saturates rather than growing, and the survivors are the LAST
    // ROGUE_JOURNAL_ENTRIES appended - so the oldest still readable is the
    // tenth, and every one before it is gone.
    EXPECT_EQ(RogueJournal_Count(), ROGUE_JOURNAL_ENTRIES);
    EXPECT_EQ(RogueJournal_EntryFromNewest(0)->param1, total - 1);
    EXPECT_EQ(RogueJournal_EntryFromNewest(ROGUE_JOURNAL_ENTRIES - 1)->param1, 10);

    // And the whole walk is still in order, which is what catches an off-by-one
    // that only shows up once head has moved.
    for (i = 0; i < ROGUE_JOURNAL_ENTRIES; i++)
        EXPECT_EQ(RogueJournal_EntryFromNewest(i)->param1, total - 1 - i);
}

TEST("run journal: reading past the end returns NULL")
{
    RogueJournal_Test_Reset();
    EXPECT(RogueJournal_EntryFromNewest(0) == NULL);

    RogueJournal_Append(ROGUE_JOURNAL_RUN_FELL, 0, 1, 0);
    EXPECT(RogueJournal_EntryFromNewest(0) != NULL);
    EXPECT(RogueJournal_EntryFromNewest(1) == NULL);
    EXPECT(RogueJournal_EntryFromNewest(ROGUE_JOURNAL_ENTRIES) == NULL);
}

TEST("run journal: an unknown kind is dropped rather than stored")
{
    RogueJournal_Test_Reset();

    // ROGUE_JOURNAL_NONE is a valid table row holding a valid empty string, so
    // a stored one would render as a blank line and be indistinguishable from a
    // gap. Both it and an id past the table are refused at the door.
    RogueJournal_Append(ROGUE_JOURNAL_NONE, 0, 0, 0);
    RogueJournal_Append(ROGUE_JOURNAL_KIND_COUNT, 0, 0, 0);
    EXPECT_EQ(RogueJournal_Count(), 0);
}

TEST("run journal: a stale save version empties the journal on next access")
{
    RogueJournal_Test_Reset();
    RogueJournal_Append(ROGUE_JOURNAL_RUN_FELL, 0, 1, 0);
    RogueJournal_Append(ROGUE_JOURNAL_RUN_FELL, 0, 2, 0);
    EXPECT_EQ(RogueJournal_Count(), 2);

    // The migration is LAZY - load_save.c has no post-load hook - so the guard
    // has to fire on the next access rather than at load. This is the case that
    // actually matters in the wild: a save written before the struct existed,
    // holding whatever bytes were already in the sector.
    RogueJournal_Data()->version = 0xAB;
    EXPECT_EQ(RogueJournal_Count(), 0);
    EXPECT(RogueJournal_EntryFromNewest(0) == NULL);
}

TEST("run journal: OnRunStarted steps the run number and opens a boundary")
{
    RogueJournal_Test_Reset();

    RogueJournal_OnRunStarted();
    EXPECT_EQ(RogueJournal_Count(), 1);
    EXPECT_EQ(RogueJournal_EntryFromNewest(0)->kind, ROGUE_JOURNAL_RUN_STARTED);
    EXPECT_EQ(RogueJournal_EntryFromNewest(0)->param1, 1);

    RogueJournal_OnRunStarted();
    EXPECT_EQ(RogueJournal_Count(), 2);
    EXPECT_EQ(RogueJournal_EntryFromNewest(0)->param1, 2);

    // The run that came first is still readable underneath it, which is the
    // whole point of the journal being persistent.
    EXPECT_EQ(RogueJournal_EntryFromNewest(1)->param1, 1);
}

TEST("run journal: paging walks every entry exactly once, in order")
{
    u32 i, page, row;
    u32 total = 20;   // deliberately NOT a multiple of the page size
    u32 pages;
    u32 seen = 0;

    RogueJournal_Test_Reset();
    for (i = 1; i <= total; i++)
        RogueJournal_Append(ROGUE_JOURNAL_RUN_FELL, 0, i, 0);

    pages = (RogueJournal_Count() + ROGUE_JOURNAL_ROWS_PER_PAGE - 1)
          / ROGUE_JOURNAL_ROWS_PER_PAGE;
    EXPECT_EQ(pages, 4);   // 20 entries, six to a page, last page holds two

    // THE CARD'S OWN INDEX EXPRESSION, lifted verbatim from BufferJournalPage.
    // The card's copy lives in a static function this test cannot call, so what
    // is pinned here is the arithmetic rather than the caller - an off-by-one at
    // a page boundary would drop or repeat one entry, and six plausible lines
    // with one silently missing is not something a screenshot settles.
    for (page = 0; page < pages; page++)
    {
        for (row = 0; row < ROGUE_JOURNAL_ROWS_PER_PAGE; row++)
        {
            const struct RogueJournalEntry *entry = RogueJournal_EntryFromNewest(
                page * ROGUE_JOURNAL_ROWS_PER_PAGE + row);

            // A short last page ends here, which is the normal case whenever the
            // count is not a multiple of six.
            if (entry == NULL)
                break;

            // The walk is newest-first and the params went in ascending, so the
            // nth entry seen must carry total - n.
            EXPECT_EQ(entry->param1, total - seen);
            seen++;
        }
    }

    // Every entry, exactly once: no gap at a page boundary, no row repeated.
    EXPECT_EQ(seen, total);
}

TEST("run journal: a param actually reaches its line")
{
    struct RogueJournalEntry a = { .kind = ROGUE_JOURNAL_RUN_FELL, .param1 = 42 };
    struct RogueJournalEntry b = { .kind = ROGUE_JOURNAL_RUN_FELL, .param1 = 43 };
    u8 lineA[ROGUE_JOURNAL_LINE_LENGTH];
    u8 lineB[ROGUE_JOURNAL_LINE_LENGTH];

    RogueJournal_BufferLine(&a, lineA);
    RogueJournal_BufferLine(&b, lineB);

    // A row that stores a param and never shows it is exactly what the
    // placeholder rule guards statically; this is the same claim, executed.
    EXPECT(StringCompare(lineA, lineB) != 0);
    EXPECT(lineA[0] != EOS);
}

TEST("run journal: every kind renders a non-empty line")
{
    u32 kind;

    // A row whose text or params are wired wrong does not crash - it renders
    // BLANK, which on the card is indistinguishable from an empty row. Walking
    // every kind is the cheapest way to catch a table row that was added and
    // never looked at. Params are given plausible in-range values because what
    // is being asserted is that the row produces text at all.
    for (kind = 1; kind < ROGUE_JOURNAL_KIND_COUNT; kind++)
    {
        struct RogueJournalEntry entry = { .param1 = 1, .param2 = 1 };
        u8 line[ROGUE_JOURNAL_LINE_LENGTH];

        entry.kind = kind;
        RogueJournal_BufferLine(&entry, line);
        EXPECT(line[0] != EOS);
    }
}

TEST("run journal: an out-of-range param renders empty rather than garbage")
{
    struct RogueJournalEntry entry = { .kind = ROGUE_JOURNAL_DUNGEON_ENTERED,
                                       .param1 = MAPSEC_NONE };
    u8 line[ROGUE_JOURNAL_LINE_LENGTH];

    // Params are STORED VALUES, so a journal written by a later build - or one
    // that slipped past the version guard - can hand a resolver an id no table
    // has. Unbounded, that indexes a ROM table and prints whatever bytes follow
    // it as a place name. The bound turns it into an empty operand instead.
    //
    // MAPSEC_NONE, THE FIRST VALUE PAST THE END, and not 0xFFFF. A wild index
    // faults hard enough to take the whole test runner down - which was tried,
    // and it kills the process before any assertion runs, so it proves the
    // hazard is real and proves nothing about this test. The boundary value
    // reads adjacent ROM instead: harmless to execute, non-empty, and therefore
    // a clean failure when the bound is removed. The same `<` covers both.
    RogueJournal_BufferLine(&entry, line);
    EXPECT_EQ(gStringVar1[0], EOS);
}

TEST("run journal: a row with no params clears the buffer it does not use")
{
    struct RogueJournalEntry fell = { .kind = ROGUE_JOURNAL_RUN_FELL, .param1 = 42 };
    struct RogueJournalEntry cleared = { .kind = ROGUE_JOURNAL_RUN_CLEARED };
    u8 line[ROGUE_JOURNAL_LINE_LENGTH];

    // ASSERTS gStringVar1 DIRECTLY, and the first version of this test did not -
    // it compared two renderings of RUN_CLEARED and required them to match,
    // which they do whatever BufferJournalParam does, because that row's text
    // has no {STR_VAR_1} in it to leak into. A test that cannot fail is worth
    // nothing, and this one was one line of reasoning away from shipping green
    // and empty.
    //
    // What the clearing actually guards is the row that declares NONE and DOES
    // carry a placeholder. check_run_journal.py rejects that pairing statically,
    // so the table cannot reach this state - the clearing is the second line of
    // defence, and this is the only way to observe it.
    RogueJournal_BufferLine(&fell, line);
    EXPECT(gStringVar1[0] != EOS);

    RogueJournal_BufferLine(&cleared, line);
    EXPECT_EQ(gStringVar1[0], EOS);
    EXPECT_EQ(gStringVar2[0], EOS);
}
