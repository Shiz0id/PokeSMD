@ The player-facing charm listing, opened from the start menu.
@
@ The listing itself is built in C by RogueCharm_ScriptBufferSummary, which pages
@ it with CHAR_PROMPT_CLEAR - so this is only a msgbox around a string that
@ already knows how long it is.

RogueCharms_EventScript_ShowList::
	lockall
	callnative RogueCharm_ScriptBufferSummary
	msgbox RogueCharms_Text_List, MSGBOX_DEFAULT
	releaseall
	end

RogueCharms_Text_List:
	.string "{STR_VAR_1}$"
