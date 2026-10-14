#ifndef DATING_H
#define DATING_H

#include "query.h"

// Guessing when a text was written from its words alone.
//
// Each 5-year period has its own word frequencies: sene in 1925, yıl in 2015. A text is
// most likely from the period whose frequencies explain its words best. This is a naive
// Bayes classifier (the method of the first spam filters): it adds up, word by word, how
// much more likely the word is in one period than in another.
//
// Only dictionary words count (forms in the lexicon), so the model reads the language and
// not the names of the ministers of the day.
#define DATE_FIRST 1920
#define DATE_STEP 5
#define DATE_BINS 21          // 1920-24, 1925-29, ..., 2020-24
#define DATE_FORMS 20000      // the most used dictionary word forms
#define DATE_PRIOR 2000.0     // pulls a rare word's frequency towards its overall one
#define DATE_MIN_TOKENS 20000 // a period with fewer words is never guessed

typedef struct {
    char form[MAX_WORD];
    long n[DATE_BINS];
    long total;
} DateForm;

typedef struct {
    DateForm *forms;          // sorted by form, for dateFormIndex
    int count;
    long tokens[DATE_BINS];   // uses of the kept forms per period
    long total;
} DatingModel;

int dateBin(int year);             // the period of a year; -1 outside 1920-2024
int dateBinMiddle(int bin);        // 1922 for 1920-24

void loadDatingModel(sqlite3 *db, DatingModel *m);
void freeDatingModel(DatingModel *m);
int dateFormIndex(const DatingModel *m, const char *form);   // -1 when the form is not kept

// A text as word counts: form is an index into the model, or -1 for a word it does not know.
typedef struct {
    int form, n;
} FormUse;

// Tokenizes and folds text into uses (malloc'd); returns how many.
int textUses(const DatingModel *m, const char *text, FormUse **uses);

// Log-likelihood of the uses in each period (-INFINITY for a period without data);
// returns the most likely period.
int dateUses(const DatingModel *m, const FormUse *uses, int count, double *logLik);

// The year a form is typical of: the middle of the periods weighted by its rate in each.
// spread is how far the use is spread around that year (in years; about 30 for a word
// used evenly over the century).
void formYear(const DatingModel *m, int form, double *year, double *spread);

// Takes the sittings of the volume that holds docId out of the model (sign -1) or puts
// them back (+1). A volume's sittings are days apart and share their topics, so a test is
// honest only when the whole volume is out.
void holdOutVolume(sqlite3 *db, DatingModel *m, const char *docId, int sign);

// How well the model dates text it has not seen. Each sitting is dated with its volume
// held out: whole, and as random samples of 20, 50, 200 and 1000 of its words.
#define DATE_LENGTHS 5
typedef struct {
    int lengths[DATE_LENGTHS];         // 0: the whole sitting
    int tested[DATE_LENGTHS];
    double meanError[DATE_LENGTHS];    // years between the true year and the guessed period's middle
    double within5[DATE_LENGTHS];      // share of guesses at most 5 years off
    double chanceError;                // always guessing the middle of the corpus
    int nSittings;
    int *year, *guess;                 // per sitting, whole: the true year and the guessed middle
} DatingTest;

void testDating(sqlite3 *db, DatingModel *m, uint64_t seed, DatingTest *out);
void freeDatingTest(DatingTest *t);

#endif
