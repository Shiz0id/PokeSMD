"""
Emit checked-in BW animation assets for a list of species.

Per species this writes:
  graphics/pokemon/<name>/bw_anim.png   indexed frame sheet, frames stacked
                                        vertically, index 0 transparent
and appends to a generated header:
  include/constants/rogue_bw_anim.h     frame count, size, and the playback
                                        sequence as (frame, hold) pairs

WHY THE FRAMES ARE ONE SHEET rather than one file each. Frames of a species are
highly redundant, and a sheet is what lets the compressor exploit that. It is
compressed as a mode 7 frame CONTAINER, so the redundancy is captured within
each small group of frames while any one group stays independently decodable -
a flat blob compresses better still but has to be decoded whole, which for a
29 frame sprite is 59 KB to reach 2 KB of it.

HOLD IS IN VIDEO FRAMES, not milliseconds. BW timings are 70-600 ms against a
16.74 ms video frame; converting here means the engine never divides at
runtime. A hold is clamped to at least 1 so a frame cannot be skipped entirely.

PLAYBACK IS FLOORED AT --min-hold VIDEO FRAMES. Rips vary: the BW set runs
7.7-10 fps, but a smooth rip can be 20 ms flat, which is 50 fps and rounds down
to a hold of 1. That is both faster than the gif asks for and the most
expensive thing the runtime can play. See DEFAULT_MIN_HOLD.

Run:  python emit_bw_anim.py --gifs D:/PokemonTest/gifs --species geodude,nosepass
      python emit_bw_anim.py --gifs ... --preset test
      python emit_bw_anim.py --gifs ... --back mewtwo=path/to/mewtwo_b.gif
Needs Pillow. Run `make` afterwards - gbagfx and compresSmol run from the build.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import downscale_bw as D

VIDEO_FRAME_MS = 1000.0 / 59.7275
# Was 96, because compresSmol segfaulted on very large sheets - Beedrill at 177
# frames and Celebi at 129 both crashed it. A frame container removes that
# limit rather than working around it: the compressor never sees the whole
# sheet any more, only one chunk of four frames at a time, so the size that
# crashed it cannot arise. 255 is now the real ceiling, and it comes from
# frameCount and seqLength being u8 in the C table.
MAX_FRAMES = 255

# Marks the one emit failure that is worth retrying rather than reporting: a
# sequence longer than the u8 seqLength field. Raising the hold floor merges
# steps and shortens the sequence, so the animation survives slightly coarser
# instead of being dropped. Matched as a prefix, so it must stay first.
U8_OVERFLOW = 'u8-overflow'

# The ceiling on that retry. A hold of 8 video frames is 7.5 fps, which is the
# bottom of the range the BW rips actually use - past it the result is not the
# animation any more, and dropping the species is the honest outcome.
MAX_MIN_HOLD = 8
MON_PIC_WIDTH = 64       # must match include/constants/pokemon.h
MON_PIC_HEIGHT = 64

# The floor on how long one step may hold, in VIDEO FRAMES - the same unit the
# runtime counts in, which is the point.
#
# A hold of 1 is the pathological case and the only one worth merging. It means
# a frame change every video frame, which is 60 fps: faster than the gif itself
# asks for, since anything under 25 ms rounds down to a single 16.74 ms frame,
# and the most expensive thing the runtime can be asked to play, since it forces
# a chunk decode every framesPerChunk frames.
#
# Expressing this as a frames-per-second cap looked equivalent and was not. A
# 30 fps cap is a 33.3 ms floor, which caught Geodude's 30 ms steps - and those
# already round to a hold of 2 and were never a problem. Measured across the
# eleven emitted gifs, a floor of 2 holds touches Celebi alone and leaves every
# other sprite byte-identical.
DEFAULT_MIN_HOLD = 2

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

# National dex bounds per generation, checked against the enum at run time
# rather than trusted. Gens 1-7 are exactly the base dex with no form entries
# interleaved, which is what makes a plain id range a safe way to name a
# generation.
#
# GEN 8 AND 9 ARE DELIBERATELY ABSENT. The enum stops being contiguous there:
# the alternate forms sit between the end of gen 8 and SPECIES_SPRIGATITO,
# which is 1289 rather than 906. A range over that region would quietly emit
# Megas and Gmaxes as if they were base species, and the roster would look
# right in the count. Name those species explicitly with --species instead.
GEN_RANGES = {
    1: (1, 151), 2: (152, 251), 3: (252, 386),
    4: (387, 493), 5: (494, 649), 6: (650, 721), 7: (722, 809),
}


def gen_species(ids, gen):
    """Every base species of one generation, in enum order.

    Taken from the enum rather than written out, so it cannot drift and so the
    awkward names come out right without being special-cased. The count is
    asserted because a range that silently returns the wrong set is the exact
    failure this table exists to avoid.
    """
    lo, hi = GEN_RANGES[gen]
    got = [s for s, i in sorted(ids.items(), key=lambda t: t[1]) if lo <= i <= hi]
    if len(got) != hi - lo + 1:
        raise SystemExit(
            f'gen {gen}: ids {lo}-{hi} name {len(got)} species, expected '
            f'{hi - lo + 1}. The enum has moved; fix GEN_RANGES before emitting.')
    return [base_name(s).replace('_', '') for s in got]


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


def base_name(species):
    """SPECIES_ constant -> the name both the gif set and the repo use.

    A species that HAS alternate forms names its own base form with a _NORMAL
    suffix in the enum - SPECIES_CASTFORM_NORMAL, SPECIES_DEOXYS_NORMAL - and
    the bare SPECIES_CASTFORM is an alias with no value of its own, so it never
    reaches ids. Neither the gif set nor the repo carries that suffix: both
    call the directory "castform".

    Getting this wrong is quiet twice over, which is why it is one function
    rather than two rules. The gif lookup misses and the species is skipped;
    and if it did not, the repo path would be graphics/pokemon/castform_normal,
    a directory BESIDE the real one, where stock_placement finds no
    anim_front.png and silently centres the sprite instead of matching the one
    the game already draws.
    """
    name = species[len('SPECIES_'):].lower()
    return name[:-len('_normal')] if name.endswith('_normal') else name


def stem_to_species(ids):
    """gif directory name -> SPECIES_ constant.

    The gif set names a directory by stripping every underscore out of the
    species name: SPECIES_NIDORAN_F is "nidoranf" and SPECIES_MR_MIME is
    "mrmime". So the mapping cannot be recovered by rule from the stem - the
    underscore is simply gone - and has to be built by walking the enum
    forwards. Deriving it the other way, as 'SPECIES_' + stem.upper(), yields
    SPECIES_NIDORANF, which is in no enum and used to abort the whole run.
    """
    out = {}
    for species in ids:
        stem = base_name(species).replace('_', '')
        # First one wins, matching the enum order, so a later alias cannot
        # steal a base species' directory.
        out.setdefault(stem, species)
    return out


def resample(seq, durs, min_hold):
    """Merge runs of steps that would hold for fewer than min_hold video frames.

    Preserves total duration.

    WHY THIS IS NOT DEDUP, which already ran. Dedup shrinks the frame POOL by
    removing duplicate images; it cannot touch the SEQUENCE, because a sequence
    revisiting an earlier pose is the normal shape of an animation. Playback
    rate comes from the gif's own per-frame durations and survives dedup
    untouched.

    Rips vary wildly. The BW set runs 7.7-10 fps - Claydol at 120-130 ms,
    Mewtwo at 100 - but a smooth rip can be 20 ms flat, which is 50 fps, and the
    GBA cannot hold a frame for less than one video frame at 16.74 ms. So the
    fastest sources both play FASTER than intended and cost the most decodes: a
    frame change every video frame is a chunk decode every framesPerChunk.

    Merging rather than resampling onto a fixed grid is deliberate. A long
    deliberate pause - Claydol has a 470 ms one - already exceeds the threshold
    and is emitted untouched, so a gif that is mostly slow keeps its timing
    exactly. Only runs of too-short steps collapse.

    The test is applied to the ROUNDED hold, not to the raw milliseconds,
    because the rounded hold is what the runtime will actually count.
    """
    out_seq, out_durs = [], []
    acc = 0.0
    pick, pick_dur = None, -1.0

    for s, d in zip(seq, durs):
        # Within a merged window the longest-held pose is the one that showed,
        # so it is the one worth keeping.
        if d > pick_dur:
            pick, pick_dur = s, d
        acc += d
        if max(1, round(acc / VIDEO_FRAME_MS)) >= min_hold:
            out_seq.append(pick)
            out_durs.append(acc)
            acc, pick, pick_dur = 0.0, None, -1.0

    # Whatever is left over is folded into the last step rather than emitted as
    # a short one, so the loop keeps its total length.
    if acc > 0:
        if out_durs:
            out_durs[-1] += acc
        else:
            out_seq.append(pick if pick is not None else seq[-1])
            out_durs.append(acc)

    return out_seq, out_durs


def prune_frames(frames, seq):
    """Drop frames the sequence no longer names, and renumber what is left.

    Resampling is what makes this necessary and is also what pays for it: the
    frames dropped from the sequence are still in the pool, still compressed
    into the container and still costing ROM, until they are pruned.
    """
    used = sorted(set(seq))
    remap = {old: new for new, old in enumerate(used)}
    return [frames[i] for i in used], [remap[s] for s in seq]


def stock_bbox(repo, stem, back=False):
    """Bounding box of the shipped 64x64 sprite's first frame.

    anim_front.png is 64x128 - two frames stacked - so only the top 64 rows are
    read. back.png is a single 64x64 frame and is read whole; taking the top
    half of it would clip the sprite and pull the alignment upward.

    Read as INDEXED with palette entry 0 as the transparent one, which is the
    GBA's rule. Converting to RGBA and taking the alpha channel looks equivalent
    and is not: these PNGs need carry no tRNS chunk, so every pixel comes back
    opaque and the box is the whole 64x64 - which silently centres every sprite
    and is only visible as a mon standing slightly wrong in game.
    """
    p = repo / 'graphics/pokemon' / stem / ('back.png' if back else 'anim_front.png')
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


def stock_placement(repo, stem, w, h, back=False):
    """Where in the 64x64 box to put a w x h frame.

    Matched to the sprite the game already draws, on CENTRE X and BOTTOM Y.
    Bottom rather than centre because a battler is positioned by its feet - the
    engine's y offsets assume the mon stands on the bottom of its box - so
    centring vertically would sink or float it relative to the static sprite it
    replaces, and the swap happens in front of the player at switch-in.
    """
    box = stock_bbox(repo, stem, back)
    if box is None:
        # Nothing to match: centre horizontally, stand on the bottom.
        return (MON_PIC_WIDTH - w) // 2, MON_PIC_HEIGHT - h
    x0, _y0, x1, y1 = box
    ox = round((x0 + x1) / 2 - w / 2)
    oy = y1 - h
    return (max(0, min(MON_PIC_WIDTH - w, ox)),
            max(0, min(MON_PIC_HEIGHT - h, oy)))


def emit_species(repo, gif_path, stem, dirname, back=False, min_hold=DEFAULT_MIN_HOLD):
    """stem is the GIF directory, dirname the REPO one, and they differ.

    The gif set strips underscores - "mrmime", "nidoranf" - while the repo
    keeps them: graphics/pokemon/mr_mime, nidoran_f, nidoran_m. Writing to the
    gif's spelling creates a directory beside the real one, which is wrong
    twice: the asset does not sit with the species it belongs to, and
    stock_bbox finds no anim_front.png there, so alignment silently falls back
    to centring instead of matching the sprite the game already draws.
    """
    built = D.build_species(str(gif_path))
    if not built:
        return None, 'no frames'
    frames, seq, durs, cols = built
    if len(cols) > 15:
        return None, f'{len(cols)} colours (4bpp allows 15)'

    before = len(frames)
    if min_hold > 1:
        seq, durs = resample(seq, durs, min_hold)
        frames, seq = prune_frames(frames, seq)
    dropped = before - len(frames)

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
    ox, oy = stock_placement(repo, dirname, w, h, back)

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

    outdir = repo / 'graphics/pokemon' / dirname
    outdir.mkdir(parents=True, exist_ok=True)
    asset = 'bw_anim_back.png' if back else 'bw_anim.png'
    idx.save(outdir / asset)

    steps = []
    for i, s in enumerate(seq):
        hold = max(1, round((durs[i] if i < len(durs) else 100) / VIDEO_FRAME_MS))
        if steps and steps[-1][0] == s:
            steps[-1][1] = min(255, steps[-1][1] + hold)   # merge repeats
        else:
            steps.append([s, min(255, hold)])

    # seqLength and frameCount are u8 in the C table, so a sequence longer than
    # 255 steps would wrap and play a fragment of itself forever.
    if len(steps) > 255 or len(frames) > 255:
        return None, (f'{U8_OVERFLOW}: {len(frames)} frames / {len(steps)} '
                      f'steps exceeds the u8 table fields')

    # Back symbols carry a suffix: a species may have both, and without it the
    # two would declare the same gBwAnimGfx_<Name> twice.
    return dict(stem=stem, dir=dirname,
                sym=c_name(dirname) + ('Back' if back else ''),
                w=tw, h=th, dropped=dropped,
                frames=len(frames), steps=steps, back=back, asset=asset), None


def write_header(repo, built, ids):
    """Emit the whole data file: graphics, sequences and the lookup table.

    Sorted by species id so the lookup can bisect. Nine entries would scan fine
    linearly, but this is meant to reach several hundred and a linear scan per
    battler per frame is exactly the kind of cost that hides until it matters.
    """
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
                 f'"graphics/pokemon/{b["dir"]}/{b["asset"]}", ".4bpp.fsmol");')
        L.append(f'const u16 gBwAnimPal_{b["sym"]}[] = INCGFX_U16('
                 f'"graphics/pokemon/{b["dir"]}/{b["asset"]}", ".gbapal");')
    L.append('')
    for b in built:
        L.append(f'static const struct BwAnimStep sBwSeq_{b["sym"]}[] =')
        L.append('{')
        for f, hold in b['steps']:
            L.append(f'    {{ {f}, {hold} }},')
        L.append('};')
        L.append('')

    # Front and back are SEPARATE tables, each sorted by species id, rather than
    # one table with a flag. A species can have both, and a single table sorted
    # by species would then hold duplicate keys, which is exactly what a binary
    # search cannot resolve.
    for back, name, label in ((False, 'sBwAnims', 'Front'), (True, 'sBwAnimsBack', 'Back')):
        rows = [b for b in built if b['back'] == back]
        L.append(f'// {label} sprites, sorted by species id - GetBwAnim bisects this.')
        L.append(f'static const struct BwAnim {name}[] =')
        L.append('{')
        for b in rows:
            L.append(f'    {{')
            L.append(f'        .species = {b["species"]},   // {b["id"]}')
            L.append(f'        .frames = gBwAnimGfx_{b["sym"]},')
            L.append(f'        .palette = gBwAnimPal_{b["sym"]},')
            L.append(f'        .seq = sBwSeq_{b["sym"]},')
            L.append(f'        .frameCount = {b["frames"]},')
            L.append(f'        .seqLength = {len(b["steps"])},')
            L.append(f'        .width = {b["w"]},')
            L.append(f'        .height = {b["h"]},')
            L.append(f'    }},')
        if not rows:
            # An empty array is not valid C, and ARRAY_COUNT of it would be
            # meaningless anyway.
            L.append('    { .species = SPECIES_NONE },')
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
    for b in sorted(built, key=lambda b: (b['dir'], b['back'])):
        asset = f'$(ASSETS_DIR_NAME)/graphics/pokemon/{b["dir"]}/{b["asset"]}.4bpp'
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
    #  2, because four animating battlers need four chunk buffers and 4 x 8 KB
    #  does not fit the heap - see the container section of
    #  roguelike-architecture.md. This knob is set by RAM, not by ROM.
    ap.add_argument('--chunk', type=int, default=2)
    #  Back sprites are named individually rather than found in --gifs, because
    #  the 1,253 gif set is front sprites only - one per species, no backs. Any
    #  back animation therefore comes from somewhere else and has no naming
    #  convention to rely on.  --back mewtwo=path/to/mewtwo_b.gif
    ap.add_argument('--back', action='append', default=[],
                    metavar='SPECIES=PATH',
                    help='a back-sprite gif for one species, repeatable')
    ap.add_argument('--min-hold', type=int, default=DEFAULT_MIN_HOLD,
                    help='floor on video frames per step; 1 keeps gif timing')
    ap.add_argument('--backs', metavar='DIR',
                    help='directory of back-sprite gifs, used for every species '
                         'named by --species or --preset that has one')
    args = ap.parse_args()

    repo = Path(args.repo)
    gifs = Path(args.gifs)

    ids = species_ids(repo)
    #  --preset takes a LIST, because emitting replaces the roster rather than
    #  extending it - so growing the roster by a generation means naming every
    #  generation already in it. `--preset gen1,gen2,test` is the whole roster,
    #  not the delta. Bare `gen1` still means what it always did.
    names = []
    for token in (args.preset or '').split(','):
        token = token.strip()
        if not token:
            continue
        if token.startswith('gen') and token[3:].isdigit():
            gen = int(token[3:])
            if gen not in GEN_RANGES:
                raise SystemExit(
                    f'no range for {token}: gens 8 and 9 are not contiguous in '
                    f'the enum, name those species with --species')
            names += gen_species(ids, gen)
        elif token in PRESETS:
            names += PRESETS[token]
        else:
            raise SystemExit(f'unknown preset {token!r}')
    if args.species:
        names += [s.strip() for s in args.species.split(',') if s.strip()]

    #  A species named twice - gen2 and test both carry Xatu - would emit its
    #  sheet twice and put a DUPLICATE KEY in a table that is bisected at
    #  runtime, which a binary search cannot resolve. write_header sorts by
    #  species id, so order here does not matter; uniqueness does.
    seen = set()
    names = [n for n in names if not (n in seen or seen.add(n))]

    jobs = [(n, gifs / n / f'{n}.gif', False) for n in names]

    if args.backs:
        # The back set nests one level deeper than the front one. Both layouts
        # are accepted rather than assumed, since a rip can arrive either way.
        root = Path(args.backs)
        for n in names:
            for cand in (root / n / n / f'{n}.gif', root / n / f'{n}.gif'):
                if cand.exists():
                    jobs.append((n, cand, True))
                    break
    for spec in args.back:
        if '=' not in spec:
            raise SystemExit(f'--back wants SPECIES=PATH, got {spec}')
        n, path = spec.split('=', 1)
        jobs.append((n.strip(), Path(path.strip()), True))

    if not jobs:
        raise SystemExit('give --species, --preset or --back')

    byStem = stem_to_species(ids)
    built = []
    skipped = []
    for n, gp, back in jobs:
        side = 'back' if back else 'front'
        species = byStem.get(n)
        if species is None:
            # Skipped rather than fatal: at 151 species one unrecognised
            # directory should not throw away the other 300 containers.
            print(f'{n:12s} {side:5s} SKIPPED: no species matches that directory')
            skipped.append((n, side, 'no species matches that directory'))
            continue
        dirname = base_name(species)
        if not (repo / 'graphics/pokemon' / dirname).is_dir():
            #  Fatal, not skipped. Writing to a directory that does not exist
            #  CREATES it, beside the real one, and the asset then ships
            #  detached from its species with the sprite silently centred.
            raise SystemExit(f'{n}: graphics/pokemon/{dirname} does not exist')
        if not gp.exists():
            print(f'{n:12s} {side:5s} MISSING {gp}')
            skipped.append((n, side, f'no gif at {gp}'))
            continue
        info, err = emit_species(repo, gp, n, dirname, back, args.min_hold)
        #  A sequence too long for the u8 fields is not a reason to drop a
        #  species - it means the rip is finer than the runtime can address,
        #  and holding each step longer merges steps until it fits. Masquerain's
        #  back is what this is for: 256 steps, one over the limit, and dropping
        #  it would have cost the whole animation to save one step.
        hold = args.min_hold
        while err and err.startswith(U8_OVERFLOW) and hold < MAX_MIN_HOLD:
            hold += 1
            info, err = emit_species(repo, gp, n, dirname, back, hold)
        if err:
            print(f'{n:12s} {side:5s} SKIPPED: {err}')
            skipped.append((n, side, err))
            continue
        info['species'], info['id'] = species, ids[species]
        built.append(info)
        total_holds = sum(h for _f, h in info['steps'])
        fps = 60.0 * len(info['steps']) / total_holds if total_holds else 0
        note = f'  resampled, {info["dropped"]} frames dropped' if info['dropped'] else ''
        if hold != args.min_hold:
            note += f'  min-hold raised to {hold} to fit the u8 sequence'
        print(f'{n:12s} {side:5s} {info["w"]}x{info["h"]}  {info["frames"]:3d} frames  '
              f'{len(info["steps"]):3d} steps  {fps:4.1f} fps{note}')

    #  A roster is emitted a hundred species at a time and the per-species
    #  lines scroll past, so a skip has to be restated at the end or it is
    #  simply not seen. This is how Masquerain's back went missing from a
    #  135 species batch without anyone noticing until the counts were compared.
    if skipped:
        print(f'\n{len(skipped)} of {len(jobs)} assets were NOT emitted:')
        for n, side, why in skipped:
            print(f'  {n:12s} {side:5s} {why}')

    if built:
        p = write_header(repo, built, ids)
        print(f'\nwrote {p}')
        r = write_rules(repo, built, args.chunk)
        print(f'wrote {r}')
        print(f'wrote {len(built)} assets under graphics/pokemon/')


if __name__ == '__main__':
    main()
