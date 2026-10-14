#include "charts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YEAR_MIN 1918
#define YEAR_MAX 2026

// ---------------------------------------------------------------- style

// The neutrals change with the theme (setChartTheme); the data colours below do not.
static Color PAPER = {250, 248, 243, 255};
static Color CARD = {255, 255, 255, 255};
static Color INK = {34, 34, 40, 255};
static Color MUTED = {112, 112, 122, 255};
static Color FAINT = {170, 170, 176, 255};
static Color GRID = {229, 227, 221, 255};
static Color CHIP = {238, 236, 230, 255};   // the background of a number chip
static Color EVEN = {236, 230, 220, 255};   // the heat map's middle: old and new word equal
static const Color TIP = {28, 32, 40, 240};  // tooltips are dark in both themes
static bool darkTheme = false;

void setChartTheme(bool dark) {
    darkTheme = dark;
    if(dark) {   // for projectors and dark rooms
        PAPER = (Color){17, 21, 28, 255};
        CARD = (Color){27, 33, 43, 255};
        INK = (Color){232, 236, 242, 255};
        MUTED = (Color){160, 169, 182, 255};
        FAINT = (Color){108, 117, 132, 255};
        GRID = (Color){42, 50, 63, 255};
        CHIP = (Color){38, 46, 58, 255};
        EVEN = (Color){62, 66, 74, 255};
    } else {
        PAPER = (Color){250, 248, 243, 255};
        CARD = (Color){255, 255, 255, 255};
        INK = (Color){34, 34, 40, 255};
        MUTED = (Color){112, 112, 122, 255};
        FAINT = (Color){170, 170, 176, 255};
        GRID = (Color){229, 227, 221, 255};
        CHIP = (Color){238, 236, 230, 255};
        EVEN = (Color){236, 230, 220, 255};
    }
}

bool chartThemeIsDark(void) {
    return darkTheme;
}

Color chartBackground(void) {
    return PAPER;
}

static Color mix(Color a, Color b, float t) {
    t = fminf(1, fmaxf(0, t));
    return (Color){(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                   (unsigned char)(a.b + (b.b - a.b) * t), 255};
}

// Old word warm, today's word cool, the 1935 proposal in between (colour-blind safe).
static const Color OLD = {199, 81, 52, 255};
static const Color NEW = {0, 114, 178, 255};
static const Color PROPOSAL = {204, 121, 167, 255};

static const Color ORIGIN_COLORS[ORIGIN_COUNT] = {
    {199, 81, 52, 255},    // Arapça
    {230, 159, 0, 255},    // Farsça
    {0, 114, 178, 255},    // Türkçe
    {0, 158, 115, 255},    // Batı dilleri
    {175, 175, 180, 255},  // diğer
};

typedef struct {
    const char *name, *label;
    Color color;
} Theme;

static const Theme THEMES[] = {
    {"meclis", "Meclis dili", {94, 79, 162, 255}},
    {"devlet", "Devlet ve siyaset", {190, 58, 96, 255}},
    {"genel", "Gündelik sözcükler", {0, 137, 138, 255}},
    {"eğitim", "Eğitim ve bilim", {214, 131, 26, 255}},
};

static Color themeColor(const char *group) {
    for(size_t i = 0; i < sizeof THEMES / sizeof THEMES[0]; i++) {
        if(strcmp(THEMES[i].name, group) == 0) return THEMES[i].color;
    }
    return MUTED;
}

static Color roleColor(const char *role) {
    if(strcmp(role, "old") == 0) return OLD;
    if(strcmp(role, "proposal") == 0) return PROPOSAL;
    return NEW;
}

static const char *roleName(const char *role) {
    if(strcmp(role, "old") == 0) return "eski";
    if(strcmp(role, "proposal") == 0) return "1935 önerisi";
    return "bugünkü";
}

static Color statusColor(FitStatus s) {
    switch(s) {
        case STATUS_REPLACED: return NEW;
        case STATUS_PARTIAL: return (Color){120, 150, 190, 255};
        case STATUS_FAILED: return OLD;
        default: return FAINT;
    }
}

typedef struct {
    int year;
    const char *label;
    const char *detail;   // shown by the time machine
} Event;

static const Event EVENTS[] = {
    {1928, "Harf Devrimi", "Latin alfabesine geçildi"},
    {1932, "TDK kuruldu", "Türk Dili Tetkik Cemiyeti kuruldu, öz Türkçe karşılıklar aranıyor"},
    {1934, "Unvanlar kaldırıldı", "Efendi, bey, paşa gibi unvanlar yasayla kaldırıldı"},
    {1935, "Cep Kılavuzu", "Osmanlıcadan Türkçeye Cep Kılavuzu yayımlandı"},
    {1945, "Anayasa öz Türkçe", "Anayasanın dili öz Türkçeleştirildi"},
    {1952, "Eski metne dönüş", "Anayasa 1924'teki diline geri döndürüldü"},
    {1983, "TDK yeniden yapılandı", "TDK, Atatürk Kültür, Dil ve Tarih Yüksek Kurumuna bağlandı"},
};
#define N_EVENTS (int)(sizeof EVENTS / sizeof EVENTS[0])

static Font regular, bold;

static Font loadFirst(const char *const *paths, int n, const int *codepoints, int count) {
    for(int i = 0; i < n; i++) {
        if(!FileExists(paths[i])) continue;
        Font f = LoadFontEx(paths[i], 64, codepoints, count);
        GenTextureMipmaps(&f.texture);
        SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
        return f;
    }
    fprintf(stderr, "no font with Turkish letters found; ç ğ ı ö ş ü will not show\n");
    return GetFontDefault();
}

void loadChartFonts(void) {
    static const char *REGULAR[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf",
    };
    static const char *BOLD[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf", "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/Library/Fonts/Arial Bold.ttf",
    };
    int codepoints[512], n = 0;
    for(int c = 32; c <= 126; c++) codepoints[n++] = c;
    for(int c = 0xA1; c <= 0x17F; c++) {
        if(c != 0xAD) codepoints[n++] = c;   // the soft hyphen has no glyph
    }
    int extra[] = {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2190, 0x2192, 0x2191, 0x2193, 0x25B6, 0x2016};
    for(size_t i = 0; i < sizeof extra / sizeof extra[0]; i++) codepoints[n++] = extra[i];
    regular = loadFirst(REGULAR, sizeof REGULAR / sizeof REGULAR[0], codepoints, n);
    bold = loadFirst(BOLD, sizeof BOLD / sizeof BOLD[0], codepoints, n);
}

void unloadChartFonts(void) {
    UnloadFont(regular);
    UnloadFont(bold);
}

Font chartFont(bool b) {
    return b ? bold : regular;
}

static float S;   // scale of the current drawing: 1 at 1600x900

static void text(const char *s, float x, float y, float size, Color c, bool b) {
    DrawTextEx(b ? bold : regular, s, (Vector2){x, y}, size * S, size * S / 40, c);
}

static float textWidth(const char *s, float size, bool b) {
    return MeasureTextEx(b ? bold : regular, s, size * S, size * S / 40).x;
}

static void textRight(const char *s, float right, float y, float size, Color c, bool b) {
    text(s, right - textWidth(s, size, b), y, size, c, b);
}

static void textCentre(const char *s, float centre, float y, float size, Color c, bool b) {
    text(s, centre - textWidth(s, size, b) / 2, y, size, c, b);
}

// 6422 → "6.422", the Turkish way.
static const char *thousands(long v) {
    static char buf[8][32];
    static int next = 0;
    char *out = buf[next++ % 8], digits[32];
    snprintf(digits, sizeof digits, "%ld", v < 0 ? -v : v);
    int len = (int)strlen(digits), o = 0;
    if(v < 0) out[o++] = '-';
    for(int i = 0; i < len; i++) {
        out[o++] = digits[i];
        if((len - i - 1) % 3 == 0 && i < len - 1) out[o++] = '.';
    }
    out[o] = 0;
    return out;
}

// 5.55 → "5,55": Turkish writes a decimal comma.
const char *decimalComma(double v, int decimals) {
    static char buf[4][32];
    static int next = 0;
    char *out = buf[next++ % 4];
    snprintf(out, 32, "%.*f", decimals, v);
    char *dot = strchr(out, '.');
    if(dot) *dot = ',';
    return out;
}

// The title block every chart starts with; returns the y below it.
static float header(Rectangle area, const char *title, const char *subtitle) {
    text(title, area.x + 40 * S, area.y + 28 * S, 40, INK, true);
    text(subtitle, area.x + 42 * S, area.y + 82 * S, 19, MUTED, false);
    return area.y + 124 * S;
}

static void footer(Rectangle area, const Study *s, const char *note) {
    char line[400];
    snprintf(line, sizeof line, "Kaynak: TBMM tutanakları, %s oturum, %s milyon sözcük.%s%s",
             thousands(s->sittings), decimalComma(s->words / 1e6, 2), note ? " " : "", note ? note : "");
    text(line, area.x + 40 * S, area.y + area.height - 38 * S, 14, FAINT, false);
}

// Filled triangle in either winding order (raylib culls one of them).
static void fillTriangle(Vector2 a, Vector2 b, Vector2 c, Color col) {
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if(cross < 0) DrawTriangle(a, b, c, col);
    else DrawTriangle(a, c, b, col);
}

static void fillQuad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col) {
    fillTriangle(a, b, c, col);
    fillTriangle(a, c, d, col);
}

static void chip(const char *label, float x, float y, Color bg, Color fg) {
    float w = textWidth(label, 15, true) + 22 * S;
    DrawRectangleRounded((Rectangle){x, y, w, 30 * S}, 0.5f, 8, bg);
    text(label, x + 11 * S, y + 6 * S, 15, fg, true);
}

// A dark box of lines next to the mouse: the first line is the title.
#define TIP_LINES 8
static void hoverBox(char lines[][96], int n, Rectangle area) {
    Vector2 m = GetMousePosition();
    float w = 0;
    for(int i = 0; i < n; i++) w = fmaxf(w, textWidth(lines[i], 16, i == 0));
    float bx = m.x + 16, by = m.y - 20, bh = n * 24 * S + 16 * S;
    if(bx + w + 30 * S > area.x + area.width) bx = m.x - w - 46 * S;
    if(by + bh > area.y + area.height) by = area.y + area.height - bh;
    DrawRectangleRounded((Rectangle){bx, by, w + 28 * S, bh}, 0.12f, 6, TIP);
    for(int i = 0; i < n; i++) text(lines[i], bx + 14 * S, by + 9 * S + i * 24 * S, 16, i ? RAYWHITE : (Color){255, 214, 120, 255}, i == 0);
}

// ---------------------------------------------------------------- shared plotting

typedef struct {
    Rectangle plot;
    float from, to;   // years on the x axis
    float yMax;
} Axes;

static float ax(const Axes *a, float year) {
    return a->plot.x + (year - a->from) / (a->to - a->from) * a->plot.width;
}

static float ay(const Axes *a, float v) {
    return a->plot.y + a->plot.height * (1 - v / a->yMax);
}

static void yearAxis(const Axes *a, bool labels) {
    for(int year = 1920; year <= 2020; year += 10) {
        float x = ax(a, year);
        if(x < a->plot.x - 1 || x > a->plot.x + a->plot.width + 1) continue;
        DrawLineEx((Vector2){x, a->plot.y}, (Vector2){x, a->plot.y + a->plot.height}, 1, GRID);
        if(labels) {
            char y[8];
            snprintf(y, sizeof y, "%d", year);
            textCentre(y, x, a->plot.y + a->plot.height + 10 * S, 15, MUTED, false);
        }
    }
}

// Dashed lines at the reform dates, labels staggered in four rows above the plot
// (1928, 1932, 1934 and 1935 are close together).
#define EVENT_ROWS 4
static void eventLines(const Axes *a, bool labels) {
    for(int i = 0; i < N_EVENTS; i++) {
        float x = ax(a, EVENTS[i].year);
        float top = a->plot.y - (labels ? (12 + 18 * (i % EVENT_ROWS)) * S : 0);
        DrawLineDashed((Vector2){x, top}, (Vector2){x, a->plot.y + a->plot.height}, (int)(5 * S) + 1, (int)(5 * S) + 1, FAINT);
        if(labels) {
            char l[64];
            snprintf(l, sizeof l, "%d %s", EVENTS[i].year, EVENTS[i].label);
            text(l, x + 5 * S, top - 6 * S, 13, MUTED, false);
        }
    }
}

