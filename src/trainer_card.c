#include "global.h"
#include "scanline_effect.h"
#include "palette.h"
#include "task.h"
#include "main.h"
#include "window.h"
#include "malloc.h"
#include "link.h"
#include "bg.h"
#include "sound.h"
#include "frontier_pass.h"
#include "overworld.h"
#include "menu.h"
#include "text.h"
#include "event_data.h"
#include "easy_chat.h"
#include "money.h"
#include "strings.h"
#include "string_util.h"
#include "trainer_card.h"
#include "gpu_regs.h"
#include "international_string_util.h"
#include "pokedex.h"
#include "pokemon_icon.h"
#include "graphics.h"
#include "pokemon_icon.h"
#include "trainer_pokemon_sprites.h"
#include "contest_util.h"
#include "decompress.h"
#include "constants/songs.h"
#include "constants/game_stat.h"
#include "constants/battle_frontier.h"
#include "constants/rgb.h"
#include "constants/trainers.h"
#include "constants/union_room.h"
#include "rogue_journal.h"
#include "rogue_dungeon.h"

enum {
    WIN_MSG,
    WIN_CARD_TEXT,
    WIN_TRAINER_PIC,
};

struct TrainerCardData
{
    u8 mainState;
    u8 printState;
    u8 gfxLoadState;
    u8 bgPalLoadState;
    u8 flipDrawState;
    bool8 isLink;
    u8 timeColonBlinkTimer;
    bool8 timeColonInvisible;
    bool8 onBack;
    bool8 allowDMACopy;
    bool8 hasPokedex;
    bool8 hasHofResult;
    bool8 hasLinkResults;
    bool8 hasBattleTowerWins;
    bool8 unused_E;
    bool8 unused_F;
    bool8 hasTrades;
    u8 badgeCount[NUM_BADGES];
    u8 easyChatProfile[TRAINER_CARD_PROFILE_LENGTH][13];
    u8 textPlayersCard[70];
    u8 textHofTime[70];
    u8 textLinkBattleType[140];
    u8 textLinkBattleWins[70];
    u8 textLinkBattleLosses[140];
    u8 textNumTrades[140];
    u8 textBerryCrushPts[140];
    u8 textUnionRoomStats[70];
    u8 textNumLinkPokeblocks[70];
    u8 textNumLinkContests[70];
    u8 textBattleFacilityStat[70];
    // The run journal's page, rendered up front by BufferJournalPage and only
    // printed by the back's print state machine. See RogueJournal_BufferLine for
    // why expanding inside the print path is not an option.
    //
    // hasJournal is the SINGLE GATE, and it is the link guard: it is assigned in
    // exactly one place, under !sData->isLink, and every site that draws or
    // reads the journal tests it. Without that, a partner's card would render
    // THIS console's run history under THEIR name - the journal comes from
    // SaveBlock3, which is always the local player's, while every other field on
    // the back comes from the received struct TrainerCard.
    bool8 hasJournal;
    u8 journalRows;   // how many of the arrays below are filled, 0..PAGE
    u8 journalPage;   // 0 is the NEWEST page, which is where the card opens
    // Cached at card setup rather than recomputed per frame. Nothing can append
    // while the card is open - it is a full-screen callback with no script
    // running behind it - so the count cannot move under the paging.
    u8 journalPages;
    u8 journalLines[ROGUE_JOURNAL_ROWS_PER_PAGE][ROGUE_JOURNAL_LINE_LENGTH];
    u8 journalFloor[ROGUE_JOURNAL_ROWS_PER_PAGE];      // 0 = draw no floor tag
    bool8 journalSeparator[ROGUE_JOURNAL_ROWS_PER_PAGE];
    u16 monIconPal[16 * PARTY_SIZE];
    s8 flipBlendY;
    bool8 timeColonNeedDraw;
    u8 cardType;
    bool8 isHoenn;
    u16 blendColor;
    MainCallback callback2;
    struct TrainerCard trainerCard;
    u16 frontTilemap[600];
    u16 backTilemap[600];
    u16 bgTilemap[600];
    u8 badgeTiles[0x80 * NUM_BADGES];
    u8 stickerTiles[0x200];
    u8 cardTiles[0x2300];
    u16 cardTilemapBuffer[0x1000];
    u16 bgTilemapBuffer[0x1000];
    u16 cardTop;
    u8 language;
};

// EWRAM
EWRAM_DATA struct TrainerCard gTrainerCards[4] = {0};
EWRAM_DATA static struct TrainerCardData *sData = NULL;

//this file's functions
static void VblankCb_TrainerCard(void);
static void HblankCb_TrainerCard(void);
static void BlinkTimeColon(void);
static void CB2_TrainerCard(void);
static void CloseTrainerCard(u8 task);
static bool8 PrintAllOnCardFront(void);
static void DrawTrainerCardWindow(u8);
static void CreateTrainerCardTrainerPic(void);
static void DrawCardScreenBackground(u16 *);
static void DrawCardFrontOrBack(u16 *);
static void DrawStarsAndBadgesOnCard(void);
static void PrintTimeOnCard(void);
static void FlipTrainerCard(void);
static bool8 IsCardFlipTaskActive(void);
static bool8 LoadCardGfx(void);
static void CB2_InitTrainerCard(void);
static u32 GetCappedGameStat(u8 statId, u32 maxValue);
static bool8 HasAllFrontierSymbols(void);
static u16 GetCaughtMonsCount(void);
static void SetPlayerCardData(struct TrainerCard *, u8);
static void TrainerCard_GenerateCardForPlayer(struct TrainerCard *);
static u8 VersionToCardType(enum GameVersion);
static void SetDataFromTrainerCard(void);
static void InitGpuRegs(void);
static void ResetGpuRegs(void);
static void InitBgsAndWindows(void);
static void SetTrainerCardCb2(void);
static void SetUpTrainerCardTask(void);
static void InitTrainerCardData(void);
static u8 GetSetCardType(void);
static void PrintNameOnCardFront(void);
static void PrintIdOnCard(void);
static void PrintMoneyOnCard(void);
static void PrintPokedexOnCard(void);
static void PrintProfilePhraseOnCard(void);
static bool8 PrintAllOnCardBack(void);
static void PrintNameOnCardBack(void);
static void PrintHofDebutTimeOnCard(void);
static void PrintLinkBattleResultsOnCard(void);
static void PrintTradesStringOnCard(void);
static void PrintBerryCrushStringOnCard(void);
static void PrintPokeblockStringOnCard(void);
static void PrintUnionStringOnCard(void);
static void PrintContestStringOnCard(void);
static void PrintPokemonIconsOnCard(void);
static void PrintBattleFacilityStringOnCard(void);
static void PrintStickersOnCard(void);
static void BufferTextsVarsForCardPage2(void);
static void BufferNameForCardBack(void);
static void BufferHofDebutTime(void);
static void BufferLinkBattleResults(void);
static void BufferNumTrades(void);
static void BufferBerryCrushPoints(void);
static void BufferUnionRoomStats(void);
static void BufferLinkPokeblocksNum(void);
static void BufferLinkContestNum(void);
static void BufferBattleFacilityStats(void);
static void PrintStatOnBackOfCard(u8 top, const u8 *str1, u8 *str2, const u8 *color);
static void BufferJournalPage(void);
static void PrintJournalOnCardBack(void);
static bool8 TryChangeJournalPage(void);
static void LoadStickerGfx(void);
static u8 SetCardBgsAndPals(void);
static void DrawCardBackStats(void);
static void Task_DoCardFlipTask(u8);
static bool8 Task_BeginCardFlip(struct Task *task);
static bool8 Task_AnimateCardFlipDown(struct Task *task);
static bool8 Task_DrawFlippedCardSide(struct Task *task);
static bool8 Task_SetCardFlipped(struct Task *task);
static bool8 Task_AnimateCardFlipUp(struct Task *task);
static bool8 Task_EndCardFlip(struct Task *task);
static void UpdateCardFlipRegs(u16);
static void LoadMonIconGfx(void);

static const u32 sTrainerCardStickers_Gfx[]      = INCGFX_U32("graphics/trainer_card/frlg/stickers.png", ".4bpp.smol");
static const u16 sUnused_Pal[]                   = INCGFX_U16("graphics/trainer_card/unused.pal", ".gbapal");
static const u16 sHoennTrainerCardBronze_Pal[]   = INCGFX_U16("graphics/trainer_card/bronze.pal", ".gbapal");
static const u16 sKantoTrainerCardGreen_Pal[]    = INCGFX_U16("graphics/trainer_card/frlg/green.pal", ".gbapal");
static const u16 sHoennTrainerCardCopper_Pal[]   = INCGFX_U16("graphics/trainer_card/copper.pal", ".gbapal");
static const u16 sKantoTrainerCardBronze_Pal[]   = INCGFX_U16("graphics/trainer_card/frlg/bronze.pal", ".gbapal");
static const u16 sHoennTrainerCardSilver_Pal[]   = INCGFX_U16("graphics/trainer_card/silver.pal", ".gbapal");
static const u16 sKantoTrainerCardSilver_Pal[]   = INCGFX_U16("graphics/trainer_card/frlg/silver.pal", ".gbapal");
static const u16 sHoennTrainerCardGold_Pal[]     = INCGFX_U16("graphics/trainer_card/gold.pal", ".gbapal");
static const u16 sKantoTrainerCardGold_Pal[]     = INCGFX_U16("graphics/trainer_card/frlg/gold.pal", ".gbapal");
static const u16 sHoennTrainerCardFemaleBg_Pal[] = INCGFX_U16("graphics/trainer_card/female_bg.pal", ".gbapal");
static const u16 sKantoTrainerCardFemaleBg_Pal[] = INCGFX_U16("graphics/trainer_card/frlg/female_bg.pal", ".gbapal");
static const u16 sHoennTrainerCardBadges_Pal[]   = INCGFX_U16("graphics/trainer_card/badges.png", ".gbapal");
static const u16 sKantoTrainerCardBadges_Pal[]   = INCGFX_U16("graphics/trainer_card/frlg/badges.png", ".gbapal");
static const u16 sTrainerCardStar_Pal[]          = INCGFX_U16("graphics/trainer_card/star.pal", ".gbapal");
static const u16 sTrainerCardSticker1_Pal[]      = INCGFX_U16("graphics/trainer_card/frlg/stickers1.pal", ".gbapal");
static const u16 sTrainerCardSticker2_Pal[]      = INCGFX_U16("graphics/trainer_card/frlg/stickers2.pal", ".gbapal");
static const u16 sTrainerCardSticker3_Pal[]      = INCGFX_U16("graphics/trainer_card/frlg/stickers3.pal", ".gbapal");
static const u16 sTrainerCardSticker4_Pal[]      = INCGFX_U16("graphics/trainer_card/frlg/stickers4.pal", ".gbapal");
static const u32 sHoennTrainerCardBadges_Gfx[]   = INCGFX_U32("graphics/trainer_card/badges.png", ".4bpp.smol");
static const u32 sKantoTrainerCardBadges_Gfx[]   = INCGFX_U32("graphics/trainer_card/frlg/badges.png", ".4bpp.smol");

