"""Generate the Gen 1 Team Rocket mini boss parties from the FRLG source.

WHY A GENERATOR WHEN THE DATA ALREADY EXISTS. `src/data/trainers_frlg.party`
holds all 53 Rocket trainers at levels 11-54, which is exactly the set wanted --
and not one of them is reachable. `gTrainers` in src/data.c is

    #if IS_FRLG
    #include "data/trainers_frlg.h"
    #else
    #include "data/trainers.h"
    #endif

one table or the other, never both, and this is an Emerald build. So the FRLG
Rocket trainers are SOURCE MATERIAL, not trainers: no id, no flag, nothing to
reference. The trainer *pic* table is not gated the same way, which is why the
art has always been available while the data was not, and why this looked far
more finished than it was.

This reads that file and re-emits the parties under ids that exist here.

NOTHING IS SCALED, and that is the difference between this and
gen_johto_parties.py. A boss has to land on the curve at the slot it stands on,
so Johto's late teams are offset onto it. A MINI BOSS is picked BY DEPTH from a
roster sorted on level -- PickTrainerForLevel finds the band that matches the
floor -- so stock levels are not a problem to be corrected, they are the axis the
roster is indexed on. Rocket's 11-54 is valuable precisely because it is wider
at the top than Aqua and Magma's 9-43, which left the floor-75 mini boss short.

THE BLOCKS ARE COPIED VERBATIM below the id line: species, levels, IVs, moves,
items and AI all stay as FireRed authored them. The only edits are the id and
nothing else. Re-run this after any change to the FRLG source and the result
re-derives; `--check` proves trainers.party still matches what this emits.

Run:  python3 tools/rogue/gen_rocket_parties.py [REPO | --repo PATH]
      python3 tools/rogue/gen_rocket_parties.py --print   # the .party text
      python3 tools/rogue/gen_rocket_parties.py --ids     # the #define lines
      python3 tools/rogue/gen_rocket_parties.py --check   # has it drifted?
"""
import argparse
import re
import sys
from pathlib import Path

SOURCE = 'src/data/trainers_frlg.party'
TARGET = 'src/data/trainers.party'

SOURCE_CLASS = 'Class: Team Rocket Frlg'
PREFIX = 'TRAINER_ROGUE_ROCKET_'

# The first id this set claims. 907 was TRAINERS_COUNT_EMERALD before Rocket,
# i.e. one past TRAINER_ROGUE_SINNOH_BARRY. Kept here so --ids and the header
# cannot disagree about where the block starts.
FIRST_ID = 907


def blocks(text):
    """-> [(id, body)] for every === TRAINER_X === block in a .party file."""
    parts = re.split(r'^=== (TRAINER_\w+) ===$', text, flags=re.M)
    return [(parts[i], parts[i + 1]) for i in range(1, len(parts), 2)]


def rocket_source(repo):
    """The FRLG Rocket blocks, in file order, renamed for this build.

    The source ids are GRUNT, GRUNT_2..GRUNT_51, ADMIN, ADMIN_2 -- the bare
    GRUNT and bare ADMIN become _1 so the emitted set reads as one numbered
    run rather than one odd name followed by a sequence.
    """
    text = (repo / SOURCE).read_text(encoding='utf-8', errors='replace')
    out = []
    for tid, body in blocks(text):
        if SOURCE_CLASS not in body:
            continue
        tail = tid.replace('TRAINER_TEAM_ROCKET_', '')
        if tail in ('GRUNT', 'ADMIN'):
            tail += '_1'
        out.append((PREFIX + tail, body.strip('\n')))
    return out


def emit(repo):
    """The .party text to append to trainers.party."""
    return ''.join('\n=== %s ===\n%s\n' % (tid, body)
                   for tid, body in rocket_source(repo))


def emit_ids(repo):
    rows = rocket_source(repo)
    width = max(len(t) for t, _ in rows) + 1
    out = []
    for n, (tid, _) in enumerate(rows):
        out.append('#define %-*s %d\n' % (width, tid, FIRST_ID + n))
    return ''.join(out), FIRST_ID + len(rows)


def levels_of(body):
    return [int(m) for m in re.findall(r'^Level: (\d+)', body, flags=re.M)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('repo', nargs='?', default=None)
    ap.add_argument('--repo', dest='repo_kw', default=None)
    ap.add_argument('--print', action='store_true', help='emit the .party text')
    ap.add_argument('--ids', action='store_true', help='emit the #define lines')
    ap.add_argument('--check', action='store_true',
                    help='verify trainers.party still matches this')
    args = ap.parse_args()
    repo = Path(args.repo_kw or args.repo or Path(__file__).resolve().parents[2])

    if args.print:
        sys.stdout.write(emit(repo))
        return 0

    if args.ids:
        text, nxt = emit_ids(repo)
        sys.stdout.write(text)
        sys.stdout.write('\n// TRAINERS_COUNT_EMERALD becomes %d\n' % nxt)
        return 0

    rows = rocket_source(repo)

    if args.check:
        have = dict(blocks((repo / TARGET).read_text(encoding='utf-8',
                                                     errors='replace')))
        bad = 0
        for tid, body in rows:
            if tid not in have:
                print('MISSING  %s is not in %s' % (tid, TARGET))
                bad += 1
            elif have[tid].strip('\n') != body:
                print('DRIFTED  %s differs from the FRLG source it came from'
                      % tid)
                bad += 1
        if bad:
            print('\n%d of %d blocks are missing or hand-edited. Regenerate '
                  'with --print rather than editing trainers.party.'
                  % (bad, len(rows)))
            return 1
        print('ok    gen_rocket_parties.py --check: all %d Rocket blocks in '
              '%s match the FRLG source' % (len(rows), TARGET))
        return 0

    lows = [min(levels_of(b)) for _, b in rows]
    highs = [max(levels_of(b)) for _, b in rows]
    means = [sum(levels_of(b)) / len(levels_of(b)) for _, b in rows]
    print('%d Rocket trainers, ids %d-%d'
          % (len(rows), FIRST_ID, FIRST_ID + len(rows) - 1))
    print('  party mean level  %.1f to %.1f' % (min(means), max(means)))
    print('  individual mons   %d to %d' % (min(lows), max(highs)))
    print()
    band = {}
    for mean in means:
        band.setdefault(int(mean) // 10 * 10, 0)
        band[int(mean) // 10 * 10] += 1
    for lo in sorted(band):
        print('  mean %2d-%2d  %3d  %s' % (lo, lo + 9, band[lo],
                                           '#' * band[lo]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
