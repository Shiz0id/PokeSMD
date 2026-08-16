"""Every dungeon theme must name its own field music.

WHY. Music lives in the MAP HEADER, and eight of the fourteen themes share
MAP_ROGUE_DUNGEON_FLOOR. So all eight played that one header's
MUS_PETALBURG_WOODS -- nine dungeons of fourteen once the woods itself is
counted, with snow, blizzard and petals sharing MUS_ABNORMAL_WEATHER on top of
that. Five distinct tracks across fourteen dungeons, and nothing anywhere said
so: the build was clean, every map was correct, and each theme's header was the
right header for its map.

A theme added later with no .music field inherits the shared map header again
and rejoins the pile, silently. That is what this checks.

It also checks the tracks are actually DISTINCT, because the point is that
dungeons sound different from each other -- a table where every row says
MUS_PETALBURG_WOODS would satisfy "every theme names its music" perfectly.

Usage:  python3 tools/rogue/check_dungeon_music.py [REPO | --repo PATH]
        python3 tools/rogue/check_dungeon_music.py --selftest
"""
import re
import sys
from pathlib import Path

SRC = 'src/rogue_dungeon.c'
SONGS = 'include/constants/songs.h'

# Themes deliberately sharing a track with another theme, and why. Anything not
# listed here must be unique.
ALLOWED_SHARING = {}

MIN_DISTINCT = 12


def fail(msg):
    print('FAIL  check_dungeon_music.py: %s' % msg)
    return False


def themes_with_music(src):
    """-> [(theme, music or None)] in table order."""
    out = []
    # COUNT excluded: sDungeonThemes[DUNGEON_THEME_COUNT] is the array's own
    # declaration, not a theme.
    blocks = [m for m in re.finditer(r'\[DUNGEON_THEME_([A-Z_]+)\] =', src)
              if m.group(1) != 'COUNT']
    for i, m in enumerate(blocks):
        end = blocks[i + 1].start() if i + 1 < len(blocks) else len(src)
        body = src[m.end():end]
        mus = re.search(r'\.music = (MUS_[A-Z_0-9]+)', body)
        out.append((m.group(1), mus.group(1) if mus else None))
    return out


def check(repo):
    repo = Path(repo)
    src = (repo / SRC).read_text(encoding='utf-8', errors='replace')
    songs = (repo / SONGS).read_text(encoding='utf-8', errors='replace')

    themes = themes_with_music(src)
    if not themes:
        return fail('no DUNGEON_THEME_* entries found in %s' % SRC)

    missing = [t for t, m in themes if m is None]
    if missing:
        return fail('%s name no music, so they fall back to the shared map '
                    'header -- which is how nine dungeons ended up playing '
                    'Petalburg Woods' % ', '.join(missing))

    unknown = [m for _, m in themes
               if not re.search(r'#define %s\b' % re.escape(m), songs)]
    if unknown:
        return fail('%s is not defined in %s' % (', '.join(sorted(set(unknown))),
                                                 SONGS))

    counts = {}
    for t, m in themes:
        counts.setdefault(m, []).append(t)
    shared = {m: ts for m, ts in counts.items()
              if len(ts) > 1 and m not in ALLOWED_SHARING}
    if shared:
        return fail('these tracks are used by more than one theme and are not '
                    'listed in ALLOWED_SHARING: %s'
                    % '; '.join('%s -> %s' % (m, ', '.join(ts))
                                for m, ts in sorted(shared.items())))

    if len(counts) < MIN_DISTINCT:
        return fail('only %d distinct tracks across %d themes (want at least '
                    '%d) -- the dungeons would sound alike'
                    % (len(counts), len(themes), MIN_DISTINCT))

    # The lookup has to be reachable, or the field is decoration.
    if 'RogueDungeon_GetLocationMusic' not in (
            repo / 'src/overworld.c').read_text(encoding='utf-8', errors='replace'):
        return fail('overworld.c does not call RogueDungeon_GetLocationMusic, '
                    'so theme->music is never read and every floor is back on '
                    'its map header')

    print('PASS  check_dungeon_music.py  (%d themes, %d distinct tracks)'
          % (len(themes), len(counts)))
    return True


def selftest(repo):
    repo = Path(repo)
    src_path = repo / SRC
    ow_path = repo / 'src/overworld.c'
    src, ow = (src_path.read_text(encoding='utf-8'),
               ow_path.read_text(encoding='utf-8'))

    cases = [
        ('a theme loses its music line', src_path,
         lambda s: s.replace('.music = MUS_SEALED_CHAMBER,', '', 1)),
        ('two themes share a track', src_path,
         lambda s: s.replace('.music = MUS_DESERT,',
                             '.music = MUS_MT_CHIMNEY,', 1)),
        ('an undefined track is named', src_path,
         lambda s: s.replace('.music = MUS_EVER_GRANDE,',
                             '.music = MUS_NOT_A_REAL_TRACK,', 1)),
        ('the lookup is never called', ow_path,
         lambda s: s.replace('RogueDungeon_GetLocationMusic', 'Unused_Music', 1)),
    ]

    ok = True
    for name, path, mutate in cases:
        original = path.read_text(encoding='utf-8')
        broken = mutate(original)
        if broken == original:
            print('  SELFTEST INCONCLUSIVE (%s): mutation changed nothing' % name)
            ok = False
            continue
        try:
            path.write_text(broken, encoding='utf-8', newline='\n')
            if check(repo):
                print('  SELFTEST FAILED (%s): the check still passed' % name)
                ok = False
            else:
                print('  selftest ok: fired on "%s"' % name)
        finally:
            path.write_text(original, encoding='utf-8', newline='\n')
    return ok


def main():
    args = list(sys.argv[1:])
    is_self = '--selftest' in args
    if is_self:
        args.remove('--selftest')
    if '--repo' in args:
        i = args.index('--repo')
        repo = args[i + 1] if i + 1 < len(args) else '.'
    else:
        repo = args[0] if args else '.'
    if is_self:
        print('--- selftest: breaking the table on purpose ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1
    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
