"""
Emit checked-in BW animation assets for a list of species.

Per species this writes:
  graphics/pokemon/<name>/bw_anim.png   indexed frame sheet, frames stacked
                                        vertically, index 0 transparent
and appends to a generated header:
  include/constants/rogue_bw_anim.h     frame count, size, and the playback
                                        sequence as (frame, hold) pairs

WHY THE FRAMES ARE ONE SHEET rather than one file each. Stacking them in a
single blob is what lets the compressor exploit redundancy BETWEEN frames, and
that is most of the win: Treecko's 26 frames compress to 14% of raw where a
lone frame manages ~28%. Measured across 22 species the mean is 4,062 B for a
whole species - so Gen 1-5 is 2.4 MB, not the 11 MB a per-frame projection
predicted.

HOLD IS IN VIDEO FRAMES, not milliseconds. BW timings are 70-600 ms against a
16.74 ms video frame; converting here means the engine never divides at
runtime. A hold is clamped to at least 1 so a frame cannot be skipped entirely.

FRAME COUNT IS CAPPED. compresSmol segfaults on very large sheets - Beedrill at
177 frames and Celebi at 129 both crashed it. Over the cap the sequence is
truncated at a loop boundary rather than the whole species being dropped.

Run:  python emit_bw_anim.py --gifs D:/PokemonTest/gifs --species geodude,nosepass
      python emit_bw_anim.py --gifs ... --preset test
Needs Pillow. Run `make` afterwards - gbagfx and compresSmol run from the build.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import downscale_bw as D

VIDEO_FRAME_MS = 1000.0 / 59.7275
MAX_FRAMES = 96          # under the size that crashes compresSmol
MON_PIC_WIDTH = 64       # must match include/constants/pokemon.h
MON_PIC_HEIGHT = 64

# The species this is being proved on. Roxanne is floor 10 and reachable in
# minutes; Tate and Liza are the double battle, which is the worst case the
# streaming design was judged against.
#
# ONLY OPPONENTS ANIMATE. The gif set is front sprites only - one gif per
# species, no backs - and the player's own Pokemon show their back sprite. So
# the starters below animate only when the run MEETS them (a rival, a wild
# encounter), never in the player's own slot. That also caps concurrent decodes
# at two even in a double battle, which is why the doubles measurement is the
# real ceiling rather than a four-way worst case.
PRESETS = {
    'test': ['geodude', 'nosepass',              # Roxanne, floor 10, singles
             'claydol', 'xatu', 'lunatone', 'solrock',   # Tate & Liza, doubles
             'treecko', 'torchic', 'mudkip'],    # only as opponents
}


def c_name(stem):
    return ''.join(p.capitalize() for p in stem.replace('-', '_').split('_'))


def species_ids(repo):
    """SPECIES_X name -> numeric id, read from the generated species enum.

    Needed because the lookup table is bisected at runtime and therefore has to
    be in SPECIES ORDER. Sorting by name instead looks right and is not: it
    puts CLAYDOL before GEODUDE while their ids run the other way, and a binary
    search over that silently misses entries.
    """
    import re
    out = {}
    p = repo / 'include/constants/species.h'
    text = p.read_text(encoding='utf-8', errors='replace')
    n = 0
    for m in re.finditer(r'^\s*(SPECIES_[A-Z0-9_]+)\s*(?:=\s*(\d+))?\s*,',
                         text, re.M):
        if m.group(2) is not None:
            n = int(m.group(2))
        out.setdefault(m.group(1), n)
        n += 1
    return out


def stock_bbox(repo, stem):
    """Bounding box of the shipped 64x64 front sprite's first frame.

    Read as INDEXED with palette entry 0 as the transparent one, which is the
    GBA's rule. Converting to RGBA and taking the alpha channel looks equivalent
    and is not: these PNGs need carry no tRNS chunk, so every pixel comes back
    opaque and the box is the whole 64x64 - which silently centres every sprite
    and is only visible as a mon standing slightly wrong in game.
    """
    p = repo / 'graphics/pokemon' / stem / 'anim_front.png'
    if not p.exists():
        return None
    im = Image.open(p)
    if im.mode != 'P':
        return None
    px = im.load()
    x0, y0, x1, y1 = im.width, im.height, 0, 0
    for y in range(min(MON_PIC_HEIGHT, im.height)):
        for x in range(im.width):
            if px[x, y] != 0:
                x0, y0 = min(x0, x), min(y0, y)
                x1, y1 = max(x1, x + 1), max(y1, y + 1)
    return None if x1 <= x0 else (x0, y0, x1, y1)


def stock_placement(repo, stem, w, h):
    """Where in the 64x64 box to put a w x h frame.

    Matched to the sprite the game already draws, on CENTRE X and BOTTOM Y.
    Bottom rather than centre because a battler is positioned by its feet - the
    engine's y offsets assume the mon stands on the bottom of its box - so
    centring vertically would sink or float it relative to the static sprite it
    replaces, and the swap happens in front of the player at switch-in.
    """
    box = stock_bbox(repo, stem)
    if box is None:
        # Nothing to match: centre horizontally, stand on the bottom.
        return (MON_PIC_WIDTH - w) // 2, MON_PIC_HEIGHT - h
    x0, _y0, x1, y1 = box
    ox = round((x0 + x1) / 2 - w / 2)
    oy = y1 - h
    return (max(0, min(MON_PIC_WIDTH - w, ox)),
            max(0, min(MON_PIC_HEIGHT - h, oy)))


def emit_species(repo, gif_path, stem):
    built = D.build_species(str(gif_path))
    if not built:
        return None, 'no frames'
    frames, seq, durs, cols = built
    if len(cols) > 15:
        return None, f'{len(cols)} colours (4bpp allows 15)'

    # Truncate at a whole number of frames if the sheet would be too big.
    if len(frames) > MAX_FRAMES:
        keep = set(range(MAX_FRAMES))
        seq = [s for s in seq if s in keep] or [0]
        frames = frames[:MAX_FRAMES]

    w, h = frames[0].size
    if w > MON_PIC_WIDTH or h > MON_PIC_HEIGHT:
        return None, f'{w}x{h} exceeds the {MON_PIC_WIDTH}x{MON_PIC_HEIGHT} OBJ limit'

    # EVERY frame is padded to the full 64x64, not cropped to its own content.
    # A battle sprite is always MON_PIC_SIZE and the engine tiles it as 8 tiles
    # across; a 48 wide sheet is 6 tiles across, so writing one into a battler's
    # VRAM shifts every tile row and produces a diagonal smear rather than a
    # sprite. It is also what makes frameSize the same 2048 for every species,
    # which is what lets one chunk buffer size serve all of them.
    tw, th = MON_PIC_WIDTH, MON_PIC_HEIGHT
    ox, oy = stock_placement(repo, stem, w, h)

    sheet = Image.new('RGBA', (tw, th * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sheet.paste(f, (ox, i * th + oy), f)

    got = sheet.getcolors(300000) or []
    # Index 0 is the transparent slot; magenta so a bug shows up loudly rather
    # than blending in.
    palette = [(255, 0, 255)] + sorted({c[1][:3] for c in got if c[1][3] > 0})
    if len(palette) > 16:
        return None, f'{len(palette)-1} colours after packing'

    px = sheet.load()
    data = []
    for y in range(sheet.height):
        for x in range(sheet.width):
            c = px[x, y]
            data.append(0 if c[3] == 0 else palette.index(c[:3]))
    idx = Image.new('P', sheet.size, 0)
    idx.putdata(data)
    flat = []
    for c in palette + [(0, 0, 0)] * (16 - len(palette)):
        flat.extend(c)
    idx.putpalette(flat)

    outdir = repo / 'graphics/pokemon' / stem
    outdir.mkdir(parents=True, exist_ok=True)
    idx.save(outdir / 'bw_anim.png')

    steps = []
    for i, s in enumerate(seq):
        hold = max(1, round((durs[i] if i < len(durs) else 100) / VIDEO_FRAME_MS))
        if steps and steps[-1][0] == s:
            steps[-1][1] = min(255, steps[-1][1] + hold)   # merge repeats
        else:
            steps.append([s, min(255, hold)])

    return dict(stem=stem, sym=c_name(stem), w=tw, h=th, species=stem.upper(),
                frames=len(frames), steps=steps), None


def write_header(repo, built, ids):
    """Emit the whole data file: graphics, sequences and the lookup table.

    Sorted by species id so the lookup can bisect. Nine entries would scan fine
    linearly, but this is meant to reach several hundred and a linear scan per
    battler per frame is exactly the kind of cost that hides until it matters.
    """
    for b in built:
        key = 'SPECIES_' + b['stem'].upper()
        if key not in ids:
            raise SystemExit(f'{key} is not in the species enum')
        b['id'] = ids[key]
    built = sorted(built, key=lambda b: b['id'])
    L = [
        '#ifndef GUARD_DATA_ROGUE_BW_ANIM_H',
        '#define GUARD_DATA_ROGUE_BW_ANIM_H',
        '',
        '// GENERATED by tools/rogue/emit_bw_anim.py - do not edit by hand.',
        '//',
        '// BW-style animated battle sprites. A species listed here plays a',
        '// frame sequence; one absent keeps the stock two-frame sprite, so a',
        '// partial roster is always a valid build.',
        '//',
        '// hold is in VIDEO FRAMES, converted from the source gif\'s',
        '// milliseconds at emit time so nothing divides at runtime.',
        '//',
        '// The frames of a species are a mode 7 frame CONTAINER, not one',
        '// asset each and not one flat blob. Frames share a blob in groups so',
        '// the compressor can still copy across a frame boundary - most of the',
        '// saving - while any one group stays reachable on its own. One flat',
        '// blob is 2.25x smaller than one asset per frame but has to be decoded',
        '// whole, which for a 29 frame sprite is 59 KB and two video frames of',
        '// work to reach 2 KB of it.',
        '',
    ]
    for b in built:
        L.append(f'const u32 gBwAnimGfx_{b["sym"]}[] = INCGFX_U32('
                 f'"graphics/pokemon/{b["stem"]}/bw_anim.png", ".4bpp.fsmol");')
        L.append(f'const u16 gBwAnimPal_{b["sym"]}[] = INCGFX_U16('
                 f'"graphics/pokemon/{b["stem"]}/bw_anim.png", ".gbapal");')
    L.append('')
    for b in built:
        L.append(f'static const struct BwAnimStep sBwSeq_{b["sym"]}[] =')
        L.append('{')
        for f, hold in b['steps']:
            L.append(f'    {{ {f}, {hold} }},')
        L.append('};')
        L.append('')

    L.append('// Sorted by species id - GetBwAnim bisects this.')
    L.append('static const struct BwAnim sBwAnims[] =')
    L.append('{')
    for b in built:
        L.append(f'    {{')
        L.append(f'        .species = SPECIES_{b["stem"].upper()},'
                 f'   // {b["id"]}')
        L.append(f'        .frames = gBwAnimGfx_{b["sym"]},')
        L.append(f'        .palette = gBwAnimPal_{b["sym"]},')
        L.append(f'        .seq = sBwSeq_{b["sym"]},')
        L.append(f'        .frameCount = {b["frames"]},')
        L.append(f'        .seqLength = {len(b["steps"])},')
        L.append(f'        .width = {b["w"]},')
        L.append(f'        .height = {b["h"]},')
        L.append(f'    }},')
    L.append('};')
    L.append('')
    L.append('#endif // GUARD_DATA_ROGUE_BW_ANIM_H')

    p = repo / 'src/data/rogue_bw_anim.h'
    p.write_text('\n'.join(L) + '\n', encoding='utf-8', newline='\n')
    return p


def write_rules(repo, built, chunk):
    """Emit the make rules that turn each frame stack into a container.

    These have to be explicit and per species because the container needs the
    frame size, and a flat 4bpp file does not carry one. scaninc still writes
    the png -> 4bpp rule on its own from the INCGFX reference; only the
    4bpp -> fsmol step is here.

    A species missing from this file fails the build with "no rule to make
    target", which is the failure worth having. The alternative - a generic
    %.fsmol pattern that guessed - would produce a container whose frames were
    offset by a tile row, and that is a garbled sprite rather than an error.
    """
    L = [
        '# GENERATED by tools/rogue/emit_bw_anim.py - do not edit by hand.',
        '#',
        '# BW animation frame containers. One rule per species, because -fw',
        '# takes the frame size in bytes and the frames per chunk, and neither',
        '# is recoverable from the 4bpp file.',
        '#',
        f'# Chunk size is {chunk}. Raising it shrinks ROM and lowers average CPU,',
        '# since a caller holding the decoded chunk decodes once per chunk',
        '# rather than once per frame - but it raises the one-off spike when a',
        '# chunk does have to be decoded, and the scratch buffer with it.',
        '',
    ]
    for b in sorted(built, key=lambda b: b['stem']):
        asset = f'$(ASSETS_DIR_NAME)/graphics/pokemon/{b["stem"]}/bw_anim.png.4bpp'
        L.append(f'{asset}.fsmol: {asset}')
        L.append(f'\t$(SMOL) -fw $< $@ {b["w"] * b["h"] // 2} {chunk}')
        L.append('')

    p = repo / 'graphics/pokemon/bw_anim_rules.mk'
    p.write_text('\n'.join(L), encoding='utf-8', newline='\n')
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gifs', required=True)
    ap.add_argument('--repo', default=str(Path(__file__).resolve().parents[2]))
    ap.add_argument('--species')
    ap.add_argument('--preset')
    #  4 is where the measured curve stops paying: it fits Gen 1-5 in ROM at
    #  5.12 MB against ~6.9 MB free, and holds one decode inside half a video
    #  frame. 8 saves another 0.7 MB but spends 80% of a frame in one go.
    ap.add_argument('--chunk', type=int, default=4)
    args = ap.parse_args()

    repo = Path(args.repo)
    gifs = Path(args.gifs)

    names = []
    if args.preset:
        names = PRESETS[args.preset]
    if args.species:
        names += [s.strip() for s in args.species.split(',') if s.strip()]
    if not names:
        raise SystemExit('give --species or --preset')

    built = []
    for n in names:
        gp = gifs / n / f'{n}.gif'
        if not gp.exists():
            print(f'{n:12s} MISSING {gp}')
            continue
        info, err = emit_species(repo, gp, n)
        if err:
            print(f'{n:12s} SKIPPED: {err}')
            continue
        built.append(info)
        total_holds = sum(h for _f, h in info['steps'])
        print(f'{n:12s} {info["w"]}x{info["h"]}  {info["frames"]:3d} frames  '
              f'{len(info["steps"]):3d} steps  loop {total_holds} video frames')

    if built:
        p = write_header(repo, built, species_ids(repo))
        print(f'\nwrote {p}')
        r = write_rules(repo, built, args.chunk)
        print(f'wrote {r}')
        print(f'wrote {len(built)} x graphics/pokemon/<name>/bw_anim.png')


if __name__ == '__main__':
    main()
