#ifndef GUARD_ROGUE_MUSIC_PLAYER_H
#define GUARD_ROGUE_MUSIC_PLAYER_H

// The in-menu music player. USM_ICO_SOUND on the start menu calls Open
// directly, the way the debug menu is opened -- both are hosted on the
// overworld as windows over the field, so neither can fade the screen out, and
// the player locks the field itself rather than relying on a script's lockall.
//
// Its tracklist is src/data/rogue_music_player.h, a table, guarded by
// tools/rogue/check_music_player_table.py. Adding a track is a row there.

// Controls, so the two places that name them agree: A plays the highlighted
// track now (a queue of one), R appends it to the queue, L skips to the next
// queued track, START stops and empties the queue, SELECT toggles GB Sounds.
// In the overworld it is SELECT+R and SELECT+L to skip.
void RogueMusicPlayer_Open(void);

// Announce the playing queue entry on the NOW PLAYING plate. Called on the way
// out of the player, and by the overworld's SELECT+L/R skip -- that one changes
// the track with no menu on screen, so without it the combo has nothing to show
// for itself and reads as a dead button. Silent when nothing is playing.
void RogueMusicPlayer_ShowNowPlaying(void);

#endif // GUARD_ROGUE_MUSIC_PLAYER_H
