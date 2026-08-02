#ifndef GUARD_ROGUE_DUNGEON_H
#define GUARD_ROGUE_DUNGEON_H

// Self-contained: the prototypes below name enum WildPokemonArea, so this must
// not rely on the includer having pulled it in first.
#include "wild_encounter.h"

// Metatile ids for the General + Cave tileset pair, derived by mining all 62
// vanilla General+Cave layouts rather than picked by eye.
//
// 0x201 is the MB_CAVE floor, so wild encounters work on it.
//
// Walls form a 3x2 autotile block. The "face" row is wall with floor directly
// below it - the part the camera actually sees - and the "interior" row is the
// bulk behind it. Columns vary by whether the neighbouring cell is open:
//
//                left    middle   right
//   interior     0x210   0x211    0x212
//   face         0x218   0x219    0x21A
//
// Evidence: 0x219 appears with walls on both sides 94% of the time (middle),
// while 0x210/0x218 favour an open west neighbour and 0x212/0x21A an open east.
#define DUNGEON_METATILE_FLOOR 0x201
#define DUNGEON_METATILE_VOID  0x200

#define DUNGEON_METATILE_WALL_INTERIOR_LEFT   0x210
#define DUNGEON_METATILE_WALL_INTERIOR_MID    0x211
#define DUNGEON_METATILE_WALL_INTERIOR_RIGHT  0x212
#define DUNGEON_METATILE_WALL_FACE_LEFT       0x218
#define DUNGEON_METATILE_WALL_FACE_MID        0x219
#define DUNGEON_METATILE_WALL_FACE_RIGHT      0x21A

// North-facing edge - wall with floor ABOVE it, i.e. the bottom boundary of a
// room. Without these the outline breaks and rooms stop reading as enclosed,
// because the interior fill is near-identical to the floor.
#define DUNGEON_METATILE_WALL_NORTH_LEFT      0x220
#define DUNGEON_METATILE_WALL_NORTH_MID       0x209
#define DUNGEON_METATILE_WALL_NORTH_RIGHT     0x222

// Outer corners: every cardinal neighbour is wall but a diagonal is floor.
// Without these a room's top corners fall through to interior fill and the
// outline shows a notch. Named for the room corner they sit at, so
// _CORNER_NW is the block diagonally up-left of a room's top-left floor tile
// and therefore has its SOUTH-EAST diagonal open.
#define DUNGEON_METATILE_WALL_CORNER_NW       0x21B  // open SE diagonal
#define DUNGEON_METATILE_WALL_CORNER_NE       0x21C  // open SW diagonal
#define DUNGEON_METATILE_WALL_CORNER_SOUTH    0x223  // open NW or NE diagonal

// One-block-thick walls, which vanilla has no art for - its cave walls are
// always at least two thick. Appended to the Cave secondary tileset rather
// than drawn: a metatile is only 8 references to existing 8x8 tiles, so these
// splice the west-facing half of 0x210 to the east-facing half of 0x212, and
// the rock top of 0x209 to the wall face of 0x219. See
// scratchpad/compose_metatiles.py.
#define DUNGEON_METATILE_WALL_SLIVER_VERT     0x39E  // floor to both west and east
#define DUNGEON_METATILE_WALL_SLIVER_HORZ     0x39F  // floor to both north and south

// End caps. A sliver that also has floor at one end needs that end closed, or
// the run stops abruptly mid-air.
#define DUNGEON_METATILE_WALL_SLIVER_VERT_TOP 0x3A0  // + floor north
#define DUNGEON_METATILE_WALL_SLIVER_VERT_BOT 0x3A1  // + floor south
#define DUNGEON_METATILE_WALL_SLIVER_HORZ_L   0x3A2  // + floor west
#define DUNGEON_METATILE_WALL_SLIVER_HORZ_R   0x3A3  // + floor east
#define DUNGEON_METATILE_WALL_SLIVER_ISOLATED 0x3A4  // floor on all four sides

// Pale sand lying on the cave floor, as a 3x3 region autotile. Vanilla uses it
// for Shoal Cave's tidal beach, the Desert Underpass and Altering Cave - four
// General+Cave layouts in all - so it is the tileset's own idea of a floor
// patch rather than something invented here. All twelve are MB_CAVE, like the
// floor, so encounters are unaffected.
//
// The _WALL row is the top edge where a wall sits above instead of floor: the
// wall's base is baked into the art, which is why it cannot be shared with the
// plain top edge.
#define DUNGEON_METATILE_SAND_NW      0x298
#define DUNGEON_METATILE_SAND_N       0x299
#define DUNGEON_METATILE_SAND_NE      0x29A
#define DUNGEON_METATILE_SAND_W       0x2A0
#define DUNGEON_METATILE_SAND_MID     0x2A1
#define DUNGEON_METATILE_SAND_E       0x2A2
#define DUNGEON_METATILE_SAND_SW      0x2A8
#define DUNGEON_METATILE_SAND_S       0x2A9
#define DUNGEON_METATILE_SAND_SE      0x2AA
#define DUNGEON_METATILE_SAND_NW_WALL 0x29B
#define DUNGEON_METATILE_SAND_N_WALL  0x29C
#define DUNGEON_METATILE_SAND_NE_WALL 0x29D

