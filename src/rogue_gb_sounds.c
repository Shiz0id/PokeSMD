#include "global.h"
#include "event_data.h"
#include "overworld.h"
#include "sound.h"
#include "string_util.h"
#include "script.h"
#include "m4a.h"
#include "gbs.h"
#include "gba/m4a_internal.h"
#include "rogue_gb_sounds.h"
#include "constants/flags.h"
#include "constants/gbs_songs.h"
#include "constants/songs.h"

// See include/rogue_gb_sounds.h for what this is and why the flag is safe to
// flip at any moment.

static const u8 sText_GbSoundsOn[] = _("Turned on the power for the\nGB SOUNDS!");
static const u8 sText_GbSoundsOff[] = _("Turned off the power for the\nGB SOUNDS!");

// The jukebox. MUS_DUMMY and GBS_MUSIC_NONE are both 0, so the zeroed default is
// "off" without depending on whether an EWRAM initialiser is honoured.
static EWRAM_DATA u16 sJukeboxSong = MUS_DUMMY;
static EWRAM_DATA u16 sJukeboxGbsId = GBS_MUSIC_NONE;

// The queue, and the position within it that sJukeboxSong is playing. Stored as
// song/arrangement pairs rather than as rows of the music player's tracklist,
// because this file has no business knowing that tracklist exists -- the player
// resolves pairs back to names for display, the same way it already does for the
// single playing track. 66 bytes of EWRAM.
static EWRAM_DATA u16 sQueueSong[ROGUE_JUKEBOX_QUEUE_MAX] = {0};
static EWRAM_DATA u16 sQueueGbsId[ROGUE_JUKEBOX_QUEUE_MAX] = {0};
static EWRAM_DATA u8 sQueueCount = 0;
static EWRAM_DATA u8 sQueuePos = 0;

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

// Which GB arrangement, if any, belongs to a song -- for the music player, which
// shows a per-track indicator and lets the player A/B one track without touching
// the global field-music flag.
//
// The song table's third column is the authority for anything with a REAL GBS
// transcription, because that is the column GetSong reads: keeping one source
// means the player can never advertise a GB version the field music would not
// also play. The music player's own table supplies a second id on top of that,
// for the thirty-six m4a-on-PSG-voicegroup remixes, which are deliberately
// unwired from the third column (see commit 2f9616684e) and are reachable only
// from here.
u16 RogueGbSounds_GetSongGbsId(u16 songId)
{
    u16 gbsId = gSongTable[songId].me;

    // Bounds-checked for the same reason GetSong bounds-checks it: song_table.inc
    // is 610 hand-editable rows and an out-of-range id indexes past the GBS table.
    if (gbsId != GBS_MUSIC_NONE && gbsId < GBS_MUSIC_COUNT)
        return gbsId;

    return GBS_MUSIC_NONE;
}

// RogueGbSounds_PlaySongAs used to live here: a direct MPlayStart of one track
// in a chosen arrangement, for auditioning. It is gone, and deliberately, not by
// oversight -- starting the music BEHIND the map music state machine's back is
// precisely what made a chosen track die at the first warp, because
// sCurrentMapMusic still named the map's own song and every transition in
// overworld.c duly acted on the difference. RogueGbSounds_SetJukebox below goes
// through PlayNewMapMusic instead, so the engine agrees with the speakers.

// Silence whatever was being auditioned and put the map back on its own track,
// resolved through the flag as usual.
//
// Same stop-then-restart order as RogueGbSounds_Toggle above, and for the same
// reason -- PlayNewMapMusic only starts, it does not stop.
//
// It goes through Overworld_PlaySpecialMapMusic rather than re-issuing
// GetCurrentMapMusic(), because GetCurrentMapMusic() is no longer a reliable
// answer to "what should the map be playing": while the jukebox is on,
// sCurrentMapMusic deliberately holds the JUKEBOX track, and re-issuing it here
// would put the jukebox back on rather than take it off. Asking the overworld
// also picks up the surfing, underwater and savedMusic layers, which the old
// version silently discarded.
void RogueGbSounds_StopAuditionAndRestoreMapMusic(void)
{
    m4aMPlayStop(&gMPlayInfo_BGM);
    // Zero first so the song the overworld names always differs from the current
    // one and is therefore actually re-issued -- Overworld_PlaySpecialMapMusic
    // no-ops when they match, which is exactly the case being repaired here.
    ResetMapMusic();
    Overworld_PlaySpecialMapMusic();
}

// -------------------------------------------------------------------------
// The jukebox -- see the block comment in include/rogue_gb_sounds.h
// -------------------------------------------------------------------------

// Put one track on. STATIC: everything outside goes through the queue, which is
// the only thing that knows what "next" means. Nothing else may set the jukebox
// behind the queue's back, or the queue position and the music disagree.
static void StartJukeboxTrack(u16 songId, u16 gbsId)
{
    // Bounds-checked here rather than trusted from the caller, for the same
    // reason GetSong bounds-checks the song table's third column: an
    // out-of-range id indexes past gGBSSongTable and returns garbage as a song
    // header. Falling back to NONE just plays the m4a arrangement.
    if (gbsId >= GBS_MUSIC_COUNT)
        gbsId = GBS_MUSIC_NONE;

    sJukeboxSong = songId;
    sJukeboxGbsId = gbsId;

    // Hand the track to the MAP MUSIC state machine instead of starting it
    // directly. sCurrentMapMusic then holds it, so every "is the music changing?"
    // comparison in overworld.c answers no and the track survives a warp playing
    // rather than being restarted from the top on each floor.
    //
    // Stop first, and reset before playing, for the reason in
    // RogueGbSounds_Toggle: the outgoing track may be a GBS one holding all four
    // PSG channels and MapMusicMain only starts, it does not stop. ResetMapMusic
    // is also what forces a restart when the SAME song is re-issued after the GB
    // Sounds flag is flipped, which is how the A/B in the player stays instant.
    m4aMPlayStop(&gMPlayInfo_BGM);
    ResetMapMusic();
    PlayNewMapMusic(songId);
}

