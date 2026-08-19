# The run journal

A persistent, cross-run log of what happened, read on the **back of the trainer
card**. Modelled on the DPPt save journal: short prose lines, newest first,
grouped by run, with a separator line naming how each run ended.

Status: **all six steps are built.** Storage, the append primitive, eleven kinds,
ten hooks, the card back, paging, and the front side's stars and badge row.
`check_run_journal.py` is at **41 selftest breaks**, and `test/rogue_journal.c`
adds **11 ROM tests** that run the ring on emulated hardware. Three event kinds
were deliberately left out — see §4. The durable record now belongs in the
skill's `roguelike-state.md`; this file is the reference for the feature.

As built: **EWRAM 242,292 → 243,064 B (+772, exactly the ring)**, IWRAM unmoved
at 19,444 B, ROM 27,540,376 → 27,542,936 B (+2,560), **49 checks + 11 ROM tests,
0 failures**. The card's page state costs 256 bytes inside `sData`, which is a
heap allocation, so EWRAM does not move for it.

**Everything testable has executed; nothing has been on screen.** `make check
TESTS='run journal'` runs the append, the wrap, the lazy migration, the line
renderer for all eleven kinds, the param bounds and the paging walk on real
emulated hardware, and each was broken on purpose to confirm the tests fail.
What no test reaches is the drawing — six rows of `FONT_NARROW` against the back
art at game scale, whether the page indicator clears the name, whether the old
glyph marks are gone, and whether the badge row reads as progress. **That still
needs playing, and the line widths are the first thing to look at.**

---

## 1. Why the trainer card

It is already reachable (`Usm_InitStartMenu` → `ShowPlayerTrainerCard`,
`src/unbound_start_menu.c:526`), already a full CB2 screen with its own heap
allocation, and **its entire back side is dead on this build**. Of the six stat
rows `PrintAllOnCardBack` can draw, four are link statistics that are
permanently zero in a roguelike (link battles, trades, Pokéblocks with friends,
link contests) and the other two are HOF debut time and frontier BP.

The front is nearly as dead: `RogueDungeon_ApplyNewGameUnlocks`
(`src/rogue_dungeon.c:2810`) sets `FLAG_BADGE01_GET` through `08` at new game so
adopted boss aces obey, so `DrawStarsAndBadgesOnCard` draws all eight badges
always, and `CountPlayerTrainerStars` reads four conditions the run can barely
reach.

So this is a repurpose, not an addition: no new screen, no new entry point, no
new task, and the flip animation, the palettes and the card art all come free.

**D-pad, L, R, START and SELECT are unused on both sides of the card.** Paging is
free.

---

## 2. Storage

### Where

`struct SaveBlock3`, appended **after** `rogueCharms`.

Measured from `PokeSMD.map`: `gSaveblock3` spans `0x02016258`–`0x020162e4`,
**140 bytes used against the 1624-byte cap**. `OW_USE_FAKE_RTC`,
`FNPC_ENABLE_NPC_FOLLOWERS`, `OW_SHOW_ITEM_DESCRIPTIONS` and
`USE_DEXNAV_SEARCH_LEVELS` are all off and `APRICORN_TREE_COUNT` is 0, so
roughly **1.4 KB is free**. `save.c:80` already static-asserts the cap, so
overrunning it fails the build rather than the save.

Vars are the wrong tool. The unused pool is down to eight ids
(`0x404E`, `0x409D`, `0x40A1`, `0x40A8`, `0x40B8`, `0x40BB`, `0x40DB`, `0x40DC`)
and a journal is not var-shaped.

### Append, never insert

Same rule that governs `USM_ICO_CHARMS`: anything inserted before an existing
member shifts every member after it, and an old save then reads the wrong bytes
for real state. The journal goes last.

### The version byte

Copy `RogueCharm_Data` (`src/rogue_charms.c:359`) exactly — lazy migration
checked on **every access**, not at load:

```c
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
```

`load_save.c` has no post-load hook — `ClearSav3` runs on a new game and nothing
runs when a save is continued — so checking on access is what covers all three
arrival paths, including the one that matters: a save written before this struct
existed, which otherwise reads whatever bytes were already in the sector and
renders them as journal entries.

This mechanism has already saved this project twice, in this same block.

---

## 3. The record