// Petalburg Woods: gTileset_General + gTileset_Rustboro. Mined from
// LAYOUT_PETALBURG_WOODS the same way the cave values were.
//
// Trees are 2x2 blocks on even coordinates - 95% x-aligned and 99% y-aligned in
// vanilla - which is why the woods generator works on a half-resolution grid
// and needs no autotiling at all.
#define WOODS_METATILE_GRASS      0x001  // plain, no encounters
#define WOODS_METATILE_TALL_GRASS 0x00D  // MB_TALL_GRASS

// The woods has no long grass. It once did, ended with 0x016/0x017 - but those
// are SOLID in all 691 of their vanilla placements across every General+Rustboro
// and General+Fortree layout, always sitting under the leafy canopy
// 0x0C6/0x0C7 with plain grass below. They are a canopy base row, and drawing
// them passable put a walkable hedge fragment under every long grass patch.
//
// The reasoning that produced it is worth remembering: they sit immediately
// after 0x015 in the metatile grid. That is adjacency, not evidence, and it is
// the same mistake that nearly made the tree crown a grass base row.
//
// There is no correct replacement here: gTileset_General contains no
// MB_LONG_GRASS_SOUTH_EDGE metatile at all. The one that exists, 0x208, is in
// gTileset_Fortree, so long grass belongs to the jungle - which is also where
// vanilla puts it, Petalburg Woods having none.
#define WOODS_METATILE_TREE_TL    0x1D4
#define WOODS_METATILE_TREE_TR    0x1D5
#define WOODS_METATILE_TREE_BL    0x1DC
#define WOODS_METATILE_TREE_BR    0x1DD

// A tree is really 2 wide by 3 tall. The third row is the ground contact, used
// only where a tree mass ends and open ground begins - across nine vanilla
// General+Rustboro layouts, EVERY tree bottom row either has another tree below
// it or sits at the map edge, so vanilla never leaves 0x1DC exposed. Ending a
// mass on 0x1DC is what leaves the trunk tip dangling.
#define WOODS_METATILE_TREE_BASE_L 0x1E4
#define WOODS_METATILE_TREE_BASE_R 0x1E5

// The top of a tree, drawn into the block ABOVE its canopy row - the crown
// poking up out of whatever is already there. Which variant depends on that
// block, so there is one pair for plain grass and one for tall grass.
//
// Measured across all nine General+Rustboro layouts: of the ~215 blocks that
// sit directly above a canopy without being part of a tree themselves, 201 are
// these four (0x1CE/0x1CF over plain grass, 0x1C6/0x1C7 over tall grass). The
// remaining strays belong to features we do not generate.
//
// Behaviour is preserved exactly - 0x1CE/0x1CF are MB_NORMAL like plain grass,
// 0x1C6/0x1C7 are MB_TALL_GRASS like tall grass - so this cannot change where
// encounters fire. Long grass has no vanilla variant and is left alone.
#define WOODS_METATILE_ABOVE_TREE_L      0x1CE
#define WOODS_METATILE_ABOVE_TREE_R      0x1CF
#define WOODS_METATILE_ABOVE_TREE_TALL_L 0x1C6
#define WOODS_METATILE_ABOVE_TREE_TALL_R 0x1C7

// New Mauville, under gTileset_General + gTileset_BikeShop. Mined from
// NewMauville_Inside_Layout with tools/rogue/derive_wall_table.py and read off
// the layout directly; validated with tools/rogue/theme_mock.py.
//
// Unlike the cave, this tileset draws one-block-thick walls natively - 0x227
// for a horizontal run and 0x290 for a vertical one are what vanilla uses when
// floor sits on both sides. So this theme needs no composed metatiles.
//
// The wall art is all drawn against black, so the mass interior has to be the
// void (0x208) rather than a filled block. 0x21F looks like a wall body but is
// a cap: stacked vertically it tiles into stripes.
#define NEWMAUVILLE_METATILE_FLOOR           0x210
#define NEWMAUVILLE_METATILE_VOID            0x208  // wall mass interior
#define NEWMAUVILLE_METATILE_WALL_BAND       0x227  // floor to north OR south
#define NEWMAUVILLE_METATILE_WALL_WEST       0x272  // floor to the west
#define NEWMAUVILLE_METATILE_WALL_EAST       0x270  // floor to the east
#define NEWMAUVILLE_METATILE_WALL_FACE_L     0x294  // floor south and west
#define NEWMAUVILLE_METATILE_WALL_FACE_R     0x293  // floor south and east
#define NEWMAUVILLE_METATILE_WALL_NORTH_L    0x296  // floor north and west
#define NEWMAUVILLE_METATILE_WALL_NORTH_R    0x295  // floor north and east
#define NEWMAUVILLE_METATILE_WALL_PILLAR     0x290  // one-wide vertical run
#define NEWMAUVILLE_METATILE_WALL_PILLAR_TOP 0x288
#define NEWMAUVILLE_METATILE_WALL_PILLAR_BOT 0x298