static void fitCurve(const Axes *a, const Fit *f, bool inverse, float thick, Color c) {
    if(!f->ok) return;
    Vector2 pts[256];
    int n = 0;
    for(float year = a->from; year <= a->to && n < 256; year += (a->to - a->from) / 200) {
        float v = fitShare(f, year);
        pts[n++] = (Vector2){ax(a, year), ay(a, inverse ? 1 - v : v)};
    }
    DrawSplineLinear(pts, n, thick, c);
}

static void wilson(long k, long n, float *lo, float *hi) {
    double z = 1.96, p = (double)k / n, z2 = z * z;
    double centre = (p + z2 / (2.0 * n)) / (1 + z2 / n);
    double half = z * sqrt(p * (1 - p) / n + z2 / (4.0 * n * n)) / (1 + z2 / n);
    *lo = (float)(centre - half);
    *hi = (float)(centre + half);
}

static const Study *sortStudy;

static int compareByCrossover(const void *x, const void *y) {
    const Fit *a = &sortStudy->concepts[*(const int *)x].fit, *b = &sortStudy->concepts[*(const int *)y].fit;
    bool ca = fitStatus(a) == STATUS_REPLACED || fitStatus(a) == STATUS_PARTIAL;
    bool cb = fitStatus(b) == STATUS_REPLACED || fitStatus(b) == STATUS_PARTIAL;
    if(ca != cb) return ca ? -1 : 1;
    if(!ca) return (a->ceiling < b->ceiling) - (a->ceiling > b->ceiling);   // never crossed: closest first
    return (a->crossover > b->crossover) - (a->crossover < b->crossover);
}

void conceptsByCrossover(const Study *s, int *order) {
    for(int i = 0; i < s->nConcepts; i++) order[i] = i;
    sortStudy = s;
    qsort(order, s->nConcepts, sizeof *order, compareByCrossover);
}

// ---------------------------------------------------------------- one concept

static void drawConcept(const ChartState *st, Rectangle area) {
    const Study *s = st->study;
    const ConceptStudy *c = &s->concepts[st->concept];
    const Fit *f = &c->fit;
    float y = header(area, c->label, "Aynı anlamdaki sözcüklerin meclis konuşmalarındaki payı");

    // The numbers in a row of chips.
    char buf[96];
    float x = area.x + 40 * S;
    FitStatus status = fitStatus(f);
    chip(statusName(status), x, y, statusColor(status), WHITE);
    x += textWidth(statusName(status), 15, true) + 36 * S;
    if(!isnan(f->crossover)) {
        if(!isnan(f->crossLo)) snprintf(buf, sizeof buf, "Geçiş yılı %.0f  (%.0f–%.0f)", f->crossover, f->crossLo, f->crossHi);
        else snprintf(buf, sizeof buf, "Geçiş yılı %.0f", f->crossover);
        chip(buf, x, y, mix(CHIP, NEW, 0.12f), NEW);
        x += textWidth(buf, 15, true) + 36 * S;
        snprintf(buf, sizeof buf, "Değişim %.0f yılda", f->duration);
        chip(buf, x, y, CHIP, INK);
        x += textWidth(buf, 15, true) + 36 * S;
    }
    snprintf(buf, sizeof buf, "Tavan %%%.0f", 100 * f->ceiling);
    chip(buf, x, y, CHIP, INK);
    x += textWidth(buf, 15, true) + 36 * S;
    snprintf(buf, sizeof buf, "%s kullanım", thousands(f->uses));
    chip(buf, x, y, CHIP, INK);
    if(f->uncertain && !isnan(f->crossover)) {
        x += textWidth(buf, 15, true) + 36 * S;
        chip("az veri: eğri belirsiz", x, y, mix(CHIP, OLD, 0.15f), OLD);
    }

    Axes a = {{area.x + 96 * S, y + 112 * S, area.width - 150 * S, area.height - (y - area.y) - 200 * S}, YEAR_MIN, YEAR_MAX, 1};
    for(int i = 0; i <= 4; i++) {
        float yy = ay(&a, i / 4.0f);
        DrawLineEx((Vector2){a.plot.x, yy}, (Vector2){a.plot.x + a.plot.width, yy}, 1, GRID);
        snprintf(buf, sizeof buf, "%%%d", i * 25);
        textRight(buf, a.plot.x - 12 * S, yy - 9 * S, 15, MUTED, false);
    }
    yearAxis(&a, true);
    eventLines(&a, true);

    // The crossover: where the new word passes 50%, with its 95% interval as a band.
    if(!isnan(f->crossover)) {
        float y50 = ay(&a, 0.5f);
        if(!isnan(f->crossLo)) {
            float x0 = ax(&a, (float)f->crossLo), x1 = ax(&a, (float)f->crossHi);
            DrawRectangleRec((Rectangle){x0, a.plot.y, x1 - x0, a.plot.height}, Fade(NEW, 0.08f));
        }
        float cx = ax(&a, (float)f->crossover);
        DrawLineEx((Vector2){cx, y50}, (Vector2){cx, a.plot.y + a.plot.height}, 2 * S, Fade(NEW, 0.6f));
        snprintf(buf, sizeof buf, "%.0f", f->crossover);
        textCentre(buf, cx, a.plot.y + a.plot.height - 34 * S, 22, NEW, true);
    }

    // Points per bin with 95% intervals, then the fitted S-curve on top.
    int width = st->binWidth > 0 ? st->binWidth : 5;
    long maxTotal = 1;
    for(int from = 1915; from <= YEAR_MAX; from += width) {
        long total = 0;
        for(int yy = from; yy < from + width; yy++) total += c->uses.allUses[yy - FIRST_YEAR];
        if(total > maxTotal) maxTotal = total;
    }
    for(int w = 0; w < c->nWords; w++) {
        Color col = roleColor(c->roles[w]);
        Vector2 prev = {0};
        bool havePrev = false;
        for(int from = 1915; from <= YEAR_MAX; from += width) {
            long k = 0, total = 0;
            for(int yy = from; yy < from + width; yy++) {
                k += c->counts[w].n[yy - FIRST_YEAR];
                total += c->uses.allUses[yy - FIRST_YEAR];
            }
            if(total == 0) continue;
            float px = ax(&a, from + (width - 1) / 2.0f), share = (float)k / total, lo, hi;
            if(px < a.plot.x || px > a.plot.x + a.plot.width) continue;
            wilson(k, total, &lo, &hi);
            DrawLineEx((Vector2){px, ay(&a, lo)}, (Vector2){px, ay(&a, hi)}, 2.5f * S, Fade(col, 0.3f));
            Vector2 p = {px, ay(&a, share)};
            if(havePrev && c->nWords > 2) DrawLineEx(prev, p, 1.5f * S, Fade(col, 0.35f));
            prev = p;
            havePrev = true;
            DrawCircleV(p, (3.5f + 6 * sqrtf((float)total / maxTotal)) * S, col);
        }
    }
    bool twoWords = c->nWords == 2;
    if(twoWords) fitCurve(&a, f, true, 3 * S, Fade(OLD, 0.75f));
    fitCurve(&a, f, false, 4 * S, Fade(NEW, 0.9f));

    // Legend.
    float lx = area.x + area.width - 40 * S;
    for(int w = c->nWords - 1; w >= 0; w--) {
        char l[128];
        snprintf(l, sizeof l, "%s (%s)", c->words[w], roleName(c->roles[w]));
        lx -= textWidth(l, 18, false);
        text(l, lx, area.y + 40 * S, 18, INK, false);
        DrawCircleV((Vector2){lx - 14 * S, area.y + 51 * S}, 7 * S, roleColor(c->roles[w]));
        lx -= 40 * S;
    }

    // Values under the mouse.
    if(st->hover && CheckCollisionPointRec(GetMousePosition(), a.plot)) {
        Vector2 m = GetMousePosition();
        int year = (int)roundf(a.from + (m.x - a.plot.x) / a.plot.width * (a.to - a.from));
        int from = 1915 + ((year - 1915) / width) * width;
        long total = 0, k[MAX_COMPETITORS] = {0};
        for(int yy = from; yy < from + width; yy++) {
            total += c->uses.allUses[yy - FIRST_YEAR];
            for(int w = 0; w < c->nWords; w++) k[w] += c->counts[w].n[yy - FIRST_YEAR];
        }
        char lines[TIP_LINES][96];
        int n = 0;
        if(width == 1) snprintf(lines[n++], 96, "%d", from);
        else snprintf(lines[n++], 96, "%d–%d", from, from + width - 1);
        for(int w = 0; w < c->nWords; w++) {
            snprintf(lines[n++], 96, "%s: %s (%%%.0f)", c->words[w], thousands(k[w]), total ? 100.0 * k[w] / total : 0);
        }
        snprintf(lines[n++], 96, "S-eğrisi: %%%.0f", 100 * fitShare(f, from + (width - 1) / 2.0f));
        DrawLineEx((Vector2){m.x, a.plot.y}, (Vector2){m.x, a.plot.y + a.plot.height}, 1, Fade(INK, 0.25f));
        hoverBox(lines, n, area);
    }
    footer(area, s, "Noktalar: dönem başına pay (büyüklük: veri miktarı), dikey çizgiler %95 güven aralığı; eğri: S-eğrisi.");
}

// ---------------------------------------------------------------- a searched word

// 2897 → 3000: a round top for the axis.
static float niceMax(float v) {
    if(v <= 0) return 1;
    float step = powf(10, floorf(log10f(v)));
    float steps[] = {1, 1.2f, 1.6f, 2, 2.5f, 3, 4, 5, 6, 8, 10};
    for(size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        if(steps[i] * step >= v) return steps[i] * step;
    }
    return 10 * step;
}

// Colours for compared words (Okabe-Ito, colour-blind safe), in the order they were typed.
static const Color WORD_COLORS[MAX_SEARCH] = {
    {0, 114, 178, 255}, {213, 94, 0, 255}, {0, 158, 115, 255},
    {204, 121, 167, 255}, {230, 159, 0, 255}, {86, 180, 233, 255},
};

// Uses per million words in 5-year periods (from 1915); NAN where there are no sittings.
static int perMillion(const Study *s, const YearCounts *counts, float *values, int *mids) {
    int n = 0;
    for(int from = 1915; from <= YEAR_MAX; from += 5) {
        long k = 0, words = 0;
        for(int yy = from; yy < from + 5; yy++) {
            k += counts->n[yy - FIRST_YEAR];
            words += s->tokens.n[yy - FIRST_YEAR];
        }
        if(!words) continue;
        values[n] = k * 1e6f / words;
        mids[n++] = from + 2;
    }
    return n;
}

