#include "query.h"
#include "inflect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool loadConcepts(const char *path, ConceptList *out) {
    FILE *f = fopen(path, "r");
    if(!f) return false;
    out->items = NULL;
    out->count = 0;
    int cap = 0;
    char line[512];
    while(fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if(line[0] == '#' || !line[0]) continue;
        char *concept = strtok(line, "\t");
        char *role = strtok(NULL, "\t");
        char *lemma = strtok(NULL, "\t");
        char *group = strtok(NULL, "\t");
        if(!concept || !role || !lemma) {
            fprintf(stderr, "%s: skipping a line without three tab-separated columns\n", path);
            continue;
        }
        if(out->count == cap) {
            cap = cap ? cap * 2 : 32;
            out->items = realloc(out->items, cap * sizeof *out->items);
        }
        Competitor *c = &out->items[out->count++];
        snprintf(c->concept, sizeof c->concept, "%s", concept);
        snprintf(c->role, sizeof c->role, "%s", role);
        trFoldString(lemma, c->lemma, sizeof c->lemma);
        snprintf(c->group, sizeof c->group, "%s", group ? group : "genel");
    }
    fclose(f);
    return true;
}

void lemmaOf(sqlite3 *db, const char *word, char *lemma) {
    sqlite3_stmt *stmt = dbPrepare(db,
        "SELECT lemma FROM lexicon WHERE form = ?1 ORDER BY lemma = ?1 DESC, src = 'wiktionary' DESC, lemma LIMIT 1");
    sqlite3_bind_text(stmt, 1, word, -1, SQLITE_TRANSIENT);
    const char *found = dbStep(db, stmt) == SQLITE_ROW ? (const char *)sqlite3_column_text(stmt, 0) : word;
    snprintf(lemma, MAX_WORD, "%s", found);
    sqlite3_finalize(stmt);
}

void resolveConcepts(sqlite3 *db, ConceptList *cl) {
    // Only spelling variants are followed (hükümet → hükûmet). Concept words are nouns and
    // adjectives: görüşme must not become the verb görüşmek, and hudut must not become
    // had ("limit"), of which Wiktionary calls it the plural.
    sqlite3_stmt *stmt = dbPrepare(db,
        "SELECT l.lemma FROM lexicon l WHERE l.form = ?1 AND l.src = 'alt-spelling' AND l.pos IN ('noun', 'adj') "
        "AND EXISTS (SELECT 1 FROM lexicon t WHERE t.form = l.lemma AND t.lemma = l.lemma AND t.pos IN ('noun', 'adj')) "
        "ORDER BY l.lemma = ?1 DESC, l.src = 'wiktionary' DESC, l.lemma LIMIT 1");
    for(int i = 0; i < cl->count; i++) {
        char lemma[MAX_WORD];
        sqlite3_bind_text(stmt, 1, cl->items[i].lemma, -1, SQLITE_TRANSIENT);
        snprintf(lemma, sizeof lemma, "%s", dbStep(db, stmt) == SQLITE_ROW ? (const char *)sqlite3_column_text(stmt, 0) : cl->items[i].lemma);
        sqlite3_reset(stmt);
        if(strcmp(lemma, cl->items[i].lemma) == 0) continue;
        printf("note: %s is counted as the dictionary word %s\n", cl->items[i].lemma, lemma);
        snprintf(cl->items[i].lemma, MAX_WORD, "%s", lemma);
    }
    sqlite3_finalize(stmt);
}

void exclusionsPathFor(const char *conceptsPath, char *out, int size) {
    const char *slash = strrchr(conceptsPath, '/');
    if(slash) snprintf(out, size, "%.*s/exclusions.tsv", (int)(slash - conceptsPath), conceptsPath);
    else snprintf(out, size, "exclusions.tsv");
}

