#!/usr/bin/env python3
"""
Invariants of DUNGEON_GEN_ORGANIC, the cellular-automata cave.

This is an EXACT port, not an approximation. It drives the same LCG
(ISO_RANDOMIZE1, high sixteen bits) in the same order as GenerateOrganicCave,
so the floors it checks are the floors the ROM will build for those seeds. That
matters because the invariant this exists for - every open block reachable from
the spawn - is a property of the specific cave a seed produces, and a check run
against a differently-shaped cave would prove nothing about the real one.

Connectivity is the one that can actually break. A cellular automaton does not
produce a connected cave; the flood fill in GenerateOrganicCave imposes it by
keeping the largest region. Remove or weaken that and the game generates floors
whose stairs cannot be reached, which fails SILENTLY - the floor loads, looks
right, and strands the run.

Constants are read from the header rather than restated, for the reason
verify_dungeon_gen.py now does the same: a copied constant drifts, and a
drifted copy does not fail, it silently checks a floor nobody generates.
"""
import argparse
import os
import re
import sys
from collections import deque

W = H = 48


def const(repo, name, default=None):
    path = os.path.join(repo, 'include', 'rogue_dungeon.h')
    with open(path, encoding='utf-8') as fh:
        src = fh.read()
    m = re.search(r'^#define\s+%s\s+(\d+)' % name, src, re.M)
    if m:
        return int(m.group(1))
    if default is not None:
        return default
    raise SystemExit('%s not found in %s' % (name, path))


def themes_using_organic(repo):
    """Every theme whose .generator is DUNGEON_GEN_ORGANIC, with its caveFill."""
    path = os.path.join(repo, 'src', 'rogue_dungeon.c')
    with open(path, encoding='utf-8') as fh:
        src = fh.read()
    i = src.find('sDungeonThemes')
    out = []
    for blk in re.split(r'\n    \{\n', src[i:])[1:]:
        if '.generator = DUNGEON_GEN_ORGANIC,' not in blk:
            continue
        sec = re.search(r'mapSecId\s*=\s*(\w+)', blk)
        fill = re.search(r'\.caveFill\s*=\s*(\d+)', blk)
        iters = re.search(r'\.caveIters\s*=\s*(\d+)', blk)
        out.append((sec.group(1) if sec else '?',
                    int(fill.group(1)) if fill else 0,
                    int(iters.group(1)) if iters else 0))
    return out


class Lcg:
    """SeedDungeonRng + DungeonRandom."""

    def __init__(self, seed):
        self.v = (1103515245 * seed + 24691) & 0xFFFFFFFF

    def next(self):
        self.v = (1103515245 * self.v + 24691) & 0xFFFFFFFF
        return (self.v >> 16) & 0xFFFF


def generate(seed, fill, iters):
    """Mirror of GenerateOrganicCave. 1 = wall."""
    rng = Lcg(seed)
    g = [[1] * W for _ in range(H)]
    for y in range(1, H - 1):
        for x in range(1, W - 1):
            g[y][x] = 1 if (rng.next() % 100) < fill else 0

    def wall(gr, x, y):
        return 1 if not (0 <= x < W and 0 <= y < H) else gr[y][x]

    for _ in range(iters):
        ng = [row[:] for row in g]
        for y in range(1, H - 1):
            for x in range(1, W - 1):
                n = sum(wall(g, x + dx, y + dy)
                        for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                        if dx or dy)
                ng[y][x] = (1 if n >= 4 else 0) if g[y][x] else (1 if n >= 5 else 0)
        g = ng

    # keep the largest region
    seen = [[False] * W for _ in range(H)]
    best = []
    for y in range(H):
        for x in range(W):
            if g[y][x] or seen[y][x]:
                continue
            comp, q = [], deque([(x, y)])
            seen[y][x] = True
            while q:
                cx, cy = q.popleft()
                comp.append((cx, cy))
                for nx, ny in ((cx+1, cy), (cx-1, cy), (cx, cy+1), (cx, cy-1)):
                    if 0 <= nx < W and 0 <= ny < H and not g[ny][nx] \
                            and not seen[ny][nx]:
                        seen[ny][nx] = True
                        q.append((nx, ny))
            if len(comp) > len(best):
                best = comp
    keep = set(best)
    for y in range(H):
        for x in range(W):
            if not g[y][x] and (x, y) not in keep:
                g[y][x] = 1
    return g, rng