static void drawWord(const ChartState *st, Rectangle area) {
    const Study *s = st->study;
    const WordSearch *ws = st->search;
    char title[400] = "", sub[300];
    for(int w = 0; w < ws->n; w++) {
        size_t len = strlen(title);
        snprintf(title + len, sizeof title - len, "%s%s", w ? " · " : "", ws->typed[w]);
    }
    if(ws->n == 1) {
        snprintf(sub, sizeof sub, "Milyon sözcükte kaç kez geçiyor (bütün biçimleriyle: %s)%s%s", ws->word[0],
                 ws->origin[0][0] ? " · kökeni: " : "", ws->origin[0]);
    } else {
        snprintf(sub, sizeof sub, "Milyon sözcükte kaç kez geçiyor, her sözcük bütün biçimleriyle. Beş yıllık dönemler.");
    }
    float y = header(area, title, sub);
    float labelRoom = ws->n > 1 ? 190 * S : 0;
    Axes a = {{area.x + 96 * S, y + 60 * S, area.width - 150 * S - labelRoom, area.height - (y - area.y) - 150 * S}, YEAR_MIN, YEAR_MAX, 1};
    float values[MAX_SEARCH][NYEARS / 5 + 2];
    int mids[MAX_SEARCH][NYEARS / 5 + 2], n[MAX_SEARCH];
    a.yMax = 0;
    for(int w = 0; w < ws->n; w++) {
        n[w] = perMillion(s, &ws->counts[w], values[w], mids[w]);
        for(int i = 0; i < n[w]; i++) a.yMax = fmaxf(a.yMax, values[w][i] * 1.05f);
    }
    a.yMax = niceMax(a.yMax);
    char buf[160];
    for(int i = 0; i <= 4; i++) {
        float yy = ay(&a, a.yMax * i / 4);
        DrawLineEx((Vector2){a.plot.x, yy}, (Vector2){a.plot.x + a.plot.width, yy}, 1, GRID);
        snprintf(buf, sizeof buf, "%s", thousands((long)roundf(a.yMax * i / 4)));
        textRight(buf, a.plot.x - 12 * S, yy - 9 * S, 15, MUTED, false);
    }
    yearAxis(&a, true);
    eventLines(&a, true);
    for(int w = 0; w < ws->n; w++) {
        Color col = ws->n == 1 ? NEW : WORD_COLORS[w];
        for(int i = 0; i + 1 < n[w]; i++) {
            Vector2 p0 = {ax(&a, mids[w][i]), ay(&a, values[w][i])}, p1 = {ax(&a, mids[w][i + 1]), ay(&a, values[w][i + 1])};
            if(ws->n == 1) fillQuad(p0, p1, (Vector2){p1.x, ay(&a, 0)}, (Vector2){p0.x, ay(&a, 0)}, Fade(NEW, 0.12f));
            DrawLineEx(p0, p1, 3 * S, col);
        }
        for(int i = 0; i < n[w]; i++) DrawCircleV((Vector2){ax(&a, mids[w][i]), ay(&a, values[w][i])}, (ws->n == 1 ? 5 : 3.5f) * S, col);
    }

    // Names at the right end of the lines, pushed apart where they would overlap.
    if(ws->n > 1) {
        float ly[MAX_SEARCH];
        int order[MAX_SEARCH];
        for(int w = 0; w < ws->n; w++) {
            ly[w] = n[w] ? ay(&a, values[w][n[w] - 1]) : ay(&a, 0);
            order[w] = w;
        }
        for(int i = 1; i < ws->n; i++) {   // sort by height
            for(int j = i; j > 0 && ly[order[j]] < ly[order[j - 1]]; j--) {
                int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
            }
        }
        for(int i = 1; i < ws->n; i++) ly[order[i]] = fmaxf(ly[order[i]], ly[order[i - 1]] + 24 * S);
        for(int w = 0; w < ws->n; w++) {
            float x = a.plot.x + a.plot.width + 16 * S;
            if(n[w]) DrawLineEx((Vector2){ax(&a, mids[w][n[w] - 1]) + 6 * S, ay(&a, values[w][n[w] - 1])}, (Vector2){x - 4 * S, ly[w]}, 1, Fade(WORD_COLORS[w], 0.5f));
            text(ws->typed[w], x, ly[w] - 11 * S, 19, WORD_COLORS[w], true);
            if(ws->origin[w][0]) text(ws->origin[w], x + textWidth(ws->typed[w], 19, true) + 8 * S, ly[w] - 7 * S, 13, MUTED, false);
        }
    }

    // Values under the mouse.
    if(st->hover && CheckCollisionPointRec(GetMousePosition(), a.plot)) {
        Vector2 m = GetMousePosition();
        int year = (int)roundf(a.from + (m.x - a.plot.x) / a.plot.width * (a.to - a.from));
        int from = 1915 + ((year - 1915) / 5) * 5;
        char lines[TIP_LINES][96];
        int nl = 0;
        snprintf(lines[nl++], 96, "%d–%d (milyonda)", from, from + 4);
        for(int w = 0; w < ws->n; w++) {
            for(int i = 0; i < n[w]; i++) {
                if(mids[w][i] == from + 2) snprintf(lines[nl++], 96, "%s: %s", ws->typed[w], thousands((long)roundf(values[w][i])));
            }
        }
        DrawLineEx((Vector2){m.x, a.plot.y}, (Vector2){m.x, a.plot.y + a.plot.height}, 1, Fade(INK, 0.25f));
        hoverBox(lines, nl, area);
    }
    footer(area, s, ws->n > 1 ? "Birkaç sözcüğü virgülle ayırarak karşılaştırın: efendi, muhterem, sayın, değerli" : "Beş yıllık dönemler.");
}

// ---------------------------------------------------------------- timeline

static void themeLegend(float x, float y) {
    for(size_t i = 0; i < sizeof THEMES / sizeof THEMES[0]; i++) {
        DrawCircleV((Vector2){x + 7 * S, y + 9 * S}, 7 * S, THEMES[i].color);
        text(THEMES[i].label, x + 20 * S, y, 15, MUTED, false);
        x += textWidth(THEMES[i].label, 15, false) + 46 * S;
    }
}

static void drawTimeline(const ChartState *st, Rectangle area) {
    const Study *s = st->study;
    int order[256];
    conceptsByCrossover(s, order);
    int crossed = 0;
    while(crossed < s->nConcepts) {
        FitStatus status = fitStatus(&s->concepts[order[crossed]].fit);
        if(status != STATUS_REPLACED && status != STATUS_PARTIAL) break;
        crossed++;
    }
    float y = header(area, "Değişim takvimi", "Her satır bir kavram: yeni sözcüğün kullanımın yarısını geçtiği yıl. Çizgi: %95 güven aralığı.");

    float panelW = 400 * S;
    Axes a = {{area.x + 270 * S, y + 70 * S, area.width - 270 * S - panelW - 90 * S, 0}, YEAR_MIN, 2026, 1};
    a.plot.height = area.y + area.height - a.plot.y - 96 * S;
    yearAxis(&a, true);
    eventLines(&a, true);
    float row = a.plot.height / (crossed > 0 ? crossed : 1);
    float size = fminf(15, fmaxf(9, row / S * 0.72f));
    for(int i = 0; i < crossed; i++) {
        const ConceptStudy *c = &s->concepts[order[i]];
        const Fit *f = &c->fit;
        float yy = a.plot.y + (i + 0.5f) * row;
        Color col = themeColor(c->group);
        if(i % 2 == 0) DrawRectangleRec((Rectangle){area.x + 40 * S, yy - row / 2, a.plot.x + a.plot.width - area.x - 40 * S, row}, Fade(GRID, 0.35f));
        textRight(c->label, a.plot.x - 14 * S, yy - size * S * 0.62f, size, INK, false);
        if(!isnan(f->crossLo)) {
            DrawLineEx((Vector2){ax(&a, (float)f->crossLo), yy}, (Vector2){ax(&a, (float)f->crossHi), yy}, 3 * S, Fade(col, f->uncertain ? 0.22f : 0.45f));
        }
        float r = fminf(row * 0.42f, (4 + 3.5f * log10f((float)f->uses + 1) / 4) * S);
        Vector2 p = {ax(&a, (float)f->crossover), yy};
        if(fitStatus(f) == STATUS_PARTIAL) {
            DrawCircleV(p, r, CARD);
            DrawRing(p, r - 2.5f * S, r, 0, 360, 24, col);
        } else {
            DrawCircleV(p, r, f->uncertain ? Fade(col, 0.45f) : col);
        }
    }

    // The row under the mouse, in detail.
    Vector2 m = GetMousePosition();
    if(st->hover && m.x > area.x + 40 * S && m.x < a.plot.x + a.plot.width && m.y > a.plot.y && m.y < a.plot.y + a.plot.height) {
        int i = (int)((m.y - a.plot.y) / row);
        if(i >= 0 && i < crossed) {
            const ConceptStudy *c = &s->concepts[order[i]];
            const Fit *f = &c->fit;
            char lines[TIP_LINES][96];
            int n = 0;
            snprintf(lines[n++], 96, "%.95s", c->label);
            if(!isnan(f->crossLo)) snprintf(lines[n++], 96, "Geçiş yılı %.0f (%.0f–%.0f)", f->crossover, f->crossLo, f->crossHi);
            else snprintf(lines[n++], 96, "Geçiş yılı %.0f", f->crossover);
            snprintf(lines[n++], 96, "Değişim %.0f yılda · tavan %%%.0f", f->duration, 100 * f->ceiling);
            snprintf(lines[n++], 96, "%s · %s kullanım%s", statusName(fitStatus(f)), thousands(f->uses), f->uncertain ? " · az veri" : "");
            DrawRectangleRec((Rectangle){area.x + 40 * S, a.plot.y + i * row, a.plot.x + a.plot.width - area.x - 40 * S, row}, Fade(NEW, 0.08f));
            hoverBox(lines, n, area);
        }
    }

    // Right panel, box 1: the meanings where today's word never took over.
    float px = a.plot.x + a.plot.width + 50 * S, pw = area.x + area.width - 40 * S - px, py = y;
    float failRow = (s->nConcepts - crossed > 6 ? 24 : 28) * S;
    float h1 = 70 * S + (s->nConcepts - crossed) * failRow;
    DrawRectangleRounded((Rectangle){px, py, pw, h1}, 0.06f, 8, CARD);
    text("Tutmayanlar", px + 20 * S, py + 16 * S, 20, OLD, true);
    text("eski sözcük hâlâ önde", px + 20 * S, py + 42 * S, 13, MUTED, false);
    for(int i = crossed; i < s->nConcepts; i++) {
        const ConceptStudy *c = &s->concepts[order[i]];
        float yy = py + 68 * S + (i - crossed) * failRow;
        text(c->label, px + 20 * S, yy, 15, INK, false);
        char pct[32];
        if(fitStatus(&c->fit) == STATUS_LITTLE_DATA) snprintf(pct, sizeof pct, "veri az");
        else snprintf(pct, sizeof pct, "en çok %%%.0f", 100 * c->fit.ceiling);
        textRight(pct, px + pw - 20 * S, yy, 15, OLD, true);
    }

    // Box 2: how many meanings flipped in each 5 years. Bunched after the reforms?
    py += h1 + 20 * S;
    float h2 = 180 * S;
    DrawRectangleRounded((Rectangle){px, py, pw, h2}, 0.06f, 8, CARD);
    text("Değişimler ne zaman yoğunlaştı?", px + 20 * S, py + 16 * S, 17, INK, true);
    text("beş yılda yeni sözcüğe geçen kavram sayısı", px + 20 * S, py + 40 * S, 13, MUTED, false);
    Axes h = {{px + 24 * S, py + 80 * S, pw - 48 * S, h2 - 128 * S}, 1915, 2025, 1};
    int bins[30] = {0}, most = 1;
    for(int i = 0; i < crossed; i++) {
        int b = ((int)s->concepts[order[i]].fit.crossover - 1915) / 5;
        if(b >= 0 && b < 30 && ++bins[b] > most) most = bins[b];
    }
    for(int b = 0; b < 22; b++) {
        if(!bins[b]) continue;
        float x0 = ax(&h, 1915 + b * 5), x1 = ax(&h, 1920 + b * 5);
        float bh = h.plot.height * bins[b] / most;
        DrawRectangleRec((Rectangle){x0 + 1.5f * S, h.plot.y + h.plot.height - bh, x1 - x0 - 3 * S, bh}, Fade(NEW, 0.75f));
        char n[8];
        snprintf(n, sizeof n, "%d", bins[b]);
        textCentre(n, (x0 + x1) / 2, h.plot.y + h.plot.height - bh - 18 * S, 12, NEW, true);
    }
    DrawLineEx((Vector2){h.plot.x, h.plot.y + h.plot.height}, (Vector2){h.plot.x + h.plot.width, h.plot.y + h.plot.height}, 1, FAINT);
    for(int i = 0; i < N_EVENTS; i++) {
        DrawCircleV((Vector2){ax(&h, EVENTS[i].year), h.plot.y + h.plot.height + 6 * S}, 3 * S, OLD);
    }
    for(int year = 1920; year <= 2020; year += 20) {
        char l[8];
        snprintf(l, sizeof l, "%d", year);
        textCentre(l, ax(&h, year), h.plot.y + h.plot.height + 14 * S, 12, MUTED, false);
    }

    // Box 3: the themes with their median crossover year (did the state's own words change
    // first?), and how to read the markers.
    py += h2 + 20 * S;
    float h3 = 262 * S;
    DrawRectangleRounded((Rectangle){px, py, pw, h3}, 0.06f, 8, CARD);
    text("Temalar", px + 20 * S, py + 14 * S, 17, INK, true);
    textRight("ortanca geçiş yılı", px + pw - 20 * S, py + 17 * S, 13, MUTED, false);
    float ly = py + 44 * S;
    for(size_t t = 0; t < sizeof THEMES / sizeof THEMES[0]; t++, ly += 24 * S) {
        double years[256];
        int n = 0;
        for(int i = 0; i < crossed; i++) {
            if(strcmp(s->concepts[order[i]].group, THEMES[t].name) == 0) years[n++] = s->concepts[order[i]].fit.crossover;
        }
        DrawCircleV((Vector2){px + 30 * S, ly + 9 * S}, 7 * S, THEMES[t].color);
        text(THEMES[t].label, px + 46 * S, ly, 15, INK, false);
        if(n) {   // years[] is already in crossover order
            char m[16];
            snprintf(m, sizeof m, "%.0f", n % 2 ? years[n / 2] : (years[n / 2 - 1] + years[n / 2]) / 2);
            textRight(m, px + pw - 20 * S, ly, 15, THEMES[t].color, true);
        }
    }
    ly += 8 * S;
    DrawCircleV((Vector2){px + 30 * S, ly + 9 * S}, 7 * S, MUTED);
    text("yerleşti: payı %85'in üstüne çıktı", px + 46 * S, ly, 14, MUTED, false);
    ly += 24 * S;
    DrawRing((Vector2){px + 30 * S, ly + 9 * S}, 5 * S, 7.5f * S, 0, 360, 24, MUTED);
    text("kısmen: eski sözcük de yaşıyor", px + 46 * S, ly, 14, MUTED, false);
    ly += 24 * S;
    DrawCircleV((Vector2){px + 30 * S, ly + 9 * S}, 7 * S, Fade(MUTED, 0.45f));
    text("soluk: az veri, tarih belirsiz", px + 46 * S, ly, 14, MUTED, false);
    ly += 24 * S;
    DrawCircleV((Vector2){px + 30 * S, ly + 9 * S}, 3 * S, OLD);
    text("dil reformu tarihleri", px + 46 * S, ly, 14, MUTED, false);

    footer(area, s, "Geçiş yılı: kullanımlara uydurulan S-eğrisinin %50'yi geçtiği yıl; aralık: oturumların yeniden örneklenmesi (bootstrap).");
}

