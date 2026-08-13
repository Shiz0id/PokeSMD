"""Does every battle background fill the whole 512x256 BG3, or only its left half?

THE BUG THIS EXISTS FOR. jungle_canopy and abyssal_depths shipped with art in
rows 0-111 of their 256x512 canvas and key colour everywhere below. That builds,
passes every other check, and looks CORRECT standing still - so it survived
review twice.

BG3 is declared with screenSize 1 in gBattleBgTemplates, which is 512x256, so its
4096-byte tilemap is two 32x32 screenblocks SIDE BY SIDE. The flat tile order the
converter walks therefore puts canvas rows 0-31 in the LEFT half of the
background and rows 32-63 in the RIGHT half - the canvas is not one tall image,
it is two screens stacked in a file. At BG3HOFS 0 you are looking at the left
half, which is why the omission is invisible until something scrolls.

The battle intro is what scrolls it. BattleIntroSlide1/2/3 drive REG_BG3HOFS per
scanline off gScanlineEffectRegBuffers, +DISPLAY_WIDTH down to 0 on the top half
of the screen and -DISPLAY_WIDTH up to 0 on the bottom - the two halves sliding
past each other. Every column of the 512 crosses the screen while that runs, so
an empty right half reads as a BLACK BAR sliding in at the start of every battle
on that environment.

The last 16 rows are the vertical-wrap strip. The map is 256 tall against a
160-tall screen, so a negative BG3VOFS - which is every screen-shake animation -
pulls rows 240-255 in above row 0. They cost no unique tiles, only tilemap
entries, because they repeat tiles the sheet already has.

WHAT IS CHECKED, and why it is shaped this way. "Is this region blank" cannot be
answered from map.bin alone without knowing which tile index is the blank one,
and that index is NOT 0 - it is wherever the first all-transparent tile of the
canvas happened to land during dedup, which differs per background. So the check
CALIBRATES ITSELF: rows 112-239 are key colour in all eighteen backgrounds in the
tree, vanilla and converted alike, so whatever single index fills that region is
this background's blank tile. Then the right half and the wrap strip are required
not to be made of it alone.

That is deliberately the weakest form of the claim. It does not assert the right
half matches the left, because vanilla's halves are genuinely different art while
the converter's are duplicates, and both are correct.

--selftest proves the measurement can fail, by blanking a real background's right
half in memory and confirming this reports it. A check that has never failed is
worth nothing.

Run:  python3 tools/rogue/check_battle_bg_canvas.py [--repo PATH] [--selftest]
"""
import re
import struct
import sys
from pathlib import Path

GFX_HEADER = 'src/data/graphics/battle_environment.h'

TILE = 8
SRC_W, SRC_H = 256, 512
COLS = SRC_W // TILE                        # 32
ENTRIES = COLS * (SRC_H // TILE)            # 2048

# In canvas rows. Art occupies 0-111 of each half; the message box covers the
# rest of the screen, so 112-239 is key colour in every background that exists.
ART_ROWS = 112
SLICE_ROWS = 16

def _block(first_row, rows):
    start = first_row // TILE * COLS
    return start, start + rows // TILE * COLS


LEFT_ART = _block(0, ART_ROWS)                       # entries    0- 447
CALIBRATE = _block(ART_ROWS, 240 - ART_ROWS)         # entries  448- 959
RIGHT_ART = _block(SRC_H // 2, ART_ROWS)             # entries 1024-1471
WRAP_SLICE = _block(SRC_H - SLICE_ROWS, SLICE_ROWS)  # entries 1984-2047

REGIONS = [('right half', RIGHT_ART), ('wrap slice', WRAP_SLICE)]


def tilemaps(repo):
    """-> [(name, path)] for every environment tilemap the header names.

    Parsed rather than globbed so a background added tomorrow is covered without
    anyone remembering this file exists, and so a directory left behind by a
    deleted environment is not checked.
    """
    text = (repo / GFX_HEADER).read_text(errors='replace')
    out = [(m.group(1), repo / m.group(2)) for m in re.finditer(
        r'gBattleEnvironmentTilemap_(\w+)\[\]\s*=\s*INCGFX_U32\("([^"]+)"', text)]
    if not out:
        sys.exit('parsed no tilemaps out of %s' % GFX_HEADER)
    return out


def indices(entries, span):
    """The distinct tile indices a region draws with, flip and bank bits off."""
    return {e & 0x3FF for e in entries[span[0]:span[1]]}


def inspect(entries):
    """-> (blank_index_or_None, [names of regions that are blank]).

    A region counts as empty only when the calibration region gave a single
    index to compare against. If rows 112-239 are NOT uniform this background
    does not follow the shared layout at all, and the caller says so rather than
    guessing which of its tiles means nothing.
    """
    cal = indices(entries, CALIBRATE)
    if len(cal) != 1:
        return None, []
    blank = cal.pop()
    return blank, [name for name, span in REGIONS
                   if indices(entries, span) == {blank}]


def check(name, entries):
    """-> True when this background is fine. Prints one line either way."""
    if len(entries) != ENTRIES:
        print('  %-18s %d entries, expected %d' % (name, len(entries), ENTRIES))
        return False

    blank, empty = inspect(entries)
    if blank is None:
        print('  %-18s rows %d-239 are not uniform - cannot tell blank from art'
              % (name, ART_ROWS))
        return False
    if empty:
        print('  %-18s %s empty (all tile %d) <-- black bar during the intro '
              'slide' % (name, ' and '.join(empty), blank))
        return False

    print('  %-18s left %2d, right %2d, slice %2d distinct tiles  ok'
          % (name, len(indices(entries, LEFT_ART)),
             len(indices(entries, RIGHT_ART)),
             len(indices(entries, WRAP_SLICE))))
    return True


def load(path):
    raw = path.read_bytes()
    return list(struct.unpack('<%dH' % (len(raw) // 2), raw[:len(raw) // 2 * 2]))


def selftest(repo):
    """Blank a real background's right half and confirm the check fires.

    Uses a background off the tree rather than a synthetic one, so the break test
    exercises the same calibration path as the real run: if the way blank tiles
    are identified stops working, this stops detecting the break too.
    """
    name, path = tilemaps(repo)[0]
    entries = load(path)
    blank, empty = inspect(entries)
    if blank is None or empty:
        sys.exit('selftest needs a known-good background, and %s is not one'
                 % name)

    ok = 0
    for region, span in REGIONS:
        broken = list(entries)
        broken[span[0]:span[1]] = [blank] * (span[1] - span[0])
        print('  break %s of %s:' % (region, name))
        if check(name, broken):
            sys.exit('SELFTEST FAILED: blanking the %s was not detected' % region)
        ok += 1

    print('selftest: %d/%d deliberate breaks detected' % (ok, len(REGIONS)))
    return 0


def main(argv):
    repo = Path('.')
    for i, a in enumerate(argv):
        if a == '--repo':
            repo = Path(argv[i + 1])

    if '--selftest' in argv:
        return selftest(repo)

    print('BG3 is 512x256, so canvas rows 32-63 are the RIGHT half of the '
          'background')
    failed = False
    for name, path in tilemaps(repo):
        if not path.exists():
            print('  %-18s MISSING %s' % (name, path))
            failed = True
            continue
        if not check(name, load(path)):
            failed = True

    if failed:
        raise SystemExit('a battle background does not fill BG3 - the intro '
                         'slide will show a black bar')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
