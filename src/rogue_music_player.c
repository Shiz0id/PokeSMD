#include "global.h"
#include "bg.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "list_menu.h"
#include "map_name_popup.h"
#include "main.h"
#include "malloc.h"
#include "menu.h"
#include "m4a.h"
#include "palette.h"
#include "script.h"
#include "sound.h"
#include "string_util.h"
#include "strings.h"
#include "task.h"
#include "text.h"
#include "window.h"
#include "rogue_gb_sounds.h"
#include "rogue_music_player.h"
#include "constants/gbs_songs.h"
#include "constants/songs.h"

#include "data/rogue_music_player.h"

// The music player. Opened from USM_ICO_SOUND on the start menu, which used to
// toggle GB Sounds outright -- the toggle is now one of the controls in here.
//
// HOSTED ON THE OVERWORLD, like the debug menu, rather than being a screen with
// its own callback and backgrounds. Windows on bg 0 over the field, one task
// driving them, no BG or palette teardown to get wrong. That is deliberate:
// eight of the nine Game Corner minigames on this branch shipped crashing on
// EXIT, all of them screens that tore down their own graphics, and none of it
// was caught by a build or a check. There is nothing here for that failure to
// happen to.
//
// The flag stays the field-music setting. Toggling it from in here also
// re-auditions whatever is playing, so the A/B is immediate, and it persists
// after the player closes -- which is the behaviour the old toggle had and the
// reason it is a flag and not a menu-local option.
//
// A CHOSEN TRACK OUTLIVES THE PLAYER. Playing one sets the jukebox in
// rogue_gb_sounds.c, which hands it to the map music state machine, and it then
// keeps playing across warps, down a floor and out the far side of a battle,
// until START stops it. The first version silenced it on close and put the map's
// music back, which reduced the whole player to an audition booth. Read the
// jukebox comment in include/rogue_gb_sounds.h before touching any of the four
// places below that reach it.

#define LIST_ROWS_SHOWN 7

// Wide enough for MAX_NAME_CHARS in FONT_NARROW plus the cursor column.
#define LIST_WINDOW_WIDTH 17
#define INFO_WINDOW_WIDTH 10

// Three levels, because one playlist nests: GAME BOY contains PSG REMIXES.
// MODE_SUBLISTS is only ever entered for a playlist that has children, so a
// flat playlist still opens its tracks in one press.
enum
{
    MODE_PLAYLISTS,
    MODE_SUBLISTS,
    MODE_TRACKS,
    MODE_COUNT,
};

#define NO_TRACK 0xFFFF

struct RogueMusicPlayerState
{
    u8 listTaskId;
    u8 listWindowId;
    u8 infoWindowId;
    u8 mode;
    u8 playlist;                    // the leaf playlist whose tracks are shown
    u8 parent;                      // the playlist entered at level 0
    u16 nowPlaying;                 // index into sRogueMusicTracks, or NO_TRACK
    // TRUE once anything in here has changed what the ears hear -- a track
    // played, or the GB Sounds flag flipped. Closing without it must leave the
    // music strictly alone, because "restore the map's music" restarts the field
    // track from the top and doing that on every open-and-back-out is a bug.
    bool8 touchedMusic;
    u16 itemCount;
    u16 highlighted;                // list id under the cursor
    u16 scrollOffset[MODE_COUNT];   // remembered per level, so B returns home
    u16 selectedRow[MODE_COUNT];
    // Sized by the biggest LIST, not by the tracklist -- see the note on
    // ROGUE_MUSIC_MAX_LIST_ROWS. Only one playlist's rows are ever in here.
    struct ListMenuItem items[ROGUE_MUSIC_MAX_LIST_ROWS];
};

// One allocation, freed on exit. The pointer is the ownership marker: every
// path out of the player goes through RogueMusicPlayer_Close, which is the only
// place that frees it and the only place that nulls it.
static EWRAM_DATA struct RogueMusicPlayerState *sPlayer = NULL;

