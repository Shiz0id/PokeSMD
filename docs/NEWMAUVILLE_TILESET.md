# The New Mauville tileset, and how its walls are actually built

Reference for the `DUNGEON_THEME_NEWMAUVILLE` art. Everything here was derived
by censusing `NewMauville_Inside_Layout` and `NewMauville_Entrance_Layout`
rather than read off a contact sheet. **Nothing in this document has been
landed in C yet** — it records findings and a validated prototype.

The tileset is `gTileset_General` + **`gTileset_BikeShop`**. There is no
`gTileset_NewMauville`; the art lives in `data/tilesets/secondary/bike_shop/`.

Headroom, measured: **248/512 metatiles used (264 free)**, **242/512 secondary
tiles referenced (270 free, 258 of them blank)**, palette slot 12 free.

## The wall is two metatiles tall

The single most important fact, and the one no neighbour-mask census can find.

| row | mid | west end | east end |
|---|---|---|---|
| cap (white top surface) | `0x21F` | `0x28C` | `0x28B` |
| face (tan body, brown base) | `0x227` | `0x294` | `0x293` |

A wall cell with floor to the south is capped **100% of the time** (35/35) when
the cell above it is wall. `PaintWalls()` paints only the face row, so the white
top surface that ties a horizontal wall to a vertical one never exists.

**A cap position cannot be identified by a neighbour mask.** Its eight
neighbours are all wall, exactly like deep interior; what identifies it is
*distance to floor going south*. Define it geometrically — this cell and the one
below are wall, the cell two below is floor — which also caps the corner
columns and is what closes the top corners.

`derive_wall_table.py` reports `0x208` (void) at 62.8% for that mask. The cap
row is inside the other 37%. Reading that as "no answer" is how this was missed.

## Convex corners are not void

Censused on "all four cardinals wall, exactly one diagonal floor":

| open diagonal | metatile | share |
|---|---|---|
| SE | `0x270` | 59% |
| SW | `0x272` | 47% |
| NE | `0x280` | 57% |
| NW | `0x282` | 73% |

Vanilla continues the column through the corner. The theme's `VOID` in all four
slots is why room outlines notch to black.

## Vertical runs need a head and a foot

Censused over **maximal runs**, not over every cell of a run — the latter only
tells you the run continues (39%, which reads as "no answer" and is not one).

- foot, below the last cell: `0x278` / `0x27A` (27% / 36%). Correct as-is.
- head, above the first cell: **no vanilla piece is correct.**

`0x268`/`0x26A` win the census at 47%/50% but their top third is **pure black** —
vanilla uses them where the mass genuinely continues into a dark recess.
Painting one above a column is pixel-identical to leaving the void. `0x295`/
`0x296` are opaque but carry half a tan wall; they are corner pieces and read as
a block stuck to the column.

Vanilla has no piece for a column ending inside a wall mass because its walls
are hand-placed and always terminate against something. **This needs a new
metatile.** It is a splice — no new 8x8 tiles:

```
0x21F top = [t20B, t20B, t212, t213]   the cap row's own surface
0x268 top = [----, t20B, ----, t216]   capped stripe, left half empty <- the black
NEW   top = [t20B, t20B, t212, t216]   surface on the left, cap on the right
```

Mirror for the west-facing run: `[t20B, t20B, t216^x, t213]`.

Known remaining defect: the head caps the column but does not close the join to
the cap row beside it, leaving a small black notch at a room's outer corners.

## Decorations are three rows, not one

`RogueDecor` is `{base, variant, variantEast}` — one row, paired horizontally.
Every vanilla wall decoration occupies a cap row, the wall face, and the floor
below, and the bookcase is two of those side by side. Bookcases are currently
painted headless *and* footless.