static const struct BgTemplate sTrainerCardBgTemplates[4] =
{
    {
        .bg = 0,
        .charBaseIndex = 0,
        .mapBaseIndex = 27,
        .screenSize = 2,
        .paletteMode = 0,
        .priority = 2,
        .baseTile = 0
    },
    {
        .bg = 1,
        .charBaseIndex = 2,
        .mapBaseIndex = 29,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 0,
        .baseTile = 0
    },
    {
        .bg = 2,
        .charBaseIndex = 0,
        .mapBaseIndex = 30,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 3,
        .baseTile = 0
    },
    {
        .bg = 3,
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 1,
        .baseTile = 192
    },
};

static const struct WindowTemplate sTrainerCardWindowTemplates[] =
{
    [WIN_MSG] = {
        .bg = 1,
        .tilemapLeft = 2,
        .tilemapTop = 15,
        .width = 27,
        .height = 4,
        .paletteNum = 15,
        .baseBlock = 0x253,
    },
    [WIN_CARD_TEXT] = {
        .bg = 1,
        .tilemapLeft = 1,
        .tilemapTop = 1,
        .width = 28,
        .height = 18,
        .paletteNum = 15,
        .baseBlock = 0x1,
    },
    [WIN_TRAINER_PIC] = {
        .bg = 3,
        .tilemapLeft = 19,
        .tilemapTop = 5,
        .width = 9,
        .height = 10,
        .paletteNum = 8,
        .baseBlock = 0x150,
    },
    DUMMY_WIN_TEMPLATE
};

static const u16 *const sHoennTrainerCardPals[] =
{
    gHoennTrainerCardGreen_Pal,  // Default (0 stars)
    sHoennTrainerCardBronze_Pal, // 1 star
    sHoennTrainerCardCopper_Pal, // 2 stars
    sHoennTrainerCardSilver_Pal, // 3 stars
    sHoennTrainerCardGold_Pal,   // 4 stars
};

static const u16 *const sKantoTrainerCardPals[] =
{
    gKantoTrainerCardBlue_Pal,   // Default (0 stars)
    sKantoTrainerCardGreen_Pal,  // 1 star
    sKantoTrainerCardBronze_Pal, // 2 stars
    sKantoTrainerCardSilver_Pal, // 3 stars
    sKantoTrainerCardGold_Pal,   // 4 stars
};

static const u8 sTrainerCardTextColors[] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_LIGHT_GRAY};
static const u8 sTrainerCardStatColors[] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_RED, TEXT_COLOR_LIGHT_RED};
static const u8 sTimeColonInvisibleTextColors[6] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_TRANSPARENT, TEXT_COLOR_TRANSPARENT};

static const u8 sTrainerPicOffset[2][PLAYER_LOOK_COUNT][2] =
{
    // Kanto
    {
        [MALE]   = {13, 4},
        [FEMALE] = {13, 4}
    },
    // Hoenn
    {
        [MALE]   = {1, 0},
        [FEMALE] = {1, 0}
    },
};

static const u8 sTrainerPicFacilityClass[][PLAYER_LOOK_COUNT] =
{
    [CARD_TYPE_FRLG] =
    {
        [MALE]   = FACILITY_CLASS_RED,
        [FEMALE] = FACILITY_CLASS_LEAF
    },
    [CARD_TYPE_RS] =
    {
        [MALE]   = FACILITY_CLASS_RS_BRENDAN,
        [FEMALE] = FACILITY_CLASS_RS_MAY
    },
    [CARD_TYPE_EMERALD] =
    {
        [MALE]   = FACILITY_CLASS_BRENDAN,
        [FEMALE] = FACILITY_CLASS_MAY
    }
};

static bool8 (*const sTrainerCardFlipTasks[])(struct Task *) =
{
    Task_BeginCardFlip,
    Task_AnimateCardFlipDown,
    Task_DrawFlippedCardSide,
    Task_SetCardFlipped,
    Task_AnimateCardFlipUp,
    Task_EndCardFlip,
};

static void VblankCb_TrainerCard(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
    BlinkTimeColon();
    if (sData->allowDMACopy)
        DmaCopy16(3, &gScanlineEffectRegBuffers[0], &gScanlineEffectRegBuffers[1], 0x140);
}

static void HblankCb_TrainerCard(void)
{
    u16 backup;
    u16 bgVOffset;

    backup = REG_IME;
    REG_IME = 0;
    bgVOffset = gScanlineEffectRegBuffers[1][REG_VCOUNT & 0xFF];
    REG_BG0VOFS = bgVOffset;
    REG_IME = backup;
}

static void CB2_TrainerCard(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

static void CloseTrainerCard(u8 taskId)
{
    SetMainCallback2(sData->callback2);
    FreeAllWindowBuffers();
    FREE_AND_SET_NULL(sData);
    DestroyTask(taskId);
}

// States for Task_TrainerCard. Skips the initial states, which are done once in order
#define STATE_HANDLE_INPUT_FRONT  10
#define STATE_HANDLE_INPUT_BACK   11
#define STATE_WAIT_FLIP_TO_BACK   12
#define STATE_WAIT_FLIP_TO_FRONT  13
#define STATE_CLOSE_CARD          14
#define STATE_WAIT_LINK_PARTNER   15
#define STATE_CLOSE_CARD_LINK     16
// Turning a journal page. TWO STATES AND NOT A LOOP: PrintAllOnCardBack is a
// state machine that draws one step per frame, and the only place in this file
// that runs its steps back to back is Task_DrawFlippedCardSide, which does so
// only when !gReceivedRemoteLinkPlayers - that exception exists for the link
// timing, not as a licence to draw synchronously. Paging reuses the same
// per-frame budget the first draw was built around.
#define STATE_REDRAW_BACK_CLEAR   17
#define STATE_REDRAW_BACK_PRINT   18

static void Task_TrainerCard(u8 taskId)
{
    switch (sData->mainState)
    {
    // Draw card initially
    case 0:
        if (!IsDma3ManagerBusyWithBgCopy())
        {
            FillWindowPixelBuffer(WIN_CARD_TEXT, PIXEL_FILL(0));
            sData->mainState++;
        }
        break;
    case 1:
        if (PrintAllOnCardFront())
            sData->mainState++;
        break;
    case 2:
        DrawTrainerCardWindow(WIN_CARD_TEXT);
        sData->mainState++;
        break;
    case 3:
        FillWindowPixelBuffer(WIN_TRAINER_PIC, PIXEL_FILL(0));
        CreateTrainerCardTrainerPic();
        DrawTrainerCardWindow(WIN_TRAINER_PIC);
        sData->mainState++;
        break;
    case 4:
        DrawCardScreenBackground(sData->bgTilemap);
        sData->mainState++;
        break;
    case 5:
        DrawCardFrontOrBack(sData->frontTilemap);
        sData->mainState++;
        break;
    case 6:
        DrawStarsAndBadgesOnCard();
        sData->mainState++;
        break;
    // Fade in
    case 7:
        if (gWirelessCommType == 1 && gReceivedRemoteLinkPlayers == TRUE)
        {
            LoadWirelessStatusIndicatorSpriteGfx();
            CreateWirelessStatusIndicatorSprite(DISPLAY_WIDTH - 10, DISPLAY_HEIGHT - 10);
        }
        BlendPalettes(PALETTES_ALL, 16, sData->blendColor);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, sData->blendColor);
        SetVBlankCallback(VblankCb_TrainerCard);
        sData->mainState++;
        break;
    case 8:
        if (!UpdatePaletteFade() && !IsDma3ManagerBusyWithBgCopy())
        {
            PlaySE(SE_RG_CARD_OPEN);
            sData->mainState = STATE_HANDLE_INPUT_FRONT;
        }
        break;
    case 9:
        if (!IsSEPlaying())
            sData->mainState++;
        break;
    case STATE_HANDLE_INPUT_FRONT:
        // Blink the : in play time
        if (!gReceivedRemoteLinkPlayers && sData->timeColonNeedDraw)
        {
            PrintTimeOnCard();
            DrawTrainerCardWindow(WIN_CARD_TEXT);
            sData->timeColonNeedDraw = FALSE;
        }
        if (JOY_NEW(A_BUTTON))
        {
            FlipTrainerCard();
            PlaySE(SE_RG_CARD_FLIP);
            sData->mainState = STATE_WAIT_FLIP_TO_BACK;
        }
        else if (JOY_NEW(B_BUTTON))
        {
            if (gReceivedRemoteLinkPlayers && sData->isLink && InUnionRoom() == TRUE)
            {
                sData->mainState = STATE_WAIT_LINK_PARTNER;
            }
            else
            {
                BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, sData->blendColor);
                sData->mainState = STATE_CLOSE_CARD;
            }
        }
        break;
    case STATE_WAIT_FLIP_TO_BACK:
        if (IsCardFlipTaskActive() && Overworld_IsRecvQueueAtMax() != TRUE)
        {
            PlaySE(SE_RG_CARD_OPEN);
            sData->mainState = STATE_HANDLE_INPUT_BACK;
        }
        break;
    case STATE_HANDLE_INPUT_BACK:
        // BEFORE the A and B handling, and it consumes nothing they use: the
        // d-pad is unbound on both sides of this card, so paging displaces no
        // existing control. A still closes and B still flips.
        if (TryChangeJournalPage())
        {
            PlaySE(SE_SELECT);
            sData->mainState = STATE_REDRAW_BACK_CLEAR;
            break;
        }

        if (JOY_NEW(B_BUTTON))
        {
            if (gReceivedRemoteLinkPlayers && sData->isLink && InUnionRoom() == TRUE)
            {
                sData->mainState = STATE_WAIT_LINK_PARTNER;
            }
            else if (gReceivedRemoteLinkPlayers)
            {
                BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, sData->blendColor);
                sData->mainState = STATE_CLOSE_CARD;
            }
            else
            {
                FlipTrainerCard();
                sData->mainState = STATE_WAIT_FLIP_TO_FRONT;
                PlaySE(SE_RG_CARD_FLIP);
            }
        }
        else if (JOY_NEW(A_BUTTON))
        {
           if (gReceivedRemoteLinkPlayers && sData->isLink && InUnionRoom() == TRUE)
           {
               sData->mainState = STATE_WAIT_LINK_PARTNER;
           }
           else
           {
               BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, sData->blendColor);
               sData->mainState = STATE_CLOSE_CARD;
           }
        }
        break;
    case STATE_WAIT_LINK_PARTNER:
        SetCloseLinkCallback();
        DrawDialogueFrame(WIN_MSG, TRUE);
        AddTextPrinterParameterized(WIN_MSG, FONT_NORMAL, gText_WaitingTrainerFinishReading, 0, 1, 255, 0);
        CopyWindowToVram(WIN_MSG, COPYWIN_FULL);
        sData->mainState = STATE_CLOSE_CARD_LINK;
        break;
    case STATE_CLOSE_CARD_LINK:
        if (!gReceivedRemoteLinkPlayers)
        {
            BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, sData->blendColor);
            sData->mainState = STATE_CLOSE_CARD;
        }
        break;
    case STATE_CLOSE_CARD:
        if (!UpdatePaletteFade())
            CloseTrainerCard(taskId);
        break;
    case STATE_WAIT_FLIP_TO_FRONT:
        if (IsCardFlipTaskActive() && Overworld_IsRecvQueueAtMax() != TRUE)
        {
            sData->mainState = STATE_HANDLE_INPUT_FRONT;
            PlaySE(SE_RG_CARD_OPEN);
        }
        break;
    // Re-renders the back in place. No flip, no BG0 scroll, no HBlank work -
    // the card is already face down and only the text layer changes.
    case STATE_REDRAW_BACK_CLEAR:
        FillWindowPixelBuffer(WIN_CARD_TEXT, PIXEL_FILL(0));
        // Re-expanded for the new page BEFORE anything prints, for the reason
        // RogueJournal_BufferLine gives: it goes through gStringVar1 and 2, and
        // the print path uses those too.
        BufferJournalPage();
        // Set explicitly rather than trusted. PrintAllOnCardBack resets it on
        // completion, so it is 0 here today - but that is the previous caller's
        // invariant, not this state's, and paging is the first thing to re-enter
        // that machine from outside the flip.
        sData->printState = 0;
        sData->mainState = STATE_REDRAW_BACK_PRINT;
        break;
    case STATE_REDRAW_BACK_PRINT:
        if (PrintAllOnCardBack())
        {
            DrawTrainerCardWindow(WIN_CARD_TEXT);
            sData->mainState = STATE_HANDLE_INPUT_BACK;
        }
        break;
   }
}

