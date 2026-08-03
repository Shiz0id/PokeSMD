@ A snowing dungeon floor. The second map named for its WEATHER rather than for
@ the theme that wants it, which is the pattern RogueDungeonFog established: the
@ weather lives in the map HEADER, GetCurrentMapType and the weather loader read
@ that out of ROM by warp group and id rather than from gMapHeader, so the RAM
@ patch that swaps tilesets per theme cannot reach it.
@
@ It shares LAYOUT_ROGUE_DUNGEON_FLOOR with RogueDungeonFloor, so mapLayoutId is
@ identical on both and every dispatch keying on it is untouched, and every
@ script is shared - the object events point straight at
@ RogueDungeonFloor_EventScript_Trainer and the frame table below is that map's.
@
@ WEATHER_SNOW is marked Unused in constants/weather.h and no vanilla map sets
@ it, but the effect itself is complete and wired at [WEATHER_SNOW] in
@ sWeatherFuncs: sixteen sprites, two flake sizes, sine-wave drift, screen wrap.
@ What was actually left unfinished is a pause-and-respawn cycle -
@ WaitSnowflakeSprite is UNUSED and tFallCounter/tFallDuration/tDeltaY2 are
@ written on every spawn and read by nothing. The flakes fall and wrap instead,
@ which looks fine; nothing on the live path is broken.
@
@ Note this is NOT only cosmetic. B_OVERWORLD_SNOW is GEN_LATEST, so arriving in
@ a battle here sets B_WEATHER_SNOW: Ice types get 1.5x Defense, Blizzard cannot
@ miss, Weather Ball becomes Ice, and Slush Rush / Ice Body / Snow Cloak come
@ online. Gen 9 snow deals no chip damage, unlike hail, so it costs the player's
@ team nothing passively. Glacia's own party is Ice and gets the Defense buff in
@ her boss fight, which is intended.

RogueDungeonSnow_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RogueDungeonFloor_OnFrame
	.byte 0
