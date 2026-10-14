#include "inflect.h"

#include <stdlib.h>
#include <string.h>

// Code points of the Turkish letters that are not ASCII.
enum { C_CEDIL = 0xE7, G_BREVE = 0x11F, DOTLESS_I = 0x131, O_UML = 0xF6, S_CEDIL = 0x15F, U_UML = 0xFC };

static bool isVowel(uint32_t c) {
    return c == 'a' || c == 'e' || c == DOTLESS_I || c == 'i' || c == 'o' || c == O_UML || c == 'u' || c == U_UML;
}

static bool isBack(uint32_t c) {
    return c == 'a' || c == DOTLESS_I || c == 'o' || c == 'u';
}

static bool isRounded(uint32_t c) {
    return c == 'o' || c == O_UML || c == 'u' || c == U_UML;
}

static bool isHighVowel(uint32_t c) {
    return c == DOTLESS_I || c == 'i' || c == 'u' || c == U_UML;
}

// "Fıstıkçı şahap": after these, d in a suffix becomes t (kitapta, not kitapda).
static bool isVoiceless(uint32_t c) {
    return c == 'f' || c == 's' || c == 't' || c == 'k' || c == C_CEDIL || c == S_CEDIL || c == 'h' || c == 'p';
}

typedef struct {
    uint32_t cp[MAX_WORD];
    int n;
    uint32_t lastVowel;   // the vowel the next suffix harmonises with
    bool front;           // loanword exception: the next suffix vowel is front (misal → misali)
} Word;

static void addLetter(Word *w, uint32_t c) {
    if(w->n < MAX_WORD / 2) w->cp[w->n++] = c;   // a code point is at most 2 bytes here
    if(isVowel(c)) {
        w->lastVowel = c;
        w->front = false;
    }
}

static bool endsInVowel(const Word *w) {
    return w->n > 0 && isVowel(w->cp[w->n - 1]);
}

// Two-way harmony: a after a back vowel, e after a front one.
static uint32_t vowelA(const Word *w) {
    return w->front || !isBack(w->lastVowel) ? 'e' : 'a';
}

// Four-way harmony: ı i u ü, by backness and rounding of the last vowel.
static uint32_t vowelI(const Word *w) {
    bool back = !w->front && isBack(w->lastVowel);
    if(isRounded(w->lastVowel)) return back ? 'u' : U_UML;
    return back ? DOTLESS_I : 'i';
}

// Appends a suffix written the usual way: A = a/e, I = ı/i/u/ü, D = d/t. A letter in
// parentheses is used only after a vowel ((y), (s), (n)); (I) is used only after a consonant.
static void addSuffix(Word *w, const char *suffix) {
    for(const char *p = suffix; *p;) {
        if(*p == '(') {
            char c = p[1];
            p += 3;
            if(c == 'I') {
                if(!endsInVowel(w)) addLetter(w, vowelI(w));
            } else if(endsInVowel(w)) {
                addLetter(w, (uint32_t)c);
            }
            continue;
        }
        uint32_t c = (unsigned char)*p++;
        if(c == 'A') c = vowelA(w);
        else if(c == 'I') c = vowelI(w);
        else if(c == 'D') c = w->n > 0 && isVoiceless(w->cp[w->n - 1]) ? 't' : 'd';
        addLetter(w, c);
    }
}

// Second person singular ("your X", (I)n) is left out on purpose: it is almost never said
// in parliament and its forms collide with frequent words (doğa → doğan, "born").
static const char *POSSESSIVE[] = {"", "(I)m", NULL, "(s)I", "(I)mIz", "(I)nIz", "lArI"};
// After a third-person possessive the case endings take n: evini, evine, evinde.
static const char *CASE[] = {"", "(y)I", "(y)A", "DA", "DAn", "(n)In", "(y)lA", "DAki"};
static const char *CASE_AFTER_3RD[] = {"", "nI", "nA", "ndA", "ndAn", "(n)In", "(y)lA", "ndAki"};

typedef struct {
    char (*out)[MAX_WORD];
    int n, max;
} FormList;

static void emit(FormList *list, const Word *w) {
    if(list->n == list->max) return;
    char *s = list->out[list->n];
    int len = 0;
    for(int i = 0; i < w->n; i++) {
        char enc[4];
        int k = utf8Encode(w->cp[i], enc);
        if(len + k >= MAX_WORD) return;
        memcpy(s + len, enc, k);
        len += k;
    }
    s[len] = 0;
    list->n++;
}

