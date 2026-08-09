@ Game corner, ported from heyopc/pokeemerald-gamecorner-expansion.
@ See docs/GAME_CORNER_PORT.md. Cabinet launch scripts are STUBBED until
@ each minigame lands; restore them from the upstream file one at a time.

@ The room behind the way-station's west alcove.
@
@ It is deliberately incongruous - neon carpet and slot machines behind a wall
@ of cut rock - because the dungeon is the Unown's illusion and this is a piece
@ of Mauville they made out of something they found. Nothing here explains
@ itself, and that is the point.
@
@ Its exit is an ordinary warp, not MAP_DYNAMIC: it always leads back to the
@ way-station's alcove, which is warp 1 there. Warp 0 must stay the descent,
@ because RogueDungeon_SetRestStopExit only ever writes index 0.

	.set REST_COINS_PRICE_50,  1000
	.set REST_COINS_PRICE_500, 10000

	.set REST_PRIZE_CANDY_COST,   300
	.set REST_PRIZE_RESTORE_COST, 200
	.set REST_PRIZE_REVIVE_COST,  500

	@ Every evolution stone costs the same, deliberately. A stone is worth
	@ exactly one thing - the pick that needs it - and pricing them by how
	@ good the resulting Pokemon is would tax a player for their starter
	@ choice. One number, ten stones, no exceptions.
	@
	@ Set above the Max Revive because a stone is permanent and the buried
	@ drop already hands them out for free; this is the counter for a run
	@ that got unlucky, not the cheap way to get one.
	.set REST_PRIZE_STONE_COST, 1000

RogueRestStopGames_EventScript_CoinsClerk::
	lock
	faceplayer
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, TRUE, RogueRestStopGames_EventScript_BuyCoins
	msgbox RogueRestStopGames_Text_TakeThisCase, MSGBOX_DEFAULT
	giveitem ITEM_COIN_CASE
	goto RogueRestStopGames_EventScript_BuyCoins
	end

RogueRestStopGames_EventScript_BuyCoins::
	message RogueRestStopGames_Text_HowManyCoins
	waitmessage
	showmoneybox 0, 0
	showcoinsbox 1, 6
	multichoicedefault 15, 0, MULTI_GAME_CORNER_COINS, 0, FALSE
	switch VAR_RESULT
	case 0, RogueRestStopGames_EventScript_Buy50
	case 1, RogueRestStopGames_EventScript_Buy500
	goto RogueRestStopGames_EventScript_CancelCoins
	end

RogueRestStopGames_EventScript_Buy50::
	checkcoins VAR_TEMP_1
	goto_if_ge VAR_TEMP_1, (MAX_COINS + 1 - 50), RogueRestStopGames_EventScript_CoinCaseFull
	checkmoney REST_COINS_PRICE_50
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NotEnoughMoney
	addcoins 50
	removemoney REST_COINS_PRICE_50
	updatemoneybox
	updatecoinsbox 1, 6
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereAreYourCoins, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_CancelCoins
	end

RogueRestStopGames_EventScript_Buy500::
	checkcoins VAR_TEMP_1
	goto_if_ge VAR_TEMP_1, (MAX_COINS + 1 - 500), RogueRestStopGames_EventScript_CoinCaseFull
	checkmoney REST_COINS_PRICE_500
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NotEnoughMoney
	addcoins 500
	removemoney REST_COINS_PRICE_500
	updatemoneybox
	updatecoinsbox 1, 6
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereAreYourCoins, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_CancelCoins
	end

RogueRestStopGames_EventScript_CoinCaseFull::
	msgbox RogueRestStopGames_Text_CaseIsFull, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_CancelCoins
	end

RogueRestStopGames_EventScript_NotEnoughMoney::
	msgbox RogueRestStopGames_Text_NotEnoughMoney, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_CancelCoins
	end

RogueRestStopGames_EventScript_CancelCoins::
	hidemoneybox
	hidecoinsbox 0, 5
	release
	end

@ ---------------------------------------------------------------- prizes
@
@ dynmultichoice rather than a MULTI_ table, the same command the starter picker
@ uses, so the prize list costs no entry in src/data/script_menu.h and can be
@ reordered here alone.
@
@ A Rare Candy is the prize worth having. Money buys items in the way-station
@ next door; only this room converts a run's winnings into LEVELS, which is the
@ one thing a roguelike run cannot otherwise buy.
RogueRestStopGames_EventScript_PrizeClerk::
	lock
	faceplayer
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCase
	message RogueRestStopGames_Text_PrizeOffer
	waitmessage
	showcoinsbox 1, 6
	dynmultichoice 0, 0, TRUE, 4, 0, DYN_MULTICHOICE_CB_NONE, RogueRestStopGames_Text_PrizeCandy, RogueRestStopGames_Text_PrizeRestore, RogueRestStopGames_Text_PrizeRevive, RogueRestStopGames_Text_PrizeStones, RogueRestStopGames_Text_PrizeNothing
	switch VAR_RESULT
	case 0, RogueRestStopGames_EventScript_PrizeCandy
	case 1, RogueRestStopGames_EventScript_PrizeRestore
	case 2, RogueRestStopGames_EventScript_PrizeRevive
	case 3, RogueRestStopGames_EventScript_PrizeStoneMenu
	goto RogueRestStopGames_EventScript_PrizeDone
	end

@ The ten evolution stones, behind one entry so the top menu stays four items
@ long. The item is DERIVED from the menu index rather than branched on:
@ ITEM_FIRE_STONE + index, which is why the order below is item order and not
@ any prettier arrangement. rogue_dungeon.c STATIC_ASSERTs that the ten are
@ still contiguous in exactly this order, because nothing in this file can
@ check itself and a silent off-by-one here sells the wrong stone.
RogueRestStopGames_EventScript_PrizeStoneMenu::
	message RogueRestStopGames_Text_StoneOffer
	waitmessage
	dynmultichoice 0, 0, TRUE, 4, 0, DYN_MULTICHOICE_CB_NONE, RogueRestStopGames_Text_StoneFire, RogueRestStopGames_Text_StoneWater, RogueRestStopGames_Text_StoneThunder, RogueRestStopGames_Text_StoneLeaf, RogueRestStopGames_Text_StoneIce, RogueRestStopGames_Text_StoneSun, RogueRestStopGames_Text_StoneMoon, RogueRestStopGames_Text_StoneShiny, RogueRestStopGames_Text_StoneDusk, RogueRestStopGames_Text_StoneDawn, RogueRestStopGames_Text_PrizeNothing
	@ 10 is the Nothing row; B returns MULTI_B_PRESSED, which is also >= 10.
	goto_if_ge VAR_RESULT, 10, RogueRestStopGames_EventScript_PrizeDone
	copyvar VAR_TEMP_2, VAR_RESULT
	addvar VAR_TEMP_2, ITEM_FIRE_STONE
	goto RogueRestStopGames_EventScript_BuyStone
	end

RogueRestStopGames_EventScript_BuyStone::
	checkcoins VAR_TEMP_1
	goto_if_lt VAR_TEMP_1, REST_PRIZE_STONE_COST, RogueRestStopGames_EventScript_NotEnoughCoins
	checkitemspace VAR_TEMP_2, 1
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoom
	removecoins REST_PRIZE_STONE_COST
	updatecoinsbox 1, 6
	giveitem VAR_TEMP_2
	goto RogueRestStopGames_EventScript_PrizeGiven
	end

RogueRestStopGames_EventScript_PrizeCandy::
	checkcoins VAR_TEMP_1
	goto_if_lt VAR_TEMP_1, REST_PRIZE_CANDY_COST, RogueRestStopGames_EventScript_NotEnoughCoins
	checkitemspace ITEM_RARE_CANDY
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoom
	removecoins REST_PRIZE_CANDY_COST
	updatecoinsbox 1, 6
	giveitem ITEM_RARE_CANDY
	goto RogueRestStopGames_EventScript_PrizeGiven
	end

RogueRestStopGames_EventScript_PrizeRestore::
	checkcoins VAR_TEMP_1
	goto_if_lt VAR_TEMP_1, REST_PRIZE_RESTORE_COST, RogueRestStopGames_EventScript_NotEnoughCoins
	checkitemspace ITEM_FULL_RESTORE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoom
	removecoins REST_PRIZE_RESTORE_COST
	updatecoinsbox 1, 6
	giveitem ITEM_FULL_RESTORE
	goto RogueRestStopGames_EventScript_PrizeGiven
	end