int applyExclusions(sqlite3 *db, const char *path) {
    FILE *f = fopen(path, "r");
    if(!f) return 0;
    sqlite3_stmt *del = dbPrepare(db, "DELETE FROM lexicon WHERE lemma = ?1 AND form = ?2");
    char line[512];
    int n = 0;
    while(fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if(line[0] == '#' || !line[0]) continue;
        char *lemma = strtok(line, "\t"), *form = strtok(NULL, "\t");
        if(!lemma || !form) continue;
        char l[MAX_WORD], fo[MAX_WORD];
        trFoldString(lemma, l, sizeof l);
        trFoldString(form, fo, sizeof fo);
        sqlite3_bind_text(del, 1, l, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(del, 2, fo, -1, SQLITE_TRANSIENT);
        dbStep(db, del);
        sqlite3_reset(del);
        n++;
    }
    sqlite3_finalize(del);
    fclose(f);
    return n;
}

static void fillByYear(sqlite3 *db, sqlite3_stmt *stmt, YearCounts *out) {
    memset(out, 0, sizeof *out);
    while(dbStep(db, stmt) == SQLITE_ROW) {
        int year = sqlite3_column_int(stmt, 0);
        if(year >= FIRST_YEAR && year <= LAST_YEAR) out->n[year - FIRST_YEAR] = sqlite3_column_int64(stmt, 1);
    }
    sqlite3_finalize(stmt);
}

void countByYear(sqlite3 *db, const char *lemma, YearCounts *out) {
    sqlite3_stmt *stmt = dbPrepare(db,
        "SELECT d.year, SUM(f.n) FROM form_count f JOIN doc d ON d.id = f.doc_id "
        "WHERE f.form IN (SELECT form FROM lexicon WHERE lemma = ?1 UNION SELECT ?1) "
        "GROUP BY d.year");
    sqlite3_bind_text(stmt, 1, lemma, -1, SQLITE_TRANSIENT);
    fillByYear(db, stmt, out);
}

void tokensByYear(sqlite3 *db, YearCounts *out) {
    fillByYear(db, dbPrepare(db, "SELECT year, SUM(tokens) FROM doc GROUP BY year"), out);
}

// Is lemma a compound whose last part carries the possessive -(s)I, like milletvekil-i
// (millet + vekil) or cumhurbaşkan-ı? Then its forms are those of the stem with a
// possessive: milletvekilleri, milletvekiline. The stem must end in a dictionary word
// that is shorter than it (vekil, başkan), so plain words like tarih-i are left alone.
static bool compoundStem(sqlite3 *db, const char *lemma, char *stem) {
    int len = (int)strlen(lemma);
    if(len < 6) return false;
    int last = len - 1;
    while(last > 0 && ((unsigned char)lemma[last] & 0xC0) == 0x80) last--;
    uint32_t cp;
    utf8Decode(lemma + last, &cp);
    if(cp != 'i' && cp != 0x131 && cp != 'u' && cp != 0xFC) return false;
    snprintf(stem, MAX_WORD, "%.*s", last, lemma);
    sqlite3_stmt *stmt = dbPrepare(db, "SELECT 1 FROM lexicon WHERE lemma = ?1 AND src = 'wiktionary' LIMIT 1");
    bool found = false;
    // Try every ending of the stem that leaves at least 3 bytes in front of it.
    for(int start = 3; start < last - 2 && !found; start++) {
        if(((unsigned char)stem[start] & 0xC0) == 0x80) continue;
        sqlite3_bind_text(stmt, 1, stem + start, -1, SQLITE_TRANSIENT);
        found = dbStep(db, stmt) == SQLITE_ROW;
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    return found;
}

int ensureParadigm(sqlite3 *db, const char *lemma) {
    static char forms[4096][MAX_WORD];
    int n = nounForms(lemma, forms, 2048);
    char stem[MAX_WORD];
    if(compoundStem(db, lemma, stem)) n += nounForms(stem, forms + n, 2048);
    sqlite3_stmt *other = dbPrepare(db, "SELECT 1 FROM lexicon WHERE form = ?1 AND lemma <> ?2 LIMIT 1");
    sqlite3_stmt *ins = dbPrepare(db, "INSERT OR IGNORE INTO lexicon VALUES (?1, ?2, 'noun', 'generated')");
    dbExec(db, "BEGIN");
    for(int i = 0; i < n; i++) {
        sqlite3_bind_text(other, 1, forms[i], -1, SQLITE_STATIC);
        sqlite3_bind_text(other, 2, lemma, -1, SQLITE_STATIC);
        bool taken = dbStep(db, other) == SQLITE_ROW;
        sqlite3_reset(other);
        if(taken) continue;
        sqlite3_bind_text(ins, 1, forms[i], -1, SQLITE_STATIC);
        sqlite3_bind_text(ins, 2, lemma, -1, SQLITE_STATIC);
        dbStep(db, ins);
        sqlite3_reset(ins);
    }
    dbExec(db, "COMMIT");
    sqlite3_finalize(other);
    sqlite3_finalize(ins);

    sqlite3_stmt *count = dbPrepare(db, "SELECT COUNT(DISTINCT form) FROM lexicon WHERE lemma = ?1");
    sqlite3_bind_text(count, 1, lemma, -1, SQLITE_STATIC);
    dbStep(db, count);
    int total = sqlite3_column_int(count, 0);
    sqlite3_finalize(count);
    return total;
}

void warnAmbiguous(sqlite3 *db, const char *lemma) {
    sqlite3_stmt *stmt = dbPrepare(db,
        "SELECT a.form, group_concat(DISTINCT b.lemma) FROM lexicon a "
        "JOIN lexicon b ON b.form = a.form AND b.lemma <> a.lemma "
        "WHERE a.lemma = ?1 GROUP BY a.form ORDER BY a.form");
    sqlite3_bind_text(stmt, 1, lemma, -1, SQLITE_TRANSIENT);
    int shown = 0, total = 0;
    while(dbStep(db, stmt) == SQLITE_ROW) {
        if(shown < 6) {
            printf("  ambiguous: %s is also a form of %s\n", sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1));
            shown++;
        }
        total++;
    }
    if(total > shown) printf("  ... %d more ambiguous forms of %s\n", total - shown, lemma);
    sqlite3_finalize(stmt);
}
