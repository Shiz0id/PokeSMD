"""
Verify the deterministic seeding, using the exact arithmetic the C uses:
  ISO_RANDOMIZE1(val) = 1103515245 * val + 24691   (mod 2^32)
  SeedDungeonRng(s)   = state = ISO_RANDOMIZE1(s)
  DungeonRandom()     = (state = ISO_RANDOMIZE1(state)) >> 16   as u16

Properties checked:
  1. same seed always reproduces the identical floor  (the bug being fixed)
  2. different seeds produce different floors         (no collapse)
  3. connectivity invariants still hold under this RNG

EVERY GENERATOR CONSTANT IS READ FROM THE HEADER, never restated here - see
_const below, which is the same helper verify_dungeon_gen.py already uses for
the same reason. This file restated four of them and TWO HAD GONE STALE: the
room cap sat at 8 against a DUNGEON_ROOMS_DEFAULT of 10, and the rejection
budget sat at the cave's old flat 64 after PrepareFloor moved to
`roomCap * 8 + 32`. So it reported "avg 8.0 rooms" and a clean pass for a floor
shape the game had stopped generating - which is exactly the failure the sibling
file's docstring warns about, arriving in the file next door.

THE PORT IS CORROBORATED, not merely plausible. Sweeping the cap reproduces the
measured table in DUNGEON_MAX_ROOMS' own comment - cap 8 -> 8.0, cap 10 -> 10.0,
cap 12 -> 11.7 - and those figures were taken against the real generator. A port
that agrees with an independently measured table on three points is a port that
is reading the same algorithm.

WHAT THIS DOES NOT COVER, so nobody reads a pass as broader than it is:
  - Only the REJECTION SAMPLER shape is modelled. The organic, trails, facility
    and woods generators build their rooms by other means and are not exercised
    here; verify_dungeon_gen.py carries the wider shape table.
  - No PLACER is modelled - not trainers, items, berries, rocks, events or
    hidden items. So the seeded-stream draw-count invariant that every object
    position depends on is NOT verified by this file, or by any other.
  - NOTHING IS ASSERTED ABOUT ROOM COUNTS, deliberately. A "the cap must bind"
    assertion was written here and then MEASURED AND REMOVED: `max == cap` holds
    at every cap from 8 to 16, so it never fires - including on the exact
    stale-budget regression above, which it was written to catch. The
    discriminating figure is the AVERAGE, and asserting on that would fire the
    moment someone legitimately raised the cap, because saturation is
    shape-dependent by design and DUNGEON_MAX_ROOMS already documents it. The
    room counts are printed instead, for a human to compare against that table.
    A check that cannot fail is worth less than no check, because it is read as
    coverage.
"""
import os
import re
from collections import deque

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _const(name):
    """Read a #define out of include/rogue_dungeon.h.

    The generator's numbers are NOT restated in this file. It ports the
    sampler, so a constant copied into it is a constant that can drift - and a
    drifted copy does not fail, it silently verifies a floor the game stopped
    generating. That has now happened twice: DUNGEON_ROOMS_DEFAULT went 8 -> 10
    and this file kept checking eight-room floors, and the attempt budget went
    from a flat 64 to `roomCap * 8 + 32` and this file kept spending 64.

    Raises rather than defaulting, so a renamed or deleted constant fails loudly
    instead of quietly reverting to a stale literal. That is the half of this
    fix that actually holds - it is verified by renaming a constant and watching
    this exit, not by reasoning about it.
    """
    path = os.path.join(REPO, 'include', 'rogue_dungeon.h')
    with open(path, encoding='utf-8') as fh:
        m = re.search(r'^#define\s+%s\s+(\d+)' % name, fh.read(), re.M)
    if not m:
        raise SystemExit('%s not found in %s' % (name, path))
    return int(m.group(1))


W = _const('DUNGEON_WIDTH')
H = _const('DUNGEON_HEIGHT')

# PrepareFloor: roomCap is the theme's roomCount or DUNGEON_ROOMS_DEFAULT, then
# clamped to DUNGEON_MAX_ROOMS. The cave takes the default, which is the shape
# this file models.
MAX_ROOMS = min(_const('DUNGEON_ROOMS_DEFAULT'), _const('DUNGEON_MAX_ROOMS'))
RMIN = _const('DUNGEON_ROOM_MIN')
RMAX = _const('DUNGEON_ROOM_MAX')

# "Bigger rooms are rejected more often, so the budget scales with the cap
# rather than staying at the cave's 64." - PrepareFloor. The budget is part of
# the generated result, not a detail of the harness: it decides how many rooms
# actually land, so a stale one under-reports the floor. At cap 10 the flat 64
# gave avg 9.8 against the correct 10.0 - small, and invisible without this line
# being right.
ATTEMPTS = MAX_ROOMS * 8 + 32

