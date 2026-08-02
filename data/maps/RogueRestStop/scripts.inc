RogueRestStop_MapScripts::
	.byte 0

@ VAR_0x800B is the nurse local id the shared script animates.
RogueRestStop_EventScript_Nurse::
	setvar VAR_0x800B, 1
	call Common_EventScript_PkmnCenterNurse
	waitmessage
	waitbuttonpress
	release
	end
