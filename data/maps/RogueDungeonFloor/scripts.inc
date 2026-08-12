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
	compare VAR_ROGUE_EVENT_SPENT, 1
	goto_if_eq RogueDungeonFloor_EventScript_EventSpent
	msgbox RogueDungeonFloor_Text_SpringOffer, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	setvar VAR_ROGUE_EVENT_SPENT, 1
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
	compare VAR_ROGUE_EVENT_SPENT, 1
	goto_if_eq RogueDungeonFloor_EventScript_EventSpent
	special RogueDungeon_BufferEventStake
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventGamblerBroke
	msgbox RogueDungeonFloor_Text_GamblerOffer, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	special RogueDungeon_ResolveEventStake
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventGamblerLost
	setvar VAR_ROGUE_EVENT_SPENT, 1
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
	compare VAR_ROGUE_EVENT_SPENT, 1
	goto_if_eq RogueDungeonFloor_EventScript_EventSpent
	special RogueDungeon_BufferEventWares
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventPedlarBroke
	msgbox RogueDungeonFloor_Text_PedlarOffer, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventPedlarDeclined
	special RogueDungeon_BuyEventWares
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventPedlarNoRoom
	setvar VAR_ROGUE_EVENT_SPENT, 1
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
	compare VAR_ROGUE_EVENT_SPENT, 1
	goto_if_eq RogueDungeonFloor_EventScript_EventSpent
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
	setvar VAR_ROGUE_EVENT_SPENT, 1
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
	compare VAR_ROGUE_EVENT_SPENT, 1
	goto_if_eq RogueDungeonFloor_EventScript_EventSpent
	msgbox RogueDungeonFloor_Text_EggOffer, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	special RogueDungeon_EventEggTake
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventEggNoRoom
	setvar VAR_ROGUE_EVENT_SPENT, 1
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
@
@ THE PROMPT COMES BEFORE THE REVEAL, and it is the same prompt either way. The
@ first cut branched to the ambush BEFORE asking anything, so a feigning Pokemon
@ jumped the player with no choice offered at all - the decision the event is
@ built around only existed on the outcome that was already safe. Now approaching
@ is the wager, and what was swaying at you is what you find out.
@
@ VAR_ROGUE_EVENT_SCRATCH holds the answer across the msgbox, because
@ MSGBOX_YESNO writes into VAR_RESULT and would destroy it.
RogueDungeonFloor_EventScript_EventInjured::
	lock
	faceplayer
	compare VAR_ROGUE_EVENT_SPENT, 1
	goto_if_eq RogueDungeonFloor_EventScript_EventSpent
	special RogueDungeon_EventInjuredApproach
	copyvar VAR_ROGUE_EVENT_SCRATCH, VAR_RESULT
	msgbox RogueDungeonFloor_Text_InjuredApproach, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	compare VAR_ROGUE_EVENT_SCRATCH, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventInjuredAmbush
	special RogueDungeon_EventInjuredJoin
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventInjuredNoRoom
	setvar VAR_ROGUE_EVENT_SPENT, 1
	playfanfare MUS_OBTAIN_ITEM
	waitfanfare
	msgbox RogueDungeonFloor_Text_InjuredJoined, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventInjuredNoRoom::
	msgbox RogueDungeonFloor_Text_InjuredNoRoom, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ Reached only after the player chose to approach, so there is no second prompt:
@ approaching WAS the choice, and asking again after the trap has sprung would be
@ the game requesting permission to spring it.
@ And the bag is NOT locked. An ambusher is an ordinary wild Pokemon and should
@ be catchable - FLAG_SYS_NO_BAG_USE is for battles the player must not win with
@ a ball, which this is not. Catching the thing that jumped you is the best
@ outcome this event has.
RogueDungeonFloor_EventScript_EventInjuredAmbush::
	msgbox RogueDungeonFloor_Text_InjuredAmbush, MSGBOX_DEFAULT
	setvar VAR_ROGUE_EVENT_SPENT, 1
	special RogueDungeon_EventInjuredAmbush
	dowildbattle
	release
	end

