"""Guard the one-buffer-per-position rule shared by the two BW animators.

THE OWNERSHIP RULE, stated once, here:

    A battler POSITION has exactly one pixel buffer,
    gMonSpritesGfxPtr->spritesGfx[position], and both BW animators publish into
    it. Whichever of them is loading into a position must stop the other one
    FIRST -- unconditionally, and BEFORE its own table miss, because a mon or a
    trainer with no animation of its own still overwrites that buffer with its
    STOCK pic.

So the rule is a PAIRING and it is symmetric:

    RogueBwAnim_OnLoadSprite       must call RogueBwTrainerAnim_Stop
    RogueBwTrainerAnim_OnLoadPic   must call RogueBwAnim_StopForBattler

and in both, the call must sit above that function's `return` on a NULL
animation. Half the rule is not half the protection: it is a silent corruption
in one direction only, which is exactly how this shipped.

THE BUG THIS EXISTS FOR. RogueBwTrainerAnim_OnLoadPic had its eviction BELOW
the miss, so it only ever ran for the thirteen trainers that have a BW
animation. Every other trainer -- every boss outside Hoenn -- left the mon
animating, and the tick copied mon frames straight over the trainer's pic.

WHY NOTHING CAUGHT IT, and why no table is wrong. An ordinary win faints the
opponent's team first, so FreeMonSprite has already dropped every sprite latch
by the time the trainer slides in for the defeat text; nothing is animating and
the missing eviction costs nothing. It takes a win with the mons still standing
-- the battle debug's Instant Win -- to make it visible, and a DOUBLE is the
worst case because both opponent positions hold a live animation.

IT ALSO ASSERTS THE STOP FUNCTIONS ARE THE RIGHT ONES. RogueBwAnim_OnSpriteFreed
looks like it would do, and does not: it drops only the sprite latch and leaves
sBwAnim set, so the tick keeps publishing into the shared buffer. Its own
comment says so. A future edit swapping one for the other reads as a tidy-up.

Usage:  python3 tools/rogue/check_bw_buffer_eviction.py [REPO]
        python3 tools/rogue/check_bw_buffer_eviction.py --repo REPO
        python3 tools/rogue/check_bw_buffer_eviction.py --selftest
"""
import re
import shutil
import sys
import tempfile
from pathlib import Path

ANIM_C = 'src/rogue_bw_anim.c'
TRAINER_C = 'src/rogue_bw_trainer_anim.c'

# (file, function, the evictor it must call, the getter whose miss it must
#  precede, the stop function that is NOT the right one)
SITES = [
    (ANIM_C, 'RogueBwAnim_OnLoadSprite', 'RogueBwTrainerAnim_Stop',
     'GetBwAnim', None),
    (TRAINER_C, 'RogueBwTrainerAnim_OnLoadPic', 'RogueBwAnim_StopForBattler',
     'GetBwTrainerAnim', 'RogueBwAnim_OnSpriteFreed'),
]


def body_of(src, name):
    """The text of one function definition, brace-matched."""
    m = re.search(r'^\w[\w \*]*\b%s\s*\([^;{]*\)\s*\{' % re.escape(name),
                  src, re.M)
    if not m:
        return None
    depth, i = 0, m.end() - 1
    while i < len(src):
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                return src[m.end():i]
        i += 1
    return None


