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

// A flowering shrub standing in open grass - the woods' only decoration, and
// the ONLY thing this tileset pair offers for the job. Censused across all
// eight vanilla General+Rustboro layouts: 53 placements, 9 of them with grass
// on three or more sides, and COLLISION 0 IN EVERY ONE. Passable is what makes
// it usable, because ApplyDecor copies the base block's collision and would
// otherwise leave a shrub you walk straight through - or, worse, a solid one
// where the generator promised open floor.
//
// It is one entry rather than three because two obvious-looking candidates did
// not survive the census, and both failures are the familiar one:
//
//   0x002 reads as a paler grass variant on a contact sheet and is nothing of
//   the kind - it carries a dark brown band across its top row, so it is a
//   transition INTO something, and scattering it through a field would draw
//   26 stray edges of a feature that is not there.
//
//   0x00E is passable in all 88 of its placements, but only 2 of those stand
//   in open grass; the rest sit at the foot of the canopy hedge 0x0C6/0x0C7,
//   which is what it is FOR. 0x00F is byte-identical to it, so it is not a
//   second variant either.
//
// Same lesson as the long grass above, in its third costume: what a metatile
// looks like alone is not what it is. Ask what vanilla puts next to it.
#define WOODS_METATILE_FLOWER_BUSH 0x004

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
// It USED to run on gTileset_General + gTileset_Fortree, and that is exactly
// why it did not read as a jungle: almost every id it named was PRIMARY - plain
// route grass for the floor, vanilla canopy for the walls, vanilla puddles -
// so it was built out of the same shared art every other theme draws from.
// Only two ids in the whole theme were Fortree's.
//
// Worse, its entire wall table was TWO metatiles: the canopy, and the canopy
// with a base row where floor sat to the south. Every other neighbour case was
// just more canopy, so a wall mass had no silhouette - no corners, no faces, no
// slivers. That is cheap and it looks it.
//
// Now gTileset_General + gTileset_RogueHowlingJungle, imported from the Howling
// Jungle sheet, which supplies all twenty PaintWalls slots from its own legend
// and a brown dirt floor with sprouts pushing through it. The floor is the
// single biggest change: the mint-green route grass is what read as "outdoors,
// generic" more than anything else did.
// MB_NORMAL, copied from the plain route grass this replaces - NO encounters.
// That is the theme's design and it predates the art: wild battles come from
// the long grass layer, the way they do in the woods, and the ground is safe to
// cross. The importer defaults a ground block to the cave's MB_CAVE, which
// would have turned encounters on across the entire floor as a side effect of
// changing what the floor looks like.
#define JUNGLE_METATILE_FLOOR            0x238  // dirt, MB_NORMAL
#define JUNGLE_METATILE_WALL_INTERIOR_L  0x203
#define JUNGLE_METATILE_WALL_INTERIOR_M  0x204
#define JUNGLE_METATILE_WALL_INTERIOR_R  0x205
#define JUNGLE_METATILE_WALL_FACE_L      0x206
#define JUNGLE_METATILE_WALL_FACE_M      0x207
#define JUNGLE_METATILE_WALL_FACE_R      0x208
#define JUNGLE_METATILE_WALL_NORTH_L     0x200
#define JUNGLE_METATILE_WALL_NORTH_M     0x201
#define JUNGLE_METATILE_WALL_NORTH_R     0x202
#define JUNGLE_METATILE_WALL_CORNER_SE   0x21D
#define JUNGLE_METATILE_WALL_CORNER_SW   0x21E
#define JUNGLE_METATILE_WALL_CORNER_NW   0x220
#define JUNGLE_METATILE_WALL_CORNER_NE   0x21F
#define JUNGLE_METATILE_SLIVER_VERT      0x20C
#define JUNGLE_METATILE_SLIVER_HORZ      0x20A
#define JUNGLE_METATILE_SLIVER_VERT_TOP  0x210
#define JUNGLE_METATILE_SLIVER_VERT_BOT  0x214
#define JUNGLE_METATILE_SLIVER_HORZ_L    0x211
#define JUNGLE_METATILE_SLIVER_HORZ_R    0x213
#define JUNGLE_METATILE_SLIVER_ISOLATED  0x20D

// Wall Alt 2, five variants of specific wall cases, paired to the case each one
// varies by its position in the sheet's legend. FREE: the walls need 14 colours
// and the walls plus these need 14. Wall Alt 1 is the flowered set and would
// have pushed the slot to 17, over the 4bpp limit, so it is not imported.
#define JUNGLE_METATILE_WALL_ALT_NORTH_M 0x22F  // varies 0x201
#define JUNGLE_METATILE_WALL_ALT_INT_L   0x230  // varies 0x203
#define JUNGLE_METATILE_WALL_ALT_INT_M   0x231  // varies 0x204
#define JUNGLE_METATILE_WALL_ALT_INT_R   0x232  // varies 0x205
#define JUNGLE_METATILE_WALL_ALT_FACE_M  0x233  // varies 0x207

// Ground Alt 1 and 2, one cell each, scattered over the dirt. Ground plus both
// of these is EXACTLY 15 colours - the 4bpp limit with no headroom left.
#define JUNGLE_METATILE_FLOOR_ALT_1      0x263
#define JUNGLE_METATILE_FLOOR_ALT_2      0x264

