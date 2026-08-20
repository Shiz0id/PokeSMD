"""Guard the random dungeon trainer pool against upstream junk.

THE RULE, stated once, here:

    A trainer the dungeon may place at random must be a real standalone
    opponent: not a placeholder, not reserved for a boss floor, fielding a
    party that suits the level the pool files it under, and belonging to a
    class whose overworld sprite is a PERSON.

WHY THIS EXISTS. It shipped, and it was found on a screen. Floor 1 kept
producing a level-5 trainer with a GROUDON, drawn as a small red gem that never
turned to face the player. Two independent bugs, both from names:

  1. TRAINER_BRENDAN_PLACEHOLDER and TRAINER_MAY_PLACEHOLDER are upstream
     placeholder entries -- one level-5 Groudon, one level-5 Kyogre -- and both
     were in the generated pool. gen_trainer_table.py excludes bosses by CLASS
     and partners by ID PATTERN, and these two are class "RS Protag" with ids
     that are not TRAINER_RS_*, so neither guard saw them. The tool's comments
     already record two earlier misses of exactly this shape ("Leader Frlg" is
     not "Leader"; the divers needed a prefix because nothing would have known
     to name "Rogue Diver"). This was the third.

     It was not rare, either. Only seven pool entries sat at level 5 or below,
     so the level window in PickTrainerForLevel landed on one of the two
     constantly for the first floors of every run.

  2. sTrainerClassGfx mapped TRAINER_CLASS_RS_PROTAG to OBJ_EVENT_GFX_RUBY and
     OBJ_EVENT_GFX_SAPPHIRE -- the Ruby and Sapphire GEMSTONES from Mt Ember's
     Ruby Path, 16x16 and inanimate. The protagonist sprites are the LINK_RS
     pair, whose graphics infos are named gObjectEventGraphicsInfo_RubySapphire*
     -- so a search for "Ruby" hands you the wrong symbol first, which is the
     never-guess-an-asset-from-a-symbol-name trap with the two candidates one
     line apart in the same enum.

Neither is visible to any existing check: the pool is well formed, the level
column is right, the sprite id is a real member of a real enum, and it builds
clean.

WHAT IT ASSERTS.

  1. No pool id ends in _PLACEHOLDER, and no pool trainer's class is one of the
     reserved boss/frontier classes. gen_trainer_table.py owns the authoritative
     exclusion sets; this restates them so a HAND EDIT of the generated header,
     or a regeneration against a changed trainers.party, cannot put one back
     without the check firing.
  2. Every class row in sTrainerClassGfx names graphics infos with
     inanimate = FALSE. RS Protag was the only inanimate row in the whole table,
     and the discriminator is exact: a trainer is a person, an item is not.
  3. Below pool level MAX_WEAK_LEVEL, no party mon exceeds MAX_WEAK_BST. Both
     numbers are CENSUSED, not guessed: across the corrected pool the strongest
     mon at level 0-24 is 475 (Carolina, level 22), and the bands above that
     legitimately reach 640-670 because vanilla puts Gyarados on a level-25
     fisherman and Salamence on a level-40 cooltrainer. Groudon at level 5 is
     670. The gap between 475 and 670 is where this rule lives.

Usage:  python3 tools/rogue/check_trainer_pool_sanity.py [REPO | --repo PATH]
        python3 tools/rogue/check_trainer_pool_sanity.py --selftest
"""
import re
import sys
import tempfile
import shutil
from pathlib import Path

POOL_H = 'include/constants/rogue_dungeon_trainers.h'
PARTY = 'src/data/trainers.party'
DUNGEON_C = 'src/rogue_dungeon.c'
GFX_H = 'src/data/object_events/object_event_graphics_info.h'
PTR_H = 'src/data/object_events/object_event_graphics_info_pointers.h'
SPECIES_DIR = 'src/data/pokemon/species_info'

SOURCES = [POOL_H, PARTY, DUNGEON_C, GFX_H, PTR_H]

# Restated from gen_trainer_table.py, which owns them. See assertion 1.
RESERVED_CLASSES = {
    'Leader', 'Elite Four', 'Champion', 'Rival',
    'Leader Frlg', 'Elite Four Frlg', 'Champion Frlg',
    'Salon Maiden', 'Dome Ace', 'Palace Maven', 'Arena Tycoon',
    'Factory Head', 'Pike Queen', 'Pyramid King',
    'Magma Leader', 'Aqua Leader', 'Magma Admin', 'Aqua Admin',
    'RS Protag',
}

# EXCLUSIVE. The censused bands are 0-24 (max 475) and 25-29 (max 640, which is
# Fisherman Roger and his Gyarados -- ordinary Emerald, and the first thing this
# rule fired on when the bound was inclusive).
MAX_WEAK_LEVEL = 25
MAX_WEAK_BST = 500

_failures = []


def fail(msg):
    _failures.append(msg)