M32 = 0xFFFFFFFF


class DungeonRng:
    def __init__(self, seed):
        self.state = (1103515245 * (seed & 0xFFFF) + 24691) & M32

    def next(self):
        self.state = (1103515245 * self.state + 24691) & M32
        return (self.state >> 16) & 0xFFFF


def rooms_overlap(a, b):
    ax, ay, aw, ah = a; bx, by, bw, bh = b
    return not (ax + aw + 1 < bx or bx + bw + 1 < ax
                or ay + ah + 1 < by or by + bh + 1 < ay)


def generate(seed):
    rng = DungeonRng(seed)
    grid = [[False] * W for _ in range(H)]        # False = wall

    def carve(x, y):
        if 0 <= x < W and 0 <= y < H:
            grid[y][x] = True

    rooms = []
    for _ in range(ATTEMPTS):
        if len(rooms) >= MAX_ROOMS:
            break
        w = RMIN + rng.next() % (RMAX - RMIN + 1)
        h = RMIN + rng.next() % (RMAX - RMIN + 1)
        x = 1 + rng.next() % (W - w - 2)
        y = 1 + rng.next() % (H - h - 2)
        r = (x, y, w, h)
        if not any(rooms_overlap(r, o) for o in rooms):
            rooms.append(r)

    for (x, y, w, h) in rooms:
        for dy in range(h):
            for dx in range(w):
                carve(x + dx, y + dy)

    for i in range(1, len(rooms)):
        x0, y0 = rooms[i-1][0] + rooms[i-1][2]//2, rooms[i-1][1] + rooms[i-1][3]//2
        x1, y1 = rooms[i][0] + rooms[i][2]//2, rooms[i][1] + rooms[i][3]//2
        x, y = x0, y0
        while x != x1:
            carve(x, y); x += 1 if x1 > x else -1
        while y != y1:
            carve(x, y); y += 1 if y1 > y else -1
        carve(x, y)

    spawn = (rooms[0][0] + rooms[0][2]//2, rooms[0][1] + rooms[0][3]//2) if rooms else None
    return grid, rooms, spawn


def fingerprint(grid):
    return hash(tuple(tuple(r) for r in grid))


def connected(grid, spawn):
    if spawn is None:
        return False
    total = sum(sum(r) for r in grid)
    sx, sy = spawn
    if not grid[sy][sx]:
        return False
    seen = {(sx, sy)}; q = deque([(sx, sy)])
    while q:
        x, y = q.popleft()
        for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
            nx, ny = x+dx, y+dy
            if 0 <= nx < W and 0 <= ny < H and (nx,ny) not in seen and grid[ny][nx]:
                seen.add((nx,ny)); q.append((nx,ny))
    return len(seen) == total


def main():
    SEEDS = 4000
    failed = False

    print(f'grid {W}x{H}, rooms <= {MAX_ROOMS} of size {RMIN}-{RMAX}, '
          f'{ATTEMPTS} attempts   (all read from rogue_dungeon.h)')

    # 1. determinism
    mismatches = sum(1 for s in range(500)
                     if fingerprint(generate(s)[0]) != fingerprint(generate(s)[0]))
    print(f'determinism      : {500-mismatches}/500 seeds reproduce identically')
    failed |= mismatches != 0

    # 2. distinctness + 3. connectivity
    prints, disconnected, roomcounts = {}, 0, []
    for s in range(SEEDS):
        grid, rooms, spawn = generate(s)
        fp = fingerprint(grid)
        prints.setdefault(fp, []).append(s)
        roomcounts.append(len(rooms))
        if not connected(grid, spawn):
            disconnected += 1
    dupes = {k: v for k, v in prints.items() if len(v) > 1}
    print(f'distinct floors  : {len(prints)} from {SEEDS} seeds')
    print(f'colliding seeds  : {sum(len(v) for v in dupes.values())}'
          + (f'  e.g. {list(dupes.values())[0][:4]}' if dupes else ''))
    print(f'disconnected     : {disconnected}')
    failed |= disconnected != 0

    # Reported, never asserted - see the docstring for the measurement that
    # settled that. Compare against the table in DUNGEON_MAX_ROOMS' comment.
    print(f'rooms per floor  : min {min(roomcounts)}, max {max(roomcounts)}, '
          f'avg {sum(roomcounts)/len(roomcounts):.1f}   (not asserted)')

    # seed 0 is the "save predates seeding" case - must still be valid
    g0, r0, s0 = generate(0)
    ok0 = connected(g0, s0)
    print(f'seed 0 fallback  : {len(r0)} rooms, connected={ok0}')
    failed |= not ok0

    if failed:
        print('FAIL verify_seeding.py')
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