// Wall skirts: the wall's own edge art spilling into the adjacent floor tile,
// keyed to the specific wall metatile. From NewMauville_Inside, split by
// neighbour: floor under the band is 0x22F 34/37 (and 7/7 under the vent, a
// band variant), under face-left 0x27F 10/11; floor east of 0x270 is 0x27D
// 24/24, of 0x295 9/9, of 0x293 7/7.
#define NEWMAUVILLE_METATILE_SKIRT_BAND_S   0x22F
#define NEWMAUVILLE_METATILE_SKIRT_FACE_L_S 0x27F
#define NEWMAUVILLE_METATILE_SKIRT_E        0x27D
#define NEWMAUVILLE_METATILE_SKIRT_CORNER   0x275

// Wall decoration. All are drop-in replacements for the wall band, so they keep
// the collision they replace. Vanilla interleaves these along a wall run. The
// bookcase is a 2-wide unit; vanilla places its halves adjacent essentially
// always (6 pairs, 1 stray in NewMauville_Inside).
#define NEWMAUVILLE_METATILE_WALL_VENT        0x277
#define NEWMAUVILLE_METATILE_WALL_COUNTER     0x2B4
#define NEWMAUVILLE_METATILE_WALL_BOOKCASE_L  0x2A1
#define NEWMAUVILLE_METATILE_WALL_BOOKCASE_R  0x2A2

// The facility tileset has no stairs of its own, but 0x0AF lives in the primary
// and so is available under any pair. Grey steps read as a service stairwell
// here, where the same art would be a grey stripe in the woods.
#define NEWMAUVILLE_METATILE_STAIRS          0x0AF

// Fiery Path, under gTileset_General + gTileset_Lavaridge. Mined from
// FieryPath_Layout (corners cross-checked against JaggedPass and MtChimney).
// Same structure as the cave: pale bumpy wall tops with dark red edging doing
// the boundary work.
#define FIERYPATH_METATILE_FLOOR          0x308
#define FIERYPATH_METATILE_WALL_INTERIOR  0x271
#define FIERYPATH_METATILE_WALL_WEST      0x306  // floor to the west
#define FIERYPATH_METATILE_WALL_EAST      0x307  // floor to the east
#define FIERYPATH_METATILE_WALL_FACE_MID  0x274
#define FIERYPATH_METATILE_WALL_FACE_L    0x30E
#define FIERYPATH_METATILE_WALL_FACE_R    0x30F
#define FIERYPATH_METATILE_WALL_NORTH_MID 0x30C  // also the native 1-wide horizontal
#define FIERYPATH_METATILE_WALL_NORTH_L   0x30A
#define FIERYPATH_METATILE_WALL_NORTH_R   0x30B
#define FIERYPATH_METATILE_WALL_CORNER_SE 0x27B  // inner corner, floor SE
#define FIERYPATH_METATILE_WALL_CORNER_SW 0x27C
#define FIERYPATH_METATILE_WALL_CORNER_NW 0x27E

// The south skirt of the 0x30C ridge - the ridge's bottom edge spilling into
// the floor tile below it. Vanilla applies it 10/10 under 0x30C and never
// under a face, which is why pooling all wall types once made it look like
// one-in-five scatter.
#define FIERYPATH_METATILE_RIDGE_SKIRT_S  0x269

// Decoration: ember-glint floor variants and rocks embedded in the wall top.
#define FIERYPATH_METATILE_FLOOR_SPARKLE_A 0x310
#define FIERYPATH_METATILE_FLOOR_SPARKLE_B 0x311
#define FIERYPATH_METATILE_WALL_ROCKS_A    0x268
#define FIERYPATH_METATILE_WALL_ROCKS_B    0x26A
#define FIERYPATH_METATILE_WALL_BOULDER    0x30D

// Composed by tools/rogue/make_fiery_slivers.py, appended after the 441
// vanilla Lavaridge metatiles. Same seven cases as the cave, except the
// horizontal sliver is native (0x30C) so only six needed composing.
#define FIERYPATH_METATILE_SLIVER_VERT     0x3B9
#define FIERYPATH_METATILE_SLIVER_VERT_TOP 0x3BA
#define FIERYPATH_METATILE_SLIVER_VERT_BOT 0x3BB
#define FIERYPATH_METATILE_SLIVER_HORZ_L   0x3BC
#define FIERYPATH_METATILE_SLIVER_HORZ_R   0x3BD
#define FIERYPATH_METATILE_SLIVER_ISOLATED 0x3BE

