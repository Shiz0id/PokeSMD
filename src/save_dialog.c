#include "global.h"
#include "battle_pyramid.h"
#include "bg.h"
#include "constants/songs.h"
#include "event_data.h"
#include "event_object_lock.h"
#include "event_scripts.h"
#include "fieldmap.h"
#include "international_string_util.h"
#include "menu.h"
#include "new_game.h"
#include "overworld.h"
#include "palette.h"
#include "save.h"
#include "script.h"
#include "sound.h"
#include "start_menu.h"
#include "strings.h"
#include "string_util.h"
#include "text_window.h"
#include "unbound_start_menu.h"
#include "window.h"

static void HideSaveInfoWindow(void);
static u8 RunSaveCallback(void);
static u8 SaveDoSaveCallback(void);
static void HideSaveInfoWindow(void);
static void HideSaveMessageWindow(void);
static u8 SaveOverwriteInputCallback(void);
static u8 SaveConfirmOverwriteDefaultNoCallback(void);
static u8 SaveConfirmOverwriteCallback(void);
static void ShowSaveMessage(const u8* message, u8 (*saveCallback)(void));
static u8 SaveFileExistsCallback(void);
static u8 SaveSavingMessageCallback(void);
static u8 SaveConfirmInputCallback(void);
static u8 SaveYesNoCallback(void);
static void ShowSaveInfoWindow(void);
static u8 SaveConfirmSaveCallback(void);

// Battle Pyramid

static u8 BattlePyramidConfirmRetireCallback(void);
static u8 BattlePyramidRetireYesNoCallback(void);
static u8 BattlePyramidRetireInputCallback(void);
static bool8 BattlePyramidRetireReturnCallback(void);


EWRAM_DATA static u8 (*sSaveDialogCallback)(void) = NULL;
EWRAM_DATA static u8 sSaveDialogTimer = 0;
EWRAM_DATA static u8 sSaveInfoWindowId = 0;

static const struct BgTemplate sBgTemplates_LinkBattleSave[] =
    {
        {.bg = 0,
         .charBaseIndex = 2,
         .mapBaseIndex = 31,
         .screenSize = 0,
         .paletteMode = 0,
         .priority = 0,
         .baseTile = 0}};

static const struct WindowTemplate sWindowTemplates_LinkBattleSave[] =
    {
        {.bg = 0,
         .tilemapLeft = 2,
         .tilemapTop = 15,
         .width = 26,
         .height = 4,
         .paletteNum = 15,
         .baseBlock = 0x194},
        DUMMY_WIN_TEMPLATE};

static const struct WindowTemplate sSaveInfoWindowTemplate = {
    .bg = 0,
    .tilemapLeft = 1,
    .tilemapTop = 1,
    .width = 14,
    .height = 10,
    .paletteNum = 15,
    .baseBlock = 8};

enum UsmSaveStates {
    USM_SAVE_IN_PROGRESS,
    USM_SAVE_SUCCESS,
    USM_SAVE_CANCELED,
    USM_SAVE_ERROR
};

static u8 RunSaveCallback(void)
{
    // True if text is still printing
    if (RunTextPrintersAndIsPrinter0Active() == TRUE) {
        return USM_SAVE_IN_PROGRESS;
    }

    return sSaveDialogCallback();
}

static void SaveStartTimer(void)
{
    sSaveDialogTimer = 60;
}

static bool8 SaveSuccesTimer(void)
{
    sSaveDialogTimer--;

    if (JOY_HELD(A_BUTTON) || JOY_HELD(B_BUTTON)) {
        PlaySE(SE_SELECT);
        return TRUE;
    }
    if (sSaveDialogTimer == 0) {
        return TRUE;
    }

    return FALSE;
}

static bool8 SaveErrorTimer(void)
{
    if (sSaveDialogTimer != 0) {
        sSaveDialogTimer--;
    }
    else if (JOY_HELD(A_BUTTON)) {
        return TRUE;
    }

    return FALSE;
}

static u8 SaveReturnSuccessCallback(void)
{
    if (!IsSEPlaying() && SaveSuccesTimer()) {
        HideSaveInfoWindow();
        return USM_SAVE_SUCCESS;
    }
    else {
        return USM_SAVE_IN_PROGRESS;
    }
}

