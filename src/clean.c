#include "clean.h"
#include "text.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

bool parseSittingId(const char *name, SittingId *id) {
    if(strncmp(name, "tbmm", 4) == 0) strcpy(id->chamber, "TBMM");
    else if(strncmp(name, "mm__", 4) == 0) strcpy(id->chamber, "MM");
    else return false;
    const char *d = name + 4;
    for(int i = 0; i < 8; i++) {
        if(!isdigit((unsigned char)d[i])) return false;   // e.g. the index volumes, tbmm08012fih
    }
    id->term = (d[0] - '0') * 10 + (d[1] - '0');
    id->volume = (d[2] - '0') * 100 + (d[3] - '0') * 10 + (d[4] - '0');
    id->sitting = (d[5] - '0') * 100 + (d[6] - '0') * 10 + (d[7] - '0');
    return true;
}

typedef struct {
    const char *chamber;
    int term, first, last;
} Term;

// Parliamentary terms (yasama dönemleri). There was no elected parliament from May 1960
// to October 1961 or from September 1980 to November 1983. From 1961 to 1980 the lower
// house was the Millet Meclisi, numbered from 1 again.
static const Term TERMS[] = {
    {"TBMM", 1, 1920, 1923}, {"TBMM", 2, 1923, 1927}, {"TBMM", 3, 1927, 1931}, {"TBMM", 4, 1931, 1935},
    {"TBMM", 5, 1935, 1939}, {"TBMM", 6, 1939, 1943}, {"TBMM", 7, 1943, 1946}, {"TBMM", 8, 1946, 1950},
    {"TBMM", 9, 1950, 1954}, {"TBMM", 10, 1954, 1957}, {"TBMM", 11, 1957, 1960},
    {"MM", 1, 1961, 1965}, {"MM", 2, 1965, 1969}, {"MM", 3, 1969, 1973}, {"MM", 4, 1973, 1977}, {"MM", 5, 1977, 1980},
    {"TBMM", 17, 1983, 1987}, {"TBMM", 18, 1987, 1991}, {"TBMM", 19, 1991, 1995}, {"TBMM", 20, 1995, 1999},
    {"TBMM", 21, 1999, 2002}, {"TBMM", 22, 2002, 2007}, {"TBMM", 23, 2007, 2011}, {"TBMM", 24, 2011, 2015},
    {"TBMM", 25, 2015, 2015}, {"TBMM", 26, 2015, 2018}, {"TBMM", 27, 2018, 2023}, {"TBMM", 28, 2023, 2028},
};