static const struct WindowTemplate sListWindowTemplate =
{
    .bg = 0,
    .tilemapLeft = 1,
    .tilemapTop = 1,
    .width = LIST_WINDOW_WIDTH,
    .height = 2 * LIST_ROWS_SHOWN,
    .paletteNum = 15,
    .baseBlock = 1,
};

static const struct WindowTemplate sInfoWindowTemplate =
{
    .bg = 0,
    .tilemapLeft = 1 + LIST_WINDOW_WIDTH + 1,
    .tilemapTop = 1,
    .width = INFO_WINDOW_WIDTH,
    // Same height as the list window: 14 tiles, 112px. The last thing drawn is
    // the THREE-line controls hint at y=69, ending at 105, so the panel is full
    // to within 7px. That is why the GB lines below are one line each rather than
    // two -- adding the queue line and the third control row needed exactly the
    // 24px that compressing them freed. Anything new here needs a taller window,
    // not another AddTextPrinter.
    .height = 2 * LIST_ROWS_SHOWN,
    .paletteNum = 15,
    // Placed past the list window's blocks: LIST_WINDOW_WIDTH * rows, +1 for
    // the base block the list window itself starts at. Overlapping these is a
    // corrupt-tilemap bug that draws fine on one of the two windows.
    .baseBlock = 1 + (LIST_WINDOW_WIDTH * 2 * LIST_ROWS_SHOWN),
};

static const u8 sText_NowPlaying[] = _("NOW PLAYING");
static const u8 sText_Nothing[] = _("- - -");
// One line each. They were two before the queue arrived; see the note on the
// info window's height for why they cannot go back to two.
static const u8 sText_GbSoundsOn[] = _("GB SOUNDS: ON");
static const u8 sText_GbSoundsOff[] = _("GB SOUNDS: OFF");
static const u8 sText_HasGbVersion[] = _("GB VERSION");
static const u8 sText_NoGbVersion[] = _("NO GB VERSION");
static const u8 sText_PsgRemix[] = _("PSG, NOT GBS");
static const u8 sText_QueueEmpty[] = _("QUEUE EMPTY");
static const u8 sText_QueueFmt[] = _("QUEUE {STR_VAR_1}/{STR_VAR_2}");
static const u8 sText_ControlsPlaylists[] = _("{A_BUTTON}OPEN  {B_BUTTON}EXIT\n{L_BUTTON}NEXT\n{SELECT_BUTTON}GB  {START_BUTTON}STOP");
static const u8 sText_ControlsTracks[] = _("{A_BUTTON}PLAY  {B_BUTTON}BACK\n{R_BUTTON}QUEUE {L_BUTTON}NEXT\n{SELECT_BUTTON}GB  {START_BUTTON}STOP");

static void Task_RogueMusicPlayer(u8 taskId);
static void RogueMusicPlayer_BuildList(void);
static void RogueMusicPlayer_OpenList(void);
static void RogueMusicPlayer_CloseList(void);
static void RogueMusicPlayer_DrawInfo(void);
static void RogueMusicPlayer_Close(u8 taskId);
static void RogueMusicPlayer_MoveCursor(s32 itemIndex, bool8 onInit,
                                        struct ListMenu *list);

// The GB arrangement the flag may switch this track to.
//
// A REMIX row is excluded deliberately. Its gbsId is one of the thirty-six
// m4a-on-a-PSG-voicegroup arrangements, which are not GB Sounds at all, and
// reporting them here would put "GB VERSION AVAILABLE" on screen for a track
// that has none -- the same false claim commit 2f9616684e removed from
// song_table.inc. Only a real GBS transcription, found through the song
// table's third column, counts.
static u16 RogueMusicPlayer_GbsIdFor(const struct RogueMusicTrack *track)
{
    if (track->forced)
        return GBS_MUSIC_NONE;

    return RogueGbSounds_GetSongGbsId(track->songId);
}

static const struct RogueMusicTrack *RogueMusicPlayer_HighlightedTrack(void)
{
    if (sPlayer->mode != MODE_TRACKS || sPlayer->highlighted >= ROGUE_MUSIC_TRACK_COUNT)
        return NULL;

    return &sRogueMusicTracks[sPlayer->highlighted];
}

