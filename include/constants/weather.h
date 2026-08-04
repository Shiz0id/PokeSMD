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
#define WEATHER_ROUTE119_CYCLE          20
#define WEATHER_ROUTE123_CYCLE          21
#define WEATHER_FOG                     22  // Aggregate of WEATHER_FOG_HORIZONTAL and WEATHER_FOG_DIAGONAL
#define WEATHER_DYNAMIC                 23
#define WEATHER_COUNT                   24

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
