@ A dungeon floor with Zubats flying through it. The NINTH map named for its
@ WEATHER rather than for the theme that wants it - see RogueDungeonAsh for why
@ weather needs a map of its own at all, and why these are named for the
@ weather rather than for the first area to ask for one.
@
@ It shares LAYOUT_ROGUE_DUNGEON_FLOOR with RogueDungeonFloor, so mapLayoutId is
@ identical on both and every dispatch keying on it is untouched - including
@ RogueDungeon_GetWildMonInfo, which gates the whole procedural encounter table
@ on exactly that comparison. Every script is shared with that map, and the
@ frame table below is its.
@
@ WEATHER_ZUBATS IS OURS, AT 24, AND IT IS THE FIRST ONE PAST THE 16-19 GAP.
@ WEATHER_COUNT moved from 24 to 25 for it - the first time that constant has
@ changed on this branch. Six places needed the new value: the constant itself;
@ sWeatherFuncs, which is indexed with no bounds check; sWeatherNames, debug
@ only; gWeatherStartsStringIds, which is sized to WEATHER_COUNT precisely so a
@ missing entry reads a defined 0 rather than the next table along; WEATHER_COUNT;
@ and TranslateWeatherNum, which is the one that bites - without a case there a
@ map header asking for zubats falls through to default: return WEATHER_NONE and
@ absolutely nothing happens, with no crash and no warning.
@
@ THE ART IS A FOLLOWER POKEMON'S AND THERE IS NO NEW ASSET AT ALL. Every other
@ weather in the game owns a sprite sheet; this one asks
@ CreateObjectGraphicsSprite for OBJ_EVENT_GFX_SPECIES(ZUBAT) and drives
@ gSpeciesInfo[SPECIES_ZUBAT].overworldData with a callback of its own. No PNG,
@ no .pal, no INCBIN. See the WEATHER_ZUBATS block in field_weather_effect.c,
@ which also explains why these are plain sprites and not object events: the
@ engine destroys object events off-screen, and a bat whose whole purpose is to
@ leave the screen would be torn down mid-exit.
@
@ Cosmetic, deliberately, like the petals and the leaves and unlike the snow:
@ absent from the overworld-to-battle switch in battle_util.c, so gBattleWeather
@ stays clear. The cave is dungeon 2 and had no weather at all before this map,
@ so there is no mechanical effect here to accidentally remove - the trap the
@ blizzard had to avoid and this one genuinely does not.
@
@ THE MUSIC IS THE THEME'S OWN, MUS_RG_ROCKET_HIDEOUT - FRLG's Cerulean Cave
@ track, despite the name. See RogueDungeonAsh's note: the
@ theme table's .music wins in practice because RogueDungeon_GetLocationMusic is
@ preferred over the map header, and this is the fallback set to agree with it.

RogueDungeonZubats_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RogueDungeonFloor_OnFrame
	.byte 0
