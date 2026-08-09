# Roguelike tooling

Host-side Python for the procedural dungeon. See `docs/ROGUELIKE.md` for what
these are used for and why.

## Running them

Scripts resolve the repo from their own location, so they work from either side
of the WSL boundary. **Which side you need depends on Pillow:**

- **WSL has no Pillow** and no `python3-venv`. Anything that touches an image
  has to run from Windows against `//wsl.localhost/Ubuntu/...`.
- Everything else runs fine under WSL with `python3 tools/rogue/<script>.py`.

| needs Pillow (run from Windows) | pure text/binary (either side) |
|---|---|
| `tileset_atlas.py`, `build_all_atlases.py` | `tileset_resolve.py` |
| `compose_metatiles.py`, `make_woods_stairs.py` | `append_metatiles.py` |
| `make_ocean_tiles.py`, `make_underwater_tiles.py` | |
| `whirlpool_art.py` (writes PNGs) | |
| `woods_prototype.py` | `gen_trainer_table.py`, `gen_starters.py` |
| | `ram_budget.py`, `derive_wall_autotile.py` |
| | `verify_*.py`, `validate_maps.py` |

Renders go to `_out/`, which is gitignored.

## What each one does

**Generators — these write checked-in files.** Re-run them rather than editing
their output by hand; all are idempotent.

