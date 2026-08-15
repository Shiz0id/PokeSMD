"""Guard the start menu's page layout and its saved-order migration.

THE INVARIANT. The Unbound start menu draws USM_MAX_ICON_COUNT icons per page.
The entries this hack adds -- Charms, Sound, and whatever comes next -- must land
on page 2, so that page 1 stays the set a player already knows: Pokedex, Party,
Bag, PokeNav, Trainer, Save. Charms originally sat between PokeNav and Trainer,
which silently pushed SAVE onto page 2.

WHY IT NEEDS A CHECK. Adding an icon is a one-line edit in
Usm_BuildDefaultMenuItems, and putting that line in the wrong place does not fail
to build, does not crash, and does not look wrong in the source -- it just quietly
moves Save. Nothing else in the suite looks at this file.

THE SECOND HALF. items[] is sized by USM_ICO_COUNT and sits BEFORE count in
struct Usm_SavedItems, so growing the enum moves count. An old save then reads
count from the wrong byte, and a count above USM_ICO_COUNT indexes past items[]
into rogueCharms. USM_SAVED_VERSION is what makes that detectable, so this also
asserts the version is written on save, checked on load, and that the count is
clamped -- all three, because any one of them alone leaves the hole open.

Usage:  python3 tools/rogue/check_start_menu_pages.py [REPO]
        python3 tools/rogue/check_start_menu_pages.py --selftest
"""
import re
import sys
import tempfile
from pathlib import Path

MENU_C = 'src/unbound_start_menu.c'
MENU_H = 'include/constants/unbound_start_menu.h'

# Page 1, in order. These are the entries that existed before this hack added
# any of its own, minus Options -- the vanilla set is seven entries, so Options
# was already on page 2 and is not being displaced by anything.
PAGE_ONE = ['POKEDEX', 'PARTY', 'BAG', 'POKENAV', 'TRAINER', 'SAVE']

# Entries this hack added. Every one must be on page 2 or later.
ADDED = ['CHARMS', 'SOUND']


def fail(msg):
    print('FAIL  check_start_menu_pages.py: %s' % msg)
    return False


def default_order(src):
    m = re.search(r'static void Usm_BuildDefaultMenuItems\(void\)\s*\{(.*?)\n\}',
                  src, re.S)
    if not m:
        return None
    return re.findall(r'Usm_AddMenuItem\(USM_ICO_([A-Z_]+)\)', m.group(1))


def check(repo):
    repo = Path(repo)
    src = (repo / MENU_C).read_text(encoding='utf-8', errors='replace')
    hdr = (repo / MENU_H).read_text(encoding='utf-8', errors='replace')

    m = re.search(r'#define USM_MAX_ICON_COUNT\s+(\d+)', src)
    if not m:
        return fail('USM_MAX_ICON_COUNT not found in %s' % MENU_C)
    per_page = int(m.group(1))

    order = default_order(src)
    if order is None:
        return fail('Usm_BuildDefaultMenuItems not found in %s' % MENU_C)

    # 1. Page 1 is the vanilla set, in order.
    if order[:per_page] != PAGE_ONE:
        return fail('page 1 is %s, expected %s -- an added entry has displaced '
                    'one of the vanilla six' % (order[:per_page], PAGE_ONE))

    # 2. Every added entry is on page 2 or later, and is actually present.
    for name in ADDED:
        if name not in order:
            return fail('USM_ICO_%s is never added in Usm_BuildDefaultMenuItems'
                        % name)
        if order.index(name) < per_page:
            return fail('USM_ICO_%s is at index %d, which is page 1 (< %d)'
                        % (name, order.index(name), per_page))

    # 3. Every icon the default order names has a row in sUsmMenuItems, or it
    #    draws from a zeroed template and crashes on a null callback.
    for name in order:
        if ('[USM_ICO_%s] =' % name) not in src:
            return fail('USM_ICO_%s is added to the menu but has no row in '
                        'sUsmMenuItems' % name)

    # 4. The saved-order migration, all three halves.
    if 'USM_SAVED_VERSION' not in hdr:
        return fail('USM_SAVED_VERSION not defined in %s' % MENU_H)
    if 'saved->version = USM_SAVED_VERSION' not in src:
        return fail('Usm_SaveItems does not write saved->version -- a stored '
                    'arrangement would never be recognised as current')
    if 'saved->version != USM_SAVED_VERSION' not in src:
        return fail('Usm_BuildMenuItems does not check saved->version -- an old '
                    'save keeps its stale order and its shifted count')
    if 'saved->count > USM_ICO_COUNT' not in src:
        return fail('Usm_BuildMenuItems does not clamp saved->count -- a garbage '
                    'count reads past items[] into rogueCharms')

    print('PASS  check_start_menu_pages.py  (%d/page; page 1 %s; %s on page 2+)'
          % (per_page, '+'.join(PAGE_ONE), '+'.join(ADDED)))
    return True


def selftest(repo):
    """Break each thing on purpose and require the check to fire."""
    repo = Path(repo)
    src = (repo / MENU_C).read_text(encoding='utf-8', errors='replace')
    hdr = (repo / MENU_H).read_text(encoding='utf-8', errors='replace')

    cases = [
        ('charms back on page 1',
         MENU_C, lambda s: s.replace(
             '    Usm_AddMenuItem(USM_ICO_TRAINER);\n    Usm_AddMenuItem(USM_ICO_SAVE);',
             '    Usm_AddMenuItem(USM_ICO_CHARMS);\n    Usm_AddMenuItem(USM_ICO_TRAINER);\n    Usm_AddMenuItem(USM_ICO_SAVE);',
             1)),
        ('sound entry dropped',
         MENU_C, lambda s: s.replace('    Usm_AddMenuItem(USM_ICO_SOUND);\n', '', 1)),
        ('version not written on save',
         MENU_C, lambda s: s.replace('saved->version = USM_SAVED_VERSION', 'saved->count = count', 1)),
        ('version not checked on load',
         MENU_C, lambda s: s.replace('saved->version != USM_SAVED_VERSION', 'saved->count == 0xFF', 1)),
        ('count not clamped',
         MENU_C, lambda s: s.replace('saved->count > USM_ICO_COUNT', 'saved->count == 0', 1)),
        ('USM_SAVED_VERSION removed',
         MENU_H, lambda s: s.replace('USM_SAVED_VERSION', 'USM_UNUSED_MARKER')),
    ]

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / 'src').mkdir(parents=True)
        (tmp / 'include' / 'constants').mkdir(parents=True)

        for name, target, mutate in cases:
            base_c, base_h = src, hdr
            if target == MENU_C:
                base_c = mutate(src)
                if base_c == src:
                    print('  SELFTEST INCONCLUSIVE (%s): mutation changed nothing '
                          '-- the source moved under this test' % name)
                    ok = False
                    continue
            else:
                base_h = mutate(hdr)
                if base_h == hdr:
                    print('  SELFTEST INCONCLUSIVE (%s): mutation changed nothing'
                          % name)
                    ok = False
                    continue

            (tmp / MENU_C).write_text(base_c, encoding='utf-8', newline='\n')
            (tmp / MENU_H).write_text(base_h, encoding='utf-8', newline='\n')

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

    # Takes the repo BOTH ways on purpose. run_all_checks.sh keeps a hand-written
    # list of which checks want a positional path and which want --repo, and that
    # list has already drifted out of step twice. A check that accepts either can
    # never be on the wrong side of it.
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
        print('--- selftest: breaking the invariant on purpose ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1

    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
