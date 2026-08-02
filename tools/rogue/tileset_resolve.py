"""
Resolve gTileset_* symbols to their actual asset paths.

Guessing paths from the symbol name does not work: directory names carry an
`_frlg` suffix the symbol lacks, digits and acronyms split unpredictably, and
crucially some tilesets SHARE assets with others. gTileset_SilphCo takes its
tiles and palettes from condominiums_frlg but its metatiles from silph_co_frlg.
So we parse the declarations instead.

  src/data/tilesets/headers.h    gTileset_X -> component symbols
  src/data/tilesets/graphics.h   gTilesetTiles_Y / gTilesetPalettes_Y -> paths
  src/data/tilesets/metatiles.h  gMetatiles_Y / gMetatileAttributes_Y -> paths
"""
import re
from pathlib import Path


class TilesetResolver:
    def __init__(self, repo: Path):
        self.repo = repo
        d = repo / 'src/data/tilesets'
        hdr = (d / 'headers.h').read_text(errors='replace')

        # Asset declarations are split across the tilesets dir AND src/graphics.c
        # (gTilesetTiles_General lives in the latter), so scan both.
        sources = [d / 'graphics.h', d / 'metatiles.h', repo / 'src/graphics.c']
        blob = '\n'.join(p.read_text(errors='replace') for p in sources if p.exists())

        # single-file INCBIN/INCGFX declarations; the type may carry ALIGNED(n)
        self.asset = {}
        for m in re.finditer(
                r'const\s+\w+\s+(?:ALIGNED\(\d+\)\s+)?(\w+)\s*\[\]\s*='
                r'\s*INC(?:BIN|GFX)_U\d+\(\s*"([^"]+)"', blob):
            self.asset[m.group(1)] = m.group(2)

        # palette arrays: symbol[][16] = { INCGFX("...00.pal"), ... }
        self.palette_sets = {}
        for m in re.finditer(
                r'const\s+u16\s+(?:ALIGNED\(\d+\)\s+)?(\w+)\s*\[\]\[16\]\s*=\s*\{(.*?)\};',
                blob, re.S):
            paths = re.findall(r'"([^"]+\.pal)"', m.group(2))
            if paths:
                self.palette_sets[m.group(1)] = paths

        # tileset structs
        self.tilesets = {}
        for m in re.finditer(
                r'const\s+struct\s+Tileset\s+(gTileset_\w+)\s*=\s*\{(.*?)\};', hdr, re.S):
            body = m.group(2)

            def field(name):
                f = re.search(rf'\.{name}\s*=\s*([A-Za-z_]\w*)', body)
                return f.group(1) if f else None

            sec = re.search(r'\.isSecondary\s*=\s*(TRUE|FALSE)', body)
            self.tilesets[m.group(1)] = {
                'tiles': field('tiles'),
                'palettes': field('palettes'),
                'metatiles': field('metatiles'),
                'attributes': field('metatileAttributes'),
                'is_secondary': bool(sec and sec.group(1) == 'TRUE'),
            }

    def resolve(self, symbol):
        """-> dict of absolute paths, or None if the symbol is unknown."""
        ts = self.tilesets.get(symbol)
        if not ts:
            return None
        tiles = self.asset.get(ts['tiles'])
        metatiles = self.asset.get(ts['metatiles'])
        attrs = self.asset.get(ts['attributes'])
        pals = self.palette_sets.get(ts['palettes'], [])
        if not (tiles and metatiles and attrs):
            return None
        return {
            'symbol': symbol,
            'is_secondary': ts['is_secondary'],
            'tiles': self.repo / tiles,
            'metatiles': self.repo / metatiles,
            'attributes': self.repo / attrs,
            'palettes': [self.repo / p for p in pals],
            'dir_hint': Path(metatiles).parent.name,
        }

    def report(self):
        total = len(self.tilesets)
        good = sum(1 for s in self.tilesets if self.resolve(s))
        return total, good, [s for s in self.tilesets if not self.resolve(s)]
