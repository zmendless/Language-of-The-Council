// Unit tests for the text and cleaning code. The examples are real lines from the
// transcripts, OCR mistakes included. Run with `make test`.
#include "../src/analysis.h"
#include "../src/clean.h"
#include "../src/inflect.h"
#include "../src/text.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks = 0, failures = 0;

static void fail(void) {
    failures++;
}

#define CHECK(cond) do { checks++; if(!(cond)) { fail(); fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #cond); } } while(0)

static void checkFold(const char *in, const char *want) {
    char out[256];
    trFoldString(in, out, sizeof out);
    checks++;
    if(strcmp(out, want) != 0) {
        fail();
        fprintf(stderr, "fold(\"%s\") = \"%s\", want \"%s\"\n", in, out, want);
    }
}

// Compares the words of line, joined by single spaces, with want.
static void checkTokens(const char *line, const char *want) {
    char got[1024] = "", word[MAX_WORD];
    Tokenizer t;
    tokInit(&t, line);
    while(tokNext(&t, word, NULL, NULL)) {
        if(*got) strcat(got, " ");
        strcat(got, word);
    }
    checks++;
    if(strcmp(got, want) != 0) {
        fail();
        fprintf(stderr, "tokens(\"%s\") = \"%s\", want \"%s\"\n", line, got, want);
    }
}

static void checkDate(const char *raw, const char *file, int y, int m, int d) {
    SittingId id;
    Date date = {0, 0, 0};
    checks++;
    if(!parseSittingId(file, &id) || !parseRecordDate(raw, &id, &date) || date.year != y || date.month != m || date.day != d) {
        fail();
        fprintf(stderr, "date in \"%s\" = %04d-%02d-%02d, want %04d-%02d-%02d\n", raw, date.year, date.month, date.day, y, m, d);
    }
}

static void testFold(void) {
    checkFold("İSTANBUL", "istanbul");
    checkFold("IŞIK", "ışık");
    checkFold("BÎRÎNCÎ", "birinci");   // OCR's Î for İ
    checkFold("Kâtip", "katip");
    checkFold("MİLLÎ", "milli");
    checkFold("ÇĞÖŞÜ", "çğöşü");

    // A text layer OCR'd without Turkish has no Turkish-only letters at all.
    CHECK(trTurkishLetterShare("Yüksek mühendis mektebinin bütçesi") > 0.1);
    CHECK(trTurkishLetterShare("Yiiksek miihendis mektebinin biitgesi") == 0.0);
}

static void testTokens(void) {
    checkTokens("Türkiye'nin 3 üncü maddesi", "türkiye maddesi");
    checkTokens("2 nci ve 3 ncü maddeleri", "ve maddeleri");
    checkTokens("65 nei Birleşimle", "birleşimle");
    checkTokens("istanbul'un muvakkat kaydiyle", "istanbul muvakkat kaydiyle");
    checkTokens("bu mes'ele ve san'at", "bu mesele ve sanat");
    checkTokens("1948'de Türkiye-Finlandiya", "türkiye finlandiya");
    checkTokens("getir\xC2\xADmiştir", "getirmiştir");
    checkTokens("İç Tüzük'ün 3'üncü maddesi", "iç tüzük maddesi");
    checkTokens("(Sürekli alkışlar).", "sürekli alkışlar");

    // Positions point into the original line, suffix included.
    const char *line = "Sayın Başkan'ın sözü";
    Tokenizer t;
    tokInit(&t, line);
    char word[MAX_WORD];
    int start, len;
    CHECK(tokNext(&t, word, &start, &len) && strcmp(word, "sayın") == 0);
    CHECK(tokNext(&t, word, &start, &len) && strcmp(word, "başkan") == 0);
    CHECK(strncmp(line + start, "Başkan'ın", len) == 0 && len == (int)strlen("Başkan'ın"));
}

