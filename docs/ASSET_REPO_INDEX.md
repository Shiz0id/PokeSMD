# Team Aqua's Asset Repo — index

Clone: `D:\PokemonTest\Team-Aquas-Asset-Repo` (Windows side, outside the decomp).
Source: `github.com/TeamAquasHideout/Team-Aquas-Asset-Repo`. 2.5 GB, 16,011 files.
Previews rendered by us: `D:\PokemonTest\tileset_previews`.

**Licence:** free to use *and edit* by default. Two conditions: attribution is
required (each asset carries its own `credits.md` / `README.md`, often listing
several artists), and individual assets may opt out of editing — check per
folder, do not assume from the top-level README.

**Organisation:** level 1 is asset type, level 2 is **creator**, not content. So
only the tilesets are findable by name; everything else needs a subject search.

**NAMES LIE — verify by rendering.** Three of three name-based picks were wrong:
"Shady Forest" is a village set, "Underwater Secondary" is sunken ruins, and
`gTileset_General` 0x024 (vanilla, for comparison) is gravel despite being the
only Hoenn arrow warp. Render or open before committing to anything.

## Top level

| dir | size | verdict |
|---|---|---|
| Battle Backgrounds | 47 MB | **highest value.** 118 PNGs, 256x512 indexed. Two creators ship "Pokeemerald ready to use" folders |
| Tilesets | 337 MB | 34 full decomp-format sets, see below |
| Audio | 121 MB | 443 `.mid` + **84 `.inc` voicegroups**, decomp naming (`mus_*`, `se_*`) |
| Overworld Trainer Sprites | 185 MB | 33 creators. **All Johto + Sinnoh leaders present** |
| Trainer Front Sprites | 24 MB | 27 creators. Same |
| Official Pokemon Assets | 135 MB | `HGSS_Front_Sprites.png`, `Gen3_Front_Sprites.png`, item icons, HGSS OW mons |
| Overworld Other Sprites | 5.7 MB | includes a **Berry trees** folder |
| Items | 2.9 MB | 4 creators |
| Field Effects | 512 KB | one `emotes.png` |
| User Interface | 24 MB | 9 creators + Fonts |
| Battle effects | 512 KB | 2 creators |
| Overworld Pokemon Sprites | 405 MB | we already ship 1.12 MB of these; replacement art, not new capability |
| Trainer Back Sprites | 277 MB | **trap** — back sprites were deliberately dropped in `8bd9b05f4f` |
| Other | 397 MB | 381 MB is `Pokemon LIFE`, a whole fan-game dump. Not decomp assets |
| Projects | 178 MB | FFVII sprites, a Zelda port. Novelty |
| Pokemon | 78 MB | custom mon sprites |
| Pokemon Essentials Packs | 93 MB | **RPG Maker**, wrong engine, needs conversion |
| Maps | 18 MB | one contributor |

## Tilesets — format

`Tilesets/The Great Tileset Exchange/Full Tilesets/`. Each is real decomp layout:
`metatiles.bin`, `metatile_attributes.bin`, `palettes/NN.pal`, `tiles.png`, often
`anim/`, plus `example.png` and `credits.md`. No sheet importing needed.

**THEY ARE TRIPLE-LAYER: 24 bytes per metatile, 12 tile entries in 3 layers.**
We are 16-byte dual-layer. Confirmed by rendering, not by the 12:1
metatiles:attributes ratio that first suggested it — a wrong stride still
renders *something*, so the ratio alone is not proof.

Conversion is lossless when a metatile populates at most **two** of its three
layers; only 3-layer metatiles force discarding art. Measured below.

They also ship 13–16 palettes (the full primary+secondary range); a secondary in
our build owns slots 7–12 only.

## Tilesets — census

`used` counts non-empty metatiles; `3L` is the lossy count. Sets are far sparser
than their nominal size, which matters against our 512-metatile secondary cap.