// The arrangement a row is played or queued in. ONE expression, used by every
// caller, because a queue entry is matched back to its row on the PAIR of song
// id and gbs id -- so a second copy of this rule that drifted would stop the
// player naming what it is playing at all.
//
// A remix's gbsId IS the track and has to be carried, because nothing can find
// it from a song number. A normal row passes NONE and lets the flag resolve it
// inside GetSong, exactly as the field music does; resolving it here as well
// would be a second source of truth for the same answer, and one that goes stale
// the moment the flag is flipped from outside.
static u16 RogueMusicPlayer_QueuedGbsIdFor(const struct RogueMusicTrack *track)
{
    return track->forced ? track->gbsId : GBS_MUSIC_NONE;
}

static void RogueMusicPlayer_PlayTrack(u16 index)
{
    const struct RogueMusicTrack *track = &sRogueMusicTracks[index];

    sPlayer->nowPlaying = index;
    sPlayer->touchedMusic = TRUE;
    RogueGbSounds_QueuePlayNow(track->songId,
                               RogueMusicPlayer_QueuedGbsIdFor(track));
}

// Which row the jukebox is on, so reopening the player says what is playing
// instead of "- - -".
//
// Matched on the PAIR, not on the song id: a REMIX row shares its song id with
// the ordinary row for the same track -- MUS_SURF is both "SURFING" and
// "SURFING (PSG)" -- and matching on the id alone would name the wrong one about
// half the time.
static u16 RogueMusicPlayer_TrackForJukebox(void)
{
    u16 songId = RogueGbSounds_GetJukeboxSong();
    u16 gbsId = RogueGbSounds_GetJukeboxGbsId();
    u32 i;

    if (songId == MUS_DUMMY)
        return NO_TRACK;

    for (i = 0; i < ROGUE_MUSIC_TRACK_COUNT; i++)
    {
        if (sRogueMusicTracks[i].songId != songId)
            continue;

        if (RogueMusicPlayer_QueuedGbsIdFor(&sRogueMusicTracks[i]) != gbsId)
            continue;

        return i;
    }

    return NO_TRACK;
}

// Name whatever the jukebox is on, on the pop-up plate.
//
// Exported because the OVERWORLD skip combo needs it: SELECT+L/R changes the
// track without the menu ever being opened, and a skip you cannot see is
// indistinguishable from a button that does not work. The close path uses the
// same call, so there is one place that decides what a track announcement says.
//
// Silent when the jukebox is off, or when the playing pair matches no row --
// which cannot happen today, since only this file ever fills the queue.
void RogueMusicPlayer_ShowNowPlaying(void)
{
    u16 index = RogueMusicPlayer_TrackForJukebox();

    if (index != NO_TRACK)
        ShowNowPlayingPopup(sRogueMusicTracks[index].name);
}

// -------------------------------------------------------------------------
// The list
// -------------------------------------------------------------------------

