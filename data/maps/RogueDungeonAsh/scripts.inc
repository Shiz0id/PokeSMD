@ A dungeon floor with volcanic ash falling through it. The SEVENTH map named
@ for its WEATHER rather than for the theme that wants it, after RogueDungeonFog,
@ Snow, Rain, Petals, Blizzard and Leaves, and named that way for the same
@ reason: weather lives in the map HEADER, and GetCurrentMapType and the weather
@ loader read that out of ROM by warp group and id rather than from gMapHeader,
@ so the RAM patch that swaps tilesets and mapsecs per theme cannot reach it.
@ Any later theme that wants ash points its mapId here.
@
@ It shares LAYOUT_ROGUE_DUNGEON_FLOOR with RogueDungeonFloor, so mapLayoutId is
@ identical on both and every dispatch keying on it is untouched - including
@ RogueDungeon_GetWildMonInfo, which gates the whole procedural encounter table
@ on exactly that comparison. Every script is shared: the object events point
@ straight at RogueDungeonFloor_EventScript_Trainer and the frame table below is
@ that map's.
@
@ WEATHER_VOLCANIC_ASH IS VANILLA'S, AND THAT IS THE ENTIRE POINT OF THIS MAP.
@ A weather unlike anything in the game costs four places and a weather joining
@ a family costs nine, all of them silent; ash costs none, because Route 113 has
@ been asking for it since 2004 and every one of those places is already right.
@ There is no new constant, no sWeatherFuncs row, no TranslateWeatherNum case
@ and no new art. Reach for a vanilla weather before spending a constant slot -
@ 16-19 is full and a new one now has to raise WEATHER_COUNT.
@
@ COSMETIC, and by inheritance rather than by decision: WEATHER_VOLCANIC_ASH is
@ absent from the overworld-to-battle switch in battle_util.c, so gBattleWeather
@ stays clear. That is the right answer here for the reason the leaves' comment
@ gives - the Fiery Path had NO weather at all before this map, so there is no
@ mechanical effect to accidentally remove. Contrast RogueDungeonSandstorm,
@ whose weather IS in that switch and which therefore changes how its dungeon
@ plays; see that map's note.
@
@ THE MUSIC IS THE THEME'S OWN, MUS_MT_CHIMNEY, and not the other weather maps'
@ MUS_ABNORMAL_WEATHER. Moving a theme onto a weather map moves its WHOLE
@ header, music included, and the Fiery Path was on RogueDungeonFloor before
@ this. In practice the theme table's .music wins anyway -
@ RogueDungeon_GetLocationMusic returns ThemeForFloor(floor)->music and
@ GetLocationMusic prefers it over the header - so this line is the fallback for
@ anything that ever lands here off a dungeon floor. Setting it to agree with
@ the theme costs nothing and means the fallback is never a surprise.

RogueDungeonAsh_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RogueDungeonFloor_OnFrame
	.byte 0
