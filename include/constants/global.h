#ifndef GUARD_CONSTANTS_GLOBAL_H
#define GUARD_CONSTANTS_GLOBAL_H

// You can use the ENABLED_ON_RELEASE and DISABLED_ON_RELEASE macros to
// control whether a feature is enabled or disabled when making a release build.
//
// For example, the overworld debug menu is enabled by default, but when using
// `make release`, it will be automatically disabled.
//
// #define DEBUG_OVERWORLD_MENU DISABLED_ON_RELEASE
#ifdef RELEASE
#define ENABLED_ON_RELEASE TRUE
#define DISABLED_ON_RELEASE FALSE
#else
#define ENABLED_ON_RELEASE FALSE
#define DISABLED_ON_RELEASE TRUE
#endif

#include "config/ai.h"
#include "config/battle.h"
#include "config/caps.h"
#include "config/contest.h"
#include "config/debug.h"
#include "config/dexnav.h"
#include "config/follower_npc.h"
#include "config/general.h"
#include "config/item.h"
#include "config/map_preview_screen.h"
#include "config/overworld.h"
#include "config/pokemon.h"
#include "config/summary_screen.h"
#include "config/wild_encounter.h"
#include "config/crafting.h"

// Invalid Versions show as "----------" in Gen 4 and Gen 5's summary screen.
// In Gens 6 and 7, invalid versions instead show "a distant land" in the summary screen.
// In Gen 4 only, migrated Pokémon with Diamond, Pearl, or Platinum's ID show as "----------".
// Gen 5 and up read Diamond, Pearl, or Platinum's ID as "Sinnoh".
// In Gen 4 and up, migrated Pokémon with HeartGold or SoulSilver's ID show the otherwise unused "Johto" string.
enum __attribute__((packed)) GameVersion
{
    VERSION_SAPPHIRE = 1,
    VERSION_RUBY = 2,
    VERSION_EMERALD = 3,
    VERSION_FIRE_RED = 4,
    VERSION_LEAF_GREEN = 5,
    VERSION_HEART_GOLD = 7,
    VERSION_SOUL_SILVER = 8,
    VERSION_DIAMOND = 10,
    VERSION_PEARL = 11,
    VERSION_PLATINUM = 12,
    VERSION_GAMECUBE = 15,
    NUM_VERSIONS = VERSION_GAMECUBE,
};

enum Language
{
    LANGUAGE_JAPANESE = 1,
    LANGUAGE_ENGLISH = 2,
    LANGUAGE_FRENCH = 3,
    LANGUAGE_ITALIAN = 4,
    LANGUAGE_GERMAN = 5,
    LANGUAGE_KOREAN = 6, // 6 goes unused but the theory is it was meant to be Korean,
    LANGUAGE_SPANISH = 7,
    NUM_LANGUAGES = LANGUAGE_SPANISH,
};

#ifdef FIRERED
    #define GAME_VERSION (VERSION_FIRE_RED)
    #define IS_FRLG 1
#else
    #ifdef LEAFGREEN
    #define GAME_VERSION (VERSION_LEAF_GREEN)
    #define IS_FRLG 1
    #else
    #define GAME_VERSION (VERSION_EMERALD)
    #define IS_FRLG 0
    #endif
#endif
#define GAME_LANGUAGE (LANGUAGE_ENGLISH)

// party sizes
#define PARTY_SIZE 6
#define MULTI_PARTY_SIZE (PARTY_SIZE / 2)
#define FRONTIER_PARTY_SIZE         3
#define FRONTIER_DOUBLES_PARTY_SIZE 4
#define FRONTIER_MULTI_PARTY_SIZE   2
#define MAX_FRONTIER_PARTY_SIZE    (max(FRONTIER_PARTY_SIZE,        \
                                    max(FRONTIER_DOUBLES_PARTY_SIZE,\
                                        FRONTIER_MULTI_PARTY_SIZE)))
#define UNION_ROOM_PARTY_SIZE       2