static u8 SaveSuccessCallback(void)
{
    if (!IsTextPrinterActiveOnWindow(0)) {
        PlaySE(SE_SAVE);
        sSaveDialogCallback = SaveReturnSuccessCallback;
    }

    return USM_SAVE_IN_PROGRESS;
}

static u8 SaveReturnErrorCallback(void)
{
    if (!SaveErrorTimer()) {
        return USM_SAVE_IN_PROGRESS;
    }
    else {
        HideSaveInfoWindow();
        return USM_SAVE_ERROR;
    }
}

static u8 SaveErrorCallback(void)
{
    if (!IsTextPrinterActiveOnWindow(0)) {
        PlaySE(SE_BOO);
        sSaveDialogCallback = SaveReturnErrorCallback;
    }

    return USM_SAVE_IN_PROGRESS;
}

static u8 SaveDoSaveCallback(void)
{
    u8 saveStatus;

    IncrementGameStat(GAME_STAT_SAVED_GAME);
    PausePyramidChallenge();

    if (gDifferentSaveFile == TRUE) {
        saveStatus = TrySavingData(SAVE_OVERWRITE_DIFFERENT_FILE);
        gDifferentSaveFile = FALSE;
    }
    else {
        saveStatus = TrySavingData(SAVE_NORMAL);
    }

    if (saveStatus == SAVE_STATUS_OK)
        ShowSaveMessage(gText_PlayerSavedGame, SaveSuccessCallback);
    else
        ShowSaveMessage(gText_SaveError, SaveErrorCallback);

    SaveStartTimer();
    return USM_SAVE_IN_PROGRESS;
}

static void HideSaveInfoWindow(void)
{
    ClearStdWindowAndFrame(sSaveInfoWindowId, FALSE);
    RemoveWindow(sSaveInfoWindowId);
}

static void HideSaveMessageWindow(void)
{
    ClearDialogWindowAndFrame(0, TRUE);
}

static u8 SaveOverwriteInputCallback(void)
{
    switch (Menu_ProcessInputNoWrapClearOnChoose()) {
        case 0:  // Yes
            sSaveDialogCallback = SaveSavingMessageCallback;
            return USM_SAVE_IN_PROGRESS;
        case MENU_B_PRESSED:
        case 1:  // No
            HideSaveInfoWindow();
            HideSaveMessageWindow();
            return USM_SAVE_CANCELED;
    }

    return USM_SAVE_IN_PROGRESS;
}

static u8 SaveConfirmOverwriteDefaultNoCallback(void)
{
    DisplayYesNoMenuWithDefault(1);  // Show Yes/No menu (No selected as default)
    sSaveDialogCallback = SaveOverwriteInputCallback;
    return USM_SAVE_IN_PROGRESS;
}

static u8 SaveConfirmOverwriteCallback(void)
{
    DisplayYesNoMenuDefaultYes();  // Show Yes/No menu
    sSaveDialogCallback = SaveOverwriteInputCallback;
    return USM_SAVE_IN_PROGRESS;
}

static void ShowSaveMessage(const u8* message, u8 (*saveCallback)(void))
{
    StringExpandPlaceholders(gStringVar4, message);
    LoadMessageBoxAndFrameGfx(0, TRUE);
    AddTextPrinterForMessage(TRUE);
    sSaveDialogCallback = saveCallback;
}

static u8 SaveFileExistsCallback(void)
{
    if (gDifferentSaveFile == TRUE) {
        ShowSaveMessage(gText_DifferentSaveFile, SaveConfirmOverwriteDefaultNoCallback);
    }
    else {
        ShowSaveMessage(gText_AlreadySavedFile, SaveConfirmOverwriteCallback);
    }

    return USM_SAVE_IN_PROGRESS;
}

static u8 SaveSavingMessageCallback(void)
{
    ShowSaveMessage(gText_SavingDontTurnOff, SaveDoSaveCallback);
    return USM_SAVE_IN_PROGRESS;
}

