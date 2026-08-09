#!/usr/bin/env python3
"""Assert every SPECIES_* named in a source file is actually in the ROM.

This is the check gap 20 asks for. The generation config disables whole
families, and a disabled family still COMPILES -- it becomes a zeroed
species_info row that a wild encounter or a prize table happily rolls into. So
species_info proves nothing and neither does the build. The link map does.

Reads pokeemerald.map and requires a gMonFrontPic_* symbol per species, so it
only runs AFTER a link. The symbol is CamelCase with underscores stripped:

    SPECIES_MR_MIME  -> gMonFrontPic_MrMime

A species with alternate forms is an alias for its base form, and the SYMBOL
carries a suffix the constant does not:

    SPECIES_CASTFORM = SPECIES_CASTFORM_NORMAL  -> gMonFrontPic_CastformNormal
    SPECIES_SHELLOS  = SPECIES_SHELLOS_WEST     -> gMonFrontPic_ShellosWestSea

so aliases are followed through species.h before giving up. A bare-name grep
misses these, which is a false negative that has already happened once while
fixing the divers.
"""
import re
import sys
from pathlib import Path

SKIP = {"SPECIES_NONE", "SPECIES_EGG"}

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)


def strip_comments(text):
    """A species named in a comment is not a species in a table.

    rogue_dungeon.c has a note reading "SPECIES_BASCULIN does not exist under
    that name either" -- left there precisely so nobody reaches for it again.
    Scanning raw text reports that sentence as a missing species, which is the
    kind of false positive that gets a check switched off.
    """
    return COMMENT.sub(" ", text)


def camel(name):
    return "".join(p.capitalize() for p in name.split("_") if p)


def alias_map(repo):
    """SPECIES_X -> SPECIES_Y for every `SPECIES_X = SPECIES_Y,` in species.h."""
    text = (repo / "include" / "constants" / "species.h").read_text(errors="ignore")
    return dict(re.findall(r"\b(SPECIES_[A-Z0-9_]+)\s*=\s*(SPECIES_[A-Z0-9_]+)\s*,", text))


def resolve(const, symbols, aliases):
    """The gMonFrontPic_ stem for a species, or None."""
    seen = set()
    while const and const not in seen:
        seen.add(const)
        stem = camel(const[len("SPECIES_"):])
        if stem in symbols:
            return stem
        # a form's symbol may extend the constant (SHELLOS_WEST -> ShellosWestSea)
        extended = sorted(s for s in symbols if s.startswith(stem))
        if extended:
            return extended[0]
        const = aliases.get(const)
    return None


# Everywhere in this build that names a species. Gap 20 asks for exactly this
# list: the generated trainer parties (which is where the divers carried eleven
# blanks for as long as they did), the roguelike's theme pools, Safari ladder
# and starter table, and the vendored game corner's prize tables.
DEFAULT_TARGETS = [
    "src/data/trainers.h",
    "src/rogue_dungeon.c",
    "include/constants/rogue_dungeon_starters.h",
    "include/constants/rogue_safari_pool.h",
    "include/constants/rogue_dungeon_trainers.h",
    "src/game_corner_gacha.c",
]


def main(argv):
    if len(argv) < 2:
        print("usage: check_species_in_rom.py <repo> [file ...]")
        return 2

    repo = Path(argv[1])
    mapfile = repo / "pokeemerald.map"
    if not mapfile.exists():
        print(f"no {mapfile} -- this is a POST-BUILD check, link first")
        return 2

    symbols = set(re.findall(r"\bgMonFrontPic_(\w+)", mapfile.read_text(errors="ignore")))
    aliases = alias_map(repo)
    if not symbols:
        print("no gMonFrontPic_ symbols in the map at all -- check the map path")
        return 2

    targets = argv[2:] or [str(repo / t) for t in DEFAULT_TARGETS]

    failed = 0
    for source in targets:
        if not Path(source).exists():
            print(f"FAIL  {source}: does not exist")
            failed += 1
            continue
        code = strip_comments(Path(source).read_text(errors="ignore"))
        named = sorted(set(re.findall(r"\bSPECIES_[A-Z0-9_]+", code)) - SKIP)
        missing = [c for c in named if resolve(c, symbols, aliases) is None]
        for const in missing:
            print(f"MISSING  {const}: no gMonFrontPic_{camel(const[8:])} in the link map")
        status = "FAIL" if missing else "ok"
        print(f"{status:4}  {Path(source).name}: "
              f"{len(named) - len(missing)}/{len(named)} species present")
        failed += len(missing)

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
