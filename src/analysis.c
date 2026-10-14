#include "analysis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- S-curve fit

typedef struct {
    const long *k, *n;
    int count, firstYear;
} FitData;

// The optimiser works on unbounded numbers; rate and ceiling are mapped into range.
static void unpack(const double *t, double *mid, double *rate, double *ceiling) {
    *mid = t[0];
    *rate = exp(t[1]);
    *ceiling = 1 / (1 + exp(-t[2]));
}

// Negative log-likelihood: how unlikely the counted uses are under these parameters
// (each year's new-word uses are binomial out of all uses of the meaning).
static double negLogLikelihood(const double *t, const FitData *d) {
    double mid, rate, ceiling;
    unpack(t, &mid, &rate, &ceiling);
    // A replacement takes between ~4 and ~150 years from 10% to 90% (rate 1 to 0.03);
    // outside that, sparse years let the curve jump or crawl implausibly.
    if(rate < 0.03 || rate > 1 || mid < 1800 || mid > 2150) return 1e30;
    double sum = 0;
    for(int i = 0; i < d->count; i++) {
        if(!d->n[i]) continue;
        double p = ceiling / (1 + exp(-rate * (d->firstYear + i - mid)));
        p = fmin(fmax(p, 1e-9), 1 - 1e-9);
        sum -= d->k[i] * log(p) + (d->n[i] - d->k[i]) * log(1 - p);
    }
    return sum;
}

// Nelder-Mead simplex search (no derivatives needed). Moves x to the minimum it finds.
static double nelderMead(double *x, const FitData *d, int maxIter) {
    enum { N = 3 };
    const double steps[N] = {12, 0.7, 1.5};
    double s[N + 1][N], f[N + 1];
    for(int i = 0; i <= N; i++) {
        for(int j = 0; j < N; j++) s[i][j] = x[j] + (i == j + 1 ? steps[j] : 0);
        f[i] = negLogLikelihood(s[i], d);
    }
    for(int iter = 0; iter < maxIter; iter++) {
        // Order the corners: s[0] best ... s[N] worst.
        for(int i = 1; i <= N; i++) {
            for(int j = i; j > 0 && f[j] < f[j - 1]; j--) {
                double tf = f[j]; f[j] = f[j - 1]; f[j - 1] = tf;
                for(int c = 0; c < N; c++) { double t = s[j][c]; s[j][c] = s[j - 1][c]; s[j - 1][c] = t; }
            }
        }
        if(fabs(f[N] - f[0]) < 1e-10 * (1 + fabs(f[0]))) break;
        double centre[N] = {0};
        for(int i = 0; i < N; i++) {
            for(int c = 0; c < N; c++) centre[c] += s[i][c] / N;
        }
        double r[N], e[N], k[N];
        for(int c = 0; c < N; c++) r[c] = centre[c] + (centre[c] - s[N][c]);
        double fr = negLogLikelihood(r, d);
        if(fr < f[0]) {
            for(int c = 0; c < N; c++) e[c] = centre[c] + 2 * (centre[c] - s[N][c]);
            double fe = negLogLikelihood(e, d);
            if(fe < fr) { memcpy(s[N], e, sizeof e); f[N] = fe; }
            else { memcpy(s[N], r, sizeof r); f[N] = fr; }
        } else if(fr < f[N - 1]) {
            memcpy(s[N], r, sizeof r);
            f[N] = fr;
        } else {
            for(int c = 0; c < N; c++) k[c] = centre[c] + 0.5 * (s[N][c] - centre[c]);
            double fk = negLogLikelihood(k, d);
            if(fk < f[N]) {
                memcpy(s[N], k, sizeof k);
                f[N] = fk;
            } else {
                for(int i = 1; i <= N; i++) {   // shrink towards the best corner
                    for(int c = 0; c < N; c++) s[i][c] = s[0][c] + 0.5 * (s[i][c] - s[0][c]);
                    f[i] = negLogLikelihood(s[i], d);
                }
            }
        }
    }
    int best = 0;
    for(int i = 1; i <= N; i++) if(f[i] < f[best]) best = i;
    memcpy(x, s[best], sizeof s[best]);
    return f[best];
}

static void finishFit(const double *t, Fit *out) {
    unpack(t, &out->mid, &out->rate, &out->ceiling);
    out->crossover = out->ceiling > 0.5 ? out->mid - log(2 * out->ceiling - 1) / out->rate : NAN;
    out->duration = 2 * log(9) / out->rate;
}

bool fitLogistic(const long *k, const long *n, int count, int firstYear, Fit *out) {
    FitData d = {k, n, count, firstYear};
    out->uses = 0;
    for(int i = 0; i < count; i++) out->uses += n[i];
    out->ok = out->uses > 0;
    out->crossLo = out->crossHi = out->crossShare = NAN;
    out->ceilingLo = out->ceilingHi = NAN;
    out->uncertain = false;
    if(!out->ok) return false;
    // Several starting points: the likelihood can have more than one valley.
    double best[3], bestF = INFINITY;
    for(int start = 0; start < 14; start++) {
        double t[3] = {1925 + 15 * (start % 7), log(0.12), start < 7 ? 3.0 : 0.0};
        double f = nelderMead(t, &d, 600);
        if(f < bestF) { bestF = f; memcpy(best, t, sizeof best); }
    }
    finishFit(best, out);
    return true;
}

