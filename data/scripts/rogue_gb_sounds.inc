@ The GB Sounds toggle.
@
@ The start menu's SOUND entry no longer comes through here -- it calls
@ RogueMusicPlayer_Open directly, the way the debug menu is opened, because a
@ script's lockall did not actually hold the field while the menu was up. This
@ is kept for a script or sign that wants the one-shot toggle on its own.

@ The bare toggle, with its own message. Nothing on the start menu reaches this
@ any more -- it is here for a script or sign that wants the one-shot toggle
@ without the whole player.
RogueGbSounds_EventScript_Toggle::
	lockall
	callnative RogueGbSounds_ScriptToggle
	msgbox RogueGbSounds_Text_Result, MSGBOX_DEFAULT
	releaseall
	end

RogueGbSounds_Text_Result:
	.string "{STR_VAR_1}$"