```c
struct PACKED RogueJournalEntry
{
    u8  kind;    // ROGUE_JOURNAL_* ; 0 is ROGUE_JOURNAL_NONE, an empty slot
    u8  floor;   // DISPLAYED floor 1..DUNGEON_TOTAL_FLOORS (115), 0 = not floor-scoped
    u16 param1;
    u16 param2;
};                                                          // 6 bytes
```

### Why six bytes and not four

Save space is not the binding constraint — 1.4 KB was free. **The binding
constraint is that changing this layout later means bumping
`ROGUE_JOURNAL_SAVE_VERSION`, which discards every existing player's journal.**
A four-byte record cannot express "X evolved into Y", "caught X on floor N with
item Y", or any event with two named operands without spending a second entry on
it, and discovering that after release costs everyone their history.

### `PACKED` is load bearing, and it was found by measuring

**Without `PACKED` the record occupies eight bytes, not six.** This toolchain
rounds an unpacked struct up to a multiple of four, so the six bytes of members
above became eight, and the 128-entry ring cost **1028 bytes of EWRAM instead of
772** — 2 bytes per entry, 256 wasted, in the block this project's own notes
call the ceiling.

Nothing failed. It built clean, every check passed, and the journal would have
worked perfectly. **What exposed it was the linker's EWRAM figure moving by more
than the struct accounted for** — 1028 against a predicted 772, and 1028 is
`128 × 8 + 4`. Reasoning about `sizeof` from the member list is what produced the
wrong number in the first place.

Two static asserts now hold it, in `rogue_journal.c`, because a comment would
not have:

```c
STATIC_ASSERT(sizeof(struct RogueJournalEntry) == 6, RogueJournalEntryIsSixBytes);
STATIC_ASSERT(sizeof(struct RogueJournal) == ROGUE_JOURNAL_ENTRIES * 6 + 4, RogueJournalRingIsExact);
```

The second also catches a header field being added without the save version
being bumped alongside it.

**And packing the entry drops the containing struct's alignment to 1**, which is
the second half of the same bug: `RogueJournal_Data` zeroes the ring with
`CpuFill16`, a 16-bit fill that needs at least 2-byte alignment. It lands
4-aligned today only because `rogueCharms` sits in front of it carrying a `u32`
— an accident that holds right up until someone reorders SaveBlock3. Hence
`struct ALIGNED(4) RogueJournal`. The 6-byte stride keeps every `param1`/`param2`
genuinely 2-byte aligned, so the byte-wise accesses `PACKED` generates cost only
speed, on a path that runs a handful of times per card page.

`floor` is a `u8` because `DUNGEON_TOTAL_FLOORS` is 115
(`80 gym + 25 Elite Four + 10 final`). It stores the **displayed** floor, matching
`VAR_ROGUE_BEST_FLOOR` and `VAR_ROGUE_LAST_RUN_FLOOR`, so zero can mean "no
floor" — dying on floor 1 is the commonest way a run ends and must not be
indistinguishable from unset.

### The ring

```c
#define ROGUE_JOURNAL_ENTRIES 128

struct ALIGNED(4) RogueJournal
{
    struct RogueJournalEntry entries[ROGUE_JOURNAL_ENTRIES];
    u8 head;     // index of the OLDEST entry
    u8 count;    // 0..ROGUE_JOURNAL_ENTRIES
    u8 runIndex; // runs begun, for the separator text; saturates
    u8 version;  // ROGUE_JOURNAL_SAVE_VERSION
};                                                          // 772 bytes
```

**`ROGUE_JOURNAL_ENTRIES` must fit in a `u8`.** The ring is full only when
`count` *equals* it, and `count` is a `u8` — so at 256 the count wraps to zero on
the 256th append, the ring never marks itself full, `head` never advances, and
the journal reads as empty forever with nothing wrong in any table. That is a
`STATIC_ASSERT` in `rogue_journal.c`, not a comment.

Measured after the fact: SaveBlock3 is **912 bytes of its 1624-byte cap**, so
712 remain.

`head`/`count` have **exactly one writer**, `RogueJournal_Append`. That is a rule
the check enforces, not a convention — a second site advancing `count` without
advancing `head` on a full ring silently overwrites the newest entry with the
oldest one every append after that, and the journal keeps rendering perfectly.

---

## 4. The kind table

Adding an event must be a **table row**, not a code edit — the same rule that
governs dungeon themes.