@ Shared tail. The NPC is NOT removed after being used: a floor is never
@ revisited - the stairs warp into a freshly generated one - so there is nothing
@ to persist and no flag to spend on it.
@
@ THAT REASONING HAS A HOLE, and it shipped: it is about coming BACK to a floor
@ and says nothing about talking to the same NPC twice while standing on it.
@ Every event was repeatable - infinite eggs, infinite heals, infinite money from
@ the fossil, and a trader that ratcheted a Pokemon up five levels a conversation.
@ VAR_ROGUE_EVENT_SPENT is the guard; see its note in constants/rogue_dungeon.h.
RogueDungeonFloor_EventScript_EventDone::
	release
	end

@ Reached when the floor's event has already been taken. Deliberately NOT silent:
@ an NPC who stops responding reads as broken, where one who says the moment has
@ passed reads as an event that happened.
RogueDungeonFloor_EventScript_EventSpent::
	msgbox RogueDungeonFloor_Text_EventSpent, MSGBOX_DEFAULT
	release
	end

RogueDungeonFloor_Text_EventSpent:
	.string "There is nothing more for you here.$"

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

RogueDungeonFloor_Text_InjuredApproach:
	.string "A {STR_VAR_1} is swaying dangerously.\p"
	.string "Approach it?$"

RogueDungeonFloor_Text_InjuredJoined:
	.string "The {STR_VAR_1} got to its feet.\p"
	.string "{STR_VAR_1} joined {PLAYER}!$"

RogueDungeonFloor_Text_InjuredNoRoom:
	.string "The {STR_VAR_1} looked at {PLAYER}'s\n"
	.string "full party, and settled back down.$"

RogueDungeonFloor_Text_InjuredAmbush:
	.string "The {STR_VAR_1} was not injured after all!$"

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
@ THE ORDER OF THE FIRST THREE CALLS IS THE WHOLE SCENE, and it is why the
@ vanish comes before the TM rather than after. The boss is not defeated here so
@ much as taken: they cry for help, they lose, and then they are simply gone.
@ Their TM hitting the floor is the evidence they were ever standing there, so
@ it has to land on an empty tile - dropped by nobody. Reordered, the TM is
@ handed over by a trainer who then disappears, which is a different and much
@ weaker beat.
RogueDungeonFloor_EventScript_BossDone::
	special RogueDungeon_OnBossDefeated
	goto_if_set FLAG_ROGUE_BOSS_REWARD_TAKEN, RogueDungeonFloor_EventScript_BossExit
	setflag FLAG_ROGUE_BOSS_REWARD_TAKEN
	call RogueDungeonFloor_EventScript_BossVanish
	call RogueDungeonFloor_EventScript_BossTM
	call RogueDungeonFloor_EventScript_BossAce
	goto RogueDungeonFloor_EventScript_BossExit

@ Swaps the boss object for their abandoned ace, in place. Declines on a mini
@ boss floor, so this is a no-op there.
@
@ THE OBJECT LEFT STANDING HERE IS THE ONLY WAY OFF THE FLOOR. A dungeon boss
@ arena draws no stairs - RogueDungeon_OnBossDefeated returns early on exactly
@ these floors - so the exit is a conversation, and deleting the boss without
@ putting something talkable in its place seals the run permanently. The ace
@ inherits this same script, which is what keeps the re-talk exit working: it
@ re-enters here, finds the reward flag already set, and falls straight through
@ to BossExit.
@ A specialvar rather than a special so the mini boss floors, which share this
@ whole script, skip the message as well as the swap. Nothing is abandoned
@ there, so "the trainer disappeared" would be a lie told over a grunt who is
@ still standing in front of the player.
RogueDungeonFloor_EventScript_BossVanish::
	specialvar VAR_RESULT, RogueDungeon_AbandonBossAce
	goto_if_eq VAR_RESULT, FALSE, RogueDungeonFloor_EventScript_BossVanishDone
	msgbox RogueDungeonFloor_Text_BossVanished, MSGBOX_DEFAULT
RogueDungeonFloor_EventScript_BossVanishDone::
	return