// ---------------------------------------------------------------- small multiples

static void drawRaces(const ChartState *st, Rectangle area) {
    const Study *s = st->study;
    int order[256];
    conceptsByCrossover(s, order);
    float y = header(area, "Sözcük yarışları", "Her kutu bir kavram: yeni sözcüğün kullanım payı, 1920–2023. Geçiş yılına göre sıralı.");
    themeLegend(area.x + 42 * S, y - 4 * S);
    float gx = area.x + 40 * S, gy = y + 40 * S, gw = area.width - 80 * S, gh = area.height - (gy - area.y) - 64 * S;
    int cols = (int)ceilf(sqrtf(s->nConcepts * gw / gh / 2)), rows = (s->nConcepts + cols - 1) / cols;   // cells about 2:1
    float cw = gw / cols, chh = gh / rows;
    for(int i = 0; i < s->nConcepts; i++) {
        const ConceptStudy *c = &s->concepts[order[i]];
        const Fit *f = &c->fit;
        FitStatus status = fitStatus(f);
        Rectangle cell = {gx + (i % cols) * cw + 5 * S, gy + (i / cols) * chh + 5 * S, cw - 10 * S, chh - 10 * S};
        DrawRectangleRounded(cell, 0.06f, 8, status == STATUS_FAILED ? mix(CARD, OLD, 0.08f) : CARD);
        DrawRectangleRounded((Rectangle){cell.x, cell.y, 5 * S, cell.height}, 1, 4, themeColor(c->group));
        // Label and crossover year.
        char year[16];
        if(status == STATUS_REPLACED || status == STATUS_PARTIAL) snprintf(year, sizeof year, "%.0f", f->crossover);
        else snprintf(year, sizeof year, "—");
        textRight(year, cell.x + cell.width - 10 * S, cell.y + 8 * S, 18, statusColor(status), true);
        float room = cell.width - 34 * S - textWidth(year, 18, true);
        float size = 14;
        while(textWidth(c->label, size, true) > room && size > 10) size -= 0.5f;
        if(textWidth(c->label, size, true) <= room) {
            text(c->label, cell.x + 14 * S, cell.y + 10 * S + (14 - size) * S * 0.5f, size, INK, true);
        } else {   // too long for one line: the old word(s), then today's word below
            char first[200] = "";
            for(int w = 0; w + 1 < c->nWords; w++) {
                size_t len = strlen(first);
                snprintf(first + len, sizeof first - len, "%s →%s", c->words[w], w + 2 < c->nWords ? " " : "");
            }
            float s1 = 11, s2 = 11;
            while(textWidth(first, s1, true) > room && s1 > 8) s1 -= 0.5f;
            while(textWidth(c->words[c->nWords - 1], s2, true) > room && s2 > 8) s2 -= 0.5f;
            text(first, cell.x + 14 * S, cell.y + 5 * S, s1, INK, true);
            text(c->words[c->nWords - 1], cell.x + 14 * S, cell.y + 19 * S, s2, INK, true);
        }

        Axes a = {{cell.x + 14 * S, cell.y + 34 * S, cell.width - 26 * S, cell.height - 42 * S}, YEAR_MIN, YEAR_MAX, 1};
        DrawLineDashed((Vector2){a.plot.x, ay(&a, 0.5f)}, (Vector2){a.plot.x + a.plot.width, ay(&a, 0.5f)}, 3, 3, GRID);
        DrawLineEx((Vector2){a.plot.x, ay(&a, 0)}, (Vector2){a.plot.x + a.plot.width, ay(&a, 0)}, 1, GRID);
        // Area under the fitted curve, the curve, and 10-year points.
        if(f->ok) {
            float prevX = ax(&a, a.from), prevY = ay(&a, fitShare(f, a.from));
            for(float yr = a.from + 2; yr <= a.to; yr += 2) {
                float x1 = ax(&a, yr), y1 = ay(&a, fitShare(f, yr));
                fillQuad((Vector2){prevX, prevY}, (Vector2){x1, y1}, (Vector2){x1, ay(&a, 0)}, (Vector2){prevX, ay(&a, 0)}, Fade(NEW, 0.13f));
                prevX = x1;
                prevY = y1;
            }
            fitCurve(&a, f, false, 2.5f * S, status == STATUS_FAILED ? OLD : NEW);
        }
        for(int from = 1920; from <= 2020; from += 10) {
            long k = 0, n = 0;
            for(int yy = from; yy < from + 10; yy++) {
                k += c->uses.newUses[yy - FIRST_YEAR];
                n += c->uses.allUses[yy - FIRST_YEAR];
            }
            if(n >= 5) DrawCircleV((Vector2){ax(&a, from + 5), ay(&a, (float)k / n)}, 2.6f * S, Fade(INK, 0.55f));
        }
        if(status == STATUS_REPLACED || status == STATUS_PARTIAL) {
            float cx = ax(&a, (float)f->crossover);
            DrawLineEx((Vector2){cx, ay(&a, 0.5f)}, (Vector2){cx, ay(&a, 0)}, 1.5f * S, Fade(NEW, 0.6f));
        }
    }
    footer(area, s, "Noktalar: on yıllık pay; eğri ve gölge: S-eğrisi; kesikli çizgi: %50; sağ üstteki yıl: geçiş yılı.");
}

// ---------------------------------------------------------------- origins

// Shares of each origin group around a year (a 5-year window, widened until it has data).
bool originShares(const Study *s, float year, float *share) {
    int centre = (int)roundf(year);
    for(int half = 2; half <= 8; half++) {
        double sum[ORIGIN_COUNT] = {0}, total = 0;
        for(int y = centre - half; y <= centre + half; y++) {
            if(y < FIRST_YEAR || y > LAST_YEAR) continue;
            for(int g = 0; g < ORIGIN_COUNT; g++) {
                sum[g] += s->origins.n[y - FIRST_YEAR][g];
                total += s->origins.n[y - FIRST_YEAR][g];
            }
        }
        if(total < 5000) continue;
        for(int g = 0; g < ORIGIN_COUNT; g++) share[g] = (float)(sum[g] / total);
        return true;
    }
    return false;
}

static const int STACK[ORIGIN_COUNT] = {ORIGIN_TURKISH, ORIGIN_WESTERN, ORIGIN_OTHER, ORIGIN_PERSIAN, ORIGIN_ARABIC};

static void drawOrigins(const ChartState *st, Rectangle area) {
    const Study *s = st->study;
    float y = header(area, "Meclisin söz varlığı nereden geliyor?",
                     "Meclis konuşmalarındaki isim ve sıfatların kökene göre payı (beş yıllık hareketli ortalama).");
    Axes a = {{area.x + 96 * S, y + 150 * S, area.width - 330 * S, area.height - (y - area.y) - 250 * S}, 1920, 2024, 1};

    // The headline: Arabic + Persian then and now.
    float first[ORIGIN_COUNT], last[ORIGIN_COUNT];
    bool haveFirst = originShares(s, 1924, first), haveLast = originShares(s, 2020, last);
    if(haveFirst && haveLast) {
        char big[64];
        float x = area.x + 42 * S;
        snprintf(big, sizeof big, "%%%.0f", 100 * (first[ORIGIN_ARABIC] + first[ORIGIN_PERSIAN]));
        text(big, x, y + 4 * S, 54, OLD, true);
        x += textWidth(big, 54, true) + 16 * S;
        text("Arapça ve Farsça kökenli", x, y + 14 * S, 17, INK, true);
        text("1920'lerin başında", x, y + 38 * S, 17, MUTED, false);
        x += textWidth("Arapça ve Farsça kökenli", 17, true) + 50 * S;
        text("→", x, y + 8 * S, 40, FAINT, true);
        x += 70 * S;
        snprintf(big, sizeof big, "%%%.0f", 100 * (last[ORIGIN_ARABIC] + last[ORIGIN_PERSIAN]));
        text(big, x, y + 4 * S, 54, OLD, true);
        x += textWidth(big, 54, true) + 16 * S;
        text("Arapça ve Farsça kökenli", x, y + 14 * S, 17, INK, true);
        text("2020'lerde", x, y + 38 * S, 17, MUTED, false);
    }

    // Stacked areas, year by year.
    float prev[ORIGIN_COUNT + 1], cur[ORIGIN_COUNT + 1];
    bool havePrev = false;
    float prevX = 0;
    for(float year = a.from; year <= a.to; year += 1) {
        float share[ORIGIN_COUNT];
        if(!originShares(s, year, share)) { havePrev = false; continue; }
        cur[0] = 0;
        for(int k = 0; k < ORIGIN_COUNT; k++) cur[k + 1] = cur[k] + share[STACK[k]];
        float x = ax(&a, year);
        if(havePrev) {
            for(int k = 0; k < ORIGIN_COUNT; k++) {
                fillQuad((Vector2){prevX, ay(&a, prev[k])}, (Vector2){x, ay(&a, cur[k])},
                         (Vector2){x, ay(&a, cur[k + 1])}, (Vector2){prevX, ay(&a, prev[k + 1])}, ORIGIN_COLORS[STACK[k]]);
            }
        }
        memcpy(prev, cur, sizeof cur);
        prevX = x;
        havePrev = true;
    }
    // Light grid over the areas, then the reform dates.
    char buf[64];
    for(int i = 1; i < 4; i++) {
        float yy = ay(&a, i / 4.0f);
        DrawLineEx((Vector2){a.plot.x, yy}, (Vector2){a.plot.x + a.plot.width, yy}, 1, Fade(WHITE, 0.35f));
    }
    for(int i = 0; i <= 4; i++) {
        snprintf(buf, sizeof buf, "%%%d", i * 25);
        textRight(buf, a.plot.x - 12 * S, ay(&a, i / 4.0f) - 9 * S, 15, MUTED, false);
    }
    for(int year = 1920; year <= 2020; year += 10) {
        snprintf(buf, sizeof buf, "%d", year);
        textCentre(buf, ax(&a, year), a.plot.y + a.plot.height + 10 * S, 15, MUTED, false);
    }
    for(int i = 0; i < N_EVENTS; i++) {
        float x = ax(&a, EVENTS[i].year);
        DrawLineEx((Vector2){x, a.plot.y}, (Vector2){x, a.plot.y + a.plot.height}, 1.5f * S, Fade(WHITE, 0.7f));
        snprintf(buf, sizeof buf, "%d %s", EVENTS[i].year, EVENTS[i].label);
        text(buf, x + 5 * S, a.plot.y - (22 + 18 * (i % EVENT_ROWS)) * S, 13, MUTED, false);
    }

    // Labels at the right edge, with the latest shares.
    if(haveLast) {
        float lx = a.plot.x + a.plot.width + 18 * S, base = 0;
        for(int k = 0; k < ORIGIN_COUNT; k++) {
            float mid = base + last[STACK[k]] / 2;
            base += last[STACK[k]];
            float yy = ay(&a, mid);
            snprintf(buf, sizeof buf, "%s %%%.0f", originGroupName(STACK[k]), 100 * last[STACK[k]]);
            if(last[STACK[k]] < 0.02f) yy = ay(&a, base) - (k == ORIGIN_COUNT - 1 ? 0 : 10 * S);
            DrawCircleV((Vector2){lx + 6 * S, yy}, 6 * S, ORIGIN_COLORS[STACK[k]]);
            text(buf, lx + 18 * S, yy - 10 * S, 16, INK, k == ORIGIN_COUNT - 1 || k == 0);
        }
    }
    footer(area, s, "Köken: Vikisözlük etimolojileri; Türkçe ekle türetilmiş sözcükler kökün kökenini alır (kitap+çı → Arapça).");
}

