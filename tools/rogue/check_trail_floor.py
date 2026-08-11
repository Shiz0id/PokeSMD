#!/usr/bin/env python3
"""
Invariants of DUNGEON_GEN_TRAILS, the clearings-and-trails generator.

An exact port of GenerateTrailFloor - same LCG, same draw order - so it checks
the floors the ROM will build.

Connectivity here is guaranteed BY CONSTRUCTION rather than by a repair pass:
TrailWalk drifts while it has budget and then finishes with a straight run to
the target, and it is that straight tail, not the walk, that makes the floor
connected. It is therefore the thing most worth guarding, because losing it
fails silently - the drift usually arrives on its own, so a floor with an
unreachable clearing would appear only on the seeds where it did not, and the
symptom is a stairs the player cannot get to.
"""
import argparse
import os
import re
import sys
from collections import deque

W = H = 48
MARGIN = 3
LOBES = 4


def cdef(src, name, default):
    m = re.search(r'^#define\s+%s\s+(\d+)' % name, src, re.M)
    return int(m.group(1)) if m else default


class Lcg:
    def __init__(self, seed):
        self.v = (1103515245 * seed + 24691) & 0xFFFFFFFF

    def next(self):
        self.v = (1103515245 * self.v + 24691) & 0xFFFFFFFF
        return (self.v >> 16) & 0xFFFF


