#!/usr/bin/env bash
# Downloads sitting PDFs and keeps only their text, as data/raw/<name>.txt.
#
#   scripts/fetch.sh URL_LIST [OUT_DIR]
#
# URL_LIST has one PDF URL per line (# starts a comment). Files that are already
# downloaded are skipped, so an interrupted run can simply be started again.
# Modern PDFs are up to ~40 MB each, so each PDF is deleted right after its text is
# extracted. pdftotext runs in its default reading-order mode on purpose: -layout
# interleaves the two printed columns line by line.
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "usage: $0 URL_LIST [OUT_DIR]" >&2
    exit 1
fi
list=$1
out=${2:-data/raw}
mkdir -p "$out"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

while read -r url; do
    [[ -z "$url" || "$url" == \#* ]] && continue
    name=$(basename "$url" .pdf)
    [ -s "$out/$name.txt" ] && continue
    if curl -sfL -m 600 --retry 2 -o "$tmp/$name.pdf" "$url" && [ "$(head -c 4 "$tmp/$name.pdf")" = "%PDF" ]; then
        pdftotext "$tmp/$name.pdf" "$out/$name.txt"
        echo "ok    $name  $(wc -w < "$out/$name.txt") words"
    else
        echo "FAIL  $url" >&2
    fi
    rm -f "$tmp/$name.pdf"
    sleep 2   # be polite to the server
done < "$list"
