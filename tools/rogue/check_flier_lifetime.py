"""The flier weathers borrow three things they did not allocate. This is the pairing.

WEATHER_ZUBATS and WEATHER_SEABIRDS draw follower Pokemon overworld sprites
rather than a weather sheet of their own, and a seabird additionally draws a
REFLECTION of itself on the water. Between them that is three allocations per
bird that this code did not make and must still give back.

THE OWNERSHIP RULE, stated once:

    CreateObjectGraphicsSprite allocates a sprite SHEET and an OBJ PALETTE for
    the flier. CreateFlierReflection allocates a second, TINTED OBJ palette and
    NO tiles - it draws the flier's own. DestroySprite frees none of these. So
    every one must be released by hand, with the SCANNING free helpers, and only
    AFTER the sprite holding it has been destroyed.

Six things have to be true, and every one of them fails silently:

  1. THE FLIER'S FREES MUST EXIST. DestroySprite frees tiles only on the
     !usingSheet branch, and OW_GFX_COMPRESS being TRUE puts every overworld
     Pokemon sprite on the other one. Destroy a bird and stop, and you leak a
     sheet and one of sixteen OBJ palette slots per bird, per floor.

  2. THE REFLECTION'S PALETTE MUST BE FREED, AND ITS TILES MUST NOT BE. The
     reflection is a whole-struct copy that keeps the flier's sheetTileStart, so
     freeing tiles for it frees the FLIER'S tiles - out from under a bird that
     is still flying, and out from under any real follower of the same species.
     Its palette, though, is its own and nothing else will reclaim it.

  3. THEY MUST BE THE *IfUnused SCANNING VARIANTS. FreeSpriteTilesByTag and
     FreeSpritePaletteByTag free unconditionally. A player walking a Wingull of
     their own shares this exact sheet and palette; the scanning helpers see the
     follower and decline, the blind ones do not.

  4. DESTROY MUST COME BEFORE FREE, which is the opposite of the intuitive
     order. The scans in (3) count the sprite itself until DestroySprite clears
     inUse. Free-then-destroy scans, finds this very sprite holding the
     resource, declines, and then destroys it - leaking exactly as completely as
     (1) while looking correct.

  5. THE FIELDS MUST BE READ BEFORE THE DESTROY. DestroySprite calls
     ResetSprite, which zeroes the struct, so sheetTileStart, oam.paletteNum and
     usingSheet must be copied out first. Read afterwards they are 0 - and 0 is
     a real tile start and a real palette number, so this does not fail, it
     frees SOMETHING ELSE'S.

  6. TEARDOWN MUST DRAIN, AND MUST TAKE THE CLOUDS WITH IT. Fliers_Finish has to
     empty the array, and a flock declaring clouds has to destroy them too -
     CreateCloudSprites loads a sheet under GFXTAG_CLOUD and fills
     PALTAG_WEATHER_2.

AND THE REFLECTION MUST NOT ANIMATE ITSELF. AnimateSprites runs
`sprite->callback(sprite)` and THEN `AnimateSprite(sprite)`. The flier's update
copies its own oam.tileNum onto the reflection, but the reflection is a
whole-struct copy carrying the flier's real anim table - so when the loop
reaches the reflection's own slot, AnimateSprite overwrites that tileNum from an
animation nothing is steering. On screen the reflection faces whichever way the
bird was flying when it was born, for ever, and its wings beat out of step.
Vanilla's SetUpReflection points anims at gDummySpriteAnimTable ({ ANIM_END })
for exactly this reason; this adaptation dropped that line and shipped the bug.
The rule generalises: a sprite whose frames are driven from outside must not
also be animating itself.

AND THE ANCHORING, which is a different silent failure in the same functions.
The fliers are world-anchored (coordOffsetEnabled = TRUE) like every other
weather sprite in the file except the rain - screen-anchored, they hold their
screen position while the camera scrolls, which reads as the birds being glued
to the player. That part is loud and was reported on sight.

The quiet part is the HALF-CONVERTED state it leaves behind. With
coordOffsetEnabled set, sprite->x and sprite->y are world coordinates and every
comparison against a screen bound has to add gSpriteCoordOffset first. A raw
comparison is correct on a map that has not scrolled and wrong the moment the
player walks - the petals and the blizzard each shipped precisely that, and each
was found by looking at the screen rather than by any check. So: no raw
sprite->x or sprite->y may be compared in the update at all.

AND ONE DATA INVARIANT, which is not a lifetime at all but fails just as
quietly: two reflecting species must not share a reflectPalTag. The palette is
loaded once per tag and tinted from whichever bird got there first, so a shared
tag paints Pelipper's reflection in Wingull's colours - on screen, and with
nothing wrong anywhere in the build. The total of maxAloft across a flock must
also fit FLIER_MAX, or the last species simply never appears.

This is the same shape as check_bw_published_palette.py and
check_jukebox_funnels.py - a lifetime rather than a table. Nothing that checks
generated data can see any of it, and the build is clean either way.

--selftest breaks each in turn, each reproducing the actual mistake rather than
corrupting the input arbitrarily.

Usage:  python3 tools/rogue/check_flier_lifetime.py [REPO] [--repo PATH] [--selftest]

Takes the repo either positionally or as --repo, following
check_start_menu_pages.py.
"""
import argparse
import re
import sys
from pathlib import Path

