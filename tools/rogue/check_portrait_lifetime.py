"""Guard the Mystery Dungeon speaker portrait: what it takes, it must give back.

THE OWNERSHIP RULE, stated once, here:

    RoguePortrait_Draw is the ONLY thing that acquires OBJ tiles, an OBJ palette,
    a sprite, a weather exemption and a rectangle of the BG0 tilemap for the
    portrait, and TearDownPortrait is the ONLY thing that releases them. Every
    path out of Draw either holds all five or holds none. The teardown hangs off
    HideFieldMessageBox, which is the single funnel every script reaches through
    release or closemessage.

WHY THIS EXISTS, and why it is not a data check. Nothing this feature can get
wrong shows up in a table. It fails by leaking OBJ VRAM until the next field
move has no room, by leaving a palette exempt from weather so an unrelated
sprite stops darkening in the fog, by drawing the box in a row the namebox
overwrites a frame later, or by putting the follower's nickname on every message
in the game for the rest of the session. All of those build clean.

That is the same class as check_jukebox_funnels.py and
check_bw_published_palette.py: a lifetime, asserted by naming the rule and then
asserting the pairing that makes it true.

WHAT IT ASSERTS.

  1. TearDownPortrait releases all five things Draw takes. A missing one is a
     leak that survives the conversation: FreeSpriteTilesByTag is the 2 KB of
     OBJ VRAM, FreeSpritePaletteByTag is one of sixteen OBJ palette slots,
     DestroySprite is one of sixty-four OAM entries, ResetPaletteColorMapType is
     the weather exemption, and FillBgTilemapBufferRect is the box itself, which
     otherwise stays on screen with no portrait in it.

  2. THE WEATHER UNDO IS PER-PALETTE. ResetPreservedPalettesInWeather throws the
     WHOLE override table away rather than one entry -- it assigns
     sPaletteColorMapTypes back to the base table -- so calling it here would
     un-preserve a field move pic or the start menu icon that had preserved a
     different slot. That is a bug in someone else's feature, caused from here,
     appearing as weather tinting something it should not.

  3. EVERY EARLY RETURN AFTER AN ACQUISITION GIVES IT BACK. The palette-failure
     branch must free the tiles it already loaded; the sprite-failure branch must
     free both. Each leak here is silent and permanent for the session.

  4. A PALETTE MISS IS CHECKED. LoadSpritePaletteWithTag returns 0xFF when all
     sixteen slots are taken, and a sprite whose palette never loaded draws
     through whatever colours its OAM slot last held -- corruption that changes
     with what else is on the map, which reads like a VRAM fault and is a missing
     branch. See references/engine-traps.md, "a sprite's palette is found by
     TAG, and a miss is silent".

  5. DRAW DOES NOT REBUILD WHILE SHOWING. Task_DrawFieldMessage runs once per
     message command, so a two-message conversation enters Draw twice. Without
     the guard the second one loads a second sheet under the same tag and leaks
     the first.

  6. THE BOX CANNOT REACH ROW 13. Rows 13-14 are the namebox and 14-19 the
     dialogue frame. A box that reached row 13 would be drawn over by whichever
     of the three ran last, which is order-dependent and therefore intermittent.

  7. THE HOOKS ARE WHERE THEY HAVE TO BE. Draw is called from case 1 of
     Task_DrawFieldMessage, which is the first state AFTER
     LoadMessageBoxAndBorderGfx has put the frame tiles in VRAM -- the box is
     built out of those tiles, so drawing it any earlier composes it from
     whatever was in those tiles before. Hide is called from
     HideFieldMessageBox. ResetState is called from InitStandardTextBoxWindows.

  8. BOTH FOLLOWER EXIT POINTS SET THE SPEAKER. GetFollowerAction settles on an
     emotion and calls ObjectEventEmote at exactly two places -- the
     conditional-message branch and the basic-message fallthrough. Miss one and
     the portrait appears for some of the follower's lines and not others, which
     reads as a rendering glitch rather than a missing call.

  9. WHOEVER SETS gSpeakerName CLEARS IT. IsSpeakerBuffered spawns a namebox for
     ANY non-NULL gSpeakerName, so a value left set puts the follower's nickname
     over every message that comes after it. battle_setup.c already has this bug
     with trainer names; this check keeps the portrait from adding a second one.

 10. EVERY FOLLOWER EMOTION MAPS TO A FACE, and the emotion list is DERIVED from
     follower_helper.h rather than kept here. A missing row is not a build error
     -- the designated initialiser leaves it 0, which is PORTRAIT_FACE_NORMAL --
     so an unmapped emotion wears a blank expression for ever. An emotion added
     upstream has to fail here rather than quietly defaulting.

 11. PLAY-FOUND: THE DECOMPRESSION BUFFER IS NOT THE SHEET SIZE. A front pic is
     anim_front.png at 64x128 -- TWO frames, 4096 bytes -- and
     DecompressDataWithHeaderWram writes what the compressed header says, not
     what the caller wants. A buffer sized to the 2048-byte sheet overran the
     next heap block on every portrait and froze the game inside a later Free,
     nowhere near the write, with a clean build.

 12. THE TWO HANDLES START AS SENTINELS. A zeroed spriteId is 0, which is a real
     sprite -- on the field usually the player -- so a teardown reached before
     RoguePortrait_ResetState would destroy it.

Usage:  python3 tools/rogue/check_portrait_lifetime.py [REPO | --repo PATH]
        python3 tools/rogue/check_portrait_lifetime.py --selftest
"""
import re
import sys
import tempfile
from pathlib import Path

