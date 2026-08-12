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
	call RogueDungeonFloor_EventScript_RunSummary
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
	@ ONE ROD FOR THE WHOLE RUN. It starts on the Old technique and gains the
	@ Good and Super ones by depth -- see ApplyRunConfig, which sets the two
	@ flags, and DUNGEON_ROD_GOOD_DUNGEON for where. So this stays a single
	@ unconditional grant even though the rod is a progression: what improves is
	@ the technique, not the item.
	additem ITEM_ROGUE_VARIABLE_ROD, 1
	@ The fourth tool, same argument as the other three: the workbench is a
	@ key item you carry, and no map in a run hands one out, so without this
	@ the entire crafting system is unreachable code. It is granted rather
	@ than sold because a run's coins already have two sinks and because a
	@ player who cannot craft cannot discover that materials are worth
	@ digging for -- and the materials are buried from floor 1.
	additem ITEM_CRAFT_BUNDLE, 1
	@ Both bikes. Vanilla gives one or the other and treats the choice as
	@ progression; a run has no Cycling Road to earn either on, so both are
	@ granted and the choice is which one suits the floor -- Mach for crossing
	@ a big open room, Acro for nothing in particular yet.
	@
	@ THESE DO NOTHING WITHOUT THE MAP FLAG. Overworld_IsBikingAllowed reads
	@ gMapHeader.allowCycling and every map in a run shipped with it false, so
	@ granting the bikes alone would have put two key items in the bag that
	@ refuse to deploy everywhere the player can stand. The run's maps set it
	@ true now. It is NOT the same gate as Fly and Teleport, which key on map
	@ type -- so the Safari copies stay MAP_TYPE_UNDERGROUND and stay unflyable.
	additem ITEM_MACH_BIKE, 1
	additem ITEM_ACRO_BIKE, 1
	@ TWO BATTLE GIMMICKS OF THE FOUR THIS BUILD COMPILES. Each is gated on the
	@ player HOLDING its key item, and the four gates live in three files:
	@
	@   battle_util.c     CheckBagHasItem(ITEM_MEGA_RING)     -> Mega Evolution
	@   battle_util.c     CheckBagHasItem(ITEM_Z_POWER_RING)  -> Z-moves, Ultra Burst
	@   battle_dynamax.c  CheckBagHasItem(ITEM_DYNAMAX_BAND)  -> Dynamax
	@   battle_terastal.c CheckBagHasItem(ITEM_TERA_ORB)      -> Terastallization
	@
	@ Mega and Z are granted. THE CHECK IS ON PLAYER POSITIONS ONLY -- a trainer
	@ is not gated on anything in the player's bag -- so a boss could already
	@ Mega Evolve against a player who structurally could not answer. Those two
	@ are parity rather than a buff.
	@
	@ DYNAMAX AND TERASTALLIZATION ARE DELIBERATELY WITHHELD, and the two are
	@ withheld for different reasons worth keeping apart.
	@
	@ Tera is the bigger of the two: it rewrites a Pokemon's typing on demand,
	@ so it changes every matchup in the game rather than adding a phase to
	@ some of them. B_FLAG_TERA_ORB_CHARGED is 0, so the orb would never need
	@ recharging and would be live in every battle of all 115 floors -- a run
	@ built on type answers to fourteen themed pools does not survive that being
	@ free. Dynamax is a two-turn power spike whose whole design assumes a raid
	@ or a gym, neither of which a floor is.
	@
	@ Both are one line to restore. P_GIGANTAMAX_FORMS and P_TERA_FORMS stay
	@ TRUE, so the forms and their data are still compiled -- what is withheld
	@ is the player's access, not the mechanic. Trainers are unaffected by this
	@ either way, since they were never gated on the bag.
	additem ITEM_MEGA_RING, 1
	additem ITEM_Z_POWER_RING, 1
	setvar VAR_ROGUE_RUN_STATE, ROGUE_RUN_ACTIVE
	releaseall
	end