// capacities of various saveblock objects
#define DAYCARE_MON_COUNT 2
#define POKEBLOCKS_COUNT 40
// 28, UP FROM VANILLA'S 16. The dungeon floor declares 24 object events and was
// over the live ceiling for a long time, so the furthest object from the player
// silently did not spawn - including, sometimes, the one thing on a floor that
// asks the player a question. 24 declared plus the player and a follower is 26,
// so this leaves two spare rather than sitting exactly on the line.
//
// This sizes SaveBlock1.objectEvents (36 bytes each) and gObjectEvents, so it
// costs 288 bytes of each. SaveBlock1 is capped by its save sectors and had 124
// bytes spare, which is why four FREE_* switches in config/save.h are on to pay
// for it - see the note there. Overflow is a BUILD ERROR, not a corrupt save:
// src/save.c asserts sizeof(struct SaveBlock1) against the sector budget.
//
// Cheap because sprite VRAM and OW palettes are keyed on GRAPHICS, not on object
// count - LoadSheetGraphicsInfo shares one sheet across every object with the
// same graphicsId, so eight item balls cost one allocation. The dungeon is
// MAP_TYPE_UNDERGROUND, so CurrentMapHasShadows() is false there and an object
// costs ONE sprite rather than two against MAX_SPRITES 64.
#define OBJECT_EVENTS_COUNT 28
#define MAIL_COUNT (10 + PARTY_SIZE)
#define SECRET_BASES_COUNT 20
#define POKE_NEWS_COUNT 16
#define PC_ITEMS_COUNT 50
#define OBJECT_EVENT_TEMPLATES_COUNT 64
#define DECOR_MAX_SECRET_BASE 16
#define DECOR_MAX_PLAYERS_HOUSE 12
#define APPRENTICE_COUNT 4
#define APPRENTICE_MAX_QUESTIONS 9
#define MAX_REMATCH_ENTRIES 100 // only REMATCH_TABLE_ENTRIES (78) are used
#define NUM_CONTEST_WINNERS 13
#define UNION_ROOM_KB_ROW_COUNT 10
#define SAVED_TRENDS_COUNT 5
#define PYRAMID_BAG_ITEMS_COUNT 10
#define ROAMER_COUNT 1 // Number of maximum concurrent active roamers

// Bag constants
#define BAG_ITEMS_COUNT 30
#define BAG_KEYITEMS_COUNT 30
#define BAG_POKEBALLS_COUNT 16
#define BAG_TMHM_COUNT 64
#define BAG_BERRIES_COUNT 46

//tx_registered_items_menu
#define REGISTERED_ITEMS_MAX 10

// Number of facilities for Ranking Hall.
// 7 facilities for single mode + tower double mode + tower multi mode.
// Excludes link modes. See RANKING_HALL_* in include/constants/battle_frontier.h
#define HALL_FACILITIES_COUNT 9
// Received via record mixing, 1 for each player other than yourself
#define HALL_RECORDS_COUNT 3

// Battle Frontier level modes.
enum FrontierLevelMode
{
    FRONTIER_LVL_50,
    FRONTIER_LVL_OPEN,
    FRONTIER_LVL_TENT, // Special usage for indicating Battle Tent
    FRONTIER_LVL_MODE_COUNT = FRONTIER_LVL_TENT,
};

#define TRAINER_ID_LENGTH 4
#define MAX_MON_MOVES 4
#define ALL_MOVES_MASK ((1 << MAX_MON_MOVES) - 1)

#define CONTESTANT_COUNT 4

enum ContestCategories
{
    CONTEST_CATEGORY_COOL,
    CONTEST_CATEGORY_BEAUTIFUL,
    CONTEST_CATEGORY_BEAUTY = CONTEST_CATEGORY_BEAUTIFUL,
    CONTEST_CATEGORY_CUTE,
    CONTEST_CATEGORY_CLEVER,
    CONTEST_CATEGORY_SMART = CONTEST_CATEGORY_CLEVER,
    CONTEST_CATEGORY_TOUGH,
    CONTEST_CATEGORIES_COUNT
};

// string lengths
#define ITEM_NAME_LENGTH 20
#define ITEM_NAME_PLURAL_LENGTH ITEM_NAME_LENGTH + 2 // 2 is used for the instance where a word's suffix becomes y->ies
#define POKEMON_NAME_LENGTH 12
#define VANILLA_POKEMON_NAME_LENGTH 10
#define POKEMON_NAME_BUFFER_SIZE max(20, POKEMON_NAME_LENGTH + 1) // Frequently used buffer size. Larger than necessary
#define PLAYER_NAME_LENGTH 7
#define MAIL_WORDS_COUNT 9
#define EASY_CHAT_BATTLE_WORDS_COUNT 6
#define MOVE_NAME_LENGTH 16
#define NUM_QUESTIONNAIRE_WORDS 4
#define QUIZ_QUESTION_LEN 9
#define WONDER_CARD_TEXT_LENGTH 40
#define WONDER_NEWS_TEXT_LENGTH 40
#define WONDER_CARD_BODY_TEXT_LINES 4
#define WONDER_NEWS_BODY_TEXT_LINES 10
#define TYPE_NAME_LENGTH 8
#define ABILITY_NAME_LENGTH 16
#define TRAINER_NAME_LENGTH 10
#define CODE_NAME_LENGTH 11

#define MAX_STAMP_CARD_STAMPS 7