static void RogueMusicPlayer_BuildList(void)
{
    u32 i;
    u32 count = 0;

    // Every loop below stops at ROGUE_MUSIC_MAX_LIST_ROWS as well as at its own
    // end. check_music_player_table.py asserts no list can reach that bound, so
    // these breaks are unreachable today -- they are here because the thing they
    // guard is a write PAST A FIXED ARRAY into the rest of the player's state,
    // and "the check says it cannot happen" is not something to stake that on
    // when the tracklist grows by an imported directory at a time.
    if (sPlayer->mode == MODE_PLAYLISTS)
    {
        // Top level: only playlists that are not nested inside another.
        for (i = 0; i < RMP_PLAYLIST_COUNT && count < ROGUE_MUSIC_MAX_LIST_ROWS; i++)
        {
            if (sRogueMusicPlaylistParent[i] != RMP_NO_PARENT)
                continue;

            sPlayer->items[count].name = sRogueMusicPlaylistNames[i];
            sPlayer->items[count].id = i;
            count++;
        }
    }
    else if (sPlayer->mode == MODE_SUBLISTS)
    {
        // The parent's own tracks first, then its children. Without the first
        // entry a playlist that has both would hide its own contents.
        sPlayer->items[count].name = sRogueMusicPlaylistNames[sPlayer->parent];
        sPlayer->items[count].id = sPlayer->parent;
        count++;

        for (i = 0; i < RMP_PLAYLIST_COUNT && count < ROGUE_MUSIC_MAX_LIST_ROWS; i++)
        {
            if (sRogueMusicPlaylistParent[i] != sPlayer->parent)
                continue;

            sPlayer->items[count].name = sRogueMusicPlaylistNames[i];
            sPlayer->items[count].id = i;
            count++;
        }
    }
    else
    {
        for (i = 0; i < ROGUE_MUSIC_TRACK_COUNT && count < ROGUE_MUSIC_MAX_LIST_ROWS; i++)
        {
            if (sRogueMusicTracks[i].playlist != sPlayer->playlist)
                continue;

            sPlayer->items[count].name = sRogueMusicTracks[i].name;
            // The id is the index into the WHOLE table, not into this playlist,
            // so everything downstream can look a row up without also knowing
            // which playlist produced it.
            sPlayer->items[count].id = i;
            count++;
        }
    }

    sPlayer->itemCount = count;
}

static void RogueMusicPlayer_OpenList(void)
{
    struct ListMenuTemplate menuTemplate = {0};

    RogueMusicPlayer_BuildList();

    menuTemplate.items = sPlayer->items;
    menuTemplate.moveCursorFunc = RogueMusicPlayer_MoveCursor;
    menuTemplate.totalItems = sPlayer->itemCount;
    menuTemplate.maxShowed = min(sPlayer->itemCount, LIST_ROWS_SHOWN);
    menuTemplate.windowId = sPlayer->listWindowId;
    menuTemplate.header_X = 0;
    menuTemplate.item_X = 8;
    menuTemplate.cursor_X = 0;
    menuTemplate.upText_Y = 1;
    menuTemplate.cursorPal = 2;
    menuTemplate.fillValue = 1;
    menuTemplate.cursorShadowPal = 3;
    menuTemplate.lettersSpacing = 0;
    menuTemplate.itemVerticalPadding = 0;
    menuTemplate.scrollMultiple = LIST_NO_MULTIPLE_SCROLL;
    menuTemplate.fontId = FONT_NARROW;
    menuTemplate.cursorKind = CURSOR_BLACK_ARROW;

    sPlayer->listTaskId = ListMenuInit(&menuTemplate,
                                       sPlayer->scrollOffset[sPlayer->mode],
                                       sPlayer->selectedRow[sPlayer->mode]);
}

static void RogueMusicPlayer_CloseList(void)
{
    // Remember where the cursor was in THIS mode before tearing the list down --
    // DestroyListMenuTask is the only thing that will report it, and it is gone
    // afterwards.
    DestroyListMenuTask(sPlayer->listTaskId,
                        &sPlayer->scrollOffset[sPlayer->mode],
                        &sPlayer->selectedRow[sPlayer->mode]);
    FillWindowPixelBuffer(sPlayer->listWindowId, PIXEL_FILL(1));
}

// Called by the list menu on every cursor move and once on init, which is what
// keeps the info panel in step with the highlight without polling it.
static void RogueMusicPlayer_MoveCursor(s32 itemIndex, bool8 onInit,
                                        struct ListMenu *list)
{
    if (!onInit)
        PlaySE(SE_SELECT);

    sPlayer->highlighted = itemIndex;
    RogueMusicPlayer_DrawInfo();
}

// -------------------------------------------------------------------------
// The info panel
// -------------------------------------------------------------------------

