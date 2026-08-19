"""Guard the run journal: the only cross-run record the game keeps.

THE OWNERSHIP RULE, stated once, here:

    The journal is persistent. Nothing that resets a run may erase it, one
    function is the only writer of the ring's head and count, and every kind the
    save can store has a row whose text and declared params agree.

WHY THIS EXISTS. Every failure this guards is silent and most of them render
perfectly while being wrong.

  - RogueDungeon_ResetRun wipes the party, the bag, the coins, the money, the
    charms and the rod flags, because all of those are per-run. The journal is
    the record OF those runs ending. Clearing it there is a one-line edit that
    looks like tidiness, builds clean, and destroys the whole feature - and the
    player only finds out on the run AFTER the one they wanted to read about.

  - The ring is full when count EQUALS ROGUE_JOURNAL_ENTRIES. A second site
    advancing count without advancing head leaves every later append overwriting
    the NEWEST entry with the oldest, forever, while the card goes on drawing a
    perfectly plausible journal.

  - A kind with no row in sJournalKinds is not absent. The C default fill for an
    omitted designated initialiser is 0, and 0 is ROGUE_JOURNAL_NONE, which is a
    valid row holding a valid empty string. The entry renders as a blank line.
    This is the sRogueMusicPlaylistParent failure in a new place: a sentinel that
    is not distinguishable from the zero value.

  - A row whose text says {STR_VAR_2} while declaring ROGUE_JOURNAL_PARAM_NONE
    prints whatever gStringVar2 was last left holding by an unrelated system -
    not a blank, a real word, dropped into a sentence that reads as if it meant
    it.

That is the same class as check_jukebox_funnels.py and check_bw_published_palette.py:
a lifetime and an ownership, asserted by naming the rule and then asserting the
pairing that makes it true. There is no curve and no atlas here to measure.

WHAT IT CANNOT DO. It cannot tell whether any hook is ever REACHED. An append
sitting in a branch the boss script does not take looks identical, from the
source, to one that fires. That is a screen question.

Usage:  python3 tools/rogue/check_run_journal.py [REPO] [--repo PATH]
        python3 tools/rogue/check_run_journal.py --selftest
"""
import re
import shutil
import sys
import tempfile
from pathlib import Path

CONST_H = 'include/constants/rogue_journal.h'
GLOBAL_H = 'include/global.h'
JOURNAL_C = 'src/rogue_journal.c'
DUNGEON_C = 'src/rogue_dungeon.c'
SPECIALS = 'data/specials.inc'
FLOOR_SCRIPT = 'data/maps/RogueDungeonFloor/scripts.inc'
CARD_C = 'src/trainer_card.c'
CHARMS_C = 'src/rogue_charms.c'

# Every file the check reads or sweeps, so the selftest can build a temp tree.
TRACKED = [CONST_H, GLOBAL_H, JOURNAL_C, DUNGEON_C, SPECIALS, FLOOR_SCRIPT,
           CARD_C, CHARMS_C]

# The single gate on the card, and the functions that must consult it. Each
# fails differently: the print path would draw a link partner's card with this
# console's history, and DrawCardBackStats would strew the vanilla wins/losses
# glyphs across the journal because they are raw tilemap writes no text path
# clears.
CARD_GATE = 'hasJournal'
CARD_GATE_CONSUMERS = ['PrintAllOnCardBack', 'DrawCardBackStats',
                       'TryChangeJournalPage']

# Turning a page must re-expand the lines, not just change the number. The
# indicator and the six rows are drawn from different places - the page counter
# and sData->journalLines - so a redraw that skips the re-buffer renders "Page
# 2/3" above page one's entries. It builds, it draws, and it is wrong.
REDRAW_STATE = 'STATE_REDRAW_BACK_CLEAR'
REDRAW_MUST_CALL = 'BufferJournalPage'

# Sources an append may live in. A kind with no site here is defined, tabled,
# renderable and never written - which is the exact shape of "step 5 added a
# table row and forgot the hook", and it looks identical to a working kind that
# has simply not happened yet.
APPEND_SITES = [JOURNAL_C, DUNGEON_C, CHARMS_C]

# Specials the journal depends on. Each must be BOTH declared and called: an
# undeclared one fails the build, while a declared-and-uncalled one builds clean
# and silently never records its event.
JOURNAL_SPECIALS = [
    'RogueJournal_OnRunStarted',
    'RogueDungeon_JournalBossDefeated',
    'RogueDungeon_JournalDungeonEntered',
]

# One permitted writer per save field. runIndex is separate from the ring's
# bookkeeping because it is stepped once per run rather than once per entry, and
# a second stepper would number two runs the same.
FIELD_WRITERS = {
    'head': 'RogueJournal_Append',
    'count': 'RogueJournal_Append',
    'runIndex': 'RogueJournal_OnRunStarted',
}

# The two TERMINAL paths and the kind each one owns. Neither may write the
# other's: a loss crediting a win is the bug RogueDungeon_OnRunCompleted was
# deliberately kept out of ResetRun to make visible, and this is the same rule
# one layer up.
TERMINALS = {
    'RogueDungeon_TryHandleWhiteOut': 'ROGUE_JOURNAL_RUN_FELL',
    'RogueDungeon_OnRunCompleted': 'ROGUE_JOURNAL_RUN_CLEARED',
}

