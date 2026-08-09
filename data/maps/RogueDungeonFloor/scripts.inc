@ Defines RogueDungeonFloor_EventScript_PickStarter and the option text.
	.include "data/maps/RogueDungeonFloor/starters.inc"

RogueDungeonFloor_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, RogueDungeonFloor_OnFrame
	.byte 0

@ Global, because RogueDungeonUnderwater shares this table rather than defining
@ its own. A debug warp deep into the run with no party has to be offered
@ starters whichever of the two dungeon maps it lands on.
RogueDungeonFloor_OnFrame::
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
	@ A tool rather than a prize. Held items are buried rather than left in
	@ balls, so without this the whole hidden half of the floor is unreachable
	@ and the player has no way to know it is there.
	additem ITEM_DOWSING_MACHINE, 1
	@ Also a tool rather than a prize, and for the same reason. A run grants no
	@ field moves, so the Safari Zone's water surface -- sixteen generated,
	@ check-guarded species -- was reachable only on a Mudkip run. Nothing else
	@ in a run responds to it: the ocean floor is already water, the seafloor is
	@ a property of the map, and the jungle's water is MB_PUDDLE on purpose.
	additem ITEM_ROGUE_SURF_TOOL, 1
	@ The third tool, and the one that makes the fishing minigame reachable at
	@ all: no map in a run grants a rod, so without this the whole feature is
	@ dead code. It works on the two water themes and on the Safari's ponds --
	@ see RogueDungeon_GetWildMonInfo, which folds fishing into the water branch.
	@
	@ DELIBERATELY THE OLD ROD AND DELIBERATELY UNCONDITIONAL. The upgradeable
	@ rod is the next piece of work and will want to own how a rod is obtained
	@ and how it improves; this is one line for it to replace rather than a
	@ progression to unpick.
	additem ITEM_OLD_ROD, 1
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

@ The special rather than a plain `warp MAP_ROGUE_DUNGEON_FLOOR`, because which
@ of the two dungeon maps a floor lives on depends on its theme: an underwater
@ floor has to be entered through a MAP_TYPE_UNDERWATER map or the player
@ arrives walking on the seafloor instead of diving over it.
@
@ It reads the floor counter to pick the map, so the addvar has to stay above it.
RogueDungeonFloor_EventScript_NextFloor::
	addvar VAR_ROGUE_DUNGEON_FLOOR, 1
	special RogueDungeon_WarpToCurrentFloor
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

@ An item ball on a generated floor. Deliberately NOT Common_EventScript_FindItem:
@ that one reads the item out of the ROM template array, which is not the copy
@ the generator writes, so every ball would hand over the same placeholder.
@
@ The shape is otherwise identical to it - set the item and the amount, then let
@ the finditem macro do the message, the fanfare, the pocket and the removeobject
@ - so a generated ball behaves exactly like a hand-placed one.
@
@ The trailing special is what keeps it gone. finditem removes the OBJECT; the
@ TEMPLATE it respawns from is still standing there until this moves it away.
RogueDungeonFloor_EventScript_ItemBall::
	specialvar VAR_RESULT, RogueDungeon_PrepareFloorItem
	finditem VAR_RESULT, VAR_0x8009
	special RogueDungeon_HideTakenFloorItem
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
	call RogueDungeonFloor_EventScript_BossTM
	call RogueDungeonFloor_EventScript_BossAce
	goto RogueDungeonFloor_EventScript_BossExit

@ The stock game's Gym reward. Both specials decline on a mini boss floor, so
@ this whole block is a no-op there.
RogueDungeonFloor_EventScript_BossTM::
	specialvar VAR_RESULT, RogueDungeon_GiveBossTM
	goto_if_eq VAR_RESULT, FALSE, RogueDungeonFloor_EventScript_BossTMDone
	playfanfare MUS_OBTAIN_TMHM
	msgbox RogueDungeonFloor_Text_GotTM, MSGBOX_DEFAULT
	waitfanfare
RogueDungeonFloor_EventScript_BossTMDone::
	return

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

@ A full party losing the ace is deliberate, and there must NOT be a swap prompt
@ here. The rest stop's archivist gives the run real box access before every
@ dungeon - IsDungeonEndFloor above routes there, and the Elite Four are dungeons
@ too - so descending a slot light is a wager the player places at the
@ way-station and pays for across the whole dungeon. Offering the swap here moves
@ the choice to a point where the dungeon is already survived and there is no
@ cost left to weigh, which makes the ace automatic and the wager a formality.
@
@ Naming the ace is what makes it a wager rather than a loss: STR_VAR_1 is
@ already buffered by RogueDungeon_PrepareBossAceOffer, so the player learns
@ exactly which Pokemon they walked past and can price the next one.
RogueDungeonFloor_EventScript_BossAceNoRoom::
	msgbox RogueDungeonFloor_Text_AceNoRoom, MSGBOX_DEFAULT
	return