static void testSittingId(void) {
    SittingId id;
    CHECK(parseSittingId("tbmm08012073", &id) && strcmp(id.chamber, "TBMM") == 0 && id.term == 8 && id.volume == 12 && id.sitting == 73);
    CHECK(parseSittingId("mm__01001001.txt", &id) && strcmp(id.chamber, "MM") == 0 && id.term == 1);
    CHECK(!parseSittingId("tbmm08012fih", &id));
}

static void testDates(void) {
    checkDate("Birinci Celse\n23.4.1336\n", "tbmm01001001", 1920, 4, 23);      // Rumi calendar
    checkDate("1 - 111 - 1935 Cuma\n1-3-1935\n", "tbmm05001001", 1935, 3, 1);   // III read as 111
    checkDate("TUTANAK DERGİSİ\n18. VI. 1948 Cuma\n", "tbmm08012073", 1948, 6, 18);
    checkDate("26 .10 .1961\nPerşembe\n", "mm__01001001", 1961, 10, 26);
    checkDate("24 .11 ,1983\n", "tbmm17001001", 1983, 11, 24);
    checkDate("4 Ağustos 2007 Cumartesi\n4.8.2007\n", "tbmm23001001", 2007, 8, 4);
    // "11" could be November or OCR's II; 5 February 1936 was a Wednesday, 5 November was not.
    checkDate("5 - 11 - 1936 Çarşamba\n", "tbmm05005001", 1936, 2, 5);
    // The header date wins over a date quoted later in the agenda.
    checkDate("18. VI. 1948 Cuma\nİçindekiler\n17 . VI . 1948 tarihinde akdedilen\n", "tbmm08012073", 1948, 6, 18);
    // A year outside the term is an OCR error and is skipped.
    checkDate("23.4.1836\n23.4.1336\n", "tbmm01001001", 1920, 4, 23);
    // OCR damage seen in the pilot: 5 read as S, a stray mark, the weekday lines below.
    checkDate("49 ncu Birleşim\n26 .2 , 197S\n\nÇarşamba\n", "mm__04010049", 1975, 2, 26);
    checkDate("Sekizinci Birleşim\n23.XI.19S1\n\nCuma\n", "tbmm09010008", 1951, 11, 23);
    checkDate("14 . XI* 1951\nÇarşamba\n", "tbmm09010004", 1951, 11, 14);
    // Letter-spaced OCR, and the weekday glued to the year with an apostrophe.
    checkDate("Yirmi dokuzuncu inikat\n19 - I I I - 1934'Pazartesi\n", "tbmm04020029", 1934, 3, 19);
    checkDate("Yetmiş sekizinci inikat\n1 5 - I X - 1 9 4 1 Pazartesi\n", "tbmm06020078", 1941, 9, 15);
    // From the sample: bullets and guillemets as separators, L for 1 in the day.
    checkDate("Yüz elli beşinci içtima\n22 II • 1337 Salı\n", "tbmm01008155", 1921, 2, 22);
    checkDate("Yetmiş altıncı inikad\n2L -VIII - 1940 Çarşamba\n", "tbmm06013076", 1940, 8, 21);
    checkDate("Yetmiş altıncı\n2 6 - V I »1944\n--<.-.\ninikat\nPazartesi\n", "tbmm07011076", 1944, 6, 26);
    checkDate("Kırk dokuzuncu birleşim\n13 • V< 1946 Pazartesi\n", "tbmm07023049", 1946, 5, 13);
}

