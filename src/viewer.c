// viewer: the charts of the study, interactive or exported as PNG files.
//
//   ./viewer [DB] [CONCEPTS.tsv]                         the window
//   ./viewer --shot NAME OUT.png [DB] [CONCEPTS.tsv]     one chart as a 1920x1080 PNG, no window
//   ./viewer --figures DIR [DB] [CONCEPTS.tsv]           every chart and every concept into DIR
//   ./viewer --ui-shot OUT.png [DB] [CONCEPTS.tsv]       the whole window, for documentation
//   ./viewer --show-shot STEP OUT.png [DB] [CONCEPTS.tsv]  one exhibition slide (-1: the title)
//   ./viewer --dark ...                                  any of these in the dark (projector) theme
//   ./viewer --video NAME DIR [DB] [CONCEPTS.tsv]        frames of an animated view, 30 per second
//                                                        (zaman or siralama; make video)
//
// NAME is takvim, parmakizi, yarislar, koken, omur, sozcukler, onyillar, zaman (or
// zaman:1950), siralama (or siralama:1950), tarih, a concept (yıl), or any other words, comma
// separated (efendi,sayın: their frequency). In the window: F full screen, P save the chart
// as a PNG (into screenshots/), arrows change the concept or year, Space plays the
// animation, E starts the exhibition mode (every chart in turn; any key stops it).
#include "raylib.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-function"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#pragma GCC diagnostic pop

#include "analysis.h"
#include "charts.h"

#include <stdio.h>
#include <string.h>

#define WIDTH 1440
#define HEIGHT 860
#define SIDEBAR 300
#define BOOTSTRAP_ROUNDS 300

static const View MENU[] = {VIEW_TIMELINE, VIEW_HEATMAP, VIEW_RACES, VIEW_TIME_MACHINE, VIEW_ORIGINS, VIEW_SURVIVAL,
                            VIEW_ERAS, VIEW_DECADES, VIEW_RANKING, VIEW_DATING, VIEW_CONCEPT};
#define N_MENU (int)(sizeof MENU / sizeof MENU[0])

// How MPs address each other: the comparison the exhibition mode and the figures show.
#define ADDRESS_WORDS "efendi, muhterem, sayın, değerli"

// The exhibition mode: every chart in turn, full screen. Animated views play to the end.
static const View SHOW[] = {VIEW_TIMELINE, VIEW_HEATMAP, VIEW_TIME_MACHINE, VIEW_ORIGINS, VIEW_SURVIVAL,
                            VIEW_RACES, VIEW_DECADES, VIEW_RANKING, VIEW_ERAS, VIEW_WORD, VIEW_DATING};
#define N_SHOW (int)(sizeof SHOW / sizeof SHOW[0])
#define SHOW_SECONDS 14

#define TITLE_SECONDS 8
#define FADE_SECONDS 0.35f
#define CAPTION_HEIGHT 0.11f   // of the screen height, in the exhibition mode

// The sidebar follows the chart theme (T switches it).
static Color SIDE_BG = {238, 236, 230, 255};
static Color SIDE_INK = {34, 34, 40, 255};
static Color SIDE_MUTED = {112, 112, 122, 255};

typedef struct {
    sqlite3 *db;
    Study *study;
    const char *conceptsPath;
    View view;
    int menu, menuScroll;           // the selected view in the sidebar
    int concept, conceptScroll;     // VIEW_CONCEPT
    int binChoice;
    bool playing;
    float year;
    bool fullScreen;
    bool exhibition;
    int showStep;                   // -1: the title slide
    float showTime;                 // seconds on the current chart
    View shownView;                 // to notice a change of view and fade it in
    float fade;                     // seconds since the view changed
    bool dark;
    char search[256];
    bool editing;
    WordSearch words;
    char status[256];
    char menuText[512], conceptText[256 * 64];
} Ui;

static const int BIN_WIDTHS[] = {1, 5, 10};

static ChartState chartState(Ui *ui, bool hover) {
    ChartState st = {ui->study, ui->concept, BIN_WIDTHS[ui->binChoice], ui->year, &ui->words, hover};
    return st;
}

static void label(const char *s, float x, float y, float size, Color c, bool bold) {
    DrawTextEx(chartFont(bold), s, (Vector2){x, y}, size, size / 40, c);
}

