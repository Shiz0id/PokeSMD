"""
Guard who owns a generated movement path, and who is allowed to free it.

This is the fifteenth check and the first that looks at the LIFETIME of memory
rather than at generated data. It exists because a double free shipped on
feature/pathfinder-hunt, built clean, passed all fourteen other checks, and
presented as an occasional crash near a hunting trainer -- which is the least
useful symptom heap corruption can have, because it cashes in somewhere else.

THE SHAPE OF THE BUG. A path from ReconstructPath is one Alloc, and TWO files
believe they own it:

  script_movement.c   frees it in the MOVEMENT_ACTION_GENERATED_END branch of
                      ScriptMovement_TakeStep -- the vendor's own code, added in
                      the same commit as the path finder
  path_finding.c      tracks it in sTrackedGeneratedScript so it can release the
                      paths that never reach that branch

Every generated path ends in GENERATED_END, so a path walked to completion is
freed by the first. The hunt then repaths -- and it only repaths once
ScriptMovement_IsObjectMovementFinished is TRUE, which is only reachable
BECAUSE that branch ran. So the release on the next repath frees an
already-freed block, every time, on every hunt. Not a race, not a rare path: a
guaranteed double free that the compiler cannot see because the two frees are in
different translation units.

WHAT THIS ENFORCES.

  1. script_movement.c's free of a generated path notifies path_finding.c on the
     same pointer. This is the pairing that makes single ownership true.
  2. path_finding.c NULLs the tracked pointer everywhere it frees it.
  3. The tracked pointer is only ever armed straight after a release, so it
     cannot be overwritten while it still owns something.
  4. Every branch of rogue_hunt.c that ends a chase releases or cancels the
     path first. Missing this leaks rather than crashes -- one buffer per
     abandoned chase, across a 115 floor run, on the heap this build actually
     runs out of.
  5. RogueHunt_Tick decides whether it may hunt from the LIVE map, not from a
     flag set when the floor loaded. The tick runs from OverworldBasic on every
     map in the game while the floor load runs only on a dungeon floor, so a
     latched flag stayed set through the warp and the rest stop's shopkeeper
     started walking at the player.

Run:  python3 tools/rogue/check_path_ownership.py
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

TRACKED = 'sTrackedGeneratedScript'
NOTIFY = 'PathFinder_OnGeneratedScriptFreed'
RELEASE = 'PathFinder_ReleaseTrackedScript'
CANCEL = 'PathFinder_CancelTrackedMovement'


def read(rel):
    path = REPO / rel
    if not path.exists():
        raise SystemExit(f'{rel} is missing - has it been renamed?')
    return path.read_text()


def block_after(text, needle):
    """The braced block opened at or after `needle`, by brace depth.

    Used instead of a line window because these branches have grown before and
    a fixed window would silently stop covering them."""
    start = text.find(needle)
    if start < 0:
        return None
    open_at = text.find('{', start)
    if open_at < 0:
        return None
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[open_at:i + 1]
    return None


def enclosing_block(text, pos):
    """The innermost braced block containing `pos`, braces included.

    Counts backwards for the unmatched `{` rather than matching a function
    signature, so an `if` branch is scoped to itself and not to the whole
    function it sits in."""
    depth = 0
    open_at = None
    for i in range(pos, -1, -1):
        if text[i] == '}':
            depth += 1
        elif text[i] == '{':
            if depth == 0:
                open_at = i
                break
            depth -= 1
    if open_at is None:
        return None

    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[open_at:i + 1]
    return None


def check_script_movement(bad):
    """1. The vendor's own free must tell path_finding.c about it."""
    text = read('src/script_movement.c')

    branch = block_after(text, 'MOVEMENT_ACTION_GENERATED_END')
    if branch is None:
        bad.append('script_movement.c: no MOVEMENT_ACTION_GENERATED_END branch '
                   'found - if the vendor free moved, this check is blind and '
                   'must be re-aimed rather than deleted')
        return

    freed = re.findall(r'\bFree\(\s*([A-Za-z_]\w*)\s*\)', branch)
    if not freed:
        # No free here is fine -- but only if nothing else took it over.
        if 'Free(' in text:
            bad.append('script_movement.c: GENERATED_END no longer frees, but '
                       'the file still frees somewhere - re-check ownership by '
                       'hand before trusting this')
        return

    notified = re.findall(NOTIFY + r'\(\s*([A-Za-z_]\w*)\s*\)', branch)
    for name in freed:
        if name not in notified:
            bad.append(f'script_movement.c: GENERATED_END frees `{name}` '
                       f'without calling {NOTIFY}({name}). path_finding.c is '
                       f'still tracking that block, and its next release frees '
                       f'it a second time.')


