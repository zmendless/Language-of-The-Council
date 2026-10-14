#ifndef QUERY_H
#define QUERY_H

#include "db.h"
#include "text.h"

#include <stdbool.h>

#define FIRST_YEAR 1900
#define LAST_YEAR 2049
#define NYEARS (LAST_YEAR - FIRST_YEAR + 1)

// A number per year: n[year - FIRST_YEAR].
typedef struct {
    long n[NYEARS];
} YearCounts;

// One competing word for a meaning, from data/concepts.tsv.
typedef struct {
    char concept[MAX_WORD];   // the meaning, named by today's word ("yıl")
    char role[16];            // "old" (Ottoman word), "proposal" (1935 guide) or "new" (today's word)
    char lemma[MAX_WORD];     // folded dictionary form
    char group[32];           // optional fourth column: a theme (meclis, devlet, genel, eğitim)
} Competitor;

typedef struct {
    Competitor *items;
    int count;
} ConceptList;

bool loadConcepts(const char *path, ConceptList *out);

// The dictionary word a folded form belongs to: hükümeti → hükumet (Wiktionary calls
// hükümet a misspelling of hükûmet). A word that is a dictionary word itself is kept.
// Unknown words are returned unchanged. lemma must hold MAX_WORD bytes.
void lemmaOf(sqlite3 *db, const char *word, char *lemma);

// Replaces each competitor word written in a variant spelling by the main spelling
// (hükümet → hükûmet), printing a note for every word that changes.
void resolveConcepts(sqlite3 *db, ConceptList *cl);

// Removes the form-to-word links listed in an exclusions file (word <TAB> form <TAB> reason):
// forms that a check of their contexts (evo kwic) showed to mean something else, like
// örneğin ("for example") for örnek. Call after ensureParadigm. Returns how many it read.
int applyExclusions(sqlite3 *db, const char *path);
// exclusions.tsv in the same folder as the concepts file.
void exclusionsPathFor(const char *conceptsPath, char *out, int size);

// Occurrences per year of every form of lemma (all its inflections in the lexicon,
// plus the lemma itself even when the lexicon does not know it).
void countByYear(sqlite3 *db, const char *lemma, YearCounts *out);

// Words per year in the whole corpus.
void tokensByYear(sqlite3 *db, YearCounts *out);

// Prints forms of lemma that also belong to another dictionary word ("talebe" is both
// "student" and the dative of "talep"). Their counts need checking by hand.
void warnAmbiguous(sqlite3 *db, const char *lemma);

// Adds the generated noun forms of lemma (see nounForms) to the lexicon, except forms
// that belong to another dictionary word. Wiktionary has full tables for some words and
// almost none for others (misal: 3 forms, örnek: 66); without this, competing words would
// be counted unequally. Returns how many forms lemma has afterwards.
int ensureParadigm(sqlite3 *db, const char *lemma);

#endif