static u8 SaveConfirmInputCallback(void)
{
    switch (Menu_ProcessInputNoWrapClearOnChoose()) {
        case 0:  // Yes
            switch (gSaveFileStatus) {
                case SAVE_STATUS_EMPTY:
                case SAVE_STATUS_CORRUPT:
                    if (gDifferentSaveFile == FALSE && !SKIP_SAVE_CONFIRMATION) {
                        sSaveDialogCallback = SaveFileExistsCallback;
                        return USM_SAVE_IN_PROGRESS;
                    }
                    sSaveDialogCallback = SaveSavingMessageCallback;
                    return USM_SAVE_IN_PROGRESS;
                default:
                    if (SKIP_SAVE_CONFIRMATION)
                        sSaveDialogCallback = SaveSavingMessageCallback;
                    else
                        sSaveDialogCallback = SaveFileExistsCallback;
                    return USM_SAVE_IN_PROGRESS;
            }
        case MENU_B_PRESSED:
        case 1:  // No
            HideSaveInfoWindow();
            HideSaveMessageWindow();
            return USM_SAVE_CANCELED;
    }

    return USM_SAVE_IN_PROGRESS;
}

static u8 SaveYesNoCallback(void)
{
    DisplayYesNoMenuDefaultYes();  // Show Yes/No menu
    sSaveDialogCallback = SaveConfirmInputCallback;
    return USM_SAVE_IN_PROGRESS;
}

static void ShowSaveInfoWindow(void)
{
    struct WindowTemplate saveInfoWindow = sSaveInfoWindowTemplate;
    u8 gender;
    u8 color;
    u32 xOffset;
    u32 yOffset;

    if (!FlagGet(FLAG_SYS_POKEDEX_GET)) {
        saveInfoWindow.height -= 2;
    }

    sSaveInfoWindowId = AddWindow(&saveInfoWindow);
    DrawStdWindowFrame(sSaveInfoWindowId, FALSE);

    gender = gSaveBlock2Ptr->playerGender;
    color = TEXT_COLOR_RED;  // Red when female, blue when male.

    if (gender == MALE) {
        color = TEXT_COLOR_BLUE;
    }

    // Print region name
    yOffset = 1;
    BufferSaveMenuText(SAVE_MENU_LOCATION, gStringVar4, TEXT_COLOR_GREEN);
    AddTextPrinterParameterized(sSaveInfoWindowId, FONT_NORMAL, gStringVar4, 0, yOffset, TEXT_SKIP_DRAW, NULL);

    // Print player name
    yOffset += 16;
    AddTextPrinterParameterized(sSaveInfoWindowId, FONT_NORMAL, gText_SavingPlayer, 0, yOffset, TEXT_SKIP_DRAW, NULL);
    BufferSaveMenuText(SAVE_MENU_NAME, gStringVar4, color);
    xOffset = GetStringRightAlignXOffset(FONT_NORMAL, gStringVar4, 0x70);
    PrintPlayerNameOnWindow(sSaveInfoWindowId, gStringVar4, xOffset, yOffset);

    // Print the finished-run count, where upstream prints badges.
    //
    // RogueDungeon_ApplyNewGameUnlocks sets all eight badge flags at run start
    // so high-level Pokemon obey, which means the badge count reads 8 on every
    // save that has ever existed and the slot is dead. VAR_ROGUE_RUNS_COMPLETED
    // is the useful thing to show there instead.
    //
    // This is the SECOND time this slot has had to be repointed. The first was
    // reported from play, because the continue window and this dialog share no
    // code at all -- and this port replaces the dialog wholesale, so it landed
    // back on gText_SavingBadges. It failed to build rather than silently
    // reverting, which is the only reason it was caught here.
    yOffset += 16;
    AddTextPrinterParameterized(sSaveInfoWindowId, FONT_NORMAL, gText_SavingRuns, 0, yOffset, TEXT_SKIP_DRAW, NULL);
    BufferSaveMenuText(SAVE_MENU_RUNS, gStringVar4, color);
    xOffset = GetStringRightAlignXOffset(FONT_NORMAL, gStringVar4, 0x70);
    AddTextPrinterParameterized(sSaveInfoWindowId, FONT_NORMAL, gStringVar4, xOffset, yOffset, TEXT_SKIP_DRAW, NULL);

    if (FlagGet(FLAG_SYS_POKEDEX_GET) == TRUE) {
        // Print pokedex count
        yOffset += 16;
        AddTextPrinterParameterized(sSaveInfoWindowId, FONT_NORMAL, gText_SavingPokedex, 0, yOffset, TEXT_SKIP_DRAW, NULL);
        BufferSaveMenuText(SAVE_MENU_CAUGHT, gStringVar4, color);
        xOffset = GetStringRightAlignXOffset(FONT_NORMAL, gStringVar4, 0x70);
        AddTextPrinterParameterized(sSaveInfoWindowId, FONT_NORMAL, gStringVar4, xOffset, yOffset, TEXT_SKIP_DRAW, NULL);
    }

    // Print play time
    yOffset += 16;
    AddTextPrinterParameterized(sSaveInfoWindowId, FONT_NORMAL, gText_SavingTime, 0, yOffset, TEXT_SKIP_DRAW, NULL);
    BufferSaveMenuText(SAVE_MENU_PLAY_TIME, gStringVar4, color);
    xOffset = GetStringRightAlignXOffset(FONT_NORMAL, gStringVar4, 0x70);
    AddTextPrinterParameterized(sSaveInfoWindowId, FONT_NORMAL, gStringVar4, xOffset, yOffset, TEXT_SKIP_DRAW, NULL);

    CopyWindowToVram(sSaveInfoWindowId, COPYWIN_GFX);
}