// The long grass is the theme's OWN now, on palette slot 9, and it cost no
// pixel art at all - it is vanilla's pixels pointed at different colours.
//
// This is the section 5 palette-only trick applied to a graft. The vanilla
// blades never use a ground palette index and the ground band never uses a
// blade one - dumped and checked, blades are indices 1-4 and ground is 13-15 -
// so the two can be recoloured independently. The blades take their ramp from
// this tileset's WALL palette, which makes the grass the same greens as the
// foliage it grows under; the ground takes its ramp from the DIRT palette.
//
// That is what buys the dirt fringe. 0x208 was the only MB_LONG_GRASS_SOUTH_
// EDGE metatile in the game, it lived in gTileset_Fortree, and its lower band
// is drawn as route grass - useless to a theme standing on dirt even if it
// could still reach it. Recoloured, the same pixels end the grass on soil.
//
// Both are grafted by import_tile_sheet.py, so they live in this tileset and
// its palettes are its own. gTileset_General's 0x015 is no longer used here,
// which is the point: that one cannot be recoloured, because its palettes are
// shared with every theme in the game.
#define JUNGLE_METATILE_LONG_GRASS       0x294  // MB_LONG_GRASS
#define JUNGLE_METATILE_LONG_GRASS_S     0x295  // the fringe, no encounters

// The sheet's water, laid as a patch region. MB_PUDDLE, and that behaviour is
// chosen rather than inherited: it is in MetatileBehavior_IsReflective, so the
// player REFLECTS in it exactly the way vanilla ponds do, and its tile flags
// are TILE_FLAG_UNUSED alone - walkable, splashy, NOT surfable, no encounters.
//
// MB_POND_WATER would reflect too and is the obvious pick, and it would have
// been a bug: it is SURFABLE|HAS_ENCOUNTERS, so it would have made this theme
// feed the WATER encounter branch while its long grass feeds the land one, and
// only one table is swapped in per floor. check_encounter_flags.py would have
// caught it, which is the point of it existing.
//
// Static. The sheet animates its water by cycling the palette, not the tiles,
// and its Sparkle overlay is a second rate on top of that; neither is imported.
#define JUNGLE_METATILE_WATER_NW         0x265
#define JUNGLE_METATILE_WATER_N          0x266
#define JUNGLE_METATILE_WATER_NE         0x267
#define JUNGLE_METATILE_WATER_W          0x268
#define JUNGLE_METATILE_WATER_MID        0x269
#define JUNGLE_METATILE_WATER_E          0x26A
#define JUNGLE_METATILE_WATER_SW         0x26B
#define JUNGLE_METATILE_WATER_S          0x26C
#define JUNGLE_METATILE_WATER_SE         0x26D

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

// A hollow dug under the roots, drawn by import_tile_sheet.py. WAS Fortree's
// wooden ladder at 0x245, then gTileset_General's grey warp 0x0A7 once the
// secondary changed under it - vanilla rock in a jungle, and one of the three
// borrowed descents that are now gone.
//
// Drawn in palette 9, the LONG GRASS palette, which is neither the dirt it sits
// in nor the foliage above it: dirt has no dark at all and foliage's only dark
// is a saturated green that reads as paint. Slot 9 still carries vanilla's
// #413931, the very colour the woods stairs use for their void. See §5.
#define JUNGLE_METATILE_STAIRS           0x296

// gTileset_Mossdeep, the sea routes out of Lilycove that lead to Tate and
// Liza's city. The first theme the player crosses SURFING rather than walking.
//
// That is not a special case in the generator, it is a property of the floor
// metatile: MB_OCEAN_WATER carries TILE_FLAG_SURFABLE, and
// GetAdjustedInitialTransitionFlags checks the behaviour under the player on
// every warp arrival and answers a surfable one with a surf blob. No HM, no
// party requirement, no badge - which is the only reason a water dungeon is not
// a softlock waiting for a player with nothing that can Surf.
//
// So nothing walkable may ever be painted here. Step onto land and the player
// dismounts, and getting back on water WOULD need the HM.
#define OCEAN_METATILE_WATER             0x170  // MB_OCEAN_WATER, encounters

// The way down is a whirlpool, drawn by make_ocean_tiles.py. It was the sea
// routes' deep-water dive spot (0x14E), which is a flat darker square - fine as
// scenery, poor as the one thing on the floor the player is hunting for.
//
// It keeps that dive spot's attribute, so it is still MB_DEEP_WATER: surfable,
// and stepping on it does not dismount the player. It spins, on the secondary
// tileset animation slot Mossdeep was not using - see TilesetAnim_Mossdeep.
#define OCEAN_METATILE_WHIRLPOOL         0x3D1  // MB_DEEP_WATER, 4 frames

// The rock mass, a 3x3 nine slice read off the sea routes as a grid. A
// neighbour-mask census cannot recover it - pooled with island shores and the
// map-edge barrier nothing beats 38% - but each piece alone is decisive.
#define OCEAN_METATILE_ROCK_NW           0x338
#define OCEAN_METATILE_ROCK_N            0x339
#define OCEAN_METATILE_ROCK_NE           0x33A
#define OCEAN_METATILE_ROCK_W            0x340
#define OCEAN_METATILE_ROCK_MID          0x341
#define OCEAN_METATILE_ROCK_E            0x342
#define OCEAN_METATILE_ROCK_SW           0x348
#define OCEAN_METATILE_ROCK_S            0x349
#define OCEAN_METATILE_ROCK_SE           0x34A

// Ours, appended after Mossdeep's 454 vanilla metatiles by make_ocean_tiles.py.
// Vanilla's smallest sea rock is 2x2, and a carved floor makes one-block walls
// constantly. All nine pieces above share one bottom layer with every edge as a
// top-layer overlay, so each of these is four quadrant copies rather than art.
#define OCEAN_METATILE_SLIVER_VERT       0x3C6
#define OCEAN_METATILE_SLIVER_HORZ       0x3C7
#define OCEAN_METATILE_SLIVER_VERT_TOP   0x3C8
#define OCEAN_METATILE_SLIVER_VERT_BOT   0x3C9
#define OCEAN_METATILE_SLIVER_HORZ_L     0x3CA
#define OCEAN_METATILE_SLIVER_HORZ_R     0x3CB
#define OCEAN_METATILE_SLIVER_ISOLATED   0x3CC

