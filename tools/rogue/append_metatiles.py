"""
Append the two spliced sliver metatiles to the Cave secondary tileset.

Idempotent: re-running will not double-append. Writes both metatiles.bin and
metatile_attributes.bin, which must stay the same length in entries.
"""
import struct
from pathlib import Path
import compose_metatiles as cm
from tileset_resolve import TilesetResolver
import tileset_atlas as ta

R = TilesetResolver(ta.REPO)
CAVE = R.resolve('gTileset_Cave')
MT_PATH = CAVE['metatiles']
AT_PATH = CAVE['attributes']

BASE_COUNT = 414          # vanilla cave metatile count
NEW = cm.APPEND_ORDER


def main():
    mt = bytearray(MT_PATH.read_bytes())
    at = bytearray(AT_PATH.read_bytes())
    count = len(mt) // 16

    if count == BASE_COUNT + len(NEW):
        print(f'already appended ({count} metatiles) - nothing to do')
        return
    # Truncate any partial previous append so this stays idempotent as the set
    # of composed metatiles grows.
    if count > BASE_COUNT:
        print(f'trimming {count - BASE_COUNT} previously appended metatiles')
        del mt[BASE_COUNT * 16:]
        del at[BASE_COUNT * 2:]
    elif count != BASE_COUNT:
        raise SystemExit(f'unexpected metatile count {count}')

    # Copy 0x211's attribute so the slivers behave exactly like ordinary wall
    # interior; only the art differs.
    interior_idx = 0x211 - 0x200
    interior_attr = struct.unpack_from('<H', at, interior_idx * 2)[0]
    print(f'0x211 attribute = 0x{interior_attr:04X} (behavior '
          f'{interior_attr & 0xFF}, layer {(interior_attr >> 12) & 0xF})')

    for name, ent in NEW:
        gid = 0x200 + len(mt) // 16
        mt += struct.pack('<8H', *ent)
        at += struct.pack('<H', interior_attr)
        print(f'  appended {name:18} as 0x{gid:03X}')

    MT_PATH.write_bytes(bytes(mt))
    AT_PATH.write_bytes(bytes(at))
    print(f'metatiles.bin  {len(mt)} bytes = {len(mt)//16} metatiles')
    print(f'attributes.bin {len(at)} bytes = {len(at)//2} entries')
    assert len(mt) // 16 == len(at) // 2, 'metatile/attribute count mismatch'
    assert len(mt) // 16 <= 512, 'exceeds secondary tileset capacity'


if __name__ == '__main__':
    main()
