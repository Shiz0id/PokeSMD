"""The falling weathers' Q7 accumulator, and what it must never hold again.

Every weather whose sprites are stored RELATIVE to the camera carries a Q7
fixed-point accumulator in an s16 data[] slot, and there are exactly two things
it can hold: the sprite's POSITION, or the sub-pixel REMAINDER of its motion.
Vanilla holds the position. On a vanilla-sized route that is fine and on a
48x48 dungeon floor it is not, and the difference is invisible in every table
this repo checks.

THE RULE THIS FILE GUARDS, stated once:

    tPosY holds a FRACTION, never a position. Init seeds it to 0, the updater
    adds whole pixels into sprite->y and keeps only the low 7 bits, and the
    sprite's vertical recycling is an EXPLICIT screen-space test rather than a
    side effect of the accumulator wrapping.

WHY IT IS NOT OBVIOUS. sprite->y is stored relative to gSpriteCoordOffsetY,
which coordOffsetEnabled adds back at draw time. On Route 119 that offset is
small, so `tPosY = sprite->y * 128` fits in an s16 and the code is correct. A
dungeon floor is 48x48 tiles = 768 pixels, and gTotalCameraPixelOffsetY counts
DOWN from zero as the player walks (field_camera.c), so gSpriteCoordOffsetY
reaches about -600. The seed is then 605 * 128 = 77440, which wraps an s16
before the first frame has run. sprite->y reads back as 93 instead of 605 -
about -515 in screen terms, far above the top of the screen.

WHAT THAT COSTS, measured by the simulation below rather than argued:

  - The sprite spawns ~515 rows above the screen instead of 3, so it spends
    tens of seconds travelling to somewhere it can be seen. The field thins from
    ~95% of frames on screen to ~66%.
  - The vertical respawn NEVER FIRES, because it reads the wrapped value. Every
    weather's recycling is dead: the fresh entry lane, the fresh drift and the
    re-rolled rest cycle that respawn exists to deal are simply never dealt.
  - The motion becomes a sawtooth on the accumulator's wrap period rather than
    a fall on the sprite's own speed.

None of that is a crash, none of it fails to build, and none of the other 53
checks in this directory can see it - it is sprite motion, which fails by
LOOKING wrong. So it is simulated here at real frame rates against the real
camera range, and asserted in the source as a pairing.

WHAT IT WALKS. Every Init*SpriteMovement in field_weather_effect.c that seeds
its y relative to gSpriteCoordOffsetY, paired with its Update*Sprite. The set is
DISCOVERED, not listed, so a fifth falling weather is covered the day it is
written - which is the whole point, because this defect was introduced three
times by copying the weather next to it.

Note what is deliberately NOT in scope: the rain. UpdateRainSprite sets
coordOffsetEnabled = FALSE, so its sprites are in screen space, its accumulator
is seeded from a tile index that cannot overflow, and none of this applies.

--selftest reintroduces each half of the bug in turn and confirms this fires,
because a check that has never failed is worth nothing. Each break is the
ACTUAL historical mistake rather than arbitrary corruption: the position seed,
the assigning shift, the missing mask, and the dropped respawn.

Usage:  python3 tools/rogue/check_weather_motion.py [REPO] [--repo PATH] [--selftest]

Takes the repo either positionally or as --repo, following
check_start_menu_pages.py: a check that accepts both can never end up on the
wrong side of the hand-maintained list in run_all_checks.sh.
"""
import argparse
import re
import sys
from pathlib import Path

SRC = 'src/field_weather_effect.c'

# gSpriteCoordOffsetY over a 48x48 floor. It starts at 0 at the top of the map
# and counts DOWN as the player walks toward the bottom - see
# gTotalCameraPixelOffsetY -= movementSpeedY in field_camera.c. 768px of map
# less a 160px screen is about 600px of travel.
OFFSETS = [0, -100, -200, -300, -450, -600]
C2C = -8            # centerToCornerVecY for a 16x16 weather sprite
FRAMES = 60 * 30    # thirty seconds
SCREEN_H = 160
RESPAWN_ROW = 163
SPAWN_ROW = -3

# Q7 fall speeds, read off the Init functions. Mean of each range.
#   snowflake  (rand & 3) * 5 + 64    -> 64..79
#   petal      (rand & 7) * 4 + 28    -> 28..56
#   leaf       see LEAF_FALL_*        -> slowest of the four
#   blizzard   (rand & 3) * 16 + 96   -> 96..144
FALL_SPEED = {'Snowflake': 72, 'Petal': 42, 'Leaf': 34, 'Blizzard': 120}

