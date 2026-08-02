@ Defines RogueDungeonFloor_EventScript_PickStarter and the option text.
	.include "data/maps/RogueDungeonFloor/starters.inc"

RogueDungeonFloor_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RogueDungeonFloor_OnFrame
	.byte 0

RogueDungeonFloor_OnFrame:
	map_script_2 VAR_ROGUE_RUN_STATE, ROGUE_RUN_NEEDS_STARTERS, RogueDungeonFloor_EventScript_ChooseStarters
	.2byte 0

@ A run begins with two starters rather than a handed-out team. Triggered by
@ the frame table whenever the run state says the party has not been picked -
@ which is true on a new game and again after a whiteout.
RogueDungeonFloor_EventScript_ChooseStarters::
	lockall
	msgbox RogueDungeonFloor_Text_PickFirst, MSGBOX_DEFAULT
	call RogueDungeonFloor_EventScript_PickStarter
	msgbox RogueDungeonFloor_Text_PickSecond, MSGBOX_DEFAULT
	call RogueDungeonFloor_EventScript_PickStarter
	additem ITEM_ULTRA_BALL, ROGUE_RUN_STARTING_BALLS
	setvar VAR_ROGUE_RUN_STATE, ROGUE_RUN_ACTIVE
	releaseall
	end

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
@
@ This runs again every time the player talks to the boss afterwards: the engine
@ answers "you already beat this trainer" by jumping to the post-battle script.
@ So only the exit half may repeat. The reward half is behind a flag, or the ace
@ could be taken once per conversation until the party was full of clones.
RogueDungeonFloor_EventScript_BossDone::
	special RogueDungeon_OnBossDefeated
	goto_if_set FLAG_ROGUE_BOSS_REWARD_TAKEN, RogueDungeonFloor_EventScript_BossExit
	setflag FLAG_ROGUE_BOSS_REWARD_TAKEN
	call RogueDungeonFloor_EventScript_BossAce
	goto RogueDungeonFloor_EventScript_BossExit

@ The special declines on a mini boss floor, so this block is a no-op there.
RogueDungeonFloor_EventScript_BossAce::
	specialvar VAR_RESULT, RogueDungeon_PrepareBossAceOffer
	goto_if_eq VAR_RESULT, ROGUE_ACE_PARTY_FULL, RogueDungeonFloor_EventScript_BossAceNoRoom
	compare VAR_RESULT, ROGUE_ACE_OFFER
	goto_if_ne RogueDungeonFloor_EventScript_BossAceDone
	msgbox RogueDungeonFloor_Text_OfferAce, MSGBOX_YESNO
	compare VAR_RESULT, YES
	goto_if_ne RogueDungeonFloor_EventScript_BossAceDone
	special RogueDungeon_GiveBossAce
	playfanfare MUS_OBTAIN_TMHM
	msgbox RogueDungeonFloor_Text_AceJoined, MSGBOX_DEFAULT
	waitfanfare
RogueDungeonFloor_EventScript_BossAceDone::
	return

@ Say so rather than saying nothing. There is still no swap UI, so a full party
@ loses the ace - but the player should at least learn what they walked past.
RogueDungeonFloor_EventScript_BossAceNoRoom::
	msgbox RogueDungeonFloor_Text_AceNoRoom, MSGBOX_DEFAULT
	return

@ Never behind the reward flag. This is the only way off an arena floor, so it
@ has to work on a re-talk too - otherwise a player who saved here is stuck.
RogueDungeonFloor_EventScript_BossExit::
	specialvar VAR_RESULT, RogueDungeon_IsDungeonEndFloor
	goto_if_eq VAR_RESULT, TRUE, RogueDungeonFloor_EventScript_ToRestStop
	msgbox RogueDungeonFloor_Text_BossCleared, MSGBOX_DEFAULT
	releaseall
	end

@ Beating the gym leader ends the dungeon. The floor counter advances here, so
@ leaving the rest stop generates the first floor of the next dungeon.
RogueDungeonFloor_EventScript_ToRestStop::
	msgbox RogueDungeonFloor_Text_GymCleared, MSGBOX_DEFAULT
	addvar VAR_ROGUE_DUNGEON_FLOOR, 1
	warp MAP_ROGUE_REST_STOP
	waitstate
	end

RogueDungeonFloor_Text_TrainerIntro::
	.string "You there! Let us settle this here!$"

RogueDungeonFloor_Text_BossCleared:
	.string "The way onward has opened!$"

RogueDungeonFloor_Text_GymCleared:
	.string "The Gym Leader is beaten!\p"
	.string "Time to rest before going deeper…$"

RogueDungeonFloor_Text_TrainerDefeat::
	.string "I could not keep up down here.$"

RogueDungeonFloor_Text_PickFirst:
	.string "Choose the partner who will begin\n"
	.string "this descent with you.$"

RogueDungeonFloor_Text_PickSecond:
	.string "And one more to stand beside them.$"

RogueDungeonFloor_Text_OfferAce:
	.string "Their {STR_VAR_1} stayed behind,\n"
	.string "and it is watching you.\p"
	.string "Take it with you?$"

RogueDungeonFloor_Text_AceJoined:
	.string "{STR_VAR_1} joined your team!$"

RogueDungeonFloor_Text_AceNoRoom:
	.string "Their {STR_VAR_1} watched you go.\p"
	.string "There was no room for it.$"