| tileset | total | used | 3L | lossy |
|---|---|---|---|---|
| **Underwater Reef Secondary** | 256 | 206 | **0** | **0%** — real seafloor: sand, rock, kelp, coral, anemones |
| **Underwater Secondary** | 256 | 184 | **0** | **0%** — sunken ruins, scenery not floor |
| **Underwater Primary** | 256 | 216 | **0** | **0%** |
| Beach Cave Secondary | 256 | 208 | 0 | 0% |
| Alternative Pokecenter Secondary | 256 | 96 | 0 | 0% |
| Gate Platinum Secondary | 128 | 52 | 0 | 0% |
| Gen 4 Interior Secondary | 512 | 416 | 0 | 0% |
| Hidden Grotto Primary FRLG | 512 | 207 | 0 | 0% |
| Emerald Slide | 512 | 504 | 0 | 0% — **no tiles.png**, cannot render |
| Gatehouse Secondary | 176 | 136 | 1 | 0.7% |
| Dojo Interior Secondary | 512 | 122 | 1 | 0.8% |
| Sewer Secondary | 256 | 128 | 1 | 0.8% |
| Sewer (Clear water) Secondary | 256 | 127 | 1 | 0.8% |
| Pyramid Interior Primary | 208 | 126 | 1 | 0.8% |
| Gen 4 Cave Secondary | 256 | 110 | 1 | 0.9% |
| Distortion World Secondary | 512 | 214 | 2 | 0.9% |
| Gatehouse Secondary Alt | 256 | 180 | 4 | 2.2% |
| Pyramid Interior Secondary | 512 | 33 | 1 | 3.0% |
| Space Meteor Secondary | 256 | 150 | 5 | 3.3% |
| Desert Village Secondary | 512 | 207 | 7 | 3.4% |
| Legend of Zelda House Secondary | 512 | 82 | 3 | 3.7% |
| Caves Alt Primary | 1024 | 479 | 24 | 5.0% |
| Small town with lab Secondary | 256 | 232 | 12 | 5.2% |
| Dojo Exterior Secondary | 512 | 154 | 9 | 5.8% |
| Lugia Movie Altar Secondary | 512 | 94 | 7 | 7.4% |
| Volcano Secondary | 256 | 170 | 16 | 9.4% |
| Brick City Secondary | 375 | 336 | 34 | 10.1% |
| Brick Cafe Interior Secondary | 512 | 284 | 41 | 14.4% |
| Caves Alt Secondary | 1024 | 983 | 204 | 20.8% — also over the 512 cap |
| Shady Forest Secondary | 512 | 186 | 47 | 25.3% — **a village**, not a forest floor |
| Desert Primary | 512 | 346 | 89 | 25.7% |
| Little Office Interior Secondary | 120 | 105 | 80 | 76.2% |
| Autumn Ruins Secondary | 512 | 512 | 511 | 99.8% — effectively a rewrite |
| Desert Pyramid Exterior Secondary | 512 | **0** | 0 | empty, a dud |
| LeoB ORAS | — | — | — | no `metatiles.bin` |
| Valencia Island | — | — | — | no `metatiles.bin` |

Aggregate is 14.5% over 7,585 used metatiles, but **no tileset sits near 14%** —
it is two populations, mostly-zero and a few heavy users. Do not use the mean.

## What maps onto our open gaps

| our gap | candidate | state |
|---|---|---|
| Underwater has no one-block-thick wall art | **Underwater Reef Secondary** | 0% lossy, the one survivor of three name-based picks |
| 14 themes share one battle backdrop (**not in the state file**) | Battle Backgrounds | cave/snow/sand/water/underwater/long_grass all present |
| Johto + Sinnoh leaders "do not exist at all" (gap 16) | trainer sprites, both dirs | all 17 present; only parties and classes still needed |
| woods floor patch | ~~Shady Forest~~ | **rejected** — village set, and 25.3% lossy |

