# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A TÜBİTAK student research project. It measures how Turkish words were replaced, for example after the Language Reform, in one consistent kind of text: the parliamentary transcripts (TBMM tutanakları), 1920 to today. The README has the background, the method and the results so far.

The project is presumably for the 2204-A high-school competition (deadline 4 January 2027; the project must be finished by then). The jury checks that the students built it themselves, so keep the code readable and explain changes in a way the team can follow. The charts are in Turkish, for the jury and the exhibition.

## Commands

```sh
make                 # evo (data tools, plain C) + viewer (raylib)
make evo             # without raylib
make test            # all unit tests (one binary, tests/test_main.c, ASan+UBSan); no per-test filter
make pilot           # fetch data/pilot_urls.txt, load the lexicon, ingest, write out/shares.csv
make figures         # out/fits.tsv, out/origins.csv and every chart as a 1920x1080 PNG in out/figures
make video           # out/zaman-makinesi.mp4 and out/en-cok-kullanilanlar.mp4 (ffmpeg)
./evo                # lists the subcommands: ingest, lexicon, share, fit, origins, eras, kwic, stats, unknown, clean
./viewer             # interactive (F full screen, P save PNG, Space play, E exhibition mode)
./viewer --shot NAME out.png   # one chart off screen: takvim, parmakizi, yarislar, koken, omur, sozcukler,
                               # onyillar, zaman:1950, siralama:1950, a concept, or words ("efendi,sayın")
./viewer --video NAME DIR      # frames of an animated view (zaman or siralama)
./viewer --show-shot STEP out.png  # one exhibition slide with its caption (-1: the title slide)
./viewer --dark ...            # any of the above in the dark theme (T in the window)
make web                       # out/web/index.html: the website (evo web fills web/template.html)
```

`--shot`, `--ui-shot`, `--figures` and `--video` render off screen (hidden window). Use them to check visual changes, then look at the PNG.

Data: `scripts/sample.sh 6 2 data/sample_urls.txt` builds the sample list (6 volumes × 2 sittings per term). `scripts/fetch.sh LIST data/raw` downloads it, skipping files it already has. Then run `evo ingest`, which re-reads every transcript in `data/raw` (seconds).

The user's own projects build with `clang … $(pkg-config --cflags --libs raylib) -lm -lX11 -lGL -lpthread -ldl`; the Makefile uses the same link flags. raylib 6.1-dev is installed in `/usr/local`. raygui is vendored in `third_party/raygui`. Python's sqlite3 module works for ad-hoc queries; there is no sqlite3 CLI.

## Architecture

The two programs share `src/text.c`, `clean.c`, `inflect.c`, `db.c`, `query.c` and `analysis.c`:
- `evo` (`src/evo.c`) is the command-line data and analysis tool.
- `viewer` (`src/viewer.c`) is the UI shell. It adds `src/charts.c` (all drawing, raylib) and raygui.

SQLite is the vendored amalgamation, compiled once to `build/sqlite3.o`.

Data flow:
1. `scripts/fetch.sh` writes `data/raw/<id>.txt`. It uses `pdftotext` in default mode; `-layout` interleaves the two printed columns.
2. `evo ingest` stores per-sitting form counts in the `doc` and `form_count` tables. On the way it runs `parseRecordDate`, then `cleanTranscript`, then the tokenizer.
3. `evo lexicon` fills `lexicon` (form → lemma; `src` is `wiktionary`, `alt-spelling`, `old-spelling` or `generated`) and `origin` from `scripts/lexicon.sh` output. It traces derived words to their root's origin (`resolveDerivedOrigins`).
4. `loadStudy` (`analysis.c`) does everything the charts and `evo fit` need:
   - resolves concept words;
   - generates paradigms and applies `data/exclusions.tsv`;
   - counts competitors per sitting;
   - fits an S-curve per concept (`fitLogistic`: Nelder–Mead on the binomial likelihood, then `bootstrapFit` resampling sittings within each year);
   - builds `form_origin` and the origin shares per year;
   - picks the characteristic words of two eras (`eraWords`: log-odds with a Dirichlet prior);
   - loads every noun and adjective used 30+ times with its uses per year (`loadVocabulary`), for the per-year rates (`smoothRate`), the words of each decade (`periodWords`) and the survival of the 1920s words (`survivalOf`).