def check(repo):
    repo = Path(repo)
    ok = True

    for rel, fn, evictor, getter, wrong_stop in SITES:
        src = (repo / rel).read_text(encoding='utf-8', errors='replace')
        body = body_of(src, fn)
        if body is None:
            print('FAIL: %s not found in %s' % (fn, rel))
            ok = False
            continue

        evict_at = body.find('%s(battler)' % evictor)
        if evict_at < 0:
            print('FAIL: %s does not call %s; the other animator keeps '
                  'publishing into the buffer this one is about to draw from'
                  % (fn, evictor))
            ok = False
            continue

        # The miss is the return that follows the table lookup.
        m = re.search(r'%s\s*\([^;]*\);' % re.escape(getter), body)
        if not m:
            print('FAIL: %s does not call %s; the shape this rule is stated '
                  'against has changed' % (fn, getter))
            ok = False
            continue

        miss = re.search(r'if\s*\(\s*anim\s*==\s*NULL\s*\)\s*\n?\s*return\s*;',
                         body[m.end():])
        if not miss:
            print('FAIL: %s has no `anim == NULL` early return after %s'
                  % (fn, getter))
            ok = False
            continue

        miss_at = m.end() + miss.start()
        if evict_at > miss_at:
            print('FAIL: %s evicts with %s only AFTER its %s miss, so a '
                  'battler with no animation of its own never evicts -- and it '
                  'still overwrites the shared buffer with its stock pic'
                  % (fn, evictor, getter))
            ok = False

        if wrong_stop and wrong_stop in body:
            print('FAIL: %s calls %s, which drops only the sprite latch and '
                  'leaves the tick publishing into the shared buffer'
                  % (fn, wrong_stop))
            ok = False

    if ok:
        print('ok: both BW animators evict the other unconditionally, before '
              'their own table miss')
    return ok


BREAKS = [
    ('the trainer path evicts only after its miss (the bug that shipped)',
     TRAINER_C,
     "    RogueBwAnim_StopForBattler(battler);\n\n"
     "    anim = GetBwTrainerAnim(trainerPic);\n"
     "    if (anim == NULL)\n        return;\n",
     "    anim = GetBwTrainerAnim(trainerPic);\n"
     "    if (anim == NULL)\n        return;\n\n"
     "    RogueBwAnim_StopForBattler(battler);\n"),
    ('the trainer path stops evicting at all',
     TRAINER_C,
     "    RogueBwAnim_StopForBattler(battler);\n", ""),
    ('the trainer path swapped to the latch-only stop',
     TRAINER_C,
     "    RogueBwAnim_StopForBattler(battler);\n",
     "    RogueBwAnim_OnSpriteFreed(battler);\n"),
    ('the mon path stops evicting the trainer',
     ANIM_C,
     "    RogueBwTrainerAnim_Stop(battler);\n", ""),
    ('the mon path evicts only after its miss',
     ANIM_C,
     "    RogueBwTrainerAnim_Stop(battler);\n\n"
     "    // Which side the battler is on decides which table to read",
     "    // Which side the battler is on decides which table to read"),
]


def selftest(repo):
    src = Path(repo)
    ok = True
    for name, rel, old, new in BREAKS:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td) / 'repo'
            for f in (ANIM_C, TRAINER_C):
                (tmp / f).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy(src / f, tmp / f)

            target = tmp / rel
            text = target.read_text(encoding='utf-8')
            if text.count(old) != 1:
                print('  SELFTEST BROKEN: %d anchors for "%s"'
                      % (text.count(old), name))
                ok = False
                continue
            # The mon-path "after its miss" break needs the call re-inserted
            # below the miss, or it is just the "stops evicting" break again.
            text = text.replace(old, new, 1)
            if name == 'the mon path evicts only after its miss':
                text = text.replace(
                    "    if (anim == NULL)\n        return;\n",
                    "    if (anim == NULL)\n        return;\n\n"
                    "    RogueBwTrainerAnim_Stop(battler);\n", 1)
            target.write_text(text, encoding='utf-8')

            if check(tmp):
                print('  SELFTEST FAILED: check passed on "%s"' % name)
                ok = False
            else:
                print('  selftest ok: fired on "%s"' % name)
    return ok


def main():
    args = list(sys.argv[1:])

    is_selftest = '--selftest' in args
    if is_selftest:
        args.remove('--selftest')

    # Takes the repo BOTH ways on purpose -- see check_start_menu_pages.py.
    if '--repo' in args:
        i = args.index('--repo')
        if i + 1 >= len(args):
            print('--repo needs a path')
            return 2
        repo = args[i + 1]
    elif args:
        repo = args[0]
    else:
        repo = '.'

    if is_selftest:
        print('--- selftest: breaking the pairing on purpose ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1

    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
