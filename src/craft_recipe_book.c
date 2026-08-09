#include "global.h"
#include "craft_recipe_book.h"
#include "task.h"
#include "main.h"
#include "bg.h"
#include "gpu_regs.h"
#include "window.h"
#include "sprite.h"
#include "list_menu.h"
#include "event_data.h"
#include "graphics.h"
#include "item_icon.h"
#include "sound.h"
#include "constants/songs.h"
#include "constants/rgb.h"
#include "menu.h"
#include "menu_helpers.h"
#include "text.h"
#include "text_window.h"
#include "international_string_util.h"
#include "craft_menu.h"
#include "craft_menu_ui.h"
#include "craft_logic.h"
#include "data/crafting_recipes.h"
#include "item.h"
#include "malloc.h"
#include "item_menu.h"
#include "strings.h"
#include "config/crafting.h"
#include "constants/rtc.h"
#include "field_weather.h"
#include "palette.h"
#include "overworld.h"
#include "string_util.h"
#include "comfy_anim.h"

static void Task_RecipeBookMenu(u8 taskId);
static void RecipeBookMainCB2(void);
static void RecipeBookVBlankCB(void);
static void RecipeBook_Init(void);
static void RecipeBook_Cleanup(void);
static void RecipeBook_BuildPocketList(void);
static void RecipeBook_BuildItemList(enum Pocket pocketId);
static void RecipeBook_InitWindows(void);
static void RecipeBook_DestroyWindows(void);
static void RecipeBook_RefreshListMenu(void);
static void RecipeBook_Print(u8 windowId, u8 fontId, const u8 *str, u8 left, u8 top, u8 colorIndex);
static void RecipeBook_PrintPocketNames(const u8 *pocketName1, const u8 *pocketName2);
static void RecipeBook_CopyPocketNameToWindow(u32 offset);
static void RecipeBook_DrawPocketIndicatorSquares(void);
static void RecipeBook_CreatePocketSwitchArrows(void);
static void RecipeBook_DestroyPocketSwitchArrows(void);
static void RecipeBook_DestroyPatternIcons(void);
static void RecipeBook_ResetPatternPreviewState(void);
static void RecipeBook_UpdatePatternPreview(void);
static void RecipeBook_SchedulePatternPreview(void);
static void RecipeBook_TickPatternPreview(void);
static void RecipeBook_UpdateIngredientsHeaderOnly(void);
static void RecipeBook_UpdateIngredientsWindow(void);
static void RecipeBook_EnablePanelWindow(void);
static void RecipeBook_DisablePanelWindow(void);
static void RecipeBook_CopyPanelTilemapToBg3(const u32 *tilemap);
static void RecipeBook_ClearPanelOverlay(void);
static void RecipeBook_BlankPatternArea(void);
static void RecipeBook_RestorePatternArea(void);
static void RecipeBook_ResetIngredientsWindow(const struct WindowTemplate *template);
static void RecipeBook_EnterExtendedIngredientsView(void);
static void RecipeBook_ExitExtendedIngredientsView(void);
static void RecipeBook_ExitExtendedIngredientsViewIfNeeded(void);
static void RecipeBook_StartExtendAnim(u8 animState);
static void RecipeBook_StopExtendAnim(void);
static void RecipeBook_TickExtendAnim(void);
static void RecipeBook_ShowPatternHideWindow(void);
static void RecipeBook_HidePatternHideWindow(void);
static u8 RecipeBook_GetUniqueIngredientCount(const struct CraftRecipe *recipe);
static u8 RecipeBook_GetUnlockedVariantCount(u16 itemId);
static u8 RecipeBook_GetVariantIndex(u16 itemId);
static const struct CraftRecipe *RecipeBook_GetRecipeVariant(u16 itemId, u8 variantIndex);
static void RecipeBook_PrintListItem(u8 windowId, u32 itemId, u8 y);
static void RecipeBook_RedrawListRow(u16 itemIndex, u8 row);
static void RecipeBook_RedrawCurrentListRow(void);
static void RecipeBook_BuildFittedItemName(u16 itemId, u8 fontId, u32 maxWidth, u8 *render);
static void RecipeBook_ApplyVariantChange(u16 itemId, u8 variantIndex);
static void RecipeBook_UpdateListScrollArrows(void);
static void RecipeBook_UpdateVariantButtons(void);
static void RecipeBook_DestroyListScrollArrows(void);
static void RecipeBook_DisableTimeOfDayBlend(void);
static void RecipeBook_RestoreTimeOfDayBlend(void);
static u8 RecipeBook_BuildIngredientList(const struct CraftRecipe *recipe, u16 ingredientIds[], u8 ingredientCounts[]);
static u16 RecipeBook_GetMaxCraftableQuantity(const struct CraftRecipe *recipe);
static bool8 RecipeBook_AdjustAutoCraftQuantity(u16 *quantity, u16 max, u16 step);
static void RecipeBook_ShowAutoCraftPrompt(const u8 *text);
static void RecipeBook_ShowAutoCraftHowManyPrompt(void);
static void RecipeBook_ShowAutoCraftConfirmPrompt(void);
static void RecipeBook_ShowAutoCraftResultPrompt(void);
static void RecipeBook_UpdateAutoCraftQuantityWindow(void);
static void RecipeBook_RemoveAutoCraftQuantityWindow(void);
static void RecipeBook_RemoveAutoCraftPromptWindow(void);
static void RecipeBook_RemoveAutoCraftWindows(void);
static void RecipeBook_ClearAutoCraftState(void);
static void RecipeBook_EndAutoCraftFlow(void);
static bool8 RecipeBook_TryStartAutoCraft(void);
static bool8 RecipeBook_CraftFromBag(u16 itemId, const struct CraftRecipe *recipe, u16 outputQty);
static void RecipeBook_HandleAutoCraftInput(void);

enum
{
    RECIPEBOOK_WIN_POCKET_NAME,
    RECIPEBOOK_WIN_LIST,
    RECIPEBOOK_WIN_VARIANT_BUTTONS,
    RECIPEBOOK_WIN_INGREDIENTS,
    RECIPEBOOK_WIN_COUNT
};

static u8 sRecipeBookWindowIds[RECIPEBOOK_WIN_COUNT];

static const struct WindowTemplate sRecipeBookWindowTemplates[] =
{
    [RECIPEBOOK_WIN_POCKET_NAME] = {
        .bg = 0,
        .tilemapLeft = 3,
        .tilemapTop = 1,
        .width = 8,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 1
    },
    [RECIPEBOOK_WIN_LIST] = {
        .bg = 0,
        .tilemapLeft = 1,
        .tilemapTop = 6,
        .width = 13,
        .height = 12,
        .paletteNum = 15,
        .baseBlock = 17
    },
    [RECIPEBOOK_WIN_VARIANT_BUTTONS] = {
        .bg = 0,
        .tilemapLeft = 10,
        .tilemapTop = 4,
        .width = 6,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 173
    },
    [RECIPEBOOK_WIN_INGREDIENTS] = {
        .bg = 0,
        .tilemapLeft = 16,
        .tilemapTop = 1,
        .width = 13,
        .height = 18,
        .paletteNum = 15,
        .baseBlock = 185
    },
    DUMMY_WIN_TEMPLATE,
};

enum
{
    RECIPEBOOK_VIEW_NORMAL,
    RECIPEBOOK_VIEW_EXTENDED
};

enum
{
    RECIPEBOOK_EXTEND_ANIM_NONE,
    RECIPEBOOK_EXTEND_ANIM_EXPAND,
    RECIPEBOOK_EXTEND_ANIM_COLLAPSE
};

enum
{
    RECIPEBOOK_AUTO_CRAFT_STATE_NONE,
    RECIPEBOOK_AUTO_CRAFT_STATE_QUANTITY,
    RECIPEBOOK_AUTO_CRAFT_STATE_CONFIRM,
    RECIPEBOOK_AUTO_CRAFT_STATE_RESULT
};

static const u8 sText_RecipeBookNoRecipes[] = _("No recipes");
static const u8 sText_RecipeBookClose[] = _("CLOSE RECIPE BOOK");
static const u8 sText_RecipeBookIngredients[] = _("Required Ingredients");
static const u8 sText_RecipeBookEllipsis[] = _("...");
static const u8 sText_RecipeBookLButton[] = _("{L_BUTTON}");
static const u8 sText_RecipeBookRButton[] = _("{R_BUTTON}");
static const u8 sText_RecipeBookIngredientsLabel[] = _("Ingredients");
static const u8 sText_RecipeBookOwnedLabel[] = _("Owned");
static const u8 sText_RecipeBookViewIngredientList[] = _("List View");
static const u8 sText_RecipeBookAButton[] = _("{A_BUTTON}");
static const u8 sText_RecipeBookSelectButton[] = _("{SELECT_BUTTON}");
static const u8 sText_RecipeBookAutoCraftHowMany[] = _("Craft how many\n{STR_VAR_1}?");

#define MAX_RECIPEBOOK_ITEMS  ((max(BAG_TMHM_COUNT,              \
                                max(BAG_BERRIES_COUNT,           \
                                max(BAG_ITEMS_COUNT,             \
                                max(BAG_KEYITEMS_COUNT,          \
                                    BAG_POKEBALLS_COUNT))))) + 1)

#define TAG_RECIPEBOOK_PATTERN_ICON_BASE 0x5400
#define RECIPEBOOK_POCKET_INDICATOR_CENTER_X 7
#define RECIPEBOOK_POCKET_INDICATOR_Y 3
#define RECIPEBOOK_BG_TILE_COUNT 32
#define RECIPEBOOK_BG_TILE_BYTES (RECIPEBOOK_BG_TILE_COUNT * 32)
#define RECIPEBOOK_BG_TILEMAP_BYTES (32 * 32 * 2)
#define RECIPEBOOK_PATTERN_BASE_X 140
#define RECIPEBOOK_PATTERN_BASE_Y 91

#define RECIPEBOOK_POCKET_SCROLL_ARROW_TAG 112
#define RECIPEBOOK_LIST_SCROLL_ARROW_TAG 113

#define RECIPEBOOK_INDICATOR_TILE_INACTIVE 0x0C
#define RECIPEBOOK_INDICATOR_TILE_ACTIVE 0x0D
#define RECIPEBOOK_INDICATOR_TILE_RIGHT_END_ACTIVE 0x12
#define RECIPEBOOK_INDICATOR_TILE_LEFT_END_INACTIVE 0x15
#define RECIPEBOOK_INDICATOR_TILE_LINK_INACTIVE 0x14
#define RECIPEBOOK_INDICATOR_TILE_LINK_LEFT_ACTIVE 0x13

#define RECIPEBOOK_POCKET_NAME_TEXT_X_OFFSET 4
#define RECIPEBOOK_POCKET_NAME_TEXT_Y_OFFSET 0
#define RECIPEBOOK_LIST_TEXT_X_OFFSET 0
#define RECIPEBOOK_LIST_TEXT_Y_OFFSET 0
#define RECIPEBOOK_VARIANT_BUTTONS_Y_OFFSET  4
#define RECIPEBOOK_VARIANT_L_BUTTON_X_OFFSET -8
#define RECIPEBOOK_VARIANT_R_BUTTON_X_OFFSET -8
#define RECIPEBOOK_VARIANT_BUTTONS_GAP 1
#define RECIPEBOOK_INGREDIENTS_TEXT_X_OFFSET 0
#define RECIPEBOOK_INGREDIENTS_TEXT_Y_OFFSET 0
#define RECIPEBOOK_INGREDIENTS_LIST_X_OFFSET 2

#define RECIPEBOOK_INGREDIENTS_HEADER_Y 0
#define RECIPEBOOK_INGREDIENTS_HEADER_ICON_GAP 1
#define RECIPEBOOK_INGREDIENTS_HEADER_TO_LIST_GAP 0
#define RECIPEBOOK_INGREDIENTS_MAX 9
#define RECIPEBOOK_INGREDIENTS_LINE_BUFFER (ITEM_NAME_LENGTH + 8)
#define RECIPEBOOK_INGREDIENTS_PAGE_BUFFER 32
#define RECIPEBOOK_INGREDIENTS_PER_PAGE 3
#define RECIPEBOOK_INGREDIENTS_LINE_SPACING -1
#define RECIPEBOOK_INGREDIENTS_ITEM_NAME_MAX_WIDTH 100
#define RECIPEBOOK_INGREDIENTS_INLINE_MAX 4
#define RECIPEBOOK_INGREDIENTS_SUMMARY_BUFFER 48
#define RECIPEBOOK_INGREDIENTS_EXTENDED_HEIGHT 18
#define RECIPEBOOK_INGREDIENTS_SELECT_ICON_X_OFFSET 7
#define RECIPEBOOK_INGREDIENTS_SELECT_ICON_Y_OFFSET 6
#define RECIPEBOOK_INGREDIENTS_LIST_VIEW_X_OFFSET 8

#define RECIPEBOOK_WINDOW_FRAME_BASE_TILE 0x1B0
#define RECIPEBOOK_WINDOW_FRAME_PALETTE 13

#define RECIPEBOOK_EXTEND_ANIM_OFFSET_PIXELS 24
#define RECIPEBOOK_EXTEND_ANIM_DURATION_FRAMES 18

#define RECIPEBOOK_PANEL_TILEMAP_LEFT 16
#define RECIPEBOOK_PANEL_TILEMAP_TOP 8
#define RECIPEBOOK_PANEL_TILEMAP_WIDTH 13
#define RECIPEBOOK_PANEL_TILEMAP_HEIGHT 12

#define RECIPEBOOK_PATTERN_CLEAR_LEFT 16
#define RECIPEBOOK_PATTERN_CLEAR_TOP 9
#define RECIPEBOOK_PATTERN_CLEAR_WIDTH (DISPLAY_TILE_WIDTH - RECIPEBOOK_PATTERN_CLEAR_LEFT)
#define RECIPEBOOK_PATTERN_CLEAR_HEIGHT (DISPLAY_TILE_HEIGHT - RECIPEBOOK_PATTERN_CLEAR_TOP)
#define RECIPEBOOK_PATTERN_CLEAR_TILE 0x01

#define RECIPEBOOK_AUTO_CRAFT_PROMPT_LEFT 2
#define RECIPEBOOK_AUTO_CRAFT_PROMPT_TOP 15
#define RECIPEBOOK_AUTO_CRAFT_PROMPT_WIDTH 26
#define RECIPEBOOK_AUTO_CRAFT_PROMPT_HEIGHT 4
#define RECIPEBOOK_AUTO_CRAFT_PROMPT_BASE_BLOCK 0x80

#define RECIPEBOOK_AUTO_CRAFT_QTY_LEFT 23
#define RECIPEBOOK_AUTO_CRAFT_QTY_TOP 11
#define RECIPEBOOK_AUTO_CRAFT_QTY_WIDTH 5
#define RECIPEBOOK_AUTO_CRAFT_QTY_HEIGHT 2
#define RECIPEBOOK_AUTO_CRAFT_QTY_BASE_BLOCK \
    (RECIPEBOOK_AUTO_CRAFT_PROMPT_BASE_BLOCK + (RECIPEBOOK_AUTO_CRAFT_PROMPT_WIDTH * RECIPEBOOK_AUTO_CRAFT_PROMPT_HEIGHT))