@ Never behind the reward flag. This is the only way off an arena floor, so it
@ has to work on a re-talk too - otherwise a player who saved here is stuck.
@
@ Run complete is asked FIRST, because Steven's floor is also the last floor of
@ its dungeon and would otherwise warp to a rest stop that has nothing after it.
RogueDungeonFloor_EventScript_BossExit::
	specialvar VAR_RESULT, RogueDungeon_IsRunCompleteFloor
	goto_if_eq VAR_RESULT, TRUE, RogueDungeonFloor_EventScript_RunComplete
	specialvar VAR_RESULT, RogueDungeon_IsDungeonEndFloor
	goto_if_eq VAR_RESULT, TRUE, RogueDungeonFloor_EventScript_ToRestStop
	msgbox RogueDungeonFloor_Text_BossCleared, MSGBOX_DEFAULT
	releaseall
	end

@ The end of the run. Wipes back to floor one needing starters, the same reset a
@ whiteout performs, and warps there - so this is reachable on a re-talk without
@ harm, since the warp leaves the arena either way.
RogueDungeonFloor_EventScript_RunComplete::
	@ Before the reset, which puts the floor counter back to 0 and would stop
	@ this floor being the run-complete one.
	special RogueDungeon_OnRunCompleted
	msgbox RogueDungeonFloor_Text_RunComplete, MSGBOX_DEFAULT
	@ Read back, so the counter is something the player can see rather than a
	@ var that has to be trusted.
	buffernumberstring STR_VAR_1, VAR_ROGUE_RUNS_COMPLETED
	msgbox RogueDungeonFloor_Text_RunsCompleted, MSGBOX_DEFAULT
	special RogueDungeon_ResetRun
	special RogueDungeon_WarpToCurrentFloor
	waitstate
	end

@ Beating the gym leader ends the dungeon. The floor counter advances here, so
@ leaving the rest stop generates the first floor of the next dungeon.
@
@ THE COORDINATES ARE NOT DECORATION. This was a bare `warp MAP_ROGUE_REST_STOP`
@ - the only argument-less warp in the whole tree, against 140 that pass
@ something - and bare is the one form that does not work.
@
@ formatwarp fills the missing x and y with -1, meaning "no coords given, use
@ the map centre". ScrCmd_warp then runs those through VarGet, and
@ VarGet(0xFFFF) is out of bounds: it indexes gSpecialVars[32767] on an array
@ of 22 pointers, reads a pointer 131068 bytes past its end - out of
@ event_scripts.o's own script data - and dereferences it. x and y come back as
@ whatever was there.
@
@ SetWarpDestination then narrows them to s8. Negative, and the intended
@ map-centre fallback still happens and nothing looks wrong; non-negative, and
@ the player is placed at a junk tile outside a 17x13 room and stands in an
@ endless field of border metatile with the music playing. Which one you get is
@ FIXED FOR A GIVEN BUILD, because that address holds ordinary script data - so
@ this hides indefinitely and then appears because an unrelated script grew and
@ moved the bytes underneath it.
@
@ 8,6 is the centre of LAYOUT_ROGUE_REST_STOP and the arrival tile
@ make_rest_stop.py already checks is open floor. Two arguments after the map
@ are read as an x/y pair, and small literals survive VarGet because anything
@ below VARS_START is returned as itself.
RogueDungeonFloor_EventScript_ToRestStop::
	msgbox RogueDungeonFloor_Text_GymCleared, MSGBOX_DEFAULT
	addvar VAR_ROGUE_DUNGEON_FLOOR, 1
	warp MAP_ROGUE_REST_STOP, 8, 6
	waitstate
	end

RogueDungeonFloor_Text_TrainerIntro::
	.string "You there! Let us settle this here!$"

RogueDungeonFloor_Text_BossCleared:
	.string "The way onward has opened!$"

RogueDungeonFloor_Text_GymCleared:
	.string "The Gym Leader is beaten!\p"
	.string "Time to rest before going deeper…$"

RogueDungeonFloor_Text_RunComplete:
	.string "STEVEN is beaten!\p"
	.string "115 floors, and nothing left below.\p"
	.string "The run is complete!\p"
	.string "…and somewhere far above, another\n"
	.string "descent is already beginning.$"

@ Worded to avoid a singular and a plural, since the count is buffered in and
@ the text cannot branch on it.
RogueDungeonFloor_Text_RunsCompleted:
	.string "Descents completed: {STR_VAR_1}$"

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

RogueDungeonFloor_Text_GotTM:
	.string "You received {STR_VAR_1}!$"
