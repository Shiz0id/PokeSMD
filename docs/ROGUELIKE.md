# Procedural roguelike — implementation notes

A Mystery-Dungeon-style roguelike built on pokeemerald-expansion. Floors are
generated at runtime; the run mirrors the stock game's progression.

Everything here was established by reading the engine or mining vanilla data.
Where a number appears, it was measured. Where something is called a trap, it
cost real time to find.

---

## 1. Current state

**The run loop is complete, start to finish.** Pick two starters → descend →
catch and build a team → mini boss → gym leader → take their TM and optionally
adopt their ace → heal at a rest stop → next dungeon → Elite Four → Steven.
Losing wipes the run; so does winning, which is the point.

- **115 floors across 14 dungeons**, and they are not all the same length:

  | dungeons | floors each | boss | mini boss | run floors |
  |---|---|---|---|---|
  | 0–7, the gyms | 10 | Roxanne … Juan | yes, at the 5th | 1–80 |
  | 8–12, the Elite Four | 5 | Sidney … Wallace | **none** | 81–105 |
  | 13, the finale | 10 | Steven | the rival, at 110 | 106–115 |

  The Elite Four come every five floors because a gauntlet is what they are.
  Giving each of them a mini boss as well would pad the endgame with grunts,
  and there is no stock trainer at that level to draw one from anyway.
- **Wallace has the flower meadow**, dungeon 12 — Ever Grande, a mint plateau
  carpeted in flower fields and ringed by tan cliffs, which is what the vanilla
  city already is. It is the first theme with **two encounter surfaces**: short
  flower beds you walk over, and stands of flowery long grass you wade into, on
  different behaviours because they are different heights. Blossom blows across
  it — `WEATHER_PETALS`, the project's first **new weather**, in a free enum
  slot vanilla left empty. Also the first Elite Four pool not type-matched to
  its member — the dungeon is deliberately not water themed. Only the finale
  still wraps to another theme's art.
- **Eight themes, one per gym dungeon, no repeats**: Petalburg Woods (dungeon 1,
  Roxanne), Granite Cave (2, Brawly), New Mauville (3, Wattson), Fiery Path
  (4, Flannery), Mirage Tower (5, Norman), the Jungle (6, Winona), the open
  Ocean (7, Tate and Liza) and the Underwater seafloor (8, Juan). **The jungle
  is imported too** — it was built almost entirely from `gTileset_General`, with
  a two-metatile wall table, and read as a Route because that is what its art
  was. It has the Howling Jungle sheet now: twenty wall slots and a dirt floor.
- **Four more for the Elite Four**, which is where the stock game puts Victory
  Road — it is the road *to* them. One per member, with their own type-matched
  wild pools. **Three are the same tileset recoloured** — see §5 — Sidney violet
  for Dark, Phoebe near-black **and fogged** for Ghost, Drake crimson for Dragon.
  **Glacia is the exception**: she has **Lapis Cave**, the project's first
  tileset that is neither vanilla nor a recolour, imported from Mystery Dungeon
  sheets whose own autotile legend supplied all twenty wall slots — so it is
  also the first with four distinct inside corners and no composed slivers. It
  is also the first tileset **composed from two sheets**: Lapis Cave's crystal
  walls over Mt. Freeze's snow floor, with that sheet's two ground variants
  scattered over it as decor. It is still snowing on her, since weather belongs
  to the map rather than the tileset. Cycling starts at dungeon 12, Wallace.
- **The jungle is the deepest import.** Winona's has foliage walls and a dirt
  floor from the Howling Jungle sheet, that sheet's water laid as pools the
  player **reflects in** (`MB_PUDDLE` — reflective, walkable, and off the water
  encounter branch), and long grass **grafted** from vanilla and recoloured into
  its own palettes, so the blades match the foliage above them and the south
  fringe ends on soil. Its encounters still come only from the grass, exactly as
  they did before any of the art changed.
- **Steven's finale has Murky Cave**, dungeon 13 — carved pillars and ochre
  rubble over olive stone, the only imported sheet that reads as somewhere
  *built* rather than somewhere grown, and the note to end a run on. Its walls
  were the first block on any sheet that **did not fit a 4bpp palette**; see §5.
  With it, **fourteen themes stand against fourteen dungeons and nothing wraps**
  — the first arrangement in the project where every dungeon lands on the theme
  written for it.
- **Every imported tileset draws its own descent.** Lapis, the jungle and Murky
  Cave were the three sharing `gTileset_General`'s grey warp `0x0A7` — vanilla
  rock in three tilesets that contain no grey. They have drawn stairs now: a
  crack in the ice, a hollow under the roots, and a cut stairwell, all on one
  step rhythm so the exit reads the same everywhere. Cost nine tiles between
  them. Fiery Path keeps `0x0A7` deliberately, where it reads. See §5.
- The ocean is crossed **surfing** and the seafloor **diving**. Surfing is a
  property of the floor metatile; diving is a property of the **map**, which is
  why underwater is the one theme with a map of its own — see §7.
- Reachable from a new game, which is slimmed to name entry only.

Everything lives in `src/rogue_dungeon.c` / `include/rogue_dungeon.h` plus
hooks in a handful of engine files. `git log origin/master..HEAD` is the story
in order.

---

## 2. Architecture

### The two-phase generator

Object event templates are loaded **before** the map is generated, so trainer
placement cannot see the rooms unless generation is split:

- `PrepareFloor(seed)` — derives rooms, exit, trainers, encounters, grass from
  the seed. Touches no map memory, so it can run at template-load time.
- `WriteFloorBlocks()` — paints the prepared floor into `sBackupMapData`.

`RogueDungeon_PrepareNewFloor()` is the entry point for the template loader.
The generator falls back to preparing the floor itself, because that loader can
be skipped and never runs on load-from-save.

### A floor costs 2 bytes

Floors are regenerated from `VAR_ROGUE_DUNGEON_SEED`, never stored. Room
layout, exit position, trainer identities and encounter tables are all derived.
The only saved state is the seed, the floor counter, and the run state.

Generation uses a **local LCG** built on `ISO_RANDOMIZE1`, never the global RNG
— sharing it would make layouts depend on the player's step count.

### The level curve has three stages, because the stock bosses do

Bosses are stock trainers fought with their **stock parties, unscaled**. So the
encounter curve is fitted to them, not the other way round, and the convention
is that a late boss's party average lands on the curve while an early one sits a
few levels over it.

One rate cannot fit the run, and neither can two:

| stage | floors | per floor | why |
|---|---|---|---|
| gyms | 1–81 | 0.51 | eight leaders spanning levels 13 to 43 |
| Elite Four | 82–105 | 0.48 | 47 to 56, but now over 25 floors, not 50 |
| finale | 106–115 | **1.60** | Wallace 56 → Steven 76 in ten floors |

The last one is the interesting one. `TRAINER_STEVEN` is Emerald's *post-game
superboss* at levels 75–78, not a champion — Emerald's champion is Wallace at
55–58, and Steven's same six Pokémon are levels 55–58 in Ruby and Sapphire where
he holds the title. Emerald reuses that party at +20. Putting him ten floors
after Wallace therefore inherits a gap vanilla spreads across an entire post
game, and the final dungeon has to climb three times as fast as the gym stretch
to get anywhere near it. It still leaves him **+5.3 over the curve**, the widest
boss-over-curve gap in the run — deliberately, but `DUNGEON_ENCOUNTER_FINAL_NUM`
is the knob if play says otherwise.

`verify_run_structure.py` prints the whole table and reads the party levels out
of `trainers.party`, so swapping a boss for one at a different level fails the
check rather than silently bending the curve.

The floor 110 rival is invented (`TRAINER_ROGUE_RIVAL`) for the same reason in
reverse: the stock game's last rival battle is **level 32**, seventy floors out
of date. There are nine spare trainer ids before the defeat flags overflow; this
takes one.

### A species pool is a ladder, not a list

`theme->species[]` must be sorted **weakest to strongest**, because
`BuildWildEncounterTable` reads a *window* into it, not a prefix:

```
tiers  = STARTING_TIER(6) + floor / TIER_FLOORS(3), clamped to speciesCount
bottom = tiers > WINDOW(8) ? tiers - WINDOW : 0
```

A prefix would keep the weakest species in play forever — that is why Zubat was
everywhere before this. With a window they **retire** as stronger ones unlock.

Two consequences that are easy to get wrong:

- **Past floor ~30, `tiers` has clamped and only the last 8 entries ever
  appear.** A pool for a deep-only theme should be 8 or fewer, or the extra
  entries are species that can never spawn. The Elite Four's four pools are 6–8
  for exactly this reason, while the gym pools are 16.
- **Slots are dealt round-robin from the window, not drawn per slot.** Encounter
  slot weights are steeply uneven (20/20/10/10/…), so independent draws let one
  species take both 20% slots and own the floor. A pool size that divides
  `NUM_LAND_MONS_ENCOUNTER_SLOTS` (12) spreads perfectly flat — Drake's six do.

**Check a species' actual typing before putting it in a themed pool.** Seadra is
pure Water and only becomes Water/Dragon as Kingdra; Swablu is Normal/Flying
until it evolves; Trapinch is pure Ground. All three read as dragons and none of
them are. `src/data/pokemon/species_info/` is the answer, not memory.

### Themes

`struct RogueDungeonTheme` owns everything that varies between dungeons: layout
donor, generator kind, metatile tables, species pool. **Adding a theme should be
a table entry, not a generator edit.**

Two generators exist:
- `DUNGEON_GEN_CAVE` — 1×1 carve, then a nine-case wall autotile, then three
  cosmetic passes: floor patches, wall skirts, block decor
- `DUNGEON_GEN_WOODS` — 2×2 stamps on a half-resolution 24×24 grid, no
  autotiling at all, and none of the cosmetic passes

---

## 3. Engine integration — the traps

### Trainer scripts: the engine assumes inline battle data in THREE places

This cost four commits. A generated trainer picks its opponent at runtime, so
its script has no inline `trainerbattle` data. The engine parses trainer scripts
as battle data in three separate places, and **the frontier facilities have an
explicit branch at all three** — that was the map the whole time.

| site | what it reads | our branch |
|---|---|---|
| `ConfigureAndSetUpOneTrainerBattle` | script bytes as `TrainerBattleParameter` | `RogueDungeon_IsGeneratedTrainer()` |
| `GetTrainerFlagFromScriptPointer` (sight check) | opponent id from script | `RogueDungeon_HasTrainerBeenBeaten()` |
| double-battle mode check in `trainer_see.c` | battle mode from script | skipped for generated trainers |

**If you add anything else that picks an opponent at runtime, grep for the
facility branches first.**

### Two different post-battle pointers

- Winning leaves via `gotobeatenscript` → `TRAINER_BATTLE_PARAM.battleScriptRetAddrA`
- Talking to an already-beaten trainer leaves via `gotopostbattlescript` →
  `sTrainerBattleEndScript`

Leaving `battleScriptRetAddrA` NULL makes the engine fall through to
`EventScript_TryGetTrainerScript`, which loops straight back into
`gotobeatenscript` — an infinite script loop that never releases player control.
Set **both**.

Both pointers are ours, and both point at the same script. So **a boss's
post-battle script runs again on every later conversation** — that is how the
engine says "nothing to fight here". Anything one-shot in it (a reward, an item,
a Pokemon) must sit behind a flag, or the player can farm it by talking. Anything
that opens the way out must sit *outside* that flag, or a reload strands them.

### `specialvar` reads the return value, not `gSpecialVar_Result`

`ScrCmd_specialvar` is `*ptr = gSpecials[index]();`. `data/specials.inc` is
assembly, so there is no prototype and **nothing warns**. A `void` special used
with `specialvar` hands the script whatever is left in `r0`, which after the
epilogue is the return address — never 0, never 1, so every `goto_if_eq` against
`TRUE`/`FALSE` silently takes the wrong branch.

This cost a softlock: `RogueDungeon_IsDungeonEndFloor` wrote `gSpecialVar_Result`
and returned `void`, so the rest-stop warp was never taken, and since a gym floor
deliberately draws no stairs the player was sealed in the arena.

**Every `specialvar` target must return `u16`.** Writing `gSpecialVar_Result`
instead compiles, links, and runs — it just answers wrong. `special` (no var) is
unaffected; those may stay `void`.

### A per-theme value hard-coded is the recurring bug in this project

It has now happened three times, and the shape is always the same: a constant
that was right for every theme that existed when it was written.

