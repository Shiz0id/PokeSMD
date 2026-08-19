#include "global.h"
#include "rogue_journal.h"
#include "string_util.h"
#include "data.h"
#include "item.h"
#include "pokemon.h"
#include "region_map.h"
#include "rogue_charms.h"
#include "constants/characters.h"
#include "constants/region_map_sections.h"

// The ring is only ever "full" when count EQUALS ROGUE_JOURNAL_ENTRIES, and
// count is a u8. At 256 entries it would wrap to zero on the 256th append: the
// ring would never mark itself full, head would never advance, and the journal
// would read as empty forever with nothing wrong in any table. Fails the build
// rather than the save.
STATIC_ASSERT(ROGUE_JOURNAL_ENTRIES <= 255, RogueJournalRingFitsInU8);

// THE RECORD WIDTH, PINNED. Its members total six bytes, and this toolchain
// rounds an unpacked struct up to a multiple of four - so dropping PACKED from
// struct RogueJournalEntry costs two bytes per entry, 256 across the ring, in
// the block this project treats as its ceiling. That shipped once and was caught
// only because the linker's EWRAM figure moved by more than the struct
// accounted for. It fails the build now instead.
STATIC_ASSERT(sizeof(struct RogueJournalEntry) == 6, RogueJournalEntryIsSixBytes);

// And the whole ring, which also catches a header field being added without the
// save version being bumped alongside it.
STATIC_ASSERT(sizeof(struct RogueJournal) == ROGUE_JOURNAL_ENTRIES * 6 + 4,
              RogueJournalRingIsExact);

// Stands in wherever a kind id has no row - the placeholder, or an id a future
// save invented. Never let a NULL reach StringExpandPlaceholders.
static const u8 sText_JournalNone[]       = _("");

static const u8 sText_JournalRunStarted[] = _("Run {STR_VAR_1} began");
static const u8 sText_JournalRunFell[]    = _("Fell on floor {STR_VAR_1}");
static const u8 sText_JournalRunCleared[] = _("Cleared the dungeon");

// KEPT SHORT ON PURPOSE. One row is 224px of window less the x=16 margin and,
// for a floor-tagged line, another 32 - so about 176px, which FONT_NARROW spends
// at roughly 5px a character. Every line below fits its worst case: the longest
// is ACE_ADOPTED at an 11-character species plus a 10-character trainer.
static const u8 sText_JournalBossBeaten[]     = _("Defeated {STR_VAR_1}");
static const u8 sText_JournalMinibossBeaten[] = _("Beat {STR_VAR_1}");
static const u8 sText_JournalAceAdopted[]     = _("{STR_VAR_1} left by {STR_VAR_2}");
static const u8 sText_JournalTmTaken[]        = _("Took {STR_VAR_1}");
static const u8 sText_JournalDungeonEntered[] = _("Reached {STR_VAR_1}");
static const u8 sText_JournalCharmMon[]       = _("{STR_VAR_1} is {STR_VAR_2}");
static const u8 sText_JournalCharmParty[]     = _("The team is {STR_VAR_1}");