// The dark cave-mouth interior from the primary tileset. In the woods this
// read as a wrong dark rectangle in grass; on red volcanic rock it reads as a
// hole descending, which is exactly what it is.
#define FIERYPATH_METATILE_STAIRS          0x0A7

// Mirage Tower, under gTileset_General + gTileset_MirageTower. Mined from
// MirageTower_1F/3F/4F, then found to need no mining at all: that tileset is a
// pure ART RESKIN of gTileset_Cave. 411 of its 414 metatiles are byte-identical
// and all 414 attributes are, so every wall id below is the cave's id doing the
// cave's job, drawn in sandstone instead of rock. Only gTileset_NavelRock comes
// anywhere close (32%), so this is not a trick that generalises.
//
// Because the definitions match, the cave's splice recipe produced slivers that
// are byte-identical too, and they land on the same appended ids - both
// tilesets have exactly 414 vanilla metatiles. That is a coincidence worth NOT
// relying on: these are spelled out separately so either tileset can change
// without silently corrupting the other theme.
//
// The one place this theme deliberately departs from vanilla Mirage Tower: that
// map walks on 0x211 and never uses 0x201 at all. We use the cave's assignment
// instead - 0x201 smooth floor, 0x211 hatched rock - because the two ids are
// shared with the cave and inverting them here would put the rock texture on
// the walkable surface, contradicting what the player learnt in Granite Cave.
#define MIRAGETOWER_METATILE_FLOOR          0x201  // smooth sand, MB_CAVE
#define MIRAGETOWER_METATILE_WALL_INTERIOR  0x211  // hatched sandstone

#define MIRAGETOWER_METATILE_WALL_WEST      0x210  // floor to the west
#define MIRAGETOWER_METATILE_WALL_EAST      0x212  // floor to the east
#define MIRAGETOWER_METATILE_WALL_FACE_L    0x218
#define MIRAGETOWER_METATILE_WALL_FACE_MID  0x219  // the brick face, floor south
#define MIRAGETOWER_METATILE_WALL_FACE_R    0x21A
#define MIRAGETOWER_METATILE_WALL_NORTH_L   0x220
#define MIRAGETOWER_METATILE_WALL_NORTH_MID 0x209  // floor north
#define MIRAGETOWER_METATILE_WALL_NORTH_R   0x222
#define MIRAGETOWER_METATILE_WALL_CORNER_NW 0x21B
#define MIRAGETOWER_METATILE_WALL_CORNER_NE 0x21C
#define MIRAGETOWER_METATILE_WALL_CORNER_S  0x223

// Composed by tools/rogue/make_mirage_slivers.py, appended after the 414
// vanilla Mirage Tower metatiles. Same seven cases as the cave, and nothing
// native to splice against - the tower's own walls are never one block thick.
#define MIRAGETOWER_METATILE_SLIVER_VERT     0x39E
#define MIRAGETOWER_METATILE_SLIVER_HORZ     0x39F
#define MIRAGETOWER_METATILE_SLIVER_VERT_TOP 0x3A0
#define MIRAGETOWER_METATILE_SLIVER_VERT_BOT 0x3A1
#define MIRAGETOWER_METATILE_SLIVER_HORZ_L   0x3A2
#define MIRAGETOWER_METATILE_SLIVER_HORZ_R   0x3A3
#define MIRAGETOWER_METATILE_SLIVER_ISOLATED 0x3A4

// The sand drift, the tower's signature floor feature and the one thing that
// stops this theme reading as a Granite Cave recolour. Spelled out separately
// from DUNGEON_METATILE_SAND_* even though the ids match, for the same reason
// the slivers are: the equality is a property of the reskin, not a guarantee.
#define MIRAGETOWER_METATILE_DRIFT_NW      0x298
#define MIRAGETOWER_METATILE_DRIFT_N       0x299
#define MIRAGETOWER_METATILE_DRIFT_NE      0x29A
#define MIRAGETOWER_METATILE_DRIFT_W       0x2A0
#define MIRAGETOWER_METATILE_DRIFT_MID     0x2A1
#define MIRAGETOWER_METATILE_DRIFT_E       0x2A2
#define MIRAGETOWER_METATILE_DRIFT_SW      0x2A8
#define MIRAGETOWER_METATILE_DRIFT_S       0x2A9
#define MIRAGETOWER_METATILE_DRIFT_SE      0x2AA
#define MIRAGETOWER_METATILE_DRIFT_NW_WALL 0x29B
#define MIRAGETOWER_METATILE_DRIFT_N_WALL  0x29C
#define MIRAGETOWER_METATILE_DRIFT_NE_WALL 0x29D

