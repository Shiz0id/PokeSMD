"""Which of our floor behaviours actually carry TILE_FLAG_HAS_ENCOUNTERS."""
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
text = (REPO / 'src/metatile_behavior.c').read_text(errors='replace')
body = re.search(r'sTileBitAttributes\[[^\]]*\]\s*=\s*\{(.*?)\n\};', text, re.S).group(1)

for name in ('MB_NORMAL', 'MB_TALL_GRASS', 'MB_LONG_GRASS', 'MB_CAVE'):
    m = re.search(r'\[' + name + r'\]\s*=\s*([^,\n]*)', body)
    entry = m.group(1).strip() if m else '(absent -> 0)'
    has = 'HAS_ENCOUNTERS' in entry
    print(f'  {name:16} {"ENCOUNTERS" if has else "no encounters":14} {entry}')
