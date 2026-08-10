#ifndef GUARD_CONFIG_SWSH_PARTY_MENU_H
#define GUARD_CONFIG_SWSH_PARTY_MENU_H

#define SWSH_PARTY_MENU                   TRUE
// FALSE, against upstream's TRUE. This would put box access on START from the
// party menu, i.e. from anywhere mid-floor. Box access is deliberately located
// at the rest stop's archivist -- the rest stop is a place, not a heal button --
// and this is the one setting on this branch that changes the run's design
// rather than its looks. Flip to TRUE if that call changes.
#define SWSH_PARTY_MENU_PC_ACCESS         FALSE
#define SWSH_PARTY_MON_IDLE_ANIMS         TRUE
#define SWSH_PARTY_MON_IDLE_ANIMS_FRAMES  300 // Number of frames before mon animation loops

#endif // GUARD_CONFIG_SWSH_PARTY_MENU_H
