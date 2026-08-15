#include "global.h"
#include "event_data.h"
#include "sound.h"
#include "string_util.h"
#include "script.h"
#include "m4a.h"
#include "rogue_gb_sounds.h"
#include "constants/flags.h"

// See include/rogue_gb_sounds.h for what this is and why the flag is safe to
// flip at any moment.

static const u8 sText_GbSoundsOn[] = _("Turned on the power for the\nGB SOUNDS!");
static const u8 sText_GbSoundsOff[] = _("Turned off the power for the\nGB SOUNDS!");

bool8 RogueGbSounds_IsEnabled(void)
{
    return FlagGet(FLAG_SYS_GBS_ENABLED);
}

void RogueGbSounds_Toggle(void)
{
    u16 song = GetCurrentMapMusic();

    if (RogueGbSounds_IsEnabled())
        FlagClear(FLAG_SYS_GBS_ENABLED);
    else
        FlagSet(FLAG_SYS_GBS_ENABLED);

    // Restart the map track so the change is audible immediately rather than at
    // the next map load. The flag is read inside GetSong, so re-issuing the same
    // song number now resolves to the other arrangement -- m4aSongNumStartOrChange
    // sees a different songHeader and restarts rather than no-opping.
    //
    // m4aMPlayStop first because the outgoing track may be a GBS one holding the
    // PSG channels, and MapMusicMain only starts the new track; it does not stop
    // the old.
    m4aMPlayStop(&gMPlayInfo_BGM);
    ResetMapMusic();
    PlayNewMapMusic(song);
}

// Script entry point. Buffers the result into gStringVar1 so the .inc can print
// it with one msgbox, the same shape RogueCharm_ScriptBufferSummary uses.
void RogueGbSounds_ScriptToggle(struct ScriptContext *ctx)
{
    RogueGbSounds_Toggle();
    StringCopy(gStringVar1,
               RogueGbSounds_IsEnabled() ? sText_GbSoundsOn : sText_GbSoundsOff);
}
