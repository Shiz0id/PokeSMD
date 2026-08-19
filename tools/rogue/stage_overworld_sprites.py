"""Stage third-party overworld NPC sprites into the build, idempotently.

The front-pic tool's sibling, and a much bigger job: a front pic touches three
files, an overworld sprite touches EIGHT, in five different syntaxes.

  graphics/object_events/pics/people/<slug>.png     the art
  graphics/object_events/palettes/<slug>.pal        its colours, JASC
  object_event_graphics.h        gObjectEventPic_X, gObjectEventPalette_X
  object_event_pic_tables.h      sPicTable_X
  object_event_graphics_info.h   gObjectEventGraphicsInfo_X
  ..._graphics_info_pointers.h   the row indexed by OBJ_EVENT_GFX_X
  constants/event_objects.h      OBJ_EVENT_GFX_X and OBJ_EVENT_PAL_TAG_X
  src/event_object_movement.c    the sObjectEventSpritePalettes row

THE PALETTE TABLE IS THE ONE THAT GETS MISSED. It is the only site in a .c
rather than a data header, and check_ow_palette_tags.py exists because it was
already missed once: the ~150 FRLG sprites were unguarded across four data
files and the palette table was the fifth, so every one of them drew through
whatever palette its OAM slot last held. Nothing failed to build. Run that
check after this tool - it is the one that proves this worked.

ORDERING TRAP, from docs/KANTO_LEADERS.md: sPicTable_* is `static`, so it must
stay in the same translation unit as the graphics info that names it. A graphics
info points at a pic table, which points at a pic. This tool appends to all
three in their existing files, which keeps that true.

WHAT IT VALIDATES, all silent failures:

  - Frame width divides the sheet exactly. A sheet that is not a whole number
    of frames does not fail to build; the last frame is a slice of nothing.
  - INDEX 0 IS THE TRANSPARENT SLOT. The GBA treats palette entry 0 as
    transparent whatever its RGB. PurrfectDoodle's Gardenia keeps her green at
    index 8, which is a perfectly valid PNG and renders her in a solid box.
    --reindex fixes that losslessly by swapping the two palette entries and
    remapping the pixels; without it the tool refuses.
  - At most 16 colours, because overworld sprites are 4bpp like everything else.

FRAME COUNT PICKS THE TABLE. Nine or more frames get
overworld_ascending_frames, the Wally pattern, which is a real walk cycle in
three directions. Exactly three get the explicit Brock-style table that repeats
the idle poses - vanilla does this for NPCs that were never given a walk cycle,
and it is not a fallback, it is what those three-frame sheets are.

Run:  python3 tools/rogue/stage_overworld_sprites.py [repo] --manifest <file>
      python3 tools/rogue/stage_overworld_sprites.py --selftest
"""
import argparse
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Pillow is required: pip install Pillow")
    sys.exit(1)

MAX_COLOURS = 16
TILE = 8


def camel(slug):
    return "".join(p.capitalize() for p in slug.split("_"))


def load_and_fix(src, tiles_w, tiles_h, reindex):
    """Open a sheet, validate it, and return (image, frames, problems).

    Reindexing is done here rather than by hand because doing it by hand means
    editing a PNG in a paint program and hoping the indices survived, which is
    exactly the sort of step that is done once and never reproducibly again.
    """
    im = Image.open(src)
    problems = []
    if im.mode != "P":
        return None, 0, [f"mode {im.mode}, not indexed (P)"]

    fw, fh = tiles_w * TILE, tiles_h * TILE
    if im.height != fh:
        problems.append(f"height {im.height}, not {fh} for {tiles_h} tiles")
    if im.width % fw:
        problems.append(f"width {im.width} is not a whole number of {fw}px frames")
    frames = im.width // fw if fw else 0

    colours = im.getcolors(65536) or []
    if len(colours) > MAX_COLOURS:
        problems.append(f"{len(colours)} colours, over the {MAX_COLOURS} 4bpp allows")

    corner = im.getpixel((0, 0))
    if corner != 0:
        if not reindex:
            problems.append(f"corner pixel is index {corner}, not 0 - index 0 must "
                            f"be the transparent slot or the sprite draws in a "
                            f"solid box; pass reindex=1 in the manifest to swap "
                            f"entries 0 and {corner}")
        else:
            # SWAP, not rotate. Swapping 0 and N is its own inverse and moves
            # exactly two entries, so every other index keeps its meaning and
            # the pixels are remapped losslessly.
            pal = im.getpalette()
            a, b = 0, corner
            for k in range(3):
                pal[a * 3 + k], pal[b * 3 + k] = pal[b * 3 + k], pal[a * 3 + k]
            px = im.load()
            for y in range(im.height):
                for x in range(im.width):
                    v = px[x, y]
                    if v == a:
                        px[x, y] = b
                    elif v == b:
                        px[x, y] = a
            im.putpalette(pal)

    return im, frames, problems


