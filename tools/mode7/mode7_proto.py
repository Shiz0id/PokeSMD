#!/usr/bin/env python3
"""
Mode 7 flyover prototype for the title screen -- host-side, renders PNGs.

The point of this script is to answer, before any C is written:

  * where the horizon has to sit, and how much ground is actually visible
  * whether the affine registers OVERFLOW at the altitudes the shot wants
    (REG_BG2PA is s16 .8, so a scale above 127.996 cannot be expressed --
    this bites near the horizon and gets worse the higher the camera goes)
  * what the ground looks like once it is forced through the affine BG's
    256-tile / 8bpp / no-flip ceiling
  * that the fixed-point ORDERING is right (Tonc's "Type C"): BG2X must be
    derived from the ALREADY-ROUNDED PA, because that is what the hardware
    accumulates across the scanline.

The ground plane is built as an INDEX image over a hand-authored palette,
because that is what the real art will be, and because palette index 0 being
transparent is load-bearing for the distortion theme -- the holes in the
platforms are not drawn, they are the backdrop BG showing through.

Run it with Windows Python (Pillow + numpy live there); the repo can be a
UNC path into WSL.

    py tools/mode7/mode7_proto.py --repo . --out ./mode7_out --theme distortion
"""

import argparse
import os
import sys

import numpy as np
from PIL import Image

# ---------------------------------------------------------------------------
# Screen and projection constants
# ---------------------------------------------------------------------------

SCREEN_W = 240
SCREEN_H = 160
SCREEN_CX = SCREEN_W // 2

# Projection plane distance in pixels. Sets the field of view:
# half-FOV = atan(120 / D). D = 160 gives ~74 degrees total.
PROJ_D = 160.0

# The ground plane is a 512x512 affine background (BGCNT_AFF512x512) with the
# display-area-overflow bit set to WRAPAROUND, which is what makes an endless
# flyover possible without streaming new tiles.
GROUND_SIZE = 512
GROUND_TILES = GROUND_SIZE // 8          # 64x64 tiles
AFFINE_TILE_LIMIT = 256                  # 1 byte per map entry, so 256 max

S16_MIN, S16_MAX = -32768, 32767         # REG_BG2PA..PD, .8 fixed
S32_MIN, S32_MAX = -(1 << 31), (1 << 31) - 1   # REG_BG2X/Y, .8 fixed (28.8)

TOTAL_FRAMES = 240      # 4 seconds at 60fps


# ---------------------------------------------------------------------------
# Palettes. Index 0 is ALWAYS transparent.
# ---------------------------------------------------------------------------

PALETTES = {
    "hoenn": [
        (0, 0, 0),          # 0 transparent
        (24, 56, 128),      # deep water
        (32, 72, 152),      # deep water wave
        (48, 104, 184),     # shallow
        (72, 136, 208),     # shallow wave
        (216, 200, 136),    # beach
        (72, 160, 72),      # grass
        (40, 120, 56),      # forest
        (96, 104, 96),      # rock
        (128, 136, 128),    # rock light
    ],
    "distortion": [
        (0, 0, 0),          # 0 transparent -- the void shows through
        (168, 232, 232),    # 1 edge highlight, pale cyan
        (96, 168, 168),     # 2 edge glow, teal
        (120, 128, 140),    # 3 platform top light
        (88, 96, 108),      # 4 platform top mid
        (56, 64, 76),       # 5 platform top dark
        (36, 44, 56),       # 6 platform shadow
        (80, 112, 96),      # 7 moss
        (52, 80, 68),       # 8 moss dark
        (128, 96, 168),     # 9 violet rune
        (72, 48, 112),      # 10 violet dark
    ],
    # Mystery-Dungeon read: rooms and corridors carved out of rock, the rock
    # itself fading to nothing a couple of cells out, so the floor plan hangs
    # in the dark. Cool stone against a warm floor so the plan is legible at
    # flyover distance.
    # Walls sit far below the floor in value on purpose: the rock should read
    # as the dark the floor is carved out of, not as a surface of its own. The
    # earlier brighter version pulled the eye off the rooms and read as ice.
    "dungeon": [
        (0, 0, 0),          # 0 transparent -- the void beyond the rock
        (78, 80, 94),       # 1 wall top highlight
        (48, 50, 62),       # 2 wall top
        (32, 33, 43),       # 3 wall top shadow
        (26, 26, 35),       # 4 wall face light
        (18, 18, 25),       # 5 wall face
        (11, 11, 16),       # 6 wall face dark
        (168, 146, 106),    # 7 floor light
        (132, 112, 82),     # 8 floor
        (94, 79, 58),       # 9 floor grid
        (248, 224, 128),    # 10 stairs glow
        (120, 96, 168),     # 11 accent
    ],
}


def palette_array(theme):
    """Pad the theme palette out to the full 256 entries the hardware has."""
    entries = list(PALETTES[theme])
    entries += [(0, 0, 0)] * (256 - len(entries))
    return np.asarray(entries, dtype=np.uint8)


# ---------------------------------------------------------------------------
# Noise
# ---------------------------------------------------------------------------

