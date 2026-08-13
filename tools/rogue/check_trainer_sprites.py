"""Every trainer the dungeon can generate should look like what it is.

Generated trainers used to take the theme's one or two sprites by slot parity,
so a floor showed at most two figures and a theme setting neither showed a hiker
for every battle in the run. sTrainerClassGfx now derives the overworld sprite
from the trainer's CLASS, which also makes the figure agree with the battle pic.

This measures whether that actually reaches the trainers the generator picks -
the table can look full and still miss the classes the pool is made of, which is
the only thing that matters. It reports coverage per class weighted by how many
trainers carry it, so a gap on a class with 60 trainers is not hidden by
twenty covered classes with two each.

Fails when coverage falls below THRESHOLD, so deleting a busy row is caught.

Run:  python3 tools/rogue/check_trainer_sprites.py [--repo PATH] [--selftest]
"""
import argparse
import collections
import re
import sys
from pathlib import Path

# 0.95, not 0.90, and the selftest is why. Coverage is 99.2% and dropping the
# single busiest class only takes it to 92.1% - so a 90% bar passed a table with
# its largest row deleted, which is a check that cannot fail for any realistic
# regression. The bar has to sit between the two.
THRESHOLD = 0.95


def repo_root(arg):
    return Path(arg) if arg else Path(__file__).resolve().parents[2]


def pool_ids(repo):
    """The trainer ids sRogueDungeonTrainers offers the generator."""
    text = (repo / "include/constants/rogue_dungeon_trainers.h").read_text()
    return re.findall(r"\{\s*(TRAINER_\w+)\s*,", text)


def trainer_classes(repo):
    """-> {TRAINER_X: class name} from the .party source."""
    text = (repo / "src/data/trainers.party").read_text(encoding="utf-8")
    out, current = {}, None
    for line in text.splitlines():
        m = re.match(r"^=== (TRAINER_\w+) ===$", line)
        if m:
            current = m.group(1)
        elif current and line.startswith("Class:"):
            out[current] = line.split(":", 1)[1].strip()
            current = None
    return out


def covered_classes(repo, drop=None):
    """-> the set of TRAINER_CLASS_* names sTrainerClassGfx gives a sprite."""
    text = (repo / "src/rogue_dungeon.c").read_text(encoding="utf-8")
    body = text.split("sTrainerClassGfx[TRAINER_CLASS_COUNT] =", 1)[1]
    body = body.split("\n};", 1)[0]
    found = set(re.findall(r"\[(TRAINER_CLASS_\w+)\]", body))
    if drop:
        found.discard(drop)
    return found


def class_constant(name):
    """"Bug Catcher" -> TRAINER_CLASS_BUG_CATCHER."""
    return "TRAINER_CLASS_" + re.sub(r"[^A-Z0-9]+", "_", name.upper()).strip("_")


def measure(repo, drop=None):
    classes = trainer_classes(repo)
    covered = covered_classes(repo, drop)
    counts = collections.Counter()
    missing = collections.Counter()
    unknown = 0
    for tid in pool_ids(repo):
        name = classes.get(tid)
        if name is None:
            unknown += 1
            continue
        const = class_constant(name)
        counts[const] += 1
        if const not in covered:
            missing[const] += 1
    total = sum(counts.values())
    lost = sum(missing.values())
    return total, lost, missing, unknown


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)
    repo = repo_root(args.repo)

    total, lost, missing, unknown = measure(repo)
    covered = total - lost
    ratio = covered / total if total else 0
    print(f"{total} generated-pool trainers, {covered} with a class sprite "
          f"({ratio * 100:.1f}%)")
    if unknown:
        print(f"  {unknown} pool ids had no Class line in trainers.party")
    if missing:
        print("  classes with no sprite, by how many trainers carry them:")
        for const, n in missing.most_common(12):
            print(f"    {const:<34} {n}")

    if args.selftest:
        # Break it on purpose: drop the busiest covered class and confirm the
        # threshold catches it. A check that has never failed is worth nothing.
        classes = trainer_classes(repo)
        counts = collections.Counter(
            class_constant(classes[t]) for t in pool_ids(repo) if t in classes)
        for const, _ in counts.most_common():
            if const in covered_classes(repo):
                busiest = const
                break
        t2, lost2, _, _ = measure(repo, drop=busiest)
        ratio2 = (t2 - lost2) / t2
        print(f"  selftest: dropping {busiest} -> {ratio2 * 100:.1f}%")
        if ratio2 >= THRESHOLD:
            raise SystemExit("SELFTEST FAILED - dropping the busiest class did "
                             "not breach the threshold, so the check is vacuous")
        print("  selftest OK - the threshold fires when a busy class is lost")

    if ratio < THRESHOLD:
        raise SystemExit(f"FAIL: only {ratio * 100:.1f}% of the generated pool "
                         f"has a class sprite, below {THRESHOLD * 100:.0f}%")
    print("PASS")


if __name__ == "__main__":
    main(sys.argv[1:])