| script | writes |
|---|---|
| `gen_trainer_table.py` | `include/constants/rogue_dungeon_trainers.h` |
| `gen_starters.py` | `include/constants/rogue_dungeon_starters.h`, `starters.inc` |
| `make_woods_stairs.py` | woods stairs art into `data/tilesets/secondary/rustboro/` |
| `make_fiery_slivers.py` | composed 1-wide wall metatiles into `data/tilesets/secondary/lavaridge/` |
| `make_mirage_slivers.py` | composed 1-wide wall metatiles into `data/tilesets/secondary/mirage_tower/` |
| `make_ocean_tiles.py` | everything the ocean adds to `data/tilesets/secondary/mossdeep/` |
| `make_underwater_tiles.py` | the seafloor's whirlpool into `data/tilesets/secondary/underwater/` |
| `compose_metatiles.py` + `append_metatiles.py` | cave sliver metatiles into `data/tilesets/secondary/cave/` |
| `make_glacia_snow.py` | Glacia's snow floor, drift and ice rock tiles into `data/tilesets/secondary/cave/` |
| `make_evergrande_tiles.py` | the flower dungeon's exit and its sixteen encounter-flowers into `data/tilesets/secondary/ever_grande/` |
| `make_flower_fldeff.py` | the flower dungeon's wade-through overlay into `graphics/field_effects/` — derived from `long_grass.png`, so it is the one generator here whose input is another checked-in asset |
| `make_petal_weather.py` | `WEATHER_PETALS`' two 8x8 frames and its palette into `graphics/weather/` |
| `make_victory_road_palettes.py` | the four Victory Road palette sets into `data/tilesets/secondary/rogue_victory_road_*/` |
| `import_tile_sheet.py` | a whole secondary tileset — tiles, metatiles, attributes and palettes — composed from one or more sheets in `tools/rogue/sheets/`; currently `rogue_lapis_cave/`, `rogue_howling_jungle/` and `rogue_murky_cave/` |
| `setup_dungeon_map.py` | the dungeon map's layout and `map.json` scaffolding |
| `setup_safari_maps.py` | the six Safari Zone maps — `data/maps/RogueSafari*/`, plus their entries in `map_groups.json`, `event_scripts.s`, `wild_encounters.json` and the rest stop's warp 2. They **share vanilla's Safari layouts** and differ in the header; the field that matters is `map_type`, because vanilla's `MAP_TYPE_ROUTE` would let a run Fly or Teleport out of itself |
| `gen_safari_pool.py` | `include/constants/rogue_safari_pool.h` — the Safari ladder, computed as every enabled species the run cannot obtain from a theme pool, a starter, or an evolution of one. `--report` first: the BST order is a first pass and BST lies. It **refuses to emit** if any stat fails to resolve or if any species is missing from `pokeemerald.map` |
| `make_rest_stop.py` | the way-station's `map.bin`, `border.bin` and layout entry, walled by a transcription of `ApplyWallAutotiling`. The plan's `G` and `S` are the two alcoves — game room west, Safari Zone east — and share one door metatile |
| `make_game_room.py` | the game room's `map.bin`, `border.bin` and layout entry, assembled from rectangles lifted out of `MauvilleCity_GameCorner` |
| `warp_tiles.py` | the way-station's two door metatiles into `data/tilesets/secondary/rogue_murky_cave/`, and the check that a map's warps can fire |
| `check_species_in_rom.py` | **every species named anywhere is actually in the ROM.** The generation config disables whole families and a disabled family still *compiles* — it becomes a zeroed `species_info` row that a wild encounter or a prize table rolls into — so `species_info` proves nothing and neither does a clean build. This reads `pokeemerald.map` and requires a `gMonFrontPic_*` per species, which makes it a **post-build** check. Run with no arguments for the default target list: the generated trainer parties, the roguelike's pools, ladder and starters, and the gacha prize table. Follows form-species aliases through `species.h` (`SPECIES_CASTFORM` → `gMonFrontPic_CastformNormal`) and strips comments, because a note *about* a missing species is not a use of one |
| `port_sprite_sheets.py` | rewrites one game corner minigame's `gDecompressionBuffer` idiom to `LoadCompressedSpriteSheet`. The expansion deleted that global scratch buffer, and each of the nine games open-codes the decompress-then-`LoadSpriteSheet` dance 1–17 times. It **refuses to write** if a site's hand-written `.size`/`.tag` disagree with what the `struct CompressedSpriteSheet` declares, because the engine call takes both from the struct — that refusal was verified by corrupting one of each. Dry runs by default; `--write` applies. Delete this once all nine games are ported |
| `emit_bw_anim.py` | a BW animated sprite per species — the frame sheet into `graphics/pokemon/<name>/bw_anim.png`, the table into `src/data/rogue_bw_anim.h`, **and the build rules into `graphics/pokemon/bw_anim_rules.mk`**. That last one is why it writes a makefile at all: a frame container needs the frame size in bytes and the frames per chunk, neither of which is recoverable from a flat 4bpp file, so each species needs an explicit rule. `--chunk` sets the granularity; see the container section of `roguelike-architecture.md` before changing it from 4 |
| `check_dungeon_objects.py` | the object event count of **every** `map.json` sharing `LAYOUT_ROGUE_DUNGEON_FLOOR`, against `DUNGEON_MAX_TRAINERS + DUNGEON_MAX_ITEMS + DUNGEON_MAX_BERRIES`. The map list is **derived** from the layout rather than written down: it used to name two maps by hand, and the three weather maps added later passed the check by not being in it — spawning no item balls and no berry trees for as long as they existed |
| `check_safari_pool.py` | the two Safari ladders: every rung reachable somewhere in the run, **`width > slots` on every floor** so the per-roll re-deal never stops firing, sorted weakest to strongest, no duplicates. That second one is the quiet failure — if the window ever narrows to the slot count the area silently becomes twelve species a visit |
| `verify_loot_table.py` | that `sLootConsumables` heals harder the deeper a run goes, by floor, per ball and per floor |

**`setup_dungeon_map.py` is scaffolding, not an owner.** It writes
`object_events: []`, so re-running it now would silently drop every trainer and
item ball slot the dungeon maps declare. `check_dungeon_objects.py` is what owns
that count, and it will say so — run it after touching either dungeon map.

The tileset writers (`make_woods_stairs`, `make_fiery_slivers`,
`make_mirage_slivers`, `make_ocean_tiles`, `make_underwater_tiles`,
`make_evergrande_tiles`, `compose_metatiles`+`append_metatiles`) **edit vanilla
asset files**. Pulling upstream changes to those tilesets means taking
upstream's file and re-running the script, not merging.

`make_evergrande_tiles.py` is the **only** appender to `gTileset_EverGrande`,
by construction rather than by convention: it stays idempotent by truncating
everything past the vanilla 168 metatiles and rewriting the tail, so a second
appender's entries get silently wiped the next time it runs. Anything needing a
new Ever Grande metatile adds itself there. Same rule as `append_metatiles.py`
has for the cave, and for the same reason.

