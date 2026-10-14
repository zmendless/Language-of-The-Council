// evo: the project's command-line tools. Run it without arguments for the list of commands.
#include "analysis.h"
#include "clean.h"
#include "dating.h"
#include "db.h"
#include "query.h"
#include "text.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void) {
    fprintf(stderr,
        "usage:\n"
        "  evo ingest  DB RAW_DIR                clean, date and count every transcript in RAW_DIR\n"
        "  evo lexicon DB FORMS.tsv ETYM.tsv     load Wiktionary forms and origins (made by scripts/lexicon.sh)\n"
        "  evo share   DB CONCEPTS.tsv OUT.csv   yearly counts of competing words, and a summary per decade\n"
        "  evo kwic    DB RAW_DIR WORD [N] [SEED]  N random uses of WORD in context, to check its meaning\n"
        "  evo fit     DB CONCEPTS.tsv OUT.tsv   fit the S-curve of every concept: when, how fast, how far\n"
        "  evo origins DB OUT.csv                Arabic/Persian/Turkish/Western share of nouns and adjectives per year\n"
        "  evo eras    DB FROM TO FROM TO [N]    the N words most typical of each of two periods\n"
        "  evo stats   DB                        sittings, words and dictionary coverage per decade\n"
        "  evo unknown DB FROM TO [N]            the N most frequent words of years FROM-TO the lexicon lacks\n"
        "  evo date    DB TEXT.txt               the 5 years a text most likely comes from, and the words that tell\n"
        "  evo datetest DB                       how well evo date does on sittings it has not seen\n"
        "  evo clean   RAW.txt                   print one cleaned transcript, to check the cleaner\n"
        "  evo web     DB CONCEPTS.tsv RAW_DIR TEMPLATE.html OUT_DIR\n"
        "                                        the web page: every result as JSON in TEMPLATE, with example sentences\n");
    return 2;
}

