#!/usr/bin/env python3
"""
Invariants of DUNGEON_GEN_FACILITY, the floorplan generator.

An EXACT port of GenerateFacilityFloor - same LCG, same draw order, same
partition rule - so the floors it checks are the floors the ROM will build.

Three things here fail silently rather than loudly:

  - A room the spanning tree never reached. Its walls look fine and the floor
    loads; it just cannot be entered, and anything placed in it (a trainer, an
    item ball, the stairs) is gone. The generator prunes these, and this check
    is what proves the pruning works.
  - A door that opens only part of a two-block partition. It reads as a doorway
    and leads into the inside of a wall.
  - A partition thinned to nothing, which would put two rooms in contact and
    turn the floorplan back into open space.
"""
import argparse
import os
import re
import sys
from collections import deque

W = H = 48
VTHICK = 2
DOOR_WIDTH = 2


def const(repo, name, default):
    path = os.path.join(repo, 'include', 'rogue_dungeon.h')
    with open(path, encoding='utf-8') as fh:
        m = re.search(r'^#define\s+%s\s+(\d+)' % name, fh.read(), re.M)
    return int(m.group(1)) if m else default


def csrc(repo):
    with open(os.path.join(repo, 'src', 'rogue_dungeon.c'), encoding='utf-8') as fh:
        return fh.read()


def themes(repo):
    src = csrc(repo)
    i = src.find('sDungeonThemes')
    out = []
    for blk in re.split(r'\n    \{\n', src[i:])[1:]:
        if '.generator = DUNGEON_GEN_FACILITY,' not in blk:
            continue
        sec = re.search(r'mapSecId\s*=\s*(\w+)', blk)
        g = lambda n: (lambda m: int(m.group(1)) if m else 0)(
            re.search(r'\.%s\s*=\s*(\d+)' % n, blk))
        out.append((sec.group(1) if sec else '?',
                    g('facilityLeaves'), g('facilityMinLeaf'),
                    g('facilityExtraDoors')))
    return out


class Lcg:
    def __init__(self, seed):
        self.v = (1103515245 * seed + 24691) & 0xFFFFFFFF

    def next(self):
        self.v = (1103515245 * self.v + 24691) & 0xFFFFFFFF
        return (self.v >> 16) & 0xFFFF


