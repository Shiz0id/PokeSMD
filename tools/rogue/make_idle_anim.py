"""
Generate idle animation frames for a STATIC trainer front pic, by displacing the
pixels it already has. For the characters the B2W2 dump does not cover.

Output is one gif per trainer, 64x64, already in the stock pic's position, ready
for emit_bw_trainer_anim.py --idle-gifs. It writes no repo data of its own:
everything downstream - the sheet, the container, the table row - is the same
path the ripped animations take, so both kinds land in ONE sorted table.

WHAT THIS IS AND IS NOT. It gives cloth flutter and floating objects. It does
not give what BW gives: no redrawn poses, no arm movement, no expression, no
weight shift. Block-matching the real rips says 29-50% of their frame-to-frame
change is genuinely redrawn art, and none of that is reachable by moving
existing pixels. Set beside a ripped sprite this reads as alive, not animated,
and that is the honest ceiling.

THE MASK IS SUPPLIED, NOT INFERRED, and that is the finding that shaped the
tool. Two automatic criteria were built and both failed:

  * geodesic distance from the feet through the ink correctly says how much a
    part should move, and then says a head is far from the feet - true, useless;
  * thickness was meant to separate cloth from bone and does not, because
    EVERYTHING NEAR A SILHOUETTE EDGE IS THIN. A hat shell scores as thin, takes
    the full swing, and slides across the face under it. Dropping the rigid core
    to exactly zero motion does not help, because the hat IS thin by that
    measure.

Nothing in the pixels distinguishes a cape from a head: both are distal from the
anchor and both sit at the silhouette. That is semantic, so it is a table.

THE WARP IS INVERSE, which is what makes it safe. Pushing source pixels to
rounded destinations opens a gap wherever the field stretches - measured at 11
holes a frame, and 49 once the field got steeper - and repairing those with a
majority filter eats outlines. Resolving each DESTINATION back to its source
cannot hole, because every destination is asked exactly once.

Two invariants hold by construction and are asserted anyway, because both are
silent when broken:
  * no pixel outside the mask ever moves;
  * no frame contains a colour the source did not, so 4bpp stays satisfiable.

Run:  python make_idle_anim.py --out <dir> [--only drake,glacia]
      python make_idle_anim.py --out <dir> --repo <repo> --preview
Needs Pillow.
"""
import argparse
import math
import sys
from collections import deque
from pathlib import Path

from PIL import Image

NEI8 = [(-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1)]
FRAMES = 16
HOLD_MS = 100          # ~6 video frames a step, inside the BW rips' 7.7-10 fps


class Cloth:
    """A region that swings, named by palette index and a bounding band.

    Palette indices rather than a painted PNG mask because they are exact,
    diffable and reviewable in a table. They are NOT sufficient on their own -
    Drake's cape maroon is also his shadow and Phoebe's flower pink is also her
    sandals - which is what the band is for. Censused per sprite, never guessed.
    """

    def __init__(self, indices, ymin=0, ymax=63, xmin=0, xmax=63,
                 sway=2.4, bob=1.2, waves=1.4, ease=1.3):
        self.indices, self.sway, self.bob = set(indices), sway, bob
        self.waves, self.ease = waves, ease
        self.ymin, self.ymax, self.xmin, self.xmax = ymin, ymax, xmin, xmax

    def select(self, idx, ink, W, H):
        return {(x, y) for y in range(H) for x in range(W)
                if ink[y][x] and idx[x, y] in self.indices
                and self.ymin <= y <= self.ymax and self.xmin <= x <= self.xmax}


class Catch:
    """A region that dips as the hopping component lands, and recovers.

    Same masking and same tear-free falloff as Cloth, but its motion is DRIVEN
    BY THE HOP rather than by a free sine - which is the whole point. A hand
    bobbing on its own clock beside a ball bobbing on another reads as two
    unrelated twitches; a hand that gives exactly when the ball arrives reads as
    catching it, and the throw comes for free.

    Straight down, no sway: a hand absorbing a catch drops, it does not swing.

    The profile is squared so the dip is SHARP. Unsquared it is still at 56% of
    full a frame after contact, so the hand hangs low while the ball is already
    climbing and the two look disconnected again. Squared, over the eight frames
    of a bounce, the hand is down 2px for one frame and 1px for two - a give and
    a recovery rather than a sag.
    """

    def __init__(self, indices, ymin=0, ymax=63, xmin=0, xmax=63, dip=2.0, ease=1.2):
        self.indices, self.dip, self.ease = set(indices), dip, ease
        self.ymin, self.ymax, self.xmin, self.xmax = ymin, ymax, xmin, xmax

    select = Cloth.select

    def motion(self, u, a):
        return 0.0, self.dip * a * (1.0 - 4.0 * u * (1.0 - u)) ** 2


