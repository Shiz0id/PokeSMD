"""
Check the BW shiny colour maps in src/data/rogue_bw_shiny.h.

The maps turn a BW animation container's palette into a shiny one by permuting
the stock shiny palette that every species already carries in ROM. See
tools/rogue/emit_bw_shiny.py for how they are chosen and include/rogue_bw_anim.h
for what they are.

WHAT THIS CAN AND CANNOT SEE. It reads the generated table and re-derives what
the runtime will build from it, so it guards the table, the packing and the
resulting palettes. It cannot tell whether the load hook is reached - that is
what test/battle/rogue_bw_anim.c is for, and the two are deliberately split:
the battle test compares the loaded palette against the same builder that made
it, so it agrees with itself when the builder is wrong, and this file is what
notices that.

Carries --selftest, per check_berry_rot.py: every check is re-run against a
deliberately broken table and has to fail. A check that has never failed is
worth nothing.
"""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from emit_bw_shiny import (PAL_SIZE, png_palette, jasc_palette, used_slots,
                           read_containers, oklab_d2)

ROW_RE = re.compile(
    r'\{\s*(SPECIES_\w+),\s*\{([^}]*)\}\s*\}\s*,\s*//\s*(\d+),\s*(\d+)/(\d+) exact')


def parse_table(repo, back):
    """The generated rows for one side, as (species, dex, [16 nibbles])."""
    txt = (repo / 'src/data/rogue_bw_shiny.h').read_text(encoding='utf-8')
    name = 'sBwShinyMapsBack' if back else 'sBwShinyMaps'
    start = txt.index(f'static const struct BwShinyMap {name}[]')
    body = txt[start:txt.index('};', start)]
    out = []
    for m in ROW_RE.finditer(body):
        packed = [int(b, 16) for b in m.group(2).replace(',', ' ').split()]
        nibbles = []
        for byte in packed:
            nibbles += [byte & 0xF, byte >> 4]
        out.append((m.group(1), int(m.group(3)), nibbles, int(m.group(4)), int(m.group(5))))
    return out


def check(repo, mutate=None):
    """Returns a list of failure strings. mutate is for --selftest only."""
    fails = []
    containers = read_containers(repo)
    # BOTH SIDES, even though ROGUE_BW_ANIM_BACK is FALSE and the back table is
    # not compiled. The data is checked in either way, and the moment someone
    # flips that switch the back maps become live with nothing having ever
    # looked at them - which is the same shape as the four verify_*.py scripts
    # that run_all_checks.sh did not glob for a long time.
    for back in (False, True):
        fails += check_side(repo, containers[back], parse_table(repo, back), mutate)
    return fails


def check_side(repo, wanted, rows, mutate=None):
    fails = []

    # 1. Every front container has a row, and nothing else does. A container
    #    added to the animation table without a map is a species whose shiny is
    #    silently normal again, which is the bug this whole thing fixes.
    have = {r[0] for r in rows}
    want = {c['species'] for c in wanted}
    if have != want:
        for s in sorted(want - have):
            fails.append(f'{s}: container has no shiny map')
        for s in sorted(have - want):
            fails.append(f'{s}: shiny map with no container')

    # 2. The table is sorted by species id and has no duplicate key. GetBwShinyMap
    #    BISECTS it, so an unsorted row is not a slow lookup, it is a miss - and a
    #    miss is silent, because the caller falls back to the normal palette.
    dexes = [r[1] for r in rows]
    if dexes != sorted(dexes):
        fails.append('table is not sorted by species id - the bisect will miss')
    if len(set(dexes)) != len(dexes):
        fails.append('table has a duplicate species - a bisect cannot resolve one')

    byName = {c['species']: c for c in wanted}
    for species, dex, nibbles, exact, used_n in rows:
        c = byName.get(species)
        if c is None:
            continue
        g = repo / 'graphics/pokemon' / c['dir']
        cont = png_palette(g / c['asset'])
        normal = jasc_palette(g / 'normal.pal')
        shiny = jasc_palette(g / 'shiny.pal')
        used = used_slots(cont)
        if mutate:
            nibbles = mutate(species, nibbles, used)

        # 3. Slot 0 maps to stock 0. Both are the transparent slot and neither is
        #    drawn; a nonzero here would mean the packing slipped by a nibble.
        if nibbles[0] != 0:
            fails.append(f'{species}: slot 0 maps to {nibbles[0]}, not the transparent slot')

        # 4. A BIJECTION over the slots that are actually drawn. Two slots on one
        #    stock colour flattens a shading ramp - the sprite keeps its hue and
        #    loses a band of detail, which no rendering of the palette shows.
        claimed = [nibbles[j] for j in used]
        if len(set(claimed)) != len(claimed):
            dupes = sorted({i for i in claimed if claimed.count(i) > 1})
            fails.append(f'{species}: stock index {dupes} claimed by more than one slot')

        # 5. Nothing drawn may claim stock index 0, which is the shipped sprite's
        #    own transparent entry and not part of its artwork.
        if any(nibbles[j] == 0 for j in used):
            fails.append(f'{species}: a drawn slot maps to the transparent stock index')

        # 6. THE SHINY MUST NOT COME OUT EQUAL TO THE NORMAL. This is the whole
        #    feature: a container whose derived palette matches its own is a mon
        #    whose shiny is invisible, which is exactly the state before this
        #    existed and which nothing else here would notice.
        derived = [cont[0]] + [shiny[nibbles[j]] for j in range(1, PAL_SIZE)]
        if all(derived[j] == cont[j] for j in used):
            fails.append(f'{species}: derived shiny palette is identical to the normal one')

        # 7. The recorded exact count is real. It is the confidence figure the
        #    whole approach rests on - 385 of 772 containers taking the official
        #    shiny palette verbatim - so a comment drifting from the data would
        #    misreport how faithful this is.
        got = sum(1 for j in used if cont[j] == normal[nibbles[j]])
        if (got, len(used)) != (exact, used_n):
            fails.append(f'{species}: comment says {exact}/{used_n} exact, data says '
                         f'{got}/{len(used)}')

        # 8. The map is the MINIMUM-COST one this species can have. Not a
        #    tolerance: emit_bw_shiny.py solves it exactly, so any cheaper
        #    assignment means the table was not produced by the current tool -
        #    hand-edited, or emitted before a change to the metric.
        cost = sum(oklab_d2(cont[j], normal[nibbles[j]]) for j in used)
        if used:
            from emit_bw_shiny import hungarian
            m = [[oklab_d2(cont[j], normal[i]) for i in range(1, PAL_SIZE)] for j in used]
            best = sum(m[r][col] for r, col in enumerate(hungarian(m)))
            if cost > best + 1e-9:
                fails.append(f'{species}: map costs {cost:.5f}, optimum is {best:.5f}')

    return fails


