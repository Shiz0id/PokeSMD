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
- **Seven themes**: Petalburg Woods (dungeon 1, Roxanne), Granite Cave
  (dungeon 2, Brawly), New Mauville (dungeon 3, Wattson), Fiery Path
  (dungeon 4, Flannery), Mirage Tower (dungeon 5, Norman), the Jungle
  (dungeon 6, Winona) and the open Ocean (dungeon 7, Tate and Liza). They cycle
  past that until more exist.
- The ocean is the only theme the player crosses **surfing**, which is a
  property of its floor metatile rather than a special case — see §7.
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

**This edits vanilla asset files** (`data/tilesets/secondary/cave/*.bin`), so
pulling upstream changes to that tileset needs care: take upstream's file, re-run
the script.

**Limits:** splicing only works when the source tiles have transparency. The
cave-mouth metatile is opaque across the full 16×16, so the woods stairs needed
genuinely new art.

### When splicing is not enough: drawing new tiles

`make_woods_stairs.py` draws the woods stairs (`0x35E`). The parts worth reusing:

- **Free slots have to be measured, not assumed.** A tile slot is free only if
  no metatile in the tileset references it *and* its pixels are blank. For
  Rustboro that is 220 of 512 tile slots, plus 161 metatile slots after this
  one. `gTileset_General` is full at 512/512, so new art goes in a secondary.
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

### Art that moves: the ocean's whirlpool

The ocean's way down is a whirlpool (`make_ocean_tiles.py`, `0x3D1`). It is the
second piece of genuinely new art in the project and the first that animates, so
it adds a few rules to the list above rather than replacing it.

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

The goal is 10+ more themes to reach the Champion. New Mauville was the first
one added after the abstraction existed, and it needed **no generator changes
and no engine changes** — a table entry, a donor layout, and a species pool.

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

1. Seven themes against fourteen dungeons, so they cycle past dungeon 7. The five
   Elite Four dungeons are only five floors long, which makes the cycling more
   visible, not less — a theme now gets half as long to make an impression.
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
6. **The jungle has no rain.** Route 119 and 120 are rainy in vanilla and the
   theme is built to be, but weather is per-map and all six themes share one
   map — only `gMapHeader.mapLayout` is swapped, and `mapLayoutId` is left alone
   deliberately because every dispatch keys on it. So per-theme weather needs a
   different hook than the tileset swap uses, and how `gMapHeader.weather`
   survives our warp path has not been checked yet.
7. The jungle's four sliver slots are never exercised: 3-wide corridors do not
   leave one-block-thick walls. They are filled with plain canopy, so the risk
   is low, but they are unvalidated and would show up if `corridorWidth` ever
   dropped back to 1.