static void testClean(void) {
    const char *raw =
        "TUTANAK DERGİSİ\n"
        "18. VI. 1948 Cuma\n"
        "1. — Geçen tutanak özeti\n"
        "BİRİNCİ OTURUM\n"
        "Açılma saati : 15\n"
        "BAŞKAN — Başkanvekili Sinan Tekelioğlu\n"
        "KÂTİPLER : Ziya Arkant (Yozgad)\n"
        "BAŞKAN — Başbakan Hasan Saka'nın kur\xC2\xAD\n"
        "duğu Hükümetin programı.\n"
        "— 227 —\n"
        "İSMAİL HAKKI ÇEVİK (Eskişehir) — Ar-\n"
        "\n"
        "kadaşlar, bu mesele\n"
        "Türkiye - Finlandiya ödeme Anlaşması\n"
        "2. — BAŞKANLIK DİVANI SEÇİMİ\n"
        "SALİH KAPUSUZ (Ankara) - Yok efendim, yok.\n"
        "R E l S SÎN — Burayı teşrif etsinler.\n"
        "MUSTAFA KEMAL Pş. (Ankara) Devamla\n"
        "— Mebusların mazbatalarını\n"
        "\f2\n"
        "ANKARA\n";
    const char *want =
        "Başkanvekili Sinan Tekelioğlu\n"
        "Başbakan Hasan Saka'nın kurduğu Hükümetin programı.\n"
        "Arkadaşlar, bu mesele\n"
        "Türkiye - Finlandiya ödeme Anlaşması\n"
        "Yok efendim, yok.\n"
        "Burayı teşrif etsinler.\n"
        "Pş. (Ankara) Devamla\n"
        "— Mebusların mazbatalarını\n";
    CleanStats st;
    char *got = cleanTranscript(raw, &st);
    checks++;
    if(strcmp(got, want) != 0) {
        fail();
        fprintf(stderr, "cleanTranscript gave:\n%s---\nwant:\n%s---\n", got, want);
    }
    CHECK(st.frontMatter == 3);   // the proceedings start at "BİRİNCİ OTURUM"
    CHECK(st.labels == 6);
    CHECK(st.furniture == 2);
    free(got);

    // The 1920s: no "Açılma saati", the chair's first words start the proceedings.
    got = cleanTranscript("Münderecat\n1. — Zaptı sabık hulâsası\nREİS — Celse küşadedildi.\n", &st);
    CHECK(strcmp(got, "Celse küşadedildi.\n") == 0 && st.frontMatter == 2);
    free(got);

    // After the last session closes come appendices (here the text of a bill): cut.
    got = cleanTranscript(
        "Açılma saati : 15\n"
        "BAŞKAN — Söz sizin.\n"
        "Açılma Saati : 16.30\n"
        "BAŞKAN — Birleşimi kapatıyorum.\n"
        "Kapanma saati : 17\n"
        "KANUN TASARISI\n"
        "Madde 1 — Bu kanun yayımı tarihinde yürürlüğe girer.\n", &st);
    checks++;
    if(strcmp(got, "Söz sizin.\nBirleşimi kapatıyorum.\n") != 0) {
        fail();
        fprintf(stderr, "appendix cut gave:\n%s---\n", got);
    }
    CHECK(st.backMatter == 3);   // the two appendix lines and the empty line after the last \n
    free(got);

    // With no markers at all nothing is cut.
    got = cleanTranscript("Bir iki üç.\n", &st);
    CHECK(strcmp(got, "Bir iki üç.\n") == 0 && st.frontMatter == 0 && st.backMatter == 0);
    free(got);
}

// Checks that the generated forms of lemma include every word in want (space-separated)
// and none in notWant.
static void checkForms(const char *lemma, const char *want, const char *notWant) {
    static char forms[2048][MAX_WORD];
    int n = nounForms(lemma, forms, 2048);
    for(int pass = 0; pass < 2; pass++) {
        char list[512];
        snprintf(list, sizeof list, "%s", pass ? notWant : want);
        for(char *w = strtok(list, " "); w; w = strtok(NULL, " ")) {
            bool found = false;
            for(int i = 0; i < n && !found; i++) found = strcmp(forms[i], w) == 0;
            checks++;
            if(found == (pass == 1)) {
                fail();
                fprintf(stderr, "nounForms(\"%s\") %s \"%s\"\n", lemma, pass ? "wrongly has" : "lacks", w);
            }
        }
    }
}