| where | what was hard-coded | what it cost |
|---|---|---|
| `CarveFloor` | `DUNGEON_METATILE_FLOOR` | Fiery Path had ten floors with **no wild Pokémon** |
| object event templates | elevation `3` | the player walked through every trainer on a water floor |
| `RogueDungeon_OnBossDefeated` | `DUNGEON_ELEVATION_FLOOR` | **floor 65 stranded the run** |

The third: that function draws the exit once an arena boss falls, and an arena
floor has no other way out by design. It wrote elevation 3 while the ocean walks
at `DUNGEON_ELEVATION_WATER`, and `IsElevationMismatchAt` blocks a move between
two *different* non-zero elevations — so the whirlpool appeared where the player
could not step, and re-entry redrew it just as wrong. Both generation paths
already read `theme->elevationFloor`; only this one did not.

**`check_encounter_flags.py` lints metatiles inside functions but not
elevations.** Anything else per-theme is unguarded — grep for the constant, not
just the field.

### Stock trainer defeat flags are permanent

We reuse the stock game's 709 trainers every floor, and their defeat flags were
designed for a game where you fight each trainer once ever. Clear them **once
per floor at template-load time**. Clearing at battle setup does not work: the
battle script checks the flag immediately after running that special, so the
trainer never registers as beaten and rematches forever.

Also keep trainer ids **distinct within a floor** — the flag is derived from the
id, so two slots sharing one leaves a trainer that refuses to battle.

### Tilesets can be swapped at runtime

`gMapHeader` is EWRAM (a RAM copy) and `CopyMapTilesetsToVram` reads
`gMapHeader.mapLayout`. Our generator runs at state 0 of `LoadMapFromWarp` while
`InitMapView()` uploads tilesets later, so:

```c
gMapHeader.mapLayout = GetMapLayout(theme->layoutId);
```

**Never patch `mapLayoutId`** — every dispatch in the project keys on it. The
themed layout is a tileset donor only; no map points at it.

### Wild encounters key on the MAP, and every map must be registered

The corollary to the swap above, and it cost **five themes their wild Pokémon**
— eight of the thirteen worked, so it read as a per-theme art bug for a long
time. `StandardWildEncounter` runs two gates before the roguelike is consulted
at all, and each one fails silently:

1. `GetCurrentMapWildMonHeaderId()` matches `gSaveBlock1Ptr->location` against
   `gWildMonHeaders` — the **real map**, not the swapped layout — and returns
   `HEADER_NONE` if it is absent. Only `MAP_ROGUE_DUNGEON_FLOOR` was ever listed
   in `wild_encounters.json`, so the four maps that exist purely to carry a
   weather setting had no encounters from the day they were added. Snow, fog and
   petals each broke their theme in the same commit that gave it its weather.
2. It then picks the land or the water branch from the behaviour under the
   player and returns if **that branch's** table is `NULL`. The ocean paints
   `MB_OCEAN_WATER`, took the water branch, and found a land-only entry.

So a theme needs an entry for its own `mapId`, carrying the table its own
encounter surface reads. The branch is decided by two flag bits and nothing
else:

| | `TILE_FLAG_HAS_ENCOUNTERS` | `TILE_FLAG_SURFABLE` | branch |
|---|---|---|---|
| `MB_CAVE`, `MB_TALL_GRASS`, `MB_LONG_GRASS`, `MB_INDOOR_ENCOUNTER` | yes | no | land, 12 slots |
| `MB_OCEAN_WATER`, `MB_SEAWEED_NO_SURFACING` | yes | yes | water, **5 slots** |

Use `TILE_FLAG_SURFABLE`, **not**
`MetatileBehavior_IsSurfableFishableWater` — the two disagree, and the
disagreement is exactly the underwater seaweed, which is surfable-flagged but
not in the fishable list. `IsWaterWildEncounter` reads the flag, so the seafloor
is a water area even though the player is walking on it. Vanilla agrees: every
`MAP_UNDERWATER_*` registers `water_mons`.

Two consequences worth carrying:

- **`encounter_rate` is live even when the table is a placeholder.** The rate is
  read off the static JSON entry before the hook is called; only the species and
  levels are substituted. Ours is 10 on every dungeon map.
- **The slot counts differ, so the ladder window has to follow.**
  `BuildWildEncounterTable` deals the window round-robin across the slots, and an
  8-wide window across 5 water slots would leave three species in the ladder that
  no floor could roll. The window is capped to the slot count instead.

Placeholder tables are deliberately **Zubat, including in the water entries** —
if the hook ever stops substituting, a Zubat while surfing is unmistakable,
where a placeholder Tentacool would let the same failure pass for real.

`check_encounter_flags.py` now derives each theme's branch from its own surface
and proves the declaration, the registration and the table length all agree.

### Metatile ids are tileset-pair specific

`0x214` is a ladder under `gTileset_Cave` and a grey stripe under
`gTileset_Rustboro`. **Nothing with a metatile id can be shared between themes.**
This shipped as a visible bug (grey stairs in the woods).

It shipped a second time, worse, and how it hid is the useful part.
`CarveFloor` named `DUNGEON_METATILE_FLOOR` directly, so every cave-generator
theme painted its floor with the *cave's* id. `theme->floor` existed from the
woods commit onward but was only ever read on the woods path, so the three
later cave themes each added a `.floor` the generator never looked at. Under
`gTileset_BikeShop` that id is a counter fragment; under `gTileset_Lavaridge`
it is a green bush whose behaviour is `MB_NORMAL`, which carries no encounter
flag — **Fiery Path had ten floors with no wild Pokémon at all**, and its
ember-sparkle decor could never match, because `ApplyDecor` keys on the
*painted* metatile and the painted one was never `0x308`.

`theme_mock.py` could not show any of it: it fills the grid from
`theme['floor']`, so it rendered the table's intent while the game rendered
something else. **The mock validates a table, not the code that reads it.**

The guard is now in `check_encounter_flags.py`, and it is a source lint rather
than a simulation: **no `*_METATILE_*` constant may appear inside a function.**
A function runs under every theme's tileset, so only a table may name a
metatile. It caught four more latent instances in the woods stamp path the same
day, now the `STAMP_BASE_L/R` and `longGrassBaseL/R` table entries.

### Object events

- The engine reads templates for the current map from the **save block**, but
  takes the **count** from ROM. So `map.json` must declare
  `DUNGEON_MAX_TRAINERS` placeholders.
- An object event with a **set** `flagId` does not spawn. That is how unused
  placeholder slots are hidden.
- **A defeated trainer object still blocks movement.** A boss standing in a
  doorway would wall the exit off permanently — which is why arenas have no exit
  at all until the boss falls, rather than a guarded chokepoint.

### Map data plumbing (adding a map)

1. `data/layouts/<Name>/{map.bin,border.bin}` + entry in `layouts.json`
2. `data/maps/<Name>/map.json` + entry in `map_groups.json`
3. `data/maps/<Name>/scripts.inc` with `<Name>_MapScripts::` / `.byte 0`
4. **An `.include` for it in `data/event_scripts.s`**
5. Two dispatch branches in `src/overworld.c` (map-load *and* load-from-save)

**Trap:** `data/event_scripts.s` has an `.if IS_FRLG` block around roughly lines
603–1051. Appending an include after the *last map in the file* puts it inside
that block, where it is silently never assembled for an Emerald build. The
symptom is an unchanged `undefined reference to <Name>_MapScripts` even though
the include is visibly present.

---

## 4. Map format

`map.bin` is a flat little-endian `u16` array, row-major, `width * height`.
Bits 0-9 metatile, 10-11 collision, 12-15 elevation. No header, no compression.

Validated by round-tripping all 785 vanilla layouts: 765 byte-identical, 20 with
exactly one extra trailing block (19 named `LAYOUT_UNUSED_*`) — a benign vanilla
quirk, the game reads `width*height` and ignores the tail.

- **Collision is binary in practice.** Only 0 and 1 appear anywhere in vanilla.
- **Elevation**: 0 = transition/any, 3 = standard ground, 1 = water, 4+ raised,
  15 multi-level. Caves and woods use only `{0, 3}`.
- **Size ceiling**: `(w+15)*(h+14) <= MAX_MAP_DATA_SIZE (10240)`. All 785 vanilla
  layouts satisfy it; the largest reaches 91.8%.
- **`sBackupMapData` already exists in EWRAM**, so generating into it costs no
  extra memory.

### Cave wall autotiling (`gTileset_General` + `gTileset_Cave`)

Resolve in this order — the south face wins because it is most visible:

| case | left | middle | right |
|---|---|---|---|
| 1-thick horizontal | — | `0x39F` | — |
| floor **south** (visible face) | `0x218` | `0x219` | `0x21A` |
| floor **north** (room bottom) | `0x220` | `0x209` | `0x222` |
| 1-thick vertical | — | `0x39E` | — |
| vertical edges | `0x210` | `0x211` | `0x212` |
| outer corners (diagonal open) | `0x21B` SE | `0x21C` SW | `0x223` NW/NE |
| interior | — | `0x211` | — |

Floor is `0x201` (`MB_CAVE`, so encounters work). `0x39E`–`0x3A4` are **ours** —
see §5.

**Do not omit the north edge.** Vanilla uses `0x211` for both floor and wall
bulk, so a missing boundary is *invisible*, not merely plain, and rooms stop
reading as enclosed. Legibility in vanilla comes entirely from a continuous
outline, never from floor/rock contrast.

### Woods (`gTileset_General` + `gTileset_Rustboro`)

Trees are **2 wide × 3 tall** blocks on even coordinates (95% x-aligned, 99%
y-aligned in vanilla). Rows: `0x1D4/0x1D5` canopy, `0x1DC/0x1DD` trunk,
`0x1E4/0x1E5` ground contact — the third row used **only where a mass ends**.
Vanilla never leaves `0x1DC` exposed; doing so dangles the trunk in mid air.

Grass: `0x001` plain (no encounters) and `0x00D` tall. **No long grass** —
vanilla Petalburg Woods has none, it being the Route 119 kind.

It briefly had some, ended with `0x016/0x017`, and that was wrong twice over.
Those two are **solid in all 691 of their vanilla placements** across every
General+Rustboro and General+Fortree layout, always under the leafy canopy
`0x0C6/0x0C7` with plain grass below: they are a canopy base row, so drawing
them passable put a walkable hedge fragment under every long grass patch. And
there is no correct replacement, because **`gTileset_General` contains no
`MB_LONG_GRASS_SOUTH_EDGE` metatile at all** — the only one, `0x208`, is in
`gTileset_Fortree`. Long grass therefore belongs to the jungle.

The reasoning that produced the bug is the thing to remember: `0x016/0x017` sit
immediately after `0x015` in the metatile grid. **Adjacency in the grid is not
evidence.** It is the same mistake that nearly turned the tree crown into a
grass base row.

A tree also has a **fourth row above it**. Vanilla draws the crown poking up
into the block above the canopy, composited over whatever is already there:
`0x1CE/0x1CF` over plain grass, `0x1C6/0x1C7` over tall grass. Across all nine
General+Rustboro layouts, of the ~215 blocks sitting directly above a canopy
that are not part of a tree themselves, 201 are these four. Long grass has no
variant and is left alone.

Behaviour is preserved exactly — `0x1CE/0x1CF` are `MB_NORMAL` like plain grass,
`0x1C6/0x1C7` are `MB_TALL_GRASS` like tall grass — so the swap cannot move
where encounters fire.

**This was nearly mis-implemented as a grass base row.** On a contact sheet
`0x1C6`/`0x1CE` read as grass with a sprout at the bottom, which looks exactly
like the bottom edge of a grass patch. The census said otherwise: they sit above
`0x1D4/0x1D5` 139 times and the "sprout" is the top of the tree. **Ask what a
candidate metatile sits next to before deciding what it is** — the picture alone
supported the wrong answer.

**Grass-only encounters needed no code.** `MB_NORMAL` carries no encounter flag;
`MB_TALL_GRASS` and `MB_LONG_GRASS` do. Choosing the metatiles was the whole
implementation. Caves keep `MB_CAVE` and encounter everywhere.

### Statistical mining does not recover autotile rules

Deriving a neighbour-bitmask → metatile table across 47 cave layouts gave a top
choice that wins only **26.5%** of the time (30.8% with diagonals). Vanilla
varies wall art decoratively, so there is no rule to recover by counting.

**Read real examples out of a vanilla layout instead**, then validate by
rendering generated output beside vanilla. That worked first time for both cave
walls and trees.

The ocean is the sharpest case of this. `derive_wall_table.py` over four
Mossdeep sea routes put **nothing above 37.8%**, because the census pools three
unrelated things — island shores, the map-edge water barrier, and the rocks.
Dumping isolated solid components as a grid showed a clean 3×3 nine slice on
sight:

