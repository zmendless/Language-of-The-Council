#include "text.h"

#include <string.h>

int utf8Decode(const char *s, uint32_t *cp) {
    const unsigned char *u = (const unsigned char *)s;
    if(u[0] < 0x80) { *cp = u[0]; return 1; }
    int n;
    uint32_t c, min;
    if((u[0] & 0xE0) == 0xC0) { n = 2; c = u[0] & 0x1F; min = 0x80; }
    else if((u[0] & 0xF0) == 0xE0) { n = 3; c = u[0] & 0x0F; min = 0x800; }
    else if((u[0] & 0xF8) == 0xF0) { n = 4; c = u[0] & 0x07; min = 0x10000; }
    else { *cp = 0xFFFD; return 1; }
    for(int i = 1; i < n; i++) {
        // Also stops at the terminating zero, which is not a continuation byte.
        if((u[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        c = (c << 6) | (u[i] & 0x3F);
    }
    if(c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) { *cp = 0xFFFD; return 1; }
    *cp = c;
    return n;
}

int utf8Encode(uint32_t cp, char *out) {
    if(cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if(cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if(cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

// Latin Extended-A (U+0100-U+017F) holds Ğ ğ İ ı Ş ş. Its letters come in capital/small
// pairs, capital first, except in two stretches where the order flips.
static bool latinExtAUpper(uint32_t cp) {
    if(cp == 0x130 || cp == 0x178) return true;                                // İ Ÿ
    if(cp == 0x131 || cp == 0x138 || cp == 0x149 || cp == 0x17F) return false; // ı ĸ ŉ ſ
    if((cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E)) return cp % 2 == 1;
    return cp % 2 == 0;
}

bool trIsUpper(uint32_t cp) {
    if(cp >= 'A' && cp <= 'Z') return true;
    if(cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return true;
    if(cp >= 0x100 && cp <= 0x17F) return latinExtAUpper(cp);
    return false;
}

bool trIsLower(uint32_t cp) {
    if(cp >= 'a' && cp <= 'z') return true;
    if(cp >= 0xDF && cp <= 0xFF && cp != 0xF7) return true;
    if(cp >= 0x100 && cp <= 0x17F) return !latinExtAUpper(cp);
    return false;
}

bool trIsLetter(uint32_t cp) {
    return trIsUpper(cp) || trIsLower(cp);
}

uint32_t trLower(uint32_t cp) {
    if(cp == 'I') return 0x131;   // I → ı
    if(cp == 0x130) return 'i';   // İ → i
    if(cp >= 'A' && cp <= 'Z') return cp + 32;
    if(cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 32;
    if(cp == 0x178) return 0xFF;
    if(cp >= 0x100 && cp <= 0x17F && latinExtAUpper(cp)) return cp + 1;
    return cp;
}

uint32_t trFold(uint32_t cp) {
    cp = trLower(cp);
    if(cp == 0xE2) return 'a';   // â
    if(cp == 0xEE) return 'i';   // î (and Î, which trLower turned into î)
    if(cp == 0xFB) return 'u';   // û
    return cp;
}

int trFoldString(const char *in, char *out, int outSize) {
    int n = 0;
    while(*in) {
        uint32_t cp;
        in += utf8Decode(in, &cp);
        char buf[4];
        int k = utf8Encode(trFold(cp), buf);
        if(n + k >= outSize) break;
        memcpy(out + n, buf, k);
        n += k;
    }
    out[n] = 0;
    return n;
}

double trTurkishLetterShare(const char *text) {
    long letters = 0, turkish = 0;
    while(*text) {
        uint32_t cp;
        text += utf8Decode(text, &cp);
        if(!trIsLetter(cp)) continue;
        letters++;
        uint32_t c = trLower(cp);
        if(c == 0xE7 || c == 0x11F || c == 0x131 || c == 0xF6 || c == 0x15F || c == 0xFC || cp == 0x130) turkish++;
    }
    return letters ? (double)turkish / letters : 0.0;
}

static bool isApostrophe(uint32_t cp) {
    return cp == '\'' || cp == '`' || cp == 0xB4 || cp == 0x2BC || cp == 0x2018 || cp == 0x2019;
}

static bool isHighVowel(uint32_t cp) {
    return cp == 'i' || cp == 0x131 || cp == 'u' || cp == 0xFC;   // i ı u ü
}

// "3 üncü", "3 ncü", "2 nci": an optional i/ı/u/ü, then n, c and i/ı/u/ü.
// OCR sometimes reads the c as ç or e ("65 nei Birleşim").
static bool isOrdinalSuffix(const char *w) {
    uint32_t cp[4];
    int n = 0;
    while(*w) {
        if(n == 4) return false;
        w += utf8Decode(w, &cp[n++]);
    }
    int i = 0;
    if(n == 4) {
        if(!isHighVowel(cp[0])) return false;
        i = 1;
    } else if(n != 3) {
        return false;
    }
    return cp[i] == 'n' && (cp[i + 1] == 'c' || cp[i + 1] == 0xE7 || cp[i + 1] == 'e') && isHighVowel(cp[i + 2]);
}

// Case and copula endings that follow an apostrophe after a name ("Ankara'da").
static const char *SUFFIXES[] = {
    "da", "de", "ta", "te", "dan", "den", "tan", "ten", "ya", "ye", "na", "ne",
    "yı", "yi", "yu", "yü", "nı", "ni", "nu", "nü", "ın", "in", "un", "ün",
    "nın", "nin", "nun", "nün", "la", "le", "yla", "yle", "lar", "ler", "ları", "leri",
    "daki", "deki", "taki", "teki", "nda", "nde", "ndan", "nden",
    "dır", "dir", "dur", "dür", "tır", "tir", "tur", "tür",
};

static bool isSuffix(const char *w) {
    for(size_t i = 0; i < sizeof SUFFIXES / sizeof SUFFIXES[0]; i++) {
        if(strcmp(w, SUFFIXES[i]) == 0) return true;
    }
    return false;
}

// Reads the letters that follow an apostrophe: folds them into buf and returns where they end.
static const char *readLetters(const char *p, char *buf, int size) {
    int n = 0;
    for(;;) {
        uint32_t cp;
        int k = utf8Decode(p, &cp);
        if(cp == 0xAD) { p += k; continue; }
        if(!trIsLetter(cp)) break;
        char enc[4];
        int m = utf8Encode(trFold(cp), enc);
        if(n + m < size) { memcpy(buf + n, enc, m); n += m; }
        p += k;
    }
    buf[n] = 0;
    return p;
}

void tokInit(Tokenizer *t, const char *line) {
    t->line = line;
    t->p = line;
    t->afterNumber = false;
}

bool tokNext(Tokenizer *t, char *word, int *start, int *len) {
    for(;;) {
        const char *p = t->p;
        if(!*p) return false;
        uint32_t cp;
        int k = utf8Decode(p, &cp);

        if(cp >= '0' && cp <= '9') {
            while(*p >= '0' && *p <= '9') p++;
            uint32_t next;
            int k2 = utf8Decode(p, &next);
            if(isApostrophe(next)) {   // 1948'de: the suffix belongs to the number
                char suffix[MAX_WORD];
                p = readLetters(p + k2, suffix, sizeof suffix);
            }
            t->p = p;
            t->afterNumber = true;
            continue;
        }
        if(!trIsLetter(cp)) {
            // Spaces and dots between a number and its ordinal suffix keep the link ("3 üncü").
            if(cp != ' ' && cp != '\t' && cp != '.') t->afterNumber = false;
            t->p = p + k;
            continue;
        }

        const char *begin = p;
        bool capital = trIsUpper(cp);
        bool tooLong = false;
        int n = 0;
        for(;;) {
            k = utf8Decode(p, &cp);
            if(cp == 0) break;
            if(trIsLetter(cp)) {
                char enc[4];
                int m = utf8Encode(trFold(cp), enc);
                if(n + m < MAX_WORD) { memcpy(word + n, enc, m); n += m; }
                else tooLong = true;
                p += k;
                continue;
            }
            uint32_t after;
            int k2 = utf8Decode(p + k, &after);
            (void)k2;
            if(cp == 0xAD && trIsLetter(after)) {   // soft hyphen: invisible, joins
                p += k;
                continue;
            }
            if(isApostrophe(cp) && trIsLetter(after)) {
                char suffix[MAX_WORD];
                const char *end = readLetters(p + k, suffix, sizeof suffix);
                if(capital || (strlen(suffix) >= 2 && isSuffix(suffix))) {
                    p = end;   // Türkiye'nin, istanbul'un: keep the stem only
                    break;
                }
                p += k;        // mes'ele → mesele: an Ottoman spelling mark inside the word
                continue;
            }
            break;
        }
        word[n] = 0;
        t->p = p;
        if(tooLong) continue;   // OCR garbage or a run-together line
        if(t->afterNumber && isOrdinalSuffix(word)) {
            t->afterNumber = false;
            continue;
        }
        t->afterNumber = false;
        if(start) *start = (int)(begin - t->line);
        if(len) *len = (int)(p - begin);
        return true;
    }
}