#define RECIPEBOOK_AUTO_CRAFT_YESNO_LEFT 23
#define RECIPEBOOK_AUTO_CRAFT_YESNO_TOP 9
#define RECIPEBOOK_AUTO_CRAFT_YESNO_WIDTH 5
#define RECIPEBOOK_AUTO_CRAFT_YESNO_HEIGHT 4
#define RECIPEBOOK_AUTO_CRAFT_YESNO_BASE_BLOCK \
    (RECIPEBOOK_AUTO_CRAFT_QTY_BASE_BLOCK + (RECIPEBOOK_AUTO_CRAFT_QTY_WIDTH * RECIPEBOOK_AUTO_CRAFT_QTY_HEIGHT))

#define RECIPEBOOK_PATTERN_HIDE_LEFT 18
#define RECIPEBOOK_PATTERN_HIDE_TOP 10
#define RECIPEBOOK_PATTERN_HIDE_WIDTH 11
#define RECIPEBOOK_PATTERN_HIDE_HEIGHT 9
#define RECIPEBOOK_PATTERN_HIDE_BASE_BLOCK 0x10

#define RECIPEBOOK_VARIANT_INDICATOR_RIGHT_PADDING -1
#define RECIPEBOOK_VARIANT_INDICATOR_GAP 2
#define RECIPEBOOK_VARIANT_INDICATOR_Y_OFFSET -3

static const struct WindowTemplate sRecipeBookIngredientsWindowTemplateExtended =
{
    .bg = 0,
    .tilemapLeft = 16,
    .tilemapTop = 1,
    .width = 13,
    .height = RECIPEBOOK_INGREDIENTS_EXTENDED_HEIGHT,
    .paletteNum = 15,
    .baseBlock = 185
};

static const struct WindowTemplate sRecipeBookAutoCraftPromptWindowTemplate =
{
    .bg = 1,
    .tilemapLeft = RECIPEBOOK_AUTO_CRAFT_PROMPT_LEFT,
    .tilemapTop = RECIPEBOOK_AUTO_CRAFT_PROMPT_TOP,
    .width = RECIPEBOOK_AUTO_CRAFT_PROMPT_WIDTH,
    .height = RECIPEBOOK_AUTO_CRAFT_PROMPT_HEIGHT,
    .paletteNum = 15,
    .baseBlock = RECIPEBOOK_AUTO_CRAFT_PROMPT_BASE_BLOCK
};

static const struct WindowTemplate sRecipeBookAutoCraftQuantityWindowTemplate =
{
    .bg = 1,
    .tilemapLeft = RECIPEBOOK_AUTO_CRAFT_QTY_LEFT,
    .tilemapTop = RECIPEBOOK_AUTO_CRAFT_QTY_TOP,
    .width = RECIPEBOOK_AUTO_CRAFT_QTY_WIDTH,
    .height = RECIPEBOOK_AUTO_CRAFT_QTY_HEIGHT,
    .paletteNum = 15,
    .baseBlock = RECIPEBOOK_AUTO_CRAFT_QTY_BASE_BLOCK
};

static const struct WindowTemplate sRecipeBookAutoCraftYesNoWindowTemplate =
{
    .bg = 1,
    .tilemapLeft = RECIPEBOOK_AUTO_CRAFT_YESNO_LEFT,
    .tilemapTop = RECIPEBOOK_AUTO_CRAFT_YESNO_TOP,
    .width = RECIPEBOOK_AUTO_CRAFT_YESNO_WIDTH,
    .height = RECIPEBOOK_AUTO_CRAFT_YESNO_HEIGHT,
    .paletteNum = 15,
    .baseBlock = RECIPEBOOK_AUTO_CRAFT_YESNO_BASE_BLOCK
};

static const struct WindowTemplate sRecipeBookPatternHideWindowTemplate =
{
    .bg = 1,
    .tilemapLeft = RECIPEBOOK_PATTERN_HIDE_LEFT,
    .tilemapTop = RECIPEBOOK_PATTERN_HIDE_TOP,
    .width = RECIPEBOOK_PATTERN_HIDE_WIDTH,
    .height = RECIPEBOOK_PATTERN_HIDE_HEIGHT,
    .paletteNum = 0,
    .baseBlock = RECIPEBOOK_PATTERN_HIDE_BASE_BLOCK
};

enum
{
    RECIPEBOOK_COLOR_POCKET_NAME,
    RECIPEBOOK_COLOR_INGREDIENT_HEADER,
    RECIPEBOOK_COLOR_INGREDIENT_HAVE,
    RECIPEBOOK_COLOR_INGREDIENT_MISS
};

static const u8 sRecipeBookFontColorTable[][3] =
{
    [RECIPEBOOK_COLOR_POCKET_NAME] = {TEXT_COLOR_TRANSPARENT, TEXT_DYNAMIC_COLOR_1, TEXT_COLOR_DARK_GRAY},
    [RECIPEBOOK_COLOR_INGREDIENT_HEADER] = {TEXT_COLOR_TRANSPARENT, TEXT_DYNAMIC_COLOR_1, TEXT_COLOR_DARK_GRAY},
    [RECIPEBOOK_COLOR_INGREDIENT_HAVE] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_GREEN, TEXT_COLOR_LIGHT_GREEN},
    [RECIPEBOOK_COLOR_INGREDIENT_MISS] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_RED, TEXT_COLOR_LIGHT_RED}
};

static const struct ScrollArrowsTemplate sRecipeBookPocketArrowTemplate =
{
    .firstArrowType = SCROLL_ARROW_LEFT,
    .firstX = 20,
    .firstY = 16,
    .secondArrowType = SCROLL_ARROW_RIGHT,
    .secondX = 100,
    .secondY = 16,
    .fullyUpThreshold = -1,
    .fullyDownThreshold = -1,
    .tileTag = RECIPEBOOK_POCKET_SCROLL_ARROW_TAG,
    .palTag = RECIPEBOOK_POCKET_SCROLL_ARROW_TAG,
    .palNum = 0,
};

static const struct ScrollArrowsTemplate sRecipeBookListScrollArrowTemplate =
{
    .firstArrowType = SCROLL_ARROW_UP,
    .firstX = 60,
    .firstY = 45,
    .secondArrowType = SCROLL_ARROW_DOWN,
    .secondX = 60,
    .secondY = 147,
    .fullyUpThreshold = -1,
    .fullyDownThreshold = -1,
    .tileTag = RECIPEBOOK_LIST_SCROLL_ARROW_TAG,
    .palTag = RECIPEBOOK_LIST_SCROLL_ARROW_TAG,
    .palNum = 0,
};

static const struct ListMenuTemplate sRecipeBookListMenuTemplate =
{
    .items = NULL,
    .moveCursorFunc = ListMenuDefaultCursorMoveFunc,
    .itemPrintFunc = RecipeBook_PrintListItem,
    .totalItems = 0,
    .maxShowed = 6,
    .windowId = RECIPEBOOK_WIN_LIST,
    .header_X = (0 + RECIPEBOOK_LIST_TEXT_X_OFFSET),
    .item_X = (8 + RECIPEBOOK_LIST_TEXT_X_OFFSET),
    .cursor_X = (0 + RECIPEBOOK_LIST_TEXT_X_OFFSET),
    .upText_Y = (1 + RECIPEBOOK_LIST_TEXT_Y_OFFSET),
    .cursorPal = TEXT_DYNAMIC_COLOR_1,
    .fillValue = 0,
    .cursorShadowPal = TEXT_COLOR_DARK_GRAY,
    .lettersSpacing = 0,
    .itemVerticalPadding = 0,
    .scrollMultiple = LIST_NO_MULTIPLE_SCROLL,
    .fontId = FONT_NARROW,
    .cursorKind = CURSOR_BLACK_ARROW
};

EWRAM_DATA static u8 sRecipeBookPockets[POCKETS_COUNT];
EWRAM_DATA static bool8 sRecipeBookPocketHasRecipes[POCKETS_COUNT];
EWRAM_DATA static u8 sRecipeBookPocketCount;
EWRAM_DATA static u8 sRecipeBookPocketIndex;
EWRAM_DATA static u16 sRecipeBookItemCount;
EWRAM_DATA static struct ListMenuItem sRecipeBookListItems[MAX_RECIPEBOOK_ITEMS];
EWRAM_DATA static u8 sRecipeBookListTaskId;
EWRAM_DATA static u16 sRecipeBookListScroll;
EWRAM_DATA static u16 sRecipeBookListCursor;
EWRAM_DATA static u16 sRecipeBookPocketScroll[POCKETS_COUNT];
EWRAM_DATA static u16 sRecipeBookPocketCursor[POCKETS_COUNT];
EWRAM_DATA static u8 ALIGNED(4) sRecipeBookPocketNameBuffer[32][32];
EWRAM_DATA static u8 sRecipeBookPocketArrowTask;
EWRAM_DATA static u16 sRecipeBookPocketArrowPos;
// HEAP, not EWRAM. Upstream declares these as three BG_SCREEN_SIZE arrays,
// which is 6,144 bytes resident for the whole run whether or not the player
// ever opens the recipe book. This build has ~25 KB of EWRAM left and the
// recipe book is a screen you stand in front of, so it pays for its own
// buffers while it is up and gives them back on the way out -- the shape the
// SwSh bag uses to cost 476 bytes for 9,000 lines. Twelve bytes of pointer
// stay behind.
EWRAM_DATA static u8 *sRecipeBookBg1TilemapBuffer = NULL;
EWRAM_DATA static u8 *sRecipeBookBg2TilemapBuffer = NULL;
EWRAM_DATA static u8 *sRecipeBookBg3TilemapBuffer = NULL;
EWRAM_DATA static u8 sRecipeBookPatternSpriteIds[CRAFT_SLOT_COUNT];
EWRAM_DATA static u16 sRecipeBookPreviewItemId;
EWRAM_DATA static u8 sRecipeBookPreviewVariantIndex;
EWRAM_DATA static u8 sRecipeBookListArrowTask;
EWRAM_DATA static u16 sRecipeBookPendingItemId;
EWRAM_DATA static u8 sRecipeBookPendingVariantIndex;
EWRAM_DATA static u8 sRecipeBookPatternDelay;
EWRAM_DATA static u16 sRecipeBookIngredientsItemId;
static const struct CraftRecipe *sRecipeBookIngredientsRecipe;
static u8 sRecipeBookIngredientsView;
static u8 sRecipeBookIngredientsViewCached;
static bool8 sRecipeBookIngredientsDirty;
static bool8 sRecipeBookIngredientsTilemapDirty;
static u8 sRecipeBookExtendAnimId;
static u8 sRecipeBookExtendAnimState;
static u8 sRecipeBookPatternHideWindowId;
static u16 sRecipeBookSavedWin0H;
static u16 sRecipeBookSavedWin0V;
static u16 sRecipeBookSavedWinIn;
static u16 sRecipeBookSavedWinOut;
EWRAM_DATA static u8 sRecipeBookVariantIndex[ITEMS_COUNT];
static bool8 sRecipeBookEnterExtendedPending;
static u16 sRecipeBookEnterExtendedVBlank;
static bool8 sRecipeBookTimeBlendOverridden;
static u8 sRecipeBookAutoCraftPromptWindowId;
static u8 sRecipeBookAutoCraftQtyWindowId;
static u8 sRecipeBookAutoCraftState;
static bool8 sRecipeBookAutoCraftYesNoActive;
static u16 sRecipeBookAutoCraftItemId;
static const struct CraftRecipe *sRecipeBookAutoCraftRecipe;
static u16 sRecipeBookAutoCraftQty;
static u16 sRecipeBookAutoCraftMaxQty;

static const struct BgTemplate sRecipeBookBgTemplates[] =
{
    {
        .bg = 0,
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 1,
        .baseTile = 0
    },
    {
        .bg = 1,
        .charBaseIndex = 1,
        .mapBaseIndex = 30,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 0,
        .baseTile = 0
    },
    {
        .bg = 2,
        .charBaseIndex = 3,
        .mapBaseIndex = 29,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 2,
        .baseTile = 0
    },
    {
        .bg = 3,
        .charBaseIndex = 2,
        .mapBaseIndex = 28,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 1,
        .baseTile = 0
    }
};