// The four room corners. The nine slice above is convex throughout, so every
// WALL_CORNER_OPEN_* slot was standing on the rock interior and the wall's dark
// edge stopped dead at each corner of a room.
//
// These are vanilla's, not ours: gTileset_General's cliff set already draws all
// four inside corners, and the corner-case scan over the Mossdeep layouts names
// one of them for each diagonal at 27-43%. Only the palette changes, to
// Mossdeep's rock palette - the same recolouring vanilla applies to the nine
// slice. Same answer Granite Cave gets from 0x21B/0x21C/0x223, found the same
// way: read the case out of a real layout rather than reasoning about it.
//
// An inside corner carries the dark wall edge from one neighbour round to the
// other in a continuous L, and it is SOLID - no cardinal neighbour of this block
// is floor, so none of it may show any. Two earlier attempts failed here: art
// borrowed from a rounded water pocket, which put a blue bite in it, and the
// same over an opaque underlay, which had no dark edge and read as plain rock.
#define OCEAN_METATILE_CORNER_OPEN_SE    0x3CD
#define OCEAN_METATILE_CORNER_OPEN_SW    0x3CE
#define OCEAN_METATILE_CORNER_OPEN_NW    0x3CF
#define OCEAN_METATILE_CORNER_OPEN_NE    0x3D0

// gTileset_Underwater, the seafloor below the sea routes. The second theme the
// player crosses without walking, and the first crossed DIVING.
//
// Unlike surfing, that is not a property of any metatile - it is the MAP.
// MAP_TYPE_UNDERWATER makes GetAdjustedInitialTransitionFlags answer with
// PLAYER_AVATAR_FLAG_UNDERWATER on arrival, ahead of any behaviour check, with
// no Dive HM and no party requirement. GetCurrentMapType reads the ROM header
// by warp group and id, so the RAM patch that swaps tilesets cannot fake it and
// this theme gets its own map. See theme->mapId.
//
// Everything below is vanilla's, at better confidence than any other theme:
// derive_wall_table.py on LAYOUT_UNDERWATER_ROUTE126 gives every cardinal case
// at 68-93%, and a corner-case scan over all twelve Underwater layouts gives
// all four inside corners at 69-94% with 155-188 examples each. Nothing here is
// composed and nothing is drawn.
//
// Note the wall mass is LIGHTER than the floor, which is inverted from every
// other theme - it is the open water above the seafloor, not rock.
#define UNDERWATER_METATILE_FLOOR          0x216  // MB_NORMAL, no encounters
#define UNDERWATER_METATILE_SEAWEED        0x281  // MB_SEAWEED_NO_SURFACING

// The same whirlpool the ocean descends through, appended here by
// make_underwater_tiles.py. Metatile ids above 0x200 belong to whichever
// secondary is loaded, so the ocean's 0x3D1 means nothing under this tileset
// and the metatile has to exist twice - shape shared from whirlpool_art.py,
// colours not, because a vortex reads as water only when it is made of the
// water around it. It keeps 0x2A9's attribute, so it is still MB_NO_SURFACING.
#define UNDERWATER_METATILE_STAIRS         0x2EC  // MB_NO_SURFACING, 4 frames

#define UNDERWATER_METATILE_WALL_NW        0x20A
#define UNDERWATER_METATILE_WALL_N         0x20B
#define UNDERWATER_METATILE_WALL_NE        0x20C
#define UNDERWATER_METATILE_WALL_W         0x212
#define UNDERWATER_METATILE_WALL_MID       0x213
#define UNDERWATER_METATILE_WALL_E         0x214
#define UNDERWATER_METATILE_WALL_SW        0x21A
#define UNDERWATER_METATILE_WALL_S         0x21B
#define UNDERWATER_METATILE_WALL_SE        0x21C

#define UNDERWATER_METATILE_CORNER_OPEN_SE 0x206
#define UNDERWATER_METATILE_CORNER_OPEN_SW 0x207
#define UNDERWATER_METATILE_CORNER_OPEN_NW 0x222
#define UNDERWATER_METATILE_CORNER_OPEN_NE 0x223


// Victory Road, the Elite Four's dungeons. ONE constant set for all four
// variants, which is the one place this project may share metatile ids across
// themes.
//
// Everywhere else that would be a bug - 0x214 is a ladder under gTileset_Cave
// and a grey stripe under gTileset_Rustboro - and Mirage Tower spells its own
// out precisely because its equality with the cave is a coincidence of two
// reskins happening to have 414 metatiles each. Here it is not a coincidence:
// gTileset_RogueVictoryRoad{Sidney,Phoebe,Glacia,Drake} literally share
// gMetatiles_Cave, so the ids are the same file. Four copies of this block
// could drift apart; the shared thing they describe could not.
//
// Vanilla Emerald's Victory Road runs on gTileset_General + gTileset_Cave, the
// identical pair to Granite Cave, so the wall vocabulary transfers verbatim -
// measured over all three vanilla Victory Road layouts, which use 0x201, 0x211,
// 0x209, 0x219, 0x210, 0x212, 0x218, 0x21A, 0x21B, 0x21C and 0x223 and nothing
// else for their walls. The difference between the two dungeons is the palette,
// and only the palette. See tools/rogue/make_victory_road_palettes.py.
//
// The slivers come along for free for the same reason: they were appended to
// gMetatiles_Cave, so these tilesets inherit them with no second append.
#define VICTORYROAD_METATILE_FLOOR            0x201  // MB_CAVE, so encounters fire
#define VICTORYROAD_METATILE_WALL_INTERIOR_L  0x210
#define VICTORYROAD_METATILE_WALL_INTERIOR_M  0x211
#define VICTORYROAD_METATILE_WALL_INTERIOR_R  0x212
#define VICTORYROAD_METATILE_WALL_FACE_L      0x218
#define VICTORYROAD_METATILE_WALL_FACE_M      0x219
#define VICTORYROAD_METATILE_WALL_FACE_R      0x21A
#define VICTORYROAD_METATILE_WALL_NORTH_L     0x220
#define VICTORYROAD_METATILE_WALL_NORTH_M     0x209
#define VICTORYROAD_METATILE_WALL_NORTH_R     0x222
#define VICTORYROAD_METATILE_WALL_CORNER_NW   0x21B  // open SE diagonal
#define VICTORYROAD_METATILE_WALL_CORNER_NE   0x21C  // open SW diagonal
#define VICTORYROAD_METATILE_WALL_CORNER_S    0x223  // open NW or NE diagonal
#define VICTORYROAD_METATILE_SLIVER_VERT      0x39E
#define VICTORYROAD_METATILE_SLIVER_HORZ      0x39F
#define VICTORYROAD_METATILE_SLIVER_VERT_TOP  0x3A0
#define VICTORYROAD_METATILE_SLIVER_VERT_BOT  0x3A1
#define VICTORYROAD_METATILE_SLIVER_HORZ_L    0x3A2
#define VICTORYROAD_METATILE_SLIVER_HORZ_R    0x3A3
#define VICTORYROAD_METATILE_SLIVER_ISOLATED  0x3A4
#define VICTORYROAD_METATILE_STAIRS_DOWN      0x214
#define VICTORYROAD_METATILE_STAIRS_UP        0x23E

