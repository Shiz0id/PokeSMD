"""Stage third-party 64x64 trainer front pics into the build, idempotently.

Same shape and the same reason as wire_gb_song.py and import_midi_pack.py: the
wiring for one trainer pic touches three files in three different syntaxes, and
doing that by hand twenty-one times is how a tree ends up with a symbol declared
and never tabled, or an enumerator with no row behind it. Neither fails to
build. Both produce a trainer that renders as whatever was at that address.

WHAT IT WIRES, per pic:

  graphics/trainers/front_pics/<file>.png    the art, copied
  src/data/graphics/trainers.h               gTrainerFrontPic_X + gTrainerPalette_X
  include/constants/trainers.h               TRAINER_PIC_X, before TRAINER_PIC_COUNT
  src/data/graphics/trainers.h               the sTrainerSprites row

NO SEPARATE .pal FILE. The palette is pulled from the PNG itself with
INCGFX_U16(..., ".gbapal"), which is what the seafloor divers already do. The
FRLG pics use a separate graphics/trainers/palettes/*.pal and that is the older
convention; following the divers keeps one file per pic instead of two.

APPENDED, NEVER INSERTED. trainerPic is stored per trainer, so shifting an
existing TRAINER_PIC_* id silently repaints hundreds of stock trainers - the
same warning the diver block already carries in constants/trainers.h.

IT VALIDATES BEFORE IT WRITES, because every one of these fails silently:

  - 64x64. A different size does not fail to build; it draws misaligned.
  - Indexed (mode P) with at most 16 colours. 4bpp is 16 and the converter
    takes the first 16 of a longer palette, so a 17-colour source loses a
    colour somewhere in the sprite rather than erroring.
  - INDEX 0 IS THE TRANSPARENT SLOT, checked by requiring the corner pixel to
    be index 0. The GBA treats palette entry 0 as transparent whatever its RGB,
    so a sprite whose background sits at index 3 renders in a solid box - and
    it is a perfectly valid PNG, so nothing upstream complains.

Run:  python3 tools/rogue/stage_trainer_pics.py [repo] --manifest <file>
      python3 tools/rogue/stage_trainer_pics.py --selftest
"""
import argparse
import re
import shutil
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Pillow is required: pip install Pillow")
    sys.exit(1)

PIC_W = PIC_H = 64
MAX_COLOURS = 16


def camel(slug):
    """rogue_sinnoh_crasher_wake -> RogueSinnohCrasherWake"""
    return "".join(p.capitalize() for p in slug.split("_"))


def validate(png):
    """Everything about a source pic that fails silently rather than loudly."""
    im = Image.open(png)
    problems = []

    if im.size != (PIC_W, PIC_H):
        problems.append(f"{im.size[0]}x{im.size[1]}, not {PIC_W}x{PIC_H}")
    if im.mode != "P":
        problems.append(f"mode {im.mode}, not indexed (P)")
    else:
        colours = im.getcolors(65536) or []
        if len(colours) > MAX_COLOURS:
            problems.append(f"{len(colours)} colours, over the {MAX_COLOURS} 4bpp allows")
        if im.getpixel((0, 0)) != 0:
            problems.append(f"corner pixel is index {im.getpixel((0, 0))}, not 0 - "
                            f"index 0 must be the transparent slot or the sprite "
                            f"draws in a solid box")
    return problems


def stage_one(repo, src, slug, dry_run):
    """Copy one pic and wire its three files. Returns a list of actions taken."""
    sym = camel(slug)
    did = []

    dest = repo / "graphics/trainers/front_pics" / f"{slug}.png"
    if not dest.exists() or dest.read_bytes() != Path(src).read_bytes():
        if not dry_run:
            shutil.copyfile(src, dest)
        did.append("png")

    # 1. the two declarations.
    gfx_path = repo / "src/data/graphics/trainers.h"
    gfx = gfx_path.read_text(errors="replace")
    decl = (f'const u32 gTrainerFrontPic_{sym}[] = INCGFX_U32('
            f'"graphics/trainers/front_pics/{slug}.png", ".4bpp.smol");\n'
            f'const u16 gTrainerPalette_{sym}[] = INCGFX_U16('
            f'"graphics/trainers/front_pics/{slug}.png", ".gbapal");\n')
    if f"gTrainerFrontPic_{sym}[]" not in gfx:
        anchor = ('const u16 gTrainerPalette_RogueDiverF[] = INCGFX_U16('
                  '"graphics/trainers/front_pics/rogue_diver_f.png", ".gbapal");\n')
        if anchor not in gfx:
            return did, f"declaration anchor (RogueDiverF) not found"
        gfx = gfx.replace(anchor, anchor + decl, 1)
        did.append("decl")

    # 2. the sTrainerSprites row.
    row = (f"    [TRAINER_PIC_{slug.upper()}] =\n"
           f"    {{\n"
           f"        .frontPic = TRAINER_FRONT_PIC(gTrainerFrontPic_{sym}, "
           f"gTrainerPalette_{sym}),\n"
           f"    }},\n")
    if f"[TRAINER_PIC_{slug.upper()}] =" not in gfx:
        anchor = ("    [TRAINER_PIC_ROGUE_DIVER_F] =\n"
                  "    {\n"
                  "        .frontPic = TRAINER_FRONT_PIC(gTrainerFrontPic_RogueDiverF, "
                  "gTrainerPalette_RogueDiverF),\n"
                  "    },\n")
        if anchor not in gfx:
            return did, "sTrainerSprites anchor (ROGUE_DIVER_F row) not found"
        gfx = gfx.replace(anchor, anchor + row, 1)
        did.append("row")

    if not dry_run and did:
        gfx_path.write_text(gfx, newline="\n")

    # 3. the enumerator, before TRAINER_PIC_COUNT.
    cst_path = repo / "include/constants/trainers.h"
    cst = cst_path.read_text(errors="replace")
    if f"TRAINER_PIC_{slug.upper()},"  not in cst:
        if "    TRAINER_PIC_COUNT,\n" not in cst:
            return did, "TRAINER_PIC_COUNT not found"
        cst = cst.replace("    TRAINER_PIC_COUNT,\n",
                          f"    TRAINER_PIC_{slug.upper()},\n    TRAINER_PIC_COUNT,\n", 1)
        if not dry_run:
            cst_path.write_text(cst, newline="\n")
        did.append("enum")

    return did, None