bool termYears(const SittingId *id, int *first, int *last) {
    for(size_t i = 0; i < sizeof TERMS / sizeof TERMS[0]; i++) {
        if(TERMS[i].term == id->term && strcmp(TERMS[i].chamber, id->chamber) == 0) {
            *first = TERMS[i].first;
            *last = TERMS[i].last;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- dates

typedef struct {
    const char *name;
    int number;
} Named;

// Folded spellings. Before 1945 the Rumi month names were used (Kânunusani = January).
static const Named MONTHS[] = {
    {"ocak", 1}, {"şubat", 2}, {"mart", 3}, {"nisan", 4}, {"mayıs", 5}, {"haziran", 6},
    {"temmuz", 7}, {"ağustos", 8}, {"eylül", 9}, {"ekim", 10}, {"kasım", 11}, {"aralık", 12},
    {"kanunusani", 1}, {"teşrinievvel", 10}, {"teşrinisani", 11}, {"kanunuevvel", 12},
};

static const Named WEEKDAYS[] = {
    {"pazar", 0}, {"pazartesi", 1}, {"salı", 2}, {"çarşamba", 3}, {"perşembe", 4}, {"cuma", 5}, {"cumartesi", 6},
};

static int lookup(const Named *table, int count, const char *word) {
    for(int i = 0; i < count; i++) {
        if(strcmp(table[i].name, word) == 0) return table[i].number;
    }
    return -1;
}

// 0 = Sunday (Sakamoto's method).
static int dayOfWeek(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if(m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// Reads a run of letters from a folded line into word; returns the position after it.
static const char *readWord(const char *p, char *word, int size) {
    int n = 0;
    for(;;) {
        uint32_t cp;
        int k = utf8Decode(p, &cp);
        if(!trIsLetter(cp)) break;
        if(n + k < size) { memcpy(word + n, p, k); n += k; }
        p += k;
    }
    word[n] = 0;
    return p;
}

// Between the parts of a date: spaces, dots, commas, dashes, slashes, and the stray marks
// OCR leaves behind ("14 . XI* 1951", "22 II • 1337", "2 6 - V I »1944", "13 • V< 1946").
static const char *skipDateSeparators(const char *p) {
    for(;;) {
        if(*p && strchr(" .,-/*'\"`_~<>:;=", *p)) { p++; continue; }
        uint32_t cp;
        int k = utf8Decode(p, &cp);
        if(cp == 0x2022 || cp == 0xBB || cp == 0xAB || cp == 0xB7 || cp == 0x2013 || cp == 0x2014) { p += k; continue; }
        return p;
    }
}

// OCR misreads digits as look-alike letters (197S, 19S1, l948). In a folded line.
static int ocrDigit(uint32_t cp) {
    if(cp >= '0' && cp <= '9') return (int)(cp - '0');
    switch(cp) {
        case 'o': return 0;
        case 'l': case 'i': case 0x131: case '|': return 1;   // 0x131 = ı
        case 'z': return 2;
        case 's': return 5;
        case 'b': return 8;
        case 'g': case 'q': return 9;
    }
    return -1;
}

// Roman numeral months as OCR sees them in a folded line: I may come out as ı, i, l, 1 or |.
static int romanMonth(const char *p, const char **end) {
    int values[8], n = 0;
    *end = p;
    for(;;) {
        int v;
        if(*p == 'v') { v = 5; p++; }
        else if(*p == 'x') { v = 10; p++; }
        else if(*p == 'i' || *p == 'l' || *p == '1' || *p == '|') { v = 1; p++; }
        else if((unsigned char)p[0] == 0xC4 && (unsigned char)p[1] == 0xB1) { v = 1; p += 2; }   // ı
        else break;
        if(n == 8) return -1;
        values[n++] = v;
    }
    *end = p;
    int total = 0;
    for(int i = 0; i < n; i++) {
        total += (i + 1 < n && values[i] < values[i + 1]) ? -values[i] : values[i];
    }
    return (n > 0 && total >= 1 && total <= 12) ? total : -1;
}

// Score of the date written at p (a day number), or -100 if p holds no valid date.
// The printed weekday decides between readings: "1 - 111 - 1935 Cuma" is 1 March 1935
// (III read as 111), because that day was a Friday. following holds the next lines,
// where the weekday is often printed.
static int scoreDate(const char *p, const char *const *following, int nFollowing, const SittingId *id, Date *out) {
    // The day: one or two digits; the second may be misread as a letter ("2L" for 21).
    if(!isdigit((unsigned char)*p)) return -100;
    int day = *p++ - '0';
    uint32_t cp, after;
    int k = utf8Decode(p, &cp);
    int second = ocrDigit(cp);
    utf8Decode(p + k, &after);
    if(second >= 0 && (isdigit((unsigned char)*p) || !trIsLetter(after))) {
        day = day * 10 + second;
        p += k;
    }
    if(isdigit((unsigned char)*p) || day < 1 || day > 31) return -100;
    const char *s = skipDateSeparators(p);
    if(s == p) return -100;

    int months[2], nMonths = 0;
    const char *afterMonth = s;
    if(isdigit((unsigned char)*s)) {
        const char *q = s;
        while(isdigit((unsigned char)*q)) q++;
        int len = (int)(q - s);
        if(len <= 2) {
            int m = len == 1 ? s[0] - '0' : (s[0] - '0') * 10 + (s[1] - '0');
            if(m >= 1 && m <= 12) months[nMonths++] = m;
        }
        bool onlyOnes = true;
        for(const char *c = s; c < q; c++) onlyOnes = onlyOnes && *c == '1';
        if(onlyOnes && len >= 2 && len <= 3) months[nMonths++] = len;   // II or III read as 11, 111
        afterMonth = q;
    } else {
        const char *end;
        int m = romanMonth(s, &end);
        uint32_t next;
        utf8Decode(end, &next);
        if(m > 0 && !trIsLetter(next)) {
            months[nMonths++] = m;
            afterMonth = end;
        } else {
            char word[MAX_WORD];
            afterMonth = readWord(s, word, sizeof word);
            m = lookup(MONTHS, sizeof MONTHS / sizeof MONTHS[0], word);
            if(m > 0) months[nMonths++] = m;
        }
    }
    if(nMonths == 0) return -100;

    const char *y = skipDateSeparators(afterMonth);
    if(y == afterMonth) return -100;
    // Four digits, at most one of them misread as a letter (and the term check below
    // throws out any year that a misreading puts outside the term).
    int year = 0, realDigits = 0;
    for(int i = 0; i < 4; i++) {
        uint32_t cp;
        int k = utf8Decode(y, &cp);
        int digit = ocrDigit(cp);
        if(digit < 0) return -100;
        if(cp >= '0' && cp <= '9') realDigits++;
        year = year * 10 + digit;
        y += k;
    }
    if(realDigits < 3 || isdigit((unsigned char)*y)) return -100;
    if(year >= 1334 && year <= 1345) year += 584;   // Rumi calendar, 1918-1925
    if(year < 1919 || year > 2035) return -100;
    int first, last;
    if(termYears(id, &first, &last) && (year < first - 1 || year > last + 1)) return -100;

    // A weekday printed after the date: on the same line or one of the next lines.
    int weekday = -1;
    const char *texts[4] = {y, NULL, NULL, NULL};
    for(int i = 0; i < nFollowing && i < 3; i++) texts[i + 1] = following[i];
    for(int t = 0; t < 4 && weekday < 0 && texts[t]; t++) {
        for(const char *q = texts[t]; *q && weekday < 0;) {
            uint32_t cp;
            int k = utf8Decode(q, &cp);
            if(!trIsLetter(cp)) { q += k; continue; }
            char word[MAX_WORD];
            q = readWord(q, word, sizeof word);
            weekday = lookup(WEEKDAYS, sizeof WEEKDAYS / sizeof WEEKDAYS[0], word);
        }
    }

    int best = -100;
    for(int i = 0; i < nMonths; i++) {
        int score = 1;
        if(weekday >= 0) score = dayOfWeek(year, months[i], day) == weekday ? 4 : -1;
        if(score > best) {
            best = score;
            out->year = year;
            out->month = months[i];
            out->day = day;
        }
    }
    return best;
}

// OCR sometimes spaces out every character of the date: "1 5 - I X - 1 9 4 1". Removes
// the spaces between single characters, giving "15-ıx-1941".
static void collapseSpacedOut(const char *in, char *out, int size) {
    int n = 0;
    for(const char *p = in; *p && n < size - 1;) {
        if(*p == ' ' && p > in) {
            // Single characters on both sides: the one before starts after a space (or the
            // line start), the one after is followed by a space (or the line end).
            const char *before = p - 1;
            while(before > in && ((unsigned char)*before & 0xC0) == 0x80) before--;
            bool singleBefore = before == in || before[-1] == ' ';
            uint32_t cp;
            int k = utf8Decode(p + 1, &cp);
            bool singleAfter = cp != ' ' && cp != 0 && (p[1 + k] == ' ' || p[1 + k] == 0);
            if(singleBefore && singleAfter) { p++; continue; }
        }
        out[n++] = *p++;
    }
    out[n] = 0;
}

bool parseRecordDate(const char *raw, const SittingId *id, Date *out) {
    enum { LINES = 150, LINE = 512 };
    static char folded[LINES][LINE];
    int n = 0;
    for(const char *p = raw; *p && n < LINES; n++) {
        const char *nl = strpbrk(p, "\n\f");
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        char line[LINE];
        if(len >= LINE) len = LINE - 1;
        memcpy(line, p, len);
        line[len] = 0;
        trFoldString(line, folded[n], LINE);
        if(!nl) { n++; break; }
        p = nl + 1;
    }

    int bestScore = -100;
    for(int i = 0; i < n; i++) {
        // The next non-empty lines, where the weekday may be printed.
        const char *following[3];
        int nFollowing = 0;
        for(int j = i + 1; j < n && nFollowing < 3; j++) {
            if(folded[j][strspn(folded[j], " \t")]) following[nFollowing++] = folded[j];
        }
        char collapsed[LINE];
        collapseSpacedOut(folded[i], collapsed, LINE);
        const char *versions[2] = {folded[i], strcmp(collapsed, folded[i]) ? collapsed : NULL};
        for(int v = 0; v < 2 && versions[v]; v++) {
            const char *line = versions[v];
            for(const char *p = line; *p; p++) {
                if(!isdigit((unsigned char)*p) || (p > line && isdigit((unsigned char)p[-1]))) continue;
                Date d;
                int score = scoreDate(p, following, nFollowing, id, &d);
                if(score > bestScore) {   // ties keep the earliest, which is the header date
                    bestScore = score;
                    *out = d;
                }
            }
        }
    }
    return bestScore > -100;
}

// ---------------------------------------------------------------- cleaning

static void trimRight(char *s) {
    size_t n = strlen(s);
    while(n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
}

static const char *skipSpaces(const char *s) {
    while(*s == ' ' || *s == '\t') s++;
    return s;
}

// Start of the last code point in s[0..len).
static int lastCharStart(const char *s, int len) {
    int i = len - 1;
    while(i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    return i;
}

// Does the line end in a word broken by a hyphen ("getir-", "getir­" with a soft hyphen)?
// Returns the byte length of the hyphen, or 0.
static int trailingHyphen(const char *s) {
    int len = (int)strlen(s);
    if(len < 2) return 0;
    int h = lastCharStart(s, len);
    uint32_t cp;
    utf8Decode(s + h, &cp);
    if(cp != '-' && cp != 0xAD && cp != 0x2010) return 0;
    uint32_t before;
    utf8Decode(s + lastCharStart(s, h), &before);
    return trIsLetter(before) ? len - h : 0;
}

static bool startsLowercase(const char *s) {
    uint32_t cp;
    utf8Decode(skipSpaces(s), &cp);
    return trIsLower(cp);
}

static void removeSoftHyphens(char *s) {
    char *w = s;
    for(const char *r = s; *r;) {
        if((unsigned char)r[0] == 0xC2 && (unsigned char)r[1] == 0xAD) { r += 2; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

typedef struct {
    int letters, upper, lower;
} LetterCount;

static LetterCount countLetters(const char *s, const char *end, bool skipParens) {
    LetterCount c = {0, 0, 0};
    int depth = 0;
    while(s < end && *s) {
        uint32_t cp;
        s += utf8Decode(s, &cp);
        if(cp == '(') depth++;
        else if(cp == ')' && depth > 0) depth--;
        else if(trIsLetter(cp) && !(skipParens && depth > 0)) {
            c.letters++;
            if(trIsUpper(cp)) c.upper++;
            else c.lower++;
        }
    }
    return c;
}

// The dash between a speaker label and the speech: an em dash, an en dash, or " - ".
// Returns where the dash character starts; *dashLen is its length in bytes.
static const char *findDash(const char *s, int limit, int *dashLen) {
    for(const char *p = s; *p && p - s < limit; p++) {
        if((unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x80 && ((unsigned char)p[2] == 0x94 || (unsigned char)p[2] == 0x93)) {
            *dashLen = 3;
            return p;
        }
        if(p[0] == ' ' && p[1] == '-' && (p[2] == ' ' || p[2] == 0)) {
            *dashLen = 1;
            return p + 1;
        }
    }
    return NULL;
}

// Where the speech starts if the line opens with a speaker label, else s itself.
//   "BAŞKAN — Buyurun."                       "SALİH KAPUSUZ (Ankara) - Yok efendim."
//   "R E l S SÎN — Burayı teşrif etsinler."   (OCR noise: a few small letters are allowed)
static const char *stripLabel(const char *s, bool *found) {
    *found = false;
    const char *p = s;
    while(*p == ' ' || *p == '\'' || *p == '"' || *p == '`' || *p == '.' || *p == ',') p++;

    int dashLen;
    const char *dash = findDash(p, 120, &dashLen);
    if(dash && dash > p) {
        LetterCount c = countLetters(p, dash, true);
        if(c.letters >= 2 && c.upper * 4 >= c.letters * 3) {
            *found = true;
            return skipSpaces(dash + dashLen);
        }
    }

    // A label that wrapped and lost its dash: two or more all-caps words at the start
    // of the line ("MUSTAFA KEMAL Pş. (Ankara) Devamla").
    const char *q = p, *runEnd = NULL;
    int words = 0;
    for(;;) {
        q = skipSpaces(q);
        const char *w = q;
        int letters = 0;
        bool allUpper = true;
        for(;;) {
            uint32_t cp;
            int k = utf8Decode(q, &cp);
            if(!trIsLetter(cp)) break;
            if(!trIsUpper(cp)) allUpper = false;
            letters++;
            q += k;
        }
        if(q == w || !allUpper || letters < 2) break;
        words++;
        runEnd = q;
    }
    if(words >= 2) {
        *found = true;
        q = skipSpaces(runEnd);
        if(*q == '(') {
            const char *close = strchr(q, ')');
            if(close) q = skipSpaces(close + 1);
        }
        const char *d = findDash(q, 4, &dashLen);
        if(d) q = skipSpaces(d + dashLen);
        return q;
    }
    return s;
}

// "1 — Azanın and içmesi", "I. - BAŞKANLIĞIN GENEL KURULA SUNUŞLARI", and OCR's "S. —" for "3. —".
static bool isNumberedHeading(const char *s) {
    const char *p = skipSpaces(s);
    int n = 0;
    while(isdigit((unsigned char)*p) && n < 3) { p++; n++; }
    if(n == 0) {
        while(*p && strchr("IVXL", *p) && n < 6) { p++; n++; }
    }
    if(n == 0) {
        uint32_t cp;
        int k = utf8Decode(p, &cp);
        if(!trIsUpper(cp)) return false;
        p += k;
        uint32_t next;
        utf8Decode(p, &next);
        if(trIsLetter(next)) return false;
    }
    p = skipSpaces(p);
    if(*p == '.' || *p == ')') p = skipSpaces(p + 1);
    int dashLen;
    if(*p == '-') return p[1] == ' ' || p[1] == 0;
    const char *d = findDash(p, 1, &dashLen);
    return d == p;
}

// Session header lines such as "KÂTİPLER : Ziya Arkant (Yozgad)" or "BAŞKAN : Başkanvekili ...":
// an all-caps word followed by a colon.
static bool isHeaderField(const char *s) {
    const char *p = skipSpaces(s);
    int letters = 0;
    for(;;) {
        uint32_t cp;
        int k = utf8Decode(p, &cp);
        if(!trIsLetter(cp)) break;
        if(!trIsUpper(cp)) return false;
        letters++;
        p += k;
    }
    return letters >= 2 && *skipSpaces(p) == ':';
}

typedef enum { LINE_EMPTY, LINE_FURNITURE, LINE_HEADING, LINE_SPEECH } LineKind;

static LineKind classify(const char *s) {
    if(!*skipSpaces(s)) return LINE_EMPTY;
    LetterCount c = countLetters(s, s + strlen(s), false);
    if(c.letters == 0 || (c.letters <= 2 && c.lower == 0)) return LINE_FURNITURE;   // "— 227 —"
    if(isNumberedHeading(s) || isHeaderField(s)) return LINE_HEADING;
    if(c.letters >= 3 && c.upper * 5 >= c.letters * 4) return LINE_HEADING;           // 80% capitals
    return LINE_SPEECH;
}

// Folds a line and drops the leading spaces and stray marks OCR leaves ("' 'REİS").
static void foldTrimmed(const char *line, char *out, int size) {
    while(*line == ' ' || *line == '\t' || *line == '\'' || *line == '"' || *line == '`' || *line == '.' || *line == ',') line++;
    trFoldString(line, out, size);
    trimRight(out);
}

static int countWords(const char *s) {
    int words = 0;
    bool inWord = false;
    for(; *s; s++) {
        bool space = *s == ' ' || *s == '\t';
        if(!space && !inWord) words++;
        inWord = !space;
    }
    return words;
}

static bool startsWith(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static bool startsWithDash(const char *s) {
    s = skipSpaces(s);
    int dashLen;
    return *s == '-' || findDash(s, 1, &dashLen) == s;
}

// Does this line start the proceedings? The opening time ("Açılma saati : 15", OCR's
// "Açılına Sa ü : 10.00", the 1920s "Bedi müzakerat; saat : 2,10"), a session heading
// ("BİRİNCİ OTURUM", "CELSE"), or the chair speaking ("BAŞKAN —", "REİS —"). The
// contents pages and the summary of the previous sitting before it contain none of these.
static bool startsProceedings(const char *line) {
    char f[512];
    foldTrimmed(line, f, sizeof f);
    int words = countWords(f);
    if((startsWith(f, "açıl") || startsWith(f, "acıl")) && words <= 6) return true;
    if(startsWith(f, "bed") && strstr(f, "müzaker") && words <= 6) return true;
    if(words >= 1 && words <= 4) {
        const char *last = strrchr(f, ' ');
        last = last ? last + 1 : f;
        LetterCount c = countLetters(line, line + strlen(line), false);
        if((startsWith(last, "oturum") || startsWith(last, "celse")) && c.upper * 5 >= c.letters * 4) return true;
    }
    static const char *CHAIR[] = {"başkan", "reis", "reıs"};
    for(int i = 0; i < 3; i++) {
        if(startsWith(f, CHAIR[i]) && startsWithDash(f + strlen(CHAIR[i]))) return true;
    }
    return false;
}

// Does this line end a session? "Kapanma saati : 19.09", the 1920s "Hitamı müzakerat",
// or the chair's "... birleşimi kapatıyorum". After the last one come appendices:
// vote lists, the full text of bills and committee reports, written answers to written
// questions. Those are written documents, not speeches, so they are cut.
static bool endsSession(const char *line) {
    char f[512];
    foldTrimmed(line, f, sizeof f);
    if(startsWith(f, "kapanma") && countWords(f) <= 6) return true;
    if(strstr(f, "hitam") && strstr(f, "müzaker")) return true;
    return strstr(f, "kapatıyorum") || strstr(f, "kapatiyorum");
}

// Opening and closing time lines in the middle of the proceedings ("Açılma Saati : 13.45").
static bool isTimeLine(const char *line) {
    char f[512];
    foldTrimmed(line, f, sizeof f);
    return countWords(f) <= 6 && (startsWith(f, "açıl") || startsWith(f, "kapanma"));
}

typedef struct {
    char *data;
    size_t len, cap;
} Buffer;

static void bufAppend(Buffer *b, const char *s, size_t n) {
    if(b->len + n + 1 > b->cap) {
        while(b->len + n + 1 > b->cap) b->cap = b->cap ? b->cap * 2 : 4096;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

char *cleanTranscript(const char *raw, CleanStats *stats) {
    memset(stats, 0, sizeof *stats);
    char *copy = strdup(raw);
    int cap = 1024, count = 0;
    char **lines = malloc(cap * sizeof *lines);
    for(char *p = copy;;) {
        if(count == cap) lines = realloc(lines, (cap *= 2) * sizeof *lines);
        lines[count++] = p;
        char *nl = strpbrk(p, "\n\f");   // \f is pdftotext's page break
        if(!nl) break;
        *nl = 0;
        p = nl + 1;
    }
    stats->linesIn = count;

    // Before the proceedings start: the contents pages and the clerks' summary of the
    // previous sitting. After the last session closes: appendices.
    int start = 0, end = count;
    for(int i = 0; i < count; i++) {
        if(startsProceedings(lines[i])) { start = i; break; }
    }
    for(int i = count - 1; i > start; i--) {
        if(endsSession(lines[i])) { end = i + 1; break; }
    }
    stats->frontMatter = start;
    stats->backMatter = count - end;

    Buffer out = {0}, cur = {0};
    for(int i = start; i < end; i++) {
        if(isTimeLine(lines[i])) {
            stats->headings++;
            continue;
        }
        trimRight(lines[i]);
        cur.len = 0;
        bufAppend(&cur, skipSpaces(lines[i]), strlen(skipSpaces(lines[i])));

        // Rejoin a word split across lines: "getir-" + "miştir" (up to two blank lines apart).
        for(;;) {
            int hyphen = trailingHyphen(cur.data);
            if(!hyphen) break;
            int j = i + 1;
            while(j < end && j <= i + 3 && !*skipSpaces(lines[j])) j++;
            if(j >= end || j > i + 3 || !startsLowercase(lines[j])) break;
            cur.len -= hyphen;
            trimRight(lines[j]);
            const char *next = skipSpaces(lines[j]);
            bufAppend(&cur, next, strlen(next));
            i = j;
        }
        removeSoftHyphens(cur.data);

        bool labelled;
        const char *speech = stripLabel(cur.data, &labelled);
        LineKind kind = classify(speech);
        if(labelled) {
            // Nothing left after the "label": it was an all-caps heading like "BİRİNCİ OTURUM".
            if(kind == LINE_EMPTY) stats->headings++;
            else stats->labels++;
        }
        switch(kind) {
            case LINE_EMPTY: break;
            case LINE_FURNITURE: stats->furniture++; break;
            case LINE_HEADING: stats->headings++; break;
            case LINE_SPEECH:
                bufAppend(&out, speech, strlen(speech));
                bufAppend(&out, "\n", 1);
                stats->linesOut++;
                break;
        }
    }
    free(cur.data);
    free(lines);
    free(copy);
    if(!out.data) bufAppend(&out, "", 0);
    return out.data;
}
