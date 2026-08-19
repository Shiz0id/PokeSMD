@ A dungeon floor of open sea with gulls over it. The TENTH map named for its
@ WEATHER rather than for the theme that wants it - see RogueDungeonAsh for why
@ weather needs a map of its own at all.
@
@ It shares LAYOUT_ROGUE_DUNGEON_FLOOR with RogueDungeonFloor, so mapLayoutId is
@ identical on both and every dispatch keying on it is untouched. Every script is
@ shared with that map, and the frame table below is its.
@
@ THE ENCOUNTER TABLE IS THE FLOOR MAP'S, ALL THREE OF IT, AND THAT IS THE ONE
@ THING THIS MAP DOES DIFFERENTLY FROM THE NINE BEFORE IT. The ocean is one of
@ only two WILD_AREA_WATER themes, and MAP_ROGUE_DUNGEON_FLOOR is the only
@ dungeon map that carries water_mons and fishing_mons - every weather map
@ before this one has land_mons alone, because every theme before this one was
@ a land theme. Cloning one of them would have taken surfing encounters and the
@ fishing rod off this dungeon silently: the build is clean either way,
@ GetCurrentMapWildMonHeaderId returns HEADER_NONE without a word, and the
@ procedural table in RogueDungeon_GetWildMonInfo never runs because it is
@ applied inside TryGenerateWildMon, which is only reached once the static header
@ has already resolved.
@
@ WEATHER_SEABIRDS IS OURS, AT 25, the second id past the 16-19 gap, and
@ WEATHER_COUNT moved to 26 for it.
@
@ THE ART IS THREE FOLLOWER POKEMON SPRITES AND A VANILLA CLOUD, AND THERE IS NO
@ NEW ASSET AT ALL. The gulls are gSpeciesInfo[SPECIES_WINGULL].overworldData
@ and SPECIES_PELIPPER's, driven through CreateObjectGraphicsSprite; the clouds
@ are WEATHER_SUNNY_CLOUDS' own sprites, created by the same CreateCloudSprites
@ that weather uses. No PNG, no .pal, no INCBIN.
@
@ THE REFLECTIONS ARE THE INTERESTING PART. Each bird gets a whole-struct sprite
@ copy that draws the BIRD'S OWN TILES - so it costs no VRAM - vertically
@ flipped, on a pond-tinted palette, and crucially with ST_OAM_OBJ_BLEND and
@ priority 3 taken from sCloudSpriteOamData. That pair is what makes a cloud read
@ as painted onto what is beneath it rather than floating over it, and a
@ reflection wants exactly the same thing. The distance below the bird is driven
@ by its altitude, so a Pelipper cruising low has a tight reflection and a
@ Wingull near the top of the screen has a distant one.
@
@ Cosmetic, deliberately: absent from the overworld-to-battle switch in
@ battle_util.c, so gBattleWeather stays clear. The ocean had no weather at all
@ before this map, so there is no mechanical effect here to accidentally remove.
@
@ THE MUSIC IS THE THEME'S OWN, MUS_ABANDONED_SHIP. See RogueDungeonAsh's note:
@ the theme table's .music wins in practice and this is the fallback, set to
@ agree with it.

RogueDungeonSeabirds_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RogueDungeonFloor_OnFrame
	.byte 0
