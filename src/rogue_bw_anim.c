#include "global.h"
#include "rogue_bw_anim.h"
#include "constants/species.h"

#include "data/rogue_bw_anim.h"

// Bisection, not a scan. The table is nine entries today and is meant to reach
// several hundred; a linear scan runs per battler and would be the kind of cost
// that stays invisible until the roster is large enough to make it matter.
//
// emit_bw_anim.py is what guarantees the ordering this depends on, and it
// sorts by the NUMERIC species id read out of the species enum. Sorting by
// name looks equivalent and is not - it puts CLAYDOL before GEODUDE while
// their ids run the other way, and this search would then silently miss them.
const struct BwAnim *GetBwAnim(u16 species)
{
    u32 lo = 0;
    u32 hi = ARRAY_COUNT(sBwAnims);

    while (lo < hi)
    {
        u32 mid = (lo + hi) / 2;
        u16 got = sBwAnims[mid].species;

        if (got == species)
            return &sBwAnims[mid];
        if (got < species)
            lo = mid + 1;
        else
            hi = mid;
    }

    return NULL;
}
