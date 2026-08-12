"""Attribute EWRAM/IWRAM usage to source files, from the linker map."""
import re, collections
from pathlib import Path

# Globbed, not named: the ROM name is a Makefile setting and has already
# changed once. See tools/rogue/rom_paths.py.
MAP = sorted(Path(__file__).resolve().parents[2].glob('*.map'))[0]

lines = MAP.read_text(errors='replace').split('\n')

# The map lists output sections in order; track which region we are inside.
region = None
sizes = collections.Counter()

sec_re = re.compile(r'^(\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)')
inp_re = re.compile(r'^\s+\.(?:bss|data|sbss|common)[.\w]*\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)')

for l in lines:
    m = sec_re.match(l)
    if m:
        name = m.group(1)
        if name in ('ewram_data', '.ewram', 'ewram'):
            region = 'EWRAM'
        elif name in ('iwram_data', '.bss', '.iwram', 'iwram'):
            region = 'IWRAM'
        elif name.startswith('.') and region and not name.startswith(('.bss', '.data')):
            pass
    m = inp_re.match(l)
    if m and region:
        size = int(m.group(2), 16)
        obj = m.group(3).split('/')[-1]
        if size:
            sizes[(region, obj)] += size

for reg in ('EWRAM', 'IWRAM'):
    items = sorted(((v, f) for (r, f), v in sizes.items() if r == reg), reverse=True)
    tot = sum(v for v, _ in items)
    print(f'=== {reg} — attributed {tot:,} bytes across {len(items)} objects ===')
    shown = 0
    for v, f in items[:18]:
        print(f'  {v:>9,}  {f}')
        shown += v
    print(f'  {tot-shown:>9,}  (everything else)')
    print()
