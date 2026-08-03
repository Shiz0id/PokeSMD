"""Turn a gridded third-party tile sheet into a GBA secondary tileset.

Everything before this script could only READ tilesets. This one writes a whole
new one -- tiles.png, sixteen palettes, metatiles.bin and metatile_attributes.bin
-- from a sheet laid out as a grid of terrain blocks with an autotile legend.

Written for the Pokemon Mystery Dungeon: Red Rescue Team "Lapis Cave" sheet
(ripped and formatted by SilverDeoxys563, who asks only for optional credit),
but the sheet layout is a config entry, so a second sheet is a table addition.

WHAT MAKES IT NON-TRIVIAL

  * The sheet is 24x24 per cell and a GBA metatile is 16x16. Downscaled with
    BOX and re-quantised to the sheet's own palette, which keeps the colours
    exact -- a plain resize invents blends and blows the 15-colour budget.
  * A tile stores palette-relative indices, so two visually identical tiles
    under different palettes are NOT the same tile. Dedupe is keyed on
    (palette, pixels) for that reason.
  * Dedupe also folds x/y flips, because a metatile entry carries flip bits.
    On this sheet that is most of the saving.
  * Attributes are copied per block from a vanilla donor metatile rather than
    synthesised. ROGUELIKE.md section 5: attributes are per entry, and one
    shared attribute is the same shape of bug as a hard-coded per-theme
    constant. The ground copies the cave floor's MB_CAVE so wild encounters
    fire; getting this wrong is silent until nothing spawns.

IDEMPOTENT, AND IT OWNS ITS DIRECTORY. It rewrites every output file from the
sheet each run, so nothing else may append to this tileset -- the same rule
append_metatiles.py has for the cave, and for the same reason.

Usage:
    python import_tile_sheet.py lapis            # report only
    python import_tile_sheet.py lapis --write    # write the tileset
    python import_tile_sheet.py lapis --decls    # print the C declarations
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[2]

NUM_TILES_IN_PRIMARY = 512      # secondary tiles are absolute 512..1023
NUM_PALS_IN_PRIMARY = 6         # secondary palettes land in slots 6..12
NUM_PALS_TOTAL = 13

# ---------------------------------------------------------------- sheet specs

# A SHEET is a source image. Every rip SilverDeoxys563 has formatted shares one
# geometry, verified identical on both sheets here, so a new one is a one-line
# entry until a sheet turns up that disagrees.
SHEETS = {
    'lapis_cave': dict(source='tools/rogue/sheets/lapis_cave.png'),
    'mt_freeze': dict(source='tools/rogue/sheets/mt_freeze.png'),
}

SHEET_GEOMETRY = dict(
    origin=(8, 162),        # top-left of the first grid RULE, not the cell
    pitch=25,               # 24px cell + 1px rule
    cell=24,
    legend_col=0,           # three 3x3 masks per row, one per terrain cell
    background=(0, 128, 128),
    colorkey=(255, 0, 255),
)

# Each NAMED column on these sheets is three grid cells wide, and the Legend
# carries one 3x3 mask per cell, so a block is addressed by the first of its
# three. The Alt groups hold a single cell each on both sheets - they are
# variants of the plain fill, not autotile cases.
COL = dict(walls=3, wall_alt1=6, wall_alt2=9,
           ground=12, ground_alt1=15, ground_alt2=18, water=21)

# A TILESET is what gets written. Blocks may come from DIFFERENT sheets, and
# blocks sharing a palette slot are quantised together against one palette.
#
# THE ORDER HERE IS THE METATILE ORDER, and metatile ids are what the theme
# table names, so appending a block is safe and reordering one is not.
TILESETS = {
    # Glacia's. Lapis Cave's crystal walls over Mt. Freeze's snow: the crystal
    # is the better wall art and the snow is the better floor for a theme that
    # is snowing on the player, and neither sheet has both.
    'lapis': dict(
        out='data/tilesets/secondary/rogue_lapis_cave',
        symbol='RogueLapisCave',
        prefix='LAPIS',
        blocks=[
            # attr is behaviour+layer type copied from a vanilla donor metatile
            # rather than synthesised - see the module docstring. 0x0008 is cave
            # 0x201: MB_CAVE, layer NORMAL, so wild encounters fire.
            dict(name='wall', sheet='lapis_cave', col=COL['walls'], pal=6, attr=0x0008),
            dict(name='ground', sheet='mt_freeze', col=COL['ground'], pal=7, attr=0x0008),
            # The decor variants MUST carry the ground's attribute, not a bare
            # one: they replace floor blocks in place, and a decorated block
            # with no encounter flag would be a dead spot the player cannot see.
            dict(name='decor1', sheet='mt_freeze', col=COL['ground_alt1'], pal=7, attr=0x0008),
            dict(name='decor2', sheet='mt_freeze', col=COL['ground_alt2'], pal=7, attr=0x0008),
            # Never painted by the theme - kept because it is what the sheet
            # has, and dropping it would move every metatile id after it.
            dict(name='water', sheet='lapis_cave', col=COL['water'], pal=8, attr=0x0008),
        ]),
}

# our PaintWalls slots, as neighbour masks. '#' wall, 'o' floor, '.' don't care
SLOTS = {
    'INTERIOR_MID':    ['###', '###', '###'],
    'INTERIOR_LEFT':   ['.##', 'o##', '.##'],
    'INTERIOR_RIGHT':  ['##.', '##o', '##.'],
    'FACE_MID':        ['###', '###', 'ooo'],
    'FACE_LEFT':       ['.##', 'o##', 'oo.'],
    'FACE_RIGHT':      ['##.', '##o', '.oo'],
    'NORTH_MID':       ['ooo', '###', '###'],
    'NORTH_LEFT':      ['oo.', 'o##', '.##'],
    'NORTH_RIGHT':     ['.oo', '##o', '##.'],
    'CORNER_OPEN_SE':  ['###', '###', '##o'],
    'CORNER_OPEN_SW':  ['###', '###', 'o##'],
    'CORNER_OPEN_NW':  ['o##', '###', '###'],
    'CORNER_OPEN_NE':  ['##o', '###', '###'],
    'SLIVER_VERT':     ['.#.', 'o#o', '.#.'],
    'SLIVER_HORZ':     ['.o.', '###', '.o.'],
    'SLIVER_VERT_TOP': ['.o.', 'o#o', '.#.'],
    'SLIVER_VERT_BOT': ['.#.', 'o#o', '.o.'],
    'SLIVER_HORZ_L':   ['.o.', 'o##', '.o.'],
    'SLIVER_HORZ_R':   ['.o.', '##o', '.o.'],
    'SLIVER_ISOLATED': ['.o.', 'o#o', '.o.'],
}


# ---------------------------------------------------------------- sheet reading

class Sheet:
    def __init__(self, spec):
        self.spec = spec
        self.img = np.asarray(Image.open(REPO / spec['source']).convert('RGB'))
        self.bg = np.array(spec['background'])
        self.key = np.array(spec['colorkey'])
        ox, oy = spec['origin']
        p, c = spec['pitch'], spec['cell']
        self.xs = list(range(ox, self.img.shape[1] - c, p))
        self.ys = list(range(oy, self.img.shape[0] - c, p))
        self.cellsz = c

    def cell(self, cx, cy):
        x, y = self.xs[cx] + 1, self.ys[cy] + 1
        return self.img[y:y + self.cellsz, x:x + self.cellsz]

    def filled(self, cx, cy):
        c = self.cell(cx, cy)
        if c.shape[0] < self.cellsz or c.shape[1] < self.cellsz:
            return False
        bg = (np.abs(c - self.bg).sum(axis=2) < 30).mean()
        mg = (np.abs(c - self.key).sum(axis=2) < 30).mean()
        return 1 - bg - mg > 0.5

    def mask(self, row, k):
        """3x3 neighbour mask for terrain cell k of this row.

        Sheet convention, verified against cells whose art was already known:
        black = SAME terrain, teal = DIFFERENT terrain (not "don't care"),
        white = the centre marker. So all eight neighbours are specified.
        """
        x0 = self.xs[self.spec['legend_col']] + 1 + k * self.cellsz
        y0 = self.ys[row] + 1
        refs = (('#', np.array([0, 0, 0])), ('o', np.array([255, 255, 255])),
                ('.', self.bg))
        out = []
        for by in range(3):
            line = ''
            for bx in range(3):
                blk = self.img[y0 + by * 8:y0 + by * 8 + 8, x0 + bx * 8:x0 + bx * 8 + 8]
                if blk.size == 0:
                    line += '?'
                    continue
                line += min(refs, key=lambda r: np.abs(blk.astype(int) - r[1])
                            .sum(axis=2).mean())[0]
            out.append(line)
        return out


def match_slot(sheet_mask, want):
    score = 0
    for r in range(3):
        for c in range(3):
            if r == 1 and c == 1:
                continue
            w = want[r][c]
            if w == '.':
                continue
            if (sheet_mask[r][c] == '#') != (w == '#'):
                return None
            score += 1
    return score


# ---------------------------------------------------------------- conversion

def block_palette(cells, key, bg):
    """Exact colour list for a block, from the ORIGINAL cells."""
    px = np.concatenate([c.reshape(-1, 3) for c in cells])
    px = px[np.abs(px - key).sum(axis=1) > 30]
    px = px[np.abs(px - bg).sum(axis=1) > 30]
    u, counts = np.unique(px, axis=0, return_counts=True)
    u = u[np.argsort(-counts)]                      # commonest first
    if len(u) > 15:
        raise SystemExit(f'block needs {len(u)} colours, 15 is the 4bpp limit')
    return [tuple(int(v) for v in c) for c in u]


def to_indices(cell24, palette, key, bg):
    """24x24 RGB -> 16x16 palette indices. Index 0 is transparent."""
    small = np.asarray(Image.fromarray(cell24).resize((16, 16), Image.BOX), dtype=int)
    pal = np.array(palette, dtype=int)
    out = np.zeros((16, 16), dtype=np.uint8)
    flat = small.reshape(-1, 3)
    # nearest palette entry, so BOX blending cannot invent a colour
    d = np.abs(flat[:, None, :] - pal[None, :, :]).sum(axis=2)
    idx = d.argmin(axis=1) + 1                      # +1: 0 stays transparent
    transparent = (np.abs(flat - key).sum(axis=1) < 30) | \
                  (np.abs(flat - bg).sum(axis=1) < 30)
    idx[transparent] = 0
    return idx.reshape(16, 16).astype(np.uint8)


class TileBank:
    """8x8 tiles, deduped per palette and across x/y flips."""

    def __init__(self):
        self.tiles = []                 # list of 8x8 uint8
        self.index = {}                 # (pal, bytes) -> (local index, xflip, yflip)

    def add(self, tile, pal):
        for xf in (0, 1):
            for yf in (0, 1):
                v = tile
                if xf:
                    v = v[:, ::-1]
                if yf:
                    v = v[::-1, :]
                hit = self.index.get((pal, v.tobytes()))
                if hit is not None:
                    # stored orientation -> ours needs the same flips back
                    return hit[0], xf, yf
        i = len(self.tiles)
        self.tiles.append(tile)
        self.index[(pal, tile.tobytes())] = (i, 0, 0)
        return i, 0, 0


def convert(ts, verbose=True):
    cache = {}

    def sheet(name):
        if name not in cache:
            cache[name] = Sheet({**SHEET_GEOMETRY, **SHEETS[name]})
        return cache[name]

    blocks = []
    for b in ts['blocks']:
        sh = sheet(b['sheet'])
        cells = [(r, k, sh.cell(b['col'] + k, r))
                 for r in range(len(sh.ys))
                 for k in range(3)
                 if b['col'] + k < len(sh.xs) and sh.filled(b['col'] + k, r)]
        blocks.append({**b, 'sh': sh, 'cells': cells})
        if verbose:
            print(f'  {b["name"]:<7} {len(cells):3d} cells  from {b["sheet"]:<11}'
                  f' palette slot {b["pal"]}')

    # Blocks sharing a palette slot share ONE palette, quantised from all their
    # cells together. The ground and its two decor variants are the case that
    # needs it: a tileset has one palette per slot, so quantising them apart
    # would give slot 7 whichever was written last and silently recolour the
    # other two. It also means the 15-colour budget is per SLOT, not per block.
    palettes = {}
    for b in blocks:
        palettes.setdefault(b['pal'], []).extend(c for _, _, c in b['cells'])
    for slot in sorted(palettes):
        owner = next(b for b in blocks if b['pal'] == slot)
        names = '+'.join(b['name'] for b in blocks if b['pal'] == slot)
        palettes[slot] = block_palette(palettes[slot], owner['sh'].key, owner['sh'].bg)
        if verbose:
            print(f'  palette {slot}  {len(palettes[slot]):2d} colours  ({names})')

    bank = TileBank()
    metatiles = []
    for b in blocks:
        for (r, k, cell) in b['cells']:
            idx = to_indices(cell, palettes[b['pal']], b['sh'].key, b['sh'].bg)
            entries = []
            for qy in (0, 1):
                for qx in (0, 1):
                    t = idx[qy * 8:qy * 8 + 8, qx * 8:qx * 8 + 8]
                    ti, xf, yf = bank.add(t, b['pal'])
                    entries.append((NUM_TILES_IN_PRIMARY + ti)
                                   | (xf << 10) | (yf << 11) | (b['pal'] << 12))
            metatiles.append(dict(block=b['name'], attr=b['attr'], sh=b['sh'],
                                  row=r, k=k, entries=entries))

    # slot -> metatile local index, from the legend of the block's OWN sheet
    slotmap = {}
    for slot, want in SLOTS.items():
        best = None
        for li, mt in enumerate(metatiles):
            if mt['block'] != 'wall':
                continue
            s = match_slot(mt['sh'].mask(mt['row'], mt['k']), want)
            if s is not None and (best is None or s > best[0]):
                best = (s, li)
        if best:
            slotmap[slot] = best[1]

    # the plain floor: ground fully surrounded by ground
    floor_li = None
    for li, mt in enumerate(metatiles):
        if mt['block'] == 'ground' and all(
                ch == '#' for j, row in enumerate(mt['sh'].mask(mt['row'], mt['k']))
                for i, ch in enumerate(row) if not (i == 1 and j == 1)):
            floor_li = li
            break

    decor = [(mt['block'], li) for li, mt in enumerate(metatiles)
             if mt['block'].startswith('decor')]

    return dict(blocks=blocks, palettes=palettes, bank=bank,
                metatiles=metatiles, slotmap=slotmap, floor=floor_li, decor=decor)


# ---------------------------------------------------------------- writing

def write_tileset(spec, conv):
    out = REPO / spec['out']
    (out / 'palettes').mkdir(parents=True, exist_ok=True)

    # ---- tiles.png : 16 tiles per row, indices are palette-relative
    tiles = conv['bank'].tiles
    rows = (len(tiles) + 15) // 16
    sheet = np.zeros((rows * 8, 16 * 8), dtype=np.uint8)
    for i, t in enumerate(tiles):
        y, x = (i // 16) * 8, (i % 16) * 8
        sheet[y:y + 8, x:x + 8] = t
    img = Image.fromarray(sheet, mode='P')
    # the PNG carries one palette for humans; every tile stores raw indices, so
    # tiles on the other palettes look wrong in an editor. That is normal.
    first = conv['palettes'][min(conv['palettes'])]
    flat = [0, 0, 0]
    for c in first:
        flat += list(c)
    flat += [0, 0, 0] * (256 - len(flat) // 3)
    img.putpalette(flat)
    img.save(out / 'tiles.png', bits=4)

    # ---- palettes : JASC-PAL, CRLF, 16 entries. Secondary uses slots 6..12.
    for slot in range(16):
        cols = conv['palettes'].get(slot, [])
        lines = ['JASC-PAL', '0100', '16', '0 0 0']
        for c in cols:
            lines.append(f'{c[0]} {c[1]} {c[2]}')
        while len(lines) < 3 + 16:
            lines.append('0 0 0')
        (out / 'palettes' / f'{slot:02d}.pal').write_bytes(
            ('\r\n'.join(lines) + '\r\n').encode())

    # ---- metatiles.bin : 8 u16 per metatile, 0-3 middle layer, 4-7 top
    mt = bytearray()
    at = bytearray()
    for m in conv['metatiles']:
        for e in m['entries']:
            mt += int(e).to_bytes(2, 'little')
        for _ in range(4):
            mt += (0).to_bytes(2, 'little')      # empty top layer
        at += int(m['attr']).to_bytes(2, 'little')
    (out / 'metatiles.bin').write_bytes(bytes(mt))
    (out / 'metatile_attributes.bin').write_bytes(bytes(at))
    return len(tiles), len(conv['metatiles'])


def print_decls(spec, conv, ntiles):
    sym, path = spec['symbol'], spec['out']
    print(f'\n--- src/data/tilesets/graphics.h  (BEFORE the #else, Emerald block) ---')
    print(f'const u32 gTilesetTiles_{sym}[] = INCGFX_U32("{path}/tiles.png", '
          f'".4bpp.fastSmol", "-num_tiles {ntiles} -Wnum_tiles");\n')
    print(f'const u16 gTilesetPalettes_{sym}[][16] =\n{{')
    for i in range(16):
        print(f'    INCGFX_U16("{path}/palettes/{i:02d}.pal", ".gbapal"),')
    print('};')
    print(f'\n--- src/data/tilesets/metatiles.h ---')
    print(f'const u16 gMetatiles_{sym}[] = INCBIN_U16("{path}/metatiles.bin");')
    print(f'const u16 gMetatileAttributes_{sym}[] = '
          f'INCBIN_U16("{path}/metatile_attributes.bin");')
    print(f'\n--- src/data/tilesets/headers.h  (BEFORE the #else at ~line 899) ---')
    print(f'const struct Tileset gTileset_{sym} =\n{{')
    print('    .isCompressed = TRUE,\n    .isSecondary = TRUE,')
    print(f'    .tiles = gTilesetTiles_{sym},')
    print(f'    .palettes = gTilesetPalettes_{sym},')
    print(f'    .metatiles = gMetatiles_{sym},')
    print(f'    .metatileAttributes = gMetatileAttributes_{sym},')
    print('    .callback = NULL,\n};')

    print(f'\n--- include/rogue_dungeon.h ---')
    base, p = 0x200, spec['prefix']
    print(f'#define {p}_METATILE_FLOOR{"":<12} 0x{base + conv["floor"]:03X}')
    for slot, li in conv['slotmap'].items():
        print(f'#define {p}_METATILE_{slot:<16} 0x{base + li:03X}')
    for i, (name, li) in enumerate(conv['decor'], 1):
        print(f'#define {p}_METATILE_DECOR_{i}{"":<10} 0x{base + li:03X}')


def main(argv):
    name = argv[0] if argv else 'lapis'
    spec = TILESETS[name]
    print(f'=== {name} -> {spec["out"]} ===')
    conv = convert(spec)

    ntiles, nmt = len(conv['bank'].tiles), len(conv['metatiles'])
    print(f'\n  {nmt} metatiles  ({512 - nmt} slots spare)')
    print(f'  {ntiles} tiles      ({512 - ntiles} slots spare)'
          f'{"   OVER BUDGET" if ntiles > 512 else ""}')
    print(f'  dedupe saved {nmt * 4 - ntiles} of {nmt * 4} quadrants')

    missing = [s for s in SLOTS if s not in conv['slotmap']]
    print(f'\n  {len(conv["slotmap"])}/{len(SLOTS)} PaintWalls slots matched'
          + (f'   MISSING: {missing}' if missing else ''))
    print(f'  floor metatile: local {conv["floor"]}'
          if conv['floor'] is not None else '  floor: NOT FOUND')
    for nm, li in conv['decor']:
        print(f'  {nm}: local {li}')

    if '--write' in argv:
        if ntiles > 512:
            raise SystemExit('refusing to write: over the 512-tile budget')
        if conv['floor'] is None:
            raise SystemExit('refusing to write: no plain floor found')
        n, m = write_tileset(spec, conv)
        print(f'\n  wrote {spec["out"]}  ({n} tiles, {m} metatiles)')
    if '--decls' in argv:
        print_decls(spec, conv, ntiles)


if __name__ == '__main__':
    main(sys.argv[1:])
