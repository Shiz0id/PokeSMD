#!/usr/bin/env bash
# Run every check_*.py, passing the repo the way each one actually wants it.
#
# FIVE of them take the repo POSITIONALLY and the rest take --repo. Passing
# --repo uniformly reports those five as failures that are not real, which has
# already wasted time once. Keep this list in step with the checks -- the count
# in this comment was itself stale by one for a while.
POSITIONAL="check_safari_pool.py check_species_in_rom.py check_craft_recipes.py check_variant_colours.py check_pool_evolutions.py"

cd "$(dirname "$0")/../.." || exit 2
repo="$PWD"
fail=0

for f in tools/rogue/check_*.py; do
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