// ---------------------------------------------------------------- lost and born words

static void drawEras(const ChartState *st, Rectangle area) {
    const Study *s = st->study;
    char sub[200];
    snprintf(sub, sizeof sub, "%d–%d ile %d–%d meclis konuşmalarını en iyi ayıran isim ve sıfatlar. Renk: sözcüğün kökeni.",
             s->eraA[0], s->eraA[1], s->eraB[0], 2023);
    float y = header(area, "Kaybolan ve doğan sözcükler", sub);
    int n = 18;
    if(s->nEarly < n) n = s->nEarly;
    if(s->nLate < n) n = s->nLate;
    float colW = (area.width - 80 * S) / 2, top = y + 84 * S, rowH = (area.height - (top - area.y) - 110 * S) / (n > 0 ? n : 1);
    float leftX = area.x + 40 * S + colW * 0.5f, rightX = area.x + 40 * S + colW * 1.5f;
    char title[64];
    snprintf(title, sizeof title, "%d'lerin meclisi", s->eraA[0]);
    textCentre(title, leftX, y + 20 * S, 24, INK, true);
    textCentre("Bugünün meclisi", rightX, y + 20 * S, 24, INK, true);

    // Where each listed word sits, to connect known old → new pairs.
    Vector2 leftPos[32], rightPos[32];
    for(int side = 0; side < 2; side++) {
        const EraWord *list = side ? s->late : s->early;
        double zMax = fabs(list[0].z);
        int arabicPersian = 0;
        for(int i = 0; i < n; i++) {
            float size = 18 + 22 * sqrtf((float)(fabs(list[i].z) / zMax));
            float cx = side ? rightX : leftX, yy = top + i * rowH;
            textCentre(list[i].word, cx, yy, size, ORIGIN_COLORS[list[i].origin], true);
            float half = textWidth(list[i].word, size, true) / 2;
            if(side) rightPos[i] = (Vector2){cx - half - 10 * S, yy + size * S * 0.55f};
            else leftPos[i] = (Vector2){cx + half + 10 * S, yy + size * S * 0.55f};
            if(list[i].origin == ORIGIN_ARABIC || list[i].origin == ORIGIN_PERSIAN) arabicPersian++;
        }
        char summary[96];
        snprintf(summary, sizeof summary, "%d sözcükten %d'i Arapça ya da Farsça", n, arabicPersian);
        textCentre(summary, side ? rightX : leftX, y + 52 * S, 16, MUTED, false);
    }
    for(int c = 0; c < s->nConcepts; c++) {
        const ConceptStudy *cs = &s->concepts[c];
        for(int i = 0; i < n; i++) {
            for(int j = 0; j < n; j++) {
                bool oldLeft = false, newRight = false;
                for(int w = 0; w < cs->nWords; w++) {
                    if(strcmp(cs->roles[w], "new") != 0 && strcmp(cs->words[w], s->early[i].word) == 0) oldLeft = true;
                    if(strcmp(cs->roles[w], "new") == 0 && strcmp(cs->words[w], s->late[j].word) == 0) newRight = true;
                }
                if(oldLeft && newRight) DrawLineBezier(leftPos[i], rightPos[j], 2 * S, Fade(INK, 0.22f));
            }
        }
    }
    // Origin legend.
    float lx = area.x + 40 * S, ly = area.y + area.height - 74 * S;
    for(int g = 0; g < ORIGIN_COUNT; g++) {
        DrawCircleV((Vector2){lx + 7 * S, ly + 9 * S}, 7 * S, ORIGIN_COLORS[g]);
        text(originGroupName(g), lx + 20 * S, ly, 15, MUTED, false);
        lx += textWidth(originGroupName(g), 15, false) + 46 * S;
    }
    text("Çizgiler: aynı anlamın eski ve yeni sözcüğü.", lx + 20 * S, ly, 15, MUTED, false);
    footer(area, s, "Sıralama: bilgilendirici önsel ile log-oran (Monroe vd. 2008); yazı büyüklüğü: ayırt edicilik.");
}

// ---------------------------------------------------------------- time machine


// The year, big, and what happened around it; returns the x where text can go beside it.
static float yearBanner(Rectangle area, float year) {
    char buf[200];
    snprintf(buf, sizeof buf, "%d", (int)year);
    text(buf, area.x + 40 * S, area.y + 18 * S, 110, INK, true);
    float ex = area.x + 40 * S + textWidth(buf, 110, true) + 36 * S;
    for(int i = N_EVENTS - 1; i >= 0; i--) {
        if(year >= EVENTS[i].year && year < EVENTS[i].year + 6) {
            snprintf(buf, sizeof buf, "%d · %s", EVENTS[i].year, EVENTS[i].label);
            text(buf, ex, area.y + 40 * S, 22, OLD, true);
            text(EVENTS[i].detail, ex, area.y + 72 * S, 18, MUTED, false);
            break;
        }
    }
    return ex;
}

// Origin of the vocabulary around a year, as one stacked bar with its legend.
static void originBar(const Study *s, float year, Rectangle bar) {
    float share[ORIGIN_COUNT];
    if(!originShares(s, year, share)) return;
    char buf[16];
    float x = bar.x;
    for(int k = ORIGIN_COUNT - 1; k >= 0; k--) {
        float w = bar.width * share[STACK[k]];
        DrawRectangleRec((Rectangle){x, bar.y, w, bar.height}, ORIGIN_COLORS[STACK[k]]);
        if(w > 46 * S) {
            snprintf(buf, sizeof buf, "%%%.0f", 100 * share[STACK[k]]);
            textCentre(buf, x + w / 2, bar.y + 7 * S, 16, WHITE, true);
        }
        x += w;
    }
    text("İsim ve sıfatların kökeni", bar.x, bar.y - 26 * S, 15, MUTED, true);
    float lx = bar.x;
    for(int k = ORIGIN_COUNT - 1; k >= 0; k--) {
        DrawCircleV((Vector2){lx + 6 * S, bar.y + bar.height + 18 * S}, 5 * S, ORIGIN_COLORS[STACK[k]]);
        text(originGroupName(STACK[k]), lx + 15 * S, bar.y + bar.height + 9 * S, 13, MUTED, false);
        lx += textWidth(originGroupName(STACK[k]), 13, false) + 32 * S;
    }
}

// The century at the bottom, with the reform dates and where we are.
static void centuryBar(Rectangle area, float year, const char *note) {
    Axes a = {{area.x + 40 * S, area.y + area.height - 44 * S, area.width - 80 * S, 4 * S}, 1920, 2024, 1};
    DrawRectangleRounded(a.plot, 1, 4, GRID);
    DrawRectangleRounded((Rectangle){a.plot.x, a.plot.y, ax(&a, year) - a.plot.x, a.plot.height}, 1, 4, NEW);
    for(int i = 0; i < N_EVENTS; i++) DrawCircleV((Vector2){ax(&a, EVENTS[i].year), a.plot.y + 2 * S}, 4 * S, OLD);
    DrawCircleV((Vector2){ax(&a, year), a.plot.y + 2 * S}, 8 * S, NEW);
    text("1920", a.plot.x, a.plot.y + 12 * S, 13, MUTED, false);
    textRight("2024", a.plot.x + a.plot.width, a.plot.y + 12 * S, 13, MUTED, false);
    textCentre(note, area.x + area.width / 2, a.plot.y + 12 * S, 13, MUTED, false);
}

static void drawTimeMachine(const ChartState *st, Rectangle area) {
    const Study *s = st->study;
    float year = st->year;
    int order[256];
    conceptsByCrossover(s, order);

    float ex = yearBanner(area, year);
    char buf[200];
    int flipped = 0;
    for(int i = 0; i < s->nConcepts; i++) flipped += observedShare(&s->concepts[i], year) > 0.5f;
    snprintf(buf, sizeof buf, "%d / %d kavramda yeni sözcük önde", flipped, s->nConcepts);
    text(buf, ex, area.y + 108 * S, 20, NEW, true);
    originBar(s, year, (Rectangle){area.x + area.width - 560 * S, area.y + 44 * S, 520 * S, 34 * S});

    // Every concept: the old word, a bar split by the observed share, the new word.
    int cols = s->nConcepts > 54 ? 3 : 2, perCol = (s->nConcepts + cols - 1) / cols;
    float top = area.y + 170 * S, bottom = area.y + area.height - 84 * S;
    float rowH = (bottom - top) / perCol, colW = (area.width - 80 * S) / cols;
    float size = fminf(18, rowH / S * 0.62f);
    for(int i = 0; i < s->nConcepts; i++) {
        const ConceptStudy *c = &s->concepts[order[i]];
        float p = observedShare(c, year);
        float cx = area.x + 40 * S + (i / perCol) * colW, yy = top + (i % perCol) * rowH;
        const char *oldWord = c->words[0], *newWord = c->words[c->nWords - 1];
        for(int w = 0; w < c->nWords; w++) if(strcmp(c->roles[w], "new") == 0) newWord = c->words[w];
        float barX = cx + colW * 0.36f, barW = colW * 0.34f, barH = rowH * 0.5f;
        bool newAhead = p > 0.5f;
        textRight(oldWord, barX - 12 * S, yy + (rowH - size * S) / 2 - 2 * S, size, newAhead ? FAINT : OLD, !newAhead);
        DrawRectangleRounded((Rectangle){barX, yy + (rowH - barH) / 2, barW, barH}, 0.4f, 6, Fade(OLD, 0.85f));
        DrawRectangleRounded((Rectangle){barX + barW * (1 - p), yy + (rowH - barH) / 2, barW * p, barH}, 0.4f, 6, NEW);
        text(newWord, barX + barW + 12 * S, yy + (rowH - size * S) / 2 - 2 * S, size, newAhead ? NEW : FAINT, newAhead);
    }
    centuryBar(area, year, "Çubuklar: o yıllarda konuşmalarda gerçekten kullanılan pay (eski: kırmızı, yeni: mavi)");
}

// ---------------------------------------------------------------- heat map

// The share of today's word as a colour: red (old word) through pale (even) to blue.

static Color shareColor(float p) {
    return p < 0.5f ? mix(OLD, EVEN, p * 2) : mix(EVEN, NEW, (p - 0.5f) * 2);
}

