#ifndef TEXT_H
#define TEXT_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_WORD 64   // bytes per word, including the terminating zero

// UTF-8. utf8Decode reads one code point and returns how many bytes it used; an invalid
// byte decodes as U+FFFD and uses one byte, so loops always move forward.
int utf8Decode(const char *s, uint32_t *cp);
int utf8Encode(uint32_t cp, char *out);

// Turkish has two separate i letters (I/ı and İ/i), so the C library's tolower() and
// towlower() give wrong answers. These functions know about them.
bool trIsLetter(uint32_t cp);
bool trIsUpper(uint32_t cp);
bool trIsLower(uint32_t cp);
uint32_t trLower(uint32_t cp);

// Lowercase and drop the circumflex (â→a, î→i, û→u). Every word we count goes through
// this, so kâtip and katip, or MİLLÎ and millî, are the same word. Î also maps to i,
// because OCR writes the capital İ as Î in all-caps headings (BÎRÎNCÎ).
uint32_t trFold(uint32_t cp);
int trFoldString(const char *in, char *out, int outSize);

// Share of letters that only Turkish has (ç ğ ı ö ş ü and capitals): about 7-12% in real
// Turkish text. Near 0 means the PDF was OCR'd without Turkish (Yüksek → "Yiiksek").
double trTurkishLetterShare(const char *text);

// Splits a line into words. Each call to tokNext writes the next folded word and where
// it sits in the line (for keyword-in-context output). Rules:
// - Türkiye'nin → türkiye: after a capitalised word, the apostrophe starts a suffix.
// - mes'ele → mesele: inside a lowercase word, the apostrophe is an Ottoman spelling
//   mark (ayn/hamza) and the word is kept whole.
// - Numbers are skipped, and so are their suffixes: 1948'de, "3 üncü", "3 ncü".
// - The soft hyphen (U+00AD) is invisible and never splits a word.
typedef struct {
    const char *line;
    const char *p;
    bool afterNumber;
} Tokenizer;

void tokInit(Tokenizer *t, const char *line);
// word must hold MAX_WORD bytes. start/len are byte offsets into the line (may be NULL).
bool tokNext(Tokenizer *t, char *word, int *start, int *len);

#endif
