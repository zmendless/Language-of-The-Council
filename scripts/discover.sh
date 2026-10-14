#!/usr/bin/env bash
# Prints the URL of every sitting PDF in one volume (cilt) of the parliamentary records.
#
#   scripts/discover.sh CHAMBER TERM VOLUME     e.g.  scripts/discover.sh TBMM 8 12
#
# CHAMBER is TBMM (1920-1960 and 1983 onward) or MM (Millet Meclisi, 1961-1980).
# A missing sitting redirects instead of returning a PDF, and the sittings of a volume
# are numbered consecutively. So we probe every 8th number until one exists, then
# binary-search both edges: about 10-40 small requests per volume instead of hundreds.
set -euo pipefail

if [ $# -ne 3 ]; then
    echo "usage: $0 TBMM|MM TERM VOLUME" >&2
    exit 1
fi
chamber=$1
term=$((10#$2))
volume=$((10#$3))
base=https://www5.tbmm.gov.tr/tutanaklar/TUTANAK

case $chamber in
    TBMM) dir=TBMM; prefix=tbmm ;;
    MM)   dir=MM__; prefix=mm__ ;;
    *)    echo "CHAMBER must be TBMM or MM" >&2; exit 1 ;;
esac

url() {
    printf '%s/%s/d%02d/c%03d/%s%02d%03d%03d.pdf' "$base" "$dir" "$term" "$volume" "$prefix" "$term" "$volume" "$1"
}

exists() {
    sleep 0.2   # be polite to the server
    [ "$(curl -s -o /dev/null -I -m 30 -w '%{http_code}' "$(url "$1")")" = 200 ]
}

hit=0
for ((b = 1; b <= 400; b += 8)); do
    if exists $b; then hit=$b; break; fi
done
if [ $hit -eq 0 ]; then
    echo "no sittings found in $chamber term $term volume $volume" >&2
    exit 1
fi

# First sitting: lo is known to be missing (0 never exists), hi is known to exist.
lo=$((hit > 8 ? hit - 8 : 0)); hi=$hit
while ((hi - lo > 1)); do
    mid=$(((lo + hi) / 2))
    if exists $mid; then hi=$mid; else lo=$mid; fi
done
first=$hi

# Last sitting: gallop forward until a miss, then binary-search back.
lo=$hit; step=8
while ((step < 4096)) && exists $((lo + step)); do
    lo=$((lo + step)); step=$((step * 2))
done
hi=$((lo + step))
while ((hi - lo > 1)); do
    mid=$(((lo + hi) / 2))
    if exists $mid; then lo=$mid; else hi=$mid; fi
done
last=$lo

for ((b = first; b <= last; b++)); do
    url $b
    echo
done
