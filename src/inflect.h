#ifndef INFLECT_H
#define INFLECT_H

#include "text.h"

// Generates the forms of a noun by Turkish vowel harmony: plural, possessive and case
// endings and their combinations (yıl → yıllar, yılı, yılda, yıllarının, yılındaki, ...).
//
// Loanwords bend the rules, so the variants are generated too: suffixes with front vowels
// after a back vowel (misal → misali, saat → saatte), a softened final consonant
// (kitap → kitabı), a dropped vowel (akıl → aklı) and a doubled consonant (hak → hakkı).
// The variants that are not real words (misalı, kitapı) never occur in the transcripts,
// so they do no harm.
//
// lemma must be folded (see trFold). Writes up to max forms; returns how many.
int nounForms(const char *lemma, char (*out)[MAX_WORD], int max);

#endif
