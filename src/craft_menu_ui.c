#include "global.h"
#include "bg.h"
#include "gpu_regs.h"
#include "palette.h"
#include "graphics.h"
#include "window.h"
#include "text_window.h"
#include "text.h"
#include "menu.h"
#include "main.h"
#include "sprite.h"
#include "decompress.h"
#include "sound.h"
#include "strings.h"
#include "string_util.h"
#include "line_break.h"
#include "international_string_util.h"
#include "item_icon.h"
#include "item_menu.h"
#include "item.h"
#include "craft_menu.h"
#include "constants/songs.h"
#include "constants/items.h"
#include "craft_logic.h"
#include "data/crafting_recipes.h"
#include "craft_menu_ui.h"

#define TAG_WB_TOPLEFT     0x4001
#define TAG_WB_TOPMID      0x4002
#define TAG_WB_TOPRIGHT    0x4003
#define TAG_WB_MIDLEFT     0x4004
#define TAG_WB_MIDMID      0x4005
#define TAG_WB_MIDRIGHT    0x4006
#define TAG_WB_BOTLEFT     0x4007
#define TAG_WB_BOTMID      0x4008
#define TAG_WB_BOTRIGHT    0x4009
#define TAG_WB_PALETTE     0x4010
#define TAG_CRAFT_ICON_BASE 0x5200
#define TAG_CRAFT_ICON_ALT_BASE (TAG_CRAFT_ICON_BASE + 0x10)

#define WB_CENTER_X 120
#define WB_CENTER_Y 60

enum
{
    WINDOW_CRAFT_GRID,
    WINDOW_CRAFT_INFO,
    WINDOW_CRAFT_MESSAGE,
    WINDOW_CRAFT_YESNO,
    WINDOW_CRAFT_ACTIONS,
    WINDOW_CRAFT_ITEMINFO,
    WINDOW_CRAFT_QUANTITY,
    NUM_CRAFT_WINDOWS
};

EWRAM_DATA static u8 sCraftGridWindowId = 0;
EWRAM_DATA static u8 sCraftItemInfoWindowId = 0;
EWRAM_DATA static u8 sCraftInfoWindowId = 0;
EWRAM_DATA static u8 sCraftMessageWindowId = 0;
EWRAM_DATA static u8 sWorkbenchSpriteIds[CRAFT_SLOT_COUNT];
EWRAM_DATA static u8 sCraftCursorPos = 0;
EWRAM_DATA static u8 sCraftSlotSpriteIds[CRAFT_SLOT_COUNT];
EWRAM_DATA static u16 sCraftSlotItemIds[CRAFT_SLOT_COUNT];
EWRAM_DATA static bool8 sCraftSlotTagFlip[CRAFT_SLOT_COUNT];
EWRAM_DATA static u8 sActionMenuWindowId;
EWRAM_DATA static u8 sQuantityWindowId;
EWRAM_DATA static bool8 sInSwapMode = FALSE;

// New helpers for showing/hiding info and item info windows
static void HideInfoWindow(void);
static void HideItemInfoWindow(void);
static void ShowItemInfoWindow(void);
static void UpdateItemInfoWindow(void);

static const u8 sText_CraftingUi_AButton[] = _("{A_BUTTON}");
static const u8 sText_CraftingUi_AddItem[] = _("Add\nItem");
static const u8 sText_CraftingUi_BButtonExit[] = _("{B_BUTTON} Exit");
static const u8 sText_CraftingUi_StartButtonCraft[] = _("{START_BUTTON} Craft");
static const u8 sText_CraftingUi_SelectButton[] = _("{SELECT_BUTTON}");
static const u8 sText_CraftingUi_RecipeBook[] = _("Recipe\nBook");

const u8 gText_CraftSwapItem[] = _("Swap Item");
const u8 gText_CraftAdjustQty[] = _("Adjust Qty");
const u8 gText_CraftSwapSlot[] = _("Swap Slot");
const u8 gText_CraftPackItem[] = _("Pack Item");

const u8 sText_CraftPlaceHowManyVar1[] = _("Place how many\n{STR_VAR_1}?");

const u8 gText_PackUpQuestion[] = _("Would you like to pack up?");

const u8 gText_CraftNoItems[] = _("Place items on the workbench to\nbegin crafting.");
const u8 gText_CraftInvalid[] = _("This won't make anything helpful...");
const u8 gText_CraftConfirm[] = _("Would you like to craft\n{STR_VAR_1} {STR_VAR_2}?");