void RogueGbSounds_ClearJukebox(void)
{
    // Cleared BEFORE the restore: the funnels in overworld.c consult the jukebox
    // to answer what this location's music is, so restoring first would just
    // hand the jukebox track back.
    sJukeboxSong = MUS_DUMMY;
    sJukeboxGbsId = GBS_MUSIC_NONE;

    // The queue goes with it. STOP that left a queue standing would put a track
    // back on the next time anything called Advance, with nothing on screen
    // having said so.
    sQueueCount = 0;
    sQueuePos = 0;

    RogueGbSounds_StopAuditionAndRestoreMapMusic();
}

// -------------------------------------------------------------------------
// The queue
// -------------------------------------------------------------------------
//
// THE QUEUE IS THE JUKEBOX. There is one playing track and it is always
// sQueue[sQueuePos] -- there is no second way to be playing something, which is
// why StartJukeboxTrack is static. Playing a track with A is a queue of one.
//
// ADVANCING IS ALWAYS MANUAL, and that is a fact about the ROM rather than a
// design preference: every real BGM track here loops forever (mus_petalburg_woods
// carries nine GOTOs, one per track; mus_victory_road ten), so "the song ended"
// is an event that never happens outside the jingles. Polling IsBGMPlaying to
// auto-advance would look correct, build clean, and never once fire.

void RogueGbSounds_QueuePlayNow(u16 songId, u16 gbsId)
{
    sQueueSong[0] = songId;
    sQueueGbsId[0] = gbsId;
    sQueueCount = 1;
    sQueuePos = 0;

    StartJukeboxTrack(songId, gbsId);
}

bool32 RogueGbSounds_QueuePush(u16 songId, u16 gbsId)
{
    if (sQueueCount >= ROGUE_JUKEBOX_QUEUE_MAX)
        return FALSE;

    sQueueSong[sQueueCount] = songId;
    sQueueGbsId[sQueueCount] = gbsId;
    sQueueCount++;

    // Queueing onto silence starts playing. Without this the first QUEUE press
    // builds a list that never sounds, which is indistinguishable from the
    // button doing nothing.
    if (sJukeboxSong == MUS_DUMMY)
    {
        sQueuePos = sQueueCount - 1;
        StartJukeboxTrack(songId, gbsId);
    }

    return TRUE;
}

// delta is +1 for next and -1 for previous. Wraps both ways, so skipping off
// either end of the queue lands back inside it rather than stopping -- and a
// queue of one restarts that one, which is the only reading of "next" it has.
bool32 RogueGbSounds_QueueAdvance(s32 delta)
{
    s32 pos;

    if (sQueueCount == 0)
        return FALSE;

    pos = (s32)sQueuePos + delta;
    while (pos < 0)
        pos += sQueueCount;
    pos %= sQueueCount;

    sQueuePos = pos;
    StartJukeboxTrack(sQueueSong[pos], sQueueGbsId[pos]);
    return TRUE;
}

u32 RogueGbSounds_QueueCount(void)
{
    return sQueueCount;
}

u32 RogueGbSounds_QueuePos(void)
{
    return sQueuePos;
}

bool32 RogueGbSounds_QueuePeek(u32 index, u16 *songId, u16 *gbsId)
{
    if (index >= sQueueCount)
        return FALSE;

    *songId = sQueueSong[index];
    *gbsId = sQueueGbsId[index];
    return TRUE;
}

u16 RogueGbSounds_GetJukeboxSong(void)
{
    return sJukeboxSong;
}

u16 RogueGbSounds_GetJukeboxGbsId(void)
{
    return sJukeboxGbsId;
}

// The arrangement GetSong must resolve this song id to while the jukebox holds
// it. Only a REMIX row ever answers with anything: its gbsId is one of the
// thirty-six m4a arrangements on a PSG voicegroup, which commit 2f9616684e
// deliberately unwired from song_table.inc's third column. A normal row leaves
// this NONE and the flag resolves the song exactly as it did while auditioning,
// so there is still one source of truth for every real GBS transcription.
u16 RogueGbSounds_GetJukeboxForcedGbsId(u16 songId)
{
    if (sJukeboxSong == MUS_DUMMY || songId != sJukeboxSong)
        return GBS_MUSIC_NONE;

    return sJukeboxGbsId;
}

// Script entry point. Buffers the result into gStringVar1 so the .inc can print
// it with one msgbox, the same shape RogueCharm_ScriptBufferSummary uses.
void RogueGbSounds_ScriptToggle(struct ScriptContext *ctx)
{
    RogueGbSounds_Toggle();
    StringCopy(gStringVar1,
               RogueGbSounds_IsEnabled() ? sText_GbSoundsOn : sText_GbSoundsOff);
}