static void drawHeatmap(const ChartState *st, Rectangle area) {
    const Study *s = st->study;
    int order[256];
    conceptsByCrossover(s, order);
    float y = header(area, "Yüz yılın parmak izi",
                     "Her satır bir kavram, her sütun bir yıl: o yıllarda konuşmalarda hangi sözcük önde? Geçiş yılına göre sıralı.");

    // Colour key, top right.
    float kw = 420 * S, kx = area.x + area.width - 40 * S - kw, ky = area.y + 44 * S;
    DrawRectangleGradientH((int)kx, (int)ky, (int)(kw / 2), (int)(16 * S), OLD, EVEN);
    DrawRectangleGradientH((int)(kx + kw / 2), (int)ky, (int)(kw / 2), (int)(16 * S), EVEN, NEW);
    DrawLineEx((Vector2){kx + kw / 2, ky - 4 * S}, (Vector2){kx + kw / 2, ky + 20 * S}, 1.5f * S, MUTED);
    text("eski sözcük önde", kx, ky + 22 * S, 14, OLD, true);
    textCentre("%50", kx + kw / 2, ky + 22 * S, 14, MUTED, false);
    textRight("bugünkü sözcük önde", kx + kw, ky + 22 * S, 14, NEW, true);

    Axes a = {{area.x + 260 * S, y + 60 * S, area.width - 260 * S - 50 * S, 0}, 1920, 2024, 1};
    a.plot.height = area.y + area.height - a.plot.y - 96 * S;
    float row = a.plot.height / s->nConcepts;
    float size = fminf(14, fmaxf(9, row / S * 0.86f));
    for(int i = 0; i < s->nConcepts; i++) {
        const ConceptStudy *c = &s->concepts[order[i]];
        float yy = a.plot.y + i * row;
        textRight(c->label, a.plot.x - 12 * S, yy + (row - size * S) / 2 - 1 * S, size, INK, false);
        for(int year = 1920; year < 2024; year++) {
            float x0 = ax(&a, year), x1 = ax(&a, year + 1);
            DrawRectangleRec((Rectangle){x0, yy, x1 - x0 + 0.6f, row - 1.5f * S}, shareColor(observedAt(c, year)));
        }
        FitStatus status = fitStatus(&c->fit);
        if(status == STATUS_REPLACED || status == STATUS_PARTIAL) {   // the crossover year
            float cx = ax(&a, (float)c->fit.crossover);
            DrawRectangleRec((Rectangle){cx - 1.2f * S, yy, 2.4f * S, row - 1.5f * S}, Fade(INK, 0.55f));
        }
    }
    // Reform dates over the map, decades below it, and the years without any sampled sitting.
    for(int i = 0; i < N_EVENTS; i++) {
        float x = ax(&a, EVENTS[i].year), top = a.plot.y - (12 + 18 * (i % EVENT_ROWS)) * S;
        DrawLineDashed((Vector2){x, top}, (Vector2){x, a.plot.y + a.plot.height}, (int)(5 * S) + 1, (int)(5 * S) + 1, Fade(INK, 0.45f));
        char l[64];
        snprintf(l, sizeof l, "%d %s", EVENTS[i].year, EVENTS[i].label);
        text(l, x + 5 * S, top - 6 * S, 13, MUTED, false);
    }
    float by = a.plot.y + a.plot.height + 4 * S;
    bool gap = false;
    for(int year = 1920; year < 2024; year++) {
        if(s->tokens.n[year - FIRST_YEAR] > 0) continue;
        DrawRectangleRec((Rectangle){ax(&a, year), by, ax(&a, year + 1) - ax(&a, year), 5 * S}, FAINT);
        gap = true;
    }
    char buf[16];
    for(int year = 1920; year <= 2020; year += 10) {
        snprintf(buf, sizeof buf, "%d", year);
        textCentre(buf, ax(&a, year), by + 10 * S, 15, MUTED, false);
    }
    Vector2 m = GetMousePosition();
    if(st->hover && CheckCollisionPointRec(m, a.plot)) {
        int i = (int)((m.y - a.plot.y) / row), year = (int)floorf(a.from + (m.x - a.plot.x) / a.plot.width * (a.to - a.from));
        if(i >= 0 && i < s->nConcepts && year >= 1920 && year < 2024) {
            const ConceptStudy *c = &s->concepts[order[i]];
            float p = observedAt(c, year);
            const char *newWord = c->words[c->nWords - 1];
            for(int w = 0; w < c->nWords; w++) if(strcmp(c->roles[w], "new") == 0) newWord = c->words[w];
            char lines[TIP_LINES][96];
            int n = 0;
            snprintf(lines[n++], 96, "%.95s", c->label);
            snprintf(lines[n++], 96, "%d: %s %%%.0f, %s %%%.0f", year, newWord, 100 * p, c->words[0], 100 * (1 - p));
            if(s->tokens.n[year - FIRST_YEAR] == 0) snprintf(lines[n++], 96, "o yıldan oturum yok: çevredeki yıllar");
            DrawRectangleLinesEx((Rectangle){ax(&a, year), a.plot.y + i * row, ax(&a, year + 1) - ax(&a, year), row}, 2, INK);
            hoverBox(lines, n, area);
        }
    }
    footer(area, s, gap ? "Renk: yeni sözcüğün çevresindeki yıllardaki payı. Koyu çizgi: geçiş yılı. Gri işaret: o yıldan oturum yok." :
                          "Renk: yeni sözcüğün çevresindeki yıllardaki payı. Koyu çizgi: geçiş yılı.");
}

// ---------------------------------------------------------------- the words of each decade

// 1920 → "1920'ler", 1930 → "1930'lar": the suffix follows how the number is read.
// With locative, "1920'lerde", "1930'larda".
static const char *decadeName(int decade, bool locative) {
    static char buf[4][24];
    static int next = 0;
    static const bool BACK[10] = {false, true, false, true, true, false, true, false, false, true};
    bool back = BACK[(decade / 10) % 10];
    char *out = buf[next++ % 4];
    snprintf(out, 24, "%d'%s%s", decade, back ? "lar" : "ler", locative ? (back ? "da" : "de") : "");
    return out;
}

static void drawDecades(const ChartState *st, Rectangle area) {
    const Study *s = st->study;
    float y = header(area, "On yılların sözcükleri",
                     "Her on yılın meclis konuşmalarında, öteki yıllara göre en belirgin isim ve sıfatlar. Renk: kökeni; büyüklük: ne kadar belirgin.");
    int cols = 4, rows = 3;
    float gx = area.x + 34 * S, gy = y + 4 * S, gw = area.width - 68 * S, gh = area.height - (gy - area.y) - 60 * S;
    float cw = gw / cols, ch = gh / rows;
    for(int d = 0; d < DECADES; d++) {
        Rectangle cell = {gx + (d % cols) * cw + 6 * S, gy + (d / cols) * ch + 6 * S, cw - 12 * S, ch - 12 * S};
        DrawRectangleRounded(cell, 0.05f, 8, CARD);
        text(decadeName(1920 + 10 * d, false), cell.x + 16 * S, cell.y + 12 * S, 24, INK, true);
        long words = 0;
        for(int yy = 1920 + 10 * d; yy < 1930 + 10 * d; yy++) words += s->tokens.n[yy - FIRST_YEAR];
        char buf[64];
        snprintf(buf, sizeof buf, "%s sözcük", thousands(words));
        textRight(buf, cell.x + cell.width - 16 * S, cell.y + 20 * S, 13, FAINT, false);
        if(s->nDecade[d] == 0) continue;

        // The words flow left to right, wrapping like a paragraph, on a common baseline per line.
        double zMax = s->decades[d][0].z;
        float left = cell.x + 16 * S, right = cell.x + cell.width - 14 * S, lineY = cell.y + 50 * S;
        int i = 0;
        while(i < s->nDecade[d]) {
            float sizes[12], x = left, lineSize = 0;
            int end = i;
            while(end < s->nDecade[d]) {
                float size = 13 + 21 * (float)(s->decades[d][end].z / zMax);
                float tw = textWidth(s->decades[d][end].word, size, true);
                if(x + tw > right && end > i) break;
                sizes[end - i] = size;
                lineSize = fmaxf(lineSize, size);
                x += tw + 12 * S;
                end++;
            }
            if(lineY + lineSize * S > cell.y + cell.height - 10 * S) break;
            x = left;
            for(int k = i; k < end; k++) {
                const EraWord *w = &s->decades[d][k];
                float size = sizes[k - i];
                text(w->word, x, lineY + (lineSize - size) * S * 0.8f, size, ORIGIN_COLORS[w->origin], true);
                x += textWidth(w->word, size, true) + 12 * S;
            }
            lineY += lineSize * S * 1.25f;
            i = end;
        }
    }
    // The twelfth cell explains the colours.
    Rectangle cell = {gx + 3 * cw + 6 * S, gy + 2 * ch + 6 * S, cw - 12 * S, ch - 12 * S};
    text("Renkler: sözcüğün kökeni", cell.x + 16 * S, cell.y + 14 * S, 17, INK, true);
    for(int g = 0; g < ORIGIN_COUNT; g++) {
        float ly = cell.y + 48 * S + g * 26 * S;
        DrawCircleV((Vector2){cell.x + 24 * S, ly + 9 * S}, 7 * S, ORIGIN_COLORS[g]);
        text(originGroupName(g), cell.x + 40 * S, ly, 16, MUTED, false);
    }
    footer(area, s, "Seçim: bilgilendirici önselle log-oran (Monroe vd. 2008), en az 30 kullanım.");
}

// ---------------------------------------------------------------- survival of the 1920s words

// A word's rate per million, 1920 to 2023, as a small filled line scaled to its own peak.
static void sparkline(const Study *s, int word, Rectangle r, Color col) {
    float peak = 0;
    for(int y = 1920; y <= 2023; y++) {
        float v = s->rates[word][y - FIRST_YEAR];
        if(!isnan(v)) peak = fmaxf(peak, v);
    }
    if(peak <= 0) return;
    Axes a = {r, 1920, 2023, peak};
    DrawLineEx((Vector2){r.x, r.y + r.height}, (Vector2){r.x + r.width, r.y + r.height}, 1, GRID);
    Vector2 prev = {0};
    bool have = false;
    for(int y = 1920; y <= 2023; y++) {
        float v = s->rates[word][y - FIRST_YEAR];
        if(isnan(v)) { have = false; continue; }
        Vector2 p = {ax(&a, y), ay(&a, v)};
        if(have) {
            fillQuad(prev, p, (Vector2){p.x, r.y + r.height}, (Vector2){prev.x, r.y + r.height}, Fade(col, 0.18f));
            DrawLineEx(prev, p, 2 * S, col);
        }
        prev = p;
        have = true;
    }
}

static void wordList(const Study *s, const int *words, int n, const char *title, const char *note, Color titleColor, Rectangle box) {
    DrawRectangleRounded(box, 0.05f, 8, CARD);
    text(title, box.x + 20 * S, box.y + 14 * S, 19, titleColor, true);
    text(note, box.x + 20 * S, box.y + 40 * S, 13, MUTED, false);
    float rowH = (box.height - 74 * S) / (n > 0 ? n : 1);
    for(int i = 0; i < n; i++) {
        const WordYears *w = &s->vocab.words[words[i]];
        float yy = box.y + 66 * S + i * rowH;
        text(w->word, box.x + 20 * S, yy + rowH / 2 - 10 * S, 17, ORIGIN_COLORS[w->origin], true);
        sparkline(s, words[i], (Rectangle){box.x + box.width * 0.45f, yy + 3 * S, box.width * 0.55f - 20 * S, rowH - 8 * S}, ORIGIN_COLORS[w->origin]);
    }
}