// THE JOURNAL, AS A TABLE. Adding an event should be a row here, an enumerator
// in constants/rogue_journal.h, and a RogueJournal_Append call at the site that
// already knows the event happened - same rule the charms and the dungeon themes
// follow. If a new event is turning into a special case in the card's print
// path, it wants a PARAM KIND, not a branch.
//
// Indexed by ROGUE_JOURNAL_*, so row 0 is the empty-slot placeholder and exists
// only to keep the id and the index the same number. check_run_journal.py
// asserts every enumerator has a row, because the C default fill for an omitted
// designated initialiser is 0 - and 0 here is a valid kind with a valid empty
// string, so a missing row is not absent, it is WRONG, and it renders as a blank
// line rather than as anything anyone would notice.
static const struct RogueJournalKind sJournalKinds[ROGUE_JOURNAL_KIND_COUNT] =
{
    [ROGUE_JOURNAL_NONE] =
    {
        .text = sText_JournalNone,
        .param1Kind = ROGUE_JOURNAL_PARAM_NONE,
        .param2Kind = ROGUE_JOURNAL_PARAM_NONE,
        .isSeparator = FALSE,
    },

    // The three run boundaries. These are the whole of step one: the journal is
    // correct and invisible until the card learns to draw it.
    //
    // RUN_STARTED carries the run number rather than the reader counting
    // separators, because a ring that has wrapped may have dropped the earliest
    // ones and a run that says which one it is stays legible anyway.
    [ROGUE_JOURNAL_RUN_STARTED] =
    {
        .text = sText_JournalRunStarted,
        .param1Kind = ROGUE_JOURNAL_PARAM_NUMBER,
        .param2Kind = ROGUE_JOURNAL_PARAM_NONE,
        .isSeparator = TRUE,
    },

    // The two TERMINAL rows, and they are separate kinds rather than one row
    // with an outcome param on purpose: only the losing path knows the floor,
    // only the winning path may be written by RogueDungeon_OnRunCompleted, and
    // a single row would let either path write the other's outcome. That is the
    // same separation that keeps OnRunCompleted out of ResetRun.
    [ROGUE_JOURNAL_RUN_FELL] =
    {
        .text = sText_JournalRunFell,
        .param1Kind = ROGUE_JOURNAL_PARAM_NUMBER,
        .param2Kind = ROGUE_JOURNAL_PARAM_NONE,
        .isSeparator = TRUE,
    },

    [ROGUE_JOURNAL_RUN_CLEARED] =
    {
        .text = sText_JournalRunCleared,
        .param1Kind = ROGUE_JOURNAL_PARAM_NONE,
        .param2Kind = ROGUE_JOURNAL_PARAM_NONE,
        .isSeparator = TRUE,
    },

    // The events. These are the first rows that are NOT separators, so they are
    // also the first to carry a floor and draw the "F42" tag beside themselves.

    [ROGUE_JOURNAL_BOSS_BEATEN] =
    {
        .text = sText_JournalBossBeaten,
        .param1Kind = ROGUE_JOURNAL_PARAM_TRAINER,
        .param2Kind = ROGUE_JOURNAL_PARAM_NONE,
        .isSeparator = FALSE,
    },

    // A separate kind from the one above rather than a flag on it, because the
    // two read differently and a journal is prose: a gym leader is DEFEATED and
    // a corridor grunt is BEATEN. They also arrive from the same script, so
    // sharing a row would mean the card could not tell them apart at all.
    [ROGUE_JOURNAL_MINIBOSS_BEATEN] =
    {
        .text = sText_JournalMinibossBeaten,
        .param1Kind = ROGUE_JOURNAL_PARAM_TRAINER,
        .param2Kind = ROGUE_JOURNAL_PARAM_NONE,
        .isSeparator = FALSE,
    },

    [ROGUE_JOURNAL_ACE_ADOPTED] =
    {
        .text = sText_JournalAceAdopted,
        .param1Kind = ROGUE_JOURNAL_PARAM_SPECIES,
        .param2Kind = ROGUE_JOURNAL_PARAM_TRAINER,
        .isSeparator = FALSE,
    },

    [ROGUE_JOURNAL_TM_TAKEN] =
    {
        .text = sText_JournalTmTaken,
        .param1Kind = ROGUE_JOURNAL_PARAM_ITEM,
        .param2Kind = ROGUE_JOURNAL_PARAM_NONE,
        .isSeparator = FALSE,
    },

    // What gives a 128-entry ring a readable spine even on a quiet run: without
    // it, a dungeon where nothing notable happened leaves no trace at all.
    [ROGUE_JOURNAL_DUNGEON_ENTERED] =
    {
        .text = sText_JournalDungeonEntered,
        .param1Kind = ROGUE_JOURNAL_PARAM_MAPSEC,
        .param2Kind = ROGUE_JOURNAL_PARAM_NONE,
        .isSeparator = FALSE,
    },

    // Two kinds rather than one with an optional species, for the reason the
    // charm system itself gives: a party-wide affliction and a per-Pokemon one
    // are different things to the player, and "The team is Cursed" is the line
    // that says so.
    [ROGUE_JOURNAL_CHARM_MON] =
    {
        .text = sText_JournalCharmMon,
        .param1Kind = ROGUE_JOURNAL_PARAM_SPECIES,
        .param2Kind = ROGUE_JOURNAL_PARAM_CHARM,
        .isSeparator = FALSE,
    },

    [ROGUE_JOURNAL_CHARM_PARTY] =
    {
        .text = sText_JournalCharmParty,
        .param1Kind = ROGUE_JOURNAL_PARAM_CHARM,
        .param2Kind = ROGUE_JOURNAL_PARAM_NONE,
        .isSeparator = FALSE,
    },
};