def read_pool_rows(text):
    return [(t, int(lvl)) for t, lvl in
            re.findall(r'\{\s*(TRAINER_\w+),\s*(\d+)\s*\}', text)]


def read_pool(text):
    return dict(read_pool_rows(text))


def read_parties(text, pool):
    """-> {trainerId: (class, [mon names])} for the pool entries only."""
    out = {}
    blocks = re.split(r'^=== (TRAINER_\w+)[^=]*===\s*$', text, flags=re.M)
    for i in range(1, len(blocks), 2):
        tid, body = blocks[i], blocks[i + 1]
        if tid not in pool:
            continue
        cls = re.search(r'^Class:\s*(.+)$', body, re.M)
        mons = []
        for line in body.splitlines():
            line = line.strip()
            if not line or ':' in line or line.startswith('-'):
                continue
            mons.append(re.sub(r'\s*\(.*\)$', '', line).split('@')[0].strip().upper())
        out[tid] = (cls.group(1).strip() if cls else '', mons)
    return out


def read_bst(repo):
    """-> {SPECIES NAME: base stat total}. This build has no isLegendary flag,
    so strength is measured rather than looked up."""
    bst = {}
    for f in sorted((repo / SPECIES_DIR).glob('gen_*_families.h')):
        text = f.read_text(errors='replace')
        for m in re.finditer(r'\[SPECIES_\w+\] =\s*\n\s*\{', text):
            chunk = text[m.end():m.end() + 4000]
            name = re.search(r'\.speciesName\s*=\s*_\("([^"]+)"\)', chunk)
            stats = {k: int(v) for k, v in re.findall(
                r'\.base(HP|Attack|Defense|Speed|SpAttack|SpDefense)\s*=\s*(\d+)',
                chunk[:1200])}
            if name and len(stats) == 6:
                bst.setdefault(name.group(1).upper(), sum(stats.values()))
    return bst


def check(repo):
    del _failures[:]
    repo = Path(repo)
    for rel in SOURCES:
        if not (repo / rel).exists():
            print('FAIL  check_trainer_pool_sanity.py: %s not found under %s'
                  % (rel, repo))
            return False

    rows = read_pool_rows((repo / POOL_H).read_text(errors='replace'))
    pool = dict(rows)
    if len(rows) != len(pool):
        seen = set()
        for tid, _ in rows:
            if tid in seen:
                fail('%s appears in the pool more than once. '
                     'PickTrainerForLevel picks by INDEX, so a repeat doubles '
                     'that trainer odds -- and the distinct-id loop in the '
                     'caller then throws the second placement away, quietly '
                     'leaving the floor a trainer short' % tid)
            seen.add(tid)
    if not pool:
        print('FAIL  check_trainer_pool_sanity.py: the pool parsed as empty')
        return False
    parties = read_parties((repo / PARTY).read_text(errors='replace'), pool)
    bst = read_bst(repo)

    # -- 1. no placeholders, no reserved classes --
    for tid in sorted(pool):
        if tid.endswith('_PLACEHOLDER'):
            fail('%s is in the pool. Upstream placeholder entries stand in for '
                 'a dynamically built party and theirs is nonsense -- the two '
                 'RS Protag ones are a level-5 Groudon and a level-5 Kyogre'
                 % tid)
        cls = parties.get(tid, ('', []))[0]
        if cls in RESERVED_CLASSES:
            fail('%s is in the pool with the reserved class "%s". That class is '
                 'excluded by gen_trainer_table.py, so either the header was '
                 'hand-edited or the tool and this list have drifted'
                 % (tid, cls))

    # -- 2. every class sprite is a person --
    dungeon_c = (repo / DUNGEON_C).read_text(errors='replace')
    table = re.search(r'sTrainerClassGfx\[TRAINER_CLASS_COUNT\]\s*=\s*\{(.*?)\n\};',
                      dungeon_c, re.S)
    if table is None:
        fail('sTrainerClassGfx is missing or no longer sized by '
             'TRAINER_CLASS_COUNT. Indexing it by a class id is only safe while '
             'it is')
    else:
        ptr_h = (repo / PTR_H).read_text(errors='replace')
        gfx_h = (repo / GFX_H).read_text(errors='replace')
        sym = dict(re.findall(r'\[(OBJ_EVENT_GFX_\w+)\]\s*=\s*&(\w+)', ptr_h))
        infos = {m.group(1): m.group(2) for m in
                 re.finditer(r'(gObjectEventGraphicsInfo_\w+) = \{(.*?)\n\};',
                             gfx_h, re.S)}
        for cls, body in re.findall(r'\[(TRAINER_CLASS_\w+)\]\s*=\s*\{([^}]*)\}',
                                    table.group(1)):
            for gfx in re.findall(r'OBJ_EVENT_GFX_\w+', body):
                info = infos.get(sym.get(gfx, ''))
                if info is None:
                    fail('%s names %s, which has no graphics info' % (cls, gfx))
                elif '.inanimate = TRUE' in info:
                    fail('%s names %s, which is INANIMATE -- an object, not a '
                         'person. A trainer drawn as one stands there as scenery '
                         'and never turns to face the player' % (cls, gfx))

    # -- 3. nothing far too strong for the level it is filed under --
    for tid, level in sorted(pool.items(), key=lambda kv: kv[1]):
        if level >= MAX_WEAK_LEVEL:
            continue
        for mon in parties.get(tid, ('', []))[1]:
            if bst.get(mon, 0) > MAX_WEAK_BST:
                fail('%s is filed at pool level %d but fields %s (base stat '
                     'total %d, over the %d ceiling for levels below %d). The '
                     'strongest legitimate mon in that range is 475, so this is '
                     'not a close call -- it is a placeholder party or a boss '
                     'that leaked into the random pool'
                     % (tid, level, mon, bst[mon], MAX_WEAK_BST, MAX_WEAK_LEVEL))

    if _failures:
        for msg in _failures:
            print('FAIL  check_trainer_pool_sanity.py: %s' % msg)
        return False

    print('ok    check_trainer_pool_sanity.py: %d pool trainers, no placeholders '
          'or reserved classes, every class sprite is a person, nothing over BST '
          '%d below level %d' % (len(pool), MAX_WEAK_BST, MAX_WEAK_LEVEL))
    return True