static void testInflect(void) {
    checkForms("yıl", "yıl yıllar yılı yıla yılda yıldan yılın yılla yıllarda yılları yıllarının yılındaki yılımız yılıyla",
               "yılan");
    checkForms("sene", "seneyi seneye senede senenin senesi seneler senesinde seneyle", "");
    checkForms("kitap", "kitabı kitaplar kitapta kitaptan kitabın", "");   // softening, d → t
    checkForms("misal", "misali misaller misalde", "");                    // front vowels after a back one
    checkForms("akıl", "aklı akla akıllar", "");                           // vowel drop
    checkForms("hak", "hakkı hakka haklar", "");                           // doubled consonant
    checkForms("reis", "reisi reisler reisin reisimiz", "");
    checkForms("talebe", "talebeler talebesi talebeye talebelerin", "");
}

// Synthetic counts that follow a known S-curve exactly: the fit must find it again.
static void checkFitRecovers(double mid, double rate, double ceiling) {
    static long k[NYEARS], n[NYEARS];
    memset(k, 0, sizeof k);
    memset(n, 0, sizeof n);
    for(int y = 1920; y <= 2020; y++) {
        n[y - FIRST_YEAR] = 400;
        k[y - FIRST_YEAR] = lround(400 * ceiling / (1 + exp(-rate * (y - mid))));
    }
    Fit f;
    CHECK(fitLogistic(k, n, NYEARS, FIRST_YEAR, &f));
    double want = ceiling > 0.5 ? mid - log(2 * ceiling - 1) / rate : NAN;
    checks++;
    bool ok = fabs(f.ceiling - ceiling) < 0.02 && (isnan(want) ? isnan(f.crossover) : fabs(f.crossover - want) < 1);
    if(!ok) {
        fail();
        fprintf(stderr, "fit(mid %.0f, rate %.2f, ceiling %.2f): crossover %.1f (want %.1f), ceiling %.3f\n",
                mid, rate, ceiling, f.crossover, want, f.ceiling);
    }
}

static void checkOrigin(const char *etymology, const char *want) {
    const char *got = originOf(etymology);
    checks++;
    if(strcmp(got, want) != 0) {
        fail();
        fprintf(stderr, "originOf(\"%s\") = %s, want %s\n", etymology, got, want);
    }
}

static void checkRoot(const char *etymology, const char *want) {
    char root[MAX_WORD] = "";
    bool found = etymologyRoot(etymology, root);
    checks++;
    if(want ? !found || strcmp(root, want) != 0 : found) {
        fail();
        fprintf(stderr, "etymologyRoot(\"%s\") = %s, want %s\n", etymology, found ? root : "(none)", want ? want : "(none)");
    }
}