```
338 339 33A
340 341 342
348 349 34A
```

Checking each id **alone** then confirms it: `0x339` faces north 79% of the
time, `0x349` south 80%, `0x342` east 76%, and no two share a dominant mask.
The corners sit lower (41–53%) only because they also appear in tighter two-wide
rocks. So the same data that looked like noise is decisive once it is asked the
right question — **the failure was the pooling, not the tileset.**

---

## 5. Composing metatiles without pixel art

A metatile is only **8 references to existing 8×8 tiles** (4 bottom layer, 4
top; each `u16` is tile bits 0-9, xflip 10, yflip 11, palette 12-15). New
metatiles can therefore be *spliced* from halves of existing ones.

We appended `0x39E`–`0x3A4` to `gTileset_Cave` for one-block-thick walls, which
vanilla has no art for (its cave walls are always 2+ thick). The vertical sliver
is the west half of `0x210` joined to the east half of `0x212`; the vertical top
cap keeps that base and overlays both north corners, exploiting the fact that
`0x220`/`0x222` are top-layer overlays rather than standalone art.

Scripts: `compose_metatiles.py` + `append_metatiles.py` (idempotent — it trims a
previous append before rewriting).

`gTileset_EverGrande` has the same rule and its own owner,
`make_evergrande_tiles.py`. There is now one appender per edited tileset, by
construction rather than by convention.

**`append_metatiles.py` is the ONLY thing that may append to the cave tileset**,
by construction rather than by convention. It stays idempotent by truncating
everything past the vanilla 414 and rewriting the tail, so a second script
appending to the same file gets silently wiped the next time this one runs.
Anything needing a new cave metatile exports its entries and is imported there —
`make_glacia_snow.py` does. Two further details it got wrong first:

- **Compare the tail's CONTENT, not its length.** It decided it had already run
  by counting metatiles, so a *changed* entry re-ran to "nothing to do" and the
  fix never landed.
- **Attributes are per-entry.** The slivers copy `0x211` and must behave like
  wall; the snow copies `0x201` and must keep `MB_CAVE`, or Glacia's floors
  spawn nothing. One shared attribute is the same shape of bug as a hard-coded
  per-theme constant.

**This edits vanilla asset files** (`data/tilesets/secondary/cave/*.bin`), so
pulling upstream changes to that tileset needs care: take upstream's file, re-run
the script.

**Limits:** splicing only works when the source tiles have transparency. The
cave-mouth metatile is opaque across the full 16×16, so the woods stairs needed
genuinely new art.

### Drawing a FLOOR: fine noise, never a feature

Glacia's snowfield (`make_glacia_snow.py`) is three pieces — flat snow as the
`.floor`, and a 2-wide drift and an ice rock as `decor`. The decor was easy.
The floor took two attempts, and the reason is worth keeping.

**A floor metatile repeats every 16 pixels, so any interior feature becomes a
lattice.** The first version had a soft diagonal swell and four white glints;
tiled, the swell became continuous candy-stripes across the whole floor and the
glints a regular dot grid. A reference image is a hand-drawn field and never has
to tile — this does, everywhere, against copies of itself.

Vanilla's answer, read off cave `0x201` rather than guessed:

| index | share |
|---|---|
| 4 | 55% |
| 5 | 34% |
| 6 | 9% |
| 3 | 1.5% |

**Fine per-pixel noise over four *adjacent* indices, never the extremes of the
ramp.** Noise has no structure to repeat, and neighbouring indices are too close
in value to read as a pattern where it does. `0x211` is flatter still — 82% one
index. Reuse the proportions and slide them along the ramp to whatever the
theme needs; snow is the same curve shifted to the pale end.

**No white in a floor.** High contrast is exactly what makes the 16-pixel period
visible. Glints belong in decor, which is position-hashed and so irregular.

Decor is also the escape from the primary-palette limit: the cave's own decor is
half `gTileset_General`'s palettes and cannot be recoloured, but *new* art drawn
in palette 6 recolours perfectly. That is why Glacia has decor and the other
three Victory Road themes have none.

### Borrowed wall art has the donor's FLOOR baked into it

**A custom floor under a borrowed wall set does not just look different — it
looks broken.** The cave's wall pieces draw the base of the cliff as *ground*
inside the wall's own metatile, so it blends into `0x201` below. Put snow under
it and that band stays cave-coloured: every room reads as a grey rectangle with
a strip of bare rock along the bottom.

This could not arise before Glacia. Every earlier theme either kept the donor's
own floor (Mirage Tower walks on `0x201`) or took its whole wall table from the
same tileset as its floor (Fiery Path) — the two always matched by
construction. **Any theme that mixes a custom floor with a borrowed wall set
needs this treatment.**

#### The same trap, in region-autotile form

The jungle hit this again from the other direction, and it is worth stating as
the general rule: **an autotile's EDGES encode what the art expects to sit in,
and changing what it sits in invalidates every one of them while leaving every
id perfectly valid.**

Its puddles are `0x0C8`–`0x0DA`, a 3×3 region autotile in the *primary* tileset.
When the jungle's secondary was replaced, those ids survived untouched and
resolved exactly as before — nothing in the build could notice. But **eight of
the nine pieces are a shore drawn against green route grass**; only the centre
is purely water. Painted on the new brown dirt, every puddle came out ringed in
a pale mint halo that reads as a rendering fault rather than as a bank. The
layer was deleted rather than carried.

Two things to take from it:

- **Primary ids surviving a secondary swap is a hazard, not a comfort.** They
  are the ones that keep working and stop *fitting*. The ids that break loudly —
  the two `gTileset_Fortree` ones — were the easy half of that change.
- **Only the mock could see it.** `check_encounter_flags.py` passes, the build
  passes, the behaviours are right. Render the theme before believing an art
  swap is done.

The fix is five reshaded metatiles and twelve tiles. Which pixels to repaint is
not a judgement call:

1. **Seed from the edges that actually have floor**, taken from the `if/else`
   chain in `PaintWalls` — not inferred from the art. `WALL_FACE_LEFT` is
   `openSouth && openWest`, so it has ground on two edges; `WALL_FACE_MID` on
   one.
2. **Flood-fill through the floor's own tonal band**, stopping at the dark
   outline. For the cave that is `{3,4,5}` — **not** 6, which is the floor's
   third-commonest tone *and* the wall's dark edge, so letting the fill through
   it leaks straight into the rock.
3. **Replace with the floor's pixels at the same coordinates**, so the
   repainted band lines up with the noise of the blocks beside it.

Two slots look like they need it and do not:

- **The north row.** Its light top band is index 3 — *lighter* than the floor's
  dominant 4. That is the wall's own lit top edge, not ground. A cliff face has
  a visible base; a cliff top does not.
- **The corners.** No cardinal neighbour of a corner is floor, only a diagonal,
  so none of them may show any. Seed them anyway and the fill claims 23% of
  `WALL_CORNER_OPEN_SW`, all of it leak — cave wall interior and cave floor
  share a tonal band, which is the same fact that makes a missing boundary
  there *invisible* rather than merely plain (§4).

**Only the quadrants the repaint touches get a new tile.** The rest reference
the original entry verbatim, flips and all. Check the source metatiles are
bottom-layer only before flattening anything — all five here are, but flattening
one that used a top layer would change whether it draws over the player. Each
new metatile copies the attribute of the piece it replaces.

### An encounter surface can cost sixteen metatiles and zero pixels

**Neither `gTileset_EverGrande` nor `gTileset_Mauville` contains a single
metatile carrying `TILE_FLAG_HAS_ENCOUNTERS`.** Every flower in both is
`MB_NORMAL`. So flowers standing in for tall grass needed new entries — but not
new art: `0x2A9`–`0x2B8` reference vanilla's flower tiles byte for byte and
differ only in the attribute, which is `MB_LONG_GRASS`.

**Attributes are per entry, and that is the whole trick.** The same pixels can
be decoration in one metatile and an encounter surface in another.

Which behaviour to hang on it is a real choice, and worth knowing the options:

| behaviour | encounters | what else it drags in |
|---|---|---|
| `MB_UNUSED_05` | yes | **nothing** — its only reference in the engine is `Unref_MetatileBehavior_IsUnused05`, which nothing calls |
| `MB_TALL_GRASS` | yes | the rustle overlay sprite |
| `MB_LONG_GRASS` | yes | a full-tile curtain sprite, the OAM clip that hides the player's lower half, and `BATTLE_ENVIRONMENT_LONG_GRASS` |

### Two encounter surfaces, and the behaviour has to match the ART'S HEIGHT

The flower dungeon has **two**, and getting there was a mistake worth recording.

The beds went on `MB_LONG_GRASS` first, for a good-sounding reason: the OAM clip
that behaviour brings (`SetObjectEventSpriteOamTableForLongGrass` swaps the
subsprite table) is purely geometric, so the player wades *into* the surface,
and wading into a flower field sounded exactly right.

Rendered in situ it was obviously wrong. **Ever Grande's beds are bold and
LOW** — half-tile blooms with hard dark outlines, a flower bed seen from above —
and the overlay is a full-height curtain that hides the player's lower half. The
two disagreed about how tall the thing underfoot was. Vanilla never has that
problem because its long grass *tile* is tall, so tile and overlay agree by
construction.

The answer was not to pick a different behaviour but to **add the surface the
overlay was describing**:

| surface | behaviour | what stepping on it does |
|---|---|---|
| short flower beds, `0x2A9`–`0x2B8` | `MB_UNUSED_05` | encounters, nothing else — no overlay, no clip |
| flowery long grass, `0x2B9`–`0x2C0` | `MB_LONG_GRASS` | encounters, the curtain, the clip |

The tall one is `gTileset_General`'s own long grass `0x015` under a bloom
overlay, keeping `0x015`'s attribute. Five new tiles, because each blossom is
placed inside a single quadrant and the other three top-layer quadrants are the
entry `0x0000`, which references no tile at all.

**Three of its eight are bare.** A feature baked into a metatile repeats every
16 pixels and becomes a lattice — §5 says that about floors and it is just as
true of a patch layer. Gaps in the cycle are what make the blossoms punctuate
the grass instead of ruling it.

**The general rule: pick the behaviour to match how tall the art is, not how
much you want the effect.** `MB_UNUSED_05` exists for exactly the case where a
surface should spawn encounters and otherwise be walked over.

Only the overlay sprite is grass-specific, and that is replaced per theme by the
section below.

### Giving one field effect a per-theme graphic

The flower dungeon needed its own wade-through curtain, and doing it turns up a
structure worth knowing: **a field effect's palette is chosen in neither of the
two places you would look.**

The template has a `paletteTag`, but nothing loads from it. The palette comes
from `data/field_effect_scripts.s` —
`field_eff_loadfadedpal_callnative gSpritePalette_GeneralFieldEffect1,
FldEff_LongGrass` — and a script is fixed per `FLDEFF` id, while a
`SpriteTemplate` is `const`. So per-theme art has to be chosen in the `FldEff_`
function itself. Four things it has to get right:

- **Vary `FLDEFFOBJ`, never `FLDEFF`.** The effect id stays `FLDEFF_LONG_GRASS`,
  because every ground-effect flag, the OAM clip, and
  `UpdateLongGrassFieldEffect`'s own `FieldEffectStop` key on it. Only the
  graphic differs, so the two templates share an anim table and a callback.
- **Load the palette *before* creating the sprite.** `LoadSpritePalette` returns
  `0xFF` when no slot is free, and `0xFF` truncates to slot **15** in a four-bit
  OAM field — so a late failure draws the art in whatever lives there. Loading
  first lets it fall back to the stock graphic instead: a wrong graphic beats a
  wrong graphic in wrong colours.
- **Call `UpdateSpritePaletteWithWeather` by hand.** `loadfadedpal` is the
  `faded` half of that script command, and stepping around it means the sprite
  ignores the weather fade every other field effect obeys — visible immediately
  on the fog and snow maps.
- **Derive the art from the one it replaces.** `make_flower_fldeff.py` reads
  `long_grass.png` and remaps its indices rather than drawing a mass from
  scratch. Two attempts at generating one failed, and the second is the
  instructive one: **noise with the right histogram still looks wrong**, because
  vanilla's light pixels are not scattered — they form blades, short diagonal
  strokes. Reusing them also inherits two properties that are hard to hit by eye
  — the mass is opaque enough to read as something you are standing *in*, and it
  **tiles invisibly**, which matters because one sprite spawns per occupied tile
  and several sit edge to edge whenever the player walks a run of them.

