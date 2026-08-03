@ A foggy dungeon floor. A SECOND map rather than a second layout, for exactly
@ the reason RogueDungeonUnderwater is one: weather lives in the map HEADER, and
@ GetCurrentMapType and the weather loader read the ROM header by warp group and
@ id, not gMapHeader, so the RAM patch that swaps tilesets per theme cannot
@ reach it. A theme that wants weather needs a map.
@
@ It shares LAYOUT_ROGUE_DUNGEON_FLOOR with RogueDungeonFloor, so mapLayoutId is
@ identical on both and every dispatch that keys on it keeps working untouched.
@
@ Named for the weather rather than for Phoebe, because nothing here is specific
@ to her floors - any theme that sets mapId to this one gets fog. That is the
@ open question in the handoff doc answered in the cheapest direction: a map per
@ WEATHER, shared by every theme that wants it, not a map per theme.
@
@ Every script is shared with RogueDungeonFloor - the object events point
@ straight at RogueDungeonFloor_EventScript_Trainer, and the frame table below
@ is that map's - so there is nothing defined here at all.

RogueDungeonFog_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RogueDungeonFloor_OnFrame
	.byte 0