def write_pal(im, dest):
    """JASC-PAL, padded to 16 entries. The converter wants exactly 16."""
    pal = im.getpalette() or []
    rows = []
    for i in range(MAX_COLOURS):
        r, g, b = pal[i * 3:i * 3 + 3] if i * 3 + 3 <= len(pal) else (0, 0, 0)
        rows.append(f"{r} {g} {b}")
    dest.write_text("JASC-PAL\n0100\n16\n" + "\n".join(rows) + "\n", newline="\n")


def insert_before(text, anchor, addition):
    if anchor not in text:
        return None
    return text.replace(anchor, addition + anchor, 1)


def stage_one(repo, entry, dry_run):
    slug = entry["slug"]
    sym = camel(slug)
    up = slug.upper()
    did = []

    im, frames, problems = load_and_fix(entry["src"], entry["tw"], entry["th"],
                                        entry["reindex"])
    if problems:
        return did, "; ".join(problems)

    # people/rogue/, matching the divers - this project's own art stays in one
    # place rather than scattered through vanilla's ~400 people sprites. The
    # declaration above names this same path and the two must not drift.
    png = repo / "graphics/object_events/pics/people/rogue" / f"{slug}.png"
    pal = repo / "graphics/object_events/palettes" / f"{slug}.pal"
    existed = png.exists()
    if not dry_run:
        png.parent.mkdir(parents=True, exist_ok=True)
        im.save(png)
        write_pal(im, pal)
    if not existed:
        did.append("png")

    # 1. the two INCBIN declarations.
    g_path = repo / "src/data/object_events/object_event_graphics.h"
    g = g_path.read_text(errors="replace")
    if f"gObjectEventPic_{sym}[]" not in g:
        # COPIED FROM THE SEAFLOOR DIVERS, not invented. They are this project's
        # own precedent for an overworld sprite with a palette of its own, and
        # every detail here was wrong on the first attempt: the pic is INCGFX
        # from the PNG with explicit tile dimensions rather than INCBIN of a
        # pre-converted .4bpp, and the palette symbol is gObjectEventPal_, not
        # gObjectEventPalette_, which is a different and also-real prefix used
        # by the light and emote palettes.
        g += (f'const u32 gObjectEventPic_{sym}[] = '
              f'INCGFX_U32("graphics/object_events/pics/people/rogue/{slug}.png", '
              f'".4bpp", "-mwidth {entry["tw"]} -mheight {entry["th"]}");\n'
              f'const u16 gObjectEventPal_{sym}[] = '
              f'INCGFX_U16("graphics/object_events/palettes/{slug}.pal", ".gbapal");\n')
        if not dry_run:
            g_path.write_text(g, newline="\n")
        did.append("gfx")

    # 2. the pic table. Frame count picks the form; see the module docstring.
    t_path = repo / "src/data/object_events/object_event_pic_tables.h"
    t = t_path.read_text(errors="replace")
    if f"sPicTable_{sym}[]" not in t:
        if frames >= 9:
            body = f"    overworld_ascending_frames(gObjectEventPic_{sym}, {entry['tw']}, {entry['th']}),\n"
        else:
            order = [0, 1, 2, 0, 0, 1, 1, 2, 2][:9]
            body = "".join(f"    overworld_frame(gObjectEventPic_{sym}, "
                           f"{entry['tw']}, {entry['th']}, {i}),\n" for i in order)
        t += (f"static const struct SpriteFrameImage sPicTable_{sym}[] = {{\n"
              f"{body}}};\n\n")
        if not dry_run:
            t_path.write_text(t, newline="\n")
        did.append("pictable")

    # 3. the graphics info.
    i_path = repo / "src/data/object_events/object_event_graphics_info.h"
    info = i_path.read_text(errors="replace")
    if f"gObjectEventGraphicsInfo_{sym} =" not in info:
        w, h = entry["tw"] * TILE, entry["th"] * TILE
        info += (f"const struct ObjectEventGraphicsInfo gObjectEventGraphicsInfo_{sym} = {{\n"
                 f"    .tileTag = TAG_NONE,\n"
                 f"    .paletteTag = OBJ_EVENT_PAL_TAG_{up},\n"
                 f"    .reflectionPaletteTag = OBJ_EVENT_PAL_TAG_NONE,\n"
                 f"    .size = {entry['tw'] * entry['th'] * 32},\n"
                 f"    .width = {w},\n"
                 f"    .height = {h},\n"
                 f"    .paletteSlot = PALSLOT_NPC_1,\n"
                 f"    .shadowSize = SHADOW_SIZE_M,\n"
                 f"    .inanimate = FALSE,\n"
                 f"    .compressed = FALSE,\n"
                 f"    .tracks = TRACKS_FOOT,\n"
                 f"    .oam = &gObjectEventBaseOam_{w}x{h},\n"
                 f"    .subspriteTables = sOamTables_{w}x{h},\n"
                 f"    .anims = sAnimTable_Standard,\n"
                 f"    .images = sPicTable_{sym},\n"
                 f"}};\n\n")
        if not dry_run:
            i_path.write_text(info, newline="\n")
        did.append("info")

    # 4. the pointer row.
    p_path = repo / "src/data/object_events/object_event_graphics_info_pointers.h"
    p = p_path.read_text(errors="replace")
    # THE EXTERN IS A SITE OF ITS OWN, and missing it is a compile error rather
    # than a silent one - the only failure in this tool that announces itself.
    # object_event_graphics_info_pointers.h is included at
    # event_object_movement.c:490 and object_event_graphics_info.h at :496, so
    # the table references definitions that do not exist yet and the file
    # carries its own forward declarations to bridge that.
    ext = f"extern const struct ObjectEventGraphicsInfo gObjectEventGraphicsInfo_{sym};\n"
    if ext not in p:
        last = None
        for m in re.finditer(r"extern const struct ObjectEventGraphicsInfo \w+;\n", p):
            last = m
        if not last:
            return did, "no extern block found in the graphics info pointers file"
        p = p[:last.end()] + ext + p[last.end():]
        did.append("extern")

    if f"[OBJ_EVENT_GFX_{up}]" not in p:
        row = f"    [OBJ_EVENT_GFX_{up}] = &gObjectEventGraphicsInfo_{sym},\n"
        # THE FIRST "};" AFTER THE TABLE'S OWN DECLARATION, not the last one in
        # the file. There are TWO tables here - gObjectEventGraphicsInfoPointers
        # and gMauvilleOldManGraphicsInfoPointers below it - so anchoring on the
        # last brace put twenty-two rows into the old man's table, which is a
        # 5-entry array indexed by a completely different enum. It compiled far
        # enough to fail on the externs instead, which is luck, not detection.
        # Matched WITHOUT the subscript: the real declaration is
        # gObjectEventGraphicsInfoPointers[NUM_OBJ_EVENT_GFX], not [], and
        # searching for the empty brackets found nothing at all.
        start = p.find("gObjectEventGraphicsInfoPointers[")
        if start < 0:
            return did, "gObjectEventGraphicsInfoPointers table not found"
        end = p.find("\n};", start)
        if end < 0:
            return did, "could not find the end of the graphics info pointer table"
        at = end + 1
        p = p[:at] + row + p[at:]
        did.append("pointer")

    # ONE WRITE FOR BOTH EDITS. The first version wrote the file inside the
    # pointer-row branch, so on a re-run where the row already existed the
    # extern was computed and thrown away - and the tool reported it as done.
    if not dry_run and ("extern" in did or "pointer" in did):
        p_path.write_text(p, newline="\n")

    # 5. the two constants.
    c_path = repo / "include/constants/event_objects.h"
    c = c_path.read_text(errors="replace")
    changed = False
    # NUM_OBJ_EVENT_GFX, not OBJ_EVENT_GFX_COUNT. The count sentinel here does
    # not follow the _COUNT convention the rest of the tree uses, and assuming
    # it did is a guess that would have appended nothing and reported success.
    if f"OBJ_EVENT_GFX_{up}," not in c:
        r = insert_before(c, "    NUM_OBJ_EVENT_GFX,", f"    OBJ_EVENT_GFX_{up},\n")
        if r is None:
            return did, "NUM_OBJ_EVENT_GFX not found"
        c, changed = r, True
        did.append("gfxid")
    if not re.search(rf"#define OBJ_EVENT_PAL_TAG_{up}\s", c):
        # The next id after the highest real tag. OBJ_EVENT_PAL_TAG_NONE is
        # 0x11FF, a sentinel rather than an allocation, so it is excluded from
        # the max - taking it would hand out 0x1200 and leave a 147-tag hole.
        tags = [int(x, 16) for x in
                re.findall(r"#define OBJ_EVENT_PAL_TAG_\w+\s+0x([0-9A-Fa-f]+)", c)]
        real = [t for t in tags if t < 0x11F0]
        if not real:
            return did, "no OBJ_EVENT_PAL_TAG_* allocations found to count from"
        nxt = max(real) + 1
        anchor = re.search(r"#define OBJ_EVENT_PAL_TAG_NONE\s+0x[0-9A-Fa-f]+\n", c)
        if not anchor:
            return did, "OBJ_EVENT_PAL_TAG_NONE not found"
        line = f"#define OBJ_EVENT_PAL_TAG_{up:<38} 0x{nxt:04X}\n"
        c = c[:anchor.start()] + line + c[anchor.start():]
        changed = True
        did.append(f"paltag 0x{nxt:04X}")
    if changed and not dry_run:
        c_path.write_text(c, newline="\n")

    # 6. THE PALETTE TABLE. The site that gets missed; see the docstring.
    m_path = repo / "src/event_object_movement.c"
    mv = m_path.read_text(errors="replace")
    if f"OBJ_EVENT_PAL_TAG_{up}}}" not in mv.replace(" ", ""):
        row = f"    {{gObjectEventPal_{sym}, OBJ_EVENT_PAL_TAG_{up}}},\n"
        mm = re.search(r"(sObjectEventSpritePalettes\[\]\s*=\s*\{)", mv)
        if not mm:
            return did, "sObjectEventSpritePalettes not found"

        # APPEND, NEVER PREPEND, AND THIS SHIPPED THE OTHER WAY.
        #
        # This used to insert straight after the opening brace, which pushed
        # OBJ_EVENT_PAL_TAG_NPC_1 through _4 off indices 0-3. SetBerryTreeGraphicsById
        # is the one consumer that indexes this table by POSITION instead of by
        # tag -- sObjectEventSpritePalettes[slot - 2], slots 2-5 straight out of
        # gBerryTreePaletteSlotTable_* -- so staging 27 sprites repainted every
        # berry tree in the game with a Sinnoh rival's palette. It built clean and
        # check_ow_palette_tags.py passed, because every TAG still resolved; what
        # moved was an index no tag participates in.
        #
        # The row goes above the OBJ_EVENT_PAL_TAG_NONE sentinel, which
        # FindObjectEventPaletteIndexByTag scans to and must stay last.
        end = re.search(r"\n#ifdef BUGFIX\b", mv[mm.end():])
        if not end:
            end = re.search(r"\n\s*\{\s*NULL\s*,\s*OBJ_EVENT_PAL_TAG_NONE", mv[mm.end():])
        if not end:
            return did, ("the sentinel row at the end of sObjectEventSpritePalettes "
                         "was not found; refusing to guess where to append")
        at = mm.end() + end.start() + 1
        mv = mv[:at] + row + mv[at:]
        if not dry_run:
            m_path.write_text(mv, newline="\n")
        did.append("paltable")

    return did, None


