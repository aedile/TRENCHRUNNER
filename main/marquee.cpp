/*
 * marquee.cpp - stroke-font text in the letterbox bars. Glyphs live on a 6-wide, 10-tall grid
 * (advance 8), drawn as polylines in the style of Atari's vector character generator.
 */
#include "marquee.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

/* Each glyph: strokes separated by ';', points "x,y" separated by spaces, y up. */
static const char *const glyphs[] = {
    /* A */ "0,0 0,7 3,10 6,7 6,0;0,4 6,4",
    /* B */ "0,0 0,10 4,10 6,8 6,6 4,5 0,5;4,5 6,4 6,2 4,0 0,0",
    /* C */ "6,8 4,10 2,10 0,8 0,2 2,0 4,0 6,2",
    /* D */ "0,0 0,10 4,10 6,8 6,2 4,0 0,0",
    /* E */ "6,0 0,0 0,10 6,10;0,5 4,5",
    /* F */ "0,0 0,10 6,10;0,5 4,5",
    /* G */ "6,8 4,10 2,10 0,8 0,2 2,0 4,0 6,2 6,4 3,4",
    /* H */ "0,0 0,10;6,0 6,10;0,5 6,5",
    /* I */ "1,0 5,0;3,0 3,10;1,10 5,10",
    /* J */ "0,2 2,0 4,0 6,2 6,10",
    /* K */ "0,0 0,10;6,10 0,4;2,6 6,0",
    /* L */ "0,10 0,0 6,0",
    /* M */ "0,0 0,10 3,6 6,10 6,0",
    /* N */ "0,0 0,10 6,0 6,10",
    /* O */ "0,2 0,8 2,10 4,10 6,8 6,2 4,0 2,0 0,2",
    /* P */ "0,0 0,10 4,10 6,8 6,6 4,4 0,4",
    /* Q */ "0,2 0,8 2,10 4,10 6,8 6,2 4,0 2,0 0,2;3,3 6,0",
    /* R */ "0,0 0,10 4,10 6,8 6,6 4,4 0,4;3,4 6,0",
    /* S */ "6,8 4,10 2,10 0,8 0,6 2,5 4,5 6,4 6,2 4,0 2,0 0,2",
    /* T */ "0,10 6,10;3,10 3,0",
    /* U */ "0,10 0,2 2,0 4,0 6,2 6,10",
    /* V */ "0,10 3,0 6,10",
    /* W */ "0,10 0,0 3,4 6,0 6,10",
    /* X */ "0,0 6,10;0,10 6,0",
    /* Y */ "0,10 3,5 6,10;3,5 3,0",
    /* Z */ "0,10 6,10 0,0 6,0",
    /* 0 */ "0,2 0,8 2,10 4,10 6,8 6,2 4,0 2,0 0,2",
    /* 1 */ "1,8 3,10 3,0;1,0 5,0",
    /* 2 */ "0,8 2,10 4,10 6,8 6,6 0,0 6,0",
    /* 3 */ "0,10 6,10 3,6 6,4 6,2 4,0 2,0 0,2",
    /* 4 */ "4,0 4,10 0,4 6,4",
    /* 5 */ "6,10 0,10 0,5 4,5 6,3 6,2 4,0 0,0",
    /* 6 */ "6,8 4,10 2,10 0,8 0,2 2,0 4,0 6,2 6,4 4,5 0,5",
    /* 7 */ "0,10 6,10 2,0",
    /* 8 */ "2,5 0,7 0,8 2,10 4,10 6,8 6,7 4,5 2,5 0,3 0,2 2,0 4,0 6,2 6,3 4,5",
    /* 9 */ "0,2 2,0 4,0 6,2 6,8 4,10 2,10 0,8 0,6 2,5 6,5",
    /* - */ "1,5 5,5",
    /* . */ "2,0 3,0 3,1 2,1 2,0",
    /* , */ "3,1 2,-1",
    /* : */ "3,7 3,8;3,2 3,3",
    /* ! */ "3,10 3,3;3,0 3,1",
    /* ? */ "0,8 2,10 4,10 6,8 6,6 3,4 3,3;3,0 3,1",
    /* ' */ "3,10 3,8",
    /* * */ "3,2 3,8;0,3 6,7;0,7 6,3",
    /* / */ "0,0 6,10",
};
static const char glyph_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.,:!?'*/";

#define GLYPH_W   6
#define GLYPH_H   10
#define ADVANCE   8
#define FULL_INTENSITY 31