static const u8 sGridTextColor[] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_LIGHT_GRAY, TEXT_COLOR_DARK_GRAY};
static const u8 sInputTextColor[] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_LIGHT_GRAY};
static const u8 sHintTextColor[] = {TEXT_COLOR_WHITE, TEXT_COLOR_GREEN, TEXT_COLOR_LIGHT_GREEN};
static const u8 sSwapTextColor[] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_RED, TEXT_COLOR_LIGHT_RED};

static const s8 sCraftCursorXOffset = -13;

struct GridSlotPos
{
    u8 x;
    u8 y;
};

static const u8 sWorkbenchColPositions[CRAFT_COLS] = {19, 44, 69};
static const u8 sWorkbenchRowPositions[CRAFT_ROWS] = {4, 28, 52};

static const struct GridSlotPos sWorkbenchSlotPositions[CRAFT_SLOT_COUNT] =
{
    {sWorkbenchColPositions[0], sWorkbenchRowPositions[0]},
    {sWorkbenchColPositions[1], sWorkbenchRowPositions[0]},
    {sWorkbenchColPositions[2], sWorkbenchRowPositions[0]},
    {sWorkbenchColPositions[0], sWorkbenchRowPositions[1]},
    {sWorkbenchColPositions[1], sWorkbenchRowPositions[1]},
    {sWorkbenchColPositions[2], sWorkbenchRowPositions[1]},
    {sWorkbenchColPositions[0], sWorkbenchRowPositions[2]},
    {sWorkbenchColPositions[1], sWorkbenchRowPositions[2]},
    {sWorkbenchColPositions[2], sWorkbenchRowPositions[2]},
};

void CraftMenuUI_GetItemIconPos(u8 slot, s16 baseX, s16 baseY, s16 *x, s16 *y)
{
    const struct GridSlotPos *pos;

    if (slot >= CRAFT_SLOT_COUNT)
    {
        *x = baseX;
        *y = baseY;
        return;
    }

    pos = &sWorkbenchSlotPositions[slot];
    *x = pos->x + baseX;
    *y = pos->y + baseY;
}

static const struct WindowTemplate sCraftWindowTemplates[NUM_CRAFT_WINDOWS] =
{
    [WINDOW_CRAFT_GRID] = {
        .bg = 0,
        .tilemapLeft = 9,
        .tilemapTop = 3,
        .width = 11,
        .height = 10,
        .paletteNum = 15,
        .baseBlock = 1
    },
    [WINDOW_CRAFT_INFO] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 15,
        .width = 26,
        .height = 4,
        .paletteNum = 15,
        .baseBlock = 145
    },
    [WINDOW_CRAFT_MESSAGE] = {
        .bg = 0,
        .tilemapLeft = 3,
        .tilemapTop = 15,
        .width = 25,
        .height = 4,
        .paletteNum = 15,
        .baseBlock = 145
    },
    [WINDOW_CRAFT_YESNO] = {
        .bg = 0,
        .tilemapLeft = 23,
        .tilemapTop = 9,
        .width = 5,
        .height = 4,
        .paletteNum = 15,
        .baseBlock = 300,
    },
    [WINDOW_CRAFT_ACTIONS] = {
        .bg = 0,
        .tilemapLeft = 22,
        .tilemapTop = 2,
        .width = 6,
        .height = 11,
        .paletteNum = 15,
        .baseBlock = 340,
    },
    [WINDOW_CRAFT_ITEMINFO] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 3,
        .width = 6,
        .height = 8,
        .paletteNum = 15,
        .baseBlock = 410
    },
    [WINDOW_CRAFT_QUANTITY] = {
        .bg = 0,
        .tilemapLeft = 23,
        .tilemapTop = 11,
        .width = 5,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 500,
    }
};

//---------------------------------------------------------------------------
// Window setup and teardown
//---------------------------------------------------------------------------