double fitShare(const Fit *f, double year) {
    return f->ok ? f->ceiling / (1 + exp(-f->rate * (year - f->mid))) : 0;
}

static uint64_t rng;

static uint64_t nextRandom(void) {   // xorshift64*
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return rng * 2685821657736338717ull;
}

static int compareDoubles(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static int compareSittingYears(const void *a, const void *b) {
    return ((const SittingUses *)a)->year - ((const SittingUses *)b)->year;
}

void bootstrapFit(const ConceptUses *u, int rounds, uint64_t seed, Fit *fit) {
    if(!fit->ok || u->nSittings == 0 || rounds < 20) return;
    rng = seed * 2654435761u + 88172645463325252ull;
    SittingUses *sorted = malloc(u->nSittings * sizeof *sorted);
    memcpy(sorted, u->sittings, u->nSittings * sizeof *sorted);
    qsort(sorted, u->nSittings, sizeof *sorted, compareSittingYears);
    double *cross = malloc(rounds * sizeof *cross), *ceilings = malloc(rounds * sizeof *ceilings);
    int nCross = 0;
    double t0[3] = {fit->mid, log(fit->rate), -log(1 / fit->ceiling - 1)};
    for(int r = 0; r < rounds; r++) {
        static long k[NYEARS], n[NYEARS];
        memset(k, 0, sizeof k);
        memset(n, 0, sizeof n);
        // Resample within each year, so every year keeps its number of sittings.
        for(int i = 0; i < u->nSittings;) {
            int j = i;
            while(j < u->nSittings && sorted[j].year == sorted[i].year) j++;
            int m = j - i, y = sorted[i].year - FIRST_YEAR;
            for(int draw = 0; draw < m; draw++) {
                const SittingUses *pick = &sorted[i + nextRandom() % m];
                k[y] += pick->newUses;
                n[y] += pick->allUses;
            }
            i = j;
        }
        FitData d = {k, n, NYEARS, FIRST_YEAR};
        double t[3];
        memcpy(t, t0, sizeof t);
        nelderMead(t, &d, 400);
        Fit f;
        finishFit(t, &f);
        ceilings[r] = f.ceiling;
        if(!isnan(f.crossover)) cross[nCross++] = f.crossover;
    }
    qsort(ceilings, rounds, sizeof *ceilings, compareDoubles);
    fit->ceilingLo = ceilings[(int)(0.025 * rounds)];
    fit->ceilingHi = ceilings[(int)(0.975 * (rounds - 1))];
    fit->crossShare = (double)nCross / rounds;
    if(nCross >= rounds / 4) {
        qsort(cross, nCross, sizeof *cross, compareDoubles);
        fit->crossLo = cross[(int)(0.025 * nCross)];
        fit->crossHi = cross[(int)(0.975 * (nCross - 1))];
    }
    fit->uncertain = fit->rate > 0.95 || fit->rate < 0.032 ||
                     (!isnan(fit->crossover) && (fit->crossShare < 0.9 || isnan(fit->crossLo) || fit->crossHi - fit->crossLo > 30));
    free(sorted);
    free(cross);
    free(ceilings);
}

float observedAt(const ConceptStudy *c, int year) {
    for(int half = 3; half <= 10; half++) {
        long k = 0, n = 0;
        for(int y = year - half; y <= year + half; y++) {
            if(y < FIRST_YEAR || y > LAST_YEAR) continue;
            k += c->uses.newUses[y - FIRST_YEAR];
            n += c->uses.allUses[y - FIRST_YEAR];
        }
        if(n >= 20) return (float)k / n;
    }
    return fitShare(&c->fit, year);
}

float observedShare(const ConceptStudy *c, float year) {
    int y0 = (int)floorf(year);
    float t = year - y0;
    return observedAt(c, y0) * (1 - t) + observedAt(c, y0 + 1) * t;
}

void findReversal(const ConceptStudy *c, Reversal *out) {
    memset(out, 0, sizeof *out);
    for(int y = 1940; y <= 1951; y++) {
        float p = observedAt(c, y);
        if(p > out->before) { out->before = p; out->beforeYear = y; }
    }
    out->after = 1;
    for(int y = 1952; y <= 1965; y++) {
        float p = observedAt(c, y);
        if(p < out->after) { out->after = p; out->lowYear = y; }
    }
    out->found = out->before >= REVERSAL_MIN_SHARE && out->before - out->after >= REVERSAL_MIN_DROP;
    for(int y = out->lowYear; out->found && y <= 2023; y++) {
        if(observedAt(c, y) >= out->before) { out->backYear = y; break; }
    }
}

double median(double *values, int n) {
    if(n == 0) return NAN;
    qsort(values, n, sizeof *values, compareDoubles);
    return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;
}

double permutationTest(const double *a, int na, const double *b, int nb, int rounds, uint64_t seed) {
    int n = na + nb;
    if(na == 0 || nb == 0) return NAN;
    double *all = malloc(n * sizeof *all), *ga = malloc(na * sizeof *ga), *gb = malloc(nb * sizeof *gb);
    memcpy(all, a, na * sizeof *a);
    memcpy(all + na, b, nb * sizeof *b);
    memcpy(ga, a, na * sizeof *a);
    memcpy(gb, b, nb * sizeof *b);
    double observed = fabs(median(ga, na) - median(gb, nb));
    rng = seed * 2654435761u + 1442695040888963407ull;
    int asExtreme = 0;
    for(int r = 0; r < rounds; r++) {
        for(int i = n - 1; i > 0; i--) {   // Fisher-Yates shuffle
            int j = (int)(nextRandom() % (uint64_t)(i + 1));
            double t = all[i]; all[i] = all[j]; all[j] = t;
        }
        memcpy(ga, all, na * sizeof *ga);
        memcpy(gb, all + na, nb * sizeof *gb);
        if(fabs(median(ga, na) - median(gb, nb)) >= observed - 1e-9) asExtreme++;
    }
    free(all);
    free(ga);
    free(gb);
    return (asExtreme + 1.0) / (rounds + 1.0);
}

FitStatus fitStatus(const Fit *f) {
    if(!f->ok || f->uses < 40) return STATUS_LITTLE_DATA;
    if(f->ceiling >= 0.85 && !isnan(f->crossover)) return STATUS_REPLACED;
    if(f->ceiling >= 0.5 && !isnan(f->crossover)) return STATUS_PARTIAL;
    return STATUS_FAILED;
}

const char *statusName(FitStatus s) {
    static const char *NAMES[] = {"yerleşti", "kısmen", "tutmadı", "veri az"};
    return NAMES[s];
}

// ---------------------------------------------------------------- etymology

const char *originOf(const char *etymology) {
    static const struct { const char *key, *label; } LANGS[] = {
        {"Arabic", "arabic"}, {"Persian", "persian"}, {"French", "french"}, {"Italian", "italian"},
        {"Greek", "greek"}, {"Armenian", "armenian"}, {"Latin", "latin"}, {"English", "english"},
        {"German", "german"}, {"Proto-Turkic", "turkic"}, {"Old Turkic", "turkic"},
        {"Middle Turkic", "turkic"}, {"Karakhanid", "turkic"}, {"Chagatai", "turkic"}, {"Uyghur", "turkic"},
        {"Kipchak", "turkic"}, {"Tatar", "turkic"}, {"Kazakh", "turkic"}, {"Kyrgyz", "turkic"},
        {"Turkmen", "turkic"}, {"Uzbek", "turkic"}, {"Yakut", "turkic"}, {"Azerbaijani", "turkic"},
        {"Mongol", "mongolic"},
    };
    static const char *STAGES[] = {"Ottoman Turkish", "Old Anatolian Turkish"};
    static const char *ASIDES[] = {"Compare", "compare", "Cognate", "cognate", "See also", "Doublet", "Equivalent to"};
    char text[4096];
    snprintf(text, sizeof text, "%s", etymology);
    if(strstr(text, "alque") || strstr(text, "oined")) return "turkish-derived";
    // Asides like "Compare also Persian ..." are not the word's own history.
    for(size_t i = 0; i < sizeof ASIDES / sizeof ASIDES[0]; i++) {
        char *p = strstr(text, ASIDES[i]);
        if(p && p > text) *p = 0;
    }
    for(int s = 0; s < 2; s++) {
        for(char *p = strstr(text, STAGES[s]); p; p = strstr(p, STAGES[s])) memset(p, ' ', strlen(STAGES[s]));
    }
    const char *best = NULL, *bestAt = NULL;
    for(size_t i = 0; i < sizeof LANGS / sizeof LANGS[0]; i++) {
        const char *p = strstr(text, LANGS[i].key);
        if(p && (!bestAt || p < bestAt)) {
            bestAt = p;
            best = LANGS[i].label;
        }
    }
    if(best) return best;
    if(strchr(text, '+') || strstr(text, "suffix")) return "turkish-derived";
    // No other language named, only older stages of Turkish ("From Ottoman Turkish
    // كیشی, from Old Anatolian Turkish ..."), or "Inherited", Wiktionary's word for native
    // descent: a Turkish word.
    if(strstr(etymology, "Inherited") || strstr(etymology, "Ottoman Turkish") || strstr(etymology, "Old Anatolian Turkish")) {
        return "turkic";
    }
    return "unclear";
}

static bool isBackVowel(uint32_t c) {
    return c == 'a' || c == 0x131 || c == 'o' || c == 'u';
}

static bool isFrontVowel(uint32_t c) {
    return c == 'e' || c == 'i' || c == 0xF6 || c == 0xFC;
}

bool etymologyRoot(const char *etymology, char *root) {
    if(strstr(etymology, "oined") || strstr(etymology, "alque")) return false;
    const char *plus = strchr(etymology, '+');
    if(!plus) return false;
    // The root is the first word after the last "from ", ", " or ": " before the "+":
    // "From Ottoman Turkish كتابجی, from kitap + -çı", "By surface analysis, millet + vekil".
    const char *start = etymology;
    for(const char *p = etymology; p < plus; p++) {
        if(strncmp(p, "from ", 5) == 0 || strncmp(p, "From ", 5) == 0) start = p + 5;
        else if((p[0] == ',' || p[0] == ':') && p[1] == ' ') start = p + 2;
    }
    while(start < plus && (*start == ' ' || *start == '"')) start++;
    char word[MAX_WORD];
    int n = 0;
    const char *p = start;
    for(;;) {
        uint32_t cp;
        int k = utf8Decode(p, &cp);
        if(!trIsLetter(cp)) break;
        if(n + k >= MAX_WORD - 4) return false;
        memcpy(word + n, p, k);
        n += k;
        p += k;
    }
    word[n] = 0;
    if(n == 0 || p > plus) return false;
    trFoldString(word, root, MAX_WORD);
    if(*p == '-') {   // a verb stem: kur- → kurmak
        uint32_t last = 0;
        for(const char *q = root; *q;) {
            uint32_t cp;
            q += utf8Decode(q, &cp);
            if(isBackVowel(cp) || isFrontVowel(cp)) last = cp;
        }
        strcat(root, isFrontVowel(last) ? "mek" : "mak");
    }
    return true;
}

int originGroup(const char *origin) {
    if(strcmp(origin, "arabic") == 0) return ORIGIN_ARABIC;
    if(strcmp(origin, "persian") == 0) return ORIGIN_PERSIAN;
    if(strcmp(origin, "turkic") == 0 || strcmp(origin, "turkish-derived") == 0) return ORIGIN_TURKISH;
    if(strcmp(origin, "french") == 0 || strcmp(origin, "italian") == 0 || strcmp(origin, "english") == 0 ||
       strcmp(origin, "german") == 0 || strcmp(origin, "latin") == 0) return ORIGIN_WESTERN;
    return ORIGIN_OTHER;
}

const char *originGroupName(int group) {
    static const char *NAMES[] = {"Arapça", "Farsça", "Türkçe", "Batı dilleri", "diğer"};
    return group >= 0 && group < ORIGIN_COUNT ? NAMES[group] : "?";
}

// ---------------------------------------------------------------- origins of the corpus

void buildFormOrigins(sqlite3 *db) {
    // A form counts if it can be a noun or adjective and cannot be a function word:
    // "için" is in the declension table of "iç" (inside), but it is mostly "for".
    sqlite3_stmt *forms = dbPrepare(db,
        "SELECT DISTINCT f.form FROM form_count f WHERE "
        "EXISTS (SELECT 1 FROM lexicon l WHERE l.form = f.form AND l.pos IN ('noun', 'adj')) AND "
        "NOT EXISTS (SELECT 1 FROM lexicon l WHERE l.form = f.form AND "
        "l.pos IN ('conj', 'postp', 'pron', 'det', 'particle', 'intj', 'num'))");
    sqlite3_stmt *pick = dbPrepare(db,
        "SELECT l.lemma, COALESCE(o.origin, 'unclear') FROM lexicon l "
        "LEFT JOIN origin o ON o.lemma = l.lemma AND o.pos = l.pos "
        "WHERE l.form = ?1 AND l.pos IN ('noun', 'adj') "
        "ORDER BY o.origin IS NOT NULL AND o.origin <> 'unclear' DESC, l.src = 'wiktionary' DESC, "
        "l.lemma = ?1 DESC, length(l.lemma) DESC LIMIT 1");
    // A verbal noun often has no etymology of its own (çalışma); its verb (çalışmak) has.
    sqlite3_stmt *other = dbPrepare(db,
        "SELECT o.origin FROM lexicon l JOIN origin o ON o.lemma = l.lemma AND o.pos = l.pos "
        "WHERE l.form = ?1 AND o.origin <> 'unclear' ORDER BY l.pos = 'verb' DESC LIMIT 1");
    sqlite3_stmt *ins = dbPrepare(db, "INSERT OR REPLACE INTO form_origin VALUES (?1, ?2, ?3)");
    dbExec(db, "BEGIN; DELETE FROM form_origin;");
    while(dbStep(db, forms) == SQLITE_ROW) {
        const char *form = (const char *)sqlite3_column_text(forms, 0);
        uint32_t cp;
        if(!form[utf8Decode(form, &cp)]) continue;   // one-letter "words" are OCR debris
        sqlite3_bind_text(pick, 1, form, -1, SQLITE_TRANSIENT);
        if(dbStep(db, pick) == SQLITE_ROW) {
            char lemma[MAX_WORD], origin[32];
            snprintf(lemma, sizeof lemma, "%s", (const char *)sqlite3_column_text(pick, 0));
            snprintf(origin, sizeof origin, "%s", (const char *)sqlite3_column_text(pick, 1));
            if(strcmp(origin, "unclear") == 0) {
                sqlite3_bind_text(other, 1, form, -1, SQLITE_TRANSIENT);
                if(dbStep(db, other) == SQLITE_ROW) snprintf(origin, sizeof origin, "%s", (const char *)sqlite3_column_text(other, 0));
                sqlite3_reset(other);
            }
            sqlite3_bind_text(ins, 1, form, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins, 2, lemma, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins, 3, origin, -1, SQLITE_TRANSIENT);
            dbStep(db, ins);
            sqlite3_reset(ins);
        }
        sqlite3_reset(pick);
    }
    sqlite3_finalize(other);
    dbExec(db, "COMMIT");
    sqlite3_finalize(forms);
    sqlite3_finalize(pick);
    sqlite3_finalize(ins);
}

void originsByYear(sqlite3 *db, OriginByYear *out) {
    memset(out, 0, sizeof *out);
    sqlite3_stmt *stmt = dbPrepare(db,
        "SELECT d.year, o.origin, SUM(f.n) FROM form_count f JOIN doc d ON d.id = f.doc_id "
        "JOIN form_origin o ON o.form = f.form GROUP BY d.year, o.origin");
    while(dbStep(db, stmt) == SQLITE_ROW) {
        int y = sqlite3_column_int(stmt, 0) - FIRST_YEAR;
        if(y < 0 || y >= NYEARS) continue;
        out->n[y][originGroup((const char *)sqlite3_column_text(stmt, 1))] += sqlite3_column_int64(stmt, 2);
    }
    sqlite3_finalize(stmt);
    stmt = dbPrepare(db,
        "SELECT d.year, SUM(f.n) FROM form_count f JOIN doc d ON d.id = f.doc_id "
        "WHERE NOT EXISTS (SELECT 1 FROM lexicon l WHERE l.form = f.form) GROUP BY d.year");
    while(dbStep(db, stmt) == SQLITE_ROW) {
        int y = sqlite3_column_int(stmt, 0) - FIRST_YEAR;
        if(y >= 0 && y < NYEARS) out->unknown[y] = sqlite3_column_int64(stmt, 1);
    }
    sqlite3_finalize(stmt);
}

static int compareEraWords(const void *a, const void *b) {
    double x = ((const EraWord *)a)->z, y = ((const EraWord *)b)->z;
    return (x < y) - (x > y);   // largest z first
}

// Log-odds ratio with an informative Dirichlet prior (Monroe, Colaresi & Quinn 2008):
// how much more typical a word is of A than of B, adjusted for how often it occurs at
// all, so rare words do not top the list by chance. Positive: typical of A.
static double logOddsZ(long countA, long countB, double totalA, double totalB) {
    const double alpha0 = 500;
    double a = alpha0 * (countA + countB) / (totalA + totalB);
    if(a <= 0) return 0;
    double la = log((countA + a) / (totalA + alpha0 - countA - a));
    double lb = log((countB + a) / (totalB + alpha0 - countB - a));
    return (la - lb) / sqrt(1 / (countA + a) + 1 / (countB + a));
}

void eraWords(sqlite3 *db, int fromA, int toA, int fromB, int toB, int top,
              EraWord *forA, int *nA, EraWord *forB, int *nB) {
    sqlite3_stmt *stmt = dbPrepare(db,
        "SELECT o.lemma, MIN(o.origin), "
        "SUM(CASE WHEN d.year BETWEEN ?1 AND ?2 THEN f.n ELSE 0 END), "
        "SUM(CASE WHEN d.year BETWEEN ?3 AND ?4 THEN f.n ELSE 0 END) "
        "FROM form_count f JOIN doc d ON d.id = f.doc_id JOIN form_origin o ON o.form = f.form "
        "GROUP BY o.lemma");
    sqlite3_bind_int(stmt, 1, fromA);
    sqlite3_bind_int(stmt, 2, toA);
    sqlite3_bind_int(stmt, 3, fromB);
    sqlite3_bind_int(stmt, 4, toB);
    int cap = 4096, n = 0;
    EraWord *all = malloc(cap * sizeof *all);
    double totalA = 0, totalB = 0;
    while(dbStep(db, stmt) == SQLITE_ROW) {
        if(n == cap) all = realloc(all, (cap *= 2) * sizeof *all);
        EraWord *w = &all[n++];
        snprintf(w->word, sizeof w->word, "%s", (const char *)sqlite3_column_text(stmt, 0));
        w->origin = originGroup((const char *)sqlite3_column_text(stmt, 1));
        w->countA = sqlite3_column_int64(stmt, 2);
        w->countB = sqlite3_column_int64(stmt, 3);
        totalA += w->countA;
        totalB += w->countB;
    }
    sqlite3_finalize(stmt);
    int kept = 0;
    for(int i = 0; i < n; i++) {
        EraWord *w = &all[i];
        if(w->countA + w->countB < 30 || strlen(w->word) < 3) continue;
        w->z = logOddsZ(w->countA, w->countB, totalA, totalB);
        all[kept++] = *w;
    }
    qsort(all, kept, sizeof *all, compareEraWords);
    *nA = 0;
    for(int i = 0; i < kept && *nA < top && all[i].z > 0; i++) forA[(*nA)++] = all[i];
    *nB = 0;
    for(int i = kept - 1; i >= 0 && *nB < top && all[i].z < 0; i--) forB[(*nB)++] = all[i];
    free(all);
}

// ---------------------------------------------------------------- the vocabulary over time

static int compareWordYears(const void *a, const void *b) {
    return strcmp(((const WordYears *)a)->word, ((const WordYears *)b)->word);
}

void loadVocabulary(sqlite3 *db, Vocabulary *out) {
    memset(out, 0, sizeof *out);
    // ORDER BY uses byte order, the same as strcmp, so the list can be searched with bsearch.
    sqlite3_stmt *stmt = dbPrepare(db,
        "SELECT o.lemma, MIN(o.origin), SUM(f.n) FROM form_count f JOIN form_origin o ON o.form = f.form "
        "GROUP BY o.lemma HAVING SUM(f.n) >= ?1 ORDER BY o.lemma");
    sqlite3_bind_int(stmt, 1, VOCAB_MIN_USES);
    int cap = 1024;
    out->words = malloc(cap * sizeof *out->words);
    while(dbStep(db, stmt) == SQLITE_ROW) {
        if(out->count == cap) out->words = realloc(out->words, (cap *= 2) * sizeof *out->words);
        WordYears *w = &out->words[out->count++];
        memset(w, 0, sizeof *w);
        snprintf(w->word, sizeof w->word, "%s", (const char *)sqlite3_column_text(stmt, 0));
        w->origin = originGroup((const char *)sqlite3_column_text(stmt, 1));
        w->total = sqlite3_column_int64(stmt, 2);
    }
    sqlite3_finalize(stmt);

    // Uses per year. Rare words are left out of the list but counted in the total.
    stmt = dbPrepare(db,
        "SELECT d.year, o.lemma, SUM(f.n) FROM form_count f JOIN doc d ON d.id = f.doc_id "
        "JOIN form_origin o ON o.form = f.form GROUP BY d.year, o.lemma");
    while(dbStep(db, stmt) == SQLITE_ROW) {
        int y = sqlite3_column_int(stmt, 0) - FIRST_YEAR;
        if(y < 0 || y >= NYEARS) continue;
        int uses = sqlite3_column_int(stmt, 2);
        out->n[y] += uses;
        WordYears key;
        snprintf(key.word, sizeof key.word, "%s", (const char *)sqlite3_column_text(stmt, 1));
        WordYears *w = bsearch(&key, out->words, out->count, sizeof *out->words, compareWordYears);
        if(w) w->n[y] += uses;
    }
    sqlite3_finalize(stmt);
}

void freeVocabulary(Vocabulary *v) {
    free(v->words);
    v->words = NULL;
    v->count = 0;
}

void smoothRate(const int *n, const YearCounts *tokens, long minTokens, float *rates) {
    for(int y = 0; y < NYEARS; y++) {
        rates[y] = NAN;
        for(int half = 2; half <= 8; half++) {
            long k = 0, words = 0;
            for(int i = y - half; i <= y + half; i++) {
                if(i < 0 || i >= NYEARS) continue;
                k += n[i];
                words += tokens->n[i];
            }
            if(words >= minTokens) {
                rates[y] = k * 1e6f / words;
                break;
            }
        }
    }
}

static long usesBetween(const int *n, int from, int to) {
    long sum = 0;
    for(int y = from; y <= to; y++) {
        if(y >= FIRST_YEAR && y <= LAST_YEAR) sum += n[y - FIRST_YEAR];
    }
    return sum;
}

int periodWords(const Vocabulary *v, int from, int to, int top, EraWord *out) {
    double totalA = 0, totalB = 0;
    for(int y = 0; y < NYEARS; y++) {
        if(FIRST_YEAR + y >= from && FIRST_YEAR + y <= to) totalA += v->n[y];
        else totalB += v->n[y];
    }
    if(totalA == 0 || v->count == 0) return 0;
    EraWord *all = malloc(v->count * sizeof *all);
    int kept = 0;
    for(int i = 0; i < v->count; i++) {
        const WordYears *w = &v->words[i];
        if(strlen(w->word) < 3) continue;   // two bytes: el, ad, ok (mostly parts of phrases)
        EraWord *e = &all[kept++];
        snprintf(e->word, sizeof e->word, "%s", w->word);
        e->origin = w->origin;
        e->countA = usesBetween(w->n, from, to);
        e->countB = w->total - e->countA;
        e->z = logOddsZ(e->countA, e->countB, totalA, totalB);
    }
    qsort(all, kept, sizeof *all, compareEraWords);
    int n = 0;
    for(int i = 0; i < kept && n < top && all[i].z > 0; i++) out[n++] = all[i];
    free(all);
    return n;
}

typedef struct {
    int index;
    long base;
    double ratio;   // today's rate / the base decade's rate
} Candidate;

static int compareByBase(const void *a, const void *b) {
    long x = ((const Candidate *)a)->base, y = ((const Candidate *)b)->base;
    return (x < y) - (x > y);
}

static int compareByRatio(const void *a, const void *b) {
    const Candidate *x = a, *y = b;
    if(x->ratio != y->ratio) return (x->ratio > y->ratio) - (x->ratio < y->ratio);
    return (x->base < y->base) - (x->base > y->base);   // equally gone: the most used first
}

void survivalOf(const Vocabulary *v, const YearCounts *tokens, int baseDecade, int top, Survival *out) {
    memset(out, 0, sizeof *out);
    out->base = baseDecade;
    int b = (baseDecade - 1920) / 10;
    if(b < 0 || b >= DECADES) return;
    double decadeTokens[DECADES], todayTokens = 0;
    for(int d = 0; d < DECADES; d++) {
        decadeTokens[d] = 0;
        for(int y = 1920 + 10 * d; y < 1930 + 10 * d; y++) decadeTokens[d] += tokens->n[y - FIRST_YEAR];
        if(d >= 8) todayTokens += decadeTokens[d];   // 2000 to today
    }
    if(decadeTokens[b] == 0 || todayTokens == 0) return;

    // The most used words of the base decade.
    Candidate *c = malloc(v->count * sizeof *c);
    int n = 0;
    for(int i = 0; i < v->count; i++) {
        long base = usesBetween(v->words[i].n, baseDecade, baseDecade + 9);
        if(strlen(v->words[i].word) < 3 || base == 0) continue;
        c[n++] = (Candidate){i, base, 0};
    }
    qsort(c, n, sizeof *c, compareByBase);
    if(n > top) n = top;
    out->top = n;

    for(int d = 0; d < DECADES; d++) {
        int alive[ORIGIN_COUNT + 1] = {0};
        for(int i = 0; i < n; i++) {
            const WordYears *w = &v->words[c[i].index];
            double baseRate = c[i].base / decadeTokens[b];
            double rate = decadeTokens[d] ? usesBetween(w->n, 1920 + 10 * d, 1929 + 10 * d) / decadeTokens[d] : 0;
            if(d == 0) {
                out->nWords[w->origin]++;
                out->nWords[ORIGIN_COUNT]++;
            }
            if(rate >= baseRate / 4) {
                alive[w->origin]++;
                alive[ORIGIN_COUNT]++;
            }
        }
        for(int g = 0; g <= ORIGIN_COUNT; g++) {
            out->alive[g][d] = decadeTokens[d] && out->nWords[g] ? (float)alive[g] / out->nWords[g] : NAN;
        }
    }

    // Examples: the words that vanished, and the Arabic and Persian words that held on.
    for(int i = 0; i < n; i++) {
        double baseRate = c[i].base / decadeTokens[b];
        c[i].ratio = usesBetween(v->words[c[i].index].n, 2000, 2029) / todayTokens / baseRate;
    }
    qsort(c, n, sizeof *c, compareByRatio);
    for(int i = 0; i < n && out->nLost < 10; i++) out->lost[out->nLost++] = c[i].index;
    for(int i = n - 1; i >= 0 && out->nKept < 10; i--) {
        int origin = v->words[c[i].index].origin;
        if(origin == ORIGIN_ARABIC || origin == ORIGIN_PERSIAN) out->kept[out->nKept++] = c[i].index;
    }
    free(c);
}

// ---------------------------------------------------------------- the study

typedef struct {
    char id[64];
    int year;
} DocYear;

static int compareDocIds(const void *a, const void *b) {
    return strcmp(((const DocYear *)a)->id, ((const DocYear *)b)->id);
}

Study *loadStudy(sqlite3 *db, const char *conceptsPath, int bootstrapRounds) {
    ConceptList cl;
    if(!loadConcepts(conceptsPath, &cl)) return NULL;
    resolveConcepts(db, &cl);
    Study *s = calloc(1, sizeof *s);
    tokensByYear(db, &s->tokens);
    for(int y = 0; y < NYEARS; y++) s->words += s->tokens.n[y];

    // Every sitting, sorted by id, to map query rows to sittings.
    sqlite3_stmt *stmt = dbPrepare(db, "SELECT id, year FROM doc ORDER BY id");
    int nDocs = 0, cap = 256;
    DocYear *docs = malloc(cap * sizeof *docs);
    while(dbStep(db, stmt) == SQLITE_ROW) {
        if(nDocs == cap) docs = realloc(docs, (cap *= 2) * sizeof *docs);
        snprintf(docs[nDocs].id, sizeof docs[nDocs].id, "%s", (const char *)sqlite3_column_text(stmt, 0));
        docs[nDocs++].year = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);
    s->sittings = nDocs;

    // Full paradigms for every competing word, then the hand-checked exclusions (which the
    // generator would otherwise add back).
    for(int i = 0; i < cl.count; i++) ensureParadigm(db, cl.items[i].lemma);
    char exclusions[1024];
    exclusionsPathFor(conceptsPath, exclusions, sizeof exclusions);
    applyExclusions(db, exclusions);

    s->concepts = calloc(cl.count, sizeof *s->concepts);
    stmt = dbPrepare(db,
        "SELECT f.doc_id, SUM(f.n) FROM form_count f "
        "WHERE f.form IN (SELECT form FROM lexicon WHERE lemma = ?1 UNION SELECT ?1) GROUP BY f.doc_id");
    long *newPer = malloc((nDocs + 1) * sizeof *newPer), *allPer = malloc((nDocs + 1) * sizeof *allPer);
    for(int i = 0; i < cl.count; i++) {
        bool seen = false;
        for(int j = 0; j < s->nConcepts; j++) seen = seen || strcmp(s->concepts[j].concept, cl.items[i].concept) == 0;
        if(seen) continue;
        ConceptStudy *c = &s->concepts[s->nConcepts++];
        snprintf(c->concept, sizeof c->concept, "%s", cl.items[i].concept);
        snprintf(c->group, sizeof c->group, "%s", cl.items[i].group);
        memset(newPer, 0, nDocs * sizeof *newPer);
        memset(allPer, 0, nDocs * sizeof *allPer);
        for(int j = i; j < cl.count && c->nWords < MAX_COMPETITORS; j++) {
            if(strcmp(cl.items[j].concept, c->concept) != 0) continue;
            // umumiye is a form of umumi: once resolved, the same word must not count twice.
            bool duplicate = false;
            for(int w = 0; w < c->nWords; w++) duplicate = duplicate || strcmp(c->words[w], cl.items[j].lemma) == 0;
            if(duplicate) continue;
            int w = c->nWords++;
            snprintf(c->words[w], MAX_WORD, "%s", cl.items[j].lemma);
            snprintf(c->roles[w], sizeof c->roles[w], "%s", cl.items[j].role);
            char label[sizeof c->label];
            snprintf(label, sizeof label, "%.130s%s%.63s", c->label, c->label[0] ? " → " : "", c->words[w]);
            memcpy(c->label, label, sizeof label);
            bool isNew = strcmp(c->roles[w], "new") == 0;
            sqlite3_bind_text(stmt, 1, c->words[w], -1, SQLITE_TRANSIENT);
            while(dbStep(db, stmt) == SQLITE_ROW) {
                DocYear key;
                snprintf(key.id, sizeof key.id, "%s", (const char *)sqlite3_column_text(stmt, 0));
                DocYear *d = bsearch(&key, docs, nDocs, sizeof *docs, compareDocIds);
                if(!d) continue;
                long uses = sqlite3_column_int64(stmt, 1);
                c->counts[w].n[d->year - FIRST_YEAR] += uses;
                allPer[d - docs] += uses;
                if(isNew) newPer[d - docs] += uses;
            }
            sqlite3_reset(stmt);
        }
        c->uses.nSittings = nDocs;
        c->uses.sittings = malloc(nDocs * sizeof *c->uses.sittings);
        for(int d = 0; d < nDocs; d++) {
            c->uses.sittings[d] = (SittingUses){docs[d].year, newPer[d], allPer[d]};
            c->uses.newUses[docs[d].year - FIRST_YEAR] += newPer[d];
            c->uses.allUses[docs[d].year - FIRST_YEAR] += allPer[d];
        }
        fitLogistic(c->uses.newUses, c->uses.allUses, NYEARS, FIRST_YEAR, &c->fit);
        bootstrapFit(&c->uses, bootstrapRounds, (uint64_t)(s->nConcepts * 7919), &c->fit);
        findReversal(c, &c->reversal);
    }
    sqlite3_finalize(stmt);
    free(newPer);
    free(allPer);
    free(docs);
    free(cl.items);

    buildFormOrigins(db);
    originsByYear(db, &s->origins);
    s->eraA[0] = 1920; s->eraA[1] = 1934;
    s->eraB[0] = 2003; s->eraB[1] = 2026;
    eraWords(db, s->eraA[0], s->eraA[1], s->eraB[0], s->eraB[1], 32, s->early, &s->nEarly, s->late, &s->nLate);

    loadVocabulary(db, &s->vocab);
    s->rates = malloc((s->vocab.count + 1) * sizeof *s->rates);
    for(int i = 0; i < s->vocab.count; i++) smoothRate(s->vocab.words[i].n, &s->tokens, 50000, s->rates[i]);
    for(int d = 0; d < DECADES; d++) {
        s->nDecade[d] = periodWords(&s->vocab, 1920 + 10 * d, 1929 + 10 * d, 12, s->decades[d]);
    }
    survivalOf(&s->vocab, &s->tokens, 1920, 300, &s->survival);
    loadDatingModel(db, &s->dating);
    testDating(db, &s->dating, 7, &s->datingTest);
    return s;
}

void freeStudy(Study *s) {
    if(!s) return;
    for(int i = 0; i < s->nConcepts; i++) free(s->concepts[i].uses.sittings);
    freeVocabulary(&s->vocab);
    freeDatingModel(&s->dating);
    freeDatingTest(&s->datingTest);
    free(s->rates);
    free(s->concepts);
    free(s);
}
