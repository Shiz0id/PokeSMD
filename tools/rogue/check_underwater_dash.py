"""Guard the underwater dash: it is a BURST, not a speed.

THE OWNERSHIP RULE, stated once, here:

    A dash burst is armed by a FRESH B press while underwater, is spent one
    tile per step, and may not be live in any other situation. Arming,
    spending and clearing are three halves of one lifetime, and each has
    exactly one site: UpdateUnderwaterDash arms and clears,
    TrySpendUnderwaterDashTile spends, and nothing else touches the counters.

WHY THIS EXISTS. Underwater used to fall through to PlayerWalkNormal, because
PlayerNotOnBikeMoving excludes PLAYER_AVATAR_FLAG_UNDERWATER from the B-dash
path. Giving it a boost is eight lines; giving it a boost that stays a boost is
not, and every way of getting it wrong builds clean and shows nothing in any
table:

  - Arm on heldKeys rather than a fresh press and the burst re-arms the frame
    its cooldown expires. The dash is then permanent with a stutter, which is
    the exact thing it was specified not to be.
  - Forget to start the cooldown when the last tile is spent and it is
    permanent with no stutter at all.
  - Forget to clear the counters when the player is not underwater and a burst
    armed on the seafloor is still live on the next map. It applies to walking,
    because PlayerNotOnBikeMoving is the same function for both.
  - Reach for PlayerRun, which is what "dash" means everywhere else in this
    file, and the sprite indexes an animation that does not exist.

That last one is the reason this is a lifetime check and not a data check. The
underwater graphics use sAnimTable_Standard, which ends at
ANIM_STD_GO_FASTEST_EAST (19, ANIM_STD_COUNT 20). ANIM_RUN_SOUTH is 20. The
player's on-foot graphics use sAnimTable_BrendanMayNormal, which carries those
entries; the underwater set does not, so PlayerRun underwater reads off the end
of the table. Nothing in a curve or an atlas can see that.

WHAT IT ASSERTS.

  1. PlayerStep ticks the dash, and does so BEFORE the frame decides on
     movement. The ordering is the whole of the "cannot carry onto land"
     guarantee: the clear runs in the same frame the player stops being
     underwater, ahead of any step that could spend a stale tile.
  2. The not-underwater path clears BOTH counters. Either one left standing
     leaks a piece of the burst across a warp.
  3. Arming reads a fresh press. UpdateUnderwaterDash may not mention heldKeys
     at all.
  4. Arming is exclusive with the cooldown tick, and is gated on there being no
     burst already live.
  5. Spending the last tile starts the cooldown, and an empty burst reports
     FALSE rather than moving the player.
  6. The underwater branch uses PlayerWalkFaster -- speed 3, spelled with the
     trailing "er" that a careless edit drops -- and never PlayerRun.
  7. The branch does not set PLAYER_AVATAR_FLAG_DASH. Thirteen things read that
     flag, and CanAwareOWESeePlayer in src/wild_encounter_ow.c makes overworld
     encounters spot a dashing player from any active distance. A movement
     boost that silently changes seafloor aggro is a different feature.
  8. The branch sits BEFORE the B-dash run block and returns, so underwater can
     never fall through into PlayerRun.
  9. The premise behind 6 still holds: both underwater graphics infos name the
     same anim table, and that table has no ANIM_RUN_* entries. If someone
     gives underwater a running animation, this is the line that says the
     constraint has moved.

Usage:  python3 tools/rogue/check_underwater_dash.py [REPO | --repo PATH]
        python3 tools/rogue/check_underwater_dash.py --selftest
"""
import re
import sys
import tempfile
from pathlib import Path

PLAYER_C = 'src/field_player_avatar.c'
GFX_H = 'src/data/object_events/object_event_graphics_info.h'
ANIMS_H = 'src/data/object_events/object_event_anims.h'

SOURCES = [PLAYER_C, GFX_H, ANIMS_H]

UPDATE_SIG = 'static void UpdateUnderwaterDash(u16 newKeys)'
SPEND_SIG = 'static bool8 TrySpendUnderwaterDashTile(void)'
STEP_SIG = 'void PlayerStep(enum Direction direction, u16 newKeys, u16 heldKeys)'
MOVING_SIG = 'static void PlayerNotOnBikeMoving(enum Direction direction, u16 heldKeys)'

UNDERWATER_IF = 'if (gPlayerAvatar.flags & PLAYER_AVATAR_FLAG_UNDERWATER)'
RUN_GATE = '&& FlagGet(FLAG_SYS_B_DASH)'

