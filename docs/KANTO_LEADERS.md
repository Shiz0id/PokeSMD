# Kanto leaders, Elite Four and Champion — staging notes

Preparation for gap 16 in the roguelike state file: the Kanto gym leaders, the
Elite Four and the Champion as post-game content behind
`FLAG_ROGUE_RUN_COMPLETED`.

**Nothing here needs extracting from a FireRed ROM.** All thirteen trainers,
their parties, their battle art and their overworld sprites are already vendored
in this tree. The work is making them reachable from an Emerald build, not
importing them. Everything below was verified against `PokeSMD.map` rather than
against the headers, because `--gc-sections` drops what nothing references and a
header says only that a symbol was declared.

## What exists, and what it costs

| asset | state | cost to use |
|---|---|---|
| party data | `src/data/trainers_frlg.party` | none — data only |
| **battle pics** | **already in the ROM, all thirteen** | **none** |
| overworld sprites | PNGs on disk, **excluded from the build** | ROM for 13 sprites |

### Battle pics are free

`gTrainerFrontPic_LeaderBrockFrlg` and its twelve siblings are all present in
`PokeSMD.map` today. The FRLG trainer pic set comes across wholesale — unrelated
entries like `gTrainerFrontPic_PainterFrlg` are in there too — so the battle art
costs nothing at all.

### Overworld sprites are guarded out

The PNGs exist (`graphics/object_events/pics/people/brock.png` and so on) and the
`OBJ_EVENT_GFX_*` constants exist, but the pointer table rows that reference them
sit inside `#if IS_FRLG` in
`src/data/object_events/object_event_graphics_info_pointers.h`, lines 658-803.
In an Emerald build they are not compiled, so `gObjectEventGraphicsInfo_Brock`
does not appear in the link map at all — not dropped by the collector, never
emitted.

Un-guarding the thirteen rows we want is the cheap move. Un-guarding the whole
block pulls in every FRLG overworld sprite, which is far more than this needs.

## The thirteen

Party sizes are the stock FireRed teams. Line numbers are into
`src/data/trainers_frlg.party`.

| trainer | line | mons | class | OW gfx |
|---|---|---|---|---|
| `TRAINER_LEADER_BROCK` | 7037 | 2 | Leader Frlg | `OBJ_EVENT_GFX_BROCK` |
| `TRAINER_LEADER_MISTY` | 7059 | 2 | Leader Frlg | `OBJ_EVENT_GFX_MISTY` |
| `TRAINER_LEADER_LT_SURGE` | 7085 | 3 | Leader Frlg | `OBJ_EVENT_GFX_LT_SURGE` |
| `TRAINER_LEADER_ERIKA` | 7119 | 3 | Leader Frlg | `OBJ_EVENT_GFX_ERIKA` |
| `TRAINER_LEADER_KOGA` | 7153 | 4 | Leader Frlg | `OBJ_EVENT_GFX_KOGA` |
| `TRAINER_LEADER_SABRINA` | 7237 | 4 | Leader Frlg | `OBJ_EVENT_GFX_SABRINA` |
| `TRAINER_LEADER_BLAINE` | 7195 | 4 | Leader Frlg | `OBJ_EVENT_GFX_BLAINE` |
| `TRAINER_LEADER_GIOVANNI` | 5025 | 5 | Leader Frlg | `OBJ_EVENT_GFX_GIOVANNI` |
| `TRAINER_ELITE_FOUR_LORELEI` | 6833 | 5 | Elite Four Frlg | `OBJ_EVENT_GFX_LORELEI` |
| `TRAINER_ELITE_FOUR_BRUNO` | 6884 | 5 | Elite Four Frlg | `OBJ_EVENT_GFX_BRUNO` |
| `TRAINER_ELITE_FOUR_AGATHA` | 6935 | 5 | Elite Four Frlg | `OBJ_EVENT_GFX_AGATHA` |
| `TRAINER_ELITE_FOUR_LANCE` | 6986 | 5 | Elite Four Frlg | `OBJ_EVENT_GFX_LANCE` |
| `TRAINER_CHAMPION_FIRST_*` | 7776 | 6 | Champion Frlg | `OBJ_EVENT_GFX_BLUE` |

