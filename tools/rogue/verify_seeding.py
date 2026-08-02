"""
Verify the deterministic seeding, using the exact arithmetic the C uses:
  ISO_RANDOMIZE1(val) = 1103515245 * val + 24691   (mod 2^32)
  DungeonRandom()     = (state = ISO_RANDOMIZE1(state)) >> 16   as u16

Properties checked:
  1. same seed always reproduces the identical floor  (the bug being fixed)
  2. different seeds produce different floors         (no collapse)
  3. connectivity invariants still hold under this RNG
"""
from collections import deque

W, H = 48, 48
MAX_ROOMS, RMIN, RMAX = 8, 5, 10
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
    for _ in range(64):
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

    # 1. determinism
    mismatches = sum(1 for s in range(500)
                     if fingerprint(generate(s)[0]) != fingerprint(generate(s)[0]))
    print(f'determinism      : {500-mismatches}/500 seeds reproduce identically')

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
    print(f'rooms per floor  : min {min(roomcounts)}, max {max(roomcounts)}, '
          f'avg {sum(roomcounts)/len(roomcounts):.1f}')

    # seed 0 is the "save predates seeding" case - must still be valid
    g0, r0, s0 = generate(0)
    print(f'seed 0 fallback  : {len(r0)} rooms, connected={connected(g0, s0)}')


if __name__ == '__main__':
    main()
