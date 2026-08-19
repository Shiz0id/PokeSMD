#!/usr/bin/env bash
# Run every check_*.py AND every verify_*.py, passing the repo the way each one
# actually wants it.
#
# SIX of them take the repo POSITIONALLY and the rest take --repo. Passing
# --repo uniformly reports those six as failures that are not real, which has
# already wasted time once. Keep this list in step with the checks.
POSITIONAL="check_safari_pool.py check_species_in_rom.py check_craft_recipes.py check_variant_colours.py check_pool_evolutions.py check_bw_trainer_anim.py"

# THE FOUR verify_*.py WERE NOT RUN BY THIS SCRIPT AT ALL until the dungeon order
# shuffle needed one of them, and that is exactly how a check rots: it passes on
# the day it is written and nothing ever asks it again. They resolve the repo from
# their own location and take no arguments, so the --repo branch below is a no-op
# for them rather than an error. All four run in well under a second together.
cd "$(dirname "$0")/../.." || exit 2
repo="$PWD"
fail=0

# tools/mode7/ is globbed too. A check that lives beside its subject but is not
# reached by this runner is worth nothing -- which is exactly what happened to
# the four verify_*.py above. New tool directories go on this line.
for f in tools/rogue/check_*.py tools/rogue/verify_*.py tools/mode7/check_*.py; do
    base="$(basename "$f")"
    if echo "$POSITIONAL" | grep -qw "$base"; then
        out="$(python3 "$f" "$repo" 2>&1)"
    else
        out="$(python3 "$f" --repo "$repo" 2>&1)"
    fi
    if [ $? -eq 0 ]; then
        printf 'PASS  %s\n' "$base"
    else
        printf 'FAIL  %s\n' "$base"
        printf '%s\n' "$out" | sed 's/^/        /' | head -12
        fail=1
    fi
done

exit $fail