PORTRAIT_C = 'src/rogue_portrait.c'
PORTRAIT_H = 'include/rogue_portrait.h'
MSGBOX_C = 'src/field_message_box.c'
EVENTOBJ_C = 'src/event_object_movement.c'
MENU_C = 'src/menu.c'
FOLLOWER_H = 'include/follower_helper.h'

SOURCES = [PORTRAIT_C, PORTRAIT_H, MSGBOX_C, EVENTOBJ_C, MENU_C, FOLLOWER_H]

# What Draw takes, and what releasing it looks like. Named with the cost of
# missing it, because the cost is the argument for the line being there.
RELEASES = [
    ('FreeSpriteTilesByTag',
     '2 KB of OBJ VRAM stays allocated under the portrait tag for the rest of '
     'the session, and the next thing that needs a 64x64 sheet -- a field move '
     'pic, the next portrait -- silently fails to load'),
    ('FreeSpritePaletteByTag',
     'one of the sixteen OBJ palette slots stays taken, and the eviction path '
     'in LoadSpritePalette starts throwing out overworld sprite palettes to '
     'make room'),
    ('DestroySprite',
     'the portrait stays on screen after the message box closes, and one of the '
     'sixty-four OAM entries is gone'),
    ('ResetPaletteColorMapType',
     'the palette slot stays exempt from the weather colour map, so whatever '
     'loads into it next stops darkening in fog or rain'),
    ('FillBgTilemapBufferRect',
     'the frame stays drawn on BG0 with nothing inside it'),
]


def fail(msg):
    print('FAIL  check_portrait_lifetime.py: %s' % msg)
    return False


def body(src, name):
    """The body of a function DEFINITION, not a call to it.

    Tolerates a trailing comment between the argument list and the brace, which
    GetFollowerAction has.
    """
    m = re.search(
        r'(?m)^[A-Za-z_][\w \*]*\b%s\([^)]*\)[ \t]*(?://[^\n]*)?\s*\n?\{(.*?)\n\}'
        % re.escape(name), src, re.S)
    return m.group(1) if m else None


def strip_comments(src):
    """Code only. This file deliberately NAMES the forbidden call in a comment
    explaining why it is forbidden, so a raw substring test would fire on the
    explanation rather than on a use."""
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    return re.sub(r'//.*', ' ', src)


def branch(src, condition):
    """The body of the if-block opened by `condition`."""
    i = src.find(condition)
    if i < 0:
        return None
    j = src.find('{', i)
    if j < 0:
        return None
    depth = 0
    for k in range(j, len(src)):
        if src[k] == '{':
            depth += 1
        elif src[k] == '}':
            depth -= 1
            if depth == 0:
                return src[j + 1:k]
    return None