@ The stock game's Gym reward. Both specials decline on a mini boss floor, so
@ this whole block is a no-op there.
@
@ Two message boxes rather than one, and the pause between them is the point.
@ The drop line names no item and no trainer: the player has just been told the
@ boss vanished, and something falling out of that empty air is the beat. The
@ receipt line then names it, from the STR_VAR_1 that GiveBossTM's CopyItemName
@ leaves behind - a TM's name in this build IS "TM39", see src/data/items.h.
@
@ THAT CLOBBER IS LOAD BEARING IN BOTH DIRECTIONS. GiveBossTM overwrites the
@ trainer name the vanish message just used, which is fine because the vanish
@ message has already printed - and it is why PrepareBossAceOffer re-buffers the
@ trainer name rather than trusting the one BossVanish put there.
RogueDungeonFloor_EventScript_BossTM::
	specialvar VAR_RESULT, RogueDungeon_GiveBossTM
	goto_if_eq VAR_RESULT, FALSE, RogueDungeonFloor_EventScript_BossTMDone
	msgbox RogueDungeonFloor_Text_TMDropped, MSGBOX_DEFAULT
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
@ Naming the ace is what makes it a wager rather than a loss: STR_VAR_2 is
@ already buffered by RogueDungeon_PrepareBossAceOffer, so the player learns
@ exactly which Pokemon they walked past and can price the next one.
@
@ It is STR_VAR_2 and not STR_VAR_1 because the offer now names the TRAINER as
@ well as the species, and the trainer took the first slot. The Pokemon is still
@ standing on the floor when this prints - it is only the adoption that is
@ refused, not the abandonment.
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

@ Every dungeon boss says this and no ordinary trainer does. Deliberately not
@ per-boss: fourteen distinct pleas would read as fourteen characters, and the
@ whole point is that whatever is down here has reduced all of them to the same
@ one voice. The stammer is doing the work, so it stays exactly as short as it
@ is - a longer line gives the player time to read it as dialogue rather than
@ hear it as panic.
RogueDungeonFloor_Text_BossIntro::
	.string "H-H-Help me!$"

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

@ STR_VAR_1 is the TRAINER's name and STR_VAR_2 the species, both buffered by
@ RogueDungeon_PrepareBossAceOffer. Naming the trainer in the possessive while
@ they are the one thing no longer on the floor is what makes the abandonment
@ land - it is still their Nosepass, and they are gone.
@
@ EVERY LINE HERE IS TRAINER NEUTRAL and must stay that way: one string serves
@ all fourteen bosses, so nothing may assume a gender, a title or a party size.
@ The line breaks are placed for the LONGEST names the table can produce -
@ TATE&LIZA with a seven-letter species - not for the Roxanne case they were
@ drafted against.
RogueDungeonFloor_Text_OfferAce:
	.string "{STR_VAR_1}'s {STR_VAR_2} lets out\n"
	.string "a cry and looks at you pleadingly.\p"
	.string "Adopt {STR_VAR_2} into your party?$"

RogueDungeonFloor_Text_AceJoined:
	.string "{STR_VAR_2} joined your team!$"

RogueDungeonFloor_Text_AceNoRoom:
	.string "{STR_VAR_1}'s {STR_VAR_2} watched\n"
	.string "you go.\p"
	.string "There was no room for it.$"

@ Printed on a floor the boss has just vanished from, before the fanfare and the
@ receipt line. Names NO item and no trainer - something falls out of the empty
@ air the boss left, and only the next box says what it was.
RogueDungeonFloor_Text_TMDropped:
	.string "A TM fell to the floor\n"
	.string "with a thud.$"

@ STR_VAR_1 is the trainer name, buffered by RogueDungeon_AbandonBossAce.
RogueDungeonFloor_Text_BossVanished:
	.string "{STR_VAR_1} disappeared!$"

RogueDungeonFloor_Text_GotTM:
	.string "Received {STR_VAR_1}!$"

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

@ ============================================================ the second slate
@
@ Eight more events. NONE of them needed a map or a single pixel: each is a
@ stock overworld sprite, a msgbox, and a special. Where a draft asked for a
@ setting -- a dig site, an overgrown cabin -- the setting is the PROP object
@ beside the NPC, which sFloorEvents places for them.
@
@ Every one that costs the player something uses the same BUFFER-then-RESOLVE
@ split the gambler and pedlar established: the real figure is shown before the
@ yes/no, and the commit cannot disagree with what was offered.