The greens do not move at all: they stay `general_1`'s ramp, which is what
`0x015` is drawn in, and `0x015` is what the overlay covers. The blooms are pink
and orange only, because that is the colourway the theme paints — a blossom in
the overlay that appears nowhere on the floor is a flower the player is standing
in that does not exist.

**They were briefly retargeted to the flower beds' ramp, and that was a bug of
inattention rather than of reasoning.** "Made of what it sits in" was the right
rule; it stopped being applied when the surface underneath changed from the beds
to the tall grass. **A rule about matching something needs re-checking whenever
the something moves.**

### When splicing is not enough: drawing new tiles

`make_woods_stairs.py` draws the woods stairs (`0x35E`). The parts worth reusing:

- **Free slots have to be measured, not assumed.** A tile slot is free only if
  no metatile in the tileset references it *and* its pixels are blank. For
  Rustboro that is 220 of 512 tile slots, plus 161 metatile slots after this
  one. `gTileset_General` is full at 512/512, so new art goes in a secondary.
- **…and then RECORDED, because writing the art invalidates the measurement.**
  The snow script re-measured on every call, so once `--write` had filled the
  slots, `append_metatiles.py` re-ran the search, got a different set, and wrote
  metatile entries pointing at blank tiles — the floor rendered as palette index
  0. Slots are pinned in a constant now and validated against the **vanilla**
  metatile count only; validating against all of them, including our own
  appends, makes the check circular.
- **Draw inside an existing palette.** Palette 2 already carries both the mint
  greens and a full earth ramp, so the stairs needed no new palette. Check what
  the palette holds before designing — it has no true black, so `413931` is as
  dark as the opening gets.
- **Bake the background into the tile; do not use the top layer for it.** A top
  layer is drawn *over* the player under `METATILE_LAYER_TYPE_NORMAL`, which
  would hide them when they stand on it. The stairs are bottom-layer only, with
  grass composited into the rounded corners.
- **Save PNGs as 4bpp** (`img.save(path, bits=4)`). `gbagfx` converts other
  depths fine, but the asset diff stays clean.
- **Copy the attribute from the metatile it replaces.** Behaviour and layer type
  came straight from grass; the stairs trigger is by metatile id, not behaviour.

Art was iterated as text art rendered over real grass tiles and compared
side by side, then round-tripped back out of the written asset files to confirm
what shipped is what was designed.

### Art that moves: the whirlpool

Both water dungeons descend through a whirlpool — the ocean's `0x3D1`
(`make_ocean_tiles.py`) and the seafloor's `0x2EC`
(`make_underwater_tiles.py`). It is the second piece of genuinely new art in the
project and the first that animates, so it adds a few rules to the list above
rather than replacing it.

**The shape is shared and the colours are not.** Metatile ids above `0x200`
belong to whichever secondary is loaded, so one exit cannot serve two tilesets
and the metatile exists twice. The spiral lives once, in `whirlpool_art.py`, as
a function of radius and angle — the exit is the one thing on a floor the player
is hunting for, and two dungeons whose exits looked subtly different would teach
them two things instead of one. But each caller passes its own **palette roles**,
because a vortex reads as water only when it is made of the water around it: the
sea's primary palette 4 over the ocean, `gTileset_Underwater`'s palette B over
the seafloor. The bottom layer differs for the same reason — animated open water
in one, the seafloor itself in the other, because the player is standing *on*
something there and outside the disc it has to still be the ground.

That split is worth copying for anything reused across tilesets: **share the
geometry, never the palette.**

- **Draw it in the palette of the thing it sits in.** The spiral is primary
  palette 4 — the sea's own — which carries whites, a foam pale and a full blue
  ramp. Using the water's palette is what makes the vortex read as the same body
  of water rather than a decal pasted on top.
- **Here the top layer is right, where the stairs wanted the bottom.** Index 0 in
  a top layer is transparent, so outside the disc the animated sea shows through
  and the whirlpool sits *in* the water. The attribute is `COVERED`, so both
  layers draw below the player and they surf over it.
- **A secondary tile may use a primary palette and vice versa.** The metatile's
  palette field indexes all sixteen loaded palettes. Vanilla already does the
  reverse here: the sea rock's top layer is *primary* tiles under a *Mossdeep*
  palette. The consequence is that the Mossdeep sheet looks wrong opened in an
  editor, which is normal and not a bug.
- **Generate the art, do not hand-place it.** Four frames of 16×16 as text art
  would be unreasonable; the spiral is a function of radius and angle, so the
  frames are parametric and the shape is tunable by constant.
- **Two-fold symmetry makes four frames a seamless loop.** Two arms rotating a
  quarter turn per frame closes exactly. Three arms at the same step would not.

Wiring the animation up, in `src/tileset_anims.c`:

- Mossdeep's secondary animation callback was `NULL`, so the slot was free.
  `InitTilesetAnim_Mossdeep` now points at `TilesetAnim_Mossdeep`.
  `TilesetAnim_Underwater` already existed for the seaweed, so the seafloor's
  whirlpool is one more queue in it rather than a new callback — but on
  **remainder 1** of the 8-tick stride, because the seaweed's `% 16 == 0` is a
  subset of `% 8 == 0` and sharing that tick would queue two DMAs on the same
  frame every other turn of the vortex.
- **The tiles must be consecutive.** `AppendTilesetAnimToBuffer` DMAs the frame
  to VRAM in one run, so the four slots are one contiguous block
  (`NUM_TILES_IN_PRIMARY + 0xE9`) and must not wrap the sheet row.
- A 16×16 frame PNG converts to four tiles in TL, TR, BL, BR order, which is the
  order the metatile references them in. `INCGFX_U16(...".4bpp")` does the
  conversion at build time; no `.4bpp` is checked in.
- The counter maxes at **256**, so a frame stride has to divide it or the loop
  stutters on wrap. 8 ticks per frame gives a turn every half second.
- The sheet still needs a copy of frame 0. The animation overwrites it within a
  few frames of the map loading, but the map has to render before that.

### Drawing the descent, three times, out of what was already there

Lapis, the jungle and Murky Cave all descended through `gTileset_General`'s warp
`0x0A7`. It is a **primary** id, so it survives any secondary — which is exactly
why it got reached for three times — and it is vanilla grey rock in three
tilesets that do not contain a grey. `import_tile_sheet.py` draws all three now,
from a `stairs` block that costs no pixel art and no sheet.

**The exit is not decoration, so it does not get a decoration's rules.** It is
one of two things on a floor the player is actively hunting for. That is the
argument for the whirlpool's split — *share the geometry, never the palette* —
and it applies here unchanged: one skeleton and one step rhythm across all
three, so the player learns the shape once.

**The step rhythm is the woods stairs', copied row for row**, because that is
the one drawn descent in this project that already reads. Why it reads:

- **The riser IS the void**, not a darker shade of the tread. What separates two
  steps is darkness. The first attempt here used a mid-tone riser and produced a
  framed rectangle lying on the floor — recognisably a *thing*, not a hole.
- **Each tread sits inside the void with a two-pixel margin**, so the well has
  depth at its edges instead of butting against its own rim.
- **A step is (void, tread, tread, lit edge), in that order**, so the run ends on
  a lit edge — the tread nearest the player — and the dark always sits above the
  thing it shadows.
- **Two rim rows**: an outer that meets the floor, a darker inner that turns the
  corner into the well.

**What varies is the framing, and it carries the theme.** Lapis and the jungle
round their four corners off to the floor, the way the woods stairs seat into
turf. **Murky Cave does not** — it runs square to the tile edge with no floor
showing anywhere, because it is the one tileset that reads as somewhere *built*,
and a stairwell in a built place was made rather than opened. That is the whole
distinction between the three, and it is one character in the generator.

#### Darkness is not only a luminance

The jungle is the entry worth keeping, because two palettes were tried and both
failed for *different* reasons:

- **Dirt (slot 7), the floor's own**, runs 70 to 169 luminance and has no dark at
  all. The void came out **lighter than the floor around it** and the stairs read
  as a plate lying on the ground.
- **Foliage (slot 6)** reaches `#006300` at luminance 58 — as dark as the woods'
  own void, on paper. It reads as **paint**, because it is a saturated green. A
  colour dark enough to be shadow can still be too saturated to *be* shadow.
- **Long grass (slot 9)** is what works. It is vanilla's palette 2 with two ramps
  swapped out by the graft, so it still carries `#413931` — the very colour the
  woods stairs use — and vanilla's tan ramp above it. The jungle's descent is
  therefore the woods' descent in the woods' tones, which is the right echo:
  both are holes dug in forest floor.

So **a theme's stairs are not obliged to use its floor's palette**, and the
corner pixels are what usually forces the question. The answer is one line: the
floor's plain fill is run back through `to_indices` against whatever palette the
stairs are drawn in. It is a no-op where the two agree, and for the jungle it
costs two near-duplicate substitutions across the twelve corner pixels.

#### A palette with headroom can be extended, from its own tileset

Lapis has no dark either — snow runs 157 to 240 — and unlike the jungle it had no
third palette to move to. Its slot 7 had **three free entries**, so the `stairs`
block appends into them, and the two colours it appends are lifted **verbatim out
of palette 6**, the crystal walls. Nothing is invented: the stairwell is lit by
the same rock it cuts into.

**Appending is safe because it only ever adds to the end.** Colours already in
the palette keep their indices, so every block already quantised against that
slot is untouched — verified by diffing the written files against the previous
commit, where Lapis' `07.pal` gains exactly two entries and loses none, and both
other tilesets' palettes come out byte-identical.

Two rules came out of building it:

- **Bind roles to colours, not to indices, and fail loudly.** Every role names an
  RGB that must already be in the palette. An index means a different colour in
  every tileset, and a nearest-match would silently bind the exit to whatever
  happened to be closest.
- **Blocks that draw rather than read go LAST in the block list**, so their
  metatile lands after every id a theme table already names. The jungle's must
  also come after its long-grass graft, since it draws in the palette that graft
  writes. Ordering here is the same hazard `varies` has: appending is safe,
  reordering silently renumbers.

Cost: **three metatiles and nine tiles total** — four for Lapis, three for the
jungle, two for Murky Cave, since each stairs metatile is two unique quadrants
and their x-mirrors.

### The cheapest art there is: a palette-only tileset

A whole theme can cost one palette directory. `gTileset_RogueVictoryRoad*` share
`gTilesetTiles_Cave`, `gMetatiles_Cave` **and** `gMetatileAttributes_Cave`, and
differ only in `.palettes`.

Vanilla does this: `gMetatiles_SecretBaseSecondary` is shared by six tilesets,
and `gTileset_MirageTower` is 411 of 414 metatiles byte-identical to
`gTileset_Cave` with a sand ramp over it.

It works here because of one measured fact: **every metatile in the cave wall
table, both stairs and all seven composed slivers draw from palette 6, and only
palette 6.** `NUM_PALS_IN_PRIMARY` is 6, so palette 6 is the first *secondary*
slot — the tileset's own. One palette file recolours everything the generator
paints.

**Check that before assuming this works elsewhere.** The same census says the
cave's sand-pool region is 45% primary palette 5 and its decor 50% primary
palette 3. Those belong to `gTileset_General` and are shared with every other
theme, so they cannot be recoloured — which is why Victory Road runs with no
patch layer and no decor rather than with mismatched ones.

Consequences worth knowing:

- **Sharing the attributes is as important as sharing the metatiles**, and
  easier to miss. Behaviours come with them, so `MB_CAVE` still carries the
  encounter flag and nothing about wild spawns changes.
- **`.callback` must be carried over too** (`InitTilesetAnim_Cave`). The
  animation DMAs tile pixels, not colours, and the tiles are shared — but `NULL`
  would silently stop the animation on those floors only.
- **This is the one place metatile ids may be shared across themes.** Everywhere
  else that is the bug §3 describes; here the ids are the same *file*, so one
  `VICTORYROAD_METATILE_*` block serves all four. Mirage Tower spells its own out
  precisely because *its* equality with the cave is a coincidence.
- **Write the palette declarations longhand, not behind a macro.**
  `tileset_resolve.py` finds asset paths by parsing them, and a pasted symbol
  name is invisible to it — the atlas tooling would silently lose the tileset.
  The theme table entries want the same treatment:
  `check_encounter_flags.py` parses `.layoutId = <token>` literally.
- **Both `graphics.h` and `headers.h` end inside an FRLG branch** — and in
  `headers.h` it is an `#else`, not a trailing `#endif`, so the Emerald block
  ends around line 836. Appending at the end of either file is silently dropped
  from an Emerald build. Same trap as `data/event_scripts.s`.

