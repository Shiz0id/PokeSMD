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

RogueRestStopGames_MapScripts::
	.byte 0

@ ---------------------------------------------------------------- coins
@
@ The case is given rather than found. There is nowhere in a run to find one,
@ and a Game Corner the player cannot play is worse than no Game Corner.
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
RogueRestStopGames_EventScript_SlotMachine0::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCaseAll
	setvar VAR_0x8004, 0
	goto RogueRestStopGames_EventScript_PlaySlots
	end

RogueRestStopGames_EventScript_SlotMachine1::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCaseAll
	setvar VAR_0x8004, 1
	goto RogueRestStopGames_EventScript_PlaySlots
	end

RogueRestStopGames_EventScript_SlotMachine2::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCaseAll
	setvar VAR_0x8004, 2
	goto RogueRestStopGames_EventScript_PlaySlots
	end

RogueRestStopGames_EventScript_SlotMachine3::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCaseAll
	setvar VAR_0x8004, 3
	goto RogueRestStopGames_EventScript_PlaySlots
	end

RogueRestStopGames_EventScript_SlotMachine4::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCaseAll
	setvar VAR_0x8004, 4
	goto RogueRestStopGames_EventScript_PlaySlots
	end

RogueRestStopGames_EventScript_SlotMachine5::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCaseAll
	setvar VAR_0x8004, 5
	goto RogueRestStopGames_EventScript_PlaySlots
	end

RogueRestStopGames_EventScript_SlotMachine6::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCaseAll
	setvar VAR_0x8004, 6
	goto RogueRestStopGames_EventScript_PlaySlots
	end

RogueRestStopGames_EventScript_SlotMachine7::
	lockall
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, RogueRestStopGames_EventScript_NoCaseAll
	setvar VAR_0x8004, 7
	goto RogueRestStopGames_EventScript_PlaySlots
	end

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