// Rocks embedded in the sand mass. Drop-in swaps for the wall interior, so they
// keep the collision they replace.
#define MIRAGETOWER_METATILE_ROCKS_A        0x202  // a pair of boulders
#define MIRAGETOWER_METATILE_ROCKS_B        0x203  // a cluster of four
#define MIRAGETOWER_METATILE_BOULDER        0x229  // one large stone

// A native MB_LADDER, unlike the cave's 0x214 - the tower has its own wooden
// ladder art and it reads correctly against sand.
#define MIRAGETOWER_METATILE_STAIRS         0x217

// Composed by tools/rogue/make_woods_stairs.py and appended to the Rustboro
// secondary tileset: earth steps in a grass-cornered opening.
//
// This used to be 0x0A7, the dark interior of the vanilla cave mouth, which
// reads as a dark rectangle dropped into grass because the sandy surround that
// frames it never gets painted. Note also that the cave stairs id means
// something else entirely under Rustboro - 0x214 there is a grey stripe - so a
// theme can never inherit another theme's metatile ids.
#define WOODS_METATILE_STAIRS 0x35E

// Jungle, under gTileset_General + gTileset_Fortree - Route 119, Route 120 and
// Fortree City. Mined from the two routes.
//
// The canopy is why this theme is not a second woods. Rustboro draws discrete
// 2x3 trees on a 2x2 grid, with gaps between them; Fortree draws a CONTINUOUS
// leaf mass that tiles 1x1 in both directions. So the jungle runs on
// DUNGEON_GEN_CAVE, which is also the path the cosmetic passes live on - the
// woods generator returns before any of them.
//
// It needs no composed metatiles and almost no table, because the mass has
// exactly ONE edge case: the row where floor sits to the south. Every other
// neighbour combination is just more canopy. 0x0C6/0x0C7 and 0x016/0x017 are
// interchangeable variants scattered for texture, not a 2-wide pair - checked
// against x-parity across both routes and Fortree City and they come out 50/50.
#define JUNGLE_METATILE_GRASS            0x001  // plain, no encounters
#define JUNGLE_METATILE_CANOPY           0x0C6  // dense leaf mass, tiles 1x1
#define JUNGLE_METATILE_CANOPY_ALT       0x0C7  // interchangeable variant
#define JUNGLE_METATILE_CANOPY_BASE      0x017  // where floor sits to the south
#define JUNGLE_METATILE_CANOPY_BASE_ALT  0x016

// Long grass is the jungle's, not the woods'. 0x208 is the only
// MB_LONG_GRASS_SOUTH_EDGE metatile in the game and it lives here; vanilla puts
// long grass directly above it in 272 of 272 placements.
#define JUNGLE_METATILE_LONG_GRASS       0x015  // MB_LONG_GRASS
#define JUNGLE_METATILE_LONG_GRASS_S     0x208  // the fringe row, no encounters

// Puddles, a 3x3 region autotile in the PRIMARY tileset at base 0x0C8 with a
// stride of 8. Every piece is MB_PUDDLE, which is TILE_FLAG_UNUSED - walkable,
// NOT surfable and carrying no encounters - so they are safe decoration. The
// pond and ocean water in this tileset are surfable and must never be painted.
#define JUNGLE_METATILE_PUDDLE_NW        0x0C8
#define JUNGLE_METATILE_PUDDLE_N         0x0C9
#define JUNGLE_METATILE_PUDDLE_NE        0x0CA
#define JUNGLE_METATILE_PUDDLE_W         0x0D0
#define JUNGLE_METATILE_PUDDLE_MID       0x0D1
#define JUNGLE_METATILE_PUDDLE_E         0x0D2
#define JUNGLE_METATILE_PUDDLE_SW        0x0D8
#define JUNGLE_METATILE_PUDDLE_S         0x0D9
#define JUNGLE_METATILE_PUDDLE_SE        0x0DA

// Fortree's wooden ladder. The stairs hook matches on metatile id rather than
// behaviour, so this only has to read as a way down - and a rope ladder through
// the canopy is exactly right for the treehouse town's route.
#define JUNGLE_METATILE_STAIRS           0x245

// Woods uses the same two elevations as caves.
#define DUNGEON_ELEVATION_FLOOR 3
#define DUNGEON_ELEVATION_WALL  0

// A dungeon theme owns everything that varies between dungeons: which layout
// supplies the tilesets, which generator shapes the floor, and which metatiles
// it paints with. Adding a theme should mean adding a table entry rather than
// editing the generator.
//
// The themed layout is a tileset donor only - no map points at it. gMapHeader
// is a RAM copy and CopyMapTilesetsToVram reads gMapHeader.mapLayout, so
// pointing that at another layout before the map view initialises swaps the
// tilesets. Only mapLayout is patched, never mapLayoutId, so every dispatch
// that keys on the id keeps working.
enum DungeonGenerator
{
    DUNGEON_GEN_CAVE,   // 1x1 carve, then a nine-case wall autotile
    DUNGEON_GEN_WOODS,  // 2x2 stamps on a half-resolution grid
};

