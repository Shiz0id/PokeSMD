"""Find the built ROM and its link map WITHOUT hardcoding their names.

Six tools in this directory used to open "pokeemerald.map" by name, which meant
renaming the ROM broke a check in the suite and five generators - none of which
have anything to do with what the ROM is called. The name is a Makefile setting
(FILE_NAME) and it has already changed once, so the tools should not know it.

Globbing is deliberate over parsing the Makefile: FILE_NAME picks up a -release
suffix conditionally, and a tool that reimplemented that logic would be a second
copy of it that drifts.
"""
from pathlib import Path


def _pick(paths):
    """Prefer a plain build over the test/debug/release ones."""
    paths = sorted(paths)
    if not paths:
        return None
    plain = [p for p in paths
             if not any(t in p.stem for t in ('-test', '-debug', '-release'))]
    return (plain or paths)[0]


def find_map(repo):
    """The link map, or None if nothing has been linked yet."""
    return _pick(Path(repo).glob('*.map'))


def find_rom(repo):
    """The built .gba, or None."""
    return _pick(Path(repo).glob('*.gba'))