def read_manifest(path):
    """slug  src  tilesW  tilesH  [reindex]  - whitespace separated, # comments."""
    out = []
    for n, line in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = line.split("#")[0].strip()
        if not line:
            continue
        parts = [p for p in line.split("\t") if p.strip()]
        if len(parts) < 4:
            print(f"  manifest line {n}: expected slug<TAB>src<TAB>tilesW<TAB>tilesH[<TAB>reindex]")
            return None
        out.append({"slug": parts[0].strip(), "src": parts[1].strip(),
                    "tw": int(parts[2]), "th": int(parts[3]),
                    "reindex": len(parts) > 4 and parts[4].strip() == "1"})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo", nargs="?", default=None)
    ap.add_argument("--repo", dest="repo_kw", default=None)
    ap.add_argument("--manifest")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    repo = Path(args.repo_kw or args.repo or Path(__file__).resolve().parents[2])

    if args.selftest:
        return selftest()
    if not args.manifest:
        print("--manifest is required (or --selftest)")
        return 2

    entries = read_manifest(args.manifest)
    if entries is None:
        return 1

    # Validate every source before writing anything, for the reason the front
    # pic tool does: one bad sheet wired into six files breaks the whole batch.
    bad = False
    for e in entries:
        if not Path(e["src"]).exists():
            print(f"  MISSING  {e['slug']}: {e['src']}")
            bad = True
            continue
        _, frames, problems = load_and_fix(e["src"], e["tw"], e["th"], e["reindex"])
        if problems:
            print(f"  INVALID  {e['slug']}: " + "; ".join(problems))
            bad = True
        else:
            e["frames"] = frames
    if bad:
        print("\nnothing written - fix the sources above and re-run")
        return 1

    wrote = 0
    for e in entries:
        did, err = stage_one(repo, e, args.dry_run)
        if err:
            print(f"  ERROR    {e['slug']}: {err}")
            return 1
        print(f"  {'ok      ' if did else 'already '} {e['slug']:<26} "
              f"{e['frames']} frames"
              + (f"  [{', '.join(did)}]" if did else ""))
        wrote += bool(did)

    print(f"\n{len(entries)} sprites, {wrote} changed"
          + (" (dry run, nothing written)" if args.dry_run else ""))
    print("NOW RUN check_ow_palette_tags.py - it is what proves the palette "
          "table row landed, and that table is the site that gets missed.")
    return 0