Invariants that span files:
- **Folding.** Every word is compared in folded form (`trFold`: Turkish I/ı and İ/i, circumflex dropped). This applies to corpus forms, lexicon forms and lemmas, and `data/concepts.tsv` words. If you change folding or tokenizing, re-run both `evo lexicon` and `evo ingest`.
- **Schema.** Changing the tables means bumping `SCHEMA_VERSION` in `src/db.c`. Older databases are refused; delete `db/` and rebuild.
- **Competitor counting order.** `evo share`, `loadStudy` and the viewer's word search all count the same way:
  1. `resolveConcepts`. It follows only `alt-spelling` rows (hükümet → hükûmet), never grammar (hudut must not become had) or verbs (görüşme must not become görüşmek).
  2. `ensureParadigm` for every word. It adds generated forms (`nounForms`, including compounds like milletvekili), skipping forms that belong to another lemma.
  3. `applyExclusions`, after step 2, or the generator re-adds the excluded forms.
  4. `countByYear`.
- **Form collisions.** 2nd-person-singular possessive forms are dropped everywhere (doğa → doğan "born"). Other collisions found with `evo kwic` go into `data/exclusions.tsv` with a reason.
- **Speaker labels** (`BAŞKAN —`, `MALİYE VEKİLİ … —`) must stay stripped. They contain competitor words.
- **Proceedings boundaries** come from `startsProceedings` and `endsSession` in `src/clean.c`. Everything before the first marker (contents, previous-minutes summary) and after the last closing marker (bills, reports, written answers) is cut.
- **Text layers OCR'd without Turkish letters** (under 2% ç ğ ı ö ş ü) are skipped by `loadTranscript`. Parts of term 5 (1936–37) are like this and need re-OCR.
- **Origin metric.** It counts only forms that can be a noun or adjective and cannot be a function word (`buildFormOrigins`). One-letter forms are ignored.
- **The time machine** animates the observed shares (`observedShare`, a widening window), not the S-curve, so that reversals such as vekil in the 1950s show.
- **New OCR quirks.** When you handle a new one, add the real line to `tests/test_main.c`. Dates, cleaning, inflection, fitting and etymology rules are covered there.

The website is `web/template.html`, plain JS with no libraries. `evo web` replaces its `__DATA__` with one JSON object (`writeWebData` in `src/evo.c`) and writes `out/web/index.html` (standalone) and `out/web/artifact.html` (no `<head>`, for hosts that add one). The page re-implements `observedAt`, `smoothRate`, `originShares` and the ranking in JS: change them in both places. Its word search uses `vocab.s` (all lexicon forms, as the viewer's search counts); the ranking and origins use `vocab.n` (noun/adjective forms from `form_origin`), as in the viewer. Check page changes with headless Firefox (`firefox --headless --no-remote --profile DIR --screenshot out.png --window-size=1280,9000 file://…`); there is no Node.

`charts.c` draws everything for a 1600×900 design scaled by `S`. Each view is a `draw*` function taking a `ChartState`. `saveView` renders off screen and flips the image (render textures are upside down). The reform dates drawn on every chart are `EVENTS` in `charts.c` (and the same list in `web/template.html`). Neutral colours (`PAPER`, `INK`, …) change with `setChartTheme`; data colours do not. Exported PNGs go through `makeOpaque`, because blending also lowers the alpha channel of a render texture. The animated views (`viewIsAnimated`) are drawn from `ChartState.year` alone, with no state between frames, so the video frames and the window show the same thing.

The URL patterns for the transcripts are in `scripts/discover.sh`. Missing sittings redirect (302), and volume numbering has gaps. Data under `data/raw`, `data/external`, `data/lexicon`, `db` and `out` is generated and not in git. The URL lists and `data/concepts.tsv` / `data/exclusions.tsv` are tracked.

## Code style

Match the user's C style: camelCase functions, UPPER_CASE macros and constants, PascalCase typedefs, 4-space indent, `for(` / `if(` without a space, braces on the same line.
