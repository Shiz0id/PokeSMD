#include "global.h"
#include "bg.h"
#include "malloc.h"

#include "game_corner.h"

// Every one of the vendored game corner minigames sets up its backgrounds the
// same way:
//
//     SetBgTilemapBuffer(SOME_BG, AllocZeroed(BG_SCREEN_SIZE));
//
// and keeps no pointer to the result, so the BG itself is the only handle. None
// of them free it. That is 2 KB per buffer per session -- 6 KB for a game with
// three backgrounds -- against a HEAP_SIZE of 115968, and it accumulates
// because InitHeap runs at boot and save-load rather than per map. AllocZeroed
// calls fatalf rather than returning NULL, so the end of that road is a hard
// crash, and the rest stop sits between every dungeon so the cabinets get
// replayed. The mining minigame shipped the identical bug; see
// Mining_FreeResources.
//
// Walking all four backgrounds is safe rather than lucky. InitBgFromTemplate
// sets sGpuBgConfigs2[bg].tilemap to NULL, and GetBgTilemapBuffer returns NULL
// for a background that is invalid or not visible -- so this frees exactly the
// buffers that were allocated and touches nothing else. It is therefore also
// safe to call on an exit path that runs before the backgrounds were set up.
void GameCorner_FreeBgTilemapBuffers(void)
{
    u32 bg;

    for (bg = 0; bg < NUM_BACKGROUNDS; bg++)
    {
        void *tilemap = GetBgTilemapBuffer(bg);

        if (tilemap != NULL)
        {
            Free(tilemap);
            UnsetBgTilemapBuffer(bg);
        }
    }
}
