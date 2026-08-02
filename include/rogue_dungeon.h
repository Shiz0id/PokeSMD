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

// Petalburg Woods: gTileset_General + gTileset_Rustboro. Mined from
// LAYOUT_PETALBURG_WOODS the same way the cave values were.
//
// Trees are 2x2 blocks on even coordinates - 95% x-aligned and 99% y-aligned in
// vanilla - which is why the woods generator works on a half-resolution grid
// and needs no autotiling at all.
#define WOODS_METATILE_GRASS      0x001  // plain, no encounters
#define WOODS_METATILE_TALL_GRASS 0x00D  // MB_TALL_GRASS
#define WOODS_METATILE_LONG_GRASS 0x015  // MB_LONG_GRASS, the Route 119 kind
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

// Where long grass meets open ground. Sits immediately after the long grass
// itself in the tile grid, and shows blades meeting a dark base.
#define WOODS_METATILE_LONG_GRASS_BASE_L 0x016
#define WOODS_METATILE_LONG_GRASS_BASE_R 0x017

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

// Light falls from the top left, so a wall shades the floor below it and to its
// right. Derived from NewMauville_Inside: floor with a wall to the north is
// 0x22F 40% of the time, to the west 0x27D 67%, diagonally 0x275 40%.
#define NEWMAUVILLE_METATILE_FLOOR_SHADOW_N  0x22F
#define NEWMAUVILLE_METATILE_FLOOR_SHADOW_W  0x27D
#define NEWMAUVILLE_METATILE_FLOOR_SHADOW_NW 0x275

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

// The only shading this tileset has: a rubble streak on floor against a north
// wall, used by vanilla for 11 of that tile's 13 appearances. The red rock is
// matte, so there is no west shadow at all.
#define FIERYPATH_METATILE_FLOOR_SHADOW_N 0x269

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

// Composed by tools/rogue/make_woods_stairs.py and appended to the Rustboro
// secondary tileset: earth steps in a grass-cornered opening.
//
// This used to be 0x0A7, the dark interior of the vanilla cave mouth, which
// reads as a dark rectangle dropped into grass because the sandy surround that
// frames it never gets painted. Note also that the cave stairs id means
// something else entirely under Rustboro - 0x214 there is a grey stripe - so a
// theme can never inherit another theme's metatile ids.
#define WOODS_METATILE_STAIRS 0x35E

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

enum DungeonStampCorner { STAMP_TL, STAMP_TR, STAMP_BL, STAMP_BR, STAMP_COUNT };

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

struct RogueDungeonTheme
{
    u16 layoutId;
    u8 generator;
    u8 elevationFloor;
    u8 elevationWall;

    u16 floor;
    u16 tallGrass;   // 0 if the theme has none, in which case encounters fire
    u16 longGrass;   // anywhere rather than only in grass
    u16 stairsDown;
    u16 stairsUp;

    u16 wall[WALL_SLOT_COUNT];  // DUNGEON_GEN_CAVE only
    u16 stamp[STAMP_COUNT];     // DUNGEON_GEN_WOODS only

    // Floor directly below or right of a wall is shaded, so a room reads as a
    // room rather than a flat cutout. All three zero opts the theme out.
    u16 shadowNorth;   // wall above
    u16 shadowWest;    // wall to the left
    u16 shadowCorner;  // both, or only diagonally above-left

    // 0 paints every qualifying block - correct for a tileset whose art is a
    // true directional shadow (New Mauville). N paints 1 in N, position-hashed
    // - for a tileset whose "shadow" is really scattered rubble (Fiery Path),
    // where a deterministic band reads as a second floor colour.
    u8 shadowRarity;

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

#define DUNGEON_MAX_ROOMS  8
#define DUNGEON_ROOM_MIN   5
#define DUNGEON_ROOM_MAX  10

// A run mirrors the stock game: 10 floors per dungeon, a mini boss halfway and
// a major battle at the end. Eight gym dungeons, then the Elite Four and the
// Champion, so thirteen dungeons and 130 floors in total.
#define DUNGEON_FLOORS_PER_DUNGEON 10
#define DUNGEON_MINIBOSS_FLOOR      4  // 0-based within the dungeon, so the 5th
#define DUNGEON_BOSS_FLOOR          9  // the 10th

#define DUNGEON_GYM_DUNGEONS 8
#define DUNGEON_GYM_FLOORS   (DUNGEON_GYM_DUNGEONS * DUNGEON_FLOORS_PER_DUNGEON)

#define DungeonIndexOf(floor)    ((floor) / DUNGEON_FLOORS_PER_DUNGEON)
#define DungeonFloorWithin(floor) ((floor) % DUNGEON_FLOORS_PER_DUNGEON)

// Levels are fitted to the stock bosses. The rate slows after the gym stretch
// because the stock game does the same: eight gyms span levels 15 to 46, but
// the Elite Four and Champion only span 46 to 58. A single rate cannot fit both
// - it would put floor 130 at level 71 against a Champion in the mid fifties.
#define DUNGEON_ENCOUNTER_BASE_LEVEL   5
#define DUNGEON_ENCOUNTER_LEVEL_NUM   51  // levels gained per 100 floors
#define DUNGEON_ENCOUNTER_LATE_NUM    24  // per hundred, past the gym stretch
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
void RogueDungeon_IsDungeonEndFloor(void);
void RogueDungeon_GiveChosenStarter(void);
bool8 RogueDungeon_TryHandleWhiteOut(void);
void RogueDungeon_PrepareBossAceOffer(void);
void RogueDungeon_GiveBossAce(void);
bool8 RogueDungeon_IsGeneratedTrainer(void);
bool8 RogueDungeon_HasTrainerBeenBeaten(u8 objectEventId);
bool8 RogueDungeon_IsBossFloor(u16 floor);

#endif // GUARD_ROGUE_DUNGEON_H
