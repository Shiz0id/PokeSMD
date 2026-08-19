#ifndef GUARD_CONSTANTS_WEATHER_H
#define GUARD_CONSTANTS_WEATHER_H

#define WEATHER_NONE                    0
#define WEATHER_SUNNY_CLOUDS            1
#define WEATHER_SUNNY                   2
#define WEATHER_RAIN                    3
#define WEATHER_SNOW                    4   // Unused
#define WEATHER_RAIN_THUNDERSTORM       5
#define WEATHER_FOG_HORIZONTAL          6
#define WEATHER_VOLCANIC_ASH            7
#define WEATHER_SANDSTORM               8
#define WEATHER_FOG_DIAGONAL            9   // Unused
#define WEATHER_UNDERWATER              10  // Unused
#define WEATHER_SHADE                   11  // Original name was closer to WEATHER_CLOUDY/OVERCAST
#define WEATHER_DROUGHT                 12
#define WEATHER_DOWNPOUR                13
#define WEATHER_UNDERWATER_BUBBLES      14
#define WEATHER_ABNORMAL                15  // The alternating weather during Groudon/Kyogre conflict
// 16-19 were an unused gap. Petals blow across the Ever Grande flower
// dungeon. Purely cosmetic: deliberately absent from the overworld-to-battle
// weather switch in battle_util.c, so it never reaches gBattleWeather.
#define WEATHER_PETALS                  16
// Downpour's rain with the lightning cut out - the jungle dungeon's, and the
// weather vanilla turns out not to have. WEATHER_DOWNPOUR looks exactly right
// for a jungle (24 raindrops against rain's 10, falling faster and steeper)
// but its Main is Thunderstorm_Main, so it flashes and thunders on a 360-720
// frame cycle; isDownpour gates only sprite motion and nothing in the bolt
// machine reads it. WEATHER_RAIN is the only stock rain without bolts, and it
// is also the lightest one.
//
// So this is Downpour_InitVars' settings driven by Rain_Main: the same first
// three states, and then it stops instead of walking into
// THUNDER_STATE_INIT_CYCLE_1. No new art, no new sprites, no new palette.
//
// IT REACHES BATTLE, unlike the petals - it is in battle_util.c's switch and
// sets B_WEATHER_RAIN_NORMAL like every other rain. Being a rain that vanilla
// has never heard of is the expensive part; see IsWeatherRainy.
#define WEATHER_MONSOON                 17
// Driving snow, for the last two floors of Glacia's dungeon - her arena and the
// approach to it. The snow's sprites and storage, given real horizontal
// velocity instead of the sine wobble, so it streaks across the screen rather
// than drifting down it. See Blizzard_InitVars.
//
// MECHANICAL, and it has to be: WEATHER_SNOW already gives every Ice type 1.5x
// Defence on Glacia's floors, so a blizzard missing from battle_util.c's switch
// would silently TAKE THAT AWAY on the two floors where she matters most. A new
// weather being cosmetic by default is the trap; here the default is also a
// regression.
#define WEATHER_BLIZZARD                18
// Falling leaves for the woods, which is dungeon 1 - so for most runs this is
// the first weather the player ever sees, and it is meant to be calm. Purely
// cosmetic, like the petals and for the same reason: nothing about the opening
// ten floors should be mechanically sharpened.
//
// THE ART IS VANILLA'S, UNMODIFIED. graphics/battle_anims/sprites/leaf.png is a
// nine-frame 16x16 tumbling rotation already in the ROM as ANIM_TAG_LEAF, and
// field_weather_effect.c INCGFXes that same PNG a second time as plain .4bpp
// while the battle anim keeps its compressed .smol copy. The recolour from
// vanilla's green to amber is entirely in graphics/weather/leaves.pal, 32 bytes
// - green was measured against a real woods floor and disappears over tall
// grass and canopy, which is most of one. See tools/rogue/make_leaf_weather.py.
//
// THIS TAKES THE LAST FREE ID IN THE 16-19 GAP. 20-23 are real values, not
// padding. A sixth project weather has to raise WEATHER_COUNT and append at 24,
// which is safe - nothing range-tests the aggregates, and the only tables sized
// by WEATHER_COUNT are sWeatherNames and gWeatherStartsStringIds - but it is no
// longer free, so read this before assuming a number is available.
#define WEATHER_LEAVES                  19
#define WEATHER_ROUTE119_CYCLE          20
#define WEATHER_ROUTE123_CYCLE          21
#define WEATHER_FOG                     22  // Aggregate of WEATHER_FOG_HORIZONTAL and WEATHER_FOG_DIAGONAL
#define WEATHER_DYNAMIC                 23
// Zubats fluttering through the cave dungeon, and THE FIRST WEATHER PAST THE
// 16-19 GAP - which is why it is here at 24 rather than in the tidy block with
// the other four, and why WEATHER_COUNT moved for the first time.
//
// Appending was checked rather than assumed, and it is safe: nothing anywhere
// range-tests the aggregates at 20-23, which appear only as case labels in
// TranslateWeatherNum; sWeatherFuncs is designated-init and not sized by the
// count; and the only two tables WEATHER_COUNT sizes are sWeatherNames and
// gWeatherStartsStringIds, at 26 bytes a row. The aggregates did not have to
// stay last. Cost of the sixth project weather: 52 bytes of ROM.
//
// THE ART IS A FOLLOWER POKEMON'S, NOT A WEATHER SHEET, and that is the whole
// experiment. Every other weather in the game owns a sprite sheet;
// UpdateZubatSprite drives gSpeciesInfo[SPECIES_ZUBAT].overworldData through
// CreateObjectGraphicsSprite, so the effect ships with no new art, no new
// palette and no new asset of any kind. See CreateZubatSprite.
//
// Purely cosmetic, like the petals and the leaves: deliberately absent from
// the overworld-to-battle switch in battle_util.c. The cave is dungeon 2 and
// had no weather before this, so there is no mechanical effect here to remove
// by omission - the trap the blizzard had to avoid and this one does not.
#define WEATHER_ZUBATS                  24
// Gulls over the open ocean, and the most elaborate weather on this branch:
// drifting clouds, a flock of Wingull, the occasional Pelipper cruising
// through beneath them, and a REFLECTION of every bird painted on the sea.
//
// IT SHARES ITS WHOLE IMPLEMENTATION WITH WEATHER_ZUBATS. The difference
// between the two is a table of struct RogueFlierKind - species, how many
// aloft, speeds, flight band, whether it reflects - and not a line of code.
// Adding a third flying weather should be a table entry; if it is turning into
// new C, read the FLIERS block in field_weather_effect.c first.
//
// The reflections are vanilla's SetUpReflection with the object event taken
// out: a whole-struct sprite copy that draws the BIRD'S OWN TILES (so it costs
// no VRAM), vertically flipped, on a pond-tinted palette. What makes them sit
// ON the water rather than float over it is the two settings they take from
// sCloudSpriteOamData - ST_OAM_OBJ_BLEND and priority 3 - which is the same
// pair that makes a cloud read as painted onto whatever is beneath it.
//
// Cosmetic, like the zubats and the leaves and unlike the sandstorm: absent
// from battle_util.c's switch. The ocean had no weather before this, so there
// is no mechanical effect here to remove by omission.
//
// THE SECOND ID PAST THE 16-19 GAP, and WEATHER_COUNT moves again for it. Same
// price as the last one: 52 bytes, because sWeatherNames and
// gWeatherStartsStringIds are the only two tables sized by the count.
#define WEATHER_SEABIRDS                25
#define WEATHER_COUNT                   26