RogueRestStopGames_EventScript_PrizeRevive::
	checkcoins VAR_TEMP_1
	goto_if_lt VAR_TEMP_1, REST_PRIZE_REVIVE_COST, RogueRestStopGames_EventScript_NotEnoughCoins
	checkitemspace ITEM_MAX_REVIVE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoom
	removecoins REST_PRIZE_REVIVE_COST
	updatecoinsbox 1, 6
	giveitem ITEM_MAX_REVIVE
	goto RogueRestStopGames_EventScript_PrizeGiven
	end

RogueRestStopGames_EventScript_PrizeGiven::
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_PrizeThanks, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_PrizeDone
	end

RogueRestStopGames_EventScript_NotEnoughCoins::
	msgbox RogueRestStopGames_Text_NotEnoughCoins, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_PrizeDone
	end

RogueRestStopGames_EventScript_NoRoom::
	msgbox RogueRestStopGames_Text_NoRoom, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_PrizeDone
	end

RogueRestStopGames_EventScript_NoCase::
	msgbox RogueRestStopGames_Text_NoCase, MSGBOX_DEFAULT
	release
	end

RogueRestStopGames_EventScript_PrizeDone::
	hidecoinsbox 0, 5
	release
	end

@ ---------------------------------------------------------------- the machines
@
@ VAR_0x8004 is the machine index; GetSlotMachineId turns it into the actual
@ machine, which is how vanilla gives each cabinet its own luck. Eight of them,
@ four played from each stool.
RogueRestStopGames_EventScript_PlaySlots::
	specialvar VAR_RESULT, GetSlotMachineId
	playslotmachine VAR_RESULT
	releaseall
	end

RogueRestStopGames_EventScript_NoCaseAll::
	msgbox RogueRestStopGames_Text_NoCase, MSGBOX_DEFAULT
	releaseall
	end

@ ---------------------------------------------------------------- text

RogueRestStopGames_Text_TakeThisCase:
	.string "You have no case for coins?\p"
	.string "Take this one. It was here when\n"
	.string "I was.$"

RogueRestStopGames_Text_HowManyCoins:
	.string "Coins, then. How many?$"

RogueRestStopGames_Text_HereAreYourCoins:
	.string "Here you are. Good luck.$"

RogueRestStopGames_Text_CaseIsFull:
	.string "Your case will not hold another.$"

RogueRestStopGames_Text_NotEnoughMoney:
	.string "You cannot afford that.$"

RogueRestStopGames_Text_NoCase:
	.string "You would need a case to hold\n"
	.string "any coins you won.$"

RogueRestStopGames_Text_PrizeOffer:
	.string "Coins are no use below.\p"
	.string "Trade them for something that is?$"

RogueRestStopGames_Text_PrizeCandy:
	.string "Rare Candy - 300$"

RogueRestStopGames_Text_PrizeRestore:
	.string "Full Restore - 200$"

RogueRestStopGames_Text_PrizeRevive:
	.string "Max Revive - 500$"

RogueRestStopGames_Text_PrizeStones:
	.string "Evolution stones$"

RogueRestStopGames_Text_StoneOffer:
	.string "Every stone, same price.\p"
	.string "Which one is yours?$"

@ Order matters - see RogueRestStopGames_EventScript_PrizeStoneMenu.
RogueRestStopGames_Text_StoneFire:
	.string "Fire Stone - 1000$"

RogueRestStopGames_Text_StoneWater:
	.string "Water Stone - 1000$"

RogueRestStopGames_Text_StoneThunder:
	.string "Thunder Stone - 1000$"

RogueRestStopGames_Text_StoneLeaf:
	.string "Leaf Stone - 1000$"

RogueRestStopGames_Text_StoneIce:
	.string "Ice Stone - 1000$"

RogueRestStopGames_Text_StoneSun:
	.string "Sun Stone - 1000$"

RogueRestStopGames_Text_StoneMoon:
	.string "Moon Stone - 1000$"

RogueRestStopGames_Text_StoneShiny:
	.string "Shiny Stone - 1000$"

RogueRestStopGames_Text_StoneDusk:
	.string "Dusk Stone - 1000$"

RogueRestStopGames_Text_StoneDawn:
	.string "Dawn Stone - 1000$"

RogueRestStopGames_Text_PrizeNothing:
	.string "Nothing$"

RogueRestStopGames_Text_PrizeThanks:
	.string "Take it down with you.$"

RogueRestStopGames_Text_NotEnoughCoins:
	.string "Not enough coins for that.$"

RogueRestStopGames_Text_NoRoom:
	.string "You have no room to carry it.$"

.set LOCALID_BOUNCER, 24

RogueRestStopGames_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, TrainerId_Onframe
	.byte 0

    @ Game Corner prices
	.set TM_DOUBLE_TEAM_COINS, 1500
	.set TM_PSYCHIC_COINS, 3500
	.set TM_FLAMETHROWER_COINS, 4000
	.set TM_THUNDERBOLT_COINS, 4000
	.set TM_ICE_BEAM_COINS, 4000
	.set DOLL_COINS, 1000
	.set RARECANDY_COINS, 4000
	.set SUNSTONE_COINS, 2000
	.set MOONSTONE_COINS, 2000
	.set FIRESTONE_COINS, 2000
	.set THUNDERSTONE_COINS, 2000
	.set WATERSTONE_COINS, 2000
	.set LEAFSTONE_COINS, 2000
	.set CHOICEBAND_COINS, 4500
	.set BRIGHTPOWDER_COINS, 4500

	.set COINS_PRICE_50,  1000
	.set COINS_PRICE_500, 10000

TrainerId_Onframe:
	map_script_2 VAR_GC_TRAINER_ID_CHECK, 0, BouncerCheck
	.2byte 0
	
BouncerCheck::
	lockall
	applymovement LOCALID_BOUNCER, Common_Movement_WalkInPlaceFasterLeft
	waitmovement 0
	playse SE_PIN
	applymovement LOCALID_BOUNCER, Common_Movement_ExclamationMark
	waitmovement 0
	applymovement LOCALID_BOUNCER, Common_Movement_Delay48
	waitmovement 0
	applymovement LOCALID_BOUNCER, Movement_Bouncer_Approach
	waitmovement 0
	msgbox RogueRestStopGames_Text_Bouncer1, MSGBOX_DEFAULT
	closemessage
	setvar VAR_GC_TRAINER_ID_CHECK, 1
	applymovement LOCALID_BOUNCER, Movement_Bouncer_Leave
	waitmovement 0
	releaseall
	end	
	
Movement_Bouncer_Approach:
	walk_left
	walk_left
	step_end	
	
Movement_Bouncer_Leave:
	walk_right
	walk_right
	walk_in_place_faster_left
	step_end		

RogueRestStopGames_EventScript_Blackjack::
	lockall
	msgbox RogueRestStopGames_Text_CabinetOffline, MSGBOX_DEFAULT
	releaseall
	end

RogueRestStopGames_EventScript_Derby::
	lockall
	msgbox RogueRestStopGames_Text_CabinetOffline, MSGBOX_DEFAULT
	releaseall
	end

RogueRestStopGames_EventScript_Snake::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, 25, Onix_Not_Enough
	showcoinsbox 1, 1
	msgbox RogueRestStopGames_Text_Snake, MSGBOX_YESNO
	closemessage
	goto_if_eq VAR_RESULT, NO, RogueRestStopGames_CancelVF
	removecoins 25
	updatecoinsbox 1, 1
	playse SE_SHOP
	delay 40
	hidecoinsbox 0, 5
	playbgm MUS_NONE, FALSE
	special StartSnake
	waitstate
	savebgm MUS_DUMMY
	fadedefaultbgm
	goto_if_eq VAR_GC_FLIP_WINNINGS, 0, RogueRestStopGames_NoWin
	showcoinsbox 1, 1
	buffernumberstring STR_VAR_1, VAR_GC_FLIP_WINNINGS
	msgbox PB_YOU_WON_TEXT, MSGBOX_DEFAULT
	waitmessage
	addcoins VAR_GC_FLIP_WINNINGS
	setvar VAR_GC_FLIP_WINNINGS, 0
	updatecoinsbox 1, 1
	playse SE_SHOP
	delay 40
	hidecoinsbox 0, 5
	closemessage
	releaseall
	end

