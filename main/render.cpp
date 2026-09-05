/*
 * render.cpp - rasterize AVG vector lists into an 8-bit frame buffer and push it to the ST7789
 * from a dedicated task.
 */
#include "render.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "RENDER";

#define FB_W DISPLAY_WIDTH     /* 240 panel columns */
#define FB_H DISPLAY_HEIGHT    /* 280 panel rows    */
#define ROWS_PER_CHUNK 14      /* 240*14*2 = 6720 bytes per DMA chunk */
#define NUM_LISTS 2

/* Orientation: beam X (0..AVG_XMAX) -> panel rows, beam Y (0..AVG_YMAX) -> panel columns. */
#define FLIP_BEAM_X 0
#define FLIP_BEAM_Y 1

/* compact vector point: panel coordinates + 8-bit palette index (0 = move only) */
typedef struct { int16_t x, y; uint8_t idx; uint8_t pad; } vpoint_t;
typedef struct { vpoint_t pts[AVG_MAX_POINTS]; int n; } vlist_t;

static vlist_t *lists[NUM_LISTS];
static QueueHandle_t free_q, frame_q;
static uint8_t *fb;                         /* FB_W * FB_H palette indices */
static uint16_t *chunk;                     /* RGB565 (byte-swapped) conversion buffer */
static uint16_t palette[256];
static uint32_t frames_drawn, frames_dropped;
static uint64_t busy_us;

/* palette index: bits 0-2 = AVG color (bit 2 red, bit 1 green, bit 0 blue, as in
 * MAME's vector color111), bits 3-7 intensity (0..31) */
static inline uint8_t make_idx(uint8_t color, uint8_t intensity)
{
    unsigned bv = (unsigned)intensity * 3 / 2;    /* phosphor glow reads brighter than raw DAC level */
    if (bv > 255) bv = 255;
    uint8_t i5 = (uint8_t)(bv >> 3);
    if (i5 == 0 && intensity) i5 = 1;
    return (uint8_t)((color & 7) | (i5 << 3));
}

static void build_palette(void)
{
    for (int i = 0; i < 256; i++) {
        unsigned v = ((i >> 3) * 255) / 31;
        unsigned r = (i & 4) ? v : 0, g = (i & 2) ? v : 0, b = (i & 1) ? v : 0;
        uint16_t c = (uint16_t)((r & 0xF8) << 8) | (uint16_t)((g & 0xFC) << 3) | (uint16_t)(b >> 3);
        palette[i] = (uint16_t)((c >> 8) | (c << 8));
    }
    palette[0] = 0;
}

static inline void beam_to_panel(int32_t bx, int32_t by, int *px, int *py)
{
    int gx = bx >> 16, gy = by >> 16;                 /* 0..250, 0..280 */
    if (gx < 0) gx = 0;
    if (gx > AVG_XMAX) gx = AVG_XMAX;
    if (gy < 0) gy = 0;
    if (gy > AVG_YMAX) gy = AVG_YMAX;
    int row = gx * (FB_H - 1) / AVG_XMAX;
    int col = gy * (FB_W - 1) / AVG_YMAX;
#if FLIP_BEAM_X
    row = FB_H - 1 - row;
#endif
#if FLIP_BEAM_Y
    col = FB_W - 1 - col;
#endif
    *px = col; *py = row;
}

/* endpoints are always inside the frame buffer, so no per-pixel bounds checks */
static void line(int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    uint8_t *p = fb + y0 * FB_W + x0;
    int stepy = sy * FB_W;
    for (;;) {
        *p = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; p += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; p += stepy; }
    }
}

static void rasterize(const vlist_t *l)
{
    memset(fb, 0, FB_W * FB_H);
    int px = 0, py = 0;
    for (int i = 0; i < l->n; i++) {
        const vpoint_t *p = &l->pts[i];
        if (i > 0 && p->idx) line(px, py, p->x, p->y, p->idx);
        px = p->x; py = p->y;
    }
}

static void present(void)
{
    display_set_window(0, 0, FB_W, FB_H);
    for (int row = 0; row < FB_H; row += ROWS_PER_CHUNK) {
        int rows = (row + ROWS_PER_CHUNK <= FB_H) ? ROWS_PER_CHUNK : (FB_H - row);
        const uint8_t *src = fb + row * FB_W;
        int n = rows * FB_W;
        for (int i = 0; i < n; i++) chunk[i] = palette[src[i]];
        display_write_preswapped(chunk, n);     /* blocks on the previous DMA chunk: emulator runs meanwhile */
    }
    display_wait_done();
}

static void render_task(void *arg)
{
    (void)arg;
    for (;;) {
        vlist_t *l;
        if (xQueueReceive(frame_q, &l, portMAX_DELAY) != pdTRUE) continue;
        int64_t t0 = esp_timer_get_time();
        rasterize(l);
        xQueueSend(free_q, &l, 0);            /* list consumed; emulator may reuse it */
        int64_t t1 = esp_timer_get_time();
        present();
        busy_us += (t1 - t0) + 2500;          /* rasterize + ~2.5 ms of chunk conversion inside present() */
        frames_drawn++;
    }
}

void render_init(void)
{
    fb = (uint8_t *)heap_caps_malloc(FB_W * FB_H, MALLOC_CAP_8BIT);
    chunk = (uint16_t *)heap_caps_malloc(ROWS_PER_CHUNK * FB_W * sizeof(uint16_t), MALLOC_CAP_8BIT);
    free_q = xQueueCreate(NUM_LISTS, sizeof(vlist_t *));
    frame_q = xQueueCreate(NUM_LISTS, sizeof(vlist_t *));
    for (int i = 0; i < NUM_LISTS; i++) {
        lists[i] = (vlist_t *)heap_caps_malloc(sizeof(vlist_t), MALLOC_CAP_8BIT);
        if (!lists[i]) { ESP_LOGE(TAG, "list allocation failed"); abort(); }
        xQueueSend(free_q, &lists[i], 0);
    }
    if (!fb || !chunk) { ESP_LOGE(TAG, "frame buffer allocation failed"); abort(); }
    memset(fb, 0, FB_W * FB_H);
    build_palette();
    /* higher priority than the emulator: it mostly sleeps on DMA and preempts only briefly */
    xTaskCreate(render_task, "render", 4096, nullptr, 6, nullptr);
    ESP_LOGI(TAG, "render task started (%d x %d, %d lists)", FB_W, FB_H, NUM_LISTS);
}

void render_submit(const avg_point_t *points, int npoints)
{
    vlist_t *l;
    if (xQueueReceive(free_q, &l, 0) != pdTRUE) {
        frames_dropped++;
        return;
    }
    int n = npoints > AVG_MAX_POINTS ? AVG_MAX_POINTS : npoints;
    for (int i = 0; i < n; i++) {
        int x, y;
        beam_to_panel(points[i].x, points[i].y, &x, &y);
        l->pts[i].x = (int16_t)x;
        l->pts[i].y = (int16_t)y;
        l->pts[i].idx = points[i].intensity ? make_idx(points[i].color, points[i].intensity) : 0;
    }
    l->n = n;
    xQueueSend(frame_q, &l, 0);
}

uint32_t render_frames_drawn(void) { uint32_t v = frames_drawn; frames_drawn = 0; return v; }
uint32_t render_frames_dropped(void) { uint32_t v = frames_dropped; frames_dropped = 0; return v; }
uint64_t render_busy_us(void) { uint64_t v = busy_us; busy_us = 0; return v; }