SRC = 'src/field_weather_effect.c'
TABLE = 'src/field_weather.c'


def body(text, signature):
    """The body of a function, from its opening brace to the matching one."""
    m = re.search(re.escape(signature) + r'\s*\{', text)
    if not m:
        return None
    i = m.end() - 1
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    return None


def at(hay, needle):
    return hay.find(needle)


def flier_kinds(src):
    """Every RogueFlierKind row, as dicts of the fields this check cares about."""
    out = {}
    for m in re.finditer(r'static const struct RogueFlierKind (\w+)\[\]\s*=\s*\{(.*?)\n\};',
                         src, re.S):
        rows = []
        for row in re.finditer(r'\{(.*?)\n    \}', m.group(2), re.S):
            body_ = row.group(1)
            d = {}
            for field in ('graphicsId', 'maxAloft', 'reflectPalTag'):
                fm = re.search(r'\.%s\s*=\s*([A-Za-z0-9_()]+)' % field, body_)
                if fm:
                    d[field] = fm.group(1)
            if d:
                rows.append(d)
        out[m.group(1)] = rows
    return out


def check(src, table, quiet=False):
    problems = []

    create = body(src, 'static bool8 CreateFlierSprite(u32 kindIndex)')
    destroy = body(src, 'static void DestroyFlierSprite(u32 index)')
    reflect = body(src, 'static void CreateFlierReflection(struct Sprite *flier, '
                        'const struct RogueFlierKind *kind,\n                                  u32 index)')
    finish = body(src, 'static bool8 Fliers_Finish(void)')

    if create is None:
        return ['CreateFlierSprite is gone - if the flier weathers were removed, '
                'remove this check with them']
    if destroy is None:
        return ['DestroyFlierSprite is gone, but CreateFlierSprite is not: the '
                'allocation has no matching release']
    if finish is None:
        return ['Fliers_Finish is gone - nothing tears the weather down']

    if 'CreateObjectGraphicsSprite' not in create:
        problems.append(
            'CreateFlierSprite no longer calls CreateObjectGraphicsSprite. If the '
            'weather now owns its own sheet this check is obsolete; if it borrows '
            'one another way, the releases below are aimed at the wrong allocator')

    i_destroy = at(destroy, 'DestroySprite(sprite)')
    i_tiles = at(destroy, 'FieldEffectFreeTilesIfUnused')
    i_pal = at(destroy, 'FieldEffectFreePaletteIfUnused(paletteNum)')
    i_refl_destroy = at(destroy, 'DestroySprite(reflection)')
    i_refl_pal = at(destroy, 'FieldEffectFreePaletteIfUnused(reflectionPalette)')

    # --- 1. the scanning variants, checked FIRST -----------------------------
    # Swapping a scanning call for a blind one also removes the scanning call, so
    # the generic "never frees" message below would otherwise be the reported
    # diagnosis for a function that frees it twice as hard.
    blind_found = set()
    for blind, scanning in (('FreeSpriteTilesByTag', 'FieldEffectFreeTilesIfUnused'),
                            ('FreeSpritePaletteByTag', 'FieldEffectFreePaletteIfUnused')):
        if re.search(r'(?<!\w)' + blind + r'\s*\(', destroy):
            blind_found.add(scanning)
            problems.append(
                'DestroyFlierSprite calls %s, which frees unconditionally. A real '
                'follower of the same species shares this exact tag; use %s, '
                'which scans the live sprites first' % (blind, scanning))

    # --- 2. the flier's own frees exist --------------------------------------
    if i_tiles < 0 and 'FieldEffectFreeTilesIfUnused' not in blind_found:
        problems.append(
            'DestroyFlierSprite never frees the sprite SHEET. DestroySprite frees '
            'tiles only when usingSheet is FALSE, and compressed overworld '
            'graphics are always TRUE - so this leaks a sheet per bird')
    if i_pal < 0 and 'FieldEffectFreePaletteIfUnused' not in blind_found:
        problems.append(
            "DestroyFlierSprite never frees the flier's OBJ PALETTE. A species' "
            'graphicsInfo carries OBJ_EVENT_PAL_TAG_DYNAMIC, so '
            'CreateObjectGraphicsSprite took one of sixteen slots')

    # --- 3. the reflection's palette, and NOT its tiles -----------------------
    if reflect is not None:
        if i_refl_pal < 0:
            problems.append(
                "DestroyFlierSprite never frees the REFLECTION's tinted palette. "
                'CreateFlierReflection loaded one per reflecting species and '
                'nothing else will reclaim it')
        if i_refl_destroy < 0:
            problems.append(
                'DestroyFlierSprite never destroys the reflection sprite, so it '
                'outlives the bird it was mirroring')
        # The reflection keeps the flier's sheetTileStart, so freeing tiles for
        # it frees the flier's - out from under a bird that is still flying.
        if re.search(r'FieldEffectFreeTilesIfUnused\(\s*reflection', destroy):
            problems.append(
                "DestroyFlierSprite frees TILES for the reflection. The reflection "
                "is a whole-struct copy and keeps the flier's sheetTileStart, so "
                "this frees the FLIER'S sheet - and any real follower's with it")
        if i_refl_pal >= 0 and i_refl_destroy >= 0 and i_refl_pal < i_refl_destroy:
            problems.append(
                "DestroyFlierSprite frees the reflection's palette BEFORE "
                'destroying it, so the scan counts the reflection itself, '
                'declines, and leaks')

    # --- 4. destroy before free ----------------------------------------------
    if i_destroy < 0:
        problems.append('DestroyFlierSprite never calls DestroySprite(sprite)')
    else:
        for label, idx in (('tiles', i_tiles), ('palette', i_pal)):
            if idx >= 0 and idx < i_destroy:
                problems.append(
                    'DestroyFlierSprite frees the %s BEFORE DestroySprite. The '
                    'scan counts this sprite while it is still inUse, declines, '
                    'and leaks - free-then-destroy looks right and frees nothing'
                    % label)

    # --- 5. the fields are read before the destroy wipes them ----------------
    for field, what in (('sheetTileStart', 'tile start'),
                        ('oam.paletteNum', 'palette number'),
                        ('usingSheet', 'usingSheet flag')):
        idx = at(destroy, field)
        if idx < 0:
            problems.append('DestroyFlierSprite never reads %s' % field)
        elif i_destroy >= 0 and idx > i_destroy:
            problems.append(
                'DestroyFlierSprite reads the %s AFTER DestroySprite, which calls '
                "ResetSprite and zeroes the struct. It reads 0, and 0 is a valid "
                "index - so this frees something else's rather than failing" % what)

    # --- 6. teardown drains, and takes the clouds ----------------------------
    if 'DestroyFlierSprite' not in finish:
        problems.append(
            'Fliers_Finish does not call DestroyFlierSprite, so every bird alive '
            'when the weather ends is leaked with its sheet and palettes')
    elif not re.search(r'while\s*\(\s*gWeatherPtr->snowflakeSpriteCount\s*\)', finish):
        problems.append(
            'Fliers_Finish no longer drains the array to empty. Removing a fixed '
            'number, or one per call, leaves survivors')
    if 'DestroyCloudSprites' not in finish:
        problems.append(
            'Fliers_Finish never destroys the clouds. A flock declaring clouds '
            'loaded a sheet under GFXTAG_CLOUD and filled PALTAG_WEATHER_2')

    # --- the teardown has to be reachable for BOTH weathers ------------------
    for weather, fn in (('WEATHER_ZUBATS', 'Zubats_Finish'),
                        ('WEATHER_SEABIRDS', 'Seabirds_Finish')):
        row = re.search(r'\[%s\]\s*=\s*\{([^}]*)\}' % weather, table)
        if not row:
            problems.append('no sWeatherFuncs row for %s - the weather cannot run, '
                            'let alone tear down' % weather)
        elif fn not in row.group(1):
            problems.append(
                "sWeatherFuncs' %s row does not name %s, so nothing frees its "
                'sheets and palettes when the weather ends' % (weather, fn))

    # --- the reflection must not animate itself ------------------------------
    if reflect is not None:
        # THE STATEMENT, not the string. `'gDummySpriteAnimTable' in reflect`
        # was the first version and it is satisfied by the COMMENT above the
        # assignment, which names the table to explain why it is there - so
        # deleting the actual line left this check passing on prose.
        if not re.search(r'reflection->anims\s*=\s*gDummySpriteAnimTable', reflect):
            problems.append(
                "CreateFlierReflection does not point the reflection's anims at "
                'gDummySpriteAnimTable. It is a whole-struct copy, so it still '
                "carries the flier's real anim table - and AnimateSprite runs "
                'AFTER the callback that copies the frame across, so it '
                'overwrites it from an animation nothing steers. The reflection '
                'then keeps the facing it was born with and flaps out of sync')
        # Copying the frame across is the other half of the same pairing.
        upd_refl = body(src, 'static void UpdateFlierReflection(struct Sprite *flier, '
                             'struct Sprite *reflection,\n                                  '
                             'const struct RogueFlierKind *kind)')
        if upd_refl is not None and 'oam.tileNum = flier->oam.tileNum' not in upd_refl:
            problems.append(
                'UpdateFlierReflection never copies the flier\'s oam.tileNum, so '
                'with its own animation neutered the reflection would hold one '
                'frame for ever')

    # --- the anchoring, and the half-converted state it can leave -----------
    update = body(src, 'static void UpdateFlierSprite(struct Sprite *sprite)')
    if update is None:
        problems.append('UpdateFlierSprite is gone')
    else:
        if not re.search(r'sprite->coordOffsetEnabled\s*=\s*TRUE', create):
            problems.append(
                'CreateFlierSprite does not set coordOffsetEnabled = TRUE, so the '
                'fliers are in SCREEN space and hold their screen position while '
                'the camera scrolls - they move with the player. Every weather '
                'sprite in the file except the rain is world-anchored')

        # A raw sprite->x / sprite->y in a COMPARISON is the half-converted bug.
        # Assignment and accumulation are fine and are how the bird actually
        # moves; it is only the tests against screen bounds that must convert.
        for axis in ('x', 'y'):
            for m in re.finditer(r'sprite->%s\s*(<|>)[^=]' % axis, update):
                problems.append(
                    'UpdateFlierSprite compares sprite->%s raw ("%s"). With '
                    'coordOffsetEnabled set that is a WORLD coordinate, and every '
                    'screen bound has to add gSpriteCoordOffset%s first - raw it '
                    'is right only until the player walks'
                    % (axis, m.group(0).strip(), axis.upper()))

        if 'gSpriteCoordOffsetX' not in update or 'gSpriteCoordOffsetY' not in update:
            problems.append(
                'UpdateFlierSprite never converts to screen space at all - it '
                'needs both gSpriteCoordOffsetX and gSpriteCoordOffsetY to test '
                'its bounds and its flight band')

        # A bird can now be carried off-screen by the PLAYER while still
        # entering or wandering, so the range test cannot live inside the LEAVE
        # case any more.
        gone = update.find('FLIER_GONE_LEFT')
        switch = update.find('switch (FLIER_PHASE(sprite))')
        if gone >= 0 and switch >= 0 and gone > switch:
            problems.append(
                'the FLIER_GONE range test is inside the phase switch. World-'
                'anchored, the PLAYER can carry the screen away from a bird that '
                'is still entering or wandering; reachable only from LEAVE, that '
                'bird flies on for ever holding a sprite slot and a palette')

    # --- the data invariant --------------------------------------------------
    kinds = flier_kinds(src)
    if not kinds:
        problems.append('found no RogueFlierKind tables at all - has the flock '
                        'table been renamed?')
    flier_max = re.search(r'#define FLIER_MAX\s+(\d+)', src)
    flier_max = int(flier_max.group(1)) if flier_max else None

    for name, rows in kinds.items():
        tags = [r.get('reflectPalTag') for r in rows
                if r.get('reflectPalTag') not in (None, 'TAG_NONE')]
        dupes = {t for t in tags if tags.count(t) > 1}
        for t in sorted(dupes):
            problems.append(
                '%s: two reflecting species share reflectPalTag %s. The palette is '
                'loaded once per tag and tinted from whichever bird got there '
                "first, so one species' reflection wears the other's colours"
                % (name, t))
        if flier_max is not None:
            total = sum(int(r['maxAloft']) for r in rows if 'maxAloft' in r)
            if total > flier_max:
                problems.append(
                    '%s: maxAloft totals %d across the flock but FLIER_MAX is %d, '
                    'so the last species simply never appears'
                    % (name, total, flier_max))

    if not quiet and not problems:
        print('CreateFlierSprite      -> CreateObjectGraphicsSprite (sheet + dynamic palette)')
        print('CreateFlierReflection  -> tinted palette, and NO tiles of its own')
        print('DestroyFlierSprite     -> reflection destroyed then its palette freed;')
        print('                          then DestroySprite, then tiles, then palette')
        print('Fliers_Finish          -> drains the array, and destroys the clouds')
        print('sWeatherFuncs          -> both flier weathers name their Finish')
        for name, rows in sorted(kinds.items()):
            print('%-22s -> %d species, maxAloft %d/%s, %d reflecting'
                  % (name, len(rows),
                     sum(int(r['maxAloft']) for r in rows if 'maxAloft' in r),
                     flier_max,
                     len([r for r in rows if r.get('reflectPalTag') not in (None, 'TAG_NONE')])))
    return problems


