#!/usr/bin/env bash
# Builds a sample spread evenly over every term: VOLUMES volumes per term, evenly spaced
# from first to last, and SITTINGS sittings from each, evenly spaced within the volume.
#
#   scripts/sample.sh VOLUMES SITTINGS OUT      e.g.  scripts/sample.sh 6 2 data/sample_urls.txt
#
# Each volume has an index PDF (tbmmDDCCCfih.pdf) that exists only if the volume does,
# so one request per guess finds a term's last volume (galloping, then binary search).
# Takes a while, because the server is probed politely.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ $# -ne 3 ]; then
    echo "usage: $0 VOLUMES SITTINGS OUT" >&2
    exit 1
fi
perTerm=$1
perVolume=$2
out=$3
base=https://www5.tbmm.gov.tr/tutanaklar/TUTANAK
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

volumeExists() {   # chamber term volume
    local dir=TBMM prefix=tbmm
    [ "$1" = MM ] && dir=MM__ && prefix=mm__
    sleep 0.2
    [ "$(curl -s -o /dev/null -I -m 30 -w '%{http_code}' \
        "$(printf '%s/%s/d%02d/c%03d/%s%02d%03dfih.pdf' "$base" "$dir" "$2" "$3" "$prefix" "$2" "$3")")" = 200 ]
}

lastVolume() {   # chamber term
    local lo=1 step=4
    while ((step < 1024)) && volumeExists "$1" "$2" $((lo + step)); do
        lo=$((lo + step)); step=$((step * 2))
    done
    local hi=$((lo + step))
    while ((hi - lo > 1)); do
        local mid=$(((lo + hi) / 2))
        if volumeExists "$1" "$2" $mid; then lo=$mid; else hi=$mid; fi
    done
    echo $lo
}

{
    echo "# Sample made by scripts/sample.sh $perTerm $perVolume: evenly spread volumes and sittings per term."
    for spec in TBMM:1 TBMM:2 TBMM:3 TBMM:4 TBMM:5 TBMM:6 TBMM:7 TBMM:8 TBMM:9 TBMM:10 TBMM:11 \
                MM:1 MM:2 MM:3 MM:4 MM:5 \
                TBMM:17 TBMM:18 TBMM:19 TBMM:20 TBMM:21 TBMM:22 TBMM:23 TBMM:24 TBMM:25 TBMM:26 TBMM:27 TBMM:28; do
        chamber=${spec%%:*}
        term=${spec##*:}
        last=$(lastVolume "$chamber" "$term")
        echo "$chamber term $term: $last volumes" >&2
        for ((i = 0; i < perTerm; i++)); do
            volume=$(((2 * i + 1) * last / (2 * perTerm) + 1))
            ((volume > last)) && volume=$last
            scripts/discover.sh "$chamber" "$term" "$volume" > "$tmp" 2> /dev/null || continue
            n=$(wc -l < "$tmp")
            for ((s = 0; s < perVolume; s++)); do
                sed -n "$(((2 * s + 1) * n / (2 * perVolume) + 1))p" "$tmp"
            done
        done
    done
} | awk '!seen[$0]++' > "$out"
echo "$(grep -vc '^#' "$out") sittings written to $out" >&2