@ ---------------------------------------------------------------- the scout
@
@ THE RUN'S FIRST SHINY, and the reason this is a trainer rather than the
@ drafted timed enclosure. The draft wanted a three-turn Safari minigame with
@ bait and rocks; what it was really asking for was a rare encounter the player
@ has to decide to chase, and a trainer pointing at one does that with no new
@ battle mode at all.
RogueDungeonFloor_EventScript_EventScout::
	lock
	faceplayer
	compare VAR_ROGUE_EVENT_SPENT, 1
	goto_if_eq RogueDungeonFloor_EventScript_EventSpent
	special RogueDungeon_EventScoutApproach
	msgbox RogueDungeonFloor_Text_ScoutOffer, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventScoutDeclined
	msgbox RogueDungeonFloor_Text_ScoutLead, MSGBOX_DEFAULT
	setvar VAR_ROGUE_EVENT_SPENT, 1
	special RogueDungeon_EventScoutBattle
	dowildbattle
	release
	end

RogueDungeonFloor_EventScript_EventScoutDeclined::
	msgbox RogueDungeonFloor_Text_ScoutDeclined, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ --------------------------------------------------------- the memorial shrine
@
@ Theme-restricted in sFloorEvents, because the grave the old woman stands at is
@ a PROP and not every theme has one worth standing at.
@
@ The commune branch is only a real choice because Revives are scarce -- see
@ DUNGEON_LOOT_REVIVE_WEIGHT. It was cut from the first slate for exactly that
@ reason and came back with the loot change.
RogueDungeonFloor_EventScript_EventShrine::
	lock
	faceplayer
	compare VAR_ROGUE_EVENT_SPENT, 1
	goto_if_eq RogueDungeonFloor_EventScript_EventSpent
	msgbox RogueDungeonFloor_Text_ShrineIntro, MSGBOX_DEFAULT
	multichoice 0, 0, MULTI_ROGUE_SHRINE, FALSE
	switch VAR_RESULT
	case 0, RogueDungeonFloor_EventScript_EventShrineCommune
	case 1, RogueDungeonFloor_EventScript_EventShrineOffering
	case 2, RogueDungeonFloor_EventScript_EventShrineDisturb
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventShrineCommune::
	special RogueDungeon_EventShrineCanRevive
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventShrineNothing
	msgbox RogueDungeonFloor_Text_ShrineCommuneOffer, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	special RogueDungeon_EventShrineRevive
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventShrineNothing
	setvar VAR_ROGUE_EVENT_SPENT, 1
	playfanfare MUS_HEAL
	waitfanfare
	msgbox RogueDungeonFloor_Text_ShrineCommuneDone, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ The counterplay charms were built for. Refuses to charge when there is nothing
@ to cleanse, which is the whole reason RogueCharm_ScriptCount exists.
RogueDungeonFloor_EventScript_EventShrineOffering::
	special RogueDungeon_EventShrineOffer
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventShrineNoCurse
	compare VAR_RESULT, 2
	goto_if_eq RogueDungeonFloor_EventScript_EventShrineBroke
	msgbox RogueDungeonFloor_Text_ShrineOfferingAsk, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	special RogueDungeon_EventShrineCleanse
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventShrineBroke
	setvar VAR_ROGUE_EVENT_SPENT, 1
	playfanfare MUS_HEAL
	waitfanfare
	msgbox RogueDungeonFloor_Text_ShrineOfferingDone, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventShrineDisturb::
	msgbox RogueDungeonFloor_Text_ShrineDisturb, MSGBOX_DEFAULT
	setvar VAR_ROGUE_EVENT_SPENT, 1
	special RogueDungeon_EventShrineGhost
	dowildbattle
	release
	end

RogueDungeonFloor_EventScript_EventShrineNothing::
	msgbox RogueDungeonFloor_Text_ShrineNothing, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventShrineBroke::
	msgbox RogueDungeonFloor_Text_ShrineBroke, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventShrineNoCurse::
	msgbox RogueDungeonFloor_Text_ShrineNoCurse, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ ------------------------------------------------------------- the herbalist
