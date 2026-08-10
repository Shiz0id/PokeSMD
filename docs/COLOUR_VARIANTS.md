# Colour variants

Third-party code, so this document sits beside it in the repo rather than in
the skill — the same reason `GAME_CORNER_PORT.md` does.

## Where it came from

`SpaceOtter99/pokeemerald-expansion@a9bf6cc`, branch `colour-variants`. The
feature commit is two new files, 478 insertions, **zero deletions** — it hooks
nothing, so upstream ships a library and every call site here is ours.

The colour maths is SpaceOtter99's, on Bjorn Ottosson's OKLab
(<https://bottosson.github.io/posts/oklab/>). **A shipping build has to carry
that credit somewhere the player can reach**, alongside the Mega Recurso divers
and volromhacking's mining graphics.

`1fae6179` vendors it unmodified and unreferenced, so `git diff 1fae6179..HEAD
-- src/variant_colours.c include/variant_colours.h` is exactly what we changed
and nothing else.

## What it does

Shifts a palette in OkLCH — polar OKLab — rather than blending in RGB, so a hue
rotation keeps its contrast instead of washing toward grey. The shift is derived
from the mon's **personality value**, so it is deterministic: the same mon is the
same colour every time it loads, for as long as it exists.

Runtime maths is integer only. The `Q8_8` float macros resolve at compile time.

## Where it is hooked, and the one that matters

| hook | file | covers |
|---|---|---|
| `GetMonSpritePalFromSpeciesAndPersonality` | `pokemon.c` | party, summary, box, pokedex, evolution, battle load |
| `GetMonSpritePalFromSpeciesAndPersonalityIsEgg` | `pokemon.c` | as above, plus the egg test |
| `RogueBwAnim_OnLoadSprite` | `rogue_bw_anim.c` | **the 772 BW animated species** |

**The third one is not optional and it is the whole reason this was not a
one-line change.** `RogueBwAnim_OnLoadSprite` loads the palette that shipped
inside the BW frame container and deliberately overwrites the one the caller
just set — its own comment has said so since it was written. Hooking only the
generic getter gives you variants everywhere *except* the battle screen, and
only for the animated species, which are most of the ones a run meets. It would
have built clean and passed every check.

`isShiny` and `personality` are passed **down** from `BattleLoadMonSpriteGfx`
rather than re-derived, because those locals are already corrected for illusion
and for transform. A Ditto takes the colour of what it copied.

## What deliberately gets no variant

- **Eggs.** An egg's palette is the egg's. Shifting it would make every egg a
  slightly different colour *as a function of the mon inside it*.
- **Shinies.** A shiny is the one colour signal in a run meant to carry
  information. Upstream shifts them — `ApplyMonSpeciesVariantToPaletteBuffer`
  takes a `shiny` argument and never reads it. `VARIANT_COLOURS_SHINY` is FALSE.
- **`GetMonSpritePalFromSpecies`**, which takes `isFemale` rather than a
  personality and so has nothing to derive a shift from. **This leaves the
  overworld follower and the sprite visualiser on stock colours, and that is a
  gap rather than a decision** — a follower would need the PID threaded to it.

## The table is empty on purpose

`gSpeciesVariants` has no entries. Every species takes the default: a hue swing
of ±10° across indices 1..15.

Upstream's five Hoenn entries were its own sample content and were dropped, the
same as the crafting merge's Hoenn recipes. **A per-species entry is a claim
about which palette indices are which part of the sprite**, and that claim
cannot be made from a species name — `PAL1(11, 3)` on Tyranitar asserts that
indices 11–13 are worth hue-shifting, and nothing here tells you whether that is
the belly plates or the eyes. Aimed at the wrong three indices it does not fail;
it produces a mon with wrong-coloured eyes, on a screen, months later.

The default is safe on any sprite for a reason worth keeping: outlines, eyes and
shading are near-grey, hue is meaningless at zero chroma, so the shift lands on
the coloured body and leaves the drawing alone.

To author an entry, render the species' palette, decide the range from what is
in it, and add it. Do not reason from the name.

## Config

All in `include/variant_colours.h`.

| define | default | effect |
|---|---|---|
| `VARIANT_COLOURS` | TRUE | FALSE restores stock palettes everywhere, no other edit |
| `VARIANT_COLOURS_SHINY` | FALSE | whether shinies are shifted too |
| `VARIANT_PAL_RING` | 4 | how many mons may hold a variant palette pointer at once |

## The ring, and why it exists

The getter used to return a pointer into `gSpeciesInfo` that lives forever. It
now returns one of `VARIANT_PAL_RING` RAM slots.

Every call site but one consumes it on the next line — `LoadPalette`,
`LoadSpritePaletteWithTag`, `memcpy`. The exception is
`pokemon_storage_system.c`'s `displayMonPalette`, set in one function and read in
another a frame or more later. That is safe because only one display mon exists
at a time and box icons use a different palette path entirely, so nothing claims
a slot in between. **Four slots is slack against a worst case of one.** If a
future screen shows two full mon sprites at once from held pointers, raise it.

## What it cost

| | before | after | delta |
|---|---|---|---|
| EWRAM | 239,196 | 239,328 | **+132** |
| IWRAM | 28,460 | 28,460 | **+0** |
| ROM | 29,081,944 | 29,097,760 | **+15,816 (+0.05%)** |

The 132 bytes are the ring and its index. The ROM is mostly the
`NUM_SPECIES`-sized table, which costs the same whether it has entries or not.

**IWRAM not moving is the point of one of the fixes.** Upstream puts the three
lookup tables and `Cbrt` in IWRAM; there are 4,308 bytes left in that region and
the tables are 544 of them, to speed up code that runs when a sprite appears
rather than once a frame.

## Checks

`tools/rogue/check_variant_colours.py <repo>` — **positional repo argument**,
like `check_safari_pool.py`, `check_species_in_rom.py` and
`check_craft_recipes.py`. That makes **four** that a runner passing `--repo`
uniformly reports as spurious failures, not three.

It holds four things, each silent otherwise: hue/chroma/luma values must come
from the legal discrete sets; a palette range must fit in 16 colours; a species
named must have a `gMonFrontPic` symbol in `pokeemerald.map`; and an entry must
actually ask for a shift.

Broken five ways on purpose and fires on all five. **With the table empty it
passes vacuously** — that is what the break test is for, and it is the reason to
re-run it after adding the first real entry.

## Measured on the emulator

`test/variant_colours.c`, nine tests, run with:

```bash
make check TESTS="variant colours"
```

They run the real code on real hardware, which is the only honest way to
exercise this: the pipeline leans on `Sin`, `Cos`, `ArcTan2` and `Sqrt`, and a
host reimplementation of those would be a model of the maths rather than the
maths.

**Round-trip fidelity at zero shift.** The concern was that every species
without a table entry goes through the full conversion, so a lossy round trip
would recolour the whole dex before any variant applied. Measured as max
channel error out of 31:

| colour family | error | |
|---|---|---|
| greys, outlines, near-black | **0** | exact |
| muddy mid-tones | **1** | imperceptible, and this is what sprites are made of |
| fully saturated primaries | **6** | gamut clipping, see below |

**The 6 is gamut clipping, not lost precision.** Chroma is stored doubled in a
`u8`, and a fully saturated RGB555 primary drives that past 255, so it clamps
and returns less saturated. It is a corner of the colour space that Pokémon
palettes almost never contain. **Measuring this as one aggregate actively hid
the answer** — a single worst-of-15 reads "error 6" and makes the conversion
look broken, when the colours that matter come back at 1 and 0. Per-instance,
never in aggregate.

## Why the default is hue 30 + chroma 25 + luma 25

**This shipped as a hue-only shift and was reported from play as looking like
stock colours, on a wild Aron.** Every host-side number said the feature worked,
and it did — it was just invisible.

**Hue is meaningless at zero chroma.** The more desaturated a species is, the
less a hue rotation can do to it, and a great many of the species a run meets in
caves and on rock floors are grey. Mean channel delta out of 31, over a full
16-bit PRN sweep against the real palettes:

| setting | Aron (grey) | Zubat (coloured) |
|---|---|---|
| hue 20 alone *(as shipped)* | 0.87 | 1.87 |
| hue 20 + chroma 10 + luma 10 | 1.33 | — |
| hue 30 + chroma 25 + luma 10 | 1.77 | 3.18 |
| **hue 30 + chroma 25 + luma 25** | **2.32** | **3.93** |
| hue 45 + chroma 25 + luma 25 | 2.46 | — |

The shipped setting moved a colour by **under 3% of its range**, and a grey
species got **less than half** what a coloured one did from the same numbers.
Chroma and luminance are the axes that work on a desaturated palette: chroma
tints a grey, luminance lightens or darkens it, and neither cares about hue.

**45° was rejected.** It buys Aron almost nothing over 30° — 2.46 against 2.32,
because hue is the axis with the least left to give here — while rotating a
coloured species far enough to start reading as a shiny.

End to end through the shipped path, against real palettes:

| | max delta /31 | colours moving /15 |
|---|---|---|
| Aron, hue-only | 6 | 9 |
| **Aron, now** | **13** | **12** |
| **Zubat, now** | **17** | **15** |

**15 is not a legal hue value.** The set is `{0, 10, 20, 30, 45, 60, 90, 180}`,
and `HUE_INDEX` is a ternary chain with no else branch, so 15 falls through to
the `<= 20` case and silently yields 19.7°. `check_variant_colours.py` fails on
anything off the set precisely so that substitution is never made silently.

## Two measurement traps that both produced confident wrong answers

**The PRN is sixteen bits and each field owns a slice of it** — hue in bits
0..6, chroma in 7..9, luminance in 10..12, directions in 13..15. Sweeping PIDs
`0..127` exercises **hue and nothing else**, pinning chroma and luma to zero
however large the entry asks for them to be. A calibration run done that way
reported that adding chroma and luminance changed the mean by *exactly nothing*.
`PRN_STRIDE` sweeps the full range.

**Max hid the answer where mean did not.** The shift is drawn uniformly from
`0..hmax`, so half of all Pokémon get less than half the headline figure. A
max-of-everything said the hue-only setting moved Aron by 6 levels; the mean
said 0.87, which is what a player actually sees. Same rule as the healing-power
average: measure per instance, and never average across mixed categories.

## Not on a screen

Still unverified, and this is a feature whose entire output is pixels:

- whether 20° reads as *variety* or as *wrong* on an actual sprite in motion
- whether a variant Pokémon reads as a shiny to a player at a glance
- the transformed and illusion paths, which pass a substituted PID — covered by
  reasoning and by the BW tests continuing to pass, not by looking
- the overworld follower, which has no personality threaded to it and so is
  still on stock colours while its battle sprite is not. **That inconsistency is
  visible in normal play** and is the most likely thing to be noticed first.
