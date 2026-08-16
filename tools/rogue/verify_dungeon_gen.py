"""
Host-side port of GenerateRogueDungeonFloor, used to check the algorithm's
invariants over many seeds before trusting it on hardware.

Checks: every floor tile is reachable from the player spawn, the spawn is on
floor, rooms never overlap, and nothing is written out of bounds.

IT EXITS NON-ZERO WHEN ANY OF THOSE FAIL, and that was once not true. run_shape
computed a problem count, main() discarded the return value, and there was no
exit path at all - so unreachable tiles, overlapping rooms and out-of-bounds
writes were detected, printed, and then reported as PASS by run_all_checks.sh,
which decides on $?. The out-of-bounds tally was the worse half: check() never
saw it, so it could not have counted even if the return value had been read.

Run with --selftest to prove the plumbing still works. It injects each fault in
turn and requires this script to fail; that is the guard against the exact
regression above, which survived for as long as it did precisely because a
vacuous pass is indistinguishable from a real one.
"""
import argparse
import os
import random
import re
import subprocess
import sys
from collections import deque

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _const(name):
    """Read a #define out of include/rogue_dungeon.h.

    The cave's numbers are NOT restated here. This check ports the generator,
    so a constant copied into it is a constant that can drift - and a drifted
    copy does not fail, it silently verifies a floor the game stopped
    generating. That already happened once: DUNGEON_ROOMS_DEFAULT went 8 -> 10
    and this file kept checking eight-room floors, which is 70 of the run's
    115 floors going unverified while the check printed PASS.
    """
    path = os.path.join(REPO, 'include', 'rogue_dungeon.h')
    with open(path, encoding='utf-8') as fh:
        m = re.search(r'^#define\s+%s\s+(\d+)' % name, fh.read(), re.M)
    if not m:
        raise SystemExit('%s not found in %s' % (name, path))
    return int(m.group(1))


W, H = 48, 48
CAVE_ROOMS = _const('DUNGEON_ROOMS_DEFAULT')
CAVE_RMIN = _const('DUNGEON_ROOM_MIN')
CAVE_RMAX = _const('DUNGEON_ROOM_MAX')

MAX_ROOMS, RMIN, RMAX = CAVE_ROOMS, CAVE_RMIN, CAVE_RMAX
CORRIDOR = 1
FLOOR, WALL = '.', '#'

# A theme may carve more openly than the cave, and the invariants have to hold
# for its shape too. Keyed to the roomCount/roomMin/roomMax/corridorWidth fields
# of struct RogueDungeonTheme.
SHAPES = {
    # Read from the header, never restated - see _const above. This is the
    # shape of 70 of the run's 115 floors, so it is the row that matters most.
    'cave (default)': dict(rooms=CAVE_ROOMS, rmin=CAVE_RMIN, rmax=CAVE_RMAX,
                           corridor=1),
    'jungle': dict(rooms=12, rmin=7, rmax=13, corridor=3),
    # The most open shape in the game, and worth its own row rather than
    # trusting the jungle's: 5-wide corridors are where a carve would start
    # writing outside the grid if it were going to.
    #
    # Same room sizes as the jungle on purpose. Widening them to 9-15 instead
    # measured WORSE on both axes - 44.6% coverage against 49.3%, and the room
    # count collapsed from 7.9 to 5.7 - which is the size-is-the-wrong-lever
    # finding repeating exactly. Corridor width alone buys the openness.
    #
    # Ever Grande carves on exactly these numbers, so this row covers both and
    # there is no second entry. Its width is not a taste choice either: at 3
    # wide the vertical sliver fires and gTileset_EverGrande has no art for one,
    # so 5 is what removes the only composed art that theme would have needed.
    'ocean / evergrande': dict(rooms=12, rmin=7, rmax=13, corridor=5),
}