static void testAnalysis(void) {
    checkFitRecovers(1950, 0.15, 0.95);
    checkFitRecovers(1938, 0.30, 0.99);
    checkFitRecovers(1985, 0.08, 0.70);
    checkFitRecovers(1960, 0.10, 0.20);   // never reached half the uses: no crossover
    Fit failed;
    static long k[NYEARS], n[NYEARS];
    for(int y = 1920; y <= 2020; y++) {
        n[y - FIRST_YEAR] = 100;
        k[y - FIRST_YEAR] = y > 1970 ? 8 : 0;
    }
    fitLogistic(k, n, NYEARS, FIRST_YEAR, &failed);
    CHECK(fitStatus(&failed) == STATUS_FAILED);

    // Permutation test against the exact answer (all 12870 splits counted, in Python):
    // these two separated groups give p = 0.0031; a group against itself, a large p.
    double early[] = {1940, 1942, 1945, 1947, 1950, 1951, 1953, 1955};
    double late[] = {1975, 1978, 1980, 1983, 1985, 1990, 1992, 1995};
    CHECK(fabs(permutationTest(early, 8, late, 8, 40000, 1) - 0.0031) < 0.0015);
    CHECK(permutationTest(early, 8, early, 8, 5000, 1) > 0.5);

    // Real Wiktionary etymologies.
    checkOrigin("From Ottoman Turkish سنه (sene, “year, era”), from Arabic سَنَة (sana).", "arabic");
    checkOrigin("From cumhur + başkanı (third-person singular possessive). Compare also Persian رئیسجمهور.", "turkish-derived");
    checkOrigin("Coined from toplanmak during the language reform to replace the Arabic-derived içtima.", "turkish-derived");
    checkOrigin("From Ottoman Turkish اولكه, from Proto-Turkic *üle- (“to divide”). Cognate with Old Turkic ülkä.", "turkic");
    checkOrigin("Borrowed from French économie.", "french");
    checkOrigin("Learned borrowing from Karakhanid بَشْغَانْ (bašğan, “leader”).", "turkic");
    checkOrigin("Inherited from Ottoman Turkish یوز (yüz, “face”).", "turkic");
    checkOrigin("From Ottoman Turkish كیشی (kişi, “person”), from Old Anatolian Turkish كشی.", "turkic");
    checkOrigin("Ultimately from Middle Mongol ᠵᠠᠰᠠᠭ (ǰasaɣ). Doublet of yasak.", "mongolic");
    checkOrigin("Inherited from Ottoman Turkish صره (sıra), from Greek σειρά (seirá).", "greek");
    checkRoot("From Ottoman Turkish كتابجی, from kitap + -çı.", "kitap");
    checkRoot("From önem (“importance”) + li", "önem");
    checkRoot("kur- (“to assemble”) + -ul (noun-deriving suffix)", "kurmak");
    checkRoot("By surface analysis, millet + vekil, literally “representative of the people”.", "millet");
    checkRoot("Coined from toplanmak during the language reform.", NULL);
}

// A made-up vocabulary: "eski" is common in the 1920s and gone later, "yeni" the reverse,
// "hep" steady. 10,000 words a year from 1920 to 2023.
static void testVocabulary(void) {
    static Vocabulary v;
    static YearCounts tokens;
    memset(&v, 0, sizeof v);
    memset(&tokens, 0, sizeof tokens);
    static const char *WORDS[] = {"eski", "hep", "yeni"};   // sorted, as loadVocabulary leaves them
    v.count = 3;
    v.words = calloc(3, sizeof *v.words);
    for(int w = 0; w < 3; w++) {
        snprintf(v.words[w].word, MAX_WORD, "%s", WORDS[w]);
        v.words[w].origin = w == 0 ? ORIGIN_ARABIC : ORIGIN_TURKISH;
    }
    for(int y = 1920; y <= 2023; y++) {
        int i = y - FIRST_YEAR;
        tokens.n[i] = 10000;
        v.words[0].n[i] = y < 1950 ? 100 : 1;
        v.words[1].n[i] = 50;
        v.words[2].n[i] = y < 1950 ? 0 : 100;
        for(int w = 0; w < 3; w++) {
            v.words[w].total += v.words[w].n[i];
            v.n[i] += v.words[w].n[i];
        }
    }

    // 100 uses in 10,000 words is 10,000 per million; around 1950 the window mixes both.
    float rates[NYEARS];
    smoothRate(v.words[0].n, &tokens, 50000, rates);
    CHECK(fabsf(rates[1930 - FIRST_YEAR] - 10000) < 1);
    CHECK(fabsf(rates[2000 - FIRST_YEAR] - 100) < 1);
    CHECK(rates[1950 - FIRST_YEAR] > 100 && rates[1950 - FIRST_YEAR] < 10000);
    CHECK(isnan(rates[1900 - FIRST_YEAR]));   // no words that early

    EraWord top[4];
    int n = periodWords(&v, 1920, 1929, 4, top);
    CHECK(n >= 1 && strcmp(top[0].word, "eski") == 0);
    n = periodWords(&v, 2000, 2009, 4, top);
    CHECK(n >= 1 && strcmp(top[0].word, "yeni") == 0);

    // Of the 1920s words, eski dies (1 per 10,000 is far below a quarter of 100), hep lives;
    // yeni was never used in the 1920s, so it is not among them.
    Survival sv;
    survivalOf(&v, &tokens, 1920, 300, &sv);
    CHECK(sv.top == 2);
    CHECK(sv.nWords[ORIGIN_ARABIC] == 1 && sv.nWords[ORIGIN_TURKISH] == 1);
    CHECK(sv.alive[ORIGIN_ARABIC][0] == 1 && sv.alive[ORIGIN_ARABIC][3] == 0);
    CHECK(sv.alive[ORIGIN_TURKISH][9] == 1);
    CHECK(sv.nLost >= 1 && strcmp(v.words[sv.lost[0]].word, "eski") == 0);
    freeVocabulary(&v);
}