# Below this the field is thin enough to read as broken on screen. The fixed
# code sits at 94-96% and the position-seeded code at 63-67%, so the threshold
# is not near either.
MIN_VISIBLE_PCT = 85.0


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


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


def families(text):
    """Falling weathers, discovered by the seed that makes them vulnerable.

    A weather is in scope exactly when it stores sprite->y relative to
    gSpriteCoordOffsetY, because that is what makes y large enough for y * 128
    to overflow. Anything in screen space is immune and is not looked at.
    """
    found = []
    for m in re.finditer(r'static void Init(\w+)SpriteMovement\(struct Sprite \*sprite\)', text):
        name = m.group(1)
        init = body(text, m.group(0))
        if init is None or 'gSpriteCoordOffsetY' not in init:
            continue
        if not re.search(r'sprite->y\s*=\s*-3\s*-\s*\(gSpriteCoordOffsetY', init):
            continue
        upd_sig = 'static void Update%sSprite(struct Sprite *sprite)' % name
        upd = body(text, upd_sig)
        if upd is None:
            sys.exit('found Init%sSpriteMovement but no Update%sSprite' % (name, name))
        found.append((name, init, upd))
    return found


def source_problems(text):
    """The rule, asserted as a pairing in the source."""
    problems = []
    fams = families(text)
    if not fams:
        sys.exit('found no falling weathers at all in %s - has the seed changed?' % SRC)

    for name, init, upd in fams:
        where = '%s: ' % name

        # 1. The seed. This is the line that was wrong in three weathers.
        if re.search(r'tPosY\s*=\s*sprite->y\s*\*\s*128', init):
            problems.append(where + 'Init seeds tPosY with the POSITION '
                            '(sprite->y * 128) - it overflows an s16 on a '
                            'dungeon floor before the first frame')
        elif not re.search(r'sprite->tPosY\s*=\s*0\s*;', init):
            problems.append(where + 'Init does not seed tPosY to 0')

        # 2. Whole pixels into y, not y assigned from the accumulator.
        if re.search(r'sprite->y\s*=\s*sprite->tPosY\s*>>\s*7', upd):
            problems.append(where + 'update ASSIGNS sprite->y from tPosY '
                            '(y = tPosY >> 7) - that carries the position in '
                            'the accumulator; it must add (y += tPosY >> 7)')
        elif not re.search(r'sprite->y\s*\+=\s*sprite->tPosY\s*>>\s*7', upd):
            problems.append(where + 'update never adds whole pixels into '
                            'sprite->y from tPosY')

        # 3. Keep only the fraction, or the accumulator is a position again.
        if not re.search(r'sprite->tPosY\s*&=\s*0x7F', upd):
            problems.append(where + 'update never masks tPosY back to a '
                            'fraction (tPosY &= 0x7F) - it grows without bound '
                            'and wraps')

        # 4. Recycling has to be stated. With the accumulator holding only a
        #    fraction, sprite->y grows for as long as the sprite lives, so the
        #    overflow that used to throw it back above the screen is gone.
        if not re.search(r'sprite->y\s*\+\s*sprite->centerToCornerVecY\s*\+\s*'
                         r'gSpriteCoordOffsetY\s*>', upd):
            problems.append(where + 'update has no screen-space vertical '
                            'respawn - sprite->y now grows without bound, so '
                            'the recycling the overflow used to provide by '
                            'accident has to be explicit (and comparing '
                            'sprite->y raw is sprite space, not screen space)')
    return problems, [f[0] for f in fams]


def simulate(delta_y, offset, seed_position, assign_y, mask, respawn):
    """One sprite for FRAMES frames. Returns (visible_pct, respawns)."""
    y = SPAWN_ROW - (offset + C2C)
    t = s16(y * 128) if seed_position else 0
    visible = 0
    respawns = 0

    for _ in range(FRAMES):
        t = s16(t + delta_y)
        if assign_y:
            y = s16(t >> 7)
        else:
            y = s16(y + (t >> 7))
        if mask:
            t &= 0x7F

        screen = y + C2C + offset
        if 0 <= (screen & 0xFF) < SCREEN_H:
            visible += 1

        if respawn and screen > RESPAWN_ROW:
            respawns += 1
            y = SPAWN_ROW - (offset + C2C)
            t = s16(y * 128) if seed_position else 0

    return 100.0 * visible / FRAMES, respawns


