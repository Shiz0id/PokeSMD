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
| `setup_dungeon_map.py` | the dungeon map's layout and `map.json` scaffolding |

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

`make_glacia_snow.py` writes tiles into the cave sheet but **not** metatiles:
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
| `check_encounter_flags.py` | every theme can spawn a wild Pokemon under its **own** tileset, and no `*_METATILE_*` constant is named inside a function |
| `check_starter_moves.py` | what each starter actually holds at `DUNGEON_STARTER_LEVEL`, and that none ends up with only status moves |
| `verify_run_structure.py` | that every floor maps to one dungeon now the Elite Four's are half length, that bosses land on the intended floors, and that the level curve still fits each boss's party — read live out of `trainers.party` |
| `woods_prototype.py` | renders generated woods floors; mirrors `StampCell`, including the tree base row, the long grass base and the crown above a canopy |

Host-side verification over thousands of seeds is much cheaper than emulator
testing and catches different bugs. Prefer it.