struct RogueJournal *RogueJournal_Data(void)
{
    struct RogueJournal *data = &gSaveBlock3Ptr->rogueJournal;

    if (data->version != ROGUE_JOURNAL_SAVE_VERSION)
    {
        CpuFill16(0, data, sizeof(*data));
        data->version = ROGUE_JOURNAL_SAVE_VERSION;
    }

    return data;
}

const struct RogueJournalKind *RogueJournal_KindInfo(u8 kind)
{
    if (kind >= ROGUE_JOURNAL_KIND_COUNT)
        return &sJournalKinds[ROGUE_JOURNAL_NONE];

    return &sJournalKinds[kind];
}

// THE ONLY WRITER OF head AND count. See rogue_journal.h.
void RogueJournal_Append(u8 kind, u8 floor, u16 param1, u16 param2)
{
    struct RogueJournal *data = RogueJournal_Data();
    struct RogueJournalEntry *entry;
    u32 slot;

    // A kind the table does not cover would render as a blank line and be
    // indistinguishable from a gap, so it is dropped at the door instead.
    if (kind == ROGUE_JOURNAL_NONE || kind >= ROGUE_JOURNAL_KIND_COUNT)
        return;

    slot = (data->head + data->count) % ROGUE_JOURNAL_ENTRIES;

    entry = &data->entries[slot];
    entry->kind = kind;
    entry->floor = floor;
    entry->param1 = param1;
    entry->param2 = param2;

    // Growing until full, then dropping the oldest. Both halves are here rather
    // than split across a caller, because a count that grows without head
    // advancing overwrites the newest entry with the oldest on every append
    // afterwards - and the journal goes on rendering perfectly the whole time.
    if (data->count < ROGUE_JOURNAL_ENTRIES)
        data->count++;
    else
        data->head = (data->head + 1) % ROGUE_JOURNAL_ENTRIES;
}

// THE ONLY WRITER OF runIndex, and a special rather than a C hook.
//
// There is no C function that runs exactly once per run to hang this on. A run
// begins when the player finishes picking starters, and both the whiteout path
// and the run-complete path reach that prompt by warping to floor one - so the
// starter script is the one thing that happens once, and it is what calls this.
// RogueDungeon_ResetRun looks like the obvious C site and is wrong twice over:
// it also runs on the losing path before the player has picked anything, and it
// is the one function that must never mention the journal at all.
void RogueJournal_OnRunStarted(void)
{
    struct RogueJournal *data = RogueJournal_Data();

    // Saturates rather than wrapping, the same way VAR_ROGUE_RUNS_COMPLETED
    // does - a player on their 256th run should not be told it is their first.
    if (data->runIndex < 255)
        data->runIndex++;

    // FLOOR 0, NOT 1. A separator is a boundary BETWEEN runs rather than
    // something that happened on a floor, and the card draws it full width with
    // no floor tag. The number this line prints is the run, not the floor.
    RogueJournal_Append(ROGUE_JOURNAL_RUN_STARTED, 0, data->runIndex, 0);
}