## Battle backdrops — how selection works, and what is staged

`BattleSetup_GetEnvironmentId()` (`src/battle_setup.c:715`) derives the backdrop
from the **metatile behaviour under the player** plus `gMapHeader.mapType`, and
nothing else. Every dungeon floor is `MAP_TYPE_UNDERGROUND`, so the whole run
collapses to `CAVE`, or `GRASS`/`LONG_GRASS` on an encounter surface and `POND`
on the water themes. Fourteen themes, about four backdrops.

**Row format** — `struct BattleEnvironment` in `include/battle_environment.h`,
table in `src/data/battle_environment.h`. The parts that matter for a new one:

```c
.entry      = ENVIRONMENT_ENTRY(Building),   // the intro slide-in graphics
.background = ENVIRONMENT_BACKGROUND(Cave),  // tileset + tilemap
.palette    = gBattleEnvironmentPalette_Cave,
```

`ENVIRONMENT_BACKGROUND(X)` expands to `gBattleEnvironmentTiles_X` /
`...Tilemap_X`, so **art is shared by name and varied by palette** — the whole
Elite Four is one `Stadium` background with five palettes. That is the cheap
axis: a recolour is a palette, not a tileset.

`LoadBattleEnvironmentGfx` clamps anything past the table to
`BATTLE_ENVIRONMENT_PLAIN`, so appending a row is safe and an out-of-range id
degrades rather than corrupting VRAM.

**DONE: the per-boss backdrops vanilla already ships are now used.**
`sDungeonBossEnvironment[]` in `src/rogue_dungeon.c`, parallel to
`sDungeonBosses`, hooked into `BattleMainCB2`. Eight leaders take
`BATTLE_ENVIRONMENT_LEADER`, the Elite Four take their own stadium palettes,
Wallace and Steven take `CHAMPION`. **Zero new art** — all of it verified
present in `PokeSMD.map`, +88 bytes of ROM, RAM unmoved.

### DONE: three backdrops in, and the converter that made them

`tools/rogue/png_to_battle_bg.py` turns a flat 256x512 indexed PNG into the
`tiles.png` / `map.bin` / `palette.pal` trio. **The step I flagged as unmeasured
turned out to be ordinary** — a tilemap is a deduplication, not a re-layout, and
the build compresses the trio to `.4bpp.smol` / `.smolTM` itself.

| theme | floors | source | unique tiles |
|---|---|---|---|
| Cave (dungeon 2) | 11-20 | `BG_Cave` | 427 |
| Fiery Path | 31-40 | `BG_Cave_Scalding` | 382 |
| Glacia | 91-100 | `BG_Cave_TormaDepths` | 320 |

All three share a silhouette — CFRU drew them as one cave at three temperatures
— so the run reads as a single cave system rather than three rooms. **Glacia the
BOSS is overridden to hers too**, the only boss whose vanilla backdrop is
replaced.

**The verifier and the repack pass are the parts worth knowing about.** Torma
Depths had **128 tiles spanning two 16-colour blocks**, which the hardware cannot
draw; the round-trip check caught it and refused to write. The cause was an
export artefact rather than an art problem — twelve distinct colours spread over
a 48-entry palette, with index 47 pure black when index 0 already is. Where every
colour fits one block, `repack()` rebuilds the palette from distinct RGB and
remaps every pixel, losslessly.

**`BG_Snow` genuinely exceeds fifteen colours**, so repack declines and the
straddling check still refuses it. Real multi-palette art needs per-tile palette
assignment, which the tool does not attempt — **budget more than a conversion for
Snow and anything like it.**

### Attribution for what is actually shipped

Every background in the ROM, and who to credit. Kept here rather than trusting
each source folder, because one of them is not covered by the folder it sits in.

