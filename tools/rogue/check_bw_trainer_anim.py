#!/usr/bin/env python3
"""
Guard the animated trainer front pics: src/data/rogue_bw_trainer_anim.h, the
sheets it names, the container rules that build them, and the two lifetime
rules the runtime depends on.

WHAT THIS CAN AND CANNOT SEE, stated up front because this project's record is
that a clean build and a passing suite said nothing about eight of nine
minigames crashing on exit. It CAN see the generated data, the agreement
between a table row and the asset it names, and - structurally - whether the
functions that own a lifetime are paired with the ones that end it. It CANNOT
see whether the hook fires, whether the trainer is drawn in the right place, or
whether the palette lands in the right slot. A leader who animates and one who
freezes on frame 0 look identical from here.

Two of the eight checks are of the LIFETIME kind rather than the data kind, and
they are the reason this file exists at all rather than being folded into a
render. The technique, borrowed from the two checks that already do it, is to
name the ownership rule in the file and then assert the pairing that makes it
true:

  * a trainer sprite is destroyed at three sites, and each must tell the
    animation, because a sprite id is a slot number and the slot is handed
    straight to the mon the trainer just threw;
  * a battler position has ONE pixel buffer and therefore one animation, so the
    mon path and the trainer path must each evict the other - and half of a
    mirrored pair reads as an omission rather than as a bug.

Run:  python3 check_bw_trainer_anim.py [repo]
      python3 check_bw_trainer_anim.py --selftest
"""
import re
import struct
import sys
from pathlib import Path