| decoration | cap row | wall face | floor below | requires |
|---|---|---|---|---|
| bookcase (2 wide) | `0x299` `0x29A` | `0x2A1` `0x2A2` | `0x2A9` `0x2AA` | wall above |
| console (2 wide) | — | `0x2D6` `0x2D7` | `0x2DE` `0x2DF` | wall above |
| crate shelf (2 wide) | `0x2A8` — | `0x2B0` `0x2B1` | `0x2B8` `0x2B9` | wall above |
| box shelf | — | `0x2B2` | `0x2BA` | wall above |
| counter | — | `0x2B4` | `0x2BC` | wall above |
| crate unit | — | `0x2B3` | `0x2BB` | wall above |
| vent | — | `0x277` | — | **floor above** |

The vent is the odd one: vanilla only ever puts it on a one-thick partition
(above it is `0x26F`, passable, 100%). On a thick wall its art floats in the
middle of the band, because it is drawn against the floor above it. Match the
placement rule to what the art is drawn against.

The old single-row pass must be **replaced, not layered under** — running both
puts a second, differently-aligned set of the same objects on the wall.

## Set pieces are collision, not paint

Two objects are not decorations. Their top row sits in the wall and their body
projects out over the floor, **solid in every cell**.

| piece | size | ids |
|---|---|---|
| generator | 4x4 | `0x2D0..0x2D3` / `0x2D8..0x2DB` / `0x2E0..0x2E3` / `0x2E8..0x2EB` |
| supercomputer | 2x3 | `0x2D4` `0x2D5` / `0x2DC` `0x2DD` / `0x2E4` `0x2E5` |

`0x2E4`/`0x2E5` were registered as a wall decoration with `0x2DC`/`0x2DD` as its
"cap". That is the bottom two thirds of a 2x3 machine with its head never
placed — it draws headless and flush into the wall.

**Placement must be connectivity-checked.** Turning floor into collision cut the
walkable area in two on **3 of 200 seeds**, once splitting 526 cells into
263 + 251. The stairs land on the far side and nothing reports it.

A third category exists that we have none of: free-standing crate stacks
(`0x2C0`..`0x2C3`), solid, standing in open floor away from any wall.

`0x2F0`-`0x2F7` is a 4-wide x 2-tall alternate generator bottom that vanilla
never places, with no top row present in either layout.

## Carve knobs

The theme sets none, so it inherits the cave's defaults (`rooms=8`, size 5-10)
and scatters eight rooms across 48x48. Measured against vanilla:

| | vanilla | rooms=8 5-10 | rooms=16 7-12 | rooms=30 4-7 |
|---|---|---|---|---|
| pure black void | 24.1% | 53.0% | 39.8% | **25.7%** |
| vertical runs per cell vs vanilla | 1.00x | 0.83x | **0.96x** | 1.81x |

**`rooms=30, room_min=4, room_max=7` matches the void target and produces an
unreadable maze.** `rooms=16, room_min=7, room_max=12` looks correct and scores
worse on void. Void fraction is not a proxy for legibility; vertical run density
tracked it far better. Larger rooms, not smaller.

The residual black is structural: every room in this carve is an island with its
own wall skin, because placement mandates a padding gap. Vanilla's rooms share
partitions. Closing that needs a BSP-style carve, which changes dungeon
topology, not just art.

## Verification, and three metrics that lied

Every check below passed while the thing it measured was broken. All three were
derived numbers standing in for looking at the render.

- **void %** drove the carve to 26% and produced the maze.
- **"bare column tops"** tested `metatile above != 0x208`. Writing `0x268`
  there passed it by definition while changing nothing on screen.
- **"black in the top 4 pixel rows"** reports **vanilla's own cap row `0x21F`
  at 100%**. It condemns the real game.

What worked was cropping the output, scaling it 5x, and looking. `0x268` versus
a correct head is obvious at that size and invisible in every metric above.

Checks worth keeping, because they fire on real breakage:

- set-piece connectivity (fires 30/30 when the body is left passable)
- stamp completeness — cap/face/floor counts must agree per half (fires 30/30)
- headless set-piece rows — any body row without its own top directly above
