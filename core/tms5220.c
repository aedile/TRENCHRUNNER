/*
 * tms5220.c - TMS5220 FIFO/status model (no synthesis yet), see tms5220.h
 */
#include "tms5220.h"
#include <string.h>

/* Speak External data rate: LPC frames are 25 ms and 4..50 bits; ~1.4 kbit/s
 * average, i.e. one FIFO byte roughly every 5.7 ms = 8600 CPU cycles at 1.512 MHz */
#define DRAIN_CYCLES 8600

void tms5220_init(tms5220_t *t) { tms5220_reset(t); }

void tms5220_reset(tms5220_t *t)
{
    memset(t, 0, sizeof(*t));
    t->wsq = 1;
    t->rsq = 1;
}

static void fifo_push(tms5220_t *t, uint8_t d)
{
    if (t->fifo_count < 16) {
        t->fifo[(t->fifo_head + t->fifo_count) & 15] = d;
        t->fifo_count++;
    }
    /* talking starts once the FIFO has filled past the buffer-low mark */
    if (t->speak_external && !t->talking && t->fifo_count >= 9) t->talking = 1;
}

static void command(tms5220_t *t, uint8_t d)
{
    t->commands++;
    switch (d & 0x70) {
        case 0x60:                     /* Speak External */
            t->speak_external = 1;
            t->fifo_count = 0; t->fifo_head = 0;
            break;
        case 0x70:                     /* Reset */
            t->speak_external = 0; t->talking = 0;
            t->fifo_count = 0; t->fifo_head = 0;
            break;
        default:                       /* Speak/Load Address/Read: no VSM on this board */
            break;
    }
}

void tms5220_data_write(tms5220_t *t, uint8_t d)
{
    t->data_in = d;
}

void tms5220_wsq(tms5220_t *t, int level)
{
    /* data is latched on the falling edge of /WS */
    if (t->wsq && !level) {
        if (t->speak_external) fifo_push(t, t->data_in);
        else command(t, t->data_in);
    }
    t->wsq = level;
}

void tms5220_rsq(tms5220_t *t, int level) { t->rsq = level; }

uint8_t tms5220_status(tms5220_t *t)
{
    uint8_t s = 0;
    if (t->talking) s |= 0x80;             /* TS  */
    if (t->fifo_count < 8) s |= 0x40;      /* BL  */
    if (t->fifo_count == 0) s |= 0x20;     /* BE  */
    return s;
}

int tms5220_readyq(const tms5220_t *t)
{
    return (t->fifo_count >= 16) ? 1 : 0;  /* /READY high (not ready) only when the FIFO is full */
}

void tms5220_tick(tms5220_t *t, unsigned cpu_cycles)
{
    if (!t->talking) return;
    t->drain_acc += cpu_cycles;
    while (t->drain_acc >= DRAIN_CYCLES) {
        t->drain_acc -= DRAIN_CYCLES;
        if (t->fifo_count > 0) {
            t->fifo_head = (t->fifo_head + 1) & 15;
            t->fifo_count--;
            t->bytes_spoken++;
        } else {
            t->talking = 0;                /* ran dry: end of speech */
            t->speak_external = 0;
        }
    }
}

void tms5220_render(tms5220_t *t, int16_t *buf, int samples, int sample_rate)
{
    (void)t; (void)buf; (void)samples; (void)sample_rate;   /* synthesis: later */
}