_failures = []


def fail(msg):
    _failures.append(msg)


def block_after(text, start):
    """The brace-matched block that begins at or after index `start`."""
    open_at = text.find('{', start)
    if open_at < 0:
        return None
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[open_at:i + 1]
    return None


def body_of(text, signature, what):
    # signature + the brace, so the forward declaration above does not match.
    at = text.find(signature + chr(10) + "{")
    if at < 0:
        fail('%s is missing. The dash lifetime has exactly one site for each '
             'of arm, spend and clear, and this is one of them' % what)
        return ''
    body = block_after(text, at)
    if body is None:
        fail('%s has no brace-matched body' % what)
        return ''
    return body


def strip_comments(text):
    text = re.sub("/[*].*?[*]/", " ", text, flags=re.S)
    return re.sub("//[^" + chr(10) + "]*", " ", text)


def check(repo):
    del _failures[:]
    repo = Path(repo)
    src = {}
    for rel in SOURCES:
        path = repo / rel
        if not path.exists():
            print('FAIL  check_underwater_dash.py: %s not found under %s'
                  % (rel, repo))
            return False
        src[rel] = path.read_text(encoding='utf-8', errors='replace')

    player = src[PLAYER_C]
    update = body_of(player, UPDATE_SIG, 'UpdateUnderwaterDash')
    spend = body_of(player, SPEND_SIG, 'TrySpendUnderwaterDashTile')
    step = body_of(player, STEP_SIG, 'PlayerStep')
    moving = body_of(player, MOVING_SIG, 'PlayerNotOnBikeMoving')

    # -- 1. the tick runs, and runs first --
    tick_at = step.find('UpdateUnderwaterDash(newKeys)')
    if tick_at < 0:
        fail('PlayerStep does not call UpdateUnderwaterDash(newKeys). Nothing '
             'else ticks the cooldown or clears the burst, so the dash would '
             'either never re-arm or never expire')
    else:
        move_at = step.find('MovePlayerAvatarUsingKeypadInput')
        prevent_at = step.find('gPlayerAvatar.preventStep')
        if move_at >= 0 and tick_at > move_at:
            fail('UpdateUnderwaterDash runs AFTER the frame movement in '
                 'PlayerStep. The clear has to happen before any step can '
                 'spend a tile, or the first step after surfacing spends a '
                 'burst armed underwater')
        if prevent_at >= 0 and tick_at > prevent_at:
            fail('UpdateUnderwaterDash runs inside the preventStep guard. The '
                 'cooldown then freezes for the whole of any script that '
                 'stops the player, and the dash comes back the instant the '
                 'script ends')

    # -- 2. the not-underwater path clears BOTH counters --
    guard_at = update.find('if (!(gPlayerAvatar.flags & PLAYER_AVATAR_FLAG_UNDERWATER))')
    if guard_at < 0:
        fail('UpdateUnderwaterDash has no not-underwater early return. A burst '
             'armed on the seafloor stays live on the next map, where '
             'PlayerNotOnBikeMoving is the same function')
    else:
        clear = block_after(update, guard_at) or ''
        if 'sUnderwaterDashTiles = 0' not in clear:
            fail('the not-underwater path does not clear sUnderwaterDashTiles')
        if 'sUnderwaterDashCooldown = 0' not in clear:
            fail('the not-underwater path does not clear '
                 'sUnderwaterDashCooldown, so a dash taken underwater is still '
                 'on cooldown after a warp and the next one silently will not '
                 'arm')
        if 'return' not in clear:
            fail('the not-underwater path does not return, so it falls into '
                 'the arming code with the flag clear')

    # -- 3. a FRESH press, never a held one --
    if 'newKeys & B_BUTTON' not in update:
        fail('UpdateUnderwaterDash does not arm on newKeys & B_BUTTON. A burst '
             'is one press; arming on anything else makes the dash permanent '
             'with a stutter')
    if 'heldKeys' in update:
        fail('UpdateUnderwaterDash mentions heldKeys. Holding B must not '
             're-arm the burst the frame its cooldown expires')

    # -- 4. arming is exclusive with the cooldown, and with a live burst --
    tick_dec = update.find('sUnderwaterDashCooldown--')
    arm_at = update.find('B_BUTTON')
    if tick_dec < 0:
        fail('UpdateUnderwaterDash never decrements sUnderwaterDashCooldown, '
             'so the first dash is the last one')
    elif arm_at >= 0 and tick_dec > arm_at:
        fail('the cooldown is decremented after the arming test rather than '
             'before it')
    if not re.search(r'else if \(sUnderwaterDashTiles == 0 &&', update):
        fail('arming is not an "else if (sUnderwaterDashTiles == 0 &&" on the '
             'cooldown tick. It has to be exclusive with a running cooldown '
             'AND with a burst already in flight, or B re-arms mid-dash and '
             'the burst never ends')

    # -- 5. the last tile starts the cooldown; an empty burst says so --
    if not re.search(r'if \(--sUnderwaterDashTiles == 0\)\s*\n\s*'
                     r'sUnderwaterDashCooldown = UNDERWATER_DASH_COOLDOWN;',
                     spend):
        fail('TrySpendUnderwaterDashTile does not start the cooldown when the '
             'last tile is spent. Without it the only limit on the dash is how '
             'long the player holds a direction, which is a speed, not a burst')
    if 'if (sUnderwaterDashTiles == 0)\n        return FALSE;' not in spend:
        fail('TrySpendUnderwaterDashTile does not report FALSE on an empty '
             'burst, so a spent dash keeps moving the player at speed 3')

    # -- 6/7/8. the movement branch --
    branch_at = moving.find(UNDERWATER_IF)
    if branch_at < 0:
        fail('PlayerNotOnBikeMoving has no underwater branch, so underwater '
             'falls through to PlayerWalkNormal and the dash does nothing')
        branch = ''
    else:
        branch = strip_comments(block_after(moving, branch_at) or '')
        run_at = moving.find(RUN_GATE)
        if run_at >= 0 and branch_at > run_at:
            fail('the underwater branch sits after the B-dash run block. '
                 'Underwater then reaches PlayerRun, which indexes an '
                 'ANIM_RUN_* entry the underwater anim table does not have')
        if 'return;' not in branch:
            fail('the underwater branch does not return, so it falls into the '
                 'run path as well')

    if branch:
        if 'PlayerWalkFaster(' not in branch:
            fail('the underwater branch does not call PlayerWalkFaster(). The '
                 'dash is speed 3; PlayerWalkFast() is speed 2 and is a '
                 'substring of the right answer, so this is exactly the edit '
                 'that looks correct in a diff')
        if 'TrySpendUnderwaterDashTile()' not in branch:
            fail('the underwater branch does not spend a tile, so nothing '
                 'counts the burst down')
        if 'PlayerRun(' in branch:
            fail('the underwater branch calls PlayerRun. The underwater '
                 'graphics have no running animation -- see 9 below')
        if 'PLAYER_AVATAR_FLAG_DASH' in branch:
            fail('the underwater branch sets PLAYER_AVATAR_FLAG_DASH. '
                 'CanAwareOWESeePlayer in src/wild_encounter_ow.c reads it, so '
                 'the boost would also change what spots the player on the '
                 'seafloor')

    # -- 9. the premise: no running animation underwater --
    tables = re.findall(
        r'gObjectEventGraphicsInfo_(?:Brendan|May)Underwater = \{(.*?)\};',
        src[GFX_H], re.S)
    if len(tables) != 2:
        fail('could not find both underwater graphics infos in %s' % GFX_H)
    else:
        names = set()
        for info in tables:
            m = re.search(r'\.anims\s*=\s*(\w+)', info)
            if m:
                names.add(m.group(1))
        if len(names) != 1:
            fail('the two underwater graphics infos name different anim '
                 'tables (%s), so one of them may have a running animation and '
                 'the other not' % ', '.join(sorted(names)))
        else:
            name = names.pop()
            m = re.search(re.escape(name) + r'\[\] = \{(.*?)\};',
                          src[ANIMS_H], re.S)
            if m is None:
                fail('the underwater anim table %s is not defined in %s'
                     % (name, ANIMS_H))
            elif 'ANIM_RUN' in m.group(1):
                fail('%s now HAS ANIM_RUN_* entries. That is not a bug, it is '
                     'a moved constraint: the reason the dash uses '
                     'PlayerWalkFaster instead of PlayerRun no longer holds, '
                     'so revisit the branch and this check together'
                     % name)

    if _failures:
        for msg in _failures:
            print('FAIL  check_underwater_dash.py: %s' % msg)
        return False

    print('ok    check_underwater_dash.py: the dash is armed by one fresh B '
          'press underwater, spends %s, starts its cooldown on the last tile, '
          'and cannot be live on land' % 'one tile per step')
    return True