def selftest():
    import tempfile
    results = []

    def make(w, h, ncol, corner):
        im = Image.new("P", (w, h))
        pal = []
        for i in range(ncol):
            pal += [(i * 17) % 256, (i * 31) % 256, (i * 47) % 256]
        pal += [0, 0, 0] * (256 - ncol)
        im.putpalette(pal)
        for y in range(h):
            for x in range(w):
                im.putpixel((x, y), (x + y) % ncol)
        im.putpixel((0, 0), corner)
        return im

    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        f = td / "t.png"

        make(140, 32, 16, 0).save(f)
        _, _, p = load_and_fix(f, 2, 4, False)
        results.append(("width not a whole number of frames",
                        any("whole number" in x for x in p)))

        make(144, 24, 16, 0).save(f)
        _, _, p = load_and_fix(f, 2, 4, False)
        results.append(("wrong height for the tile count",
                        any("height" in x for x in p)))

        make(144, 32, 32, 0).save(f)
        _, _, p = load_and_fix(f, 2, 4, False)
        results.append(("too many colours", any("colours" in x for x in p)))

        make(144, 32, 16, 7).save(f)
        _, _, p = load_and_fix(f, 2, 4, False)
        results.append(("index 0 not transparent, reindex off",
                        any("index 0" in x for x in p)))

        # And the repair itself: with reindex on it must be accepted AND the
        # pixel that was transparent must now be index 0. Checking only that it
        # was accepted would pass a no-op.
        make(144, 32, 16, 7).save(f)
        im, _, p = load_and_fix(f, 2, 4, True)
        results.append(("reindex repairs it", not p and im.getpixel((0, 0)) == 0))

        make(144, 32, 16, 0).save(f)
        im, frames, p = load_and_fix(f, 2, 4, False)
        results.append(("a valid 9-frame sheet passes", not p and frames == 9))

    bad = 0
    for name, ok in results:
        print(f"  {'ok     ' if ok else '*** FAILED ***'}  {name}")
        bad += not ok
    print(f"\n{len(results) - bad}/{len(results)} selftest cases behave")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