RogueRestStopGames_EventScript_FlappyBird::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, 25, Butterfree_Not_Enough
	showcoinsbox 1, 1
	msgbox RogueRestStopGames_Text_Bird, MSGBOX_YESNO
	closemessage
	goto_if_eq VAR_RESULT, NO, RogueRestStopGames_CancelVF
	removecoins 25
	updatecoinsbox 1, 1
	playse SE_SHOP
	delay 40
	hidecoinsbox 0, 5
	playbgm MUS_NONE, FALSE
	special StartFlappyBird
	waitstate
	savebgm MUS_DUMMY
	fadedefaultbgm
	goto_if_eq VAR_GC_FLIP_WINNINGS, 0, RogueRestStopGames_NoWin
	showcoinsbox 1, 1
	buffernumberstring STR_VAR_1, VAR_GC_FLIP_WINNINGS
	msgbox PB_YOU_WON_TEXT, MSGBOX_DEFAULT
	waitmessage
	addcoins VAR_GC_FLIP_WINNINGS
	setvar VAR_GC_FLIP_WINNINGS, 0
	updatecoinsbox 1, 1
	playse SE_SHOP
	delay 40
	hidecoinsbox 0, 5
	closemessage
	releaseall
	end

RogueRestStopGames_EventScript_BlockStacker::
	lockall
	msgbox RogueRestStopGames_Text_CabinetOffline, MSGBOX_DEFAULT
	releaseall
	end

BlockStacker_Not_Enough::
	msgbox BS_Not_Enough, MSGBOX_DEFAULT
	releaseall
	end	
	
Pinball_Not_Enough::
	msgbox PB_Not_Enough, MSGBOX_DEFAULT
	releaseall
	end	
	
Onix_Not_Enough::
	msgbox Snake_Not_Enough, MSGBOX_DEFAULT
	releaseall
	end		
	
Juice_Not_Enough::
	msgbox Bjuice_Not_Enough, MSGBOX_DEFAULT
	releaseall
	end			
	
Butterfree_Not_Enough::
	msgbox Bird_Not_Enough, MSGBOX_DEFAULT
	releaseall
	end			
	
VF_Not_Enough::
	msgbox Flip_Not_Enough, MSGBOX_DEFAULT
	releaseall
	end		
	
RogueRestStopGames_EventScript_Pachinko::
	lockall
	msgbox RogueRestStopGames_Text_CabinetOffline, MSGBOX_DEFAULT
	releaseall
	end

RogueRestStopGames_EventScript_PinballMeowth::
	lockall
	msgbox RogueRestStopGames_Text_CabinetOffline, MSGBOX_DEFAULT
	releaseall
	end

RogueRestStopGames_EventScript_PinballDiglett::
	lockall
	msgbox RogueRestStopGames_Text_CabinetOffline, MSGBOX_DEFAULT
	releaseall
	end

RogueRestStopGames_EventScript_PinballSeel::
	lockall
	msgbox RogueRestStopGames_Text_CabinetOffline, MSGBOX_DEFAULT
	releaseall
	end

RogueRestStopGames_EventScript_PinballGengar::
	lockall
	msgbox RogueRestStopGames_Text_CabinetOffline, MSGBOX_DEFAULT
	releaseall
	end

RogueRestStopGames_EventScript_VoltorbFlip::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, 50, VF_Not_Enough
	showcoinsbox 1, 1
	msgbox RogueRestStopGames_Text_VF, MSGBOX_YESNO
	closemessage
	goto_if_eq VAR_RESULT, NO, RogueRestStopGames_CancelVF
	removecoins 50
	updatecoinsbox 1, 1
	playse SE_SHOP
	delay 40
	hidecoinsbox 0, 5
	playbgm MUS_RG_OAK, FALSE
	setvar VAR_GC_FLIP_WINNINGS, 0
	special Special_ViewVoltorbFlip
	waitstate
	goto_if_eq VAR_RESULT, 0, VF_Come_Again
	goto_if_eq VAR_RESULT, 1, VF_Go_On
	releaseall
	end

VF_Come_Again::
	savebgm MUS_DUMMY
	fadedefaultbgm
	msgbox VF_Come_Again_Text, MSGBOX_DEFAULT
	setvar VAR_GC_FLIP_WINNINGS, 0
	releaseall
	end
	
VF_Go_On::
	showcoinsbox 1, 1
	buffernumberstring STR_VAR_1, VAR_GC_FLIP_WINNINGS	
	msgbox RogueRestStopGames_Text_VF_Keep_Going, MSGBOX_YESNO
	closemessage
	goto_if_eq VAR_RESULT, NO, VF_Cash_Out
	goto VF_Loop
	
VF_Loop::
	hidecoinsbox 0, 5
	special Special_ViewVoltorbFlip
	waitstate
	goto_if_eq VAR_RESULT, 0, VF_Come_Again
	goto_if_eq VAR_RESULT, 1, VF_Go_On
	releaseall
	end

VF_Cash_Out::	
	addcoins VAR_GC_FLIP_WINNINGS
	setvar VAR_GC_FLIP_WINNINGS, 0
	setvar VAR_GC_FLIP_LEVEL, 0
	updatecoinsbox 1, 1
	playse SE_SHOP
	savebgm MUS_DUMMY
	fadedefaultbgm
	delay 40
	hidecoinsbox 0, 5
	releaseall
	end
	
RogueRestStopGames_EventScript_Gacha_Basic::
	lockall
	msgbox RogueRestStopGames_Text_CabinetOffline, MSGBOX_DEFAULT
	releaseall
	end

RogueRestStopGames_EventScript_Gacha_Great::
	lockall
	msgbox RogueRestStopGames_Text_CabinetOffline, MSGBOX_DEFAULT
	releaseall
	end

RogueRestStopGames_EventScript_Gacha_Ultra::
	lockall
	msgbox RogueRestStopGames_Text_CabinetOffline, MSGBOX_DEFAULT
	releaseall
	end

RogueRestStopGames_EventScript_Gacha_Master::
	lockall
	msgbox RogueRestStopGames_Text_CabinetOffline, MSGBOX_DEFAULT
	releaseall
	end

RogueRestStopGames_CancelBJ::
	closemessage
	releaseall
	end
	
RogueRestStopGames_NoWin::
	releaseall
	end	
	
RogueRestStopGames_CancelVF::
	hidecoinsbox 0, 5
	closemessage
	releaseall
	end

RogueRestStopGames_EventScript_ChooseCoinsDefault50::
	multichoicedefault 15, 0, MULTI_GAME_CORNER_COINS, 0, FALSE
	switch VAR_RESULT
	case 0, RogueRestStopGames_EventScript_Buy50Coins
	case 1, RogueRestStopGames_EventScript_Buy500Coins
	goto RogueRestStopGames_EventScript_CancelBuyCoins
	end

@ Unused
RogueRestStopGames_EventScript_ChooseCoinsDefault500::
	multichoicedefault 15, 0, MULTI_GAME_CORNER_COINS, 1, FALSE
	switch VAR_RESULT
	case 0, RogueRestStopGames_EventScript_Buy50Coins
	case 1, RogueRestStopGames_EventScript_Buy500Coins
	goto RogueRestStopGames_EventScript_CancelBuyCoins
	end

RogueRestStopGames_EventScript_Buy50Coins::
	checkcoins VAR_TEMP_1
	goto_if_ge VAR_TEMP_1, (MAX_COINS + 1 - 50), RogueRestStopGames_EventScript_NoRoomForCoins
	checkmoney COINS_PRICE_50
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NotEnoughMoney
	addcoins 50
	removemoney COINS_PRICE_50
	updatemoneybox
	updatecoinsbox 1, 6
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_ThankYouHereAreYourCoins, MSGBOX_DEFAULT
	hidemoneybox
	hidecoinsbox 0, 5
	release
	end

RogueRestStopGames_EventScript_Buy500Coins::
	checkcoins VAR_TEMP_1
	goto_if_ge VAR_TEMP_1, (MAX_COINS + 1 - 500), RogueRestStopGames_EventScript_NoRoomForCoins
	checkmoney COINS_PRICE_500
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NotEnoughMoney
	addcoins 500
	removemoney COINS_PRICE_500
	updatemoneybox
	updatecoinsbox 1, 6
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_ThankYouHereAreYourCoins, MSGBOX_DEFAULT
	hidemoneybox
	hidecoinsbox 0, 5
	release
	end

RogueRestStopGames_EventScript_NeedCoinCase::
	msgbox RogueRestStopGames_Text_NeedCoinCaseForCoins, MSGBOX_DEFAULT
	release
	end

RogueRestStopGames_EventScript_CancelBuyCoins::
	msgbox RogueRestStopGames_Text_DontNeedCoinsThen, MSGBOX_DEFAULT
	hidemoneybox
	hidecoinsbox 0, 5
	release
	end

RogueRestStopGames_EventScript_NoRoomForCoins::
	msgbox RogueRestStopGames_Text_CoinCaseIsFull, MSGBOX_DEFAULT
	hidemoneybox
	hidecoinsbox 0, 5
	release
	end

RogueRestStopGames_EventScript_PrizeCornerDolls::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_ExchangeCoinsForPrizes, MSGBOX_DEFAULT
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, TRUE, RogueRestStopGames_EventScript_ChooseDollPrizeMessage
	release
	end

