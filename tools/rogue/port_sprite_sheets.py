#!/usr/bin/env python3
"""Rewrite the decompress-into-gDecompressionBuffer idiom to LoadCompressedSpriteSheet.

The expansion deleted gDecompressionBuffer. Every call site here is the same
shape: decompress a struct CompressedSpriteSheet into scratch, then hand a
struct SpriteSheet to LoadSpriteSheet. LoadCompressedSpriteSheet does exactly
that -- but it takes size and tag from the struct, so this refuses to rewrite a
site whose hand-written size/tag do not match what the struct declares.
"""
import re
import sys
from pathlib import Path

SITE = re.compile(
    r"[ \t]*LZ77UnCompWram\(\s*(?P<sheet>[A-Za-z_][\w\[\]\.]*?)\.data\s*,\s*gDecompressionBuffer\s*\);\s*\n"
    r"[ \t]*(?P<var>\w+)\.data\s*=\s*gDecompressionBuffer;\s*\n"
    r"[ \t]*(?P=var)\.size\s*=\s*(?P<size>[^;]+);\s*\n"
    r"[ \t]*(?P=var)\.tag\s*=\s*(?P<tag>[^;]+);\s*\n"
    r"(?P<indent>[ \t]*)LoadSpriteSheet\(&(?P=var)\);"
)

# .tag = X inside a CompressedSpriteSheet initialiser, keyed by the object name
DECL = re.compile(
    r"struct\s+CompressedSpriteSheet\s+(?P<name>\w+)(?P<arr>\s*\[\s*\])?\s*=\s*(?P<body>\{.*?\n\};)",
    re.S,
)


def declared_tags(src):
    """name -> [tag, ...] in declaration order (index order for arrays)."""
    out = {}
    for m in DECL.finditer(src):
        tags = re.findall(r"\.tag\s*=\s*([^,\n}]+)", m.group("body"))
        out[m.group("name")] = [t.strip() for t in tags]
    return out


def resolve(sheet_expr, tags):
    """Declared tag for a sheet expression, or None if it cannot be resolved."""
    m = re.fullmatch(r"(\w+)\[(\w+)\]", sheet_expr)
    if m:
        name, idx = m.groups()
        if name not in tags:
            return None
        if idx.isdigit() and int(idx) < len(tags[name]):
            return tags[name][int(idx)]
        # loop variable: every entry must agree, or we cannot prove it
        uniq = set(t for t in tags[name] if t)
        return uniq.pop() if len(uniq) == 1 else None
    return tags.get(sheet_expr, [None])[0]


def main(path):
    src = Path(path).read_text()
    tags = declared_tags(src)
    sites, refused = [], []

    def repl(m):
        sheet, size, tag = m["sheet"], m["size"].strip(), m["tag"].strip()
        want_size = f"{sheet}.size"
        decl_tag = resolve(sheet, tags)
        ok_size = size == want_size
        ok_tag = tag == f"{sheet}.tag" or (decl_tag is not None and tag == decl_tag)
        if not (ok_size and ok_tag):
            refused.append((sheet, size, tag, decl_tag, ok_size, ok_tag))
            return m.group(0)
        sites.append((sheet, tag))
        return f"{m['indent']}LoadCompressedSpriteSheet(&{sheet});"

    out = SITE.sub(repl, src)

    for sheet, size, tag, decl_tag, ok_size, ok_tag in refused:
        print(f"REFUSED {sheet}: size={size} (ok={ok_size}) "
              f"tag={tag} declared={decl_tag} (ok={ok_tag})")
    for sheet, tag in sites:
        print(f"rewrote {sheet}  tag={tag}")
    print(f"\n{len(sites)} rewritten, {len(refused)} refused")

    if refused:
        print("refusing to write; resolve the mismatches by hand")
        return 1
    if "--write" in sys.argv:
        Path(path).write_text(out, newline="\n")
        print("written")
    else:
        print("dry run; pass --write to apply")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
