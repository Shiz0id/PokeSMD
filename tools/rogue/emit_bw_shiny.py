"""
Emit the shiny colour map for every BW animation container.

A BW frame container carries ONE palette and it is the gif's, so a shiny
animated Pokemon showed normal colours - the one thing the whole sprite feature
could not express. There is no shiny gif set anywhere to emit from; the 1,253
directory rip is normal sprites only.

WHAT IS SHIPPED IS NOT A PALETTE. It is a permutation: eight bytes per
container holding, for each of the container's 16 palette slots, the index of
the STOCK shiny palette entry that slot should take. The runtime then builds
the shiny palette with sixteen indexed loads out of
gSpeciesInfo[species].shinyPalette, which every species already carries in ROM
for its stock sprite. So the animated shiny costs 8 bytes of ROM per container
and no new colour data at all.

WHY A PERMUTATION IS ENOUGH, and this is the finding the whole approach rests
on: this build has P_GBA_STYLE_SPECIES_GFX FALSE, so the shipped sprites are
the Gen 4/5 art - the same generation the BW gifs were ripped from. Measured
over the roster, 123 of 386 species have all fifteen container colours present
VERBATIM in the stock normal palette and 181 have fourteen or more. Where a
colour matches exactly, handing it the stock shiny colour of the matched index
is not an approximation of the official shiny palette, it IS the official
shiny palette.

HOW THE MAP IS CHOSEN. Each container colour is matched to the stock NORMAL
colour it is nearest in OkLab, as a minimum-cost bijection over the whole
palette rather than one nearest neighbour at a time. Both halves earn their
keep and both were arrived at by rendering:

  * OkLab rather than RGB - RGB distance skewed Regirock's greys to olive.
  * A bijection rather than per-colour nearest - two container colours landing
    on one stock index is what made Magcargo's body inherit its shell's
    recolour. Under a bijection no two slots can claim one stock colour.
  * OPTIMAL rather than greedy - greedy claims the single best pair first and
    can strand a colour that had one good option left. They disagree on 71 of
    386 species, Regirock on nine indices of fifteen.

Three alternatives were built and rejected on the rendered result, which is
worth recording so they are not retried:

  * Adding the stock shiny/normal RGB DIFFERENCE to the container colour. It
    preserves the gif's own shading and is exact wherever the match is exact,
    but an addition cannot rotate a hue: Koffing's purple body plus a
    purple->teal difference lands on grey, not teal.
  * Transferring the recolour in OkLCH - rotate hue by the pair's hue
    difference, scale chroma by their ratio. It fixed Magcargo and broke
    Regirock and Spinda outright, because hue is meaningless at zero chroma
    and most of a rock Pokemon is near-grey. Same trap the colour variants
    feature already documents.
  * Matching by WHERE a colour appears, using the shipped sprite as a
    labelling of the container frame. The two are aligned on the bounding box,
    but not tightly enough - every Koffing index voted for one stock index.

Run:  python emit_bw_shiny.py --repo ~/decomps/pokeemerald-expansion

Needs nothing but the standard library, and reads only files that are checked
in - the container PNG's palette chunk and the species' normal.pal/shiny.pal.
That is deliberate: emit_bw_anim.py cannot be re-run without the 1,253 gif set,
which does not live in the repo, and a generated file nobody can regenerate is
a liability.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

PAL_SIZE = 16


# ---------------------------------------------------------------------------
# Asset reading. No Pillow: a PNG palette is one uncompressed chunk, and the
# check that guards this file has to run under run_all_checks.sh, which has no
# third-party packages available to it.

def png_palette(path):
    """The PLTE chunk as 16 (r, g, b) triples, quantised to 5 bits per channel.

    Quantised the way gbagfx does it - DOWNCONVERT_BIT_DEPTH is a divide by 8 -
    because the map is chosen against the values the GBA will actually hold,
    not against the 8-bit ones in the file.
    """
    data = path.read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path} is not a PNG')
    off = 8
    while off < len(data):
        length, kind = struct.unpack('>I4s', data[off:off + 8])
        if kind == b'PLTE':
            raw = data[off + 8:off + 8 + length]
            pal = [(raw[i] // 8, raw[i + 1] // 8, raw[i + 2] // 8)
                   for i in range(0, len(raw), 3)]
            pal += [(0, 0, 0)] * (PAL_SIZE - len(pal))
            return pal[:PAL_SIZE]
        off += 12 + length
    raise ValueError(f'{path} has no PLTE chunk')


def jasc_palette(path):
    """A JASC-PAL as 16 (r, g, b) triples, quantised to 5 bits per channel."""
    tok = path.read_text(encoding='utf-8', errors='replace').split()
    if tok[0] != 'JASC-PAL':
        raise ValueError(f'{path} is not a JASC palette')
    count = int(tok[2])
    vals = tok[3:3 + count * 3]
    pal = [(int(vals[i * 3]) // 8, int(vals[i * 3 + 1]) // 8, int(vals[i * 3 + 2]) // 8)
           for i in range(count)]
    pal += [(0, 0, 0)] * (PAL_SIZE - len(pal))
    return pal[:PAL_SIZE]


def used_slots(pal):
    """Which container palette slots hold a real colour.

    emit_bw_anim.py builds the palette as [magenta] + sorted(unique colours)
    and pads the tail with black. Because it SORTS, a genuine black is the
    smallest colour and lands at index 1 - so black at the tail is always
    padding and never art, and the used count is recoverable from the palette
    alone. That is what lets this tool avoid decoding pixel data.

    Index 0 is the transparent slot and is never drawn.
    """
    last = PAL_SIZE - 1
    while last >= 1 and pal[last] == (0, 0, 0):
        last -= 1
    return list(range(1, last + 1))


# ---------------------------------------------------------------------------
# Colour maths. Host-side only - nothing below this line ships.

def _srgb_to_linear(v5):
    c = v5 / 31.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


_OKLAB_CACHE = {}


def oklab(c5):
    got = _OKLAB_CACHE.get(c5)
    if got is None:
        r, g, b = (_srgb_to_linear(x) for x in c5)
        l = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b) ** (1 / 3)
        m = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b) ** (1 / 3)
        s = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b) ** (1 / 3)
        got = (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
               1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
               0.0259040371 * l - 0.7827717662 * m + 0.8086757660 * s)
        _OKLAB_CACHE[c5] = got
    return got


def oklab_d2(a, b):
    A, B = oklab(a), oklab(b)
    return (A[0] - B[0]) ** 2 + (A[1] - B[1]) ** 2 + (A[2] - B[2]) ** 2


def hungarian(cost):
    """Minimum-cost assignment for a rectangular cost matrix (rows <= cols).

    The O(n^3) shortest-augmenting-path form with potentials. Written out
    rather than taken from scipy because this tool is meant to be runnable
    with nothing installed, and 15x15 does not justify a dependency.

    Returns a list mapping row -> column.
    """
    n = len(cost)
    m = len(cost[0])
    assert n <= m
    INF = float('inf')
    u = [0.0] * (n + 1)
    v = [0.0] * (m + 1)
    p = [0] * (m + 1)          # column -> row
    way = [0] * (m + 1)
    for i in range(1, n + 1):
        p[0] = i
        j0 = 0
        minv = [INF] * (m + 1)
        used = [False] * (m + 1)
        while True:
            used[j0] = True
            i0 = p[j0]
            delta = INF
            j1 = 0
            for j in range(1, m + 1):
                if used[j]:
                    continue
                cur = cost[i0 - 1][j - 1] - u[i0] - v[j]
                if cur < minv[j]:
                    minv[j] = cur
                    way[j] = j0
                if minv[j] < delta:
                    delta = minv[j]
                    j1 = j
            for j in range(m + 1):
                if used[j]:
                    u[p[j]] += delta
                    v[j] -= delta
                else:
                    minv[j] -= delta
            j0 = j1
            if p[j0] == 0:
                break
        while j0:
            j1 = way[j0]
            p[j0] = p[j1]
            j0 = j1
    out = [0] * n
    for j in range(1, m + 1):
        if p[j]:
            out[p[j] - 1] = j - 1
    return out


def build_map(container_pal, normal_pal):
    """Container slot -> stock palette index, as 16 nibbles.

    Slot 0 is the transparent one and maps to stock index 0, which is the
    shipped sprite's own transparent entry - so neither is ever drawn and the
    two agree about that. Unused slots map to 0 for the same reason.
    """
    used = used_slots(container_pal)
    out = [0] * PAL_SIZE
    if used:
        cost = [[oklab_d2(container_pal[j], normal_pal[i]) for i in range(1, PAL_SIZE)]
                for j in used]
        for row, col in enumerate(hungarian(cost)):
            out[used[row]] = col + 1
    return out


# ---------------------------------------------------------------------------
# The generated header.

CONTAINER_RE = re.compile(
    r'const u32 gBwAnimGfx_(\w+)\[\] = INCGFX_U32\('
    r'"graphics/pokemon/([\w/]+)/(bw_anim(?:_back)?\.png)"')
ROW_RE = re.compile(
    r'\.species = (\w+),\s*//\s*(\d+)\s*\n\s*\.frames = gBwAnimGfx_(\w+),')


def read_containers(repo):
    """Every container declared by src/data/rogue_bw_anim.h, front and back.

    Parsed out of the generated header rather than re-derived from a species
    list, so this tool always emits maps for exactly the roster that exists.
    A species added to the animation table and missing here is a build error,
    not a silently unmapped shiny.
    """
    src = repo / 'src/data/rogue_bw_anim.h'
    txt = src.read_text(encoding='utf-8')
    sym = {m.group(1): (m.group(2), m.group(3)) for m in CONTAINER_RE.finditer(txt)}

    head, _, tail = txt.partition('static const struct BwAnim sBwAnimsBack')
    out = {False: [], True: []}
    for back, chunk in ((False, head), (True, tail)):
        for m in ROW_RE.finditer(chunk):
            species, dex, name = m.group(1), int(m.group(2)), m.group(3)
            if name not in sym:
                raise SystemExit(f'{species}: no container declared for {name}')
            d, asset = sym[name]
            out[back].append(dict(species=species, dex=dex, sym=name, dir=d, asset=asset))
    return out


def emit(repo, verbose=False):
    containers = read_containers(repo)
    stats = dict(exact=0, slots=0, full=0, total=0)
    tables = {}
    for back, rows in containers.items():
        emitted = []
        for r in rows:
            g = repo / 'graphics/pokemon' / r['dir']
            cont = png_palette(g / r['asset'])
            normal = jasc_palette(g / 'normal.pal')
            m = build_map(cont, normal)
            used = used_slots(cont)
            exact = sum(1 for j in used if cont[j] == normal[m[j]])
            stats['exact'] += exact
            stats['slots'] += len(used)
            stats['total'] += 1
            if used and exact == len(used):
                stats['full'] += 1
            emitted.append((r, m, exact, len(used)))
            if verbose:
                print(f"  {r['dir']:18s} {'back' if back else 'front':5s} "
                      f"{exact:2d}/{len(used):2d} exact")
        tables[back] = emitted

    L = [
        '#ifndef GUARD_DATA_ROGUE_BW_SHINY_H',
        '#define GUARD_DATA_ROGUE_BW_SHINY_H',
        '',
        '// GENERATED by tools/rogue/emit_bw_shiny.py - do not edit by hand.',
        '//',
        '// One row per BW animation container: for each of the container\'s 16',
        '// palette slots, the index of the STOCK shiny palette entry that slot',
        '// takes. Packed two slots to a byte, low nibble first.',
        '//',
        '// A container carries the gif\'s palette and only that, so a shiny',
        '// animated Pokemon used to show normal colours. The runtime rebuilds',
        '// its palette out of gSpeciesInfo[species].shinyPalette through this',
        '// map, which costs 8 bytes per container and no new colour data - the',
        '// shiny palette itself is already in ROM for the stock sprite.',
        '//',
        '// KEYED BY SPECIES AND BISECTED SEPARATELY, not held in struct BwAnim',
        '// and not parallel to it by index. Two generated tables coupled by',
        '// position drift silently, and the failure would be one species wearing',
        '// another\'s shiny colours - which no build and no check would see.',
        '',
        '#if !defined(ROGUE_BW_ANIM_BACK)',
        '#error "include rogue_bw_anim.h before data/rogue_bw_shiny.h"',
        '#endif',
        '',
    ]
    for back, label, name in ((False, 'Front', 'sBwShinyMaps'),
                              (True, 'Back', 'sBwShinyMapsBack')):
        rows = tables.get(back, [])
        if back:
            L.append('#if ROGUE_BW_ANIM_BACK')
        L.append(f'// {label} containers, sorted by species id - GetBwShinyMap bisects this.')
        L.append(f'static const struct BwShinyMap {name}[] =')
        L.append('{')
        for r, m, exact, used in sorted(rows, key=lambda t: t[0]['dex']):
            packed = ', '.join(f'0x{(m[k] | (m[k + 1] << 4)):02X}' for k in range(0, PAL_SIZE, 2))
            L.append(f'    {{ {r["species"]}, {{ {packed} }} }},'
                     f'   // {r["dex"]}, {exact}/{used} exact')
        if not rows:
            L.append('    { SPECIES_NONE, { 0 } },')
        L.append('};')
        if back:
            L.append('#endif // ROGUE_BW_ANIM_BACK')
        L.append('')
    L.append('#endif // GUARD_DATA_ROGUE_BW_SHINY_H')

    p = repo / 'src/data/rogue_bw_shiny.h'
    p.write_text('\n'.join(L) + '\n', encoding='utf-8', newline='\n')
    return p, stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=str(Path(__file__).resolve().parents[2]))
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args()
    repo = Path(args.repo).expanduser()

    p, s = emit(repo, args.verbose)
    print(f'wrote {p}')
    print(f'  {s["total"]} containers')
    print(f'  {s["exact"]}/{s["slots"]} slots ({100.0 * s["exact"] / max(1, s["slots"]):.1f}%) '
          f'match a stock colour exactly')
    print(f'  {s["full"]} containers take the official shiny palette verbatim')


if __name__ == '__main__':
    main()