def rooms_overlap(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return not (ax + aw + 1 < bx or bx + bw + 1 < ax
                or ay + ah + 1 < by or by + bh + 1 < ay)


def generate(rng):
    grid = [[WALL] * W for _ in range(H)]
    oob = 0

    def carve(x, y):
        nonlocal oob
        if x < 0 or y < 0 or x >= W or y >= H:
            oob += 1
            return
        grid[y][x] = FLOOR

    def carve_wide(x, y):
        half = CORRIDOR // 2
        for dy in range(-half, half + 1):
            for dx in range(-half, half + 1):
                carve(x + dx, y + dy)

    rooms = []
    for _ in range(MAX_ROOMS * 8 + 32):
        if len(rooms) >= MAX_ROOMS:
            break
        w = RMIN + rng.randrange(RMAX - RMIN + 1)
        h = RMIN + rng.randrange(RMAX - RMIN + 1)
        x = 1 + rng.randrange(W - w - 2)
        y = 1 + rng.randrange(H - h - 2)
        r = (x, y, w, h)
        if not any(rooms_overlap(r, o) for o in rooms):
            rooms.append(r)

    for (x, y, w, h) in rooms:
        for dy in range(h):
            for dx in range(w):
                carve(x + dx, y + dy)

    for i in range(1, len(rooms)):
        x0, y0 = rooms[i-1][0] + rooms[i-1][2] // 2, rooms[i-1][1] + rooms[i-1][3] // 2
        x1, y1 = rooms[i][0] + rooms[i][2] // 2, rooms[i][1] + rooms[i][3] // 2
        x, y = x0, y0
        while x != x1:
            carve_wide(x, y); x += 1 if x1 > x else -1
        while y != y1:
            carve_wide(x, y); y += 1 if y1 > y else -1
        carve_wide(x, y)

    spawn = None
    if rooms:
        spawn = (rooms[0][0] + rooms[0][2] // 2, rooms[0][1] + rooms[0][3] // 2)
    return grid, rooms, spawn, oob


def check(grid, rooms, spawn):
    problems = []
    if spawn is None:
        return ['no rooms generated']
    sx, sy = spawn
    if grid[sy][sx] != FLOOR:
        problems.append('spawn is not on floor')

    total_floor = sum(row.count(FLOOR) for row in grid)
    seen = {(sx, sy)}
    q = deque([(sx, sy)])
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < W and 0 <= ny < H and (nx, ny) not in seen and grid[ny][nx] == FLOOR:
                seen.add((nx, ny)); q.append((nx, ny))
    if len(seen) != total_floor:
        problems.append(f'unreachable floor: {total_floor - len(seen)} of {total_floor} tiles')

    for i in range(len(rooms)):
        for j in range(i + 1, len(rooms)):
            if rooms_overlap(rooms[i], rooms[j]):
                problems.append(f'rooms {i} and {j} overlap')
    return problems


def run_shape(name, shape, fault=None, seeds=2000):
    global MAX_ROOMS, RMIN, RMAX, CORRIDOR
    MAX_ROOMS, RMIN, RMAX, CORRIDOR = (
        shape['rooms'], shape['rmin'], shape['rmax'], shape['corridor'])

    bad = 0
    room_counts, floor_fracs, total_oob = [], [], 0
    for seed in range(seeds):
        rng = random.Random(seed)
        grid, rooms, spawn, oob = generate(rng)
        # --selftest only. A real run passes fault=None and is unaffected.
        if fault is not None:
            oob += fault(grid)
        total_oob += oob
        problems = check(grid, rooms, spawn)
        room_counts.append(len(rooms))
        floor_fracs.append(sum(r.count(FLOOR) for r in grid) / (W * H))
        if problems:
            bad += 1
            if bad <= 3:
                print(f'  seed {seed}: {problems}')
    print(f'== {name}  rooms<={MAX_ROOMS} size {RMIN}-{RMAX} corridor {CORRIDOR}')
    print(f'  seeds tested        : {seeds}')
    print(f'  seeds with problems : {bad}')
    print(f'  out-of-bounds writes: {total_oob}')
    print(f'  rooms per floor     : min {min(room_counts)}, max {max(room_counts)}, '
          f'avg {sum(room_counts)/len(room_counts):.1f}')
    print(f'  floor coverage      : min {min(floor_fracs):.1%}, max {max(floor_fracs):.1%}, '
          f'avg {sum(floor_fracs)/len(floor_fracs):.1%}')
    # BOTH halves are returned. total_oob used to be printed and dropped, which
    # made "nothing is written out of bounds" a claim this file never enforced.
    return bad, total_oob


def _fault_unreachable(grid):
    """Punch a floor tile the spawn can never reach.

    (0, 0) is safe to use because a real floor NEVER carves row 0 or column 0:
    rooms are placed at x, y >= 1 and the widest corridor reaches at most one
    tile past a room centre. Measured over 400 seeds of all three shapes, the
    minimum carved coordinate is x=1, y=1 - checked rather than assumed, since
    a fault the generator could produce on its own would make the selftest
    pass for the wrong reason.
    """
    grid[0][0] = FLOOR
    return 0


def _fault_oob(grid):
    """As if one carve had run off the grid."""
    return 1


FAULTS = {
    'unreachable': _fault_unreachable,
    'oob': _fault_oob,
}


def _run_self(args):
    """Re-invoke this script as a SUBPROCESS and return its exit code.

    Deliberately not a call into main(). The regression this guards is that
    main() discarded run_shape's return value, so a selftest that inspected
    that return value would pass against the live bug - which is the failure
    shape this project has already paid for once, in a battle test that passed
    against the fault it was written for. The only honest question is what the
    PROCESS exits with, because that is all run_all_checks.sh reads.
    """
    proc = subprocess.run([sys.executable, os.path.abspath(__file__)] + args,
                          capture_output=True, text=True)
    return proc.returncode


def selftest():
    """Break each invariant on purpose and require the SCRIPT to fail.

    A handful of seeds is enough - the question is whether a detected problem
    reaches the exit code, not how often the generator misbehaves.
    """
    failures = []

    # A clean run must still pass, or the faults below prove nothing: a script
    # that fails unconditionally would satisfy every case after this one.
    print('== selftest: a clean run must exit 0')
    rc = _run_self(['--seeds', '20'])
    print('   exit=%d' % rc)
    if rc != 0:
        failures.append('a clean run exited %d, so the fault cases below '
                        'prove nothing' % rc)

    for name in sorted(FAULTS):
        print('== selftest: --inject %s must exit 1' % name)
        rc = _run_self(['--seeds', '20', '--inject', name])
        print('   exit=%d' % rc)
        if rc == 0:
            failures.append('--inject %s was detected but still exited 0, so '
                            'run_all_checks.sh would report PASS' % name)

    print()
    if failures:
        print('FAIL selftest: this script cannot fail when it should')
        for f in failures:
            print('  - %s' % f)
        return 1
    print('selftest OK: clean run exits 0, and every injected fault exits 1')
    return 0


def main():
    global MAX_ROOMS, RMIN, RMAX, CORRIDOR
    ap = argparse.ArgumentParser()
    ap.add_argument('--selftest', action='store_true',
                    help='inject each fault and require this script to fail')
    ap.add_argument('--seeds', type=int, default=2000,
                    help='seeds per shape (default 2000)')
    ap.add_argument('--inject', choices=sorted(FAULTS),
                    help=argparse.SUPPRESS)   # --selftest only
    # run_all_checks.sh passes --repo to everything not on its positional list.
    # This file resolves the repo from its OWN location - the constants are read
    # out of the header at import time, before any argument is parsed - so the
    # flag is accepted to keep the runner working, and a path pointing anywhere
    # else is refused rather than quietly ignored.
    ap.add_argument('--repo', default=None,
                    help='must be the tree this script lives in')
    args = ap.parse_args()

    if args.repo is not None:
        want = os.path.realpath(args.repo)
        if want != os.path.realpath(REPO):
            print('--repo %s does not match the tree this script lives in (%s).'
                  % (want, os.path.realpath(REPO)))
            print('This file reads its constants at import time and cannot '
                  'retarget; run the copy inside the repo you mean.')
            return 2

    if args.selftest:
        return selftest()

    fault = FAULTS[args.inject] if args.inject else None
    failures = []
    for name, shape in SHAPES.items():
        bad, oob = run_shape(name, shape, fault=fault, seeds=args.seeds)
        if bad:
            failures.append('%s: %d seed(s) had problems' % (name, bad))
        if oob:
            failures.append('%s: %d out-of-bounds write(s)' % (name, oob))
    # Back to the cave's shape so the sample floor below is the familiar one.
    MAX_ROOMS, RMIN, RMAX, CORRIDOR = CAVE_ROOMS, CAVE_RMIN, CAVE_RMAX, 1

    print('\nsample floor (seed 7), @ = spawn:')
    grid, rooms, spawn, _ = generate(random.Random(7))
    grid[spawn[1]][spawn[0]] = '@'
    for row in grid:
        print('  ' + ''.join(row))

    # LAST, so the verdict is the last thing on screen. run_all_checks.sh keys
    # on the exit code, not on this text.
    if failures:
        print('\nFAIL verify_dungeon_gen.py')
        for f in failures:
            print('  - %s' % f)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