static void drawSurvival(const ChartState *st, Rectangle area) {
    const Study *s = st->study;
    const Survival *sv = &s->survival;
    char sub[240];
    snprintf(sub, sizeof sub, "%s meclisin en çok kullandığı %d isim ve sıfattan kaçı sonraki on yıllarda hâlâ kullanılıyor?",
             decadeName(sv->base, true), sv->top);
    float y = header(area, "Sözcüklerin ömrü", sub);

    // The headline: the last decade with data, Arabic against Turkish.
    int last = DECADES - 1;
    while(last > 0 && isnan(sv->alive[ORIGIN_COUNT][last])) last--;
    float x = area.x + 42 * S;
    static const int HEAD[] = {ORIGIN_ARABIC, ORIGIN_TURKISH};
    for(int i = 0; i < 2; i++) {
        int g = HEAD[i];
        if(isnan(sv->alive[g][last])) continue;
        char big[16], line[96];
        snprintf(big, sizeof big, "%%%.0f", 100 * sv->alive[g][last]);
        text(big, x, y - 2 * S, 54, ORIGIN_COLORS[g], true);
        x += textWidth(big, 54, true) + 14 * S;
        snprintf(line, sizeof line, "%s sözcüklerin", originGroupName(g));
        text(line, x, y + 8 * S, 17, INK, true);
        snprintf(line, sizeof line, "%s hâlâ kullanılıyor", decadeName(1920 + 10 * last, true));
        text(line, x, y + 32 * S, 17, MUTED, false);
        x += textWidth(line, 17, false) + 70 * S;
    }

    float panelW = 520 * S;
    Axes a = {{area.x + 96 * S, y + 110 * S, area.width - 96 * S - panelW - 200 * S, 0}, 1920, 2030, 1};
    a.plot.height = area.y + area.height - a.plot.y - 96 * S;
    char buf[96];
    for(int i = 0; i <= 4; i++) {
        float yy = ay(&a, i / 4.0f);
        DrawLineEx((Vector2){a.plot.x, yy}, (Vector2){a.plot.x + a.plot.width, yy}, 1, GRID);
        snprintf(buf, sizeof buf, "%%%d", i * 25);
        textRight(buf, a.plot.x - 12 * S, yy - 9 * S, 15, MUTED, false);
    }
    for(int d = 0; d < DECADES; d++) {
        textCentre(decadeName(1920 + 10 * d, false), ax(&a, 1925 + 10 * d), a.plot.y + a.plot.height + 10 * S, 14, MUTED, false);
    }
    // One line per origin with enough words to be more than noise; all of them together dashed.
    static const int LINES[] = {ORIGIN_COUNT, ORIGIN_WESTERN, ORIGIN_PERSIAN, ORIGIN_TURKISH, ORIGIN_ARABIC};
    float labelY[5];
    int nLabels = 0, labelGroup[5];
    for(size_t k = 0; k < sizeof LINES / sizeof LINES[0]; k++) {
        int g = LINES[k];
        if(sv->nWords[g] < 15) continue;
        Color col = g == ORIGIN_COUNT ? MUTED : ORIGIN_COLORS[g];
        bool main = g == ORIGIN_ARABIC || g == ORIGIN_TURKISH;
        Vector2 prev = {0};
        bool have = false;
        int lastD = -1;
        for(int d = 0; d < DECADES; d++) {
            if(isnan(sv->alive[g][d])) { have = false; continue; }
            Vector2 p = {ax(&a, 1925 + 10 * d), ay(&a, sv->alive[g][d])};
            if(have) {
                if(g == ORIGIN_COUNT) DrawLineDashed(prev, p, (int)(6 * S) + 1, (int)(5 * S) + 1, col);
                else DrawLineEx(prev, p, (main ? 5 : 2.5f) * S, Fade(col, main ? 1 : 0.8f));
            }
            DrawCircleV(p, (main ? 6 : 4) * S, col);
            prev = p;
            have = true;
            lastD = d;
        }
        if(lastD >= 0) {
            labelGroup[nLabels] = g;
            labelY[nLabels++] = ay(&a, sv->alive[g][lastD]);
        }
    }
    // Labels at the right, pushed apart.
    for(int i = 1; i < nLabels; i++) {
        for(int j = i; j > 0 && labelY[j] < labelY[j - 1]; j--) {
            float t = labelY[j]; labelY[j] = labelY[j - 1]; labelY[j - 1] = t;
            int g = labelGroup[j]; labelGroup[j] = labelGroup[j - 1]; labelGroup[j - 1] = g;
        }
    }
    for(int i = 1; i < nLabels; i++) labelY[i] = fmaxf(labelY[i], labelY[i - 1] + 42 * S);
    for(int i = 0; i < nLabels; i++) {
        int g = labelGroup[i];
        snprintf(buf, sizeof buf, "%s %%%.0f", g == ORIGIN_COUNT ? "hepsi" : originGroupName(g), 100 * sv->alive[g][last]);
        float lx = a.plot.x + a.plot.width + 14 * S;
        text(buf, lx, labelY[i] - 12 * S, 17, g == ORIGIN_COUNT ? MUTED : ORIGIN_COLORS[g], true);
        snprintf(buf, sizeof buf, "%d sözcükten", sv->nWords[g]);
        text(buf, lx, labelY[i] + 8 * S, 13, FAINT, false);
    }

    // Examples, with each word's own curve.
    float px = area.x + area.width - 40 * S - panelW, ph = (area.y + area.height - 70 * S - (y - 8 * S)) / 2 - 10 * S;
    wordList(s, sv->lost, sv->nLost < 8 ? sv->nLost : 8, "Kaybolanlar", "1920'lerin sık sözcükleri, bugün neredeyse hiç", OLD,
             (Rectangle){px, y - 8 * S, panelW, ph});
    wordList(s, sv->kept, sv->nKept < 8 ? sv->nKept : 8, "Direnenler", "bugün de güçlü Arapça ve Farsça sözcükler", NEW,
             (Rectangle){px, y - 8 * S + ph + 20 * S, panelW, ph});
    footer(area, s, "Yaşıyor: o on yıldaki sıklığı 1920'lerdekinin en az dörtte biri. Eğriler: her sözcüğün 1920–2023 sıklığı, kendi tepesine göre.");
}

// ---------------------------------------------------------------- the most used words, animated

#define RANK_ROWS 12

// "değil" is tagged a noun in Wiktionary but is the negative particle; "bura" is the stem
// of burada ("here"). Both would top the list without being content words.
static bool rankSkip(const char *word) {
    return strcmp(word, "değil") == 0 || strcmp(word, "bura") == 0;
}

static float rateAt(const Study *s, int word, float year) {
    int y0 = (int)floorf(year);
    float t = year - y0;
    float a = s->rates[word][y0 - FIRST_YEAR], b = s->rates[word][y0 + 1 - FIRST_YEAR];
    if(isnan(a)) a = isnan(b) ? 0 : b;
    if(isnan(b)) b = a;
    return a * (1 - t) + b * t;
}

// The top n words at a year, most used first.
static int topWords(const Study *s, float year, int *out, int n) {
    float value[RANK_ROWS + 1];
    int count = 0;
    for(int w = 0; w < s->vocab.count; w++) {
        if(rankSkip(s->vocab.words[w].word)) continue;
        float v = rateAt(s, w, year);
        if(count == n && v <= value[n - 1]) continue;
        int i = count < n ? count++ : n - 1;
        while(i > 0 && value[i - 1] < v) {
            value[i] = value[i - 1];
            out[i] = out[i - 1];
            i--;
        }
        value[i] = v;
        out[i] = w;
    }
    return count;
}

static void drawRanking(const ChartState *st, Rectangle area) {
    const Study *s = st->study;
    float year = fminf(2023, fmaxf(1920, st->year));
    float ex = yearBanner(area, year);
    text("Meclisin en çok kullandığı isim ve sıfatlar", ex, area.y + 108 * S, 20, NEW, true);
    originBar(s, year, (Rectangle){area.x + area.width - 560 * S, area.y + 44 * S, 520 * S, 34 * S});

    // The ranks are exact at whole years; in between, each bar slides from its place in one
    // year to its place in the next (entering and leaving words slide in from below).
    int y0 = (int)floorf(year);
    float t = year - y0, ease = t * t * (3 - 2 * t);
    int before[RANK_ROWS + 1], after[RANK_ROWS + 1];
    int nBefore = topWords(s, (float)y0, before, RANK_ROWS + 1), nAfter = topWords(s, (float)y0 + 1, after, RANK_ROWS + 1);
    int words[2 * (RANK_ROWS + 1)], nWords = 0;
    float place[2 * (RANK_ROWS + 1)];
    for(int pass = 0; pass < 2; pass++) {
        const int *list = pass ? after : before;
        for(int r = 0; r < (pass ? nAfter : nBefore); r++) {
            bool seen = false;
            for(int i = 0; i < nWords; i++) seen = seen || words[i] == list[r];
            if(seen) continue;
            float r0 = RANK_ROWS + 1, r1 = RANK_ROWS + 1;
            for(int k = 0; k < nBefore; k++) if(before[k] == list[r]) r0 = (float)k;
            for(int k = 0; k < nAfter; k++) if(after[k] == list[r]) r1 = (float)k;
            words[nWords] = list[r];
            place[nWords++] = r0 + (r1 - r0) * ease;
        }
    }
    float top = area.y + 176 * S, bottom = area.y + area.height - 92 * S;
    float rowH = (bottom - top) / RANK_ROWS, labelW = 210 * S, barX = area.x + 40 * S + labelW, barMax = area.width - 80 * S - labelW - 140 * S;
    float maxValue = 1;
    for(int i = 0; i < nWords; i++) maxValue = fmaxf(maxValue, rateAt(s, words[i], year));
    for(int i = 0; i < nWords; i++) {
        float rank = place[i];
        if(rank > RANK_ROWS - 0.3f) continue;
        float alpha = fminf(1, (RANK_ROWS - 0.3f - rank) * 1.5f);
        const WordYears *w = &s->vocab.words[words[i]];
        float v = rateAt(s, words[i], year), yy = top + rank * rowH, bh = rowH * 0.72f;
        Color col = ORIGIN_COLORS[w->origin];
        textRight(w->word, barX - 14 * S, yy + (bh - 24 * S) / 2, 24, Fade(INK, alpha), true);
        float bw = fmaxf(4 * S, barMax * v / maxValue);
        DrawRectangleRounded((Rectangle){barX, yy, bw, bh}, 0.25f, 6, Fade(col, alpha));
        char buf[32];
        snprintf(buf, sizeof buf, "%s", thousands((long)roundf(v)));
        text(buf, barX + bw + 12 * S, yy + (bh - 20 * S) / 2, 20, Fade(MUTED, alpha), true);
    }
    centuryBar(area, year, "Çubuklar: milyon sözcükte kaç kez (çevresindeki yıllar, bütün biçimleriyle). Renk: kökeni.");
}

// ---------------------------------------------------------------- dispatch

// ---------------------------------------------------------------- dating a text

static void drawDating(const ChartState *st, Rectangle area) {
    const Study *s = st->study;
    const DatingTest *t = &s->datingTest;
    float y = header(area, "Metnin yaşı",
                     "Program bir metnin yalnızca sözcüklerine bakıp hangi beş yılda söylendiğini tahmin ediyor. Hiç görmediği oturumlarda denendi.");
    int whole = DATE_LENGTHS - 1;

    // The headline: the error on whole sittings against guessing without reading.
    float x = area.x + 42 * S;
    char big[32], line[300];
    snprintf(big, sizeof big, "%s yıl", decimalComma(t->meanError[whole], 1));
    text(big, x, y - 2 * S, 54, PROPOSAL, true);
    x += textWidth(big, 54, true) + 14 * S;
    text("ortalama hata, bütün bir oturumda", x, y + 8 * S, 17, INK, true);
    snprintf(line, sizeof line, "hiç okumadan hep ortadaki yılı söyleseydi: %s yıl", decimalComma(t->chanceError, 1));
    text(line, x, y + 32 * S, 17, MUTED, false);

    // Error by text length, as bars.
    float left = area.x + 42 * S, top = y + 120 * S, barX = left + 190 * S, barW = area.width * 0.45f - 240 * S;
    text("Metin ne kadar uzun olmalı?", left, top, 22, INK, true);
    float maxErr = niceMax(t->chanceError);
    float rowH = 62 * S, ry = top + 50 * S;
    for(int l = 0; l <= DATE_LENGTHS; l++, ry += rowH) {
        bool chance = l == DATE_LENGTHS;
        double err = chance ? t->chanceError : t->meanError[l];
        if(chance) snprintf(line, sizeof line, "okumadan");
        else if(t->lengths[l]) snprintf(line, sizeof line, "%d sözcük", t->lengths[l]);
        else snprintf(line, sizeof line, "bütün oturum");
        textRight(line, barX - 14 * S, ry + 10 * S, 18, chance ? MUTED : INK, !chance);
        float w = (float)(err / maxErr) * barW;
        DrawRectangleRounded((Rectangle){barX, ry + 4 * S, fmaxf(4 * S, w), rowH - 22 * S}, 0.25f, 6, chance ? FAINT : PROPOSAL);
        snprintf(line, sizeof line, "%s yıl", decimalComma(err, 1));
        text(line, barX + w + 10 * S, ry + 9 * S, 18, INK, true);
        if(!chance) {
            snprintf(line, sizeof line, "%%%.0f'i 5 yıldan az hatalı", 100 * t->within5[l]);
            text(line, barX + w + 100 * S, ry + 11 * S, 15, MUTED, false);
        }
    }
    text("20 sözcük: kabaca bir cümle. Bir oturum: binlerce sözcük.", left, ry + 6 * S, 15, MUTED, false);

    // Every sitting: its true year against the guess.
    float side = fminf(area.height - (y - area.y) - 150 * S, area.width * 0.42f);
    Rectangle plot = {area.x + area.width - 60 * S - side, y + 40 * S, side, side};
    Axes a = {plot, 1918, 2026, 1};
    float span = YEAR_MAX - YEAR_MIN;
    for(int yr = 1920; yr <= 2020; yr += 20) {
        float px = ax(&a, yr), py = plot.y + plot.height * (1 - (yr - YEAR_MIN) / span);
        DrawLineEx((Vector2){px, plot.y}, (Vector2){px, plot.y + plot.height}, 1, GRID);
        DrawLineEx((Vector2){plot.x, py}, (Vector2){plot.x + plot.width, py}, 1, GRID);
        snprintf(line, sizeof line, "%d", yr);
        textCentre(line, px, plot.y + plot.height + 10 * S, 14, MUTED, false);
        textRight(line, plot.x - 10 * S, py - 8 * S, 14, MUTED, false);
    }
    DrawLineDashed((Vector2){plot.x, plot.y + plot.height}, (Vector2){plot.x + plot.width, plot.y}, (int)(6 * S) + 1, (int)(5 * S) + 1, FAINT);
    for(int i = 0; i < t->nSittings; i++) {
        float jitter = (i % 5 - 2) * 0.5f;   // guesses fall on period middles: spread them a little
        float px = ax(&a, t->year[i]), py = plot.y + plot.height * (1 - (t->guess[i] + jitter - YEAR_MIN) / span);
        DrawCircleV((Vector2){px, py}, 4 * S, Fade(PROPOSAL, 0.6f));
    }
    text("Her nokta bir oturum", plot.x, plot.y - 34 * S, 17, INK, true);
    textCentre("gerçek yıl", plot.x + plot.width / 2, plot.y + plot.height + 34 * S, 15, MUTED, false);
    text("tahmin", plot.x - 56 * S, plot.y - 34 * S + 22 * S, 15, MUTED, false);
    text("çizgi: doğru tahmin", plot.x + plot.width - textWidth("çizgi: doğru tahmin", 15, false), plot.y - 30 * S, 15, FAINT, false);
    snprintf(line, sizeof line, "Naif Bayes: her beş yılın sözcük sıklıkları. Yalnızca sözlükteki %s sözcük biçimi, kişi adları değil. "
             "Her oturum, cildi modelden çıkarılarak tahmin edildi.", thousands(s->dating.count));
    footer(area, s, line);
}

