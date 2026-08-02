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
| `compose_metatiles.py` + `append_metatiles.py` | cave sliver metatiles into `data/tilesets/secondary/cave/` |
| `setup_dungeon_map.py` | the dungeon map's layout and `map.json` scaffolding |

The tileset writers (`make_woods_stairs`, `make_fiery_slivers`,
`compose_metatiles`+`append_metatiles`) **edit vanilla asset files**. Pulling
upstream changes to those tilesets means taking upstream's file and re-running
the script, not merging.

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
| `woods_prototype.py` | renders generated woods floors |

Host-side verification over thousands of seeds is much cheaper than emulator
testing and catches different bugs. Prefer it.