class Hop:
    """A detached component that bounces. No mask needed and no warp.

    A disconnected island of ink is the one thing the automatic pass gets right:
    it has no path to the feet, so it is free by definition. Being a rigid
    translation it also cannot hole and cannot shear.

    CATCH_DROP IS WHAT MAKES IT A TOSS RATHER THAN A HOVER. At rest the ball sits
    where the artist put it, which for Sidney is one empty row above his
    fingertips - close, but never touching, so it read as floating. Dropping the
    whole arc by catch_drop puts the bottom of the cycle ON the hand while
    leaving the apex where it was, because the travel is (height + catch_drop).

    Geometry, censused rather than guessed: the ball's lowest row is y30 and the
    fingertips start at y32. THE HAND DIPS AWAY AS THE BALL FALLS, so the two
    displacements largely cancel and the drop has to pay for both - at
    catch_drop 3 against dip 2 the net closure is one pixel and it still read as
    a near miss.

    Contact was settled by 8-CONNECTIVITY, not by measuring the gap. A
    per-column gap probe silently excludes exactly the columns where the two
    have merged, so it reports the distance between the parts that are NOT
    touching and gets further from the truth the closer the ball gets. Flooding
    from the topmost pixel and asking whether the blob is bigger than the ball
    cannot lie that way. Measured over the eight frames of a bounce: catch_drop
    3 touches on one frame, 5 touches on three - a graze against a catch.
    """

    def __init__(self, height=3.0, bounces=2, component=0, catch_drop=0.0):
        self.height, self.bounces, self.component = height, bounces, component
        self.catch_drop = catch_drop


# The recipes. One row per trainer, and adding a trainer is a row rather than an
# edit to anything below.
RECIPES = {
    # Cape only. The head, hat, beard, hands and boots never move.
    'drake': dict(pic='TRAINER_PIC_ELITE_FOUR_DRAKE', stock='elite_four_drake',
                  cloth=[Cloth({4}, xmin=30, sway=2.6, bob=1.4)]),
    # Dress hem below the waist. The bodice, collar and held ball stay put.
    'glacia': dict(pic='TRAINER_PIC_ELITE_FOUR_GLACIA', stock='elite_four_glacia',
                   cloth=[Cloth({6, 7, 8}, ymin=44, sway=2.4, bob=1.2)]),
    # SARONG ONLY - the flowers are deliberately not in the mask. They read as
    # rigid ornaments rather than as cloth, and moving them looked wrong.
    'phoebe': dict(pic='TRAINER_PIC_ELITE_FOUR_PHOEBE', stock='elite_four_phoebe',
                   cloth=[Cloth({5, 6, 7, 8, 9}, ymin=40, sway=2.2, bob=1.1)]),
    # NO CLOTH AT ALL. Sidney has nothing that hangs, and swinging his trouser
    # legs read as a limp rather than as an idle. His Poke Ball is a separate
    # island of ink, so it bounces - and his hand gives as it lands, which is
    # what turns a hover into a toss.
    #
    # The hand is the skin indices inside a band that stops short of the sleeve,
    # so attachment_distance pins it at the WRIST and the fingertips travel
    # furthest. Grown by two afterwards, which is what pulls in its own dark
    # outline; without that the hand would slide out from under its own edge.
    'sidney': dict(pic='TRAINER_PIC_ELITE_FOUR_SIDNEY', stock='elite_four_sidney',
                   cloth=[], hop=Hop(height=3.0, bounces=2, catch_drop=5.0),
                   catch=[Catch({1, 2, 3}, xmin=11, xmax=21, ymin=30, ymax=42,
                                dip=2.0)]),
}


def components(ink, W, H):
    seen = [[-1] * W for _ in range(H)]
    out = []
    for sy in range(H):
        for sx in range(W):
            if not ink[sy][sx] or seen[sy][sx] >= 0:
                continue
            q, cells = deque([(sx, sy)]), []
            seen[sy][sx] = len(out)
            while q:
                x, y = q.popleft()
                cells.append((x, y))
                for dx, dy in NEI8:
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < W and 0 <= ny < H and ink[ny][nx] and seen[ny][nx] < 0:
                        seen[ny][nx] = len(out)
                        q.append((nx, ny))
            out.append(cells)
    out.sort(key=len, reverse=True)
    return out