static bool8 LoadCardGfx(void)
{
    switch (sData->gfxLoadState)
    {
    case 0:
        if (sData->cardType != CARD_TYPE_FRLG)
            DecompressDataWithHeaderWram(gHoennTrainerCardBg_Tilemap, sData->bgTilemap);
        else
            DecompressDataWithHeaderWram(gKantoTrainerCardBg_Tilemap, sData->bgTilemap);
        break;
    case 1:
        if (sData->cardType != CARD_TYPE_FRLG)
            DecompressDataWithHeaderWram(gHoennTrainerCardBack_Tilemap, sData->backTilemap);
        else
            DecompressDataWithHeaderWram(gKantoTrainerCardBack_Tilemap, sData->backTilemap);
        break;
    case 2:
        if (!sData->isLink)
        {
            if (sData->cardType != CARD_TYPE_FRLG)
                DecompressDataWithHeaderWram(gHoennTrainerCardFront_Tilemap, sData->frontTilemap);
            else
                DecompressDataWithHeaderWram(gKantoTrainerCardFront_Tilemap, sData->frontTilemap);
        }
        else
        {
            if (sData->cardType != CARD_TYPE_FRLG)
                DecompressDataWithHeaderWram(gHoennTrainerCardFrontLink_Tilemap, sData->frontTilemap);
            else
                DecompressDataWithHeaderWram(gKantoTrainerCardFrontLink_Tilemap, sData->frontTilemap);
        }
        break;
    case 3:
        if (sData->cardType != CARD_TYPE_FRLG)
            DecompressDataWithHeaderWram(sHoennTrainerCardBadges_Gfx, sData->badgeTiles);
        else
            DecompressDataWithHeaderWram(sKantoTrainerCardBadges_Gfx, sData->badgeTiles);
        break;
    case 4:
        if (sData->cardType != CARD_TYPE_FRLG)
            DecompressDataWithHeaderWram(gHoennTrainerCard_Gfx, sData->cardTiles);
        else
            DecompressDataWithHeaderWram(gKantoTrainerCard_Gfx, sData->cardTiles);
        break;
    case 5:
        if (sData->cardType == CARD_TYPE_FRLG)
            DecompressDataWithHeaderWram(sTrainerCardStickers_Gfx, sData->stickerTiles);
        break;
    default:
        sData->gfxLoadState = 0;
        return TRUE;
    }
    sData->gfxLoadState++;
    return FALSE;
}

static void CB2_InitTrainerCard(void)
{
    switch (gMain.state)
    {
    case 0:
        ResetGpuRegs();
        SetUpTrainerCardTask();
        gMain.state++;
        break;
    case 1:
        DmaClear32(3, (void *)OAM, OAM_SIZE);
        gMain.state++;
        break;
    case 2:
        if (!sData->blendColor)
            DmaClear16(3, (void *)PLTT, PLTT_SIZE);
        gMain.state++;
        break;
    case 3:
        ResetSpriteData();
        FreeAllSpritePalettes();
        ResetPaletteFade();
        gMain.state++;
    case 4:
        InitBgsAndWindows();
        gMain.state++;
        break;
    case 5:
        LoadMonIconGfx();
        gMain.state++;
        break;
    case 6:
        if (LoadCardGfx() == TRUE)
            gMain.state++;
        break;
    case 7:
        LoadStickerGfx();
        gMain.state++;
        break;
    case 8:
        InitGpuRegs();
        gMain.state++;
        break;
    case 9:
        BufferTextsVarsForCardPage2();
        gMain.state++;
        break;
    case 10:
        if (SetCardBgsAndPals() == TRUE)
            gMain.state++;
        break;
    default:
        SetTrainerCardCb2();
        break;
    }
}

static u32 GetCappedGameStat(u8 statId, u32 maxValue)
{
    u32 statValue = GetGameStat(statId);

    return min(maxValue, statValue);
}

static bool8 HasAllFrontierSymbols(void)
{
    u8 i;
    for (i = 0; i < NUM_FRONTIER_FACILITIES; i++)
    {
        if (!FlagGet(FLAG_SYS_TOWER_SILVER + 2 * i) || !FlagGet(FLAG_SYS_TOWER_GOLD + 2 * i))
            return FALSE;
    }
    return TRUE;
}

// STARS ARE RUNS CLEARED, not the vanilla four conditions.
//
// A run has no Hall of Fame, no contest paintings and no Frontier symbols, and
// its Pokedex resets with the party - so all four vanilla conditions are
// unreachable and every card in this build was permanently a zero-star card.
// The star count also picks the card's PALETTE, so the one thing the roguelike
// does have a ladder of is what should drive it: clear a run, the card goes up a
// grade.
//
// CLAMPED TO 4, and that is not cosmetic. stars indexes sHoennTrainerCardPals[],
// which has exactly five entries, and it is a field sent over link. A fifth win
// unclamped reads a palette pointer past the end of that table.
u32 CountPlayerTrainerStars(void)
{
    u32 runs = VarGet(VAR_ROGUE_RUNS_COMPLETED);

    return runs < 4 ? runs : 4;
}

// The vanilla version counted GAME_STAT_ENTERED_HOF, HasAllRegionalMons,
// CountPlayerMuseumPaintings and HasAllFrontierSymbols. It is deleted rather
// than kept behind a branch because none of the four is reachable in a run and a
// dead copy would only rot; `git log` has it if a future mode needs it back.

static void SetPlayerCardData(struct TrainerCard *trainerCard, u8 cardType)
{
    u32 playTime;
    u8 i;

    trainerCard->gender = gSaveBlock2Ptr->playerGender;
    trainerCard->playTimeHours = gSaveBlock2Ptr->playTimeHours;
    trainerCard->playTimeMinutes = gSaveBlock2Ptr->playTimeMinutes;

    playTime = GetGameStat(GAME_STAT_FIRST_HOF_PLAY_TIME);
    if (!GetGameStat(GAME_STAT_ENTERED_HOF))
        playTime = 0;

    trainerCard->hofDebutHours = playTime >> 16;
    trainerCard->hofDebutMinutes = (playTime >> 8) & 0xFF;
    trainerCard->hofDebutSeconds = playTime & 0xFF;
    if ((playTime >> 16) > 999)
    {
        trainerCard->hofDebutHours = 999;
        trainerCard->hofDebutMinutes = 59;
        trainerCard->hofDebutSeconds = 59;
    }

    trainerCard->hasPokedex = FlagGet(FLAG_SYS_POKEDEX_GET);
    trainerCard->caughtAllHoenn = HasAllRegionalMons();
    trainerCard->caughtMonsCount = GetCaughtMonsCount();

    trainerCard->trainerId = (gSaveBlock2Ptr->playerTrainerId[1] << 8) | gSaveBlock2Ptr->playerTrainerId[0];

    trainerCard->linkBattleWins = GetCappedGameStat(GAME_STAT_LINK_BATTLE_WINS, 9999);
    trainerCard->linkBattleLosses = GetCappedGameStat(GAME_STAT_LINK_BATTLE_LOSSES, 9999);

    trainerCard->pokemonTrades = GetCappedGameStat(GAME_STAT_POKEMON_TRADES, 0xFFFF);

    trainerCard->money = GetMoney(&gSaveBlock1Ptr->money);
    trainerCard->stars = CountPlayerTrainerStars();

    for (i = 0; i < TRAINER_CARD_PROFILE_LENGTH; i++)
        trainerCard->easyChatProfile[i] = gSaveBlock1Ptr->easyChatProfile[i];

    StringCopy(trainerCard->playerName, gSaveBlock2Ptr->playerName);

    switch (cardType)
    {
    case CARD_TYPE_EMERALD:
        trainerCard->battleTowerWins = 0;
        trainerCard->battleTowerStraightWins = 0;
        trainerCard->contestsWithFriends = GetCappedGameStat(GAME_STAT_WON_LINK_CONTEST, 999);
        trainerCard->pokeblocksWithFriends = GetCappedGameStat(GAME_STAT_POKEBLOCKS_WITH_FRIENDS, 0xFFFF);
        if (CountPlayerMuseumPaintings() >= CONTEST_CATEGORIES_COUNT)
            trainerCard->hasAllPaintings = TRUE;
        break;
    case CARD_TYPE_FRLG:
        trainerCard->battleTowerWins = 0;
        trainerCard->battleTowerStraightWins = 0;
        trainerCard->contestsWithFriends = 0;
        trainerCard->pokeblocksWithFriends = 0;
        trainerCard->hasAllPaintings = 0;
        trainerCard->linkPoints.berryCrush = GetCappedGameStat(GAME_STAT_PLAYED_BERRY_CRUSH, 0xFFFF);
        trainerCard->unionRoomNum = GetCappedGameStat(GAME_STAT_NUM_UNION_ROOM_BATTLES, 0xFFFF);
        trainerCard->shouldDrawStickers = TRUE;
        trainerCard->stickers[0] = VarGet(VAR_HOF_BRAG_STATE);
        trainerCard->stickers[1] = VarGet(VAR_EGG_BRAG_STATE);
        trainerCard->stickers[2] = VarGet(VAR_LINK_WIN_BRAG_STATE);

        trainerCard->monIconTint = VarGet(VAR_TRAINER_CARD_MON_ICON_TINT_IDX);

        trainerCard->monSpecies[0] = VarGet(VAR_TRAINER_CARD_MON_ICON_1);
        trainerCard->monSpecies[1] = VarGet(VAR_TRAINER_CARD_MON_ICON_2);
        trainerCard->monSpecies[2] = VarGet(VAR_TRAINER_CARD_MON_ICON_3);
        trainerCard->monSpecies[3] = VarGet(VAR_TRAINER_CARD_MON_ICON_4);
        trainerCard->monSpecies[4] = VarGet(VAR_TRAINER_CARD_MON_ICON_5);
        trainerCard->monSpecies[5] = VarGet(VAR_TRAINER_CARD_MON_ICON_6);
        break;
    }
}