def selftest(repo):
    repo = Path(repo)
    src = {rel: (repo / rel).read_text(encoding='utf-8', errors='replace')
           for rel in SOURCES}

    def move_tick_late(s):
        s = s.replace('    UpdateUnderwaterDash(newKeys);\n', '', 1)
        return s.replace(
            'MovePlayerAvatarUsingKeypadInput(direction, newKeys, heldKeys);',
            'MovePlayerAvatarUsingKeypadInput(direction, newKeys, heldKeys);\n'
            '                    UpdateUnderwaterDash(newKeys);', 1)

    cases = [
        ('nothing ticks the dash', PLAYER_C,
         lambda s: s.replace('    UpdateUnderwaterDash(newKeys);\n', '', 1)),
        ('the tick runs after the frame movement', PLAYER_C, move_tick_late),
        ('surfacing stops clearing the burst', PLAYER_C,
         lambda s: s.replace('        sUnderwaterDashTiles = 0;\n'
                             '        sUnderwaterDashCooldown = 0;\n',
                             '        sUnderwaterDashCooldown = 0;\n', 1)),
        ('surfacing stops clearing the cooldown', PLAYER_C,
         lambda s: s.replace('        sUnderwaterDashTiles = 0;\n'
                             '        sUnderwaterDashCooldown = 0;\n',
                             '        sUnderwaterDashTiles = 0;\n', 1)),
        ('the burst arms on a held button', PLAYER_C,
         lambda s: s.replace('(newKeys & B_BUTTON)', '(heldKeys & B_BUTTON)', 1)),
        ('arming stops being exclusive with the cooldown', PLAYER_C,
         lambda s: s.replace('    else if (sUnderwaterDashTiles == 0 &&',
                             '    if (sUnderwaterDashTiles == 0 &&', 1)),
        ('the cooldown is never ticked down', PLAYER_C,
         lambda s: s.replace('        sUnderwaterDashCooldown--;',
                             '        (void)sUnderwaterDashCooldown;', 1)),
        ('the last tile stops starting the cooldown', PLAYER_C,
         lambda s: s.replace(
             '    if (--sUnderwaterDashTiles == 0)\n'
             '        sUnderwaterDashCooldown = UNDERWATER_DASH_COOLDOWN;\n',
             '    sUnderwaterDashTiles--;\n', 1)),
        ('an empty burst keeps moving the player', PLAYER_C,
         lambda s: s.replace('    if (sUnderwaterDashTiles == 0)\n'
                             '        return FALSE;\n', '', 1)),
        ('the dash drops to speed 2', PLAYER_C,
         lambda s: s.replace('PlayerWalkFaster(direction);',
                             'PlayerWalkFast(direction);', 1)),
        ('the dash reaches for PlayerRun', PLAYER_C,
         lambda s: s.replace('            PlayerWalkFaster(direction);',
                             '            PlayerRun(direction);', 1)),
        ('the dash also sets the DASH flag', PLAYER_C,
         lambda s: s.replace('            PlayerWalkFaster(direction);',
                             '            PlayerWalkFaster(direction),'
                             ' gPlayerAvatar.flags |= PLAYER_AVATAR_FLAG_DASH;',
                             1)),
        ('the branch stops spending tiles', PLAYER_C,
         lambda s: s.replace('if (TrySpendUnderwaterDashTile())',
                             'if (TRUE)', 1)),
        ('the underwater branch is gone entirely', PLAYER_C,
         lambda s: s.replace('    ' + UNDERWATER_IF, '    if (FALSE)', 1)),
        # BOTH infos, or this fires on the "different tables" assertion and
        # the ANIM_RUN branch it is meant to exercise never runs.
        ('underwater is given a running animation', GFX_H,
         lambda s: s
         .replace('.anims = sAnimTable_Standard,' + chr(10) +
                  '    .images = sPicTable_BrendanUnderwater,',
                  '.anims = sAnimTable_BrendanMayNormal,' + chr(10) +
                  '    .images = sPicTable_BrendanUnderwater,', 1)
         .replace('.anims = sAnimTable_Standard,' + chr(10) +
                  '    .images = sPicTable_MayUnderwater,',
                  '.anims = sAnimTable_BrendanMayNormal,' + chr(10) +
                  '    .images = sPicTable_MayUnderwater,', 1)),
    ]

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        for rel in SOURCES:
            (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
        for name, target, mutate in cases:
            mutated = dict(src)
            try:
                mutated[target] = mutate(src[target])
            except Exception as err:
                print('  SELFTEST INCONCLUSIVE (%s): mutation raised %r'
                      % (name, err))
                ok = False
                continue
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