// Slots in a theme wall table, in the order the autotile rule tests them.
enum DungeonWallSlot
{
    WALL_INTERIOR_LEFT, WALL_INTERIOR_MID, WALL_INTERIOR_RIGHT,
    WALL_FACE_LEFT,     WALL_FACE_MID,     WALL_FACE_RIGHT,
    WALL_NORTH_LEFT,    WALL_NORTH_MID,    WALL_NORTH_RIGHT,
    WALL_CORNER_NW,     WALL_CORNER_NE,    WALL_CORNER_SOUTH,
    WALL_SLIVER_VERT,   WALL_SLIVER_HORZ,
    WALL_SLIVER_VERT_TOP, WALL_SLIVER_VERT_BOT,
    WALL_SLIVER_HORZ_L,   WALL_SLIVER_HORZ_R, WALL_SLIVER_ISOLATED,
    WALL_SLOT_COUNT,
};

// A 2x2 stamp, plus the ground-contact row used where a mass ends and open
// ground begins. Vanilla never leaves a trunk row exposed, so the bottom pair
// is swapped for the base pair whenever the cell below is open. These live in
// the table rather than in the generator because a metatile id means something
// else under every other tileset pair.
enum DungeonStampCorner
{
    STAMP_TL, STAMP_TR, STAMP_BL, STAMP_BR,
    STAMP_BASE_L, STAMP_BASE_R,
    STAMP_COUNT,
};

// A soft region lying ON the floor - Mirage Tower's sand drifts, and the same
// sand in the cave, where vanilla uses it for Shoal Cave's beach and the Desert
// Underpass. Unlike RogueDecor, which swaps one block for another, this is a
// REGION autotile: a cell's art depends on which sides the region continues
// into, so the blob gets rounded edges instead of a hard rectangle.
//
// The fourth row is the top edge where a WALL sits above rather than plain
// floor. Vanilla bakes the wall's own base into those pieces, so they are a
// skirt and a region edge at once - 0x29B-0x29D against 0x298-0x29A.
//
// Every piece keeps the floor's own collision and elevation, and in both themes
// every piece carries MB_CAVE like the floor does, so neither movement nor
// encounters change. This pass can no more affect reachability than ApplyDecor.
enum DungeonPatchSlot
{
    PATCH_NW,      PATCH_N,      PATCH_NE,
    PATCH_W,       PATCH_MID,    PATCH_E,
    PATCH_SW,      PATCH_S,      PATCH_SE,
    PATCH_NW_WALL, PATCH_N_WALL, PATCH_NE_WALL,
    PATCH_SLOT_COUNT,
};

// Blobs are stamped as ellipses from a seed-derived centre and radius. A dozen
// is plenty to break up a 48x48 floor and keeps the pass cheap.
#define DUNGEON_MAX_PATCH_BLOBS 12

// One region laid over the floor. A theme can have several: the jungle draws
// long grass and then punches puddles through it, so the later layer wins where
// they overlap. Each layer autotiles against its OWN geometry, so a layer
// covered by a later one still has correct edges underneath.
struct RoguePatchLayer
{
    u16 tile[PATCH_SLOT_COUNT];
    u8 blobs;    // ellipses stamped per floor, capped at DUNGEON_MAX_PATCH_BLOBS
    u8 radius;   // nominal; each blob varies a little either way
};

// One cosmetic wall swap: wherever `base` was painted, `variant` may replace it.
// Keyed on the painted metatile rather than on a wall slot, so a variant that
// suits several slots needs only one entry, and a theme whose slots share a
// metatile cannot decorate one of them by accident.
//
// A nonzero variantEast makes the entry a 2-wide unit (a bookcase, a console):
// it lands only where the block east is also `base`, and writes both halves.
// Split art placed as a single would read as cut off at the frame edge.
struct RogueDecor
{
    u16 base;
    u16 variant;
    u16 variantEast;
};

// If the block north of a floor tile is `wall`, that floor becomes `south`;
// if the block west of it is `wall`, it becomes `east`. Zero means this wall
// has no skirt on that side.
struct RogueSkirt
{
    u16 wall;
    u16 south;
    u16 east;
};

struct RogueDungeonTheme
{
    u16 layoutId;
    u8 generator;
    u8 elevationFloor;
    u8 elevationWall;

