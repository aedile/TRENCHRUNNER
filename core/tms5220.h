/*
 * tms5220.h - TMS5220 speech synthesizer (Speak External mode, as used by Star Wars).
 * LPC-10 synthesis following MAME's tms5220.cpp (BSD-3-Clause) and the TI patents;
 * coefficient tables from MAME's tms5110r.hxx.
 */
#ifndef TMS5220_H
#define TMS5220_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TMS_FIFO_SIZE 16
#define TMS_CLOCK_HZ 672000            /* Star Wars: 12.096 MHz / 2 / 9 */
#define TMS_SAMPLE_RATE (TMS_CLOCK_HZ / 80)

typedef struct {
    /* host interface */
    uint8_t fifo[TMS_FIFO_SIZE];
    int fifo_head, fifo_tail, fifo_count, fifo_bits_taken;
    int wsq, rsq;
    uint8_t data_in;
    int SPEN, TALK, TALKD, DDIS;
    int buffer_low, buffer_empty, previous_talk_status;
    /* frame parameters */
    int new_energy_idx, new_pitch_idx, new_k_idx[10];
    int current_energy, current_pitch, current_k[10], previous_energy;
    int zpar, uv_zpar, OLDE, OLDP, inhibit;
    /* generator */
    int IP, PC, subcycle, pitch_count, pitch_zero;
    uint32_t RNG;
    int excitation;
    int32_t u[11], x[10];
    int16_t last_sample;
    /* synthesis runs in emulated time (one sample per 180 CPU cycles) into a
     * ring buffer; render drains it at the mixer rate */
    uint32_t cycle_acc;
    int16_t ring[2048];
    unsigned ring_w, ring_r;
    uint32_t resample_acc;
    int16_t prev_out;
    /* diagnostics */
    uint32_t commands, bytes_in, frames, talk_starts;
} tms5220_t;

void tms5220_init(tms5220_t *t);
void tms5220_reset(tms5220_t *t);
void tms5220_wsq(tms5220_t *t, int level);         /* /WS: write strobe (data or command) */
void tms5220_rsq(tms5220_t *t, int level);         /* /RS: read strobe */
void tms5220_data_write(tms5220_t *t, uint8_t d);  /* value on the data bus */
uint8_t tms5220_status(tms5220_t *t);
int tms5220_readyq(const tms5220_t *t);            /* /READY level: 0 = ready */
void tms5220_tick(tms5220_t *t, unsigned cpu_cycles);   /* advances synthesis by CPU time */
void tms5220_render(tms5220_t *t, int16_t *buf, int samples, int sample_rate);  /* mixes into buf */

#ifdef __cplusplus
}
#endif

#endif
