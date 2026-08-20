@ A dungeon floor whose weather is ROLLED FROM A POOL rather than fixed.
@
@ A map per WEATHER, shared by every theme that wants it - the same rule
@ RogueDungeonFog states and for the same reason: weather lives in the map
@ HEADER, and the weather loader reads the ROM header by warp group and id, not
@ gMapHeader, so the RAM patch that swaps tilesets per theme cannot reach it.
@ Here the "weather" is WEATHER_DYNAMIC, an aggregate that resolves through
@ TranslateWeatherNum -> GetDynamicWeather() to one entry of a pool.
@
@ WHICH pool is keyed on gMapHeader.regionMapSectionId, which this project
@ already writes per theme from theme->mapSecId. So two themes can share this
@ one map and still get completely different weather, with no new plumbing.
@ See sDynamicWeatherPools in src/field_weather_effect.c.
@
@ THE ROLL HAPPENS ONCE PER MAP LOAD, not continuously - so this is per-FLOOR
@ variety within a dungeon, not weather that changes while the player stands
@ still. Do not describe it as the latter.
@
@ It shares LAYOUT_ROGUE_DUNGEON_FLOOR with RogueDungeonFloor, so mapLayoutId is
@ identical on both and every dispatch that keys on it keeps working untouched.
@
@ Every script is shared with RogueDungeonFloor - the object events point
@ straight at RogueDungeonFloor_EventScript_Trainer, and the frame table below
@ is that map's - so there is nothing defined here at all.

RogueDungeonDynamic_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RogueDungeonFloor_OnFrame
	.byte 0