def selftest(src, table):
    breaks = [
        ("the flier's sheet is never freed",
         lambda s, t: (re.sub(r'\n *if \(usingSheet\)\n *FieldEffectFreeTilesIfUnused\(tileStart\);',
                              '', s, count=1), t)),
        ("the flier's palette is never freed",
         lambda s, t: (s.replace('    FieldEffectFreePaletteIfUnused(paletteNum);\n', '', 1), t)),
        ("the reflection's palette is never freed",
         lambda s, t: (s.replace('        FieldEffectFreePaletteIfUnused(reflectionPalette);\n',
                                 '', 1), t)),
        ("tiles are freed for the reflection (frees the flier's sheet)",
         lambda s, t: (s.replace('        DestroySprite(reflection);\n',
                                 '        DestroySprite(reflection);\n'
                                 '        FieldEffectFreeTilesIfUnused(reflection->sheetTileStart);\n',
                                 1), t)),
        ('the blind free is back (clobbers a real follower)',
         lambda s, t: (s.replace('FieldEffectFreeTilesIfUnused(tileStart);',
                                 'FreeSpriteTilesByTag(tileStart);', 1), t)),
        ('free before destroy (scan sees the sprite, frees nothing)',
         lambda s, t: (_destroy_after_frees(s), t)),
        ('Fliers_Finish no longer drains the array',
         lambda s, t: (re.sub(r'while \(gWeatherPtr->snowflakeSpriteCount\)\n'
                              r' *DestroyFlierSprite\(gWeatherPtr->snowflakeSpriteCount - 1\);',
                              'if (gWeatherPtr->snowflakeSpriteCount)\n'
                              '        DestroyFlierSprite(0);', s, count=1), t)),
        ('the clouds are left loaded at teardown',
         lambda s, t: (re.sub(r'\n *if \(sFlierFlock != NULL && sFlierFlock->clouds\)\n'
                              r' *DestroyCloudSprites\(\);\n', '\n', s, count=1), t)),
        ('two reflecting species share a palette tag',
         lambda s, t: (s.replace('.reflectPalTag = PALTAG_FLIER_REFLECTION_2,',
                                 '.reflectPalTag = PALTAG_FLIER_REFLECTION_1,', 1), t)),
        ('the flock asks for more birds than FLIER_MAX',
         lambda s, t: (s.replace('.maxAloft = 4, .spawnDelay = 95,',
                                 '.maxAloft = 12, .spawnDelay = 95,', 1), t)),
        ('the reflection animates itself (keeps its birth facing)',
         lambda s, t: (s.replace('    reflection->anims = gDummySpriteAnimTable;\n', '', 1), t)),
        ('the reflection stops being told which frame to draw',
         lambda s, t: (s.replace('    reflection->oam.tileNum = flier->oam.tileNum;   // the flier\'s own tiles\n',
                                 '', 1), t)),
        ('the fliers go back to screen space (they follow the player)',
         # ANCHORED ON THE LINE AFTER IT, because the bare statement is a
         # SUBSTRING of the clouds' own twelve-space-indented copy earlier in
         # the file, and str.replace is substring matching - the first version
         # of this break silently mutated CreateCloudSprites and left the
         # fliers intact. The check then passed and this reported MISSED,
         # which is the only reason it was noticed.
         lambda s, t: (s.replace('    sprite->coordOffsetEnabled = TRUE;\n'
                                 '    FLIER_SET_KIND(sprite, kindIndex);',
                                 '    FLIER_SET_KIND(sprite, kindIndex);', 1), t)),
        ('a bound test is left unconverted (right until the player walks)',
         lambda s, t: (s.replace('if (screenX > FLIER_ONSCREEN_LEFT && screenX < FLIER_ONSCREEN_RIGHT)',
                                 'if (sprite->x > FLIER_ONSCREEN_LEFT && sprite->x < FLIER_ONSCREEN_RIGHT)',
                                 1), t)),
        ('a weather row loses its Finish',
         lambda s, t: (s, t.replace('Seabirds_InitAll,      Seabirds_Finish',
                                    'Seabirds_InitAll,      None_Finish', 1))),
    ]

    print('selftest: breaking each half of the ownership rule in turn')
    ok = True
    for label, mutate in breaks:
        bsrc, btable = mutate(src, table)
        if bsrc == src and btable == table:
            print('  NOT APPLIED  %s  (source no longer has the shape this break '
                  'edits - fix the selftest)' % label)
            ok = False
            continue
        problems = check(bsrc, btable, quiet=True)
        if problems:
            print('  detected  %s' % label)
            print('                -> %s' % problems[0])
        else:
            print('  MISSED    %s' % label)
            ok = False

    if check(src, table, quiet=True):
        print('  MISSED    the unmodified source does not pass')
        for p in check(src, table, quiet=True):
            print('              ' + p)
        ok = False
    else:
        print('  ok        the unmodified source still passes')
    return ok


def _destroy_after_frees(s):
    """Move DestroySprite(sprite) below the frees, reproducing the ordering bug."""
    m = re.search(r'( *)DestroySprite\(sprite\);\n', s)
    if not m:
        return s
    s2 = s[:m.start()] + s[m.end():]
    m2 = re.search(r'( *)FieldEffectFreePaletteIfUnused\(paletteNum\);\n', s2)
    if not m2:
        return s
    return s2[:m2.end()] + m2.group(1) + 'DestroySprite(sprite);\n' + s2[m2.end():]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('repo_pos', nargs='?', default=None)
    ap.add_argument('--repo', default=None)
    ap.add_argument('--selftest', action='store_true')
    args = ap.parse_args()

    repo = Path(args.repo or args.repo_pos or '.')
    src = (repo / SRC).read_text(errors='replace')
    table = (repo / TABLE).read_text(errors='replace')

    if args.selftest:
        sys.exit(0 if selftest(src, table) else 1)

    problems = check(src, table)
    if problems:
        print('\nFAIL: %d problem(s)' % len(problems))
        for p in problems:
            print('  ' + p)
        sys.exit(1)
    print('\nPASS: every resource the flier weathers borrow is released, in the '
          'right order')


if __name__ == '__main__':
    main()
