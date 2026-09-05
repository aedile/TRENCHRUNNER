#pragma once
#include <stdint.h>
#include "avg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Rendering runs in its own FreeRTOS task so the panel's DMA transfer overlaps
 * with emulation. The emulator hands over completed vector lists; if the
 * renderer is busy the frame is dropped (the game keeps running in real time). */
void render_init(void);
void render_submit(const avg_point_t *points, int npoints);   /* called from the emulator task */
uint32_t render_frames_drawn(void);
uint32_t render_frames_dropped(void);
uint64_t render_busy_us(void);                                /* CPU time spent rasterizing/converting */

#ifdef __cplusplus
}
#endif