@
@ Stands beside a berry tree, which is her whole setting. The drafted "fully
@ revive all fainted Pokemon" is HealPlayerParty, which already does that -- so
@ the only new thing here is the price, and the price is a charm.
RogueDungeonFloor_EventScript_EventHerbalist::
	lock
	faceplayer
	compare VAR_ROGUE_EVENT_SPENT, 1
	goto_if_eq RogueDungeonFloor_EventScript_EventSpent
	msgbox RogueDungeonFloor_Text_HerbIntro, MSGBOX_DEFAULT
	multichoice 0, 0, MULTI_ROGUE_HERBALIST, FALSE
	switch VAR_RESULT
	case 0, RogueDungeonFloor_EventScript_EventHerbBrew
	case 1, RogueDungeonFloor_EventScript_EventHerbForage
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventHerbBrew::
	msgbox RogueDungeonFloor_Text_HerbBrewOffer, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	setvar VAR_ROGUE_EVENT_SPENT, 1
	special RogueDungeon_EventHerbBrew
	playfanfare MUS_HEAL
	waitfanfare
	msgbox RogueDungeonFloor_Text_HerbBrewDone, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventHerbForage::
	special RogueDungeon_EventHerbForage
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventHerbNoRoom
	setvar VAR_ROGUE_EVENT_SPENT, 1
	playfanfare MUS_OBTAIN_ITEM
	waitfanfare
	msgbox RogueDungeonFloor_Text_HerbForageDone, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventHerbNoRoom::
	msgbox RogueDungeonFloor_Text_HerbNoRoom, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ ------------------------------------------------------------ the fossil dig
@
@ A hiker beside a breakable rock. The drafted "underground dig site with two
@ fossils embedded in the rock wall" is exactly that, and needed no map.
RogueDungeonFloor_EventScript_EventFossil::
	lock
	faceplayer
	compare VAR_ROGUE_EVENT_SPENT, 1
	goto_if_eq RogueDungeonFloor_EventScript_EventSpent
	special RogueDungeon_EventFossilOffer
	msgbox RogueDungeonFloor_Text_FossilIntro, MSGBOX_DEFAULT
	multichoice 0, 0, MULTI_ROGUE_FOSSIL, FALSE
	switch VAR_RESULT
	case 0, RogueDungeonFloor_EventScript_EventFossilTake
	case 1, RogueDungeonFloor_EventScript_EventFossilSell
	case 2, RogueDungeonFloor_EventScript_EventFossilStudy
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventFossilTake::
	msgbox RogueDungeonFloor_Text_FossilTakeAsk, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	special RogueDungeon_EventFossilTake
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventFossilNoRoom
	setvar VAR_ROGUE_EVENT_SPENT, 1
	playfanfare MUS_OBTAIN_ITEM
	waitfanfare
	msgbox RogueDungeonFloor_Text_FossilTaken, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventFossilSell::
	msgbox RogueDungeonFloor_Text_FossilSellAsk, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	setvar VAR_ROGUE_EVENT_SPENT, 1
	special RogueDungeon_EventFossilSell
	playfanfare MUS_OBTAIN_ITEM
	waitfanfare
	msgbox RogueDungeonFloor_Text_FossilSold, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventFossilStudy::
	setvar VAR_ROGUE_EVENT_SPENT, 1
	special RogueDungeon_EventFossilStudy
	playfanfare MUS_OBTAIN_ITEM
	waitfanfare
	msgbox RogueDungeonFloor_Text_FossilStudied, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventFossilNoRoom::
	msgbox RogueDungeonFloor_Text_FossilNoRoom, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ ------------------------------------------------------- the shady move tutor
@
@ ChoosePartyMon, NOT the move tutor's own selection flow. That one filters to
@ Pokemon which CAN learn the move, and teaching a move the Pokemon could never
@ have learned is the entire point of this event.
RogueDungeonFloor_EventScript_EventTutor::
	lock
	faceplayer
	compare VAR_ROGUE_EVENT_SPENT, 1
	goto_if_eq RogueDungeonFloor_EventScript_EventSpent
	msgbox RogueDungeonFloor_Text_TutorIntro, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	special ChoosePartyMon
	waitstate
	compare VAR_0x8004, PARTY_NOTHING_CHOSEN
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	special RogueDungeon_EventTutorOffer
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventTutorRefused
	compare VAR_RESULT, 2
	goto_if_eq RogueDungeonFloor_EventScript_EventTutorOverwrite
	msgbox RogueDungeonFloor_Text_TutorOfferFree, MSGBOX_YESNO
	goto RogueDungeonFloor_EventScript_EventTutorConfirm

@ The move being LOST is named before the yes/no. Overwriting a move without
@ saying which would be the least fair thing in the run.
RogueDungeonFloor_EventScript_EventTutorOverwrite::
	msgbox RogueDungeonFloor_Text_TutorOfferOverwrite, MSGBOX_YESNO
	goto RogueDungeonFloor_EventScript_EventTutorConfirm

