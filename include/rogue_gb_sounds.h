#ifndef GUARD_ROGUE_GB_SOUNDS_H
#define GUARD_ROGUE_GB_SOUNDS_H

// GB Sounds: the GBS sequencer drives the GBA's four legacy PSG channels from
// Game Boy song data instead of m4a. FLAG_SYS_GBS_ENABLED selects it, and
// GetSong in gbs.c reads that flag on every song lookup - a song with no GBS
// arrangement (GBS_MUSIC_NONE in the third column of sound/song_table.inc)
// falls through to m4a untouched, so the flag is safe to flip at any time.
//
// This is the seam the eventual in-menu music player hangs off. Today it is a
// toggle; the start menu entry that reaches it is USM_ICO_SOUND.

bool8 RogueGbSounds_IsEnabled(void);
void RogueGbSounds_Toggle(void);

#endif // GUARD_ROGUE_GB_SOUNDS_H