def motion_problems(names, quiet):
    problems = []
    if not quiet:
        print('  simulated motion, %d frames per case, offsets %s'
              % (FRAMES, ' '.join(str(o) for o in OFFSETS)))
        print('  %-12s %8s %9s %10s' % ('weather', 'offset', 'on screen', 'respawns'))
    for name in names:
        delta_y = FALL_SPEED.get(name)
        if delta_y is None:
            problems.append('%s: no fall speed recorded in FALL_SPEED - add it '
                            'when adding a falling weather' % name)
            continue
        for offset in OFFSETS:
            pct, respawns = simulate(delta_y, offset, seed_position=False,
                                     assign_y=False, mask=True, respawn=True)
            if not quiet:
                print('  %-12s %8d %8.1f%% %10d' % (name, offset, pct, respawns))
            if pct < MIN_VISIBLE_PCT:
                problems.append('%s at camera offset %d is on screen only %.1f%% '
                                'of frames' % (name, offset, pct))
            if respawns == 0:
                problems.append('%s at camera offset %d never recycles in %d '
                                'frames' % (name, offset, FRAMES))
    return problems


def check(text, quiet=False):
    problems, names = source_problems(text)
    if not quiet:
        print('falling weathers found: %s' % ', '.join(names))
    if not problems:
        problems = motion_problems(names, quiet)
    return problems


def selftest(text):
    """Reintroduce each half of the real bug and confirm this fires."""
    breaks = [
        ('the position seed is back (tPosY = sprite->y * 128)',
         lambda t: t.replace('    // A FRACTION, NOT A POSITION - zero, not y * 128. '
                             'See UpdateSnowflakeSprite.\n    sprite->tPosY = 0;',
                             '    sprite->tPosY = sprite->y * 128;', 1)),
        ('the update assigns y from the accumulator (y = tPosY >> 7)',
         lambda t: t.replace('    sprite->y += sprite->tPosY >> 7;\n'
                             '    sprite->tPosY &= 0x7F;\n'
                             '    sprite->tWaveIndex += sprite->tWaveDelta;',
                             '    sprite->y = sprite->tPosY >> 7;\n'
                             '    sprite->tWaveIndex += sprite->tWaveDelta;', 1)),
        ('the fraction mask is gone (tPosY &= 0x7F)',
         lambda t: t.replace('    sprite->y += sprite->tPosY >> 7;\n'
                             '    sprite->tPosY &= 0x7F;\n'
                             '    sprite->tWaveIndex += sprite->tWaveDelta;',
                             '    sprite->y += sprite->tPosY >> 7;\n'
                             '    sprite->tWaveIndex += sprite->tWaveDelta;', 1)),
        ('the vertical respawn is gone',
         lambda t: re.sub(r'    if \(sprite->y \+ sprite->centerToCornerVecY \+ '
                          r'gSpriteCoordOffsetY > 163\)\n    \{\n.*?\n    \}\n',
                          '', t, count=1, flags=re.S)),
        ('the respawn compares in sprite space rather than screen space',
         lambda t: t.replace('if (sprite->y + sprite->centerToCornerVecY + '
                             'gSpriteCoordOffsetY > 163)',
                             'if (sprite->y > 163)', 1)),
    ]

    print('selftest: reintroducing each half of the bug in turn')
    ok = True
    for label, mutate in breaks:
        broken = mutate(text)
        if broken == text:
            print('  NOT APPLIED  %s  (the source no longer has the shape this '
                  'break edits - fix the selftest)' % label)
            ok = False
            continue
        try:
            problems = check(broken, quiet=True)
        except SystemExit as e:
            problems = ['exited: %s' % e]
        if problems:
            print('  detected  %s' % label)
            print('                -> %s' % problems[0])
        else:
            print('  MISSED    %s' % label)
            ok = False

    # And the check must still pass on the real source, or "detected" above
    # would only mean it fails on everything.
    if check(text, quiet=True):
        print('  MISSED    the unmodified source does not pass')
        ok = False
    else:
        print('  ok        the unmodified source still passes')
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('repo_pos', nargs='?', default=None,
                    help='path to the decomp repo (or use --repo)')
    ap.add_argument('--repo', default=None)
    ap.add_argument('--selftest', action='store_true')
    args = ap.parse_args()

    repo = Path(args.repo or args.repo_pos or '.')
    text = (repo / SRC).read_text(errors='replace')

    if args.selftest:
        sys.exit(0 if selftest(text) else 1)

    problems = check(text)
    if problems:
        print('\nFAIL: %d problem(s)' % len(problems))
        for p in problems:
            print('  ' + p)
        sys.exit(1)
    print('\nPASS: every falling weather carries a fraction, not a position, '
          'and recycles at every camera offset')


if __name__ == '__main__':
    main()
