@ The single exit is MAP_DYNAMIC, because which of the dungeon maps it leads to
@ depends on the next dungeon's theme and a warp event cannot be conditional.
@
@ ON_LOAD rather than setting it on the way in, so it is recomputed however the
@ player got here - including loading a save made inside the rest stop, where
@ the warp the player is about to step on was last written by a different run.
RogueRestStop_MapScripts::
	map_script MAP_SCRIPT_ON_LOAD, RogueRestStop_OnLoad
	map_script MAP_SCRIPT_ON_TRANSITION, RogueRestStop_OnTransition
	.byte 0

RogueRestStop_OnLoad::
	special RogueDungeon_SetRestStopExit
	end

@ ON_TRANSITION, not ON_LOAD: the Unown seeder edits object event TEMPLATES, and
@ that only takes effect in the window after the map's templates are copied out
@ of ROM and before anything spawns from them. It is the same window
@ setobjectxyperm writes in.
RogueRestStop_OnTransition::
	special RogueDungeon_SeedRestStopUnown
	end

@ ---------------------------------------------------------------- the keeper
@
@ NOT Common_EventScript_PkmnCenterNurse, which this used while the rest stop
@ was a spare Pokemon Center. That script ends in FLDEFF_POKECENTER_HEAL, and
@ FldEff_PokecenterHeal places its glowing balls at SCREEN coordinates (93,36)
@ and its monitor sprite at (124,24) - absolute values chosen to land on the
@ healing machine art in the Center's layout. In a rock chamber it would draw a
@ Pokemon Center monitor hanging in mid-air over the floor.
@
@ So the heal is presented rather than animated: fade out, HealPlayerParty, fade
@ back, the healing fanfare. The same effect on the party, and nothing on screen
@ that has to line up with furniture this room does not have.
RogueRestStop_EventScript_Nurse::
	lock
	faceplayer
	msgbox RogueRestStop_Text_KeeperOffer, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, RogueRestStop_EventScript_KeeperDecline
	message RogueRestStop_Text_KeeperResting
	waitmessage
	closemessage
	fadescreen FADE_TO_BLACK
	special HealPlayerParty
	callnative UpdateFollowingPokemon
	fadescreen FADE_FROM_BLACK
	playfanfare MUS_HEAL
	waitfanfare
	msgbox RogueRestStop_Text_KeeperDone, MSGBOX_DEFAULT
	release
	end

RogueRestStop_EventScript_KeeperDecline::
	msgbox RogueRestStop_Text_KeeperNoThanks, MSGBOX_DEFAULT
	release
	end

@ ---------------------------------------------------------------- the merchant
@
@ Stock deepens with the run. The bands are the boundaries the player can
@ actually feel - through the gyms, into the Elite Four at floor 81, and the
@ finale at 106 - rather than round numbers, so the shelf changes when the run
@ does. VAR_ROGUE_DUNGEON_FLOOR is the floor about to be ENTERED, because the
@ boss script advances it before warping here.
RogueRestStop_EventScript_Merchant::
	lock
	faceplayer
	message RogueRestStop_Text_MerchantGreet
	waitmessage
	goto_if_ge VAR_ROGUE_DUNGEON_FLOOR, 106, RogueRestStop_EventScript_MartDeep
	goto_if_ge VAR_ROGUE_DUNGEON_FLOOR, 81, RogueRestStop_EventScript_MartMid
	goto RogueRestStop_EventScript_MartShallow
	end

RogueRestStop_EventScript_MartShallow::
	pokemart RogueRestStop_Pokemart_Shallow
	msgbox RogueRestStop_Text_MerchantFarewell, MSGBOX_DEFAULT
	release
	end

RogueRestStop_EventScript_MartMid::
	pokemart RogueRestStop_Pokemart_Mid
	msgbox RogueRestStop_Text_MerchantFarewell, MSGBOX_DEFAULT
	release
	end

RogueRestStop_EventScript_MartDeep::
	pokemart RogueRestStop_Pokemart_Deep
	msgbox RogueRestStop_Text_MerchantFarewell, MSGBOX_DEFAULT
	release
	end

	.align 2
