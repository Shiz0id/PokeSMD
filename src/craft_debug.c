#include "global.h"
#include "bg.h"
#include "gpu_regs.h"
#include "palette.h"
#include "window.h"
#include "text_window.h"
#include "text.h"
#include "string_util.h"
#include "strings.h"
#include "menu_helpers.h"
#include "main.h"
#include "menu.h"
#include "craft_logic.h"
#include "data/crafting_recipes.h"
#include "craft_menu.h"
#include "craft_debug.h"
#include "constants/rgb.h"

static const u8 sDebugTextColor[] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_LIGHT_GRAY};

static const u8 sText_DebugMenuTitle[] = _("Crafting Debug Menu");
static const u8 sText_SlotLabel[] = _("Slot ");
static const u8 sText_ColonSpace[] = _(": ");
static const u8 sText_None[] = _("None");
static const u8 sText_ResultLabel[] = _("Result: ");

static const u16 sBgColor[] = { RGB_WHITE };

static const struct BgTemplate sBgTemplate[] =
{
    {
        .bg = 0,
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 0,
        .baseTile = 0
    }
};


enum
{
    WIN_TITLE,
    WIN_LIST,
};

static const struct WindowTemplate sDebugWindowTemplates[] =
{
    [WIN_TITLE] = {
        .bg = 0,
        .tilemapLeft = 0,
        .tilemapTop = 0,
        .width = 30,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 0x1A0,
    },
    [WIN_LIST] = {
        .bg = 0,
        .tilemapLeft = 0,
        .tilemapTop = 2,
        .width = 30,
        .height = 15,
        .paletteNum = 15,
        .baseBlock = 0x1DC,
    },
    DUMMY_WIN_TEMPLATE,
};

static void Task_CraftDebugMenu(u8 taskId);
#define tState       data[0]
#define tListWinId   data[1]
#define tTitleWinId  data[2]

static void MainCB2(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

static void VBlankCB(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

void CB2_CraftDebugMenu(void)
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
        InitBgsFromTemplates(0, sBgTemplate, ARRAY_COUNT(sBgTemplate));
        ResetAllBgsCoordinates();
        FreeAllWindowBuffers();
        DeactivateAllTextPrinters();
        ResetTasks();
        ResetSpriteData();
        SetBackdropFromColor(RGB_BLACK);
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
        FillBgTilemapBufferRect(0, 0, 0, 0, DISPLAY_TILE_WIDTH, DISPLAY_TILE_HEIGHT, 15);
        gMain.state++;
        break;
    case 2:
        InitWindows(sDebugWindowTemplates);
        LoadMessageBoxAndBorderGfx();
        ResetPaletteFade();
        LoadPalette(GetOverworldTextboxPalettePtr(), 0xf0, 16);
        gMain.state++;
        break;
    case 3:
        CreateTask(Task_CraftDebugMenu, 0);
        SetVBlankCallback(VBlankCB);
        SetMainCallback2(MainCB2);
        gMain.state++;
        break;
    case 4:
        break;
    }
}

static void PrintSlots(u8 windowId)
{
    int row, col, index;

    FillWindowPixelBuffer(windowId, PIXEL_FILL(1));
    index = 0;
    for (row = 0; row < CRAFT_ROWS; row++)
    {
        for (col = 0; col < CRAFT_COLS; col++, index++)
        {
            u8 lineBuffer[64];
            u8 qtyBuffer[16];
            u8 itemName[32];
            u8 numBuffer[4];
            u8 y = index * 12;

            StringCopy(lineBuffer, sText_SlotLabel);
            ConvertIntToDecimalStringN(numBuffer, index + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
            StringAppend(lineBuffer, numBuffer);
            StringAppend(lineBuffer, sText_ColonSpace);

            if (gCraftSlots[row][col].itemId != ITEM_NONE)
            {
                CopyItemName(gCraftSlots[row][col].itemId, itemName);
                StringAppend(lineBuffer, itemName);

                ConvertIntToDecimalStringN(gStringVar1, gCraftSlots[row][col].quantity, STR_CONV_MODE_LEFT_ALIGN, 3);
                StringExpandPlaceholders(qtyBuffer, gText_xVar1);
                StringAppend(lineBuffer, qtyBuffer);
            }
            else
            {
                StringAppend(lineBuffer, sText_None);
            }

            AddTextPrinterParameterized3(windowId, FONT_SMALL, 1, y, sDebugTextColor, 0, lineBuffer);
        }
    }

    {
        u16 resultItem;
        const struct CraftRecipe *recipe = CraftLogic_GetMatchingRecipe(gCraftRecipes, gCraftRecipeCount, &resultItem);
        u8 lineBuffer[64];
        u8 itemName[32];
        u8 qtyBuffer[16];
        u8 y = index * 12;

        StringCopy(lineBuffer, sText_ResultLabel);
        if (recipe != NULL)
        {
            u16 qty = CraftLogic_GetCraftableQuantity(recipe);
            CopyItemName(resultItem, itemName);
            StringAppend(lineBuffer, itemName);
            ConvertIntToDecimalStringN(gStringVar1, qty, STR_CONV_MODE_LEFT_ALIGN, 3);
            StringExpandPlaceholders(qtyBuffer, gText_xVar1);
            StringAppend(lineBuffer, qtyBuffer);
        }
        else
        {
            StringAppend(lineBuffer, sText_None);
        }

        AddTextPrinterParameterized3(windowId, FONT_SMALL, 1, y, sDebugTextColor, 0, lineBuffer);
    }

    CopyWindowToVram(windowId, COPYWIN_FULL);
}

static void PrintTitle(u8 windowId)
{
    FillWindowPixelBuffer(windowId, PIXEL_FILL(1));
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 60, 1, sDebugTextColor, 0, sText_DebugMenuTitle);
    CopyWindowToVram(windowId, COPYWIN_FULL);
}

static void Task_CraftDebugMenu(u8 taskId)
{
    switch (gTasks[taskId].tState)
    {
    case 0:
        gTasks[taskId].tTitleWinId = AddWindow(&sDebugWindowTemplates[WIN_TITLE]);
        gTasks[taskId].tListWinId = AddWindow(&sDebugWindowTemplates[WIN_LIST]);
        PutWindowTilemap(gTasks[taskId].tTitleWinId);
        PutWindowTilemap(gTasks[taskId].tListWinId);
        PrintTitle(gTasks[taskId].tTitleWinId);
        PrintSlots(gTasks[taskId].tListWinId);
        ShowBg(0);
        LoadPalette(sBgColor, 0, 2);
        BeginNormalPaletteFade(0xFFFFFFFF, 0, 16, 0, RGB_BLACK);
        gTasks[taskId].tState++;
        break;
    case 1:
        if (!gPaletteFade.active)
            gTasks[taskId].tState++;
        break;
    case 2:
        if (JOY_NEW(B_BUTTON | A_BUTTON))
        {
            BeginNormalPaletteFade(0xFFFFFFFF, 0, 0, 16, RGB_BLACK);
            gTasks[taskId].tState++;
        }
        break;
    case 3:
        if (!gPaletteFade.active)
        {
            ClearWindowTilemap(gTasks[taskId].tTitleWinId);
            ClearWindowTilemap(gTasks[taskId].tListWinId);
            RemoveWindow(gTasks[taskId].tTitleWinId);
            RemoveWindow(gTasks[taskId].tListWinId);
            DestroyTask(taskId);
            SetMainCallback2(CB2_ReturnToCraftMenu);
        }
        break;
    }
}

#undef tState
#undef tListWinId
#undef tTitleWinId
