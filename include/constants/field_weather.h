#ifndef GUARD_CONSTANTS_FIELD_WEATHER_H
#define GUARD_CONSTANTS_FIELD_WEATHER_H

#include "config/overworld.h"

// sPaletteColorMapTypes & field_effect_scripts
enum ColorMapType
{
    COLOR_MAP_NONE,
    COLOR_MAP_DARK_CONTRAST,
    COLOR_MAP_CONTRAST,
};

#define MAX_RAIN_SPRITES             24
#define NUM_CLOUD_SPRITES            3
#define NUM_FOG_HORIZONTAL_SPRITES   20
#define NUM_ASH_SPRITES              20
#define NUM_FOG_DIAGONAL_SPRITES     20
#define NUM_SANDSTORM_SPRITES        20
#define NUM_SWIRL_SANDSTORM_SPRITES  5
// Vanilla's sixteen, raised for the same reason the petals were: sixteen read
// as a light dusting on a floor whose whole character is that it is snowing.
// Twenty, and the snow-covered ground under it now sells the rest.
//
// WEATHER_SNOW is used by exactly ONE map in this build, MAP_ROGUE_DUNGEON_SNOW,
// so this global is Glacia's in practice - no stock map pays for it.
//
// Free at load, like the petals: UpdateVisibleSnowflakeSprites spawns one per 36
// frames, but Snow_InitAll spins until the target count is met, so a floor
// arrives with all twenty already up.
#define NUM_SNOWFLAKE_SPRITES        20
// Petals reuse the snowflake sprite array, so this must never exceed its 101.
//
// MORE than the snow's, not fewer. This was 10 on the reasoning that a
// petal drifts four times as far sideways as a flake and so covers more ground
// per sprite. True, and still the wrong call: it made the air look empty rather
// than breezy, and the blossom IS this floor's character, so it wants to be
// plainly there rather than tastefully suggested.
//
// Raising it costs no start-up delay. UpdateVisiblePetalSprites only spawns one
// per 36 frames, but Petals_InitAll spins that loop until the target is met, so
// a floor loads with all of them already up; the gradual ramp is only ever seen
// on an in-map weather CHANGE, which a dungeon floor never does.
#define NUM_PETAL_SPRITES            24

// Controls how the weather should be changing the screen palettes.
#define WEATHER_PAL_STATE_CHANGING_WEATHER   0
#define WEATHER_PAL_STATE_SCREEN_FADING_IN   1
#define WEATHER_PAL_STATE_SCREEN_FADING_OUT  2
#define WEATHER_PAL_STATE_IDLE               3

// Modes for FadeScreen
#define FADE_FROM_BLACK  0
#define FADE_TO_BLACK    1
#define FADE_FROM_WHITE  2
#define FADE_TO_WHITE    3

// Shadows values
#define BASE_SHADOW_INTENSITY (16 - OW_SHADOW_INTENSITY)
#define SHADOW_COLOR_INDEX     9 // Within the weather palette, shadow sprites' color index

#endif // GUARD_CONSTANTS_FIELD_WEATHER_H