// Does this ending start with a vowel after a consonant? (I)m, (s)I, (y)A and (n)In do;
// lAr, DA and (y)lA do not.
static bool vowelInitial(const char *suffix) {
    if(suffix[0] == 'A' || suffix[0] == 'I') return true;
    if(suffix[0] == '(') return suffix[1] == 'I' || suffix[3] == 'A' || suffix[3] == 'I';
    return false;
}

// All number, possessive and case combinations on one stem. vowelStem is used when the
// first ending starts with a vowel (kitab-ı), stem otherwise (kitap-lar).
static void paradigm(FormList *list, const Word *stem, const Word *vowelStem) {
    for(int plural = 0; plural < 2; plural++) {
        for(int poss = 0; poss < 7; poss++) {
            if(!POSSESSIVE[poss]) continue;
            for(int c = 0; c < 8; c++) {
                const char *first = plural ? "lAr" : poss ? POSSESSIVE[poss] : CASE[c];
                Word w = vowelInitial(first) ? *vowelStem : *stem;
                if(plural) addSuffix(&w, "lAr");
                if(poss) addSuffix(&w, POSSESSIVE[poss]);
                bool third = poss == 3 || poss == 6;
                addSuffix(&w, third ? CASE_AFTER_3RD[c] : CASE[c]);
                emit(list, &w);
            }
        }
    }
}

static int compareForms(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

int nounForms(const char *lemma, char (*out)[MAX_WORD], int max) {
    Word base = {{0}, 0, 'a', false};
    int syllables = 0;
    for(const char *p = lemma; *p;) {
        uint32_t c;
        p += utf8Decode(p, &c);
        addLetter(&base, c);
        if(isVowel(c)) syllables++;
    }
    if(base.n == 0 || syllables == 0) return 0;

    // Stems used before a vowel: the word itself and the loanword changes that may apply.
    Word stems[6];
    int nStems = 0;
    stems[nStems++] = base;
    uint32_t last = base.cp[base.n - 1];
    uint32_t prev = base.n > 1 ? base.cp[base.n - 2] : 0;
    uint32_t soft = last == 'p' ? 'b' : last == C_CEDIL ? 'c' : last == 't' ? 'd'
                  : last == 'k' ? (prev == 'n' ? 'g' : G_BREVE) : 0;
    if(soft && syllables >= 2) {   // kitap → kitab-ı, ağaç → ağac-ı, köpek → köpeğ-i
        Word w = base;
        w.cp[w.n - 1] = soft;
        stems[nStems++] = w;
    }
    // akıl → akl-ı, isim → ism-i: two syllables ending consonant + high vowel + consonant.
    if(syllables == 2 && base.n >= 3 && !isVowel(last) && isHighVowel(prev) && !isVowel(base.cp[base.n - 3])) {
        Word w = base;
        w.cp[w.n - 2] = last;
        w.n--;
        stems[nStems++] = w;
        if(soft) {   // kayıp → kayb-ı
            w.cp[w.n - 1] = soft;
            stems[nStems++] = w;
        }
    }
    if(syllables == 1 && !isVowel(last)) {   // hak → hakk-ı, his → hiss-i, ret → redd-i
        Word w = base;
        addLetter(&w, last);
        stems[nStems++] = w;
        if(soft) {
            w.cp[w.n - 1] = soft;
            w.cp[w.n - 2] = soft;
            stems[nStems++] = w;
        }
    }

    FormList list = {out, 0, max};
    for(int front = 0; front < 2; front++) {
        if(front && !isBack(base.lastVowel)) break;   // the exception only matters after a back vowel
        Word stem = base;
        stem.front = front;
        for(int s = 0; s < nStems; s++) {
            Word vowelStem = stems[s];
            vowelStem.front = front;
            paradigm(&list, &stem, &vowelStem);
        }
    }

    // Sort and drop duplicates.
    qsort(out, list.n, MAX_WORD, compareForms);
    int unique = 0;
    for(int i = 0; i < list.n; i++) {
        if(unique == 0 || strcmp(out[i], out[unique - 1]) != 0) {
            if(unique != i) memcpy(out[unique], out[i], MAX_WORD);
            unique++;
        }
    }
    return unique;
}