def check_path_finding(bad):
    """2 and 3. The tracked pointer is armed and disarmed in lockstep."""
    text = read('src/path_finding.c')

    if NOTIFY not in text:
        bad.append(f'path_finding.c: {NOTIFY} is gone. script_movement.c has no '
                   f'way to say it freed a path, so the tracked pointer '
                   f'outlives the block it points at.')

    lines = text.split('\n')

    # 2. Freeing the tracked pointer must NULL it, within the same block.
    for i, line in enumerate(lines):
        if re.search(r'\bFree\(\s*' + TRACKED + r'\s*\)', line):
            window = '\n'.join(lines[i:i + 4])
            if not re.search(TRACKED + r'\s*=\s*NULL', window):
                bad.append(f'path_finding.c:{i + 1}: frees {TRACKED} without '
                           f'setting it to NULL - the next release frees it '
                           f'again')

    # 3. Arming it must be immediately preceded by a release, or the previous
    #    path is dropped while something is still walking it.
    for i, line in enumerate(lines):
        m = re.search(TRACKED + r'\s*=\s*(?!NULL)(\S.*)', line)
        if not m:
            continue
        window = '\n'.join(lines[max(0, i - 6):i])
        if RELEASE not in window and NOTIFY not in window:
            bad.append(f'path_finding.c:{i + 1}: arms {TRACKED} without a '
                       f'{RELEASE}() first - whatever it pointed at before is '
                       f'leaked, or is still installed and now untracked')


def check_hunt(bad):
    """4 and 5. Ending a chase releases the path; hunting is decided live."""
    text = read('src/rogue_hunt.c')

    # 5 first: the map gate. A latched enable is the bug this replaced.
    tick = block_after(text, 'void RogueHunt_Tick(void)')
    if tick is None:
        bad.append('rogue_hunt.c: RogueHunt_Tick not found')
    elif 'mapLayoutId' not in text:
        bad.append('rogue_hunt.c: nothing reads gMapHeader.mapLayoutId. The '
                   'tick runs on EVERY map, so without a live map test the '
                   'rest stop and the Safari hunt with the last floor\'s state.')

    # 4: every branch that stands the hunter down frees the path it was walking.
    #
    # Scoped to the INNERMOST ENCLOSING BLOCK rather than to a window of lines
    # either side. A window has to guess whether the release comes before or
    # after the assignment -- RogueHunt_OnFloorLoad releases last, the branches
    # in UpdateActiveHunter release first -- and a window wide enough for both
    # is wide enough to read one branch's release as the next branch's.
    for m in re.finditer(r'sActiveHunter\s*=\s*HUNT_NO_HUNTER', text):
        block = enclosing_block(text, m.start())
        if block is None:
            continue
        if RELEASE not in block and CANCEL not in block:
            line_no = text.count('\n', 0, m.start()) + 1
            bad.append(f'rogue_hunt.c:{line_no}: stands the hunter down without '
                       f'{RELEASE}() or {CANCEL}() in the same block - the path '
                       f'it was walking stays owned with nothing to free it')


def main():
    bad = []
    check_script_movement(bad)
    check_path_finding(bad)
    check_hunt(bad)

    for line in bad:
        print('FAIL:', line)
    if bad:
        return 1

    print('generated paths have one owner: script_movement.c frees on '
          'GENERATED_END and notifies, path_finding.c frees everything else')
    print('every hunt stand-down releases its path, and hunting is gated on the '
          'live map rather than a latched flag')
    return 0


if __name__ == '__main__':
    sys.exit(main())