def check(repo):
    repo = Path(repo)
    src = {}
    for rel in SOURCES:
        path = repo / rel
        if not path.exists():
            return fail('%s not found' % rel)
        src[rel] = path.read_text(encoding='utf-8', errors='replace')

    # 1. Everything Draw takes, TearDownPortrait gives back.
    teardown = body(src[PORTRAIT_C], 'TearDownPortrait')
    if teardown is None:
        return fail('TearDownPortrait not found in %s -- the ownership rule is '
                    'stated at the top of this file and needs re-hosting'
                    % PORTRAIT_C)
    for call, cost in RELEASES:
        if call not in teardown:
            return fail('TearDownPortrait does not call %s(); without it, %s'
                        % (call, cost))

    # 2. The weather undo is per-palette, not the global reset.
    if 'ResetPreservedPalettesInWeather' in strip_comments(src[PORTRAIT_C]):
        return fail('%s calls ResetPreservedPalettesInWeather(), which discards '
                    'the WHOLE weather override table rather than our one entry '
                    '-- it would un-preserve a field move pic or the start menu '
                    'icon that had preserved a different slot. Use '
                    'ResetPaletteColorMapType(index + 16)' % PORTRAIT_C)

    draw = body(src[PORTRAIT_C], 'RoguePortrait_Draw')
    if draw is None:
        return fail('RoguePortrait_Draw not found in %s' % PORTRAIT_C)

    # 3 and 4. The failure branches give back what they already took.
    if 'palIndex == 0xFF' not in draw:
        return fail('RoguePortrait_Draw does not test LoadSpritePaletteWithTag '
                    'for 0xFF. A palette miss is silent: the sprite draws '
                    'through whatever colours its OAM slot last held, which '
                    'looks like a VRAM fault and is a missing branch')
    palFail = branch(draw, 'palIndex == 0xFF')
    if palFail is None or 'FreeSpriteTilesByTag' not in palFail:
        return fail('the palette-failure branch of RoguePortrait_Draw does not '
                    'free the sprite tiles it already loaded -- 2 KB of OBJ VRAM '
                    'leaks every time the palette slots are full')

    if 'spriteId == MAX_SPRITES' not in draw:
        return fail('RoguePortrait_Draw does not test CreateSprite for '
                    'MAX_SPRITES, so a full OAM would index gSprites[64]')
    spriteFail = branch(draw, 'spriteId == MAX_SPRITES')
    if spriteFail is None:
        return fail('the CreateSprite failure branch of RoguePortrait_Draw '
                    'cannot be read')
    for call in ('FreeSpriteTilesByTag', 'FreeSpritePaletteByTag'):
        if call not in spriteFail:
            return fail('the CreateSprite failure branch of RoguePortrait_Draw '
                        'does not call %s() -- it holds some of the five and '
                        'none of them is ever released, because TearDownPortrait '
                        'only runs for a portrait that went up' % call)

    # 5. No rebuild while showing.
    if not re.search(r'if\s*\(\s*sPortrait\.showing\s*\)\s*\n?\s*return', draw):
        return fail('RoguePortrait_Draw does not return early when a portrait is '
                    'already showing. Task_DrawFieldMessage runs once per message '
                    'command, so a two-message conversation enters Draw twice and '
                    'the second call loads a second sheet under the same tag')

    # 6. The box floor stays clear of the namebox.
    m = re.search(r'#define\s+PORTRAIT_BOX_BOTTOM_ROW\s+(\d+)', src[PORTRAIT_H])
    if m is None:
        return fail('PORTRAIT_BOX_BOTTOM_ROW not found in %s' % PORTRAIT_H)
    if int(m.group(1)) > 12:
        return fail('PORTRAIT_BOX_BOTTOM_ROW is %s. Rows 13-14 are the namebox '
                    '(field_name_box.c, tilemapTop 13 height 2) and 14-19 the '
                    'dialogue frame (menu.c sStandardTextBox_WindowTemplates). A '
                    'box reaching row 13 is drawn over by whichever ran last'
                    % m.group(1))
    # The comparison, not just the constant -- boxTop is also computed from
    # PORTRAIT_BOX_BOTTOM_ROW + 1, so a looser test passes with the bound gone.
    if not re.search(r'boxHeight\s*>\s*PORTRAIT_BOX_BOTTOM_ROW\s*\+\s*1', draw):
        return fail('RoguePortrait_Draw does not bound the box height against '
                    'PORTRAIT_BOX_BOTTOM_ROW, so art taller than the floor allows '
                    'would put boxTop off the top of the tilemap')

    # 7. The hooks.
    task = body(src[MSGBOX_C], 'Task_DrawFieldMessage')
    if task is None:
        return fail('Task_DrawFieldMessage not found in %s' % MSGBOX_C)
    if 'RoguePortrait_Draw' not in task:
        return fail('Task_DrawFieldMessage does not call RoguePortrait_Draw, so '
                    'a speaker is set and nothing ever draws it')
    # It has to sit after the frame gfx load, which is case 0.
    gfxAt = max(task.find('LoadMessageBoxAndBorderGfx'), task.find('LoadSignPostWindowFrameGfx'))
    if gfxAt < 0 or task.find('RoguePortrait_Draw') < gfxAt:
        return fail('RoguePortrait_Draw is called before the message box frame '
                    'gfx are loaded. The portrait box is built out of those same '
                    'tiles at DLG_WINDOW_BASE_TILE_NUM, so drawing first composes '
                    'the frame from whatever those tiles held before')

    hide = body(src[MSGBOX_C], 'HideFieldMessageBox')
    if hide is None:
        return fail('HideFieldMessageBox not found in %s' % MSGBOX_C)
    if 'RoguePortrait_Hide' not in hide:
        return fail('HideFieldMessageBox does not call RoguePortrait_Hide. It is '
                    'the single funnel every script path reaches -- ScrCmd_release '
                    'and ScrCmd_closemessage are its only external callers -- so '
                    'without it nothing ever tears the portrait down')

    init = body(src[MENU_C], 'InitStandardTextBoxWindows')
    if init is None:
        return fail('InitStandardTextBoxWindows not found in %s' % MENU_C)
    if 'RoguePortrait_ResetState' not in init:
        return fail('InitStandardTextBoxWindows does not call '
                    'RoguePortrait_ResetState, so a portrait that was up when the '
                    'field tore down leaves stale handles pointing at sprites '
                    'that no longer exist')

    # 8. Both follower exit points.
    action = body(src[EVENTOBJ_C], 'GetFollowerAction')
    if action is None:
        return fail('GetFollowerAction not found in %s' % EVENTOBJ_C)
    emotes = len(re.findall(r'ObjectEventEmote\s*\(', action))
    speakers = len(re.findall(r'RoguePortrait_SetSpeakerMon\s*\(', action))
    if emotes == 0:
        return fail('GetFollowerAction no longer calls ObjectEventEmote -- the '
                    'two exit points this check pairs against have moved')
    if speakers != emotes:
        return fail('GetFollowerAction calls ObjectEventEmote %d time(s) but '
                    'RoguePortrait_SetSpeakerMon %d time(s). Each ObjectEventEmote '
                    'is an exit point with a settled emotion; missing one means '
                    'the portrait appears for some of the follower\'s lines and '
                    'not others' % (emotes, speakers))

    # 9. The speaker name is given back.
    if 'gSpeakerName' in src[PORTRAIT_C]:
        hidefn = body(src[PORTRAIT_C], 'RoguePortrait_Hide')
        if hidefn is None:
            return fail('RoguePortrait_Hide not found in %s' % PORTRAIT_C)
        if 'gSpeakerName = NULL' not in hidefn:
            return fail('%s sets gSpeakerName but RoguePortrait_Hide never clears '
                        'it. IsSpeakerBuffered spawns a namebox for ANY non-NULL '
                        'gSpeakerName, so the follower\'s nickname would appear '
                        'over every message for the rest of the session'
                        % PORTRAIT_C)
        # The guard itself, not just a mention -- ownsSpeakerName is also
        # assigned FALSE inside the block, so a looser test passes with the
        # condition widened to TRUE.
        if not re.search(r'if\s*\(\s*sPortrait\.ownsSpeakerName\s*\)', hidefn):
            return fail('RoguePortrait_Hide clears gSpeakerName unconditionally. '
                        'It must only clear the one it set, or it wipes a speaker '
                        'another script legitimately set')

    # 10. Every follower emotion maps to a face.
    #
    # DERIVED FROM THE ENUM, not from a list kept here. A hand-maintained list of
    # "the emotions that matter" is the shape this project has been burned by
    # twice; an emotion added upstream has to fail here rather than quietly
    # taking the designated initialiser's 0, which is PORTRAIT_FACE_NORMAL.
    emotions = re.findall(r'(FOLLOWER_EMOTION_[A-Z0-9_]+)\s*(?:=\s*\d+\s*)?,',
                          src[FOLLOWER_H])
    emotions = [e for e in emotions if e != 'FOLLOWER_EMOTION_LENGTH']
    if not emotions:
        return fail('no FOLLOWER_EMOTION_* constants found in %s -- the mapping '
                    'cannot be checked against anything' % FOLLOWER_H)
    m = re.search(r'sFollowerEmotionToFace\[[^\]]*\]\s*=\s*\{(.*?)\};',
                  src[PORTRAIT_C], re.S)
    if m is None:
        return fail('sFollowerEmotionToFace not found in %s' % PORTRAIT_C)
    table = m.group(1)
    for name in emotions:
        if ('[%s]' % name) not in table:
            return fail('sFollowerEmotionToFace has no row for %s. That is not a '
                        'build error -- the designated initialiser leaves it 0, '
                        'which is PORTRAIT_FACE_NORMAL -- so that emotion would '
                        'silently wear a blank expression for ever' % name)

    # 11. PLAY-FOUND: the decompression buffer is not the sheet size.
    code = strip_comments(src[PORTRAIT_C])
    m = re.search(r'#define\s+PORTRAIT_DECOMP_SIZE\s+([^\n]+)', code)
    if m is None:
        return fail('PORTRAIT_DECOMP_SIZE not found in %s. LoadSpecialPokePic '
                    'writes as many frames as the compressed header says, and a '
                    'front pic in this build is anim_front.png at 64x128 -- TWO '
                    'frames, 4096 bytes. A buffer sized to the 2048-byte sheet '
                    'overran the next heap block on every portrait and froze the '
                    'game on the following Free' % PORTRAIT_C)
    if 'MAX_MON_PIC_FRAMES' not in m.group(1):
        return fail('PORTRAIT_DECOMP_SIZE is not sized by MAX_MON_PIC_FRAMES. It '
                    'reads %r. The decompressor writes every frame the pic has, '
                    'not the one frame that reaches VRAM' % m.group(1).strip())
    if not re.search(r'AllocZeroed\s*\(\s*PORTRAIT_DECOMP_SIZE\s*\)', code):
        return fail('RoguePortrait_Draw does not allocate PORTRAIT_DECOMP_SIZE. '
                    'Allocating the sheet size instead is the heap overflow that '
                    'froze the game, and it froze it inside Free rather than at '
                    'the write, so the stack said nothing about this file')

    # 11. The handles start as sentinels, not as zero.
    decl = re.search(r'sPortrait\s*=\s*\{(.*?)\};', src[PORTRAIT_C], re.S)
    if decl is None:
        return fail('the sPortrait declaration cannot be read in %s' % PORTRAIT_C)
    if 'MAX_SPRITES' not in decl.group(1) or '0xFF' not in decl.group(1):
        return fail('sPortrait does not start with MAX_SPRITES and 0xFF in its '
                    'two handles. A zeroed spriteId is 0, which is a REAL sprite '
                    '-- on the field usually the player -- so a teardown reached '
                    'before RoguePortrait_ResetState would destroy it')

    print('ok    check_portrait_lifetime.py: portrait acquires and releases all '
          '%d resources, both follower exit points set the speaker, and all '
          'three hooks are in place' % len(RELEASES))
    return True


