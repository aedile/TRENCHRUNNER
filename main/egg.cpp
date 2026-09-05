/*
 * egg.cpp - clip player for the easter egg.
 *
 * Media layout (tools/pack_media.py): a header, a clip table, per-clip frame indexes, then
 * the JPEG frames and the MP3 stream of each clip. Video is decoded by the ROM TinyJPEG
 * decoder straight into 16-row strips that go to the panel as they complete, so no whole
 * frame is ever held in RAM. Audio is decoded by libhelix-mp3, resampled from the clip's
 * rate to the mixer's 20050 Hz, and streamed through audio_hal, which stays locked to the
 * DAC clock exactly as it is for the game.
 *
 * All working memory except the MP3 decoder state is carved out of the renderer's frame
 * buffer, which is idle while the game is paused, so the clip needs about 36 KB of heap.
 */
#include "egg.h"
#include "render.h"
#include "display.h"
#include "audio_hal.h"
#include "mp3dec.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp32c6/rom/tjpgd.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "EGG";

#define VIDEO_W 240
#define VIDEO_H 136
#define VIDEO_Y ((DISPLAY_HEIGHT - VIDEO_H) / 2)
#define STRIP_ROWS 16                                /* one 4:2:0 MCU row */
#define STRIP_HALF_BYTES (VIDEO_W * (STRIP_ROWS / 2) * 2)   /* under the display's DMA buffer */
#define MAX_BEHIND_US 120000
#define PIN_BTN_BOOT GPIO_NUM_9

/* scratch carve-up (bytes) */
#define RING_SAMPLES   4096                           /* ~200 ms of mixer-rate audio */
#define POOL_BYTES     8192                           /* TinyJPEG work area */
#define JPEG_BYTES     16384                          /* largest frame we accept */
#define INDEX_BYTES    8192                           /* up to 1024 frames */
#define IN_BYTES       8192                           /* MP3 stream buffer */
#define PCM_SAMPLES    (MAX_NCHAN * MAX_NGRAN * MAX_NSAMP + 8)

typedef struct {
    uint32_t fps_num, fps_den, nframes, index_off, max_frame, mp3_off, mp3_size, sample_rate;
} clip_t;
typedef struct { uint32_t off, size; } frame_t;

static const esp_partition_t *part;
static clip_t clips[8];
static int nclips = -1;

static int media_open(void)
{
    if (nclips >= 0) return nclips;
    nclips = 0;
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "media");
    if (!part) { ESP_LOGW(TAG, "no media partition"); return 0; }
    uint32_t hdr[4];
    if (esp_partition_read(part, 0, hdr, sizeof(hdr)) != ESP_OK) return 0;
    if (memcmp(hdr, "MDLA", 4) != 0 || hdr[1] != 1 || hdr[2] == 0 || hdr[2] > 8) {
        ESP_LOGW(TAG, "no media image at 0x%lx (header %08lx)", (unsigned long)part->address, (unsigned long)hdr[0]);
        return 0;
    }
    if (esp_partition_read(part, 16, clips, hdr[2] * sizeof(clip_t)) != ESP_OK) return 0;
    nclips = (int)hdr[2];
    for (int i = 0; i < nclips; i++)
        ESP_LOGI(TAG, "clip %d: %lu frames, max %lu B, mp3 %lu B @ %lu Hz", i, (unsigned long)clips[i].nframes,
                 (unsigned long)clips[i].max_frame, (unsigned long)clips[i].mp3_size, (unsigned long)clips[i].sample_rate);
    return nclips;
}

int egg_clip_count(void) { return media_open(); }

/* ---- player state (one clip at a time) ---- */
typedef struct {
    const clip_t *clip;
    /* scratch */
    int16_t *ring; uint16_t *strip; uint8_t *pool, *jpeg, *in_buf; frame_t *index; int16_t *pcm;
    /* video */
    int strip_top, strip_rows;
    /* mp3 */
    HMP3Decoder dec; uint32_t mp3_pos, mp3_end; int filled; uint8_t *rd; int mp3_done;
    int16_t carry; uint32_t rs_pos;                   /* resampler: last sample, 16.16 position */
    int abort;
    SemaphoreHandle_t done;
    int result;
} player_t;
static player_t P;

