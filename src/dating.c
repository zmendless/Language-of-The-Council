#include "dating.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int dateBin(int year) {
    if(year < DATE_FIRST || year >= DATE_FIRST + DATE_STEP * DATE_BINS) return -1;
    return (year - DATE_FIRST) / DATE_STEP;
}

int dateBinMiddle(int bin) {
    return DATE_FIRST + DATE_STEP * bin + DATE_STEP / 2;
}

static int compareDateForms(const void *a, const void *b) {
    return strcmp(((const DateForm *)a)->form, ((const DateForm *)b)->form);
}

int dateFormIndex(const DatingModel *m, const char *form) {
    DateForm key;
    snprintf(key.form, sizeof key.form, "%s", form);
    DateForm *f = bsearch(&key, m->forms, m->count, sizeof *m->forms, compareDateForms);
    return f ? (int)(f - m->forms) : -1;
}

void loadDatingModel(sqlite3 *db, DatingModel *m) {
    memset(m, 0, sizeof *m);
    sqlite3_stmt *stmt = dbPrepare(db,
        "SELECT form FROM form_count WHERE form IN (SELECT form FROM lexicon) "
        "GROUP BY form ORDER BY SUM(n) DESC LIMIT ?1");
    sqlite3_bind_int(stmt, 1, DATE_FORMS);
    m->forms = calloc(DATE_FORMS, sizeof *m->forms);
    while(dbStep(db, stmt) == SQLITE_ROW) {
        snprintf(m->forms[m->count++].form, MAX_WORD, "%s", (const char *)sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);
    qsort(m->forms, m->count, sizeof *m->forms, compareDateForms);

    stmt = dbPrepare(db,
        "SELECT d.year, f.form, SUM(f.n) FROM form_count f JOIN doc d ON d.id = f.doc_id GROUP BY d.year, f.form");
    while(dbStep(db, stmt) == SQLITE_ROW) {
        int bin = dateBin(sqlite3_column_int(stmt, 0));
        int i = bin < 0 ? -1 : dateFormIndex(m, (const char *)sqlite3_column_text(stmt, 1));
        if(i < 0) continue;
        long n = sqlite3_column_int64(stmt, 2);
        m->forms[i].n[bin] += n;
        m->forms[i].total += n;
        m->tokens[bin] += n;
        m->total += n;
    }
    sqlite3_finalize(stmt);
}

void freeDatingModel(DatingModel *m) {
    free(m->forms);
    memset(m, 0, sizeof *m);
}

int textUses(const DatingModel *m, const char *text, FormUse **uses) {
    int count = 0, cap = 64;
    *uses = malloc(cap * sizeof **uses);
    Tokenizer tok;
    tokInit(&tok, text);
    char word[MAX_WORD];
    while(tokNext(&tok, word, NULL, NULL)) {
        int form = dateFormIndex(m, word);
        int i = 0;
        while(i < count && (form < 0 || (*uses)[i].form != form)) i++;
        if(form >= 0 && i < count) {
            (*uses)[i].n++;
            continue;
        }
        if(count == cap) *uses = realloc(*uses, (cap *= 2) * sizeof **uses);
        (*uses)[count++] = (FormUse){form, 1};
    }
    return count;
}

// The chance of the form in a period, pulled towards its overall chance:
// (uses in the period + PRIOR * overall share) / (words in the period + PRIOR).
static double formChance(const DatingModel *m, const DateForm *f, int bin) {
    double overall = (f->total + 0.5) / (m->total + 0.5 * m->count);
    return (f->n[bin] + DATE_PRIOR * overall) / (m->tokens[bin] + DATE_PRIOR);
}

int dateUses(const DatingModel *m, const FormUse *uses, int count, double *logLik) {
    int best = -1;
    for(int b = 0; b < DATE_BINS; b++) {
        if(m->tokens[b] < DATE_MIN_TOKENS) {
            logLik[b] = -INFINITY;
            continue;
        }
        double sum = 0;
        for(int i = 0; i < count; i++) {
            if(uses[i].form >= 0) sum += uses[i].n * log(formChance(m, &m->forms[uses[i].form], b));
        }
        logLik[b] = sum;
        if(best < 0 || sum > logLik[best]) best = b;
    }
    return best;
}

void formYear(const DatingModel *m, int form, double *year, double *spread) {
    const DateForm *f = &m->forms[form];
    double w = 0, sum = 0, sq = 0;
    for(int b = 0; b < DATE_BINS; b++) {
        if(m->tokens[b] < DATE_MIN_TOKENS) continue;
        double rate = (double)f->n[b] / m->tokens[b];
        w += rate;
        sum += rate * dateBinMiddle(b);
        sq += rate * dateBinMiddle(b) * dateBinMiddle(b);
    }
    *year = w > 0 ? sum / w : NAN;
    *spread = w > 0 ? sqrt(fmax(0, sq / w - *year * *year)) : NAN;
}

void holdOutVolume(sqlite3 *db, DatingModel *m, const char *docId, int sign) {
    sqlite3_stmt *stmt = dbPrepare(db,
        "SELECT d.year, f.form, f.n FROM form_count f JOIN doc d ON d.id = f.doc_id "
        "WHERE (d.chamber, d.term, d.volume) = (SELECT chamber, term, volume FROM doc WHERE id = ?1)");
    sqlite3_bind_text(stmt, 1, docId, -1, SQLITE_TRANSIENT);
    while(dbStep(db, stmt) == SQLITE_ROW) {
        int bin = dateBin(sqlite3_column_int(stmt, 0));
        int i = bin < 0 ? -1 : dateFormIndex(m, (const char *)sqlite3_column_text(stmt, 1));
        if(i < 0) continue;
        long n = sign * sqlite3_column_int64(stmt, 2);
        m->forms[i].n[bin] += n;
        m->forms[i].total += n;
        m->tokens[bin] += n;
        m->total += n;
    }
    sqlite3_finalize(stmt);
}

static uint64_t rng;

static uint64_t nextRandom(void) {   // xorshift64*
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return rng * 2685821657736338717ull;
}

// length words drawn at random from a sitting with total words (without putting back).
static int sampleUses(const FormUse *all, int count, long total, int length, FormUse *out) {
    int n = 0;
    long left = total;
    int want = length;
    for(int i = 0; i < count && want > 0; i++) {
        // Each of the sitting's words is drawn with chance want/left: a fair sample.
        int k = 0;
        for(int j = 0; j < all[i].n && want > 0; j++, left--) {
            if((long)(nextRandom() % left) < want) {
                k++;
                want--;
            }
        }
        if(k) out[n++] = (FormUse){all[i].form, k};
    }
    return n;
}

static int compareInts(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

void testDating(sqlite3 *db, DatingModel *m, uint64_t seed, DatingTest *out) {
    static const int LENGTHS[DATE_LENGTHS] = {20, 50, 200, 1000, 0};
    memset(out, 0, sizeof *out);
    memcpy(out->lengths, LENGTHS, sizeof LENGTHS);
    rng = seed;

    int nDocs = 0, cap = 256;
    char (*ids)[64] = malloc(cap * sizeof *ids);
    int *years = malloc(cap * sizeof *years);
    sqlite3_stmt *stmt = dbPrepare(db, "SELECT id, year FROM doc ORDER BY id");
    while(dbStep(db, stmt) == SQLITE_ROW) {
        if(dateBin(sqlite3_column_int(stmt, 1)) < 0) continue;
        if(nDocs == cap) {
            cap *= 2;
            ids = realloc(ids, cap * sizeof *ids);
            years = realloc(years, cap * sizeof *years);
        }
        snprintf(ids[nDocs], sizeof ids[nDocs], "%s", (const char *)sqlite3_column_text(stmt, 0));
        years[nDocs++] = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);
    out->year = malloc((nDocs + 1) * sizeof *out->year);
    out->guess = malloc((nDocs + 1) * sizeof *out->guess);

    stmt = dbPrepare(db, "SELECT form, n FROM form_count WHERE doc_id = ?1");
    int usesCap = 1024;
    FormUse *uses = malloc(usesCap * sizeof *uses), *sample = malloc(usesCap * sizeof *sample);
    for(int d = 0; d < nDocs; d++) {
        int count = 0;
        long total = 0;
        sqlite3_bind_text(stmt, 1, ids[d], -1, SQLITE_TRANSIENT);
        while(dbStep(db, stmt) == SQLITE_ROW) {
            if(count == usesCap) {
                usesCap *= 2;
                uses = realloc(uses, usesCap * sizeof *uses);
                sample = realloc(sample, usesCap * sizeof *sample);
            }
            int n = sqlite3_column_int(stmt, 1);
            uses[count++] = (FormUse){dateFormIndex(m, (const char *)sqlite3_column_text(stmt, 0)), n};
            total += n;
        }
        sqlite3_reset(stmt);

        holdOutVolume(db, m, ids[d], -1);
        double logLik[DATE_BINS];
        for(int l = 0; l < DATE_LENGTHS; l++) {
            int length = out->lengths[l];
            if(length && total < 2 * length) continue;   // a sample must be a small part of the sitting
            int guess = length ? dateUses(m, sample, sampleUses(uses, count, total, length, sample), logLik)
                               : dateUses(m, uses, count, logLik);
            if(guess < 0) continue;
            int error = abs(dateBinMiddle(guess) - years[d]);
            out->tested[l]++;
            out->meanError[l] += error;
            out->within5[l] += error <= 5;
            if(!length) {
                out->year[out->nSittings] = years[d];
                out->guess[out->nSittings++] = dateBinMiddle(guess);
            }
        }
        holdOutVolume(db, m, ids[d], +1);
    }
    sqlite3_finalize(stmt);
    for(int l = 0; l < DATE_LENGTHS; l++) {
        if(!out->tested[l]) continue;
        out->meanError[l] /= out->tested[l];
        out->within5[l] /= out->tested[l];
    }

    // Without reading anything, the best constant guess is the median year.
    int *sorted = malloc((out->nSittings + 1) * sizeof *sorted);
    memcpy(sorted, out->year, out->nSittings * sizeof *sorted);
    qsort(sorted, out->nSittings, sizeof *sorted, compareInts);
    for(int i = 0; i < out->nSittings; i++) out->chanceError += abs(out->year[i] - sorted[out->nSittings / 2]);
    if(out->nSittings) out->chanceError /= out->nSittings;
    free(sorted);
    free(uses);
    free(sample);
    free(ids);
    free(years);
}

void freeDatingTest(DatingTest *t) {
    free(t->year);
    free(t->guess);
    t->year = t->guess = NULL;
}