static void TrainerCard_GenerateCardForPlayer(struct TrainerCard *trainerCard)
{
    memset(trainerCard, 0, sizeof(struct TrainerCard));
    trainerCard->version = GAME_VERSION;
    SetPlayerCardData(trainerCard, VersionToCardType(GAME_VERSION));

    if (!IS_FRLG)
    {
        trainerCard->hasAllFrontierSymbols = HasAllFrontierSymbols();
        trainerCard->frontierBP = gSaveBlock2Ptr->frontier.cardBattlePoints;
    }

    if (trainerCard->gender == FEMALE)
        trainerCard->unionRoomClass = gUnionRoomFacilityClasses[(trainerCard->trainerId % NUM_UNION_ROOM_CLASSES) + NUM_UNION_ROOM_CLASSES];
    else
        trainerCard->unionRoomClass = gUnionRoomFacilityClasses[trainerCard->trainerId % NUM_UNION_ROOM_CLASSES];
}

void TrainerCard_GenerateCardForLinkPlayer(struct TrainerCard *trainerCard)
{
    memset(trainerCard, 0, 0x60);
    trainerCard->version = GAME_VERSION;
    SetPlayerCardData(trainerCard, VersionToCardType(GAME_VERSION));

    if (!IS_FRLG)
    {
        trainerCard->linkHasAllFrontierSymbols = HasAllFrontierSymbols();
        *((u16 *)&trainerCard->linkPoints.frontier) = gSaveBlock2Ptr->frontier.cardBattlePoints;
    }

    if (trainerCard->gender == FEMALE)
        trainerCard->unionRoomClass = gUnionRoomFacilityClasses[(trainerCard->trainerId % NUM_UNION_ROOM_CLASSES) + NUM_UNION_ROOM_CLASSES];
    else
        trainerCard->unionRoomClass = gUnionRoomFacilityClasses[trainerCard->trainerId % NUM_UNION_ROOM_CLASSES];
}

void CopyTrainerCardData(struct TrainerCard *dst, struct TrainerCard *src, u8 gameVersion)
{
    memset(dst, 0, sizeof(struct TrainerCard));
    dst->version = gameVersion;

    switch (VersionToCardType(gameVersion))
    {
    case CARD_TYPE_FRLG:
        memcpy(dst, src, 0x60);
        break;
    case CARD_TYPE_RS:
        memcpy(dst, src, 0x38);
        break;
    case CARD_TYPE_EMERALD:
        memcpy(dst, src, 0x60);
        dst->linkPoints.frontier = 0;
        dst->hasAllFrontierSymbols = src->linkHasAllFrontierSymbols;
        dst->frontierBP = *((u16 *)&src->linkPoints.frontier);
        break;
    }
}

static void SetDataFromTrainerCard(void)
{
    u8 i;
    u32 badgeFlag;

    sData->hasPokedex = FALSE;
    sData->hasHofResult = FALSE;
    sData->hasLinkResults = FALSE;
    sData->hasBattleTowerWins = FALSE;
    sData->unused_E = FALSE;
    sData->unused_F = FALSE;
    sData->hasTrades = FALSE;
    memset(sData->badgeCount, 0, sizeof(sData->badgeCount));

    // THE LINK GUARD, and the only assignment of hasJournal anywhere.
    //
    // Every other field on the back comes from sData->trainerCard, which for a
    // received card is the PARTNER'S data. The journal does not: it comes from
    // SaveBlock3, which is always this console's. So a journal drawn on a link
    // card would be the local player's run history printed under someone else's
    // name, with nothing on screen looking wrong.
    //
    // An empty journal falls through to the vanilla stat rows rather than
    // showing six blank lines - which is also what a fresh save sees before its
    // first run has opened.
    sData->hasJournal = (!sData->isLink && RogueJournal_Count() != 0);

    // The card always opens on the newest page. Both of these are derived here,
    // beside the gate, so nothing downstream has to ask the journal again.
    sData->journalPage = 0;
    sData->journalPages = 0;
    if (sData->hasJournal)
        sData->journalPages = (RogueJournal_Count() + ROGUE_JOURNAL_ROWS_PER_PAGE - 1)
                            / ROGUE_JOURNAL_ROWS_PER_PAGE;

    if (sData->trainerCard.hasPokedex)
        sData->hasPokedex++;

    if (sData->trainerCard.hofDebutHours
     || sData->trainerCard.hofDebutMinutes
     || sData->trainerCard.hofDebutSeconds)
        sData->hasHofResult++;

    if (sData->trainerCard.linkBattleWins || sData->trainerCard.linkBattleLosses)
        sData->hasLinkResults++;
    if (sData->trainerCard.pokemonTrades)
        sData->hasTrades++;
    if (sData->trainerCard.battleTowerWins || sData->trainerCard.battleTowerStraightWins)
        sData->hasBattleTowerWins++;

    // THE BADGE ROW IS DUNGEONS CLEARED THIS RUN, not badges.
    //
    // RogueDungeon_ApplyNewGameUnlocks sets FLAG_BADGE01_GET through 08 at new
    // game so adopted boss aces obey their trainer, so the vanilla loop here lit
    // all eight on every card from the first frame of a new save - eight slots
    // saying nothing. The run has a real ladder of exactly this shape.
    //
    // EIGHT SLOTS AND FOURTEEN DUNGEONS, so this shows the gym stretch only.
    // That is the honest reading rather than a compromise: the row is eight
    // 16x16 cells at a fixed 3-tile pitch (see DrawStarsAndBadgesOnCard), the
    // Elite Four are a different kind of thing, and a ninth badge has nowhere to
    // go. Past dungeon 8 the row simply reads full.
    //
    // Link-safe without a guard of its own: DrawStarsAndBadgesOnCard only draws
    // badges when !sData->isLink, and badgeCount is read nowhere else.
    badgeFlag = RogueDungeon_DungeonsClearedThisRun();
    for (i = 0; i < NUM_BADGES; i++)
    {
        if (i < badgeFlag)
            sData->badgeCount[i]++;
    }
}

static void InitGpuRegs(void)
{
    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_WIN0_ON | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
    ShowBg(0);
    ShowBg(1);
    ShowBg(2);
    ShowBg(3);
    SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT1_BG0 | BLDCNT_EFFECT_DARKEN);
    SetGpuReg(REG_OFFSET_BLDY, 0);
    SetGpuReg(REG_OFFSET_WININ, WININ_WIN0_BG_ALL | WININ_WIN0_OBJ | WININ_WIN0_CLR);
    SetGpuReg(REG_OFFSET_WINOUT, WINOUT_WIN01_BG1 | WINOUT_WIN01_BG2 | WINOUT_WIN01_BG3 | WINOUT_WIN01_OBJ);
    SetGpuReg(REG_OFFSET_WIN0V, DISPLAY_HEIGHT);
    SetGpuReg(REG_OFFSET_WIN0H, DISPLAY_WIDTH);
    if (gReceivedRemoteLinkPlayers)
        EnableInterrupts(INTR_FLAG_VBLANK | INTR_FLAG_HBLANK | INTR_FLAG_VCOUNT | INTR_FLAG_TIMER3 | INTR_FLAG_SERIAL);
    else
        EnableInterrupts(INTR_FLAG_VBLANK | INTR_FLAG_HBLANK);
}

static void UpdateCardFlipRegs(u16 cardTop)
{
    s8 blendY = (cardTop + 40) / 10;

    if (blendY <= 4)
        blendY = 0;
    sData->flipBlendY = blendY;
    SetGpuReg(REG_OFFSET_BLDY, sData->flipBlendY);
    SetGpuReg(REG_OFFSET_WIN0V, WIN_RANGE(sData->cardTop, DISPLAY_HEIGHT - sData->cardTop));
}

static void ResetGpuRegs(void)
{
    SetVBlankCallback(NULL);
    SetHBlankCallback(NULL);
    SetGpuReg(REG_OFFSET_DISPCNT, 0);
    SetGpuReg(REG_OFFSET_BG0CNT, 0);
    SetGpuReg(REG_OFFSET_BG1CNT, 0);
    SetGpuReg(REG_OFFSET_BG2CNT, 0);
    SetGpuReg(REG_OFFSET_BG3CNT, 0);
}

static void InitBgsAndWindows(void)
{
    ResetBgsAndClearDma3BusyFlags(0);
    InitBgsFromTemplates(0, sTrainerCardBgTemplates, ARRAY_COUNT(sTrainerCardBgTemplates));
    ChangeBgX(0, 0, BG_COORD_SET);
    ChangeBgY(0, 0, BG_COORD_SET);
    ChangeBgX(1, 0, BG_COORD_SET);
    ChangeBgY(1, 0, BG_COORD_SET);
    ChangeBgX(2, 0, BG_COORD_SET);
    ChangeBgY(2, 0, BG_COORD_SET);
    ChangeBgX(3, 0, BG_COORD_SET);
    ChangeBgY(3, 0, BG_COORD_SET);
    InitWindows(sTrainerCardWindowTemplates);
    DeactivateAllTextPrinters();
    LoadMessageBoxAndBorderGfx();
}

static void SetTrainerCardCb2(void)
{
    SetMainCallback2(CB2_TrainerCard);
}

static void SetUpTrainerCardTask(void)
{
    ResetTasks();
    ScanlineEffect_Stop();
    CreateTask(Task_TrainerCard, 0);
    InitTrainerCardData();
    SetDataFromTrainerCard();
}

static bool8 PrintAllOnCardFront(void)
{
    switch (sData->printState)
    {
    case 0:
        PrintNameOnCardFront();
        break;
    case 1:
        PrintIdOnCard();
        break;
    case 2:
        PrintMoneyOnCard();
        break;
    case 3:
        PrintPokedexOnCard();
        break;
    case 4:
        PrintTimeOnCard();
        break;
    case 5:
        PrintProfilePhraseOnCard();
        break;
    default:
        sData->printState = 0;
        return TRUE;
    }
    sData->printState++;
    return FALSE;
}

