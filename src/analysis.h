#ifndef ANALYSIS_H
#define ANALYSIS_H

#include "dating.h"
#include "query.h"

#include <math.h>

// ---------------------------------------------------------------- S-curves

// How the uses of one meaning split between its competing words: per year, and per
// sitting (for the bootstrap). "New" is today's word; "all" is every competitor together.
typedef struct {
    int year;
    long newUses, allUses;
} SittingUses;

typedef struct {
    long newUses[NYEARS], allUses[NYEARS];
    SittingUses *sittings;
    int nSittings;
} ConceptUses;

// Language change follows an S-curve: slow start, fast middle, slow end (Kroch 1989).
//   share(year) = ceiling / (1 + exp(-rate * (year - mid)))
// The ceiling is below 1 when the old word never disappears (kelime/sözcük).
typedef struct {
    bool ok;
    double mid, rate, ceiling;
    double crossover;          // year today's word passed 50% of the uses; NAN if it never did
    double duration;           // years from 10% to 90% of the ceiling
    double crossLo, crossHi;   // 95% bootstrap interval of the crossover year
    double crossShare;         // share of bootstrap rounds in which a crossover happened
    double ceilingLo, ceilingHi;
    long uses;                 // all uses counted
    bool uncertain;            // the data pin the curve down poorly (wide interval, or a speed at its limit)
} Fit;

// Maximum-likelihood fit to k[i] successes out of n[i] in year firstYear + i.
bool fitLogistic(const long *k, const long *n, int count, int firstYear, Fit *out);
double fitShare(const Fit *f, double year);

// 95% intervals: the sittings of each year are resampled with replacement and the curve
// refitted, rounds times. Fills crossLo/crossHi/crossShare/ceilingLo/ceilingHi.
void bootstrapFit(const ConceptUses *u, int rounds, uint64_t seed, Fit *fit);

typedef enum { STATUS_REPLACED, STATUS_PARTIAL, STATUS_FAILED, STATUS_LITTLE_DATA } FitStatus;
FitStatus fitStatus(const Fit *f);
const char *statusName(FitStatus s);   // Turkish, for charts and tables

double median(double *values, int n);   // sorts values
// Two-sided permutation test: how often does shuffling the two groups' labels give a
// difference in medians at least as large as the real one? Small p: unlikely by chance.
double permutationTest(const double *a, int na, const double *b, int nb, int rounds, uint64_t seed);

// ---------------------------------------------------------------- the study

#define MAX_COMPETITORS 4

// A step back: after the 1952 return of the constitution to its old wording, today's word
// lost ground in many meanings. The highest share before 1952 and the lowest after it.
#define REVERSAL_MIN_SHARE 0.2f   // the new word had at least this share before
#define REVERSAL_MIN_DROP 0.1f    // and lost at least this much (10 points)
typedef struct {
    bool found;
    float before;     // highest share of today's word, 1940-1951
    float after;      // lowest share, 1952-1965
    int beforeYear, lowYear;
    int backYear;     // first year from lowYear on with the share back at `before` (0: never)
} Reversal;
// One meaning and its competing words, counted and fitted.
typedef struct {
    char concept[MAX_WORD];
    char group[32];                         // theme from concepts.tsv: meclis, devlet, genel, eğitim
    char label[200];                        // "sene → yıl"
    int nWords;
    char words[MAX_COMPETITORS][MAX_WORD];
    char roles[MAX_COMPETITORS][16];
    YearCounts counts[MAX_COMPETITORS];
    ConceptUses uses;
    Fit fit;
    Reversal reversal;                      // its step back after 1952 (findReversal)
} ConceptStudy;

// The share of today's word around a year, from the counts themselves: a window that
// widens until it holds enough uses. Unlike the S-curve, it shows reversals (vekil came
// back in the 1950s). Where there are no data it falls back to the S-curve.
// observedShare interpolates between whole years, for animations.
float observedAt(const ConceptStudy *c, int year);
float observedShare(const ConceptStudy *c, float year);

void findReversal(const ConceptStudy *c, Reversal *out);

