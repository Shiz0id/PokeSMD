@ A dungeon floor with leaves coming down through the canopy. The SIXTH map
@ named for its WEATHER rather than for the theme that wants it, after
@ RogueDungeonFog, Snow, Rain, Petals and Blizzard, and named that way for the
@ same reason: weather lives in the map HEADER, and GetCurrentMapType and the
@ weather loader read that out of ROM by warp group and id rather than from
@ gMapHeader, so the RAM patch that swaps tilesets and mapsecs per theme cannot
@ reach it. Any later theme that wants leaves points its mapId here.
@
@ It shares LAYOUT_ROGUE_DUNGEON_FLOOR with RogueDungeonFloor, so mapLayoutId is
@ identical on both and every dispatch keying on it is untouched, and every
@ script is shared - the object events point straight at
@ RogueDungeonFloor_EventScript_Trainer and the frame table below is that map's.
@
@ THE MUSIC IS MUS_PETALBURG_WOODS AND NOT THE OTHER WEATHER MAPS'
@ MUS_ABNORMAL_WEATHER. The woods ran on RogueDungeonFloor before this map
@ existed, so its music is that map's; copying the petals map wholesale would
@ have moved the theme onto a new header and taken the woods' own track away
@ with it, silently, on a change that was supposed to be about weather. A new
@ map inherits NONE of the old one's header - ask what the theme had before,
@ not only what the weather should add.
@
@ WEATHER_LEAVES is ours, at 19, and it is the LAST id in the 16-19 gap. Five
@ places needed the new value: sWeatherFuncs, which is indexed with no bounds
@ check; sWeatherNames, debug only; gWeatherStartsStringIds, which is sized to
@ WEATHER_COUNT precisely so a missing entry reads a defined 0 instead of the
@ next table along; the constant itself; and TranslateWeatherNum, which is the
@ one that bites - without a case there a map header asking for leaves falls
@ through to default: return WEATHER_NONE and absolutely nothing happens, with
@ no crash and no warning.
@
@ The art is VANILLA'S, unmodified: graphics/battle_anims/sprites/leaf.png, the
@ nine-frame tumbling rotation ANIM_TAG_LEAF draws Razor Leaf with, INCGFXed a
@ second time as plain .4bpp. The only new asset is a 32-byte palette, because
@ vanilla's green ramp was measured against a real woods floor and vanishes over
@ tall grass and canopy. See tools/rogue/make_leaf_weather.py.
@
@ Cosmetic, deliberately, like the petals and unlike the snow: absent from the
@ overworld-to-battle switch in battle_util.c, so gBattleWeather stays clear.
@ The woods is dungeon 1 and had no weather at all before this, so there is no
@ mechanical effect here to accidentally remove - which is the trap the blizzard
@ had to avoid and this one genuinely does not.

RogueDungeonLeaves_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RogueDungeonFloor_OnFrame
	.byte 0