RogueRestStopGames_EventScript_ChooseDollPrizeMessage::
	message RogueRestStopGames_Text_WhichPrize
	waitmessage
	setvar VAR_TEMP_1, 0
	showcoinsbox 1, 1
	goto RogueRestStopGames_EventScript_ChooseDollPrize

RogueRestStopGames_EventScript_ReturnToChooseDollPrize::
	message RogueRestStopGames_Text_WhichPrize
	waitmessage
	goto RogueRestStopGames_EventScript_ChooseDollPrize

RogueRestStopGames_EventScript_ChooseDollPrize::
	multichoice 12, 0, MULTI_GAME_CORNER_DOLLS, FALSE
	switch VAR_RESULT
	case 0, RogueRestStopGames_EventScript_TreeckoDoll
	case 1, RogueRestStopGames_EventScript_TorchicDoll
	case 2, RogueRestStopGames_EventScript_MudkipDoll
	case 3, RogueRestStopGames_EventScript_CancelDollSelect
	goto RogueRestStopGames_EventScript_CancelDollSelect
	end

RogueRestStopGames_EventScript_TreeckoDoll::
	setvar VAR_TEMP_1, 1
	bufferdecorationname STR_VAR_1, DECOR_TREECKO_DOLL
	goto RogueRestStopGames_EventScript_ConfirmDollPrize

RogueRestStopGames_EventScript_TorchicDoll::
	setvar VAR_TEMP_1, 2
	bufferdecorationname STR_VAR_1, DECOR_TORCHIC_DOLL
	goto RogueRestStopGames_EventScript_ConfirmDollPrize

RogueRestStopGames_EventScript_MudkipDoll::
	setvar VAR_TEMP_1, 3
	bufferdecorationname STR_VAR_1, DECOR_MUDKIP_DOLL
	goto RogueRestStopGames_EventScript_ConfirmDollPrize

RogueRestStopGames_EventScript_ConfirmDollPrize::
	msgbox RogueRestStopGames_Text_SoYourChoiceIsX, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, RogueRestStopGames_EventScript_CancelDollSelect
	switch VAR_TEMP_1
	case 1, RogueRestStopGames_EventScript_BuyTreeckoDoll
	case 2, RogueRestStopGames_EventScript_BuyTorchicDoll
	case 3, RogueRestStopGames_EventScript_BuyMudkipDoll
	end

RogueRestStopGames_EventScript_BuyTreeckoDoll::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, DOLL_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForDoll
	bufferdecorationname STR_VAR_2, DECOR_TREECKO_DOLL
	checkdecorspace DECOR_TREECKO_DOLL
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForDoll
	removecoins DOLL_COINS
	adddecoration DECOR_TREECKO_DOLL
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_SendToYourHomePC, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseDollPrize
	end

RogueRestStopGames_EventScript_BuyTorchicDoll::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, DOLL_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForDoll
	bufferdecorationname STR_VAR_2, DECOR_TORCHIC_DOLL
	checkdecorspace DECOR_TORCHIC_DOLL
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForDoll
	removecoins DOLL_COINS
	adddecoration DECOR_TORCHIC_DOLL
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_SendToYourHomePC, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseDollPrize
	end

RogueRestStopGames_EventScript_BuyMudkipDoll::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, DOLL_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForDoll
	bufferdecorationname STR_VAR_2, DECOR_MUDKIP_DOLL
	checkdecorspace DECOR_MUDKIP_DOLL
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForDoll
	removecoins DOLL_COINS
	adddecoration DECOR_MUDKIP_DOLL
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_SendToYourHomePC, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseDollPrize
	end

RogueRestStopGames_EventScript_NotEnoughCoinsForDoll::
	msgbox RogueRestStopGames_Text_NotEnoughCoins, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseDollPrize
	end

RogueRestStopGames_EventScript_NoRoomForDoll::
	call Common_EventScript_NoRoomForDecor
	goto RogueRestStopGames_EventScript_ReturnToChooseDollPrize
	end

RogueRestStopGames_EventScript_CancelDollSelect::
	msgbox RogueRestStopGames_Text_OhIsThatSo, MSGBOX_DEFAULT
	hidecoinsbox 0, 0
	release
	end

RogueRestStopGames_EventScript_PrizeCornerTMs::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_ExchangeCoinsForPrizes, MSGBOX_DEFAULT
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, TRUE, RogueRestStopGames_EventScript_ChooseTMPrizeMessage
	release
	end
	
RogueRestStopGames_EventScript_PrizeCornerNew::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_ExchangeCoinsForPrizes, MSGBOX_DEFAULT
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, TRUE, RogueRestStopGames_EventScript_ChooseNewPrizeMessage
	release
	end	
	
RogueRestStopGames_EventScript_PrizeCornerNew2::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_ExchangeCoinsForPrizes, MSGBOX_DEFAULT
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, TRUE, RogueRestStopGames_EventScript_ChooseNewPrizeMessage2
	release
	end		

RogueRestStopGames_EventScript_ChooseTMPrizeMessage::
	message RogueRestStopGames_Text_WhichPrize
	waitmessage
	setvar VAR_TEMP_1, 0
	showcoinsbox 1, 1
	goto RogueRestStopGames_EventScript_ChooseTMPrize
	
RogueRestStopGames_EventScript_ChooseNewPrizeMessage::
	message RogueRestStopGames_Text_WhichPrize
	waitmessage
	setvar VAR_TEMP_1, 0
	showcoinsbox 1, 1
	goto RogueRestStopGames_EventScript_ChooseNewPrize	
	
RogueRestStopGames_EventScript_ChooseNewPrizeMessage2::
	message RogueRestStopGames_Text_WhichPrize
	waitmessage
	setvar VAR_TEMP_1, 0
	showcoinsbox 1, 1
	goto RogueRestStopGames_EventScript_ChooseNewPrize2		

RogueRestStopGames_EventScript_ReturnToChooseTMPrize::
	message RogueRestStopGames_Text_WhichPrize
	waitmessage
	goto RogueRestStopGames_EventScript_ChooseTMPrize
	
RogueRestStopGames_EventScript_ReturnToChooseNewPrize::
	message RogueRestStopGames_Text_WhichPrize
	waitmessage
	goto RogueRestStopGames_EventScript_ChooseNewPrize	
	
RogueRestStopGames_EventScript_ReturnToChooseNewPrize2::
	message RogueRestStopGames_Text_WhichPrize
	waitmessage
	goto RogueRestStopGames_EventScript_ChooseNewPrize2	

RogueRestStopGames_EventScript_ChooseTMPrize::
	multichoice 12, 0, MULTI_GAME_CORNER_TMS, FALSE
	switch VAR_RESULT
	case 0, RogueRestStopGames_EventScript_TMDoubleTeam
	case 1, RogueRestStopGames_EventScript_TMPsychic
	case 2, RogueRestStopGames_EventScript_TMFlamethrower
	case 3, RogueRestStopGames_EventScript_TMThunderbolt
	case 4, RogueRestStopGames_EventScript_TMIceBeam
	case 5, RogueRestStopGames_EventScript_CancelTMSelect
	goto RogueRestStopGames_EventScript_CancelTMSelect
	end
	
RogueRestStopGames_EventScript_ChooseNewPrize::
	multichoice 12, 0, MULTI_GAMECORNER_NEW, FALSE
	switch VAR_RESULT
	case 0, RogueRestStopGames_EventScript_RareCandy
	case 1, RogueRestStopGames_EventScript_SunStone
	case 2, RogueRestStopGames_EventScript_MoonStone
	case 3, RogueRestStopGames_EventScript_ChoiceBand
	case 4, RogueRestStopGames_EventScript_BrightPowder
	case 7, RogueRestStopGames_EventScript_CancelTMSelect
	goto RogueRestStopGames_EventScript_CancelTMSelect
	end	

RogueRestStopGames_EventScript_ChooseNewPrize2::
	multichoice 12, 0, MULTI_GAMECORNER_NEW2, FALSE
	switch VAR_RESULT
	case 0, RogueRestStopGames_EventScript_FireStone
	case 1, RogueRestStopGames_EventScript_ThunderStone
	case 2, RogueRestStopGames_EventScript_WaterStone
	case 3, RogueRestStopGames_EventScript_LeafStone
	case 4, RogueRestStopGames_EventScript_CancelTMSelect
	goto RogueRestStopGames_EventScript_CancelTMSelect
	end		

RogueRestStopGames_EventScript_ChoiceBand::
	setvar VAR_TEMP_1, 8
	bufferitemname STR_VAR_1, ITEM_CHOICE_BAND
	setvar VAR_0x8004, ITEM_CHOICE_BAND
	goto RogueRestStopGames_EventScript_ConfirmNewPrize
	
