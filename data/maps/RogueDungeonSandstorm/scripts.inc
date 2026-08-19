@ A dungeon floor in a sandstorm. The EIGHTH map named for its WEATHER rather
@ than for the theme that wants it - see RogueDungeonAsh for why weather needs a
@ map of its own at all, and why these are named for the weather.
@
@ It shares LAYOUT_ROGUE_DUNGEON_FLOOR with RogueDungeonFloor, so mapLayoutId is
@ identical on both and every dispatch keying on it is untouched. Every script
@ is shared with that map, and the frame table below is its.
@
@ THIS ONE IS MECHANICAL, AND UNLIKE EVERY OTHER WEATHER ON THIS BRANCH THAT IS
@ NOT A FREE CHOICE. WEATHER_SANDSTORM is already a case in the
@ overworld-to-battle switch in battle_util.c and sets B_WEATHER_SANDSTORM, so
@ pointing Mirage Tower at this map does not merely repaint it: every battle on
@ the dungeon now chips 1/16 max HP a turn off everything that is not Rock,
@ Ground or Steel, the player's team included, and Rock types take a 1.5x
@ Special Defence boost with them.
@
@ That is a difficulty change, not a coat of paint, and it is deliberate - a
@ sandstone tower standing in Hoenn's desert is the one place in the run where
@ a sandstorm is the obvious reading. But it is the exact inverse of the trap
@ WEATHER_BLIZZARD had to avoid. There the danger was a new weather silently
@ LOSING a battle effect its map already had; here it is a new weather silently
@ GAINING one, because the vanilla constant brings the switch case with it and
@ nothing about adding a map announces that. Both are recorded in the INTENT
@ table in tools/rogue/check_weather_ids.py, which is where the question "was
@ this decided or defaulted" is answerable at all.
@
@ THE MUSIC IS THE THEME'S OWN, MUS_DESERT. See RogueDungeonAsh's note: the
@ theme table's .music wins in practice, and this is the fallback set to agree
@ with it.

RogueDungeonSandstorm_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RogueDungeonFloor_OnFrame
	.byte 0