static void RecipeBookMainCB2(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

static void RecipeBookVBlankCB(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

void CB2_CraftRecipeBookMenu(void)
{
    switch (gMain.state)
    {
    default:
    case 0:
        SetVBlankCallback(NULL);
        gMain.state++;
        break;
    case 1:
        ResetVramOamAndBgCntRegs();
        SetGpuReg(REG_OFFSET_DISPCNT, 0);
        ResetBgsAndClearDma3BusyFlags(0);
        InitBgsFromTemplates(0, sRecipeBookBgTemplates, ARRAY_COUNT(sRecipeBookBgTemplates));
        ResetAllBgsCoordinates();
        FreeAllWindowBuffers();
        DeactivateAllTextPrinters();
        ResetTasks();
        ResetSpriteData();
        SetBackdropFromColor(RGB_BLACK);
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
        FillBgTilemapBufferRect(0, 0, 0, 0, DISPLAY_TILE_WIDTH, DISPLAY_TILE_HEIGHT, 15);
        sRecipeBookBg1TilemapBuffer = AllocZeroed(BG_SCREEN_SIZE);
        SetBgTilemapBuffer(1, sRecipeBookBg1TilemapBuffer);
        CopyBgTilemapBufferToVram(1);
        DecompressAndCopyTileDataToVram(1, gBlankGfxCompressed, 0, 0, 0);
        LoadUserWindowBorderGfxOnBg(0, RECIPEBOOK_WINDOW_FRAME_BASE_TILE, BG_PLTT_ID(RECIPEBOOK_WINDOW_FRAME_PALETTE));
        LoadUserWindowBorderGfxOnBg(1, RECIPEBOOK_WINDOW_FRAME_BASE_TILE, BG_PLTT_ID(RECIPEBOOK_WINDOW_FRAME_PALETTE));
        sRecipeBookBg2TilemapBuffer = AllocZeroed(BG_SCREEN_SIZE);
        SetBgTilemapBuffer(2, sRecipeBookBg2TilemapBuffer);
        LoadBgTiles(2, gRecipeBookMenu_Gfx, RECIPEBOOK_BG_TILE_BYTES, 0);
        CopyToBgTilemapBuffer(2, gRecipeBookMenu_Tilemap, RECIPEBOOK_BG_TILEMAP_BYTES, 0);
        CopyBgTilemapBufferToVram(2);
        sRecipeBookBg3TilemapBuffer = AllocZeroed(BG_SCREEN_SIZE);
        SetBgTilemapBuffer(3, sRecipeBookBg3TilemapBuffer);
        LoadBgTiles(3, gRecipeBookMenu_Gfx, RECIPEBOOK_BG_TILE_BYTES, 0);
        CopyBgTilemapBufferToVram(3);
        HideBg(3);
        LoadPalette(gRecipeBookMenu_Pal, BG_PLTT_ID(0), PLTT_SIZE_4BPP);
        gMain.state++;
        break;
    case 2:
        ResetPaletteFade();
        RecipeBook_Init();
        gMain.state++;
        break;
    case 3:
        CreateTask(Task_RecipeBookMenu, 0);
        SetVBlankCallback(RecipeBookVBlankCB);
        SetMainCallback2(RecipeBookMainCB2);
        gMain.state++;
        break;
    case 4:
        break;
    }
}

static bool8 RecipeBook_IsRecipeUnlocked(const struct CraftRecipeList *list)
{
    u8 i;

    for (i = 0; i < list->count; i++)
    {
        u16 flag = list->recipes[i].unlockFlag;
        if (flag == 0 || FlagGet(flag))
            return TRUE;
    }

    return FALSE;
}

static void RecipeBook_BuildPocketList(void)
{
    u16 itemId;
    u8 pocket;

    for (pocket = 0; pocket < POCKETS_COUNT; pocket++)
        sRecipeBookPocketHasRecipes[pocket] = FALSE;

    for (itemId = 0; itemId < gCraftRecipeCount; itemId++)
    {
        const struct CraftRecipeList *list = &gCraftRecipes[itemId];

        if (list->count == 0)
            continue;
        if (!RecipeBook_IsRecipeUnlocked(list))
            continue;

        pocket = GetItemPocket(itemId);
        if (pocket >= POCKETS_COUNT)
            continue;
        sRecipeBookPocketHasRecipes[pocket] = TRUE;
    }

    sRecipeBookPocketCount = 0;
    for (pocket = 0; pocket < POCKETS_COUNT; pocket++)
    {
        if (sRecipeBookPocketHasRecipes[pocket])
            sRecipeBookPockets[sRecipeBookPocketCount++] = pocket;
    }

    if (sRecipeBookPocketIndex >= sRecipeBookPocketCount)
        sRecipeBookPocketIndex = 0;
}

static void RecipeBook_BuildItemList(enum Pocket pocketId)
{
    u16 itemId;

    sRecipeBookItemCount = 0;
    for (itemId = 0; itemId < gCraftRecipeCount; itemId++)
    {
        const struct CraftRecipeList *list = &gCraftRecipes[itemId];

        if (list->count == 0)
            continue;
        if (!RecipeBook_IsRecipeUnlocked(list))
            continue;
        if (GetItemPocket(itemId) != pocketId)
            continue;

        if (sRecipeBookItemCount >= MAX_RECIPEBOOK_ITEMS - 1)
            break;

        sRecipeBookListItems[sRecipeBookItemCount].name = gText_EmptyString7;
        sRecipeBookListItems[sRecipeBookItemCount].id = itemId;
        sRecipeBookItemCount++;
    }

    if (sRecipeBookItemCount == 0)
    {
        sRecipeBookListItems[0].name = gText_EmptyString7;
        sRecipeBookListItems[0].id = ITEM_NONE;
        sRecipeBookItemCount = 1;
    }
    else
    {
        if (sRecipeBookItemCount < MAX_RECIPEBOOK_ITEMS)
        {
            sRecipeBookListItems[sRecipeBookItemCount].name = gText_EmptyString7;
            sRecipeBookListItems[sRecipeBookItemCount].id = LIST_CANCEL;
            sRecipeBookItemCount++;
        }
    }
}

static void RecipeBook_InitWindows(void)
{
    InitWindows(sRecipeBookWindowTemplates);
    ListMenuLoadStdPalAt(BG_PLTT_ID(12), 1);
    LoadPalette(&gStandardMenuPalette, BG_PLTT_ID(15), PLTT_SIZE_4BPP);

    sRecipeBookWindowIds[RECIPEBOOK_WIN_POCKET_NAME] = AddWindow(&sRecipeBookWindowTemplates[RECIPEBOOK_WIN_POCKET_NAME]);
    sRecipeBookWindowIds[RECIPEBOOK_WIN_LIST] = AddWindow(&sRecipeBookWindowTemplates[RECIPEBOOK_WIN_LIST]);
    sRecipeBookWindowIds[RECIPEBOOK_WIN_VARIANT_BUTTONS] = AddWindow(&sRecipeBookWindowTemplates[RECIPEBOOK_WIN_VARIANT_BUTTONS]);
    sRecipeBookWindowIds[RECIPEBOOK_WIN_INGREDIENTS] = AddWindow(&sRecipeBookWindowTemplates[RECIPEBOOK_WIN_INGREDIENTS]);

    FillWindowPixelBuffer(sRecipeBookWindowIds[RECIPEBOOK_WIN_POCKET_NAME], PIXEL_FILL(0));
    PutWindowTilemap(sRecipeBookWindowIds[RECIPEBOOK_WIN_POCKET_NAME]);
    CopyWindowToVram(sRecipeBookWindowIds[RECIPEBOOK_WIN_POCKET_NAME], COPYWIN_FULL);

    FillWindowPixelBuffer(sRecipeBookWindowIds[RECIPEBOOK_WIN_LIST], PIXEL_FILL(0));
    PutWindowTilemap(sRecipeBookWindowIds[RECIPEBOOK_WIN_LIST]);
    CopyWindowToVram(sRecipeBookWindowIds[RECIPEBOOK_WIN_LIST], COPYWIN_FULL);

    FillWindowPixelBuffer(sRecipeBookWindowIds[RECIPEBOOK_WIN_VARIANT_BUTTONS], PIXEL_FILL(0));
    PutWindowTilemap(sRecipeBookWindowIds[RECIPEBOOK_WIN_VARIANT_BUTTONS]);
    CopyWindowToVram(sRecipeBookWindowIds[RECIPEBOOK_WIN_VARIANT_BUTTONS], COPYWIN_FULL);

    FillWindowPixelBuffer(sRecipeBookWindowIds[RECIPEBOOK_WIN_INGREDIENTS], PIXEL_FILL(0));
    PutWindowTilemap(sRecipeBookWindowIds[RECIPEBOOK_WIN_INGREDIENTS]);
    CopyWindowToVram(sRecipeBookWindowIds[RECIPEBOOK_WIN_INGREDIENTS], COPYWIN_FULL);
}

static void RecipeBook_DestroyWindows(void)
{
    u8 i;

    if (sRecipeBookListTaskId != TASK_NONE)
    {
        DestroyListMenuTask(sRecipeBookListTaskId, &sRecipeBookListScroll, &sRecipeBookListCursor);
        sRecipeBookListTaskId = TASK_NONE;
    }

    for (i = 0; i < RECIPEBOOK_WIN_COUNT; i++)
    {
        if (sRecipeBookWindowIds[i] != WINDOW_NONE)
        {
            ClearStdWindowAndFrameToTransparent(sRecipeBookWindowIds[i], TRUE);
            RemoveWindow(sRecipeBookWindowIds[i]);
            sRecipeBookWindowIds[i] = WINDOW_NONE;
        }
    }
}

static void RecipeBook_RefreshListMenu(void)
{
    struct ListMenuTemplate listTemplate = sRecipeBookListMenuTemplate;
    enum Pocket pocketId = POCKET_ITEMS;

    if (sRecipeBookPocketCount != 0)
        pocketId = sRecipeBookPockets[sRecipeBookPocketIndex];
    RecipeBook_BuildItemList(pocketId);

    FillWindowPixelBuffer(sRecipeBookWindowIds[RECIPEBOOK_WIN_LIST], PIXEL_FILL(0));
    CopyWindowToVram(sRecipeBookWindowIds[RECIPEBOOK_WIN_LIST], COPYWIN_FULL);

    if (sRecipeBookListTaskId != TASK_NONE)
    {
        DestroyListMenuTask(sRecipeBookListTaskId, &sRecipeBookListScroll, &sRecipeBookListCursor);
        sRecipeBookListTaskId = TASK_NONE;
    }

    if (sRecipeBookPocketCount != 0)
    {
        sRecipeBookListScroll = sRecipeBookPocketScroll[pocketId];
        sRecipeBookListCursor = sRecipeBookPocketCursor[pocketId];
        SetCursorWithinListBounds(&sRecipeBookListScroll, &sRecipeBookListCursor, listTemplate.maxShowed, sRecipeBookItemCount);
        sRecipeBookPocketScroll[pocketId] = sRecipeBookListScroll;
        sRecipeBookPocketCursor[pocketId] = sRecipeBookListCursor;
    }
    else
    {
        sRecipeBookListScroll = 0;
        sRecipeBookListCursor = 0;
    }

    listTemplate.items = sRecipeBookListItems;
    listTemplate.totalItems = sRecipeBookItemCount;
    listTemplate.windowId = sRecipeBookWindowIds[RECIPEBOOK_WIN_LIST];
    sRecipeBookListTaskId = ListMenuInit(&listTemplate, sRecipeBookListScroll, sRecipeBookListCursor);
    RecipeBook_UpdateListScrollArrows();
    RecipeBook_UpdateVariantButtons();
}

static void RecipeBook_SavePocketListState(void)
{
    enum Pocket pocketId;

    if (sRecipeBookPocketCount == 0)
        return;

    pocketId = sRecipeBookPockets[sRecipeBookPocketIndex];
    sRecipeBookPocketScroll[pocketId] = sRecipeBookListScroll;
    sRecipeBookPocketCursor[pocketId] = sRecipeBookListCursor;
}

static void RecipeBook_Print(u8 windowId, u8 fontId, const u8 *str, u8 left, u8 top, u8 colorIndex)
{
    AddTextPrinterParameterized4(windowId, fontId, left, top, 0, 0, sRecipeBookFontColorTable[colorIndex], TEXT_SKIP_DRAW, str);
}

static void RecipeBook_PrintPocketNames(const u8 *pocketName1, const u8 *pocketName2)
{
    struct WindowTemplate window = {0};
    u16 windowId;
    int offset;

    window.width = 16;
    window.height = 2;
    windowId = AddWindow(&window);
    FillWindowPixelBuffer(windowId, PIXEL_FILL(0));
    offset = GetStringCenterAlignXOffset(FONT_NORMAL, pocketName1, 0x40) + RECIPEBOOK_POCKET_NAME_TEXT_X_OFFSET;
    RecipeBook_Print(windowId, FONT_NORMAL, pocketName1, offset, 1 + RECIPEBOOK_POCKET_NAME_TEXT_Y_OFFSET,
                     RECIPEBOOK_COLOR_POCKET_NAME);
    if (pocketName2)
    {
        offset = GetStringCenterAlignXOffset(FONT_NORMAL, pocketName2, 0x40) + RECIPEBOOK_POCKET_NAME_TEXT_X_OFFSET;
        RecipeBook_Print(windowId, FONT_NORMAL, pocketName2, offset + 0x40, 1 + RECIPEBOOK_POCKET_NAME_TEXT_Y_OFFSET,
                         RECIPEBOOK_COLOR_POCKET_NAME);
    }
    CpuCopy32((u8 *)GetWindowAttribute(windowId, WINDOW_TILE_DATA), sRecipeBookPocketNameBuffer, sizeof(sRecipeBookPocketNameBuffer));
    RemoveWindow(windowId);
}

static void RecipeBook_CopyPocketNameToWindow(u32 offset)
{
    u8 (*tileDataBuffer)[32][32];
    u8 *windowTileData;
    int bottomOffset;

    if (offset > 8)
        offset = 8;
    tileDataBuffer = &sRecipeBookPocketNameBuffer;
    windowTileData = (u8 *)GetWindowAttribute(sRecipeBookWindowIds[RECIPEBOOK_WIN_POCKET_NAME], WINDOW_TILE_DATA);
    CpuCopy32(&tileDataBuffer[0][offset], windowTileData, 0x100);
    bottomOffset = offset + 16;
    CpuCopy32(&tileDataBuffer[0][bottomOffset], windowTileData + 0x100, 0x100);
    CopyWindowToVram(sRecipeBookWindowIds[RECIPEBOOK_WIN_POCKET_NAME], COPYWIN_GFX);
}

static u16 RecipeBook_GetSelectedItemId(void)
{
    u16 index = sRecipeBookListScroll + sRecipeBookListCursor;

    if (index >= sRecipeBookItemCount)
        return ITEM_NONE;

    return sRecipeBookListItems[index].id;
}

static const struct CraftRecipe *RecipeBook_GetPreviewRecipe(u16 itemId)
{
    u8 variantIndex = RecipeBook_GetVariantIndex(itemId);

    return RecipeBook_GetRecipeVariant(itemId, variantIndex);
}

static u8 RecipeBook_BuildIngredientList(const struct CraftRecipe *recipe, u16 ingredientIds[], u8 ingredientCounts[])
{
    u8 ingredientCount = 0;
    u8 i;
    int r, c;

    if (recipe == NULL)
        return 0;

    for (r = 0; r < CRAFT_ROWS; r++)
    {
        for (c = 0; c < CRAFT_COLS; c++)
        {
            u16 ingredientId = recipe->pattern[r][c];

            if (ingredientId == ITEM_NONE)
                continue;

            for (i = 0; i < ingredientCount; i++)
            {
                if (ingredientIds[i] == ingredientId)
                {
                    ingredientCounts[i]++;
                    break;
                }
            }

            if (i == ingredientCount && ingredientCount < RECIPEBOOK_INGREDIENTS_MAX)
            {
                ingredientIds[ingredientCount] = ingredientId;
                ingredientCounts[ingredientCount] = 1;
                ingredientCount++;
            }
        }
    }

    return ingredientCount;
}

static void RecipeBook_ClearPatternIcons(void)
{
    int i;

    for (i = 0; i < CRAFT_SLOT_COUNT; i++)
    {
        if (sRecipeBookPatternSpriteIds[i] != SPRITE_NONE)
        {
            DestroySprite(&gSprites[sRecipeBookPatternSpriteIds[i]]);
            FreeSpriteTilesByTag(TAG_RECIPEBOOK_PATTERN_ICON_BASE + i);
            FreeSpritePaletteByTag(TAG_RECIPEBOOK_PATTERN_ICON_BASE + i);
            sRecipeBookPatternSpriteIds[i] = SPRITE_NONE;
        }
    }
}

static void RecipeBook_ResetPatternPreviewState(void)
{
    RecipeBook_ClearPatternIcons();
    sRecipeBookPreviewItemId = ITEM_NONE;
    sRecipeBookPreviewVariantIndex = 0;
    sRecipeBookPendingItemId = ITEM_NONE;
    sRecipeBookPendingVariantIndex = 0;
    sRecipeBookPatternDelay = 0;
}

static void RecipeBook_UpdatePatternPreview(void)
{
    const struct CraftRecipe *recipe;
    u16 itemId;
    u8 variantIndex;
    int r, c;

    if (sRecipeBookIngredientsView == RECIPEBOOK_VIEW_EXTENDED)
        return;

    itemId = RecipeBook_GetSelectedItemId();
    variantIndex = RecipeBook_GetVariantIndex(itemId);
    if (itemId == sRecipeBookPreviewItemId && variantIndex == sRecipeBookPreviewVariantIndex)
        return;

    RecipeBook_ClearPatternIcons();
    sRecipeBookPreviewItemId = itemId;
    sRecipeBookPreviewVariantIndex = variantIndex;
    recipe = RecipeBook_GetPreviewRecipe(itemId);
    if (recipe == NULL)
        return;

    for (r = 0; r < CRAFT_ROWS; r++)
    {
        for (c = 0; c < CRAFT_COLS; c++)
        {
            u16 ingredientId = recipe->pattern[r][c];
            u8 slot = r * CRAFT_COLS + c;

            if (ingredientId != ITEM_NONE)
            {
                u16 tag = TAG_RECIPEBOOK_PATTERN_ICON_BASE + slot;
                u8 spriteId = AddItemIconSprite(tag, tag, ingredientId);
                s16 x;
                s16 y;

                if (spriteId != MAX_SPRITES)
                {
                    UpdateSpritePaletteWithTime(IndexOfSpritePaletteTag(tag));
                    CraftMenuUI_GetItemIconPos(slot, RECIPEBOOK_PATTERN_BASE_X, RECIPEBOOK_PATTERN_BASE_Y, &x, &y);
                    gSprites[spriteId].x = x;
                    gSprites[spriteId].y = y;
                    gSprites[spriteId].oam.priority = 0;
                    sRecipeBookPatternSpriteIds[slot] = spriteId;
                }
            }
        }
    }
}

static void RecipeBook_SchedulePatternPreviewWithDelay(u8 delay)
{
    u16 itemId = RecipeBook_GetSelectedItemId();
    u8 variantIndex = RecipeBook_GetVariantIndex(itemId);

    if (sRecipeBookIngredientsView == RECIPEBOOK_VIEW_EXTENDED)
        return;

    if (itemId == sRecipeBookPreviewItemId
        && variantIndex == sRecipeBookPreviewVariantIndex
        && sRecipeBookPatternDelay == 0)
        return;
    if (itemId == sRecipeBookPendingItemId
        && variantIndex == sRecipeBookPendingVariantIndex
        && sRecipeBookPatternDelay != 0)
        return;

    RecipeBook_ClearPatternIcons();
    sRecipeBookPreviewItemId = ITEM_NONE;
    sRecipeBookPendingItemId = itemId;
    sRecipeBookPendingVariantIndex = variantIndex;
    sRecipeBookPatternDelay = delay;
}

static void RecipeBook_SchedulePatternPreview(void)
{
    RecipeBook_SchedulePatternPreviewWithDelay(2);
}

static void RecipeBook_TickPatternPreview(void)
{
    u16 itemId;
    u8 variantIndex;

    if (sRecipeBookIngredientsView == RECIPEBOOK_VIEW_EXTENDED)
        return;

    if (sRecipeBookPatternDelay == 0)
        return;

    sRecipeBookPatternDelay--;
    if (sRecipeBookPatternDelay != 0)
        return;

    itemId = RecipeBook_GetSelectedItemId();
    variantIndex = RecipeBook_GetVariantIndex(itemId);
    if (sRecipeBookPendingItemId != itemId || sRecipeBookPendingVariantIndex != variantIndex)
    {
        RecipeBook_SchedulePatternPreview();
        return;
    }

    RecipeBook_UpdatePatternPreview();
}

static s32 RecipeBook_FindBreakPos(const u8 *str, u32 maxWidth, u8 fontId)
{
    s32 best = -1;
    u32 i;
    u8 temp[RECIPEBOOK_INGREDIENTS_LINE_BUFFER];

    for (i = 0; str[i] != EOS; i++)
    {
        if (str[i] == CHAR_SPACE)
        {
            StringCopyN(temp, str, i);
            temp[i] = EOS;
            if (GetStringWidth(fontId, temp, 0) <= maxWidth)
                best = i;
            else
                break;
        }
    }

    return best;
}

static void RecipeBook_TruncateIngredientLine(const u8 *src, u8 *dest, u32 width, u8 fontId)
{
    u32 ellipsisWidth = GetStringWidth(fontId, sText_RecipeBookEllipsis, 0);
    u32 maxWidth = (width > ellipsisWidth) ? (width - ellipsisWidth) : 0;
    s32 truncPos = RecipeBook_FindBreakPos(src, maxWidth, fontId);

    if (truncPos < 0)
    {
        s32 i;
        u8 temp[RECIPEBOOK_INGREDIENTS_LINE_BUFFER];

        if (maxWidth == 0)
        {
            StringCopy(dest, sText_RecipeBookEllipsis);
            return;
        }

        truncPos = -1;
        for (i = 0; src[i] != EOS; i++)
        {
            StringCopyN(temp, src, i + 1);
            temp[i + 1] = EOS;
            if (GetStringWidth(fontId, temp, 0) > maxWidth)
                break;
            truncPos = i;
        }

        if (truncPos < 0)
        {
            StringCopy(dest, sText_RecipeBookEllipsis);
        }
        else
        {
            StringCopyN(dest, src, truncPos + 1);
            dest[truncPos + 1] = EOS;
            StringAppend(dest, sText_RecipeBookEllipsis);
        }
    }
    else
    {
        StringCopyN(dest, src, truncPos);
        dest[truncPos] = EOS;
        StringAppend(dest, sText_RecipeBookEllipsis);
    }
}

static u8 RecipeBook_GetUniqueIngredientCount(const struct CraftRecipe *recipe)
{
    u16 ingredientIds[RECIPEBOOK_INGREDIENTS_MAX];
    u8 ingredientCounts[RECIPEBOOK_INGREDIENTS_MAX];

    return RecipeBook_BuildIngredientList(recipe, ingredientIds, ingredientCounts);
}

static u8 RecipeBook_GetUnlockedVariantCount(u16 itemId)
{
    const struct CraftRecipeList *list;
    u8 count = 0;
    u8 i;

    if (itemId == ITEM_NONE || itemId == LIST_CANCEL)
        return 0;
    if (itemId >= gCraftRecipeCount)
        return 0;

    list = &gCraftRecipes[itemId];
    if (list->count == 0)
        return 0;

    for (i = 0; i < list->count; i++)
    {
        const struct CraftRecipe *recipe = &list->recipes[i];
        if (recipe->unlockFlag == 0 || FlagGet(recipe->unlockFlag))
            count++;
    }

    return count;
}

static u8 RecipeBook_GetVariantIndex(u16 itemId)
{
    u8 count;

    if (itemId >= ITEMS_COUNT)
        return 0;

    count = RecipeBook_GetUnlockedVariantCount(itemId);
    if (count == 0)
        return 0;

    if (sRecipeBookVariantIndex[itemId] >= count)
        sRecipeBookVariantIndex[itemId] = count - 1;

    return sRecipeBookVariantIndex[itemId];
}

static const struct CraftRecipe *RecipeBook_GetRecipeVariant(u16 itemId, u8 variantIndex)
{
    const struct CraftRecipeList *list;
    u8 count = 0;
    u8 i;

    if (itemId == ITEM_NONE || itemId == LIST_CANCEL)
        return NULL;
    if (itemId >= gCraftRecipeCount)
        return NULL;

    list = &gCraftRecipes[itemId];
    if (list->count == 0)
        return NULL;

    for (i = 0; i < list->count; i++)
    {
        const struct CraftRecipe *recipe = &list->recipes[i];
        if (recipe->unlockFlag == 0 || FlagGet(recipe->unlockFlag))
        {
            if (count == variantIndex)
                return recipe;
            count++;
        }
    }

    return NULL;
}

static void RecipeBook_BuildFittedItemName(u16 itemId, u8 fontId, u32 maxWidth, u8 *render)
{
    u8 name[ITEM_NAME_LENGTH + 1];
    u32 fitFontId;
    u8 *end;

    CopyItemName(itemId, name);
    fitFontId = GetFontIdToFit(name, fontId, 0, maxWidth);
    if (GetStringWidth(fitFontId, name, 0) <= maxWidth)
        StringCopy(render, name);
    else
        RecipeBook_TruncateIngredientLine(name, render, maxWidth, fitFontId);

    end = render + StringLength(render);
    if (fitFontId != fontId)
        PrependFontIdToFit(render, end, fontId, maxWidth);
}

static void RecipeBook_PrintListItem(u8 windowId, u32 itemId, u8 y)
{
    u8 render[ITEM_NAME_LENGTH + 8];
    u8 fraction[RECIPEBOOK_INGREDIENTS_PAGE_BUFFER];
    u8 maxFraction[RECIPEBOOK_INGREDIENTS_PAGE_BUFFER];
    u8 fontId = sRecipeBookListMenuTemplate.fontId;
    u8 indicatorFontId = FONT_SMALL_NARROWER;
    u8 bigH = GetFontAttribute(fontId, FONTATTR_MAX_LETTER_HEIGHT);
    u8 smallH = GetFontAttribute(indicatorFontId, FONTATTR_MAX_LETTER_HEIGHT);
    u8 nameX = sRecipeBookListMenuTemplate.item_X;
    u32 windowWidth = WindowWidthPx(windowId);
    u32 maxNameWidth;
    u8 colors[3] = {sRecipeBookListMenuTemplate.fillValue, sRecipeBookListMenuTemplate.cursorPal, sRecipeBookListMenuTemplate.cursorShadowPal};
    u8 variantCount;

    if (itemId == LIST_CANCEL)
    {
        AddTextPrinterParameterized4(windowId, fontId, nameX, y, 0, 0, colors, TEXT_SKIP_DRAW, sText_RecipeBookClose);
        return;
    }
    if (itemId == ITEM_NONE && sRecipeBookItemCount == 1)
    {
        AddTextPrinterParameterized4(windowId, fontId, nameX, y, 0, 0, colors, TEXT_SKIP_DRAW, sText_RecipeBookNoRecipes);
        return;
    }
    if (itemId >= gCraftRecipeCount)
        return;

    variantCount = RecipeBook_GetUnlockedVariantCount(itemId);

    if (variantCount > 1)
    {
        u8 variantIndex = RecipeBook_GetVariantIndex(itemId);
        u8 indicatorY = y + (bigH - smallH) / 2 + RECIPEBOOK_VARIANT_INDICATOR_Y_OFFSET;
        u8 indicatorColors[3] = {TEXT_COLOR_TRANSPARENT, TEXT_DYNAMIC_COLOR_1, TEXT_COLOR_DARK_GRAY};
        u32 fractionWidth;
        u32 maxFractionWidth;
        u32 indicatorBaseX;
        u32 indicatorX;
        u32 fractionX;
        u8 *ptr;

        ptr = fraction;
        *ptr++ = CHAR_LEFT_PAREN;
        ptr = ConvertIntToDecimalStringN(ptr, variantIndex + 1, STR_CONV_MODE_LEFT_ALIGN, 2);
        *ptr++ = CHAR_SLASH;
        ptr = ConvertIntToDecimalStringN(ptr, variantCount, STR_CONV_MODE_LEFT_ALIGN, 2);
        *ptr++ = CHAR_RIGHT_PAREN;
        *ptr = EOS;

        ptr = maxFraction;
        *ptr++ = CHAR_LEFT_PAREN;
        ptr = ConvertIntToDecimalStringN(ptr, variantCount, STR_CONV_MODE_LEFT_ALIGN, 2);
        *ptr++ = CHAR_SLASH;
        ptr = ConvertIntToDecimalStringN(ptr, variantCount, STR_CONV_MODE_LEFT_ALIGN, 2);
        *ptr++ = CHAR_RIGHT_PAREN;
        *ptr = EOS;

        fractionWidth = GetStringWidth(indicatorFontId, fraction, 0);
        maxFractionWidth = GetStringWidth(indicatorFontId, maxFraction, 0);

        indicatorBaseX = windowWidth - RECIPEBOOK_VARIANT_INDICATOR_RIGHT_PADDING - maxFractionWidth;
        maxNameWidth = (indicatorBaseX > nameX + RECIPEBOOK_VARIANT_INDICATOR_GAP)
            ? (indicatorBaseX - nameX - RECIPEBOOK_VARIANT_INDICATOR_GAP)
            : 0;
        indicatorX = indicatorBaseX;

        fractionX = indicatorX;

        RecipeBook_BuildFittedItemName(itemId, fontId, maxNameWidth, render);
        AddTextPrinterParameterized4(windowId, fontId, nameX, y, 0, 0, colors, TEXT_SKIP_DRAW, render);

        if (fractionWidth <= maxFractionWidth)
        {
            AddTextPrinterParameterized4(windowId, indicatorFontId, fractionX, indicatorY, 0, 0,
                                         indicatorColors, TEXT_SKIP_DRAW, fraction);
        }
    }
    else
    {
        maxNameWidth = (windowWidth > nameX + RECIPEBOOK_VARIANT_INDICATOR_RIGHT_PADDING)
            ? (windowWidth - nameX - RECIPEBOOK_VARIANT_INDICATOR_RIGHT_PADDING)
            : 0;
        RecipeBook_BuildFittedItemName(itemId, fontId, maxNameWidth, render);
        AddTextPrinterParameterized4(windowId, fontId, nameX, y, 0, 0, colors, TEXT_SKIP_DRAW, render);
    }
}

static void RecipeBook_RedrawListRow(u16 itemIndex, u8 row)
{
    u8 windowId = sRecipeBookWindowIds[RECIPEBOOK_WIN_LIST];
    u8 yMultiplier = GetFontAttribute(sRecipeBookListMenuTemplate.fontId, FONTATTR_MAX_LETTER_HEIGHT)
                   + sRecipeBookListMenuTemplate.itemVerticalPadding;
    u8 y = row * yMultiplier + sRecipeBookListMenuTemplate.upText_Y;
    u8 x = sRecipeBookListMenuTemplate.item_X;
    u8 width = WindowWidthPx(windowId) - x;
    u8 height = yMultiplier;

    if (windowId == WINDOW_NONE)
        return;
    if (itemIndex >= sRecipeBookItemCount)
        return;

    FillWindowPixelRect(windowId, PIXEL_FILL(sRecipeBookListMenuTemplate.fillValue), x, y, width, height);
    RecipeBook_PrintListItem(windowId, sRecipeBookListItems[itemIndex].id, y);
    CopyWindowToVram(windowId, COPYWIN_GFX);
}

static void RecipeBook_RedrawCurrentListRow(void)
{
    RecipeBook_RedrawListRow(sRecipeBookListScroll + sRecipeBookListCursor, sRecipeBookListCursor);
}

static void RecipeBook_UpdateIngredientsHeaderOnly(void)
{
    u8 windowId = sRecipeBookWindowIds[RECIPEBOOK_WIN_INGREDIENTS];
    const struct CraftRecipe *recipe;
    u16 itemId;
    u16 ingredientIds[RECIPEBOOK_INGREDIENTS_MAX];
    u8 ingredientCounts[RECIPEBOOK_INGREDIENTS_MAX];
    u8 ingredientCount;
    u8 i;
    bool8 canAutoCraft;
    bool8 hasAllIngredients;
    u8 headerFontId = FONT_NARROWER;
    u8 headerY = RECIPEBOOK_INGREDIENTS_HEADER_Y + RECIPEBOOK_INGREDIENTS_TEXT_Y_OFFSET;
    u32 windowWidth;
    s16 headerX;
    u8 copyMode;

    if (windowId == WINDOW_NONE)
        return;

    itemId = RecipeBook_GetSelectedItemId();
    recipe = RecipeBook_GetPreviewRecipe(itemId);

    FillWindowPixelBuffer(windowId, PIXEL_FILL(0));

    windowWidth = WindowWidthPx(windowId);
    headerX = GetStringCenterAlignXOffset(headerFontId, sText_RecipeBookIngredients, windowWidth)
            + RECIPEBOOK_INGREDIENTS_TEXT_X_OFFSET;
    if (headerX < 0)
        headerX = 0;

    AddTextPrinterParameterized4(windowId, headerFontId, headerX, headerY, 0, 0,
                                 sRecipeBookFontColorTable[RECIPEBOOK_COLOR_INGREDIENT_HEADER], TEXT_SKIP_DRAW,
                                 sText_RecipeBookIngredients);

    canAutoCraft = CraftLogic_IsAutoCraftEnabled();
    hasAllIngredients = FALSE;
    if (recipe != NULL)
    {
        ingredientCount = RecipeBook_BuildIngredientList(recipe, ingredientIds, ingredientCounts);
        if (ingredientCount != 0)
        {
            hasAllIngredients = TRUE;
            for (i = 0; i < ingredientCount; i++)
            {
                if (CountTotalItemQuantityInBag(ingredientIds[i]) < ingredientCounts[i])
                {
                    hasAllIngredients = FALSE;
                    break;
                }
            }
        }
    }

    if (canAutoCraft && hasAllIngredients)
    {
        s16 iconX = headerX - GetStringWidth(headerFontId, sText_RecipeBookAButton, 0)
                  - RECIPEBOOK_INGREDIENTS_HEADER_ICON_GAP;

        if (iconX < 0)
            iconX = 0;

        AddTextPrinterParameterized4(windowId, headerFontId, iconX, headerY, 0, 0,
                                     sRecipeBookFontColorTable[RECIPEBOOK_COLOR_INGREDIENT_HEADER], TEXT_SKIP_DRAW,
                                     sText_RecipeBookAButton);
    }

    sRecipeBookIngredientsDirty = TRUE;
    copyMode = sRecipeBookIngredientsTilemapDirty ? COPYWIN_FULL : COPYWIN_GFX;
    CopyWindowToVram(windowId, copyMode);
}

static void RecipeBook_UpdateIngredientsWindow(void)
{
    u8 windowId = sRecipeBookWindowIds[RECIPEBOOK_WIN_INGREDIENTS];
    const struct CraftRecipe *recipe;
    u16 itemId;
    u16 ingredientIds[RECIPEBOOK_INGREDIENTS_MAX];
    u8 ingredientCounts[RECIPEBOOK_INGREDIENTS_MAX];
    u8 ingredientCount = 0;
    u8 i;
    u8 copyMode;
    bool8 canAutoCraft;
    bool8 hasAllIngredients;
    s16 headerX;

    if (windowId == WINDOW_NONE)
        return;

    if (sRecipeBookEnterExtendedPending)
        return;

    if (sRecipeBookExtendAnimState != RECIPEBOOK_EXTEND_ANIM_NONE)
        return;

    itemId = RecipeBook_GetSelectedItemId();
    recipe = RecipeBook_GetPreviewRecipe(itemId);

    if (!sRecipeBookIngredientsDirty
        && !sRecipeBookIngredientsTilemapDirty
        && itemId == sRecipeBookIngredientsItemId
        && recipe == sRecipeBookIngredientsRecipe
        && sRecipeBookIngredientsView == sRecipeBookIngredientsViewCached)
        return;

    sRecipeBookIngredientsDirty = FALSE;
    copyMode = COPYWIN_GFX;
    if (sRecipeBookIngredientsTilemapDirty)
    {
        copyMode = COPYWIN_FULL;
        sRecipeBookIngredientsTilemapDirty = FALSE;
    }
    sRecipeBookIngredientsItemId = itemId;
    sRecipeBookIngredientsRecipe = recipe;
    sRecipeBookIngredientsViewCached = sRecipeBookIngredientsView;

    FillWindowPixelBuffer(windowId, PIXEL_FILL(0));

    u8 headerFontId = FONT_NARROWER;
    u8 ingredientBaseFontId = FONT_SMALL;
    u8 headerLineHeight = GetFontAttribute(headerFontId, FONTATTR_MAX_LETTER_HEIGHT);
    u8 ingredientLineHeight = GetFontAttribute(ingredientBaseFontId, FONTATTR_MAX_LETTER_HEIGHT);
    u8 headerY = RECIPEBOOK_INGREDIENTS_HEADER_Y + RECIPEBOOK_INGREDIENTS_TEXT_Y_OFFSET;
    u32 windowWidth = WindowWidthPx(windowId);
    u32 usableWidth = windowWidth;

    canAutoCraft = CraftLogic_IsAutoCraftEnabled();
    hasAllIngredients = TRUE;
    headerX = GetStringCenterAlignXOffset(headerFontId, sText_RecipeBookIngredients, windowWidth)
            + RECIPEBOOK_INGREDIENTS_TEXT_X_OFFSET;
    if (headerX < 0)
        headerX = 0;
    AddTextPrinterParameterized4(windowId, headerFontId, headerX, headerY, 0, 0,
                                 sRecipeBookFontColorTable[RECIPEBOOK_COLOR_INGREDIENT_HEADER], TEXT_SKIP_DRAW,
                                 sText_RecipeBookIngredients);

    if (recipe == NULL)
    {
        CopyWindowToVram(windowId, copyMode);
        return;
    }

    ingredientCount = RecipeBook_BuildIngredientList(recipe, ingredientIds, ingredientCounts);

    if (ingredientCount == 0)
    {
        CopyWindowToVram(windowId, copyMode);
        return;
    }

    u8 listStartY = headerY + headerLineHeight + RECIPEBOOK_INGREDIENTS_HEADER_TO_LIST_GAP;

    if (sRecipeBookIngredientsView == RECIPEBOOK_VIEW_NORMAL)
    {
        if (ingredientCount <= RECIPEBOOK_INGREDIENTS_INLINE_MAX)
        {
            u8 end = ingredientCount;
            u8 x = RECIPEBOOK_INGREDIENTS_TEXT_X_OFFSET + RECIPEBOOK_INGREDIENTS_LIST_X_OFFSET;
            u8 y = listStartY;
            u8 line[RECIPEBOOK_INGREDIENTS_LINE_BUFFER];
            u8 render[RECIPEBOOK_INGREDIENTS_LINE_BUFFER];

            for (i = 0; i < end; i++)
            {
                u16 required = ingredientCounts[i];
                u16 have = CountTotalItemQuantityInBag(ingredientIds[i]);
                u8 colorId = (have >= required) ? RECIPEBOOK_COLOR_INGREDIENT_HAVE : RECIPEBOOK_COLOR_INGREDIENT_MISS;
                if (have < required)
                    hasAllIngredients = FALSE;
                u8 *endPtr = CopyItemName(ingredientIds[i], line);
                u8 lineFontId = ingredientBaseFontId;

                if (required > 1)
                {
                    *endPtr++ = CHAR_SPACE;
                    *endPtr++ = CHAR_x;
                    endPtr = ConvertIntToDecimalStringN(endPtr, required, STR_CONV_MODE_LEFT_ALIGN, 2);
                    *endPtr = EOS;
                }

                if (GetStringWidth(ingredientBaseFontId, line, 0) > RECIPEBOOK_INGREDIENTS_ITEM_NAME_MAX_WIDTH)
                    lineFontId = FONT_SMALL_NARROW;

                if (GetStringWidth(lineFontId, line, 0) <= usableWidth)
                    StringCopy(render, line);
                else
                    RecipeBook_TruncateIngredientLine(line, render, usableWidth, lineFontId);

                AddTextPrinterParameterized4(windowId, lineFontId, x, y, 0, 0,
                                             sRecipeBookFontColorTable[colorId], TEXT_SKIP_DRAW, render);
                y += ingredientLineHeight + RECIPEBOOK_INGREDIENTS_LINE_SPACING;
            }
        }
        else
        {
            u8 ownedFull = 0;
            u8 summary[RECIPEBOOK_INGREDIENTS_SUMMARY_BUFFER];
            u8 *ptr = summary;
            u8 y = listStartY;

            for (i = 0; i < ingredientCount; i++)
            {
                u16 required = ingredientCounts[i];
                u16 have = CountTotalItemQuantityInBag(ingredientIds[i]);
                if (have >= required)
                    ownedFull++;
                else
                    hasAllIngredients = FALSE;
            }

            ptr = ConvertIntToDecimalStringN(ptr, ownedFull, STR_CONV_MODE_LEFT_ALIGN, 2);
            *ptr++ = CHAR_SLASH;
            ptr = ConvertIntToDecimalStringN(ptr, ingredientCount, STR_CONV_MODE_LEFT_ALIGN, 2);
            *ptr++ = CHAR_SPACE;
            ptr = StringCopy(ptr, sText_RecipeBookIngredientsLabel);
            *ptr = EOS;

            AddTextPrinterParameterized4(windowId, ingredientBaseFontId,
                                         GetStringCenterAlignXOffset(ingredientBaseFontId, summary, windowWidth),
                                         y, 0, 0,
                                         sRecipeBookFontColorTable[RECIPEBOOK_COLOR_INGREDIENT_HEADER], TEXT_SKIP_DRAW,
                                         summary);
            y += ingredientLineHeight + RECIPEBOOK_INGREDIENTS_LINE_SPACING;
            AddTextPrinterParameterized4(windowId, ingredientBaseFontId,
                                         GetStringCenterAlignXOffset(ingredientBaseFontId, sText_RecipeBookOwnedLabel, windowWidth),
                                         y, 0, 0,
                                         sRecipeBookFontColorTable[RECIPEBOOK_COLOR_INGREDIENT_HEADER], TEXT_SKIP_DRAW,
                                         sText_RecipeBookOwnedLabel);

            {
                u8 baseY = y + ingredientLineHeight + RECIPEBOOK_INGREDIENTS_LINE_SPACING;
                u8 iconY = baseY + RECIPEBOOK_INGREDIENTS_SELECT_ICON_Y_OFFSET;
                u8 iconX = RECIPEBOOK_INGREDIENTS_SELECT_ICON_X_OFFSET;

                AddTextPrinterParameterized4(windowId, ingredientBaseFontId, iconX, iconY, 0, 0,
                                             sRecipeBookFontColorTable[RECIPEBOOK_COLOR_INGREDIENT_HEADER], TEXT_SKIP_DRAW,
                                             sText_RecipeBookSelectButton);
                s16 listViewX = GetStringCenterAlignXOffset(ingredientBaseFontId, sText_RecipeBookIngredientsLabel, windowWidth)
                             + RECIPEBOOK_INGREDIENTS_LIST_VIEW_X_OFFSET;
                s16 listViewListX = GetStringCenterAlignXOffset(ingredientBaseFontId, sText_RecipeBookViewIngredientList, windowWidth)
                                 + RECIPEBOOK_INGREDIENTS_LIST_VIEW_X_OFFSET;

                if (listViewX < 0)
                    listViewX = 0;
                if (listViewListX < 0)
                    listViewListX = 0;

                AddTextPrinterParameterized4(windowId, ingredientBaseFontId,
                                             listViewX,
                                             baseY, 0, 0,
                                             sRecipeBookFontColorTable[RECIPEBOOK_COLOR_INGREDIENT_HEADER], TEXT_SKIP_DRAW,
                                             sText_RecipeBookIngredientsLabel);
                AddTextPrinterParameterized4(windowId, ingredientBaseFontId,
                                             listViewListX,
                                             baseY + ingredientLineHeight + RECIPEBOOK_INGREDIENTS_LINE_SPACING, 0, 0,
                                             sRecipeBookFontColorTable[RECIPEBOOK_COLOR_INGREDIENT_HEADER], TEXT_SKIP_DRAW,
                                             sText_RecipeBookViewIngredientList);
            }
        }
    }
    else
    {
        u8 end = ingredientCount;
        u8 x = RECIPEBOOK_INGREDIENTS_TEXT_X_OFFSET + RECIPEBOOK_INGREDIENTS_LIST_X_OFFSET;
        u8 y = listStartY;
        u8 line[RECIPEBOOK_INGREDIENTS_LINE_BUFFER];
        u8 render[RECIPEBOOK_INGREDIENTS_LINE_BUFFER];

        if (end > RECIPEBOOK_INGREDIENTS_MAX)
            end = RECIPEBOOK_INGREDIENTS_MAX;

        for (i = 0; i < end; i++)
        {
            u16 required = ingredientCounts[i];
            u16 have = CountTotalItemQuantityInBag(ingredientIds[i]);
            u8 colorId = (have >= required) ? RECIPEBOOK_COLOR_INGREDIENT_HAVE : RECIPEBOOK_COLOR_INGREDIENT_MISS;
            if (have < required)
                hasAllIngredients = FALSE;
            u8 *endPtr = CopyItemName(ingredientIds[i], line);
            u8 lineFontId = ingredientBaseFontId;

            if (required > 1)
            {
                *endPtr++ = CHAR_SPACE;
                *endPtr++ = CHAR_x;
                endPtr = ConvertIntToDecimalStringN(endPtr, required, STR_CONV_MODE_LEFT_ALIGN, 2);
                *endPtr = EOS;
            }

            if (GetStringWidth(ingredientBaseFontId, line, 0) > RECIPEBOOK_INGREDIENTS_ITEM_NAME_MAX_WIDTH)
                lineFontId = FONT_SMALL_NARROW;

            if (GetStringWidth(lineFontId, line, 0) <= usableWidth)
                StringCopy(render, line);
            else
                RecipeBook_TruncateIngredientLine(line, render, usableWidth, lineFontId);

            AddTextPrinterParameterized4(windowId, lineFontId, x, y, 0, 0,
                                         sRecipeBookFontColorTable[colorId], TEXT_SKIP_DRAW, render);
            y += ingredientLineHeight + RECIPEBOOK_INGREDIENTS_LINE_SPACING;
        }
    }

    if (canAutoCraft && hasAllIngredients)
    {
        s16 iconX = headerX - GetStringWidth(headerFontId, sText_RecipeBookAButton, 0)
                  - RECIPEBOOK_INGREDIENTS_HEADER_ICON_GAP;

        if (iconX < 0)
            iconX = 0;

        AddTextPrinterParameterized4(windowId, headerFontId, iconX, headerY, 0, 0,
                                     sRecipeBookFontColorTable[RECIPEBOOK_COLOR_INGREDIENT_HEADER], TEXT_SKIP_DRAW,
                                     sText_RecipeBookAButton);
    }

    CopyWindowToVram(windowId, copyMode);
}

static u16 RecipeBook_GetMaxCraftableQuantity(const struct CraftRecipe *recipe)
{
    u16 ingredientIds[RECIPEBOOK_INGREDIENTS_MAX];
    u8 ingredientCounts[RECIPEBOOK_INGREDIENTS_MAX];
    u8 ingredientCount;
    u16 maxSets = 0xFFFF;
    u8 i;

    if (recipe == NULL || recipe->resultQuantity == 0)
        return 0;

    ingredientCount = RecipeBook_BuildIngredientList(recipe, ingredientIds, ingredientCounts);
    if (ingredientCount == 0)
        return 0;

    for (i = 0; i < ingredientCount; i++)
    {
        u16 have = CountTotalItemQuantityInBag(ingredientIds[i]);
        u16 sets = have / ingredientCounts[i];
        if (sets < maxSets)
            maxSets = sets;
    }

    if (maxSets == 0 || maxSets == 0xFFFF)
        return 0;

    u16 outputQty = maxSets * recipe->resultQuantity;
    if (outputQty > MAX_BAG_ITEM_CAPACITY)
    {
        outputQty = MAX_BAG_ITEM_CAPACITY - (MAX_BAG_ITEM_CAPACITY % recipe->resultQuantity);
        if (outputQty < recipe->resultQuantity)
            return 0;
    }

    return outputQty;
}

static bool8 RecipeBook_AdjustAutoCraftQuantity(u16 *quantity, u16 max, u16 step)
{
    u16 prev = *quantity;
    u32 bulk;

    if (step == 0)
        step = 1;

    bulk = (u32)step * 10;

    if (JOY_REPEAT(DPAD_UP))
    {
        *quantity += step;
        if (*quantity > max)
            *quantity = step;
    }
    else if (JOY_REPEAT(DPAD_DOWN))
    {
        if (*quantity <= step)
            *quantity = max;
        else
            *quantity -= step;
    }
    else if (JOY_REPEAT(DPAD_RIGHT))
    {
        u32 next = *quantity + bulk;

        if (next > max)
            *quantity = max;
        else
            *quantity = (u16)next;
    }
    else if (JOY_REPEAT(DPAD_LEFT))
    {
        if (*quantity <= step || *quantity <= bulk)
            *quantity = step;
        else
            *quantity -= (u16)bulk;
    }
    else
    {
        return FALSE;
    }

    if (*quantity != prev)
    {
        PlaySE(SE_SELECT);
        return TRUE;
    }

    return FALSE;
}

static void RecipeBook_ShowAutoCraftPrompt(const u8 *text)
{
    u8 windowId = sRecipeBookAutoCraftPromptWindowId;

    if (windowId == WINDOW_NONE)
    {
        windowId = AddWindow(&sRecipeBookAutoCraftPromptWindowTemplate);
        sRecipeBookAutoCraftPromptWindowId = windowId;
    }

    DrawStdFrameWithCustomTileAndPalette(windowId, TRUE, RECIPEBOOK_WINDOW_FRAME_BASE_TILE,
                                         RECIPEBOOK_WINDOW_FRAME_PALETTE);
    FillWindowPixelBuffer(windowId, PIXEL_FILL(1));
    AddTextPrinterParameterized(windowId, FONT_NORMAL, text, 0, 1, 0, NULL);
    PutWindowTilemap(windowId);
    CopyWindowToVram(windowId, COPYWIN_FULL);
}

static void RecipeBook_ShowAutoCraftHowManyPrompt(void)
{
    CopyItemName(sRecipeBookAutoCraftItemId, gStringVar1);
    StringExpandPlaceholders(gStringVar4, sText_RecipeBookAutoCraftHowMany);
    RecipeBook_ShowAutoCraftPrompt(gStringVar4);
}

static void RecipeBook_ShowAutoCraftConfirmPrompt(void)
{
    ConvertIntToDecimalStringN(gStringVar1, sRecipeBookAutoCraftQty, STR_CONV_MODE_LEFT_ALIGN, 3);
    CopyItemNameHandlePlural(sRecipeBookAutoCraftItemId, gStringVar2, sRecipeBookAutoCraftQty);
    StringExpandPlaceholders(gStringVar4, gText_CraftConfirm);
    RecipeBook_ShowAutoCraftPrompt(gStringVar4);
    CreateYesNoMenu(&sRecipeBookAutoCraftYesNoWindowTemplate, RECIPEBOOK_WINDOW_FRAME_BASE_TILE,
                    RECIPEBOOK_WINDOW_FRAME_PALETTE, 0);
    sRecipeBookAutoCraftYesNoActive = TRUE;
}

static void RecipeBook_ShowAutoCraftResultPrompt(void)
{
    CraftMenu_SetCraftSuccessVars(sRecipeBookAutoCraftItemId, sRecipeBookAutoCraftQty);
    StringExpandPlaceholders(gStringVar4, gText_CraftSuccess);
    RecipeBook_ShowAutoCraftPrompt(gStringVar4);
}

static void RecipeBook_UpdateAutoCraftQuantityWindow(void)
{
    u8 windowId = sRecipeBookAutoCraftQtyWindowId;
    u32 windowWidth;

    if (windowId == WINDOW_NONE)
    {
        windowId = AddWindow(&sRecipeBookAutoCraftQuantityWindowTemplate);
        sRecipeBookAutoCraftQtyWindowId = windowId;
    }

    DrawStdFrameWithCustomTileAndPalette(windowId, TRUE, RECIPEBOOK_WINDOW_FRAME_BASE_TILE,
                                         RECIPEBOOK_WINDOW_FRAME_PALETTE);
    FillWindowPixelBuffer(windowId, PIXEL_FILL(1));
    ConvertIntToDecimalStringN(gStringVar1, sRecipeBookAutoCraftQty, STR_CONV_MODE_LEADING_ZEROS, 3);
    StringExpandPlaceholders(gStringVar4, gText_xVar1);
    windowWidth = WindowWidthPx(windowId);
    AddTextPrinterParameterized(windowId, FONT_NORMAL, gStringVar4,
                                GetStringCenterAlignXOffset(FONT_NORMAL, gStringVar4, windowWidth), 2, 0, NULL);
    PutWindowTilemap(windowId);
    CopyWindowToVram(windowId, COPYWIN_FULL);
}

static void RecipeBook_RemoveAutoCraftQuantityWindow(void)
{
    if (sRecipeBookAutoCraftQtyWindowId != WINDOW_NONE)
    {
        ClearStdWindowAndFrameToTransparent(sRecipeBookAutoCraftQtyWindowId, TRUE);
        RemoveWindow(sRecipeBookAutoCraftQtyWindowId);
        sRecipeBookAutoCraftQtyWindowId = WINDOW_NONE;
    }
}

static void RecipeBook_RemoveAutoCraftPromptWindow(void)
{
    if (sRecipeBookAutoCraftPromptWindowId != WINDOW_NONE)
    {
        ClearStdWindowAndFrameToTransparent(sRecipeBookAutoCraftPromptWindowId, TRUE);
        RemoveWindow(sRecipeBookAutoCraftPromptWindowId);
        sRecipeBookAutoCraftPromptWindowId = WINDOW_NONE;
    }
}

static void RecipeBook_RemoveAutoCraftWindows(void)
{
    if (sRecipeBookAutoCraftYesNoActive)
    {
        EraseYesNoWindow();
        sRecipeBookAutoCraftYesNoActive = FALSE;
    }

    RecipeBook_RemoveAutoCraftPromptWindow();
    RecipeBook_RemoveAutoCraftQuantityWindow();
}

static void RecipeBook_ClearAutoCraftState(void)
{
    sRecipeBookAutoCraftState = RECIPEBOOK_AUTO_CRAFT_STATE_NONE;
    sRecipeBookAutoCraftRecipe = NULL;
    sRecipeBookAutoCraftItemId = ITEM_NONE;
    sRecipeBookAutoCraftQty = 0;
    sRecipeBookAutoCraftMaxQty = 0;
    sRecipeBookAutoCraftYesNoActive = FALSE;
}

static void RecipeBook_EndAutoCraftFlow(void)
{
    RecipeBook_RemoveAutoCraftWindows();
    RecipeBook_ClearAutoCraftState();
    RecipeBook_SchedulePatternPreview();
    RecipeBook_UpdateListScrollArrows();
}

static bool8 RecipeBook_TryStartAutoCraft(void)
{
    u16 itemId;
    const struct CraftRecipe *recipe;
    u16 maxQty;
    u16 step;

    if (!CraftLogic_IsAutoCraftEnabled())
        return FALSE;

    itemId = RecipeBook_GetSelectedItemId();
    if (itemId == ITEM_NONE || itemId == LIST_CANCEL)
        return FALSE;

    recipe = RecipeBook_GetPreviewRecipe(itemId);
    if (recipe == NULL)
        return FALSE;

    maxQty = RecipeBook_GetMaxCraftableQuantity(recipe);
    step = recipe->resultQuantity;
    if (step == 0 || maxQty < step)
        return FALSE;

    sRecipeBookAutoCraftItemId = itemId;
    sRecipeBookAutoCraftRecipe = recipe;
    sRecipeBookAutoCraftMaxQty = maxQty;
    sRecipeBookAutoCraftQty = step;
    sRecipeBookAutoCraftYesNoActive = FALSE;
    RecipeBook_ResetPatternPreviewState();
    RecipeBook_DestroyListScrollArrows();
    sRecipeBookAutoCraftState = RECIPEBOOK_AUTO_CRAFT_STATE_QUANTITY;
    RecipeBook_ShowAutoCraftHowManyPrompt();
    RecipeBook_UpdateAutoCraftQuantityWindow();

    return TRUE;
}

static bool8 RecipeBook_CraftFromBag(u16 itemId, const struct CraftRecipe *recipe, u16 outputQty)
{
    u16 ingredientIds[RECIPEBOOK_INGREDIENTS_MAX];
    u8 ingredientCounts[RECIPEBOOK_INGREDIENTS_MAX];
    u8 ingredientCount;
    u16 sets;
    u8 i;

    if (recipe == NULL || recipe->resultQuantity == 0 || outputQty == 0)
        return FALSE;

    ingredientCount = RecipeBook_BuildIngredientList(recipe, ingredientIds, ingredientCounts);
    if (ingredientCount == 0)
        return FALSE;

    sets = outputQty / recipe->resultQuantity;
    if (sets == 0)
        return FALSE;

    for (i = 0; i < ingredientCount; i++)
        RemoveBagItem(ingredientIds[i], ingredientCounts[i] * sets);

    AddBagItem(itemId, outputQty);
    return TRUE;
}

static void RecipeBook_HandleAutoCraftInput(void)
{
    switch (sRecipeBookAutoCraftState)
    {
    case RECIPEBOOK_AUTO_CRAFT_STATE_QUANTITY:
        if (sRecipeBookAutoCraftRecipe != NULL
            && RecipeBook_AdjustAutoCraftQuantity(&sRecipeBookAutoCraftQty, sRecipeBookAutoCraftMaxQty,
                                                  sRecipeBookAutoCraftRecipe->resultQuantity))
            RecipeBook_UpdateAutoCraftQuantityWindow();

        if (JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            RecipeBook_EndAutoCraftFlow();
        }
        else if (JOY_NEW(A_BUTTON))
        {
            PlaySE(SE_SELECT);
            RecipeBook_ShowAutoCraftConfirmPrompt();
            sRecipeBookAutoCraftState = RECIPEBOOK_AUTO_CRAFT_STATE_CONFIRM;
        }
        break;
    case RECIPEBOOK_AUTO_CRAFT_STATE_CONFIRM:
    {
        s8 input = Menu_ProcessInputNoWrapClearOnChoose();

        if (input != MENU_NOTHING_CHOSEN)
            sRecipeBookAutoCraftYesNoActive = FALSE;

        switch (input)
        {
        case 0:
            PlaySE(SE_SELECT);
            RecipeBook_CraftFromBag(sRecipeBookAutoCraftItemId, sRecipeBookAutoCraftRecipe, sRecipeBookAutoCraftQty);
            RecipeBook_ShowAutoCraftResultPrompt();
            RecipeBook_RemoveAutoCraftQuantityWindow();
            sRecipeBookAutoCraftState = RECIPEBOOK_AUTO_CRAFT_STATE_RESULT;
            sRecipeBookIngredientsDirty = TRUE;
            RecipeBook_UpdateIngredientsWindow();
            break;
        case 1:
        case MENU_B_PRESSED:
            PlaySE(SE_SELECT);
            RecipeBook_ShowAutoCraftHowManyPrompt();
            RecipeBook_UpdateAutoCraftQuantityWindow();
            sRecipeBookAutoCraftState = RECIPEBOOK_AUTO_CRAFT_STATE_QUANTITY;
            break;
        }
        break;
    }
    case RECIPEBOOK_AUTO_CRAFT_STATE_RESULT:
        if (JOY_NEW(A_BUTTON | B_BUTTON))
        {
            PlaySE(SE_SELECT);
            RecipeBook_EndAutoCraftFlow();
        }
        break;
    }
}

static void RecipeBook_EnablePanelWindow(void)
{
    u16 left = RECIPEBOOK_PANEL_TILEMAP_LEFT * 8;
    u16 right = (RECIPEBOOK_PANEL_TILEMAP_LEFT + RECIPEBOOK_PANEL_TILEMAP_WIDTH) * 8;
    u16 top = RECIPEBOOK_PANEL_TILEMAP_TOP * 8;
    u16 bottom = (RECIPEBOOK_PANEL_TILEMAP_TOP + RECIPEBOOK_PANEL_TILEMAP_HEIGHT) * 8;

    sRecipeBookSavedWin0H = GetGpuReg(REG_OFFSET_WIN0H);
    sRecipeBookSavedWin0V = GetGpuReg(REG_OFFSET_WIN0V);
    sRecipeBookSavedWinIn = GetGpuReg(REG_OFFSET_WININ);
    sRecipeBookSavedWinOut = GetGpuReg(REG_OFFSET_WINOUT);

    SetGpuReg(REG_OFFSET_WIN0H, (left << 8) | right);
    SetGpuReg(REG_OFFSET_WIN0V, (top << 8) | bottom);
    SetGpuReg(REG_OFFSET_WININ, WININ_WIN0_BG_ALL | WININ_WIN0_OBJ | WININ_WIN0_CLR);
    SetGpuReg(REG_OFFSET_WINOUT, WINOUT_WIN01_BG0 | WINOUT_WIN01_BG1 | WINOUT_WIN01_BG2 | WINOUT_WIN01_OBJ | WINOUT_WIN01_CLR);
    SetGpuReg(REG_OFFSET_DISPCNT, GetGpuReg(REG_OFFSET_DISPCNT) | DISPCNT_WIN0_ON);
}

static void RecipeBook_DisablePanelWindow(void)
{
    SetGpuReg(REG_OFFSET_WIN0H, sRecipeBookSavedWin0H);
    SetGpuReg(REG_OFFSET_WIN0V, sRecipeBookSavedWin0V);
    SetGpuReg(REG_OFFSET_WININ, sRecipeBookSavedWinIn);
    SetGpuReg(REG_OFFSET_WINOUT, sRecipeBookSavedWinOut);
    SetGpuReg(REG_OFFSET_DISPCNT, GetGpuReg(REG_OFFSET_DISPCNT) & ~DISPCNT_WIN0_ON);
}

static void RecipeBook_CopyPanelTilemapToBg3(const u32 *tilemap)
{
    u16 *dst = (u16 *)sRecipeBookBg3TilemapBuffer;
    const u16 *src = (const u16 *)tilemap;
    u8 y;

    memset(sRecipeBookBg3TilemapBuffer, 0, BG_SCREEN_SIZE);

    for (y = 0; y < RECIPEBOOK_PANEL_TILEMAP_HEIGHT; y++)
    {
        const u16 *srcRow = src + (RECIPEBOOK_PANEL_TILEMAP_TOP + y) * 32 + RECIPEBOOK_PANEL_TILEMAP_LEFT;
        u16 *dstRow = dst + (RECIPEBOOK_PANEL_TILEMAP_TOP + y) * 32 + RECIPEBOOK_PANEL_TILEMAP_LEFT;

        CpuCopy16(srcRow, dstRow, RECIPEBOOK_PANEL_TILEMAP_WIDTH * sizeof(u16));
    }

    CopyBgTilemapBufferToVram(3);
}

static void RecipeBook_ClearPanelOverlay(void)
{
    memset(sRecipeBookBg3TilemapBuffer, 0, BG_SCREEN_SIZE);
    CopyBgTilemapBufferToVram(3);
    ChangeBgY(3, 0, BG_COORD_SET);
    HideBg(3);
}

static void RecipeBook_BlankPatternArea(void)
{
    FillBgTilemapBufferRect(2, RECIPEBOOK_PATTERN_CLEAR_TILE,
                            RECIPEBOOK_PATTERN_CLEAR_LEFT,
                            RECIPEBOOK_PATTERN_CLEAR_TOP,
                            RECIPEBOOK_PATTERN_CLEAR_WIDTH,
                            RECIPEBOOK_PATTERN_CLEAR_HEIGHT,
                            0);
    CopyBgTilemapBufferToVram(2);
}

static void RecipeBook_RestorePatternArea(void)
{
    u16 *dst = (u16 *)sRecipeBookBg2TilemapBuffer;
    const u16 *src = (const u16 *)gRecipeBookMenu_Tilemap;
    u8 y;

    for (y = 0; y < RECIPEBOOK_PATTERN_CLEAR_HEIGHT; y++)
    {
        const u16 *srcRow = src + (RECIPEBOOK_PATTERN_CLEAR_TOP + y) * 32 + RECIPEBOOK_PATTERN_CLEAR_LEFT;
        u16 *dstRow = dst + (RECIPEBOOK_PATTERN_CLEAR_TOP + y) * 32 + RECIPEBOOK_PATTERN_CLEAR_LEFT;

        CpuCopy16(srcRow, dstRow, RECIPEBOOK_PATTERN_CLEAR_WIDTH * sizeof(u16));
    }

    CopyBgTilemapBufferToVram(2);
}

static void RecipeBook_ResetIngredientsWindow(const struct WindowTemplate *template)
{
    u8 windowId = sRecipeBookWindowIds[RECIPEBOOK_WIN_INGREDIENTS];

    if (windowId != WINDOW_NONE)
    {
        ClearWindowTilemap(windowId);
        RemoveWindow(windowId);
    }

    sRecipeBookWindowIds[RECIPEBOOK_WIN_INGREDIENTS] = AddWindow(template);
    FillWindowPixelBuffer(sRecipeBookWindowIds[RECIPEBOOK_WIN_INGREDIENTS], PIXEL_FILL(0));
    PutWindowTilemap(sRecipeBookWindowIds[RECIPEBOOK_WIN_INGREDIENTS]);
    sRecipeBookIngredientsDirty = TRUE;
    sRecipeBookIngredientsTilemapDirty = TRUE;
}

static void RecipeBook_ShowPatternHideWindow(void)
{
    if (sRecipeBookPatternHideWindowId != WINDOW_NONE)
        return;

    sRecipeBookPatternHideWindowId = AddWindow(&sRecipeBookPatternHideWindowTemplate);
    FillWindowPixelBuffer(sRecipeBookPatternHideWindowId, PIXEL_FILL(0));
    PutWindowTilemap(sRecipeBookPatternHideWindowId);
    CopyWindowToVram(sRecipeBookPatternHideWindowId, COPYWIN_FULL);
}

static void RecipeBook_HidePatternHideWindow(void)
{
    if (sRecipeBookPatternHideWindowId == WINDOW_NONE)
        return;

    ClearWindowTilemap(sRecipeBookPatternHideWindowId);
    CopyWindowToVram(sRecipeBookPatternHideWindowId, COPYWIN_MAP);
    RemoveWindow(sRecipeBookPatternHideWindowId);
    sRecipeBookPatternHideWindowId = WINDOW_NONE;
}

static void RecipeBook_FinishExtendAnim(u8 animState)
{
    RecipeBook_StopExtendAnim();

    if (animState == RECIPEBOOK_EXTEND_ANIM_EXPAND)
    {
        sRecipeBookIngredientsDirty = TRUE;
        RecipeBook_UpdateIngredientsWindow();
    }
    else if (animState == RECIPEBOOK_EXTEND_ANIM_COLLAPSE)
    {
        RecipeBook_ClearPanelOverlay();
        RecipeBook_RestorePatternArea();
        RecipeBook_HidePatternHideWindow();
        sRecipeBookIngredientsView = RECIPEBOOK_VIEW_NORMAL;
        RecipeBook_ResetIngredientsWindow(&sRecipeBookWindowTemplates[RECIPEBOOK_WIN_INGREDIENTS]);
        RecipeBook_ResetPatternPreviewState();
        RecipeBook_UpdatePatternPreview();
        RecipeBook_UpdateIngredientsWindow();
    }
}

static void RecipeBook_StartExtendAnim(u8 animState)
{
    struct ComfyAnimEasingConfig config;

    if (sRecipeBookExtendAnimId != INVALID_COMFY_ANIM)
        ReleaseComfyAnim(sRecipeBookExtendAnimId);

    InitComfyAnimConfig_Easing(&config);
    config.durationFrames = RECIPEBOOK_EXTEND_ANIM_DURATION_FRAMES;
    config.delayFrames = 0;
    if (animState == RECIPEBOOK_EXTEND_ANIM_COLLAPSE)
    {
        config.from = Q_24_8(0);
        config.to = Q_24_8(RECIPEBOOK_EXTEND_ANIM_OFFSET_PIXELS);
        config.easingFunc = ComfyAnimEasing_EaseInCubic;
    }
    else
    {
        config.from = Q_24_8(RECIPEBOOK_EXTEND_ANIM_OFFSET_PIXELS);
        config.to = Q_24_8(0);
        config.easingFunc = ComfyAnimEasing_EaseOutCubic;
        RecipeBook_CopyPanelTilemapToBg3(gRecipeBookMenuExtended_Tilemap);
        ShowBg(3);
    }
    sRecipeBookExtendAnimId = CreateComfyAnim_Easing(&config);
    sRecipeBookExtendAnimState = (sRecipeBookExtendAnimId != INVALID_COMFY_ANIM) ? animState : RECIPEBOOK_EXTEND_ANIM_NONE;
    ChangeBgY(3, config.from, BG_COORD_SET);

    if (sRecipeBookExtendAnimState == RECIPEBOOK_EXTEND_ANIM_NONE)
        RecipeBook_FinishExtendAnim(animState);
}

static void RecipeBook_StopExtendAnim(void)
{
    if (sRecipeBookExtendAnimId != INVALID_COMFY_ANIM)
        ReleaseComfyAnim(sRecipeBookExtendAnimId);

    sRecipeBookExtendAnimId = INVALID_COMFY_ANIM;
    sRecipeBookExtendAnimState = RECIPEBOOK_EXTEND_ANIM_NONE;
    ChangeBgY(3, 0, BG_COORD_SET);
}

static void RecipeBook_TickExtendAnim(void)
{
    struct ComfyAnim *anim;
    u8 animState;

    if (sRecipeBookExtendAnimState == RECIPEBOOK_EXTEND_ANIM_NONE)
        return;

    if (sRecipeBookExtendAnimId >= NUM_COMFY_ANIMS || !gComfyAnims[sRecipeBookExtendAnimId].inUse)
    {
        animState = sRecipeBookExtendAnimState;
        RecipeBook_FinishExtendAnim(animState);
        return;
    }

    anim = &gComfyAnims[sRecipeBookExtendAnimId];
    TryAdvanceComfyAnim(anim);
    ChangeBgY(3, anim->position, BG_COORD_SET);

    if (anim->completed)
    {
        animState = sRecipeBookExtendAnimState;
        RecipeBook_FinishExtendAnim(animState);
    }
}

static void RecipeBook_EnterExtendedIngredientsView(void)
{
    if (sRecipeBookIngredientsView == RECIPEBOOK_VIEW_EXTENDED)
        return;

    sRecipeBookIngredientsView = RECIPEBOOK_VIEW_EXTENDED;
    RecipeBook_StopExtendAnim();
    RecipeBook_ShowPatternHideWindow();
    RecipeBook_ResetPatternPreviewState();
    sRecipeBookEnterExtendedPending = TRUE;
    sRecipeBookEnterExtendedVBlank = gMain.vblankCounter1;
}

static void RecipeBook_ExitExtendedIngredientsView(void)
{
    if (sRecipeBookIngredientsView == RECIPEBOOK_VIEW_NORMAL)
        return;
    if (sRecipeBookExtendAnimState == RECIPEBOOK_EXTEND_ANIM_COLLAPSE)
        return;

    RecipeBook_StopExtendAnim();
    sRecipeBookEnterExtendedPending = FALSE;
    RecipeBook_UpdateIngredientsHeaderOnly();
    RecipeBook_BlankPatternArea();
    RecipeBook_StartExtendAnim(RECIPEBOOK_EXTEND_ANIM_COLLAPSE);
}

static void RecipeBook_ExitExtendedIngredientsViewIfNeeded(void)
{
    if (sRecipeBookIngredientsView == RECIPEBOOK_VIEW_EXTENDED)
        RecipeBook_ExitExtendedIngredientsView();
}

static void RecipeBook_DestroyPatternIcons(void)
{
    RecipeBook_ResetPatternPreviewState();
}

static u8 RecipeBook_GetOddPocketIndicatorX(u8 pocketIndex)
{
    return RECIPEBOOK_POCKET_INDICATOR_CENTER_X - (sRecipeBookPocketCount / 2) + pocketIndex;
}

static void RecipeBook_ClearPocketIndicatorRow(void)
{
    u8 maxWidth = 18;
    s8 clearLeft = RECIPEBOOK_POCKET_INDICATOR_CENTER_X - (maxWidth / 2);
    u8 clearWidth = maxWidth;

    if (clearLeft < 0)
    {
        clearWidth = maxWidth + clearLeft;
        clearLeft = 0;
    }
    if (clearLeft + clearWidth > DISPLAY_TILE_WIDTH)
        clearWidth = DISPLAY_TILE_WIDTH - clearLeft;

    FillBgTilemapBufferRect(2, 0x01, clearLeft, RECIPEBOOK_POCKET_INDICATOR_Y, clearWidth, 1, 0);
}

static void RecipeBook_DrawPocketIndicatorSquares(void)
{
    u8 i;
    const u8 y = RECIPEBOOK_POCKET_INDICATOR_Y;

    RecipeBook_ClearPocketIndicatorRow();

    if (sRecipeBookPocketCount == 0)
    {
        CopyBgTilemapBufferToVram(2);
        return;
    }

    if (sRecipeBookPocketCount > 17)
    {
        CopyBgTilemapBufferToVram(2);
        return;
    }

    if ((sRecipeBookPocketCount & 1) != 0)
    {
        for (i = 0; i < sRecipeBookPocketCount; i++)
        {
            u16 tile = (i == sRecipeBookPocketIndex)
                ? RECIPEBOOK_INDICATOR_TILE_ACTIVE
                : RECIPEBOOK_INDICATOR_TILE_INACTIVE;
            u8 x = RecipeBook_GetOddPocketIndicatorX(i);

            FillBgTilemapBufferRect(2, tile, x, y, 1, 1, 0);
        }
    }
    else
    {
        u8 runLen = sRecipeBookPocketCount + 1;
        u8 startX = RECIPEBOOK_POCKET_INDICATOR_CENTER_X - (sRecipeBookPocketCount / 2);

        for (i = 0; i < runLen; i++)
        {
            u16 tile;

            if (i == 0)
            {
                tile = (sRecipeBookPocketIndex == 0)
                    ? RECIPEBOOK_INDICATOR_TILE_RIGHT_END_ACTIVE
                    : BG_TILE_H_FLIP(RECIPEBOOK_INDICATOR_TILE_LEFT_END_INACTIVE);
            }
            else if (i == runLen - 1)
            {
                tile = (sRecipeBookPocketIndex == sRecipeBookPocketCount - 1)
                    ? BG_TILE_H_FLIP(RECIPEBOOK_INDICATOR_TILE_RIGHT_END_ACTIVE)
                    : RECIPEBOOK_INDICATOR_TILE_LEFT_END_INACTIVE;
            }
            else
            {
                u8 leftPocket = i - 1;
                u8 rightPocket = i;

                if (sRecipeBookPocketIndex == leftPocket)
                    tile = RECIPEBOOK_INDICATOR_TILE_LINK_LEFT_ACTIVE;
                else if (sRecipeBookPocketIndex == rightPocket)
                    tile = BG_TILE_H_FLIP(RECIPEBOOK_INDICATOR_TILE_LINK_LEFT_ACTIVE);
                else
                    tile = RECIPEBOOK_INDICATOR_TILE_LINK_INACTIVE;
            }

            FillBgTilemapBufferRect(2, tile, startX + i, y, 1, 1, 0);
        }
    }

    CopyBgTilemapBufferToVram(2);
}

static void RecipeBook_CreatePocketSwitchArrows(void)
{
    if (sRecipeBookPocketArrowTask == TASK_NONE && sRecipeBookPocketCount > 1)
        sRecipeBookPocketArrowTask = AddScrollIndicatorArrowPair(&sRecipeBookPocketArrowTemplate, &sRecipeBookPocketArrowPos);
}

static void RecipeBook_DestroyPocketSwitchArrows(void)
{
    if (sRecipeBookPocketArrowTask != TASK_NONE)
    {
        RemoveScrollIndicatorArrowPair(sRecipeBookPocketArrowTask);
        sRecipeBookPocketArrowTask = TASK_NONE;
    }
}

static void RecipeBook_DestroyListScrollArrows(void)
{
    if (sRecipeBookListArrowTask != TASK_NONE)
    {
        RemoveScrollIndicatorArrowPair(sRecipeBookListArrowTask);
        sRecipeBookListArrowTask = TASK_NONE;
    }
}

static void RecipeBook_UpdateListScrollArrows(void)
{
    s32 maxScroll = sRecipeBookItemCount - sRecipeBookListMenuTemplate.maxShowed;
    struct ScrollArrowsTemplate template;

    if (maxScroll <= 0)
    {
        RecipeBook_DestroyListScrollArrows();
        return;
    }

    if (sRecipeBookListArrowTask != TASK_NONE)
        RecipeBook_DestroyListScrollArrows();

    template = sRecipeBookListScrollArrowTemplate;
    template.fullyUpThreshold = 0;
    template.fullyDownThreshold = maxScroll;
    sRecipeBookListArrowTask = AddScrollIndicatorArrowPair(&template, &sRecipeBookListScroll);
}

static void RecipeBook_UpdateVariantButtons(void)
{
    u8 windowId = sRecipeBookWindowIds[RECIPEBOOK_WIN_VARIANT_BUTTONS];
    u8 fontId = FONT_SMALL_NARROW;
    u32 windowWidth;
    u32 lWidth;
    u32 rWidth;
    u32 totalWidth;
    s16 baseX;
    s16 lX;
    s16 rX;
    u8 y;
    bool8 showButtons;

    if (windowId == WINDOW_NONE)
        return;

    windowWidth = WindowWidthPx(windowId);
    lWidth = GetStringWidth(fontId, sText_RecipeBookLButton, 0);
    rWidth = GetStringWidth(fontId, sText_RecipeBookRButton, 0);
    totalWidth = lWidth + RECIPEBOOK_VARIANT_BUTTONS_GAP + rWidth;
    baseX = (windowWidth > totalWidth) ? (s16)((windowWidth - totalWidth) / 2) : 0;
    lX = baseX + RECIPEBOOK_VARIANT_L_BUTTON_X_OFFSET;
    rX = baseX + lWidth + RECIPEBOOK_VARIANT_BUTTONS_GAP + RECIPEBOOK_VARIANT_R_BUTTON_X_OFFSET;
    y = RECIPEBOOK_VARIANT_BUTTONS_Y_OFFSET;

    if (lX < 0)
        lX = 0;
    if (rX < 0)
        rX = 0;

    FillWindowPixelBuffer(windowId, PIXEL_FILL(sRecipeBookListMenuTemplate.fillValue));

    showButtons = RecipeBook_GetUnlockedVariantCount(RecipeBook_GetSelectedItemId()) > 1;
    if (showButtons)
    {
        AddTextPrinterParameterized4(windowId, fontId, lX, y, 0, 0,
                                     sRecipeBookFontColorTable[RECIPEBOOK_COLOR_INGREDIENT_HEADER], TEXT_SKIP_DRAW,
                                     sText_RecipeBookLButton);
        AddTextPrinterParameterized4(windowId, fontId, rX, y, 0, 0,
                                     sRecipeBookFontColorTable[RECIPEBOOK_COLOR_INGREDIENT_HEADER], TEXT_SKIP_DRAW,
                                     sText_RecipeBookRButton);
    }

    CopyWindowToVram(windowId, COPYWIN_GFX);
}

static void RecipeBook_ApplyVariantChange(u16 itemId, u8 variantIndex)
{
    sRecipeBookVariantIndex[itemId] = variantIndex;
    RecipeBook_SchedulePatternPreview();
    RecipeBook_UpdateIngredientsWindow();
    RecipeBook_RedrawCurrentListRow();
}

static void RecipeBook_DisableTimeOfDayBlend(void)
{
    if (sRecipeBookTimeBlendOverridden)
        return;

    gTimeBlend.startBlend = gTimeOfDayBlend[TIME_DAY];
    gTimeBlend.endBlend = gTimeOfDayBlend[TIME_DAY];
    gTimeBlend.weight = 256;
    gTimeBlend.altWeight = 256;
    sRecipeBookTimeBlendOverridden = TRUE;
}

static void RecipeBook_RestoreTimeOfDayBlend(void)
{
    if (!sRecipeBookTimeBlendOverridden)
        return;

    sRecipeBookTimeBlendOverridden = FALSE;
    // Gained a bool32 updateBlend between their 1.14.2 base and our
    // 1.16.4. TRUE here: this function exists to RESTORE the blend the
    // recipe book overrode, so recomputing the time without recomputing
    // the blend would leave the override on screen.
    UpdateTimeOfDay(TRUE);
    ApplyWeatherColorMapIfIdle(gWeatherPtr->colorMapIndex);
}

static void RecipeBook_Init(void)
{
    u8 i;

    for (i = 0; i < RECIPEBOOK_WIN_COUNT; i++)
        sRecipeBookWindowIds[i] = WINDOW_NONE;
    for (i = 0; i < CRAFT_SLOT_COUNT; i++)
        sRecipeBookPatternSpriteIds[i] = SPRITE_NONE;
    sRecipeBookPocketIndex = 0;
    sRecipeBookListScroll = 0;
    sRecipeBookListCursor = 0;
    sRecipeBookListTaskId = TASK_NONE;
    sRecipeBookPocketArrowTask = TASK_NONE;
    sRecipeBookPocketArrowPos = 0;
    sRecipeBookListArrowTask = TASK_NONE;
    sRecipeBookPreviewItemId = ITEM_NONE;
    sRecipeBookPreviewVariantIndex = 0;
    sRecipeBookPendingItemId = ITEM_NONE;
    sRecipeBookPendingVariantIndex = 0;
    sRecipeBookPatternDelay = 0;
    sRecipeBookIngredientsItemId = ITEM_NONE;
    sRecipeBookIngredientsRecipe = NULL;
    sRecipeBookIngredientsView = RECIPEBOOK_VIEW_NORMAL;
    sRecipeBookIngredientsViewCached = sRecipeBookIngredientsView;
    sRecipeBookIngredientsDirty = TRUE;
    sRecipeBookIngredientsTilemapDirty = FALSE;
    sRecipeBookExtendAnimId = INVALID_COMFY_ANIM;
    sRecipeBookExtendAnimState = RECIPEBOOK_EXTEND_ANIM_NONE;
    sRecipeBookPatternHideWindowId = WINDOW_NONE;
    sRecipeBookEnterExtendedPending = FALSE;
    sRecipeBookEnterExtendedVBlank = 0;
    memset(sRecipeBookVariantIndex, 0, sizeof(sRecipeBookVariantIndex));
    sRecipeBookTimeBlendOverridden = FALSE;
    sRecipeBookAutoCraftPromptWindowId = WINDOW_NONE;
    sRecipeBookAutoCraftQtyWindowId = WINDOW_NONE;
    sRecipeBookAutoCraftState = RECIPEBOOK_AUTO_CRAFT_STATE_NONE;
    sRecipeBookAutoCraftYesNoActive = FALSE;
    sRecipeBookAutoCraftItemId = ITEM_NONE;
    sRecipeBookAutoCraftRecipe = NULL;
    sRecipeBookAutoCraftQty = 0;
    sRecipeBookAutoCraftMaxQty = 0;
    RecipeBook_DisableTimeOfDayBlend();
    RecipeBook_EnablePanelWindow();

    RecipeBook_BuildPocketList();
    RecipeBook_InitWindows();
    if (sRecipeBookPocketCount == 0)
    {
        RecipeBook_PrintPocketNames(sText_RecipeBookNoRecipes, NULL);
    }
    else
    {
        RecipeBook_PrintPocketNames(gPocketNamesStringsTable[sRecipeBookPockets[sRecipeBookPocketIndex]], NULL);
    }
    RecipeBook_CopyPocketNameToWindow(0);
    RecipeBook_RefreshListMenu();
    RecipeBook_UpdatePatternPreview();
    RecipeBook_UpdateIngredientsWindow();
    RecipeBook_UpdateListScrollArrows();
    RecipeBook_DrawPocketIndicatorSquares();
    RecipeBook_CreatePocketSwitchArrows();
}

static void RecipeBook_Cleanup(void)
{
    TRY_FREE_AND_SET_NULL(sRecipeBookBg1TilemapBuffer);
    TRY_FREE_AND_SET_NULL(sRecipeBookBg2TilemapBuffer);
    TRY_FREE_AND_SET_NULL(sRecipeBookBg3TilemapBuffer);
    RecipeBook_RemoveAutoCraftWindows();
    RecipeBook_ClearAutoCraftState();
    RecipeBook_StopExtendAnim();
    RecipeBook_ClearPanelOverlay();
    RecipeBook_HidePatternHideWindow();
    RecipeBook_DestroyWindows();
    RecipeBook_DestroyPocketSwitchArrows();
    RecipeBook_DestroyListScrollArrows();
    RecipeBook_DestroyPatternIcons();
    RecipeBook_RestoreTimeOfDayBlend();
    RecipeBook_DisablePanelWindow();
    UnsetBgTilemapBuffer(1);
    UnsetBgTilemapBuffer(2);
    UnsetBgTilemapBuffer(3);
}


static void Task_RecipeBookMenu(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
#define tPocketSwitchState data[1]
#define tPocketSwitchTimer data[2]
#define tPocketSwitchDir data[3]
#define tPocketSwitchTarget data[4]

    switch (gTasks[taskId].data[0])
    {
    case 0:
        ShowBg(0);
        ShowBg(1);
        ShowBg(2);
        BeginNormalPaletteFade(0xFFFFFFFF, 0, 16, 0, RGB_BLACK);
        gTasks[taskId].data[0]++;
        break;
    case 1:
        if (!gPaletteFade.active)
            gTasks[taskId].data[0]++;
        break;
    case 2:
        RecipeBook_TickExtendAnim();
        if (sRecipeBookExtendAnimState != RECIPEBOOK_EXTEND_ANIM_NONE)
            break;
        if (sRecipeBookAutoCraftState != RECIPEBOOK_AUTO_CRAFT_STATE_NONE)
        {
            RecipeBook_HandleAutoCraftInput();
            break;
        }
        if (JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            if (sRecipeBookIngredientsView == RECIPEBOOK_VIEW_EXTENDED)
            {
                RecipeBook_ExitExtendedIngredientsView();
            }
            else
            {
                RecipeBook_SavePocketListState();
                BeginNormalPaletteFade(0xFFFFFFFF, 0, 0, 16, RGB_BLACK);
                gTasks[taskId].data[0]++;
            }
        }
        else
        {
            if (sRecipeBookEnterExtendedPending)
            {
                if (gMain.vblankCounter1 != sRecipeBookEnterExtendedVBlank)
                {
                    sRecipeBookEnterExtendedPending = FALSE;
                    RecipeBook_ResetIngredientsWindow(&sRecipeBookIngredientsWindowTemplateExtended);
                    RecipeBook_UpdateIngredientsHeaderOnly();
                    RecipeBook_BlankPatternArea();
                    RecipeBook_StartExtendAnim(RECIPEBOOK_EXTEND_ANIM_EXPAND);
                }
                break;
            }

            if (tPocketSwitchState != 0)
            {
                tPocketSwitchTimer++;
                if (!(tPocketSwitchTimer & 1))
                {
                    if (tPocketSwitchDir == MENU_CURSOR_DELTA_RIGHT)
                        RecipeBook_CopyPocketNameToWindow(tPocketSwitchTimer >> 1);
                    else
                        RecipeBook_CopyPocketNameToWindow(8 - (tPocketSwitchTimer >> 1));
                }
                if (tPocketSwitchTimer >= 16)
                {
                    sRecipeBookPocketIndex = tPocketSwitchTarget;
                    RecipeBook_RefreshListMenu();
                    RecipeBook_DrawPocketIndicatorSquares();
                    if (sRecipeBookIngredientsView == RECIPEBOOK_VIEW_EXTENDED)
                        RecipeBook_ExitExtendedIngredientsView();
                    else
                    {
                        RecipeBook_SchedulePatternPreview();
                        RecipeBook_UpdateIngredientsWindow();
                    }
                    tPocketSwitchState = 0;
                }
            }
            else
            {
                const struct CraftRecipe *recipe;
                u8 ingredientCount;
                u8 variantCount;
                u16 currentItemId = RecipeBook_GetSelectedItemId();

                recipe = RecipeBook_GetPreviewRecipe(currentItemId);
                ingredientCount = RecipeBook_GetUniqueIngredientCount(recipe);
                variantCount = RecipeBook_GetUnlockedVariantCount(currentItemId);

                if (JOY_NEW(SELECT_BUTTON))
                {
                    if (sRecipeBookIngredientsView == RECIPEBOOK_VIEW_EXTENDED)
                    {
                        PlaySE(SE_SELECT);
                        RecipeBook_ExitExtendedIngredientsView();
                    }
                    else if (ingredientCount > RECIPEBOOK_INGREDIENTS_INLINE_MAX)
                    {
                        PlaySE(SE_SELECT);
                        RecipeBook_EnterExtendedIngredientsView();
                    }
                    else
                    {
                        PlaySE(SE_FAILURE);
                    }
                }
                else
                {
                    if (sRecipeBookPocketCount > 1)
                    {
                        s8 direction = 0;

                        if (JOY_NEW(DPAD_LEFT))
                            direction = MENU_CURSOR_DELTA_LEFT;
                        else if (JOY_NEW(DPAD_RIGHT))
                            direction = MENU_CURSOR_DELTA_RIGHT;

                        if (direction != 0)
                        {
                            u8 newIndex = sRecipeBookPocketIndex;

                            PlaySE(SE_SELECT);
                            RecipeBook_ExitExtendedIngredientsViewIfNeeded();
                            RecipeBook_SavePocketListState();
                            if (direction == MENU_CURSOR_DELTA_RIGHT)
                                newIndex = (newIndex + 1) % sRecipeBookPocketCount;
                            else
                                newIndex = (newIndex == 0) ? (sRecipeBookPocketCount - 1) : (newIndex - 1);

                            tPocketSwitchState = 1;
                            tPocketSwitchTimer = 0;
                            tPocketSwitchDir = direction;
                            tPocketSwitchTarget = newIndex;

                            if (direction == MENU_CURSOR_DELTA_RIGHT)
                                RecipeBook_PrintPocketNames(gPocketNamesStringsTable[sRecipeBookPockets[sRecipeBookPocketIndex]],
                                                            gPocketNamesStringsTable[sRecipeBookPockets[newIndex]]);
                            else
                                RecipeBook_PrintPocketNames(gPocketNamesStringsTable[sRecipeBookPockets[newIndex]],
                                                            gPocketNamesStringsTable[sRecipeBookPockets[sRecipeBookPocketIndex]]);

                            RecipeBook_CopyPocketNameToWindow(direction == MENU_CURSOR_DELTA_RIGHT ? 0 : 8);
                        }
                    }
                    if (variantCount > 1)
                    {
                        u8 variantIndex = RecipeBook_GetVariantIndex(currentItemId);

                        if (JOY_NEW(L_BUTTON))
                        {
                            PlaySE(SE_SELECT);
                            RecipeBook_ExitExtendedIngredientsViewIfNeeded();
                            RecipeBook_ApplyVariantChange(currentItemId,
                                                          (variantIndex + variantCount - 1) % variantCount);
                        }
                        else if (JOY_NEW(R_BUTTON))
                        {
                            PlaySE(SE_SELECT);
                            RecipeBook_ExitExtendedIngredientsViewIfNeeded();
                            RecipeBook_ApplyVariantChange(currentItemId,
                                                          (variantIndex + 1) % variantCount);
                        }
                    }
                }
            }

            if (tPocketSwitchState == 0 && sRecipeBookListTaskId != TASK_NONE)
            {
                if (JOY_NEW(A_BUTTON) && RecipeBook_TryStartAutoCraft())
                {
                    PlaySE(SE_SELECT);
                    break;
                }

                u16 prevItemId = RecipeBook_GetSelectedItemId();
                s32 listPosition = ListMenu_ProcessInput(sRecipeBookListTaskId);
                ListMenuGetScrollAndRow(sRecipeBookListTaskId, &sRecipeBookListScroll, &sRecipeBookListCursor);
                RecipeBook_SavePocketListState();
                    if (prevItemId != RecipeBook_GetSelectedItemId())
                    {
                        u16 newItemId = RecipeBook_GetSelectedItemId();

                        RecipeBook_ExitExtendedIngredientsViewIfNeeded();
                        RecipeBook_GetVariantIndex(newItemId);
                        RecipeBook_UpdateVariantButtons();
                    }

                RecipeBook_SchedulePatternPreview();
                RecipeBook_UpdateIngredientsWindow();

                if (listPosition == LIST_CANCEL)
                {
                    PlaySE(SE_SELECT);
                    BeginNormalPaletteFade(0xFFFFFFFF, 0, 0, 16, RGB_BLACK);
                    gTasks[taskId].data[0]++;
                }
            }

            if (tPocketSwitchState == 0)
                RecipeBook_TickPatternPreview();
        }
        break;
    case 3:
        if (!gPaletteFade.active)
        {
            RecipeBook_Cleanup();
            DestroyTask(taskId);
            SetMainCallback2(CB2_ReturnToCraftMenu);
        }
        break;
    }

#undef tPocketSwitchState
#undef tPocketSwitchTimer
#undef tPocketSwitchDir
#undef tPocketSwitchTarget
}