```c
enum
{
    ROGUE_JOURNAL_PARAM_NONE,
    ROGUE_JOURNAL_PARAM_NUMBER,
    ROGUE_JOURNAL_PARAM_SPECIES,
    ROGUE_JOURNAL_PARAM_TRAINER,
    ROGUE_JOURNAL_PARAM_ITEM,
    ROGUE_JOURNAL_PARAM_MOVE,
    ROGUE_JOURNAL_PARAM_DUNGEON,
};

struct RogueJournalKind
{
    const u8 *text;    // {STR_VAR_1} / {STR_VAR_2}
    u8 param1Kind;
    u8 param2Kind;
    bool8 isSeparator; // drawn in the stat colour, spans the row
};

static const struct RogueJournalKind sJournalKinds[ROGUE_JOURNAL_KIND_COUNT];
```

One resolver, `BufferJournalParam(paramKind, value, dest)`, turns a `u16` into a
string: `GetSpeciesName` (`include/pokemon.h:819`), `GetTrainerNameFromId`
(`include/data.h:308`), `ItemId_GetName`, `GetMoveName`, or
`gRegionMapEntries[ThemeForFloor(n)->mapSecId].name` for a dungeon.

Adding an event is then: one enumerator, one table row, one `RogueJournal_Append`
call at the site.

**The trap this table shape creates, and the check catches:** a row whose format
string contains `{STR_VAR_2}` while declaring `ROGUE_JOURNAL_PARAM_NONE` for
`param2Kind` prints **whatever `gStringVar2` was left holding** by something
else. That is not a blank — it is a real word from an unrelated system, printed
into a sentence that reads as if it meant it. The check asserts placeholder count
against declared param kinds, in both directions.

### Starting set

**Eleven kinds as built**, ids 0–10:

| kind | text | params | hook |
|---|---|---|---|
| `RUN_STARTED` | *sep* `Run {1} began` | number | `RogueJournal_OnRunStarted` |
| `RUN_FELL` | *sep* `Fell on floor {1}` | number | `TryHandleWhiteOut` |
| `RUN_CLEARED` | *sep* `Cleared the dungeon` | — | `OnRunCompleted` |
| `BOSS_BEATEN` | `Defeated {1}` | trainer | `JournalBossDefeated` |
| `MINIBOSS_BEATEN` | `Beat {1}` | trainer | `JournalBossDefeated` |
| `ACE_ADOPTED` | `{1} left by {2}` | species, trainer | `GiveBossAce` |
| `TM_TAKEN` | `Took {1}` | item | `GiveBossTM` |
| `DUNGEON_ENTERED` | `Reached {1}` | mapsec | `JournalDungeonEntered` |
| `CHARM_MON` | `{1} is {2}` | species, charm | `ScriptGrantMon` |
| `CHARM_PARTY` | `The team is {1}` | charm | `ScriptGrantParty` |

`DUNGEON_ENTERED` at every dungeon boundary is what gives a 128-entry ring a
readable spine even when nothing else was worth recording.

**Adding the seven event kinds needed NO save version bump**, which is the record
format doing exactly what it was widened for: ids 0–3 still mean what they meant,
so a journal written before this build reads back unchanged.

**`CHARM_MON` and `CHARM_PARTY` are two kinds, not one with an optional
species**, for the reason the charm system itself gives: a party-wide affliction
and a per-Pokémon one are different things to the player. Same for
`BOSS_BEATEN` / `MINIBOSS_BEATEN` — they arrive from the same script, so a shared
row would leave the card unable to tell a gym leader from a corridor grunt.

### Three kinds deliberately NOT built

`MON_CAUGHT`, `MON_EVOLVED` and `MON_LOST` are the most journal-like entries and
none of them has a hook in a `rogue_*` file. They need `pokemon.c`,
`evolution_scene.c` and `party_menu.c` respectively — the first two are upstream
files this branch already patches, so each hook is one more thing to re-host on
every merge, and **`party_menu.c` is a montmoguri fork that already orphaned the
EV allocator once by being replaced wholesale.** They are worth doing; they are
not worth doing as an afterthought at the end of a six-step feature. Each is a
table row, an enumerator and one call whenever the appetite is there.

---

## 5. Where entries are written

Every hook already exists. None of these needs a new call site invented for it:

| event | site |
|---|---|
| boss defeated | `RogueDungeon_OnBossDefeated`, `src/rogue_dungeon.c:5830` |
| run cleared | `RogueDungeon_OnRunCompleted`, `src/rogue_dungeon.c:3158` |
| run lost | the whiteout path that writes `VAR_ROGUE_LAST_RUN_FLOOR`, `src/rogue_dungeon.c:3653` |
| ace adopted | `RogueDungeon_GiveBossAce`, `src/rogue_dungeon.c:3104` |
| charm gained | the nine mutating paths `RecalcParty` was added to, `src/rogue_charms.c` |
| dungeon entered | `PrepareFloor`, on a dungeon boundary |
| caught / evolved | vanilla capture and `EvolutionScene` paths |

---

## 6. Run separators

The journal is **persistent** and `RogueDungeon_ResetRun` must not touch it.
That is the one place this feature deliberately disagrees with everything else
in that function: `ResetRun` wipes the party, the bag, the coins, the money, the
charms and the rod flags precisely because they are per-run. The journal is the
record *of* those runs ending, so a check asserts `ResetRun` does **not** clear
it — clearing it there looks like tidiness and destroys the feature.

Separators are written by the two **terminal** paths, not by `ResetRun`, because
only they know the outcome:

- **Loss** — in `RogueDungeon_TryHandleWhiteOut`, beside the
  `VAR_ROGUE_LAST_RUN_FLOOR` write, which already has the floor in hand.
- **Win** — in `RogueDungeon_OnRunCompleted`, called from the boss script's
  run-complete branch only and pointedly *not* from inside `ResetRun`, for
  exactly the reason that matters here: **a loss must not write a win.** That
  separation already exists and the journal inherits it. The check asserts each
  terminal writes its own kind **and not the other's**.

**The losing append reads `VAR_ROGUE_LAST_RUN_FLOOR`, not
`VAR_ROGUE_DUNGEON_FLOOR`, on purpose.** `ResetRun` puts the floor counter back
to 0 and leaves that var standing, so the append cannot be silently broken by
being moved after it. That is why there is *no* ordering rule in the check:
asserting an order the code does not depend on would be a rule that can never
catch anything — write the order-independent version instead.

### `RUN_STARTED` is a special, not a C hook

There is **no C function that runs exactly once per run.** Both the whiteout path
and the run-complete path warp to floor one and the starter prompt runs
afterwards, so the starter script is the once-per-run event.
`RogueJournal_OnRunStarted` is therefore a `def_special`, called from
`data/maps/RogueDungeonFloor/scripts.inc` immediately before
`setvar VAR_ROGUE_RUN_STATE, ROGUE_RUN_ACTIVE` — beside that line deliberately,
so a run that exists without a journal entry opening it is not reachable.

`RogueDungeon_ResetRun` looks like the obvious C site and is wrong twice over: it
also runs on the losing path *before* the player has picked anything, and it is
the one function that must never mention the journal at all.

It is also the only writer of `runIndex`, which the check enforces separately
from the ring's `head`/`count` — a second stepper numbers two runs the same.

**The reader partitions on separators, and the newest group is the run in
progress**, which is what makes page 1 a run-progress checker rather than a
history book. A ring that wraps can drop a separator; the oldest group then
reads as "earlier", which is acceptable and needs no code.

---

## 7. Rendering

### Geometry, unchanged

`PrintStatOnBackOfCard` (`src/trainer_card.c:1213`) puts a row at
**y = top × 16 + 33**, label at x=16 and value right-aligned to x=216, with the
player name at y=9. `WIN_CARD_TEXT` is 28×18 tiles (224×144 px) on BG1.

**Stage one keeps the vanilla six-row grid** — `top` 0..5, y=33/49/65/81/97/113 —
and adds a left-aligned sibling, `PrintJournalLineOnBackOfCard(top, line, color)`.
Six entries per page.

This is deliberate. `graphics/trainer_card/back.bin` is a 30×20 tilemap with the
row rules **baked into the art**, so a denser layout is an art change, and the
project's own rule is to judge art against its neighbours at game scale rather
than on a contact sheet. Six rows with paging costs no art risk and ships. A
repaint for 10–12 lines is stage two, if paging turns out to feel bad.

Separator rows use `sTrainerCardStatColors` (red); ordinary rows use
`sTrainerCardTextColors` (grey). Both tables already exist.

### `DrawCardBackStats` has to be rewritten

`src/trainer_card.c:1537` writes decorative glyph tiles at **hardcoded BG3 cells**
(rows 9–16) that match the wins/losses marks printed in the vanilla back art.
Left alone, those marks appear over the journal. It becomes either a no-op or a
floor-tag decorator.

### Buffer first, print second