static void RogueMusicPlayer_DrawInfo(void)
{
    const struct RogueMusicTrack *track;
    u8 windowId = sPlayer->infoWindowId;

    FillWindowPixelBuffer(windowId, PIXEL_FILL(1));

    AddTextPrinterParameterized(windowId, FONT_SMALL, sText_NowPlaying,
                                0, 1, TEXT_SKIP_DRAW, NULL);

    AddTextPrinterParameterized(windowId, FONT_NARROW,
                                sPlayer->nowPlaying == NO_TRACK
                                    ? sText_Nothing
                                    : sRogueMusicTracks[sPlayer->nowPlaying].name,
                                0, 13, TEXT_SKIP_DRAW, NULL);

    // Position within the queue, not just its length: "QUEUE 2/5" is the only
    // thing on screen that says SELECT+R has somewhere to go.
    if (RogueGbSounds_QueueCount() == 0)
    {
        AddTextPrinterParameterized(windowId, FONT_SMALL, sText_QueueEmpty,
                                    0, 33, TEXT_SKIP_DRAW, NULL);
    }
    else
    {
        ConvertIntToDecimalStringN(gStringVar1, RogueGbSounds_QueuePos() + 1,
                                   STR_CONV_MODE_LEFT_ALIGN, 2);
        ConvertIntToDecimalStringN(gStringVar2, RogueGbSounds_QueueCount(),
                                   STR_CONV_MODE_LEFT_ALIGN, 2);
        StringExpandPlaceholders(gStringVar4, sText_QueueFmt);
        AddTextPrinterParameterized(windowId, FONT_SMALL, gStringVar4,
                                    0, 33, TEXT_SKIP_DRAW, NULL);
    }

    AddTextPrinterParameterized(windowId, FONT_SMALL,
                                RogueGbSounds_IsEnabled()
                                    ? sText_GbSoundsOn
                                    : sText_GbSoundsOff,
                                0, 45, TEXT_SKIP_DRAW, NULL);

    // Only meaningful over a track, so the playlist level says nothing rather
    // than reporting the last track's answer.
    track = RogueMusicPlayer_HighlightedTrack();
    if (track != NULL)
    {
        const u8 *status;

        // A remix says what it is. It is an m4a arrangement on a PSG
        // voicegroup, not GB Sounds, and the flag does nothing to it.
        if (track->forced)
            status = sText_PsgRemix;
        else if (RogueMusicPlayer_GbsIdFor(track) != GBS_MUSIC_NONE)
            status = sText_HasGbVersion;
        else
            status = sText_NoGbVersion;

        AddTextPrinterParameterized(windowId, FONT_SMALL, status,
                                    0, 57, TEXT_SKIP_DRAW, NULL);
    }

    AddTextPrinterParameterized(windowId, FONT_SMALL,
                                sPlayer->mode == MODE_TRACKS
                                    ? sText_ControlsTracks
                                    : sText_ControlsPlaylists,
                                0, 69, TEXT_SKIP_DRAW, NULL);

    CopyWindowToVram(windowId, COPYWIN_GFX);
}

// -------------------------------------------------------------------------
// Open, run, close
// -------------------------------------------------------------------------

void RogueMusicPlayer_Open(void)
{
    u8 taskId;

    sPlayer = AllocZeroed(sizeof(*sPlayer));

    // The heap can refuse. Every line below dereferences this, so an unchecked
    // NULL is a hang on the frame the menu opens -- and the allocation used to
    // grow with every track imported, which is exactly the direction that makes
    // an unchecked alloc find you eventually. Failing to open is recoverable;
    // the field is still held by nobody, because nothing has been locked yet.
    if (sPlayer == NULL)
        return;

    sPlayer->mode = MODE_PLAYLISTS;
    // Not NO_TRACK: a track chosen on a previous open may still be playing, and
    // the panel has to name it rather than claim nothing is on.
    sPlayer->nowPlaying = RogueMusicPlayer_TrackForJukebox();

    // Hold the field ourselves rather than through a script's lockall. The
    // first version opened via special + waitstate and the lock did not hold:
    // the player could walk, open the bag and regenerate the floor while the
    // menu was up and reading the same button presses.
    LockPlayerFieldControls();
    FreezeObjectEvents();

    HideMapNamePopUpWindow();
    LoadMessageBoxAndBorderGfx();

    sPlayer->listWindowId = AddWindow(&sListWindowTemplate);
    sPlayer->infoWindowId = AddWindow(&sInfoWindowTemplate);

    DrawStdWindowFrame(sPlayer->listWindowId, FALSE);
    DrawStdWindowFrame(sPlayer->infoWindowId, FALSE);
    CopyWindowToVram(sPlayer->listWindowId, COPYWIN_GFX);

    RogueMusicPlayer_OpenList();
    RogueMusicPlayer_DrawInfo();

    taskId = CreateTask(Task_RogueMusicPlayer, 0);
    (void)taskId;
}