// A model of two words by hand: sene belongs to the 1920s, yıl to the 2010s, ve to both.
static void testDatingModel(void) {
    CHECK(dateBin(1919) == -1 && dateBin(1920) == 0 && dateBin(1924) == 0 && dateBin(1925) == 1);
    CHECK(dateBinMiddle(0) == 1922 && dateBinMiddle(18) == 2012);
    static DateForm forms[3];
    memset(forms, 0, sizeof forms);
    DatingModel m = {forms, 3, {0}, 0};
    snprintf(forms[0].form, MAX_WORD, "sene");   // sorted, as loadDatingModel leaves them
    snprintf(forms[1].form, MAX_WORD, "ve");
    snprintf(forms[2].form, MAX_WORD, "yıl");
    int early = dateBin(1922), late = dateBin(2012);
    forms[0].n[early] = 900; forms[0].n[late] = 10;
    forms[1].n[early] = 30000; forms[1].n[late] = 30000;
    forms[2].n[early] = 5; forms[2].n[late] = 800;
    for(int f = 0; f < 3; f++) {
        for(int b = 0; b < DATE_BINS; b++) {
            forms[f].total += forms[f].n[b];
            m.tokens[b] += forms[f].n[b];
            m.total += forms[f].n[b];
        }
    }
    CHECK(dateFormIndex(&m, "yıl") == 2 && dateFormIndex(&m, "sözcük") == -1);

    double logLik[DATE_BINS];
    FormUse *uses;
    int n = textUses(&m, "Bu sene ve geçen sene", &uses);
    CHECK(dateUses(&m, uses, n, logLik) == early);
    CHECK(isinf(logLik[dateBin(1950)]));   // no words that period: never guessed
    free(uses);
    n = textUses(&m, "Bu yıl ve geçen yıl", &uses);
    CHECK(dateUses(&m, uses, n, logLik) == late);
    free(uses);

    double year, spread;
    formYear(&m, 0, &year, &spread);
    CHECK(year < 1930);
    formYear(&m, 1, &year, &spread);
    CHECK(fabs(year - 1967) < 1 && spread > 40);   // used alike in both: tells nothing
}

// The new word at 90% in the 1940s, 10% from 1952 to 1960, then 90% again: a reversal
// that is over by the mid-1960s. A steady 90% is none.
static void testReversal(void) {
    static ConceptStudy dip, steady;
    memset(&dip, 0, sizeof dip);
    memset(&steady, 0, sizeof steady);
    for(int y = 1930; y <= 2000; y++) {
        int i = y - FIRST_YEAR;
        dip.uses.allUses[i] = steady.uses.allUses[i] = 100;
        dip.uses.newUses[i] = y >= 1952 && y <= 1960 ? 10 : 90;
        steady.uses.newUses[i] = 90;
    }
    Reversal r;
    findReversal(&dip, &r);
    CHECK(r.found && r.before > 0.85f && r.after < 0.15f);
    CHECK(r.lowYear >= 1955 && r.lowYear <= 1957);   // the middle of the dip, where the window holds only dip years
    CHECK(r.backYear >= 1962 && r.backYear <= 1966);
    findReversal(&steady, &r);
    CHECK(!r.found);
}

int main(void) {
    testFold();
    testTokens();
    testSittingId();
    testDates();
    testClean();
    testInflect();
    testAnalysis();
    testVocabulary();
    testDatingModel();
    testReversal();
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
