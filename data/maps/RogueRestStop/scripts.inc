@ Both exits are MAP_DYNAMIC, because which of the two dungeon maps they lead to
@ depends on the next dungeon's theme and a warp event cannot be conditional.
@
@ ON_LOAD rather than setting it on the way in, so it is recomputed however the
@ player got here - including loading a save made inside the rest stop, where
@ the warp the player is about to step on was last written by a different run.
RogueRestStop_MapScripts::
	map_script MAP_SCRIPT_ON_LOAD, RogueRestStop_OnLoad
	.byte 0

RogueRestStop_OnLoad::
	special RogueDungeon_SetRestStopExit
	end

@ VAR_0x800B is the nurse local id the shared script animates.
RogueRestStop_EventScript_Nurse::
	setvar VAR_0x800B, 1
	call Common_EventScript_PkmnCenterNurse
	waitmessage
	waitbuttonpress
	release
	end
