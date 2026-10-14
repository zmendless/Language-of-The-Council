# make           build the data tools (evo) and the viewer
# make evo       only the data tools; needs nothing but a C compiler
# make test      run the unit tests
# make pilot     download the pilot sample and build db/corpus.sqlite from it
# make sample    the same with the small sample (data/sample_urls.txt, about 260 sittings)
# make large     adds the larger sample (data/large_urls.txt, about 1,000 sittings; hours)
# make figures   every chart as a 1920x1080 PNG in out/figures (after make pilot)
# make figures-dark  the same in the dark theme, for projectors, in out/figures-koyu
# make web       the web page: out/web/index.html (opens from disk) and out/web/artifact.html
# make video     the animations as out/zaman-makinesi.mp4 and out/en-cok-kullanilanlar.mp4 (needs ffmpeg)

CFLAGS = -std=gnu11 -O2 -Wall -Wextra
SQLITE_DEFS = -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0
RAYLIB = $(shell pkg-config --cflags --libs raylib) -lm -lX11 -lGL -lpthread -ldl

CORE = src/text.c src/clean.c src/inflect.c src/db.c src/query.c src/analysis.c src/dating.c
HEADERS = $(wildcard src/*.h)
DB = db/corpus.sqlite

all: evo viewer

# SQLite is one big file that never changes; compile it once.
build/sqlite3.o: third_party/sqlite/sqlite3.c
	@mkdir -p build
	$(CC) -O2 -w $(SQLITE_DEFS) -c $< -o $@

evo: src/evo.c $(CORE) $(HEADERS) build/sqlite3.o
	$(CC) $(CFLAGS) src/evo.c $(CORE) build/sqlite3.o -o $@ -lm

viewer: src/viewer.c src/charts.c $(CORE) $(HEADERS) build/sqlite3.o
	$(CC) $(CFLAGS) -Ithird_party/raygui src/viewer.c src/charts.c $(CORE) build/sqlite3.o -o $@ $(RAYLIB)

build/tests: tests/test_main.c $(CORE) $(HEADERS) build/sqlite3.o
	@mkdir -p build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined tests/test_main.c $(CORE) build/sqlite3.o -o $@ -lm

test: build/tests
	./build/tests

data/lexicon/forms.tsv:
	scripts/lexicon.sh

pilot: evo data/lexicon/forms.tsv
	scripts/fetch.sh data/pilot_urls.txt data/raw
	@mkdir -p db out
	./evo lexicon $(DB) data/lexicon/forms.tsv data/lexicon/etymology.tsv
	./evo ingest $(DB) data/raw
	./evo share $(DB) data/concepts.tsv out/shares.csv

large: evo data/lexicon/forms.tsv
	scripts/fetch.sh data/large_urls.txt data/raw
	@mkdir -p db out
	./evo lexicon $(DB) data/lexicon/forms.tsv data/lexicon/etymology.tsv
	./evo ingest $(DB) data/raw
	./evo share $(DB) data/concepts.tsv out/shares.csv

sample: evo data/lexicon/forms.tsv
	scripts/fetch.sh data/sample_urls.txt data/raw
	@mkdir -p db out
	./evo lexicon $(DB) data/lexicon/forms.tsv data/lexicon/etymology.tsv
	./evo ingest $(DB) data/raw

figures: viewer evo
	./evo fit $(DB) data/concepts.tsv out/fits.tsv
	./evo origins $(DB) out/origins.csv
	./viewer --figures out/figures

figures-dark: viewer
	./viewer --dark --figures out/figures-koyu

# Every result as JSON inside web/template.html, with example sentences from data/raw.
web: evo
	@mkdir -p out/web
	./evo web $(DB) data/concepts.tsv data/raw web/template.html out/web

video: out/zaman-makinesi.mp4 out/en-cok-kullanilanlar.mp4

# The frames are rendered off screen, joined with ffmpeg, then deleted.
out/zaman-makinesi.mp4 out/en-cok-kullanilanlar.mp4: viewer
	rm -rf out/frames
	./viewer --video $(if $(findstring zaman,$@),zaman,siralama) out/frames
	ffmpeg -y -loglevel error -framerate 30 -i out/frames/%04d.png -c:v libx264 -pix_fmt yuv420p -crf 26 $@
	rm -rf out/frames
	@ls -la $@

clean:
	rm -rf build evo viewer

.PHONY: all test pilot sample large figures figures-dark web video clean out/zaman-makinesi.mp4 out/en-cok-kullanilanlar.mp4