static const struct CompressedSpriteSheet sWorkbenchSheets[] =
{
    { gCraftWorkbench_TopLeft_Gfx,  0x400, TAG_WB_TOPLEFT },
    { gCraftWorkbench_TopMid_Gfx,   0x400, TAG_WB_TOPMID },
    { gCraftWorkbench_TopRight_Gfx, 0x400, TAG_WB_TOPRIGHT },
    { gCraftWorkbench_MidLeft_Gfx,  0x400, TAG_WB_MIDLEFT },
    { gCraftWorkbench_MidMid_Gfx,   0x400, TAG_WB_MIDMID },
    { gCraftWorkbench_MidRight_Gfx, 0x400, TAG_WB_MIDRIGHT },
    { gCraftWorkbench_BotLeft_Gfx,  0x400, TAG_WB_BOTLEFT },
    { gCraftWorkbench_BotMid_Gfx,   0x400, TAG_WB_BOTMID },
    { gCraftWorkbench_BotRight_Gfx, 0x400, TAG_WB_BOTRIGHT },
};

static const struct SpritePalette sWorkbenchPalette = { gCraftWorkbench_Pal, TAG_WB_PALETTE };

static const struct OamData sOam_Workbench32 =
{
    .shape = SPRITE_SHAPE(32x32),
    .size = SPRITE_SIZE(32x32),
    .priority = 1,
};

#define WB_TEMPLATE(tag) {            \
    .tileTag = tag,                   \
    .paletteTag = TAG_WB_PALETTE,     \
    .oam = &sOam_Workbench32,         \
    .anims = gDummySpriteAnimTable,   \
    .images = NULL,                   \
    .affineAnims = gDummySpriteAffineAnimTable, \
    .callback = SpriteCallbackDummy   \
}

static const struct SpriteTemplate sWorkbenchTemplates[CRAFT_SLOT_COUNT] =
{
    WB_TEMPLATE(TAG_WB_TOPLEFT),
    WB_TEMPLATE(TAG_WB_TOPMID),
    WB_TEMPLATE(TAG_WB_TOPRIGHT),
    WB_TEMPLATE(TAG_WB_MIDLEFT),
    WB_TEMPLATE(TAG_WB_MIDMID),
    WB_TEMPLATE(TAG_WB_MIDRIGHT),
    WB_TEMPLATE(TAG_WB_BOTLEFT),
    WB_TEMPLATE(TAG_WB_BOTMID),
    WB_TEMPLATE(TAG_WB_BOTRIGHT),
};

static void LoadCraftWindows(void)
{
    sCraftGridWindowId = AddWindow(&sCraftWindowTemplates[WINDOW_CRAFT_GRID]);
    sCraftItemInfoWindowId = AddWindow(&sCraftWindowTemplates[WINDOW_CRAFT_ITEMINFO]);
    sCraftInfoWindowId = AddWindow(&sCraftWindowTemplates[WINDOW_CRAFT_INFO]);
}

static void ShowGridWindow(void)
{
    FillWindowPixelBuffer(sCraftGridWindowId, PIXEL_FILL(0));
    PutWindowTilemap(sCraftGridWindowId);
    CopyWindowToVram(sCraftGridWindowId, COPYWIN_FULL);
}

static void ShowInfoWindow(void)
{
    DrawStdFrameWithCustomTileAndPalette(sCraftInfoWindowId, TRUE, 0x214, 14);
    FillWindowPixelBuffer(sCraftInfoWindowId, PIXEL_FILL(1));
    CopyWindowToVram(sCraftInfoWindowId, COPYWIN_FULL);
}

static void HideInfoWindow(void)
{
    if (sCraftInfoWindowId != WINDOW_NONE)
    {
        ClearStdWindowAndFrame(sCraftInfoWindowId, TRUE);
        RemoveWindow(sCraftInfoWindowId);
        sCraftInfoWindowId = WINDOW_NONE;
    }
}

static void HideItemInfoWindow(void)
{
    if (sCraftItemInfoWindowId != WINDOW_NONE)
    {
        ClearStdWindowAndFrame(sCraftItemInfoWindowId, TRUE);
        RemoveWindow(sCraftItemInfoWindowId);
        sCraftItemInfoWindowId = WINDOW_NONE;
    }
}

static void ShowItemInfoWindow(void)
{
    if (sCraftItemInfoWindowId == WINDOW_NONE)
    {
        sCraftItemInfoWindowId = AddWindow(&sCraftWindowTemplates[WINDOW_CRAFT_ITEMINFO]);
        UpdateItemInfoWindow();
    }
}