// Glacia's snow, drawn by tools/rogue/make_glacia_snow.py and appended to the
// cave's metatiles.bin after the seven slivers. Adapted from a Red Rescue Team
// snow field, which has exactly the three things the generator already has
// passes for: flat snow is a FLOOR, and the drifts and rocks are DECOR - a
// block swapped for a variant with collision and elevation copied from the
// base, so they cannot change reachability.
//
// These live in the SHARED cave files, because the Victory Road tilesets share
// gMetatiles_Cave with Granite Cave - that sharing is why they are cheap. The
// cave can see them and never paints them, which costs it nothing but ROM.
//
// Drawn entirely in palette 6, so Glacia's existing recolour renders them as
// snow with no new palette: white at 8, pale ice at 1 and 9, a blue ladder at
// 2-7. Under Granite Cave's palette 6 the same tiles are brown, which does not
// matter for the same reason.
//
// The floor is fine per-pixel noise over three ADJACENT indices and nothing
// else - see the note in make_glacia_snow.py for why a swell and a few glints
// were both wrong in kind.
#define VICTORYROAD_METATILE_SNOW_FLOOR       0x3A5
#define VICTORYROAD_METATILE_SNOW_DRIFT_L     0x3A6  // 2 wide, needs _R east
#define VICTORYROAD_METATILE_SNOW_DRIFT_R     0x3A7
#define VICTORYROAD_METATILE_ICE_ROCK         0x3A8

// The cliff pieces reshaded to stand in snow instead of on the cave floor.
//
// The cave's wall art has the cave FLOOR baked into whichever edges face
// floor - the base of a cliff is drawn as ground inside the wall's own
// metatile so it blends into 0x201 below. Borrow that art and put a different
// floor under it and the band stays cave-coloured: against pale snow the wall
// reads as a grey rectangle with a strip of bare rock along the bottom rather
// than as an outcrop standing in snow.
//
// This is the first theme in the project to mix a CUSTOM floor with a BORROWED
// wall set. Every earlier one either kept the donor's own floor (Mirage Tower
// walks on 0x201) or took its whole wall table from the same tileset as its
// floor (Fiery Path), so the two always matched by construction. Any future
// theme doing the same mixing will need the same treatment.
//
// Only these five. The north row is untouched because its light top band is
// index 3, LIGHTER than the floor's dominant 4 - the wall's own lit top edge,
// not ground. A cliff face has a visible base; a cliff top does not. The
// corners are untouched because no cardinal neighbour of a corner is floor.
//
// Each keeps the attribute of the piece it replaces, so behaviour and layer
// type are unchanged and only the art differs.
#define VICTORYROAD_METATILE_SNOW_FACE_L      0x3A9
#define VICTORYROAD_METATILE_SNOW_FACE_M      0x3AA
#define VICTORYROAD_METATILE_SNOW_FACE_R      0x3AB
#define VICTORYROAD_METATILE_SNOW_INTERIOR_L  0x3AC
#define VICTORYROAD_METATILE_SNOW_INTERIOR_R  0x3AD