// Where words came from, by year, among nouns and adjectives (function words such as
// ve, ama, her are Arabic or Persian too, but they were never replaced).
enum { ORIGIN_ARABIC, ORIGIN_PERSIAN, ORIGIN_TURKISH, ORIGIN_WESTERN, ORIGIN_OTHER, ORIGIN_COUNT };
typedef struct {
    long n[NYEARS][ORIGIN_COUNT];
    long unknown[NYEARS];   // words the dictionary cannot place
} OriginByYear;

const char *originGroupName(int group);   // Turkish names: "Arapça", ...

// The most characteristic words of one era against another.
typedef struct {
    char word[MAX_WORD];
    int origin;          // ORIGIN_*
    long countA, countB;
    double z;            // positive: typical of era A
} EraWord;

// Every noun and adjective used at least VOCAB_MIN_USES times, with its uses per year.
#define VOCAB_MIN_USES 30
typedef struct {
    char word[MAX_WORD];
    int origin;          // ORIGIN_*
    long total;
    int n[NYEARS];
} WordYears;

typedef struct {
    WordYears *words;    // sorted by word
    int count;
    long n[NYEARS];      // all noun and adjective uses per year, rare words included
} Vocabulary;

void loadVocabulary(sqlite3 *db, Vocabulary *out);
void freeVocabulary(Vocabulary *v);

// Uses per million words around each year: a window that widens until it holds at least
// minTokens words. rates[NYEARS], NAN where even the widest window has too few.
void smoothRate(const int *n, const YearCounts *tokens, long minTokens, float *rates);

// The words most typical of the years from..to against all other years (log-odds with a
// Dirichlet prior, as in eraWords). Fills up to top words, most typical first.
int periodWords(const Vocabulary *v, int from, int to, int top, EraWord *out);

// Of the top words of a base decade, how many are still in use later: a word counts as
// alive in a decade while its rate per million is at least a quarter of its base rate.
#define DECADES 11   // the 1920s to the 2020s
typedef struct {
    int base, top;                            // e.g. the 300 most used words of the 1920s
    int nWords[ORIGIN_COUNT + 1];             // per origin; the last is all of them
    float alive[ORIGIN_COUNT + 1][DECADES];   // share alive per decade, NAN without data
    int lost[10], kept[10];                   // examples (indices into the vocabulary): gone,
    int nLost, nKept;                         // and Arabic or Persian words still strong today
} Survival;
void survivalOf(const Vocabulary *v, const YearCounts *tokens, int baseDecade, int top, Survival *out);

typedef struct {
    ConceptStudy *concepts;
    int nConcepts;
    YearCounts tokens;
    OriginByYear origins;
    int eraA[2], eraB[2];
    EraWord early[32], late[32];
    int nEarly, nLate;
    Vocabulary vocab;
    float (*rates)[NYEARS];          // per vocabulary word: smoothRate with 50,000 words
    EraWord decades[DECADES][12];    // periodWords of each decade
    int nDecade[DECADES];
    Survival survival;
    DatingModel dating;              // guessing a text's years from its words
    DatingTest datingTest;           // how well it does on sittings it has not seen
    int sittings;
    long words;
} Study;

// Counts and fits everything the charts and tables need. Builds the form_origin table
// first (see buildFormOrigins).
Study *loadStudy(sqlite3 *db, const char *conceptsPath, int bootstrapRounds);
void freeStudy(Study *s);

// Gives every word form in the corpus one dictionary word and origin, preferring nouns
// and adjectives. Needed before originsByYear and eraWords.
void buildFormOrigins(sqlite3 *db);
void originsByYear(sqlite3 *db, OriginByYear *out);
// Fills forA/forB with up to top words most typical of each era (min uses each side).
void eraWords(sqlite3 *db, int fromA, int toA, int fromB, int toB, int top,
              EraWord *forA, int *nA, EraWord *forB, int *nB);

// ---------------------------------------------------------------- etymology

// Where Wiktionary says a word came from: arabic, persian, french, turkic,
// turkish-derived (built in Turkish: önem + -li, or coined in the Language Reform), ...
const char *originOf(const char *etymology);
// For a derived word ("From kitap + -çı"), the word it was built from, folded; false if
// the etymology is not of that form. A verb stem ("kur- + -ul") becomes its infinitive.
bool etymologyRoot(const char *etymology, char *root);
int originGroup(const char *origin);   // ORIGIN_* for an origin name

#endif