// These are used in maps' coord_weather_event entries.
// They are not a one-to-one mapping with the engine's
// internal weather constants above.
#define COORD_EVENT_WEATHER_SUNNY_CLOUDS        1
#define COORD_EVENT_WEATHER_SUNNY               2
#define COORD_EVENT_WEATHER_RAIN                3
#define COORD_EVENT_WEATHER_SNOW                4
#define COORD_EVENT_WEATHER_RAIN_THUNDERSTORM   5
#define COORD_EVENT_WEATHER_FOG_HORIZONTAL      6
#define COORD_EVENT_WEATHER_FOG_DIAGONAL        7
#define COORD_EVENT_WEATHER_VOLCANIC_ASH        8
#define COORD_EVENT_WEATHER_SANDSTORM           9
#define COORD_EVENT_WEATHER_SHADE               10
#define COORD_EVENT_WEATHER_DROUGHT             11
#define COORD_EVENT_WEATHER_ROUTE119_CYCLE      20
#define COORD_EVENT_WEATHER_ROUTE123_CYCLE      21

// These are the "abnormal weather events" that are used
// to find Kyogre and Groudon.

// Groudon/Terra Cave locations
#define TERRA_CAVE_LOCATIONS_START          1
#define ABNORMAL_WEATHER_ROUTE_114_NORTH    (TERRA_CAVE_LOCATIONS_START + 0)
#define ABNORMAL_WEATHER_ROUTE_114_SOUTH    (TERRA_CAVE_LOCATIONS_START + 1)
#define ABNORMAL_WEATHER_ROUTE_115_WEST     (TERRA_CAVE_LOCATIONS_START + 2)
#define ABNORMAL_WEATHER_ROUTE_115_EAST     (TERRA_CAVE_LOCATIONS_START + 3)
#define ABNORMAL_WEATHER_ROUTE_116_NORTH    (TERRA_CAVE_LOCATIONS_START + 4)
#define ABNORMAL_WEATHER_ROUTE_116_SOUTH    (TERRA_CAVE_LOCATIONS_START + 5)
#define ABNORMAL_WEATHER_ROUTE_118_EAST     (TERRA_CAVE_LOCATIONS_START + 6)
#define ABNORMAL_WEATHER_ROUTE_118_WEST     (TERRA_CAVE_LOCATIONS_START + 7)
#define TERRA_CAVE_LOCATIONS                8