// Lapis Cave, Glacia's, and the first theme in the project whose tileset is
// neither vanilla nor a recolour of one. gTileset_General +
// gTileset_RogueLapisCave, built by tools/rogue/import_tile_sheet.py from a
// Mystery Dungeon: Red Rescue Team sheet ripped and formatted by
// SilverDeoxys563 - the same source the snow field above was adapted from,
// imported wholesale this time rather than redrawn.
//
// NONE OF THIS TABLE WAS MINED. Every other theme's wall vocabulary had to be
// recovered by reading vanilla layouts, because vanilla ships maps and not
// rules. This sheet ships the rules: a Legend column giving each cell's
// neighbour mask, which the importer decodes and matches against the twenty
// PaintWalls slots directly. All twenty matched at full score, so nothing here
// was guessed and nothing needed a mock to disambiguate.
//
// Two things follow that no other theme gets:
//
//  * FOUR DISTINCT INSIDE CORNERS. Vanilla Emerald never drew them, which is
//    why every other table repeats one metatile across CORNER_NW and CORNER_NE.
//    This is the first where the two differ.
//  * NO COMPOSED SLIVERS. The cave needed seven spliced metatiles, Fiery Path
//    six, the ocean seven - vanilla's walls are never one block thick. All
//    seven sliver cases are native here.
//
// The interior is a flat dark block, so a wall mass reads as void with a
// crystal rim rather than as textured rock. That is the sheet's own look, and
// it is also the only fill it has that tiles invisibly: measured seam 0.0,
// against 35-42 for every textured rock face in the Meteor Falls tileset that
// was evaluated beside it, where a fill at that seam became visible corduroy.
//
// THE FLOOR IS NOT FROM THIS SHEET. The walls are Lapis Cave's crystal; the
// ground is Mt. Freeze's snow, from a second rip by the same author, because
// this theme is snowing on the player and neither sheet has both. The importer
// composes a tileset from blocks across sheets and quantises everything sharing
// a palette slot together - ground and its two decor variants are all slot 7,
// twelve colours between them. Lapis' own ground is still in the sheet and is
// simply not imported; swapping back is a one-word change to the block table.
//
// The ids below did not move when the ground was swapped, which is luck worth
// naming rather than a guarantee: both sheets carry the same 47 ground cells in
// the same legend order, so the block landed at the same offset. A sheet with a
// different cell count would shift every id after the walls.
#define LAPIS_METATILE_FLOOR                  0x233  // MB_CAVE, so encounters fire
#define LAPIS_METATILE_WALL_INTERIOR_L        0x203
#define LAPIS_METATILE_WALL_INTERIOR_M        0x204
#define LAPIS_METATILE_WALL_INTERIOR_R        0x205
#define LAPIS_METATILE_WALL_FACE_L            0x206
#define LAPIS_METATILE_WALL_FACE_M            0x207
#define LAPIS_METATILE_WALL_FACE_R            0x208
#define LAPIS_METATILE_WALL_NORTH_L           0x200
#define LAPIS_METATILE_WALL_NORTH_M           0x201
#define LAPIS_METATILE_WALL_NORTH_R           0x202
#define LAPIS_METATILE_WALL_CORNER_SE         0x21D  // open SE diagonal
#define LAPIS_METATILE_WALL_CORNER_SW         0x21E  // open SW
#define LAPIS_METATILE_WALL_CORNER_NW         0x220  // open NW
#define LAPIS_METATILE_WALL_CORNER_NE         0x21F  // open NE, and NOT the same
#define LAPIS_METATILE_SLIVER_VERT            0x20C
#define LAPIS_METATILE_SLIVER_HORZ            0x20A
#define LAPIS_METATILE_SLIVER_VERT_TOP        0x210
#define LAPIS_METATILE_SLIVER_VERT_BOT        0x214
#define LAPIS_METATILE_SLIVER_HORZ_L          0x211
#define LAPIS_METATILE_SLIVER_HORZ_R          0x213
#define LAPIS_METATILE_SLIVER_ISOLATED        0x20D
// A crack in the ice, drawn by import_tile_sheet.py - no longer gTileset_
// General's grey warp 0x0A7, which was the one piece of this theme that did not
// belong. Drawn in palette 7, the snow, so the corners are the snowfield
// itself; but snow runs 157 to 240 luminance and has no dark whatsoever, so the
// mouth and rim are APPENDED into its three free slots, lifted verbatim out of
// palette 6. The well is therefore lit like the crystal walls it cuts into.
#define LAPIS_METATILE_STAIRS                 0x28F
// Mt. Freeze's Ground Alt 1 and Alt 2 - a small clump and a swept drift, both
// drawn on the plain snow in the SAME palette, so they read as surface texture
// rather than as objects sitting on it. One cell each is all the sheet has.
//
// They carry the ground's attribute, not a bare one. Decor replaces a floor
// block in place, so a decor metatile without MB_CAVE would be a hole in the
// encounter surface that looks exactly like snow - check_encounter_flags.py
// tests what the theme table names as its floor, and would not see it.
#define LAPIS_METATILE_DECOR_CLUMP            0x25E
#define LAPIS_METATILE_DECOR_DRIFT            0x25F


// Ever Grande, the flower meadow, and Wallace's. gTileset_General +
// gTileset_EverGrande.
//
// THE WALL IS THE PRIMARY'S. 0x068-0x07A is gTileset_General's cliff nine
// slice, on a stride of 8 like every other nine slice in this project. A
// neighbour-mask census over EverGrandeCity_Layout could find nothing above 55%
// because that map is a multi-level elevation plateau and the census pools
// faces at four different heights - the ocean's pooling mistake exactly. One
// isolated 3x3 cliff mass on MtPyre_Summit_Layout gives the whole table on
// sight, which is the technique section 4 already recommends over counting.
//
// The four inside corners are the ones mined for the ocean, here in their own
// tan palette rather than recoloured into Mossdeep's.
#define EVERGRANDE_METATILE_FLOOR             0x27C  // MB_SHORT_GRASS, no encounters
#define EVERGRANDE_METATILE_WALL_NORTH_L      0x068
#define EVERGRANDE_METATILE_WALL_NORTH_M      0x069
#define EVERGRANDE_METATILE_WALL_NORTH_R      0x06A
#define EVERGRANDE_METATILE_WALL_INTERIOR_L   0x070
#define EVERGRANDE_METATILE_WALL_INTERIOR_M   0x071
#define EVERGRANDE_METATILE_WALL_INTERIOR_R   0x072
#define EVERGRANDE_METATILE_WALL_FACE_L       0x078
#define EVERGRANDE_METATILE_WALL_FACE_M       0x079
#define EVERGRANDE_METATILE_WALL_FACE_R       0x07A
#define EVERGRANDE_METATILE_CORNER_OPEN_SE    0x074
#define EVERGRANDE_METATILE_CORNER_OPEN_SW    0x089
#define EVERGRANDE_METATILE_CORNER_OPEN_NW    0x07D
#define EVERGRANDE_METATILE_CORNER_OPEN_NE    0x07B

