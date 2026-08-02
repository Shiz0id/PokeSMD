"""
Host-side port of GenerateRogueDungeonFloor, used to check the algorithm's
invariants over many seeds before trusting it on hardware.

Checks: every floor tile is reachable from the player spawn, the spawn is on
floor, rooms never overlap, and nothing is written out of bounds.
"""
import random
from collections import deque

W, H = 48, 48
MAX_ROOMS, RMIN, RMAX = 8, 5, 10
CORRIDOR = 1
FLOOR, WALL = '.', '#'

# A theme may carve more openly than the cave, and the invariants have to hold
# for its shape too. Keyed to the roomCount/roomMin/roomMax/corridorWidth fields
# of struct RogueDungeonTheme.
SHAPES = {
    'cave (default)': dict(rooms=8, rmin=5, rmax=10, corridor=1),
    'jungle': dict(rooms=12, rmin=7, rmax=13, corridor=3),
    # The most open shape in the game, and worth its own row rather than
    # trusting the jungle's: 5-wide corridors against rooms up to 15 is where a
    # carve would start writing outside the grid if it were going to.
    # The most open shape in the game, and worth its own row rather than
    # trusting the jungle's: 5-wide corridors are where a carve would start
    # writing outside the grid if it were going to.
    #
    # Same room sizes as the jungle on purpose. Widening them to 9-15 instead
    # measured WORSE on both axes - 44.6% coverage against 49.3%, and the room
    # count collapsed from 7.9 to 5.7 - which is the size-is-the-wrong-lever
    # finding repeating exactly. Corridor width alone buys the openness.
    'ocean': dict(rooms=12, rmin=7, rmax=13, corridor=5),
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


def run_shape(name, shape):
    global MAX_ROOMS, RMIN, RMAX, CORRIDOR
    MAX_ROOMS, RMIN, RMAX, CORRIDOR = (
        shape['rooms'], shape['rmin'], shape['rmax'], shape['corridor'])

    bad = 0
    room_counts, floor_fracs, total_oob = [], [], 0
    for seed in range(2000):
        rng = random.Random(seed)
        grid, rooms, spawn, oob = generate(rng)
        total_oob += oob
        problems = check(grid, rooms, spawn)
        room_counts.append(len(rooms))
        floor_fracs.append(sum(r.count(FLOOR) for r in grid) / (W * H))
        if problems:
            bad += 1
            if bad <= 3:
                print(f'  seed {seed}: {problems}')
    print(f'== {name}  rooms<={MAX_ROOMS} size {RMIN}-{RMAX} corridor {CORRIDOR}')
    print(f'  seeds tested        : 2000')
    print(f'  seeds with problems : {bad}')
    print(f'  out-of-bounds writes: {total_oob}')
    print(f'  rooms per floor     : min {min(room_counts)}, max {max(room_counts)}, '
          f'avg {sum(room_counts)/len(room_counts):.1f}')
    print(f'  floor coverage      : min {min(floor_fracs):.1%}, max {max(floor_fracs):.1%}, '
          f'avg {sum(floor_fracs)/len(floor_fracs):.1%}')
    return bad


def main():
    global MAX_ROOMS, RMIN, RMAX, CORRIDOR
    for name, shape in SHAPES.items():
        run_shape(name, shape)
    # Back to the cave's shape so the sample floor below is the familiar one.
    MAX_ROOMS, RMIN, RMAX, CORRIDOR = 8, 5, 10, 1

    print('\nsample floor (seed 7), @ = spawn:')
    grid, rooms, spawn, _ = generate(random.Random(7))
    grid[spawn[1]][spawn[0]] = '@'
    for row in grid:
        print('  ' + ''.join(row))


if __name__ == '__main__':
    main()