static u8 SaveConfirmSaveCallback(void)
{
    ShowSaveInfoWindow();

    if (InBattlePyramid_()) {
        ShowSaveMessage(gText_BattlePyramidConfirmRest, SaveYesNoCallback);
    }
    else {
        ShowSaveMessage(gText_ConfirmSave, SaveYesNoCallback);
    }
    return USM_SAVE_IN_PROGRESS;
}

void SaveDialog_InitSave(void)
{
    SaveMapView();
    LoadUserWindowBorderGfx_(0, 8, BG_PLTT_ID(14));
    sSaveDialogCallback = SaveConfirmSaveCallback;
}

void Task_SaveDialogHandleSave(u8 taskId)
{
    switch (RunSaveCallback()) {
        case USM_SAVE_IN_PROGRESS:
            break;
        case USM_SAVE_SUCCESS:
        case USM_SAVE_CANCELED:  // Back to start menu
            ClearDialogWindowAndFrameToTransparent(0, TRUE);
            ScriptUnfreezeObjectEvents();
            UnlockPlayerFieldControls();
            DestroyTask(taskId);
            break;
        case USM_SAVE_ERROR:  // Close start menu
            ClearDialogWindowAndFrameToTransparent(0, TRUE);
            ScriptUnfreezeObjectEvents();
            UnlockPlayerFieldControls();
            SoftResetInBattlePyramid();
            DestroyTask(taskId);
            break;
    }
}

void Task_SaveDialogHandleBattlePyramidRetire(u8 taskId)
{
    switch (RunSaveCallback())
    {
    case USM_SAVE_SUCCESS: // No (Stay in battle pyramid)
        gMenuCallback = BattlePyramidRetireReturnCallback;
        break;
    case USM_SAVE_IN_PROGRESS:
        break;
    case USM_SAVE_CANCELED: // Yes (Retire from battle pyramid)
        ClearDialogWindowAndFrameToTransparent(0, TRUE);
        ScriptUnfreezeObjectEvents();
        UnlockPlayerFieldControls();
        ScriptContext_SetupScript(BattlePyramid_Retire);
        break;
    }
}

// Battle Pyramid

void SaveDialog_InitBattlePyramidRetire(void)
{
    sSaveDialogCallback = BattlePyramidConfirmRetireCallback;
}

static u8 BattlePyramidConfirmRetireCallback(void)
{
    ShowSaveMessage(gText_BattlePyramidConfirmRetire, BattlePyramidRetireYesNoCallback);

    return USM_SAVE_IN_PROGRESS;
}

static u8 BattlePyramidRetireYesNoCallback(void)
{
    DisplayYesNoMenuWithDefault(1); // Show Yes/No menu (No selected as default)
    sSaveDialogCallback = BattlePyramidRetireInputCallback;

    return USM_SAVE_IN_PROGRESS;
}

static u8 BattlePyramidRetireInputCallback(void)
{
    switch (Menu_ProcessInputNoWrapClearOnChoose())
    {
    case 0: // Yes
        return USM_SAVE_CANCELED;
    case MENU_B_PRESSED:
    case 1: // No
        HideSaveMessageWindow();
        return USM_SAVE_SUCCESS;
    }

    return USM_SAVE_IN_PROGRESS;
}

static bool8 BattlePyramidRetireReturnCallback(void)
{
    Usm_InitStartMenu();
    return FALSE;
}
