Craft_EventScript_CraftTable::
    msgbox Text_CraftTable_WantToCraft, MSGBOX_YESNO
    delay 4
    closemessage
    goto_if_eq VAR_RESULT, YES, Craft_EventScript_OpenCraftMenu
    goto Craft_Cancel
    end

Text_CraftTable_WantToCraft:
    .string "There's a WORK STATION...\n"
    .string "Want to CRAFT something?$"

Craft_EventScript_OpenCraftMenu::
    playse SE_WIN_OPEN
    special StartCraftMenu
    waitstate
    releaseall
    end

Craft_Cancel:
    releaseall
    end

@ CRAFT TUTORIAL
    @ State descriptions for VAR_ROUTE102_STATE
    @ 0: New Game
    @ 1: Fetch Quest
    @ 2: Antidote crafting
    @ 3: Normal. Free Use of Work Station

CraftTutorial_EventScript_Table::
    lock
    goto Craft_EventScript_CraftTable
    end

