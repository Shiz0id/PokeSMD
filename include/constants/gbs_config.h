#ifndef GUARD_GBS_CONFIG_H
#define GUARD_GBS_CONFIG_H

// If set to TRUE, the sound check debug menu will be available.
// Tests various sounds like cries, sound effects and music.
//
// FALSE here: upstream's sound_check_menu.c was NOT ported. This flag does not
// gate GB Sounds itself -- both settings support it. It only decides whether the
// GBS enable is read from FLAG_SYS_GBS_ENABLED inside GetSong (FALSE) or passed
// in as a parameter so the debug menu can override it (TRUE). Setting it TRUE
// without porting that menu leaves m4a.c calling SongNumStart* wrappers that
// nothing else needs.
#define ENABLE_DEBUG_SOUND_CHECK_MENU FALSE

#endif // GUARD_GBS_CONFIG_H