RogueDungeonFloor_EventScript_EventTutorConfirm::
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	setvar VAR_ROGUE_EVENT_SPENT, 1
	special RogueDungeon_EventTutorTeach
	playfanfare MUS_LEVEL_UP
	waitfanfare
	msgbox RogueDungeonFloor_Text_TutorTaught, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventTutorRefused::
	msgbox RogueDungeonFloor_Text_TutorRefused, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ ------------------------------------------------- the volatile evolution crystal
@
@ ULTRA RARE -- see its weight in sFloorEvents. It is a palette variant of the
@ breakable rock rather than new art, which is what made "a giant crackling
@ geode" cost nothing.
RogueDungeonFloor_EventScript_EventCrystal::
	lock
	compare VAR_ROGUE_EVENT_SPENT, 1
	goto_if_eq RogueDungeonFloor_EventScript_EventSpent
	msgbox RogueDungeonFloor_Text_CrystalIntro, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	special ChoosePartyMon
	waitstate
	compare VAR_0x8004, PARTY_NOTHING_CHOSEN
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	special RogueDungeon_EventCrystalOffer
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventCrystalCannot
	msgbox RogueDungeonFloor_Text_CrystalOffer, MSGBOX_YESNO
	compare VAR_RESULT, NO
	goto_if_eq RogueDungeonFloor_EventScript_EventDone
	special RogueDungeon_EventCrystalEvolve
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventCrystalCannot
	setvar VAR_ROGUE_EVENT_SPENT, 1
	playfanfare MUS_EVOLVED
	waitfanfare
	msgbox RogueDungeonFloor_Text_CrystalEvolved, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventCrystalCannot::
	msgbox RogueDungeonFloor_Text_CrystalCannot, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ --------------------------------------------------------- the Ditto item ball
@
@ A trap that looks exactly like a reward, which is why it wears the item ball
@ sprite and says nothing until it is touched.
@
@ A DOUBLE battle: Ditto's own Transform copies the player's lead, so the trap
@ fights back with the team that walked into it and no code makes that happen.
RogueDungeonFloor_EventScript_EventDittoBall::
	lock
	compare VAR_ROGUE_EVENT_SPENT, 1
	goto_if_eq RogueDungeonFloor_EventScript_EventSpent
	msgbox RogueDungeonFloor_Text_DittoSpring, MSGBOX_DEFAULT
	setvar VAR_ROGUE_EVENT_SPENT, 1
	special RogueDungeon_EventDittoAmbush
	dowildbattle
	release
	end

@ ------------------------------------------------------------ the enraged totem
@
@ Wears the rolled species' own overworld sprite, the same way the injured
@ Pokemon does. NO LEGENDARIES: the pool it draws from carries none.
@
@ The stat boost is settotemboost, the engine's OWN totem-battle feature -- it
@ brings its own animation and its own message, and reimplementing it in C would
@ have been a second copy of something already in the build.
RogueDungeonFloor_EventScript_EventTotem::
	lock
	faceplayer
	compare VAR_ROGUE_EVENT_SPENT, 1
	goto_if_eq RogueDungeonFloor_EventScript_EventSpent
	special RogueDungeon_EventTotemApproach
	msgbox RogueDungeonFloor_Text_TotemIntro, MSGBOX_DEFAULT
	multichoice 0, 0, MULTI_ROGUE_TOTEM, FALSE
	switch VAR_RESULT
	case 0, RogueDungeonFloor_EventScript_EventTotemFight
	case 1, RogueDungeonFloor_EventScript_EventTotemSneak
	case 2, RogueDungeonFloor_EventScript_EventTotemFeed
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventTotemFight::
	msgbox RogueDungeonFloor_Text_TotemFight, MSGBOX_DEFAULT
	setvar VAR_ROGUE_EVENT_SPENT, 1
	special RogueDungeon_EventTotemBattle
	settotemboost B_POSITION_OPPONENT_LEFT, 1, 1, 1, 1, 1
	dowildbattle
	release
	end