// This is the first theme that needs NO composed sliver art, because the
// corridors are 5 wide and the vertical case then never occurs at all - 0 hits
// across two mock floors, against 19 at 3 wide. What is left is the horizontal
// one, and 0x079 is simply the cliff's own south face: vanilla uses it for a
// one-block-thick horizontal wall 492 times of 990, and for its two caps at 77%
// and 67%. Showing a south face and nothing else IS what such a wall looks like
// from below, so there is nothing to draw.
//
// The three vertical slots sit on the wall interior, the way underwater leaves
// all seven, and the mock's unhit-slot report is what confirms they never fire.
#define EVERGRANDE_METATILE_SLIVER_HORZ       0x079

// Ever Grande's cobble garden wall, and the arena dais. arenaPlatform paints
// one wall block ringed by floor, which the autotiler resolves to
// WALL_SLIVER_ISOLATED, so this is what Wallace stands on.
//
// It is deliberately NOT used for the ordinary slivers. Tried there first and
// it is wrong at game scale: its shaded face is blue-grey against the cliff's
// warm tan, so the vertical piece reads as a pillar of a foreign material and
// the horizontal one as a hole in the ground. It works in vanilla as a long
// terrace wall bordering a field, never as a one-block gap in a rock mass.
#define EVERGRANDE_METATILE_COBBLE            0x232

// The brick path, standing in for a paved terrace. It is a PATH and not a
// region: 0x23C is a left edge, 0x23E a right edge, and there is NO north or
// south edge art at all - the mask census puts continues-NSWE, SWE and NWE all
// on the same fill. So a blob's top and bottom are hard cuts. It survives
// anyway because ApplyFloorPatches erodes blobs round and the brick texture is
// busy enough that the cut does not register.
#define EVERGRANDE_METATILE_PATH_W            0x23C
#define EVERGRANDE_METATILE_PATH_MID          0x23D
#define EVERGRANDE_METATILE_PATH_E            0x23E

// Ours, appended by tools/rogue/make_evergrande_tiles.py, which is the ONLY
// thing that may append to this tileset.
//
// The exit is the mint floor's own bottom layer under a new palette-8 stone
// overlay, drawn in the woods stairs' shape because that is what the player has
// already learned means down. Its attribute is COVERED rather than NORMAL, or
// the overlay would draw over them while they stand on it.
#define EVERGRANDE_METATILE_STAIRS_DOWN       0x2A8

// THE FLOOR HAS TWO ENCOUNTER SURFACES, and they differ in what stepping on
// them does, not only in how they look.
//
// The short beds: sixteen entries referencing vanilla's flower tiles byte for
// byte, differing only in the behaviour. Vanilla's own are MB_NORMAL, and
// neither this tileset nor gTileset_Mauville holds a single metatile with
// TILE_FLAG_HAS_ENCOUNTERS, so a flower with wild Pokemon in it had to be a new
// entry. No new art at all.
//
// MB_UNUSED_05 carries TILE_FLAG_HAS_ENCOUNTERS and nothing else - its one
// reference in the engine is a function nothing calls - so these spawn wild
// Pokemon, draw no overlay and do not clip the player. That is how Ever Grande
// City itself treats them: you walk over them.
//
// EIGHT PHASES, NOT EIGHT VARIANTS - see RoguePatchLayer.phase. The pink and
// yellow sets are the same eight pictures under two palettes, and vanilla uses
// one colourway per field, never mixed.
#define EVERGRANDE_METATILE_FLOWERS_PINK      0x2A9  // 0x2A9-0x2B0
#define EVERGRANDE_METATILE_FLOWERS_YELLOW    0x2B1  // 0x2B1-0x2B8
#define EVERGRANDE_FLOWER_PHASE               8

// The tall surface: gTileset_General's own long grass 0x015 under a bloom
// overlay, keeping 0x015's attribute so it stays MB_LONG_GRASS. This is what
// the wade-through curtain belongs to - a full-height overlay that hides the
// player's lower half needs something full-height underfoot, and the beds are
// bold and LOW.
//
// Eight again so the patch layer's phase works the same way, but THREE OF THE
// EIGHT ARE BARE. A feature baked into a metatile repeats every 16 pixels and
// becomes a lattice; gaps in the cycle are what make the blossoms punctuate the
// grass rather than rule it, and a bare one costs no tiles, since an entirely
// transparent top-layer quadrant is the entry 0x0000.
#define EVERGRANDE_METATILE_LONG_GRASS        0x2B9  // 0x2B9-0x2C0
#define EVERGRANDE_LONG_GRASS_PHASE           8