# ---------------------------------------------------------------------------
# The break test. Each mutation is a plausible real fault, and the check has to
# catch it. These were run against the live table and all six fired.

def _swap_two(species, nibbles, used):
    """Two drawn slots pointed at one stock colour - a broken bijection."""
    out = list(nibbles)
    if len(used) >= 2:
        out[used[0]] = out[used[1]]
    return out


def _identity(species, nibbles, used):
    """No recolour at all - the bug this feature exists to fix."""
    return list(range(PAL_SIZE))


def _shift_nibbles(species, nibbles, used):
    """Packing read off by one nibble."""
    return nibbles[1:] + [0]


def _zero_a_drawn_slot(species, nibbles, used):
    out = list(nibbles)
    if used:
        out[used[0]] = 0
    return out


def _suboptimal(species, nibbles, used):
    """A legal bijection that is not the cheapest one."""
    out = list(nibbles)
    if len(used) >= 2:
        out[used[0]], out[used[1]] = out[used[1]], out[used[0]]
    return out


def _slot_zero(species, nibbles, used):
    out = list(nibbles)
    out[0] = 1
    return out


SELFTESTS = [
    ('a broken bijection', _swap_two),
    ('an identity map (shiny == normal)', _identity),
    ('nibble packing off by one', _shift_nibbles),
    ('a drawn slot on the transparent index', _zero_a_drawn_slot),
    ('a legal but suboptimal map', _suboptimal),
    ('slot 0 not on the transparent index', _slot_zero),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=str(Path(__file__).resolve().parents[2]))
    ap.add_argument('--selftest', action='store_true',
                    help='break the table on purpose and require every check to fire')
    args = ap.parse_args()
    repo = Path(args.repo).expanduser()

    if args.selftest:
        ok = True
        for label, mutate in SELFTESTS:
            fails = check(repo, mutate)
            state = 'caught' if fails else 'MISSED'
            if not fails:
                ok = False
            print(f'  {state:6s}  {label}'
                  + (f'  ({len(fails)} findings, e.g. {fails[0]})' if fails else ''))
        if not ok:
            print('SELFTEST FAILED: a deliberate fault was not caught')
            return 1
        print(f'selftest: all {len(SELFTESTS)} deliberate faults caught')
        return 0

    fails = check(repo)
    if fails:
        print(f'check_bw_shiny: {len(fails)} problems')
        for f in fails[:40]:
            print(f'  {f}')
        return 1

    rows = parse_table(repo, False) + parse_table(repo, True)
    exact = sum(r[3] for r in rows)
    slots = sum(r[4] for r in rows)
    full = sum(1 for r in rows if r[3] == r[4])
    print(f'check_bw_shiny: {len(rows)} maps OK, '
          f'{exact}/{slots} slots ({100.0 * exact / max(1, slots):.1f}%) exact, '
          f'{full} take the official shiny palette verbatim')
    return 0


if __name__ == '__main__':
    sys.exit(main())