// ---------------------------------------------------------------- reversals

// The concepts whose new word stepped back after 1952, the largest drop first.
static int reversalsByDrop(const Study *s, int *order) {
    int n = 0;
    for(int i = 0; i < s->nConcepts; i++) {
        if(!s->concepts[i].reversal.found) continue;
        const Reversal *r = &s->concepts[i].reversal;
        int j = n++;
        for(; j > 0; j--) {   // insertion by the size of the drop
            const Reversal *p = &s->concepts[order[j - 1]].reversal;
            if(p->before - p->after >= r->before - r->after) break;
            order[j] = order[j - 1];
        }
        order[j] = i;
    }
    return n;
}

static void drawReversals(const ChartState *st, Rectangle area) {
    const Study *s = st->study;
    int order[256];
    int n = reversalsByDrop(s, order);
    float y = header(area, "Geri dönüşler",
                     "1952'de anayasa eski diline döndürüldü. Yeni sözcükler meclisten de çekildi: 1952'den önceki en yüksek payları ve sonraki en düşük payları.");
    char line[200];
    snprintf(line, sizeof line, "%d / %d", n, s->nConcepts);
    float x = area.x + 42 * S;
    text(line, x, y - 2 * S, 54, OLD, true);
    x += textWidth(line, 54, true) + 14 * S;
    snprintf(line, sizeof line, "kavramda yeni sözcük en az %.0f puan geri çekildi", 100 * REVERSAL_MIN_DROP);
    text(line, x, y + 8 * S, 17, INK, true);
    text("ve çoğu ancak 1960'larda eski yerine döndü", x, y + 32 * S, 17, MUTED, false);

    // Left: one row per concept, from its share before (blue) to its share after (red).
    float listTop = y + 92 * S, rowH = 25 * S, labelW = 250 * S;
    float plotL = area.x + 42 * S + labelW, plotR = area.x + area.width * 0.52f;
    int rows = (int)((area.y + area.height - 70 * S - listTop - 30 * S) / rowH);
    if(rows > n) rows = n;
    for(int k = 0; k <= 4; k++) {
        float gx = plotL + (plotR - plotL) * k / 4;
        DrawLineEx((Vector2){gx, listTop - 6 * S}, (Vector2){gx, listTop + rows * rowH}, 1, GRID);
        snprintf(line, sizeof line, "%%%d", k * 25);
        textCentre(line, gx, listTop + rows * rowH + 6 * S, 14, MUTED, false);
    }
    for(int k = 0; k < rows; k++) {
        const ConceptStudy *c = &s->concepts[order[k]];
        const Reversal *r = &c->reversal;
        float cy = listTop + k * rowH + rowH / 2;
        textRight(c->label, plotL - 14 * S, cy - 9 * S, 15, INK, false);
        float xb = plotL + (plotR - plotL) * r->before, xa = plotL + (plotR - plotL) * r->after;
        DrawLineEx((Vector2){xa + 9 * S, cy}, (Vector2){xb, cy}, 3 * S, Fade(MUTED, 0.5f));
        fillTriangle((Vector2){xa + 5 * S, cy}, (Vector2){xa + 13 * S, cy - 5 * S}, (Vector2){xa + 13 * S, cy + 5 * S}, Fade(MUTED, 0.8f));
        DrawCircleV((Vector2){xb, cy}, 6 * S, NEW);
        DrawCircleV((Vector2){xa, cy}, 6 * S, OLD);
        if(r->backYear) snprintf(line, sizeof line, "%d", r->backYear);
        else snprintf(line, sizeof line, "dönmedi");
        text(line, plotR + 16 * S, cy - 8 * S, 14, FAINT, false);
    }
    text("geri döndüğü", plotR + 16 * S, listTop - 48 * S, 14, MUTED, true);
    text("yıl", plotR + 16 * S, listTop - 30 * S, 14, MUTED, true);
    if(rows < n) {
        snprintf(line, sizeof line, "ve %d kavram daha", n - rows);
        text(line, plotL, listTop + rows * rowH + 30 * S, 14, FAINT, false);
    }
    // Legend over the list.
    DrawCircleV((Vector2){plotL + 6 * S, listTop - 22 * S}, 6 * S, NEW);
    text("1952'den önce en yüksek", plotL + 18 * S, listTop - 31 * S, 14, MUTED, false);
    float lx = plotL + 40 * S + textWidth("1952'den önce en yüksek", 14, false);
    DrawCircleV((Vector2){lx, listTop - 22 * S}, 6 * S, OLD);
    text("sonra en düşük", lx + 12 * S, listTop - 31 * S, 14, MUTED, false);

    // Right: the six largest as curves, 1940-1975, with the 1950s dip.
    int big = n < 6 ? n : 6;
    float gx0 = area.x + area.width * 0.52f + 150 * S, gw = (area.x + area.width - 40 * S - gx0 - 24 * S) / 2;
    float gy0 = listTop - 20 * S, gh = (area.y + area.height - 80 * S - gy0 - 2 * 30 * S) / 3;
    for(int k = 0; k < big; k++) {
        const ConceptStudy *c = &s->concepts[order[k]];
        Rectangle box = {gx0 + (k % 2) * (gw + 24 * S), gy0 + (k / 2) * (gh + 30 * S), gw, gh};
        text(c->label, box.x, box.y, 15, INK, true);
        Axes a = {{box.x, box.y + 26 * S, box.width, box.height - 46 * S}, 1940, 1975, 1};
        DrawRectangleRec((Rectangle){ax(&a, 1952), a.plot.y, ax(&a, 1960) - ax(&a, 1952), a.plot.height}, Fade(OLD, 0.08f));
        DrawLineEx((Vector2){a.plot.x, ay(&a, 0.5f)}, (Vector2){a.plot.x + a.plot.width, ay(&a, 0.5f)}, 1, GRID);
        DrawLineEx((Vector2){a.plot.x, a.plot.y + a.plot.height}, (Vector2){a.plot.x + a.plot.width, a.plot.y + a.plot.height}, 1, GRID);
        DrawLineDashed((Vector2){ax(&a, 1952), a.plot.y}, (Vector2){ax(&a, 1952), a.plot.y + a.plot.height}, (int)(4 * S) + 1, (int)(4 * S) + 1, FAINT);
        Vector2 prev = {0};
        for(int yr = 1940; yr <= 1975; yr++) {
            Vector2 p = {ax(&a, yr), ay(&a, observedAt(c, yr))};
            if(yr > 1940) DrawLineEx(prev, p, 3 * S, mix(OLD, NEW, observedAt(c, yr) * 1.4f - 0.2f));
            prev = p;
        }
        for(int yr = 1940; yr <= 1970; yr += 10) {
            snprintf(line, sizeof line, "%d", yr);
            textCentre(line, ax(&a, yr), a.plot.y + a.plot.height + 4 * S, 12, FAINT, false);
        }
    }
    footer(area, s, "Pay: kavramın bütün kullanımları içinde bugünkü sözcüğün payı, en az yedi yıllık bir pencerede. Pembe şerit: 1952–1960.");
}

const char *viewName(View v) {
    static const char *NAMES[] = {"Kavram", "Değişim takvimi", "Sözcük yarışları", "Söz varlığının kökeni",
                                  "Kaybolan ve doğan sözcükler", "Zaman makinesi", "Sözcük ara",
                                  "Yüz yılın parmak izi", "On yılların sözcükleri", "Sözcüklerin ömrü",
                                  "En çok kullanılanlar", "Metnin yaşı", "Geri dönüşler"};
    return v < VIEW_COUNT ? NAMES[v] : "?";
}

bool viewIsAnimated(View v) {
    return v == VIEW_TIME_MACHINE || v == VIEW_RANKING;
}

void drawView(View v, const ChartState *st, Rectangle area) {
    S = fminf(area.width / 1600, area.height / 900);
    DrawRectangleRec(area, PAPER);
    if(!st->study || st->study->nConcepts == 0) {
        text("Veri yok: önce `make pilot` çalıştırın.", area.x + 40 * S, area.y + 40 * S, 24, INK, true);
        return;
    }
    switch(v) {
        case VIEW_CONCEPT: drawConcept(st, area); break;
        case VIEW_TIMELINE: drawTimeline(st, area); break;
        case VIEW_RACES: drawRaces(st, area); break;
        case VIEW_ORIGINS: drawOrigins(st, area); break;
        case VIEW_ERAS: drawEras(st, area); break;
        case VIEW_TIME_MACHINE: drawTimeMachine(st, area); break;
        case VIEW_WORD: if(st->search && st->search->n) drawWord(st, area); break;
        case VIEW_HEATMAP: drawHeatmap(st, area); break;
        case VIEW_DECADES: drawDecades(st, area); break;
        case VIEW_SURVIVAL: drawSurvival(st, area); break;
        case VIEW_RANKING: drawRanking(st, area); break;
        case VIEW_DATING: drawDating(st, area); break;
        case VIEW_REVERSALS: drawReversals(st, area); break;
        default: break;
    }
}

// Blending also lowers the alpha channel of a render texture wherever something
// half-transparent was drawn, which would leave see-through pixels in the PNG.
void makeOpaque(Image *img) {
    ImageFormat(img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    unsigned char *p = img->data;
    for(int i = 0; i < img->width * img->height; i++) p[4 * i + 3] = 255;
}

bool saveView(View v, const ChartState *st, int width, int height, const char *path) {
    RenderTexture2D rt = LoadRenderTexture(width, height);
    BeginTextureMode(rt);
    ClearBackground(PAPER);
    ChartState still = *st;
    still.hover = false;
    drawView(v, &still, (Rectangle){0, 0, (float)width, (float)height});
    EndTextureMode();
    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);   // render textures are stored upside down
    makeOpaque(&img);
    bool ok = ExportImage(img, path);
    UnloadImage(img);
    UnloadRenderTexture(rt);
    return ok;
}