RogueDungeonFloor_EventScript_EventTotemSneak::
	special RogueDungeon_EventTotemSneak
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventTotemBroke
	setvar VAR_ROGUE_EVENT_SPENT, 1
	msgbox RogueDungeonFloor_Text_TotemSneak, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventTotemFeed::
	special RogueDungeon_EventTotemFeed
	compare VAR_RESULT, 0
	goto_if_eq RogueDungeonFloor_EventScript_EventTotemNoFood
	setvar VAR_ROGUE_EVENT_SPENT, 1
	msgbox RogueDungeonFloor_Text_TotemFeed, MSGBOX_DEFAULT
	dowildbattle
	release
	end

RogueDungeonFloor_EventScript_EventTotemBroke::
	msgbox RogueDungeonFloor_Text_TotemBroke, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

RogueDungeonFloor_EventScript_EventTotemNoFood::
	msgbox RogueDungeonFloor_Text_TotemNoFood, MSGBOX_DEFAULT
	goto RogueDungeonFloor_EventScript_EventDone

@ ------------------------------------------------------------------------ text

RogueDungeonFloor_Text_ScoutOffer:
	.string "I have been tracking a {STR_VAR_1}\n"
	.string "through these halls for days.\p"
	.string "It is not like the others. Will you\n"
	.string "help me corner it?$"

RogueDungeonFloor_Text_ScoutLead:
	.string "There! Do you see how it shines?$"

RogueDungeonFloor_Text_ScoutDeclined:
	.string "Then I will keep looking alone.$"

RogueDungeonFloor_Text_ShrineIntro:
	.string "An old woman tends a grave here.\p"
	.string "The flames above it burn purple,\n"
	.string "and they do not flicker.$"

RogueDungeonFloor_Text_ShrineCommuneOffer:
	.string "The spirits will trade, she says.\p"
	.string "Your lead's strength for one who\n"
	.string "has already fallen. Agreed?$"

RogueDungeonFloor_Text_ShrineCommuneDone:
	.string "{STR_VAR_1} drew breath again.$"

RogueDungeonFloor_Text_ShrineNothing:
	.string "The flames stay still.\p"
	.string "There is nothing here to trade for.$"

RogueDungeonFloor_Text_ShrineOfferingAsk:
	.string "Leave an offering, she says, and\n"
	.string "what clings to you may let go.\p"
	.string "{STR_VAR_2} of them cling to you.\n"
	.string "The offering is {STR_VAR_1}. Pay it?$"

RogueDungeonFloor_Text_ShrineBroke:
	.string "She looks at your empty hands and\n"
	.string "says nothing at all.$"

RogueDungeonFloor_Text_ShrineOfferingDone:
	.string "The purple flames guttered out,\n"
	.string "and the weight went with them.$"

RogueDungeonFloor_Text_ShrineNoCurse:
	.string "She looks you over and shakes\n"
	.string "her head. Nothing clings to you.$"

RogueDungeonFloor_Text_ShrineDisturb:
	.string "You reach into the flames.\p"
	.string "Something reaches back!$"

RogueDungeonFloor_Text_HerbIntro:
	.string "An old woman is stripping the tree\n"
	.string "beside her with practised hands.$"

RogueDungeonFloor_Text_HerbBrewOffer:
	.string "She offers a bitter brew.\p"
	.string "It will wake every one of them, she\n"
	.string "says, but they will be slow for a\l"
	.string "long while after. Drink it?$"

RogueDungeonFloor_Text_HerbBrewDone:
	.string "Your team stirred and stood.$"

RogueDungeonFloor_Text_HerbForageDone:
	.string "You gathered {STR_VAR_2} {STR_VAR_1}!$"

RogueDungeonFloor_Text_HerbNoRoom:
	.string "You had nowhere to put them.$"

RogueDungeonFloor_Text_FossilIntro:
	.string "A hiker is working at the rock face.\p"
	.string "Something old is set into it, and it\n"
	.string "is not stone.$"

RogueDungeonFloor_Text_FossilTakeAsk:
	.string "He can free it, he says, but the\n"
	.string "rock is not the only thing set here.\p"
	.string "Take the fossil anyway?$"

RogueDungeonFloor_Text_FossilTaken:
	.string "{STR_VAR_1} came loose from the rock!$"

RogueDungeonFloor_Text_FossilNoRoom:
	.string "Your team was full.\p"
	.string "He set it back into the rock.$"

