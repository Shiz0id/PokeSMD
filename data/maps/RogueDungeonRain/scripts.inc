@ A rainy dungeon floor, and the fourth map that exists only to carry a weather
@ setting. The reasoning is RogueDungeonFog's: weather lives in the map HEADER,
@ and GetCurrentMapType and the weather loader read the ROM header by warp group
@ and id rather than gMapHeader, so the RAM patch that swaps tilesets per theme
@ cannot reach it. A theme that wants weather needs a map.
@
@ It shares LAYOUT_ROGUE_DUNGEON_FLOOR with RogueDungeonFloor, so mapLayoutId is
@ identical on both and every dispatch that keys on it keeps working untouched.
@ That is also what makes the two overworld.c branches need no edit at all.
@
@ Named for the weather rather than for the jungle, like the three before it:
@ any theme that points theme->mapId here gets rain, and none of them has to be
@ named in this file.
@
@ THE WEATHER IS OURS. Vanilla has three rains and none of them is the one a
@ jungle wants: WEATHER_DOWNPOUR is the right rain to look at - 24 drops against
@ WEATHER_RAIN's 10, falling faster and steeper - but its Main is
@ Thunderstorm_Main, so it flashes lightning and thunders every 360 to 720
@ frames, and WEATHER_RAIN is the only stock rain without bolts and also the
@ lightest. WEATHER_MONSOON is the downpour's rain driven by Rain_Main. See
@ include/constants/weather.h and Monsoon_InitVars.
@
@ AND IT REACHES BATTLE. Every rain lands on B_WEATHER_RAIN_NORMAL in
@ battle_util.c's overworld-to-battle switch, so the whole jungle dungeon is
@ fought in rain: Water boosted, Fire halved, Thunder and Hurricane unable to
@ miss, Solar Beam halved, Swift Swim live. That is a decision, not an inherited
@ default - a new weather is absent from that switch until it is put there, and
@ WEATHER_PETALS is the one that was left out on purpose.
@
@ Every script is shared with RogueDungeonFloor - the object events point
@ straight at that map's scripts, and the frame table below is its own - so
@ there is nothing defined here at all.

RogueDungeonRain_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RogueDungeonFloor_OnFrame
	.byte 0