Follow `BufferTextsVarsForCardPage2`: a `BufferJournalPage()` fills
`sData->journalLines[6][40]`, and the print state machine only prints. Expanding
placeholders inside the per-frame print path would interleave `gStringVar1`,
`gStringVar2` and `gStringVar4` with the front side's own uses of them
(`PrintMoneyOnCard` uses `gStringVar1` and `gStringVar4`).

**The dead link buffers were NOT reclaimed, and that plan was wrong.**
`textBerryCrushPts`, `textUnionRoomStats`, `textNumLinkPokeblocks` and
`textNumLinkContests` look like ~350 free bytes because they are unreachable on
an Emerald card — but they are written by the **FRLG** branches, and a link
partner can be running FireRed, in which case `sData->trainerCard.version` makes
those branches live. Deleting them to save 254 bytes inside a 31 KB heap
allocation would have broken a real, if rare, path for nothing. The page buffer
is simply added.

---

## 8. Paging

D-pad up/down (or L/R) in `STATE_HANDLE_INPUT_BACK` changes page. A and B keep
their meanings — A closes, B flips to the front — so nothing is displaced.

**Redraw through a new `mainState`, not a synchronous loop.** Add
`STATE_REDRAW_BACK` that drives `PrintAllOnCardBack()` one step per frame exactly
as `mainState` 1 does for the front, then returns to `STATE_HANDLE_INPUT_BACK`.
The eight-step-per-frame split is what the card's frame budget is built around;
`Task_DrawFlippedCardSide` (`:1683`) only runs its steps back-to-back when
`!gReceivedRemoteLinkPlayers`, and that exception exists for the link timing, not
as a licence to draw synchronously.

The redraw sequence is the one the flip path already uses:
`FillWindowPixelBuffer(WIN_CARD_TEXT, PIXEL_FILL(0))` → print steps →
`DrawTrainerCardWindow(WIN_CARD_TEXT)`. As built it is **two** states,
`STATE_REDRAW_BACK_CLEAR` and `_PRINT`: the clear arm also re-expands the lines
and sets `printState` explicitly rather than trusting the value the flip path
happens to leave behind.

Page 0 is newest. The count is derived once in `SetDataFromTrainerCard` beside
the gate, and the page indicator shares the **name row** at y=9 — the name is
right-aligned to x=216 there and the left half is empty — rather than spending
one of the six journal rows on itself. Hidden at one page, where "Page 1/1" is
noise.

**It clamps rather than wrapping.** A wrap makes the ends invisible: the player
cannot tell "this is the oldest thing I have" from "it came round again", and
knowing how far back the journal goes is the point. The music player's queue
wraps for the opposite reason — a one-entry queue there has to restart itself.

**The failure this arrangement is built to avoid** is a redraw that moves the
indicator without re-expanding the lines. The number and the six rows come from
different places (`journalPage` and `journalLines`), so skipping the re-buffer
draws "Page 2/3" over page one's entries — clean build, plausible screen, wrong
content, and both halves individually correct so nothing else would catch it.
That is check rule 16.

---

## 9. The front side

Optional, and separable from everything above — do it after the back works.

| element | now | proposed |
|---|---|---|
| ID No. | trainer id | run number, or the run seed |
| MONEY | money | keep — it is real currency in the rest stop |
| POKéDEX | caught count | keep |
| TIME | play time | keep |
| badge row | all eight, always | the eight **gym dungeons cleared this run** |
| stars | four unreachable conditions | runs completed, **clamped to 4** |

The badge row has eight 16×16 slots at 3-tile spacing from x=4
(`DrawStarsAndBadgesOnCard`, `:1512`), so the eight gym dungeons fit exactly and
the five Elite Four dungeons plus the final one do not — showing only the gyms is
the honest reading, not a compromise.

**Stars must be clamped to 4.** The value indexes `sHoennTrainerCardPals[]`,
which has five entries, and it is also sent over link. Runs completed is a `u16`.
Wiring it in unclamped reads a palette pointer past the end of the table on the
fifth win — the card would come up in whatever colours followed it in ROM, or
worse.

---

## 10. Link safety

**This is the one thing that fails in a way nobody will test for.**

`struct TrainerCard` is the link wire format. It lives in `gTrainerCards[4]`, is
received from partners, and `CopyTrainerCardData` (`:806`) memcpys a **fixed
0x38 or 0x60 byte prefix** by card type while `TrainerCard_GenerateCardForLinkPlayer`
memsets exactly `0x60`. Fields appended past those bounds are silently not
transferred and silently not cleared.

