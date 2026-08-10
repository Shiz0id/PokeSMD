"""How far can a hunter path for a given HUNT_*_MAX_NODES budget?

The reach of the hunt is not set by any radius constant -- it is set by the node
budget, and that relationship has never been measured. `maxNodes` caps CREATED
nodes in path_finding.c: once ctx->nodeCount hits it, PathNode_CreateNeighbor
silently returns, the frontier drains, and FindPathForObjectEvent returns NULL.
No warning, no partial path. The hunter simply stands still.

This ports the search faithfully enough to count nodes -- weighted A* with
PATH_FINDER_WEIGHT 1.5 over a Manhattan heuristic, four-way movement, unit step
cost -- and runs it over floors from the same generator verify_dungeon_gen.py
models, so the geometry is the real thing rather than an open field.

Reports, per budget, what fraction of reachable pairs are solvable and how far
they reach, so the constant can be chosen against a target rather than guessed.
"""
import heapq
import random
from collections import deque

W, H = 48, 48
FLOOR, WALL = ".", "#"
WEIGHT = 1.5

SHAPES = {
    "cave (default)": dict(rooms=8, rmin=5, rmax=10, corridor=1),
    "jungle": dict(rooms=12, rmin=7, rmax=13, corridor=3),
    "ocean / evergrande": dict(rooms=12, rmin=7, rmax=13, corridor=5),
}

BUDGETS = [64, 128, 256, 512, 1024, 2048]


def rooms_overlap(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return not (ax + aw + 1 <= bx or bx + bw + 1 <= ax
                or ay + ah + 1 <= by or by + bh + 1 <= ay)


def carve_corridor(grid, x0, y0, x1, y1, width):
    half = width // 2
    for x in range(min(x0, x1), max(x0, x1) + 1):
        for d in range(-half, half + 1):
            if 0 <= y0 + d < H:
                grid[y0 + d][x] = FLOOR
    for y in range(min(y0, y1), max(y0, y1) + 1):
        for d in range(-half, half + 1):
            if 0 <= x1 + d < W:
                grid[y][x1 + d] = FLOOR


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
    for i in range(1, len(rooms)):
        x0 = rooms[i - 1][0] + rooms[i - 1][2] // 2
        y0 = rooms[i - 1][1] + rooms[i - 1][3] // 2
        x1 = rooms[i][0] + rooms[i][2] // 2
        y1 = rooms[i][1] + rooms[i][3] // 2
        carve_corridor(grid, x0, y0, x1, y1, shape["corridor"])
    return grid, rooms


def open_tiles(grid):
    return [(x, y) for y in range(H) for x in range(W) if grid[y][x] == FLOOR]


def reachable_from(grid, start):
    """True BFS reachability, so unreachable pairs are excluded from the stats."""
    seen = {start}
    q = deque([start])
    while q:
        x, y = q.popleft()
        for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            n = (x + dx, y + dy)
            if (0 <= n[0] < W and 0 <= n[1] < H
                    and grid[n[1]][n[0]] == FLOOR and n not in seen):
                seen.add(n)
                q.append(n)
    return seen


def astar_nodes(grid, start, goal, max_nodes):
    """Returns nodes created, or None if the budget ran out first.

    Mirrors PathNode_CreateNeighbor: the budget caps node CREATION, and the
    search then drains rather than failing loudly.
    """
    def h(p):
        return (abs(p[0] - goal[0]) + abs(p[1] - goal[1])) * WEIGHT

    created = 1
    frontier = [(h(start), 0, start)]
    best_g = {start: 0}
    closed = set()

    while frontier:
        _, g, cur = heapq.heappop(frontier)
        if cur in closed:
            continue
        closed.add(cur)
        if cur == goal:
            return created
        for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            n = (cur[0] + dx, cur[1] + dy)
            if not (0 <= n[0] < W and 0 <= n[1] < H) or grid[n[1]][n[0]] != FLOOR:
                continue
            if n in closed:
                continue
            ng = g + 1
            if n in best_g and best_g[n] <= ng:
                continue
            if created >= max_nodes:
                continue          # budget exhausted: silently stop expanding
            created += 1
            best_g[n] = ng
            heapq.heappush(frontier, (ng + h(n), ng, n))
    return None


def main():
    rng = random.Random(20260810)
    samples = []   # (chebyshev distance, nodes needed with an unbounded budget)

    for label, shape in SHAPES.items():
        for _ in range(40):
            grid, rooms = generate(rng, shape)
            if len(rooms) < 2:
                continue
            tiles = open_tiles(grid)
            if not tiles:
                continue
            anchor = rng.choice(tiles)
            reach = reachable_from(grid, anchor)
            reach_list = sorted(reach)
            if len(reach_list) < 50:
                continue
            for _ in range(25):
                a = rng.choice(reach_list)
                b = rng.choice(reach_list)
                if a == b:
                    continue
                cheb = max(abs(a[0] - b[0]), abs(a[1] - b[1]))
                n = astar_nodes(grid, a, b, 10 ** 9)
                if n is not None:
                    samples.append((cheb, n, label))

    print(f"{len(samples)} reachable pairs across {len(SHAPES)} floor shapes\n")

    print("Budget needed vs straight-line (Chebyshev) distance:")
    print(f"{'distance':>10} {'pairs':>6} {'median':>8} {'90th':>8} {'max':>8}")
    buckets = [(1, 5), (6, 10), (11, 15), (16, 20), (21, 30), (31, 47)]
    for lo, hi in buckets:
        group = sorted(n for d, n, _ in samples if lo <= d <= hi)
        if not group:
            continue
        med = group[len(group) // 2]
        p90 = group[int(len(group) * 0.9)]
        print(f"{lo:4d}-{hi:<5d} {len(group):>6} {med:>8} {p90:>8} {group[-1]:>8}")

    print("\nFraction of reachable pairs solvable at each budget:")
    print(f"{'budget':>8} {'heap':>9} {'all':>7} "
          + " ".join(f"{lo}-{hi}".rjust(7) for lo, hi in buckets))
    for budget in BUDGETS:
        # 20 B/node buffer + 4 B queue slot + 4 B list slot, list rounded up to
        # a power of two, which for these budgets it already is.
        heap = budget * 28
        overall = sum(1 for _, n, _ in samples if n <= budget) / len(samples)
        row = []
        for lo, hi in buckets:
            group = [n for d, n, _ in samples if lo <= d <= hi]
            row.append(f"{sum(1 for n in group if n <= budget) / len(group):>7.0%}"
                       if group else "      -")
        print(f"{budget:>8} {heap // 1024:>6} KB {overall:>7.0%} " + " ".join(row))


if __name__ == "__main__":
    main()
