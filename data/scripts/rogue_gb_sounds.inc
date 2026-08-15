@ The GB Sounds toggle, opened from the start menu's SOUND entry.
@
@ Today this only flips FLAG_SYS_GBS_ENABLED and says so; the entry exists so
@ the eventual in-menu music player has somewhere to live. The message text is
@ built in C by RogueGbSounds_ScriptToggle, so this is only a msgbox around it.

RogueGbSounds_EventScript_Toggle::
	lockall
	callnative RogueGbSounds_ScriptToggle
	msgbox RogueGbSounds_Text_Result, MSGBOX_DEFAULT
	releaseall
	end

RogueGbSounds_Text_Result:
	.string "{STR_VAR_1}$"
