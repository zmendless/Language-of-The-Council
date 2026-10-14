#!/usr/bin/env bash
# Builds data/pilot_urls.txt for the end-to-end prototype: two sittings (a third and two
# thirds of the way into the volume) from one volume of every term, 1920 to today.
# A volume from the middle of each term avoids the opening sittings, which are mostly
# oaths and elections. Volume numbering has gaps (term 24 has volumes 2 and 4 but no 3),
# so each term lists candidate volumes, tried in order. Takes a few minutes, because the
# server is probed politely.
set -euo pipefail
cd "$(dirname "$0")/.."

out=data/pilot_urls.txt
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

{
    echo "# Pilot sample, made by scripts/pilot.sh: two sittings from one volume per term."
    while read -r chamber term volumes; do
        found=no
        for volume in $volumes; do
            if scripts/discover.sh "$chamber" "$term" "$volume" > "$tmp" 2> /dev/null; then
                found=yes
                break
            fi
        done
        if [ $found = no ]; then
            echo "no volume found for $chamber term $term (tried $volumes)" >&2
            continue
        fi
        n=$(wc -l < "$tmp")
        sed -n "$((n / 3 + 1))p" "$tmp"
        [ "$n" -ge 2 ] && sed -n "$((2 * n / 3 + 1))p" "$tmp"
    done <<'EOF'
TBMM 1 10 20 5 4 2
TBMM 2 10 20 5 4 2
TBMM 3 10 20 5 4 2
TBMM 4 10 20 5 4 2
TBMM 5 10 20 5 4 2
TBMM 6 10 20 5 4 2
TBMM 7 10 20 5 4 2
TBMM 8 10 20 5 4 2
TBMM 9 10 20 5 4 2
TBMM 10 10 20 5 4 2
TBMM 11 10 20 5 4 2
MM 1 10 20 5 4 2
MM 2 10 20 5 4 2
MM 3 10 20 5 4 2
MM 4 10 20 5 4 2
MM 5 10 20 5 4 2
TBMM 17 10 20 5 4 2
TBMM 18 10 20 5 4 2
TBMM 19 10 20 5 4 2
TBMM 20 10 20 5 4 2
TBMM 21 10 20 5 4 2
TBMM 22 10 20 5 4 2
TBMM 23 10 20 5 4 2
TBMM 24 10 20 5 4 2
TBMM 26 50 20 5 4 2
TBMM 27 10 20 5 4 2
TBMM 28 10 20 5 4 2
EOF
} > "$out"
echo "$(grep -vc '^#' "$out") sittings written to $out"