    // Shape of the carve, so a theme can be more open than the cave without a
    // second generator. Zero means the cave's own defaults.
    //
    // Room SIZE alone cannot buy much openness: bigger rooms simply stop
    // fitting, so coverage plateaus near 35% while the room count collapses
    // from 8 to 3 and the variance gets ugly. Room COUNT and corridor WIDTH are
    // the levers that work - 12 rooms of 7-13 with 3-wide corridors measures
    // 42.7% average against the cave's 24.0%, and still averages 7.8 rooms.
    u8 roomCount;       // cap, up to DUNGEON_MAX_ROOMS
    u8 roomMin, roomMax;
    u8 corridorWidth;   // odd, centred on the path; 0 or 1 is the cave's

    u16 floor;
    u16 tallGrass;   // 0 if the theme has none, in which case encounters fire
    u16 longGrass;   // anywhere rather than only in grass
    u16 longGrassBaseL;  // where long grass meets open ground below it, or the
    u16 longGrassBaseR;  // blades are cut off flat

    // DUNGEON_GEN_WOODS: the block directly above a tree canopy, where vanilla
    // draws the tree's crown poking up. One pair for plain floor and one for
    // tall grass, because the crown is composited over what is already there.
    // Zero leaves the block alone, which is what long grass gets.
    u16 aboveTreeFloorL, aboveTreeFloorR;
    u16 aboveTreeGrassL, aboveTreeGrassR;

    u16 stairsDown;
    u16 stairsUp;

    u16 wall[WALL_SLOT_COUNT];  // DUNGEON_GEN_CAVE only
    u16 stamp[STAMP_COUNT];     // DUNGEON_GEN_WOODS only

    // Wall skirts: the wall's own bottom or side edge, spilling into the floor
    // tile next to it. Keyed to the SPECIFIC wall metatile, and deterministic -
    // vanilla applies these at effectively 100% per wall type, and pooling
    // across wall types is what once made them look probabilistic here.
    const struct RogueSkirt *skirts;
    u8 skirtCount;
    u16 shadowCorner;  // floor with skirted wall both north and west, or only
                       // diagonally above-left

    // Soft regions laid over the floor and autotiled at their edges, painted in
    // order so a later layer wins. Runs before the skirts, so a wall's own edge
    // art still beats all of them where a theme has both.
    const struct RoguePatchLayer *patches;
    u8 patchCount;

    // Cosmetic swaps applied to already-painted wall blocks. Collision is not
    // touched, so decoration can never affect connectivity.
    const struct RogueDecor *decor;
    u8 decorCount;
    u8 decorRarity;    // 1 in N eligible blocks; 0 disables

    const u16 *species;
    u8 speciesCount;
};

// Half-resolution grid for DUNGEON_GEN_WOODS, so a cell is one 2x2 stamp.
#define DUNGEON_CELLS_W (DUNGEON_WIDTH / 2)
#define DUNGEON_CELLS_H (DUNGEON_HEIGHT / 2)

// Each floor has exactly one exit, placed at a seed-derived position. Because
// generation is deterministic the stairs position needs no save data at all -
// it is recomputed whenever the floor is.
//
// 0x214 is MB_LADDER, the dark opening in the cave floor. 0x23E is a wooden
// ladder. Both simply advance a floor; the direction is cosmetic variety.
//
// The hook matches on metatile id rather than behaviour, so it cannot be
// confused by an unrelated tile that happens to share a behaviour.
#define DUNGEON_METATILE_STAIRS_DOWN 0x214
#define DUNGEON_METATILE_STAIRS_UP   0x23E

// The floor is regenerated from its seed rather than stored, so a whole floor
// costs 2 bytes of save data. Var aliases live in the constants header because
// the map scripts need them too.
#include "constants/rogue_dungeon.h"

#define DUNGEON_WIDTH  48
#define DUNGEON_HEIGHT 48

// Boss floors are a single centred arena rather than rooms and corridors.
#define DUNGEON_ARENA_WIDTH  15
#define DUNGEON_ARENA_HEIGHT 13

// The array bound. Themes ask for fewer through roomCount; only the jungle
// wants more than the cave's eight, and the extra four rooms cost 16 bytes.
#define DUNGEON_MAX_ROOMS 12
#define DUNGEON_ROOMS_DEFAULT 8
#define DUNGEON_ROOM_MIN   5
#define DUNGEON_ROOM_MAX  10

// A run mirrors the stock game, but its dungeons are not all the same length,
// so the floor-to-dungeon mapping is a table walk rather than a division:
//
//   dungeons 0-7   ten floors, mini boss at the 5th, gym leader at the 10th
//                  -> floors 1-80
//   dungeons 8-12  five floors, an Elite Four member at the 5th, NO mini boss
//                  -> floors 81-105
//   dungeon  13    ten floors, the rival at the 5th, Steven at the 10th
//                  -> floors 106-115, and the run ends there
//
// The Elite Four sit five floors apart because a gauntlet is what they are.
// Giving each of them a mini boss as well would pad the endgame out with
// grunts, and there is nobody left at that level to draw one from anyway.
#define DUNGEON_LONG_FLOORS    10
#define DUNGEON_SHORT_FLOORS    5
#define DUNGEON_MINIBOSS_FLOOR  4  // 0-based within the dungeon, so the 5th