static bool8 PrintAllOnCardBack(void)
{
    // THE JOURNAL REPLACES THE STAT ROWS, it does not share the page with them.
    // The six rows it draws are exactly the six PrintStatOnBackOfCard uses, and
    // on this build four of those six are permanently empty anyway - link
    // battles, trades, Pokeblocks with friends and link contests are all zero in
    // a roguelike, so SetDataFromTrainerCard already suppresses them.
    //
    // A LINK CARD, OR A JOURNAL WITH NOTHING IN IT, FALLS THROUGH TO THE VANILLA
    // ROWS UNCHANGED. That is the whole of the link behaviour: nothing below is
    // reached with hasJournal set, and nothing above it is skipped without.
    if (sData->hasJournal)
    {
        switch (sData->printState)
        {
        case 0:
            PrintNameOnCardBack();
            break;
        case 1:
            PrintJournalOnCardBack();
            break;
        default:
            sData->printState = 0;
            return TRUE;
        }
        sData->printState++;
        return FALSE;
    }

    switch (sData->printState)
    {
    case 0:
        PrintNameOnCardBack();
        break;
    case 1:
        PrintHofDebutTimeOnCard();
        break;
    case 2:
        PrintLinkBattleResultsOnCard();
        break;
    case 3:
        PrintTradesStringOnCard();
        break;
    case 4:
        PrintBerryCrushStringOnCard();
        PrintPokeblockStringOnCard();
        break;
    case 5:
        PrintUnionStringOnCard();
        PrintContestStringOnCard();
        break;
    case 6:
        PrintPokemonIconsOnCard();
        PrintBattleFacilityStringOnCard();
        break;
    case 7:
        PrintStickersOnCard();
        break;
    default:
        sData->printState = 0;
        return TRUE;
    }
    sData->printState++;
    return FALSE;
}

static void BufferTextsVarsForCardPage2(void)
{
    BufferNameForCardBack();
    BufferJournalPage();
    BufferHofDebutTime();
    BufferLinkBattleResults();
    BufferNumTrades();
    BufferBerryCrushPoints();
    BufferUnionRoomStats();
    BufferLinkPokeblocksNum();
    BufferLinkContestNum();
    BufferBattleFacilityStats();
}

static void PrintNameOnCardFront(void)
{
    u8 buffer[32];
    u8 *txtPtr;
    txtPtr = StringCopy(buffer, gText_TrainerCardName);
    StringCopy(txtPtr, sData->trainerCard.playerName);
    ConvertInternationalString(txtPtr, sData->language);
    if (sData->cardType == CARD_TYPE_FRLG)
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, 20, 28, sTrainerCardTextColors, TEXT_SKIP_DRAW, buffer);
    else
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, 16, 33, sTrainerCardTextColors, TEXT_SKIP_DRAW, buffer);
}

static void PrintIdOnCard(void)
{
    u8 buffer[32];
    u8 *txtPtr;
    s32 xPos;
    u32 top;
    txtPtr = StringCopy(buffer, gText_TrainerCardIDNo);
    ConvertIntToDecimalStringN(txtPtr, sData->trainerCard.trainerId, STR_CONV_MODE_LEADING_ZEROS, 5);
    if (sData->cardType == CARD_TYPE_FRLG)
    {
        xPos = GetStringCenterAlignXOffset(FONT_NORMAL, buffer, 80) + 132;
        top = 9;
    }
    else
    {
        xPos = GetStringCenterAlignXOffset(FONT_NORMAL, buffer, 96) + 120;
        top = 9;
    }

    AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, xPos, top, sTrainerCardTextColors, TEXT_SKIP_DRAW, buffer);
}

static void PrintMoneyOnCard(void)
{
    s32 xOffset;
    u8 top;

    if (!sData->isHoenn)
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, 20, 56, sTrainerCardTextColors, TEXT_SKIP_DRAW, gText_TrainerCardMoney);
    else
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, 16, 57, sTrainerCardTextColors, TEXT_SKIP_DRAW, gText_TrainerCardMoney);

    ConvertIntToDecimalStringN(gStringVar1, sData->trainerCard.money, STR_CONV_MODE_LEFT_ALIGN, MAX_MONEY_DIGITS);
    StringExpandPlaceholders(gStringVar4, gText_PokedollarVar1);
    if (!sData->isHoenn)
    {
        xOffset = GetStringRightAlignXOffset(FONT_NORMAL, gStringVar4, 144);
        top = 56;
    }
    else
    {
        xOffset = GetStringRightAlignXOffset(FONT_NORMAL, gStringVar4, 128);
        top = 57;
    }
    AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, xOffset, top, sTrainerCardTextColors, TEXT_SKIP_DRAW, gStringVar4);
}

static u16 GetCaughtMonsCount(void)
{
    if (IsNationalPokedexEnabled())
        return GetNationalPokedexCount(FLAG_GET_CAUGHT);
    else
        return GetRegionalPokedexCount(FLAG_GET_CAUGHT);
}

static void PrintPokedexOnCard(void)
{
    s32 xOffset;
    u8 top;
    if (FlagGet(FLAG_SYS_POKEDEX_GET))
    {
        if (!sData->isHoenn)
            AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, 20, 72, sTrainerCardTextColors, TEXT_SKIP_DRAW, gText_TrainerCardPokedex);
        else
            AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, 16, 73, sTrainerCardTextColors, TEXT_SKIP_DRAW, gText_TrainerCardPokedex);
        StringCopy(ConvertIntToDecimalStringN(gStringVar4, sData->trainerCard.caughtMonsCount, STR_CONV_MODE_LEFT_ALIGN, 4), gText_EmptyString6);
        if (!sData->isHoenn)
        {
            xOffset = GetStringRightAlignXOffset(FONT_NORMAL, gStringVar4, 144);
            top = 72;
        }
        else
        {
            xOffset = GetStringRightAlignXOffset(FONT_NORMAL, gStringVar4, 128);
            top = 73;
        }
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, xOffset, top, sTrainerCardTextColors, TEXT_SKIP_DRAW, gStringVar4);
    }
}

static const u8 *const sTimeColonTextColors[] = {sTrainerCardTextColors, sTimeColonInvisibleTextColors};

static void PrintTimeOnCard(void)
{
    u16 hours;
    u16 minutes;
    s32 width;
    u32 x, y, totalWidth;

    if (!sData->isHoenn)
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, 20, 88, sTrainerCardTextColors, TEXT_SKIP_DRAW, gText_TrainerCardTime);
    else
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, 16, 89, sTrainerCardTextColors, TEXT_SKIP_DRAW, gText_TrainerCardTime);

    if (sData->isLink)
    {
        hours = sData->trainerCard.playTimeHours;
        minutes = sData->trainerCard.playTimeMinutes;
    }
    else
    {
        hours = gSaveBlock2Ptr->playTimeHours;
        minutes = gSaveBlock2Ptr->playTimeMinutes;
    }

    if (hours > 999)
        hours = 999;
    if (minutes > 59)
        minutes = 59;
    width = GetStringWidth(FONT_NORMAL, gText_Colon2, 0);

    if (!sData->isHoenn)
    {
        x = 144;
        y = 88;
    }
    else
    {
        x = 128;
        y = 89;
    }
    totalWidth = width + 30;
    x -= totalWidth;

    FillWindowPixelRect(WIN_CARD_TEXT, PIXEL_FILL(0), x, y, totalWidth, 15);
    ConvertIntToDecimalStringN(gStringVar4, hours, STR_CONV_MODE_RIGHT_ALIGN, 3);
    AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, x, y, sTrainerCardTextColors, TEXT_SKIP_DRAW, gStringVar4);
    x += 18;
    AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, x, y, sTimeColonTextColors[sData->timeColonInvisible], TEXT_SKIP_DRAW, gText_Colon2);
    x += width;
    ConvertIntToDecimalStringN(gStringVar4, minutes, STR_CONV_MODE_LEADING_ZEROS, 2);
    AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, x, y, sTrainerCardTextColors, TEXT_SKIP_DRAW, gStringVar4);
}

static void PrintProfilePhraseOnCard(void)
{
    static const u8 yOffsetsLine1[] = {113, 104};
    static const u8 yOffsetsLine2[] = {129, 120};

    if (sData->isLink)
    {
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, 8, yOffsetsLine1[sData->isHoenn], sTrainerCardTextColors, TEXT_SKIP_DRAW, sData->easyChatProfile[0]);
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, GetStringWidth(FONT_NORMAL, sData->easyChatProfile[0], 0) + 14, yOffsetsLine1[sData->isHoenn], sTrainerCardTextColors, TEXT_SKIP_DRAW, sData->easyChatProfile[1]);
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, 8, yOffsetsLine2[sData->isHoenn], sTrainerCardTextColors, TEXT_SKIP_DRAW, sData->easyChatProfile[2]);
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, GetStringWidth(FONT_NORMAL, sData->easyChatProfile[2], 0) + 14, yOffsetsLine2[sData->isHoenn], sTrainerCardTextColors, TEXT_SKIP_DRAW, sData->easyChatProfile[3]);
    }
}

static void BufferNameForCardBack(void)
{
    StringCopy(sData->textPlayersCard, sData->trainerCard.playerName);
    ConvertInternationalString(sData->textPlayersCard, sData->language);
    if (sData->cardType != CARD_TYPE_FRLG)
    {
        StringCopy(gStringVar1, sData->textPlayersCard);
        StringExpandPlaceholders(sData->textPlayersCard, gText_Var1sTrainerCard);
    }
}

static void PrintNameOnCardBack(void)
{
    if (!sData->isHoenn)
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, 136, 9, sTrainerCardTextColors, TEXT_SKIP_DRAW, sData->textPlayersCard);
    else
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, GetStringRightAlignXOffset(FONT_NORMAL, sData->textPlayersCard, 216), 9, sTrainerCardTextColors, TEXT_SKIP_DRAW, sData->textPlayersCard);
}

static const u8 sText_HofTime[] = _("{STR_VAR_1}:{STR_VAR_2}:{STR_VAR_3}");

static void BufferHofDebutTime(void)
{
    if (sData->hasHofResult)
    {
        ConvertIntToDecimalStringN(gStringVar1, sData->trainerCard.hofDebutHours, STR_CONV_MODE_RIGHT_ALIGN, 3);
        ConvertIntToDecimalStringN(gStringVar2, sData->trainerCard.hofDebutMinutes, STR_CONV_MODE_LEADING_ZEROS, 2);
        ConvertIntToDecimalStringN(gStringVar3, sData->trainerCard.hofDebutSeconds, STR_CONV_MODE_LEADING_ZEROS, 2);
        StringExpandPlaceholders(sData->textHofTime, sText_HofTime);
    }
}