RogueDungeonFloor_Text_FossilSellAsk:
	.string "A curator would pay {STR_VAR_1} for\n"
	.string "this, he says. Sell it?$"

RogueDungeonFloor_Text_FossilSold:
	.string "He counted it out on the spot.$"

RogueDungeonFloor_Text_FossilStudied:
	.string "You studied the carvings around\n"
	.string "the fossil for a long while.\p"
	.string "Your team learned something.$"

RogueDungeonFloor_Text_TutorIntro:
	.string "Someone is leaning against the wall\n"
	.string "with their collar up.\p"
	.string "They know techniques nothing here\n"
	.string "should know. Interested?$"

RogueDungeonFloor_Text_TutorOfferFree:
	.string "{STR_VAR_2} can be taught {STR_VAR_1}.\p"
	.string "It will leave a mark. Go ahead?$"

RogueDungeonFloor_Text_TutorOfferOverwrite:
	.string "{STR_VAR_2} can be taught {STR_VAR_1},\n"
	.string "but it will forget {STR_VAR_3}.\p"
	.string "It will leave a mark, too.\n"
	.string "Go ahead?$"

RogueDungeonFloor_Text_TutorTaught:
	.string "{STR_VAR_2} learned {STR_VAR_1}!$"

RogueDungeonFloor_Text_TutorRefused:
	.string "They look that one over and\n"
	.string "wave you away.$"

RogueDungeonFloor_Text_CrystalIntro:
	.string "A geode the size of a door stands\n"
	.string "here, crackling.\p"
	.string "Hold something up to it?$"

RogueDungeonFloor_Text_CrystalOffer:
	.string "The light bends around {STR_VAR_1}.\p"
	.string "It could become {STR_VAR_2} right\n"
	.string "now, ready or not. Let it?$"

RogueDungeonFloor_Text_CrystalEvolved:
	.string "{STR_VAR_1} became {STR_VAR_2}!\p"
	.string "Something in it will not settle.$"

RogueDungeonFloor_Text_CrystalCannot:
	.string "The light passed straight through.\n"
	.string "Nothing happened.$"

RogueDungeonFloor_Text_DittoSpring:
	.string "You reach for the ball.\p"
	.string "It reaches back!$"

RogueDungeonFloor_Text_TotemIntro:
	.string "A {STR_VAR_1} blocks the way, far\n"
	.string "larger than it has any right to be.\p"
	.string "Something glitters behind it.$"

RogueDungeonFloor_Text_TotemFight:
	.string "It draws itself up!$"

RogueDungeonFloor_Text_TotemSneak:
	.string "You scatter {STR_VAR_2} worth of\n"
	.string "supplies and slip past in the fuss.$"

RogueDungeonFloor_Text_TotemBroke:
	.string "You have nothing to distract it with.$"

RogueDungeonFloor_Text_TotemFeed:
	.string "You set the {STR_VAR_2} down and\n"
	.string "step back.\p"
	.string "It calms, and looks at you plainly.$"

RogueDungeonFloor_Text_TotemNoFood:
	.string "You have nothing it would want.$"

@ The dungeon's berry trees. Vanilla's BerryTreeScript everywhere except for one
@ check in front of it.
@
@ A ROTTEN TREE IS A DUD AND SAYS SO. It used to yield 1 berry against a good
@ tree's 6, which is invisible unless the player is counting - the mechanic was
@ working perfectly and had never once been noticed. Now it gives nothing and
@ tells you why, which is the difference between a rule and a rumour.
@
@ The tree is left standing rather than removed. A floor is never revisited, so
@ there is nothing to gain by clearing it, and a withered tree the player can
@ walk back past is a better memory of the floor than an empty tile.
RogueDungeonFloor_EventScript_BerryTree::
	lockall
	special RogueDungeon_EventBerryIsRotten
	compare VAR_RESULT, 1
	goto_if_eq RogueDungeonFloor_EventScript_BerryTreeRotten
	releaseall
	goto BerryTreeScript
	end

RogueDungeonFloor_EventScript_BerryTreeRotten::
	msgbox RogueDungeonFloor_Text_BerryRotten, MSGBOX_DEFAULT
	releaseall
	end

RogueDungeonFloor_Text_BerryRotten:
	.string "The berries on this tree are black\n"
	.string "and shrivelled.\p"
	.string "There is nothing worth taking.$"
