@ The underwater dungeon floor. A SECOND map rather than a second layout: it
@ shares LAYOUT_ROGUE_DUNGEON_FLOOR with RogueDungeonFloor, so every dispatch
@ that keys on mapLayoutId keeps working untouched, and the only thing that
@ differs is the map header - MAP_TYPE_UNDERWATER.
@
@ That map type is the whole reason this map exists. GetCurrentMapType reads the
@ ROM header by warp group and id, NOT gMapHeader, so the RAM patch that swaps
@ tilesets per theme cannot reach it. And it is worth reaching: arriving on an
@ underwater map is what puts the player in the diving avatar, with no Dive HM
@ and no party requirement, the same way arriving on surfable water hands out a
@ surf blob. It also gets the underwater battle backdrop, the diving field
@ effect and the bubble weather for free, all of which a hook would have missed.
@
@ Every script is shared with RogueDungeonFloor - the object events point
@ straight at RogueDungeonFloor_EventScript_Trainer, and the frame table below
@ is that map's - so there is nothing defined here at all.
@
@ The starter table is shared rather than skipped even though floor 1 is never
@ underwater: the debug floor warp deliberately leaves the run state alone, so
@ warping deep with no party is how a test run is started, and that has to be
@ offered starters whichever of the two maps it lands on.

RogueDungeonUnderwater_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RogueDungeonFloor_OnFrame
	.byte 0