static void Task_RogueMusicPlayer(u8 taskId)
{
    s32 selection = ListMenu_ProcessInput(sPlayer->listTaskId);

    if (JOY_NEW(A_BUTTON))
    {
        if (sPlayer->mode != MODE_TRACKS)
        {
            u32 i;
            bool32 hasChildren = FALSE;

            PlaySE(SE_SELECT);

            if (sPlayer->mode == MODE_PLAYLISTS)
            {
                for (i = 0; i < RMP_PLAYLIST_COUNT; i++)
                {
                    if (sRogueMusicPlaylistParent[i] == selection)
                    {
                        hasChildren = TRUE;
                        break;
                    }
                }
            }

            RogueMusicPlayer_CloseList();

            if (hasChildren)
            {
                sPlayer->parent = selection;
                sPlayer->mode = MODE_SUBLISTS;
            }
            else
            {
                sPlayer->playlist = selection;
                sPlayer->mode = MODE_TRACKS;
            }

            // A level entered fresh starts at the top. Without this, the
            // remembered position from the LAST list at this level is applied
            // to the new one, which is out of range for any shorter list.
            sPlayer->scrollOffset[sPlayer->mode] = 0;
            sPlayer->selectedRow[sPlayer->mode] = 0;
            RogueMusicPlayer_OpenList();
            RogueMusicPlayer_DrawInfo();
        }
        else
        {
            PlaySE(SE_SELECT);
            RogueMusicPlayer_PlayTrack(selection);
            RogueMusicPlayer_DrawInfo();
        }
    }
    else if (JOY_NEW(B_BUTTON))
    {
        if (sPlayer->mode != MODE_PLAYLISTS)
        {
            PlaySE(SE_SELECT);
            RogueMusicPlayer_CloseList();
            // Back one level: tracks reached through a nested playlist return
            // to that submenu, not all the way out.
            if (sPlayer->mode == MODE_TRACKS
             && sRogueMusicPlaylistParent[sPlayer->playlist] != RMP_NO_PARENT)
                sPlayer->mode = MODE_SUBLISTS;
            else
                sPlayer->mode = MODE_PLAYLISTS;
            RogueMusicPlayer_OpenList();
            RogueMusicPlayer_DrawInfo();
        }
        else
        {
            PlaySE(SE_SELECT);
            RogueMusicPlayer_Close(taskId);
        }
    }
    else if (JOY_NEW(SELECT_BUTTON))
    {
        // The global field-music flag, flipped from in here. Deliberately NOT
        // RogueGbSounds_Toggle: that one restarts the MAP's music, which would
        // cut off whatever is being auditioned and start the field track over
        // the top of it.
        if (RogueGbSounds_IsEnabled())
            FlagClear(FLAG_SYS_GBS_ENABLED);
        else
            FlagSet(FLAG_SYS_GBS_ENABLED);

        // The field music now resolves differently, and the close path owes the
        // player a restart to make that audible even if nothing is auditioning.
        sPlayer->touchedMusic = TRUE;

        // Re-issue the CURRENT QUEUE ENTRY so the A/B is immediate. Advance(0)
        // rather than PlayTrack: PlayTrack means "play this now", which is a
        // queue of one, so toggling GB Sounds would silently throw away a queue
        // the player had just spent a minute building.
        RogueGbSounds_QueueAdvance(0);

        RogueMusicPlayer_DrawInfo();
    }
    else if (JOY_NEW(START_BUTTON))
    {
        // STOP means "stop the jukebox", and the map gets its own music back --
        // not silence. An m4aMPlayStop alone left the field mute until the next
        // warp happened to restart something, which is indistinguishable from a
        // hang in the sound engine and was reported as one. It empties the queue
        // too; see RogueGbSounds_ClearJukebox.
        RogueGbSounds_ClearJukebox();
        sPlayer->nowPlaying = NO_TRACK;
        // ClearJukebox has just re-issued the map's music through the flag, so
        // the close path has nothing left to put right.
        sPlayer->touchedMusic = FALSE;
        RogueMusicPlayer_DrawInfo();
    }
    else if (JOY_NEW(R_BUTTON))
    {
        // QUEUE. Reads the HIGHLIGHTED row, not `selection` -- ListMenu_ProcessInput
        // only returns a row on the frame A is pressed and gives LIST_NOTHING_CHOSEN
        // on every other, so `selection` is -1 here.
        //
        // Safe to take L and R at all because the list menu is built with
        // LIST_NO_MULTIPLE_SCROLL, the one scrollMultiple mode that does not bind
        // them to page scrolling.
        const struct RogueMusicTrack *track = RogueMusicPlayer_HighlightedTrack();

        if (track != NULL)
        {
            if (RogueGbSounds_QueuePush(track->songId,
                                        RogueMusicPlayer_QueuedGbsIdFor(track)))
            {
                PlaySE(SE_SELECT);
                sPlayer->touchedMusic = TRUE;
                // Queueing onto silence starts playing, so what is on may have
                // just changed. Re-derived rather than assumed.
                sPlayer->nowPlaying = RogueMusicPlayer_TrackForJukebox();
            }
            else
            {
                PlaySE(SE_FAILURE);   // full
            }

            RogueMusicPlayer_DrawInfo();
        }
    }
    else if (JOY_NEW(L_BUTTON))
    {
        // NEXT. Available at every level, not just over a track, because the
        // thing being skipped is what is PLAYING and that has nothing to do with
        // where the cursor happens to be.
        if (RogueGbSounds_QueueAdvance(1))
        {
            PlaySE(SE_SELECT);
            sPlayer->nowPlaying = RogueMusicPlayer_TrackForJukebox();
            RogueMusicPlayer_DrawInfo();
        }
    }
}

