# Pokémon Shattered Mystery Dungeon — features and expansions

The complete feature inventory for `roguelike/vertical-slice`, current as of
`33f7579be2` (2026-08-20). ROM builds as **`PokeSMD.gba`**, GBA header title
`SHATTERED MD`, game code still `BPEE` so existing `.sav` files keep working.

This is the *what*. The *why* — reasoning, measurements, engine traps, the
record of what each merge cost — lives in the `rogue-dungeon` Agent Skill
outside the tree (`D:\PokemonTest\.claude\skills\rogue-dungeon\`), and the
per-subject repo docs listed at the end are the reference for third-party work.

---

## 1. What the game is

pokeemerald-expansion turned into a roguelike. A run is **115 procedurally
generated floors** across **14 themed dungeons**, with the stock gym leaders,
Elite Four and champion standing at the bottom of each as bosses. You start with
**two Pokémon at level 10** and 100 Poké Balls, and the run ends when your party
whites out or when you beat the finale.

Nothing about a floor is stored. **A floor costs two bytes of save state** — a
seed and a counter — and room layout, exit position, trainer identities, item
placement and encounter tables are all re-derived from them. Generation runs on
a local LCG built on `ISO_RANDOMIZE1`, never the global RNG, so a layout cannot
depend on the player's step count.

Implementation is `src/rogue_dungeon.c` / `include/rogue_dungeon.h` plus hooks
in a handful of engine files, with nine further `src/rogue_*.c` modules for the
systems that grew their own homes (charms, journal, portraits, BW animation,
GB Sounds, the music player, Mode 7, Voltorb Flip).

---

## 2. Run structure

### Floors and dungeons

| | |
|---|---|
| Gym dungeons | 8, **10 floors each** — floors 1–80 |
| Elite Four dungeons | 5, **5 floors each** — floors 81–105 |
| Finale | 1, 10 floors — floors 106–115 |
| Total | **115 floors, 14 dungeon slots** |

Every dungeon's last floor is a boss arena — a centred arena laid out the same
way on every theme regardless of which generator the theme uses. Floor 110
carries a fixed rival mini boss.

### The level curve has three stages

Bosses are fought with their **stock parties, unscaled**, so the encounter curve
is fitted to them rather than the other way round.

| stage | floors | levels per floor | why |
|---|---|---|---|
| gyms | 1–80 | 0.51 | eight leaders spanning levels 13 to 43 |
| Elite Four | 81–105 | 0.48 | 47 to 56, over 25 floors |
| finale | 106–115 | **1.60** | Wallace 56 → Steven 76 in ten floors |

`TRAINER_STEVEN` is Emerald's post-game superboss, not its champion, so he lands
**+5.3 over the curve** — the widest boss gap in the run, and deliberate.
`verify_run_structure.py` prints the whole table and reads party levels straight
out of `trainers.party`, so swapping a boss for one at a different level fails
the check instead of silently bending the curve.

### Four regions of bosses

Each of the 14 dungeon **identities** independently rolls one of **four
regions** — Hoenn, Kanto, Johto, Sinnoh — so a single run mixes them freely.
That is 56 boss rows across five parallel tables (trainer, overworld sprite, TM,
music, backdrop).

| region | roster | levels |
|---|---|---|
| **Hoenn** | Roxanne → Juan, Sidney → Wallace, **Steven** as finale | stock |
| **Kanto** | Brock → Giovanni, Lorelei → Blue, **Red** (Mt. Silver team) as finale | stock FireRed, `KANTO_TOLERANCE` 12 |
| **Johto** | Falkner → Clair, Will → Lance, **Ethan** as finale | scaled **up** |
| **Sinnoh** | Roark → Volkner in **Platinum's gym order**, Aaron → Cynthia, **Dawn** as finale | scaled **down** (canonical teams run +5 to +8 over) |

Ethan rather than Red for Johto because Red is already Kanto's finale and a run
cannot field one trainer twice. Dawn's finale team is her anime six, **fully
evolved** — a 314 BST Piplup as the run's final ace at level 81 is absurd.
Silver and Barry exist as rivals with parties of their own but are **placed by
nothing** today; the floor-110 rival is a separate code path.

Kanto is the only region left at stock levels. Sinnoh was scaled because thirteen
of its rows sit on a different curve entirely; Kanto is two outliers on an
otherwise decent fit, which one wider tolerance covers honestly. Three regions
each with an allowance of their own stops being a check and becomes a list of
excuses.

**Trainer id budget is now the constraint.** `TRAINERS_COUNT_EMERALD` is **907**
against a ceiling of **920** — thirteen spare. A fifth region needs the ceiling
raised (which renumbers every system flag and invalidates saves), and needs a
third region var: the region word is 2 bits × 14 identities = 28 bits across
`VAR_ROGUE_RUN_REGION_LO`/`_HI`, bounded at four regions.

### Dungeon order shuffle

Behind `FLAG_ROGUE_RUN_COMPLETED`: a first playthrough walks vanilla's own
progression, and clearing the run once unlocks the shuffle. An Options page 2
entry (`DUNGEON SHUFFLE`) turns it back off, and the toggle works **both ways**
— that was a play-found one-way switch, now fixed.

Dungeons permute **within a group, never across**: gyms move in bands of two
(16 orders), and four of the five Elite Four slots permute freely from a 24-row
table with **Wallace pinned fifth**. **384 orders**, times 16,384 region words.
Confining the permutation is what keeps every floor boundary byte-identical —
moving a 5-floor dungeon into a 10-floor slot would shift everything after it.

The critical distinction, and the whole correctness story, is **slot in,
identity out** (`DungeonForSlot`). A *slot* is a position in the run — which
floors it spans, whether it has a mini boss, where it sits on the curve. An
*identity* is which dungeon stands there — theme, boss, sprite, TM. Floor
arithmetic stays on the slot; everything cosmetic and combat-facing takes the
identity.

`verify_run_structure.py` check 7 walks all 384 orders against the real curve
and real parties and asserts every boss lands within a tolerance of 9. Measured
worst cases: **−6.6 (Sidney at slot 11)** and **+8.0 (Brawly at slot 0)**.

### Ending a run

- **Loss** — a death summary names the floor it ended on, the dungeon, and the
  deepest floor ever reached. Stored at the whiteout, shown by the starter
  script; both vars hold the *displayed* floor because zero is the sentinel and
  dying on floor 1 is the commonest ending.
- **Win** — recorded (a completion counter on the continue window, credited only
  from the boss script's run-complete branch, never from the shared `ResetRun`)
  but **not yet rewarded**. Party and bag are still wiped. The one thing behind
  `FLAG_ROGUE_RUN_COMPLETED` today is the shuffle.
- **Money is wiped with the run**, which is what makes the gambler event and the
  rest stop's mart mean anything. It was not, for a long time.

---

## 3. The floor

### Five generators

| generator | shape | themes |
|---|---|---|
| `DUNGEON_GEN_CAVE` | 1×1 carve, nine-case wall autotile, cosmetic passes | New Mauville, Mirage Tower, Ocean, Underwater, Sidney, Phoebe, Glacia, Drake |
| `DUNGEON_GEN_WOODS` | 3×3 stamps on a coarse grid, no autotiling | Woods |
| `DUNGEON_GEN_ORGANIC` | cellular-automata cave, no rooms or corridors | Granite Cave, Fiery Path, Murky Cave (30 floors) |
| `DUNGEON_GEN_TRAILS` | irregular clearings on a jittered grid joined by one-wide meandering trails | Jungle, Flower Meadow |
| `DUNGEON_GEN_FACILITY` | a floorplan — rectangles tiling the interior exactly, joined by doors punched through shared walls | **written, currently unused** |

Cave and Woods share the room sampler and corridor chain outright, so their room
graph is always a **path graph**: `n` rooms chained in insertion order, `n-1`
corridors, no branches, no cycles, zero dead-end tiles measured over 1,600
floors. Organic goes the other way — spanning traverse ~85 tiles against 37–41.

Organic costs **576 bytes of EWRAM**, two bitplanes, and needs neither a BFS
queue (a queueless alternating-direction relabel converges in 4.2 rounds) nor
new sliver art (a CA cave makes *fewer* one-block-thick walls than
rooms-and-corridors). Facility paints through the same bitplane, because a floor
runs exactly one generator.

Trails takes narrow floor from 2.7% to ~12–16% and coverage from 42% to 21–28%,
while **encounter density per step is preserved** — grass is 41.1% of walkable
floor under the old generator and 42.2% under trails. Clearing merge is
controlled by lobes per clearing, not lobe size.

`ApplyCosmeticPasses` (floor patches, wall skirts, block decor) runs on every
generator path and **must run before the stairs are placed**, so a decoration
cannot paint over the exit.

### Fourteen themes

`struct RogueDungeonTheme` owns everything that varies: layout donor, generator
kind, metatile tables, wall autotile table, species pool, encounter window,
music, battle backdrop, weather map, region-map name. **Adding a theme is a
table entry, not a generator edit.**

| # | theme | mapsec | generator | music |
|---|---|---|---|---|
| 1 | Petalburg Woods (autumn) | `MAPSEC_ROGUE_WOODS` | WOODS | `MUS_AQUA_SADPOP` |
| 2 | Granite Cave | `MAPSEC_ROGUE_CAVE` | ORGANIC | `MUS_RG_ROCKET_HIDEOUT` |
| 3 | New Mauville | `MAPSEC_ROGUE_NEWMAUVILLE` | CAVE | `MUS_AQUA_MAGMA_HIDEOUT` |
| 4 | Fiery Path | `MAPSEC_ROGUE_FIERYPATH` | ORGANIC | `MUS_MT_CHIMNEY` |
| 5 | Mirage Tower | `MAPSEC_ROGUE_MIRAGETOWER` | CAVE | `MUS_DESERT` |
| 6 | Jungle | `MAPSEC_ROGUE_JUNGLE` | TRAILS | `MUS_ROUTE119` |
| 7 | Ocean | `MAPSEC_ROGUE_OCEAN` | CAVE | `MUS_ABANDONED_SHIP` |
| 8 | Seafloor | `MAPSEC_ROGUE_UNDERWATER` | CAVE | `MUS_UNDERWATER` |
| 9 | Victory Road — Sidney | `MAPSEC_ROGUE_VR_SIDNEY` | CAVE | `MUS_VICTORY_ROAD` |
| 10 | Victory Road — Phoebe | `MAPSEC_ROGUE_VR_PHOEBE` | CAVE | `MUS_MT_PYRE_EXTERIOR` |
| 11 | Victory Road — Glacia | `MAPSEC_ROGUE_VR_GLACIA` | CAVE | `MUS_ABNORMAL_WEATHER` |
| 12 | Victory Road — Drake | `MAPSEC_ROGUE_VR_DRAKE` | CAVE | `MUS_CAVE_OF_ORIGIN` |
| 13 | Flower Meadow (Ever Grande) | `MAPSEC_ROGUE_EVERGRANDE` | TRAILS | `MUS_EVER_GRANDE` |
| 14 | Murky Cave | `MAPSEC_ROGUE_MURKYCAVE` | ORGANIC | `MUS_SEALED_CHAMBER` |

The theme's name is written into `gMapHeader.regionMapSectionId` on every
generate, so the in-game location name is how you verify a shuffle without a
debug build. (Met locations are **not** covered — `GetCurrentRegionMapSectionId`
re-derives from the saved warp.)

Notable art work behind these:

- **The woods is autumn grass by palette rotation, not redrawn art.** Same tile
  art copied into Rustboro, rotated −65°, with only palette indices 12–15
  touched. Four tree variants, 36 metatiles, `tools/rogue/append_rustboro.py`
  owning the tileset the way `append_metatiles.py` owns the Cave.
- **The Flower Meadow is a full third-party import** — 238 metatiles, 362 tiles,
  all seven secondary palettes consumed, Western Cave dark's flowering hedge
  over Purity Forest's grass with Western Cave dark's stone as the plaza. It has
  all twenty wall slots and both autotiles filled, which is what took the
  vertical-sliver constraint off Ever Grande.
- **New Mauville's wall art** — cap row, convex corners, two-metatile column
  base, three-row wall furniture, generator and supercomputer set pieces, crate
  clutter. Prototyped in `tools/rogue/newmauville/`, reference is
  `docs/NEWMAUVILLE_TILESET.md`. Column head still open.

### Eleven weather maps

`FOG`, `SNOW`, `PETALS`, `RAIN`, `BLIZZARD`, `LEAVES`, `ASH`, `SANDSTORM`,
`ZUBATS`, `SEABIRDS`, and `DYNAMIC`. One map per **weather**, shared by every
theme that wants it. `WEATHER_COUNT` is 26.

**Six weathers are this project's own**: `WEATHER_PETALS` (16),
`WEATHER_MONSOON` (17), `WEATHER_BLIZZARD` (18), `WEATHER_LEAVES` (19),
`WEATHER_ZUBATS` (24), `WEATHER_SEABIRDS` (25). Ash and sandstorm are vanilla
weathers on new maps and cost nothing but the map — with the catch that a
vanilla weather brings its `battle_util.c` membership along, so Mirage Tower now
genuinely chips 1/16 max HP a turn off both teams.

- **`WEATHER_LEAVES` is the woods'**, so it is the first weather most runs see.
  Vanilla's `battle_anims/sprites/leaf.png` unmodified, shipped amber because
  the green ramp vanishes over tall grass. Its motion is the blizzard's — real
  travel plus a sine bob — and **the wind gusts**: six seconds calm, then a
  four-second swell off a half sine taking the field from 42 to 82 px/s, for
  four bytes of EWRAM and one addition.
- **`ZUBATS` and `SEABIRDS` are one implementation and a table.** A
  `struct RogueFlierKind` per species and a `RogueFlierFlock` grouping them, so
  **adding a flying weather is a table entry**. No new art either — they drive
  follower Pokémon overworld sprites via `CreateObjectGraphicsSprite`.
- **The ocean is clouds, a Wingull flock, a lone Pelipper, and reflections of
  all of them** — vanilla's `SetUpReflection` with the object event taken out,
  drawing the bird's own tiles for zero VRAM.
- **Pooled weather.** The jungle and Murky Cave sit on `MAP_ROGUE_DUNGEON_DYNAMIC`
  and roll one weather per floor from a pool keyed on `regionMapSectionId` — so
  two themes share one map and still get completely different weather.

| theme | pool |
|---|---|
| Jungle | MONSOON, RAIN, FOG_HORIZONTAL, SUNNY_CLOUDS, SHADE |
| Murky Cave | FOG_HORIZONTAL, FOG_DIAGONAL, SHADE, ZUBATS |

The roll is **once per map load** — per-floor variety within a dungeon, not
weather that shifts while you stand still.

### What is on a floor

| thing | count | notes |
|---|---|---|
| Trainers | 5 → 7 | +1 every 40 floors |
| Item balls | 4 → 8 | +1 every 25 floors |
| Buried items | 2 → 9 | Dowsing Machine granted at run start; ceiling 16 (a hidden item's id **is** its flag) |
| Buried crafting materials | 2 | additive, not a share of the held-item roll |
| Berry trees | 4 | on themes with soil; **one in four is rotten**, yielding 1 instead of 6 |
| Mining rocks | 3 | entry point to the mining minigame |
| Floor event | 0 or 1 | **50% of floors**, ~58 a run |

`OBJECT_EVENTS_COUNT` is **28**, up from vanilla's 16 — a floor declares 21
objects plus the player and a follower. (Several comments in
`constants/rogue_dungeon.h` still say 24, from the raise before this one; count
from `constants/global.h`.) Object events are destroyed off-screen
and respawn from their **template**, which is why anything that must stay taken
(a picked egg, a mined rock, an abandoned boss ace) rewrites the template rather
than the live object.

---

## 4. Encounters and species

### A species pool is a ladder, not a list

`theme->species[]` is sorted weakest to strongest, and `BuildWildEncounterTable`
reads a **window** into it rather than a prefix — so weak species *retire* as
stronger ones unlock.

```
window = theme->encounterWindow (default 8)
tiers  = window + DungeonFloorWithin(floor), clamped to speciesCount
bottom = max(tiers - window, 0)
```

The ramp counts from the floor **within the dungeon**, not the run floor. Keying
it on the absolute floor made the underwater pool pin at its top five entries and
the woods never reach past its ninth: **63 curated species could not appear in
the game at all**, silently.

| themes | window | pool size |
|---|---|---|
| the eight gyms | **12** | 21 |
| Sidney, Phoebe, Glacia, Ever Grande | 8 | 11 |
| Drake | 7 | 7 |
| Steven | 8 | 13 |

Twelve is also `NUM_LAND_MONS_ENCOUNTER_SLOTS`, so a gym floor fills every land
slot with a different species. Drake is 7 because that is the whole of Gen 1–4
Dragon once legendaries, 600 BST pseudos and pre-evolutions are out.

- **Slots are dealt round-robin from the window**, not drawn per slot — encounter
  weights are 20/20/10/10/…, so independent draws let one species own the floor.
- **The table is re-dealt per roll wherever the window is wider than the slots.**
  Ocean and Underwater go from five species a floor to the full window; the
  Safari offers 47 species through 12 slots. The rotation advances by `slots`,
  not by one, so consecutive tables share nothing — measured 13 distinct species
  at stride 1 against 18 at stride `slots`. Long-run fairness requires the stride
  be coprime with the width, which `check_safari_pool.py` asserts.
- **Fishing is the water branch with a table of its own**, strided by 1. The rod
  works only where a theme's `wildArea` is water — the ocean, the seafloor and
  the Safari's ponds. Which of its ten slots a cast reads is the rod's
  *technique* (Old 0–1, Good 2–4, Super 5–9), unlocked by dungeon index.
- **No floor offers a species below its evolution level** — `DevolveForLevel`,
  guarded by `check_pool_evolutions.py`.

### Species roster

- **Gen 1–4 on, Gen 5–9 off**, each disabled generation keeping its starter
  families. A disabled family still *compiles* to a zeroed `species_info` row a
  wild encounter can roll into, so anything named must be checked against
  `PokeSMD.map`, not against `species_info`.
- **Thirty starter picks** — the Gen 1–9 trios plus Pikachu, Clefairy and Eevee.
  You pick two. Each carries a fallback attack of its own type;
  `check_starter_moves.py` grades all thirty (24 of 30 already know theirs).
- **The Clefairy line is a project override** — BST 323 → **340** and 483 →
  **530**, learnsets rebuilt because vanilla gives Clefairy no new attack between
  level 4 and 32. Cleffa is deliberately unreachable.
- **The Pikachu line is a project override** — both Raichus 485 → **530**,
  Pikachu untouched at 320. Volt Tackle is Pikachu's at 48 and Raichu never
  learns it, so holding a 320 BST to level 48 is the price. Alolan Raichu is
  unreachable by design and is the standing argument for a form-changer NPC.
- **Eevee needed no stat work** — all eight branches are exactly 525. Leafeon and
  Glaceon are stone-only here.

### Two encounter surfaces, surf and dive

Ever Grande carries flower beds *and* flowery long grass on different
behaviours, because they are different heights. The ocean is crossed surfing (a
property of the floor metatile); the seafloor is dived (a property of the
**map**, which is why underwater is the one theme with a map of its own).

---

## 5. Loot, economy and pacing

- **Loot is banded, not floored.** `sLootConsumables` gives every entry a floor
  it arrives on **and one it leaves on** — a minimum alone leaves Potions the
  commonest find on floor 100 because they were there first. Held items band the
  other way round on purpose: cheap ones close, ones that stay good (Leftovers,
  the Choice items) have no upper bound.
- **Quantity is part of the ladder.** A Hyper Potion arrives in twos, so a single
  Max Potion is a *downgrade* in raw HP — both it and Full Restore ship at
  quantity 2 for that reason alone. `verify_loot_table.py` found that and is what
  to re-run after any table edit.
- Two item balls near the top of a run, seven by the end, mostly healing, scaling
  80 HP per floor to 2800.
- **Evolution stones, buried** — ten, contiguous from `ITEM_FIRE_STONE`, pinned
  by nine `STATIC_ASSERT`s because the Game Corner counter derives its stone by
  offset. One floor in seven from floor 4, deliberately generous: a Clefairy run
  that never finds a Moon Stone is stuck at a stage-1 statline for 115 floors.
- **Party-wide experience, permanently** (a flag, not code — `I_EXP_SHARE_FLAG`),
  and wild encounters pay **150%**. Gen 7 behaviour: additive, not a split, so it
  costs participants nothing.
- **An anti-grind clock on wild experience.** The first 12 wild knockouts on a
  floor pay full; every one after that pays 15pp less, bottoming out at 25%.
  Nothing else in a run depletes — the nurse heals free, revives never retire
  from the loot table, grass can be walked forever — so before this the level
  curve was a suggestion. The counter is packed as `(floor << 8) | kos` in one
  var, which makes the reset implicit and save-scumming impossible.
- **EVs are allocated by hand**, not earned — a stat editor on the party menu,
  with both `MonGainEVs` call sites commented out. Defensible because stock
  trainer parties in this build carry no EVs at all.

---

## 6. Floor events

Twenty events, one per floor at most, on **50% of floors** — ~58 a run. Banded
by depth and weighted in three tiers (RARE 1, DEFAULT 10, COMMON 20). Every one
is a **question with a cost on both branches**; an event that is only a reward is
an item ball with more text, and the floor already has eight of those.

| event | floors | weight | per run | what it is |
|---|---|---|---|---|
| Gambler | 1+ | COMMON | 5.04 | double or nothing on a third of your money |
| Trader | 1+ | COMMON | 5.04 | one of yours for one of his, five levels up, species unseen |
| Egg | 1+ | COMMON | 5.04 | free; the price is the party slot. Hatches on pickup |
| Shrine | 1+ | COMMON | 5.04 | old woman + grave prop. Commune / Offering / Disturb — the run's **only** charm cleanse |
| Herbalist | 1+ | COMMON | 5.04 | old woman + berry tree. Revival brew (+ Sluggish) or forage |
| Tutor | 11+ | COMMON | 4.38 | a forbidden move, paid for in Overexerted or Frail |
| Spring | 1+ | default | 2.52 | free full heal |
| Injured | 1+ | default | 2.52 | joins, or is feigning and ambushes. Rolled at prepare time, so unscummable |
| Orb | 1+ | default | 2.52 | no NPC — `OBJ_EVENT_GFX_METEORITE`. A boon **and** a price, as two party-wide act-scoped charms |
| Shuppet | 1+ | default | 2.52 | wanders. **Eats one affliction** — the only event that removes a charm |
| Clefairy | 1+ | default | 2.52 | moonlit, meteorite prop. Offers a **Moon Stone specifically** |
| Scout | 11+ | default | 2.19 | points at a rare Pokémon. **The run's first shiny**, forced via `MON_DATA_IS_SHINY` |
| Ditto Ball | 11+ | default | 2.19 | a fake item ball, then a **double** wild Ditto battle |
| Transposer | 11+ | default | 2.19 | scientist. Hidden ability swap |
| Pokerus | 11+ | default | 2.19 | a PC terminal. Grants Pokérus, then takes the lead to 1 HP and poisons it |
| Nest | 11+ | default | 2.19 | **three wild battles back to back, no heal between**, each `-4` levels and climbing |
| Pedlar | 21+ | default | 1.96 | one item sight unseen from the floor's loot table, at a deliberately bad price |
| Alpha | 21+ | default | 1.96 | prowls. An oversized wild mon with `settotemboost` |
| Crystal | 1+ | **RARE** | 0.25 | volatile evolution crystal; a palette swap of the breakable rock |
| Fossil | 11+ | **RARE** | 0.22 | hiker + rock. Extract (+ Hexed) / Sell / Study |

Mechanics worth knowing:

- **The script pointer lives in the table**, so there is no dispatch. `PlaceEvents`
  runs **last** in the prepare sequence and rolls its odds unconditionally before
  returning, because every placer draws from the same seeded stream and a
  variable number of draws would shift every object placed after it. Its collision
  test is one attempt, not a retry loop, for the same reason.
- Three fields — `weight`, `themeMask`, `propGfxId` — are all **default-safe**: a
  row that sets none behaves exactly as the original six did.
- Props are placed **deterministically** (one tile east, falling back to west,
  inside the same room). A prop that will not fit is simply absent.
- **`VAR_ROGUE_EVENT_SPENT` is `VAR_TEMP_0`**, wiped by `ClearTempFieldEventData`
  on every map load. Every floor is reached by a warp, so it clears itself on
  arrival with no flag spent and no cleanup code to forget. This closed a
  play-found bug where every event fired as many times as you talked to it —
  infinite full heals, infinite money from the Fossil's sell branch, and a Trader
  that ratcheted a Pokémon up five levels per conversation.
- The Trader, Egg, Injured and Nest all draw from **`sSafariLandSpecies`** — 161
  entries, sorted weakest to strongest, every one verified present in the ROM and
  deliberately a species the run cannot obtain any other way.
- **The trade is self-balancing**: the reward arrives at the traded mon's level
  + 5, so trading the weakest thing on the bench at floor 80 returns a level-15
  random against a curve of 45. Refused on a party of one.

### The Alpha

Beating or catching a floor-21+ Alpha pays a **mega stone** drawn against your
own party, or a **Z-crystal** typed off one of them; from the **second** Alpha on,
the **Shiny Charm** joins the same pool and retires itself once won (it is a key
item, so the already-carried skip takes it out).

A caught Alpha **keeps its boost** — it arrives holding `ROGUE_CHARM_ALPHA`, +1
to all five combat stats for the run, `reapplyOnSwitchIn`. It was already on the
field at that strength when you chose to catch it. Alphas caught into a PC box
are re-installed from a four-slot personality registry on `RogueRunModifiers`,
and the registry is **not consumed on first use** — being an Alpha is a property
of the Pokémon, not something that happened to it.

---

## 7. Charms and afflictions

A complete per-run modifier system: negative *and* positive, duration-limited,
per-Pokémon or party-wide, with removal. `src/rogue_charms.c`.

**Storage is a struct in SaveBlock3, keyed on `personality`** — not on party
slot, because this build merged both the SwSh party menu and the storage system,
so reorders and box round-trips happen constantly. ~76 bytes of a 1,624-byte
block, with a version byte and lazy migration checked on **every access** (there
is no post-load hook that covers all three arrival paths).

**Thirteen charms, six effect kinds:**

| kind | example |
|---|---|
| `RECOIL` | Unstable, Overexerted — % max HP at battle start |
| `HEAL` | Rejuvenating — 8% a battle |
| `STAT_DROP` | Cursed, Sluggish, Orb Burdened |
| `STAT_BOOST` | Emboldened, Orb Awakened, **Alpha** |
| `MAX_HP` | Frail (−15%), Brittle — capped at 90% |
| `DAMAGE_TAKEN` | Hexed (+10% super-effective, 3 battles, party-wide) |

Rules that took work and should not be re-derived:

1. **Four kinds apply at one known moment to a known party slot.** `MAX_HP` and
   `DAMAGE_TAKEN` are read from `CalculateMonStats` and the damage calculation —
   engine paths that run constantly and know nothing about charms. Those two
   lookups **must not write, must not sync, and must be cheap**. A migration
   firing inside `CalculateMonStats` during save loading would rewrite the block
   the load is filling.
2. **Both HP and stat effects are netted**, not applied in turn, so the recoil
   half of a mirrored pair can never be the thing that drops a Pokémon to 1 HP.
3. **Recoil cannot faint** — it floors at 1 HP. A party wiped by charms between
   floors is a whiteout with no battle in it.
4. **Durations tick at battle end**, not start — recoil applies in
   `BeginBattleIntro` and stat drops in the first-turn events, two different
   moments, and a counter ticked at the first would expire a one-battle charm
   before the second read it.
5. **Charms follow the Pokémon**, and a mon deposited to a box comes back clean —
   the only reading that cannot be exploited by parking an affliction in a box.
   (The Alpha registry is the deliberate exception.)
6. **The max-HP effect is idempotent by construction** — it scales the freshly
   computed maximum, so recalculation cannot compound it and removing the charm
   restores the value.
7. **`reapplyOnSwitchIn` is a per-charm table flag.** Only Cursed and Alpha set
   it; it hooks the *end* of `SwitchInClearSetData`, which resets every stat
   stage on its way through.
8. **The totem pipeline was not reused** — `gQueuedStatBoosts` ends in
   `BattleScript_TotemBoost`, which prints "the aura flared to life", the wrong
   thing to tell someone whose party was cursed.

**Visibility** is a `CHARMS` entry on the start menu (between the party and the
trainer card), naming Pokémon by nickname rather than by slot. Deliberately not
in the summary screen or the party menu — both are montmoguri forks, and putting
the first player-facing charm UI in a fork means re-hosting it after every merge.
Its icon is FRLG's Volcano Badge recoloured, which lights up when selected
because its flame is a single palette index.

A **debug tool** (Utilities → *Rogue charms…*) grants any charm to any slot or
party-wide, with live durations, UP/DOWN for the charm, LEFT/RIGHT for the
target, A to grant and SELECT to wipe. That is what makes the resync path
testable at all.

Ten `callnative` script wrappers are the event track's only door into charm
storage — grant, query, buffer-name, cleanse-one, cleanse-mon, cleanse-all.

---

## 8. Places

### The rest stop

A place, not a heal button — its own map, reached between dungeons, holding:

- **the nurse** (free heal)
- **the mart** (which money being wiped with the run is what makes meaningful)
- **the archivist**, who owns box access — which is why
  `SWSH_PARTY_MENU_PC_ACCESS` is FALSE here against upstream's TRUE
- **west and east arrow warps** with drawn chevrons. They used to be a shared
  `MB_NON_ANIMATED_DOOR` tile facing the arriving player *south*, into solid rock

### The Safari Zone

Six maps off the east alcove, with its own generated species ladder — **47-wide
land window through 12 slots**, which is exactly the case the per-roll re-deal
exists for. `gen_safari_pool.py` builds it, verified present in the ROM, and
prints a drop list of **209 alternate forms** (Rotom's appliances, Wormadam and
Burmy's cloaks, Shellos/Gastrodon, Deoxys, the regional variants) that are in
the ROM and reachable by nothing — the standing argument for a form-changer NPC.

Reachable on every run because of the **Wave Charm**, below.

### The Game Corner

A room off the rest stop with all **nine minigames**, hand-ported from
`heyopc/pokeemerald-gamecorner-expansion` — that branch is not built on
pokeemerald-expansion and we are 12,800 commits ahead of its merge base, so this
is a file-by-file port, not a merge. Reference is **`docs/GAME_CORNER_PORT.md`**;
`docs/GAME_CORNER_CANDIDATES.md` is its companion.

Pinball (four tables), pachinko, the Mauville derby, gacha, blackjack, block
stacker, snake, flappy bird, Voltorb Flip. Plus a prize counter and a stone
counter that derives its item by offset (`ITEM_FIRE_STONE + index`, with a
`STATIC_ASSERT` that the ten are still contiguous).

**Four of their nine vars landed exactly on the roguelike's** — a hand of
blackjack would have overwritten the run state, and a round of Voltorb Flip would
have reseeded the next floor under the player. All nine were rehoused to `0x407D`+
as `VAR_GC_*`, leaving `0x40F7`–`0x40FF` whole. That was found by looking, not by
the build, which is why the var-collision gap is still the one open structural
risk in this project.

**Eight of the nine crashed the game on exit** after the port built clean and
passed every check — the canonical evidence for what a passing check does and
does not cover.

### Mining and crafting

- **A mining minigame** reached from the rocks on a floor. Its own reward table
  is already exactly the crafting tree's mineral half — all four shards, Heart
  Scale, Star Piece, and six evolution stones outright — which is why
  `sLootMaterials` no longer buries a shard. A dowsing hit is a press of A; the
  minigame is a decision about where to dig with a stress meter running.
- **A 3×3 workbench the player carries**, with bulk crafting, its own recipe book
  and debug surface. `check_craft_recipes.py` pairs every recipe against the
  floors its ingredients can actually be held, and asserts every deep item costs
  a scarce material.

---

## 9. Items granted by the run

- **The Wave Charm** (`ITEM_ROGUE_SURF_TOOL`) — lets the **lead** Pokémon carry
  the player, Gen 7 Ride-style, so the Safari's water is reachable on every run
  rather than only a Mudkip one. Opens nothing else, by construction.
- **The variable rod** (`ITEM_ROGUE_VARIABLE_ROD`, "Anglers Rod") — one rod whose
  technique changes with progress, unlocked by dungeon index.
- **The Dowsing Machine**, in its ORAS active-mode form. Note it refuses to run
  while surfing or underwater, so buried items are not placed on the ocean or the
  seafloor.
- **Eight key items at the starter pick**, each because no map in a run would
  otherwise hand it over.
- **Multiple key items registered to SELECT** — `REGISTERED_ITEMS_MAX` 10. Worth
  being honest that a run has about two things to register, and that two or more
  registered items turn the SELECT press into a menu, which eats any SELECT+X
  combo.

---

## 10. Presentation

### Battle sprites

- **BW-style animated battle sprites, Gen 1–3, both sides** — 386 species, 772
  containers, **9.03 MB** and the largest category in the build. Cost is 23.8 KB
  per species both sides at k=2, measured, not estimated. Frames stream rather
  than being resident; **heap, not ROM, is the binding constraint.**
  `ROGUE_BW_ANIM_BACK` turns the back animations off on their own and drops
  87,432 B of ROM.
- **Shinies work** — an 8-byte permutation of the stock shiny palette per
  container, which closed the "animated shinies are wrong by construction" gap.
- **Animated trainer front pics** — thirteen of the fourteen Hoenn bosses move.
  Tate and Liza are deliberately absent. The rule that matters: **one battler
  position, one pixel buffer**, and whichever animator is loading into a position
  must stop the other one first — unconditionally, and *above* its own table
  miss. Half that rule shipped and corrupted the defeat screen for every non-Hoenn
  boss.
- **Colour variants** — every Pokémon shifts its own colours in OkLCH from its
  personality value. Guarded by `check_variant_colours.py`. The caught-mon
  Pokédex page needed its own fix, because it draws from the battler's sprite
  GFX rather than a fresh decode, and with BW animations in that buffer the stock
  palette scrambled every caught mon. **Mon icons still have no variant** —
  `GetValidMonIconPalettePtr` keys on species alone.
- **Every boss has a mugshot**, all 57 trainers. The colour is keyed on
  **identity**, not region: gym leaders cycle Purple/Green/Pink/Blue/Yellow, the
  Elite Four take the first four in fought order, the Champion is Yellow and the
  finale wraps to Purple so it never repeats the Champion it follows. **No two
  adjacent identities share a colour**, which is the property that matters when
  the run fights them in order. The scheme was read off the ten pre-existing
  entries, not invented — it reproduces all ten exactly with zero changes.
- **Johto and Sinnoh sprites are staged** — 27 front pics and 27 overworld
  sprites, both regions complete plus a rival and a superboss each. Reference and
  full credits are `docs/JOHTO_SINNOH_SPRITES.md`.
- All ~150 FRLG overworld sprites are unguarded, for trainer variety —
  `sTrainerClassGfx` selects them and ten classes carry an FRLG alt keyed on a
  bit of the trainer id.

### Boss floors

**Bosses are abducted, not merely defeated.** Every one opens with "H-H-Help
me!" — one line for all fourteen, because whatever is down here has reduced them
all to one voice. On defeat the boss **vanishes**, their TM drops out of the
empty air, and **their ace is left standing on the tile they occupied** and can
be adopted. The ace inherits the boss's own script, which is the anti-brick: a
boss floor draws no stairs, so the only way off is a conversation.

The adopted ace carries the boss as its original trainer — the OT id is invented
as a pure function of the trainer id, and shininess is re-asserted afterwards
since it derives from `otId ^ personality`.

**Every floor used to resolve to `BATTLE_ENVIRONMENT_CAVE`.** Now
`sDungeonBossEnvironment[]` gives the leaders vanilla's LEADER interior and the
Elite Four their own stadium palettes, and a `battleEnvironment` field on the
theme table covers ordinary floors — **zero new art**. Glacia is the one boss
whose vanilla backdrop is replaced, with her own dungeon's frozen cave.

### The Mystery Dungeon speaker portrait

A framed box above the left end of the field message box holding the face of
whoever is talking, plus the nickname in the namebox — the PMD look.
`src/rogue_portrait.c`, art table in `src/data/rogue_portraits.h`.

**Real PMD art is in: 15 species, 256 faces** — the first-stage starters of gens
1–4 plus Pikachu, Clefairy and Eevee, every species a run can begin with. Cut
from Sprite Repository sheets by `tools/rogue/import_portraits.py`, which reads
the dex mapping out of `enum NationalDexOrder` in the repo rather than carrying
one of its own.

The face follows the follower's own emotion — `GetFollowerAction` already settles
one of eleven `FOLLOWER_EMOTION_*` per line, which is exactly PMD's expression
axis, and `sFollowerEmotionToFace` is the single place the two lists meet. It is
chosen **once per conversation**, not per draw, so expression cannot change
between pages of one line.

**The box is sized by the art** and bottoms out at row 12 (13–14 are the namebox,
14–19 the dialogue frame). The frame is the dialogue box's own tiles written
straight into the BG0 tilemap — not a shortcut, the only route, because BG0 has
no window tile budget left. The sprite is owned directly under our own tag rather
than via `CreateMonPicSprite`, because `ResetAllPicSprites()` is never called on
the overworld path.

Cost: **20 bytes EWRAM, zero IWRAM, ~20 KB ROM**, of which 18.9 KB is
`gRoguePortraits[NUM_SPECIES]` sitting empty and waiting for art. Adding
portraits is a table entry.

### The Mode 7 title screen

The game boots into a **Mode 7 dungeon flyover** — an affine 512×512 wraparound
plane rasterised from a real dungeon floor plan, with a swarm of Unown, horizon
fog, a vignette and a twinkling starfield, on a four-phase looping camera shot
(cruise → lift → settle → drift-and-turn, 5.5s a cycle). START plays a Dialga cry
and hands off to the main menu.

The Pokémon logo is dropped entirely; PRESS START and the copyright line are
vanilla's own sprites. Measured limits worth keeping:

- **Camera height 64–96 is the clean window.** Below ~44 lambda quantisation
  repeats between scanlines; at 150+ `REG_BG2PA` overflows s16 near the horizon.
  The shot tops out at 128, where exactly one row parks.
- **20 Unown is the cap**, measured on hardware. The binding limit is the
  **busiest scanline**, not the total, and affine OBJ cost is charged per pixel of
  *width* — which is why the front pics are cropped to their middle 4×4 tile block
  and repacked as 32×32.
- **The DMA budget is full.** DMA0 is the affine scanline transfer, DMA1/DMA2 are
  the sound FIFOs owned by m4a, DMA3 is every `DmaCopy` in the game. There is no
  free channel for a second per-scanline register write, which is why the
  atmosphere layer is dither plus one static BLDALPHA.
- **Stars twinkle by palette rotation** — four brightnesses moved among four
  entries every 12th frame. Four palette writes, nothing per star.

`ROGUE_MODE7_TEST` in `include/rogue_mode7.h` flips back to the vanilla title
screen; `MODE7_DEBUG` (default FALSE) carries the VBlank meter, manual camera and
the swarm toggles.

### The run journal

A persistent, cross-run log read on the **back of the trainer card**, modelled on
the DPPt save journal: short prose lines, newest first, grouped by run, with a
separator naming how each run ended. `src/rogue_journal.c`.

The card back was chosen because it is already reachable, already a full CB2
screen, and **its entire back side was dead on this build** — four of its six
stat rows are link statistics that are permanently zero in a roguelike.

**Eleven event kinds**: run started / fell / cleared, boss beaten, mini boss
beaten, ace adopted, TM taken, dungeon entered, charm on a mon, charm party-wide.
Six-byte `PACKED` records in a 128-entry ring, **772 bytes of EWRAM**. Without
`PACKED` the toolchain rounds the record to eight bytes and the ring costs 1,028
— caught only because the linker's EWRAM figure moved by more than the struct
accounted for.

`check_run_journal.py` carries **41 selftest breaks**, and `test/rogue_journal.c`
adds **11 ROM tests** that run the ring on emulated hardware.

### Pop-ups

Leaving the music player with a track playing names it on screen, on the same
plate the dungeon floor name uses, in the **top right** so it never reads as a
floor name. Same task, same window, same artwork — the GEN_3 plate slides by
scrolling the whole of BG0, so a second pop-up task would judder against the
first.

---

## 11. Audio

The largest single body of work outside the sprites.

### The library

**370 tracks across 19 playlists**, from the GBA Music Pack (BW, HGSS, DPPt,
Ranger, XY, PMD, Modern and more), Team Aqua's repo (54 songs shipping their own
per-song voicegroups, 353 tracks), a guest set and three hand-imported loose
MIDIs. `docs/AUDIO_CREDITS.md` in the repo is the reference for who made what,
and records which packs were deliberately **not** imported and what blocks the
ones that cannot be.

### Instruments

- **The All-Instrument Patch is decompiled from source** — 112 voicegroups, 30
  keysplit tables, 405 samples, reproducing `all_instruments.inc` exactly from
  applying `All-Instrument Patch (Emerald).ups` and decompiling the result.
  `check_voicegroup_roundtrip.py` is what makes it a decompile rather than a
  transcription.
- **DPPt plays on DP's own instruments** — four voicegroups ripped from DP's
  SDAT (`dp_basic`, `dp_battle`, `dp_dungeon`, `dp_field`), **229 samples**.
  Seven tools do the ripping; five tracks whose SDAT sequences use features the
  rip cannot carry are retired to `mus_dummy` rather than repointed.
  `check_ds_voicegroups.py` guards three failures that each shipped once:
  unaligned `WaveData` (which does not fault on ARM7, it **rotates the word**),
  transposition in `voice_directsound`'s dead note byte, and a program past the
  end of a voicegroup.
- **Program 13 is a drum track in most imports and a xylophone in nine.** GM keeps
  no drum kit among its 128 melodic programs, and m4a has no channel-10 concept,
  so the pack's rips put drums on program 13 where the stock patch plays them as
  pitched xylophone hits. Fixed **per song** with a second table
  (`voicegroup_all_instruments_drums`, +1,536 bytes) rather than globally, because
  a global override fixes 64 songs and breaks 9.
  `docs/PROGRAM_13_SPLIT.md` is generated and is the reference.
- **`retarget_drum_channel.py`** handles the different bug the three loose MIDIs
  had — drums on MIDI channel 10 selecting programs 1, 16 and 0. Correct GM,
  meaningless to m4a. It refuses if program 13 is already in use.

### Boss music is regional

| region | leaders | Elite Four | champion |
|---|---|---|---|
| Hoenn | engine's own themes | engine | engine |
| Kanto | `MUS_RG_VS_GYM_LEADER` | **same track** (FRLG has no separate E4 theme) | `MUS_RG_VS_CHAMPION` |
| Johto | `MUS_HGSS_BATTLE_GYM_JOHTO` | **same track** | `MUS_HGSS_CHAMPION_LANCE` |
| Sinnoh | `MUS_DPPT_BATTLE_GYM_LEADER` | `MUS_DPPT_BATTLE_LEAGUE` | `MUS_DPPT_BATTLE_CHAMPION` |

All four finales keep `MUS_VS_FRONTIER_BRAIN`. `check_boss_regions.py` guards the
song ids, because `song_table.inc` is indexed by row and ids 613/614 are dead
`mus_dummy` rows — a boss pointing at one resolves, builds, and plays **silence**.

### GB Sounds and PSG arrangements

**36 songs play in Game Boy voice**, every dungeon with its own track, driven by
a planner that reads the voicegroup rather than guessing. Four hardware limits
none of which announce themselves, and the finding that **sounding time, not note
count, decides fullness** — channel contention turned out to be a weak predictor
of quality.

The PSG remixes are not "the GB version" and the check now enforces that.

### The music player and the jukebox

A full in-game music player on the start menu (renamed `JUKEBOX`), three modes
deep — playlists → sublists → tracks — with an info panel and a queue.

**A chosen track outlives the player.** It keeps playing across warps, down a
floor, and out the far side of a battle, until START stops it. The mechanism is
the map music state machine: `SetJukebox` stops the outgoing track, calls
`ResetMapMusic` and then `PlayNewMapMusic`, so `sCurrentMapMusic` *holds* the
chosen track and every "is the new song different?" comparison in `overworld.c`
answers no. It plays through a warp seamlessly, which a direct `MPlayStart`
audition never could have.

**Five funnels in `overworld.c` had to be reached** — two must report the
jukebox, three must stand aside for it. Miss any one and the track dies at a
different moment, with a clean build and nothing wrong in any table. That is what
`check_jukebox_funnels.py` guards, with **fourteen selftest breaks, one per
site** — and it is the clearest example in the project of a *lifetime* check.

A nine-playlist bug is worth recording: `RMP_NO_PARENT` is `0xFF`, but the C
default fill for an omitted designated initialiser is `0`, which is
`RMP_PLAYLIST_DUNGEON`. Every playlist imported after the original ten silently
became a **child of Dungeon Floors**. Builds clean, every track resolves, all
playlists present and playable — one level down from where they belong.

### Engine changes for audio

- **The HQ mixer (ipatix) is in**, which needed IWRAM this build did not have
  until the wireless stack was removed and `gLink`/`gRfu` moved to EWRAM.
- **DirectSound channels raised from 5 to 12.**
- Options default to **STEREO**, and `SetPokemonCryStereo` had to be *called*,
  not merely stored — its only two callers both run before `SetDefaultOptions`
  does on a new game.

---

## 12. Interface — merged and ported from upstream

Fifteen upstream features, each a real `git merge` or a hand-port. Three are
montmoguri's and **two of the three silently orphaned our code** by wrapping the
vanilla file in `#if !SWSH_*` and forking it. Before merging any branch that
replaces a vanilla file rather than editing it, diff `origin/master...HEAD` for
that file first and re-host every hook.

| feature | notes |
|---|---|
| **A second Options page** | battle mode (1v1/2v2/MIX), battle speed 1×–4×, followers, autorun, dungeon shuffle |
| **A sprite-based START menu** (Unbound style) | `src/unbound_start_menu.c` — and note `src/start_menu.c` is a **second, dead implementation** that nothing reaches |
| **A Sword/Shield bag menu** | `swsh_item_menu.c` is live; `item_menu.c` is `#if !SWSH_ITEM_MENU` and does not compile. Took `item_menu.c`, which made two later branches unmergeable |
| **A Sword/Shield party menu** | took `party_menu.c` **with the EV allocator inside it**. `SWSH_PARTY_MENU_PC_ACCESS` is FALSE here — box access is the archivist's |
| **A Sword/Shield storage system** | dispatches at runtime instead of forking the file, so it orphaned nothing. 392 bytes of IWRAM |
| **A Gen 5 battle UI** | plus a Gen 5 summary screen |
| **The EV allocator** | a stat editor on the party menu; both `MonGainEVs` sites commented out |
| **A Stardew-style fishing minigame** | wired into the dungeon's water branch |
| **The bulk crafting system** | buffers on the heap |
| **The mining minigame** | wired to the floor's rocks |
| **Multi-register SELECT** | `REGISTERED_ITEMS_MAX` 10 |
| **Overworld followers** | `OW_FOLLOWERS_ENABLED` TRUE for **+552 bytes of ROM and zero RAM**, because `OW_POKEMON_OBJECT_EVENTS` was already TRUE and the 1.12 MB of sprites was already in the build |
| **ORAS dowsing**, **Gen 1 fishing**, **BW2 map popups**, etc. | expansion features enabled rather than merged |

### Project defaults

`SetDefaultOptions` ships this project's starting position rather than vanilla's,
on the reasoning that a roguelike is replayed from the top many times:

| setting | vanilla | here |
|---|---|---|
| text speed | MID | **FAST** |
| sound | MONO | **STEREO** |
| battle mode | MIXED | **SINGLES** |
| autorun | FALSE | **TRUE** |

New game only — these are SaveBlock2 fields and nothing migrates them. Battle
mode is not only taste here: the roguelike applies it to *every* generated battle
by hand, so one field decides what an entire run looks like.

---

## 13. Budgets and constraints

**RAM is the ceiling, not ROM.** Measured off `PokeSMD.map` at HEAD, with the
uncommitted portrait work in the tree:

| | used | of | |
|---|---|---|---|
| EWRAM | **246,024 B** | 262,144 | **93.85%** |
| IWRAM | **19,440 B** | 32,768 | 59.33% |
| ROM | **29,231,232 B** | 33,554,432 | 87.12% |

**IWRAM is no longer the tight one, and that is recent.** It sat at ~86.65%
until the wireless stack was removed outright and `gLink` and `gRfu` were moved
to EWRAM — 7,348 bytes between them — which is what made room for ipatix's HQ
mixer, whose stack this build could not previously afford. EWRAM took the cost
and is now the number to watch on every link.

~84% of EWRAM is three fixed buffers, so cutting stock content frees ROM and
moves RAM by exactly zero. `HEAP_SIZE` is the lever if EWRAM gets tight — 51% of
all EWRAM in one constant — but heap exhaustion fails at *runtime*. The whole
hidden-item system cost 220 bytes; that is the scale a new feature should land at.

Take totals off the **linker**, not off `ram_budget.py` (which attributes by
symbol and lands short); the tool's value is the per-object breakdown.

Save space is not a constraint. SaveBlock3 is expansion's designated extension
block and this build has ~1.4 KB of its 1,624 free. Everything appended to it
carries a **version byte with lazy migration checked on access**, which has now
saved this project twice.

**Var and flag ids are the real scarcity.** Every id this project holds is an
alias over the "unused" pool, so the pool still calls it unused and a collision
does not fail to build — it silently rewrites the run. Eight var ids remain
(`0x404E`, `0x409D`, `0x40A1`, `0x40A8`, `0x40B8`, `0x40BB`, `0x40DB`, `0x40DC`).
Check `constants/rogue_dungeon.h`, not `vars.h`.

---

## 14. Tooling and verification

**~150 Python tools in `tools/rogue/`** — theme and art generators, sheet
importers, MIDI and SDAT tooling, party generators, budget readers, and the
invariant checks. Run them with `bash tools/rogue/run_all_checks.sh`, which
globs `tools/rogue/check_*.py`, `tools/rogue/verify_*.py` and
`tools/mode7/check_*.py`.

**63 checks today** — 53 `check_*.py`, 5 `verify_*.py`, 5 under `tools/mode7/`.
Counted from the directory, because that line has drifted repeatedly.

Three things worth knowing about them:

1. **A check that has never failed is worth nothing.** The pattern to copy is
   `check_berry_rot.py`, which carries its own break test via `--selftest` — it
   runs its measurement against a deliberately biased hash and fails if that
   still passes, so "has this check ever fired?" is answered by the tool rather
   than by memory. Several checks have been caught passing **vacuously** —
   matching an identifier that also appears in a comment, or a field assignment
   rather than the `if` that guarded it — and every one was found by the selftest
   and by nothing else. **Assert the statement or the call, never the identifier.**
2. **Some check a lifetime rather than data.** `check_jukebox_funnels.py`,
   `check_portrait_lifetime.py`, `check_flier_lifetime.py`,
   `check_bw_buffer_eviction.py`, `check_boss_regions.py`,
   `check_sprite_palette_reset.py`. The technique is to name the ownership rule at
   the top of the file, then assert the pairing that makes it true. Read one of
   these first if you are guarding something that fails by crashing, by being the
   wrong colour, or by going quiet, rather than by being wrong in a table.
3. **The limit is narrow and specific.** A passing check says nothing about memory
   lifetime, VRAM, or an index into an engine table — that is where the
   nineteen play-found fixes all lived. It says a great deal about generated data,
   and the checks catch real bugs before hardware. Do not re-derive the claim that
   they "mean almost nothing".

**A check for a generated structure should assert the property, never re-run the
algorithm** — `check_dungeon_plane.py` replayed the C's dilation and so agreed
with its bug; it now BFS-computes distance to the nearest floor instead.

### Debug surfaces

- **Utility → `Rogue floor warp…`** — warp to any floor. SELECT toggles
  `sDebugForceAlphaEvent` (sticky) so Alphas can be walked to. The override is
  applied **after** the weighted draw and the odds roll still runs, so the debug
  floor is the same floor as the real one of that seed.
- **Utility → `Rogue run state…`** — A toggles the completion flags *both ways*,
  SELECT rerolls the order and region words, the d-pad steps the slots and names
  who stands at each through `BossRowForSlot`.
- **Utilities → `Rogue charms…`** — grant any charm to any slot.
- **`ROGUE_DEBUG_OBJECT_CENSUS`** — records peak live object events and template
  spawn *refusals* as they happen, so one lap of a floor answers a question that
  is invisible from inside the game by construction.

---

## 15. Known gaps and what is next

### Open, with a known technique

- **No check guards var/flag id collisions.** Three projects have now raided the
  same tail. `check_id_claims.py` was written and withdrawn unfinished — union-find
  over alias edges merges two independent claimants that both alias the same pool
  name. Fix is to not union across a pool name. Until then the rule is manual.
- **Nothing guards the boss-vanish anti-brick invariant.** If the abandoned ace
  ever stops inheriting a script that reaches `BossExit`, a boss arena becomes
  inescapable and no check will notice. `check_boss_vanish.py` is unwritten.
- **Heap teardown and DMA lifetime checks** — pair every `InitWindows` with a
  `FreeAllWindowBuffers` (catches the Game Corner double free); flag a `Free` of a
  buffer last passed to `LoadBgTiles` in the same function (catches the fishing
  corruption).
- **`DUNGEON_GEN_FACILITY` is written but no theme uses it**, so
  `check_facility_floor.py` passes vacuously — it prints "nothing to check" and
  returns 0. Give the generator a theme or retire the check.
- **`verify_dungeon_gen.py` does not cover the woods** — it ports the cave only.
- **Underwater has no one-block-thick wall art**; the woods has no floor patch and
  cannot get one from vanilla (the only nine-sliced region material in
  `gTileset_General` is Route 116's *elevated* terrain, which draws a plateau in
  the middle of a flat wood).
- **The jungle's water is static** and its Sparkle overlay is unimported. The
  Flower Meadow dropped its water for the same reason — 94 tiles and a whole
  palette for a *static* pond.
- **Mon icons have no colour variant**, so the nickname screen shows stock
  colours while the battle sprite does not.

### Content

- **Winning is recorded but not rewarded.** The party and bag are still wiped and
  nothing carries forward. What is missing is content *behind* the flag, not the
  flag — the difficulty ladder is the obvious next thing, since band width is
  already a tier and check 7 proves any given width safe.
- **A shiny-encounter badge** on the continue window, complementary to the run
  counter. Constrained by there being no third row — it has to share the
  `CLEARED` row. A var is free; the open question is what it counts, and *caught*
  is the one worth arguing for.
- **A second Game Corner counter selling Pokémon.** The stone counter's
  derive-by-offset trick does not transfer — species ids are not contiguous in any
  order worth selling, so this needs a real (species, level, price) table.
- **A form-changer NPC at the rest stop** — the mechanism that turns
  `gen_safari_pool.py`'s 209 dropped alternate forms into content, and the planned
  route for Alolan Raichu.
- **Pokémon Pink features for a Clefairy run.** The groundwork is in. Note that
  Yellow's defining hook — Pikachu refusing to evolve — does **not** transfer: it
  would directly undo the rebuilt Clefairy learnsets. If the refusal is wanted it
  has to be the player's choice with a reward attached, the shape Volt Tackle
  already uses.
- **Rotom's Workshop** is the one drafted event not built; the draft is a title
  and one line, with no choices in it.

### Flagged risks

- `RecalcParty` is called from `RogueCharm_OnBattleEnd`, which runs inside
  `FreeResetData_ReturnToOvOrDoEvolutions`, and `CalculateMonStats` writes
  `gBattleScripting.levelUpHP` on its way through. The reasoning that this is safe
  was not verified on hardware. If post-battle evolutions or level-up HP readouts
  behave oddly, **suspect this first.**
- Forty-five of the fifty-six mugshot pics are **untuned** — the mugshot forces
  `SPRITE_SHAPE(64x32)`, so only a window of the 64×64 front pic shows. Untuned is
  a shipping state (Sidney and Phoebe ship on bare defaults), not a gap, but they
  are the ones to look at.
- **377 of the 386 BW-animated species have never been on a screen.** For anything
  writing OBJ VRAM, that is the only test that has ever caught anything.

---

## 16. Companion documents in this repo

| doc | what it is |
|---|---|
| `docs/ROGUELIKE.md` | a stub pointing at the `rogue-dungeon` skill |
| `docs/GAME_CORNER_PORT.md` | the real reference for the nine-minigame port |
| `docs/GAME_CORNER_CANDIDATES.md` | what is built, what is free in the ROM, what was rejected |
| `docs/AUDIO_CREDITS.md` | who wrote every imported song, and what was deliberately not imported |
| `docs/PROGRAM_13_SPLIT.md` | **generated** — which songs get a drum kit at GM program 13. Edit the tool, not the doc |
| `docs/KANTO_LEADERS.md` | the Kanto thirteen |
| `docs/JOHTO_SINNOH_SPRITES.md` | staged art and all forty-two spriter credits |
| `docs/NEWMAUVILLE_TILESET.md` | how New Mauville's walls and set pieces are really built |
| `docs/RUN_JOURNAL.md` | the journal's full design record |
| `docs/COLOUR_VARIANTS.md` | the OkLCH per-personality colour shift |
| `docs/ASSET_REPO_INDEX.md` | Team Aqua's asset repo, indexed with conversion costs |
| `tools/rogue/README.md` | the tool inventory |

`git log origin/master..HEAD` is the story in order, and the commit messages
carry the reasoning.