RogueRestStopGames_EventScript_BrightPowder::
	setvar VAR_TEMP_1, 9
	bufferitemname STR_VAR_1, ITEM_BRIGHT_POWDER
	setvar VAR_0x8004, ITEM_BRIGHT_POWDER
	goto RogueRestStopGames_EventScript_ConfirmNewPrize	
	
RogueRestStopGames_EventScript_RareCandy::
	setvar VAR_TEMP_1, 1
	bufferitemname STR_VAR_1, ITEM_RARE_CANDY
	setvar VAR_0x8004, ITEM_RARE_CANDY
	goto RogueRestStopGames_EventScript_ConfirmNewPrize
	
RogueRestStopGames_EventScript_SunStone::
	setvar VAR_TEMP_1, 2
	bufferitemname STR_VAR_1, ITEM_SUN_STONE
	setvar VAR_0x8004, ITEM_SUN_STONE
	goto RogueRestStopGames_EventScript_ConfirmNewPrize

RogueRestStopGames_EventScript_MoonStone::
	setvar VAR_TEMP_1, 3
	bufferitemname STR_VAR_1, ITEM_MOON_STONE
	setvar VAR_0x8004, ITEM_MOON_STONE
	goto RogueRestStopGames_EventScript_ConfirmNewPrize

RogueRestStopGames_EventScript_ThunderStone::
	setvar VAR_TEMP_1, 5
	bufferitemname STR_VAR_1, ITEM_THUNDER_STONE
	setvar VAR_0x8004, ITEM_THUNDER_STONE
	goto RogueRestStopGames_EventScript_ConfirmNewPrize

RogueRestStopGames_EventScript_WaterStone::
	setvar VAR_TEMP_1, 6
	bufferitemname STR_VAR_1, ITEM_WATER_STONE
	setvar VAR_0x8004, ITEM_WATER_STONE
	goto RogueRestStopGames_EventScript_ConfirmNewPrize

RogueRestStopGames_EventScript_LeafStone::
	setvar VAR_TEMP_1, 7
	bufferitemname STR_VAR_1, ITEM_LEAF_STONE
	setvar VAR_0x8004, ITEM_LEAF_STONE
	goto RogueRestStopGames_EventScript_ConfirmNewPrize

RogueRestStopGames_EventScript_FireStone::
	setvar VAR_TEMP_1, 4
	bufferitemname STR_VAR_1, ITEM_FIRE_STONE
	setvar VAR_0x8004, ITEM_FIRE_STONE
	goto RogueRestStopGames_EventScript_ConfirmNewPrize	

RogueRestStopGames_EventScript_ConfirmNewPrize::
	//special BufferItemName
	msgbox RogueRestStopGames_Text_SoYourChoiceIsTheNewX, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, RogueRestStopGames_EventScript_CancelTMSelect
	switch VAR_TEMP_1
	case 1, RogueRestStopGames_EventScript_BuyRareCandy
	case 2, RogueRestStopGames_EventScript_BuySunStone
	case 3, RogueRestStopGames_EventScript_BuyMoonStone
	case 4, RogueRestStopGames_EventScript_BuyFireStone
	case 5, RogueRestStopGames_EventScript_BuyThunderStone
	case 6, RogueRestStopGames_EventScript_BuyWaterStone
	case 7, RogueRestStopGames_EventScript_BuyLeafStone
	case 8, RogueRestStopGames_EventScript_BuyChoiceBand
	case 9, RogueRestStopGames_EventScript_BuyBrightPowder
	
	end

RogueRestStopGames_EventScript_BuyBrightPowder::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, BRIGHTPOWDER_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForNew
	checkitemspace ITEM_BRIGHT_POWDER
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForNew
	removecoins BRIGHTPOWDER_COINS
	additem ITEM_BRIGHT_POWDER
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereYouGo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseNewPrize
	end

RogueRestStopGames_EventScript_BuyChoiceBand::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, CHOICEBAND_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForNew
	checkitemspace ITEM_CHOICE_BAND
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForNew
	removecoins CHOICEBAND_COINS
	additem ITEM_CHOICE_BAND
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereYouGo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseNewPrize
	end

RogueRestStopGames_EventScript_BuyRareCandy::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, RARECANDY_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForNew
	checkitemspace ITEM_RARE_CANDY
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForNew
	removecoins RARECANDY_COINS
	additem ITEM_RARE_CANDY
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereYouGo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseNewPrize
	end
	
RogueRestStopGames_EventScript_BuySunStone::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, SUNSTONE_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForNew
	checkitemspace ITEM_SUN_STONE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForNew
	removecoins SUNSTONE_COINS
	additem ITEM_SUN_STONE
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereYouGo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseNewPrize
	end

RogueRestStopGames_EventScript_BuyMoonStone::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, MOONSTONE_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForNew
	checkitemspace ITEM_MOON_STONE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForNew
	removecoins MOONSTONE_COINS
	additem ITEM_MOON_STONE
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereYouGo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseNewPrize
	end

RogueRestStopGames_EventScript_BuyFireStone::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, FIRESTONE_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForNew2
	checkitemspace ITEM_FIRE_STONE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForNew2
	removecoins FIRESTONE_COINS
	additem ITEM_FIRE_STONE
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereYouGo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseNewPrize2
	end

RogueRestStopGames_EventScript_BuyThunderStone::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, THUNDERSTONE_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForNew2
	checkitemspace ITEM_THUNDER_STONE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForNew2
	removecoins THUNDERSTONE_COINS
	additem ITEM_THUNDER_STONE
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereYouGo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseNewPrize2
	end

RogueRestStopGames_EventScript_BuyWaterStone::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, WATERSTONE_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForNew2
	checkitemspace ITEM_WATER_STONE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForNew2
	removecoins WATERSTONE_COINS
	additem ITEM_WATER_STONE
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereYouGo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseNewPrize2
	end

RogueRestStopGames_EventScript_BuyLeafStone::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, LEAFSTONE_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForNew2
	checkitemspace ITEM_LEAF_STONE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForNew2
	removecoins LEAFSTONE_COINS
	additem ITEM_LEAF_STONE
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereYouGo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseNewPrize2
	end	

RogueRestStopGames_EventScript_TMDoubleTeam::
	setvar VAR_TEMP_1, 1
	bufferitemname STR_VAR_1, ITEM_TM_DOUBLE_TEAM
	setvar VAR_0x8004, ITEM_TM_DOUBLE_TEAM
	goto RogueRestStopGames_EventScript_ConfirmTMPrize

RogueRestStopGames_EventScript_TMPsychic::
	setvar VAR_TEMP_1, 2
	bufferitemname STR_VAR_1, ITEM_TM_PSYCHIC
	setvar VAR_0x8004, ITEM_TM_PSYCHIC
	goto RogueRestStopGames_EventScript_ConfirmTMPrize

RogueRestStopGames_EventScript_TMFlamethrower::
	setvar VAR_TEMP_1, 3
	bufferitemname STR_VAR_1, ITEM_TM_FLAMETHROWER
	setvar VAR_0x8004, ITEM_TM_FLAMETHROWER
	goto RogueRestStopGames_EventScript_ConfirmTMPrize

RogueRestStopGames_EventScript_TMThunderbolt::
	setvar VAR_TEMP_1, 4
	bufferitemname STR_VAR_1, ITEM_TM_THUNDERBOLT
	setvar VAR_0x8004, ITEM_TM_THUNDERBOLT
	goto RogueRestStopGames_EventScript_ConfirmTMPrize

RogueRestStopGames_EventScript_TMIceBeam::
	setvar VAR_TEMP_1, 5
	bufferitemname STR_VAR_1, ITEM_TM_ICE_BEAM
	setvar VAR_0x8004, ITEM_TM_ICE_BEAM
	goto RogueRestStopGames_EventScript_ConfirmTMPrize

RogueRestStopGames_EventScript_ConfirmTMPrize::
	special BufferTMHMMoveName
	msgbox RogueRestStopGames_Text_SoYourChoiceIsTheTMX, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, RogueRestStopGames_EventScript_CancelTMSelect
	switch VAR_TEMP_1
	case 1, RogueRestStopGames_EventScript_BuyTMDoubleTeam
	case 2, RogueRestStopGames_EventScript_BuyTMPsychic
	case 3, RogueRestStopGames_EventScript_BuyTMFlamethrower
	case 4, RogueRestStopGames_EventScript_BuyTMThunderbolt
	case 5, RogueRestStopGames_EventScript_BuyTMIceBeam
	end

