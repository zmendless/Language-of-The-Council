#ifndef CLEAN_H
#define CLEAN_H

#include <stdbool.h>

// Which sitting a transcript file holds, from its name:
// tbmm08012073 = TBMM, term 8, volume 12, sitting 73; mm__01001001 = Millet Meclisi.
typedef struct {
    char chamber[8];   // "TBMM" or "MM"
    int term, volume, sitting;
} SittingId;

typedef struct {
    int year, month, day;
} Date;

typedef struct {
    int linesIn;       // lines in the raw text
    int frontMatter;   // lines before the proceedings start (contents pages, previous-minutes summary)
    int backMatter;    // lines after the last session closes (vote lists, bills, written answers)
    int headings;      // all-caps and numbered headings, opening and closing times
    int furniture;     // page numbers and other lines without words
    int labels;        // speaker labels removed from the start of a turn
    int linesOut;      // lines of speech kept
} CleanStats;

bool parseSittingId(const char *name, SittingId *id);

// The years a term sat. Dates outside them are OCR errors.
bool termYears(const SittingId *id, int *first, int *last);

// Finds the sitting's date near the top of the raw text. Dates from 1920-1925 are in the
// Rumi calendar (23.4.1336) and are converted.
bool parseRecordDate(const char *raw, const SittingId *id, Date *out);

// Returns a new string (free it) with only the proceedings: the contents pages before them
// and the appendices after them cut, headings, page numbers and speaker labels ("BAŞKAN —")
// removed, and hyphenated words rejoined.
// Speaker labels matter most: they contain the very words we count (REİS, BAŞKAN,
// VEKİLİ, BAKANI), and how the clerks wrote them says nothing about the speakers.
char *cleanTranscript(const char *raw, CleanStats *stats);

#endif