/* ---- audio: decode one MP3 frame and push it to the mixer ring ---- */
static void mp3_step(void)
{
    if (P.mp3_done) return;
    for (;;) {
        if (P.filled < 2048 && P.mp3_pos < P.mp3_end) {
            memmove(P.in_buf, P.rd, P.filled);
            P.rd = P.in_buf;
            size_t want = IN_BYTES - P.filled;
            if (want > P.mp3_end - P.mp3_pos) want = P.mp3_end - P.mp3_pos;
            if (esp_partition_read(part, P.mp3_pos, P.in_buf + P.filled, want) != ESP_OK) { P.mp3_done = 1; return; }
            P.mp3_pos += want;
            P.filled += want;
        }
        if (P.filled <= 0) { P.mp3_done = 1; return; }
        int sync = MP3FindSyncWord(P.rd, P.filled);
        if (sync < 0) { P.filled = 0; if (P.mp3_pos >= P.mp3_end) { P.mp3_done = 1; return; } continue; }
        P.rd += sync; P.filled -= sync;
        int left = P.filled;
        int16_t *out = P.pcm + 1;                                  /* pcm[0] holds the resampler carry */
        int err = MP3Decode(P.dec, &P.rd, &left, out, 0);
        if (err) {
            if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
                if (P.mp3_pos >= P.mp3_end) { P.mp3_done = 1; return; }
                P.filled = left;
                continue;
            }
            P.rd++; P.filled--;                                    /* bad frame: resync */
            continue;
        }
        P.filled = left;
        MP3FrameInfo info;
        MP3GetLastFrameInfo(P.dec, &info);
        int n = info.outputSamps;
        if (n <= 0) continue;
        if (info.nChans == 2) { n /= 2; for (int i = 0; i < n; i++) out[i] = (int16_t)(((int)out[2 * i] + out[2 * i + 1]) / 2); }
        /* resample info.samprate -> AUDIO_SAMPLE_RATE with linear interpolation; pcm[0] is the previous last sample */
        P.pcm[0] = P.carry;
        uint32_t step = (uint32_t)(((uint64_t)info.samprate << 16) / AUDIO_SAMPLE_RATE);
        int16_t tmp[64]; int t = 0;
        while ((P.rs_pos >> 16) < (uint32_t)n) {
            uint32_t i = P.rs_pos >> 16, f = P.rs_pos & 0xffff;
            int32_t a = P.pcm[i], b = P.pcm[i + 1];
            tmp[t++] = (int16_t)(a + (((b - a) * (int32_t)f) >> 16));
            if (t == 64) { audio_stream_push(tmp, t); t = 0; }
            P.rs_pos += step;
        }
        if (t) audio_stream_push(tmp, t);
        P.rs_pos -= (uint32_t)n << 16;
        P.carry = out[n - 1];
        return;
    }
}

/* ---- video ---- */
typedef struct { uint32_t pos, size; } src_t;
static src_t jsrc;

static unsigned int in_func(JDEC *jd, uint8_t *buf, unsigned int n)
{
    (void)jd;
    if (jsrc.pos + n > jsrc.size) n = jsrc.size - jsrc.pos;
    if (buf) memcpy(buf, P.jpeg + jsrc.pos, n);
    jsrc.pos += n;
    return n;
}

static void flush_strip(void)
{
    if (P.strip_rows <= 0) return;
    display_set_window(0, (uint16_t)(VIDEO_Y + P.strip_top), VIDEO_W, (uint16_t)P.strip_rows);
    int half = P.strip_rows > STRIP_ROWS / 2 ? STRIP_ROWS / 2 : P.strip_rows;
    display_write_preswapped(P.strip, (uint32_t)(VIDEO_W * half));
    if (P.strip_rows > half) display_write_preswapped(P.strip + VIDEO_W * half, (uint32_t)(VIDEO_W * (P.strip_rows - half)));
    P.strip_rows = 0;
    audio_update();                                                /* keep the DAC queue topped up mid-frame */
}

static unsigned int out_func(JDEC *jd, void *bitmap, JRECT *rect)
{
    (void)jd;
    if (rect->top != P.strip_top || P.strip_rows == 0) {
        flush_strip();
        P.strip_top = rect->top;
        P.strip_rows = rect->bottom - rect->top + 1;
        if (P.strip_top + P.strip_rows > VIDEO_H) P.strip_rows = VIDEO_H - P.strip_top;
        if (P.strip_rows > STRIP_ROWS) P.strip_rows = STRIP_ROWS;
    }
    const uint8_t *src = (const uint8_t *)bitmap;
    const int bw = rect->right - rect->left + 1;
    for (int y = rect->top; y <= rect->bottom && y < P.strip_top + P.strip_rows; y++) {
        const uint8_t *row = src + (y - rect->top) * bw * 3;
        uint16_t *dst = P.strip + (y - P.strip_top) * VIDEO_W + rect->left;
        for (int x = rect->left; x <= rect->right && x < VIDEO_W; x++, row += 3) {
            uint16_t px = (uint16_t)(((row[0] & 0xF8) << 8) | ((row[1] & 0xFC) << 3) | (row[2] >> 3));
            *dst++ = (uint16_t)((px >> 8) | (px << 8));
        }
    }
    return 1;
}