RogueRestStopGames_EventScript_BuyTMDoubleTeam::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, TM_DOUBLE_TEAM_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForTM
	checkitemspace ITEM_TM_DOUBLE_TEAM
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForTM
	removecoins TM_DOUBLE_TEAM_COINS
	additem ITEM_TM_DOUBLE_TEAM
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereYouGo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseTMPrize
	end

RogueRestStopGames_EventScript_BuyTMPsychic::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, TM_PSYCHIC_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForTM
	checkitemspace ITEM_TM_PSYCHIC
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForTM
	removecoins TM_PSYCHIC_COINS
	additem ITEM_TM_PSYCHIC
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereYouGo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseTMPrize
	end

RogueRestStopGames_EventScript_BuyTMFlamethrower::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, TM_FLAMETHROWER_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForTM
	checkitemspace ITEM_TM_FLAMETHROWER
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForTM
	removecoins TM_FLAMETHROWER_COINS
	additem ITEM_TM_FLAMETHROWER
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereYouGo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseTMPrize
	end

RogueRestStopGames_EventScript_BuyTMThunderbolt::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, TM_THUNDERBOLT_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForTM
	checkitemspace ITEM_TM_THUNDERBOLT
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForTM
	removecoins TM_THUNDERBOLT_COINS
	additem ITEM_TM_THUNDERBOLT
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereYouGo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseTMPrize
	end

RogueRestStopGames_EventScript_BuyTMIceBeam::
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, TM_ICE_BEAM_COINS, RogueRestStopGames_EventScript_NotEnoughCoinsForTM
	checkitemspace ITEM_TM_ICE_BEAM
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForTM
	removecoins TM_ICE_BEAM_COINS
	additem ITEM_TM_ICE_BEAM
	updatecoinsbox 1, 1
	playse SE_SHOP
	msgbox RogueRestStopGames_Text_HereYouGo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseTMPrize
	end

RogueRestStopGames_EventScript_NotEnoughCoinsForTM::
	msgbox RogueRestStopGames_Text_NotEnoughCoins, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseTMPrize
	end
	
RogueRestStopGames_EventScript_NotEnoughCoinsForNew::
	msgbox RogueRestStopGames_Text_NotEnoughCoins, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseNewPrize
	end	
	
RogueRestStopGames_EventScript_NotEnoughCoinsForNew2::
	msgbox RogueRestStopGames_Text_NotEnoughCoins, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_ReturnToChooseNewPrize2
	end		

RogueRestStopGames_EventScript_NoRoomForTM::
	call Common_EventScript_BagIsFull
	goto RogueRestStopGames_EventScript_ReturnToChooseTMPrize
	end
	
RogueRestStopGames_EventScript_NoRoomForNew::
	call Common_EventScript_BagIsFull
	goto RogueRestStopGames_EventScript_ReturnToChooseNewPrize
	end	
	
RogueRestStopGames_EventScript_NoRoomForNew2::
	call Common_EventScript_BagIsFull
	goto RogueRestStopGames_EventScript_ReturnToChooseNewPrize2
	end		

RogueRestStopGames_EventScript_CancelTMSelect::
	msgbox RogueRestStopGames_Text_OhIsThatSo, MSGBOX_DEFAULT
	hidecoinsbox 0, 0
	release
	end

RogueRestStopGames_EventScript_Woman2::
	msgbox RogueRestStopGames_Text_CoinsAreNeededToPlay, MSGBOX_NPC
	end
	
RogueRestStopGames_EventScript_Dealer::
	msgbox RogueRestStopGames_Text_Dealer, MSGBOX_NPC
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end
	
RogueRestStopGames_EventScript_Dealer2::
	msgbox RogueRestStopGames_Text_Dealer2, MSGBOX_NPC
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end	
	
RogueRestStopGames_EventScript_VFGuy1::
	msgbox RogueRestStopGames_Text_VFGuy1, MSGBOX_NPC
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end		
	
RogueRestStopGames_EventScript_VFGuy2::
	msgbox RogueRestStopGames_Text_VFGuy2, MSGBOX_NPC
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end			

RogueRestStopGames_EventScript_Gentleman::
	msgbox RogueRestStopGames_Text_RouletteOnlyLuck, MSGBOX_NPC
	end

RogueRestStopGames_EventScript_Girl::
	lock
	faceplayer
	goto_if_set FLAG_RECEIVED_STARTER_DOLL, RogueRestStopGames_EventScript_ReceivedStarterDoll
	msgbox RogueRestStopGames_Text_GotTwoOfSameDollWantOne, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, RogueRestStopGames_EventScript_DeclineStarterDoll
	switch VAR_STARTER_MON
	case 0, RogueRestStopGames_EventScript_GiveTreeckoDoll
	case 1, RogueRestStopGames_EventScript_GiveTorchicDoll
	case 2, RogueRestStopGames_EventScript_GiveMudkipDoll
	end

RogueRestStopGames_EventScript_GiveTreeckoDoll::
	bufferdecorationname STR_VAR_2, DECOR_TREECKO_DOLL
	checkdecorspace DECOR_TREECKO_DOLL
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForStarterDoll
	msgbox RogueRestStopGames_Text_HereYouGo2, MSGBOX_DEFAULT
	givedecoration DECOR_TREECKO_DOLL
	setflag FLAG_RECEIVED_STARTER_DOLL
	goto RogueRestStopGames_EventScript_ReceivedStarterDoll
	end

RogueRestStopGames_EventScript_GiveTorchicDoll::
	bufferdecorationname STR_VAR_2, DECOR_TORCHIC_DOLL
	checkdecorspace DECOR_TORCHIC_DOLL
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForStarterDoll
	msgbox RogueRestStopGames_Text_HereYouGo2, MSGBOX_DEFAULT
	givedecoration DECOR_TORCHIC_DOLL
	setflag FLAG_RECEIVED_STARTER_DOLL
	goto RogueRestStopGames_EventScript_ReceivedStarterDoll
	end

RogueRestStopGames_EventScript_GiveMudkipDoll::
	bufferdecorationname STR_VAR_2, DECOR_MUDKIP_DOLL
	checkdecorspace DECOR_MUDKIP_DOLL
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoRoomForStarterDoll
	msgbox RogueRestStopGames_Text_HereYouGo2, MSGBOX_DEFAULT
	givedecoration DECOR_MUDKIP_DOLL
	setflag FLAG_RECEIVED_STARTER_DOLL
	goto RogueRestStopGames_EventScript_ReceivedStarterDoll
	end

RogueRestStopGames_EventScript_NoRoomForStarterDoll::
	call Common_EventScript_NoRoomForDecor
	msgbox RogueRestStopGames_Text_YouWantItButNotNow, MSGBOX_DEFAULT
	release
	end

RogueRestStopGames_EventScript_DeclineStarterDoll::
	msgbox RogueRestStopGames_Text_DontBeNegative, MSGBOX_DEFAULT
	release
	end

RogueRestStopGames_EventScript_ReceivedStarterDoll::
	msgbox RogueRestStopGames_Text_CantWinJackpot, MSGBOX_DEFAULT
	release
	end

RogueRestStopGames_EventScript_PokefanM::
	lock
	faceplayer
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, TRUE, RogueRestStopGames_EventScript_TryGive20Coins
	msgbox RogueRestStopGames_Text_NeedCoinCaseGoNextDoor, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end

RogueRestStopGames_EventScript_TryGive20Coins::
	goto_if_set FLAG_RECEIVED_20_COINS, RogueRestStopGames_EventScript_PokefanMNormal
	checkcoins VAR_TEMP_1
	goto_if_ge VAR_TEMP_1, 1, RogueRestStopGames_EventScript_PokefanMNormal @ Only give 20 coins if player has no coins
	setflag FLAG_RECEIVED_20_COINS
	addcoins 20
	msgbox RogueRestStopGames_Text_LuckOnlyLastSoLongTakeCoins, MSGBOX_DEFAULT
	playse SE_SHOP
	goto RogueRestStopGames_EventScript_PokefanMNormal
	end

RogueRestStopGames_EventScript_PokefanMNormal::
	msgbox RogueRestStopGames_Text_MauvilleSomethingForEveryone, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end

RogueRestStopGames_EventScript_OldMan::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_RouletteTablesDifferentRates, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end
	
RogueRestStopGames_EventScript_PachinkoGuy::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_PachinkoGuy, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end	
	
RogueRestStopGames_EventScript_Hiker::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_Hiker, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end		

RogueRestStopGames_EventScript_Bouncer::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_Bouncer2, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end		
	
RogueRestStopGames_EventScript_DerbyLady1::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_Derby_1, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end			
	