static void UpdateCraftInfoWindow(void)
{
    if (sCraftInfoWindowId == WINDOW_NONE)
        return;
    AddTextPrinterParameterized3(sCraftInfoWindowId, FONT_NORMAL, 2, 7, sInputTextColor, 0, sText_CraftingUi_AButton);
    AddTextPrinterParameterized3(sCraftInfoWindowId, FONT_SMALL, 13, 2, sInputTextColor, 0, sText_CraftingUi_AddItem);
    AddTextPrinterParameterized3(sCraftInfoWindowId, FONT_NORMAL, 45, 7, sInputTextColor, 0, sText_CraftingUi_BButtonExit);
    const u8 *startColor = CraftLogic_CanCraft(gCraftRecipes, gCraftRecipeCount) ? sHintTextColor : sInputTextColor;
    AddTextPrinterParameterized3(sCraftInfoWindowId, FONT_NORMAL, 85, 7, startColor, 0, sText_CraftingUi_StartButtonCraft);
    AddTextPrinterParameterized3(sCraftInfoWindowId, FONT_NORMAL, 148, 7, sInputTextColor, 0, sText_CraftingUi_SelectButton);
    AddTextPrinterParameterized3(sCraftInfoWindowId, FONT_SMALL, 175, 2, sInputTextColor, 0, sText_CraftingUi_RecipeBook);
}

static void UpdateItemInfoWindow(void)
{
    if (sCraftItemInfoWindowId == WINDOW_NONE)
        return;

    int row = CRAFT_SLOT_ROW(sCraftCursorPos);
    int col = CRAFT_SLOT_COL(sCraftCursorPos);

    if (gCraftSlots[row][col].itemId != ITEM_NONE)
    {
        FillWindowPixelBuffer(sCraftItemInfoWindowId, PIXEL_FILL(1));
        PutWindowTilemap(sCraftItemInfoWindowId);
        DrawStdFrameWithCustomTileAndPalette(sCraftItemInfoWindowId, TRUE, 0x214, 14);

        u8 name[ITEM_NAME_LENGTH];
        CopyItemName(gCraftSlots[row][col].itemId, name);
        u32 width = WindowWidthPx(sCraftItemInfoWindowId) - 16;
        u8 fontId = GetFontIdToFit(name, FONT_NORMAL, 0, width);
        BreakStringAutomatic(name, width, 0, fontId, HIDE_SCROLL_PROMPT);
        AddTextPrinterParameterized3(sCraftItemInfoWindowId, FONT_NARROWER, 2, 1, sInputTextColor, 0, name);
        ConvertIntToDecimalStringN(gStringVar1, gCraftSlots[row][col].quantity, STR_CONV_MODE_LEFT_ALIGN, 3);
        StringExpandPlaceholders(gStringVar4, gText_xVar1);
        AddTextPrinterParameterized3(sCraftItemInfoWindowId, FONT_NORMAL, 2, 45, sInputTextColor, 0, gStringVar4);
        CopyWindowToVram(sCraftItemInfoWindowId, COPYWIN_FULL);
    }
    else
    {
        ClearStdWindowAndFrameToTransparent(sCraftItemInfoWindowId, TRUE);
    }
}

//---------------------------------------------------------------------------
// Cursor and grid updates
//---------------------------------------------------------------------------


static void CreateWorkbenchSprite(void)
{
    int row, col, i;
    for (i = 0; i < CRAFT_SLOT_COUNT; i++)
        LoadCompressedSpriteSheet(&sWorkbenchSheets[i]);
    LoadSpritePalette(&sWorkbenchPalette);
    UpdateSpritePaletteWithTime(IndexOfSpritePaletteTag(TAG_WB_PALETTE));

    const s16 xBase = WB_CENTER_X - 32;
    const s16 yBase = WB_CENTER_Y - 32;
    for (row = 0; row < CRAFT_ROWS; row++)
    {
        for (col = 0; col < CRAFT_COLS; col++)
        {
            i = row * CRAFT_COLS + col;
            sWorkbenchSpriteIds[i] = CreateSprite(&sWorkbenchTemplates[i], xBase + 32 * col, yBase + 32 * row, 0);
        }
    }
}