@ The death summary, shown once, immediately before the run that follows it is
@ set up. A loss is otherwise silent - the party is wiped, the player is stood
@ back on floor one and nothing tells them how far they got - and a run this
@ long ending with no account of itself is what makes a loss read as wasted
@ time rather than as a score to beat.
@
@ HERE RATHER THAN AT THE WHITEOUT, because the whiteout runs from C with no
@ message window open and warps out of the map on the same frame. What it
@ leaves behind is VAR_ROGUE_LAST_RUN_FLOOR, and non-zero is what "a summary is
@ waiting" means - see RogueDungeon_TryHandleWhiteOut.
@
@ Clearing the var is what makes it show once. Without that it would reappear
@ on every entry to the starter prompt, including a plain new game.
RogueDungeonFloor_EventScript_RunSummary::
	compare VAR_ROGUE_LAST_RUN_FLOOR, 0
	goto_if_eq RogueDungeonFloor_EventScript_NoRunSummary
	special RogueDungeon_BufferRunSummary
	msgbox RogueDungeonFloor_Text_RunSummary, MSGBOX_DEFAULT
	setvar VAR_ROGUE_LAST_RUN_FLOOR, 0
RogueDungeonFloor_EventScript_NoRunSummary:
	return

RogueDungeonFloor_Text_RunSummary:
	.string "That run ended on floor {STR_VAR_1},\n"
	.string "in {STR_VAR_2}.\p"
	.string "Deepest floor reached: {STR_VAR_3}.$"

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

@ ------------------------------------------------------------- floor events
@
@ One per floor at most, one floor in four, placed and rolled in C by
@ PlaceEvents. The template's script pointer comes straight out of
@ sDungeonEvents, so each event has its OWN entry point and there is no
@ dispatch here - nothing below asks which event it is.
@
@ Every one is a question with a cost on both branches. An event that is only a
@ reward is an item ball with more text, and the floor already has eight of
@ those.
@
@ The spring. A free full heal, deliberately with no catch: the cost is the
@ opportunity, since walking on hurt is the alternative and the bag is finite.
@ HealPlayerParty rather than the Pokemon Center field effect, for the reason
@ the rest stop's nurse documents - that effect places its sprites at screen
@ coordinates that assume a Center's layout.
RogueDungeonFloor_EventScript_EventSpring::
	lock
	faceplayer
	msgbox RogueDungeonFloor_Text_SpringOffer, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	special HealPlayerParty
	playfanfare MUS_HEAL
	waitfanfare
	msgbox RogueDungeonFloor_Text_SpringDone, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ The gambler. Double or nothing on a third of what the player is carrying.
@
@ A THIRD RATHER THAN A FIXED SUM, so the offer scales with the run instead of
@ being unrefusable on floor 5 and pocket change on floor 90. checkmoney's
@ result is the gate: an offer the player cannot cover would otherwise take
@ money they do not have, and removemoney on an insufficient balance is exactly
@ the kind of thing that reads as a broken event rather than a declined one.
RogueDungeonFloor_EventScript_EventGambler::
	lock
	faceplayer
	special RogueDungeon_BufferEventStake
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventGamblerBroke
	msgbox RogueDungeonFloor_Text_GamblerOffer, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	special RogueDungeon_ResolveEventStake
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventGamblerLost
	msgbox RogueDungeonFloor_Text_GamblerWon, MSGBOX_DEFAULT
	playfanfare MUS_OBTAIN_ITEM
	waitfanfare
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventGamblerLost::
	msgbox RogueDungeonFloor_Text_GamblerLost, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventGamblerBroke::
	msgbox RogueDungeonFloor_Text_GamblerBroke, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ The pedlar. One item, sight unseen, at a price set by depth.
@
@ The item is rolled in C from the same banded loot table the floor's item balls
@ draw from, so it is always depth-appropriate and never needs a literal here.
@ The price is deliberately above what the item is usually worth - the thing
@ being sold is the gamble, and a pedlar who was reliably good value would just
@ be the mart with extra steps.
RogueDungeonFloor_EventScript_EventPedlar::
	lock
	faceplayer
	special RogueDungeon_BufferEventWares
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventPedlarBroke
	msgbox RogueDungeonFloor_Text_PedlarOffer, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventPedlarDeclined
	special RogueDungeon_BuyEventWares
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventPedlarNoRoom
	playfanfare MUS_OBTAIN_ITEM
	waitfanfare
	msgbox RogueDungeonFloor_Text_PedlarBought, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventPedlarDeclined::
	msgbox RogueDungeonFloor_Text_PedlarDeclined, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventPedlarNoRoom::
	msgbox RogueDungeonFloor_Text_PedlarNoRoom, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventPedlarBroke::
	msgbox RogueDungeonFloor_Text_PedlarBroke, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ The trader. A Pokemon for a Pokemon, five levels up, species unseen.