static void RogueMusicPlayer_Close(u8 taskId)
{
    // Order matters: tear the list down before the window it draws into.
    RogueMusicPlayer_CloseList();

    ClearStdWindowAndFrame(sPlayer->listWindowId, TRUE);
    ClearStdWindowAndFrame(sPlayer->infoWindowId, TRUE);
    RemoveWindow(sPlayer->listWindowId);
    RemoveWindow(sPlayer->infoWindowId);

    // THE MUSIC IS LEFT ALONE when a track is playing -- that is the point of the
    // player, and the map music funnels in overworld.c now keep it playing from
    // here on. The restore is only for the other case: the GB Sounds flag was
    // flipped with nothing auditioning, so the field track needs re-issuing for
    // the change to be heard before the next map load.
    if (RogueGbSounds_GetJukeboxSong() == MUS_DUMMY && sPlayer->touchedMusic)
        RogueGbSounds_StopAuditionAndRestoreMapMusic();

    // Name the track on the way out, on the same plate the floor name uses but
    // in the other corner. The pop-up copies the string, so it does not matter
    // that sPlayer is freed below -- and the plate's own thirty-frame wait means
    // it appears once the menu is already gone.
    RogueMusicPlayer_ShowNowPlaying();

    DestroyTask(taskId);

    Free(sPlayer);
    sPlayer = NULL;

    // The inverse of Open, and the same pair Debug_DestroyMenu_Full uses.
    UnfreezeObjectEvents();
    UnlockPlayerFieldControls();
}