static void DestroyWorkbenchSprite(void)
{
    int i;
    for (i = 0; i < CRAFT_SLOT_COUNT; i++)
    {
        if (sWorkbenchSpriteIds[i] != SPRITE_NONE)
        {
            DestroySpriteAndFreeResources(&gSprites[sWorkbenchSpriteIds[i]]);
            sWorkbenchSpriteIds[i] = SPRITE_NONE;
        }
    }
    FreeSpriteTilesByTag(TAG_WB_TOPLEFT);
    FreeSpriteTilesByTag(TAG_WB_TOPMID);
    FreeSpriteTilesByTag(TAG_WB_TOPRIGHT);
    FreeSpriteTilesByTag(TAG_WB_MIDLEFT);
    FreeSpriteTilesByTag(TAG_WB_MIDMID);
    FreeSpriteTilesByTag(TAG_WB_MIDRIGHT);
    FreeSpriteTilesByTag(TAG_WB_BOTLEFT);
    FreeSpriteTilesByTag(TAG_WB_BOTMID);
    FreeSpriteTilesByTag(TAG_WB_BOTRIGHT);
    FreeSpritePaletteByTag(TAG_WB_PALETTE);
}

static u16 GetCraftIconTag(u8 slot, bool8 alt)
{
    return (alt ? TAG_CRAFT_ICON_ALT_BASE : TAG_CRAFT_ICON_BASE) + slot;
}

static void DestroyCraftIconSprite(u8 slot)
{
    if (sCraftSlotSpriteIds[slot] != SPRITE_NONE)
    {
        bool8 alt = sCraftSlotTagFlip[slot];
        DestroySprite(&gSprites[sCraftSlotSpriteIds[slot]]);
        FreeSpriteTilesByTag(GetCraftIconTag(slot, alt));
        FreeSpritePaletteByTag(GetCraftIconTag(slot, alt));
        sCraftSlotSpriteIds[slot] = SPRITE_NONE;
    }
}

void CraftMenuUI_DrawIcons(void)
{
    int row, col, index, i;
    u8 oldSpriteIds[CRAFT_SLOT_COUNT];
    u16 oldTags[CRAFT_SLOT_COUNT];

    for (row = 0, index = 0; row < CRAFT_ROWS; row++)
    {
        for (col = 0; col < CRAFT_COLS; col++, index++)
        {
            oldSpriteIds[index] = SPRITE_NONE;
            if (gCraftSlots[row][col].itemId != sCraftSlotItemIds[index])
            {
                bool8 oldFlip = sCraftSlotTagFlip[index];
                u16 newTag = GetCraftIconTag(index, !oldFlip);
                u8 spriteId = SPRITE_NONE;

                if (gCraftSlots[row][col].itemId != ITEM_NONE)
                {
                    spriteId = AddItemIconSprite(newTag, newTag, gCraftSlots[row][col].itemId);
                    if (spriteId != MAX_SPRITES)
                    {
                        UpdateSpritePaletteWithTime(IndexOfSpritePaletteTag(newTag));
                        gSprites[spriteId].x = sWorkbenchSlotPositions[index].x + 79;
                        gSprites[spriteId].y = sWorkbenchSlotPositions[index].y + 28;
                        gSprites[spriteId].oam.priority = 0;
                    }
                    else
                    {
                        spriteId = SPRITE_NONE;
                    }
                }

                if (sCraftSlotSpriteIds[index] != SPRITE_NONE)
                {
                    gSprites[sCraftSlotSpriteIds[index]].invisible = TRUE;
                    oldSpriteIds[index] = sCraftSlotSpriteIds[index];
                    oldTags[index] = GetCraftIconTag(index, oldFlip);
                }

                sCraftSlotSpriteIds[index] = spriteId;
                sCraftSlotItemIds[index] = gCraftSlots[row][col].itemId;
                sCraftSlotTagFlip[index] = !oldFlip;
            }
        }
    }

    BuildOamBuffer();

    for (i = 0; i < CRAFT_SLOT_COUNT; i++)
    {
        if (oldSpriteIds[i] != SPRITE_NONE)
        {
            DestroySprite(&gSprites[oldSpriteIds[i]]);
            FreeSpriteTilesByTag(oldTags[i]);
            FreeSpritePaletteByTag(oldTags[i]);
        }
    }

    UpdateItemInfoWindow();
    if (sCraftInfoWindowId != WINDOW_NONE)
    {
        UpdateCraftInfoWindow();
        CopyWindowToVram(sCraftInfoWindowId, COPYWIN_FULL);
    }
}