| shipped as | source | artist |
|---|---|---|
| `scalding_cave` | `CFRU/BG_Cave_Scalding.png` | CFRU, see `CFRU/Credits.md` |
| `rogue_cave` | `CFRU/BG_Cave.png` | CFRU |
| `frozen_depths` | `CFRU/BG_Cave_TormaDepths.png` | CFRU |
| `murky_depths` | `Leob0505/building.png` | Leob0505 |
| `deep_woods` | `Leob0505/forest_from_cfru.png` | Leob0505 |
| `open_ocean` | `Leob0505/water.png` | Leob0505 |
| `open_plain` | `Leob0505/plain.png` | Leob0505 |

**NOT SHIPPED, and not from the repo at all:**
`Leob0505/battlebgjungle_by_aveontrainer_dd2p5b8.png` is **aveontrainer's**, from
DeviantArt, free to use **with attribution required**. It was dropped into the
Leob0505 folder by hand, so that folder's `Credits.md` does NOT cover it and a
re-clone or `git pull` of the asset repo would lose or orphan it. **Move it out
of the clone before relying on it.**

### The jungle art, and why it is not in yet

Measured rather than guessed. The geometry is fine: a clean 2x downscale of its
512x288 gives 256x144, and dropping the bottom 32 rows leaves exactly the 112-row
art band, with the loss falling behind the message box.

Colour is the blocker. 152 distinct colours against a ceiling of 48, and 48 means
**three banks of 16 with every 8x8 tile drawing from ONE bank**. Median cut
respects the total and ignores the per-tile rule, so:

| | |
|---|---|
| quantise to 48 | 18% of pixels change - acceptable |
| quantise to 16 | 66% change - visibly muddied |
| **48, naive banking** | **353 of 448 tiles straddle two banks (79%)** |

`png_to_battle_bg.py` refuses this, correctly - same category as `BG_Snow`, worse
degree. What it needs is a **per-tile palette assignment pass**: cluster tiles by
colour usage, assign each a bank, quantise within. That is a real algorithm, and
it would also unlock `BG_Snow`, the harder half of the CFRU set, and any future
commissioned art that does not arrive pre-banked. **Today the converter can only
take art that is already GBA-shaped, which is a live constraint on sourcing.**

### The rest of the CFRU set

Seven are already decomp-shaped in `CFRU/Pokeemerald ready to use/` (cave,
long_grass, pond_water, rock, sand, tall_grass, water) with bonus
`palette_morning.pal` / `palette_night.pal`. The other 41 go through the
converter. Leob0505 ships ten more ready-to-use.

### Original notes: what Scalding Cave needed

Source: `Battle Backgrounds/CFRU/BG_Cave_Scalding.png`, 256x512 indexed.

1. Split the PNG into a tileset and a tilemap, then compress both the way
   `ENVIRONMENT_BACKGROUND` expects, emitting
   `gBattleEnvironmentTiles_ScaldingCave` and `...Tilemap_ScaldingCave`.
   **This is the unmeasured step** — CFRU ships a finished image, not the
   tiles/tilemap pair the macro names, so something has to do that split.
2. Palette to `gBattleEnvironmentPalette_ScaldingCave`.
3. Append `BATTLE_ENVIRONMENT_SCALDING_CAVE` to `enum BattleEnvironments` and a
   row to `gBattleEnvironmentInfo`, reusing `ENVIRONMENT_ENTRY(Cave)` and the
   cave nature-power / camouflage constants.
4. Add a `battleEnvironment` field to the theme table and point Fiery Path at
   it, defaulting to the sentinel so every other theme is unchanged. The boss
   override already in `BattleMainCB2` is where a theme override would join.

**Check step 1 before promising the rest is cheap.** Every other CFRU terrain
is the same shape, so whatever splits one splits all forty-eight.

## Pointers

- `Tilesets/Other Tilesets/` — 6 more collections, uncensused
- `Tilesets/The Great Tileset Exchange/Individual Tiles/` — 4 creators, uncensused
- Wiki (feature branches, tutorials) is on the GitHub repo, not in the clone