# Opens a run. A special rather than a C hook, because nothing in C runs exactly
# once per run -- see the note on its definition.
RUN_START_SPECIAL = 'RogueJournal_OnRunStarted'

# Nothing outside this file may reach into the save block for the journal.
OWNER_C = JOURNAL_C


def fail(msg):
    print('FAIL  check_run_journal.py: %s' % msg)
    return False


def read(repo, rel):
    p = Path(repo) / rel
    if not p.exists():
        return None
    return p.read_text(encoding='utf-8', errors='replace')


def kind_ids(const_src):
    """(count, [(name, value)]) for the kind enumerators.

    STRUCTURAL, NOT A BLACKLIST. This started as "every ROGUE_JOURNAL_* define
    except these three names", and the first layout constant added to the header
    -- ROGUE_JOURNAL_LINE_LENGTH -- was promptly reported as a kind with no table
    row. A hand-maintained exclusion list is the same shape as the one
    run_all_checks.sh keeps, which this repo's notes already record drifting
    twice. The kinds are instead exactly the defines ABOVE ROGUE_JOURNAL_KIND_COUNT,
    which is where they have to be for the count to mean anything.
    """
    m = re.search(r'#define\s+ROGUE_JOURNAL_KIND_COUNT\s+(\d+)', const_src)
    if m is None:
        return None, None

    pairs = []
    for name, value in re.findall(r'#define\s+ROGUE_JOURNAL_([A-Z0-9_]+)\s+(\d+)',
                                  const_src[:m.start()]):
        if name.startswith('PARAM_'):
            continue
        pairs.append((name, int(value)))
    return int(m.group(1)), pairs


def kind_rows(journal_src):
    """{KIND: (textSymbol, param1Kind, param2Kind)} from sJournalKinds."""
    m = re.search(r'static const struct RogueJournalKind sJournalKinds'
                  r'\[ROGUE_JOURNAL_KIND_COUNT\]\s*=\s*\{(.*?)\n\};',
                  journal_src, re.S)
    if m is None:
        return None

    rows = {}
    for name, body in re.findall(r'\[ROGUE_JOURNAL_([A-Z0-9_]+)\]\s*=\s*\{([^{}]*)\}',
                                 m.group(1)):
        text = re.search(r'\.text\s*=\s*(\w+)', body)
        p1 = re.search(r'\.param1Kind\s*=\s*(ROGUE_JOURNAL_PARAM_\w+)', body)
        p2 = re.search(r'\.param2Kind\s*=\s*(ROGUE_JOURNAL_PARAM_\w+)', body)
        sep = re.search(r'\.isSeparator\s*=\s*(TRUE|FALSE)', body)
        rows[name] = (text.group(1) if text else None,
                      p1.group(1) if p1 else None,
                      p2.group(1) if p2 else None,
                      sep.group(1) == 'TRUE' if sep else False)
    return rows


def strings(journal_src):
    """{symbol: literal} for the _(\"...\") declarations."""
    out = {}
    for sym, lit in re.findall(r'static const u8 (\w+)\[\]\s*=\s*_\("((?:[^"\\]|\\.)*)"\)',
                               journal_src):
        out[sym] = lit
    return out


def case_block(src, label):
    """The body of one `case LABEL:` arm, up to the next case or the switch end."""
    m = re.search(r'case\s+%s\s*:' % re.escape(label), src)
    if m is None:
        return None
    rest = src[m.end():]
    end = re.search(r'\n\s*case\s+\w+\s*:|\n\s*\}', rest)
    return rest[:end.start()] if end else rest