RogueRestStop_Pokemart_Shallow:
	.2byte ITEM_POKE_BALL
	.2byte ITEM_GREAT_BALL
	.2byte ITEM_POTION
	.2byte ITEM_SUPER_POTION
	.2byte ITEM_ANTIDOTE
	.2byte ITEM_PARALYZE_HEAL
	.2byte ITEM_AWAKENING
	@ NOT ITEM_ESCAPE_ROPE. RogueDungeonFloor sets allow_escaping true and
	@ nothing in rogue_dungeon.c ever calls SetEscapeWarp, so a rope would warp
	@ the player to a stale destination and out of the run entirely. Repel is
	@ the item that actually helps here anyway - a cave floor carries its
	@ encounters everywhere, so reaching the stairs is the thing being bought.
	.2byte ITEM_REPEL
	pokemartlistend

	.align 2
RogueRestStop_Pokemart_Mid:
	.2byte ITEM_GREAT_BALL
	.2byte ITEM_ULTRA_BALL
	.2byte ITEM_SUPER_POTION
	.2byte ITEM_HYPER_POTION
	.2byte ITEM_FULL_HEAL
	.2byte ITEM_REVIVE
	.2byte ITEM_X_ATTACK
	.2byte ITEM_X_DEFENSE
	pokemartlistend

	.align 2
RogueRestStop_Pokemart_Deep:
	.2byte ITEM_ULTRA_BALL
	.2byte ITEM_HYPER_POTION
	.2byte ITEM_MAX_POTION
	.2byte ITEM_FULL_RESTORE
	.2byte ITEM_FULL_HEAL
	.2byte ITEM_REVIVE
	.2byte ITEM_MAX_REVIVE
	pokemartlistend

@ ---------------------------------------------------------------- the archivist
@
@ Box access with no PC terminal, because gTileset_Cave has no PC art and this
@ room is cut rock. ShowPokemonStorageSystemPC is the same special the vanilla
@ PC's DEPOSIT/WITHDRAW branch calls, so this is the real storage system rather
@ than a reduced one.
RogueRestStop_EventScript_Storage::
	lock
	faceplayer
	msgbox RogueRestStop_Text_ArchivistOffer, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, RogueRestStop_EventScript_ArchivistDecline
	playse SE_PC_LOGIN
	msgbox RogueRestStop_Text_ArchivistOpened, MSGBOX_DEFAULT
	@ no waitstate here: specials.inc declares this one waitstate=1, so the
	@ macro emits it, and an explicit one is warned about and dropped.
	special ShowPokemonStorageSystemPC
	release
	end

RogueRestStop_EventScript_ArchivistDecline::
	msgbox RogueRestStop_Text_ArchivistNoThanks, MSGBOX_DEFAULT
	release
	end

@ ---------------------------------------------------------------- text

RogueRestStop_Text_KeeperOffer:
	.string "This deep, the rock holds still.\p"
	.string "Rest your POKéMON here?$"

RogueRestStop_Text_KeeperResting:
	.string "Sit. It will not take long.$"

RogueRestStop_Text_KeeperDone:
	.string "They are well again.\p"
	.string "The way down is behind me.\n"
	.string "It is always behind me.$"

RogueRestStop_Text_KeeperNoThanks:
	.string "Then rest a while anyway.$"

RogueRestStop_Text_MerchantGreet:
	.string "You came up from below?\p"
	.string "Nobody else does.$"

RogueRestStop_Text_MerchantFarewell:
	.string "Spend it. You cannot take it\n"
	.string "with you.$"

RogueRestStop_Text_ArchivistOffer:
	.string "I keep what will not fit in a party.\p"
	.string "Shall I open the record?$"

RogueRestStop_Text_ArchivistOpened:
	.string "The storage system opened.$"

RogueRestStop_Text_ArchivistNoThanks:
	.string "It will keep.$"

@ Shared by all six slots. They have nothing to say - the point is that they are
@ looking at you, and that they are the only thing in the run that does.
RogueRestStop_EventScript_Unown::
	msgbox RogueRestStop_Text_Unown, MSGBOX_NPC
	end

RogueRestStop_Text_Unown:
	.string "It does not move.\p"
	.string "The eye follows you anyway.$"