// Searched words, separated by commas or spaces: all their forms and spellings, counted
// per year, in the same order as concepts are counted (see CLAUDE.md).
static void searchWord(Ui *ui, const char *input) {
    WordSearch *ws = &ui->words;
    memset(ws, 0, sizeof *ws);
    char copy[256];
    snprintf(copy, sizeof copy, "%s", input);
    for(char *tok = strtok(copy, ", \t"); tok && ws->n < MAX_SEARCH; tok = strtok(NULL, ", \t")) {
        int w = ws->n++;
        char folded[MAX_WORD];
        snprintf(ws->typed[w], MAX_WORD, "%s", tok);
        trFoldString(tok, folded, sizeof folded);
        lemmaOf(ui->db, folded, ws->word[w]);
        ensureParadigm(ui->db, ws->word[w]);
    }
    char exclusions[1024];
    exclusionsPathFor(ui->conceptsPath, exclusions, sizeof exclusions);
    applyExclusions(ui->db, exclusions);
    sqlite3_stmt *stmt = dbPrepare(ui->db, "SELECT origin FROM origin WHERE lemma = ?1 ORDER BY pos = 'noun' DESC LIMIT 1");
    for(int w = 0; w < ws->n; w++) {
        countByYear(ui->db, ws->word[w], &ws->counts[w]);
        sqlite3_bind_text(stmt, 1, ws->word[w], -1, SQLITE_TRANSIENT);
        if(dbStep(ui->db, stmt) == SQLITE_ROW) {
            snprintf(ws->origin[w], sizeof ws->origin[w], "%s", originGroupName(originGroup((const char *)sqlite3_column_text(stmt, 0))));
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    if(ws->n) ui->view = VIEW_WORD;
}

static void savePng(Ui *ui) {
    MakeDirectory("screenshots");
    char path[256];
    if(ui->view == VIEW_CONCEPT) snprintf(path, sizeof path, "screenshots/kavram-%s.png", ui->study->concepts[ui->concept].concept);
    else if(ui->view == VIEW_WORD) snprintf(path, sizeof path, "screenshots/sozcuk-%s%s.png", ui->words.word[0], ui->words.n > 1 ? "-vb" : "");
    else if(ui->view == VIEW_TIME_MACHINE) snprintf(path, sizeof path, "screenshots/zaman-%d.png", (int)ui->year);
    else if(ui->view == VIEW_RANKING) snprintf(path, sizeof path, "screenshots/siralama-%d.png", (int)ui->year);
    else snprintf(path, sizeof path, "screenshots/gorunum-%d.png", ui->view);
    ChartState st = chartState(ui, false);
    bool ok = saveView(ui->view, &st, 1920, 1080, path);
    snprintf(ui->status, sizeof ui->status, ok ? "Kaydedildi: %s" : "Kaydedilemedi: %s", path);
}

// The ranking reorders every year, so it plays slower than the time machine.
static float yearsPerSecond(View v) {
    return v == VIEW_RANKING ? 2.5f : 4;
}

static void togglePlay(Ui *ui) {
    ui->playing = !ui->playing;
    if(ui->playing && ui->year >= 2023) ui->year = 1920;
}

static void selectView(Ui *ui, View v) {
    ui->view = v;
    for(int i = 0; i < N_MENU; i++) if(MENU[i] == v) ui->menu = i;
}

static void setFullScreen(Ui *ui, bool on) {
    if(ui->fullScreen != on) ToggleBorderlessWindowed();
    ui->fullScreen = on;
}

// The dark theme for projectors: charts, sidebar and raygui's controls together.
static void setTheme(Ui *ui, bool dark) {
    ui->dark = dark;
    setChartTheme(dark);
    SIDE_BG = dark ? (Color){24, 30, 39, 255} : (Color){238, 236, 230, 255};
    SIDE_INK = dark ? (Color){232, 236, 242, 255} : (Color){34, 34, 40, 255};
    SIDE_MUTED = dark ? (Color){150, 160, 174, 255} : (Color){112, 112, 122, 255};
    // GuiLoadStyleDefault unloads raygui's current font, which is the charts' font: give
    // it raylib's own font first.
    GuiSetFont(GetFontDefault());
    GuiLoadStyleDefault();
    if(dark) {
        static const struct { int prop; Color c; } DARK[] = {
            {BORDER_COLOR_NORMAL, {70, 80, 96, 255}}, {BASE_COLOR_NORMAL, {33, 40, 52, 255}}, {TEXT_COLOR_NORMAL, {210, 216, 226, 255}},
            {BORDER_COLOR_FOCUSED, {61, 155, 224, 255}}, {BASE_COLOR_FOCUSED, {40, 58, 80, 255}}, {TEXT_COLOR_FOCUSED, {232, 236, 242, 255}},
            {BORDER_COLOR_PRESSED, {61, 155, 224, 255}}, {BASE_COLOR_PRESSED, {28, 80, 120, 255}}, {TEXT_COLOR_PRESSED, {255, 255, 255, 255}},
            {BORDER_COLOR_DISABLED, {50, 56, 66, 255}}, {BASE_COLOR_DISABLED, {30, 34, 42, 255}}, {TEXT_COLOR_DISABLED, {100, 106, 116, 255}},
        };
        for(size_t i = 0; i < sizeof DARK / sizeof DARK[0]; i++) GuiSetStyle(DEFAULT, DARK[i].prop, ColorToInt(DARK[i].c));
        GuiSetStyle(DEFAULT, BACKGROUND_COLOR, ColorToInt((Color){24, 30, 39, 255}));
        GuiSetStyle(DEFAULT, LINE_COLOR, ColorToInt((Color){70, 80, 96, 255}));
    }
    GuiSetFont(chartFont(false));
    GuiSetStyle(DEFAULT, TEXT_SIZE, 17);
    GuiSetStyle(LISTVIEW, LIST_ITEMS_HEIGHT, 28);
}

static void showStep(Ui *ui, int step) {
    ui->showStep = step >= N_SHOW ? -1 : step;   // after the last chart, the title again
    ui->showTime = 0;
    if(ui->showStep < 0) {
        ui->playing = false;
        return;
    }
    View v = SHOW[ui->showStep];
    if(v == VIEW_WORD) searchWord(ui, ADDRESS_WORDS);
    selectView(ui, v);
    ui->playing = viewIsAnimated(v);
    ui->year = 1920;
}

static void toggleExhibition(Ui *ui) {
    ui->exhibition = !ui->exhibition;
    setFullScreen(ui, ui->exhibition);
    if(ui->exhibition) showStep(ui, -1);
    else ui->playing = false;
}

// The exhibition mode moves on after SHOW_SECONDS, or when an animation has ended and
// rested a few seconds; any key or click stops it.
static void updateExhibition(Ui *ui) {
    if(GetKeyPressed() || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        toggleExhibition(ui);
        return;
    }
    ui->showTime += GetFrameTime();
    if(ui->showStep < 0) {
        if(ui->showTime > TITLE_SECONDS) showStep(ui, 0);
        return;
    }
    bool animated = viewIsAnimated(ui->view);
    if((!animated && ui->showTime > SHOW_SECONDS) || (animated && !ui->playing && ui->showTime > 4)) {
        showStep(ui, ui->showStep + 1);
    }
    if(animated && ui->playing) ui->showTime = 0;   // the rest is counted after the end
}

static void update(Ui *ui) {
    ui->fade += GetFrameTime();
    if(ui->exhibition) updateExhibition(ui);
    else if(!ui->editing) {
        if(IsKeyPressed(KEY_F)) setFullScreen(ui, !ui->fullScreen);
        if(IsKeyPressed(KEY_T)) setTheme(ui, !ui->dark);
        if(IsKeyPressed(KEY_P)) savePng(ui);
        if(IsKeyPressed(KEY_E)) toggleExhibition(ui);
        if(IsKeyPressed(KEY_SPACE)) {
            if(!viewIsAnimated(ui->view)) selectView(ui, VIEW_TIME_MACHINE);
            togglePlay(ui);
        }
        int n = ui->study->nConcepts;
        if(ui->view == VIEW_CONCEPT && n > 0) {
            if(IsKeyPressed(KEY_DOWN)) ui->concept = (ui->concept + 1) % n;
            if(IsKeyPressed(KEY_UP)) ui->concept = (ui->concept + n - 1) % n;
        }
        if(viewIsAnimated(ui->view)) {
            if(IsKeyPressed(KEY_RIGHT)) ui->year = fminf(2023, ui->year + 1);
            if(IsKeyPressed(KEY_LEFT)) ui->year = fmaxf(1920, ui->year - 1);
        }
    }
    if(ui->playing) {
        ui->year += GetFrameTime() * yearsPerSecond(ui->view);   // the century in under a minute
        if(ui->year >= 2023) {
            ui->year = 2023;
            ui->playing = false;
        }
    }
}

static void sidebar(Ui *ui, float h) {
    DrawRectangle(0, 0, SIDEBAR, (int)h, SIDE_BG);
    label("Türkçenin", 20, 20, 26, SIDE_INK, true);
    label("yüz yıllık değişimi", 20, 50, 26, SIDE_INK, true);
    label("TBMM tutanakları, 1920–2023", 20, 84, 16, SIDE_MUTED, false);

    // The views.
    int selected = ui->menu;
    GuiListView((Rectangle){20, 118, SIDEBAR - 40, N_MENU * 32 + 8}, ui->menuText, &ui->menuScroll, &ui->menu);
    if(ui->menu < 0) ui->menu = selected;   // clicking the selected item again unselects it in raygui
    if(ui->menu != selected) ui->view = MENU[ui->menu];

    float y = 118 + N_MENU * 32 + 30;
    if(ui->view == VIEW_CONCEPT || ui->view == VIEW_WORD) {
        label("Kavramlar", 20, y, 16, SIDE_INK, true);
        int previous = ui->concept;
        GuiListView((Rectangle){20, y + 24, SIDEBAR - 40, h - y - 250}, ui->conceptText, &ui->conceptScroll, &ui->concept);
        if(ui->concept < 0) ui->concept = previous;
        if(ui->concept != previous) ui->view = VIEW_CONCEPT;
        float sy = h - 210;
        label("Gruplama", 20, sy, 16, SIDE_INK, true);
        GuiToggleGroup((Rectangle){20, sy + 24, (SIDEBAR - 48) / 3.0f, 28}, "Yıl;5 yıl;10 yıl", &ui->binChoice);
        label("Herhangi bir sözcük", 20, sy + 66, 16, SIDE_INK, true);
        if(GuiTextBox((Rectangle){20, sy + 90, SIDEBAR - 120, 32}, ui->search, sizeof ui->search, ui->editing)) ui->editing = !ui->editing;
        bool go = GuiButton((Rectangle){SIDEBAR - 92, sy + 90, 72, 32}, "Ara");
        if((go || (!ui->editing && IsKeyPressed(KEY_ENTER))) && ui->search[0]) searchWord(ui, ui->search);
    } else if(viewIsAnimated(ui->view)) {
        if(GuiButton((Rectangle){20, y, SIDEBAR - 40, 40}, ui->playing ? "Durdur" : "Oynat")) togglePlay(ui);
        label("Yıl", 20, y + 60, 16, SIDE_INK, true);
        GuiSliderBar((Rectangle){20, y + 84, SIDEBAR - 40, 24}, NULL, NULL, &ui->year, 1920, 2023);
        label("Boşluk: oynat / durdur", 20, y + 124, 15, SIDE_MUTED, false);
        label("← →: bir yıl geri / ileri", 20, y + 146, 15, SIDE_MUTED, false);
    } else {
        static const char *HELP[VIEW_COUNT] = {
            [VIEW_TIMELINE] = "Yeni sözcüğün kullanımın\nyarısını geçtiği yıl.\n\nAlttaki çubuklar:\ndeğişimler reformlardan\nsonra mı toplanıyor?",
            [VIEW_RACES] = "Bütün kavramların\nS-eğrileri bir arada.",
            [VIEW_ORIGINS] = "İsim ve sıfatların\nArapça, Farsça, Türkçe\nve Batı kökenli payı.",
            [VIEW_ERAS] = "İki dönemi en iyi ayıran\nsözcükler, kökenlerine\ngöre renkli.",
            [VIEW_HEATMAP] = "Her kavram, her yıl:\nkırmızı eski sözcük,\nmavi bugünkü sözcük önde.\n\n1950'lerdeki kırmızı\nşerit: eskiye dönüş.",
            [VIEW_DECADES] = "Her on yılın en belirgin\nsözcükleri: dil ve tarih\nbir arada.",
            [VIEW_SURVIVAL] = "1920'lerin sık\nsözcüklerinden kaçı\nhâlâ kullanılıyor?",
            [VIEW_DATING] = "Program bir metnin\nyılını yalnızca\nsözcüklerinden tahmin\nediyor. Ne kadar iyi?",
        };
        if(HELP[ui->view]) label(HELP[ui->view], 20, y, 17, SIDE_MUTED, false);
    }
    label("F tam ekran · P PNG · E sergi · T tema", 20, h - 62, 15, SIDE_MUTED, false);
    label(ui->status, 20, h - 38, 14, SIDE_MUTED, false);
}

// "1955'te", "1950'de": the suffix follows how the number is read.
static const char *yearLocative(int year) {
    static char buf[4][16];
    static int next = 0;
    static const char *ONES[] = {"", "bir", "iki", "üç", "dört", "beş", "altı", "yedi", "sekiz", "dokuz"};
    static const char *TENS[] = {"", "on", "yirmi", "otuz", "kırk", "elli", "altmış", "yetmiş", "seksen", "doksan"};
    const char *word = year % 10 ? ONES[year % 10] : year % 100 ? TENS[year % 100 / 10] : year % 1000 ? "yüz" : "bin";
    bool front = true, hard = false;
    for(const char *p = word; *p;) {   // the last vowel decides e or a; the last letter d or t
        uint32_t cp;
        p += utf8Decode(p, &cp);
        if(cp == 'e' || cp == 'i' || cp == 0xF6 || cp == 0xFC) front = true;
        if(cp == 'a' || cp == 0x131 || cp == 'o' || cp == 'u') front = false;
        hard = cp == 0xE7 || cp == 'f' || cp == 'h' || cp == 'k' || cp == 'p' || cp == 's' || cp == 0x15F || cp == 't';
    }
    char *out = buf[next++ % 4];
    snprintf(out, 16, "%d'%c%c", year, hard ? 't' : 'd', front ? 'e' : 'a');
    return out;
}

static bool crossedOver(const ConceptStudy *c) {
    FitStatus st = fitStatus(&c->fit);
    return st == STATUS_REPLACED || st == STATUS_PARTIAL;
}

// One line under each chart in the exhibition mode: what to see in it, with its numbers.
static void captionFor(const Ui *ui, View v, char *out, int size) {
    const Study *s = ui->study;
    int crossed = 0, best = 1920, most = 0;
    for(int i = 0; i < s->nConcepts; i++) crossed += crossedOver(&s->concepts[i]);
    for(int y = 1920; y <= 2009; y++) {   // the 15 years in which the most meanings changed
        int n = 0;
        for(int i = 0; i < s->nConcepts; i++) {
            double c = s->concepts[i].fit.crossover;
            n += crossedOver(&s->concepts[i]) && c >= y && c < y + 15;
        }
        if(n > most) { most = n; best = y; }
    }
    float a[ORIGIN_COUNT], b[ORIGIN_COUNT];
    int ahead50 = 0, ahead55 = 0;
    for(int i = 0; i < s->nConcepts; i++) {
        ahead50 += observedShare(&s->concepts[i], 1950) > 0.5f;
        ahead55 += observedShare(&s->concepts[i], 1955) > 0.5f;
    }
    const Survival *sv = &s->survival;
    int last = DECADES - 1;
    while(last > 0 && isnan(sv->alive[ORIGIN_COUNT][last])) last--;
    switch(v) {
        case VIEW_TIMELINE:
            snprintf(out, size, "%d kavramın %d tanesinde yeni sözcük öne geçti. En yoğun dönem %d–%d: %d geçiş bu on beş yılda.",
                     s->nConcepts, crossed, best, best + 14, most);
            break;
        case VIEW_HEATMAP:
            snprintf(out, size, "%s %d kavramda yeni sözcük öndeydi, %s yalnızca %d kavramda: 1952'den sonra eski sözcükler geri döndü.",
                     yearLocative(1950), ahead50, yearLocative(1955), ahead55);
            break;
        case VIEW_TIME_MACHINE:
            snprintf(out, size, "Yüz yıl yarım dakikada: her satırda eski sözcük (kırmızı) ve bugünkü sözcük (mavi) yarışıyor.");
            break;
        case VIEW_ORIGINS:
            if(originShares(s, 1924, a) && originShares(s, 2020, b)) {
                snprintf(out, size, "Arapça ve Farsça kökenli isim ve sıfatlar: 1920'lerin başında %%%.0f, 2020'lerde %%%.0f.",
                         100 * (a[ORIGIN_ARABIC] + a[ORIGIN_PERSIAN]), 100 * (b[ORIGIN_ARABIC] + b[ORIGIN_PERSIAN]));
            } else {
                snprintf(out, size, "İsim ve sıfatların kökeni, yıl yıl.");
            }
            break;
        case VIEW_SURVIVAL:
            snprintf(out, size, "1920'lerin sık sözcüklerinden bugün Türkçe olanların %%%.0f kadarı yaşıyor, Arapça olanların %%%.0f kadarı.",
                     100 * sv->alive[ORIGIN_TURKISH][last], 100 * sv->alive[ORIGIN_ARABIC][last]);
            break;
        case VIEW_RACES:
            snprintf(out, size, "Her kutu bir kavram: değişim yavaş başlıyor, hızlanıyor, yavaşlıyor. Dil değişimi S-eğrisi çizer.");
            break;
        case VIEW_DECADES:
            snprintf(out, size, "Her on yılın sözcükleri o yılların gündemini de gösteriyor: dilin tarihi, ülkenin tarihi.");
            break;
        case VIEW_RANKING:
            snprintf(out, size, "Meclisin en sık sözcükleri: kırmızılar (Arapça) azalıyor, maviler (Türkçe) çoğalıyor.");
            break;
        case VIEW_ERAS:
            snprintf(out, size, "1920'lerin meclisini bugünün meclisinden en iyi ayıran sözcükler.");
            break;
        case VIEW_DATING:
            snprintf(out, size, "Bir program, bir oturumun yılını yalnızca sözcüklerinden ortalama %s yıl hatayla buluyor.",
                     decimalComma(s->datingTest.meanError[DATE_LENGTHS - 1], 1));
            break;
        case VIEW_WORD:
            snprintf(out, size, "Milletvekilleri birbirine nasıl seslendi? Efendi, 1934'te unvanlar kaldırılınca düştü; sonra muhterem, sayın, değerli.");
            break;
        default:
            out[0] = 0;
    }
}

// The exhibition's opening slide: the project in one screen, with one word pair after another.
static void drawTitle(const Ui *ui, float w, float h) {
    const Study *s = ui->study;
    float k = fminf(w / 1600, h / 900);
    ClearBackground(chartBackground());
    label("TBMM TUTANAKLARI · 1920–2023", 120 * k, 170 * k, 22 * k, SIDE_MUTED, false);
    label("Türkçenin yüz yıllık değişimi", 114 * k, 210 * k, 92 * k, SIDE_INK, true);
    char line[200];
    snprintf(line, sizeof line, "%d oturum · %s milyon sözcük · %d kavram", s->sittings, decimalComma(s->words / 1e6, 1), s->nConcepts);
    label(line, 120 * k, 330 * k, 30 * k, SIDE_MUTED, false);

    // One crossed meaning every 1.6 seconds: the old word struck out, today's word, its year.
    int order[256], crossed = 0;
    conceptsByCrossover(s, order);
    while(crossed < s->nConcepts && crossedOver(&s->concepts[order[crossed]])) crossed++;
    if(crossed > 0) {
        const ConceptStudy *c = &s->concepts[order[(int)(ui->showTime / 1.6f) % crossed]];
        const char *newWord = c->words[c->nWords - 1];
        for(int i = 0; i < c->nWords; i++) if(strcmp(c->roles[i], "new") == 0) newWord = c->words[i];
        float t = fmodf(ui->showTime, 1.6f), alpha = fminf(1, t * 4) * fminf(1, (1.6f - t) * 5);
        Font bold = chartFont(true);
        float size = 120 * k, y = 470 * k, x = 120 * k;
        Color old = {199, 81, 52, 255}, neu = ui->dark ? (Color){61, 155, 224, 255} : (Color){0, 114, 178, 255};
        DrawTextEx(bold, c->words[0], (Vector2){x, y}, size, size / 40, Fade(old, alpha));
        float ow = MeasureTextEx(bold, c->words[0], size, size / 40).x;
        DrawLineEx((Vector2){x, y + size * 0.55f}, (Vector2){x + ow, y + size * 0.55f}, 8 * k, Fade(old, alpha));
        x += ow + 50 * k;
        DrawTextEx(bold, "→", (Vector2){x, y}, size, size / 40, Fade(SIDE_MUTED, alpha));
        x += MeasureTextEx(bold, "→", size, size / 40).x + 50 * k;
        DrawTextEx(bold, newWord, (Vector2){x, y}, size, size / 40, Fade(neu, alpha));
        x += MeasureTextEx(bold, newWord, size, size / 40).x + 40 * k;
        snprintf(line, sizeof line, "%.0f", c->fit.crossover);
        label(line, x, y + 50 * k, 44 * k, Fade(SIDE_MUTED, alpha), false);
    }
    label("Hangi sözcük eskisinin yerini ne zaman aldı, hangisi tutmadı, hangisi geri döndü?", 120 * k, 720 * k, 30 * k, SIDE_INK, false);
}

static void draw(Ui *ui, float w, float h, bool hover) {
    ClearBackground(chartBackground());
    ChartState st = chartState(ui, hover && !ui->editing);
    if(ui->view != ui->shownView) {   // a new view fades in
        ui->shownView = ui->view;
        ui->fade = 0;
    }
    if(ui->exhibition && ui->showStep < 0) {
        drawTitle(ui, w, h);
        return;
    }
    Rectangle area = ui->fullScreen ? (Rectangle){0, 0, w, h} : (Rectangle){SIDEBAR, 0, w - SIDEBAR, h};
    if(ui->exhibition) area.height = h * (1 - CAPTION_HEIGHT);
    drawView(ui->view, &st, area);
    if(ui->fade < FADE_SECONDS) DrawRectangleRec(area, Fade(chartBackground(), 1 - ui->fade / FADE_SECONDS));
    if(ui->exhibition) {   // the caption band, with a line showing how long until the next chart
        char caption[400];
        captionFor(ui, ui->view, caption, sizeof caption);
        float bandY = area.height, bandH = h - bandY, k = fminf(w / 1600, h / 900);
        DrawRectangle(0, (int)bandY, (int)w, (int)bandH + 1, (Color){24, 30, 39, 255});
        float size = 30 * k;   // shrunk until the caption fits on one line
        while(size > 16 * k && MeasureTextEx(chartFont(false), caption, size, size / 40).x > w - 120 * k) size -= k;
        label(caption, 60 * k, bandY + (bandH - size) / 2, size, (Color){240, 242, 246, 255}, false);
        float progress = viewIsAnimated(ui->view) ? (ui->year - 1920) / (2023 - 1920) : ui->showTime / SHOW_SECONDS;
        DrawRectangle(0, (int)bandY, (int)(w * fminf(1, progress)), (int)(4 * k) + 1, (Color){61, 155, 224, 255});
        return;
    }
    if(!ui->fullScreen) sidebar(ui, h);
}


static void initUi(Ui *ui) {
    ui->menuText[0] = 0;
    for(int i = 0; i < N_MENU; i++) {
        if(i) strcat(ui->menuText, ";");
        strcat(ui->menuText, viewName(MENU[i]));
    }
    ui->conceptText[0] = 0;
    for(int i = 0; i < ui->study->nConcepts; i++) {
        if(i) strcat(ui->conceptText, ";");
        strcat(ui->conceptText, ui->study->concepts[i].label);
    }
    ui->view = MENU[0];
    searchWord(ui, ADDRESS_WORDS);
    ui->view = MENU[0];
    ui->year = 1920;
    ui->binChoice = 1;
    ui->shownView = ui->view;
    ui->fade = FADE_SECONDS;
    setTheme(ui, false);
}

// NAME on the command line: a view, a concept, or a word.
static void selectByName(Ui *ui, const char *name) {
    static const struct { const char *name; View view; } NAMES[] = {
        {"takvim", VIEW_TIMELINE}, {"parmakizi", VIEW_HEATMAP}, {"yarislar", VIEW_RACES}, {"koken", VIEW_ORIGINS},
        {"omur", VIEW_SURVIVAL}, {"sozcukler", VIEW_ERAS}, {"onyillar", VIEW_DECADES}, {"zaman", VIEW_TIME_MACHINE},
        {"siralama", VIEW_RANKING}, {"tarih", VIEW_DATING},
    };
    for(size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++) {
        size_t len = strlen(NAMES[i].name);
        if(strncmp(name, NAMES[i].name, len) == 0 && (name[len] == 0 || name[len] == ':')) {
            ui->view = NAMES[i].view;
            if(name[len] == ':') ui->year = (float)atof(name + len + 1);
            return;
        }
    }
    char folded[MAX_WORD];
    trFoldString(name, folded, sizeof folded);
    for(int i = 0; i < ui->study->nConcepts && !strchr(name, ','); i++) {
        if(strcmp(ui->study->concepts[i].concept, folded) == 0) {
            ui->view = VIEW_CONCEPT;
            ui->concept = i;
            return;
        }
    }
    searchWord(ui, name);
}

// Every chart for the report: the overviews, the time machine at a few years, every concept.
static int saveFigures(Ui *ui, const char *dir) {
    MakeDirectory(dir);
    char path[1024];
    int saved = 0, wanted = 0;
    searchWord(ui, ADDRESS_WORDS);
    static const struct { View view; const char *file; } OVERVIEWS[] = {
        {VIEW_TIMELINE, "01-takvim"}, {VIEW_RACES, "02-yarislar"}, {VIEW_ORIGINS, "03-koken"}, {VIEW_ERAS, "04-sozcukler"},
        {VIEW_HEATMAP, "06-parmak-izi"}, {VIEW_SURVIVAL, "07-sozcuklerin-omru"}, {VIEW_DECADES, "08-on-yillar"},
        {VIEW_WORD, "10-hitap"}, {VIEW_DATING, "11-metnin-yasi"},
    };
    for(size_t i = 0; i < sizeof OVERVIEWS / sizeof OVERVIEWS[0]; i++, wanted++) {
        snprintf(path, sizeof path, "%s/%s.png", dir, OVERVIEWS[i].file);
        ChartState st = chartState(ui, false);
        saved += saveView(OVERVIEWS[i].view, &st, 1920, 1080, path);
    }
    int years[] = {1925, 1940, 1955, 1975, 2020};
    for(size_t i = 0; i < sizeof years / sizeof years[0]; i++, wanted++) {
        ui->year = (float)years[i];
        snprintf(path, sizeof path, "%s/05-zaman-%d.png", dir, years[i]);
        ChartState st = chartState(ui, false);
        saved += saveView(VIEW_TIME_MACHINE, &st, 1920, 1080, path);
        snprintf(path, sizeof path, "%s/09-siralama-%d.png", dir, years[i]);
        saved += saveView(VIEW_RANKING, &st, 1920, 1080, path);
        wanted++;
    }
    for(int i = 0; i < ui->study->nConcepts; i++, wanted++) {
        ui->concept = i;
        snprintf(path, sizeof path, "%s/kavram-%s.png", dir, ui->study->concepts[i].concept);
        ChartState st = chartState(ui, false);
        saved += saveView(VIEW_CONCEPT, &st, 1920, 1080, path);
    }
    printf("%d of %d figures saved in %s\n", saved, wanted, dir);
    return saved == wanted ? 0 : 1;
}

// An animated view as numbered frames, 30 per second at its playing speed, with a pause at
// the start and at the end; `make video` joins them into an MP4.
static int saveVideoFrames(Ui *ui, View view, const char *dir) {
    MakeDirectory(dir);
    const int fps = 30, holdStart = fps, holdEnd = 2 * fps;
    const float perFrame = yearsPerSecond(view) / fps;
    int moving = (int)((2023 - 1920) / perFrame) + 1, total = holdStart + moving + holdEnd, saved = 0;
    for(int f = 0; f < total; f++) {
        int step = f - holdStart;
        ui->year = step < 0 ? 1920 : step >= moving ? 2023 : 1920 + step * perFrame;
        char path[1024];
        snprintf(path, sizeof path, "%s/%04d.png", dir, f);
        ChartState st = chartState(ui, false);
        saved += saveView(view, &st, 1920, 1080, path);
        if(f % 100 == 0) printf("frame %d of %d\n", f, total);
    }
    printf("%d frames saved in %s\n", saved, dir);
    return saved == total ? 0 : 1;
}

static int saveWindow(Ui *ui, const char *path, int width, int height) {
    RenderTexture2D rt = LoadRenderTexture(width, height);
    BeginTextureMode(rt);
    draw(ui, width, height, false);
    EndTextureMode();
    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);   // render textures are stored upside down
    makeOpaque(&img);
    bool ok = ExportImage(img, path);
    UnloadImage(img);
    UnloadRenderTexture(rt);
    printf("%s %s\n", ok ? "saved" : "could not save", path);
    return !ok;
}

int main(int argc, char **argv) {
    const char *shotName = NULL, *shotPath = NULL, *figuresDir = NULL, *uiShotPath = NULL, *videoName = NULL, *videoDir = NULL;
    const char *showPath = NULL;
    int showAt = 0;
    bool dark = argc > 1 && strcmp(argv[1], "--dark") == 0;   // the projector theme, for any mode
    if(dark) {
        argv++;
        argc--;
    }
    int arg = 1;
    if(argc >= 4 && strcmp(argv[1], "--show-shot") == 0) {
        showAt = atoi(argv[2]);
        showPath = argv[3];
        arg = 4;
    } else if(argc >= 4 && strcmp(argv[1], "--shot") == 0) {
        shotName = argv[2];
        shotPath = argv[3];
        arg = 4;
    } else if(argc >= 3 && strcmp(argv[1], "--figures") == 0) {
        figuresDir = argv[2];
        arg = 3;
    } else if(argc >= 3 && strcmp(argv[1], "--ui-shot") == 0) {
        uiShotPath = argv[2];
        arg = 3;
    } else if(argc >= 4 && strcmp(argv[1], "--video") == 0) {
        videoName = argv[2];
        videoDir = argv[3];
        arg = 4;
    }
    const char *dbPath = argc > arg ? argv[arg] : "db/corpus.sqlite";
    const char *conceptsPath = argc > arg + 1 ? argv[arg + 1] : "data/concepts.tsv";
    if(!FileExists(dbPath)) {
        fprintf(stderr, "%s not found: build it first with `make pilot`\n", dbPath);
        return 1;
    }
    bool offscreen = shotPath || figuresDir || uiShotPath || videoDir || showPath;

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(offscreen ? FLAG_WINDOW_HIDDEN : FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(WIDTH, HEIGHT, "Türkçenin yüz yıllık değişimi");
    SetTargetFPS(60);
    loadChartFonts();
    if(!offscreen) {   // the study takes a few seconds: say so
        BeginDrawing();
        ClearBackground((Color){250, 248, 243, 255});
        label("Hesaplanıyor…", 40, 40, 32, SIDE_INK, true);
        EndDrawing();
    }

    static Ui ui;
    ui.db = dbOpen(dbPath);
    ui.conceptsPath = conceptsPath;
    ui.study = loadStudy(ui.db, conceptsPath, BOOTSTRAP_ROUNDS);
    if(!ui.study) {
        perror(conceptsPath);
        return 1;
    }
    initUi(&ui);
    if(dark) setTheme(&ui, true);

    int status = 0;
    if(shotPath) {
        selectByName(&ui, shotName);
        ChartState st = chartState(&ui, false);
        bool ok = saveView(ui.view, &st, 1920, 1080, shotPath);
        printf("%s %s\n", ok ? "saved" : "could not save", shotPath);
        status = !ok;
    } else if(figuresDir) {
        status = saveFigures(&ui, figuresDir);
    } else if(uiShotPath) {
        status = saveWindow(&ui, uiShotPath, WIDTH, HEIGHT);
    } else if(showPath) {   // one exhibition slide, a few seconds in (animations at 1955)
        ui.exhibition = ui.fullScreen = true;
        showStep(&ui, showAt);
        ui.showTime = 3;
        if(viewIsAnimated(ui.view)) ui.year = 1955;
        ui.shownView = ui.view;
        ui.fade = FADE_SECONDS;
        status = saveWindow(&ui, showPath, 1920, 1080);
    } else if(videoDir) {
        selectByName(&ui, videoName);
        if(!viewIsAnimated(ui.view)) {
            fprintf(stderr, "--video takes zaman or siralama, not %s\n", videoName);
            status = 1;
        } else {
            status = saveVideoFrames(&ui, ui.view, videoDir);
        }
    } else {
        while(!WindowShouldClose()) {
            update(&ui);
            BeginDrawing();
            draw(&ui, (float)GetScreenWidth(), (float)GetScreenHeight(), true);
            EndDrawing();
        }
    }
    freeStudy(ui.study);
    unloadChartFonts();
    CloseWindow();
    sqlite3_close(ui.db);
    return status;
}
