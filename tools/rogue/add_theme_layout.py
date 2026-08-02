"""
Add a donor layout for a new dungeon theme.

A theme needs a layout entry only so the engine has a tileset pair to load; the
blocks are overwritten at runtime by the generator. Nothing warps to it and no
map points at it.

Creates data/layouts/<Name>/{border,map}.bin and appends the entry to
layouts.json, matching the file's two-space formatting rather than reserialising
it, so the diff stays to the lines actually added.

Usage:
  python add_theme_layout.py ROGUE_DUNGEON_NEWMAUVILLE RogueDungeonNewMauville \\
      gTileset_General gTileset_BikeShop --fill 0x208
"""
import argparse
import json
import struct
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LAYOUTS = REPO / 'data/layouts/layouts.json'

TEMPLATE = """    {{
      "id": "LAYOUT_{id}",
      "name": "{name}_Layout",
      "width": {w},
      "height": {h},
      "primary_tileset": "{primary}",
      "secondary_tileset": "{secondary}",
      "border_filepath": "data/layouts/{name}/border.bin",
      "blockdata_filepath": "data/layouts/{name}/map.bin"
    }}"""


def main():
    p = argparse.ArgumentParser()
    p.add_argument('id')
    p.add_argument('name')
    p.add_argument('primary')
    p.add_argument('secondary')
    p.add_argument('--width', type=int, default=48)
    p.add_argument('--height', type=int, default=48)
    p.add_argument('--fill', default='0x000',
                   help='metatile id for every block; the generator overwrites it')
    a = p.parse_args()

    data = json.loads(LAYOUTS.read_text(encoding='utf-8'))
    if any(e and e.get('id') == f'LAYOUT_{a.id}' for e in data['layouts']):
        raise SystemExit(f'LAYOUT_{a.id} already present - nothing to do')

    fill = int(a.fill, 0)
    block = fill | (1 << 10)                 # collision 1, elevation 0
    d = REPO / 'data/layouts' / a.name
    d.mkdir(parents=True, exist_ok=True)
    (d / 'map.bin').write_bytes(struct.pack(f'<{a.width * a.height}H',
                                            *([block] * (a.width * a.height))))
    (d / 'border.bin').write_bytes(struct.pack('<4H', *([block] * 4)))

    text = LAYOUTS.read_text(encoding='utf-8')
    marker = '\n  ]\n}'
    if not text.endswith(marker) and not text.endswith(marker + '\n'):
        raise SystemExit('layouts.json does not end as expected; add by hand')
    entry = TEMPLATE.format(id=a.id, name=a.name, w=a.width, h=a.height,
                            primary=a.primary, secondary=a.secondary)
    text = text.replace(marker, ',\n' + entry + marker)
    LAYOUTS.write_text(text, encoding='utf-8', newline='\n')

    print(f'added LAYOUT_{a.id}  {a.width}x{a.height}  '
          f'{a.primary} + {a.secondary}')
    print(f'  wrote {d}/map.bin ({a.width * a.height * 2} bytes) and border.bin')
    print(f'\nnext: add the theme to sDungeonThemes in src/rogue_dungeon.c')


if __name__ == '__main__':
    main()
