#ifndef GUARD_GBS_SONG_TABLE_H
#define GUARD_GBS_SONG_TABLE_H

#include "gbs.h"

extern const struct SongHeader gbs_Music_LakeOfRage;
extern const struct SongHeader gbs_Music_Route37;
extern const struct SongHeader gbs_Music_VioletCity;
extern const struct SongHeader gbs_Music_ViridianCity;
extern const struct SongHeader gbs_Music_AzaleaTown;

extern const struct SongHeader gbs_Music_SuicuneBattle;
extern const struct SongHeader gbs_Music_MobileCenter;

extern const struct SongHeader gbs_Sfx_Item;
extern const struct SongHeader gbs_Sfx_LevelUp;
extern const struct SongHeader gbs_Sfx_GetTm;
extern const struct SongHeader gbs_Sfx_GetBadge;

// Transcribed from Emerald by tools/rogue/midi_to_gbs.py, unlike everything
// above it, which came with the GameboySounds branch.
extern const struct SongHeader gbs_Music_PetalburgWoods;

extern const struct SongHeader gbs_Music_Lavender;
extern const struct SongHeader gbs_Music_TrainerBattle;
extern const struct SongHeader gbs_Music_BikeRiding;
extern const struct SongHeader gbs_Music_RBYTitleScreen;
extern const struct SongHeader gbs_Music_Dungeon2;
extern const struct SongHeader gbs_Music_CinnabarMansion;
extern const struct SongHeader gbs_Music_PokemonTower;

extern const struct SongHeader gbs_Music_Route101RSE;
extern const struct SongHeader gbs_Music_AbandonedShipRSE;
extern const struct SongHeader gbs_Music_OceanicMuseumRSE;
extern const struct SongHeader gbs_Music_MewBattle;

// An m4a song, not a GBS one -- built against voicegroup_gb_woods so every voice
// is a PSG voice. mid2agb emits it with the same struct SongHeader layout, which
// is what lets it sit in the GBS table beside real GBS tracks.
extern const struct SongHeader mus_petalburg_woods_gb;
extern const struct SongHeader mus_route119_gb;
extern const struct SongHeader mus_victory_road_gb;
extern const struct SongHeader mus_sealed_chamber_gb;
extern const struct SongHeader mus_route111_gb;
extern const struct SongHeader mus_mt_pyre_exterior_gb;
extern const struct SongHeader mus_mt_pyre_gb;
extern const struct SongHeader mus_mt_chimney_gb;
extern const struct SongHeader mus_ever_grande_gb;
extern const struct SongHeader mus_cave_of_origin_gb;
extern const struct SongHeader mus_aqua_magma_hideout_gb;
extern const struct SongHeader mus_abnormal_weather_gb;
extern const struct SongHeader mus_encounter_rich_gb;
extern const struct SongHeader mus_encounter_swimmer_gb;
extern const struct SongHeader mus_encounter_interviewer_gb;
extern const struct SongHeader mus_surf_gb;
extern const struct SongHeader mus_underwater_gb;
extern const struct SongHeader mus_vs_gym_leader_gb;
extern const struct SongHeader mus_vs_frontier_brain_gb;
extern const struct SongHeader mus_vs_elite_four_gb;
extern const struct SongHeader mus_vs_champion_gb;
extern const struct SongHeader mus_encounter_hiker_gb;
extern const struct SongHeader mus_encounter_champion_gb;
extern const struct SongHeader mus_encounter_brendan_gb;
extern const struct SongHeader mus_encounter_twins_gb;
extern const struct SongHeader mus_encounter_suspicious_gb;
extern const struct SongHeader mus_encounter_may_gb;
extern const struct SongHeader mus_encounter_male_gb;
extern const struct SongHeader mus_encounter_magma_gb;
extern const struct SongHeader mus_encounter_intense_gb;
extern const struct SongHeader mus_encounter_girl_gb;
extern const struct SongHeader mus_encounter_female_gb;
extern const struct SongHeader mus_encounter_elite_four_gb;
extern const struct SongHeader mus_encounter_cool_gb;
extern const struct SongHeader mus_encounter_aqua_gb;
extern const struct SongHeader mus_vs_trainer_gb;


#endif // GUARD_GBS_SONG_TABLE_H