// Kyogre/Marina Cave locations
#define MARINE_CAVE_LOCATIONS_START         (TERRA_CAVE_LOCATIONS_START + TERRA_CAVE_LOCATIONS)
#define ABNORMAL_WEATHER_ROUTE_105_NORTH    (MARINE_CAVE_LOCATIONS_START + 0)
#define ABNORMAL_WEATHER_ROUTE_105_SOUTH    (MARINE_CAVE_LOCATIONS_START + 1)
#define ABNORMAL_WEATHER_ROUTE_125_WEST     (MARINE_CAVE_LOCATIONS_START + 2)
#define ABNORMAL_WEATHER_ROUTE_125_EAST     (MARINE_CAVE_LOCATIONS_START + 3)
#define ABNORMAL_WEATHER_ROUTE_127_NORTH    (MARINE_CAVE_LOCATIONS_START + 4)
#define ABNORMAL_WEATHER_ROUTE_127_SOUTH    (MARINE_CAVE_LOCATIONS_START + 5)
#define ABNORMAL_WEATHER_ROUTE_129_WEST     (MARINE_CAVE_LOCATIONS_START + 6)
#define ABNORMAL_WEATHER_ROUTE_129_EAST     (MARINE_CAVE_LOCATIONS_START + 7)
#define MARINE_CAVE_LOCATIONS               8

#define ABNORMAL_WEATHER_LOCATIONS  (MARINE_CAVE_LOCATIONS + TERRA_CAVE_LOCATIONS)
#define ABNORMAL_WEATHER_NONE       0

#endif // GUARD_CONSTANTS_WEATHER_H
