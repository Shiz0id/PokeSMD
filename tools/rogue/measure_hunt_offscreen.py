"""Does the off-screen pursuit actually close on the player?

StepOffScreenTowardPlayer moves the hunter while its object event does not
exist -- RemoveObjectEventIfOutsideView destroys anything outside a 19x17 window
around the player, and PathFinder needs a live ObjectEvent to path with. So a
hunter the player is successfully running from has to be moved by hand.

This is the harness that rejected the first attempt. A greedy walk (major axis,
then minor, then minor reversed, then major reversed; first non-solid wins)
closed from only 75% of starting positions -- the other 25% were not boxed in,
they oscillated forever in a local minimum, stepping into a concave wall,
sliding along it, and stepping back out. Invisible off screen, and from the
player`s side indistinguishable from the hunt being dead.

What ships instead is gradient descent on a breadth-first field flooded from the
player, which cannot have a local minimum. Same harness, 100%.

The bar is NOT reaching the player. It is reaching the spawn window, after which
the object respawns from its template and the real A* takes over.
"""
import random
import sys

W, H = 48, 48
FLOOR, WALL = ".", "#"
SPAWN_WINDOW = 16      # Chebyshev; the real window is 19x17, so this is strict
STEP_CAP = 400         # ~66 seconds at HUNT_OFFSCREEN_FRAMES 10

SHAPES = {
    "cave (default)": dict(rooms=8, rmin=5, rmax=10, corridor=1),
    "jungle": dict(rooms=12, rmin=7, rmax=13, corridor=3),
    "ocean / evergrande": dict(rooms=12, rmin=7, rmax=13, corridor=5),
}

NORTH, SOUTH, EAST, WEST = (0, -1), (0, 1), (1, 0), (-1, 0)
OPPOSITE = {NORTH: SOUTH, SOUTH: NORTH, EAST: WEST, WEST: EAST}


def rooms_overlap(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return not (ax + aw + 1 <= bx or bx + bw + 1 <= ax
                or ay + ah + 1 <= by or by + bh + 1 <= ay)


def generate(rng, shape):
    grid = [[WALL] * W for _ in range(H)]
    rooms = []
    for _ in range(shape["rooms"] * 8 + 32):
        if len(rooms) >= shape["rooms"]:
            break
        w = rng.randint(shape["rmin"], shape["rmax"])
        h = rng.randint(shape["rmin"], shape["rmax"])
        x = rng.randint(1, W - w - 2)
        y = rng.randint(1, H - h - 2)
        r = (x, y, w, h)
        if not any(rooms_overlap(r, o) for o in rooms):
            rooms.append(r)
    for (x, y, w, h) in rooms:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                grid[yy][xx] = FLOOR
    half = shape["corridor"] // 2
    for i in range(1, len(rooms)):
        x0 = rooms[i - 1][0] + rooms[i - 1][2] // 2
        y0 = rooms[i - 1][1] + rooms[i - 1][3] // 2
        x1 = rooms[i][0] + rooms[i][2] // 2
        y1 = rooms[i][1] + rooms[i][3] // 2
        for x in range(min(x0, x1), max(x0, x1) + 1):
            for d in range(-half, half + 1):
                if 0 <= y0 + d < H:
                    grid[y0 + d][x] = FLOOR
        for y in range(min(y0, y1), max(y0, y1) + 1):
            for d in range(-half, half + 1):
                if 0 <= x1 + d < W:
                    grid[y][x1 + d] = FLOOR
    return grid, rooms


def solid(grid, x, y):
    if not (0 <= x < W and 0 <= y < H):
        return True                      # MapGridGetCollisionAt returns 1 here
    return grid[y][x] == WALL


def step_order(hx, hy, px, py):
    """Exactly StepHunterOffScreen's four candidates, in order."""
    dx, dy = px - hx, py - hy
    major = EAST if dx > 0 else WEST
    minor = SOUTH if dy > 0 else NORTH
    if abs(dx) < abs(dy):
        major, minor = minor, major
    return [major, minor, OPPOSITE[minor], OPPOSITE[major]]


def bfs_field(grid, player, target):
    """StepOffScreenTowardPlayer: flood from the player, stop at the hunter."""
    from collections import deque
    dist = {player: 0}
    q = deque([player])
    while q:
        cur = q.popleft()
        if cur == target:
            break
        for dx, dy in (NORTH, SOUTH, EAST, WEST):
            n = (cur[0] + dx, cur[1] + dy)
            if n in dist or solid(grid, *n):
                continue
            dist[n] = dist[cur] + 1
            q.append(n)
    return dist


def walk(grid, start, player, cap=STEP_CAP):
    hx, hy = start
    for steps in range(1, cap + 1):
        dist = bfs_field(grid, player, (hx, hy))
        here = dist.get((hx, hy))
        if here is None:
            return None, steps            # genuinely unreachable
        moved = False
        for dx, dy in (NORTH, SOUTH, EAST, WEST):
            n = (hx + dx, hy + dy)
            if dist.get(n) == here - 1 and here - 1 != 0:
                hx, hy = n
                moved = True
                break
        if not moved:
            return None, steps
        if max(abs(hx - player[0]), abs(hy - player[1])) <= SPAWN_WINDOW:
            return steps, steps
    return None, cap


def reachable(grid, start):
    from collections import deque
    seen, q = {start}, deque([start])
    while q:
        x, y = q.popleft()
        for dx, dy in (NORTH, SOUTH, EAST, WEST):
            n = (x + dx, y + dy)
            if (0 <= n[0] < W and 0 <= n[1] < H
                    and grid[n[1]][n[0]] == FLOOR and n not in seen):
                seen.add(n)
                q.append(n)
    return seen


def main():
    rng = random.Random(20260810)
    ok, stuck, capped, steps_all = 0, 0, 0, []

    for label, shape in SHAPES.items():
        for _ in range(60):
            grid, rooms = generate(rng, shape)
            if len(rooms) < 2:
                continue
            anchor = (rooms[0][0] + rooms[0][2] // 2, rooms[0][1] + rooms[0][3] // 2)
            reach = sorted(reachable(grid, anchor))
            if len(reach) < 100:
                continue
            for _ in range(20):
                player = rng.choice(reach)
                start = rng.choice(reach)
                if max(abs(start[0] - player[0]),
                       abs(start[1] - player[1])) <= SPAWN_WINDOW:
                    continue              # already inside the window; A* owns it
                result, taken = walk(grid, start, player)
                if result is None:
                    if taken >= STEP_CAP:
                        capped += 1
                    else:
                        stuck += 1
                else:
                    ok += 1
                    steps_all.append(result)

    total = ok + stuck + capped
    if total == 0:
        print("no cases generated")
        return 1

    steps_all.sort()
    print(f"{total} off-screen pursuits, player stationary\n")
    print(f"  reached the spawn window : {ok:5d}  ({ok / total:.1%})")
    print(f"  boxed in, no legal move  : {stuck:5d}  ({stuck / total:.1%})")
    print(f"  still walking at cap     : {capped:5d}  ({capped / total:.1%})")
    if steps_all:
        print(f"\n  steps to close: median {steps_all[len(steps_all) // 2]}, "
              f"90th {steps_all[int(len(steps_all) * 0.9)]}, "
              f"max {steps_all[-1]}")
        secs = steps_all[int(len(steps_all) * 0.9)] * 10 / 60
        print(f"  at HUNT_OFFSCREEN_FRAMES 10 that 90th is ~{secs:.0f}s")
    return 0 if (stuck + capped) / total < 0.05 else 1


if __name__ == "__main__":
    sys.exit(main())