def shared(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    for t in range(1, VTHICK + 1):
        if ax + aw + t == bx or bx + bw + t == ax:
            lo, hi = max(ay, by), min(ay + ah, by + bh)
            if hi - lo >= 2:
                return (0, ax + aw if ax + aw + t == bx else bx + bw, t, lo, hi)
        if ay + ah + t == by or by + bh + t == ay:
            lo, hi = max(ax, bx), min(ax + aw, bx + bw)
            if hi - lo >= 2:
                return (1, ay + ah if ay + ah + t == by else by + bh, t, lo, hi)
    return None


def build(seed, leaves, minleaf, extra):
    rng = Lcg(seed)
    rects = [[1, 1, W - 2, H - 2]]
    while len(rects) < leaves:
        bi = max(range(len(rects)), key=lambda i: rects[i][2] * rects[i][3])
        x, y, w, h = rects[bi]
        if w > h + 2:
            horiz = False
        elif h > w + 2:
            horiz = True
        else:
            horiz = (rng.next() & 1) != 0
        if horiz:
            if h < minleaf * 2:
                break
            cut = minleaf + rng.next() % (h - minleaf * 2 + 1)
            rects.append([x, y + cut, w, h - cut])
            rects[bi][3] = cut
        else:
            if w < minleaf * 2:
                break
            cut = minleaf + rng.next() % (w - minleaf * 2 + 1)
            rects.append([x + cut, y, w - cut, h])
            rects[bi][2] = cut

    rooms = [(r[0], r[1], r[2] - VTHICK, r[3] - 1) for r in rects
             if r[2] - VTHICK >= 3 and r[3] - 1 >= 3]
    if not rooms:
        return None

    grid = [[1] * W for _ in range(H)]
    for (rx, ry, rw, rh) in rooms:
        for dy in range(rh):
            for dx in range(rw):
                grid[ry + dy][rx + dx] = 0

    doors = []

    def punch(axis, coord, thick, lo, hi):
        slots = hi - lo
        width = min(DOOR_WIDTH, slots)
        if width <= 0:
            return
        start = lo + rng.next() % (slots - width + 1)
        for i in range(width):
            for k in range(thick):
                c = (coord + k, start + i) if axis == 0 else (start + i, coord + k)
                grid[c[1]][c[0]] = 0
                doors.append((c, axis, coord, thick))

    n = len(rooms)
    joined = [False] * n
    joined[0] = True
    while True:
        cand = [(i, j) for i in range(n) for j in range(n)
                if joined[i] and not joined[j] and shared(rooms[i], rooms[j])]
        if not cand:
            break
        i, j = cand[rng.next() % len(cand)]
        punch(*shared(rooms[i], rooms[j]))
        joined[j] = True

    for _ in range(extra):
        i = rng.next() % n
        j = rng.next() % n
        if i == j:
            continue
        sw = shared(rooms[i], rooms[j])
        if sw:
            punch(*sw)

    # prune unreachable rooms, exactly as the C does
    start = (rooms[0][0] + rooms[0][2] // 2, rooms[0][1] + rooms[0][3] // 2)
    seen = {start}
    q = deque([start])
    while q:
        x, y = q.popleft()
        for nx, ny in ((x+1, y), (x-1, y), (x, y+1), (x, y-1)):
            if 0 <= nx < W and 0 <= ny < H and not grid[ny][nx] and (nx, ny) not in seen:
                seen.add((nx, ny))
                q.append((nx, ny))
    live = []
    for r in rooms:
        if (r[0], r[1]) in seen:
            live.append(r)
        else:
            for dy in range(r[3]):
                for dx in range(r[2]):
                    grid[r[1] + dy][r[0] + dx] = 1
    return grid, live, doors


def check(grid, rooms, doors):
    bad = []
    if not rooms:
        return ['no rooms survived']

    for x in range(W):
        if not grid[0][x] or not grid[H-1][x]:
            bad.append('border open at column %d' % x)
            break
    for y in range(H):
        if not grid[y][0] or not grid[y][W-1]:
            bad.append('border open at row %d' % y)
            break

    for (rx, ry, rw, rh) in rooms:
        for dy in range(rh):
            for dx in range(rw):
                if grid[ry+dy][rx+dx]:
                    bad.append('room %d,%d not all floor' % (rx, ry))
                    break

    for i, a in enumerate(rooms):
        for b in rooms[i+1:]:
            if not (a[0] + a[2] <= b[0] or b[0] + b[2] <= a[0]
                    or a[1] + a[3] <= b[1] or b[1] + b[3] <= a[1]):
                bad.append('rooms overlap: %s %s' % (a, b))

    # a door must open the FULL thickness of its partition
    for (c, axis, coord, thick) in doors:
        for k in range(thick):
            cc = (coord + k, c[1]) if axis == 0 else (c[0], coord + k)
            if grid[cc[1]][cc[0]]:
                bad.append('door at %s opens only part of a %d-thick wall'
                           % (c, thick))
                break

    floor = [(x, y) for y in range(H) for x in range(W) if not grid[y][x]]
    start = (rooms[0][0] + rooms[0][2] // 2, rooms[0][1] + rooms[0][3] // 2)
    seen = {start}
    q = deque([start])
    while q:
        x, y = q.popleft()
        for nx, ny in ((x+1, y), (x-1, y), (x, y+1), (x, y-1)):
            if 0 <= nx < W and 0 <= ny < H and not grid[ny][nx] and (nx, ny) not in seen:
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
    a = ap.parse_args()
    repo = os.path.abspath(a.repo)

    src = csrc(repo)

    def cdef(name, default):
        m = re.search(r'^#define\s+%s\s+(\d+)' % name, src, re.M)
        return int(m.group(1)) if m else default

    dl = cdef('FACILITY_LEAVES_DEFAULT', 14)
    dm = cdef('FACILITY_MINLEAF_DEFAULT', 8)
    de = cdef('FACILITY_EXTRA_DOORS_DEFAULT', 3)
    cap = const(repo, 'DUNGEON_MAX_ROOMS', 16)

    ts = themes(repo)
    if not ts:
        print('no theme uses DUNGEON_GEN_FACILITY - nothing to check')
        return 0

    fail = 0
    for (name, lv, ml, ex) in ts:
        lv = min(lv or dl, cap)
        ml = ml or dm
        ex = ex or de
        bad = 0
        cov, nr = [], []
        for seed in range(1, a.seeds + 1):
            r = build(seed, lv, ml, ex)
            if r is None:
                bad += 1
                continue
            grid, rooms, doors = r
            probs = check(grid, rooms, doors)
            cov.append(sum(1 for y in range(H) for x in range(W)
                           if not grid[y][x]) / (W * H) * 100)
            nr.append(len(rooms))
            if probs:
                bad += 1
                if bad <= 3:
                    print('FAIL %s seed %d: %s' % (name, seed, '; '.join(probs[:3])))
        print('%-28s leaves %d minleaf %d extra %d  seeds %d  bad %d  '
              'cover %.1f%%  rooms %.1f  [%s]'
              % (name, lv, ml, ex, a.seeds, bad,
                 sum(cov) / len(cov), sum(nr) / len(nr),
                 'ok' if bad == 0 else 'FAILED'))
        if bad:
            fail = 1
    return fail


if __name__ == '__main__':
    sys.exit(main())