static void PrintStatOnBackOfCard(u8 top, const u8 *statName, u8 *stat, const u8 *color)
{
    static const u8 xOffsets[] = {8, 16};
    static const u8 widths[] = {216, 216};

    AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, xOffsets[sData->isHoenn], top * 16 + 33, sTrainerCardTextColors, TEXT_SKIP_DRAW, statName);
    AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, GetStringRightAlignXOffset(FONT_NORMAL, stat, widths[sData->isHoenn]), top * 16 + 33, color, TEXT_SKIP_DRAW, stat);
}

static void PrintHofDebutTimeOnCard(void)
{
    if (sData->hasHofResult)
        PrintStatOnBackOfCard(0, gText_HallOfFameDebut, sData->textHofTime, sTrainerCardStatColors);
}

static const u8 sText_JournalFloorTag[] = _("F{STR_VAR_1}");
static const u8 sText_JournalPage[] = _("Page {STR_VAR_1}/{STR_VAR_2}");

// UP is newer, DOWN is older - the direction the page already reads in.
// Returns TRUE when the page moved, which is what puts the card into the redraw.
//
// CLAMPS RATHER THAN WRAPPING. A wrap on a paged list makes the ends invisible:
// the player cannot tell "this is the oldest thing I have" from "it scrolled
// round again", and the whole point of the journal is knowing how far back it
// goes. The queue in the music player wraps for the opposite reason - a
// one-entry queue there has to be able to restart itself.
static bool8 TryChangeJournalPage(void)
{
    if (!sData->hasJournal || sData->journalPages <= 1)
        return FALSE;

    if (JOY_NEW(DPAD_DOWN) && sData->journalPage + 1 < sData->journalPages)
    {
        sData->journalPage++;
        return TRUE;
    }

    if (JOY_NEW(DPAD_UP) && sData->journalPage != 0)
    {
        sData->journalPage--;
        return TRUE;
    }

    return FALSE;
}

// Renders one page of the journal into sData, newest first.
//
// BUFFERED UP FRONT, like every other string on this side of the card, because
// RogueJournal_BufferLine expands through gStringVar1 and gStringVar2 - and the
// FRONT of the card uses gStringVar1 for the money row. Expanding inside the
// per-frame print path would interleave the two.
static void BufferJournalPage(void)
{
    u32 i;

    sData->journalRows = 0;

    if (!sData->hasJournal)
        return;

    for (i = 0; i < ROGUE_JOURNAL_ROWS_PER_PAGE; i++)
    {
        const struct RogueJournalEntry *entry = RogueJournal_EntryFromNewest(
            sData->journalPage * ROGUE_JOURNAL_ROWS_PER_PAGE + i);

        // Fewer entries than rows is the normal case for a young save, not an
        // error - the count is what bounds the draw, not the array size.
        if (entry == NULL)
            break;

        RogueJournal_BufferLine(entry, sData->journalLines[i]);
        sData->journalFloor[i] = entry->floor;
        sData->journalSeparator[i] = RogueJournal_KindInfo(entry->kind)->isSeparator;
        sData->journalRows++;
    }
}

// One journal row, in the geometry PrintStatOnBackOfCard already establishes:
// y = top * 16 + 33, so the six rows land on the rules the back art draws and
// nothing needs repainting.
//
// LEFT ALIGNED, unlike the stat printer, which right-aligns its value to x=216.
// A journal line is a sentence rather than a label and a number.
static void PrintJournalOnCardBack(void)
{
    static const u8 xOffsets[] = {8, 16};

    u32 i;
    u8 x = xOffsets[sData->isHoenn];

    // The page indicator shares the NAME row rather than spending one of the six
    // journal rows on itself. The name is right-aligned to x=216 there and the
    // left half of that row is empty on the back, so the two do not meet.
    // Hidden on a single page, where "Page 1/1" is noise.
    if (sData->journalPages > 1)
    {
        ConvertIntToDecimalStringN(gStringVar1, sData->journalPage + 1,
                                   STR_CONV_MODE_LEFT_ALIGN, 2);
        ConvertIntToDecimalStringN(gStringVar2, sData->journalPages,
                                   STR_CONV_MODE_LEFT_ALIGN, 2);
        StringExpandPlaceholders(gStringVar4, sText_JournalPage);
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NORMAL, x, 9,
                                     sTrainerCardTextColors, TEXT_SKIP_DRAW,
                                     gStringVar4);
    }

    for (i = 0; i < sData->journalRows; i++)
    {
        u8 y = i * 16 + 33;

        // A separator spans the row in the stat colour; the reader partitions
        // runs on these, so they have to be findable at a glance rather than
        // read. Ordinary entries get the text colour and a floor tag.
        if (sData->journalSeparator[i])
        {
            AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NARROW, x, y,
                                         sTrainerCardStatColors, TEXT_SKIP_DRAW,
                                         sData->journalLines[i]);
            continue;
        }

        // Floor 0 means the entry is not floor-scoped, which is what every
        // separator carries - but an ordinary kind may too, so this is a test
        // rather than an else. UNEXERCISED TODAY: all three kinds that exist are
        // separators, so nothing reaches this branch until the event kinds land.
        if (sData->journalFloor[i] != 0)
        {
            ConvertIntToDecimalStringN(gStringVar1, sData->journalFloor[i],
                                       STR_CONV_MODE_LEFT_ALIGN, 3);
            StringExpandPlaceholders(gStringVar4, sText_JournalFloorTag);
            AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NARROW, x, y,
                                         sTrainerCardStatColors, TEXT_SKIP_DRAW,
                                         gStringVar4);
        }

        // FONT_NARROW for the entry text, and the floor tag above it too. The
        // row is 224px less the x=16 margin and another 32 for the tag, so about
        // 176px - which FONT_NORMAL spends in roughly 29 characters and the
        // longest line here exceeds. The tag matches so the two do not sit at
        // visibly different weights on one row.
        AddTextPrinterParameterized3(WIN_CARD_TEXT, FONT_NARROW, x + 32, y,
                                     sTrainerCardTextColors, TEXT_SKIP_DRAW,
                                     sData->journalLines[i]);
    }
}

static const u8 *const sLinkBattleTexts[] =
{
    [CARD_TYPE_FRLG]    = gText_LinkBattles,
    [CARD_TYPE_RS]      = gText_LinkCableBattles,
    [CARD_TYPE_EMERALD] = gText_LinkBattles
};

static void BufferLinkBattleResults(void)
{
    if (sData->hasLinkResults)
    {
        StringCopy(sData->textLinkBattleType, sLinkBattleTexts[sData->cardType]);
        ConvertIntToDecimalStringN(sData->textLinkBattleWins, sData->trainerCard.linkBattleWins, STR_CONV_MODE_LEFT_ALIGN, 4);
        ConvertIntToDecimalStringN(sData->textLinkBattleLosses, sData->trainerCard.linkBattleLosses, STR_CONV_MODE_LEFT_ALIGN, 4);
    }
}

static void PrintLinkBattleResultsOnCard(void)
{
    if (sData->hasLinkResults)
    {
        StringCopy(gStringVar1, sData->textLinkBattleWins);
        StringCopy(gStringVar2, sData->textLinkBattleLosses);
        StringExpandPlaceholders(gStringVar4, gText_WinsLosses);
        PrintStatOnBackOfCard(1, sData->textLinkBattleType, gStringVar4, sTrainerCardTextColors);
    }
}

static void BufferNumTrades(void)
{
    if (sData->hasTrades)
        ConvertIntToDecimalStringN(sData->textNumTrades, sData->trainerCard.pokemonTrades, STR_CONV_MODE_RIGHT_ALIGN, 5);
}

static void PrintTradesStringOnCard(void)
{
    if (sData->hasTrades)
        PrintStatOnBackOfCard(2, gText_PokemonTrades, sData->textNumTrades, sTrainerCardStatColors);
}

static void BufferBerryCrushPoints(void)
{
    if (sData->cardType == CARD_TYPE_FRLG && sData->trainerCard.linkPoints.berryCrush)
        ConvertIntToDecimalStringN(sData->textBerryCrushPts, sData->trainerCard.linkPoints.berryCrush, STR_CONV_MODE_RIGHT_ALIGN, 5);
}

static void PrintBerryCrushStringOnCard(void)
{
    if (sData->cardType == CARD_TYPE_FRLG && sData->trainerCard.linkPoints.berryCrush)
        PrintStatOnBackOfCard(4, gText_BerryCrush, sData->textBerryCrushPts, sTrainerCardStatColors);
}

static void BufferUnionRoomStats(void)
{
    if (sData->cardType == CARD_TYPE_FRLG && sData->trainerCard.unionRoomNum)
        ConvertIntToDecimalStringN(sData->textUnionRoomStats, sData->trainerCard.unionRoomNum, STR_CONV_MODE_RIGHT_ALIGN, 5);
}

static void PrintUnionStringOnCard(void)
{
    if (sData->cardType == CARD_TYPE_FRLG && sData->trainerCard.unionRoomNum)
        PrintStatOnBackOfCard(3, gText_UnionTradesAndBattles, sData->textUnionRoomStats, sTrainerCardStatColors);
}

static void BufferLinkPokeblocksNum(void)
{
    if (sData->cardType != CARD_TYPE_FRLG && sData->trainerCard.pokeblocksWithFriends)
    {
        ConvertIntToDecimalStringN(gStringVar1, sData->trainerCard.pokeblocksWithFriends, STR_CONV_MODE_RIGHT_ALIGN, 5);
        StringExpandPlaceholders(sData->textNumLinkPokeblocks, gText_NumPokeblocks);
    }
}

static void PrintPokeblockStringOnCard(void)
{
    if (sData->cardType != CARD_TYPE_FRLG && sData->trainerCard.pokeblocksWithFriends)
        PrintStatOnBackOfCard(3, gText_PokeblocksWithFriends, sData->textNumLinkPokeblocks, sTrainerCardStatColors);
}

static void BufferLinkContestNum(void)
{
    if (sData->cardType != CARD_TYPE_FRLG && sData->trainerCard.contestsWithFriends)
        ConvertIntToDecimalStringN(sData->textNumLinkContests, sData->trainerCard.contestsWithFriends, STR_CONV_MODE_RIGHT_ALIGN, 5);
}

static void PrintContestStringOnCard(void)
{
    if (sData->cardType != CARD_TYPE_FRLG && sData->trainerCard.contestsWithFriends)
        PrintStatOnBackOfCard(4, gText_WonContestsWFriends, sData->textNumLinkContests, sTrainerCardStatColors);
}

