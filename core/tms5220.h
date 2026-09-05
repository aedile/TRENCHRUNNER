/*
 * tms5220.h - TMS5220 speech synthesizer interface (host-side FIFO/status model).
 * Speech synthesis itself is a later step; for now the chip accepts Speak External
 * data, drains it at speech rate and reports status so the sound program runs.
 */
#ifndef TMS5220_H
#define TMS5220_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t fifo[16];
    int fifo_count, fifo_head;
    int talking;
    int speak_external;
    int wsq, rsq;          /* control line states (active low) */
    uint8_t data_in;
    uint32_t drain_acc;    /* cycles accumulator for FIFO draining */
    uint32_t bytes_spoken;
    uint32_t commands;
} tms5220_t;

void tms5220_init(tms5220_t *t);
void tms5220_reset(tms5220_t *t);
void tms5220_wsq(tms5220_t *t, int level);      /* /WS: write strobe */
void tms5220_rsq(tms5220_t *t, int level);      /* /RS: read strobe */
void tms5220_data_write(tms5220_t *t, uint8_t d); /* data bus while /WS low */
uint8_t tms5220_status(tms5220_t *t);
int tms5220_readyq(const tms5220_t *t);         /* /READY level: 0 = ready */
void tms5220_tick(tms5220_t *t, unsigned cpu_cycles);
void tms5220_render(tms5220_t *t, int16_t *buf, int samples, int sample_rate);

#ifdef __cplusplus
}
#endif

#endif