static int show_frame(uint32_t i)
{
    frame_t f = P.index[i];
    if (f.size > JPEG_BYTES || esp_partition_read(part, f.off, P.jpeg, f.size) != ESP_OK) return -1;
    JDEC jd;
    jsrc.pos = 0; jsrc.size = f.size;
    if (jd_prepare(&jd, in_func, P.pool, POOL_BYTES, nullptr) != JDR_OK) return -1;
    if (jd.width != VIDEO_W || jd.height != VIDEO_H) { ESP_LOGE(TAG, "frame is %ux%u", jd.width, jd.height); return -1; }
    P.strip_rows = 0;
    if (jd_decomp(&jd, out_func, 0) != JDR_OK) return -1;
    flush_strip();
    return 0;
}

static void player_task(void *arg)
{
    (void)arg;
    const clip_t *c = P.clip;
    const int64_t frame_us = (int64_t)1000000 * c->fps_den / c->fps_num;
    uint32_t shown = 0, skipped = 0;
    int boot_released = 0;

    /* prime the audio so the first frames have sound, then start the clock */
    for (int k = 0; k < 4 && !P.mp3_done; k++) mp3_step();
    const int64_t t0 = esp_timer_get_time();
    uint32_t i = 0;
    for (;;) {
        bool boot = gpio_get_level(PIN_BTN_BOOT) == 0;
        if (!boot) boot_released = 1;
        else if (boot_released) { P.abort = 1; break; }             /* a fresh press ends the show */

        while (!P.mp3_done && audio_stream_space() >= 800) mp3_step();
        audio_update();

        if (i < c->nframes) {
            int64_t target = (int64_t)i * frame_us;
            int64_t now = esp_timer_get_time() - t0;
            if (now > target + MAX_BEHIND_US) { skipped++; i++; continue; }
            if (now < target - 3000) { vTaskDelay(1); continue; }
            if (show_frame(i) != 0) { P.result = -1; break; }
            shown++; i++;
            continue;
        }
        if (P.mp3_done && audio_stream_queued() == 0) break;      /* video over, audio drained */
        vTaskDelay(1);
    }
    int64_t ms = (esp_timer_get_time() - t0) / 1000;
    ESP_LOGI(TAG, "clip done: %lu shown, %lu skipped, %lld ms (%.1f fps)%s", (unsigned long)shown, (unsigned long)skipped,
             (long long)ms, ms ? shown * 1000.0 / ms : 0.0, P.abort ? ", aborted" : "");
    xSemaphoreGive(P.done);
    vTaskDelete(nullptr);
}

int egg_play(int index)
{
    if (media_open() <= 0 || index < 0 || index >= nclips) return -1;
    const clip_t *c = &clips[index];
    if (c->max_frame > JPEG_BYTES || c->nframes * sizeof(frame_t) > INDEX_BYTES) { ESP_LOGE(TAG, "clip %d too big for the scratch buffers", index); return -1; }

    size_t scratch_size;
    uint8_t *scratch = render_scratch(&scratch_size);
    size_t need = RING_SAMPLES * 2 + VIDEO_W * STRIP_ROWS * 2 + POOL_BYTES + JPEG_BYTES + INDEX_BYTES + IN_BYTES + PCM_SAMPLES * 2;
    if (!scratch || scratch_size < need) { ESP_LOGE(TAG, "scratch %u < %u", (unsigned)scratch_size, (unsigned)need); return -1; }

    memset(&P, 0, sizeof(P));
    P.clip = c;
    uint8_t *p = scratch;
    P.ring = (int16_t *)p;   p += RING_SAMPLES * 2;
    P.strip = (uint16_t *)p; p += VIDEO_W * STRIP_ROWS * 2;
    P.pool = p;              p += POOL_BYTES;
    P.jpeg = p;              p += JPEG_BYTES;
    P.index = (frame_t *)p;  p += INDEX_BYTES;
    P.in_buf = p;            p += IN_BYTES;
    P.pcm = (int16_t *)p;
    if (esp_partition_read(part, c->index_off, P.index, c->nframes * sizeof(frame_t)) != ESP_OK) return -1;

    P.dec = MP3InitDecoder();
    if (!P.dec) { ESP_LOGE(TAG, "MP3 decoder init failed (heap %lu)", (unsigned long)esp_get_free_heap_size()); return -1; }
    P.mp3_pos = c->mp3_off; P.mp3_end = c->mp3_off + c->mp3_size; P.rd = P.in_buf;
    P.done = xSemaphoreCreateBinary();

    display_fill(0x0000);
    audio_stream_begin(P.ring, RING_SAMPLES);
    ESP_LOGI(TAG, "playing clip %d, heap %lu", index, (unsigned long)esp_get_free_heap_size());
    if (xTaskCreate(player_task, "egg", 12288, nullptr, 5, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "player task failed");
        P.result = -1;
    } else {
        xSemaphoreTake(P.done, portMAX_DELAY);
    }
    audio_stream_end();
    MP3FreeDecoder(P.dec);
    vSemaphoreDelete(P.done);
    display_wait_done();                     /* the last strip may still be on the DMA */
    display_fill(0x0000);
    return P.result;
}