static void BufferBattleFacilityStats(void)
{
    switch (sData->cardType)
    {
    case CARD_TYPE_RS:
        if (sData->hasBattleTowerWins)
        {
            ConvertIntToDecimalStringN(gStringVar1, sData->trainerCard.battleTowerWins, STR_CONV_MODE_RIGHT_ALIGN, 4);
            ConvertIntToDecimalStringN(gStringVar2, sData->trainerCard.battleTowerStraightWins, STR_CONV_MODE_RIGHT_ALIGN, 4);
            StringExpandPlaceholders(sData->textBattleFacilityStat, gText_WinsStraight);
        }
        break;
    case CARD_TYPE_EMERALD:
        if (sData->trainerCard.frontierBP)
        {
            ConvertIntToDecimalStringN(gStringVar1, sData->trainerCard.frontierBP, STR_CONV_MODE_RIGHT_ALIGN, 5);
            StringExpandPlaceholders(sData->textBattleFacilityStat, gText_NumBP);
        }
        break;
    case CARD_TYPE_FRLG:
        break;
    }
}

static void PrintBattleFacilityStringOnCard(void)
{
    switch (sData->cardType)
    {
    case CARD_TYPE_RS:
        if (sData->hasBattleTowerWins)
            PrintStatOnBackOfCard(5, gText_BattleTower, sData->textBattleFacilityStat, sTrainerCardTextColors);
        break;
    case CARD_TYPE_EMERALD:
        if (sData->trainerCard.frontierBP)
            PrintStatOnBackOfCard(5, gText_BattlePtsWon, sData->textBattleFacilityStat, sTrainerCardStatColors);
        break;
    case CARD_TYPE_FRLG:
        break;
    }
}

static void PrintPokemonIconsOnCard(void)
{
    u8 i;
    u8 paletteSlots[PARTY_SIZE] = {5, 6, 7, 8, 9, 10};
    u8 xOffsets[PARTY_SIZE] = {0, 4, 8, 12, 16, 20};

    if (sData->cardType == CARD_TYPE_FRLG)
    {
        for (i = 0; i < PARTY_SIZE; i++)
        {
            if (sData->trainerCard.monSpecies[i])
            {
                u8 monSpecies = GetMonIconPaletteIndexFromSpecies(sData->trainerCard.monSpecies[i]);
                WriteSequenceToBgTilemapBuffer(3, 16 * i + 224, xOffsets[i] + 3, 15, 4, 4, paletteSlots[monSpecies], 1);
            }
        }
    }
}

static void LoadMonIconGfx(void)
{
    u8 i;

    CpuSet(gMonIconPalettes, sData->monIconPal, 0x60);
    switch (sData->trainerCard.monIconTint)
    {
    case MON_ICON_TINT_NORMAL:
        break;
    case MON_ICON_TINT_BLACK:
        TintPalette_CustomTone(sData->monIconPal, 96, 0, 0, 0);
        break;
    case MON_ICON_TINT_PINK:
        TintPalette_CustomTone(sData->monIconPal, 96, 500, 330, 310);
        break;
    case MON_ICON_TINT_SEPIA:
        TintPalette_SepiaTone(sData->monIconPal, 96);
        break;
    }
    LoadPalette(sData->monIconPal, BG_PLTT_ID(5), 6 * PLTT_SIZE_4BPP);

    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (sData->trainerCard.monSpecies[i])
            LoadBgTiles(3, GetMonIconTiles(sData->trainerCard.monSpecies[i], 0), 512, 16 * i + 32);
    }
}

static void PrintStickersOnCard(void)
{
    u8 i;
    u8 paletteSlots[4] = {11, 12, 13, 14};

    if (sData->cardType == CARD_TYPE_FRLG && sData->trainerCard.shouldDrawStickers == TRUE)
    {
        for (i = 0; i < TRAINER_CARD_STICKER_TYPES; i++)
        {
            u8 sticker = sData->trainerCard.stickers[i];
            if (sData->trainerCard.stickers[i])
                WriteSequenceToBgTilemapBuffer(3, i * 4 + 320, i * 3 + 2, 2, 2, 2, paletteSlots[sticker - 1], 1);
        }
    }
}

static void LoadStickerGfx(void)
{
    LoadPalette(sTrainerCardSticker1_Pal, BG_PLTT_ID(11), PLTT_SIZE_4BPP);
    LoadPalette(sTrainerCardSticker2_Pal, BG_PLTT_ID(12), PLTT_SIZE_4BPP);
    LoadPalette(sTrainerCardSticker3_Pal, BG_PLTT_ID(13), PLTT_SIZE_4BPP);
    LoadPalette(sTrainerCardSticker4_Pal, BG_PLTT_ID(14), PLTT_SIZE_4BPP);
    LoadBgTiles(3, sData->stickerTiles, 1024, 128);
}

static void DrawTrainerCardWindow(u8 windowId)
{
    PutWindowTilemap(windowId);
    CopyWindowToVram(windowId, COPYWIN_FULL);
}

static u8 SetCardBgsAndPals(void)
{
    switch (sData->bgPalLoadState)
    {
    case 0:
        LoadBgTiles(3, sData->badgeTiles, ARRAY_COUNT(sData->badgeTiles), 0);
        break;
    case 1:
        LoadBgTiles(0, sData->cardTiles, 0x1800, 0);
        break;
    case 2:
        if (sData->cardType != CARD_TYPE_FRLG)
        {
            LoadPalette(sHoennTrainerCardPals[sData->trainerCard.stars], BG_PLTT_ID(0), 3 * PLTT_SIZE_4BPP);
            LoadPalette(sHoennTrainerCardBadges_Pal, BG_PLTT_ID(3), PLTT_SIZE_4BPP);
            if (sData->trainerCard.gender != MALE)
                LoadPalette(sHoennTrainerCardFemaleBg_Pal, BG_PLTT_ID(1), PLTT_SIZE_4BPP);
        }
        else
        {
            LoadPalette(sKantoTrainerCardPals[sData->trainerCard.stars], BG_PLTT_ID(0), 3 * PLTT_SIZE_4BPP);
            LoadPalette(sKantoTrainerCardBadges_Pal, BG_PLTT_ID(3), PLTT_SIZE_4BPP);
            if (sData->trainerCard.gender != MALE)
                LoadPalette(sKantoTrainerCardFemaleBg_Pal, BG_PLTT_ID(1), PLTT_SIZE_4BPP);
        }
        LoadPalette(sTrainerCardStar_Pal, BG_PLTT_ID(4), PLTT_SIZE_4BPP);
        break;
    case 3:
        SetBgTilemapBuffer(0, sData->cardTilemapBuffer);
        SetBgTilemapBuffer(2, sData->bgTilemapBuffer);
        break;
    case 4:
        FillBgTilemapBufferRect_Palette0(0, 0, 0, 0, 32, 32);
        FillBgTilemapBufferRect_Palette0(2, 0, 0, 0, 32, 32);
        FillBgTilemapBufferRect_Palette0(3, 0, 0, 0, 32, 32);
    default:
        return 1;
    }
    sData->bgPalLoadState++;
    return 0;
}

static void DrawCardScreenBackground(u16 *ptr)
{
    s16 i, j;
    u16 *dst = sData->bgTilemapBuffer;

    for (i = 0; i < 20; i++)
    {
        for (j = 0; j < 32; j++)
        {
            if (j < 30)
                dst[32 * i + j] = ptr[30 * i + j];
            else
                dst[32 * i + j] = ptr[0];
        }
    }
    CopyBgTilemapBufferToVram(2);
}

static void DrawCardFrontOrBack(u16 *ptr)
{
    s16 i, j;
    u16 *dst = sData->cardTilemapBuffer;

    for (i = 0; i < 20; i++)
    {
        for (j = 0; j < 32; j++)
        {
            if (j < 30)
                dst[32 * i + j] = ptr[30 * i + j];
            else
                dst[32 * i + j] = ptr[0];
        }
    }
    CopyBgTilemapBufferToVram(0);
}

static void DrawStarsAndBadgesOnCard(void)
{
    static const u8 yOffsets[] = {7, 7};

    s16 i, x, y;
    u16 tileNum = 192;
    u8 palNum = 3;

    FillBgTilemapBufferRect(3, 143, 15, yOffsets[sData->isHoenn], sData->trainerCard.stars, 1, 4);
    if (!sData->isLink)
    {
        x = 4;
        y = IS_FRLG ? 16 : 15;
        for (i = 0; i < NUM_BADGES; i++, tileNum += 2, x += 3)
        {
            if (sData->badgeCount[i])
            {
                FillBgTilemapBufferRect(3, tileNum, x, y, 1, 1, palNum);
                FillBgTilemapBufferRect(3, tileNum + 1, x + 1, y, 1, 1, palNum);
                FillBgTilemapBufferRect(3, tileNum + 16, x, y + 1, 1, 1, palNum);
                FillBgTilemapBufferRect(3, tileNum + 17, x + 1, y + 1, 1, 1, palNum);
            }
        }
    }
    CopyBgTilemapBufferToVram(3);
}

static void DrawCardBackStats(void)
{
    // These are the wins/losses and straight-wins marks, written as raw glyph
    // tiles at BG3 cells hardcoded to the vanilla back art's rows 9-16. The
    // journal draws over exactly those rows, so left running they would strew
    // stray punctuation across it - and they are tilemap writes, not text, so
    // nothing in the print path would clear them.
    if (sData->hasJournal)
        return;

    if (sData->cardType == CARD_TYPE_FRLG)
    {
        if (sData->hasTrades)
        {
            FillBgTilemapBufferRect(3, 141, 27, 9, 1, 1, 1);
            FillBgTilemapBufferRect(3, 157, 27, 10, 1, 1, 1);
        }
        if (sData->trainerCard.linkPoints.berryCrush)
        {
            FillBgTilemapBufferRect(3, 141, 21, 13, 1, 1, 1);
            FillBgTilemapBufferRect(3, 157, 21, 14, 1, 1, 1);
        }
        if (sData->trainerCard.unionRoomNum)
        {
            FillBgTilemapBufferRect(3, 141, 27, 11, 1, 1, 1);
            FillBgTilemapBufferRect(3, 157, 27, 12, 1, 1, 1);
        }
    }
    else
    {
        if (sData->hasTrades)
        {
            FillBgTilemapBufferRect(3, 141, 27, 9, 1, 1, 0);
            FillBgTilemapBufferRect(3, 157, 27, 10, 1, 1, 0);
        }
        if (sData->trainerCard.contestsWithFriends)
        {
            FillBgTilemapBufferRect(3, 141, 27, 13, 1, 1, 0);
            FillBgTilemapBufferRect(3, 157, 27, 14, 1, 1, 0);
        }
        if (sData->hasBattleTowerWins)
        {
            FillBgTilemapBufferRect(3, 141, 17, 15, 1, 1, 0);
            FillBgTilemapBufferRect(3, 157, 17, 16, 1, 1, 0);
            FillBgTilemapBufferRect(3, 140, 27, 15, 1, 1, 0);
            FillBgTilemapBufferRect(3, 156, 27, 16, 1, 1, 0);
        }
    }
    CopyBgTilemapBufferToVram(3);
}