So:

1. **No run state goes in `struct TrainerCard`.** The print functions read
   `RogueJournal_Data()` and `VarGet` directly. They run local-only.
2. **Every journal site is gated on `!sData->isLink`.** A received card fills
   `sData->trainerCard` with the *partner's* data, but a `RogueJournal_Data()`
   call in the same function returns **yours** — so without the guard, viewing a
   friend's card shows your run journal under their name, with no error and
   nothing out of place on screen.
3. Derive a `hasJournal` in `SetDataFromTrainerCard` (`:824`) alongside
   `hasHofResult` and friends, so the gate is expressed the way the file already
   expresses every other row's gate.

---

## 11. `check_run_journal.py`

A **lifetime** check, not a data one. Copy `check_jukebox_funnels.py`: state the
ownership rule once at the top of the file, then assert the pairing that makes it
true. Accept the repo **both** positionally and as `--repo`, the way
`check_start_menu_pages.py` does.

> **The rule.** The journal is the only cross-run record the game keeps. Every
> path that ends a run writes a separator naming how it ended; nothing that
> resets a run erases it; and every entry it renders is the local player's.

**Built, with 17 `--selftest` breaks, all firing:**

1. **The ring fits a `u8`**, and the `STATIC_ASSERT` that enforces it is present.
   *Breaks: raise it to 256; delete the assert.*
2. **The record is `PACKED`, both size asserts are present, and the ring is
   `ALIGNED`.** *Breaks: unpack it; delete either assert; unalign it.* This is
   the padding bug above, guarded so it cannot return.
3. **`rogueJournal` is the last member of SaveBlock3.** *Break: add a member
   after it.*
4. **`RogueJournal_Data` checks the version on every access and rewrites it
   after zeroing.** *Breaks: remove either half.* Without the rewrite it re-zeroes
   on every access, which is a journal that is always empty.
5. **`RogueJournal_Append` is the only writer of `head` and `count`, and
   `rogue_journal.c` is the only file that reaches
   `gSaveBlock3Ptr->rogueJournal`.** *Breaks: a second site assigning `->count`;
   advancing `->head` elsewhere; another file touching the save member.*
6. **`RogueDungeon_ResetRun` does not mention the journal.** *Break: add a clear.*
7. **Every kind has a row, and no row names a kind that does not exist.**
   *Breaks: delete a row; add one for an undefined kind.* This is the
   `sRogueMusicPlaylistParent` failure exactly — the C default fill for an
   omitted designated initialiser is 0, and 0 is a valid enumerator, so a missing
   row is not absent, it is **wrong**.
8. **Placeholders and declared params agree, both directions.** *Breaks: a
   declared param with no `{STR_VAR_n}`; a `{STR_VAR_n}` with `PARAM_NONE`.*

Note on break 7: the "row for a kind that does not exist" case originally
*renamed* a row, which trips the missing-row rule first — so the extra-row rule
was never exercised behind a selftest line that read as green. It adds a row now.
That is the same failure mode as the two anchors that silently expired when Johto
was appended.

**Step 2 added three more, taking it to 24 breaks:**

9. **Every kind with `isSeparator` has an append site.** *Break: flip the `NONE`
   placeholder to a separator.* Generic rather than a list of the three, so a
   separator added later cannot be defined, tabled, rendered and never written —
   which is a run boundary that silently does not exist, and the reader
   partitions the ring on exactly these.
10. **Each terminal writes its own kind and not the other's.** *Breaks: delete
    either append; make the losing path append `RUN_CLEARED`.*
11. **`RogueJournal_OnRunStarted` is both declared and called.** *Breaks: remove
    the `def_special`; remove the script call.* Both halves, because they fail
    differently — undeclared fails the build, declared-but-uncalled builds clean
    and leaves every run with no opening boundary.

**The selftest found a hole in the check itself, which is the point of having
one.** The single-writer rule was anchored as `\w+->(head|count|runIndex)`, and
`RogueJournal_Data()->runIndex++` has a `)` before the arrow, not an identifier —
so the most natural way for a second writer to appear was invisible to the rule
that exists to catch it. It anchors on the arrow now.

**Step 3 added four more, taking it to 31 breaks:**

12. **The kind ids are exactly `0..KIND_COUNT-1`.** *Break: renumber one to leave
    a gap.* Ids are stored in the save and index `sJournalKinds` directly, so a
    gap indexes past a row and a duplicate renders two events as one.