void CraftMenuUI_UpdateGrid(void)
{
    int i;
    FillWindowPixelBuffer(sCraftGridWindowId, PIXEL_FILL(0));
    for (i = 0; i < CRAFT_SLOT_COUNT; i++)
    {
        const struct GridSlotPos *pos = &sWorkbenchSlotPositions[i];
        if (i == sCraftCursorPos)
        {
            const u8 *color = sInSwapMode ? sSwapTextColor : sGridTextColor;
            AddTextPrinterParameterized3(sCraftGridWindowId, FONT_NORMAL, pos->x + sCraftCursorXOffset, pos->y, color, 0, gText_SelectorArrow2);
        }
    }
    CopyWindowToVram(sCraftGridWindowId, COPYWIN_FULL);
    UpdateItemInfoWindow();
}

bool8 CraftMenuUI_HandleDpadInput(void)
{
    u8 oldPos = sCraftCursorPos;

    if (JOY_NEW(DPAD_UP) && sCraftCursorPos >= CRAFT_COLS)
        sCraftCursorPos -= CRAFT_COLS;
    else if (JOY_NEW(DPAD_DOWN) && sCraftCursorPos < CRAFT_SLOT_COUNT - CRAFT_COLS)
        sCraftCursorPos += CRAFT_COLS;
    else if (JOY_NEW(DPAD_LEFT) && sCraftCursorPos % CRAFT_COLS != 0)
        sCraftCursorPos--;
    else if (JOY_NEW(DPAD_RIGHT) && sCraftCursorPos % CRAFT_COLS != CRAFT_COLS - 1)
        sCraftCursorPos++;

    return (sCraftCursorPos != oldPos);
}

void CraftMenuUI_Init(void)
{
    int i;
    for (i = 0; i < CRAFT_SLOT_COUNT; i++)
    {
        sWorkbenchSpriteIds[i] = SPRITE_NONE;
        sCraftSlotSpriteIds[i] = SPRITE_NONE;
        sCraftSlotItemIds[i] = ITEM_NONE;
        sCraftSlotTagFlip[i] = FALSE;
    }
    sCraftCursorPos = 0;
    sActionMenuWindowId = WINDOW_NONE;
    sQuantityWindowId = WINDOW_NONE;
    sInSwapMode = FALSE;

    LoadCraftWindows();
    ShowGridWindow();
    ShowInfoWindow();
    CreateWorkbenchSprite();
    CraftMenuUI_DrawIcons();
    UpdateCraftInfoWindow();
    CraftMenuUI_UpdateGrid();
    UpdateItemInfoWindow();
}

void CraftMenuUI_Close(void)
{
    int i;
    gCraftActiveSlot = sCraftCursorPos;
    ClearStdWindowAndFrame(sCraftGridWindowId, TRUE);
    if (sCraftGridWindowId != WINDOW_NONE)
    {
        RemoveWindow(sCraftGridWindowId);
        sCraftGridWindowId = WINDOW_NONE;
    }
    HideItemInfoWindow();
    HideInfoWindow();
    CraftMenuUI_HideActionMenu();

    for (i = 0; i < CRAFT_SLOT_COUNT; i++)
        DestroyCraftIconSprite(i);

    DestroyWorkbenchSprite();
}

u8 CraftMenuUI_GetCursorPos(void)
{
    return sCraftCursorPos;
}

void CraftMenuUI_SetCursorPos(u8 pos)
{
    if (pos < CRAFT_SLOT_COUNT)
        sCraftCursorPos = pos;
    else
        sCraftCursorPos = 0;
    CraftMenuUI_UpdateGrid();
}

//---------------------------------------------------------------------------
// Action menu and quantity helpers
//---------------------------------------------------------------------------

void CraftMenuUI_ShowActionMenu(void)
{
    HideInfoWindow();
    if (sActionMenuWindowId == WINDOW_NONE)
    {
        sActionMenuWindowId = AddWindow(&sCraftWindowTemplates[WINDOW_CRAFT_ACTIONS]);
        DrawStdFrameWithCustomTileAndPalette(sActionMenuWindowId, TRUE, 0x214, 14);
        FillWindowPixelBuffer(sActionMenuWindowId, PIXEL_FILL(1));
        static const struct MenuAction actions[] = {
            {gText_CraftSwapItem, {NULL}},
            {gText_CraftAdjustQty, {NULL}},
            {gText_CraftSwapSlot, {NULL}},
            {gText_CraftPackItem, {NULL}},
            {gText_Cancel2, {NULL}},
        };
        static const u8 ids[] = {0,1,2,3,4};
        PrintMenuActionTexts(sActionMenuWindowId, FONT_SHORT_NARROWER, 8, 1, 0, 16, ARRAY_COUNT(actions), actions, ids);
        InitMenuInUpperLeftCornerNormal(sActionMenuWindowId, ARRAY_COUNT(actions), 0);
        CopyWindowToVram(sActionMenuWindowId, COPYWIN_FULL);
    }
}