def selftest(repo):
    repo = Path(repo)
    src = {}
    for rel in SOURCES:
        src[rel] = (repo / rel).read_text(encoding='utf-8', errors='replace')

    if not check(repo):
        print('  SELFTEST INCONCLUSIVE: the check does not pass unmutated')
        return False

    cases = [
        # -- rule 1: the five releases --
        ('the OBJ tiles are never freed', PORTRAIT_C,
         lambda s: s.replace('    FreeSpriteTilesByTag(PORTRAIT_TILE_TAG);\n'
                             '    FreeSpritePaletteByTag(FLDEFF_PAL_TAG_ROGUE_PORTRAIT);\n',
                             '    FreeSpritePaletteByTag(FLDEFF_PAL_TAG_ROGUE_PORTRAIT);\n', 1)),
        ('the OBJ palette is never freed', PORTRAIT_C,
         lambda s: s.replace('    FreeSpriteTilesByTag(PORTRAIT_TILE_TAG);\n'
                             '    FreeSpritePaletteByTag(FLDEFF_PAL_TAG_ROGUE_PORTRAIT);\n',
                             '    FreeSpriteTilesByTag(PORTRAIT_TILE_TAG);\n', 1)),
        ('the sprite is never destroyed', PORTRAIT_C,
         lambda s: s.replace('        DestroySprite(&gSprites[sPortrait.spriteId]);',
                             '        (void)0;', 1)),
        ('the weather exemption is never undone', PORTRAIT_C,
         lambda s: s.replace('        ResetPaletteColorMapType(sPortrait.palIndex + 16);',
                             '        (void)0;', 1)),
        ('the frame is left drawn on BG0', PORTRAIT_C,
         lambda s: s.replace('        FillBgTilemapBufferRect(0, 0, sPortrait.boxLeft, sPortrait.boxTop,\n'
                             '                                sPortrait.boxWidth, sPortrait.boxHeight, 0);',
                             '        (void)0;', 1)),

        # -- rule 2: the weather undo is per-palette --
        ('the global weather reset is used instead', PORTRAIT_C,
         lambda s: s.replace('ResetPaletteColorMapType(sPortrait.palIndex + 16);',
                             'ResetPreservedPalettesInWeather();', 1)),

        # -- rules 3 and 4: the failure branches --
        ('the palette miss is not checked at all', PORTRAIT_C,
         lambda s: s.replace('if (palIndex == 0xFF)', 'if (FALSE)', 1)),
        ('the palette-failure branch leaks the tiles', PORTRAIT_C,
         lambda s: s.replace('        FreeSpriteTilesByTag(PORTRAIT_TILE_TAG);\n'
                             '        return;\n    }\n\n    sPortrait.boxLeft',
                             '        return;\n    }\n\n    sPortrait.boxLeft', 1)),
        ('the CreateSprite failure branch leaks the palette', PORTRAIT_C,
         lambda s: s.replace('        FreeSpriteTilesByTag(PORTRAIT_TILE_TAG);\n'
                             '        FreeSpritePaletteByTag(FLDEFF_PAL_TAG_ROGUE_PORTRAIT);\n'
                             '        return;\n    }\n\n    sPortrait.spriteId',
                             '        FreeSpriteTilesByTag(PORTRAIT_TILE_TAG);\n'
                             '        return;\n    }\n\n    sPortrait.spriteId', 1)),
        ('CreateSprite is not checked for MAX_SPRITES', PORTRAIT_C,
         lambda s: s.replace('if (spriteId == MAX_SPRITES)', 'if (FALSE)', 1)),

        # -- rule 5: no rebuild while showing --
        ('a second message rebuilds the portrait', PORTRAIT_C,
         lambda s: s.replace('    if (sPortrait.showing)\n        return;',
                             '    if (FALSE)\n        return;', 1)),

        # -- rule 6: the box floor --
        ('the box is allowed into the namebox rows', PORTRAIT_H,
         lambda s: s.replace('#define PORTRAIT_BOX_BOTTOM_ROW 12',
                             '#define PORTRAIT_BOX_BOTTOM_ROW 14', 1)),
        ('tall art is no longer bounded', PORTRAIT_C,
         lambda s: s.replace('if (boxHeight > PORTRAIT_BOX_BOTTOM_ROW + 1)',
                             'if (FALSE)', 1)),

        # -- rule 7: the three hooks --
        ('nothing ever draws the portrait', MSGBOX_C,
         lambda s: s.replace('        RoguePortrait_Draw();\n', '', 1)),
        ('the portrait is drawn before the frame gfx load', MSGBOX_C,
         lambda s: s.replace('    case 0:\n        if (gMsgIsSignPost)',
                             '    case 0:\n        RoguePortrait_Draw();\n        if (gMsgIsSignPost)', 1)
                   .replace('        RoguePortrait_Draw();\n        task->tState++;\n        break;\n    }\n    case 2:',
                            '        task->tState++;\n        break;\n    }\n    case 2:', 1)),
        ('nothing ever tears the portrait down', MSGBOX_C,
         lambda s: s.replace('    RoguePortrait_Hide();\n', '', 1)),
        ('the field re-init leaves stale handles', MENU_C,
         lambda s: s.replace('    RoguePortrait_ResetState();\n', '', 1)),

        # -- rule 8: both follower exit points --
        ('only one of the two follower exit points sets the speaker', EVENTOBJ_C,
         lambda s: s.replace('        ObjectEventEmote(objEvent, emotion);\n'
                             '        RoguePortrait_SetSpeakerMon(mon, emotion);',
                             '        ObjectEventEmote(objEvent, emotion);', 1)),

        # -- rule 9: the speaker name is given back --
        ('the follower nickname sticks to every later message', PORTRAIT_C,
         lambda s: s.replace('        gSpeakerName = NULL;', '        (void)0;', 1)),
        ('the teardown wipes a speaker it did not set', PORTRAIT_C,
         lambda s: s.replace('    if (sPortrait.ownsSpeakerName)\n    {',
                             '    if (TRUE)\n    {', 1)),

        # -- rule 10: every emotion maps to a face --
        ('an emotion has no face and silently wears a blank one', PORTRAIT_C,
         lambda s: s.replace('[FOLLOWER_EMOTION_CURIOUS]  = PORTRAIT_FACE_INSPIRED,',
                             '', 1)),

        # -- rule 11: the decompression buffer is not the sheet size --
        ('the decompression buffer is sized to the sheet again', PORTRAIT_C,
         lambda s: s.replace('AllocZeroed(PORTRAIT_DECOMP_SIZE)',
                             'AllocZeroed(PORTRAIT_SHEET_SIZE)', 1)),
        ('PORTRAIT_DECOMP_SIZE stops counting frames', PORTRAIT_C,
         lambda s: s.replace('#define PORTRAIT_DECOMP_SIZE (MON_PIC_SIZE * MAX_MON_PIC_FRAMES)',
                             '#define PORTRAIT_DECOMP_SIZE (MON_PIC_SIZE)', 1)),

        # -- rule 11: the handles start as sentinels --
        ('the sprite handle starts at 0, which is a real sprite', PORTRAIT_C,
         lambda s: s.replace('    .spriteId = MAX_SPRITES,\n    .palIndex = 0xFF,\n',
                             '    .spriteId = 0,\n    .palIndex = 0,\n', 1)),
    ]

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / 'src').mkdir(parents=True)
        (tmp / 'include').mkdir(parents=True)
        for name, target, mutate in cases:
            mutated = dict(src)
            mutated[target] = mutate(src[target])
            if mutated[target] == src[target]:
                print('  SELFTEST INCONCLUSIVE (%s): mutation changed nothing'
                      % name)
                ok = False
                continue
            for rel, text in mutated.items():
                (tmp / rel).write_text(text, encoding='utf-8', newline='\n')
            if check(tmp):
                print('  SELFTEST FAILED (%s): the check still passed' % name)
                ok = False
            else:
                print('  selftest ok: fired on "%s"' % name)
    return ok


def main():
    args = list(sys.argv[1:])
    is_selftest = '--selftest' in args
    if is_selftest:
        args.remove('--selftest')
    # Accepts the repo both positionally and via --repo, so it cannot land on
    # the wrong side of run_all_checks.sh's hand-maintained list.
    if '--repo' in args:
        i = args.index('--repo')
        if i + 1 >= len(args):
            print('--repo needs a path')
            return 2
        repo = args[i + 1]
    else:
        repo = args[0] if args else '.'

    if is_selftest:
        print('--- selftest: breaking the rule on purpose ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1
    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