13. **`hasJournal` is assigned exactly once, in `SetDataFromTrainerCard`, under
    `!sData->isLink`.** *Breaks: drop the link test; add a second assignment;
    move the assignment elsewhere.*
14. **`PrintAllOnCardBack` and `DrawCardBackStats` both consult it.** *Breaks:
    remove either.*
15. **Every `RogueJournal_*` call in `trainer_card.c` sits in a function that
    consults `hasJournal`.** *Break: a journal read in `PrintNameOnCardBack`.*

**The kind parser had to be rebuilt structurally.** It began as "every
`ROGUE_JOURNAL_*` define except these three names", and the first layout constant
added for step 3 — `ROGUE_JOURNAL_LINE_LENGTH` — was immediately reported as a
kind with no table row. A hand-maintained exclusion list is the same shape as the
one `run_all_checks.sh` keeps, which this repo has already watched drift twice.
Kinds are now exactly the defines *above* `ROGUE_JOURNAL_KIND_COUNT`, which is
where they must be for the count to mean anything.

### The ROM tests, and breaking them on purpose

`test/rogue_journal.c` is 8 cases run under mgba by `make check TESTS='run
journal'`. The check asserts the *shape* of this code and cannot execute a line
of it; the ring's wrap is arithmetic that fails silently, so it wanted the other
kind of test.

**Both mechanisms were broken deliberately to confirm the tests fail.** Deleting
the `head` advance failed *a full ring drops the oldest and never the newest*;
deleting the `dest[0] = EOS` failed *a row with no params clears the buffer it
does not use*. The other six stayed green, which is what says the two cases are
pointed at the right things.

**One of those tests was vacuous when first written**, and it is worth recording
why. It compared two renderings of `RUN_CLEARED` and required them to match —
which they do no matter what `BufferJournalParam` does, because that row's text
has no `{STR_VAR_1}` to leak into. It asserts `gStringVar1[0] == EOS` directly
now. A test that cannot fail was one line of reasoning from shipping green and
empty, which is the third time this session a break test earned its keep.

**Step 4 added four more check breaks (35) and a ninth ROM test:**

16. **Turning a page re-expands the lines**, and the back's input arm routes into
    the redraw state. *Breaks: delete the `BufferJournalPage()` call; make the
    d-pad never reach the redraw.*
17. **Paging is gated on `hasJournal`** — `TryChangeJournalPage` joins the gate
    consumers. *Break: drop the gate from its guard.*
18. **`journalPages` is derived only in `SetDataFromTrainerCard`.** *Break: a
    second derivation.*

The ROM test *paging walks every entry exactly once, in order* lifts the card's
index expression (`page * ROWS + row`) verbatim and asserts that walking 20
entries page by page — four pages, the last one short — yields every entry once
in order. The card's own copy lives in a static function a test cannot call, so
what is pinned is the arithmetic. Breaking `EntryFromNewest`'s bound by one
failed it along with four others; restored, all nine pass.

**No check rule guards the page clamp, deliberately.** An out-of-range page makes
`EntryFromNewest` return `NULL`, which `BufferJournalPage` already handles — the
result is a blank page, not corruption, and that NULL behaviour is itself covered
by a ROM test. A brittle textual rule for a benign failure is not worth the line.

### Steps 5 and 6: 41 breaks, and two holes the check found in itself

19. **EVERY kind has an append site**, not only the separators. *Breaks: delete
    the `TM_TAKEN` append; delete the `CHARM_PARTY` append.* This rule was
    separators-only when three boundaries were all that existed; step 5 added
    seven event kinds and the old scope would have watched all seven go unhooked
    without a word.
20. **All three journal specials are declared and called.** *Breaks: undeclare
    the boss special; drop either script call.*
21. **`CountPlayerTrainerStars` clamps to 4.** *Break: return the raw count.*

**Rule 19 immediately caught real code.** `RogueDungeon_JournalBossDefeated`
picked its kind with a ternary and passed the result as a variable, so neither
`BOSS_BEATEN` nor `MINIBOSS_BEATEN` had a literal append site the rule could see.
It is two literal calls now. That is a real constraint the check imposes on how
hooks are written, and it is worth stating: **a kind must reach
`RogueJournal_Append` by name.**