// enum Gender is TWO THINGS in this tree and it is worth knowing which before
// touching either. It types the player-facing graphics signatures, and its
// MALE/FEMALE tokens are ALSO what struct Trainer's `gender:1` bitfield holds.
// A Pokemon's gender is a different scheme entirely (MON_MALE/MON_FEMALE/
// MON_GENDERLESS, 0x00/0xFE/0xFF, in constants/pokemon.h).
//
// SO THIS ENUM IS NOT WHERE A THIRD PLAYER GENDER GOES. Widening it to three
// would hand the third value to struct Trainer.gender, which is one bit wide
// and would truncate it silently - a trainer that is neither male nor female
// becoming male, with a clean build. The two enums below exist for that reason.
enum Gender
{
    MALE,
    FEMALE,
    GENDER_COUNT,
};

// WHO THE PLAYER IS. Drives text - the save-select screen, anything that would
// otherwise say BOY or GIRL. Never indexes art.
enum PlayerGender
{
    GENDER_MASCULINE,
    GENDER_FEMININE,
    GENDER_ANDROGYNOUS,
    PLAYER_GENDER_COUNT,
};

// WHAT THE PLAYER LOOKS LIKE. Indexes every art table - avatar graphics,
// trainer pics, head icons, mugshot palettes. Stored in
// gSaveBlock2Ptr->playerGender, which is why both are pinned to the values
// that field has always held: every existing [MALE] and [FEMALE] designator in
// a player art table still means exactly what it meant, and every save written
// before this still reads correctly.
//
// IT IS A SEPARATE AXIS FROM IDENTITY ON PURPOSE. An androgynous player may
// present as either look, which is the whole point - the alternative is
// telling somebody their identity dictates their sprite.
//
// THERE WAS A THIRD LOOK HERE AND REMOVING IT IS THE POINT, not a retreat.
// PLAYER_LOOK_ANDRO carried Kris's art and existed only because identity had
// nowhere else to live; once PlayerGender above became its own field, a third
// LOOK meant "the enby sprite", which is the exact sentence these two enums
// were split apart to avoid. Kris is now the feminine half of OUTFIT_JOHTO -
// a character an outfit offers, reachable at any identity, like every other.
//
// So this enum is two wide and equals GENDER_COUNT again. That is a coincidence
// of arity, not a merge: this one indexes art and enum Gender also types
// struct Trainer's one-bit field. Do not fold them together.
enum PlayerLook
{
    PLAYER_LOOK_MASC = MALE,
    PLAYER_LOOK_FEM = FEMALE,
    PLAYER_LOOK_COUNT,
};

// Named a whole word apart from GENDER_COUNT on purpose. The commit this was
// ported from calls its third-gender count GENDERS_COUNT, one letter from the
// two-wide GENDER_COUNT it sits beside, and the two size different tables.

#define NUM_BARD_SONG_WORDS    6
#define NUM_STORYTELLER_TALES  4
#define NUM_TRADER_ITEMS       4
#define GIDDY_MAX_TALES       10
#define GIDDY_MAX_QUESTIONS    8

#define OPTIONS_BUTTON_MODE_NORMAL 0
#define OPTIONS_BUTTON_MODE_LR 1
#define OPTIONS_BUTTON_MODE_L_EQUALS_A 2

#define OPTIONS_TEXT_SPEED_SLOW 0
#define OPTIONS_TEXT_SPEED_MID 1
#define OPTIONS_TEXT_SPEED_FAST 2
#define OPTIONS_TEXT_SPEED_INSTANT 3

#define OPTIONS_SOUND_MONO 0
#define OPTIONS_SOUND_STEREO 1

#define OPTIONS_BATTLE_STYLE_SHIFT 0
#define OPTIONS_BATTLE_STYLE_SET 1

#define OPTIONS_BATTLE_SPEED_1X  0
#define OPTIONS_BATTLE_SPEED_2X  1
#define OPTIONS_BATTLE_SPEED_3X  2
#define OPTIONS_BATTLE_SPEED_4X  3

enum __attribute__((packed)) Direction
{
    DIR_NONE,
    DIR_SOUTH,
    DIR_NORTH,
    DIR_WEST,
    DIR_EAST,
    CARDINAL_DIRECTION_COUNT,
    DIR_SOUTHWEST = CARDINAL_DIRECTION_COUNT,
    DIR_SOUTHEAST,
    DIR_NORTHWEST,
    DIR_NORTHEAST,
};

enum Connection
{
    CONNECTION_INVALID = -1,
    CONNECTION_NONE,
    CONNECTION_SOUTH,
    CONNECTION_NORTH,
    CONNECTION_WEST,
    CONNECTION_EAST,
    CONNECTION_DIVE,
    CONNECTION_EMERGE
};

#if TESTING
#include "config/test.h"
#endif

#endif // GUARD_CONSTANTS_GLOBAL_H