**Note Giovanni sits apart at line 5025**, not with the other leaders — his gym
is the Rocket hideout in the stock game. Do not assume the block is contiguous.

**The Champion is three trainers, not one.** `TRAINER_CHAMPION_FIRST_SQUIRTLE`,
`_BULBASAUR` and `_CHARMANDER` differ by which starter the player chose. That
choice does not map onto a run with thirty starter picks, so pick one, or roll
one from the run seed. There are also `_2` rematch tiers for the Elite Four and
`TRAINER_CHAMPION_REMATCH_*` if a second difficulty band is ever wanted.

**The levels are stock FireRed and are nowhere near this curve.** Brock leads
with a level 12 Geodude. `verify_run_structure.py` check 7 holds every boss
within `SHUFFLE_TOLERANCE` of its slot, so these teams need scaling before they
can stand anywhere in a run. That is a design decision, not a port detail.

## The trainer id budget

### The Battle Frontier is NOT the low hanging fruit

`FRONTIER_TRAINER_*` are indices 0-299 into `gFacilityTrainers`, a
`struct BattleFrontierTrainer` array. **They are not `TRAINER_*` ids and they own
no defeat flag.** Pruning the Battle Frontier would free ROM and **zero trainer
ids**. The same is true of the Battle Tent and Battle Dome tables.

Frontier *brains* are real trainer ids and could be pruned, but there are only a
handful of them, and the roguelike's own generated table already excludes them
from the random pool, so they are not free either — the ids would come back but
the pool would not change.

### Where the ceiling actually is

`TRAINERS_COUNT_EMERALD` is 863 against `MAX_TRAINERS_COUNT_EMERALD` 864.
`TRAINER_ROGUE_RIVAL` is 855 and the seven divers take 856-862, leaving **one
free id**. Thirteen are needed.

The binding constraint is the trainer flag block: flags run 0x500 to
`TRAINER_FLAGS_START + MAX_TRAINERS_COUNT - 1`, and `SYSTEM_FLAGS` is defined as
`TRAINER_FLAGS_END + 1`. So raising the count does not hit a wall — it shifts
every system flag upward and grows `NUM_FLAG_BYTES`.

Computed, not estimated:

| `MAX_TRAINERS_COUNT_EMERALD` | `FLAGS_COUNT` | `NUM_FLAG_BYTES` | `SYSTEM_FLAGS` |
|---|---|---|---|
| 864 (today) | 2400 | 300 | 0x860 |
| 876 (+12, the minimum) | 2416 | **302** | 0x86C |
| **880 (+16)** | 2416 | **302** | 0x870 |

**876 and 880 cost the same two bytes**, because the flag count rounds up to a
byte boundary either way. So take 880: it yields **seventeen spare ids** for the
same price as the twelve strictly needed, which is the difference between doing
this once and doing it again for the Johto and Sinnoh leaders.

Two bytes against SaveBlock1, which has ~132 free. Storage is not the cost.

### The real cost is save invalidation

Shifting `SYSTEM_FLAGS` from 0x860 to 0x870 moves **every** system flag, badge
flag and `FLAG_ROGUE_*` alias. That is a compile-time renumbering, so nothing
breaks at runtime — but an existing save holds its flag bits at the old offsets
and would read them back shifted. Badges, run-completed, every event flag.

This build already has one save-invalidating change pending (appending to
SaveBlock3 for charms), so the cheap move is to **do both in the same version
bump** rather than spending two.

## Suggested order

1. Raise `MAX_TRAINERS_COUNT_EMERALD` to 880 and confirm the linker figures move
   by the predicted two bytes. Do this alone, and build, so a later failure is
   not ambiguous between this and the port.
2. Add the thirteen ids after `TRAINER_ROGUE_DIVER_7`, following the comment
   convention already there that records what each block spends.
3. Port the party data. It lives in a `.party` file the Emerald build does not
   read, so it has to be moved into `trainers.party` or read by a generator the
   way `gen_trainer_table.py` already reads the Emerald set.
4. Un-guard the thirteen overworld rows only, and re-check the link map.
5. Scale the parties to the curve, and only then decide where they stand.
