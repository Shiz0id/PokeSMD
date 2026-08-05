"""Base stats, typing and evolution method for Eevee and its eight branches.

Eevee is a starter pick, so the question this answers is the same one asked of
the Clefairy and Pikachu lines: does the branch reach starter-final power, and
can a run actually get there. Resolves the `P_UPDATED_STATS >= GEN_n ? a : b`
ternaries and the named stat macros against the configured gen, because a bare
\\d+ pattern takes the wrong branch on most of these entries.

Usage:  python3 tools/rogue/eeveelution_stats.py [--repo PATH]
"""
import argparse
import os
import re
from pathlib import Path

STAT_KEYS = ['baseHP', 'baseAttack', 'baseDefense', 'baseSpAttack',
             'baseSpDefense', 'baseSpeed']
EEVEELUTIONS = ['EEVEE', 'VAPOREON', 'JOLTEON', 'FLAREON', 'ESPEON',
                'UMBREON', 'LEAFEON', 'GLACEON', 'SYLVEON']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=os.environ.get('POKEDECOMP_REPO', '.'))
    args = ap.parse_args()
    repo = Path(args.repo).resolve()
    info = repo / 'src/data/pokemon/species_info'

    cfg = (repo / 'include/config/pokemon.h').read_text(errors='replace')
    m = re.search(r'#define\s+P_UPDATED_STATS\s+(\w+)', cfg)
    tok = m.group(1)
    stats_gen = 9 if tok == 'GEN_LATEST' else int(re.search(r'GEN_(\d+)', tok).group(1))

    macros = {}
    for p in sorted(info.glob('*.h')):
        gate = None
        for line in p.read_text(errors='replace').splitlines():
            g = re.match(r'\s*#if\s+P_UPDATED_STATS\s*>=\s*GEN_(\d+)', line)
            if g:
                gate = stats_gen >= int(g.group(1))
                continue
            if re.match(r'\s*#el(se|if)', line) and gate is not None:
                gate = not gate
                continue
            if re.match(r'\s*#endif', line):
                gate = None
                continue
            d = re.match(r'\s*#define\s+(\w+)\s+(.+)', line)
            if d and gate is not False:
                macros.setdefault(d.group(1), d.group(2).strip())

    def value(expr, depth=0):
        expr = expr.strip().rstrip(',').strip('()')
        t = re.match(r'.*?P_UPDATED_STATS\s*>=\s*GEN_(\d+).*?\?\s*(\d+)\s*:\s*(\d+)', expr)
        if t:
            return int(t.group(2)) if stats_gen >= int(t.group(1)) else int(t.group(3))
        n = re.fullmatch(r'(\d+)', expr)
        if n:
            return int(n.group(1))
        w = re.fullmatch(r'([A-Z][A-Z0-9_]*)', expr)
        if w and depth < 4 and w.group(1) in macros:
            return value(macros[w.group(1)], depth + 1)
        return None

    mons = {}
    for path in sorted(info.glob('*.h')):
        text = path.read_text(errors='replace')
        parts = re.split(r'\n\s*\[SPECIES_(\w+)\]\s*=', text)
        for name, body in zip(parts[1::2], parts[2::2]):
            if name not in EEVEELUTIONS or name in mons:
                continue
            body = body[:20000]
            stats = {}
            for k in STAT_KEYS:
                mm = re.search(r'\.%s\s*=\s*([^\n]+)' % k, body)
                if mm:
                    v = value(mm.group(1))
                    if v is not None:
                        stats[k] = v
            ty = re.search(r'\.types\s*=\s*MON_TYPES\(([^)]*)\)', body)
            mons[name] = {
                'stats': stats,
                'bst': sum(stats.values()) if len(stats) == 6 else None,
                'types': '/'.join(t.title() for t in re.findall(r'TYPE_(\w+)', ty.group(1))) if ty else '?',
            }

    evo = {}
    text = (info / 'gen_1_families.h').read_text(errors='replace')
    parts = re.split(r'\n\s*\[SPECIES_(\w+)\]\s*=', text)
    for name, body in zip(parts[1::2], parts[2::2]):
        if name != 'EEVEE':
            continue
        block = body[:body.find('.abilities')] if '.abilities' in body else body[:8000]
        for kind, arg, target in re.findall(
                r'\{(EVO_\w+),\s*([^,]+),\s*SPECIES_(\w+)([^}]*)\}', block):
            evo[target] = f'{kind.replace("EVO_", "")} {arg.strip()}'
        for target, cond in re.findall(
                r'SPECIES_(\w+),\s*CONDITIONS\(\{([^}]*)\}', block):
            if target in evo:
                evo[target] += f'  [{cond.strip()}]'

    print(f'P_UPDATED_STATS = GEN_{stats_gen}\n')
    hdr = f'{"":<10} {"type":<16} {"HP":>3} {"Atk":>4} {"Def":>4} {"SpA":>4} {"SpD":>4} {"Spe":>4} {"BST":>5}  how'
    print(hdr)
    print('-' * len(hdr))
    for name in EEVEELUTIONS:
        d = mons.get(name)
        if not d or d['bst'] is None:
            print(f'{name.title():<10} MISSING')
            continue
        s = d['stats']
        print(f'{name.title():<10} {d["types"]:<16} '
              + ' '.join(f'{s[k]:>4}' for k in STAT_KEYS)
              + f' {d["bst"]:>5}  {evo.get(name, "-")}')

    branches = [mons[n]['bst'] for n in EEVEELUTIONS[1:] if mons.get(n) and mons[n]['bst']]
    if branches:
        print(f'\nbranches: {len(branches)}, BST min {min(branches)}, max {max(branches)}, '
              f'all equal: {len(set(branches)) == 1}')
        print('trio-starter finals for comparison: 525-535, avg 526')


if __name__ == '__main__':
    main()