@
@ REFUSED ON A PARTY OF ONE, and that guard is not politeness - trading the last
@ Pokemon away leaves an empty party in the middle of a floor, which is a
@ whiteout the engine was never asked for.
@
@ ChoosePartyMon puts the chosen slot in VAR_0x8004 and a value at or above
@ PARTY_SIZE means the player backed out, which is the same idiom the debug menu's
@ EV and IV readouts use.
RogueDungeonFloor_EventScript_EventTrader::
	lock
	faceplayer
	special RogueDungeon_EventTraderCanTrade
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventTraderAlone
	msgbox RogueDungeonFloor_Text_TraderOffer, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventTraderDeclined
	special ChoosePartyMon
	waitstate
	goto_if_ge VAR_0x8004, PARTY_SIZE, RogueDungeonFloor_EventScript_EventTraderDeclined
	special RogueDungeon_EventTraderOffer
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventTraderDeclined
	msgbox RogueDungeonFloor_Text_TraderConfirm, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventTraderDeclined
	special RogueDungeon_EventTraderDo
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventTraderDeclined
	playfanfare MUS_OBTAIN_ITEM
	waitfanfare
	msgbox RogueDungeonFloor_Text_TraderDone, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventTraderDeclined::
	msgbox RogueDungeonFloor_Text_TraderDeclined, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventTraderAlone::
	msgbox RogueDungeonFloor_Text_TraderAlone, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ The egg. Free, and the price is the party slot.
RogueDungeonFloor_EventScript_EventEgg::
	lock
	faceplayer
	msgbox RogueDungeonFloor_Text_EggOffer, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	special RogueDungeon_EventEggTake
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventEggNoRoom
	playfanfare MUS_EVOLVED
	waitfanfare
	msgbox RogueDungeonFloor_Text_EggHatched, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventEggNoRoom::
	msgbox RogueDungeonFloor_Text_EggNoRoom, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ The injured Pokemon, wearing its own overworld sprite.
@
@ Whether it joins or was feigning is decided at PREPARE time from the floor's
@ seed, so saving and reloading regenerates the same floor and the same answer.
@ Rolling it here would let a player reload until it joined.
@
@ The ambush goes through a special rather than setwildbattle because that command
@ takes its level as a byte literal, and this one is the floor's level plus five.
RogueDungeonFloor_EventScript_EventInjured::
	lock
	faceplayer
	special RogueDungeon_EventInjuredApproach
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventInjuredAmbush
	msgbox RogueDungeonFloor_Text_InjuredOffer, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	special RogueDungeon_EventInjuredJoin
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventInjuredNoRoom
	playfanfare MUS_OBTAIN_ITEM
	waitfanfare
	msgbox RogueDungeonFloor_Text_InjuredJoined, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventInjuredNoRoom::
	msgbox RogueDungeonFloor_Text_InjuredNoRoom, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ No yes/no here on purpose. Approaching WAS the choice, and a prompt after the
@ trap has sprung would be the game asking permission to spring it.
RogueDungeonFloor_EventScript_EventInjuredAmbush::
@ And the bag is NOT locked. An ambusher is an ordinary wild Pokemon and should
@ be catchable - FLAG_SYS_NO_BAG_USE is for battles the player must not win with
@ a ball, which this is not. Catching the thing that jumped you is the best
@ outcome this event has.
RogueDungeonFloor_EventScript_EventInjuredAmbush::
	msgbox RogueDungeonFloor_Text_InjuredAmbush, MSGBOX_DEFAULT
	special RogueDungeon_EventInjuredAmbush
	dowildbattle
	release
	end

@ Shared tail. The NPC is NOT removed after being used: a floor is never
@ revisited - the stairs warp into a freshly generated one - so there is nothing
@ to persist and no flag to spend on it.
RogueDungeonFloor_EventScript_EventDone::
	release
	end

RogueDungeonFloor_Text_SpringOffer:
	.string "An old woman is resting beside a\n"
	.string "spring here.\p"
	.string "“You look like you have come a long\n"
	.string "way. Shall I tend to your team?”$"

RogueDungeonFloor_Text_SpringDone:
	.string "“There. Go carefully down there.”$"

