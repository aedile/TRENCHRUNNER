/*
 * marquee.h - vector-style text in the letterbox bars above and below the game picture
 * (portrait layout). Stroke font in the Atari vector style; static or scrolling.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MARQUEE_TOP = 0, MARQUEE_BOTTOM = 1 };

/* colors as the AVG sees them: bit 2 red, bit 1 green, bit 0 blue */
enum {
    MARQUEE_NONE = 0,
    MARQUEE_BLUE = 1, MARQUEE_GREEN = 2, MARQUEE_CYAN = 3,
    MARQUEE_RED = 4, MARQUEE_MAGENTA = 5, MARQUEE_YELLOW = 6, MARQUEE_WHITE = 7
};

/* Set the text for one bar. text = NULL or "" clears it. Asterisks are drawn in star_color
 * (MARQUEE_NONE = same as the text). scale is pixels per font unit (2 gives 20-pixel-tall
 * letters). scroll_px_s = 0 shows the text centred; otherwise it scrolls right to left at that
 * many pixels per second and wraps. Letters A-Z, 0-9 and - . , : ! ? ' * / are drawn; lower
 * case is drawn as upper case; anything else is a space. */
void marquee_set(int bar, const char *text, uint8_t color, uint8_t star_color, int scale, int scroll_px_s);

/* Draw both bars into an 8-bit palette-index frame buffer of fb_w x fb_h; the picture occupies
 * rows [pic_top, pic_bottom) and the bars are everything above and below it. Called by the
 * renderer once per frame. */
void marquee_draw(uint8_t *fb, int fb_w, int fb_h, int pic_top, int pic_bottom);

#ifdef __cplusplus
}
#endif
