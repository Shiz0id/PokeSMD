RogueDungeonFloor_MapScripts::
	.byte 0

@ Started from C by RogueDungeon_TryStartStairsScript when the player steps
@ onto the generated exit. Warping to this same map re-enters LoadMapFromWarp,
@ which rolls a fresh seed and so generates the next floor.
RogueDungeonFloor_EventScript_Stairs::
	lockall
	msgbox RogueDungeonFloor_Text_Stairs, MSGBOX_YESNO
	compare VAR_RESULT, YES
	goto_if_eq RogueDungeonFloor_EventScript_NextFloor
	releaseall
	end

RogueDungeonFloor_EventScript_NextFloor::
	addvar VAR_ROGUE_DUNGEON_FLOOR, 1
	warp MAP_ROGUE_DUNGEON_FLOOR
	waitstate
	end

RogueDungeonFloor_Text_Stairs:
	.string "Continue to the next floor?$"

@ Shared by every generated trainer. The opponent is picked per floor, so the
@ ordinary trainerbattle command cannot be used - it bakes the trainer id in at
@ assembly time. The special configures the battle by hand and we jump to the
@ shared trainer tail the engine already provides.
RogueDungeonFloor_EventScript_Trainer::
	special RogueDungeon_SetUpTrainerBattle
	goto EventScript_TryDoNormalTrainerBattle

@ Where SetTrainerBattleEndScript points, so gotopostbattlescript lands here.
RogueDungeonFloor_EventScript_TrainerDone::
	releaseall
	end

@ Boss floors have no exit until the boss falls - that is what forces the fight,
@ since an arena has no other way out.
RogueDungeonFloor_EventScript_BossDone::
	special RogueDungeon_OnBossDefeated
	msgbox RogueDungeonFloor_Text_BossCleared, MSGBOX_DEFAULT
	releaseall
	end

RogueDungeonFloor_Text_TrainerIntro::
	.string "You there! Let us settle this here!$"

RogueDungeonFloor_Text_BossCleared:
	.string "The way onward has opened!$"

RogueDungeonFloor_Text_TrainerDefeat::
	.string "I could not keep up down here.$"