def read_manifest(path):
    """slug<TAB or whitespace>source path, # comments, blank lines skipped."""
    out = []
    for n, line in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = line.split("#")[0].strip()
        if not line:
            continue
        parts = line.split("\t") if "\t" in line else line.rsplit(" ", 1)
        if len(parts) != 2:
            print(f"  manifest line {n}: expected '<slug>\\t<source png>'")
            return None
        out.append((parts[0].strip(), parts[1].strip()))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo", nargs="?", default=None)
    ap.add_argument("--repo", dest="repo_kw", default=None)
    ap.add_argument("--manifest", required=False)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    # Takes the repo positionally OR as --repo, following
    # check_start_menu_pages.py, so it can never end up on the wrong side of a
    # runner's hand-maintained list of which form each tool wants.
    repo = Path(args.repo_kw or args.repo or Path(__file__).resolve().parents[2])

    if args.selftest:
        return selftest()

    if not args.manifest:
        print("--manifest is required (or use --selftest)")
        return 2

    entries = read_manifest(args.manifest)
    if entries is None:
        return 1

    # VALIDATE EVERY SOURCE BEFORE WRITING ANYTHING. One bad pic wired into
    # the tables breaks the build for the whole batch, which is the same reason
    # import_midi_pack.py test-converts the entire directory up front.
    bad = False
    for slug, src in entries:
        if not Path(src).exists():
            print(f"  MISSING  {slug}: {src}")
            bad = True
            continue
        problems = validate(src)
        if problems:
            print(f"  INVALID  {slug}: " + "; ".join(problems))
            bad = True
    if bad:
        print("\nnothing written - fix the sources above and re-run")
        return 1

    wrote = 0
    for slug, src in entries:
        did, err = stage_one(repo, src, slug, args.dry_run)
        if err:
            print(f"  ERROR    {slug}: {err}")
            return 1
        print(f"  {'ok      ' if did else 'already '} {slug}"
              + (f"  [{', '.join(did)}]" if did else ""))
        wrote += bool(did)

    print(f"\n{len(entries)} pics, {wrote} changed"
          + (" (dry run, nothing written)" if args.dry_run else ""))
    return 0


def selftest():
    """Break each validation rule on purpose and require it to be caught."""
    import tempfile
    ok = 0
    cases = []

    def make(size, mode, ncol, corner):
        im = Image.new("P", size)
        pal = []
        for i in range(ncol):
            pal += [i * 7 % 256, i * 13 % 256, i * 29 % 256]
        pal += [0, 0, 0] * (256 - ncol)
        im.putpalette(pal)
        for y in range(size[1]):
            for x in range(size[0]):
                im.putpixel((x, y), (x + y) % ncol)
        im.putpixel((0, 0), corner)
        return im

    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        cases.append(("wrong size", make((56, 64), "P", 16, 0), "64x64"))
        cases.append(("too many colours", make((64, 64), "P", 32, 0), "colours"))
        cases.append(("index 0 not transparent", make((64, 64), "P", 16, 5), "index 0"))

        for name, im, expect in cases:
            f = td / "t.png"
            im.save(f)
            problems = validate(f)
            fired = any(expect in p for p in problems)
            print(f"  {'fires ' if fired else '*** SILENT ***'}  {name}")
            ok += fired

        # A valid pic must produce no complaints at all, or the checks above
        # are firing on everything and prove nothing.
        f = td / "good.png"
        make((64, 64), "P", 16, 0).save(f)
        clean = not validate(f)
        print(f"  {'ok     ' if clean else '*** FALSE POSITIVE ***'}  a valid pic passes")
        ok += clean

    total = len(cases) + 1
    print(f"\n{ok}/{total} selftest cases behave")
    return 0 if ok == total else 1


if __name__ == "__main__":
    sys.exit(main())