static void BlinkTimeColon(void)
{
    if (++sData->timeColonBlinkTimer > 60)
    {
        sData->timeColonBlinkTimer = 0;
        sData->timeColonInvisible ^= 1;
        sData->timeColonNeedDraw = TRUE;
    }
}

u8 GetTrainerCardStars(u8 cardId)
{
    struct TrainerCard *trainerCards = gTrainerCards;
    return trainerCards[cardId].stars;
}

#define tFlipState data[0]
#define tCardTop   data[1]

static void FlipTrainerCard(void)
{
    u8 taskId = CreateTask(Task_DoCardFlipTask, 0);
    Task_DoCardFlipTask(taskId);
    SetHBlankCallback(HblankCb_TrainerCard);
}

static bool8 IsCardFlipTaskActive(void)
{
    if (FindTaskIdByFunc(Task_DoCardFlipTask) == TASK_NONE)
        return TRUE;
    else
        return FALSE;
}

static void Task_DoCardFlipTask(u8 taskId)
{
    while (sTrainerCardFlipTasks[gTasks[taskId].tFlipState](&gTasks[taskId]))
        ;
}

static bool8 Task_BeginCardFlip(struct Task *task)
{
    u32 i;

    HideBg(1);
    HideBg(3);
    ScanlineEffect_Stop();
    ScanlineEffect_Clear();
    for (i = 0; i < DISPLAY_HEIGHT; i++)
        gScanlineEffectRegBuffers[1][i] = 0;
    task->tFlipState++;
    return FALSE;
}

// Note: Cannot be DISPLAY_HEIGHT / 2, or cardHeight will be 0
#define CARD_FLIP_Y ((DISPLAY_HEIGHT / 2) - 3)

static bool8 Task_AnimateCardFlipDown(struct Task *task)
{
    u32 cardHeight, r5, r10, cardTop, r6, var_24, cardBottom, var;
    s16 i;

    sData->allowDMACopy = FALSE;
    if (task->tCardTop >= CARD_FLIP_Y)
        task->tCardTop = CARD_FLIP_Y;
    else
        task->tCardTop += 7;

    sData->cardTop = task->tCardTop;
    UpdateCardFlipRegs(task->tCardTop);

    cardTop = task->tCardTop;
    cardBottom = DISPLAY_HEIGHT - cardTop;
    cardHeight = cardBottom - cardTop;
    r6 = -cardTop << 16;
    r5 = (DISPLAY_HEIGHT << 16) / cardHeight;
    r5 -= 1 << 16;
    var_24 = r6;
    var_24 += r5 * cardHeight;
    r10 = r5 / cardHeight;
    r5 *= 2;

    for (i = 0; i < cardTop; i++)
        gScanlineEffectRegBuffers[0][i] = -i;
    for (; i < (s16)cardBottom; i++)
    {
        var = r6 >> 16;
        r6 += r5;
        r5 -= r10;
        gScanlineEffectRegBuffers[0][i] = var;
    }
    var = var_24 >> 16;
    for (; i < DISPLAY_HEIGHT; i++)
        gScanlineEffectRegBuffers[0][i] = var;

    sData->allowDMACopy = TRUE;
    if (task->tCardTop >= CARD_FLIP_Y)
        task->tFlipState++;

    return FALSE;
}

static bool8 Task_DrawFlippedCardSide(struct Task *task)
{
    sData->allowDMACopy = FALSE;
    if (Overworld_IsRecvQueueAtMax() == TRUE)
        return FALSE;

    do
    {
        switch (sData->flipDrawState)
        {
        case 0:
            FillWindowPixelBuffer(WIN_CARD_TEXT, PIXEL_FILL(0));
            FillBgTilemapBufferRect_Palette0(3, 0, 0, 0, 0x20, 0x20);
            break;
        case 1:
            if (!sData->onBack)
            {
                if (!PrintAllOnCardBack())
                    return FALSE;
            }
            else
            {
                if (!PrintAllOnCardFront())
                    return FALSE;
            }
            break;
        case 2:
            if (!sData->onBack)
                DrawCardFrontOrBack(sData->backTilemap);
            else
                DrawTrainerCardWindow(WIN_CARD_TEXT);
            break;
        case 3:
            if (!sData->onBack)
                DrawCardBackStats();
            else
                FillWindowPixelBuffer(WIN_TRAINER_PIC, PIXEL_FILL(0));
            break;
        case 4:
            if (sData->onBack)
                CreateTrainerCardTrainerPic();
            break;
        default:
            task->tFlipState++;
            sData->allowDMACopy = TRUE;
            sData->flipDrawState = 0;
            return FALSE;
        }
        sData->flipDrawState++;
    } while (!gReceivedRemoteLinkPlayers);

    return FALSE;
}

static bool8 Task_SetCardFlipped(struct Task *task)
{
    sData->allowDMACopy = FALSE;

    // If on back of card, draw front of card because its being flipped
    if (sData->onBack)
    {
        DrawTrainerCardWindow(WIN_TRAINER_PIC);
        DrawCardScreenBackground(sData->bgTilemap);
        DrawCardFrontOrBack(sData->frontTilemap);
        DrawStarsAndBadgesOnCard();
    }
    DrawTrainerCardWindow(WIN_CARD_TEXT);
    sData->onBack ^= 1;
    task->tFlipState++;
    sData->allowDMACopy = TRUE;
    PlaySE(SE_RG_CARD_FLIPPING);
    return FALSE;
}

static bool8 Task_AnimateCardFlipUp(struct Task *task)
{
    u32 cardHeight, r5, r10, cardTop, r6, var_24, cardBottom, var;
    s16 i;

    sData->allowDMACopy = FALSE;
    if (task->tCardTop <= 5)
        task->tCardTop = 0;
    else
        task->tCardTop -= 5;

    sData->cardTop = task->tCardTop;
    UpdateCardFlipRegs(task->tCardTop);

    cardTop = task->tCardTop;
    cardBottom = DISPLAY_HEIGHT - cardTop;
    cardHeight = cardBottom - cardTop;
    r6 = -cardTop << 16;
    r5 = (DISPLAY_HEIGHT << 16) / cardHeight;
    r5 -= 1 << 16;
    var_24 = r6;
    var_24 += r5 * cardHeight;
    r10 = r5 / cardHeight;
    r5 /= 2;

    for (i = 0; i < cardTop; i++)
        gScanlineEffectRegBuffers[0][i] = -i;
    for (; i < (s16)cardBottom; i++)
    {
        var = r6 >> 16;
        r6 += r5;
        r5 += r10;
        gScanlineEffectRegBuffers[0][i] = var;
    }
    var = var_24 >> 16;
    for (; i < DISPLAY_HEIGHT; i++)
        gScanlineEffectRegBuffers[0][i] = var;

    sData->allowDMACopy = TRUE;
    if (task->tCardTop <= 0)
        task->tFlipState++;

    return FALSE;
}

static bool8 Task_EndCardFlip(struct Task *task)
{
    ShowBg(1);
    ShowBg(3);
    SetHBlankCallback(NULL);
    DestroyTask(FindTaskIdByFunc(Task_DoCardFlipTask));
    return FALSE;
}

void ShowPlayerTrainerCard(void (*callback)(void))
{
    sData = AllocZeroed(sizeof(*sData));
    sData->callback2 = callback;
    if (callback == CB2_ReshowFrontierPass)
        sData->blendColor = RGB_WHITE;
    else
        sData->blendColor = RGB_BLACK;

    if (InUnionRoom() == TRUE)
        sData->isLink = TRUE;
    else
        sData->isLink = FALSE;

    sData->language = GAME_LANGUAGE;
    TrainerCard_GenerateCardForPlayer(&sData->trainerCard);
    SetMainCallback2(CB2_InitTrainerCard);
}

void ShowTrainerCardInLink(u8 cardId, void (*callback)(void))
{
    sData = AllocZeroed(sizeof(*sData));
    sData->callback2 = callback;
    sData->isLink = TRUE;
    sData->trainerCard = gTrainerCards[cardId];
    sData->language = gLinkPlayers[cardId].language;
    SetMainCallback2(CB2_InitTrainerCard);
}

static void InitTrainerCardData(void)
{
    u8 i;

    sData->mainState = 0;
    sData->timeColonBlinkTimer = gSaveBlock2Ptr->playTimeVBlanks;
    sData->timeColonInvisible = FALSE;
    sData->onBack = FALSE;
    sData->flipBlendY = 0;
    sData->cardType = GetSetCardType();
    for (i = 0; i < TRAINER_CARD_PROFILE_LENGTH; i++)
        CopyEasyChatWord(sData->easyChatProfile[i], sData->trainerCard.easyChatProfile[i]);
}

static u8 GetSetCardType(void)
{
    if (sData == NULL)
    {
        if (gGameVersion == VERSION_FIRE_RED || gGameVersion == VERSION_LEAF_GREEN)
            return CARD_TYPE_FRLG;
        else if (gGameVersion == VERSION_EMERALD)
            return CARD_TYPE_EMERALD;
        else
            return CARD_TYPE_RS;
    }
    else
    {
        if (sData->trainerCard.version == VERSION_FIRE_RED || sData->trainerCard.version == VERSION_LEAF_GREEN)
        {
            sData->isHoenn = FALSE;
            return CARD_TYPE_FRLG;
        }
        else if (sData->trainerCard.version == VERSION_EMERALD)
        {
            sData->isHoenn = TRUE;
            return CARD_TYPE_EMERALD;
        }
        else
        {
            sData->isHoenn = TRUE;
            return CARD_TYPE_RS;
        }
    }
}

static u8 VersionToCardType(enum GameVersion version)
{
    if (version == VERSION_FIRE_RED || version == VERSION_LEAF_GREEN)
        return CARD_TYPE_FRLG;
    else if (version == VERSION_EMERALD)
        return CARD_TYPE_EMERALD;
    else
        return CARD_TYPE_RS;
}

static void CreateTrainerCardTrainerPic(void)
{
    if (InUnionRoom() == TRUE && gReceivedRemoteLinkPlayers)
    {
        CreateTrainerCardTrainerPicSprite(FacilityClassToPicIndex(sData->trainerCard.unionRoomClass),
                    TRUE,
                    sTrainerPicOffset[sData->isHoenn][sData->trainerCard.gender][0],
                    sTrainerPicOffset[sData->isHoenn][sData->trainerCard.gender][1],
                    8,
                    WIN_TRAINER_PIC);
    }
    else
    {
        CreateTrainerCardTrainerPicSprite(FacilityClassToPicIndex(sTrainerPicFacilityClass[sData->cardType][sData->trainerCard.gender]),
                    TRUE,
                    sTrainerPicOffset[sData->isHoenn][sData->trainerCard.gender][0],
                    sTrainerPicOffset[sData->isHoenn][sData->trainerCard.gender][1],
                    8,
                    WIN_TRAINER_PIC);
    }
}