def generate(seed, clearings, lobe, wander, maxrooms, straight_tail=True):
    rng = Lcg(seed)
    g = [[1] * W for _ in range(H)]

    def carve(x, y):
        if MARGIN <= x < W - MARGIN and MARGIN <= y < H - MARGIN:
            g[y][x] = 0

    clearings = min(clearings, maxrooms)
    cols = 3 if clearings <= 9 else 4
    rows = (clearings + cols - 1) // cols
    cw = max(1, (W - 2 * MARGIN - 8) // cols)
    ch = max(1, (H - 2 * MARGIN - 8) // rows)
    n = min(cols * rows, maxrooms)
    order = list(range(n))
    for i in range(n - 1, 0, -1):
        k = rng.next() % (i + 1)
        order[i], order[k] = order[k], order[i]
    clearings = min(clearings, n)

    cx, cy = [], []
    for i in range(clearings):
        gx, gy = order[i] % cols, order[i] // cols
        px = MARGIN + 4 + gx * cw + rng.next() % cw
        py = MARGIN + 4 + gy * ch + rng.next() % ch
        cx.append(px)
        cy.append(py)
        for _ in range(LOBES):
            lw = 3 + rng.next() % lobe
            lh = 3 + rng.next() % lobe
            ox = px - lw // 2 + rng.next() % 5 - 2
            oy = py - lh // 2 + rng.next() % 5 - 2
            for dy in range(lh):
                for dx in range(lw):
                    carve(ox + dx, oy + dy)

    def walk(x, y, tx, ty):
        budget = (abs(tx - x) + abs(ty - y)) * 4 + 40
        while (x != tx or y != ty) and budget > 0:
            budget -= 1
            carve(x, y)
            if rng.next() % 100 < wander:
                d = rng.next() % 4
                nx = x + (1 if d == 0 else -1 if d == 1 else 0)
                ny = y + (1 if d == 2 else -1 if d == 3 else 0)
                if MARGIN <= nx < W - MARGIN and MARGIN <= ny < H - MARGIN:
                    x, y = nx, ny
                continue
            if abs(tx - x) > abs(ty - y):
                x += 1 if tx > x else -1
            elif ty != y:
                y += 1 if ty > y else -1
            else:
                x += 1 if tx > x else -1
        if straight_tail:
            while x != tx:
                carve(x, y); x += 1 if tx > x else -1
            while y != ty:
                carve(x, y); y += 1 if ty > y else -1
            carve(x, y)

    used = [False] * clearings
    used[0] = True
    cur = 0
    while True:
        best, bestd = -1, 0
        for i in range(clearings):
            if used[i]:
                continue
            d = abs(cx[i] - cx[cur]) + abs(cy[i] - cy[cur])
            if best < 0 or d < bestd:
                best, bestd = i, d
        if best < 0:
            break
        walk(cx[cur], cy[cur], cx[best], cy[best])
        used[best] = True
        cur = best
    return g, rng


def fit_rooms(g, rng, cap, size=3):
    rooms = []
    for _ in range(400):
        if len(rooms) >= cap:
            break
        x = 1 + rng.next() % (W - size - 2)
        y = 1 + rng.next() % (H - size - 2)
        if any(g[y + dy][x + dx] for dy in range(size) for dx in range(size)):
            continue
        if any(not (x + size + 1 < rx or rx + rw + 1 < x
                    or y + size + 1 < ry or ry + rh + 1 < y)
               for (rx, ry, rw, rh) in rooms):
            continue
        rooms.append((x, y, size, size))
    return rooms


def check(g, rooms):
    bad = []
    floor = [(x, y) for y in range(H) for x in range(W) if not g[y][x]]
    if not floor:
        return ['no open blocks']

    # nothing may be carved inside the margin, or the cave opens onto the edge
    for y in range(H):
        for x in range(W):
            if not g[y][x] and not (MARGIN <= x < W - MARGIN
                                    and MARGIN <= y < H - MARGIN):
                bad.append('carved inside the margin at %d,%d' % (x, y))
                break
        if bad:
            break

    if not rooms:
        bad.append('no rooms fitted')
        return bad

    for (rx, ry, rw, rh) in rooms:
        for dy in range(rh):
            for dx in range(rw):
                if g[ry + dy][rx + dx]:
                    bad.append('room %d,%d not all floor' % (rx, ry))
                    break

    spawn = (rooms[0][0] + rooms[0][2] // 2, rooms[0][1] + rooms[0][3] // 2)
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
    ap.add_argument('--no-straight-tail', action='store_true',
                    help='deliberately break TrailWalk, to prove the '
                         'connectivity assertion is not vacuous')
    a = ap.parse_args()
    repo = os.path.abspath(a.repo)

    csrc = open(os.path.join(repo, 'src', 'rogue_dungeon.c'), encoding='utf-8').read()
    hsrc = open(os.path.join(repo, 'include', 'rogue_dungeon.h'), encoding='utf-8').read()

    dc = cdef(csrc, 'TRAIL_CLEARINGS_DEFAULT', 12)
    dl = cdef(csrc, 'TRAIL_LOBE_DEFAULT', 5)
    dw = cdef(csrc, 'TRAIL_WANDER_DEFAULT', 35)
    maxrooms = cdef(hsrc, 'DUNGEON_MAX_ROOMS', 16)
    cap = min(cdef(hsrc, 'DUNGEON_ROOMS_DEFAULT', 10), maxrooms)

    i = csrc.find('sDungeonThemes')
    themes = []
    for blk in re.split(r'\n    \{\n', csrc[i:])[1:]:
        if '.generator = DUNGEON_GEN_TRAILS,' not in blk:
            continue
        m = re.search(r'mapSecId\s*=\s*(\w+)', blk)
        gv = lambda nm: (lambda x: int(x.group(1)) if x else 0)(
            re.search(r'\.%s\s*=\s*(\d+)' % nm, blk))
        themes.append((m.group(1) if m else '?', gv('trailClearings'),
                       gv('trailLobe'), gv('trailWander')))

    if not themes:
        print('no theme uses DUNGEON_GEN_TRAILS - nothing to check')
        return 0

    # A STRESS ROW, run whatever the table currently says.
    #
    # TrailWalk drifts on a budget and then finishes with a straight run, and
    # that straight run is what makes the floor connected. At the shipped
    # wander of 35 the drift always arrives on its own, so removing the tail
    # changes nothing and a check that only tested the shipped value would be
    # asserting something it can never see fail. Measured over 4,788 walks the
    # budget is exhausted 0% of the time at wander 50, 0.4% at 60, 7.7% at 70
    # and 42.6% at 80 - so the tail is dead code at the default and essential
    # above it, and trailWander is a field a theme may set.
    #
    # This row exercises the guard at a setting where it is load-bearing.
    themes = list(themes) + [("(stress) wander 80", dc, dl, 80)]

    fail = 0
    for (name, c, l, w) in themes:
        c, l, w = c or dc, l or dl, w or dw
        bad = 0
        cov, nr = [], []
        for seed in range(1, a.seeds + 1):
            g, rng = generate(seed, c, l, w, maxrooms,
                              straight_tail=not a.no_straight_tail)
            rooms = fit_rooms(g, rng, cap)
            probs = check(g, rooms)
            cov.append(sum(1 for y in range(H) for x in range(W)
                           if not g[y][x]) / (W * H) * 100)
            nr.append(len(rooms))
            if probs:
                bad += 1
                if bad <= 3:
                    print('FAIL %s seed %d: %s' % (name, seed, '; '.join(probs[:2])))
        print('%-28s clearings %d lobe %d wander %d  seeds %d  bad %d  '
              'cover %.1f%%  rooms %.1f  [%s]'
              % (name, c, l, w, a.seeds, bad, sum(cov) / len(cov),
                 sum(nr) / len(nr), 'ok' if bad == 0 else 'FAILED'))
        if bad:
            fail = 1
    return fail


if __name__ == '__main__':
    sys.exit(main())
