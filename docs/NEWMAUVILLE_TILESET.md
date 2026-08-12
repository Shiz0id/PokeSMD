# The New Mauville tileset, and how its walls are actually built

Reference for the `DUNGEON_THEME_NEWMAUVILLE` art. Every figure here is a census
of `NewMauville_Inside_Layout` and `NewMauville_Entrance_Layout`, not a reading
off a contact sheet. Tileset is `gTileset_General` + `gTileset_BikeShop`; the art
is in `data/tilesets/secondary/bike_shop/`. Prototypes: `tools/rogue/newmauville/`.

**All of it is landed except the column head.** Headroom: 248/512 metatiles used
(264 free), 242/512 secondary tiles (270 free), palette slot 12 free. None were
needed.

## The wall is two metatiles tall — LANDED

| row | mid | west end | east end |
|---|---|---|---|
| cap (white top surface) | `0x21F` | `0x28C` | `0x28B` |
| face (tan body, brown base) | `0x227` | `0x294` | `0x293` |

A face with wall above it is capped 35/35; `0x28C` sits over `0x294` 8/8 and
`0x28B` over `0x293` 8/8. **No neighbour mask can find a cap** — its eight
neighbours are all wall, like deep interior — so `WallCapFor()` tests geometry:
this cell and the one below wall, the cell two below floor. Tested *after*
`openNorth`, because vanilla never caps a wall it can see over the top of.

## Convex corners are not void — LANDED

| open diagonal | SE | SW | NE | NW |
|---|---|---|---|---|
| metatile | `0x270` 59% | `0x272` 47% | `0x280` 57% | `0x282` 73% |

Vanilla continues the column through the corner. `VOID` in these four slots is
what notched every room outline to black.

## The column stands on a base two metatiles tall — LANDED

`0x290` body → `0x298` shoulder → `0x2A0` flat brown base on the floor → `0x27F`
skirt beneath it. 3/3 over one-wide runs. The theme ended at the shoulder, so the
block meeting the floor was never drawn; `WALL_SLIVER_VERT_BOT_UPPER` is the new
slot, 0 falling back to `WALL_SLIVER_VERT`.

## The column head still needs a new metatile — OPEN

`0x268`/`0x26A` win the census at 47%/50% and their top third is pure black, so
painting one is pixel-identical to leaving the void; `0x295`/`0x296` read as a
block stuck on. Vanilla has no piece because its walls always terminate against
something. The fix is a splice — two metatile entries, no new 8x8 tiles:

```
0x21F top = [t20B, t20B, t212, t213]   the cap row's own surface
0x268 top = [----, t20B, ----, t216]   capped stripe, left half empty <- the black
NEW   top = [t20B, t20B, t212, t216]   surface on the left, cap on the right
```

## Decorations are three rows, not one — LANDED

| decoration | cap row | wall face | floor below | needs |
|---|---|---|---|---|
| bookcase (2 wide) | `0x299` `0x29A` | `0x2A1` `0x2A2` | `0x2A9` `0x2AA` | cap above |
| console (2 wide) | — | `0x2D6` `0x2D7` | `0x2DE` `0x2DF` | cap above |
| crate shelf (2 wide) | `0x2A8` — | `0x2B0` `0x2B1` | `0x2B8` `0x2B9` | cap above |
| box shelf | — | `0x2B2` | `0x2BA` | cap above |
| counter | — | `0x2B4` | `0x2BC` | cap above |
| crate unit | — | `0x2B3` | `0x2BB` | cap above |
| vent | — | `0x277` | — | **floor above** |

`RogueWallStamp` replaces `RogueDecor` here; running both would put two
differently-aligned sets of the same objects on one wall. The vent keys on floor
*above* because its art is drawn against the room above a thin partition.
`0x2A0` is excluded: it is flat brown wall mass, the column base above.

## Density is per stamp, not per theme — LANDED

Censused: 42 decoration units over 182 visible wall faces = **23%** (not the 59%
the prototype claimed), and 7 vents on 53 thin partitions = 13%. One shared gate
put a vent on *every* thin partition, because the vent is the only piece that
fits one. So: choose among fitting pieces first, then gate on that piece's own
chance.

## Set pieces are collision, not paint — LANDED

| piece | size | ids |
|---|---|---|
| generator | 4x4 | `0x2D0..0x2D3` / `0x2D8..0x2DB` / `0x2E0..0x2E3` / `0x2E8..0x2EB` |
| supercomputer | 2x3 | `0x2D4` `0x2D5` / `0x2DC` `0x2DD` / `0x2E4` `0x2E5` |
| crate stacks | 1x1..2x2 | `0x2C0`..`0x2C3`, free-standing, clear ring |

Top row replaces capped wall face, body projects over floor, solid in every
cell — registering `0x2E4`/`0x2E5` as a decoration drew a headless machine.
Placement is flood-filled and **rolled back** if it splits the floor (3 of 200
seeds, once 526 cells into 263 + 251), and refuses the stairs, spawn, trainers,
items, rocks, berries and buried items.

`0x2F0`-`0x2F7` is an alternate generator bottom vanilla never places.

## Carve knobs

The theme runs `DUNGEON_GEN_CAVE` at the defaults (rooms 10, size 5-10). It ran
`DUNGEON_GEN_FACILITY` for a while and was moved back, because thin partitions
give wall furniture nothing to hang off. Candidate spots per floor, 60 floors of
each:

| piece | facility | cave |
|---|---|---|
| generator 4x4 | **0.00** | 4.48 |
| supercomputer 2x3 | 1.10 | 16.43 |
| any 1x1 wall mount | 3.90 | 54.87 |

Zero is not "rare", it is never. An older table here compared void fraction
across `rooms=8/16/30`; its one surviving conclusion is that **void fraction is
not a proxy for legibility** — `rooms=30, 4-7` matched vanilla's void and
produced an unreadable maze. Vertical run density tracked it far better.

## Three metrics that lied

Each passed while the thing it measured was broken, because each was a derived
number standing in for looking at the render.

- **void %** drove the carve to 26% and produced the maze.
- **"bare column tops"** tested `metatile above != 0x208`; writing `0x268` there
  passed it by definition while changing nothing on screen.
- **"black in the top 4 pixel rows"** reports vanilla's own cap `0x21F` at 100%.

What worked was cropping the output, scaling it 5x, and looking.

## Checks

`check_wall_caps.py` derives what must be painted from the **carve**, not from
the dispatch it is checking, so reordering a branch makes it fire. It runs two
fixtures because one alone is vacuous: generated floors for the caps, and
vanilla's own collision for the column, which generated floors barely contain.
Verified by breaking it three ways.
