# Procedural roguelike — implementation notes

A Mystery-Dungeon-style roguelike built on pokeemerald-expansion. Floors are
generated at runtime; the run mirrors the stock game's progression.

Everything here was established by reading the engine or mining vanilla data.
Where a number appears, it was measured. Where something is called a trap, it
cost real time to find.

---

## 1. Current state

**The run loop is complete.** Pick two starters → descend → catch and build a
team → mini boss every 5 floors → gym leader every 10 → optionally adopt the
boss's ace → heal at a rest stop → next dungeon. Losing wipes the run.

- **13 dungeons × 10 floors = 130 floors.** Eight gym leaders, then Sidney,
  Phoebe, Glacia, Drake, Wallace.
- **Five themes**: Petalburg Woods (dungeon 1, Roxanne), Granite Cave
  (dungeon 2, Brawly), New Mauville (dungeon 3, Wattson), Fiery Path
  (dungeon 4, Flannery) and Mirage Tower (dungeon 5, Norman). They cycle past
  that until more exist.
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

Grass: `0x001` plain (no encounters), `0x00D` tall, `0x015` long, with
`0x016/0x017` as the long grass base row.

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

**Only two themes have a region set**, and this is worth checking before
designing one: Fiery Path's floor variety is `0x310`/`0x311`, which are already
decor, and New Mauville's apparent region was its existing skirt tiles. The
woods has none at all — and it uses `DUNGEON_GEN_WOODS`, which returns before
`ApplyWallAutotiling` and so never reaches any of these passes. What the woods
actually wants is a **tall grass base row** (`0x1C6`/`0x1C7` over
`0x1CE`/`0x1CF`), the same 2-wide unit idea as the long grass base it already
draws.

**All three passes are position-hashed, not drawn from the dungeon RNG.**
`WriteFloorBlocks` can repaint a floor without `PrepareFloor` having run again,
so consuming RNG there would leave the state dependent on how the player arrived
and a floor would redecorate itself on re-entry.

---

## 8. Tooling

All of it lives in **`tools/rogue/`** — see `tools/rogue/README.md` for the
per-script table and which ones write checked-in files.

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
- Long shell heredocs break on apostrophes in prose. Write patch scripts to a
  file, or use the editing tools directly.
- `Path.write_text` on Windows emits CRLF into repo files. Pass `newline='\n'`.

---

## 10. Known gaps

1. Nothing happens after floor 130 — the boss table wraps to Roxanne.
2. Five themes against thirteen dungeons, so they cycle past dungeon 5.
3. A full party silently declines a boss ace — no swap UI. Being unable to take
   a Champion's ace because of a spare Zubat is a bad moment.
4. The rest stop has only a nurse. It reuses `LAYOUT_POKEMON_CENTER_1F` and is
   ready for a mart and game corner.
5. The woods still has no tall grass base row, so a patch of tall grass ends
   flat where it meets open ground. `0x1C6`/`0x1C7` over `0x1CE`/`0x1CF` is the
   2-wide unit vanilla uses, the same idea as the long grass base already
   drawn — and it is the only one of these floor treatments the woods can take,
   because `DUNGEON_GEN_WOODS` returns before `ApplyWallAutotiling` and so
   reaches none of the patch, skirt or decor passes.