FRAME_W = 64
FRAME_H = 64
FRAME_BYTES = FRAME_W * FRAME_H // 2
PAL_SIZE = 16
# emit_bw_trainer_anim.py puts magenta in the transparent slot deliberately, so
# a frame drawn with index 0 shows up loudly instead of blending into the
# backdrop. Quantised to 5 bits per channel the way gbagfx converts it.
TRANSPARENT_KEY = (255 // 8, 0, 255 // 8)

DATA = 'src/data/rogue_bw_trainer_anim.h'
RULES = 'graphics/trainers/bw_trainer_anim_rules.mk'
RUNTIME = 'src/rogue_bw_trainer_anim.c'
SPECIES_RUNTIME = 'src/rogue_bw_anim.c'
CONTROLLERS = 'src/battle_controllers.c'
ANIMS = 'src/data.c'
PICS = 'include/constants/trainers.h'


# --------------------------------------------------------------- file reading
def png_size(data):
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    w, h = struct.unpack('>II', data[16:24])
    return w, h


def png_palette(data):
    off = 8
    while off < len(data):
        length, kind = struct.unpack('>I4s', data[off:off + 8])
        if kind == b'PLTE':
            raw = data[off + 8:off + 8 + length]
            return [(raw[i] // 8, raw[i + 1] // 8, raw[i + 2] // 8)
                    for i in range(0, len(raw), 3)]
        off += 12 + length
    raise ValueError('no PLTE chunk')


def parse_table(text):
    """The rows of sBwTrainerAnims, in file order.

    File order matters and is the point: GetBwTrainerAnim bisects this array,
    so the order it is WRITTEN in is the order the runtime assumes. Sorting the
    rows here before checking them would check a table the game never sees.
    """
    m = re.search(r'static const struct BwTrainerAnim sBwTrainerAnims\[\]\s*=\s*\{(.*?)\n\};',
                  text, re.S)
    if not m:
        raise ValueError('no sBwTrainerAnims[] in ' + DATA)
    rows = []
    for body in re.findall(r'\{(.*?)\}', m.group(1), re.S):
        f = dict(re.findall(r'\.(\w+)\s*=\s*([A-Za-z0-9_]+)', body))
        if 'trainerPic' not in f:
            continue
        idm = re.search(r'\.trainerPic\s*=\s*\w+,\s*//\s*(\d+)', body)
        rows.append(dict(pic=f['trainerPic'],
                         id=int(idm.group(1)) if idm else None,
                         gfx=f.get('frames', ''), pal=f.get('palette', ''),
                         seq=f.get('seq', ''),
                         frameCount=int(f.get('frameCount', -1)),
                         seqLength=int(f.get('seqLength', -1)),
                         width=int(f.get('width', -1)),
                         height=int(f.get('height', -1))))
    return rows


def parse_seqs(text):
    out = {}
    for m in re.finditer(r'static const struct BwAnimStep (sBwTrainerSeq_\w+)\[\]\s*=\s*\{(.*?)\n\};',
                         text, re.S):
        out[m.group(1)] = [(int(a), int(b)) for a, b in
                           re.findall(r'\{\s*(\d+)\s*,\s*(\d+)\s*\}', m.group(2))]
    return out


def parse_assets(text):
    """symbol -> asset path, from the INCGFX declarations."""
    out = {}
    for m in re.finditer(r'const u(?:32|16) (gBwTrainer(?:Gfx|Pal)_\w+)\[\]\s*=\s*INCGFX_U\d+\("([^"]+)"',
                         text):
        out[m.group(1)] = m.group(2)
    return out


def parse_pic_ids(text):
    m = re.search(r'enum\s+__attribute__\(\(packed\)\)\s+TrainerPicID\s*\{(.*?)\}', text, re.S)
    if not m:
        raise ValueError('no enum TrainerPicID')
    out, n = {}, 0
    for e in re.finditer(r'(TRAINER_PIC_[A-Z0-9_]+)\s*(?:=\s*(\d+))?\s*,', m.group(1)):
        if e.group(2) is not None:
            n = int(e.group(2))
        out.setdefault(e.group(1), n)
        n += 1
    return out


def func_body(text, name):
    """The text of one C function, from its opening brace to the matching one."""
    m = re.search(r'\n[\w \*]*?\b' + re.escape(name) + r'\s*\([^)]*\)\s*\n?\{', text)
    if not m:
        return None
    i = text.index('{', m.start())
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    return None


def load(repo):
    repo = Path(repo)
    data = (repo / DATA).read_text(encoding='utf-8', errors='replace')
    ctx = dict(repo=repo,
               rows=parse_table(data), seqs=parse_seqs(data),
               assets=parse_assets(data),
               rules=(repo / RULES).read_text(encoding='utf-8', errors='replace'),
               runtime=(repo / RUNTIME).read_text(encoding='utf-8', errors='replace'),
               species=(repo / SPECIES_RUNTIME).read_text(encoding='utf-8', errors='replace'),
               controllers=(repo / CONTROLLERS).read_text(encoding='utf-8', errors='replace'),
               anims=(repo / ANIMS).read_text(encoding='utf-8', errors='replace'),
               pic_ids=parse_pic_ids((repo / PICS).read_text(encoding='utf-8', errors='replace')),
               png={})
    for sym, rel in ctx['assets'].items():
        p = repo / rel
        ctx['png'][rel] = p.read_bytes() if p.exists() else None
    return ctx


# ------------------------------------------------------------------- checks
def check_table_is_bisectable(ctx):
    """GetBwTrainerAnim bisects, so the table must be strictly increasing.

    A duplicate key is worse than an unsorted one: a binary search cannot
    resolve it at all, and which of the two rows it lands on is an artefact of
    the array length.
    """
    bad, seen, last = [], set(), -1
    for r in ctx['rows']:
        if r['pic'] not in ctx['pic_ids']:
            bad.append(f'{r["pic"]} is in no TrainerPicID enum')
            continue
        n = ctx['pic_ids'][r['pic']]
        if r['id'] is not None and r['id'] != n:
            bad.append(f'{r["pic"]} is commented as id {r["id"]} but the enum says {n}')
        if r['pic'] in seen:
            bad.append(f'{r["pic"]} appears twice')
        seen.add(r['pic'])
        if n <= last:
            bad.append(f'{r["pic"]} (id {n}) is out of order after id {last}')
        last = n
    return bad


def check_row_matches_sheet(ctx):
    """frameCount must equal the sheet's height in 64px frames.

    THE FAILURE THIS CATCHES IS SILENT AND OUT OF BOUNDS. The container is
    indexed by frame number at runtime with no length to check against, so a
    row claiming more frames than the sheet holds reads past the blob, and one
    claiming fewer simply loses the tail. Neither is a build error.
    """
    bad = []
    for r in ctx['rows']:
        rel = ctx['assets'].get(r['gfx'])
        if rel is None:
            bad.append(f'{r["pic"]}: no INCGFX declaration for {r["gfx"]}')
            continue
        blob = ctx['png'].get(rel)
        if blob is None:
            bad.append(f'{r["pic"]}: {rel} does not exist')
            continue
        w, h = png_size(blob)
        if w != FRAME_W:
            bad.append(f'{r["pic"]}: sheet is {w}px wide, must be {FRAME_W} '
                       f'(the engine tiles a battle sprite 8 tiles across)')
        if h != FRAME_H * r['frameCount']:
            bad.append(f'{r["pic"]}: frameCount {r["frameCount"]} wants a '
                       f'{FRAME_H * r["frameCount"]}px sheet, got {h}px')
        if (r['width'], r['height']) != (FRAME_W, FRAME_H):
            bad.append(f'{r["pic"]}: width/height are {r["width"]}x{r["height"]}, '
                       f'must be {FRAME_W}x{FRAME_H}')
        pal = png_palette(ctx['png'][ctx['assets'][r['pal']]]) if r['pal'] in ctx['assets'] else []
        if len(pal) > PAL_SIZE:
            bad.append(f'{r["pic"]}: palette has {len(pal)} entries, 4bpp allows {PAL_SIZE}')
        elif pal and pal[0] != TRANSPARENT_KEY:
            bad.append(f'{r["pic"]}: palette index 0 is {pal[0]}, expected the '
                       f'magenta transparent key {TRANSPARENT_KEY}')
    return bad


def check_sequences(ctx):
    """Every step names a frame that exists and holds for at least one frame.

    A hold of 0 is the one that does not merely look wrong: the runtime stores
    hold - 1 into a u8, so zero underflows to 255 and the trainer freezes for
    four seconds on that pose.
    """
    bad = []
    for r in ctx['rows']:
        seq = ctx['seqs'].get(r['seq'])
        if seq is None:
            bad.append(f'{r["pic"]}: no sequence {r["seq"]}')
            continue
        if len(seq) != r['seqLength']:
            bad.append(f'{r["pic"]}: seqLength {r["seqLength"]} but {len(seq)} steps')
        if not seq:
            bad.append(f'{r["pic"]}: empty sequence')
        for i, (frame, hold) in enumerate(seq):
            if frame >= r['frameCount']:
                bad.append(f'{r["pic"]} step {i}: frame {frame} of {r["frameCount"]}')
            if hold < 1:
                bad.append(f'{r["pic"]} step {i}: hold {hold} underflows to 255')
            if hold > 255 or r['frameCount'] > 255 or r['seqLength'] > 255:
                bad.append(f'{r["pic"]}: a u8 table field is over 255')
    return bad


def check_container_rules(ctx):
    """Every sheet needs a rule, and the frame size in it must be 2048.

    -fw takes the frame size in bytes and it is NOT recoverable from the 4bpp
    file, so a wrong number here does not fail: it builds a container whose
    frames are offset, which is a garbled sprite. A missing rule does fail, with
    "no rule to make target", which is the good outcome and is why this only
    has to guard the number.
    """
    bad = []
    for r in ctx['rows']:
        rel = ctx['assets'].get(r['gfx'])
        if rel is None:
            continue
        m = re.search(re.escape(rel) + r'\.4bpp\.fsmol:.*?\n\t\$\(SMOL\) -fw \$< \$@ (\d+) (\d+)',
                      ctx['rules'], re.S)
        if not m:
            bad.append(f'{r["pic"]}: no container rule for {rel} in {RULES}')
            continue
        if int(m.group(1)) != FRAME_BYTES:
            bad.append(f'{r["pic"]}: container built at frame size {m.group(1)}, '
                       f'must be {FRAME_BYTES}')
    return bad


def check_publish_writes_one_slot(ctx):
    """PublishFrame writes image slot 0 only, so gAnims_Trainer must name only 0.

    The species path fills BOTH resident slots because a mon's frontAnimFrames
    cycles image frames 0 and 1 and either may win. A trainer cannot: both
    entries of gAnims_Trainer are sAnim_GeneralFrame0 and that is ANIMCMD_FRAME(0, 0).
    Writing the second slot would be a 2 KB copy per frame change that nothing
    reads - but if gAnims_Trainer ever gains a real second frame, the single
    write becomes a STALE SPRITE rather than a waste, and no build would say so.
    """
    bad = []
    m = re.search(r'const union AnimCmd \*const gAnims_Trainer\[\]\s*=\s*\{(.*?)\}',
                  ctx['anims'], re.S)
    if not m:
        return ['gAnims_Trainer not found in ' + ANIMS]
    entries = [e.strip() for e in m.group(1).split(',') if e.strip()]
    for e in entries:
        if e != 'sAnim_GeneralFrame0':
            bad.append(f'gAnims_Trainer names {e}; PublishFrame only writes image '
                       f'slot 0, so a trainer anim naming another frame draws stale pixels')
    body = func_body(ctx['anims'], 'sAnim_GeneralFrame0')
    decl = re.search(r'const union AnimCmd sAnim_GeneralFrame0\[\]\s*=\s*\{(.*?)\};',
                     ctx['anims'], re.S)
    if decl and 'ANIMCMD_FRAME(0,' not in decl.group(1).replace(' ', '').replace(
            'ANIMCMD_FRAME(0,', 'ANIMCMD_FRAME(0,'):
        bad.append('sAnim_GeneralFrame0 does not start at image frame 0')
    return bad


def check_destroy_sites_are_hooked(ctx):
    """Every site that destroys a trainer sprite must tell the animation.

    A SPRITE ID IS A SLOT NUMBER, NOT AN IDENTITY. trainerSlideSpriteIds keeps
    naming the slot after DestroySprite, and the slot is reused immediately - by
    the mon the trainer just threw, or by whatever the next screen creates. This
    is the fault that drew a Geodude over Roxanne on the species side, in the
    other direction.

    Checked by finding the destroy sites rather than by listing them, so a
    fourth one added later fails here instead of shipping unhooked.
    """
    bad = []
    text = ctx['controllers']
    owners = set()
    for m in re.finditer(r'DestroySprite\(&gSprites\[gBattleStruct->trainerSlideSpriteIds\[\w+\]\]\)',
                         text):
        owners.add(('destroy', m.start()))
    for m in re.finditer(r'\n\s*FreeSpriteOamMatrix\(sprite\);', text):
        owners.add(('cb', m.start()))
    if not owners:
        return ['found no trainer-sprite destroy site in ' + CONTROLLERS +
                ' - the search has gone stale, not the code']

    for kind, pos in sorted(owners):
        # The enclosing function: walk back to the previous line that starts at
        # column 0 and looks like a definition.
        head = text.rfind('\n}', 0, pos)
        start = text.rfind('\nstatic void ', 0, pos)
        start = max(start, text.rfind('\nvoid ', 0, pos))
        if start < head:
            start = head
        body = text[start:text.find('\n}', pos) + 2]
        name = re.search(r'\n(?:static )?\w+ (\w+)\(', body)
        name = name.group(1) if name else f'<{kind}@{pos}>'
        if 'RogueBwTrainerAnim_OnSpriteFreed' not in body:
            bad.append(f'{name} destroys a trainer sprite without calling '
                       f'RogueBwTrainerAnim_OnSpriteFreed')
    return bad


def check_eviction_is_mirrored(ctx):
    """A position has one pixel buffer, so each path must evict the other.

    Both features publish into gMonSpritesGfxPtr->spritesGfx[position]. Half of
    this pair reads as an omission rather than as a bug - the build is clean and
    the common case works, because the two usually alternate in the order that
    happens to be safe - and the failure is one sprite's frames landing in the
    other's tiles.
    """
    bad = []
    load_pic = func_body(ctx['runtime'], 'RogueBwTrainerAnim_OnLoadPic')
    load_mon = func_body(ctx['species'], 'RogueBwAnim_OnLoadSprite')
    if load_pic is None:
        bad.append('RogueBwTrainerAnim_OnLoadPic not found')
    elif 'RogueBwAnim_StopForBattler' not in load_pic:
        bad.append('RogueBwTrainerAnim_OnLoadPic does not evict the mon animation '
                   '(RogueBwAnim_StopForBattler)')
    if load_mon is None:
        bad.append('RogueBwAnim_OnLoadSprite not found')
    elif 'RogueBwTrainerAnim_Stop' not in load_mon:
        bad.append('RogueBwAnim_OnLoadSprite does not evict the trainer animation '
                   '(RogueBwTrainerAnim_Stop)')
    return bad


def check_hooked_and_freed(ctx):
    """The load hook exists, the tick is driven, and the buffers are released.

    The chunk buffers are allocated outside gMonSpritesGfxPtr, so nothing frees
    them by accident: a missing RogueBwTrainerAnim_Free leaks one buffer per
    battle and no check downstream would see it.
    """
    bad = []
    repo = ctx['repo']
    gfx = (repo / 'src/battle_gfx_sfx_util.c').read_text(encoding='utf-8', errors='replace')
    decomp = func_body(gfx, 'DecompressTrainerFrontPic')
    if decomp is None:
        bad.append('DecompressTrainerFrontPic not found')
    elif 'RogueBwTrainerAnim_OnLoadPic' not in decomp:
        bad.append('DecompressTrainerFrontPic does not call RogueBwTrainerAnim_OnLoadPic')
    free = func_body(gfx, 'FreeMonSpritesGfx')
    if free is None or 'RogueBwTrainerAnim_Free' not in free:
        bad.append('FreeMonSpritesGfx does not call RogueBwTrainerAnim_Free - the '
                   'chunk buffers leak one battle at a time')
    tick = func_body(ctx['species'], 'RogueBwAnim_Tick')
    if tick is None or 'RogueBwTrainerAnim_Tick' not in tick:
        bad.append('RogueBwAnim_Tick does not drive RogueBwTrainerAnim_Tick - the '
                   'trainer would load its first frame and never advance')
    return bad


CHECKS = [
    ('table is bisectable', check_table_is_bisectable),
    ('row matches its sheet', check_row_matches_sheet),
    ('sequences are in range', check_sequences),
    ('container rules', check_container_rules),
    ('publish writes one slot', check_publish_writes_one_slot),
    ('destroy sites are hooked', check_destroy_sites_are_hooked),
    ('eviction is mirrored', check_eviction_is_mirrored),
    ('hooked, ticked and freed', check_hooked_and_freed),
]


def run(ctx, quiet=False):
    failed = 0
    for name, fn in CHECKS:
        try:
            bad = fn(ctx)
        except Exception as e:                       # a parse failure is a failure
            bad = [f'{type(e).__name__}: {e}']
        if bad:
            failed += 1
            if not quiet:
                print(f'FAIL  {name}')
                for b in bad:
                    print(f'        {b}')
        elif not quiet:
            print(f'ok    {name}')
    return failed


# ----------------------------------------------------------------- selftest
def selftest(repo):
    """Break each check on purpose and require it to fire.

    A CHECK THAT HAS NEVER FAILED IS WORTH NOTHING, and the two structural ones
    are the likeliest to pass vacuously: they search source text, and a search
    that has gone stale finds nothing and reports nothing wrong.
    """
    import copy

    def drop_call(text, func, call):
        body = func_body(text, func)
        return text.replace(body, body.replace(call, 'DoNothing'), 1)

    cases = []

    def case(name, check, mutate):
        cases.append((name, check, mutate))

    def swap_rows(c):
        c['rows'][0], c['rows'][-1] = c['rows'][-1], c['rows'][0]
    case('table order reversed', check_table_is_bisectable, swap_rows)

    def dup_row(c):
        c['rows'].insert(1, dict(c['rows'][0]))
    case('duplicate key', check_table_is_bisectable, dup_row)

    def bump_frames(c):
        c['rows'][0]['frameCount'] += 1
    case('frameCount one too high', check_row_matches_sheet, bump_frames)

    def zero_hold(c):
        k = c['rows'][0]['seq']
        c['seqs'][k] = [(f, 0) for f, _ in c['seqs'][k]]
    case('a hold of 0', check_sequences, zero_hold)

    def oob_frame(c):
        k = c['rows'][0]['seq']
        c['seqs'][k] = [(c['rows'][0]['frameCount'], 4)] + c['seqs'][k][1:]
    case('step names a frame past the end', check_sequences, oob_frame)

    def wrong_fw(c):
        c['rules'] = c['rules'].replace('$@ 2048 2', '$@ 4096 2')
    case('container frame size wrong', check_container_rules, wrong_fw)

    def second_frame(c):
        c['anims'] = c['anims'].replace(
            'const union AnimCmd *const gAnims_Trainer[] ={\n    sAnim_GeneralFrame0,\n    sAnim_GeneralFrame0,',
            'const union AnimCmd *const gAnims_Trainer[] ={\n    sAnim_GeneralFrame0,\n    sAnim_GeneralFrame1,')
    case('gAnims_Trainer gains a second frame', check_publish_writes_one_slot, second_frame)

    def unhook_destroy(c):
        c['controllers'] = drop_call(c['controllers'], 'SpriteCB_FreeOpponentSprite',
                                     'RogueBwTrainerAnim_OnSpriteFreed')
    case('a destroy site loses its hook', check_destroy_sites_are_hooked, unhook_destroy)

    def unmirror_a(c):
        c['runtime'] = drop_call(c['runtime'], 'RogueBwTrainerAnim_OnLoadPic',
                                 'RogueBwAnim_StopForBattler')
    case('trainer does not evict the mon', check_eviction_is_mirrored, unmirror_a)

    def unmirror_b(c):
        c['species'] = drop_call(c['species'], 'RogueBwAnim_OnLoadSprite',
                                 'RogueBwTrainerAnim_Stop')
    case('mon does not evict the trainer', check_eviction_is_mirrored, unmirror_b)

    def untick(c):
        c['species'] = drop_call(c['species'], 'RogueBwAnim_Tick',
                                 'RogueBwTrainerAnim_Tick')
    case('tick is never driven', check_hooked_and_freed, untick)

    base = load(repo)
    if run(base, quiet=True):
        print('SELFTEST ABORTED: the unmodified tree already fails.')
        return 1

    bad = 0
    for name, check, mutate in cases:
        c = copy.deepcopy(base)
        mutate(c)
        if check(c):
            print(f'ok    fires on: {name}')
        else:
            bad += 1
            print(f'FAIL  SILENT on: {name}')
    print(f'\n{len(cases) - bad} of {len(cases)} deliberate faults were caught.')
    return 1 if bad else 0


def main():
    args = [a for a in sys.argv[1:]]
    self_ = '--selftest' in args
    args = [a for a in args if not a.startswith('--')]
    repo = Path(args[0]) if args else Path(__file__).resolve().parents[2]

    if self_:
        return selftest(repo)

    ctx = load(repo)
    failed = run(ctx)
    print(f'\n{len(ctx["rows"])} animated trainer pics, '
          f'{len(CHECKS) - failed} of {len(CHECKS)} checks passed.')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
