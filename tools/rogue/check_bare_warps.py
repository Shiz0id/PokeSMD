"""No script may call `warp` without arguments.

A bare `warp MAP_X` looks like the tidiest possible way to say "put the player
on that map", and it is the one form that is broken.

formatwarp fills the missing x and y with -1. ScrCmd_warp puts those through
VarGet, and VarGet(0xFFFF) indexes gSpecialVars[0xFFFF - 0x8000] = [32767] on
an array of 22 pointers - a read 131068 bytes past its end, into script data -
then dereferences the result. x and y become whatever bytes happened to be
there. SetWarpDestination narrows them to s8, so:

  low byte >= 0x80  ->  negative  ->  the intended map-centre fallback happens
                                      and everything looks correct
  low byte <  0x80  ->  positive  ->  the player is dropped at a junk tile,
                                      usually outside the map, standing in an
                                      endless field of border metatile

Which one a build gets is FIXED, because the address holds ordinary script
data. That is what makes this worth a check rather than a comment: it does not
fail when you write it, it fails later, when an unrelated script grows and
moves the bytes it was reading. This branch cost a full debugging session
exactly that way - the post-Roxanne rest stop warp broke because a game-corner
menu three files away got longer.

Vanilla never relies on it: 140 warps in the tree pass arguments and the only
bare one was ours.

Usage:  python3 tools/rogue/check_bare_warps.py [--repo PATH]
"""
import argparse
import os
import re
import sys
from pathlib import Path

# Only the commands that go through formatwarp, because only those inherit the
# dummy coords. Taken from asm/macros/event.inc, not from what sounds like a
# warp: `warphole` is declared `map:req` with no coord parameters at all and
# reads the player's current position instead, so its three bare call sites in
# vanilla are correct. Listing it here made this check fail on untouched
# vanilla scripts, which is the tell that the rule was wrong rather than the
# code.
WARP_CMDS = ('warp', 'warpsilent', 'warpdoor', 'warpteleport',
             'warpmossdeepgym', 'setwarp', 'setdynamicwarp')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=os.environ.get('POKEDECOMP_REPO', '.'))
    args = ap.parse_args()
    repo = Path(args.repo).resolve()

    pattern = re.compile(r'^(%s)\s+(MAP_[A-Z0-9_]+)\s*$' % '|'.join(WARP_CMDS))

    bare, total = [], 0
    for path in sorted(repo.glob('data/**/*.inc')):
        rel = path.relative_to(repo)
        for i, line in enumerate(path.read_text(errors='replace').splitlines(), 1):
            s = line.split('@')[0].strip()
            if not s:
                continue
            if re.match(r'^(%s)\s+MAP_' % '|'.join(WARP_CMDS), s):
                total += 1
                m = pattern.match(s)
                if m:
                    bare.append((str(rel), i, s))

    print(f'warp-family calls naming a map: {total}')
    print(f'without arguments             : {len(bare)}\n')
    for rel, i, s in bare:
        print(f'  FAIL: {rel}:{i}')
        print(f'        {s}')
        print(f'        add a warp id, or an x/y pair - see the header of this script')
    print(f'{"FAILED" if bare else "ok"}: {len(bare)} bare warp(s)')
    return 1 if bare else 0


if __name__ == '__main__':
    sys.exit(main())