u32 RogueJournal_Count(void)
{
    return RogueJournal_Data()->count;
}

// TEST ONLY. Nothing in the game may call this: the journal is persistent, and
// the whole of check_run_journal.py rule 6 is that the one function which looks
// like it should clear it - RogueDungeon_ResetRun - must not. It exists so
// test/rogue_journal.c can start each case from a known ring.
//
// Zeroes with CpuFill16 rather than assigning head and count, which also keeps
// it out of the single-writer rule honestly rather than by exemption.
void RogueJournal_Test_Reset(void)
{
    struct RogueJournal *data = RogueJournal_Data();

    CpuFill16(0, data, sizeof(*data));
    data->version = ROGUE_JOURNAL_SAVE_VERSION;
}

// Turns one stored u16 into a word. ONE RESOLVER RATHER THAN A SWITCH PER KIND,
// which is what keeps adding an event to a table row: a new operand type is a
// case here and every existing row can then use it.
static void BufferJournalParam(u8 paramKind, u16 value, u8 *dest)
{
    switch (paramKind)
    {
    case ROGUE_JOURNAL_PARAM_NUMBER:
        ConvertIntToDecimalStringN(dest, value, STR_CONV_MODE_LEFT_ALIGN, 3);
        break;
    case ROGUE_JOURNAL_PARAM_SPECIES:
        StringCopy(dest, GetSpeciesName(value));
        break;
    // BOUNDS-CHECKED, because these are stored values. A journal written by a
    // later build - or corrupted before the version guard caught it - would
    // otherwise index a ROM table with whatever it held and print the bytes
    // that followed it as a name.
    case ROGUE_JOURNAL_PARAM_TRAINER:
        if (value < TRAINERS_COUNT)
            StringCopy(dest, GetTrainerNameFromId(value));
        else
            dest[0] = EOS;
        break;
    case ROGUE_JOURNAL_PARAM_ITEM:
        StringCopy(dest, GetItemName(value));
        break;
    case ROGUE_JOURNAL_PARAM_MAPSEC:
        if (value < MAPSEC_NONE)
            StringCopy(dest, gRegionMapEntries[value].name);
        else
            dest[0] = EOS;
        break;
    // Never NULL by contract, and it clamps an out-of-range id itself.
    case ROGUE_JOURNAL_PARAM_CHARM:
        StringCopy(dest, RogueCharm_GetName(value));
        break;
    case ROGUE_JOURNAL_PARAM_NONE:
    default:
        // CLEARED, not left alone. A row declaring NONE has no placeholder for
        // it and check_run_journal.py enforces that pairing - but a stale buffer
        // is one table typo away from being printed as a real word from an
        // unrelated system, which is the failure that rule exists to prevent.
        // Clearing costs nothing and removes the sharp edge entirely.
        dest[0] = EOS;
        break;
    }
}

void RogueJournal_BufferLine(const struct RogueJournalEntry *entry, u8 *dest)
{
    const struct RogueJournalKind *info;

    if (entry == NULL)
    {
        dest[0] = EOS;
        return;
    }

    info = RogueJournal_KindInfo(entry->kind);

    BufferJournalParam(info->param1Kind, entry->param1, gStringVar1);
    BufferJournalParam(info->param2Kind, entry->param2, gStringVar2);
    StringExpandPlaceholders(dest, info->text);
}

const struct RogueJournalEntry *RogueJournal_EntryFromNewest(u32 index)
{
    struct RogueJournal *data = RogueJournal_Data();

    if (index >= data->count)
        return NULL;

    // head is the OLDEST, so the newest sits count - 1 steps forward from it.
    // index < count, so count - 1 - index cannot underflow.
    return &data->entries[(data->head + data->count - 1 - index) % ROGUE_JOURNAL_ENTRIES];
}