def selftest(repo):
    repo = Path(repo)
    cases = [
        ('a placeholder is back in the pool', POOL_H,
         lambda s: s.replace('    { TRAINER_CALVIN_1, 5 },',
                             '    { TRAINER_BRENDAN_PLACEHOLDER, 5 },\n'
                             '    { TRAINER_CALVIN_1, 5 },', 1)),
        ('a reserved class leaks in under a different id', POOL_H,
         lambda s: s.replace('    { TRAINER_CALVIN_1, 5 },',
                             '    { TRAINER_MAY_PLACEHOLDER, 5 },', 1)),
        ('a class is drawn as an item again', DUNGEON_C,
         lambda s: s.replace(
             '[TRAINER_CLASS_RS_PROTAG]    = { OBJ_EVENT_GFX_LINK_RS_BRENDAN,'
             ' OBJ_EVENT_GFX_LINK_RS_MAY },',
             '[TRAINER_CLASS_RS_PROTAG]    = { OBJ_EVENT_GFX_RUBY,'
             ' OBJ_EVENT_GFX_SAPPHIRE },', 1)),
        ('a different class is drawn as an item', DUNGEON_C,
         lambda s: s.replace('[TRAINER_CLASS_LASS]         = { OBJ_EVENT_GFX_LASS,',
                             '[TRAINER_CLASS_LASS]         = { OBJ_EVENT_GFX_RUBY,',
                             1)),
        ('sTrainerClassGfx stops being sized by the class count', DUNGEON_C,
         lambda s: s.replace('sTrainerClassGfx[TRAINER_CLASS_COUNT] =',
                             'sTrainerClassGfx[] =', 1)),
        ('a boss party is filed at a low level', POOL_H,
         lambda s: re.sub(r'\{ TRAINER_QUINCY, \d+ \}',
                          '{ TRAINER_QUINCY, 4 }', s, count=1)),
        ('the same trainer is listed twice', POOL_H,
         lambda s: s.replace('    { TRAINER_LYLE, 3 },',
                             '    { TRAINER_LYLE, 3 },\n'
                             '    { TRAINER_LYLE, 3 },', 1)),
    ]

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        # the species tables are read as a directory, so mirror the tree once
        for rel in SOURCES:
            (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(repo / SPECIES_DIR, tmp / SPECIES_DIR)
        base = {rel: (repo / rel).read_text(errors='replace') for rel in SOURCES}

        for name, target, mutate in cases:
            mutated = dict(base)
            mutated[target] = mutate(base[target])
            if mutated[target] == base[target]:
                print('  SELFTEST INCONCLUSIVE (%s): mutation changed nothing'
                      % name)
                ok = False
                continue
            for rel, text in mutated.items():
                (tmp / rel).write_text(text, encoding='utf-8', newline='\n')
            if check(tmp):
                print('  SELFTEST FAILED (%s): the check still passed' % name)
                ok = False
            else:
                print('  selftest ok: fired on "%s"' % name)
    return ok


def main():
    args = list(sys.argv[1:])
    is_selftest = '--selftest' in args
    if is_selftest:
        args.remove('--selftest')
    # Accepts the repo both positionally and via --repo, so it cannot land on
    # the wrong side of run_all_checks.sh's hand-maintained list.
    if '--repo' in args:
        i = args.index('--repo')
        if i + 1 >= len(args):
            print('--repo needs a path')
            return 2
        repo = args[i + 1]
    else:
        repo = args[0] if args else '.'

    if is_selftest:
        print('--- selftest: breaking the rule on purpose ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1
    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