`warp_tiles.py` is the same arrangement for `gTileset_RogueMurkyCave`, with one
extra wrinkle: `import_tile_sheet.py` owns the first 150 metatiles of that file
and rewrites them from the sheet, so **re-importing murky drops the doors**.
Re-run `warp_tiles.py --write` (or `make_rest_stop.py --write`, which calls it)
afterwards. The behaviour check will say so either way.

**A `warp_event` does not warp on its own.** The engine fires it only when the
metatile under it has a warp *behaviour*, so a warp on plain floor is dead and
nothing complains — that is how the way-station, its game room and the way out
of the game room all shipped unreachable at once. `warp_tiles.py` checks every
warp in both maps against the tile beneath it, and `make_rest_stop.py` and
`make_game_room.py` run that check on `--write`.

`import_tile_sheet.py` is the odd one out: it **owns** its output directory
rather than editing a vanilla one, and rewrites every file in it on each run, so
nothing else may append there. It reads the sheet's own Legend column — a
neighbour mask per cell — and matches it against the twenty `PaintWalls` slots,
which is why Lapis needed no autotile mining at all. See §5 of `ROGUELIKE.md`.

Its `TILESETS` table lists blocks, each naming its own **sheet** and column, so
one tileset can be composed from several rips — Glacia's is Lapis Cave's walls
over Mt. Freeze's snow. Three rules come with that: blocks sharing a palette slot
are quantised **together** against one 15-colour palette; the block order is
the **metatile order**, so appending a block is safe and reordering one silently
renumbers every id after it; and a block declaring `varies` is a column of
alternates paired to the base block **by legend position**, which is what emits
the `RogueDecor` entries.

Two more block keys. **`autotile`** gives any block the wall block's nine-mask
treatment, which is what a water body laid as a patch region needs. **`over`**
flattens a block onto another's plain fill before any colour is read, because
these sheets draw edge cells with transparent corners meant to sit on the
terrain they border. And a **`graft`** block is art on no sheet at all,
assembled from vanilla tiles and recoloured into this tileset's palettes — the
jungle's long grass, which cost no pixel art. Grafts run after the sheet
palettes exist, since those are what they draw their colours from.

A **`stairs`** block is the descent, drawn from the theme's own colours instead
of borrowing `gTileset_General`'s grey warp. It runs when the grafts do and for
the same reason. Three things about it are load-bearing:

- Its `roles` name **RGB values that must already be in the palette**, and it
  raises if one is not. Binding the exit by index, or by nearest match, would
  let it land silently on the wrong colour.
- `append` adds colours to a palette that has headroom — Lapis' snow has no dark
  at all, so its mouth is lifted verbatim out of the crystal walls' palette.
  Appending only ever writes past the end, so existing indices never move.
- **`stairs` blocks go last in the list.** Their metatile has to land after every
  id a theme table already names, and the jungle's has to run after its graft
  because it draws in the palette that graft writes.

`rounded` is the only shape control: it opens the four corners back up to the
floor. Murky Cave is the one that sets it false, because it is the one tileset
that reads as built. See §5 of `ROGUELIKE.md` for why the jungle's stairs are
drawn in its long-grass palette rather than in its floor's.

**A palette that does not fit is reduced, not refused.** A block wanting more
than fifteen colours has its lowest-error pairs merged — `distance × min(count)`
— until it fits, and **every merge is printed**. Read that output: it is the
only notice that art lost a colour. Murky Cave's walls arrive at seventeen.