RogueDungeonFloor_Text_GamblerOffer:
	.string "“Care for a wager, friend?\p"
	.string "¥{STR_VAR_1} on a coin toss. Call it\n"
	.string "right and you leave with double.”$"

RogueDungeonFloor_Text_GamblerWon:
	.string "“Ha! The floor favours you today.”\p"
	.string "{PLAYER} won ¥{STR_VAR_1}!$"

RogueDungeonFloor_Text_GamblerLost:
	.string "“Ah. Better luck the next floor down.”\p"
	.string "{PLAYER} lost ¥{STR_VAR_1}…$"

RogueDungeonFloor_Text_GamblerBroke:
	.string "“No money, no wager. Come back when\n"
	.string "your pockets have something in them.”$"

RogueDungeonFloor_Text_PedlarOffer:
	.string "A pedlar has set out a blanket here.\p"
	.string "“One item. I will not tell you which.\n"
	.string "¥{STR_VAR_1}. Yes or no?”$"

RogueDungeonFloor_Text_PedlarBought:
	.string "{PLAYER} obtained the {STR_VAR_2}!$"

RogueDungeonFloor_Text_PedlarDeclined:
	.string "“Suit yourself. It was a good one,\n"
	.string "too.”$"

RogueDungeonFloor_Text_PedlarNoRoom:
	.string "“You have nowhere to put it. Come\n"
	.string "back when your bag is not bursting.”$"

RogueDungeonFloor_Text_PedlarBroke:
	.string "“¥{STR_VAR_1}. No? Then we have nothing\n"
	.string "to discuss.”$"

RogueDungeonFloor_Text_TraderOffer:
	.string "A hiker is sat against the wall with\n"
	.string "a ball in his lap.\p"
	.string "“Straight swap. One of yours for mine,\n"
	.string "and mine comes out five levels above\l"
	.string "whatever you hand me.\p"
	.string "I will not tell you what it is.”$"

RogueDungeonFloor_Text_TraderConfirm:
	.string "“{STR_VAR_1}, then. Mine will be at\n"
	.string "level {STR_VAR_2}.\p"
	.string "Last chance to change your mind.”$"

RogueDungeonFloor_Text_TraderDone:
	.string "{PLAYER} handed over {STR_VAR_1}…\p"
	.string "and received {STR_VAR_2}!$"

RogueDungeonFloor_Text_TraderDeclined:
	.string "“Wise, probably.”$"

RogueDungeonFloor_Text_TraderAlone:
	.string "“You have got the one and that is all.\n"
	.string "I am not taking it off you.”$"

RogueDungeonFloor_Text_EggOffer:
	.string "A woman is crouched over something\n"
	.string "warm.\p"
	.string "“It is nearly out. I cannot carry it\n"
	.string "any further. Will you take it?”$"

RogueDungeonFloor_Text_EggHatched:
	.string "The egg cracked open…\p"
	.string "{PLAYER}'s {STR_VAR_1} hatched!$"

RogueDungeonFloor_Text_EggNoRoom:
	.string "“You have no room for it. Come back\n"
	.string "lighter and it is yours.”$"

RogueDungeonFloor_Text_InjuredOffer:
	.string "A {STR_VAR_1} is curled up here.\n"
	.string "It is hurt, and it is watching you.\p"
	.string "Help it?$"

RogueDungeonFloor_Text_InjuredJoined:
	.string "The {STR_VAR_1} got to its feet.\p"
	.string "{STR_VAR_1} joined {PLAYER}!$"

RogueDungeonFloor_Text_InjuredNoRoom:
	.string "The {STR_VAR_1} looked at {PLAYER}'s\n"
	.string "full party, and settled back down.$"

RogueDungeonFloor_Text_InjuredAmbush:
	.string "The {STR_VAR_1} was not hurt at all!$"

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

@ A mining rock. The minigame is a special, so this is a lock, a call and a
@ cleanup -- StartMining sets its own callback and returns to the field itself,
@ which is why nothing here waits on it.
@
@ The rock is removed BEFORE the minigame rather than after: StartMining leaves
@ through CB2_ReturnToField, so script execution does not resume on the far side
@ and anything written after the special would never run.
RogueDungeonFloor_EventScript_MiningRock::
	lock
	faceplayer
	special RogueDungeon_HideMinedRock
	special StartMining
	releaseall
	end
