#!/usr/bin/env bash
# Extracts word forms and etymologies from the Wiktionary Turkish data (kaikki.org,
# CC BY-SA) into two TSV files that `evo lexicon` loads into the database:
#
#   data/lexicon/forms.tsv       form <TAB> lemma <TAB> part of speech <TAB> tags (comma-separated)
#   data/lexicon/etymology.tsv   lemma <TAB> part of speech <TAB> etymology text
#
# Nouns come with full declension tables (sene, seneyi, senede, senesi, ...), which is
# what lets us count every inflected form of a word. Downloads the 437 MB file once.
set -euo pipefail
cd "$(dirname "$0")/.."

src=data/external/kaikki-turkish.jsonl
url=https://kaikki.org/dictionary/Turkish/kaikki.org-dictionary-Turkish.jsonl
out=data/lexicon
mkdir -p data/external "$out"
[ -s "$src" ] || curl -L --fail -o "$src" "$url"

# Every entry's headword plus its inflected forms. Rows tagged "table-tags" or
# "inflection-template" are table metadata, not words. Wiktionary also has pages that
# only point to another word: single inflected forms ("mektebe: dative singular of
# mektep") and other spellings ("hükümet: misspelling of hükûmet"). Those, with their
# own forms, are mapped to the word they point to.
jq -r '
    select(.word != null and .pos != null)
    | .word as $w | .pos as $p
    | [[$w, ""]] + [(.forms // [])[]
                    | select(((.tags // []) | (index("table-tags") or index("inflection-template"))) | not)
                    | select(.form | type == "string")
                    | [.form, ((.tags // []) | join(","))]] as $forms
    | if ([(.senses // [])[] | select(.form_of != null or .alt_of != null)] | length) > 0
         and ([(.senses // [])[] | select(.form_of == null and .alt_of == null)] | length) == 0
      then (.senses[] | ((.tags // []) | join(",")) as $t | ((.form_of // []) + (.alt_of // []))[]
            | .word | select(type == "string") | [$w, ., $p, $t] | @tsv)
      else $forms[] | [.[0], $w, $p, .[1]] | @tsv
      end
' "$src" > "$out/forms.tsv"

jq -r '
    select(.word != null and .pos != null and .etymology_text != null)
    | [.word, .pos, (.etymology_text | gsub("[\t\r\n]"; " "))] | @tsv
' "$src" > "$out/etymology.tsv"

echo "$(wc -l < "$out/forms.tsv") forms, $(wc -l < "$out/etymology.tsv") etymologies in $out/"
