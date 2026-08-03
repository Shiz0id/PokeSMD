@ A dungeon floor with blossom blowing across it. The THIRD map named for its
@ WEATHER rather than for the theme that wants it, after RogueDungeonFog and
@ RogueDungeonSnow, and named that way on purpose: weather lives in the map
@ HEADER, and GetCurrentMapType and the weather loader read that out of ROM by
@ warp group and id rather than from gMapHeader, so the RAM patch that swaps
@ tilesets per theme cannot reach it. Any later theme that wants petals points
@ its mapId here rather than adding a fourth map.
@
@ It shares LAYOUT_ROGUE_DUNGEON_FLOOR with RogueDungeonFloor, so mapLayoutId is
@ identical on both and every dispatch keying on it is untouched, and every
@ script is shared - the object events point straight at
@ RogueDungeonFloor_EventScript_Trainer and the frame table below is that map's.
@
@ WEATHER_PETALS is ours, at 16. Slots 16 to 19 were an unused gap between
@ WEATHER_ABNORMAL and the route weather cycles, and WEATHER_COUNT was already
@ 24, so the constant cost nothing. Three tables needed the new value:
@ sWeatherFuncs, which is indexed with no bounds check; sWeatherNames, debug
@ only; and TranslateWeatherNum, which is the one that bites - without a case
@ there a map header asking for petals falls through to
@ default: return WEATHER_NONE and absolutely nothing happens, with no error.
@
@ The effect is the snow's, with a wider drift and the snow's own CUT feature
@ finished. InitSnowflakeSpriteMovement writes tFallCounter, tFallDuration and
@ tDeltaY2 on every spawn and nothing reads them, and WaitSnowflakeSprite is
@ UNUSED: a pause-and-resume cycle was built and left inert. Snow does not need
@ it; a petal that stalls, hangs and then drops again is most of what separates
@ blossom from confetti falling at a constant rate.
@
@ Unlike the snow this IS only cosmetic, and that is a decision rather than an
@ omission. B_OVERWORLD_SNOW is GEN_LATEST, so a battle on RogueDungeonSnow sets
@ B_WEATHER_SNOW and hands every Ice type 1.5x Defense. Petals are deliberately
@ absent from the overworld-to-battle switch in battle_util.c, so gBattleWeather
@ stays clear and Wallace's floor is mechanically plain.

RogueDungeonPetals_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RogueDungeonFloor_OnFrame
	.byte 0