typedef struct { char text[96]; uint8_t color, star_color; int scale; int scroll; } bar_t;
static bar_t bars[2];

static const char *glyph_for(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    const char *p = strchr(glyph_chars, c);
    return (p && c) ? glyphs[p - glyph_chars] : NULL;
}

static inline uint8_t pal(uint8_t color, unsigned intensity)
{
    return (uint8_t)((color & 7) | (intensity << 3));
}

void marquee_set(int bar, const char *text, uint8_t color, uint8_t star_color, int scale, int scroll_px_s)
{
    if (bar < 0 || bar > 1) return;
    bar_t *b = &bars[bar];
    if (!text) text = "";
    strncpy(b->text, text, sizeof(b->text) - 1);
    b->text[sizeof(b->text) - 1] = 0;
    b->color = pal(color, FULL_INTENSITY);
    b->star_color = star_color ? pal(star_color, FULL_INTENSITY) : b->color;
    b->scale = scale < 1 ? 1 : scale;
    b->scroll = scroll_px_s;
}

/* Bresenham with the x range clipped to [0, w) (y is always inside the bar by construction) */
static void line(uint8_t *fb, int w, int x0, int y0, int x1, int y1, uint8_t c)
{
    if ((x0 < 0 && x1 < 0) || (x0 >= w && x1 >= w)) return;
    if (x0 < 0 || x1 < 0 || x0 >= w || x1 >= w) {
        int dx = x1 - x0, dy = y1 - y0;             /* clip against the vertical edges, keeping the slope */
        if (x0 < 0)   { y0 += dy * (0 - x0) / dx; x0 = 0; }
        if (x1 < 0)   { y1 += dy * (0 - x1) / dx; x1 = 0; }
        if (x0 >= w)  { y0 += dy * (w - 1 - x0) / dx; x0 = w - 1; }
        if (x1 >= w)  { y1 += dy * (w - 1 - x1) / dx; x1 = w - 1; }
    }
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fb[y0 * w + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* draw one glyph with its origin (bottom-left of the grid) at panel (ox, oy_base), y up */
static void draw_glyph(uint8_t *fb, int w, const char *g, int ox, int oy_base, int scale, uint8_t c)
{
    int px = 0, py = 0, have = 0;
    while (*g) {
        if (*g == ';') { have = 0; g++; continue; }
        if (*g == ' ') { g++; continue; }
        int gx = (int)strtol(g, (char **)&g, 10);
        if (*g == ',') g++;
        int gy = (int)strtol(g, (char **)&g, 10);
        int x = ox + gx * scale, y = oy_base - gy * scale;
        if (have) line(fb, w, px, py, x, y, c);
        px = x; py = y; have = 1;
    }
}

static void draw_bar(uint8_t *fb, int fb_w, int bar_top, int bar_h, const bar_t *b, int64_t now_us)
{
    int n = (int)strlen(b->text);
    if (!n || bar_h < GLYPH_H * b->scale) return;
    int text_w = n * ADVANCE * b->scale;
    int oy_base = bar_top + (bar_h + GLYPH_H * b->scale) / 2;       /* baseline row, letters extend up */
    int ox;
    if (b->scroll <= 0) {
        ox = (fb_w - text_w + (ADVANCE - GLYPH_W) * b->scale) / 2;
    } else {
        int period = text_w + fb_w;                                   /* enters on the right, leaves on the left */
        int off = (int)((now_us * b->scroll / 1000000) % period);
        ox = fb_w - off;
    }
    for (int i = 0; i < n; i++, ox += ADVANCE * b->scale) {
        char ch = b->text[i];
        if (ox + GLYPH_W * b->scale < 0 || ox >= fb_w) continue;
        const char *g = glyph_for(ch);
        if (g) draw_glyph(fb, fb_w, g, ox, oy_base, b->scale, ch == '*' ? b->star_color : b->color);
    }
}

void marquee_draw(uint8_t *fb, int fb_w, int fb_h, int pic_top, int pic_bottom)
{
    int64_t now = esp_timer_get_time();
    if (pic_top > 0)
        draw_bar(fb, fb_w, 0, pic_top, &bars[MARQUEE_TOP], now);
    if (pic_bottom < fb_h)
        draw_bar(fb, fb_w, pic_bottom, fb_h - pic_bottom, &bars[MARQUEE_BOTTOM], now);
}