**`attr` is a design decision, not boilerplate.** It is the behaviour copied from
a vanilla donor, and the ground default (`0x0008`, the cave's `MB_CAVE`) carries
wild encounters. The jungle's ground is `0x0000` because that theme's encounters
come from its long grass and its floor is meant to be safe to cross — taking the
default would have turned battles on across the whole floor as a side effect of
changing the art. Read the attributes back out of the written `.bin` when it
matters; nothing else checks a decor block against the floor it replaces. It prints the `#define`s it would write; diff them
against `rogue_dungeon.h` after any change to the table rather than assuming
ids held still. It also prints the tile count, which has to be copied into the
`-num_tiles` argument in `graphics.h` — `-Wnum_tiles` makes `gbagfx` check it.

`make_glacia_snow.py` wrote Glacia's snowfield, which **nothing uses any more** —
she moved to the imported Lapis Cave tileset, orphaning the snow art along with
`LAYOUT_ROGUE_DUNGEON_VRGLACIA` and `gTileset_RogueVictoryRoadGlacia`. **Kept
deliberately, not pending removal** — the whole orphan is ~5.1 KB of a ROM with
6.9 MB free, it moves neither RAM figure, and the tiles sit in the cave sheet
that five live tilesets share. The measurement is under "Settled" in the skill's
`roguelike-state.md`; do not re-derive it. It writes tiles but **not**
metatiles:
`append_metatiles.py` is the only appender to that tileset and imports the snow
entries, because it stays idempotent by truncating everything past the vanilla
414 and would wipe a second appender's work. Run `make_glacia_snow.py --write`
first, then `append_metatiles.py`. Its `--preview` needs Pillow; `--write` does
too, since it edits a PNG.

`make_victory_road_palettes.py` is the exception that does not touch a vanilla
asset at all: it writes only into directories of its own, because the tilesets
it feeds share Cave's tiles and metatiles and add nothing but palettes. `--preview` renders every tone side
by side on real cave art at 2x and 5x and needs Pillow; `--write` is plain text
and runs anywhere. Re-run `--write all` after changing a tone, then rebuild the
atlas.

`whirlpool_art.py` is shared by the last two rather than being a generator
itself: two themes descend through the same vortex on different tilesets, so the
shape lives in one place and each caller supplies its own palette roles.

**Reference and analysis — read-only.**

| script | purpose |
|---|---|
| `tileset_resolve.py` | `gTileset_*` → real asset paths. Never guess these; see `docs/ROGUELIKE.md` §8 |
| `tileset_atlas.py` | render metatiles, build labelled contact sheets |
| `build_all_atlases.py` | atlas + JSON index for all 137 tileset pairs |
| `render_layout.py` | render a whole vanilla layout, and census its metatiles by collision |
| `sheet.py` | render a chosen list of metatiles, big and labelled |
| `derive_wall_table.py` | tally which metatile vanilla uses per open-neighbour mask, with confidence |
| `derive_wall_autotile.py` | the earlier cave-only version, keyed on wall neighbours |
| `tree_edges.py`, `woods_details.py` | how vanilla ends a tree mass and a grass patch |
| `wall_autotile_4bit.json` | the derived cave wall table |

**Adding a theme.** `docs/ROGUELIKE.md` §7 is the full recipe; these are the two
tools it turns on:

| script | purpose |
|---|---|
| `theme_mock.py` | render a candidate theme table through a transcription of `PaintWalls()`, `ApplyFloorShading()` and `ApplyWallDecor()`, before writing any C |
| `add_theme_layout.py` | create the donor layout and append it to `layouts.json` |

**Verification — run these after touching the generator.**

| script | checks |
|---|---|
| `verify_dungeon_gen.py` | connectivity, reachability, no out-of-bounds writes |
| `verify_seeding.py` | determinism, and that floors do not correlate |
| `validate_maps.py` | the map.bin codec against vanilla layouts |
| `check_encounter_flags.py` | every theme can spawn a wild Pokemon under its **own** tileset; that its `mapId` — **and every map `sFloorMapOverrides` swaps in for its last floors** — is registered in `wild_encounters.json` with a full-length table for the branch its surface actually reads; and no `*_METATILE_*` constant is named inside a function |
| `check_starter_moves.py` | what each of the 30 picks actually holds at `DUNGEON_STARTER_LEVEL`, and that every one has a damaging move **of its own type** — the weaker "not only status moves" version of this passed a Clefairy armed with 20 BP of Stored Power. Models the three-step grant in `RogueDungeon_GiveChosenStarter` (known → free slot → displace a status move only if otherwise unarmed) and reads move *category*, which is what the C reads and is immune to `.power` being a ternary |
| `eeveelution_stats.py` | base stats, typing and evolution method for Eevee and its eight branches, for the same reachability question asked of the Clefairy and Pikachu lines. Reports, not a check. Resolves the `P_UPDATED_STATS` ternaries and the named stat macros, which a bare `\d+` pattern gets wrong on most of these entries |
| `check_bare_warps.py` | that no script calls `warp` (or any other `formatwarp` command) with just a map. The dummy `-1` coords go through `VarGet(0xFFFF)`, which indexes `gSpecialVars[32767]` on a 22-entry array and dereferences whatever it finds — so the player lands on a junk tile outside the map, in an endless field of border metatile, with the music playing. Stable per build and flips when an unrelated script moves those bytes, which is exactly how it bit the post-boss rest stop warp. `warphole` is excluded: it takes no coords and its bare vanilla call sites are correct |
| `check_dungeon_names.py` | that every `MAPSEC_ROGUE_*` sits before `MAPSEC_NONE` **and has a `gRegionMapEntries` row** — that array is unsized and `GetMapName` bounds on `MAPSEC_NONE`, so a missing row is an out-of-bounds read, not a build error — that all 14 themes set `.mapSecId` (0 would silently mean `MAPSEC_LITTLEROOT_TOWN`), and that no name exceeds `MAP_NAME_LENGTH` or is wider in pixels than the widest name vanilla itself ships |
| `check_continue_labels.py` | that no label on the save-select screen collides with its own value. The main menu draws label at a fixed x and value right-aligned to a fixed x with nothing between them checking, so an over-long label silently draws under the number and is only visible by booting the ROM. Measures real glyph widths **indexed by the charmap byte, not by ASCII** — `'A'` is `0xBB`, and indexing by `ord()` reads unrelated glyphs and reports a 6-character label as 23px |
| `verify_run_structure.py` | that every floor maps to one dungeon now the Elite Four's are half length, that bosses land on the intended floors, that the level curve still fits each boss's party — read live out of `trainers.party` — and that each `sFloorMapOverrides` entry covers a contiguous run of exactly `lastFloors` floors ending on its dungeon's boss floor |
| `check_species_pools.py` | that every species in a theme's pool can actually be rolled, that no pool is too short to fill `DUNGEON_ENCOUNTER_WINDOW`, and that no pool repeats a species. A pool is read through a sliding window ending at a tier that climbs with the floor **within** the dungeon, so a pool longer than the deepest tier its dungeon reaches has a tail nothing can roll, and one shorter than the window repeats species across a floor's slots. Neither fails to build and neither is visible without counting — while the ramp keyed off the **absolute** floor instead, 63 curated species across the fourteen themes could never appear, eleven of them in Underwater alone. Broken three ways on purpose to confirm it fires |
| `woods_prototype.py` | renders generated woods floors; mirrors `StampCell`, including the tree base row, the long grass base and the crown above a canopy, and now `ApplyDecor` + `DecorHash` — `decorRarity` is the only knob and what it controls is a density, which is a thing to count rather than eyeball a floor at a time |
| `rom_budget.py` | where the ROM is going, out of `pokeemerald.map`. The companion to `ram_budget.py`, and newly needed now ROM is 91% rather than 79%. Reports by category, by object, or by symbol prefix. Two traps it exists to avoid: the `.gba` is always exactly 32 MB because `gbafix -p` pads to a power of two, and an object's size is not its ROM cost because assets are `INCBIN`'d into whatever references them — so attribution is by symbol, sized by the distance to the next one |
| `check_frame_containers.py` | that every BW frame container round-trips to the exact frame stack it was built from, at four chunk sizes, and that its header agrees with the generated `sBwAnims` table. Needs no Pillow — frames stack on whole tile rows, so in 4bpp each is a contiguous run and `gbagfx` plus `compresSmol` are enough. This is what caught the five-bit `MODE_MASK` against a four-bit mode field, which broke every container with an **odd** chunk count and left even ones perfect. `test/compression/smol.c` covers the same ground on the emulator for two species |

Host-side verification over thousands of seeds is much cheaper than emulator
testing and catches different bugs. Prefer it.