void CraftMenuUI_HideActionMenu(void)
{
    if (sActionMenuWindowId != WINDOW_NONE)
    {
        ClearStdWindowAndFrameToTransparent(sActionMenuWindowId, TRUE);
        RemoveWindow(sActionMenuWindowId);
        sActionMenuWindowId = WINDOW_NONE;
    }
}

s8 CraftMenuUI_ProcessActionMenuInput(void)
{
    s8 selection = Menu_ProcessInputNoWrap();
    if (selection != MENU_NOTHING_CHOSEN)
        PlaySE(SE_SELECT);
    if (selection == MENU_B_PRESSED)
        return 4; // cancel
    return selection;
}

void CraftMenuUI_StartSwapMode(void)
{
    sInSwapMode = TRUE;
    HideInfoWindow();
    CraftMenuUI_UpdateGrid();
}

void CraftMenuUI_EndSwapMode(void)
{
    sInSwapMode = FALSE;
    CraftMenuUI_UpdateGrid();
    CraftMenuUI_RedrawInfo();
}

bool8 CraftMenuUI_InSwapMode(void)
{
    return sInSwapMode;
}

void CraftMenuUI_RedrawInfo(void)
{
    if (sCraftInfoWindowId == WINDOW_NONE)
    {
        sCraftInfoWindowId = AddWindow(&sCraftWindowTemplates[WINDOW_CRAFT_INFO]);
        ShowInfoWindow();
    }
    else
    {
        FillWindowPixelBuffer(sCraftInfoWindowId, PIXEL_FILL(1));
    }
    UpdateCraftInfoWindow();
    CopyWindowToVram(sCraftInfoWindowId, COPYWIN_FULL);
}

void CraftMenuUI_PrintInfo(const u8 *text, u8 x, u8 y)
{
    if (sCraftInfoWindowId == WINDOW_NONE)
    {
        sCraftInfoWindowId = AddWindow(&sCraftWindowTemplates[WINDOW_CRAFT_INFO]);
        ShowInfoWindow();
    }
    FillWindowPixelBuffer(sCraftInfoWindowId, PIXEL_FILL(1));
    AddTextPrinterParameterized3(sCraftInfoWindowId, FONT_NORMAL, x, y, sInputTextColor, 0, text);
    CopyWindowToVram(sCraftInfoWindowId, COPYWIN_FULL);
}

//---------------------------------------------------------------------------
// Message boxes and confirmation prompts
//---------------------------------------------------------------------------

void CraftMenuUI_DisplayMessage(u8 taskId, const u8 *text, TaskFunc nextTask)
{
    HideInfoWindow();

    if (sCraftMessageWindowId != WINDOW_NONE)
    {
        ClearDialogWindowAndFrame(sCraftMessageWindowId, TRUE);
        RemoveWindow(sCraftMessageWindowId);
        sCraftMessageWindowId = WINDOW_NONE;
    }

    sCraftMessageWindowId = AddWindow(&sCraftWindowTemplates[WINDOW_CRAFT_MESSAGE]);
    LoadMessageBoxAndBorderGfx();
    StringExpandPlaceholders(gStringVar4, text);
    DisplayMessageAndContinueTask(taskId, sCraftMessageWindowId, DLG_WINDOW_BASE_TILE_NUM,
                                  DLG_WINDOW_PALETTE_NUM, FONT_NORMAL, GetPlayerTextSpeedDelay(),
                                  gStringVar4, nextTask);
    CopyWindowToVram(sCraftMessageWindowId, COPYWIN_FULL);
}

void CraftMenuUI_ShowYesNo(void)
{
    CreateYesNoMenu(&sCraftWindowTemplates[WINDOW_CRAFT_YESNO], STD_WINDOW_BASE_TILE_NUM, STD_WINDOW_PALETTE_NUM, 0);
}