// A dungeon's boss always stands on its last floor, whatever its length, so
// there is no DUNGEON_BOSS_FLOOR any more - ask DungeonLengthOf.
#define DUNGEON_GYM_DUNGEONS 8
#define DUNGEON_E4_DUNGEONS  5
#define DUNGEON_COUNT       (DUNGEON_GYM_DUNGEONS + DUNGEON_E4_DUNGEONS + 1)

#define DUNGEON_GYM_FLOORS   (DUNGEON_GYM_DUNGEONS * DUNGEON_LONG_FLOORS)
#define DUNGEON_E4_FLOORS    (DUNGEON_E4_DUNGEONS * DUNGEON_SHORT_FLOORS)
#define DUNGEON_E4_END_FLOOR (DUNGEON_GYM_FLOORS + DUNGEON_E4_FLOORS)
#define DUNGEON_TOTAL_FLOORS (DUNGEON_E4_END_FLOOR + DUNGEON_LONG_FLOORS)

// Levels are fitted to the stock bosses, and the stock bosses climb at three
// different rates, so the curve does too.
//
// Eight gyms span levels 13 to 43 over eighty floors. The Elite Four span 47 to
// 56 over twenty-five, so that stretch has to climb about twice as fast per
// floor as it used to, now that it is half as long.
//
// Then Steven. TRAINER_STEVEN is Emerald's post-game superboss at levels 75-78,
// not a champion - Emerald's champion is Wallace at 55-58, and Steven's same six
// Pokemon are levels 55-58 in Ruby and Sapphire, where he holds the title.
// Reaching him from Wallace in ten floors therefore cannot be gentle: the final
// dungeon climbs three times as fast as the gym stretch and still leaves him
// standing five levels above it, the widest boss-over-curve gap in the run.
// FINAL_NUM is the knob if that proves too much in play.
#define DUNGEON_ENCOUNTER_BASE_LEVEL   5
#define DUNGEON_ENCOUNTER_LEVEL_NUM   51  // levels gained per 100 floors
#define DUNGEON_ENCOUNTER_E4_NUM      48  // per hundred, through the Elite Four
#define DUNGEON_ENCOUNTER_FINAL_NUM  160  // per hundred, the climb to Steven
#define DUNGEON_ENCOUNTER_LEVEL_DEN  100
#define DUNGEON_ENCOUNTER_LEVEL_SPREAD 2

// The species pool is a window that SLIDES with depth rather than a prefix that
// only grows. A prefix keeps the weakest species in play forever, which is why
// Zubat was everywhere; with a window they retire as stronger ones unlock.
#define DUNGEON_ENCOUNTER_STARTING_TIER 6
#define DUNGEON_ENCOUNTER_TIER_FLOORS   3
#define DUNGEON_ENCOUNTER_WINDOW        8

void GenerateRogueDungeonFloor(u16 *backupMapData, bool8 setPlayerPosition);
bool8 RogueDungeon_TryStartStairsScript(struct MapPosition *position);
const struct WildPokemonInfo *RogueDungeon_GetWildMonInfo(enum WildPokemonArea area);
void RogueDungeon_ApplyNewGameUnlocks(void);
void RogueDungeon_GetFloorName(u8 *dest);
void RogueDungeon_PrepareNewFloor(void);
void RogueDungeon_LoadObjectEventTemplates(void);
void RogueDungeon_SetUpTrainerBattle(void);
void RogueDungeon_OnBossDefeated(void);
void RogueDungeon_GiveChosenStarter(void);
bool8 RogueDungeon_TryHandleWhiteOut(void);
void RogueDungeon_GiveBossAce(void);

// specialvar targets. The script command stores the function's RETURN VALUE -
// it does not read gSpecialVar_Result - and data/specials.inc is assembly, so
// nothing checks the prototype. A void special here silently hands the script
// whatever is left in r0, which is the return address. Keep these returning u16.
u16 RogueDungeon_IsDungeonEndFloor(void);
u16 RogueDungeon_IsRunCompleteFloor(void);
u16 RogueDungeon_PrepareBossAceOffer(void);
u16 RogueDungeon_GiveBossTM(void);
bool8 RogueDungeon_IsGeneratedTrainer(void);
bool8 RogueDungeon_HasTrainerBeenBeaten(u8 objectEventId);
bool8 RogueDungeon_IsBossFloor(u16 floor);

void RogueDungeon_ResetRun(void);

#endif // GUARD_ROGUE_DUNGEON_H
