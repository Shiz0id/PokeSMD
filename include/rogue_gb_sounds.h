#ifndef GUARD_ROGUE_GB_SOUNDS_H
#define GUARD_ROGUE_GB_SOUNDS_H

// GB Sounds: the GBS sequencer drives the GBA's four legacy PSG channels from
// Game Boy song data instead of m4a. FLAG_SYS_GBS_ENABLED selects it, and
// GetSong in gbs.c reads that flag on every song lookup - a song with no GBS
// arrangement (GBS_MUSIC_NONE in the third column of sound/song_table.inc)
// falls through to m4a untouched, so the flag is safe to flip at any time.
//
// USM_ICO_SOUND on the start menu opens the music player, which uses the
// audition and jukebox entry points below; the flag remains the FIELD MUSIC
// setting and the player toggles it from inside.

bool8 RogueGbSounds_IsEnabled(void);
void RogueGbSounds_Toggle(void);

// GetSongGbsId returns GBS_MUSIC_NONE when a song has no GB arrangement, which
// is what the music player's per-track indicator reads. It answers from
// song_table.inc's third column only, so it never claims a GB version the field
// music would not also play.
u16 RogueGbSounds_GetSongGbsId(u16 songId);

// Put the map back on its own music, resolved through the flag as usual.
void RogueGbSounds_StopAuditionAndRestoreMapMusic(void);

// THE JUKEBOX: the track picked in the music player, which keeps playing after
// the player closes - across warps, down a floor, and out the far side of a
// battle. That is the whole point of the player; auditioning a track and losing
// it on exit is not a music player.
//
// It is handed to the MAP MUSIC state machine as a song id rather than started
// directly with MPlayStart, and that is the mechanism, not an implementation
// detail. Every music decision in overworld.c is of the form "is the new song
// different from GetCurrentMapMusic()?", so once sCurrentMapMusic holds the
// jukebox track and the funnels below report it as the location's music, each of
// those comparisons answers no and the track is LEFT ALONE. A warp does not
// restart it - it plays through, seamlessly, which is what an audition started
// with MPlayStart could never do.
//
// GetJukeboxForcedGbsId is the other half, read by GetSong in gbs.c: a REMIX row
// names one of the thirty-six m4a-on-PSG-voicegroup arrangements, which
// song_table.inc's third column deliberately does not carry, so nothing could
// find it from a song number without this. Without it the remix silently becomes
// the plain m4a track the first time anything re-issues the song.
//
// SESSION-ONLY, and deliberately so: it lives in EWRAM, not the save block, so
// loading a save always starts on the map's own music and no save format moved.
void RogueGbSounds_ClearJukebox(void);          // also empties the queue
u16 RogueGbSounds_GetJukeboxSong(void);         // MUS_DUMMY when the jukebox is off
u16 RogueGbSounds_GetJukeboxGbsId(void);
u16 RogueGbSounds_GetJukeboxForcedGbsId(u16 songId);

// THE QUEUE, and it IS the jukebox -- the playing track is always the entry at
// QueuePos, and there is no other way to be playing something. The function that
// actually starts a track is static in rogue_gb_sounds.c for that reason: a
// second way in would let the queue position and the music disagree.
//
// ADVANCING IS ALWAYS MANUAL -- L/R in the music player, SELECT+L/R in the
// overworld. That is a fact about the ROM, not a preference. Every real BGM track
// here loops forever (mus_petalburg_woods carries nine GOTOs, one per track), so
// "the song ended" never happens outside the jingles, and an auto-advance polling
// IsBGMPlaying would build clean and never once fire.
#define ROGUE_JUKEBOX_QUEUE_MAX 16

void RogueGbSounds_QueuePlayNow(u16 songId, u16 gbsId);   // queue of one
bool32 RogueGbSounds_QueuePush(u16 songId, u16 gbsId);    // FALSE when full
bool32 RogueGbSounds_QueueAdvance(s32 delta);             // +1 next, -1 previous; wraps
u32 RogueGbSounds_QueueCount(void);
u32 RogueGbSounds_QueuePos(void);
bool32 RogueGbSounds_QueuePeek(u32 index, u16 *songId, u16 *gbsId);

#endif // GUARD_ROGUE_GB_SOUNDS_H