// Murky Cave, Steven's finale, and the last dungeon that was still wrapping to
// another theme's art. gTileset_General + gTileset_RogueMurkyCave, imported
// from the Murky Cave sheet.
//
// Carved pillars and ochre rubble. It is the only one of the four imported
// sheets that reads as somewhere BUILT rather than somewhere grown, which is
// the note to end a run on - the player has been through woods, caves, sea and
// jungle, and finishes somewhere that was made.
//
// THE FIRST BLOCK ON ANY SHEET THAT DID NOT FIT 4bpp. Its walls want seventeen
// colours against a palette's fifteen, so import_tile_sheet.py reduces by
// merging the pair with the lowest estimated pixel error - distance times the
// rarer count - rather than by dropping the rarest outright. Here that folds
// one near-duplicate brown (distance 16) and then the two mossy greens into
// each other (distance 50): the moss survives as one tone instead of two, where
// dropping by rarity would have taken the only green out of a brown wall. Both
// wall Alt columns are free on top of that, adding no colour the walls lack.
#define MURKY_METATILE_FLOOR             0x239  // MB_CAVE, so encounters fire
#define MURKY_METATILE_WALL_INTERIOR_L   0x203
#define MURKY_METATILE_WALL_INTERIOR_M   0x204
#define MURKY_METATILE_WALL_INTERIOR_R   0x205
#define MURKY_METATILE_WALL_FACE_L       0x206
#define MURKY_METATILE_WALL_FACE_M       0x207
#define MURKY_METATILE_WALL_FACE_R       0x208
#define MURKY_METATILE_WALL_NORTH_L      0x200
#define MURKY_METATILE_WALL_NORTH_M      0x201
#define MURKY_METATILE_WALL_NORTH_R      0x202
#define MURKY_METATILE_WALL_CORNER_SE    0x21D
#define MURKY_METATILE_WALL_CORNER_SW    0x21E
#define MURKY_METATILE_WALL_CORNER_NW    0x220
#define MURKY_METATILE_WALL_CORNER_NE    0x21F
#define MURKY_METATILE_SLIVER_VERT       0x20C
#define MURKY_METATILE_SLIVER_HORZ       0x20A
#define MURKY_METATILE_SLIVER_VERT_TOP   0x210
#define MURKY_METATILE_SLIVER_VERT_BOT   0x214
#define MURKY_METATILE_SLIVER_HORZ_L     0x211
#define MURKY_METATILE_SLIVER_HORZ_R     0x213
#define MURKY_METATILE_SLIVER_ISOLATED   0x20D

// Six wall variants - five from Wall Alt 1 and one from Wall Alt 2 - each
// paired to the specific wall case it varies by its position in the legend.
#define MURKY_METATILE_WALL_ALT_NORTH_M  0x22F  // varies 0x201
#define MURKY_METATILE_WALL_ALT_INT_L    0x230  // varies 0x203
#define MURKY_METATILE_WALL_ALT_INT_M    0x231  // varies 0x204
#define MURKY_METATILE_WALL_ALT_INT_R    0x232  // varies 0x205
#define MURKY_METATILE_WALL_ALT_FACE_M   0x233  // varies 0x207
#define MURKY_METATILE_WALL_ALT_INT_M2   0x234  // varies 0x204, a second one

// Ground Alt and "Unused Ground" - the second is art the original game never
// used, which costs nothing to put back.
#define MURKY_METATILE_FLOOR_ALT_1       0x264
#define MURKY_METATILE_FLOOR_ALT_2       0x265

// Pools. MB_PUDDLE, so the player reflects in them and splashes through, and so
// the theme feeds only the land encounter branch - see the jungle's water.
#define MURKY_METATILE_WATER_NW          0x266
#define MURKY_METATILE_WATER_N           0x267
#define MURKY_METATILE_WATER_NE          0x268
#define MURKY_METATILE_WATER_W           0x269
#define MURKY_METATILE_WATER_MID         0x26A
#define MURKY_METATILE_WATER_E           0x26B
#define MURKY_METATILE_WATER_SW          0x26C
#define MURKY_METATILE_WATER_S           0x26D
#define MURKY_METATILE_WATER_SE          0x26E

// Cut, not opened. The only one of the three drawn descents that runs SQUARE to
// the tile edge with no floor showing at any corner, because this is the only
// tileset that reads as somewhere built - and a stairwell in a built place was
// made. Its own floor stone spans 46 to 215 luminance, so unlike Lapis it
// needed nothing appended and nothing borrowed.
#define MURKY_METATILE_STAIRS            0x295


// Woods uses the same two elevations as caves.
#define DUNGEON_ELEVATION_FLOOR 3
#define DUNGEON_ELEVATION_WALL  0