`make_victory_road_palettes.py --preview` renders every tone on real cave art
at 2× and 5×; the lightness band in each tone is a linear remap rather than a
gamma, because its width *is* the contrast budget and the wall outline is the
only thing separating a wall from the floor it meets.

### Importing a whole tileset from a sheet, legend and all

Lapis Cave (`gTileset_RogueLapisCave`, Glacia's) is the first theme whose
tileset is neither vanilla nor a recolour of one. `import_tile_sheet.py` builds
it — tiles, metatiles, attributes and palettes — from a Mystery Dungeon: Red
Rescue Team sheet ripped and formatted by SilverDeoxys563.

**The thing worth importing was not the art, it was the rules.** Every other
theme's wall vocabulary had to be recovered by reading vanilla layouts, because
vanilla ships maps and not rules — see §4, where statistical mining is shown not
to recover them. This sheet ships a **Legend column giving each cell's neighbour
mask**, which the importer decodes and matches against the twenty `PaintWalls`
slots directly. All twenty matched at full score. Nothing was guessed, and no
mock was needed to disambiguate anything.

Two things follow that no other theme gets:

- **Four distinct inside corners.** Vanilla Emerald never drew them, which is
  why every other table in the project repeats one metatile across
  `CORNER_OPEN_NW` and `_NE`. This is the first where the two differ.
- **No composed slivers.** The cave needed seven spliced metatiles, Fiery Path
  six, the ocean seven — vanilla's walls are never one block thick. All seven
  sliver cases are native here.

The cost is that the interior is a flat dark block, so a wall mass reads as void
with a crystal rim rather than as textured rock. That is the sheet's own look,
and it is also the only fill it has that **tiles invisibly: measured seam 0.0**,
against 35–42 for every textured rock face in the Meteor Falls tileset evaluated
beside it, where a fill at that seam became visible corduroy.

The sheet has no stairs of its own, so the theme borrowed `gTileset_General`'s
grey warp (`0x0A7`) for a while — the one piece that did not belong. It has its
own now, drawn rather than composed, and the crystal it is lit by is literally
palette 6's: see *Drawing the descent* above.

#### One tileset, two sheets

**A tileset does not have to come from one sheet.** Glacia wears Lapis Cave's
crystal walls over **Mt. Freeze's snow** — a second rip by the same author — for
the plain reason that neither sheet has both, and hers is the theme it is
snowing on. `import_tile_sheet.py` takes a list of blocks, each naming its own
sheet and column, and the block order *is* the metatile order.

Three things that fall out of composing rather than importing:

- **The 15-colour budget is per PALETTE SLOT, not per block.** The ground and
  its two decor variants all sit in slot 7 — a tileset has one palette per slot,
  so quantising them apart would hand slot 7 whichever was written last and
  silently recolour the other two. They are quantised together instead: twelve
  colours between the three.
- **Every block carries its own donor attribute.** Decor replaces a floor block
  in place, so the decor metatiles need the ground's `MB_CAVE` and not a bare
  attribute. A decor tile without the encounter flag would be a hole in the
  encounter surface **that looks exactly like snow**, and §3's checker tests
  what the theme table names as its floor — it would not see it.
- **Ids move when a block's cell count changes.** Swapping the ground happened
  to leave every id put, because both sheets carry the same 47 ground cells in
  the same legend order. That is luck, not a guarantee. Re-run the importer and
  diff the printed defines rather than assuming.
- **Alternates pair by LEGEND POSITION, not by order.** A block declaring
  `varies` is a column of variants for another block, and an Alt cell at
  `(row, k)` varies whatever the base draws at the same `(row, k)` — the same
  neighbour mask, so the same autotile case. The jungle's five wall variants
  fall out of the sheet this way. Pairing them by hand would put a north-edge
  variant on an interior block, which reads as a hole in the foliage.
- **A block's donor attribute is a design decision, not boilerplate.** The
  importer defaults a ground block to the cave's `MB_CAVE`, which carries
  encounters. The jungle's ground is `MB_NORMAL`, copied from the route grass it
  replaced, because that theme's wild battles come from its long grass layer and
  its ground is safe to cross. Taking the default would have switched encounters
  on across the whole floor **as a side effect of changing what the floor looks
  like** — a gameplay change disguised as an art change, and nothing would have
  flagged it.
- **`over` flattens a block onto another's plain fill before any colour is
  read.** These sheets draw a terrain's edge cells with transparent corners and
  say so outright — *"When Ground is adjacent to Water, treat Water as though it
  were a Ground tile."* A metatile could do that with its top layer and
  eventually should; flattening bakes the composite, so the jungle's water can
  only ever border dirt, but it stays one layer with no question about whether
  the player walks over or under it.

#### A palette that does not fit is a thing to solve

Murky Cave's walls are the first block on any sheet to want **more than the
fifteen colours a 4bpp palette has** — seventeen — so the importer reduces
rather than refusing.

It merges the pair minimising **`distance × min(count)`**, an estimate of the
pixel error the merge introduces, and repeats. **Ranking by error rather than by
rarity is the entire point**, and this sheet is why: its two mossy greens are
0.5% of the wall pixels each and would go first on rarity, taking the only green
out of a brown wall. Weighted by distance they are 50 apart, so what goes
instead is a near-duplicate brown 16 away and 187 pixels big — and the greens
are then folded into *each other*, so the moss survives as one tone rather than
two. A rare colour that is far from everything else is exactly the one worth
keeping, because being far from everything else is what makes it a colour rather
than a shade.

Every merge is **reported**, so art quietly losing a colour is never silent.

#### A blend cannot be transparent

Worth its own heading because it was invisible until a terrain's own palette
happened to approach the sheet's background, and it was latent in **every**
import before that.

The importer decided transparency by testing the **downscaled** 16×16 for the
background colour. BOX-averaging two of the jungle water's dark teals —
`(0,99,107)` and `(8,115,140)` — lands on `(4,107,123)`, which is exactly 30
from the sheet's own `(0,128,128)` background and so was read as a hole. Every
water bank came out with a dotted line of **black pixels**, from art that has no
transparency there at all.

**Transparency is not a colour, so a blend of two opaque colours cannot be
one.** The mask is computed at source resolution and downscaled by majority vote
over the area each output pixel covers. Re-running Lapis afterwards gives the
same 285 tiles, the same 143 metatiles and the same floor id — and its painted
blocks now carry zero transparent pixels, where before some were speckled the
same way and nobody had looked.

#### Grafting: vanilla pixels, this tileset's colours

A `graft` block is art on no sheet, assembled from vanilla tiles and given a
palette of the tileset it is joining. The jungle's long grass is the case, and
it cost **no pixel art at all**.

It works because of something checked rather than assumed: in vanilla's long
grass **the blades never use a ground palette index and the ground never uses a
blade one** — blades are indices 1–4, ground is 13–15 — so the two recolour
independently. The blades take their ramp from this tileset's *wall* palette,
which makes the grass the same greens as the foliage above it; the ground takes
its ramp from the *dirt* palette.

That is what buys the fringe back. `0x208` was the only
`MB_LONG_GRASS_SOUTH_EDGE` metatile in the game, it was Fortree's, and its lower
band is drawn as route grass — so leaving Fortree lost it, and it would have
been wrong on dirt even if kept. Recoloured, the same pixels end a stand of
grass on shaded soil.

**Map ranks, not nearest colours.** What has to survive a recolour is the
*order*: a blade ramp that stops descending stops reading as blades. Grafts run
after the sheet palettes exist, because those are what they draw from.

The swap also improved the floor: Mt. Freeze's snow measures a **seam of 5.0
horizontal and 6.2 vertical against the Lapis ground's 8.2 and 12.0**, so the
fill it replaced was the more visible of the two. (The flat wall interior is 0.0
by construction — a flat block has no seam to show.) Its Ground Alt 1 and Alt 2,
one cell each on both sheets, are the decor: a clump and a swept drift, in the
floor's own palette, so they read as surface texture and not as ornament resting
on it. 1 in 8, denser than the ornamental themes run at, because there is
nothing here meant to be noticed individually.

**A theme swap orphans more than the theme table.** Moving Glacia off the snow
recolour left `LAYOUT_ROGUE_DUNGEON_VRGLACIA`, `gTileset_RogueVictoryRoadGlacia`
and everything `make_glacia_snow.py` wrote referenced by nothing but
`layouts.json` — still linked into the ROM, still costing space. Her decor array
went with the snowfield rather than being repointed: it named metatile ids in
the snow tileset's files, and under Lapis those same ids are unrelated pieces of
crystal wall, which is the §3 bug exactly. **The replacement is new art, which
is what repointing could never have been** — a second sheet, not a rename.

---

## 6. Constraints

### RAM is the ceiling, not ROM

A stock expansion build already uses **86% of both EWRAM and IWRAM** before we
add anything. ROM is not scarce (~6.5 MB free of 32 MB).

EWRAM, 226 KB used, where it goes:

| bytes | object |
|---|---|
| 115,968 | `malloc.o` — `gHeap`, one fixed `EWRAM_DATA` array, `HEAP_SIZE 0x1C500` |
| 55,308 | `load_save.o` — SaveBlock buffers |
| 20,524 | `fieldmap.o` — `sBackupMapData`, the map grid |

**~84% is three fixed buffers.** Deleting stock content does not shrink them.

- **Cutting content frees ROM, not RAM.** Making the Birch intro unreachable
  freed 6,504 bytes of ROM automatically via `--gc-sections`, and moved EWRAM
  and IWRAM by exactly zero.
- **If EWRAM gets tight, tune `HEAP_SIZE` first** — 51% of all EWRAM in one
  constant. A roguelike that never opens contests or the frontier may not need
  vanilla's heap. Heap exhaustion fails at *runtime*, so this needs play-testing.
- **Mark every new static `EWRAM_DATA`.** Plain statics land in IWRAM, which has
  roughly 4 KB free against 35 KB of EWRAM. Watch the linker's IWRAM line on
  every build.

---

## 7. Adding a theme

New Mauville was the first one added after the abstraction existed, and it
needed **no generator changes and no engine changes** — a table entry, a donor
layout, and a species pool.

**Ask first whether it needs to be a new tileset at all.** A palette-only
recolour of a tileset already done costs one palette directory and inherits the
wall table, the slivers, the corners and the skirts verbatim — see §5 for how
and for the one measurement that decides whether it will work. The four Victory
Road themes are that, and they are the cheapest themes in the project by an
order of magnitude. The process below is for when the answer is no.

The process, with the tool for each step:

1. **Find a vanilla map** using the tileset pair you want, in `layouts.json`.
   Search by map name, not tileset name: New Mauville's interior runs on
   `gTileset_BikeShop`, which no one would guess.
2. **Render the whole layout** — `render_layout.py <Layout>`. It also censuses
   which metatiles appear, split by collision, which immediately gives you the
   floor and the wall mass.
3. **Read the metatile ids off the layout as a grid**, not statistically.
   `derive_wall_table.py` tallies which metatile vanilla uses per open-neighbour
   mask and reports how decisive each is; treat anything under ~80% as a hint,
   not an answer. On New Mauville almost every mask came back undecisive because
   vanilla's walls are covered in decorations — the grid dump settled it in one
   look.
4. **Render candidates** — `sheet.py <primary> <secondary> <ids...>` — and
   *look* at them. `0x21F` reads as a wall body on a contact sheet and is
   actually a cap; it stripes when stacked.
5. **Mock the whole table before writing any C** — `theme_mock.py <name>`. It
   transcribes `PaintWalls()` exactly, so a wrong slot is obvious on sight and
   invisible in a diff. It also reports which slots were never hit, so you know
   what is still unvalidated.
6. **Add the donor layout** — `add_theme_layout.py <ID> <Name> <primary>
   <secondary>`. 48×48, dummy blocks. No map points at it; only the tileset
   pair matters, because the generator swaps `gMapHeader.mapLayout` and leaves
   `mapLayoutId` alone.
7. **Add the `RogueDungeonTheme` entry** and extend `enum DungeonThemeId`.
   `ThemeForFloor` is a modulo, so nothing else needs touching.

Choosing a generator:

- Walls that are 2×2-aligned blocks (trees, rocks) → `DUNGEON_GEN_WOODS`, and
  skip autotiling entirely.
- Walls that are genuinely 1×1 → `DUNGEON_GEN_CAVE`.

**Do not choose it by what the theme looks like.** The jungle is all trees and
grass and still runs on `DUNGEON_GEN_CAVE`, because Fortree draws a *continuous*
leaf mass that tiles 1×1 where Rustboro draws discrete 2×3 trees on a 2×2 grid.
Prefer `DUNGEON_GEN_CAVE` when it fits: it is the only path the cosmetic passes
run on, so a woods-generator theme gets no patches, skirts or decor at all.

### A theme the player surfs across

The ocean floor is `0x170`, `MB_OCEAN_WATER`. Nothing in the generator knows
about surfing; it falls out of one engine check:

```c
// GetAdjustedInitialTransitionFlags, overworld.c
else if (MetatileBehavior_IsSurfableWaterOrUnderwater(metatileBehavior) == TRUE)
    return PLAYER_AVATAR_FLAG_SURFING;
```

The engine reads the behaviour **under the player's arrival tile on every warp**
and answers a surfable one with a surf blob. **No HM, no party requirement, no
badge** — which is the only reason a water dungeon is not a softlock waiting for
a player whose team cannot Surf. It is how vanilla handles emerging from a dive.

Two preconditions, both already true and both worth not breaking:
`MAP_TYPE_UNDERGROUND` on the dungeon map (the check bails on `MAP_TYPE_INDOOR`
and answers `UNDERWATER` differently), and `FLAG_SYS_CRUISE_MODE` clear.

**Nothing walkable may ever be painted on such a floor.** Step onto land and the
player dismounts, and getting back on water *would* need the HM. So the walls
are solid rock, and the way down is a **whirlpool** (`0x3D1`) which keeps the
attribute of the deep-water dive spot it replaced — still `MB_DEEP_WATER`, still
surfable, so it does not dismount anyone who stands on it.

Three things followed that were not obvious:

- **Water is elevation 1, not 3.** Every other theme walks at
  `DUNGEON_ELEVATION_FLOOR` (3); vanilla puts deep water at elevation 1 in 100%
  of its blocks and ocean water in 92%. Hence `DUNGEON_ELEVATION_WATER`.
- **Object event templates had the floor's elevation hard-coded to 3.** Two
  different non-zero elevations do not collide, so on a water floor the player
  would have walked straight through every trainer instead of being able to talk
  to one. Templates now take `theme->elevationFloor`. This is the same shape of
  bug as `CarveFloor` naming `DUNGEON_METATILE_FLOOR` directly: a constant that
  was right for every theme that existed when it was written.
- **A trainer's sprite has to suit what it is standing on.** `theme->trainerGfx`
  / `trainerGfxAlt` — swimmers here, alternating so a floor is not one repeated
  figure. Vanilla Routes 124–126 use exactly `SWIMMER_M`/`SWIMMER_F`.

`theme->arenaPlatform` stands the arena's trainer on a solid block of its own,
so Tate and Liza are not waiting in the middle of the sea. It needs no art: the
block goes down **before** the autotile pass, which sees one wall ringed by
floor and paints `WALL_SLIVER_ISOLATED`, already a lone rock. It also turns that
trainer talk-only, because a boss that keeps its sight range walks off the
platform to approach and then spends the rest of the floor standing on water.

### A theme the player dives through — and needs a second MAP for

Surfing falls out of a metatile behaviour. **Diving does not: it falls out of
the map header.**

```c
// GetAdjustedInitialTransitionFlags, overworld.c — checked BEFORE any behaviour
else if (mapType == MAP_TYPE_UNDERWATER)
    return PLAYER_AVATAR_FLAG_UNDERWATER;
```

Unconditional, like the surf case — no Dive HM, no badge, no party requirement.
But **`GetCurrentMapType()` goes through `GetMapTypeByWarpData()`, which reads
the ROM header by warp group and id, not `gMapHeader`.** So the RAM patch that
swaps `gMapHeader.mapLayout` per theme cannot reach it, and no amount of
per-theme trickery makes a shared map underwater.

Hence `theme->mapId`, and `MAP_ROGUE_DUNGEON_UNDERWATER`. **This is cheap, and
cheaper than it looks:**

- **Two maps can share one layout** — 49 vanilla layouts already do,
  `LAYOUT_POKEMON_CENTER_1F` by sixteen maps. The second map reuses
  `LAYOUT_ROGUE_DUNGEON_FLOOR`, so `mapLayoutId` is *identical* on both and
  every dispatch that keys on it keeps working untouched. The map-plumbing
  recipe's step 5 — two dispatch branches in `overworld.c` — costs nothing here.
- Scripts are shared too. The new map's object events point straight at
  `RogueDungeonFloor_EventScript_Trainer`, and its frame table at that map's
  `RogueDungeonFloor_OnFrame`, so its `scripts.inc` defines nothing at all.
- It also buys the underwater **battle backdrop**, the diving **field effect**,
  the cave-transition pair and `WEATHER_UNDERWATER_BUBBLES` — about eighteen
  places key on `MAP_TYPE_UNDERWATER`, and a hook faking the avatar flag would
  have got every one of them wrong.

**Every path that puts the player on a floor has to choose the map.** There were
five, not the one that was obvious: the stairs script, the run-complete script,
the whiteout, the new-game start, and **the debug floor warp** — that last one
matters most, because it is the tool the theme gets looked at with, and left
alone it enters an underwater floor on foot and makes the theme look broken.
They all go through `RogueDungeon_WarpToCurrentFloor` / `…SetWarpToCurrentFloor`.

The rest stop's exits are `warp_event`s, which cannot be conditional, so they
are **`MAP_DYNAMIC`** — the engine's own mechanism for this — with the
destination set from the rest stop's `ON_LOAD`. On load rather than on the way
in, so it is right however the player got there, including a save made inside it.

Two things underwater gets for free that the ocean had to fight for:

- **Elevation is 3**, the ordinary walking one, not the ocean's 1. Vanilla puts
  1467 of 1468 passable underwater blocks at 3.
- **No `arenaPlatform`.** The seafloor is ordinary walkable ground, so Juan
  stands on it the way every land boss does.

Encounters come from **seaweed**, not the floor: `0x216` is `MB_NORMAL` and
carries none, `0x281` is `MB_SEAWEED_NO_SURFACING` and carries both
`HAS_ENCOUNTERS` and `SURFABLE`. The `NO_SURFACING` variant matters — a dungeon
has no paired surface map, so a plain `MB_SEAWEED` would leave an unanswered
dive warp. Seaweed is a single uniform 2×2 metatile with no edge art at all
(`0x201` and `0x281` are the same four tiles under different palettes), so the
patch layer's nine-slice degenerates to a plain fill.

`theme->longGrass` names it while `tallGrass` stays 0 — the jungle's
arrangement. That is not an abuse of the field: `tallGrass` is what switches on
grass-**blob** placement, and a patch-layer theme would then paint it twice.
`longGrass` is left as the declaration of where encounters fire, which is what
`check_encounter_flags.py` reads. Everything that would paint *from* it lives in
`StampCell`, which is `DUNGEON_GEN_WOODS` only.

### Adding a WEATHER

`WEATHER_PETALS` was the first new one. It is cheap, and the cost is almost
entirely in knowing which four places to touch.

**There are free slots.** `WEATHER_COUNT` is 24, but the named values stop at
`WEATHER_ABNORMAL` (15) and resume at `WEATHER_ROUTE119_CYCLE` (20) — **16–19
are an unused gap**, already inside the bound, and `sWeatherNames[WEATHER_COUNT]`
already sizes for them.

| what | why it matters |
|---|---|
| `sWeatherFuncs` | indexed with **no bounds check**, and it ends at index 14 |
| `TranslateWeatherNum` | **the one that bites** — see below |
| `sWeatherNames` | debug only |
| the constant | the gap at 16–19 |

**`TranslateWeatherNum` is the silent failure.** Without a `case`, a map header
asking for the new weather falls through to `default: return WEATHER_NONE` and
*absolutely nothing happens* — no crash, no warning, no effect. The map loads
perfectly and is simply not weathered.

**Never put new colours in `PALTAG_WEATHER`.** It holds `gFogPalette` and is
shared by rain, snow, ash, bubbles and the fog itself, so recolouring it
repaints every weather in the game. The route is `PALTAG_WEATHER_2`, a
lazily-allocated second slot filled by `LoadCustomWeatherSpritePalette()` —
which **clouds and sandstorm already use**, so there is precedent to copy rather
than plumbing to invent. It calls `UpdateSpritePaletteWithWeather` itself, so
the fade comes free. There is only one such slot, so a `PALTAG_WEATHER_2`
weather cannot coexist with clouds or sandstorm; one weather runs at a time, so
this never arises.

**Decide explicitly whether it reaches battle.** Petals are absent from the
overworld-to-battle switch in `battle_util.c`, so `gBattleWeather` stays clear
and they are purely cosmetic. That is a decision, not an omission — `WEATHER_SNOW`
*is* mechanical (§10), and a new weather silently inheriting nothing is the
right default only if you meant it.

**Sprite storage can be shared.** Petals reuse `snowflakeSprites[]` and its
counters, because only one weather runs at a time and a second array would cost
the `Weather` struct 64 bytes for nothing.

**The snow's cut feature is worth finishing rather than deleting.**
`InitSnowflakeSpriteMovement` writes `tFallCounter`, `tFallDuration` and
`tDeltaY2` on every spawn and nothing reads them, and `WaitSnowflakeSprite` is
`UNUSED` — a pause-and-resume cycle was built and left inert. Snow does not need
it; petals do, and a petal that stalls, hangs and drops again is most of what
separates blossom from confetti falling at a constant rate. **Dead machinery in
a vanilla effect is often the feature the next effect wants.**

The drift itself is two constants. Snow offsets x by `gSineTable[i] / 64`, which
is ±4 pixels and reads as a wobble; petals use `/ 16`, so the sideways travel
*is* the motion. Snow also starts every flake at wave index 0, so they all swing
in step — petals randomise it.

### Openness

`roomCount`, `roomMin`, `roomMax` and `corridorWidth` let a theme carve more
openly without a second generator. Measured over 2000 seeds each:

| shape | coverage | rooms |
|---|---|---|
| cave, 8 rooms of 5–10, 1-wide | 24.2% (17–33) | 8.0 |
| jungle, 12 rooms of 7–13, 3-wide | 42.8% (25–58) | 7.9 |
| ocean, 12 rooms of 7–13, 5-wide | 49.3% (29–68) | 7.9 |

**Room size alone is the wrong lever.** Bigger rooms simply stop fitting, so
coverage plateaus near 35% while the room count collapses from 8 to 3 and the
variance gets ugly (one sweep bottomed out at 11.8%). Room *count* and corridor
*width* are what actually buy openness, and they keep the room count roughly
where it was, which matters because trainers and the exit are placed per room.

**This was walked into a second time**, by the person who wrote the paragraph
above, while tuning the ocean. Reaching for 9–15 rooms *and* 5-wide corridors
measured **worse on both axes** than 7–13 at the same width — 44.6% coverage
against 49.3%, with the room count collapsing from 7.9 to 5.7. Widening the
corridor and leaving the rooms alone was strictly better. A sweep of 7–11 at
7-wide goes further still, 58.2% and 9.1 rooms, but averages floors that are
three-quarters open with a max of 77.9%, which stops reading as a dungeon.

`verify_dungeon_gen.py` runs every shape in its `SHAPES` table, so a new one
gets its reachability and out-of-bounds checks for free — add an entry when you
add a theme that carves differently.

### Corridor width is a cheaper answer to slivers than art

Four themes needed composed sliver metatiles. Ever Grande needed none, and the
reason generalises: **which sliver slots fire is a function of `corridorWidth`,
so widening the carve can retire the case a tileset has no art for.**

Measured over two mock floors:

| corridor | `SLIVER_VERT` | `SLIVER_HORZ` + caps |
|---|---|---|
| 3 | **19** | 2 |
| 5 | **0** | 11 |

At 3 the vertical fires and `gTileset_EverGrande` has nothing for it — a census
over all 250 General-primary layouts puts the best candidate at 22%. At 5 it
never occurs, and the load moves to the horizontal, which is decisive native
art: `0x079`, the cliff's own **south face**, is what vanilla uses for a
one-thick horizontal wall 492 times of 990 and for both caps at 77% and 67%.
Showing a south face and nothing else *is* what such a wall looks like from
below, so there was nothing to draw.

**Check the slot counts at each width before reaching for the splice tools.**
Note this is the opposite of what the jungle and underwater notes imply — they
observe that wide corridors retire slivers generally, and here 5-wide *raised*
the horizontal count from 2 to 11. Wide corridors do not remove slivers; they
change **which** ones you get.

### Material mismatch: a wall set that works in vanilla can still fail as art

Ever Grande's cobble garden wall (`0x232` vertical, `0x234` horizontal) is
genuinely one block thick in vanilla — `0x232` runs north-south with grass on
both sides 85% of the time — and it still could not serve as the cliff's sliver
art. At game scale the vertical piece reads as a pillar of a foreign material
and the horizontal one as a **hole in the ground**, because the cobble's shaded
face is blue-grey against the cliff's warm tan.

It works in vanilla as a long terrace wall bordering a field, and fails as a
one-block gap inside a rock mass. **"Vanilla uses this as a thin wall" is not the
same claim as "this can be a thin wall in your wall set"** — the second needs
the mock, and the mock settled it in one render.

It is used instead for `WALL_SLIVER_ISOLATED`, which is what `arenaPlatform`
paints, so Wallace stands on a stone dais rather than a lone boulder.

**Check whether the tileset draws one-block-thick walls natively before assuming
it needs composed metatiles.** The cave needed seven spliced metatiles because
vanilla caves are never one thick. New Mauville needed none: it is a facility
full of thin partitions, so `0x227` and `0x290` already exist for exactly the
sliver cases. Fiery Path needed six — its horizontal sliver (`0x30C`) is native
(vanilla uses it with 100% consistency wherever floor sits both north and south)
but nothing vertical exists. Its splices were much cleaner than the cave's,
because every Lavaridge edge is a top-layer overlay over one shared bumpy base:
each sliver is four quadrant copies, no hand-built entries
(`make_fiery_slivers.py`).

The ocean needed all seven — vanilla's smallest sea rock is 2×2 — and got them
the same cheap way, because all nine of its nine-slice pieces share **one**
bottom layer (`41C6 41C7 41C7 41C6`) with every edge a top-layer overlay laid
out as a regular 6×6 tile grid (`make_ocean_tiles.py`). **Check the metatile
entries before deciding a splice is hard**: the cave's was fiddly and needed
hand-built entries, and both later tilesets turned out to be pure overlay work.

**There are four diagonal corner cases and there used to be three slots.**
`WALL_CORNER_SOUTH` fired on "NW *or* NE open", so a tileset with distinct art
for the two could not say so. They are now four, named for the OPEN diagonal —
the old names described the wall's own position, which is why `WALL_CORNER_NW`
held the art Fiery Path calls its *south-east* corner and every table looked
wrong until you knew that.

**Know what an inside corner is FOR before hunting for art.** Its job is to
carry the wall's dark edge from one neighbour round to the other in a continuous
L — from `FACE_MID`'s bottom band to `INTERIOR_RIGHT`'s side band — and it is
**solid**. No cardinal neighbour of that block is floor, so none of it may show
any. Everything else is decoration.

The ocean's sat on the rock interior for a while and the edge stopped dead at
every room corner. Two attempts to fix it failed, both of which looked right in
isolation:

- Windows into a water-**pocket** block in `gTileset_General` (columns 6–B
  against the convex slice's 0–5). That art is drawn from a *rounded* pocket, so
  its corner pixels are transparent; over the sea they became a blue bite. **A
  room corner may never show water** — the open diagonal is a full block away.
- The same windows over an opaque rock underlay. No water, but no dark edge
  either, so it was barely distinguishable from the plain rock it replaced.

The actual answer was vanilla's, and it was already in the scan output. Running
the corner case over every Mossdeep layout names one metatile per diagonal at
**27–43%** — `0x074` SE, `0x089` SW, `0x07D` NW, `0x07B` NE — and those are
`gTileset_General`'s cliff **inside corners**. They were misread as land art
because they live in the primary and look like plain rock on a contact sheet.
Only the palette changes, to Mossdeep's rock palette, which is the same
recolouring vanilla already applies to the nine slice. Not composed, not drawn.

Note `0x07D` and `0x07B` are not mirrors of anything — they mix the pocket block
with a single tile of the convex slice — which is why guessing at 2×2 windows
never landed on them.

**This is the cave's answer reached the cave's way.** Granite Cave's
`0x21B`/`0x21C`/`0x223` are native pieces found by reading the case out of a real
layout, and putting the ocean's attempt in a 2×2 junction beside `0x21B` is what
made the failure obvious: the cave's corner has a dark mass flowing into both
neighbours, and the ocean's had no dark edge in it at all. **Build that junction
first for any new theme** — corner, the two edges it must meet, and floor. It is
four metatiles and it answers the question in one look.

All four are `COVERED`, like the rock interior. Layer type would have mattered
if they showed sea — the nine slice's north row is `NORMAL` so a near shore
overlaps someone surfing past — but a solid corner can never overlap the player
anyway: the only tiles you could stand on to reach it are its cardinal
neighbours, and all four are wall.

Slivers matter more here than the count suggests. At 3-wide corridors the ocean
hit 19 of them across two mock floors and they are far more visible against open
water than against rock — bare interior art reads as a rendering fault, not as a
reef. Four of the seven slots are still never hit by the mock, but
`SLIVER_ISOLATED` is exercised in game regardless, because it is what the arena
platform is made of.

**Diff the candidate secondary against a tileset you have already done.** Some
vanilla secondaries are pure art reskins of another — same metatile
definitions, same attributes, different pixels. `gTileset_MirageTower` is one:
411 of its 414 metatiles are byte-identical to `gTileset_Cave` and all 414
attributes are, so the cave's whole wall table transferred verbatim and the
cave's splice recipe produced byte-identical slivers, in sand instead of rock.
That turned a day of mining into an afternoon. It does not generalise — a scan
of all 95 secondaries found only `gTileset_NavelRock` even partly similar, at
32% — but the check costs one script and is worth running first.

Two traps came with it. **The reskin's own vanilla map may not use the
convention you want**: Mirage Tower walks on `0x211` and never uses `0x201`,
but those two ids are shared with the cave, where `0x201` is the smooth floor
and `0x211` the hatched rock. Following vanilla would have put the rock texture
on the walkable surface and inverted what the player learnt two dungeons
earlier. **And identical ids are a coincidence to write down, not to rely on**
— both tilesets happen to have exactly 414 vanilla metatiles, so both append
their slivers at `0x39E`–`0x3A4`. The constants are spelled out separately per
theme so either tileset can change without silently corrupting the other.

**Check what the wall art is drawn against.** New Mauville's edge pieces are all
lit strips over black, so the mass interior has to be the void metatile. Filling
it with a wall body puts a lit edge in the middle of a dark mass.

### Shading and decoration

Two more passes run at the end of `ApplyWallAutotiling`, both optional per theme
and both leaving collision alone.

**`ApplySkirts`: wall edges bleed into the floor, keyed by wall identity.**
Vanilla draws a wall's bottom or side edge into the adjacent floor tile — a
skirt. Which floor tile depends on the SPECIFIC wall metatile, and per wall
type it is effectively 100% deterministic: New Mauville's floor under the band
is `0x22F` 34/37, east of `0x270` is `0x27D` 24/24; Fiery Path's floor under
the `0x30C` ridge is `0x269` 10/10 and *never* under a face. Per theme this is
a `RogueSkirt` table: `{wall, south, east}`, plus one corner tile.

**Census by neighbour identity, never pooled.** Pooling all wall types together
turns these deterministic rules into fake probabilities — 40–67% in New
Mauville, 21% in Fiery Path — and that error shipped twice: first as a solid
skirt band under every Fiery Path wall (read as a second floor colour), then as
a randomly thinned one (still wrong — the tile is the ridge's own edge, and
random placement floats fragments of wall in open floor). The query that
settles it is "for floor with a wall north, split by the north metatile" — a
three-line change to the census that turns 21% into 10/10.

**`ApplyDecor` swaps the occasional block for a decorated variant.** Keyed on
the *painted metatile*, not the wall slot, so one entry covers every slot
sharing that metatile — and a floor base (Fiery Path's ember sparkles) is as
valid as a wall base. The block's own collision and elevation are copied over
unchanged, which is what makes it safe: a wall variant stays wall, a floor
variant stays floor, and reachability cannot change. What remains deliberately
unsupported is decoration that *adds* collision — solid props standing on floor
could wall off a corridor or bury the stairs.

A 2-wide entry (nonzero `variantEast`) lands only where the block east is also
undecorated base and writes both halves — split art like New Mauville's
bookcase reads as cut off if a lone half is placed. The west-to-east scan makes
double decoration impossible, because a placed unit turns both blocks into
non-base metatiles.

`decorRarity` is 1-in-N; New Mauville uses 12, landing 6–15% of wall-band blocks
depending on the floor. Vanilla is far denser because it is a designed facility;
a generated floor being scanned for stairs and trainers wants the wall mostly
plain.

**The decoration lists come free from the wall mining.** `derive_wall_table.py`
reports the runners-up per slot, and on a decorated vanilla map those runners-up
*are* the decoration set — the same data that makes the wall table look
undecisive.

**`ApplyFloorPatches` lays a soft region ON the floor and autotiles its edges.**
Unlike decor, which swaps one block for another, this is a *region* autotile: a
cell's art depends on which sides the region continues into, so a blob gets
rounded edges rather than a hard rectangle. Mirage Tower's sand drift and the
cave's sand pools are the same twelve metatiles.

Three things make it work:

- **Membership is a pure function of geometry** — inside an ellipse, and not a
  wall. It must not depend on what the pass has already painted, or a cell
  written earlier stops reading as part of the region and its neighbour draws an
  edge through the middle of the blob.
- **Eroded by one.** The raw ellipse leaves one- and two-block specks wherever a
  blob merely clips a room, and a lone tile of a second colour reads as a
  mistake rather than a drift. Requiring two of the four neighbours to be in the
  raw shape removes them and leaves anything larger untouched. The erosion tests
  the *raw* shape, not the eroded one, so it stays pure.
- **A fourth row for "wall above".** Vanilla bakes the wall's own base into the
  region's top edge where a wall sits above it (`0x29B`–`0x29D` against
  `0x298`–`0x29A`), so those are separate slots, not the same art shaded.

It runs before the skirts, so a wall's own edge art still wins for a theme with
both, and before the stairs are placed, so it cannot bury the exit. Collision
and elevation come from the floor, so like decor it cannot change reachability.

**A theme can have several layers**, painted in order so a later one wins. The
jungle lays long grass in six blobs of radius 8 — big enough to swallow a
clearing whole, which is the point, since long grass is its only encounter
surface — and then punches ten small puddles through it, so water sits *in* the
grass rather than under it. Each layer autotiles against its own geometry, so
the grass underneath keeps correct edges where a puddle covers it. The layer
index is folded into the blob hash salt, or the two layers would stamp
identically.

### A set of N consecutive metatiles may be N PHASES, not N variants

Ever Grande's flowers look like a scatter set: sixteen metatiles, each four
consecutive distinct tiles, no flips, no top layer, and on a contact sheet the
eight of a colourway are almost indistinguishable. Painting a blob by picking
among them at random is the obvious implementation and it is wrong.

They are **eight steps of a diagonal banding**. Over the 341 flower blocks of
`EverGrandeCity_Layout`, **85% satisfy `variant = (y - x + k) mod 8`** for one of
three values of `k` — one constant per flower field, wherever the artist started
that field. Rendered, the field comes out as alternating diagonal rows of pink
and orange, which is visible in the vanilla map the moment you crop it and
invisible in any per-metatile view.

So the generator paints a diagonal, not a hash (`RoguePatchLayer.phase`). Three
things fall out of that:

- **It is cheaper than a hash** and consumes no RNG, so `WriteFloorBlocks` can
  repaint a floor without the flowers reshuffling under the player — the same
  property the position-hashed passes need a hash to get.
- **`0x298`–`0x2A7` are the same sixteen with a sand-edged bottom, preserving
  phase.** A ready-made south edge, if one is ever wanted.
- **Vanilla uses one colourway per field, never mixed** — pink and orange in one,
  yellow and blue in another. So a theme or a blob can pick a colourway; the two
  sets are the same 32 tiles under palettes A and B.

**The general lesson: before treating N consecutive metatiles as
interchangeable, test whether their index is a function of position.** A census
that asks "which of these appears here" answers uniformly and tells you nothing;
the question that works is "what is `variant - f(x, y)`".

**Not every theme has a region set**, and it is worth checking before designing
one. Fiery Path's floor variety is `0x310`/`0x311`, already decor; New
Mauville's apparent region turned out to be its existing skirt tiles; the woods
has none, and could not use one anyway because `DUNGEON_GEN_WOODS` returns
before `ApplyWallAutotiling`.

The jungle's puddles are worth copying elsewhere: they are `0x0C8`–`0x0DA` in
the **primary** tileset, so they are available under *any* pair, and every piece
is `MB_PUDDLE`, which carries no flags at all — walkable, **not surfable**, no
encounters. The pond and ocean water in the same tileset are surfable and must
never be painted, or the player can surf out of the floor.

**All three passes are position-hashed, not drawn from the dungeon RNG.**
`WriteFloorBlocks` can repaint a floor without `PrepareFloor` having run again,
so consuming RNG there would leave the state dependent on how the player arrived
and a floor would redecorate itself on re-entry.

---

## 8. Tooling

All of it lives in **`tools/rogue/`** — see `tools/rogue/README.md` for the
per-script table and which ones write checked-in files.

**In game there is a floor warp**: R + START in the overworld → Utilities →
*Rogue floor warp…*. It dials 1 to `DUNGEON_TOTAL_FLOORS` and shows the dungeon,
the floor within it, the encounter level and whether the floor is an arena — so
floor 85 reading `D9 F5 Lv46 BOSS` confirms the segmentation without anyone
counting. It opens on the current floor, and it deliberately does not touch the
run state, so warping in without a party starts a deep test run at the starter
prompt rather than refusing.

Windows has Pillow; WSL does not, and `python3-venv` is not installed, so
anything that touches an image has to run from Windows against the repo over
UNC (`//wsl.localhost/Ubuntu/home/p50/decomps/pokeemerald-expansion`). Scripts
resolve the repo from their own location, so they run from either side.

**Never guess tileset paths from the symbol name.** FRLG secondaries carry an
`_frlg` directory suffix the symbol lacks, acronyms and digits split
unpredictably, and **7 tilesets share assets across directories** (`SilphCo`
takes tiles from `condominiums_frlg` but metatiles from `silph_co_frlg`). Parse
`headers.h`, `graphics.h`, `metatiles.h` **and `src/graphics.c`** —
`gTilesetTiles_General` is declared in the last one.

---

## 9. Process notes

- **Prototype in Python and render it before writing C.** The woods generator,
  every autotile table, and both metatile splices were validated visually first.
  This caught several wrong readings cheaply.
- **Verify invariants host-side over thousands of seeds** — connectivity,
  reachability, no out-of-bounds writes, determinism. Cheaper than emulator
  testing and catches different bugs.
- **Measure per-instance, not in aggregate.** An encounter-distribution check
  averaged over 3000 floors showed no problem; the bug was *per-floor* variance
  (one species taking up to 85% of a single floor's slots).
- **Judge a wall metatile against its neighbours, never on a contact sheet.**
  All four concave corners looked right rendered side by side. Dropped into a
  probe floor — one rectangular room, which produces exactly one of each corner
  case — two of them were obviously wrong, because the fault was lighting that
  disagreed with the piece next to it. A single room is the cheapest possible
  fixture for anything keyed on neighbours.
- **Judge it at game scale too, not only zoomed.** The blue bite in those same
  corners shipped because every check was run at 5× or more, where it read as a
  shoreline turning. At 2× it reads as a hole. Render the candidate at 2× beside
  the alternative before believing a zoomed comparison — the zoom that makes a
  seam visible also makes a wrong pixel look deliberate.
- **A census over metatiles does not settle what the tile sheet holds.** Twice
  now the answer to "vanilla has no art for this" was that vanilla has no
  *metatile* for it and the tiles were sitting there unused.
- **Read the scan output again before deciding it found nothing.** The ocean's
  inside corners were in the very first corner-case scan, at 27–43%, and were
  dismissed as land art because they live in the primary tileset and look like
  plain rock on a contact sheet. Two wrong implementations followed. When a
  scan's top answer is rejected, say out loud what it *is* — do not just note
  what it is not.
- **Solve a case once, then check the theme that already solved it.** Granite
  Cave had inside corners working from native pieces. Comparing against it
  directly — same 2×2 junction, both tilesets side by side — settled in one look
  what two rounds of reasoning from first principles had got wrong.
- Long shell heredocs break on apostrophes in prose. Write patch scripts to a
  file, or use the editing tools directly.
- `Path.write_text` on Windows emits CRLF into repo files. Pass `newline='\n'`.

---

## 10. Known gaps

1. ~~Themes against dungeons.~~ **CLOSED.** Fourteen themes against fourteen
   dungeons: Steven's finale has Murky Cave, and `ThemeForFloor`'s modulo is now
   a bounds guard rather than a routing decision. Nothing wraps.

   Kept because the way it closed is the lesson. **The prediction that a new
   theme needs real pixel art was wrong every single time.** Ever Grande needed
   four tiles and nothing else; the Elite Four needed one palette directory
   each; Glacia, the jungle and the finale needed *none at all*, because a
   third-party sheet with an autotile legend supplies the whole wall vocabulary
   and `import_tile_sheet.py` turns it into a tileset. Check what already exists
   — vanilla's tilesets, a palette recolour, a rip — before authoring anything.

   The two techniques that did the work are in §5: the **palette-only tileset**,
   which lets any existing theme spawn a recolour for one directory, and
   **sheet import**, which is now how a theme gets art. Both have the same
   caveat in different clothes — check that what a theme paints comes from *its*
   palettes and *its* tileset, because the ids that survive a swap are the ones
   that stop fitting.
1c. **Ever Grande's terrace is a path, not a region.** `0x23C`/`0x23D`/`0x23E`
   has a left edge, a fill and a right edge and **no north or south edge art at
   all** — the mask census puts continues-NSWE, SWE and NWE on the same fill. It
   is laid as a patch layer anyway and survives because `ApplyFloorPatches`
   erodes blobs round and the brick texture is busy, but a blob's top and bottom
   are hard cuts. It also sits close in value to the cliff, so a plaza touching a
   wall merges into it. The brick is secondary palette 9 and the flowers are 10
   and 11, so palette 9 can be shifted cooler without touching a flower pixel —
   it also carries the round shrub `0x220`, so that is not free.
1f. **The jungle's water is static, and its Sparkle layer is unimported.** The
   water itself is in and reflective, but the sheet animates it by **cycling the
   palette** rather than the tiles, and ships a separate **98%-transparent
   Sparkle overlay** at a second rate (6 frames against the water's 14). Two
   things are missing for either: a fill test that accepts a mostly-transparent
   cell — today's rejects them, correctly, as empty — and use of the metatile's
   **top layer**, which the importer writes as zeros. Mind that a top layer
   under `METATILE_LAYER_TYPE_NORMAL` draws *over* the player: fine for
   scenery, wrong for anything walked on. Painting real **surfable** water would
   also make the jungle the first **two-branch** theme, since its long grass is
   a land encounter surface; `MB_PUDDLE` sidesteps that entirely, which is why
   it is what the water carries. See §3.
1d. ~~Borrowed stairs.~~ **CLOSED.** All three themes that ended at
   `gTileset_General`'s warp `0x0A7` — Lapis, the jungle, Murky Cave — have
   descents drawn from their own material now: `LAPIS_METATILE_STAIRS` `0x28F`,
   `JUNGLE_METATILE_STAIRS` `0x296`, `MURKY_METATILE_STAIRS` `0x295`. See §5.

   **Fiery Path still uses `0x0A7` and should.** It was never part of this gap:
   that theme is a vanilla tileset, and the dark cave mouth reads as a hole
   descending on red volcanic rock. The debt was never "borrows a primary id",
   it was "borrows art made of colours the tileset does not contain".

   Kept because of what the entry got wrong. It called this "the single most
   repeated shortcut", which was true, and predicted the fix was *composing a
   pair from the crystal* — new art, per theme. It cost none: three `stairs`
   block entries, nine tiles, and one shape parameter. **That is the same wrong
   prediction gap 1 made every time it closed** — see the note there. The
   question to ask first is still what the tileset already holds.

   What was *not* obvious, and is the part worth carrying forward: the hard
   problem was never the geometry, it was finding a **dark**. Two of the three
   floors' own palettes have no colour dark enough to be a mouth, and one of the
   two alternatives that looked dark enough was too saturated to read as shadow.
   §5 has both.
1e. **The snowfield is orphaned but still in the ROM.**
   `LAYOUT_ROGUE_DUNGEON_VRGLACIA`, `gTileset_RogueVictoryRoadGlacia` and its
   72 KB of palettes are referenced by nothing but `layouts.json` now that
   Glacia runs on Lapis, and a layout listed there is emitted whether a map
   points at it or not. `make_glacia_snow.py`'s tiles also still sit in the cave
   sheet. Removing them is free ROM; keeping them costs nothing but clutter if
   a future Ice theme wants the art back.
1a. **Underwater has no divers.** Trainers there are `SWIMMER_M`/`SWIMMER_F`,
   who are drawn treading the surface in swimwear while standing on the
   seafloor. There is no diver NPC graphic in the game at all — the only diving
   sprites are the player's own `BRENDAN_UNDERWATER` / `MAY_UNDERWATER`, which
   are the obvious recolour base. This one needs new art; there is no table
   entry that fixes it.
1b. Underwater has no one-block-thick wall art, because the case never occurs
   once across all twelve vanilla Underwater layouts. The sliver slots fall back
   on the edge that would be visible rather than the unedged interior, but the
   mock still hits the horizontal three ways about 11 times a floor. Composed
   art, the way `make_ocean_tiles.py` does it, would be better.
2. Winning ends the run the same way losing does: the party and bag are wiped
   and the player is back on floor 1. Nothing is carried forward and nothing
   records that it happened, so there is no reason to have won rather than
   stopped. A completion counter is the obvious next thing.
3. A full party still loses the boss ace — there is no swap UI. It now says so
   instead of declining in silence, but being unable to take a Champion's ace
   because of a spare Zubat is still a bad moment.
4. The rest stop has only a nurse. It reuses `LAYOUT_POKEMON_CENTER_1F` and is
   ready for a mart and game corner.
5. The woods still gets none of the cosmetic passes — `DUNGEON_GEN_WOODS`
   returns before `ApplyWallAutotiling`, so patches, skirts and decor never run
   for it. Everything decorative it does have is inside `StampCell`. That is
   fine while the only variety is per-cell, but a woods theme wanting scattered
   props or ground patches would need the passes lifted out of the cave path.
6. **The jungle has no rain** — but the open question in this entry is answered
   and the pattern has been used twice, so this is now a table entry rather than
   a design decision.

   Weather is per-map for the same reason diving is: `GetCurrentMapType` and the
   weather loader read the ROM header by warp group and id, not `gMapHeader`, so
   the RAM patch that swaps tilesets per theme cannot reach it.
   `MAP_ROGUE_DUNGEON_FOG` and `MAP_ROGUE_DUNGEON_SNOW` are the result. Both
   share `LAYOUT_ROGUE_DUNGEON_FLOOR`, so `mapLayoutId` is identical and every
   dispatch keying on it is untouched, and both `scripts.inc` define nothing.

   They are named for the **weather**, not for Phoebe and Glacia, and that is
   the answer: a map per weather, shared by every theme that points
   `theme->mapId` at it. `MAP_ROGUE_DUNGEON_PETALS` is the third, for Ever
   Grande, and a rain map for the jungle would be the fourth — one `map.json`
   and an empty `scripts.inc` each.

   Every warp path picks the change up on its own, because they all go through
   one accessor that returns `ThemeForFloor(floor)->mapId`. That is why the
   five-paths problem underwater exposed only had to be solved once.

   **`WEATHER_SNOW` is worth knowing about.** `constants/weather.h` marks it
   `// Unused` and no vanilla map sets it, but it is not a stub: it is wired at
   `[WEATHER_SNOW]` in `sWeatherFuncs` with a full init/main/finish set, real
   art, and 16 sprites with two flake sizes, sine drift and screen wrap. What
   was cut is a pause-and-respawn cycle — `WaitSnowflakeSprite` is `UNUSED`, and
   `tFallCounter`/`tFallDuration`/`tDeltaY2` are written on every spawn and read
   by nothing. Nothing on the live path is broken.

   **And weather is not always only cosmetic.** `B_OVERWORLD_SNOW` is
   `GEN_LATEST`, so a battle on a snowing map sets `B_WEATHER_SNOW`: Ice types
   get ×1.5 Defense, Blizzard cannot miss, Weather Ball turns Ice, and Slush
   Rush / Ice Body / Snow Cloak come online. Gen 9 snow deals no chip damage
   where hail would, so it costs the player nothing passively — but **check what
   a weather does in battle before hanging it on a theme**, because rain, sun
   and sandstorm all reach `gBattleWeather` the same way.
7. The jungle's four sliver slots are never exercised: 3-wide corridors do not
   leave one-block-thick walls. They are filled with plain canopy, so the risk
   is low, but they are unvalidated and would show up if `corridorWidth` ever
   dropped back to 1.
