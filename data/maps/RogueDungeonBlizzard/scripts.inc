@ Driving snow, and the fifth map that exists only to carry a weather setting.
@ The reasoning is RogueDungeonFog's: weather lives in the map HEADER, and
@ GetCurrentMapType and the weather loader read the ROM header by warp group and
@ id rather than gMapHeader, so the RAM patch that swaps tilesets per theme
@ cannot reach it. A theme that wants weather needs a map.
@
@ It shares LAYOUT_ROGUE_DUNGEON_FLOOR like the other four, so mapLayoutId is
@ identical and every dispatch keying on it is untouched.
@
@ THE FIRST ONE THAT IS NOT A WHOLE THEME'S. The other four are reached because
@ a theme points theme->mapId at them, which makes them the weather for all five
@ or ten floors of a dungeon. This one is the last TWO floors of Glacia's -
@ her arena and the approach to it - so the weather closes in as the player
@ nears her rather than being the room they have been walking through since
@ floor 91. That needed a mechanism: see sFloorMapOverrides in rogue_dungeon.c,
@ which MapForFloor consults before falling back on the theme's own map.
@
@ Still named for the WEATHER rather than for Glacia, like the four before it.
@ Nothing here is specific to her, and a second theme wanting driving snow
@ points an override at this same map.
@
@ Every script is shared with RogueDungeonFloor - the object events point
@ straight at that map's scripts, and the frame table below is its own - so
@ there is nothing defined here at all.

RogueDungeonBlizzard_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RogueDungeonFloor_OnFrame
	.byte 0
