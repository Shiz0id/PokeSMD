#!/usr/bin/env python3
"""Hold gSpeciesVariants to the things that fail silently.

Takes the repo POSITIONALLY, like check_safari_pool.py, check_species_in_rom.py
and check_craft_recipes.py. A runner passing --repo uniformly gets four
spurious failures now, not three.

Four jobs, and every one of them exists because the failure is silent:

1. HUE/CHROMA/LUMA VALUES MUST BE FROM THE LEGAL SETS. HUE_INDEX and its two
   siblings are ternary chains with no else that means "wrong". Upstream's own
   Smeargle entry asks for a hue of 360 -- meaning, plainly, a full turn -- and
   falls through the whole chain to index 7, which is 180. A half turn where
   the author wrote no turn at all. It compiles, it runs, and the mon is the
   wrong colour. Nothing else in the build looks at this.

2. A PALETTE RANGE MUST FIT IN THE PALETTE. start and length are four bits
   each, so start+length can reach 30 against 16 colours. The C clamps, so an
   over-long range does not corrupt anything -- it just silently shifts fewer
   indices than the entry asks for, which reads as "the variant barely does
   anything on that one species".

3. A SPECIES NAMED HERE MUST BE IN THE ROM. This is the rule the pools already
   follow and that gap 20 says nothing enforces anywhere: a disabled
   generation still compiles to a zeroed species_info row, so a name from a
   switched-off family resolves, indexes the table, and does nothing visible.
   Gen 5-9 are off in this build. Checked against pokeemerald.map, because the
   link map is the only proof a species survived the toggles -- species_info
   is not, and stopping there is what left eleven diver party slots naming
   species that were not in the ROM.

4. AN ENTRY MUST ACTUALLY ASK FOR SOMETHING. An entry with a palette range and
   all three amounts zero is indistinguishable from no entry at all --
   GetSpeciesVariants treats all-zero as absent and hands back the default --
   so it is almost certainly a half-written entry rather than an intent.

Exit code 0 on pass, 1 on failure. Verify the harness as well as the check:
measuring $? across the WSL boundary has reported success for a deliberate
non-zero exit before, so use `cmd && echo pass || echo fail`.
"""

import re
import sys
from pathlib import Path

LEGAL_HUE = {0, 10, 20, 30, 45, 60, 90, 180}
LEGAL_CL = {0, 5, 10, 25}
PAL_COLOURS = 16

SRC = "src/variant_colours.c"
MAP = "pokeemerald.map"


def die(msg):
    print("check_variant_colours: %s" % msg, file=sys.stderr)
    sys.exit(2)


def read_table(repo):
    """Pull the gSpeciesVariants initialiser out of the .c.

    Deliberately reads the SOURCE rather than any generated artefact: this
    table is hand-authored, and the point of the check is to catch what a hand
    wrote before the compiler silently accepts it.
    """
    path = repo / SRC
    if not path.is_file():
        die("no %s under %s" % (SRC, repo))
    text = path.read_text(encoding="utf-8")

    m = re.search(
        r"gSpeciesVariants\s*\[\s*NUM_SPECIES\s*\]\s*=\s*\{(.*?)\n\};",
        text,
        re.S,
    )
    if m is None:
        die("could not find the gSpeciesVariants initialiser in %s" % SRC)
    return m.group(1)


def parse_entries(body):
    """Split into [SPECIES_X] = { ... } entries.

    Strips // comments first. The table ships with a long comment block that
    names macros and a species id in prose, and matching those as entries is
    exactly the sort of thing that makes a check pass vacuously.
    """
    body = re.sub(r"//[^\n]*", "", body)

    entries = []
    for m in re.finditer(r"\[\s*(SPECIES_[A-Z0-9_]+)\s*\]\s*=\s*\{(.*?)\}\s*,", body, re.S):
        entries.append((m.group(1), m.group(2)))
    return entries


def parse_calls(chunk, name):
    """Every NAME(a, b, ...) call in an entry, as lists of raw argument text."""
    out = []
    for m in re.finditer(r"\b%s\s*\(([^)]*)\)" % name, chunk):
        out.append([a.strip() for a in m.group(1).split(",")])
    return out


def as_int(tok):
    try:
        return int(tok, 0)
    except ValueError:
        return None


