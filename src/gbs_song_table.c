#include "gbs_song_table.h"
#include "constants/gbs_songs.h"

#define SONG(label, ...) { .header = (struct SongHeader*)&label, __VA_ARGS__ }
const struct Song gGBSSongTable[GBS_MUSIC_COUNT] = {
    // Gold/Silver
    [GBS_MUSIC_VIRIDIAN_CITY] = SONG(gbs_Music_ViridianCity, 0),
    [GBS_MUSIC_AZALEA_TOWN]   = SONG(gbs_Music_AzaleaTown, 0),
    [GBS_MUSIC_VIOLET_CITY]   = SONG(gbs_Music_VioletCity, 0),
    [GBS_MUSIC_LAKE_OF_RAGE]  = SONG(gbs_Music_LakeOfRage, 0),
    [GBS_MUSIC_ROUTE_37]      = SONG(gbs_Music_Route37, 0),

    // Crystal
    [GBS_MUSIC_SUICUNE_BATTLE] = SONG(gbs_Music_SuicuneBattle, 0),
    [GBS_MUSIC_MOBILE_CENTER]  = SONG(gbs_Music_MobileCenter, 0),

    // Gold/Silver sfx
    [GBS_SFX_ITEM]      = SONG(gbs_Sfx_Item, 2),
    [GBS_SFX_LEVEL_UP]  = SONG(gbs_Sfx_LevelUp, 2),
    [GBS_SFX_GET_TM]    = SONG(gbs_Sfx_GetTm, 2),
    [GBS_SFX_GET_BADGE] = SONG(gbs_Sfx_GetBadge, 2),

    // Red/Blue
    [GBS_MUSIC_LAVENDER]         = SONG(gbs_Music_Lavender, 0),
    [GBS_MUSIC_TRAINER_BATTLE]   = SONG(gbs_Music_TrainerBattle, 0),
    [GBS_MUSIC_BIKE_RIDING]      = SONG(gbs_Music_BikeRiding, 0),
    [GBS_MUSIC_RBY_TITLE_SCREEN] = SONG(gbs_Music_RBYTitleScreen, 0),
    [GBS_MUSIC_DUNGEON2]         = SONG(gbs_Music_Dungeon2, 0),
    [GBS_MUSIC_CINNABAR_MANSION] = SONG(gbs_Music_CinnabarMansion, 0),
    [GBS_MUSIC_POKEMON_TOWER]    = SONG(gbs_Music_PokemonTower, 0),

    // Custom
    [GBS_MUSIC_ROUTE_101_RSE]      = SONG(gbs_Music_Route101RSE, 0),
    [GBS_MUSIC_ABANDONED_SHIP_RSE] = SONG(gbs_Music_AbandonedShipRSE, 0),
    [GBS_MUSIC_OCEANIC_MUSEUM_RSE] = SONG(gbs_Music_OceanicMuseumRSE, 0),
    [GBS_MUSIC_MEW_BATTLE_RSE]     = SONG(gbs_Music_MewBattle, 0),

    // The experiment: same MIDI as mus_petalburg_woods, built against a
    // PSG-only voicegroup instead of being transcribed. Dungeon 1's music, so
    // the Sound menu entry A/Bs sampled against Game Boy on the first floor
    // the player ever sees.
    [GBS_MUSIC_WOODS_M4A_PSG]      = SONG(mus_petalburg_woods_gb, 0),
    [GBS_MUSIC_VS_TRAINER_PSG] = SONG(mus_vs_trainer_gb, 0),
    [GBS_MUSIC_ENCOUNTER_AQUA_PSG] = SONG(mus_encounter_aqua_gb, 0),
    [GBS_MUSIC_ENCOUNTER_COOL_PSG] = SONG(mus_encounter_cool_gb, 0),
    [GBS_MUSIC_ENCOUNTER_ELITE_FOUR_PSG] = SONG(mus_encounter_elite_four_gb, 0),
    [GBS_MUSIC_ENCOUNTER_FEMALE_PSG] = SONG(mus_encounter_female_gb, 0),
    [GBS_MUSIC_ENCOUNTER_GIRL_PSG] = SONG(mus_encounter_girl_gb, 0),
    [GBS_MUSIC_ENCOUNTER_INTENSE_PSG] = SONG(mus_encounter_intense_gb, 0),
    [GBS_MUSIC_ENCOUNTER_MAGMA_PSG] = SONG(mus_encounter_magma_gb, 0),
    [GBS_MUSIC_ENCOUNTER_MALE_PSG] = SONG(mus_encounter_male_gb, 0),
    [GBS_MUSIC_ENCOUNTER_MAY_PSG] = SONG(mus_encounter_may_gb, 0),
    [GBS_MUSIC_ENCOUNTER_SUSPICIOUS_PSG] = SONG(mus_encounter_suspicious_gb, 0),
    [GBS_MUSIC_ENCOUNTER_TWINS_PSG] = SONG(mus_encounter_twins_gb, 0),
    [GBS_MUSIC_ENCOUNTER_BRENDAN_PSG] = SONG(mus_encounter_brendan_gb, 0),
    [GBS_MUSIC_ENCOUNTER_CHAMPION_PSG] = SONG(mus_encounter_champion_gb, 0),
    [GBS_MUSIC_ENCOUNTER_HIKER_PSG] = SONG(mus_encounter_hiker_gb, 0),
    [GBS_MUSIC_VS_CHAMPION_PSG] = SONG(mus_vs_champion_gb, 0),
    [GBS_MUSIC_VS_ELITE_FOUR_PSG] = SONG(mus_vs_elite_four_gb, 0),
    [GBS_MUSIC_VS_FRONTIER_BRAIN_PSG] = SONG(mus_vs_frontier_brain_gb, 0),
    [GBS_MUSIC_VS_GYM_LEADER_PSG] = SONG(mus_vs_gym_leader_gb, 0),
    [GBS_MUSIC_UNDERWATER_PSG] = SONG(mus_underwater_gb, 0),
    [GBS_MUSIC_SURF_PSG] = SONG(mus_surf_gb, 0),
};
