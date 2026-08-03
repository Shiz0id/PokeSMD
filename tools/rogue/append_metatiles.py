"""
Append every metatile this project adds to the Cave secondary tileset.

ONE appender, deliberately. It is idempotent by truncating everything past the
vanilla 414 and rewriting the tail, so a second script appending to the same
file would be silently wiped the next time this one ran. Anything that needs a
new cave metatile exports its entries and is imported here instead.

Sources, in the order that fixes the appended ids - do not reorder:
  compose_metatiles  the seven spliced 1-wide wall slivers, 0x39E-0x3A4
  make_glacia_snow   flat snow, a 2-wide drift and an ice rock

Writes both metatiles.bin and metatile_attributes.bin, which must stay the same
length in entries.
"""
import struct
from pathlib import Path
import compose_metatiles as cm
import make_glacia_snow as snow
from tileset_resolve import TilesetResolver
import tileset_atlas as ta

R = TilesetResolver(ta.REPO)
CAVE = R.resolve('gTileset_Cave')
MT_PATH = CAVE['metatiles']
AT_PATH = CAVE['attributes']

BASE_COUNT = 414          # vanilla cave metatile count

# (name, entry, metatile to copy the ATTRIBUTE from). The attribute carries the
# behaviour, and behaviour is what gates encounters - the slivers are wall and
# must behave like wall, the snow is floor and must keep MB_CAVE or Glacia's
# floors spawn nothing. One shared attribute for both would be the same class
# of bug as a hard-coded per-theme constant.
NEW = ([(name, ent, 0x211) for name, ent in cm.APPEND_ORDER]
       + [(name, ent, 0x201) for name, ent in snow.append_order()])


def main():
    mt = bytearray(MT_PATH.read_bytes())
    at = bytearray(AT_PATH.read_bytes())
    count = len(mt) // 16

    # Compare the CONTENT of the tail, not just its length. Counting was enough
    # while the entries never changed, but it means an edited entry re-runs to
    # "nothing to do" and the change silently never lands - which is exactly
    # what happened when the snow metatiles were first appended pointing at the
    # wrong tiles.
    want = b''.join(struct.pack('<8H', *ent) for _, ent, _ in NEW)
    if count == BASE_COUNT + len(NEW) and bytes(mt[BASE_COUNT * 16:]) == want:
        print(f'already appended ({count} metatiles, tail matches) - nothing to do')
        return

    # Truncate any previous append so this stays idempotent as the set of
    # appended metatiles grows or changes.
    if count > BASE_COUNT:
        print(f'trimming {count - BASE_COUNT} previously appended metatiles')
        del mt[BASE_COUNT * 16:]
        del at[BASE_COUNT * 2:]
    elif count != BASE_COUNT:
        raise SystemExit(f'unexpected metatile count {count}')

    def attr_of(src):
        return struct.unpack_from('<H', at, (src - 0x200) * 2)[0]

    for src in (0x211, 0x201):
        a = attr_of(src)
        print(f'0x{src:03X} attribute = 0x{a:04X} (behavior {a & 0xFF}, '
              f'layer {(a >> 12) & 0xF})')

    for name, ent, src in NEW:
        gid = 0x200 + len(mt) // 16
        mt += struct.pack('<8H', *ent)
        at += struct.pack('<H', attr_of(src))
        print(f'  appended {name:18} as 0x{gid:03X}  (attr from 0x{src:03X})')

    MT_PATH.write_bytes(bytes(mt))
    AT_PATH.write_bytes(bytes(at))
    print(f'metatiles.bin  {len(mt)} bytes = {len(mt)//16} metatiles')
    print(f'attributes.bin {len(at)} bytes = {len(at)//2} entries')
    assert len(mt) // 16 == len(at) // 2, 'metatile/attribute count mismatch'
    assert len(mt) // 16 <= 512, 'exceeds secondary tileset capacity'


if __name__ == '__main__':
    main()