def attachment_distance(mask, ink, W, H):
    """Distance within the mask from where it joins the rest of the body.

    Zero at the attachment is what makes the swing tear-free: the pixels that
    touch the static body do not move relative to it, and the displacement grows
    smoothly outwards to the free edge.
    """
    d, q = {}, deque()
    for (x, y) in mask:
        for dx, dy in NEI8:
            n = (x + dx, y + dy)
            if (0 <= n[0] < W and 0 <= n[1] < H and ink[n[1]][n[0]]
                    and n not in mask):
                d[(x, y)] = 0
                q.append((x, y))
                break
    if not q:
        # A mask joined to nothing is a detached region; treat its topmost row
        # as the pinned end rather than letting the whole thing translate.
        top = min(y for _x, y in mask)
        for p in mask:
            if p[1] == top:
                d[p] = 0
                q.append(p)
    while q:
        x, y = q.popleft()
        for dx, dy in NEI8:
            n = (x + dx, y + dy)
            if n in mask and n not in d:
                d[n] = d[(x, y)] + 1
                q.append(n)
    far = max(d.values()) if d else 1
    for p in mask:
        d.setdefault(p, far)
    return d, max(1, far)


def spread(mask, W, H):
    """Nearest mask pixel for every pixel, so the inverse warp can sample the
    field just outside the region without folding the edge back on itself."""
    near, q = {p: p for p in mask}, deque(mask)
    while q:
        x, y = q.popleft()
        for dx, dy in NEI8:
            n = (x + dx, y + dy)
            if 0 <= n[0] < W and 0 <= n[1] < H and n not in near:
                near[n] = near[(x, y)]
                q.append(n)
    return near


def build(src, recipe, frames=FRAMES):
    idx = src.load()
    im = src.convert('RGBA')
    px = im.load()
    W, H = im.size
    ink = [[px[x, y][3] > 0 for x in range(W)] for y in range(H)]

    fields = []
    for cloth in list(recipe.get('cloth', [])) + list(recipe.get('catch', [])):
        mask = cloth.select(idx, ink, W, H)
        if not mask:
            raise SystemExit(f'empty mask for indices {sorted(cloth.indices)} '
                             f'in band y{cloth.ymin}-{cloth.ymax} '
                             f'x{cloth.xmin}-{cloth.xmax}')
        # Grown by two so the region carries its own outline. Without this the
        # black outline stays behind while the cloth moves out from under it.
        for _ in range(2):
            mask |= {(x + dx, y + dy) for (x, y) in mask for dx, dy in NEI8
                     if 0 <= x + dx < W and 0 <= y + dy < H and ink[y + dy][x + dx]}
        d, far = attachment_distance(mask, ink, W, H)
        fields.append((mask, {p: (d[p] / far) ** cloth.ease for p in mask},
                       spread(mask, W, H), cloth))

    hop = recipe.get('hop')
    hop_cells = []
    if hop is not None:
        comps = components(ink, W, H)
        loose = [c for c in comps[1:] if len(c) >= 4]
        if not loose:
            raise SystemExit('no detached component to hop')
        hop_cells = loose[min(hop.component, len(loose) - 1)]

    moving = set(hop_cells)
    for mask, _a, _n, _c in fields:
        moving |= mask

    # THE CLOTH IS DRAWN BEHIND THE BODY, and saying so is what makes the
    # invariant assertable. A swinging region does not only vacate pixels, it
    # arrives at new ones - and some of those are body. Left unstated, the warp
    # writes cape over coat and "nothing outside the mask moves" is false in a
    # way that is invisible until a limb flickers. So a warped pixel may land on
    # transparency and may not land on static ink; the body always wins.
    static_ink = {(x, y) for y in range(H) for x in range(W)
                  if ink[y][x] and (x, y) not in moving}

    out = []
    for f in range(frames):
        t = 2 * math.pi * f / frames
        # The hop's position within one bounce, resolved BEFORE the warp because
        # a Catch field is a function of it. This is the coupling that makes the
        # hand and the ball one gesture instead of two.
        u = ((f * hop.bounces / frames) % 1.0) if hop is not None else 0.0
        dst = im.copy()
        dp = dst.load()
        for (x, y) in moving:
            dp[x, y] = (0, 0, 0, 0)

        for mask, amp, near, cloth in fields:
            for ny in range(H):
                for nx in range(W):
                    sx, sy = float(nx), float(ny)
                    for _ in range(2):
                        q = near.get((int(round(sx)), int(round(sy))))
                        a = amp.get(q, 0.0) if q is not None else 0.0
                        if isinstance(cloth, Catch):
                            dx, dy = cloth.motion(u, a)
                        else:
                            dx = cloth.sway * a * math.sin(t + cloth.waves * math.pi * a)
                            dy = cloth.bob * a * math.sin(2 * t + math.pi * a)
                        sx, sy = nx - dx, ny - dy
                    s = (int(round(sx)), int(round(sy)))
                    if s in mask and (nx, ny) not in static_ink:
                        dp[nx, ny] = px[s[0], s[1]]

        if hop_cells:
            # A parabolic arc, not a sine: a sine spends as long at the top as
            # at the bottom, which reads as floating. u is the position within
            # one bounce, and 4u(1-u) is the arc that peaks in the middle and
            # touches down cleanly at both ends.
            # Travel is height + catch_drop so that lowering the bottom of the
            # cycle onto the hand does not also lower the apex.
            dy = int(round(hop.catch_drop
                           - (hop.height + hop.catch_drop) * 4.0 * u * (1.0 - u)))
            for (x, y) in hop_cells:
                if 0 <= y + dy < H and (x, y + dy) not in static_ink:
                    dp[x, y + dy] = px[x, y]

        out.append(dst)

    verify(im, out, static_ink)
    return out