def species_in_rom(repo):
    """Species whose front pic symbol survived into the link map.

    Same test gen_safari_pool.py uses, and for the same reason.
    """
    path = repo / MAP
    if not path.is_file():
        die("no %s -- build first, the link map is the only proof a species "
            "survived the generation toggles" % MAP)
    text = path.read_text(encoding="utf-8", errors="replace")
    return set(re.findall(r"gMonFrontPic_([A-Za-z0-9_]+)", text))


def norm(species_const):
    """SPECIES_MR_MIME -> MRMIME-ish key for loose matching against symbols."""
    return species_const[len("SPECIES_"):].replace("_", "").upper()


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        die("usage: check_variant_colours.py <repo>")
    repo = Path(argv[1])
    if not repo.is_dir():
        die("not a directory: %s" % repo)

    body = read_table(repo)
    entries = parse_entries(body)
    failures = []

    rom = None  # loaded lazily; an empty table needs no link map

    for species, chunk in entries:
        for idx, (palname, hclname) in enumerate((("PAL1", "HCL1"), ("PAL2", "HCL2")), start=1):
            pals = parse_calls(chunk, palname)
            hcls = parse_calls(chunk, hclname)

            for args in pals:
                if len(args) != 2:
                    failures.append("%s %s takes 2 arguments, got %d" % (species, palname, len(args)))
                    continue
                start, length = as_int(args[0]), as_int(args[1])
                if start is None or length is None:
                    failures.append("%s %s has non-numeric arguments %r" % (species, palname, args))
                    continue
                if not (0 <= start < PAL_COLOURS):
                    failures.append("%s %s start %d is outside 0..%d"
                                    % (species, palname, start, PAL_COLOURS - 1))
                if length < 0 or start + length > PAL_COLOURS:
                    failures.append(
                        "%s %s(%d, %d) runs to index %d, past the %d colours a palette "
                        "has -- the C clamps, so this silently shifts fewer indices "
                        "than it asks for" % (species, palname, start, length,
                                              start + length, PAL_COLOURS))

            for args in hcls:
                if len(args) != 4:
                    failures.append("%s %s takes 4 arguments, got %d" % (species, hclname, len(args)))
                    continue
                h, c, l = as_int(args[0]), as_int(args[1]), as_int(args[2])
                if h is None or c is None or l is None:
                    failures.append("%s %s has non-numeric arguments %r" % (species, hclname, args))
                    continue
                if h not in LEGAL_HUE:
                    failures.append(
                        "%s %s hue %s is not one of %s -- HUE_INDEX has no else branch, "
                        "so this silently becomes %s degrees"
                        % (species, hclname, h, sorted(LEGAL_HUE),
                           180 if h > 90 else "something else"))
                if c not in LEGAL_CL:
                    failures.append("%s %s chroma %s is not one of %s -- CHR_INDEX "
                                    "silently rounds it" % (species, hclname, c, sorted(LEGAL_CL)))
                if l not in LEGAL_CL:
                    failures.append("%s %s luma %s is not one of %s -- LUM_INDEX "
                                    "silently rounds it" % (species, hclname, l, sorted(LEGAL_CL)))

                if h == 0 and c == 0 and l == 0 and pals:
                    failures.append(
                        "%s %s asks for no shift at all while %s names a range -- "
                        "GetSpeciesVariants treats an all-zero entry as absent, so this "
                        "entry does nothing and is almost certainly half-written"
                        % (species, hclname, palname))

            if pals and not hcls:
                failures.append("%s has %s but no %s, so the range is never shifted"
                                % (species, palname, hclname))
            if hcls and not pals:
                failures.append("%s has %s but no %s, so there is no range to shift"
                                % (species, hclname, palname))

        if rom is None:
            rom = species_in_rom(repo)
        key = norm(species)
        if not any(norm("SPECIES_" + sym) == key for sym in rom):
            failures.append(
                "%s has no gMonFrontPic symbol in %s -- a disabled generation still "
                "compiles to a zeroed species_info row, so this entry would resolve, "
                "index the table and do nothing visible" % (species, MAP))

    if failures:
        print("check_variant_colours: FAIL (%d)" % len(failures))
        for f in failures:
            print("  - %s" % f)
        return 1

    print("check_variant_colours: pass (%d species entries)" % len(entries))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