// Water is elevation 1, not 3. Every other theme walks at 3; vanilla puts deep
// water at elevation 1 in 100% of its blocks and ocean water in 92%. Getting
// this wrong is invisible in a diff and in a mock, so it is spelled out.
#define DUNGEON_ELEVATION_WATER 1

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

    // Every cardinal is wall, so only a diagonal can be open. NAMED FOR THE
    // OPEN DIAGONAL, which is the only way to read them without getting it
    // backwards - the old names described the wall's position instead, so
    // WALL_CORNER_NW took the art a tileset calls its SOUTH-EAST corner and
    // Fiery Path's table looks wrong until you know that.
    //
    // There are four cases and there used to be three: NW and NE shared one
    // slot, so a tileset with distinct art for them could not say so. That is
    // what left the ocean's cliffs notched.
    WALL_CORNER_OPEN_SE, WALL_CORNER_OPEN_SW,
    WALL_CORNER_OPEN_NW, WALL_CORNER_OPEN_NE,
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

    // Nonzero makes each slot's id the BASE of `phase` consecutive metatiles,
    // and the one actually painted is base + (y - x) mod phase.
    //
    // This exists because Ever Grande's flower metatiles are not interchangeable
    // variants: each set of eight is eight PHASES of a diagonal banding, and 85%
    // of the 341 flower blocks on the vanilla map satisfy
    // variant = (y - x + k) mod 8 for one of three per-field offsets - one
    // constant per field, wherever the artist started. Picking among them at
    // random, which is what a position hash would do, destroys the pattern and
    // turns rows of alternating colour into noise.
    //
    // A diagonal is also cheaper than a hash, and like the rest of this pass it
    // consumes no RNG, so WriteFloorBlocks can repaint a floor without the
    // flowers reshuffling under the player.
    u8 phase;
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

    // Which MAP the theme's floors live on, as opposed to which layout supplies
    // its tilesets. Almost always MAP_ROGUE_DUNGEON_FLOOR: the generator swaps
    // gMapHeader.mapLayout per theme, so one map carries every theme that
    // differs only in art.
    //
    // A theme needs its own map only for something the map HEADER owns, and the
    // header is read out of ROM by warp group and id - GetCurrentMapType goes
    // through GetMapTypeByWarpData, not gMapHeader - so the RAM patch that
    // swaps tilesets cannot reach it. Underwater is the case that needs it:
    // MAP_TYPE_UNDERWATER is what hands the player the diving avatar on
    // arrival, with no Dive HM and no party requirement, and it also carries
    // the underwater battle backdrop, the diving field effect and the bubble
    // weather. Set it on EVERY theme rather than defaulting, because 0 is a
    // valid map id and a silent wrong map is a very confusing bug.
    u16 mapId;

    // What the floor-entry banner calls this place. Written into
    // gMapHeader.regionMapSectionId on every generate rather than declared in
    // map.json, because almost every theme shares MAP_ROGUE_DUNGEON_FLOOR and a
    // map declares exactly ONE section - so the json can only ever name one of
    // them, and it named GRANITE CAVE for six themes that are not caves.
    //
    // Set it on EVERY theme rather than relying on a default: a designated
    // initialiser leaves 0, and 0 is MAPSEC_LITTLEROOT_TOWN - a wrong answer
    // that looks like a deliberate one. check_dungeon_names.py fails on any
    // theme that does not name itself.
    u8 mapSecId;

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

    // Berries grow here. Set on the themes that are OUTDOORS in the sense that
    // matters - open sky and soil - which is Petalburg Woods, the Jungle and
    // Ever Grande, and not the caves, New Mauville, the tower or the sea. Left
    // 0 everywhere else, which is what a designated initialiser gives for free.
    u8 berries;

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

    // Ordinary trainers stand wherever the generator drops them, so their sprite
    // has to suit the surface they are standing on - a hiker on the open sea
    // reads as a bug. Two of them so a floor is not all one figure; zero for
    // either leaves the default. Bosses and mini bosses carry their own sprites
    // and ignore this.
    u16 trainerGfx, trainerGfxAlt;

    // Stand the arena's trainer on a solid block of its own. Set for themes
    // whose floor is not something a person can stand on - the ocean would
    // otherwise have Tate and Liza waiting in the middle of the sea.
    //
    // Nothing has to be drawn for it: the block goes down before the autotile
    // pass, which sees one wall surrounded by floor and paints WALL_SLIVER_
    // ISOLATED, already a lone rock. It also turns the arena trainer talk-only,
    // because a boss that keeps its sight range steps off the platform to
    // approach the player and spends the rest of the floor standing on water.
    bool8 arenaPlatform;

    const u16 *species;
    u8 speciesCount;

    // Which of the engine's encounter tables this theme's floors feed, matching
    // the metatile behaviour of whatever surface it actually paints encounters
    // on. WILD_AREA_LAND is 0 and is right for eleven of the thirteen themes,
    // so only the two water ones set it - unlike mapId above, a wrong value
    // here cannot go unnoticed, because check_encounter_flags.py derives the
    // area from the surface's own MB_ and fails if the two disagree.
    //
    // This exists because StandardWildEncounter picks the land or the water
    // branch from the tile under the player, and each branch needs its own
    // non-NULL table in wild_encounters.json before it will call the hook at
    // all. The ocean paints MB_OCEAN_WATER and had only a land table, so it
    // took the water branch, found NULL and returned - no wild encounters, on
    // either water theme, for as long as they have existed.
    u8 wildArea;
};

// A map used for the LAST FEW FLOORS of one theme's dungeon, instead of that
// theme's own. The first thing in this project that varies by floor rather than
// by theme, and it exists because weather is per-map: a theme has exactly one
// mapId, so without this the only weathers available are ones that last a whole
// dungeon.
//
// Expressed as a table rather than a test inside MapForFloor for the usual
// reason - a table is data the tooling can read. check_encounter_flags.py parses
// this one and holds every map named here to the same registration it holds a
// theme's mapId to, which matters more here than anywhere: an override map is
// reachable ONLY on two floors deep in a run, so a missing wild encounter entry
// would be found by playtesting some time around the Elite Four, if at all.
//
// `lastFloors` counts back from the dungeon's end, so it is stated in the same
// terms IsDungeonBossFloor is and survives a change to DUNGEON_SHORT_FLOORS. A
// value of 2 means the boss's floor and the one before it.
struct RogueFloorMapOverride
{
    u8 themeId;     // enum DungeonThemeId - which dungeon's tail this covers
    u8 lastFloors;  // how many floors back from the boss, inclusive
    u16 mapId;
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
// Which FLDEFFOBJ_ the long-grass effect should wear here. The effect id stays
// FLDEFF_LONG_GRASS either way; only the graphic differs, so the flower dungeon
// gets blossoms and the jungle keeps its blades.
u8 RogueDungeon_LongGrassFieldEffectObj(void);
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

// These pick the destination MAP from the floor's theme, which is what keeps an
// underwater floor from being entered on foot. See theme->mapId. EVERY path
// that puts the player on a dungeon floor goes through one of them.
void RogueDungeon_SetWarpToCurrentFloor(void);  // destination only, then WarpIntoMap
void RogueDungeon_WarpToCurrentFloor(void);     // the full ScrCmd_warp sequence
void RogueDungeon_SetRestStopExit(void);        // the rest stop's MAP_DYNAMIC exits
void RogueDungeon_SeedRestStopUnown(void);      // who is watching, and from where

// Item balls. Prepare puts the contents where the finditem macro reads them;
// Hide runs after the pickup and only bites if the ball actually went.
u16 RogueDungeon_PrepareFloorItem(void);
void RogueDungeon_HideTakenFloorItem(void);

// Credits a finished run. From the boss script's run-complete branch only -
// ResetRun is shared with the whiteout and must not count a loss as a win.
void RogueDungeon_OnRunCompleted(void);

// Debug menu support. Describes a floor in one short line; see the debug warp
// tool in src/debug.c.
void RogueDungeon_GetDebugFloorInfo(u16 floor, u8 *dest);

#endif // GUARD_ROGUE_DUNGEON_H
