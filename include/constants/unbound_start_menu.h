#ifndef GUARD_CONSTANTS_UNBOUND_START_MENU_H
#define GUARD_CONSTANTS_UNBOUND_START_MENU_H

enum Usm_Icons {
    USM_ICO_POKEDEX,
    USM_ICO_PARTY,
    USM_ICO_BAG,
    USM_ICO_POKENAV,
    USM_ICO_TRAINER,
    USM_ICO_SAVE,
    USM_ICO_OPTIONS,
    USM_ICO_SAFARI_RETIRE,
    USM_ICO_FRONTIER_RETIRE,
    USM_ICO_DEBUG,
    // Appended rather than slotted in beside the others: these ids are
    // STORED, in Usm_SavedItems.items[] in SaveBlock3, so inserting one
    // would silently renumber every icon a player has already arranged.
    USM_ICO_CHARMS,
    USM_ICO_COUNT
};


#endif /* end of include guard: GUARD_CONSTANTS_UNBOUND_START_MENU_H */
