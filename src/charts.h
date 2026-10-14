#ifndef CHARTS_H
#define CHARTS_H

// The charts, drawn with raylib into any rectangle: the window, or an off-screen texture
// for PNG export. Designed at 1600x900 and scaled to the rectangle.
#include "raylib.h"

#include "analysis.h"

typedef enum {
    VIEW_CONCEPT,        // one meaning: its words' shares, the S-curve, the crossover year
    VIEW_TIMELINE,       // every meaning on one timeline: when the new word took over
    VIEW_RACES,          // small multiples: all S-curves at once, ordered by crossover
    VIEW_ORIGINS,        // Arabic / Persian / Turkish / Western share of the vocabulary per year
    VIEW_ERAS,           // the words most typical of the 1920s and of today
    VIEW_TIME_MACHINE,   // animated: one year at a time, words flip from old to new
    VIEW_WORD,           // searched words: frequency per million words
    VIEW_HEATMAP,        // every meaning, every year: a colour from old word to new word
    VIEW_DECADES,        // the words most typical of each decade
    VIEW_SURVIVAL,       // how many of the 1920s' most used words are still in use, by origin
    VIEW_RANKING,        // animated: the most used nouns and adjectives of each year
    VIEW_DATING,         // how well the program dates a text it has not seen
    VIEW_COUNT
} View;

const char *viewName(View v);   // Turkish, for the menu

// VIEW_WORD: up to MAX_SEARCH words typed together ("efendi, muhterem, sayın") to compare.
#define MAX_SEARCH 6
typedef struct {
    int n;
    char typed[MAX_SEARCH][MAX_WORD];    // as the user wrote it
    char word[MAX_SEARCH][MAX_WORD];     // the dictionary word, folded
    char origin[MAX_SEARCH][64];         // "" when the dictionary has none
    YearCounts counts[MAX_SEARCH];
} WordSearch;

typedef struct {
    const Study *study;
    int concept;          // VIEW_CONCEPT: index into study->concepts
    int binWidth;         // VIEW_CONCEPT: years per point (1, 5 or 10)
    float year;           // VIEW_TIME_MACHINE and VIEW_RANKING
    const WordSearch *search;   // VIEW_WORD
    bool hover;           // show values under the mouse (off for exports)
} ChartState;

// Light (paper) or dark (for projectors) neutrals; the data colours stay the same.
void setChartTheme(bool dark);
bool chartThemeIsDark(void);
Color chartBackground(void);

void loadChartFonts(void);
void unloadChartFonts(void);
Font chartFont(bool bold);

void drawView(View v, const ChartState *st, Rectangle area);
void makeOpaque(Image *img);   // every pixel fully opaque, for PNG export
bool saveView(View v, const ChartState *st, int width, int height, const char *path);

// The views that play over the years (VIEW_TIME_MACHINE, VIEW_RANKING).
bool viewIsAnimated(View v);

// The share of today's word around a year, from the counts (a window widened until it
// holds enough uses), and the origin shares (ORIGIN_COUNT values) around a year.
float observedShare(const ConceptStudy *c, float year);
bool originShares(const Study *s, float year, float *share);

// 5.55 → "5,55" (a static buffer, four calls deep).
const char *decimalComma(double v, int decimals);

// Concepts ordered by crossover year (those that never crossed last); fills order.
void conceptsByCrossover(const Study *s, int *order);

#endif