void CraftMenuUI_ClearMessage(void)
{
    if (sCraftMessageWindowId != WINDOW_NONE)
    {
        ClearDialogWindowAndFrame(sCraftMessageWindowId, TRUE);
        RemoveWindow(sCraftMessageWindowId);
        sCraftMessageWindowId = WINDOW_NONE;
    }

    if (sCraftInfoWindowId == WINDOW_NONE)
    {
        sCraftInfoWindowId = AddWindow(&sCraftWindowTemplates[WINDOW_CRAFT_INFO]);
        ShowInfoWindow();
        UpdateCraftInfoWindow();
    }

    ShowItemInfoWindow();
}

void CraftMenuUI_DisplayPackUpMessage(u8 taskId, TaskFunc nextTask)
{
    HideItemInfoWindow();
    CraftMenuUI_DisplayMessage(taskId, gText_PackUpQuestion, nextTask);
}

void CraftMenuUI_ShowPackUpYesNo(void)
{
    CraftMenuUI_ShowYesNo();
}

void CraftMenuUI_ClearPackUpMessage(void)
{
    CraftMenuUI_ClearMessage();
}

void CraftMenuUI_DisplayNoItemsMessage(u8 taskId, TaskFunc nextTask)
{
    HideItemInfoWindow();
    CraftMenuUI_DisplayMessage(taskId, gText_CraftNoItems, nextTask);
}

void CraftMenuUI_DisplayInvalidMessage(u8 taskId, TaskFunc nextTask)
{
    HideItemInfoWindow();
    CraftMenuUI_DisplayMessage(taskId, gText_CraftInvalid, nextTask);
}

void CraftMenuUI_DisplayCraftConfirmMessage(u8 taskId, u16 itemId, u16 quantity, TaskFunc nextTask)
{
    ConvertIntToDecimalStringN(gStringVar1, quantity, STR_CONV_MODE_LEFT_ALIGN, 3);
    CopyItemNameHandlePlural(itemId, gStringVar2, quantity);
    StringExpandPlaceholders(gStringVar4, gText_CraftConfirm);
    HideItemInfoWindow();
    CraftMenuUI_DisplayMessage(taskId, gStringVar4, nextTask);
}

static void ShowQuantityWindow(void)
{
    DrawStdFrameWithCustomTileAndPalette(sQuantityWindowId, TRUE, 0x214, 14);
    FillWindowPixelBuffer(sQuantityWindowId, PIXEL_FILL(1));
    CopyWindowToVram(sQuantityWindowId, COPYWIN_FULL);
}

u8 CraftMenuUI_AddQuantityWindow(void)
{
    if (sQuantityWindowId == WINDOW_NONE)
    {
        HideItemInfoWindow();
        sQuantityWindowId = AddWindow(&sCraftWindowTemplates[WINDOW_CRAFT_QUANTITY]);
        ShowQuantityWindow();
    }
    return sQuantityWindowId;
}

void CraftMenuUI_PrintQuantity(u16 quantity)
{
    if (sQuantityWindowId != WINDOW_NONE)
    {
        ConvertIntToDecimalStringN(gStringVar1, quantity, STR_CONV_MODE_LEADING_ZEROS, 3);
        StringExpandPlaceholders(gStringVar4, gText_xVar1);
        AddTextPrinterParameterized(sQuantityWindowId, FONT_NORMAL, gStringVar4,
                                   GetStringCenterAlignXOffset(FONT_NORMAL, gStringVar4, 0x28), 2, 0, 0);
    }
}

void CraftMenuUI_RemoveQuantityWindow(void)
{
    if (sQuantityWindowId != WINDOW_NONE)
    {
        ClearStdWindowAndFrameToTransparent(sQuantityWindowId, TRUE);
        RemoveWindow(sQuantityWindowId);
        sQuantityWindowId = WINDOW_NONE;
        ShowItemInfoWindow();
    }
}

void CraftMenuUI_DisplayAdjustQtyMessage(u8 taskId, u16 itemId, TaskFunc nextTask)
{
    CopyItemNameHandlePlural(itemId, gStringVar1, 2);
    StringExpandPlaceholders(gStringVar4, sText_CraftPlaceHowManyVar1);
    HideItemInfoWindow();
    CraftMenuUI_DisplayMessage(taskId, gStringVar4, nextTask);
}

void CraftMenuUI_ClearAdjustQtyMessage(void)
{
    CraftMenuUI_ClearMessage();
}