def verify(src, frames, static_ink):
    """The two things that are silent when they break.

    Neither has ever failed since the inverse warp went in, which is exactly why
    they are asserted rather than trusted: a field edit that reintroduced
    forward scatter would show up here and nowhere else until someone looked at
    a screen.
    """
    px = src.load()
    W, H = src.size
    base = {c for c in src.getdata() if c[3] > 0}
    for i, f in enumerate(frames):
        fp = f.load()
        for (x, y) in static_ink:
            if fp[x, y] != px[x, y]:
                raise SystemExit(f'frame {i}: static pixel {x},{y} changed - '
                                 f'the cloth is overwriting the body')
        got = {c for c in f.getdata() if c[3] > 0}
        if got - base:
            raise SystemExit(f'frame {i} invented {len(got - base)} colours; '
                             f'4bpp has no room for them')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=str(Path(__file__).resolve().parents[2]))
    ap.add_argument('--out', required=True, help='directory to write the gifs to')
    ap.add_argument('--only', help='comma-separated recipe names')
    ap.add_argument('--preview', action='store_true',
                    help='also write a 4x preview gif beside each output')
    args = ap.parse_args()

    repo, out = Path(args.repo), Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    want = RECIPES
    if args.only:
        pick = [s.strip() for s in args.only.split(',') if s.strip()]
        bad = [p for p in pick if p not in RECIPES]
        if bad:
            raise SystemExit(f'unknown recipe(s): {", ".join(bad)}. '
                             f'Known: {", ".join(sorted(RECIPES))}')
        want = {k: RECIPES[k] for k in pick}

    for name, recipe in sorted(want.items()):
        p = repo / 'graphics/trainers/front_pics' / f'{recipe["stock"]}.png'
        src = Image.open(p)
        if src.mode != 'P':
            raise SystemExit(f'{p} is not an indexed PNG')
        fr = build(src, recipe)
        # Saved with a real per-frame duration so emit_bw_trainer_anim.py reads
        # the timing off the gif exactly as it does for a rip, rather than
        # needing a second path for locally generated frames.
        fr[0].save(out / f'{name}.gif', save_all=True, append_images=fr[1:],
                   duration=HOLD_MS, loop=0, disposal=2, transparency=0)
        distinct = len({f.tobytes() for f in fr})
        print(f'{name:9s} {len(fr)} frames, {distinct} distinct  -> {out / (name + ".gif")}')
        if args.preview:
            S = 4
            sh = []
            for f in fr:
                bg = Image.new('RGBA', f.size, (56, 56, 72, 255))
                bg.alpha_composite(f)
                sh.append(bg.convert('RGB').resize((64 * S, 64 * S), Image.NEAREST))
            sh[0].save(out / f'preview_{name}.gif', save_all=True,
                       append_images=sh[1:], duration=HOLD_MS, loop=0)


if __name__ == '__main__':
    sys.exit(main())
