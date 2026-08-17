#ifndef GUARD_MAP_NAME_POPUP_H
#define GUARD_MAP_NAME_POPUP_H

// Exported type declarations

// Exported RAM declarations

// Exported ROM declarations
void HideMapNamePopUpWindow(void);
void ShowMapNamePopup(void);
u8 *GetPopUpMapName(u8 *dest, const struct MapHeader *mapHeader);

// The plate can announce something that is not a place. The music player uses
// this on the way out, so the jukebox track you just picked is named on screen
// as you walk away from the menu.
//
// It draws in the TOP RIGHT rather than the top left, so it never reads as the
// floor name, and it shares the map-name pop-up's task, window and artwork
// rather than standing a second one up -- see the POPUP_STYLE comment in
// src/map_name_popup.c for why sharing is not optional here.
//
// trackName is COPIED. Callers may free it immediately; the music player does.
void ShowNowPlayingPopup(const u8 *trackName);

// 6 bytes of colour control codes, then the string. Widened from 27 for the
// music player's longest track names (22 characters): the plate has always
// fitted long strings by shrinking the font, but this BUFFER used to stop four
// characters short of what the tracklist can hold, and overrunning it smashes a
// stack frame rather than merely looking wrong.
#define MAP_POPUP_STRING_BUFFER_LENGTH 32
#define MAP_POPUP_PREFIX_BUFFER_LENGTH 6

// The longest string the plate can carry, EOS included. Parsed out of this
// header by tools/rogue/check_music_player_table.py, which asserts every track
// name fits -- so a name added to the tracklist cannot quietly overrun it.
#define MAP_POPUP_NAME_BUFFER_LENGTH \
    (MAP_POPUP_STRING_BUFFER_LENGTH - MAP_POPUP_PREFIX_BUFFER_LENGTH)

#endif //GUARD_MAP_NAME_POPUP_H