def strip_comments(src):
    """Blank out comments, preserving offsets and line structure.

    The card's own comments name RogueJournal_* functions while explaining the
    link guard, and a scan that counted those as call sites would report the
    struct definition as an unguarded read.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        if src.startswith('//', i):
            j = src.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif src.startswith('/*', i):
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join('\n' if c == '\n' else ' ' for c in src[i:j]))
            i = j
        else:
            out.append(src[i])
            i += 1
    return ''.join(out)


def function_body(src, name):
    """The body of a function definition, brace-matched."""
    m = re.search(r'\n[\w \*]*\b%s\s*\([^;{]*\)\s*\{' % re.escape(name), src)
    if m is None:
        return None
    depth = 0
    start = m.end() - 1
    for i in range(start, len(src)):
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                return src[start:i + 1]
    return None


def enclosing_function(src, offset):
    """Name of the function definition a source offset falls inside."""
    best = None
    for m in re.finditer(r'\n[\w \*]*?\b(\w+)\s*\([^;{]*\)\s*\{', src):
        if m.start() > offset:
            break
        best = m.group(1)
    return best


def saveblock3_members(global_src):
    m = re.search(r'struct SaveBlock3\s*\{(.*?)\n\};', global_src, re.S)
    if m is None:
        return None
    members = []
    for line in m.group(1).splitlines():
        line = line.strip()
        if not line or line.startswith('//') or line.startswith('#'):
            continue
        decl = re.match(r'(?:struct\s+\w+|\w+)\s+(\w+)\s*(?:\[[^\]]*\])?\s*;', line)
        if decl:
            members.append(decl.group(1))
    return members


def check(repo):
    const_src = read(repo, CONST_H)
    global_src = read(repo, GLOBAL_H)
    journal_src = read(repo, JOURNAL_C)
    dungeon_src = read(repo, DUNGEON_C)

    for rel, src in ((CONST_H, const_src), (GLOBAL_H, global_src),
                     (JOURNAL_C, journal_src), (DUNGEON_C, dungeon_src)):
        if src is None:
            return fail('%s not found' % rel)

    # ---- 1. The ring size must fit the u8 head/count, and be asserted in C.
    m = re.search(r'#define\s+ROGUE_JOURNAL_ENTRIES\s+(\d+)', const_src)
    if m is None:
        return fail('ROGUE_JOURNAL_ENTRIES not defined in %s' % CONST_H)
    entries = int(m.group(1))
    if entries > 255:
        return fail('ROGUE_JOURNAL_ENTRIES is %d; count is a u8 and the ring is '
                    'only full when count EQUALS it, so past 255 it wraps to '
                    'zero, head never advances and the journal reads empty'
                    % entries)
    if not re.search(r'STATIC_ASSERT\(\s*ROGUE_JOURNAL_ENTRIES\s*<=\s*255',
                     journal_src):
        return fail('the ROGUE_JOURNAL_ENTRIES <= 255 STATIC_ASSERT is gone from '
                    '%s -- raising the ring past a u8 would then fail silently '
                    'at runtime instead of loudly at build time' % JOURNAL_C)

    # ---- 1b. The record width, and the two things that hold it.
    #
    # This is guarding a mistake that was actually made. The members total six
    # bytes and the toolchain rounds an unpacked struct up to a multiple of four,
    # so without PACKED the record occupies EIGHT and the ring costs 1028 bytes
    # of EWRAM instead of 772 -- 256 bytes of pure padding in the block this
    # project calls its ceiling. Nothing failed; the linker's EWRAM figure moving
    # by more than the struct accounted for is what exposed it.
    if not re.search(r'struct PACKED RogueJournalEntry', global_src):
        return fail('struct RogueJournalEntry is not PACKED -- its six bytes of '
                    'members would occupy eight, costing 2 bytes per entry')
    if not re.search(r'sizeof\(struct RogueJournalEntry\)\s*==\s*6', journal_src):
        return fail('the sizeof(struct RogueJournalEntry) == 6 STATIC_ASSERT is '
                    'gone from %s -- it is the only thing that fails the build '
                    'if the record silently regrows' % JOURNAL_C)
    if not re.search(r'sizeof\(struct RogueJournal\)\s*==', journal_src):
        return fail('the sizeof(struct RogueJournal) STATIC_ASSERT is gone from '
                    '%s -- it is what catches a header field added without the '
                    'save version being bumped alongside it' % JOURNAL_C)
    # Packing the entry drops the containing struct's alignment to 1, and
    # RogueJournal_Data zeroes it with CpuFill16, which needs at least 2. It
    # lands 4-aligned today only because rogueCharms sits in front of it carrying
    # a u32 -- an accident that holds until someone reorders SaveBlock3.
    if not re.search(r'struct ALIGNED\(\d+\) RogueJournal\b', global_src):
        return fail('struct RogueJournal is not ALIGNED -- the packed entry makes '
                    'it byte-aligned, and RogueJournal_Data zeroes it with a '
                    '16-bit CpuFill16')

    # ---- 2. The journal is the LAST member of SaveBlock3.
    members = saveblock3_members(global_src)
    if members is None:
        return fail('struct SaveBlock3 not found in %s' % GLOBAL_H)
    if 'rogueJournal' not in members:
        return fail('rogueJournal is not a member of struct SaveBlock3')
    if members[-1] != 'rogueJournal':
        return fail('rogueJournal is not the LAST member of SaveBlock3 (last is '
                    '%r) -- anything inserted before an existing member shifts '
                    'every member after it, and an old save then reads the wrong '
                    'bytes for real state' % members[-1])

    # ---- 3. The version is checked on every access, and rewritten after zeroing.
    body = function_body(journal_src, 'RogueJournal_Data')
    if body is None:
        return fail('RogueJournal_Data not found in %s' % JOURNAL_C)
    if 'version != ROGUE_JOURNAL_SAVE_VERSION' not in body:
        return fail('RogueJournal_Data does not check the stored version -- '
                    'load_save.c has no post-load hook, so a save written before '
                    'this struct existed would render whatever bytes were '
                    'already in the sector')
    if 'version = ROGUE_JOURNAL_SAVE_VERSION' not in body:
        return fail('RogueJournal_Data zeroes the journal but never writes the '
                    'current version -- it would re-zero on every single access')

    # ---- 4. One writer per save field, and one owner of the save member.
    # Anchored on the ARROW, not on an identifier before it. `\w+->` misses a
    # write made straight through a call -- RogueJournal_Data()->runIndex++ --
    # which is the most natural way for a second writer to appear, and the
    # selftest caught the check passing that.
    for m in re.finditer(r'->\s*(head|count|runIndex)\s*(?:=[^=]|\+\+|--|\+=|-=)',
                         journal_src):
        field = m.group(1)
        fn = enclosing_function(journal_src, m.start())
        if fn != FIELD_WRITERS[field]:
            return fail('%s writes ->%s, but %s is the only function permitted '
                        'to -- a count that grows without head advancing makes '
                        'every later append overwrite the newest entry with the '
                        'oldest, invisibly, and a second stepper of runIndex '
                        'numbers two runs the same'
                        % (fn or '<file scope>', field, FIELD_WRITERS[field]))

    for rel in TRACKED:
        if rel == OWNER_C or not rel.endswith('.c'):
            continue
        src = read(repo, rel)
        if src and 'gSaveBlock3Ptr->rogueJournal' in src:
            return fail('%s reaches gSaveBlock3Ptr->rogueJournal directly; only '
                        '%s may, and everything else goes through the '
                        'RogueJournal_* functions' % (rel, OWNER_C))

    # ---- 5. ResetRun must NOT touch the journal.
    reset = function_body(dungeon_src, 'RogueDungeon_ResetRun')
    if reset is None:
        return fail('RogueDungeon_ResetRun not found in %s' % DUNGEON_C)
    if 'Journal' in reset:
        return fail('RogueDungeon_ResetRun mentions the journal. It wipes the '
                    'party, bag, coins, money, charms and rod flags because '
                    'those are per-run; the journal is the record OF runs ending '
                    'and must survive exactly what they do not')

    # ---- 6. Every kind has a row.
    kind_count, kind_pairs = kind_ids(const_src)
    if kind_count is None:
        return fail('ROGUE_JOURNAL_KIND_COUNT not defined in %s' % CONST_H)
    if not kind_pairs:
        return fail('no ROGUE_JOURNAL_* kind enumerators found above '
                    'ROGUE_JOURNAL_KIND_COUNT in %s' % CONST_H)

    # The ids must be exactly 0..count-1, with no gap, duplicate or overshoot.
    # Kind ids are STORED IN THE SAVE, so a gap means sJournalKinds is indexed
    # past a row that exists and a duplicate means two events render as one -
    # and both build clean. This also catches the renumbering that the header
    # forbids in words.
    values = sorted(v for _n, v in kind_pairs)
    if values != list(range(kind_count)):
        return fail('the kind ids are %s but ROGUE_JOURNAL_KIND_COUNT is %d -- '
                    'they must be exactly 0..%d, since the ids are stored in the '
                    'save and index sJournalKinds directly'
                    % (values, kind_count, kind_count - 1))

    ids = [n for n, _v in kind_pairs]
    rows = kind_rows(journal_src)
    if rows is None:
        return fail('sJournalKinds table not found in %s' % JOURNAL_C)
    for name in ids:
        if name not in rows:
            return fail('ROGUE_JOURNAL_%s has no row in sJournalKinds -- the '
                        'default fill is 0, which is ROGUE_JOURNAL_NONE, a valid '
                        'row holding a valid empty string, so the entry renders '
                        'as a blank line rather than as anything wrong' % name)
    for name in rows:
        if name not in ids:
            return fail('sJournalKinds has a row for ROGUE_JOURNAL_%s, which is '
                        'not a defined kind' % name)

    # ---- 7. Placeholders and declared params agree, both directions.
    lits = strings(journal_src)
    for name in ids:
        sym, p1, p2, _sep = rows[name]
        if sym is None or p1 is None or p2 is None:
            return fail('the sJournalKinds row for ROGUE_JOURNAL_%s does not set '
                        'all of .text, .param1Kind and .param2Kind' % name)
        if sym not in lits:
            return fail('the sJournalKinds row for ROGUE_JOURNAL_%s names text '
                        '%s, which is not a string literal in this file'
                        % (name, sym))
        text = lits[sym]
        for slot, kind in ((1, p1), (2, p2)):
            declared = kind != 'ROGUE_JOURNAL_PARAM_NONE'
            present = ('{STR_VAR_%d}' % slot) in text
            if declared and not present:
                return fail('ROGUE_JOURNAL_%s declares param%d as %s but its text '
                            'has no {STR_VAR_%d} -- the value is stored and never '
                            'shown' % (name, slot, kind, slot))
            if present and not declared:
                return fail('ROGUE_JOURNAL_%s uses {STR_VAR_%d} but declares '
                            'param%d as NONE -- nothing fills that buffer, so the '
                            'line prints whatever an unrelated system left in it'
                            % (name, slot, slot))

    # ---- 8. Every separator kind is actually written by something.
    #
    # Generic rather than a hand-written list of the three, so a separator added
    # later cannot be defined, tabled, rendered and never appended - which is a
    # run boundary that silently does not exist, and the reader partitions the
    # ring on exactly these.
    site_srcs = {rel: (read(repo, rel) or '') for rel in APPEND_SITES}
    separators = [n for n in ids if rows[n][3]]
    if not separators:
        return fail('no row in sJournalKinds sets isSeparator -- the reader '
                    'partitions the ring on separators, so there would be no run '
                    'boundaries at all')

    # EVERY kind, not only the separators. This started as a separators-only
    # rule when the three boundaries were all that existed; step 5 added seven
    # event kinds, and a rule scoped to separators would have watched all seven
    # go unhooked without a word.
    for name in ids:
        if name == 'NONE':
            continue  # the placeholder row is never appended, by design
        needle = 'RogueJournal_Append(ROGUE_JOURNAL_%s' % name
        if not any(needle in src for src in site_srcs.values()):
            return fail('ROGUE_JOURNAL_%s has no append site in %s -- the kind '
                        'exists, has a row and renders, and nothing ever writes '
                        'it, which is indistinguishable from an event that has '
                        'simply not happened yet'
                        % (name, ' or '.join(APPEND_SITES)))

    # ---- 9. Each terminal path writes its OWN outcome and not the other's.
    for fn_name, kind in TERMINALS.items():
        fn = function_body(dungeon_src, fn_name)
        if fn is None:
            return fail('%s not found in %s' % (fn_name, DUNGEON_C))
        if ('RogueJournal_Append(%s' % kind) not in fn:
            return fail('%s does not append %s -- the run ends with no boundary '
                        'in the journal, so it reads as though it merged into '
                        'the next one' % (fn_name, kind))
        for other_fn, other_kind in TERMINALS.items():
            if other_fn == fn_name:
                continue
            if ('RogueJournal_Append(%s' % other_kind) in fn:
                return fail('%s appends %s, which belongs to %s -- a loss '
                            'crediting a win is exactly the bug that keeps '
                            'OnRunCompleted out of ResetRun'
                            % (fn_name, other_kind, other_fn))

    # ---- 10. The run-start special is declared AND called.
    #
    # Both halves, because they fail differently: undeclared fails the build,
    # while declared-and-never-called builds clean and leaves every run in the
    # journal with no opening boundary.
    specials_src = read(repo, SPECIALS)
    script_src = read(repo, FLOOR_SCRIPT)
    if specials_src is None or script_src is None:
        return fail('%s or %s not found' % (SPECIALS, FLOOR_SCRIPT))
    for name in JOURNAL_SPECIALS:
        if ('def_special %s' % name) not in specials_src:
            return fail('%s is not declared in %s' % (name, SPECIALS))
        if not re.search(r'(?<!def_)special %s\b' % re.escape(name), script_src):
            return fail('%s is declared but never called from %s -- it would '
                        'build clean and silently record nothing'
                        % (name, FLOOR_SCRIPT))

    # ---- 11. THE LINK GUARD on the trainer card.
    #
    # Every other field on the card's back comes from sData->trainerCard, which
    # for a received card is the PARTNER'S data. The journal does not - it comes
    # from SaveBlock3, which is always this console's - so a journal drawn on a
    # link card is the local player's run history printed under someone else's
    # name, with nothing on screen looking wrong. sData->hasJournal is the single
    # gate that stops it, so all three halves are asserted: it is assigned once,
    # that assignment is under !sData->isLink, and everything that reads or draws
    # the journal consults it.
    card_src = read(repo, CARD_C)
    if card_src is None:
        return fail('%s not found' % CARD_C)
    card_code = strip_comments(card_src)

    assigns = re.findall(r'%s\s*=\s*([^;]*);' % CARD_GATE, card_code)
    if len(assigns) != 1:
        return fail('%s is assigned %d times in %s; it must be assigned exactly '
                    'once, or the gate can be reopened somewhere the link check '
                    'does not run' % (CARD_GATE, len(assigns), CARD_C))
    if '!sData->isLink' not in assigns[0]:
        return fail('the %s assignment in %s does not test !sData->isLink -- a '
                    'received card would render THIS console\'s run journal '
                    'under the partner\'s name' % (CARD_GATE, CARD_C))

    gate_fn = enclosing_function(card_code, card_code.index(CARD_GATE + ' ='))
    if gate_fn != 'SetDataFromTrainerCard':
        return fail('%s is assigned in %s rather than in SetDataFromTrainerCard, '
                    'which is where every other per-card gate is derived'
                    % (CARD_GATE, gate_fn or '<file scope>'))

    for fn_name in CARD_GATE_CONSUMERS:
        fn = function_body(card_code, fn_name)
        if fn is None:
            return fail('%s not found in %s' % (fn_name, CARD_C))
        if CARD_GATE not in fn:
            return fail('%s does not consult sData->%s -- the journal and the '
                        'vanilla back would draw over each other, and a link '
                        'card would show the local journal'
                        % (fn_name, CARD_GATE))

    for m in re.finditer(r'RogueJournal_\w+', card_code):
        fn = enclosing_function(card_code, m.start())
        body = function_body(card_code, fn) if fn else None
        if body is None or CARD_GATE not in body:
            return fail('%s in %s reads the journal without consulting '
                        'sData->%s -- every read on this screen has to be behind '
                        'the one gate that knows whether the card is a link card'
                        % (fn or '<file scope>', CARD_C, CARD_GATE))

    # ---- 12. PAGING re-expands the lines rather than only moving the number.
    #
    # The indicator comes from sData->journalPage and the six rows come from
    # sData->journalLines, which are filled by a separate pass. A redraw that
    # changes the first without redoing the second draws "Page 2/3" over page
    # one's entries - clean build, plausible screen, wrong content. Nothing else
    # in this check or in the ROM tests can see it, because both halves are
    # individually correct.
    redraw = case_block(card_code, REDRAW_STATE)
    if redraw is None:
        return fail('%s not found in %s -- paging must redraw through the '
                    'per-frame print machine, not a synchronous loop'
                    % (REDRAW_STATE, CARD_C))
    if REDRAW_MUST_CALL not in redraw:
        return fail('%s does not call %s -- turning a page would move the '
                    'indicator and leave the previous page\'s six lines on '
                    'screen' % (REDRAW_STATE, REDRAW_MUST_CALL))

    # And the input arm has to reach it, or the d-pad does nothing at all.
    back_input = case_block(card_code, 'STATE_HANDLE_INPUT_BACK')
    if back_input is None:
        return fail('STATE_HANDLE_INPUT_BACK not found in %s' % CARD_C)
    if 'TryChangeJournalPage' not in back_input or REDRAW_STATE not in back_input:
        return fail('STATE_HANDLE_INPUT_BACK does not route a page change into '
                    '%s -- the page would move with nothing redrawing it'
                    % REDRAW_STATE)

    # ---- 13. The page count is derived once, beside the gate.
    page_assigns = re.findall(r'journalPages\s*=\s*[^;]*;', card_code)
    if len(page_assigns) != 2:
        return fail('journalPages is assigned %d times in %s; it must be the '
                    'zero and the one derivation in SetDataFromTrainerCard, so '
                    'the bound the paging clamps against has a single source'
                    % (len(page_assigns), CARD_C))
    for m in re.finditer(r'journalPages\s*=\s*[^;]*;', card_code):
        fn = enclosing_function(card_code, m.start())
        if fn != 'SetDataFromTrainerCard':
            return fail('journalPages is assigned in %s rather than in '
                        'SetDataFromTrainerCard' % (fn or '<file scope>'))

    # ---- 14. THE STAR COUNT IS CLAMPED.
    #
    # stars indexes sHoennTrainerCardPals[], which has exactly five entries, and
    # it is a field sent over link. Now that it counts runs cleared rather than
    # the four unreachable vanilla conditions, it is an unbounded save var - so
    # a fifth win unclamped reads a palette pointer past the end of that table.
    # Nothing about that fails to build, and the card simply comes up in whatever
    # colours followed it in ROM.
    stars_fn = function_body(card_code, 'CountPlayerTrainerStars')
    if stars_fn is None:
        return fail('CountPlayerTrainerStars not found in %s' % CARD_C)
    if not re.search(r'<\s*4|>\s*4|4\s*:|min', stars_fn):
        return fail('CountPlayerTrainerStars does not clamp to 4 -- it indexes '
                    'sHoennTrainerCardPals[], which has five entries, and it is '
                    'sent over link')

    print('PASS  check_run_journal.py  (%d kinds, %d separators, all appended; '
          '%d-entry ring, %d B; one writer per field; card gated on %s; paging '
          'redraws; stars clamped)'
          % (len(ids), len(separators), entries, entries * 6 + 4, CARD_GATE))
    return True


def selftest(repo):
    """Break each rule on purpose and require the check to fire.

    THE BASELINE GUARD BELOW IS NOT CEREMONY. Without it, a check that already
    fails on the clean tree makes every single mutation "fire" - the check
    returns False whatever was done to it - and the harness reports `selftest
    passed` while proving nothing at all. That happened here, on the run that
    added the event kinds: the tree was genuinely broken, all cases went green,
    and the summary line was a lie. A break test that cannot distinguish "the
    mutation was caught" from "everything is on fire" is the exact failure this
    whole file exists to prevent, one level up.
    """
    print('  baseline: the unmutated tree must PASS before any break means '
          'anything')
    if not check(repo):
        print('  SELFTEST ABORTED: the check does not pass on the clean tree, so '
              'every mutation would "fire" for the wrong reason')
        return False

    cases = [
        ('ring raised past a u8', CONST_H,
         lambda s: s.replace('#define ROGUE_JOURNAL_ENTRIES 128',
                             '#define ROGUE_JOURNAL_ENTRIES 256', 1)),
        ('the ring STATIC_ASSERT deleted', JOURNAL_C,
         lambda s: re.sub(r'STATIC_ASSERT\(\s*ROGUE_JOURNAL_ENTRIES\s*<=\s*255[^;]*;',
                          '', s, count=1)),
        ('the record unpacked', GLOBAL_H,
         lambda s: s.replace('struct PACKED RogueJournalEntry',
                             'struct RogueJournalEntry', 1)),
        ('the record-width STATIC_ASSERT deleted', JOURNAL_C,
         lambda s: re.sub(r'STATIC_ASSERT\(sizeof\(struct RogueJournalEntry\)[^;]*;',
                          '', s, count=1)),
        ('the ring-size STATIC_ASSERT deleted', JOURNAL_C,
         lambda s: re.sub(r'STATIC_ASSERT\(sizeof\(struct RogueJournal\)[^;]*;',
                          '', s, count=1)),
        ('the journal struct unaligned', GLOBAL_H,
         lambda s: s.replace('struct ALIGNED(4) RogueJournal\n',
                             'struct RogueJournal\n', 1)),
        ('journal no longer the last SaveBlock3 member', GLOBAL_H,
         lambda s: s.replace('    struct RogueJournal rogueJournal;\n'
                             '}; /* max size 1624 bytes */',
                             '    struct RogueJournal rogueJournal;\n'
                             '    u8 somethingLater;\n'
                             '}; /* max size 1624 bytes */', 1)),
        ('version never checked', JOURNAL_C,
         lambda s: s.replace('data->version != ROGUE_JOURNAL_SAVE_VERSION',
                             'data->head == 0xFF', 1)),
        ('version never rewritten after zeroing', JOURNAL_C,
         lambda s: s.replace('        data->version = ROGUE_JOURNAL_SAVE_VERSION;\n',
                             '', 1)),
        ('a second writer of count', JOURNAL_C,
         lambda s: s.replace('u32 RogueJournal_Count(void)\n{\n',
                             'u32 RogueJournal_Count(void)\n{\n'
                             '    struct RogueJournal *d = RogueJournal_Data();\n'
                             '    d->count = 0;\n', 1)),
        ('head advanced outside Append', JOURNAL_C,
         lambda s: s.replace('    if (index >= data->count)',
                             '    data->head++;\n    if (index >= data->count)', 1)),
        ('another file reaches the save member', DUNGEON_C,
         lambda s: s.replace('void RogueDungeon_ResetRun(void)\n{\n',
                             'void RogueDungeon_ResetRun(void)\n{\n'
                             '    gSaveBlock3Ptr->rogueJournal.count = 0;\n', 1)),
        ('ResetRun clears the journal', DUNGEON_C,
         lambda s: s.replace('    VarSet(VAR_ROGUE_DUNGEON_FLOOR, 0);',
                             '    RogueJournal_Reset();\n'
                             '    VarSet(VAR_ROGUE_DUNGEON_FLOOR, 0);', 1)),
        ('a kind loses its row', JOURNAL_C,
         lambda s: re.sub(r'\n    \[ROGUE_JOURNAL_RUN_CLEARED\]\s*=\s*\{[^{}]*\},\n',
                          '\n', s, count=1)),
        # ADDS a row rather than renaming one. Renaming trips the missing-row
        # rule first, so the extra-row rule would go untested behind a selftest
        # line that read as green.
        ('a gap in the kind ids', CONST_H,
         lambda s: s.replace('#define ROGUE_JOURNAL_RUN_CLEARED 3',
                             '#define ROGUE_JOURNAL_RUN_CLEARED 4', 1)),
        ('a row for a kind that does not exist', JOURNAL_C,
         lambda s: s.replace('    [ROGUE_JOURNAL_RUN_CLEARED] =',
                             '    [ROGUE_JOURNAL_RUN_VANISHED] =\n'
                             '    {\n'
                             '        .text = sText_JournalRunCleared,\n'
                             '        .param1Kind = ROGUE_JOURNAL_PARAM_NONE,\n'
                             '        .param2Kind = ROGUE_JOURNAL_PARAM_NONE,\n'
                             '        .isSeparator = TRUE,\n'
                             '    },\n\n'
                             '    [ROGUE_JOURNAL_RUN_CLEARED] =', 1)),
        ('a declared param with no placeholder', JOURNAL_C,
         lambda s: s.replace('static const u8 sText_JournalRunFell[]    = '
                             '_("Fell on floor {STR_VAR_1}");',
                             'static const u8 sText_JournalRunFell[]    = '
                             '_("Fell short");', 1)),
        ('a placeholder with no declared param', JOURNAL_C,
         lambda s: s.replace('static const u8 sText_JournalRunCleared[] = '
                             '_("Cleared the dungeon");',
                             'static const u8 sText_JournalRunCleared[] = '
                             '_("Cleared the dungeon with {STR_VAR_2}");', 1)),

        # --- step 2: the boundaries ---
        ('a second stepper of runIndex', JOURNAL_C,
         lambda s: s.replace('u32 RogueJournal_Count(void)\n{\n',
                             'u32 RogueJournal_Count(void)\n{\n'
                             '    RogueJournal_Data()->runIndex++;\n', 1)),
        ('the losing terminal deleted', DUNGEON_C,
         lambda s: re.sub(r'    RogueJournal_Append\(ROGUE_JOURNAL_RUN_FELL,[^;]*;',
                          '', s, count=1)),
        ('the winning terminal deleted', DUNGEON_C,
         lambda s: re.sub(r'    RogueJournal_Append\(ROGUE_JOURNAL_RUN_CLEARED,[^;]*;',
                          '', s, count=1)),
        ('the losing path credits a win', DUNGEON_C,
         lambda s: s.replace('RogueJournal_Append(ROGUE_JOURNAL_RUN_FELL, 0,\n'
                             '                        VarGet(VAR_ROGUE_LAST_RUN_FLOOR), 0);',
                             'RogueJournal_Append(ROGUE_JOURNAL_RUN_FELL, 0,\n'
                             '                        VarGet(VAR_ROGUE_LAST_RUN_FLOOR), 0);\n'
                             '    RogueJournal_Append(ROGUE_JOURNAL_RUN_CLEARED, 0, 0, 0);', 1)),
        # Targets the "there must BE separators" half. This case used to flip the
        # NONE placeholder INTO a separator, which fired while rule 8 covered
        # separators only; generalising that rule to every kind - and exempting
        # NONE - silently retired it. The baseline guard is what surfaced that,
        # by making the case fail honestly instead of passing in a sea of red.
        ('no separators at all', JOURNAL_C,
         lambda s: s.replace('.isSeparator = TRUE', '.isSeparator = FALSE')),
        ('the run-start special undeclared', SPECIALS,
         lambda s: s.replace('\tdef_special RogueJournal_OnRunStarted\n', '', 1)),
        ('the run-start special never called', FLOOR_SCRIPT,
         lambda s: s.replace('\tspecial RogueJournal_OnRunStarted\n', '', 1)),

        # --- step 3: the card ---
        ('the link guard dropped', CARD_C,
         lambda s: s.replace('(!sData->isLink && RogueJournal_Count() != 0)',
                             '(RogueJournal_Count() != 0)', 1)),
        ('a second assignment of the gate', CARD_C,
         lambda s: s.replace('static void BufferJournalPage(void)\n{\n',
                             'static void BufferJournalPage(void)\n{\n'
                             '    sData->hasJournal = TRUE;\n', 1)),
        ('the gate derived outside SetDataFromTrainerCard', CARD_C,
         lambda s: s.replace(
             '    sData->hasJournal = (!sData->isLink && RogueJournal_Count() != 0);\n',
             '', 1).replace('static void BufferJournalPage(void)\n{\n',
                            'static void BufferJournalPage(void)\n{\n'
                            '    sData->hasJournal = (!sData->isLink && RogueJournal_Count() != 0);\n', 1)),
        ('the back print path ignores the gate', CARD_C,
         lambda s: s.replace('    if (sData->hasJournal)\n    {\n'
                             '        switch (sData->printState)',
                             '    if (FALSE)\n    {\n'
                             '        switch (sData->printState)', 1)),
        ('DrawCardBackStats no longer stands aside', CARD_C,
         lambda s: s.replace('    if (sData->hasJournal)\n        return;\n\n', '', 1)),
        ('a journal read outside the gate', CARD_C,
         lambda s: s.replace('static void PrintNameOnCardBack(void)\n{\n',
                             'static void PrintNameOnCardBack(void)\n{\n'
                             '    if (RogueJournal_Count() != 0)\n        return;\n', 1)),

        # --- step 4: paging ---
        ('the page turns without re-expanding the lines', CARD_C,
         lambda s: s.replace('        BufferJournalPage();\n', '', 1)),
        ('paging ungated on the journal', CARD_C,
         lambda s: s.replace('    if (!sData->hasJournal || sData->journalPages <= 1)',
                             '    if (sData->journalPages <= 1)', 1)),
        ('the d-pad never reaches the redraw', CARD_C,
         lambda s: s.replace('        if (TryChangeJournalPage())\n', '        if (FALSE)\n', 1)),
        ('a second derivation of the page count', CARD_C,
         lambda s: s.replace('static bool8 TryChangeJournalPage(void)\n{\n',
                             'static bool8 TryChangeJournalPage(void)\n{\n'
                             '    sData->journalPages = 1;\n', 1)),

        # --- steps 5 and 6: the events and the front ---
        ('an event kind with no append site', DUNGEON_C,
         lambda s: re.sub(r'    RogueJournal_Append\(ROGUE_JOURNAL_TM_TAKEN,[^;]*;',
                          '', s, count=1)),
        ('a charm kind with no append site', CHARMS_C,
         lambda s: re.sub(r'        RogueJournal_Append\(ROGUE_JOURNAL_CHARM_PARTY,[^;]*;',
                          '', s, count=1)),
        ('the boss special undeclared', SPECIALS,
         lambda s: s.replace('\tdef_special RogueDungeon_JournalBossDefeated\n', '', 1)),
        ('the boss special never called', FLOOR_SCRIPT,
         lambda s: s.replace('\tspecial RogueDungeon_JournalBossDefeated\n', '', 1)),
        ('the dungeon special never called', FLOOR_SCRIPT,
         lambda s: s.replace('\tspecial RogueDungeon_JournalDungeonEntered\n', '', 2)),
        ('the star clamp removed', CARD_C,
         lambda s: s.replace('    return runs < 4 ? runs : 4;', '    return runs;', 1)),
    ]

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        for name, target, mutate in cases:
            for rel in TRACKED:
                dst = tmp / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                src = read(repo, rel)
                if src is None:
                    print('  SELFTEST INCONCLUSIVE (%s): %s missing from the repo'
                          % (name, rel))
                    ok = False
                    src = ''
                if rel == target:
                    mutated = mutate(src)
                    if mutated == src:
                        print('  SELFTEST INCONCLUSIVE (%s): the mutation changed '
                              'nothing -- the source has moved under this test'
                              % name)
                        ok = False
                    src = mutated
                dst.write_text(src, encoding='utf-8', newline='\n')

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

    # Takes the repo BOTH ways on purpose -- see the note in
    # check_start_menu_pages.py. run_all_checks.sh keeps a hand-written list of
    # which checks want a positional path and which want --repo, and a check that
    # accepts either can never end up on the wrong side of it.
    if '--repo' in args:
        i = args.index('--repo')
        if i + 1 >= len(args):
            print('--repo needs a path')
            return 2
        repo = args[i + 1]
    elif args:
        repo = args[0]
    else:
        repo = '.'

    if is_selftest:
        print('--- selftest: breaking the invariant on purpose ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1

    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