RogueRestStopGames_EventScript_DerbyLady2::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_Derby_2, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end			

RogueRestStopGames_EventScript_Fisherman::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_Fisherman, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end			
	
RogueRestStopGames_EventScript_Gachalady::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_Gachalady, MSGBOX_DEFAULT
	closemessage
	end			

RogueRestStopGames_EventScript_Employee::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_Employee, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end			

RogueRestStopGames_EventScript_Cook::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_EasyToLoseTrackOfTime, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end

RogueRestStopGames_EventScript_Man::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_UpTo3CoinsCanBeUsed, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end

RogueRestStopGames_EventScript_JuiceLady::	
	lock
	faceplayer
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase2
	showcoinsbox 1, 1
	msgbox RogueRestStopGames_Text_Juice, MSGBOX_YESNO
	closemessage
	goto_if_eq VAR_RESULT, NO, RogueRestStopGames_CancelVF
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, 10, Juice_Not_Enough
	removecoins 10
	updatecoinsbox 1, 1
	playse SE_SHOP
	delay 40
	hidecoinsbox 0, 5
	giveitem ITEM_BERRY_JUICE
	release
	end

RogueRestStopGames_EventScript_NPCReturnToSlots::
	closemessage
	applymovement VAR_LAST_TALKED, Common_Movement_FaceOriginalDirection
	waitmovement 0
	release
	end

RogueRestStopGames_EventScript_Maniac::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_DifficultToStopOn7, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end

RogueRestStopGames_EventScript_Woman::
	lock
	faceplayer
	msgbox RogueRestStopGames_Text_HeresSomeSlotsInfo, MSGBOX_DEFAULT
	goto RogueRestStopGames_EventScript_NPCReturnToSlots
	end

RogueRestStopGames_EventScript_SlotMachine0::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	setvar VAR_0x8004, 0
	specialvar VAR_RESULT, GetSlotMachineId
	playbgm MUS_GAME_CORNER, FALSE
	playslotmachine VAR_RESULT
	releaseall
	end

RogueRestStopGames_EventScript_SlotMachine1::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	setvar VAR_0x8004, 1
	specialvar VAR_RESULT, GetSlotMachineId
	playbgm MUS_GAME_CORNER, FALSE
	playslotmachine VAR_RESULT
	releaseall
	end

RogueRestStopGames_EventScript_SlotMachine2::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	setvar VAR_0x8004, 2
	specialvar VAR_RESULT, GetSlotMachineId
	playbgm MUS_GAME_CORNER, FALSE
	playslotmachine VAR_RESULT
	releaseall
	end

RogueRestStopGames_EventScript_SlotMachine3::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	setvar VAR_0x8004, 3
	specialvar VAR_RESULT, GetSlotMachineId
	playbgm MUS_GAME_CORNER, FALSE
	playslotmachine VAR_RESULT
	releaseall
	end

RogueRestStopGames_EventScript_SlotMachine4::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	setvar VAR_0x8004, 4
	specialvar VAR_RESULT, GetSlotMachineId
	playbgm MUS_GAME_CORNER, FALSE
	playslotmachine VAR_RESULT
	releaseall
	end

RogueRestStopGames_EventScript_SlotMachine5::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	setvar VAR_0x8004, 5
	specialvar VAR_RESULT, GetSlotMachineId
	playbgm MUS_GAME_CORNER, FALSE
	playslotmachine VAR_RESULT
	releaseall
	end

RogueRestStopGames_EventScript_SlotMachine6::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	setvar VAR_0x8004, 6
	specialvar VAR_RESULT, GetSlotMachineId
	playbgm MUS_GAME_CORNER, FALSE
	playslotmachine VAR_RESULT
	releaseall
	end

RogueRestStopGames_EventScript_SlotMachine7::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	setvar VAR_0x8004, 7
	specialvar VAR_RESULT, GetSlotMachineId
	playbgm MUS_GAME_CORNER, FALSE
	playslotmachine VAR_RESULT
	releaseall
	end

RogueRestStopGames_EventScript_SlotMachine8::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	setvar VAR_0x8004, 8
	specialvar VAR_RESULT, GetSlotMachineId
	playbgm MUS_GAME_CORNER, FALSE
	playslotmachine VAR_RESULT
	releaseall
	end

RogueRestStopGames_EventScript_SlotMachine9::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	setvar VAR_0x8004, 9
	specialvar VAR_RESULT, GetSlotMachineId
	playbgm MUS_GAME_CORNER, FALSE
	playslotmachine VAR_RESULT
	releaseall
	end

RogueRestStopGames_EventScript_SlotMachine10::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	setvar VAR_0x8004, 10
	specialvar VAR_RESULT, GetSlotMachineId
	playbgm MUS_GAME_CORNER, FALSE
	playslotmachine VAR_RESULT
	releaseall
	end

RogueRestStopGames_EventScript_SlotMachine11::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCoinCase
	setvar VAR_0x8004, 11
	specialvar VAR_RESULT, GetSlotMachineId
	playbgm MUS_GAME_CORNER, FALSE
	playslotmachine VAR_RESULT
	releaseall
	end

RogueRestStopGames_EventScript_NoCoinCase::
	msgbox RogueRestStopGames_Text_CantPlayWithNoCoinCase, MSGBOX_DEFAULT
	releaseall
	end

RogueRestStopGames_EventScript_NoCoinCase2::
	msgbox RogueRestStopGames_Text_CantBuyWithNoCoinCase, MSGBOX_DEFAULT
	releaseall
	end	

RogueRestStopGames_Text_ThisIsMauvilleGameCorner:
	.string "This is MAUVILLE GAME CORNER.$"

RogueRestStopGames_Text_PachinkoGuy:
	.string "I used to own one of these dusty old\n"
	.string "DROPSPIN MACHINES.\p"
	.string "The manual always said 'Use the\n"
	.string "DIRECTIONAL BUTTONS to TILT and\l"
	.string "change the DROP SPEED…'\p"
	.string "I've always wondered what that\n"
	.string "means.$"

RogueRestStopGames_Text_Hiker:
	.string "Gah!\n"
	.string "I blew it all on that ONIX game.\p"
	.string "You need to be careful…\p"
	.string "You only win 2 COINS for\n"
	.string "every point in these machines.$"
	
RogueRestStopGames_Text_Derby_1:
	.string "Looking to bet on the DERBY?\n"
	.string "We've gone digital!\p"
	.string "Use one of the tablets\n"
	.string "in front of the counter.$"	
	
RogueRestStopGames_Text_Derby_2:
	.string "It's not all about CONDITION.\p"
	.string "At the end of the day, it's\n"
	.string "really all luck.\p"
	.string "Sometimes it's best to just\n"
	.string "pick your favorite!$"	
	
RogueRestStopGames_Text_Fisherman:
	.string "I come here for the food.\p"
	.string "It's no CELADON CAFE…\n"
	.string "But it'll do.$"		
	
RogueRestStopGames_Text_Gachalady:
	.string "The LOTTERY MACHINES have odds,\n"
	.string "but nothing is guaranteed.\p"
	.string "Some machines are better than\n"
	.string "others, and there are different\l"
	.string "rarities in all of them.\p"
	.string "New POKéMON are also weighted\n"
	.string "By your POKéDEX, so be wary.$"	
	
RogueRestStopGames_Text_Employee:
	.string "I'm on the lookout for cheaters.\n"
	.string "Nobody likes a cheater!$"
	
RogueRestStopGames_Text_Bouncer1:
	.string "You need a valid TRAINER ID\n"
	.string "to enter the GAME CORNER.\p"
	.string "Let's see…\p"
	.string "…\p"
	.string "Hmm…\p"
	.string "…\p"
	.string "Everything looks good here.\n"
	.string "Enjoy the GAME CORNER!$"	
	
RogueRestStopGames_Text_Bouncer2:
	.string "Enjoy the GAME CORNER!$"	
	
RogueRestStopGames_Text_Pachinko:
	.string "A DROPSPIN MACHINE!\n"
	.string "It's 3 COINS per BALL.$"
	
RogueRestStopGames_Text_NeedCoinCaseForCoins:
	.string "Okay, you wanted some COINS for\n"
	.string "the games?\p"
	.string "But you don't have a COIN CASE for\n"
	.string "stowing the COINS.$"

RogueRestStopGames_Text_WereYouLookingForCoins:
	.string "Were you looking for COINS?\p"
	.string "It's ¥1000 for 50 COINS.\n"
	.string "Would you like some?$"

RogueRestStopGames_Text_ThankYouHereAreYourCoins:
	.string "Thank you very much!\n"
	.string "Here are your COINS!$"

RogueRestStopGames_Text_DontHaveEnoughMoney:
	.string "Um… You don't appear to have\n"
	.string "enough money…$"