def value_noise(size, cells, rng):
    """One octave of smooth value noise, via bicubic upscale of a coarse grid."""
    grid = rng.random((cells, cells)).astype(np.float32)
    wrapped = np.pad(grid, ((0, 1), (0, 1)), mode="wrap")
    im = Image.fromarray((wrapped * 255).astype(np.uint8), mode="L")
    im = im.resize((size + size // cells, size + size // cells), Image.BICUBIC)
    return np.asarray(im, dtype=np.float32)[:size, :size] / 255.0


def fbm(size, rng, octaves=((8, 0.55), (16, 0.30), (32, 0.15))):
    h = sum(w * value_noise(size, c, rng) for c, w in octaves)
    return (h - h.min()) / (h.max() - h.min())


def shift(mask, dy, dx):
    """Wrapping shift -- the plane wraps, so its analysis must too."""
    return np.roll(np.roll(mask, dy, axis=0), dx, axis=1)


# ---------------------------------------------------------------------------
# Ground: Hoenn theme
# ---------------------------------------------------------------------------

def make_ground_hoenn(seed):
    rng = np.random.default_rng(seed)
    h = fbm(GROUND_SIZE, rng)
    idx = np.zeros((GROUND_SIZE, GROUND_SIZE), dtype=np.uint8)

    for lo, hi, v in [(0.00, 0.42, 1), (0.42, 0.50, 3), (0.50, 0.54, 5),
                      (0.54, 0.68, 6), (0.68, 0.82, 7), (0.82, 1.01, 8)]:
        idx[(h >= lo) & (h < hi)] = v

    yy, xx = np.mgrid[0:GROUND_SIZE, 0:GROUND_SIZE]
    wave = ((np.sin(xx * 0.5) + np.sin(yy * 0.37)) > 1.35)
    idx[wave & (idx == 1)] = 2
    idx[wave & (idx == 3)] = 4
    idx[(h > 0.90)] = 9
    return idx


# ---------------------------------------------------------------------------
# Ground: Distortion World theme
# ---------------------------------------------------------------------------

def make_ground_distortion(seed):
    """
    Fragmented angular platforms floating in nothing.

    Two deliberate choices, both of which also cut the tile count:

      * platform edges are snapped to a 4px grid, so the silhouette is angular
        rather than organic -- that is the Distortion World read, and it means
        far fewer distinct edge tiles
      * everything off a platform is palette index 0, which on hardware is
        TRANSPARENT, so the void is the backdrop BG rather than painted pixels
    """
    rng = np.random.default_rng(seed)
    # Low frequencies only: the Distortion World reads as a few big broken
    # slabs, not as terrain. High-octave noise here produces a landscape with
    # lakes in it, which is the wrong silhouette entirely.
    h = fbm(GROUND_SIZE, rng, octaves=((4, 0.64), (8, 0.24), (16, 0.12)))

    # Hard threshold, then snap to a 4px grid by majority vote so the edges go
    # angular. Snapping is what makes it look built rather than eroded.
    # The threshold is high on purpose -- around a third solid, so the void
    # dominates and the platforms read as fragments floating in it.
    solid = h > 0.60
    blk = 4
    coarse = solid.reshape(GROUND_SIZE // blk, blk,
                           GROUND_SIZE // blk, blk).mean(axis=(1, 3))
    solid = np.repeat(np.repeat(coarse > 0.5, blk, axis=0), blk, axis=1)

    # Scatter a few small islands so the void is not empty at altitude.
    debris = fbm(GROUND_SIZE, np.random.default_rng(seed + 991),
                 octaves=((24, 1.0),))
    dcoarse = (debris > 0.88).reshape(GROUND_SIZE // blk, blk,
                                      GROUND_SIZE // blk, blk).mean(axis=(1, 3))
    solid |= np.repeat(np.repeat(dcoarse > 0.5, blk, axis=0), blk, axis=1)

    idx = np.zeros((GROUND_SIZE, GROUND_SIZE), dtype=np.uint8)

    # Surface shading from the same height field, so platforms have interior
    # form instead of being flat slabs.
    hn = (h - h.min()) / (h.max() - h.min())
    idx[solid] = 4
    idx[solid & (hn > 0.60)] = 3
    idx[solid & (hn < 0.56)] = 5

    # Moss in the low interior.
    moss = fbm(GROUND_SIZE, np.random.default_rng(seed + 17),
               octaves=((16, 0.7), (32, 0.3)))
    idx[solid & (moss > 0.62)] = 7
    idx[solid & (moss > 0.76)] = 8

    # A faint rune grid, the "this place is constructed" cue.
    yy, xx = np.mgrid[0:GROUND_SIZE, 0:GROUND_SIZE]
    grid = ((xx % 32 == 0) | (yy % 32 == 0))
    idx[solid & grid] = 9
    idx[solid & grid & (moss > 0.70)] = 10

    # Edge treatment: one ring of glow, one ring of pale highlight outside it.
    interior = solid.copy()
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        interior &= shift(solid, dy, dx)
    rim = solid & ~interior

    interior2 = interior.copy()
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        interior2 &= shift(interior, dy, dx)
    rim2 = interior & ~interior2

    idx[rim2] = 1
    idx[rim] = 2

    # Undercut shadow just inside the lower edge, which sells "floating".
    under = solid & ~shift(solid, -3, 0)
    idx[under & ~rim] = 6

    idx[~solid] = 0
    return idx


# ---------------------------------------------------------------------------
# Ground: dungeon floor plan
# ---------------------------------------------------------------------------

# Matched to the real generator in include/rogue_dungeon.h so the title screen
# shows the shape of an actual floor rather than an invented one.
DUNGEON_W = DUNGEON_H = 48          # DUNGEON_WIDTH / DUNGEON_HEIGHT
DUNGEON_ROOMS = 10                  # DUNGEON_ROOMS_DEFAULT
DUNGEON_ROOM_MIN, DUNGEON_ROOM_MAX = 5, 10
DUNGEON_CELL_PX = 8                 # one dungeon cell per 8x8 affine tile
DUNGEON_WALL_RING = 2               # cells of rock before it fades to void


def generate_floor(seed):
    """
    Rooms plus L-corridors, the same shape the run generator produces.

    Returns a (48, 48) uint8 grid: 0 void rock, 1 floor, plus the stairs cell
    marked 2. Deliberately a plain reimplementation -- the point is the
    SILHOUETTE of a floor, not bit-compatibility with the C.
    """
    rng = np.random.default_rng(seed)
    grid = np.zeros((DUNGEON_H, DUNGEON_W), dtype=np.uint8)

    rooms = []
    for _ in range(400):
        if len(rooms) >= DUNGEON_ROOMS:
            break
        w = int(rng.integers(DUNGEON_ROOM_MIN, DUNGEON_ROOM_MAX + 1))
        h = int(rng.integers(DUNGEON_ROOM_MIN, DUNGEON_ROOM_MAX + 1))
        x = int(rng.integers(2, DUNGEON_W - w - 2))
        y = int(rng.integers(2, DUNGEON_H - h - 2))
        # Keep a two-cell gap so rooms never share a wall.
        if any(x < rx + rw + 2 and rx < x + w + 2 and
               y < ry + rh + 2 and ry < y + h + 2 for rx, ry, rw, rh in rooms):
            continue
        rooms.append((x, y, w, h))
        grid[y:y + h, x:x + w] = 1

    centres = [(x + w // 2, y + h // 2) for x, y, w, h in rooms]
    order = sorted(range(len(centres)), key=lambda i: centres[i][0])

    def carve_l(a, b, horizontal_first):
        (ax, ay), (bx, by) = a, b
        if horizontal_first:
            grid[ay, min(ax, bx):max(ax, bx) + 1] = 1
            grid[min(ay, by):max(ay, by) + 1, bx] = 1
        else:
            grid[min(ay, by):max(ay, by) + 1, ax] = 1
            grid[by, min(ax, bx):max(ax, bx) + 1] = 1

    for i in range(len(order) - 1):
        carve_l(centres[order[i]], centres[order[i + 1]], bool(rng.integers(2)))
    # A couple of extra links so the plan reads as a network, not a chain.
    for _ in range(2):
        if len(order) > 3:
            i, j = rng.choice(len(order), 2, replace=False)
            carve_l(centres[order[i]], centres[order[j]], bool(rng.integers(2)))

    if rooms:
        sx, sy, sw, sh = rooms[-1]
        grid[sy + sh // 2, sx + sw // 2] = 2
    return grid


def make_ground_dungeon(seed):
    """
    Rasterise a floor plan into the affine plane, one dungeon cell per 8x8
    tile.

    Because every cell renders to exactly one tile and there are only a handful
    of cell types, the distinct-tile count lands far under the 256 ceiling --
    which is the real argument for this theme over a landscape.
    """
    grid = generate_floor(seed)
    walkable = grid > 0

    # Rock within DUNGEON_WALL_RING cells of a floor is drawn; beyond that it
    # is void, so the plan hangs in the dark instead of sitting in a slab.
    near = walkable.copy()
    for _ in range(DUNGEON_WALL_RING):
        n = near.copy()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1),
                       (-1, 1), (-1, -1)):
            n |= shift(near, dy, dx)
        near = n
    rock = near & ~walkable

    idx = np.zeros((GROUND_SIZE, GROUND_SIZE), dtype=np.uint8)
    off = (GROUND_SIZE - DUNGEON_W * DUNGEON_CELL_PX) // 2

    rng = np.random.default_rng(seed + 5)
    speck = rng.random((DUNGEON_H, DUNGEON_W))

    for cy in range(DUNGEON_H):
        for cx in range(DUNGEON_W):
            py, px = off + cy * DUNGEON_CELL_PX, off + cx * DUNGEON_CELL_PX
            cell = np.zeros((DUNGEON_CELL_PX, DUNGEON_CELL_PX), dtype=np.uint8)

            if walkable[cy, cx]:
                cell[:] = 8
                if speck[cy, cx] > 0.72:
                    cell[:] = 7
                # A faint grid so the floor reads as tiled rather than painted.
                cell[0, :] = 9
                cell[:, 0] = 9
                if grid[cy, cx] == 2:
                    cell[1:7, 1:7] = 10
                    cell[2:6, 2:6] = 11
            elif rock[cy, cx]:
                # A cell with floor directly SOUTH of it is a wall FACE -- the
                # side you actually see. Everything else is wall top.
                face = (cy + 1 < DUNGEON_H) and walkable[cy + 1, cx]
                if face:
                    cell[:4, :] = 3
                    cell[4:, :] = 5
                    cell[7, :] = 6
                else:
                    cell[:] = 2
                    if not (cy > 0 and (walkable[cy - 1, cx] or rock[cy - 1, cx])):
                        cell[0, :] = 1
                    if speck[cy, cx] > 0.85:
                        cell[3:5, 3:5] = 3
            else:
                continue

            idx[py:py + DUNGEON_CELL_PX, px:px + DUNGEON_CELL_PX] = cell

    return idx


GROUND_BUILDERS = {"hoenn": make_ground_hoenn,
                   "distortion": make_ground_distortion,
                   "dungeon": make_ground_dungeon}


# ---------------------------------------------------------------------------
# Tile ceiling
# ---------------------------------------------------------------------------

def force_tile_limit(idx, palette, tile_limit=AFFINE_TILE_LIMIT):
    """
    Force an index plane through the affine BG's tile ceiling: 8x8 tiles, at
    most `tile_limit` distinct, and NO per-tile flip (map entries are a bare
    byte, so a mirrored tile costs a second slot).

    Returns (idx_out, unique_before, unique_after).
    """
    th = GROUND_SIZE // 8
    tiles = idx.reshape(th, 8, th, 8).transpose(0, 2, 1, 3).reshape(-1, 64)
    uniq, inverse, counts = np.unique(tiles, axis=0, return_inverse=True,
                                      return_counts=True)
    unique_before = len(uniq)

    if unique_before > tile_limit:
        # Keep the most-used tiles, map the rest to the nearest survivor in RGB
        # space. Deliberately the dumb approach: it shows the FLOOR of what the
        # limit costs, so hand-drawn art can only do better.
        keep = np.argsort(-counts)[:tile_limit]
        codebook = uniq[keep]
        pal = palette.astype(np.float32)
        cb_rgb = pal[codebook].reshape(tile_limit, -1)
        uq_rgb = pal[uniq].reshape(unique_before, -1)

        nearest = np.empty(unique_before, dtype=np.int32)
        for i in range(0, unique_before, 512):
            block = uq_rgb[i:i + 512]
            d = ((block[:, None, :] - cb_rgb[None, :, :]) ** 2).sum(axis=2)
            nearest[i:i + 512] = d.argmin(axis=1)
        tiles = codebook[nearest][inverse]

    out = (tiles.reshape(th, th, 8, 8).transpose(0, 2, 1, 3)
           .reshape(GROUND_SIZE, GROUND_SIZE).astype(np.uint8))
    return out, unique_before, min(unique_before, tile_limit)


# ---------------------------------------------------------------------------
# Camera
# ---------------------------------------------------------------------------

def ease_in_out(t):
    t = float(np.clip(t, 0.0, 1.0))
    return t * t * (3.0 - 2.0 * t)


def ease_out(t):
    t = float(np.clip(t, 0.0, 1.0))
    return 1.0 - (1.0 - t) ** 3


class Camera:
    def __init__(self, x, z, height, yaw, horizon, ground_alpha, logo_slide,
                 warp_amp=0.0, warp_phase=0.0):
        self.x = x
        self.z = z
        self.height = height
        self.yaw = yaw
        self.horizon = horizon
        self.ground_alpha = ground_alpha
        self.logo_slide = logo_slide
        self.warp_amp = warp_amp        # world px of lateral sway per scanline
        self.warp_phase = warp_phase


def camera_curve(theme, frame):
    """
    Three-phase shot: cruise, climb, settle.

    Altitudes are picked from the measured clean window (64-96): below ~44 the
    lambda quantisation starts repeating between scanlines, and at 150+ BG2PA
    overflows s16 on the row nearest the horizon.
    """
    t = frame / (TOTAL_FRAMES - 1)

    if theme == "dungeon":
        # Higher and slower than the other themes: the point of this shot is
        # that you can READ the floor plan, so rooms have to stay legible.
        # Capped at 120 because BG2PA starts clamping near the horizon at 150.
        if t < 0.45:
            height, speed, yaw_rate = 88.0, 2.6, 0.0
            ground_alpha = min(1.0, frame / 30.0)
            logo_slide = 0.0
        elif t < 0.80:
            k = ease_in_out((t - 0.45) / 0.35)
            height = 88.0 + (120.0 - 88.0) * k
            speed = 2.6 + (0.8 - 2.6) * k
            yaw_rate = 0.0
            ground_alpha = 1.0
            logo_slide = 0.0
        else:
            k = ease_out((t - 0.80) / 0.20)
            height = 120.0 + (128.0 - 120.0) * k
            speed = 0.8 + (0.25 - 0.8) * k
            yaw_rate = 0.0
            ground_alpha = 1.0 - 0.15 * k
            logo_slide = k
        horizon = 48.0 + 8.0 * ease_in_out(max(0.0, t - 0.45) / 0.55)
        return height, speed, yaw_rate, horizon, ground_alpha, logo_slide, 0.0

    if theme == "distortion":
        # Slower and floatier, with a continuous yaw drift rather than a sway,
        # and a lateral warp that breathes.
        if t < 0.45:
            height, speed = 72.0, 3.0
            yaw_rate = 0.0035
            ground_alpha = min(1.0, frame / 30.0)
            logo_slide = 0.0
            warp = 9.0
        elif t < 0.80:
            k = ease_in_out((t - 0.45) / 0.35)
            height = 72.0 + (108.0 - 72.0) * k
            speed = 3.0 + (0.9 - 3.0) * k
            yaw_rate = 0.0035 * (1.0 - k)
            ground_alpha = 1.0
            logo_slide = 0.0
            warp = 9.0 - 4.0 * k
        else:
            k = ease_out((t - 0.80) / 0.20)
            height = 108.0 + (118.0 - 108.0) * k
            speed = 0.9 + (0.3 - 0.9) * k
            yaw_rate = 0.0
            ground_alpha = 1.0 - 0.20 * k
            logo_slide = k
            warp = 5.0 - 2.5 * k
        horizon = 46.0 + 10.0 * ease_in_out(max(0.0, t - 0.45) / 0.55)
        return height, speed, yaw_rate, horizon, ground_alpha, logo_slide, warp

    # hoenn
    if t < 0.45:
        height, speed = 68.0, 6.0
        yaw_rate = 0.0
        ground_alpha = min(1.0, frame / 24.0)
        logo_slide = 0.0
    elif t < 0.80:
        k = ease_in_out((t - 0.45) / 0.35)
        height = 68.0 + (140.0 - 68.0) * k
        speed = 6.0 + (1.2 - 6.0) * k
        yaw_rate = 0.0
        ground_alpha = 1.0
        logo_slide = 0.0
    else:
        k = ease_out((t - 0.80) / 0.20)
        height = 140.0 + (150.0 - 140.0) * k
        speed = 1.2 + (0.4 - 1.2) * k
        yaw_rate = 0.0
        ground_alpha = 1.0 - 0.55 * k
        logo_slide = k
    horizon = 44.0 + 12.0 * ease_in_out(max(0.0, t - 0.45) / 0.55)
    return height, speed, yaw_rate, horizon, ground_alpha, logo_slide, 0.0


def build_camera_path(theme):
    """Integrate speed and yaw rate into positions -- the curve is piecewise,
    so solving it in closed form would just be a second place to be wrong."""
    cams = []
    x, z, yaw = 256.0, 0.0, 0.0
    for frame in range(TOTAL_FRAMES):
        hgt, speed, yaw_rate, horizon, ga, slide, warp = camera_curve(theme, frame)
        if theme == "hoenn":
            yaw = 0.16 * np.sin(frame / TOTAL_FRAMES * 5.0)
        cams.append(Camera(x, z, hgt, yaw, horizon, ga, slide,
                           warp_amp=warp, warp_phase=frame * 0.045))
        x += speed * np.sin(yaw)
        z += speed * np.cos(yaw)
        yaw += yaw_rate
    return cams


# ---------------------------------------------------------------------------
# The projection
# ---------------------------------------------------------------------------

def scanline_affine(cam, mode):
    """
    Per-scanline affine parameters, exactly as they would be written to
    REG_BG2PA / REG_BG2PC / REG_BG2X / REG_BG2Y inside HBlank.

    Those six registers occupy 16 CONTIGUOUS bytes at 0x4000020, so one HBlank
    DMA of four 32-bit words writes the whole set. PB and PD are zero because
    writing BG2X/Y every scanline makes their inter-scanline accumulation
    irrelevant.

    mode:
      "float"    reference render in floating point
      "hw"       Tonc Type C: lambda in high precision, BG2X derived from the
                 ROUNDED PA (correct)
      "exact"    BG2X derived from the exact float scale instead of the rounded
                 PA. A mild ordering error -- sub-pixel, shows as speckle.
      "lambda8"  Tonc Type A proper: lambda itself computed in .8 first.
    """
    sy = np.arange(SCREEN_H, dtype=np.float64)[:, None]

    h = sy - cam.horizon
    valid = (h > 0.5)
    h_safe = np.where(valid, h, 1.0)

    scale = cam.height / h_safe          # Tonc eq 20.2: lambda = z/D = y_cam/h
    if mode == "lambda8":
        scale = np.rint(scale * 256.0) / 256.0
    z = scale * PROJ_D

    ca, sa = np.cos(cam.yaw), np.sin(cam.yaw)

    wx_c = cam.x + z * sa
    wz_c = cam.z + z * ca

    # The Distortion World warp. This is FREE: BG2X/BG2Y are already being
    # written every scanline, so a lateral offset per scanline costs one add.
    # It is applied along the camera's RIGHT axis so the plane snakes rather
    # than shears, and it is a function of world depth so the wave travels with
    # the ground instead of sitting on the screen like heat shimmer.
    if cam.warp_amp:
        off = cam.warp_amp * np.sin(z * 0.010 + cam.warp_phase)
        wx_c = wx_c + off * ca
        wz_c = wz_c - off * sa

    pa_f = scale * ca
    pc_f = -scale * sa

    lam8 = np.rint((cam.height / h_safe) * 256.0)[valid[:, 0], 0]
    stats = {
        "max_scale": float(scale[valid[:, 0]].max()) if valid.any() else 0.0,
        "lambda8_duplicate_rows": int((np.diff(lam8) == 0).sum()) if lam8.size > 1 else 0,
        "visible_rows": int(valid.sum()),
    }

    if mode == "float":
        sx = np.arange(SCREEN_W, dtype=np.float64)[None, :]
        texX = wx_c + (sx - SCREEN_CX) * pa_f
        texY = wz_c + (sx - SCREEN_CX) * pc_f
        stats["pa_overflow_rows"] = 0
        stats["max_pa"] = float(np.abs(pa_f * 256).max())
        stats["max_bg2"] = 0
        return texX, texY, np.broadcast_to(valid, (SCREEN_H, SCREEN_W)), stats

    pa_raw = np.rint(pa_f * 256.0)
    pc_raw = np.rint(pc_f * 256.0)
    stats["pa_overflow_rows"] = int(((np.abs(pa_raw) > S16_MAX) & valid).sum())
    stats["max_pa"] = float(np.abs(pa_raw[valid[:, 0]]).max()) if valid.any() else 0.0

    pa = np.clip(pa_raw, S16_MIN, S16_MAX).astype(np.int64)
    pc = np.clip(pc_raw, S16_MIN, S16_MAX).astype(np.int64)

    if mode in ("hw", "lambda8"):
        # Type C. The hardware walks texX = BG2X + n*PA using the ROUNDED PA,
        # so the centre offset must come out with that same rounded value.
        bg2x = np.rint(wx_c * 256.0).astype(np.int64) - SCREEN_CX * pa
        bg2y = np.rint(wz_c * 256.0).astype(np.int64) - SCREEN_CX * pc
    elif mode == "exact":
        bg2x = np.rint((wx_c - SCREEN_CX * pa_f) * 256.0).astype(np.int64)
        bg2y = np.rint((wz_c - SCREEN_CX * pc_f) * 256.0).astype(np.int64)
    else:
        raise ValueError(f"unknown mode {mode!r}")

    stats["max_bg2"] = int(max(np.abs(bg2x[valid[:, 0]]).max(),
                               np.abs(bg2y[valid[:, 0]]).max())) if valid.any() else 0
    stats["bg2_overflow"] = bool(stats["max_bg2"] > S32_MAX)

    n = np.arange(SCREEN_W, dtype=np.int64)[None, :]
    return (bg2x + n * pa) >> 8, (bg2y + n * pc) >> 8, \
        np.broadcast_to(valid, (SCREEN_H, SCREEN_W)), stats


def render_ground(ground_idx, cam, mode):
    """Sample the plane. Wraps, matching the affine display-overflow bit."""
    texX, texY, valid, stats = scanline_affine(cam, mode)
    ix = np.mod(np.floor(texX).astype(np.int64), GROUND_SIZE)
    iy = np.mod(np.floor(texY).astype(np.int64), GROUND_SIZE)
    return ground_idx[iy, ix], valid, stats


# ---------------------------------------------------------------------------
# Backdrop and compositing
# ---------------------------------------------------------------------------

def make_backdrop(theme, cam, frame):
    if theme == "hoenn":
        top = np.array([248, 248, 252], dtype=np.float32)
        bot = np.array([176, 216, 248], dtype=np.float32)
        t = np.linspace(0.0, 1.0, SCREEN_H, dtype=np.float32)[:, None, None]
        return np.broadcast_to(top * (1 - t) + bot * t,
                               (SCREEN_H, SCREEN_W, 3)).copy()

    if theme == "dungeon":
        # Near-black with a soft glow under the plan, so the floor reads as
        # suspended over depth rather than sitting on a backdrop. On hardware
        # this is a cheap text BG -- a vertical ramp and nothing else.
        yy = np.arange(SCREEN_H, dtype=np.float32)[:, None, None]
        deep = np.array([8, 8, 14], dtype=np.float32)
        glow = np.array([40, 36, 62], dtype=np.float32)
        g = np.exp(-((yy - (cam.horizon + 34.0)) ** 2) / 2600.0)
        out = deep + (glow - deep) * g
        band = np.exp(-((yy - cam.horizon) ** 2) / 120.0)
        out = out + np.array([26, 30, 48], dtype=np.float32) * band
        return np.clip(np.broadcast_to(out, (SCREEN_H, SCREEN_W, 3)).copy(), 0, 255)

    # Distortion: a slow logarithmic spiral centred on the horizon. On hardware
    # this is a separate BG, and a true rotating vortex needs either an affine
    # BG of its own (there isn't one spare in Mode 1) or an animated tilemap --
    # see the notes at the bottom of this file.
    yy, xx = np.mgrid[0:SCREEN_H, 0:SCREEN_W].astype(np.float32)
    dx = xx - SCREEN_CX
    dy = (yy - cam.horizon) * 2.4
    r = np.sqrt(dx * dx + dy * dy) + 2.0
    theta = np.arctan2(dy, dx)

    v = np.sin(theta * 3.0 + np.log(r) * 3.2 - frame * 0.018)
    v = (v + 1.0) * 0.5
    v *= np.clip(1.0 - (r / 320.0), 0.0, 1.0) ** 0.7

    deep = np.array([14, 10, 26], dtype=np.float32)
    glow = np.array([84, 56, 128], dtype=np.float32)
    out = deep[None, None, :] + (glow - deep)[None, None, :] * v[:, :, None]

    # A pale band right at the horizon, so the plane has something to sit against.
    band = np.exp(-((yy - cam.horizon) ** 2) / 90.0)[:, :, None]
    out = out + np.array([40, 60, 80], dtype=np.float32)[None, None, :] * band
    return np.clip(out, 0, 255)


def composite_frame(backdrop, ground_idx, valid, palette, logo_rgb, logo_alpha, cam):
    frame = backdrop.astype(np.float32).copy()

    rgb = palette[ground_idx].astype(np.float32)
    # Palette index 0 is transparent -- this is what makes the void real rather
    # than painted, and it is why the holes in the platforms show the backdrop.
    opaque = (ground_idx != 0) & valid

    a = opaque.astype(np.float32) * cam.ground_alpha
    sy = np.arange(SCREEN_H, dtype=np.float32)[:, None]
    # Fade the plane into the backdrop approaching the horizon. This is not
    # decoration: it is what hides the scanline where BG2PA would clamp.
    a = a * np.clip((sy - cam.horizon) / 26.0, 0.0, 1.0)

    frame = frame * (1 - a[:, :, None]) + rgb * a[:, :, None]

    if cam.logo_slide > 0.0:
        # Vanilla holds BG2X at -29, so logo pixel 0 lands at screen x=+29, and
        # slides BG2Y from -32 to 0.
        y0 = int(round(32 - 32 * cam.logo_slide))
        x0 = 29
        lh = min(256, SCREEN_H - y0)
        lw = min(256, SCREEN_W - x0)
        sub = logo_rgb[:lh, :lw].astype(np.float32)
        sa = (logo_alpha[:lh, :lw].astype(np.float32) * cam.logo_slide)[:, :, None]
        dst = frame[y0:y0 + lh, x0:x0 + lw]
        frame[y0:y0 + lh, x0:x0 + lw] = dst * (1 - sa) + sub * sa

    return np.clip(frame, 0, 255).astype(np.uint8)


# ---------------------------------------------------------------------------
# The Unown swarm
# ---------------------------------------------------------------------------

# Unown are OBJ, not BG -- a budget nothing else in this shot touches. The
# forms are 64x64 4bpp in the decomp; at flyover distance they project to
# roughly 10-40 px, so on hardware these would be redrawn at 32x32 (512 bytes
# each) and scaled with affine OAM. Note the GBA has only 32 affine matrix
# slots shared across all sprites, so a swarm larger than that has to reuse
# scales -- quantising depth into a few buckets, which is what QUANT does here.
UNOWN_FORMS = ("", "b", "c", "f", "k", "n", "r", "question")
UNOWN_QUANT = 4          # px granularity of projected size; models the OAM limit
UNOWN_WORLD_SIZE = 23.0  # world units tall

# Body goes violet and very dark; the eye stays bright. The eye is the whole
# read at this size -- at 12 px the silhouette is mush and the eye is the only
# thing that says "something is looking at you".
UNOWN_TINT = {
    4: (170, 220, 235),     # eye edge
    5: (238, 250, 255),     # eye core
    6: (10, 8, 16),         # outline / pupil
    7: (86, 70, 124),       # body light
    8: (58, 46, 88),        # body mid
    9: (38, 30, 58),        # body dark
}


def load_unown(repo):
    """Load the real Unown forms and recolour them for the swarm."""
    out = []
    for form in UNOWN_FORMS:
        p = os.path.join(repo, "graphics", "pokemon", "unown",
                         form, "front.png") if form else \
            os.path.join(repo, "graphics", "pokemon", "unown", "front.png")
        idx = np.asarray(Image.open(p), dtype=np.uint8)
        rgb = np.zeros(idx.shape + (3,), dtype=np.float32)
        for i, c in UNOWN_TINT.items():
            rgb[idx == i] = c
        # Index 0 is the background key in these sheets.
        out.append((rgb, (idx != 0).astype(np.float32)))
    return out


def project_point(cam, wx, wz, wy):
    """
    World -> screen with the same camera the ground plane uses.

    Consistency check: a point at wy=0 lands at sy = horizon + cam.height*D/z,
    which inverts to the ground's own lambda = cam.height / (sy - horizon).
    """
    ca, sa = np.cos(cam.yaw), np.sin(cam.yaw)
    dx, dz = wx - cam.x, wz - cam.z
    zc = dx * sa + dz * ca            # forward
    xc = dx * ca - dz * sa            # right
    if zc < 12.0:
        return None
    sx = SCREEN_CX + xc * PROJ_D / zc
    sy = cam.horizon + (cam.height - wy) * PROJ_D / zc
    return sx, sy, PROJ_D / zc, zc


def swarm_world(cam, frame, n, n_forms, seed=3):
    """
    A helix of Unown around the flight path, continuously flowing toward the
    camera and wrapping. Seeded once and evaluated per frame, so each Unown
    keeps its identity -- form, orbit and phase -- across the whole shot.
    """
    rng = np.random.default_rng(seed)
    base_ahead = rng.random(n) * 155.0
    ang0 = rng.random(n) * 2.0 * np.pi
    spin = 0.011 + rng.random(n) * 0.020
    rad = 22.0 + rng.random(n) * 44.0
    hgt = 34.0 + rng.random(n) * 30.0
    forms = rng.integers(0, n_forms, n)

    ahead = 38.0 + np.mod(base_ahead - frame * 0.62, 152.0)
    ang = ang0 + frame * spin

    ca, sa = np.cos(cam.yaw), np.sin(cam.yaw)
    lateral = np.cos(ang) * rad
    wx = cam.x + sa * ahead + ca * lateral
    wz = cam.z + ca * ahead - sa * lateral
    wy = hgt + np.sin(ang) * rad * 0.55
    return wx, wz, wy, forms, ahead


def draw_swarm(frame_rgb, cam, unown, positions, cache):
    """Depth-sorted, back to front. Far ones fade into the dark."""
    wx, wz, wy, forms, ahead = positions
    for i in np.argsort(-ahead):
        p = project_point(cam, wx[i], wz[i], wy[i])
        if p is None:
            continue
        sx, sy, scale, zc = p
        size = int(round(UNOWN_WORLD_SIZE * scale / UNOWN_QUANT)) * UNOWN_QUANT
        if size < 6 or size > 120:
            continue

        key = (int(forms[i]), size)
        if key not in cache:
            rgb, alpha = unown[int(forms[i])]
            im = Image.fromarray(rgb.astype(np.uint8)).resize((size, size), Image.BILINEAR)
            am = Image.fromarray((alpha * 255).astype(np.uint8)).resize((size, size), Image.BILINEAR)
            cache[key] = (np.asarray(im, dtype=np.float32),
                          np.asarray(am, dtype=np.float32) / 255.0)
        srgb, salpha = cache[key]

        x0, y0 = int(round(sx - size / 2)), int(round(sy - size / 2))
        x1, y1 = x0 + size, y0 + size
        cx0, cy0 = max(0, x0), max(0, y0)
        cx1, cy1 = min(SCREEN_W, x1), min(SCREEN_H, y1)
        if cx0 >= cx1 or cy0 >= cy1:
            continue

        sub_rgb = srgb[cy0 - y0:cy1 - y0, cx0 - x0:cx1 - x0]
        sub_a = salpha[cy0 - y0:cy1 - y0, cx0 - x0:cx1 - x0]
        fade = float(np.clip(1.15 - (ahead[i] - 95.0) / 85.0, 0.20, 1.0))
        a = (sub_a * fade)[:, :, None]
        dst = frame_rgb[cy0:cy1, cx0:cx1]
        frame_rgb[cy0:cy1, cx0:cx1] = dst * (1 - a) + sub_rgb * a


def apply_vignette(frame_rgb, strength=0.55):
    yy, xx = np.mgrid[0:SCREEN_H, 0:SCREEN_W].astype(np.float32)
    vx = (xx - SCREEN_CX) / SCREEN_CX
    vy = (yy - SCREEN_H / 2.0) / (SCREEN_H / 2.0)
    v = 1.0 - strength * np.clip(vx * vx + vy * vy, 0.0, 1.0) ** 1.2
    return frame_rgb * v[:, :, None]


def load_press_start(repo):
    """The top band of press_start.png is PRESS START; below it is the
    copyright line, which is a separate sprite on the real screen."""
    p = os.path.join(repo, "graphics", "title_screen", "press_start.png")
    im = Image.open(p)
    pal = np.asarray(im.getpalette()[:16 * 3], dtype=np.uint8).reshape(16, 3)
    full = np.asarray(im, dtype=np.uint8)

    # The sheet stacks PRESS START on rows 1-7 and the copyright line on rows
    # 8-14 with NO blank row between them, so this split is a hard index rather
    # than something to detect. On the real screen they are two separate
    # sprites placed at y=108 and y=148, both centred on START_BANNER_X = 128.
    out = []
    for lo, hi, y in ((1, 8, 108), (8, 15, 148)):
        idx = full[lo:hi]
        xs = np.nonzero((idx != 0).any(axis=0))[0]
        idx = idx[:, xs.min():xs.max() + 1]
        out.append((pal[idx].astype(np.float32),
                    (idx != 0).astype(np.float32), y))
    return out


def load_logo(repo):
    """
    Rebuild the real Pokemon logo from the decomp's own assets: a 256-tile 8bpp
    sheet plus a 32x32 AFFINE tilemap (one byte per entry). That is the same
    format the ground plane uses, so composing it correctly confirms the format.
    """
    gfx = os.path.join(repo, "graphics", "title_screen")
    sheet = Image.open(os.path.join(gfx, "pokemon_logo.png"))
    pal = np.asarray(sheet.getpalette()[:256 * 3], dtype=np.uint8).reshape(256, 3)
    si = np.asarray(sheet, dtype=np.uint8)
    sw = si.shape[1] // 8
    tiles = si.reshape(-1, 8, sw, 8).transpose(0, 2, 1, 3).reshape(-1, 8, 8)
    tmap = np.frombuffer(open(os.path.join(gfx, "pokemon_logo.bin"), "rb").read(),
                         dtype=np.uint8).reshape(32, 32)
    idx = tiles[tmap].transpose(0, 2, 1, 3).reshape(256, 256)
    return pal[idx], (idx != 0)


# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------

def upscale(arr, factor):
    return Image.fromarray(arr).resize(
        (arr.shape[1] * factor, arr.shape[0] * factor), Image.NEAREST)


def contact_sheet(frames, cols, factor):
    fw, fh = frames[0].shape[1] * factor, frames[0].shape[0] * factor
    rows = (len(frames) + cols - 1) // cols
    pad = 6
    sheet = Image.new("RGB", (cols * fw + (cols + 1) * pad,
                              rows * fh + (rows + 1) * pad), (20, 20, 24))
    for i, f in enumerate(frames):
        r, c = divmod(i, cols)
        sheet.paste(upscale(f, factor), (pad + c * (fw + pad), pad + r * (fh + pad)))
    return sheet


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.environ.get("POKEDECOMP_REPO", "."))
    ap.add_argument("--out", default="./mode7_out")
    ap.add_argument("--theme", default="distortion", choices=sorted(GROUND_BUILDERS))
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--logo", dest="logo", action="store_true", default=None,
                    help="composite the Pokemon logo (default: only for non-dungeon themes)")
    ap.add_argument("--no-logo", dest="logo", action="store_false",
                    help="no logo -- PRESS START only")
    ap.add_argument("--swarm", type=int, default=0,
                    help="number of Unown in the swarm (0 = none)")
    ap.add_argument("--vignette", type=float, default=0.0)
    args = ap.parse_args()
    if args.logo is None:
        args.logo = (args.theme != "dungeon")

    os.makedirs(args.out, exist_ok=True)
    tag = args.theme
    palette = palette_array(args.theme)

    print(f"theme: {args.theme}")
    print("building ground plane ...")
    ideal = GROUND_BUILDERS[args.theme](args.seed)
    ground, uniq_before, uniq_after = force_tile_limit(ideal, palette)
    solid_pct = float((ideal != 0).mean() * 100.0)
    print(f"  plane              : {GROUND_SIZE}x{GROUND_SIZE} px, "
          f"{GROUND_TILES}x{GROUND_TILES} tiles, {solid_pct:.0f}% solid")
    print(f"  distinct 8x8 tiles : {uniq_before}  (affine ceiling "
          f"{AFFINE_TILE_LIMIT}, no flip)")
    print(f"  after forcing      : {uniq_after}")
    print(f"  palette entries    : {len(PALETTES[args.theme])} of 256")

    for name, arr in (("ideal", ideal), ("256tiles", ground)):
        Image.fromarray(palette[arr]).save(
            os.path.join(args.out, f"{tag}_ground_{name}.png"))
    side = Image.new("RGB", (GROUND_SIZE * 2 + 12, GROUND_SIZE), (20, 20, 24))
    side.paste(Image.fromarray(palette[ideal]), (0, 0))
    side.paste(Image.fromarray(palette[ground]), (GROUND_SIZE + 12, 0))
    side.save(os.path.join(args.out, f"{tag}_ground_compare.png"))

    logo_rgb, logo_alpha = load_logo(args.repo)
    if not args.logo:
        # Zero alpha rather than a branch everywhere -- composite_frame stays
        # one code path whether or not the logo is in the shot.
        logo_alpha = np.zeros_like(logo_alpha)
    title_text = load_press_start(args.repo) if not args.logo else []

    unown = load_unown(args.repo) if args.swarm else []
    swarm_cache = {}
    if args.swarm:
        print(f"  swarm              : {args.swarm} Unown, "
              f"{len(unown)} forms, size quantised to {UNOWN_QUANT} px")

    cams = build_camera_path(args.theme)

    print("rendering flyover ...")
    worst = {"max_scale": 0.0, "pa_overflow_rows": 0, "max_pa": 0.0,
             "max_bg2": 0, "bg2_overflow": False, "lambda8_duplicate_rows": 0}
    frames = []
    for i, cam in enumerate(cams):
        g, valid, st = render_ground(ground, cam, "hw")
        bd = make_backdrop(args.theme, cam, i)
        f = composite_frame(bd, g, valid, palette,
                            logo_rgb, logo_alpha, cam).astype(np.float32)

        if args.swarm:
            draw_swarm(f, cam, unown,
                       swarm_world(cam, i, args.swarm, len(unown)), swarm_cache)
        if args.vignette:
            f = apply_vignette(f, args.vignette)
        if cam.logo_slide > 0.0:
            for n, (t_rgb, t_a, ty) in enumerate(title_text):
                th, tw = t_a.shape
                tx0 = max(0, 128 - tw // 2)
                ty0 = ty - th // 2
                # Only PRESS START breathes; the copyright line is static, as
                # it is on the real screen.
                pulse = 0.55 + 0.45 * float(np.sin(i * 0.10)) if n == 0 else 1.0
                a = (t_a * cam.logo_slide * pulse)[:, :, None]
                dst = f[ty0:ty0 + th, tx0:tx0 + tw]
                f[ty0:ty0 + th, tx0:tx0 + tw] = dst * (1 - a) + t_rgb * a

        frames.append(np.clip(f, 0, 255).astype(np.uint8))
        for k in worst:
            if k == "bg2_overflow":
                worst[k] = worst[k] or st.get(k, False)
            else:
                worst[k] = max(worst[k], st.get(k, 0))

    print()
    print("  REGISTER HEADROOM")
    print(f"    peak per-scanline scale : {worst['max_scale']:.1f}")
    print(f"    peak |BG2PA| (.8 fixed) : {worst['max_pa']:.0f}  (s16 limit {S16_MAX})")
    print(f"    scanlines that OVERFLOW : {worst['pa_overflow_rows']}")
    print(f"    peak |BG2X/Y| (.8 fixed): {worst['max_bg2']}  (s32 limit {S32_MAX})")
    print(f"    BG2X/Y overflow         : {worst['bg2_overflow']}")
    print(f"    lambda8 duplicate rows  : {worst['lambda8_duplicate_rows']}"
          f"  (only matters if lambda is computed in .8)")
    print()

    keys = [10, 40, 75, 110, 145, 175, 205, 235]
    contact_sheet([frames[k] for k in keys], 4, 2).save(
        os.path.join(args.out, f"{tag}_keyframes.png"))

    gif = [upscale(frames[i], 3).convert("P", palette=Image.ADAPTIVE)
           for i in range(0, TOTAL_FRAMES, 3)]
    gif[0].save(os.path.join(args.out, f"{tag}_flyover.gif"), save_all=True,
                append_images=gif[1:], duration=50, loop=0, optimize=True)

    # Warp on/off, so the cost of the effect is visible rather than assumed.
    probe = cams[75]
    off = Camera(probe.x, probe.z, probe.height, probe.yaw, probe.horizon,
                 probe.ground_alpha, probe.logo_slide, 0.0, 0.0)
    pair = []
    for c in (off, probe):
        g, valid, _ = render_ground(ground, c, "hw")
        bd = make_backdrop(args.theme, c, 75)
        pair.append(composite_frame(bd, g, valid, palette, logo_rgb, logo_alpha, c))
    contact_sheet(pair, 2, 3).save(os.path.join(args.out, f"{tag}_warp_off_on.png"))

    print(f"wrote files to {args.out}")


if __name__ == "__main__":
    sys.exit(main())