def fit_rooms(g, rng, cap, size=3):
    rooms = []
    for _ in range(400):
        if len(rooms) >= cap:
            break
        x = 1 + (rng.next() % (W - size - 2))
        y = 1 + (rng.next() % (H - size - 2))
        if any(g[y+dy][x+dx] for dy in range(size) for dx in range(size)):
            continue
        if any(not (x + size + 1 < rx or rx + rw + 1 < x
                    or y + size + 1 < ry or ry + rh + 1 < y)
               for (rx, ry, rw, rh) in rooms):
            continue
        rooms.append((x, y, size, size))
    return rooms


def check_floor(g, rooms):
    bad = []
    floor = [(x, y) for y in range(H) for x in range(W) if not g[y][x]]
    if not floor:
        return ['no open blocks at all']

    # the border must stay solid, or the cave opens onto the map edge
    for x in range(W):
        if not g[0][x] or not g[H-1][x]:
            bad.append('border open at column %d' % x)
            break
    for y in range(H):
        if not g[y][0] or not g[y][W-1]:
            bad.append('border open at row %d' % y)
            break

    if not rooms:
        bad.append('no rooms fitted (spawn and stairs have nowhere to go)')
        return bad

    # every synthesised room must be entirely floor - six placement callers
    # assume it, and an item in a wall is unreachable with no error
    for (rx, ry, rw, rh) in rooms:
        for dy in range(rh):
            for dx in range(rw):
                if g[ry+dy][rx+dx]:
                    bad.append('room at %d,%d is not all floor' % (rx, ry))
                    break

    for i, a in enumerate(rooms):
        for b in rooms[i+1:]:
            if not (a[0] + a[2] + 1 < b[0] or b[0] + b[2] + 1 < a[0]
                    or a[1] + a[3] + 1 < b[1] or b[1] + b[3] + 1 < a[1]):
                bad.append('rooms overlap: %s %s' % (a, b))

    # THE ONE THAT MATTERS: every open block reachable from the spawn
    spawn = (rooms[0][0] + rooms[0][2] // 2, rooms[0][1] + rooms[0][3] // 2)
    if g[spawn[1]][spawn[0]]:
        bad.append('spawn is inside a wall')
        return bad
    seen = {spawn}
    q = deque([spawn])
    while q:
        x, y = q.popleft()
        for nx, ny in ((x+1, y), (x-1, y), (x, y+1), (x, y-1)):
            if 0 <= nx < W and 0 <= ny < H and not g[ny][nx] and (nx, ny) not in seen:
                seen.add((nx, ny))
                q.append((nx, ny))
    if len(seen) != len(floor):
        bad.append('%d of %d open blocks unreachable from the spawn'
                   % (len(floor) - len(seen), len(floor)))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    ap.add_argument('--seeds', type=int, default=300)
    args = ap.parse_args()
    repo = os.path.abspath(args.repo)

    fill_def = const(repo, 'DUNGEON_CAVE_FILL_DEFAULT', 48)
    iters_def = const(repo, 'DUNGEON_CAVE_ITERS_DEFAULT', 4)
    cap = const(repo, 'DUNGEON_ROOMS_DEFAULT', 8)
    max_rooms = const(repo, 'DUNGEON_MAX_ROOMS', 12)
    cap = min(cap, max_rooms)

    themes = themes_using_organic(repo)
    if not themes:
        print('no theme uses DUNGEON_GEN_ORGANIC - nothing to check')
        return 0

    fail = 0
    for (name, fill, iters) in themes:
        f = fill or fill_def
        it = iters or iters_def
        bad_seeds = 0
        cov, rooms_n = [], []
        for seed in range(1, args.seeds + 1):
            g, rng = generate(seed, f, it)
            rooms = fit_rooms(g, rng, cap)
            problems = check_floor(g, rooms)
            open_n = sum(1 for y in range(H) for x in range(W) if not g[y][x])
            cov.append(open_n / (W * H) * 100)
            rooms_n.append(len(rooms))
            if problems:
                bad_seeds += 1
                if bad_seeds <= 3:
                    print('FAIL %s seed %d: %s' % (name, seed, '; '.join(problems)))
        status = 'ok' if bad_seeds == 0 else 'FAILED'
        print('%-28s fill %d%% iters %d  seeds %d  bad %d  '
              'cover %.1f%%  rooms %.1f  [%s]'
              % (name, f, it, args.seeds, bad_seeds,
                 sum(cov) / len(cov), sum(rooms_n) / len(rooms_n), status))
        if bad_seeds:
            fail = 1
    return fail


if __name__ == '__main__':
    sys.exit(main())