**And the selftest harness had a hole that made it worthless in exactly the
situation it was needed.** With the tree genuinely broken, `check()` returns
`False` for *every* mutation — so all 41 cases "fired", and the harness printed
`selftest passed` over a completely broken checkout. It now runs the check on the
unmutated tree first and aborts if that fails. The guard earned its place on its
first run, by exposing a case that had silently retired: `a separator nothing
ever appends` flipped the `NONE` placeholder into a separator, which stopped
firing the moment rule 19 exempted `NONE`. It targets the "there must be
separators at all" rule now.

### The two ROM tests steps 5–6 added

*every kind renders a non-empty line* walks all eleven kinds and requires each to
produce text. A row wired wrong does not crash — it renders **blank**, which on
the card is indistinguishable from an empty row.

*an out-of-range param renders empty rather than garbage* pins the resolver
bounds. It tests `MAPSEC_NONE`, the first value past the end, **and not
`0xFFFF`** — that was tried, and a wild index does not fail an assertion, it
takes the whole test runner down (`Invalid TestRunner state, exiting`) and on a
second attempt hung mgba entirely, with `StringCopy` running away through ROM.
That proves the hazard is real and proves nothing about the test. The boundary
value reads adjacent ROM: harmless to execute, non-empty, and therefore a clean
failure when the bound is removed. One `<` covers both.

`sizeof(struct SaveBlock3)` needs no assertion here — `save.c:80` already
static-asserts it, and a second copy of that rule is a second thing to keep true.

### What it cannot cover

The same limit the fifteen play-found fixes established. It cannot tell whether
a hook is ever **reached**: a `BOSS_BEATEN` append sitting in a branch the boss
script does not take looks identical, from the table, to one that works. It says
nothing about whether six rows are legible at game scale, whether the page
indicator collides with the name, or whether `DrawCardBackStats`'s old glyphs are
really gone. Those are screen questions and only a screen answers them.

---

## 12. Build order

Each step builds and is checkable on its own.

1. ~~`struct RogueJournal` in SaveBlock3, `RogueJournal_Data`,
   `RogueJournal_Append`, the kind table with the three separators only. Nothing
   renders.~~ **Done.** `include/constants/rogue_journal.h`,
   `include/rogue_journal.h`, `src/rogue_journal.c`, the SaveBlock3 member, and
   `tools/rogue/check_run_journal.py` at 17 selftest breaks.
2. ~~The two terminal hooks plus `RUN_STARTED`. The journal is now correct and
   invisible.~~ **Done.** `RogueJournal_OnRunStarted` (special + `specials.inc` +
   the floor script), `RUN_FELL` in `RogueDungeon_TryHandleWhiteOut`,
   `RUN_CLEARED` in `RogueDungeon_OnRunCompleted`.
3. ~~The card back: `hasJournal`, `BufferJournalPage`, the left-aligned printer,
   `DrawCardBackStats` rewritten, the link guards.~~ **Built, not played.** The
   journal *replaces* the six stat rows rather than sharing the page; a link card
   or an empty journal falls through to the vanilla rows untouched.
   `test/rogue_journal.c` covers the ring on hardware. **What is still unverified
   is every pixel of it** — six rows against the back art, at game scale, with
   the old glyph marks gone.
4. ~~Paging and `STATE_REDRAW_BACK`.~~ **Built, not played.** D-pad up/down,
   clamped, redrawing through `STATE_REDRAW_BACK_CLEAR` / `_PRINT`, with the page
   indicator in the name row.
5. ~~The remaining event kinds, one table row at a time.~~ **Done, minus three.**
   Seven kinds and their hooks; `MON_CAUGHT`, `MON_EVOLVED` and `MON_LOST` are
   left out with reasons in §4.
6. ~~The front side, if wanted, with the star clamp.~~ **Done, partly.** Stars are
   runs cleared (clamped, checked) and the badge row is dungeons cleared this
   run. The ID number was left as the trainer ID — it is a link-visible field
   that genuinely identifies the player, unlike the two that were dead.

---

## 13. Open questions

- ~~**Ring size.**~~ **Settled at 128 / 772 bytes.** A run with
  `DUNGEON_ENTERED` on all fourteen dungeon boundaries plus its bosses spends
  ~30 entries by itself, so 128 is roughly four runs of history. Raising it later
  is a version bump that discards every journal, which is why it was settled
  before the hooks landed rather than after.
- **Whether `MON_LOST` can be detected at all** without hooking party death in a
  fork this branch would then have to re-host after every upstream merge.
- **Whether the front side is worth touching**, or whether badges-always-eight is
  better left as visible evidence that the run grants them.