RogueRestStopGames_Text_CoinCaseIsFull:
	.string "Oh?\n"
	.string "Your COIN CASE is full.$"

RogueRestStopGames_Text_DontNeedCoinsThen:
	.string "Oh… You don't need COINS, then?\n"
	.string "Good luck on your adventure!$"

RogueRestStopGames_Text_ExchangeCoinsForPrizes:
	.string "Welcome.\p"
	.string "You can exchange your COINS for\n"
	.string "prizes here.$"

RogueRestStopGames_Text_WhichPrize:
	.string "Which prize would you like?$"

RogueRestStopGames_Text_SoYourChoiceIsTheTMX:
	.string "So your choice is\n"
	.string "the {STR_VAR_1} {STR_VAR_2}?$"
	
RogueRestStopGames_Text_SoYourChoiceIsTheNewX:
	.string "So your choice is\n"
	.string "the {STR_VAR_1}?$"

RogueRestStopGames_Text_BlackJack:
	.string "It's a BLACKJACK TABLE!\n"
	.string "Want to play?$"
	
RogueRestStopGames_Text_Derby:
	.string "It's the MAUVILLE DERBY!\n"
	.string "Want to place a bet?$"	
	
RogueRestStopGames_Text_Snake:
	.string "It's ONIX!\n"
	.string "It costs 25 COINS to play.$"		
	
RogueRestStopGames_Text_Bird:
	.string "It's FLUTTERFREE!\n"
	.string "It costs 25 COINS to play.$"		
	
RogueRestStopGames_Text_Pinball_Meowth:
	.string "It's a PINBALL MACHINE!\n"
	.string "It costs 25 COINS to play.$"

RogueRestStopGames_Text_BS:
	.string "It's BLOCK BUILDER!\n"
	.string "It costs 25 COINS to play.$"
	
RogueRestStopGames_Text_Pinball_Seel:
	.string "It's a PINBALL MACHINE!\n"
	.string "It costs 25 COINS to play.$"
	
RogueRestStopGames_Text_Pinball_Diglett:
	.string "It's a PINBALL MACHINE!\n"
	.string "It costs 50 COINS to play.$"
	
RogueRestStopGames_Text_Pinball_Gengar:
	.string "It's a PINBALL MACHINE!\n"
	.string "It costs 100 COINS to play.$"
	
RogueRestStopGames_Text_VF:
	.string "It's VOLTORB FLIP!\n"
	.string "It costs 50 COINS to play.$"

VF_Come_Again_Text:
	.string "Too bad.\n"
	.string "Try again soon!$"

RogueRestStopGames_Text_SendToYourHomePC:
	.string "Thank you!\n"
	.string "We'll send it to your PC at home.$"

PB_YOU_WON_TEXT:
	.string "You won\n"
	.string "{STR_VAR_1} COINS!$"

PB_Not_Enough:
	.string "It's a PINBALL MACHINE!\p"
	.string "You don't have enough COINS…$"
	
Snake_Not_Enough:
	.string "It's ONIX!\p"
	.string "You don't have enough COINS…$"	
	
Bjuice_Not_Enough:
	.string "You don't have enough COINS.$"	
	
Bird_Not_Enough:
	.string "It's FLUTTERFREE!\p"
	.string "You don't have enough COINS…$"		
	
BS_Not_Enough:
	.string "It's BLOCK BUILDER!\p"
	.string "You don't have enough COINS…$"	

Flip_Not_Enough:
	.string "It's VOLTORB FLIP!\p"
	.string "You don't have enough COINS…$"

@ Unused
RogueRestStopGames_Text_NoRoomForPlacingDecor:
	.string "There isn't any room available for\n"
	.string "placing {STR_VAR_1}.$"
	
RogueRestStopGames_Text_VF_Keep_Going:
	.string "You've made {STR_VAR_1} COINS.\n"
	.string "Keep going?$"

RogueRestStopGames_Text_OhIsThatSo:
	.string "Oh, is that so? \n"
	.string "You need to save some COINS before\l"
	.string "coming back here.$"

RogueRestStopGames_Text_SoYourChoiceIsX:
	.string "So your choice is {STR_VAR_1}?$"

RogueRestStopGames_Text_HereYouGo:
	.string "Here you go!$"

@ Unused
RogueRestStopGames_Text_CantCarryAnyMore:
	.string "Oh, you can't carry any more than that.$"

RogueRestStopGames_Text_GotTwoOfSameDollWantOne:
	.string "I made a mistake and got two of\n"
	.string "the same DOLLS.\p"
	.string "Would you like one of them?$"

RogueRestStopGames_Text_HereYouGo2:
	.string "Here you go!$"

RogueRestStopGames_Text_YouWantItButNotNow:
	.string "Huh?\n"
	.string "You want it, but not right now?$"

RogueRestStopGames_Text_DontBeNegative:
	.string "Oh, don't be so negative!\n"
	.string "You can have this!$"

RogueRestStopGames_Text_CantWinJackpot:
	.string "There's a prize I want, but I can't win\n"
	.string "the jackpot.$"

RogueRestStopGames_Text_NeedCoinCaseGoNextDoor:
	.string "Hey, kid, if you want to play here,\n"
	.string "you need a COIN CASE.\p"
	.string "I think the young lady next door\n"
	.string "had one. Go see her!$"

RogueRestStopGames_Text_LuckOnlyLastSoLongTakeCoins:
	.string "My luck can only last so long.\n"
	.string "This is too much for me.\l"
	.string "Here, take some COINS!$"

RogueRestStopGames_Text_MauvilleSomethingForEveryone:
	.string "MAUVILLE has something for\n"
	.string "everyone.\p"
	.string "For me, it's the GAME CORNER.$"

RogueRestStopGames_Text_RouletteTablesDifferentRates:
	.string "The ROULETTE tables have different\n"
	.string "rates.\p"
	.string "Check your COINS if you're going to\n"
	.string "pick a table.$"

RogueRestStopGames_Text_EasyToLoseTrackOfTime:
	.string "It's easy to lose track of time in here. \n"
	.string "I should get back to work.$"

RogueRestStopGames_Text_CoinsAreNeededToPlay:
	.string "COINS are needed to play here\n"
	.string "in the GAME CORNER.$"
	
	RogueRestStopGames_Text_Dealer:
	.string "Some say BLACKJACK is about skill.\n"
	.string "Others say it's about luck.\p"
	.string "I say it's about having fun…\n"
	.string "and maybe winning big!$"
	
	RogueRestStopGames_Text_Dealer2:
	.string "You can bet anything divisible\n"
	.string "by 10 here.\p"
	.string "Maybe you should go all in!$"	

RogueRestStopGames_Text_VFGuy1:
	.string "You want to play VOLTORB FLIP?\p"
	.string "You need to be on the other\n"
	.string "side of the table.$"
	
RogueRestStopGames_Text_VFGuy2:
	.string "Try the other side of\n"
	.string "the table, pal.$"	

RogueRestStopGames_Text_RouletteOnlyLuck:
	.string "This ROULETTE thing…\n"
	.string "It's rather demanding.\p"
	.string "Win or lose, it's only by luck.$"

RogueRestStopGames_Text_UpTo3CoinsCanBeUsed:
	.string "Up to three COINS can be used to play\n"
	.string "the SLOTS.$"

RogueRestStopGames_Text_DifficultToStopOn7:
	.string "It's very difficult to make it stop\n"
	.string "right on “7.”\p"
	.string "If it stops on “7” during the REEL TIME\n"
	.string "bonus game, you'll receive extra COINS.$"

RogueRestStopGames_Text_HeresSomeSlotsInfo:
	.string "Here's some information for you\n"
	.string "about the SLOTS.\p"
	.string "The more lightning bolts you stock,\n"
	.string "the more REEL TIME chances you get.\p"
	.string "In a game with the maximum five\n"
	.string "REEL TIME chances…\p"
	.string "It is possible to receive four regular\n"
	.string "bonuses, then a big bonus.\p"
	.string "That would total 660 COINS, but it's\n"
	.string "very difficult to get.$"

RogueRestStopGames_Text_CantPlayWithNoCoinCase:
	.string "You can't play if you don't have\n"
	.string "a COIN CASE.$"

RogueRestStopGames_Text_CantBuyWithNoCoinCase:
	.string "Huh? Looks like you need\n"
	.string "a COIN CASE.$"
	
RogueRestStopGames_Text_Juice:
	.string "BERRY JUICE!\n"
	.string "Just 10 COINS.$"	
RogueRestStopGames_Text_CabinetOffline:
	.string "The cabinet is dark.\n"
	.string "Someone forgot to plug it in.$"