static char *readFile(const char *path) {
    FILE *f = fopen(path, "rb");
    if(!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(size + 1);
    size_t got = fread(data, 1, size, f);
    data[got] = 0;
    fclose(f);
    return data;
}

static int compareStrings(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

// The .txt files in dir, sorted so that every run sees them in the same order.
static char **listTranscripts(const char *dir, int *count) {
    DIR *d = opendir(dir);
    if(!d) {
        perror(dir);
        exit(1);
    }
    char **names = NULL;
    int n = 0, cap = 0;
    struct dirent *e;
    while((e = readdir(d))) {
        size_t len = strlen(e->d_name);
        if(len < 5 || strcmp(e->d_name + len - 4, ".txt") != 0) continue;
        if(n == cap) names = realloc(names, (cap = cap ? cap * 2 : 64) * sizeof *names);
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, n, sizeof *names, compareStrings);
    *count = n;
    return names;
}

// Real Turkish text has 7-12% Turkish-only letters. Some PDFs were OCR'd with an
// English model and have none (the 1936 "Yiiksek miihendis mektebinin").
#define MIN_TURKISH_LETTERS 0.02

// A transcript ready to count: its id, date and cleaned text, split into lines.
typedef struct {
    char id[64];
    SittingId sitting;
    Date date;
    double turkishLetters;
    CleanStats stats;
    char *text;
    char **lines;
    int lineCount;
} Transcript;

static void splitLines(Transcript *t) {
    int cap = 256;
    t->lineCount = 0;
    t->lines = malloc(cap * sizeof *t->lines);
    for(char *p = t->text; *p;) {
        if(t->lineCount == cap) t->lines = realloc(t->lines, (cap *= 2) * sizeof *t->lines);
        t->lines[t->lineCount++] = p;
        char *nl = strchr(p, '\n');
        if(!nl) break;
        *nl = 0;
        p = nl + 1;
    }
}

static void freeTranscript(Transcript *t) {
    free(t->lines);
    free(t->text);
}

// verbose: report transcripts that cannot be used (ingest does; kwic does not repeat it).
static bool loadTranscript(const char *dir, const char *fileName, Transcript *t, bool verbose) {
    snprintf(t->id, sizeof t->id, "%.*s", (int)strlen(fileName) - 4, fileName);
    if(!parseSittingId(t->id, &t->sitting)) {
        if(verbose) fprintf(stderr, "skip %s: not a sitting file name\n", fileName);
        return false;
    }
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, fileName);
    char *raw = readFile(path);
    if(!raw || !parseRecordDate(raw, &t->sitting, &t->date)) {
        if(verbose) fprintf(stderr, "skip %s: no date found\n", t->id);
        free(raw);
        return false;
    }
    t->turkishLetters = trTurkishLetterShare(raw);
    if(t->turkishLetters < MIN_TURKISH_LETTERS) {
        if(verbose) fprintf(stderr, "skip %s: its text layer has no Turkish letters (OCR'd without Turkish), so its words "
                "cannot be matched; it needs to be OCR'd again\n", t->id);
        free(raw);
        return false;
    }
    t->text = cleanTranscript(raw, &t->stats);
    free(raw);
    splitLines(t);
    return true;
}

// ---------------------------------------------------------------- word counter

typedef struct {
    char *word;
    long n;
} Slot;

typedef struct {
    Slot *slots;
    int cap, used;   // cap is a power of two
} Counter;

static uint32_t hashString(const char *s) {   // FNV-1a
    uint32_t h = 2166136261u;
    while(*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

static Slot *findSlot(Counter *c, const char *word) {
    uint32_t i = hashString(word) & (c->cap - 1);
    while(c->slots[i].word && strcmp(c->slots[i].word, word) != 0) i = (i + 1) & (c->cap - 1);
    return &c->slots[i];
}

static void counterAdd(Counter *c, const char *word) {
    if(c->used * 10 >= c->cap * 7) {   // grow at 70% full
        Slot *old = c->slots;
        int oldCap = c->cap;
        c->cap = oldCap ? oldCap * 2 : 1024;
        c->slots = calloc(c->cap, sizeof *c->slots);
        for(int i = 0; i < oldCap; i++) {
            if(old[i].word) *findSlot(c, old[i].word) = old[i];
        }
        free(old);
    }
    Slot *s = findSlot(c, word);
    if(!s->word) {
        s->word = strdup(word);
        c->used++;
    }
    s->n++;
}

static void counterFree(Counter *c) {
    for(int i = 0; i < c->cap; i++) free(c->slots[i].word);
    free(c->slots);
}

// ---------------------------------------------------------------- ingest

static int cmdIngest(char **argv) {
    sqlite3 *db = dbOpen(argv[0]);
    int count;
    char **names = listTranscripts(argv[1], &count);
    dbExec(db, "PRAGMA synchronous = OFF");   // after a crash, just ingest again
    sqlite3_stmt *delCounts = dbPrepare(db, "DELETE FROM form_count WHERE doc_id = ?1");
    sqlite3_stmt *insDoc = dbPrepare(db,
        "INSERT OR REPLACE INTO doc VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16)");
    sqlite3_stmt *insCount = dbPrepare(db, "INSERT INTO form_count VALUES (?1, ?2, ?3)");

    int done = 0, skipped = 0;
    long allWords = 0;
    for(int i = 0; i < count; i++) {
        Transcript t;
        if(!loadTranscript(argv[1], names[i], &t, true)) {
            skipped++;
            continue;
        }
        Counter words = {0};
        long tokens = 0;
        char word[MAX_WORD];
        for(int l = 0; l < t.lineCount; l++) {
            Tokenizer tok;
            tokInit(&tok, t.lines[l]);
            while(tokNext(&tok, word, NULL, NULL)) {
                counterAdd(&words, word);
                tokens++;
            }
        }

        char date[16];
        snprintf(date, sizeof date, "%04d-%02d-%02d", t.date.year, t.date.month, t.date.day);
        dbExec(db, "BEGIN");
        sqlite3_bind_text(delCounts, 1, t.id, -1, SQLITE_TRANSIENT);
        dbStep(db, delCounts);
        sqlite3_reset(delCounts);

        const CleanStats *st = &t.stats;
        sqlite3_bind_text(insDoc, 1, t.id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insDoc, 2, t.sitting.chamber, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insDoc, 3, t.sitting.term);
        sqlite3_bind_int(insDoc, 4, t.sitting.volume);
        sqlite3_bind_int(insDoc, 5, t.sitting.sitting);
        sqlite3_bind_text(insDoc, 6, date, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insDoc, 7, t.date.year);
        sqlite3_bind_int64(insDoc, 8, tokens);
        sqlite3_bind_double(insDoc, 9, t.turkishLetters);
        int values[] = {st->linesIn, st->frontMatter, st->backMatter, st->headings, st->furniture, st->labels, st->linesOut};
        for(int v = 0; v < 7; v++) sqlite3_bind_int(insDoc, 10 + v, values[v]);
        dbStep(db, insDoc);
        sqlite3_reset(insDoc);

        for(int s = 0; s < words.cap; s++) {
            if(!words.slots[s].word) continue;
            sqlite3_bind_text(insCount, 1, t.id, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(insCount, 2, words.slots[s].word, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(insCount, 3, words.slots[s].n);
            dbStep(db, insCount);
            sqlite3_reset(insCount);
        }
        dbExec(db, "COMMIT");

        printf("%s  %s  %7ld words  %5d labels  %5d headings   cut: %5d lines before, %5d after\n",
               t.id, date, tokens, st->labels, st->headings, st->frontMatter, st->backMatter);
        counterFree(&words);
        freeTranscript(&t);
        done++;
        allWords += tokens;
    }
    printf("%d sittings ingested (%ld words), %d skipped\n", done, allWords, skipped);
    sqlite3_finalize(delCounts);
    sqlite3_finalize(insDoc);
    sqlite3_finalize(insCount);
    sqlite3_close(db);
    return 0;
}

// ---------------------------------------------------------------- lexicon

// Letters only: drops multi-word entries ("Türkiye Büyük Millet Meclisi") and the
// Arabic-script spellings that Wiktionary lists for Ottoman words.
static bool isWord(const char *s) {
    if(!*s) return false;
    while(*s) {
        uint32_t cp;
        s += utf8Decode(s, &cp);
        if(!trIsLetter(cp)) return false;
    }
    return true;
}

// Splits a TSV line in place into up to max fields; returns how many it found.
static int splitTabs(char *line, char **fields, int max) {
    line[strcspn(line, "\r\n")] = 0;
    int n = 0;
    while(n < max) {
        fields[n++] = line;
        char *tab = strchr(line, '\t');
        if(!tab) break;
        *tab = 0;
        line = tab + 1;
    }
    return n;
}

typedef struct {
    char form[MAX_WORD + 4], lemma[MAX_WORD], pos[32];
} Variant;

// Before the spelling rules of the 1930s-40s, many words ending in p, t or ç were written
// with b, d or c: kâtib, intihab, Yozgad. For every such lemma we add the old spelling of
// each of its forms, unless that spelling is already some other word (at → ad would be).
static int addOldSpellings(sqlite3 *db, sqlite3_stmt *ins) {
    sqlite3_stmt *sel = dbPrepare(db,
        "SELECT form, lemma, pos FROM lexicon WHERE src = 'wiktionary' AND length(lemma) >= 4 "
        "AND substr(lemma, -1) IN ('p', 't', 'ç') AND substr(form, 1, length(lemma)) = lemma");
    sqlite3_stmt *exists = dbPrepare(db, "SELECT 1 FROM lexicon WHERE form = ?1");
    Variant *found = NULL;
    int n = 0, cap = 0;
    while(dbStep(db, sel) == SQLITE_ROW) {
        const char *form = (const char *)sqlite3_column_text(sel, 0);
        const char *lemma = (const char *)sqlite3_column_text(sel, 1);
        int len = (int)strlen(lemma);
        int last = (unsigned char)lemma[len - 1] == 0xA7 ? 2 : 1;   // ç is two bytes
        const char *repl = last == 2 ? "c" : lemma[len - 1] == 'p' ? "b" : "d";
        Variant v;
        snprintf(v.form, sizeof v.form, "%.*s%s%s", len - last, lemma, repl, form + len);
        snprintf(v.lemma, sizeof v.lemma, "%s", lemma);
        snprintf(v.pos, sizeof v.pos, "%s", (const char *)sqlite3_column_text(sel, 2));
        sqlite3_bind_text(exists, 1, v.form, -1, SQLITE_TRANSIENT);
        bool taken = dbStep(db, exists) == SQLITE_ROW;
        sqlite3_reset(exists);
        if(taken) continue;
        if(n == cap) found = realloc(found, (cap = cap ? cap * 2 : 1024) * sizeof *found);
        found[n++] = v;
    }
    sqlite3_finalize(sel);
    sqlite3_finalize(exists);
    for(int i = 0; i < n; i++) {   // inserted after the scan, so the scan never sees its own rows
        sqlite3_bind_text(ins, 1, found[i].form, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 2, found[i].lemma, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 3, found[i].pos, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 4, "old-spelling", -1, SQLITE_STATIC);
        dbStep(db, ins);
        sqlite3_reset(ins);
    }
    free(found);
    return n;
}

// A word built in Turkish from a foreign root is not a Turkish word in the sense of the
// Language Reform: kitapçı = kitap (Arabic) + -çı. Such words take their root's origin.
// Repeated so that chains (a word from a derived word) resolve too.
static int resolveDerivedOrigins(sqlite3 *db) {
    sqlite3_stmt *sel = dbPrepare(db, "SELECT lemma, pos, etymology FROM origin WHERE origin = 'turkish-derived'");
    sqlite3_stmt *root = dbPrepare(db,
        "SELECT origin FROM origin WHERE lemma = ?1 AND origin <> 'unclear' "
        "ORDER BY pos IN ('noun', 'adj', 'verb') DESC LIMIT 1");
    sqlite3_stmt *upd = dbPrepare(db, "UPDATE origin SET origin = ?3 WHERE lemma = ?1 AND pos = ?2");
    int total = 0;
    for(int round = 0; round < 3; round++) {
        typedef struct { char lemma[MAX_WORD], pos[32], origin[32]; } Change;
        Change *changes = NULL;
        int n = 0, cap = 0;
        while(dbStep(db, sel) == SQLITE_ROW) {
            const char *lemma = (const char *)sqlite3_column_text(sel, 0);
            char rootWord[MAX_WORD];
            if(!etymologyRoot((const char *)sqlite3_column_text(sel, 2), rootWord) || strcmp(rootWord, lemma) == 0) continue;
            sqlite3_bind_text(root, 1, rootWord, -1, SQLITE_TRANSIENT);
            if(dbStep(db, root) == SQLITE_ROW) {
                const char *o = (const char *)sqlite3_column_text(root, 0);
                if(strcmp(o, "turkish-derived") != 0) {
                    if(n == cap) changes = realloc(changes, (cap = cap ? cap * 2 : 256) * sizeof *changes);
                    snprintf(changes[n].lemma, MAX_WORD, "%s", lemma);
                    snprintf(changes[n].pos, 32, "%s", (const char *)sqlite3_column_text(sel, 1));
                    snprintf(changes[n].origin, 32, "%s", o);
                    n++;
                }
            }
            sqlite3_reset(root);
        }
        sqlite3_reset(sel);
        for(int i = 0; i < n; i++) {
            sqlite3_bind_text(upd, 1, changes[i].lemma, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(upd, 2, changes[i].pos, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(upd, 3, changes[i].origin, -1, SQLITE_TRANSIENT);
            dbStep(db, upd);
            sqlite3_reset(upd);
        }
        free(changes);
        total += n;
        if(n == 0) break;
    }
    sqlite3_finalize(sel);
    sqlite3_finalize(root);
    sqlite3_finalize(upd);
    return total;
}

static int cmdLexicon(char **argv) {
    sqlite3 *db = dbOpen(argv[0]);
    FILE *forms = fopen(argv[1], "r");
    FILE *etym = fopen(argv[2], "r");
    if(!forms || !etym) {
        perror(!forms ? argv[1] : argv[2]);
        return 1;
    }
    dbExec(db, "BEGIN; DELETE FROM lexicon; DELETE FROM origin;");
    sqlite3_stmt *ins = dbPrepare(db, "INSERT OR IGNORE INTO lexicon VALUES (?1, ?2, ?3, ?4)");
    char *line = NULL;
    size_t size = 0;
    long rows = 0;
    while(getline(&line, &size, forms) > 0) {
        char *f[4];
        int nf = splitTabs(line, f, 4);
        if(nf < 3) continue;
        // "Your (singular) X": almost never said in parliament, and these forms collide
        // with frequent words (doğan is "your nature", but mostly "born" or the name Doğan).
        if(nf == 4 && strstr(f[3], "second-person") && strstr(f[3], "singular")) continue;
        char form[MAX_WORD], lemma[MAX_WORD];
        trFoldString(f[0], form, sizeof form);
        trFoldString(f[1], lemma, sizeof lemma);
        if(!isWord(form) || !isWord(lemma)) continue;
        // (Each headword's own row comes from its entry; a page that points to a word must
        // not add that word under the page's part of speech: inceleme → incelemek.)
        sqlite3_bind_text(ins, 1, form, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 2, lemma, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 3, f[2], -1, SQLITE_TRANSIENT);
        // Other spellings of a word (hükümet for hükûmet) are marked: concept words are
        // redirected only through these, never through grammar (hudut, plural of had).
        bool spelling = nf == 4 && (strstr(f[3], "alt-of") || strstr(f[3], "misspelling"));
        sqlite3_bind_text(ins, 4, spelling ? "alt-spelling" : "wiktionary", -1, SQLITE_STATIC);
        dbStep(db, ins);
        sqlite3_reset(ins);
        rows++;
    }
    int variants = addOldSpellings(db, ins);
    sqlite3_finalize(ins);

    sqlite3_stmt *insOrigin = dbPrepare(db, "INSERT OR IGNORE INTO origin VALUES (?1, ?2, ?3, ?4)");
    long origins = 0;
    while(getline(&line, &size, etym) > 0) {
        char *f[3];
        if(splitTabs(line, f, 3) != 3) continue;
        char lemma[MAX_WORD];
        trFoldString(f[0], lemma, sizeof lemma);
        if(!isWord(lemma)) continue;
        sqlite3_bind_text(insOrigin, 1, lemma, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insOrigin, 2, f[1], -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insOrigin, 3, originOf(f[2]), -1, SQLITE_STATIC);
        sqlite3_bind_text(insOrigin, 4, f[2], -1, SQLITE_TRANSIENT);
        dbStep(db, insOrigin);
        sqlite3_reset(insOrigin);
        origins++;
    }
    sqlite3_finalize(insOrigin);
    int derived = resolveDerivedOrigins(db);
    dbExec(db, "COMMIT");
    free(line);
    fclose(forms);
    fclose(etym);
    printf("%ld Wiktionary forms, %d old spellings, %ld etymologies loaded; %d derived words traced to their root\n",
           rows, variants, origins, derived);
    sqlite3_close(db);
    return 0;
}

// ---------------------------------------------------------------- share

static int cmdShare(char **argv) {
    sqlite3 *db = dbOpen(argv[0]);
    ConceptList cl;
    if(!loadConcepts(argv[1], &cl)) {
        perror(argv[1]);
        return 1;
    }
    resolveConcepts(db, &cl);
    FILE *out = fopen(argv[2], "w");
    if(!out) {
        perror(argv[2]);
        return 1;
    }
    YearCounts tokens;
    tokensByYear(db, &tokens);
    YearCounts *counts = malloc(cl.count * sizeof *counts);
    int *nForms = malloc(cl.count * sizeof *nForms);
    fprintf(out, "concept,role,lemma,year,count,year_tokens\n");
    for(int i = 0; i < cl.count; i++) ensureParadigm(db, cl.items[i].lemma);
    char exclusions[1024];
    exclusionsPathFor(argv[1], exclusions, sizeof exclusions);
    applyExclusions(db, exclusions);
    for(int i = 0; i < cl.count; i++) {
        const Competitor *c = &cl.items[i];
        nForms[i] = ensureParadigm(db, c->lemma);
        applyExclusions(db, exclusions);
        countByYear(db, c->lemma, &counts[i]);
        for(int y = 0; y < NYEARS; y++) {
            if(tokens.n[y]) fprintf(out, "%s,%s,%s,%d,%ld,%ld\n", c->concept, c->role, c->lemma, FIRST_YEAR + y, counts[i].n[y], tokens.n[y]);
        }
    }
    fclose(out);

    // Summary: per concept and decade, how often each competitor was used and the share
    // of today's word among them.
    for(int i = 0; i < cl.count; i++) {
        bool first = true;
        for(int j = 0; j < i; j++) first = first && strcmp(cl.items[j].concept, cl.items[i].concept) != 0;
        if(!first) continue;
        int members[8], m = 0;
        for(int j = i; j < cl.count && m < 8; j++) {
            if(strcmp(cl.items[j].concept, cl.items[i].concept) == 0) members[m++] = j;
        }
        printf("\n%-8s", cl.items[i].concept);
        for(int k = 0; k < m; k++) printf("%12s", cl.items[members[k]].lemma);
        printf("   new share\n%-8s", "(forms)");
        for(int k = 0; k < m; k++) printf("%12d", nForms[members[k]]);
        printf("\n");
        for(int decade = 1920; decade <= 2020; decade += 10) {
            long sums[8] = {0}, total = 0, fresh = 0;
            for(int k = 0; k < m; k++) {
                for(int y = decade; y < decade + 10; y++) sums[k] += counts[members[k]].n[y - FIRST_YEAR];
                total += sums[k];
                if(strcmp(cl.items[members[k]].role, "new") == 0) fresh += sums[k];
            }
            printf("%-8d", decade);
            for(int k = 0; k < m; k++) printf("%12ld", sums[k]);
            if(total) printf("   %5.1f%%\n", 100.0 * fresh / total);
            else printf("       -\n");
        }
        for(int k = 0; k < m; k++) warnAmbiguous(db, cl.items[members[k]].lemma);
    }
    printf("\nyearly counts written to %s\n", argv[2]);
    free(nForms);
    free(counts);
    free(cl.items);
    sqlite3_close(db);
    return 0;
}

// ---------------------------------------------------------------- kwic

typedef struct {
    int year;
    char *text;
} Hit;

static uint64_t rngState = 1;

static uint64_t nextRandom(void) {   // xorshift64*: the same seed gives the same sample everywhere
    rngState ^= rngState >> 12;
    rngState ^= rngState << 25;
    rngState ^= rngState >> 27;
    return rngState * 2685821657736338717ull;
}

static int compareHits(const void *a, const void *b) {
    return ((const Hit *)a)->year - ((const Hit *)b)->year;
}

static int cmdKwic(int argc, char **argv) {
    sqlite3 *db = dbOpen(argv[0]);
    char folded[MAX_WORD], lemma[MAX_WORD];
    trFoldString(argv[2], folded, sizeof folded);
    lemmaOf(db, folded, lemma);
    ensureParadigm(db, lemma);
    int want = argc > 3 ? atoi(argv[3]) : 20;
    rngState = argc > 4 ? (uint64_t)atoll(argv[4]) * 2654435761u + 1 : 1;
    if(want < 1) want = 1;

    sqlite3_stmt *sel = dbPrepare(db, "SELECT form FROM lexicon WHERE lemma = ?1 UNION SELECT ?1");
    sqlite3_bind_text(sel, 1, lemma, -1, SQLITE_TRANSIENT);
    Counter forms = {0};
    while(dbStep(db, sel) == SQLITE_ROW) counterAdd(&forms, (const char *)sqlite3_column_text(sel, 0));
    sqlite3_finalize(sel);

    Hit *sample = calloc(want, sizeof *sample);
    long seen = 0;
    int count;
    char **names = listTranscripts(argv[1], &count);
    for(int i = 0; i < count; i++) {
        Transcript t;
        if(!loadTranscript(argv[1], names[i], &t, false)) continue;
        for(int l = 0; l < t.lineCount; l++) {
            const char *line = t.lines[l];
            const char *prev = l > 0 ? t.lines[l - 1] : "";
            const char *next = l + 1 < t.lineCount ? t.lines[l + 1] : "";
            Tokenizer tok;
            tokInit(&tok, line);
            char word[MAX_WORD];
            int start, len;
            while(tokNext(&tok, word, &start, &len)) {
                if(!findSlot(&forms, word)->word) continue;
                seen++;
                long slot = seen <= want ? seen - 1 : (long)(nextRandom() % seen);   // reservoir sampling
                if(slot >= want) continue;
                // The neighbouring lines too: in two-column print a line is only ~6 words.
                char *text = malloc(strlen(prev) + strlen(line) + strlen(next) + strlen(t.id) + 16);
                sprintf(text, "%s  %.*s[%.*s]%s  %s  (%s)", prev, start, line, len, line + start,
                        line + start + len, next, t.id);
                free(sample[slot].text);
                sample[slot].year = t.date.year;
                sample[slot].text = text;
            }
        }
        freeTranscript(&t);
    }
    int shown = seen < want ? (int)seen : want;
    qsort(sample, shown, sizeof *sample, compareHits);
    for(int i = 0; i < shown; i++) printf("%d  %s\n", sample[i].year, sample[i].text);
    printf("\n%ld uses of %s (%d forms) in total; %d shown\n", seen, lemma, forms.used, shown);
    sqlite3_close(db);
    return 0;
}

// ---------------------------------------------------------------- stats

static int cmdStats(char **argv) {
    sqlite3 *db = dbOpen(argv[0]);
    // Coverage: the share of words whose form the lexicon knows, i.e. that we can trace
    // to a dictionary word and an origin. Older decades have more unknown words.
    sqlite3_stmt *stmt = dbPrepare(db,
        "SELECT (d.year / 10) * 10, COUNT(DISTINCT d.id), SUM(f.n), "
        "SUM(CASE WHEN EXISTS (SELECT 1 FROM lexicon l WHERE l.form = f.form) THEN f.n ELSE 0 END) "
        "FROM form_count f JOIN doc d ON d.id = f.doc_id GROUP BY 1 ORDER BY 1");
    printf("decade  sittings       words  known to the lexicon\n");
    while(dbStep(db, stmt) == SQLITE_ROW) {
        long words = sqlite3_column_int64(stmt, 2), known = sqlite3_column_int64(stmt, 3);
        printf("%d    %8d  %10ld  %5.1f%%\n", sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1),
               words, words ? 100.0 * known / words : 0.0);
    }
    sqlite3_finalize(stmt);
    stmt = dbPrepare(db, "SELECT origin, COUNT(*) FROM origin GROUP BY origin ORDER BY 2 DESC");
    printf("\norigins of the dictionary words (from Wiktionary etymologies)\n");
    while(dbStep(db, stmt) == SQLITE_ROW) printf("  %-16s %6d\n", sqlite3_column_text(stmt, 0), sqlite3_column_int(stmt, 1));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
}

// ---------------------------------------------------------------- fit

static int compareCrossovers(const void *a, const void *b) {
    const ConceptStudy *x = a, *y = b;
    double cx = isnan(x->fit.crossover) ? 1e9 : x->fit.crossover, cy = isnan(y->fit.crossover) ? 1e9 : y->fit.crossover;
    return (cx > cy) - (cx < cy);
}

// printf pads by bytes, but ç ğ ı ö ş ü take two: pad by letters instead.
static void printPadded(const char *s, int width) {
    int letters = 0;
    for(const char *p = s; *p; p++) letters += ((unsigned char)*p & 0xC0) != 0x80;
    printf("%s%*s", s, width > letters ? width - letters : 0, "");
}

// The S-curve of every concept, sorted by the year today's word took over.
static int cmdFit(char **argv) {
    sqlite3 *db = dbOpen(argv[0]);
    Study *s = loadStudy(db, argv[1], 300);
    if(!s) {
        perror(argv[1]);
        return 1;
    }
    FILE *out = fopen(argv[2], "w");
    if(!out) {
        perror(argv[2]);
        return 1;
    }
    qsort(s->concepts, s->nConcepts, sizeof *s->concepts, compareCrossovers);
    fprintf(out, "concept\tgroup\twords\tcrossover\tcross_lo\tcross_hi\tcross_share\tduration\tceiling\tceiling_lo\tceiling_hi\tuses\tstatus\n");
    printPadded("words", 32);
    printPadded("group", 8);
    printf("%22s %9s %8s %9s  %s\n", "crossover (95%)", "10→90%", "ceiling", "uses", "status");
    for(int i = 0; i < s->nConcepts; i++) {
        const ConceptStudy *c = &s->concepts[i];
        const Fit *f = &c->fit;
        char cross[48] = "-", range[48] = "";
        if(!isnan(f->crossover)) snprintf(cross, sizeof cross, "%.0f", f->crossover);
        if(!isnan(f->crossLo)) snprintf(range, sizeof range, "(%.0f–%.0f)", f->crossLo, f->crossHi);
        char duration[16] = "-";
        if(!isnan(f->crossover)) snprintf(duration, sizeof duration, "%.0f y", f->duration);
        printPadded(c->label, 32);
        printPadded(c->group, 8);
        printf("%8s %13s %9s %7.0f%% %9ld  %s%s\n", cross, range, duration, 100 * f->ceiling, f->uses,
               statusName(fitStatus(f)), f->uncertain ? " (belirsiz)" : "");
        fprintf(out, "%s\t%s\t%s\t%.1f\t%.1f\t%.1f\t%.2f\t%.1f\t%.3f\t%.3f\t%.3f\t%ld\t%s\n", c->concept, c->group, c->label,
                f->crossover, f->crossLo, f->crossHi, f->crossShare, f->duration, f->ceiling, f->ceilingLo, f->ceilingHi,
                f->uses, statusName(fitStatus(f)));
    }
    fclose(out);
    // Did some kinds of words change earlier? Median crossover year per theme, each theme
    // against all the others with a permutation test.
    printf("\nmedian crossover year per theme (against all other themes):\n");
    for(int i = 0; i < s->nConcepts; i++) {
        bool first = true;
        for(int j = 0; j < i; j++) first = first && strcmp(s->concepts[j].group, s->concepts[i].group) != 0;
        if(!first) continue;
        double in[256], rest[256], sorted[256];
        int nIn = 0, nRest = 0;
        for(int j = 0; j < s->nConcepts; j++) {
            FitStatus st = fitStatus(&s->concepts[j].fit);
            if(st != STATUS_REPLACED && st != STATUS_PARTIAL) continue;
            if(strcmp(s->concepts[j].group, s->concepts[i].group) == 0) in[nIn++] = s->concepts[j].fit.crossover;
            else rest[nRest++] = s->concepts[j].fit.crossover;
        }
        if(!nIn || !nRest) continue;
        memcpy(sorted, in, nIn * sizeof *in);
        double mIn = median(sorted, nIn);
        memcpy(sorted, rest, nRest * sizeof *rest);
        double mRest = median(sorted, nRest);
        double p = permutationTest(in, nIn, rest, nRest, 20000, 7);
        printf("  ");
        printPadded(s->concepts[i].group, 8);
        printf("%.0f  (%2d meanings)   others %.0f   difference %+5.1f years   p = %.3f%s\n", mIn, nIn, mRest,
               mIn - mRest, p, p < 0.05 ? "  *" : "");
    }
    printf("  p: share of 20000 random reshuffles of the theme labels with at least as large a difference.\n");

    // The steps back after 1952, the largest first.
    int order[256], nRev = 0;
    for(int i = 0; i < s->nConcepts && nRev < 256; i++) if(s->concepts[i].reversal.found) order[nRev++] = i;
    for(int i = 1; i < nRev; i++) {   // insertion sort by the size of the drop
        for(int j = i; j > 0; j--) {
            const Reversal *a = &s->concepts[order[j]].reversal, *b = &s->concepts[order[j - 1]].reversal;
            if(a->before - a->after <= b->before - b->after) break;
            int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }
    }
    printf("\nreversals: today's word lost at least %.0f points after 1952 (%d of %d meanings)\n",
           100 * REVERSAL_MIN_DROP, nRev, s->nConcepts);
    for(int k = 0; k < nRev; k++) {
        const ConceptStudy *c = &s->concepts[order[k]];
        const Reversal *r = &c->reversal;
        printf("  ");
        printPadded(c->label, 34);
        printf("%3.0f%% (%d) -> %3.0f%% (%d)", 100 * r->before, r->beforeYear, 100 * r->after, r->lowYear);
        if(r->backYear) printf("   back in %d\n", r->backYear);
        else printf("   never back\n");
    }
    printf("\ncrossover: the year today's word passed half of the uses; 10→90%%: how long the change took;\n"
           "ceiling: the share it levels off at. Intervals from %d bootstrap rounds over sittings. Written to %s\n", 300, argv[2]);
    freeStudy(s);
    sqlite3_close(db);
    return 0;
}

// ---------------------------------------------------------------- origins

// Origin of nouns and adjectives per 5 years: how much of the vocabulary in use was
// Arabic, Persian, Turkish or Western.
static int cmdOrigins(char **argv) {
    sqlite3 *db = dbOpen(argv[0]);
    buildFormOrigins(db);
    static OriginByYear o;
    originsByYear(db, &o);
    FILE *out = fopen(argv[1], "w");
    if(!out) {
        perror(argv[1]);
        return 1;
    }
    fprintf(out, "year,arabic,persian,turkish,western,other,unknown\n");
    for(int y = 0; y < NYEARS; y++) {
        long sum = 0;
        for(int g = 0; g < ORIGIN_COUNT; g++) sum += o.n[y][g];
        if(!sum) continue;
        fprintf(out, "%d", FIRST_YEAR + y);
        for(int g = 0; g < ORIGIN_COUNT; g++) fprintf(out, ",%ld", o.n[y][g]);
        fprintf(out, ",%ld\n", o.unknown[y]);
    }
    fclose(out);
    printf("period      Arapça  Farsça  Türkçe  Batı   diğer   (nouns and adjectives)\n");
    for(int from = 1920; from <= 2025; from += 5) {
        long g[ORIGIN_COUNT] = {0}, sum = 0;
        for(int y = from; y < from + 5; y++) {
            for(int k = 0; k < ORIGIN_COUNT; k++) {
                g[k] += o.n[y - FIRST_YEAR][k];
                sum += o.n[y - FIRST_YEAR][k];
            }
        }
        if(!sum) continue;
        printf("%d–%d", from, from + 4);
        for(int k = 0; k < ORIGIN_COUNT; k++) printf("  %5.1f%%", 100.0 * g[k] / sum);
        printf("   %ld words\n", sum);
    }
    // The most frequent words in "diğer": where the classification can still improve.
    sqlite3_stmt *stmt = dbPrepare(db,
        "SELECT o.lemma, o.origin, SUM(f.n) AS n FROM form_count f JOIN form_origin o ON o.form = f.form "
        "WHERE o.origin NOT IN ('arabic', 'persian', 'turkic', 'turkish-derived', 'french', 'italian', 'english', 'german', 'latin') "
        "GROUP BY o.lemma ORDER BY n DESC LIMIT 24");
    printf("\nmost frequent words counted as \"diğer\" (origin label in brackets):\n");
    int column = 0;
    while(dbStep(db, stmt) == SQLITE_ROW) {
        printf("  %-16s %-10s %7d%s", sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1), sqlite3_column_int(stmt, 2),
               ++column % 3 ? "" : "\n");
    }
    if(column % 3) printf("\n");
    sqlite3_finalize(stmt);
    printf("yearly counts written to %s\n", argv[1]);
    sqlite3_close(db);
    return 0;
}

// ---------------------------------------------------------------- eras

static int cmdEras(int argc, char **argv) {
    sqlite3 *db = dbOpen(argv[0]);
    int top = argc > 5 ? atoi(argv[5]) : 30;
    if(top > 64) top = 64;
    buildFormOrigins(db);
    static EraWord a[64], b[64];
    int nA, nB;
    eraWords(db, atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), top, a, &nA, b, &nB);
    printf("%-34s %-34s\n", "typical of the first period", "typical of the second period");
    for(int i = 0; i < nA || i < nB; i++) {
        char left[160] = "", right[160] = "";
        if(i < nA) snprintf(left, sizeof left, "%.63s (%s, z=%.0f)", a[i].word, originGroupName(a[i].origin), a[i].z);
        if(i < nB) snprintf(right, sizeof right, "%.63s (%s, z=%.0f)", b[i].word, originGroupName(b[i].origin), -b[i].z);
        printf("%-34s %-34s\n", left, right);
    }
    sqlite3_close(db);
    return 0;
}

// ---------------------------------------------------------------- unknown

// The most frequent words of some years that the lexicon cannot trace to a dictionary
// word. They are names, OCR errors, words split by a missed hyphen, or old words that
// Wiktionary lacks; the last kind is worth adding by hand.
static int cmdUnknown(int argc, char **argv) {
    sqlite3 *db = dbOpen(argv[0]);
    int from = atoi(argv[1]), to = atoi(argv[2]);
    int limit = argc > 3 ? atoi(argv[3]) : 50;
    sqlite3_stmt *stmt = dbPrepare(db,
        "SELECT f.form, SUM(f.n) AS total FROM form_count f JOIN doc d ON d.id = f.doc_id "
        "WHERE d.year BETWEEN ?1 AND ?2 AND NOT EXISTS (SELECT 1 FROM lexicon l WHERE l.form = f.form) "
        "GROUP BY f.form ORDER BY total DESC LIMIT ?3");
    sqlite3_bind_int(stmt, 1, from);
    sqlite3_bind_int(stmt, 2, to);
    sqlite3_bind_int(stmt, 3, limit);
    int column = 0;
    while(dbStep(db, stmt) == SQLITE_ROW) {
        printf("%-22s %6d%s", sqlite3_column_text(stmt, 0), sqlite3_column_int(stmt, 1), ++column % 3 ? "   " : "\n");
    }
    if(column % 3) printf("\n");
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
}

// ---------------------------------------------------------------- clean

static int cmdClean(char **argv) {
    char *raw = readFile(argv[0]);
    if(!raw) {
        perror(argv[0]);
        return 1;
    }
    const char *base = strrchr(argv[0], '/');
    SittingId id;
    Date date;
    if(parseSittingId(base ? base + 1 : argv[0], &id) && parseRecordDate(raw, &id, &date)) {
        fprintf(stderr, "date: %04d-%02d-%02d\n", date.year, date.month, date.day);
    } else {
        fprintf(stderr, "date: not found\n");
    }
    CleanStats st;
    char *text = cleanTranscript(raw, &st);
    fputs(text, stdout);
    fprintf(stderr, "%d lines in: %d before the proceedings, %d after them, %d headings, %d page furniture; "
            "%d speaker labels removed; %d lines kept\n",
            st.linesIn, st.frontMatter, st.backMatter, st.headings, st.furniture, st.labels, st.linesOut);
    free(text);
    free(raw);
    return 0;
}

// ---------------------------------------------------------------- dating

typedef struct {
    int form, n;
    double weight;
} Clue;

static int compareClues(const void *a, const void *b) {
    double x = ((const Clue *)a)->weight, y = ((const Clue *)b)->weight;
    return (x < y) - (x > y);
}

static int cmdDate(char **argv) {
    sqlite3 *db = dbOpen(argv[0]);
    char *text = readFile(argv[1]);
    if(!text) {
        perror(argv[1]);
        return 1;
    }
    DatingModel m;
    loadDatingModel(db, &m);
    FormUse *uses;
    int count = textUses(&m, text, &uses);
    double logLik[DATE_BINS];
    int best = dateUses(&m, uses, count, logLik);
    if(best < 0) {
        printf("no words the model knows\n");
        return 1;
    }
    // How much more likely each period is than the best one, as a ratio (1 = as likely).
    printf("most likely %d-%d\n\n", DATE_FIRST + DATE_STEP * best, DATE_FIRST + DATE_STEP * best + DATE_STEP - 1);
    for(int b = 0; b < DATE_BINS; b++) {
        if(isinf(logLik[b])) continue;
        double diff = logLik[b] - logLik[best];
        int bar = diff > -20 ? (int)(40 + 2 * diff) : 0;
        printf("%d-%d  %7.1f  %.*s\n", DATE_FIRST + DATE_STEP * b, DATE_FIRST + DATE_STEP * b + DATE_STEP - 1, diff,
               bar, "########################################");
    }
    // The words that pull towards the guessed period most: how much likelier there than on average.
    Clue *clues = malloc((count + 1) * sizeof *clues);
    int nClues = 0;
    for(int i = 0; i < count; i++) {
        if(uses[i].form < 0) continue;
        double year, spread;
        formYear(&m, uses[i].form, &year, &spread);
        double here = (double)m.forms[uses[i].form].n[best] / m.tokens[best];
        double overall = (double)m.forms[uses[i].form].total / m.total;
        clues[nClues++] = (Clue){uses[i].form, uses[i].n, uses[i].n * log((here + 1e-7) / (overall + 1e-7))};
    }
    qsort(clues, nClues, sizeof *clues, compareClues);
    printf("\nwords typical of %d-%d (typical year):\n", DATE_FIRST + DATE_STEP * best, DATE_FIRST + DATE_STEP * best + DATE_STEP - 1);
    for(int i = 0; i < nClues && i < 15 && clues[i].weight > 0; i++) {
        double year, spread;
        formYear(&m, clues[i].form, &year, &spread);
        printf("  %-20s %.0f\n", m.forms[clues[i].form].form, year);
    }
    free(clues);
    free(uses);
    free(text);
    freeDatingModel(&m);
    sqlite3_close(db);
    return 0;
}

static int cmdDateTest(char **argv) {
    sqlite3 *db = dbOpen(argv[0]);
    DatingModel m;
    loadDatingModel(db, &m);
    DatingTest t;
    testDating(db, &m, 7, &t);
    printf("%d sittings, each dated with its volume left out of the model\n\n", t.nSittings);
    printf("words      tested  mean error  within 5 years\n");
    for(int l = 0; l < DATE_LENGTHS; l++) {
        if(t.lengths[l]) printf("%-10d", t.lengths[l]);
        else printf("%-10s", "all");
        printf(" %6d  %7.1f y  %9.0f%%\n", t.tested[l], t.meanError[l], 100 * t.within5[l]);
    }
    printf("\nwithout reading (always the median year): %.1f years off on average\n", t.chanceError);
    freeDatingTest(&t);
    freeDatingModel(&m);
    sqlite3_close(db);
    return 0;
}

// ---------------------------------------------------------------- web

// JSON output: strings escaped, NAN written as null.
static void jsonString(FILE *f, const char *s) {
    fputc('"', f);
    for(const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if(*p == '"' || *p == '\\') fprintf(f, "\\%c", *p);
        else if(*p < 0x20) fputc(' ', f);
        else fputc(*p, f);
    }
    fputc('"', f);
}

static void jsonNumber(FILE *f, double v, int decimals) {
    if(isnan(v) || isinf(v)) fputs("null", f);
    else fprintf(f, "%.*f", decimals, v);
}

#define WEB_FIRST 1920
#define WEB_LAST 2023

static void jsonYears(FILE *f, const long *perYear) {   // perYear indexed by year - FIRST_YEAR
    fputc('[', f);
    for(int y = WEB_FIRST; y <= WEB_LAST; y++) fprintf(f, "%s%ld", y > WEB_FIRST ? "," : "", perYear[y - FIRST_YEAR]);
    fputc(']', f);
}

static void jsonIntYears(FILE *f, const int *perYear) {
    fputc('[', f);
    for(int y = WEB_FIRST; y <= WEB_LAST; y++) fprintf(f, "%s%d", y > WEB_FIRST ? "," : "", perYear[y - FIRST_YEAR]);
    fputc(']', f);
}

static void jsonEraWords(FILE *f, const EraWord *w, int n) {
    fputc('[', f);
    for(int i = 0; i < n; i++) {
        fprintf(f, "%s{\"w\":", i ? "," : "");
        jsonString(f, w[i].word);
        fprintf(f, ",\"o\":%d,\"z\":%.1f}", w[i].origin, w[i].z);
    }
    fputc(']', f);
}

// Example sentences: for each competing word, a few real uses from the transcripts, from
// the years the word belongs to (an old word before 1935, today's word from 2000 on).
#define EXAMPLES 3
typedef struct {
    int year;
    char id[64];
    char *before, *word, *after;
} Example;

typedef struct {
    Example own[EXAMPLES], any[EXAMPLES];
    long seenOwn, seenAny;
} ExamplePool;

static void freeExample(Example *e) {
    free(e->before);
    free(e->word);
    free(e->after);
    memset(e, 0, sizeof *e);
}

// A readable snippet: mostly letters and spaces (not a table of numbers or OCR noise).
static bool readable(const char *s) {
    int letters = 0, total = 0;
    for(const char *p = s; *p;) {
        uint32_t cp;
        p += utf8Decode(p, &cp);
        total++;
        letters += trIsLetter(cp) || cp == ' ' || cp == ',' || cp == '.';
    }
    return total >= 60 && letters * 10 >= total * 9;
}

// Up to max bytes of s ending at a space, from the end (fromEnd) or from the start.
static char *clip(const char *s, int len, int max, bool fromEnd) {
    if(len > max) {
        if(fromEnd) {
            const char *start = s + len - max;
            while(*start && *start != ' ') start++;
            len -= (int)(start - s);
            s = start;
        } else {
            int cut = max;
            while(cut > 0 && s[cut] != ' ') cut--;
            len = cut;
        }
    }
    char *out = malloc(len + 1);
    memcpy(out, s, len);
    out[len] = 0;
    return out;
}

static void collectExamples(sqlite3 *db, const char *rawDir, const Study *s, ExamplePool *pools) {
    // Every form of every competing word, mapped to its slot (concept * MAX_COMPETITORS + word).
    Counter forms = {0};
    sqlite3_stmt *sel = dbPrepare(db, "SELECT form FROM lexicon WHERE lemma = ?1 UNION SELECT ?1");
    for(int c = 0; c < s->nConcepts; c++) {
        for(int w = 0; w < s->concepts[c].nWords; w++) {
            sqlite3_bind_text(sel, 1, s->concepts[c].words[w], -1, SQLITE_TRANSIENT);
            while(dbStep(db, sel) == SQLITE_ROW) {
                const char *form = (const char *)sqlite3_column_text(sel, 0);
                counterAdd(&forms, form);
                findSlot(&forms, form)->n = c * MAX_COMPETITORS + w;
            }
            sqlite3_reset(sel);
        }
    }
    sqlite3_finalize(sel);

    int count;
    char **names = listTranscripts(rawDir, &count);
    for(int i = 0; i < count; i++) {
        Transcript t;
        if(!loadTranscript(rawDir, names[i], &t, false)) {
            free(names[i]);
            continue;
        }
        for(int l = 0; l < t.lineCount; l++) {
            Tokenizer tok;
            tokInit(&tok, t.lines[l]);
            char word[MAX_WORD];
            int start, len;
            while(tokNext(&tok, word, &start, &len)) {
                Slot *slot = findSlot(&forms, word);
                if(!slot->word) continue;
                int c = (int)slot->n / MAX_COMPETITORS, w = (int)slot->n % MAX_COMPETITORS;
                const ConceptStudy *cs = &s->concepts[c];
                bool isNew = strcmp(cs->roles[w], "new") == 0;
                bool own = isNew ? t.date.year >= 2000 : t.date.year < 1935;
                // The line with its neighbours: in two-column print a line is only ~6 words.
                const char *prev = l > 0 ? t.lines[l - 1] : "", *next = l + 1 < t.lineCount ? t.lines[l + 1] : "";
                char before[1024], after[1024];
                snprintf(before, sizeof before, "%s %.*s", prev, start, t.lines[l]);
                snprintf(after, sizeof after, "%s %s", t.lines[l] + start + len, next);
                char whole[2200];
                snprintf(whole, sizeof whole, "%s%.*s%s", before, len, t.lines[l] + start, after);
                if(!readable(whole)) continue;
                ExamplePool *pool = &pools[slot->n];
                for(int k = 0; k < 2; k++) {
                    if(k == 0 && !own) continue;
                    Example *set = k == 0 ? pool->own : pool->any;
                    long *counter = k == 0 ? &pool->seenOwn : &pool->seenAny;
                    long seen = ++*counter;
                    long at = seen <= EXAMPLES ? seen - 1 : (long)(nextRandom() % seen);   // reservoir sampling
                    if(at >= EXAMPLES) continue;
                    Example *e = &set[at];
                    freeExample(e);
                    e->year = t.date.year;
                    snprintf(e->id, sizeof e->id, "%s", t.id);
                    e->before = clip(before, (int)strlen(before), 110, true);
                    e->word = clip(t.lines[l] + start, len, len, false);
                    e->after = clip(after, (int)strlen(after), 110, false);
                }
            }
        }
        freeTranscript(&t);
        free(names[i]);
    }
    free(names);
    counterFree(&forms);
}

static void jsonExamples(FILE *f, ExamplePool *pool) {
    // Examples from the word's own years first; others only when there are none.
    Example *set = pool->seenOwn ? pool->own : pool->any;
    long seen = pool->seenOwn ? pool->seenOwn : pool->seenAny;
    int n = seen < EXAMPLES ? (int)seen : EXAMPLES;
    fputc('[', f);
    for(int i = 0; i < n; i++) {
        fprintf(f, "%s{\"y\":%d,\"id\":", i ? "," : "", set[i].year);
        jsonString(f, set[i].id);
        fputs(",\"b\":", f);
        jsonString(f, set[i].before);
        fputs(",\"w\":", f);
        jsonString(f, set[i].word);
        fputs(",\"a\":", f);
        jsonString(f, set[i].after);
        fputc('}', f);
    }
    fputc(']', f);
    for(int i = 0; i < EXAMPLES; i++) {
        freeExample(&pool->own[i]);
        freeExample(&pool->any[i]);
    }
}

static void jsonDating(FILE *f, const DatingModel *m, const DatingTest *t) {
    fprintf(f, ",\"dating\":{\"first\":%d,\"step\":%d,\"prior\":%g,\"minTokens\":%d,\"tok\":[",
            DATE_FIRST, DATE_STEP, DATE_PRIOR, DATE_MIN_TOKENS);
    for(int b = 0; b < DATE_BINS; b++) fprintf(f, "%s%ld", b ? "," : "", m->tokens[b]);
    fputs("],\"f\":[", f);
    for(int i = 0; i < m->count; i++) {
        if(i) fputc(',', f);
        jsonString(f, m->forms[i].form);
    }
    // Counts per period, trailing zeros left out.
    fputs("],\"c\":[", f);
    for(int i = 0; i < m->count; i++) {
        int last = DATE_BINS - 1;
        while(last > 0 && !m->forms[i].n[last]) last--;
        fputs(i ? ",[" : "[", f);
        for(int b = 0; b <= last; b++) fprintf(f, "%s%ld", b ? "," : "", m->forms[i].n[b]);
        fputc(']', f);
    }
    fputs("],\"test\":{\"len\":[", f);
    for(int l = 0; l < DATE_LENGTHS; l++) fprintf(f, "%s%d", l ? "," : "", t->lengths[l]);
    fputs("],\"n\":[", f);
    for(int l = 0; l < DATE_LENGTHS; l++) fprintf(f, "%s%d", l ? "," : "", t->tested[l]);
    fputs("],\"err\":[", f);
    for(int l = 0; l < DATE_LENGTHS; l++) {
        if(l) fputc(',', f);
        jsonNumber(f, t->meanError[l], 1);
    }
    fputs("],\"w5\":[", f);
    for(int l = 0; l < DATE_LENGTHS; l++) {
        if(l) fputc(',', f);
        jsonNumber(f, t->within5[l], 3);
    }
    fputs("],\"chance\":", f);
    jsonNumber(f, t->chanceError, 1);
    fputs(",\"year\":[", f);
    for(int i = 0; i < t->nSittings; i++) fprintf(f, "%s%d", i ? "," : "", t->year[i]);
    fputs("],\"guess\":[", f);
    for(int i = 0; i < t->nSittings; i++) fprintf(f, "%s%d", i ? "," : "", t->guess[i]);
    fputs("]}}", f);
}

// Everything the web page shows, as one JSON object.
static void writeWebData(FILE *f, sqlite3 *db, const Study *s, ExamplePool *pools) {
    fprintf(f, "{\"first\":%d,\"last\":%d,\"sittings\":%d,\"words\":%ld,", WEB_FIRST, WEB_LAST, s->sittings, s->words);
    fputs("\"tokens\":", f);
    jsonYears(f, s->tokens.n);

    long docs[NYEARS] = {0};
    sqlite3_stmt *stmt = dbPrepare(db, "SELECT year, COUNT(*) FROM doc GROUP BY year");
    while(dbStep(db, stmt) == SQLITE_ROW) {
        int y = sqlite3_column_int(stmt, 0) - FIRST_YEAR;
        if(y >= 0 && y < NYEARS) docs[y] = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);
    fputs(",\"docs\":", f);
    jsonYears(f, docs);

    fputs(",\"origins\":[", f);
    for(int y = WEB_FIRST; y <= WEB_LAST; y++) {
        const long *n = s->origins.n[y - FIRST_YEAR];
        fprintf(f, "%s[%ld,%ld,%ld,%ld,%ld]", y > WEB_FIRST ? "," : "", n[0], n[1], n[2], n[3], n[4]);
    }
    fputs("],\"concepts\":[", f);
    for(int c = 0; c < s->nConcepts; c++) {
        const ConceptStudy *cs = &s->concepts[c];
        const Fit *fit = &cs->fit;
        fprintf(f, "%s{\"id\":", c ? "," : "");
        jsonString(f, cs->concept);
        fputs(",\"group\":", f);
        jsonString(f, cs->group);
        fputs(",\"words\":[", f);
        for(int w = 0; w < cs->nWords; w++) {
            fprintf(f, "%s{\"w\":", w ? "," : "");
            jsonString(f, cs->words[w]);
            fputs(",\"role\":", f);
            jsonString(f, cs->roles[w]);
            fputs(",\"n\":", f);
            jsonYears(f, cs->counts[w].n);
            fputs(",\"ex\":", f);
            jsonExamples(f, &pools[c * MAX_COMPETITORS + w]);
            fputc('}', f);
        }
        fputs("],\"newUses\":", f);
        jsonYears(f, cs->uses.newUses);
        fputs(",\"allUses\":", f);
        jsonYears(f, cs->uses.allUses);
        fprintf(f, ",\"fit\":{\"ok\":%s,\"mid\":", fit->ok ? "true" : "false");
        jsonNumber(f, fit->mid, 3);
        fputs(",\"rate\":", f);
        jsonNumber(f, fit->rate, 5);
        fputs(",\"ceiling\":", f);
        jsonNumber(f, fit->ceiling, 4);
        fputs(",\"crossover\":", f);
        jsonNumber(f, fit->crossover, 2);
        fputs(",\"lo\":", f);
        jsonNumber(f, fit->crossLo, 2);
        fputs(",\"hi\":", f);
        jsonNumber(f, fit->crossHi, 2);
        fputs(",\"duration\":", f);
        jsonNumber(f, fit->duration, 1);
        fprintf(f, ",\"uses\":%ld,\"uncertain\":%s,\"status\":%d}}", fit->uses, fit->uncertain ? "true" : "false", fitStatus(fit));
    }

    // Themes: median crossover year of each, against all the others (as evo fit prints).
    fputs("],\"themes\":[", f);
    int written = 0;
    for(int i = 0; i < s->nConcepts; i++) {
        bool first = true;
        for(int j = 0; j < i; j++) first = first && strcmp(s->concepts[j].group, s->concepts[i].group) != 0;
        if(!first) continue;
        double in[256], rest[256], sorted[256];
        int nIn = 0, nRest = 0;
        for(int j = 0; j < s->nConcepts; j++) {
            FitStatus st = fitStatus(&s->concepts[j].fit);
            if(st != STATUS_REPLACED && st != STATUS_PARTIAL) continue;
            if(strcmp(s->concepts[j].group, s->concepts[i].group) == 0) in[nIn++] = s->concepts[j].fit.crossover;
            else rest[nRest++] = s->concepts[j].fit.crossover;
        }
        if(!nIn || !nRest) continue;
        memcpy(sorted, in, nIn * sizeof *in);
        double mIn = median(sorted, nIn);
        memcpy(sorted, rest, nRest * sizeof *rest);
        double mRest = median(sorted, nRest);
        fprintf(f, "%s{\"group\":", written++ ? "," : "");
        jsonString(f, s->concepts[i].group);
        fprintf(f, ",\"median\":%.1f,\"others\":%.1f,\"n\":%d,\"p\":%.4f}", mIn, mRest, nIn,
                permutationTest(in, nIn, rest, nRest, 20000, 7));
    }

    // The vocabulary: every noun and adjective used 30 times or more, uses per year.
    fputs("],\"vocab\":{\"w\":[", f);
    for(int i = 0; i < s->vocab.count; i++) {
        if(i) fputc(',', f);
        jsonString(f, s->vocab.words[i].word);
    }
    fputs("],\"o\":[", f);
    for(int i = 0; i < s->vocab.count; i++) fprintf(f, "%s%d", i ? "," : "", s->vocab.words[i].origin);
    fputs("],\"n\":[", f);
    for(int i = 0; i < s->vocab.count; i++) {
        if(i) fputc(',', f);
        jsonIntYears(f, s->vocab.words[i].n);
    }
    // For the search: every form the lexicon gives each word, as the viewer's search and
    // the concepts count (efendim is an interjection, so the origin counts above skip it).
    fputs("],\"s\":[", f);
    for(int i = 0; i < s->vocab.count; i++) {
        YearCounts counts;
        countByYear(db, s->vocab.words[i].word, &counts);
        if(i) fputc(',', f);
        jsonYears(f, counts.n);
    }
    fputs("]},\"decades\":[", f);
    for(int d = 0; d < DECADES; d++) {
        if(d) fputc(',', f);
        jsonEraWords(f, s->decades[d], s->nDecade[d]);
    }
    fprintf(f, "],\"eras\":{\"a\":[%d,%d],\"b\":[%d,%d],\"early\":", s->eraA[0], s->eraA[1], s->eraB[0], WEB_LAST);
    jsonEraWords(f, s->early, s->nEarly < 18 ? s->nEarly : 18);
    fputs(",\"late\":", f);
    jsonEraWords(f, s->late, s->nLate < 18 ? s->nLate : 18);

    const Survival *sv = &s->survival;
    fprintf(f, "},\"survival\":{\"base\":%d,\"top\":%d,\"n\":[", sv->base, sv->top);
    for(int g = 0; g <= ORIGIN_COUNT; g++) fprintf(f, "%s%d", g ? "," : "", sv->nWords[g]);
    fputs("],\"alive\":[", f);
    for(int g = 0; g <= ORIGIN_COUNT; g++) {
        fputs(g ? ",[" : "[", f);
        for(int d = 0; d < DECADES; d++) {
            if(d) fputc(',', f);
            jsonNumber(f, sv->alive[g][d], 3);
        }
        fputc(']', f);
    }
    fputs("],\"lost\":[", f);
    for(int i = 0; i < sv->nLost; i++) {
        if(i) fputc(',', f);
        jsonString(f, s->vocab.words[sv->lost[i]].word);
    }
    fputs("],\"kept\":[", f);
    for(int i = 0; i < sv->nKept; i++) {
        if(i) fputc(',', f);
        jsonString(f, s->vocab.words[sv->kept[i]].word);
    }
    fputs("]}", f);
    jsonDating(f, &s->dating, &s->datingTest);
    fputc('}', f);
}

// Writes the page twice: index.html to open from disk (with its own <head>), and
// artifact.html without it, for hosts that add their own.
static int cmdWeb(char **argv) {
    sqlite3 *db = dbOpen(argv[0]);
    Study *s = loadStudy(db, argv[1], 300);
    if(!s) {
        perror(argv[1]);
        return 1;
    }
    char *page = readFile(argv[3]);
    char *slot = page ? strstr(page, "__DATA__") : NULL;
    if(!slot) {
        fprintf(stderr, "%s: no __DATA__ placeholder\n", argv[3]);
        return 1;
    }
    ExamplePool *pools = calloc(s->nConcepts * MAX_COMPETITORS, sizeof *pools);
    rngState = 20240101;
    collectExamples(db, argv[2], s, pools);
    char path[4096];
    snprintf(path, sizeof path, "%s/data.json", argv[4]);
    FILE *f = fopen(path, "w");
    if(!f) {
        perror(path);
        return 1;
    }
    writeWebData(f, db, s, pools);
    fclose(f);
    char *data = readFile(path);
    for(int bare = 0; bare < 2; bare++) {
        snprintf(path, sizeof path, "%s/%s", argv[4], bare ? "artifact.html" : "index.html");
        f = fopen(path, "w");
        if(!f) {
            perror(path);
            return 1;
        }
        if(!bare) fputs("<!doctype html>\n<html lang=\"tr\">\n<head>\n<meta charset=\"utf-8\">\n"
                        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, viewport-fit=cover\">\n</head>\n<body>\n", f);
        fwrite(page, 1, slot - page, f);
        fputs(data, f);
        fputs(slot + strlen("__DATA__"), f);
        if(!bare) fputs("\n</body>\n</html>\n", f);
        fclose(f);
        printf("wrote %s\n", path);
    }
    free(data);
    free(page);
    free(pools);
    freeStudy(s);
    sqlite3_close(db);
    return 0;
}

int main(int argc, char **argv) {
    if(argc < 2) return usage();
    const char *cmd = argv[1];
    if(strcmp(cmd, "ingest") == 0 && argc == 4) return cmdIngest(argv + 2);
    if(strcmp(cmd, "lexicon") == 0 && argc == 5) return cmdLexicon(argv + 2);
    if(strcmp(cmd, "share") == 0 && argc == 5) return cmdShare(argv + 2);
    if(strcmp(cmd, "kwic") == 0 && argc >= 5 && argc <= 7) return cmdKwic(argc - 2, argv + 2);
    if(strcmp(cmd, "fit") == 0 && argc == 5) return cmdFit(argv + 2);
    if(strcmp(cmd, "origins") == 0 && argc == 4) return cmdOrigins(argv + 2);
    if(strcmp(cmd, "eras") == 0 && argc >= 7 && argc <= 8) return cmdEras(argc - 2, argv + 2);
    if(strcmp(cmd, "stats") == 0 && argc == 3) return cmdStats(argv + 2);
    if(strcmp(cmd, "unknown") == 0 && argc >= 5 && argc <= 6) return cmdUnknown(argc - 2, argv + 2);
    if(strcmp(cmd, "date") == 0 && argc == 4) return cmdDate(argv + 2);
    if(strcmp(cmd, "datetest") == 0 && argc == 3) return cmdDateTest(argv + 2);
    if(strcmp(cmd, "clean") == 0 && argc == 3) return cmdClean(argv + 2);
    if(strcmp(cmd, "web") == 0 && argc == 7) return cmdWeb(argv + 2);
    return usage();
}
